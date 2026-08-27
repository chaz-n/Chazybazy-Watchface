# Chazybazy's Watchface 

A Pebble watchapp/watchface written in C using the Pebble SDK.

![til](./screenshots/emery_1.0.1_20260825-203352.gif)

## Settings

The watchface has a settings page (Clay), reachable from the watchface list in
the Pebble mobile app. It offers one choice, the second hand:

| Mode | Behaviour | Cost |
| --- | --- | --- |
| **Always on** (default) | The second hand sweeps continuously. | The watch wakes every second and animates at 30fps. |
| **Only when the backlight is on** | The hand fades in and out with the backlight. | Per-second wakes only while the screen is lit. |
| **Off** | No second hand. | The face wakes once a minute. |

The middle mode uses `backlight_service_subscribe`, which reports one edge each
time the backlight turns on or off, whatever lit it — a wrist flick, a double
tap, or coming back from the menu. Because only edges are reported, the mode
seeds itself from `light_is_on()` when it starts, since the screen is usually
already lit when the watchface launches or the setting changes.

Nothing needs tuning: the hand follows your own backlight timeout rather than a
guess at it, and no extra sensor is powered to work out when the screen is lit.

The hand dissolves over `SECOND_FADE_DURATION` rather than snapping. Line
drawing treats the stroke alpha as all-or-nothing on this hardware — alpha 2
renders identically to alpha 3, alpha 1 not at all — so the hand cannot be
blended over the dial. It is faded by ramping its colour toward the black
background instead, which on a 64-colour display gives four steps per channel.
Per-second ticking stops only once the hand has finished fading out.

The chosen mode is persisted on the watch, so it survives a restart and applies
before the phone connects.

## Building & running

### With the local SDK

```sh
pebble build                          # build for all targetPlatforms
pebble install --emulator emery       # install on the emery emulator
pebble install --phone <ip>           # install to a paired phone
```

Download from the Pebble App Store now: [Chazybazy's Watchface](https://apps.repebble.com/chazybazy-s-watchface_14d2b30fb13d46068fff58d7)

### Installing over the CloudPebble connection

The CloudPebble connection is a transport to your phone, so the tool can push a
locally built `.pbw` to the watch without a cable or a LAN IP. Enable the
developer connection in the Pebble mobile app first, then:

```sh
pebble login                          # authenticate the tool with your account
pebble build
pebble install --cloudpebble          # push this build to the paired watch
```

## Publishing

`pebble publish` uploads a release to the Pebble appstore
(`appstore-api.repebble.com`). It needs `pebble login` first, and it publishes
whatever is currently built, so build before you publish:

```sh
pebble build
pebble publish --release-notes "First release"
```

Before uploading, the tool captures rollover GIFs on the emulator for every
supported platform (this is where `screenshots/` comes from). `--all-platforms`
adds static screenshots as well, and `--no-gif-all-platforms` skips the capture.


## Target platforms

`targetPlatforms` in `package.json` controls which watches you build for. The
modern Pebble hardware is **emery** (Pebble Time 2), **gabbro** (Pebble Round
2), and **flint** (Pebble 2 Duo); the original Pebble platforms are aplite,
basalt, chalk and diorite. This project targets **emery** only — add the others
back to `targetPlatforms` if you want them.

## Project layout

```
src/c/           C source for the watchapp
src/pkjs/        PebbleKit JS (phone-side) source, if any
worker_src/c/    Background worker source, if any
resources/       Images, fonts, and other bundled resources
package.json     Project metadata (UUID, platforms, resources, message keys)
wscript          Build rules — usually no need to edit
```

This project is configured as a watchface (`pebble.watchapp.watchface` is
`true` in `package.json`).

## Documentation

Full SDK docs, tutorials, and API reference: <https://developer.repebble.com>
