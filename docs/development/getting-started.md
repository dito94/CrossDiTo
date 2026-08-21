---
title: Getting Started
parent: Development
nav_order: 1
---

# Getting Started

This guide helps you build and run CrossDiTo locally.

## Prerequisites

- PlatformIO Core (`pio`) or VS Code + PlatformIO IDE
- Python 3.8+
- `clang-format` 21+ in your `PATH` (CI uses clang-format 21)
- USB-C cable
- Xteink X4 Pro for hardware testing

If `./bin/clang-format-fix` fails with either of these errors, install clang-format 21:

- `clang-format: No such file or directory`
- `.clang-format: error: unknown key 'AlignFunctionDeclarations'`

Examples:

```sh
# Debian/Ubuntu (try this first)
sudo apt-get update && sudo apt-get install -y clang-format-21

# If the package is unavailable, add LLVM apt repo and retry
wget https://apt.llvm.org/llvm.sh
chmod +x llvm.sh
sudo ./llvm.sh 21
sudo apt-get update
sudo apt-get install -y clang-format-21

# macOS (Homebrew)
brew install clang-format
```

Then verify:

```sh
clang-format-21 --version
```

The reported major version must be 21 or newer.

## Clone

```sh
git clone https://github.com/dito94/CrossDiTo.git
cd CrossDiTo
```

The hardware SDK used by the verified X4 Pro release is vendored in `freeink-sdk/`, so no submodule initialization is required.

## Build

```sh
pio run -e x4-pro
pio run -e x4-pro-simulator
```

`pio run` without an environment builds the sole production target, X4 Pro.
Normal local builds include a development branch suffix. To create a clean release-version image, set `CROSSINK_RELEASE_VERSION=1.5.1` for the build.

```sh
CROSSINK_RELEASE_VERSION=1.5.1 pio run -e x4-pro
```

## Flash

```sh
pio run -e x4-pro --target upload
```

## Validation

```sh
./bin/clang-format-fix
pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high
pio run
```

## What to read next

- [Architecture Overview](./architecture.md)
- [Testing and Debugging](./testing-debugging.md)
