# CrossDiTo

> **A focused Xteink X4 Pro firmware fork of [CrossInk 1.5](https://github.com/uxjulia/CrossInk), itself based on [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader).** CrossDiTo prioritizes reliable reading, responsive navigation, restrained power use, and clear typography.

### Supported Device

- Xteink X4 Pro

The current hardware-verified release is [CrossDiTo 1.5.1](./docs/releases/v1.5.1.md), based on CrossInk 1.5.0. The complete project history is in [CHANGELOG.md](./CHANGELOG.md).

## What's different in this fork

My goal with this fork was to maintain the core Crosspoint firmware while integrating my preferred typography and some lightweight reading statistics. I’ve focused on keeping the underlying system stable while layering in a few "nice-to-have" features and UI refinements along the way.

<table>
  <tr>
    <td align="center">
      <img src="./docs/images/bitter-small-15-margin.jpg" alt="Font: Bitter, Size: 12 pt, Margin: 15" /><br/>
      <em>Font: Bitter, Size: 12 pt, Margin: 15</em>
    </td>
    <td align="center">
      <img src="./docs/images/reading-stats.jpg" alt="Reading Stats with custom front button mapping shown" /><br/>
      <em>Reading Stats with custom front button mapping shown</em>
    </td>
  </tr>
</table>

### Highlights

- New reader fonts: Lexend Deca and Bitter.
- Unicode emoji and miscellaneous symbols support (a limited subset).
- Reader font sizes: 10 pt, 12 pt, 14 pt, and 16 pt.
- Added ~~strikethrough~~ support.
- Made <u>underlines</u> thicker for better visibility.
- Added a custom `Minimal` theme and sleep screen option for the minimalists out there.
- Added a custom `Dashboard` theme and sleep screen option for reading stats enthusiasts.
- Added support for `<hr>` section breaks.
- Added support for "redaction" style rendering.
- Added improved support for tables with simple markup.
- Added ability to add bookmarks.
- Added ability to remap front buttons that only applies in the reader.
- Added Bionic Reading and Guide Dots as optional reader modes.
- Added Force Paragraph Indents for books that render as one giant wall of text.
- Added ability to pin a sleep image as a favorite. The favorited image will always be displayed when your sleep settings are set to `Custom` or `Cover + Custom` (when no cover is available).
- Added more in-reader control remapping options for side buttons, short power button clicks, and long-press menu actions.
- Added ability to mark a book as finished from the in-book menu. A pop-up will also display once 99% of the book is reached. This status allows tracking of total books read.
- Added ability to move finished books to "Read" folder.
- In-book menu to quickly adjust reader options without having to exit the book.
- Reading stats: total books read, total reading time, number of sessions, pages turned, average session time, pages turned per minute. You can also set your reading stats as your sleep screen.
- All-time reading stats [syncing](./docs/reading-stats-sync.md) between two CrossDiTo devices.
- Reading [progress sync](./docs/nearby-position-sync.md) between two CrossDiTo devices.
- Added customizable Auto Page Turn Interval (anything between 5-120 seconds).
- Added ability to view Recent Books as a 3x3 grid view.
- To view release notes and download firmware, visit the [CrossDiTo releases](https://github.com/dito94/CrossDiTo/releases) page.

---

### Reader Fonts

The default fonts have been replaced with Lexend Deca and Bitter. These fonts have been chosen specifically to improve reading fluency and e-ink performance. These 'sturdier' typefaces feature uniform stroke weights and open geometries, allowing the X4 Pro to render crisp, high-contrast text with font-aliasing on while significantly reducing ghosting and artifacts.

- [Lexend Deca](https://fonts.google.com/specimen/Lexend+Deca) - A research-backed sans-serif typeface designed to improve reading fluency. Lexend was engineered based on the theory that reading issues are often a design problem (visual crowding) rather than a cognitive one.
- [Bitter](https://fonts.google.com/specimen/Bitter) - A "contemporary" slab serif typeface for text, it is specially designed for comfortably reading on digital screens. The consistent stroke weight of Bitter helps it render particularly well on e-ink devices. The medium weight has been chosen specifically for improved rendering on the X4 Pro.

The UI now uses [Inter](https://fonts.google.com/specimen/Inter) as the display font which has improved readability at smaller sizes.

### Emojis and Misc Glyphs

- Support for a limited set of Unicode [Emoticons](https://unicode-explorer.com/b/1F600) and [Miscellaneous Symbols](https://unicode-explorer.com/b/2600) using [Noto Emoji](https://fonts.google.com/noto/specimen/Noto+Emoji) and [Noto Sans Symbols](https://fonts.google.com/noto/specimen/Noto+Sans+Symbols) font.

---

### Font Sizes

CrossDiTo includes 10 pt, 12 pt, 14 pt, and 16 pt built-in reader font sizes.

See [SD Card Fonts](./docs/sd-card-fonts.md) for installing additional font families and size ranges.

---

### Reader features

Reader Options, Bionic Reading, Guide Dots, Force Paragraph Indents, reading stats, and finished-book behavior are documented in [Reader Features](./docs/reader-features.md).

### Custom button actions

CrossDiTo adds configurable button shortcuts.

See [Controls](./docs/controls.md) for the full action list and defaults.

---

## Tips for the best reading experience

CrossDiTo uses the X4 Pro's ESP32-S3 and PSRAM, but internal RAM and e-ink bandwidth are still constrained compared with a phone, tablet, or desktop app.

- Keep folders under about 200 files. For the smoothest browsing, aim for 50-100 files per folder.
- Having 1000+ books on the SD card is fine if they are split into smaller folders, such as by author, series, genre, or read/unread status.
- Avoid putting every book in the SD card root. The file browser has to scan and sort the current folder before it can show it.
- Text-first EPUBs are the best fit. Large image-heavy EPUBs, scanned books, comics, and omnibus files with thousands of sections may load slowly or fail under memory pressure.
- As a rough target, EPUBs under 20 MB tend to work the best. Files over 50 MB may still work, but they are more likely to be slow or memory-sensitive, especially if they contain many large images.
- If an EPUB is unusually slow, try [optimizing](./docs/webserver.md#epub-optimization) it with the built-in web optimizer (via File Transfer) before copying it to the SD card: remove unused high-resolution images, split very large omnibus files, and avoid embedding multiple full font families when possible.
- Use a reliable SD card and leave some free space. CrossDiTo stores settings, reading progress, cache files, stats, and generated book data on the card.

## Development Device Simulator

The [device simulator](https://github.com/uxjulia/crossink-simulator) renders the e-ink display in an SDL2 window so firmware changes can be sanity-checked without flashing hardware.

See [Simulator](./docs/simulator.md) for setup, platform notes, keyboard controls, and cache tips.

---

## Installation

Download `CrossDiTo-x4-pro-v1.5.1.bin` from the [CrossDiTo 1.5.1 release](https://github.com/dito94/CrossDiTo/releases/tag/v1.5.1). For an existing CrossDiTo installation, copy the file to the SD card and select **Settings > System > SD Card Firmware Update**. USB command-line flashing is also documented.

See [Installation](./docs/installation.md) for step-by-step flashing and revert instructions.

---

## Documentation

- [User Guide](./docs/user-guide.md)
- [Installation](./docs/installation.md)
- [SD Card Fonts](./docs/sd-card-fonts.md)
- [Reader Features](./docs/reader-features.md)
- [Dictionary](./docs/dictionary.md)
- [Controls](./docs/controls.md)
- [Simulator](./docs/simulator.md)
- [Data Cache](./docs/data-cache.md)
- [Web server usage](./docs/webserver.md)
- [Web server endpoints](./docs/webserver-endpoints.md)
- [Common issues](./docs/troubleshooting.md)
- [Project scope](./SCOPE.md)
- [Development docs](./docs/development/README.md)

---

## Development quick start

CrossDiTo uses PlatformIO for building and flashing firmware.

See [Getting Started](./docs/development/getting-started.md) for prerequisites, clone setup, and validation commands.

### Nix/NixOS

Nix/NixOS users can enter the development shell with either `nix develop` (flakes) or `nix-shell`:

```bash
nix develop -f nix
# or
nix-shell nix
```

To flash the X4 Pro's ESP32-S3, enable PlatformIO's udev rules in your NixOS configuration:

```nix
services.udev.packages = with pkgs; [ platformio-core.udev ];
```

After rebuilding the system configuration, reconnect the device or reload udev rules.

### Build / flash / monitor

Connect the X4 Pro via USB-C:

```sh
# Xteink X4 Pro
pio run -e x4-pro --target upload
```

`x4-pro` is the only production firmware environment. Running `pio run` without `-e` builds the same target.

See [Testing and Debugging](./docs/development/testing-debugging.md) for serial logging, simulator checks, static analysis, and bug-report guidance.

---

## Repository layout

- `src/` - app orchestration, settings/state, and activity implementations (home, reader, settings, network, boot/sleep)
- `lib/` - supporting libraries: EPUB parsing/layout, fonts, i18n, filesystem helpers, HAL wrappers, and more
- `freeink-sdk/` - the exact vendored hardware SDK snapshot used by the verified X4 Pro release; it contains display, input, storage, frontlight, and battery support
- `web/` - web portal sources (`templates/`, `pages/`, `assets/`); compiled by `scripts/build_web.py` into `src/network/html/*.generated.h`
- `docs/` - user and developer documentation, published via the `site/` Astro site
- `site/` - Astro project that builds `docs/` into the CrossDiTo documentation website
- `test/` - unit tests and EPUB test fixtures
- `scripts/` - build, codegen, and release tooling (i18n generation, web asset building, hyphenation tries, release packaging, etc.)
- `bin/` - helper scripts for formatting (`clang-format-fix`) and CI checks
- `fs_/` - sample SD card contents (books, sleep images, themes) used by the simulator
- `nix/` - Nix/NixOS development shell definitions
- `managed_components/` - ESP-IDF managed component dependencies, fetched automatically during build
- [`SCOPE.md`](./SCOPE.md) and [`CHANGELOG.md`](./CHANGELOG.md) - project scope and complete release history

## Internals

CrossDiTo keeps its 48 KB display framebuffer in PSRAM and stores reusable book data on the SD card, preserving faster internal RAM for tasks, drivers, and latency-sensitive work.

See [Data Cache](./docs/data-cache.md) for the `.crosspoint` layout and [File Formats](./docs/file-formats.md) for binary cache details.

## Notice on Contributions

CrossDiTo intentionally stays narrow and X4 Pro-only. Bug reports may be opened in [CrossDiTo issues](https://github.com/dito94/CrossDiTo/issues). Major features requiring broad device support or ongoing upstream maintenance should be proposed to [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader).
