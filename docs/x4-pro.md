# Xteink X4 Pro bring-up

The X4 Pro uses an ESP32-S3 with 16 MB flash and 8 MB PSRAM. It needs a
separate build from the ESP32-C3 X3/X4 and the differently wired Seeed Sticky.
Hardware boot, restart, the RetroInk interface, touch, and page buttons are
verified on a UC8179-panel unit. Short-tap sleep/wake is also verified, including
returning from the book-cover sleep screen to the normal UI.

## Build

Use pioarduino PlatformIO Core v6.1.19, matching CI, and initialize the pinned
SDK before building:

```sh
git submodule update --init --recursive
pio run -e x4-pro
```

On Linux/macOS, `PLATFORMIO_RUN_JOBS=6 pio run -e x4-pro` limits compiler
parallelism, including the automatic follow-up build after SDK regeneration.
Passing only `-j 6` does not propagate that limit to the follow-up build in
the pinned toolchain.

The output is `.pio/build/x4-pro/firmware-x4-pro.bin`. This is an application
image, not a complete flash backup or a merged bootloader/partition/app image.
The release workflow currently publishes only X3/X4 firmware; use the Pro
source build or its CI artifact.

The target enables the SDK's X4 Pro board profile, touch and capacitive Home
key, dual-channel frontlight, and native SDMMC through SdFat's block-device
interface. The single display framebuffer uses PSRAM with the SDK's existing
internal-RAM fallback if that allocation fails. These flags apply only to the
Pro target. Display-controller detection runs in `HalDisplay::begin()` before
display SPI initialization. It releases the reset pin's sleep hold before
probing, so a sleeping UC8179 can answer and select the correct driver on wake.

## Reader navigation

- Tap the capacitive Home pad to return from an EPUB to Home. This also works
  with reader touchscreen input disabled.
- Hold the Home pad or swipe upward in the reader to open the reader menu.
- Swipe downward in the reader to open the frontlight panel.
- In menus, tap the title bar's small square at the upper left to go back.
  The reader menu also has a Home icon at the upper right.

## First hardware test

1. Identify the connected chip as ESP32-S3 and back up its existing flash
   before the first firmware write. Check the installed partition table before
   using an app-only update; do not assume the stock layout matches
   `partitions.csv`. Preserve the SD card and factory configuration.
2. Preserve an existing layout by writing the app image to the inactive OTA
   partition at the offset read from that device's table, verifying the write,
   and then selecting that slot with a fresh OTA record. A previously failed
   slot may be marked ABORTED; writing new app bytes alone does not clear that
   state. Follow `src/network/OtaBootSwitch.cpp` for the sequence, state, and
   CRC format, retaining the valid fallback record. PlatformIO's normal upload
   also writes bootloader/partition data, so use it only for an intentional
   installation of the repo's full layout. Monitor at 115200 baud.
   On Linux, the port needs the `cdc_acm` driver and your account
   needs access to the device (typically through `dialout`). Use the device's
   `/dev/serial/by-id/` path if multiple serial devices are connected.
3. Confirm the boot log identifies the Pro profile, initializes PSRAM, detects
   the display controller, mounts SDMMC, and reaches the RetroInk home screen
   without allocation failures or resets. Record internal heap/largest block,
   PSRAM availability, and task stack high-water marks when diagnosing failures.
4. Test touch at all four corners, Home tap/hold, both page buttons, frontlight
   brightness and warmth, and full/partial display refresh. Panel controllers
   vary by production batch; retain the detection log if the display stays blank.
5. Open an EPUB, turn pages, return to Library, then sleep and wake. Confirm
   reading position survives and SD access, touch, and frontlight still work.
   No cache format changes are made by this target; no cache reset is required.

The SDK's `docs/xteink-x4pro-support.md` records the board wiring and known
hardware findings. Successful compilation alone does not validate this unit's
panel, touch mapping, or power behavior.

## Hardware boot verification — 2026-09-19

One ESP32-S3 revision 0.2 unit reported 16 MB flash and 8 MB PSRAM. Its
installed table used app0 at `0x10000` and app1 at `0x7f0000`, each 8064 KiB;
these offsets differ from the repo's table and must not be assumed for other
devices. Its failed app1 image targeted ESP32-C3 and was marked ABORTED.

After a full flash backup, the 6,244,720-byte S3 image was written and
hash-verified at app1. Only the failed OTA record was replaced (sequence 4,
NEW state), preserving app0 and the partition table. After boot, readback
confirmed the new record had become VALID; a second reset booted the S3
firmware again.

Both boots logged the `xteink_x4_pro` profile, a mounted SDMMC card, RTC,
frontlight initialization, 8 MB PSRAM, and UC8179 detection from the display
bus. Refreshes completed without a reset loop. At idle, logs reported about
255 KB free internal heap and 8.32 MB free PSRAM. The log also reported no
IMU and no installed dictionary; visual/input and reading checks are separate
from this boot verification.

## Display detection after sleep

The original wake failure accepted the short Power press and restored the
frontlight, but the display probe returned `FF FF FF FF FF` and selected the
default SSD1677 driver. Refreshes then waited 30 seconds while the sleep cover
remained visible. The SDK holds display reset high during sleep; its normal
display initialization released that hold only after the earlier probe.

The Pro's HAL now releases the reset hold before probing and selecting the
driver. A hardware sleep/wake cycle with short Power set to Sleep logged
`PowerButton`, `shortAllowed=1`, `VER=00 00 01 FF FF`, and UC8179 selection.
Subsequent display refreshes completed in about 0.56 or 1.50 seconds, and the
user confirmed that the normal UI returned. No settings or cache reset is required.
