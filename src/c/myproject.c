#include <pebble.h>

#define TICK_MINOR 6
#define TICK_MAJOR 12
#define NUMERAL_INSET 16
#define HAND_MARGIN_MINUTE 14
#define HAND_MARGIN_SECOND 8
// The hour hand as a share of the minute hand, the usual analog proportion.
#define HOUR_HAND_PERCENT 62
#define HAND_TAIL 20

#define DATE_PADDING_X 7
#define DATE_PADDING_Y 2
// Pebble's system fonts render their glyphs flush to the bottom of the line
// box, so centering on the measured content size leaves the text low. Measured
// on screen for Gothic 24 Bold: 12px of slack above, 2px below.
#define DATE_TEXT_LIFT 5

// Half a second of bounce per second of clock was the face's biggest running
// cost: it renders at the full frame rate for as long as it lasts. A quarter
// second keeps the sweep and halves the frames.
#define SECOND_ANIM_DURATION 250
#define MINUTE_ANIM_DURATION 500
#define SECOND_FADE_DURATION 350
#define INTRO_DURATION 900

// The second hand is the only expensive thing on this face: drawing it means a
// SECOND_UNIT tick plus a 30fps bounce every second, where the rest of the dial
// only needs waking once a minute. The mode chooses when to pay for it.
typedef enum {
  SecondHandModeAlways = 0,
  SecondHandModeBacklight = 1,
  SecondHandModeNever = 2,
} SecondHandMode;

#define SECOND_MODE_DEFAULT SecondHandModeAlways
#define PERSIST_KEY_SECOND_MODE 1

static Window *s_window;
static Layer *s_face_layer;
static char s_date_buffer[12];

// The dial -- ticks, numerals and the date window -- is the same picture from
// one frame to the next, and re-rendering it was most of the cost of a frame:
// 60 ticks at a 5px and 2px stroke, and thick lines are expensive to draw. So
// it is rendered once into a bitmap and blitted from then on, and only
// re-rendered when the date changes or the layer is resized. That also takes
// the per-frame text measurement and the 120 dial-point computations off the
// hot path, since they only run on a re-render.
static GBitmap *s_dial_cache;
static bool s_dial_valid;

// Dial geometry, recomputed from the layer bounds on each draw.
static GPoint s_center;
static int16_t s_rx, s_ry;

// Hand angles are driven by the bounce animation rather than read straight off
// the clock. Each is offset by a full turn so it never goes negative mid-bounce.
static int32_t s_second_from, s_second_to, s_second_angle;
static int32_t s_minute_from, s_minute_to, s_minute_angle;
static int32_t s_hour_from, s_hour_to, s_hour_angle;
static Animation *s_second_anim;
// The minute and hour hands step together once a minute, so one animation
// drives both.
static Animation *s_minute_anim;

// The sweep the hands make when the face loads: they run up from 12 to the
// current time instead of appearing already in place. It drives all three
// angles, so nothing else may move a hand while it is running.
static Animation *s_intro_anim;
static bool s_intro_running;

// Whether the second hand should be on screen. In backlight mode this follows
// the backlight; in the other two modes it is fixed.
static bool s_second_visible = true;
// How far the hand has faded in: 0 is fully out (not drawn), _MAX fully in.
// The hand is only really gone, and per-second ticking only really stops, once
// this reaches 0.
static int32_t s_second_fade = 0;
static int32_t s_fade_from, s_fade_to;
static Animation *s_fade_anim;
// Tri-state: -1 until the first subscription, so the initial mode always
// subscribes whichever rate it needs.
static int8_t s_ticking_per_second = -1;

static const char *const s_numerals[] = {
  "12", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11"
};

// A point on the dial at `angle`, `inset` pixels in from the edge of the
// display. On a rectangular screen the dial edge is the screen rectangle
// itself, corners included, so the face is full bleed; on a round screen the
// corners are off-display, so the edge is the inscribed circle.
static int16_t prv_min_radius(void) {
  return s_rx < s_ry ? s_rx : s_ry;
}

