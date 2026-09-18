---
title: Installation
nav_order: 2
---

# Installation

Grab a prebuilt `.bin` from the [Releases
page](https://github.com/smashedllama/retroink/releases) and use one of the
install methods below, easiest first. Building from source is only needed if
you want to modify the code yourself.

## Supported Devices

- Xteink X3, X4
- Seeed Studio Sticky

## Install via the web flash tool (easiest)

No software to install. Connect your device with USB-C, then go to
[crosspointreader.com/#flash-tools](https://crosspointreader.com/#flash-tools)
in Chrome or Edge, pick the `.bin` you downloaded, and follow the on-page
steps.

## Install via SD card

Works even on USB-locked devices, and doesn't need a computer connected to
the reader.

1. Copy the `.bin` onto your SD card, anywhere on it.
2. On the device, go to **Settings > System > SD Card Firmware Update**.
3. Navigate to the `.bin` file and confirm the update.

## Install via USB (esptool)

These instructions are for macOS and Linux.

Install `esptool`:

```sh
pip3 install esptool
```

Connect your device with USB-C, then find the device port:

```sh
# Linux
dmesg | grep tty

# macOS
ls /dev/cu.*
```

Flash the firmware:

```sh
# Linux
esptool.py --chip esp32c3 --port /dev/ttyACM0 --baud 921600 write_flash 0x10000 /path/to/firmware-x3-x4.bin

# macOS
esptool.py --chip esp32c3 --port /dev/cu.usbmodem2101 --baud 921600 write_flash 0x10000 /path/to/firmware-x3-x4.bin
```

Replace the port and firmware path with your actual values.

## Build from source

You'll need [PlatformIO](https://platformio.org/) (the `pio` CLI).

```sh
git clone https://github.com/smashedllama/retroink.git
cd retroink
pio run -e default
```

The built firmware lands at `.pio/build/default/firmware-x3-x4.bin`. Use it
with any of the install methods above.

## After installing

See [What's Different in RetroInk](./whats-different.html) for what's new
over stock CrossInk, or jump straight to [Obsidian Clipping
Sync](./obsidian-sync.html) setup if that's what brought you here.
