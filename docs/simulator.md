---
title: Simulator
nav_order: 15
---

# Development Device Simulator

CrossInk can run in the [CrossPoint simulator](https://github.com/uxjulia/crosspoint-simulator), which renders the e-ink display in an SDL2 window. Use it for quick sanity checks without flashing firmware every time.

## Platform Support

The simulator is currently configured for macOS on Apple Silicon.

The `platformio.ini` `[env:simulator]` section contains hardcoded `-arch arm64` and Homebrew paths under `/opt/homebrew`.

- Intel Mac users need to remove `-arch arm64` and change Homebrew paths to `/usr/local`.
- Linux requires similar path changes plus a replacement for `lib/simulator_mock/src/MD5Builder.h`, which uses the macOS-only `CommonCrypto` API.
- Native Windows is not supported. Use WSL and follow the Linux adjustments.

## Prerequisites

```sh
# macOS
brew install sdl2

# Linux (Debian/Ubuntu)
sudo apt install libsdl2-dev
```

## Setup

Place EPUB books in `./fs_/books/` relative to the project root. That maps to the SD-card `/books/` path on device.

## Build And Run

```sh
pio run -e simulator
.pio/build/simulator/program
```

Use the X4 Pro environment to enable its touch, frontlight, and Home-key behavior:

```sh
pio run -e x4-pro-simulator -t run_simulator
```

## Keyboard Controls

| Key | Action |
| --- | --- |
| Up / Down | Page back / forward (side buttons) |
| Left / Right | Left / right front buttons |
| Return | Confirm / Select |
| Escape | Back |
| P | Power |
| H | X4 Pro Home key (tap to go Home; hold for 700 ms to toggle the reader menu) |

The `H` mapping is active only in `x4-pro-simulator`.

## Clock

The plain `simulator` env's clock (from the fetched `crossink-simulator`
package, `.pio/libdeps/simulator/simulator/src/HalClock.cpp`) previously only
reported as available when `SIMULATOR_DEVICE_X3`, `SIMULATOR_DEVICE_X4_PRO`,
or `SIMULATOR_DEVICE_STICKY` was defined, which the plain `simulator` env
never does. Every screen gated on `isAvailable()` (Reading Stats, Reading
Year, the header clock, sleep screens) showed "Set Date/Time" with no way to
clear it, since there was no real RTC behind it to set.

`.pio/libdeps/` isn't checked into this repo, so a fresh `pio pkg install`
(or a clean clone) brings back the original stub. If clock-dependent screens
regress, either:

- Patch `HalClock::begin()` in `.pio/libdeps/simulator/simulator/src/HalClock.cpp`
  to unconditionally set `_available = true` (it already reads the host
  machine's real UTC clock; it just wasn't being allowed to report as
  available on this env), or
- Run `simulator-X3`, `x4-pro-simulator`, or `sticky-simulator` instead,
  which already define one of those macros and have always had a working
  clock.

## Cache Note

On first open of an EPUB, an **Indexing...** popup appears while the section cache is built in `.crosspoint/`.

If rendering looks stale after a code change, delete `./fs_/.crosspoint/` to clear simulator caches.