static GPoint prv_dial_point(int16_t inset, int32_t angle) {
  const int32_t dx = sin_lookup(angle);
  const int32_t dy = -cos_lookup(angle);
  int64_t reach;

#if PBL_ROUND
  reach = s_rx - inset;
#else
  // Distance along the ray to the inset rectangle: whichever edge it meets
  // first, horizontal or vertical.
  const int64_t half_w = s_rx - inset;
  const int64_t half_h = s_ry - inset;
  const int64_t adx = dx < 0 ? -dx : dx;
  const int64_t ady = dy < 0 ? -dy : dy;

  if (adx == 0) {
    reach = half_h;
  } else if (ady == 0) {
    reach = half_w;
  } else {
    const int64_t to_side = half_w * TRIG_MAX_RATIO / adx;
    const int64_t to_top = half_h * TRIG_MAX_RATIO / ady;
    reach = (to_side < to_top) ? to_side : to_top;
  }
#endif

  return (GPoint) {
    .x = s_center.x + (int16_t)(dx * reach / TRIG_MAX_RATIO),
    .y = s_center.y + (int16_t)(dy * reach / TRIG_MAX_RATIO),
  };
}

static void prv_draw_ticks(GContext *ctx) {
  for (int i = 0; i < 60; i++) {
    const bool major = (i % 5 == 0);
    const int32_t angle = TRIG_MAX_ANGLE * i / 60;
    const int16_t length = major ? TICK_MAJOR : TICK_MINOR;

    graphics_context_set_stroke_color(ctx, major ? PBL_IF_COLOR_ELSE(GColorLightGray, GColorWhite)
                                                 : PBL_IF_COLOR_ELSE(GColorDarkGray, GColorWhite));
    graphics_context_set_stroke_width(ctx, major ? 5 : 2);
    graphics_draw_line(ctx, prv_dial_point(length, angle), prv_dial_point(0, angle));
  }
}

static void prv_draw_numerals(GContext *ctx) {
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  graphics_context_set_text_color(ctx, GColorWhite);

  for (int i = 0; i < 12; i++) {
    if (i == 3) {
      continue;  // the date aperture sits here
    }
    const int32_t angle = TRIG_MAX_ANGLE * i / 12;
    const GPoint p = prv_dial_point(NUMERAL_INSET + TICK_MAJOR, angle);
    // Gothic 24 sits low in its line box, hence the extra lift on y.
    const GRect box = GRect(p.x - 20, p.y - 19, 40, 30);
    graphics_draw_text(ctx, s_numerals[i], font, box, GTextOverflowModeWordWrap,
                       GTextAlignmentCenter, NULL);
  }
}

// A day-date aperture in place of the 3 numeral, the way an analog watch cuts
// one into the dial. Pinned near the right edge so the wider window still
// fits. Drawn before the hands, so they sweep over it.
static void prv_draw_date(GContext *ctx) {
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);

  // Measure the text and build the window around it, so the day-date is
  // centered by construction and the window always fits the longest weekday.
  const GSize text = graphics_text_layout_get_content_size(
      s_date_buffer, font, GRect(0, 0, 120, 40), GTextOverflowModeWordWrap,
      GTextAlignmentCenter);
  const int16_t w = text.w + DATE_PADDING_X * 2;
  const int16_t h = text.h + DATE_PADDING_Y * 2;
  const GRect box = GRect(s_center.x + s_rx - 4 - w, s_center.y - h / 2, w, h);

  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, box, 3, GCornersAll);
  graphics_context_set_stroke_color(ctx, PBL_IF_COLOR_ELSE(GColorDarkGray, GColorWhite));
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_round_rect(ctx, box, 3);

  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, s_date_buffer, font,
                     GRect(box.origin.x, box.origin.y + DATE_PADDING_Y - DATE_TEXT_LIFT,
                           w, text.h),
                     GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

// Hand lengths are measured from the center, not in from the dial edge: the
// dial edge is the screen rectangle, so a hand inset from it would stretch
// toward the corners and shrink toward the sides as it went round. Every hand
// sweeps a true circle instead, keeping one length all the way round.
static int16_t prv_minute_radius(void) {
  return prv_min_radius() - HAND_MARGIN_MINUTE;
}

static int16_t prv_hour_radius(void) {
  return prv_minute_radius() * HOUR_HAND_PERCENT / 100;
}

static int16_t prv_second_radius(void) {
  return prv_min_radius() - HAND_MARGIN_SECOND;
}

