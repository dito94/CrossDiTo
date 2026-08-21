---
title: Testing & Debugging
parent: Development
nav_order: 4
---

# Testing and Debugging

CrossDiTo runs on real hardware, so debugging usually combines local build checks, simulator checks, and on-device logs.

## Local checks

Make sure `clang-format` 21+ is installed and available in `PATH` before running the formatting step.
If needed, see [Getting Started](./getting-started.md).

```sh
./bin/clang-format-fix
pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high
pio run -e x4-pro
pio run -e x4-pro-simulator
```

`pio run` without `-e` builds the X4 Pro production firmware. Use the explicit environment while iterating so logs and artifacts are unambiguous.

## Flash and monitor

Flash firmware:

```sh
pio run -e x4-pro --target upload
```

Open serial monitor:

```sh
pio device monitor
```

Optional enhanced monitor:

```sh
python3 -m pip install pyserial colorama matplotlib
python3 scripts/debugging_monitor.py
```

## Useful bug report contents

- Firmware version and build environment
- Exact steps to reproduce
- Expected vs actual behavior
- Serial logs from boot through failure
- Whether issue reproduces after clearing the affected book cache or using **Clear Reading Cache**

## Common troubleshooting references

- [Common Issues](../troubleshooting.md)
