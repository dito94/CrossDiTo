---
title: Simulator
nav_order: 15
---

# Development Device Simulator

CrossDiTo can run in the [device simulator](https://github.com/uxjulia/crossink-simulator), which renders the X4 Pro display in an SDL2 window. Use it for quick sanity checks without flashing firmware every time.

## Platform Support

The native build uses `sdl2-config`, so SDL2 must be installed and available in `PATH`. macOS and Linux are the primary supported hosts; Windows users should use WSL.

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

The simulator always uses the X4 Pro capability profile.

## Cache Note

On first open of an EPUB, an **Indexing...** popup appears while the section cache is built in `.crosspoint/`.

If rendering looks stale after a code change, delete `./fs_/.crosspoint/` to clear simulator caches.