// Draws a hand from a short tail behind the center out to `radius` pixels from
// it.
static void prv_draw_hand(GContext *ctx, int32_t angle, int16_t radius, uint8_t width,
                          GColor color) {
  const GPoint tip = {
    .x = s_center.x + (int16_t)(sin_lookup(angle) * radius / TRIG_MAX_RATIO),
    .y = s_center.y - (int16_t)(cos_lookup(angle) * radius / TRIG_MAX_RATIO),
  };
  const GPoint tail = {
    .x = s_center.x - (int16_t)(sin_lookup(angle) * HAND_TAIL / TRIG_MAX_RATIO),
    .y = s_center.y + (int16_t)(cos_lookup(angle) * HAND_TAIL / TRIG_MAX_RATIO),
  };

  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, width);
  graphics_draw_line(ctx, tail, tip);
}

// Line drawing treats the stroke alpha as all-or-nothing, so the hand cannot be
// blended over the dial. It is faded by ramping its colour toward the black
// background instead: four steps per channel on a 64-colour display, which over
// a third of a second reads as a dissolve.
static GColor prv_second_color(void) {
  GColor color = PBL_IF_COLOR_ELSE(GColorRed, GColorWhite);
#if PBL_COLOR
  // Rounded, so the ramp lands squarely on the endpoints rather than lingering
  // one step above black.
  const int32_t half = ANIMATION_NORMALIZED_MAX / 2;
  color.r = (color.r * s_second_fade + half) / ANIMATION_NORMALIZED_MAX;
  color.g = (color.g * s_second_fade + half) / ANIMATION_NORMALIZED_MAX;
  color.b = (color.b * s_second_fade + half) / ANIMATION_NORMALIZED_MAX;
#endif
  return color;
}

static void prv_draw_dial(GContext *ctx, const GRect bounds) {
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  prv_draw_ticks(ctx);
  prv_draw_numerals(ctx);
  prv_draw_date(ctx);
}

static bool prv_dial_cache_fits(const GRect bounds) {
  if (!s_dial_cache) {
    return false;
  }
  const GSize size = gbitmap_get_bounds(s_dial_cache).size;
  return size.w == bounds.size.w && size.h == bounds.size.h;
}

// Copies the dial that was just drawn out of the framebuffer and into the
// cache. Row by row through the row info rather than one memcpy of the whole
// buffer, because a round display stores each row at its own offset and only
// the on-screen span of it is addressable.
static void prv_capture_dial(GContext *ctx, const GRect bounds) {
  if (!s_dial_cache) {
    s_dial_cache = gbitmap_create_blank(bounds.size,
                                        PBL_IF_COLOR_ELSE(GBitmapFormat8Bit, GBitmapFormat1Bit));
    if (!s_dial_cache) {
      return;  // No room for it: fall back to drawing the dial every frame.
    }
  }

  GBitmap *fb = graphics_capture_frame_buffer(ctx);
  if (!fb) {
    return;
  }
  for (int y = 0; y < bounds.size.h; y++) {
    const GBitmapDataRowInfo src = gbitmap_get_data_row_info(fb, y);
    const GBitmapDataRowInfo dst = gbitmap_get_data_row_info(s_dial_cache, y);
    const int16_t min_x = src.min_x > dst.min_x ? src.min_x : dst.min_x;
    const int16_t max_x = src.max_x < dst.max_x ? src.max_x : dst.max_x;
    if (max_x < min_x) {
      continue;
    }
#if PBL_COLOR
    memcpy(dst.data + min_x, src.data + min_x, max_x - min_x + 1);
#else
    // One byte to the pixel on colour, eight pixels to the byte here.
    const int16_t first = min_x / 8;
    const int16_t last = max_x / 8;
    memcpy(dst.data + first, src.data + first, last - first + 1);
#endif
  }
  graphics_release_frame_buffer(ctx, fb);
  s_dial_valid = true;
}

static int s_dbg_frames, s_dbg_busy_us;
static time_t s_dbg_win;

static uint32_t prv_dbg_now_us(void) {
  time_t s; uint16_t ms; time_ms(&s, &ms);
  return (uint32_t)(s % 60) * 1000000 + (uint32_t)ms * 1000;
}

