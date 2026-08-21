---
title: Installation
nav_order: 2
---

# Installation

## Supported Device

- Xteink X4 Pro

## Download the verified firmware

Download `CrossDiTo-x4-pro-v1.5.1.bin` from the [CrossDiTo 1.5.1 release](https://github.com/dito94/CrossDiTo/releases/tag/v1.5.1).

The verified application image is 6,137,264 bytes and has this SHA-256 checksum:

```text
be7afe4ea3e55bfbd46a7935715872b864a6df14ddc2f521918d2c69e3e65463
```

The release image was flashed to an X4 Pro and read back in full with an exact checksum match.

## SD Card Firmware Update

Use this method for an existing CrossDiTo installation. It also works when USB data transfer is unavailable.

1. Place the downloaded `CrossDiTo-x4-pro-v1.5.1.bin` file anywhere on the SD card.
2. Go to `Settings > System > SD Card Firmware Update`.
3. Select the `.bin` file and confirm the update.

## USB-locked devices

Use the SD Card Firmware Update method above. It does not require USB data access.

## Command Line

These instructions are for macOS and Linux. Windows users should use the web installer.

Install `esptool`:

```sh
pip3 install esptool
```

Download `CrossDiTo-x4-pro-v1.5.1.bin` from the [CrossDiTo releases page](https://github.com/dito94/CrossDiTo/releases), then connect the X4 Pro with USB-C.

Find the device port:

```sh
# Linux
dmesg | grep tty

# macOS
ls /dev/cu.*
```

Flash the firmware using the X4 Pro's `esp32s3` target:

```sh
# Linux
esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 write_flash 0x10000 /path/to/CrossDiTo-x4-pro.bin

# macOS
esptool.py --chip esp32s3 --port /dev/cu.usbmodem2101 --baud 921600 write_flash 0x10000 /path/to/CrossDiTo-x4-pro.bin
```

Replace the port and firmware path with your actual values.
