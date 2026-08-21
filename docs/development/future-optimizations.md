---
title: Future Optimization Roadmap
parent: Development
nav_order: 5
---

# Future Optimization Roadmap

This roadmap is limited to Xteink X4 Pro work that can materially improve
reading responsiveness or battery life. It does not add applications, network
features, or settings for internal implementation choices.

The first implementation pass is now in the Unreleased firmware. Hardware
measurements still decide whether an optimization remains enabled or needs
controller-specific adjustment. Unsafe variants remain recorded so they are
not repeatedly rediscovered and implemented without the missing evidence.

## Acceptance gate

Before an optimization ships, it must:

1. Beat a repeatable baseline on the same X4 Pro, firmware configuration, book,
   layout, brightness, and battery/USB state.
2. Produce a noticeable result: normally at least 15% less time in a user-visible
   operation, at least 100 ms removed from an interaction, or at least 10% lower
   measured board current in the state being optimized.
3. Preserve button and touch wake, frontlight stability, USB transfer, SD
   reliability, cache recovery, and both supported X4 Pro panel-controller
   variants.
4. Keep the one-framebuffer policy. Any new scratch or cache allocation must be
   bounded, allocated once, reused, and included in free-heap/largest-block
   measurements.
5. Pass the simulator and X4 Pro builds, static analysis, cache migration tests,
   and a hardware regression run. A known-good firmware image must remain
   available during hardware experiments.

## Priority 1: deadline-driven idle sleep

**Status: implemented; hardware current and wake validation pending.**

Replace the fixed 50 ms idle wait with an event/deadline-driven wait. Physical
buttons and the GT911 interrupt already signal a static semaphore, but the main
loop still wakes about 20 times per second while otherwise idle. The replacement
should block until input or the nearest real deadline: touch recovery polling,
frontlight schedule boundary, automatic sleep, connected USB work, or an
activity that explicitly requests another tick.

Expected mechanism: fewer CPU-domain wakeups, fewer idle GT911/I2C checks, and
longer uninterrupted automatic light-sleep intervals. Input interrupts should
remain immediate; this is not polling with a longer perceived input delay.

Ship only if unplugged reader-idle current falls by at least 10% with no missed
short button press, touch, capacitive Home action, or USB command.

## Deliberately excluded: touchscreen power gating

Touchscreen power gating is not part of the CrossDiTo optimization plan. Touch
remains initialized and immediately available in the reader regardless of
whether touch input is currently used. This avoids a mode-specific wake and
reinitialization path and follows the product decision to keep the touchscreen
available.

## Priority 3: compiled, flat EPUB cache pipeline

**Status: layout-neutral compiled event cache implemented; contiguous page-blob
replacement deferred until measurements show page deserialization is still a
material bottleneck.**

Introduce a versioned, layout-neutral intermediate cache for each EPUB spine:
a compact sequential text/token stream with resolved structural/style records.
Layout changes could then repaginate from that stream instead of reopening XML,
reapplying CSS, and rebuilding thousands of small strings.

For layout-specific pages, evaluate a contiguous page-blob format rendered from
offsets into one reusable PSRAM buffer. This would avoid rebuilding a graph of
`PageElement`, `TextBlock`, `shared_ptr`, and string allocations whenever a
cached page is opened, revisited, or reached by a jump.

Expected mechanism: fewer SD operations, less XML/CSS work, sequential PSRAM
access, and much less heap churn during chapter opening and repagination. It
extends the current streaming parser rather than replacing it with a whole-book
DOM.

Ship incrementally, with a cache-format version bump and fallback rebuilding.
Require at least a 20% reduction in representative uncached-chapter repagination
or cached random-page load time, plus equal rendering across multilingual,
image-heavy, table-heavy, RTL, ruby, and malformed EPUB fixtures.

## Priority 4: controller-backed regional UI updates

**Status: removed from the production X4 Pro display stack after hardware
testing found corrupted window contents, displaced page fragments, and
cumulative ghosting. Any future attempt must start as a separate diagnostic
build and pass image-integrity, baseline, latency, and long-reading tests on
each X4 Pro controller variant before production code is changed.**

Add a region-refresh API below `HalDisplay`, implemented and validated separately
for SSD1677 and UC8179 X4 Pro panels. Derive changed regions after rendering
using compact tile hashes or a post-render byte comparison; do not rely only on
drawing-operation bounds because erases and clears must be included.