static void prv_face_update_proc(Layer *layer, GContext *ctx) {
  const uint32_t dbg_t0 = prv_dbg_now_us();
  const GRect bounds = layer_get_bounds(layer);
  s_center = grect_center_point(&bounds);
  s_rx = bounds.size.w / 2 - 1;
  s_ry = bounds.size.h / 2 - 1;

  graphics_context_set_antialiased(ctx, true);

  // The face layer is the whole window, so cache rows and framebuffer rows are
  // the same rows.
  if (!prv_dial_cache_fits(bounds)) {
    if (s_dial_cache) {
      gbitmap_destroy(s_dial_cache);
      s_dial_cache = NULL;
    }
    s_dial_valid = false;
  }

  if (s_dial_valid) {
    graphics_context_set_compositing_mode(ctx, GCompOpAssign);
    graphics_draw_bitmap_in_rect(ctx, s_dial_cache, bounds);
  } else {
    prv_draw_dial(ctx, bounds);
    prv_capture_dial(ctx, bounds);
  }
  prv_draw_hand(ctx, s_hour_angle, prv_hour_radius(), 9, GColorWhite);
  prv_draw_hand(ctx, s_minute_angle, prv_minute_radius(), 7, GColorWhite);
  // The bottom of the ramp rounds to the dial's own black. Drawing that would
  // be invisible against the dial but a solid black stroke across the date
  // window and the numerals, so it is skipped instead.
  const GColor second_color = prv_second_color();
  if (s_second_fade > 0 && !gcolor_equal(second_color, GColorBlack)) {
    prv_draw_hand(ctx, s_second_angle, prv_second_radius(), 2, second_color);
  }

  // Center cap
  graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(GColorRed, GColorWhite));
  graphics_fill_circle(ctx, s_center, 6);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_circle(ctx, s_center, 2);

  s_dbg_frames++;
  s_dbg_busy_us += (int)(prv_dbg_now_us() - dbg_t0);
  time_t dbg_now = time(NULL);
  if (s_dbg_win == 0) { s_dbg_win = dbg_now; }
  if (dbg_now - s_dbg_win >= 5) {
    APP_LOG(APP_LOG_LEVEL_INFO, "DBG frames=%d over %ds busy_ms=%d",
            s_dbg_frames, (int)(dbg_now - s_dbg_win), s_dbg_busy_us / 1000);
    s_dbg_frames = 0; s_dbg_busy_us = 0; s_dbg_win = dbg_now;
  }
}

// The bounce, in two stages. The animation service runs at 30fps, so a
// one-second step gets only ~15 frames: a curve that spends most of its travel
// in one or two of them looks like a jump, not a sweep. So the linear input is
// first smoothstepped (easing both ends, keeping any single frame small), then
// fed through an easeOutBack that carries the hand a little past the mark and
// settles it monotonically.
//   u = t^2 * (3 - 2t)
//   f = 1 + 4*(u - 1)^3 + 3*(u - 1)^2
// f exceeds ANIMATION_NORMALIZED_MAX mid-flight, never drops below zero, and
// lands exactly on target.
static AnimationProgress prv_bounce_curve(AnimationProgress linear) {
  const int64_t max = ANIMATION_NORMALIZED_MAX;
  const int64_t t = linear;

  const int64_t u = t * t / max * (3 * max - 2 * t) / max;

  const int64_t d = u - max;
  const int64_t d2 = d * d / max;
  const int64_t d3 = d2 * d / max;
  const int64_t f = max + 4 * d3 + 3 * d2;
  return (AnimationProgress)(f < 0 ? 0 : f);
}

static int32_t prv_lerp(int32_t from, int32_t to, AnimationProgress progress) {
  return from + (int32_t)((int64_t)(to - from) * progress / ANIMATION_NORMALIZED_MAX);
}

static void prv_second_update(Animation *anim, const AnimationProgress progress) {
  s_second_angle = prv_lerp(s_second_from, s_second_to, progress);
  layer_mark_dirty(s_face_layer);
}

// Minute and hour move off the same progress, so the hour hand creeps forward
// in step with the minute hand instead of snapping a moment later.
static void prv_minute_update(Animation *anim, const AnimationProgress progress) {
  s_minute_angle = prv_lerp(s_minute_from, s_minute_to, progress);
  s_hour_angle = prv_lerp(s_hour_from, s_hour_to, progress);
  layer_mark_dirty(s_face_layer);
}

