# CrossDiTo changelog

This changelog contains only changes made by CrossDiTo after it was based on [CrossInk 1.5.0](https://github.com/uxjulia/CrossInk). It does not reproduce or claim CrossInk or [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) features. Refer to those upstream projects for their contributors and release histories.

## [v1.5.1] - 2026-08-19

### Added

- Xteink X4 Pro now has production, debug, simulator, CI, and release-catalog targets.
- EPUB chapters now keep a versioned, layout-neutral compiled event cache so font, margin, orientation, and spacing changes can repaginate without tokenizing the same XHTML again.
- EPUB reader jumps now offer a one-shot Return to Previous Reading Position action without storing a history.
- X4 Pro can automatically switch its frontlight on and off in a user-set daily 15-minute schedule, including overnight windows.
- EPUB indexing can paginate an entire book for the active layout and cache exact whole-book page offsets.
- Reader status bars can show either chapter-relative pages or the current page within the fully paginated book.

### Changed

- Firmware identity is now CrossDiTo 1.5.1 while retaining a visible reference to its CrossInk 1.5.0 upstream base.
- Lyra Carousel snapshots are keyed by book order, covers, and layout rather than reading progress, and the selected book is redrawn live so progress stays current without rebuilding every cached frame after leaving a book.
- Idle reading now releases the 240 MHz CPU lock after 300 ms, uses tickless automatic light sleep between input events, and powers down the ESP32-S3 CPU domain while idle; button and touch interrupts wake the main loop immediately.
- X4 Pro frontlight levels use a perceptual curve that reduces LED duty at low and medium settings.
- Reader resume no longer loads recent books, KOReader credentials, or validates a dictionary before showing the first page; each is loaded only when first used.
- EPUB reading retains one already-deserialized next page when memory permits, avoiding the next page's SD read and object reconstruction without adding another speculative page allocation.
- Fully visible 1-bit glyphs now write directly to the packed framebuffer, avoiding per-pixel clipping and rotation work while keeping the existing renderer as the fallback.
- Production startup no longer waits 250 ms for USB serial enumeration; file transfer starts after the same window without blocking the boot path, while crash logs remain in RTC memory.
- X4 Pro builds pin the matching DIO/Octal-PSRAM SDK, keep the e-ink bus at its controller's 20 MHz limit, and preserve usable internal heap instead of reserving 24 KB that the application cannot use.
- CrossDiTo now uses a minimal monochrome owl mark on its boot and sleep screens and across its web branding.
- Full Book EPUB pagination now runs cooperatively after the current page appears, yielding between pages for reader input and closing SD handles between background chunks.
- The firmware is now named CrossDiTo and ships only for Xteink X4 Pro. Existing `/.crosspoint/` data and legacy build symbols remain compatible.
- Build, CI, release, OTA, documentation, and web-optimizer device selection now expose only the X4 Pro target.
- Long e-ink refresh waits now yield a shared SPI bus and temporarily use the lower idle CPU frequency when Wi-Fi is off, reducing SD-card stalls and power use without slowing drawing work.
- Activity navigation uses fixed-capacity ownership and image dithering uses contiguous reusable workspaces, reducing heap fragmentation during long reading sessions.
- Startup and render-task diagnostics now report framebuffer memory and the lowest observed render-task stack headroom.

### Fixed

- X4 Pro EPUB page turns now use the verified pre-roadmap row-band composition and original controller start/finish sequence; the experimental controller strip and rectangular-update paths were removed after hardware testing showed cumulative displaced-page fragments and ghosting.
- Returning Home from a grayscale reader now performs one clean refresh, clearing reader text residue without slowing later Home interactions.
- X4 Pro UI changes now stay on the controller-safe full-frame update path, with the unused regional hash table and native-window dispatch removed.
- Returning from a book no longer shows the Home carousel loading bar merely because reading progress or statistics changed. The first run after this cache-format change performs one expected rebuild.
- X4 Pro frontlight PWM now remains continuous through automatic light sleep, preventing the light from flashing after the reader becomes idle.
- Lyra Carousel swipes now advance exactly one book instead of sometimes selecting a side cover on touch-down and advancing again on release.
- X4 Pro diagnostic DIO builds now pin the matching Arduino/ESP-IDF library variant, preventing cached QIO and automatic power settings from leaking into an otherwise safe image.
- X4 Pro touch wake now follows the GT911's active-low interrupt and retains a slow recovery poll, avoiding constant idle I2C traffic without losing a tap if an edge is missed.
- Quick Resume now restores the current EPUB page in memory after silent chapter indexing, preventing a stale or blank page on the next partial refresh.
- EPUBs with unusually large manifests now build their item lookup in bounded chunks and verify full item IDs, avoiding arena exhaustion and hash-collision mix-ups.
- Touch-down actions no longer leak the same finger lift into the next screen; touch context menus, keyboard redraws, and the X4 Pro Home-key hold remain reliable across activity changes and reader touch settings.
- Long-press Power dark-mode shortcuts now toggle only once, dictionary word selection remains visible in dark mode, and theme changes are synchronized with rendering.
- KOReader Sync and OPDS use the reader-safe render stack, while Nearby Stats Sync now handles reconnect timing, duplicate packets, and mismatched acknowledgements safely.
- Full Book indexing no longer leaves a blank screen during its pass or an unsafe render-task/cache state that could reboot the X4 Pro when reopening the book.
- Wi-Fi scans now finish drawing their status screen before starting the radio and publish results under the render lock, preventing X4 Pro scan-time panics; crash reports now retain Xtensa panic addresses for diagnosis.
- EPUB status bars now keep unfinished chapter pagination stable as `current/…` and show the total only when it is exact, instead of displaying a changing `~current/total` estimate.
- Explicit reader jumps no longer inherit a stale relayout target after changing reader settings from the same menu.
- Display operations are serialized with storage access on shared-bus devices, while X4 Pro SDMMC access remains independent.
- Startup, activity, image, XTC, and network-server allocation failures now return a logged error instead of aborting the firmware.
- X4 Pro simulator input, frontlight, edge-button, touch, board-name, and device-detection behavior now matches the hardware profile.
- Release and release-candidate builds now embed one CrossDiTo version and emit only the X4 Pro artifact.
