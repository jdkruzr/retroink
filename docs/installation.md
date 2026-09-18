---
title: Installation
nav_order: 2
---

# Installation

RetroInk doesn't have a hosted web flash tool or a releases page yet. It's
a personal fork, built from source. This covers the two ways to get it onto
your device: build it yourself, or flash a `.bin` someone already built.

## Supported Devices

- Xteink X3, X4
- Seeed Studio Sticky

## Build from source

You'll need [PlatformIO](https://platformio.org/) (the `pio` CLI).

```sh
git clone https://github.com/smashedllama/retroink.git
cd retroink
pio run -e default
```

The built firmware lands at `.pio/build/default/firmware-x3-x4.bin`. That's
the file the two install methods below both want.

## Install via SD card

Works even on USB-locked devices, and doesn't need a computer connected to
the reader.

1. Copy `firmware-x3-x4.bin` onto your SD card, anywhere on it.
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

## After installing

See [What's Different in RetroInk](./whats-different.html) for what's new
over stock CrossInk, or jump straight to [Obsidian Clipping
Sync](./obsidian-sync.html) setup if that's what brought you here.