// The sweep moves all three hands off one progress, from 12 to the time they
// settle on.
static void prv_intro_update(Animation *anim, const AnimationProgress progress) {
  s_second_angle = prv_lerp(s_second_from, s_second_to, progress);
  s_minute_angle = prv_lerp(s_minute_from, s_minute_to, progress);
  s_hour_angle = prv_lerp(s_hour_from, s_hour_to, progress);
  layer_mark_dirty(s_face_layer);
}

static void prv_intro_stopped(Animation *anim, bool finished, void *context) {
  s_intro_running = false;
}

static const AnimationImplementation s_second_impl = {
  .update = prv_second_update,
};

static const AnimationImplementation s_intro_impl = {
  .update = prv_intro_update,
};

static const AnimationHandlers s_intro_handlers = {
  .stopped = prv_intro_stopped,
};

// Defined below, with the tick handler it subscribes.
static void prv_sync_tick_rate(void);

static void prv_fade_update(Animation *anim, const AnimationProgress progress) {
  s_second_fade = prv_lerp(s_fade_from, s_fade_to, progress);
  // The tick rate is part of the fade: per-second wakes stop only once the hand
  // has finished fading out, and resume the moment it starts fading in.
  prv_sync_tick_rate();
  layer_mark_dirty(s_face_layer);
}

static const AnimationImplementation s_minute_impl = {
  .update = prv_minute_update,
};

static const AnimationImplementation s_fade_impl = {
  .update = prv_fade_update,
};

// Replaces whatever is in `slot`, so a tick arriving mid-bounce restarts that
// hand cleanly rather than leaking the old animation. `custom` wins over
// `curve` when given; the fade and the sweep want stock curves, since the
// bounce would overshoot past fully opaque and back below transparent.
static void prv_schedule(Animation **slot, const AnimationImplementation *impl,
                         uint32_t duration, AnimationCurve curve,
                         AnimationCurveFunction custom, const AnimationHandlers *handlers) {
  if (*slot) {
    animation_unschedule(*slot);
    animation_destroy(*slot);
  }
  *slot = animation_create();
  animation_set_duration(*slot, duration);
  if (custom) {
    animation_set_custom_curve(*slot, custom);
  } else {
    animation_set_curve(*slot, curve);
  }
  if (handlers) {
    animation_set_handlers(*slot, *handlers, NULL);
  }
  animation_set_implementation(*slot, impl);
  animation_schedule(*slot);
}

static void prv_animate_second(int seconds) {
  // The extra full turn keeps every angle positive across the 59 -> 0 wrap.
  s_second_to = TRIG_MAX_ANGLE + TRIG_MAX_ANGLE * seconds / 60;
  s_second_from = s_second_to - TRIG_MAX_ANGLE / 60;

  prv_schedule(&s_second_anim, &s_second_impl, SECOND_ANIM_DURATION,
               AnimationCurveEaseInOut, prv_bounce_curve, NULL);
}

static void prv_animate_minute(int hours, int minutes) {
  s_minute_to = TRIG_MAX_ANGLE + TRIG_MAX_ANGLE * minutes / 60;
  s_minute_from = s_minute_to - TRIG_MAX_ANGLE / 60;

  s_hour_to = TRIG_MAX_ANGLE + TRIG_MAX_ANGLE * ((hours % 12) * 60 + minutes) / (12 * 60);
  s_hour_from = s_hour_to - TRIG_MAX_ANGLE / (12 * 60);

  prv_schedule(&s_minute_anim, &s_minute_impl, MINUTE_ANIM_DURATION,
               AnimationCurveEaseInOut, prv_bounce_curve, NULL);
}

// Where the three hands are heading, all starting from 12 o'clock. Called
// again on a tick mid-sweep, so a minute turning over during the sweep moves
// the target rather than starting a second animation on top of it.
static void prv_set_intro_targets(const struct tm *t) {
  s_second_to = TRIG_MAX_ANGLE + TRIG_MAX_ANGLE * t->tm_sec / 60;
  s_minute_to = TRIG_MAX_ANGLE + TRIG_MAX_ANGLE * t->tm_min / 60;
  s_hour_to = TRIG_MAX_ANGLE + TRIG_MAX_ANGLE * ((t->tm_hour % 12) * 60 + t->tm_min) / (12 * 60);
  s_second_from = TRIG_MAX_ANGLE;
  s_minute_from = TRIG_MAX_ANGLE;
  s_hour_from = TRIG_MAX_ANGLE;
}

