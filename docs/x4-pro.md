# Xteink X4 Pro bring-up

The X4 Pro uses an ESP32-S3 with 16 MB flash and 8 MB PSRAM. It needs a
separate build from the ESP32-C3 X3/X4 and the differently wired Seeed Sticky.
Hardware validation of this RetroInk target is pending.

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
Pro target. Display-controller detection already runs before display SPI
initialization in `setupDisplayAndFonts()`.

## First hardware test

1. Identify the connected chip as ESP32-S3 and back up its existing flash
   before the first firmware write. Check the installed partition table before
   using an app-only update; do not assume the stock layout matches
   `partitions.csv`. Preserve the SD card and factory configuration.
2. With a compatible flash layout established, upload using
   `pio run -e x4-pro -t upload --upload-port /dev/ttyACM0` and monitor at
   115200 baud. On Linux, the port needs the `cdc_acm` driver and your account
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
