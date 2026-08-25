#include <pebble.h>

#define TICK_MINOR 6
#define TICK_MAJOR 12
#define NUMERAL_INSET 16
#define HAND_MARGIN_MINUTE 14
#define HAND_MARGIN_HOUR 46
#define HAND_MARGIN_SECOND 8
#define HAND_TAIL 20

#define DATE_PADDING_X 7
#define DATE_PADDING_Y 2
// Pebble's system fonts render their glyphs flush to the bottom of the line
// box, so centering on the measured content size leaves the text low. Measured
// on screen for Gothic 24 Bold: 12px of slack above, 2px below.
#define DATE_TEXT_LIFT 5

#define SECOND_ANIM_DURATION 500
#define MINUTE_ANIM_DURATION 500

static Window *s_window;
static Layer *s_face_layer;
static char s_date_buffer[12];

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

// Draws a hand from a short tail behind the center out to `margin` pixels short
// of the dial edge, so hands reach further toward the corners of a rectangular
// display than toward its sides. A hand given `fixed_radius` instead sweeps a
// true circle, keeping one length all the way round.
static void prv_draw_hand(GContext *ctx, int32_t angle, int16_t margin, uint8_t width,
                          GColor color, bool fixed_radius) {
  const GPoint tip = fixed_radius
      ? (GPoint) {
          .x = s_center.x + (int16_t)(sin_lookup(angle) * (prv_min_radius() - margin)
                                      / TRIG_MAX_RATIO),
          .y = s_center.y - (int16_t)(cos_lookup(angle) * (prv_min_radius() - margin)
                                      / TRIG_MAX_RATIO),
        }
      : prv_dial_point(margin, angle);
  const GPoint tail = {
    .x = s_center.x - (int16_t)(sin_lookup(angle) * HAND_TAIL / TRIG_MAX_RATIO),
    .y = s_center.y + (int16_t)(cos_lookup(angle) * HAND_TAIL / TRIG_MAX_RATIO),
  };

  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, width);
  graphics_draw_line(ctx, tail, tip);
}

static void prv_face_update_proc(Layer *layer, GContext *ctx) {
  const GRect bounds = layer_get_bounds(layer);
  s_center = grect_center_point(&bounds);
  s_rx = bounds.size.w / 2 - 1;
  s_ry = bounds.size.h / 2 - 1;

  graphics_context_set_antialiased(ctx, true);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  prv_draw_ticks(ctx);
  prv_draw_numerals(ctx);

  prv_draw_date(ctx);
  prv_draw_hand(ctx, s_hour_angle, HAND_MARGIN_HOUR, 9, GColorWhite, false);
  prv_draw_hand(ctx, s_minute_angle, HAND_MARGIN_MINUTE, 7, GColorWhite, false);
  prv_draw_hand(ctx, s_second_angle, HAND_MARGIN_SECOND, 2,
                PBL_IF_COLOR_ELSE(GColorRed, GColorWhite), true);

  // Center cap
  graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(GColorRed, GColorWhite));
  graphics_fill_circle(ctx, s_center, 6);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_circle(ctx, s_center, 2);
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

static const AnimationImplementation s_second_impl = {
  .update = prv_second_update,
};

static const AnimationImplementation s_minute_impl = {
  .update = prv_minute_update,
};

// Replaces whatever is in `slot`, so a tick arriving mid-bounce restarts that
// hand cleanly rather than leaking the old animation.
static void prv_schedule(Animation **slot, const AnimationImplementation *impl,
                         uint32_t duration) {
  if (*slot) {
    animation_unschedule(*slot);
    animation_destroy(*slot);
  }
  *slot = animation_create();
  animation_set_duration(*slot, duration);
  animation_set_custom_curve(*slot, prv_bounce_curve);
  animation_set_implementation(*slot, impl);
  animation_schedule(*slot);
}

static void prv_animate_second(int seconds) {
  // The extra full turn keeps every angle positive across the 59 -> 0 wrap.
  s_second_to = TRIG_MAX_ANGLE + TRIG_MAX_ANGLE * seconds / 60;
  s_second_from = s_second_to - TRIG_MAX_ANGLE / 60;

  prv_schedule(&s_second_anim, &s_second_impl, SECOND_ANIM_DURATION);
}

static void prv_animate_minute(int hours, int minutes) {
  s_minute_to = TRIG_MAX_ANGLE + TRIG_MAX_ANGLE * minutes / 60;
  s_minute_from = s_minute_to - TRIG_MAX_ANGLE / 60;

  s_hour_to = TRIG_MAX_ANGLE + TRIG_MAX_ANGLE * ((hours % 12) * 60 + minutes) / (12 * 60);
  s_hour_from = s_hour_to - TRIG_MAX_ANGLE / (12 * 60);

  prv_schedule(&s_minute_anim, &s_minute_impl, MINUTE_ANIM_DURATION);
}

static void prv_update_date(const struct tm *t) {
  strftime(s_date_buffer, sizeof(s_date_buffer), "%a %d", t);
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
  if (units_changed & MINUTE_UNIT) {
    prv_animate_minute(tick_time->tm_hour, tick_time->tm_min);
  }
  prv_animate_second(tick_time->tm_sec);
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
  // Start settled on the current time; the animations take over from the next tick.
  s_second_angle = TRIG_MAX_ANGLE + TRIG_MAX_ANGLE * t->tm_sec / 60;
  s_minute_angle = TRIG_MAX_ANGLE + TRIG_MAX_ANGLE * t->tm_min / 60;
  s_hour_angle = TRIG_MAX_ANGLE + TRIG_MAX_ANGLE * ((t->tm_hour % 12) * 60 + t->tm_min) / (12 * 60);
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
  layer_destroy(s_face_layer);
}

static void prv_init(void) {
  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = prv_window_load,
    .unload = prv_window_unload,
  });
  window_stack_push(s_window, true);

  tick_timer_service_subscribe(SECOND_UNIT, prv_tick_handler);
}

static void prv_deinit(void) {
  tick_timer_service_unsubscribe();
  window_destroy(s_window);
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}