static void prv_start_intro(const struct tm *t) {
  prv_set_intro_targets(t);
  s_second_angle = TRIG_MAX_ANGLE;
  s_minute_angle = TRIG_MAX_ANGLE;
  s_hour_angle = TRIG_MAX_ANGLE;
  s_intro_running = true;
  // Eased out, not bounced: a sweep of most of a turn would overshoot by a
  // visible margin and read as the hand missing its mark.
  prv_schedule(&s_intro_anim, &s_intro_impl, INTRO_DURATION, AnimationCurveEaseOut,
               NULL, &s_intro_handlers);
}

static void prv_update_date(const struct tm *t) {
  strftime(s_date_buffer, sizeof(s_date_buffer), "%a %d", t);
  // The date window is part of the cached dial, so a new date means a redraw.
  s_dial_valid = false;
  // Dial text reads better in caps.
  for (char *c = s_date_buffer; *c; c++) {
    if (*c >= 'a' && *c <= 'z') {
      *c -= 'a' - 'A';
    }
  }
}

static void prv_tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  if (units_changed & DAY_UNIT) {
    prv_update_date(tick_time);
  }
  if (s_intro_running) {
    prv_set_intro_targets(tick_time);
    return;
  }
  if (units_changed & MINUTE_UNIT) {
    prv_animate_minute(tick_time->tm_hour, tick_time->tm_min);
  }
  if (s_second_fade > 0) {
    prv_animate_second(tick_time->tm_sec);
  }
}

// Puts the second hand at the current second without animating, for when it
// reappears partway through a minute.
static void prv_sync_second_angle(void) {
  const time_t now = time(NULL);
  const struct tm *t = localtime(&now);
  s_second_angle = TRIG_MAX_ANGLE + TRIG_MAX_ANGLE * t->tm_sec / 60;
}

// Moves the tick subscription to match whether the hand is on screen at all,
// fade included. Idempotent, so the fade can call it every frame.
static void prv_sync_tick_rate(void) {
  // A fade that has been scheduled but not yet run its first frame is already
  // a hand on its way in, so it counts as on screen.
  const bool fading_in = s_fade_anim && animation_is_scheduled(s_fade_anim) && s_fade_to > 0;
  const int8_t per_second = (s_second_fade > 0 || fading_in) ? 1 : 0;
  if (per_second == s_ticking_per_second) {
    return;
  }
  s_ticking_per_second = per_second;
  tick_timer_service_subscribe(per_second ? SECOND_UNIT : MINUTE_UNIT, prv_tick_handler);
  if (!per_second && s_second_anim) {
    animation_unschedule(s_second_anim);
  }
}

// Shows or hides the second hand. `animated` dissolves it, which is what a
// backlight transition wants; otherwise it snaps, which is what setting the
// initial state and changing the setting want.
static void prv_set_second_visible(bool visible, bool animated) {
  s_second_visible = visible;

  // The sweep owns the angle until it lands; snapping the hand to the live
  // second here is what made it jump as it came in.
  if (visible && !s_intro_running) {
    prv_sync_second_angle();
  }

  // Likewise, a snap mid-sweep would flash the hand on at full strength, so
  // while the face is loading in every change dissolves.
  if (animated || s_intro_running) {
    s_fade_from = s_second_fade;
    s_fade_to = visible ? ANIMATION_NORMALIZED_MAX : 0;
    prv_schedule(&s_fade_anim, &s_fade_impl, SECOND_FADE_DURATION,
                 AnimationCurveEaseInOut, NULL, NULL);
    // Fading in has to raise the tick rate up front; fading out lowers it from
    // prv_fade_update once the hand is actually gone.
    prv_sync_tick_rate();
    return;
  }

  if (s_fade_anim) {
    animation_unschedule(s_fade_anim);
  }
  s_second_fade = visible ? ANIMATION_NORMALIZED_MAX : 0;
  prv_sync_tick_rate();
  layer_mark_dirty(s_face_layer);
}