Use regional updates only for genuinely small UI changes such as a selection,
popup, status icon, or progress indicator. Page turns normally change most of
the panel and should retain the existing page waveform and hard maximum clean
refresh interval.

Expected mechanism: less SPI traffic and, only where the controller genuinely
limits the driven region, shorter visible UI updates. Ship only if both panel
variants show a repeatable latency improvement without added ghosting.

## Priority 5: measured hot-path compiler optimization

**Status: phase timing remains for inflate, XHTML/compiled parsing, and page
serialization/deserialization. The anti-aliasing experiment and its extra
panel timing code were reverted after X4 Pro hardware showed corrupted page
transitions and baseline ghosting. The proven row-band path remains active. No
speculative `-O2` override is enabled until measurements show a remaining
CPU-dominant translation unit.**

Add phase timing for EPUB inflation, XML/CSS processing, width calculation,
line breaking/hyphenation, page serialization, page deserialization, glyph
rasterization, and panel transfer. Apply performance optimization (`-O2`) only
to translation units or functions responsible for a meaningful share of the
measured wait.

Expected mechanism: reduce CPU-bound indexing time and return to light sleep
sooner without spending scarce internal RAM on speculative IRAM placement.
Check firmware size, task stack high-water marks, heap shape, and crash decoding
after every compiler-policy change. Do not globally enable QIO, larger caches,
or IRAM placement as a substitute for profiling.

## Recorded conditional ideas

These ideas remain available for later experiments but are lower priority than
the work above:

- **Further row/word glyph blitting:** CrossDiTo already has a packed 1-bit glyph
  fast path. Extend it only if profiling shows rasterization still accounts for
  at least 10% of page-preparation time. The implementation must handle source
  alignment and PSRAM writes safely; the earlier “two cycles” estimate is not a
  valid target.
- **Supported 40 MHz idle frequency:** test only through `esp_pm_configure`, not
  raw clock registers. Automatic light sleep may make the gain too small to
  justify a new operating point. Frontlight PWM, SD, touch, USB, and wake must
  remain stable.
- **Bounded SD/ZIP read-ahead:** tune an existing inflate or storage buffer only
  if phase traces show repeated small reads as a dominant cost. Do not add a
  persistent 128–256 KB chapter buffer or manually issue SD stop commands.
- **RTC resume hint:** a small validated snapshot may shorten routing on a real
  deep-sleep wake, but it cannot replace settings, fonts, page data, the 48 KB
  framebuffer, or SD caches. It is useful only if it removes a measured wake
  delay beyond the current Quick Resume path.
- **No-op refresh suppression:** use a compact full-frame or tile signature only
  if instrumentation finds duplicate display submissions. Pixel-change totals
  must not replace the hard periodic clean-refresh limit.

## Recorded but blocked mechanisms

The following proposals must not be implemented as originally described:

- **ULP “Ghost Sentinel” during active reading:** EXT1 already wakes directly
  from the X4 Pro's RTC-capable button/touch pins. Deep sleep discards the live
  reader's DRAM/PSRAM and peripheral state, stops ordinary frontlight PWM, and
  resumes through a reset. Revisit only if measured automatic-light-sleep
  current is unacceptable and a full reader-ready wake—not merely wake-stub
  execution—can meet the interaction target.
- **Thermal waveform truncation:** SSD1677 OTP already selects temperature-bound
  waveforms. Custom phase removal requires panel-vendor waveform data and
  characterization for every supported panel batch; temperature alone is not
  sufficient evidence.
- **Frontlight “LC resonance” frequency tuning:** the OEM uses 10 kHz, and no
  verified schematic identifies the LED driver, filter, or claimed resonance.
  Require oscilloscope, lux, board-current, and thermal measurements before
  changing frequency.
- **GT911 `0x8040 = 0x01` doze:** that command requests diagnostic difference/raw
  data; it is not a sleep command. Use the dedicated GPIO2 rail only in a mode
  where touch is intentionally unavailable.
- **Manual SD CMD12 and direct 256 KB PSRAM DMA:** ESP-IDF already handles
  multi-block termination, while X4 Pro SDMMC transfers may require internal
  DMA-capable buffering. Optimize at the storage abstraction after measurement.
- **Blanket USB/GPIO shutdown:** USB host detection already releases its power
  lock when disconnected, and the X4 Pro schematic is incomplete. Never hold or
  repurpose unconfirmed pins to chase an estimated current figure.