// The backlight service reports the real thing, one edge per wake, however the
// screen was lit -- wrist flick, double tap, or coming back from the menu. So
// the hand simply tracks it, and follows the user's own backlight timeout
// rather than a guess at it.
static void prv_backlight_handler(bool on) {
  if (on != s_second_visible) {
    prv_set_second_visible(on, true);
  }
}

// Applies a mode from scratch: tears down whatever the last one had running,
// then subscribes only to what this one needs. Safe to call repeatedly.
static void prv_apply_second_mode(SecondHandMode mode) {
  backlight_service_unsubscribe();

  if (mode == SecondHandModeBacklight) {
    backlight_service_subscribe(prv_backlight_handler);
    // Only edges are reported, so seed from the current state: the screen is
    // usually already lit when the face is launched or the setting changes.
    prv_set_second_visible(light_is_on(), false);
  } else {
    prv_set_second_visible(mode == SecondHandModeAlways, false);
  }
}

static void prv_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  const GRect bounds = layer_get_bounds(window_layer);
  s_center = grect_center_point(&bounds);

  s_face_layer = layer_create(bounds);
  layer_set_update_proc(s_face_layer, prv_face_update_proc);
  layer_add_child(window_layer, s_face_layer);

  const time_t now = time(NULL);
  const struct tm *t = localtime(&now);
  prv_update_date(t);
  // Sweep up from 12 to the current time; the per-tick animations take over
  // once the sweep lands.
  prv_start_intro(t);
}

static void prv_window_unload(Window *window) {
  animation_unschedule_all();
  if (s_second_anim) {
    animation_destroy(s_second_anim);
    s_second_anim = NULL;
  }
  if (s_minute_anim) {
    animation_destroy(s_minute_anim);
    s_minute_anim = NULL;
  }
  if (s_fade_anim) {
    animation_destroy(s_fade_anim);
    s_fade_anim = NULL;
  }
  if (s_intro_anim) {
    animation_destroy(s_intro_anim);
    s_intro_anim = NULL;
  }
  s_intro_running = false;
  if (s_dial_cache) {
    gbitmap_destroy(s_dial_cache);
    s_dial_cache = NULL;
  }
  s_dial_valid = false;
  layer_destroy(s_face_layer);
}

// Clay sends a radiogroup selection as a string, but a hand-rolled config page
// or a future Clay release could send a plain number, so take either.
static int32_t prv_tuple_int(const Tuple *t) {
  if (t->type == TUPLE_CSTRING) {
    return atoi(t->value->cstring);
  }
  switch (t->length) {
    case 1: return t->value->int8;
    case 2: return t->value->int16;
    default: return t->value->int32;
  }
}

static SecondHandMode prv_clamp_mode(int32_t value) {
  if (value < SecondHandModeAlways || value > SecondHandModeNever) {
    return SECOND_MODE_DEFAULT;
  }
  return (SecondHandMode)value;
}

static void prv_inbox_received(DictionaryIterator *iter, void *context) {
  const Tuple *t = dict_find(iter, MESSAGE_KEY_SecondHandMode);
  if (!t) {
    return;
  }
  const SecondHandMode mode = prv_clamp_mode(prv_tuple_int(t));
  persist_write_int(PERSIST_KEY_SECOND_MODE, mode);
  prv_apply_second_mode(mode);
}

static SecondHandMode prv_load_second_mode(void) {
  if (!persist_exists(PERSIST_KEY_SECOND_MODE)) {
    return SECOND_MODE_DEFAULT;
  }
  return prv_clamp_mode(persist_read_int(PERSIST_KEY_SECOND_MODE));
}

static void prv_init(void) {
  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = prv_window_load,
    .unload = prv_window_unload,
  });
  window_stack_push(s_window, true);

  // Subscribes the tick service too, at whatever rate the mode calls for.
  prv_apply_second_mode(prv_load_second_mode());

  app_message_register_inbox_received(prv_inbox_received);
  app_message_open(64, 64);
}

static void prv_deinit(void) {
  backlight_service_unsubscribe();
  tick_timer_service_unsubscribe();
  window_destroy(s_window);
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}
