# Changelog

## v1.1

### Display

- **Next race** — date and local time in large type in the countdown box (removed separate “LIGHTS OUT” / small date lines).
- **Standings rows** — admin **Display** tab: toggle and **reorder** driver/constructor elements (code badge, team logo, name, flag, bar, points); position `#` always shown.
- **Driver code badges** — TLA boxes (ANT, RUS, …) restored; optional via **Driver code badge** toggle.
- **Driver flags** — fixed column alignment (no stagger when point totals differ in width).
- **Constructor names** — canonical short labels (e.g. Aston Martin, Red Bull); full-size font with improved bar/points spacing.
- **Circuit maps** — auto-rotate tall `.raw` silhouettes; center track in slot between city line and podium.
- **Layout** — constructor bars spaced from 3-digit points; constructor/driver truncation uses correct font metrics.

### Web admin & Wi‑Fi

- **Display** tab — row visibility and left→right order for drivers/grid and constructors.
- **`/health`** — plain-text liveness check (`ok` + IP).
- **Admin reliability** — Wi‑Fi stays up for the web UI; HTTP server restarts cleanly after WiFiManager / reconnect; served during long e-paper refresh and WAV playback.
- **Wi‑Fi save** — hidden-field fix so “Keep Wi‑Fi on” is not cleared accidentally on save.

### Build

- Same PlatformIO `photopainter` target; flash ~1.17 MB.

---

## v1.0

First public release on GitHub.

### Display & data

- 800×480 six-color e-paper UI for [Waveshare ESP32-S3 PhotoPainter](https://www.waveshare.com/wiki/ESP32-S3-PhotoPainter)
- Next / last race, countdown, driver & constructor standings, qualifying grid, podium
- Optional SD assets: flags, circuit maps (incl. Montreal rotation), constructor logos
- Ergast-style API via [jolpi.ca](https://api.jolpi.ca/)
- Phase-based refresh scheduler (configurable in admin UI)

### Web admin (`http://<device-ip>/`)

- Schedule, audio, API cache, Wi‑Fi, SD upload/browse, boot splash, redraw / force refresh / reboot  
- Full reference: [ADMIN_PAGE.md](ADMIN_PAGE.md)

### Audio

- Boot, loaded, and update WAV clips from `/sound/`
- Results celebration (boot/theme WAV once when results first post for a GP)
- Quiet hours and volume control

### Hardware integration

- AXP2101 PMIC (battery %, rail setup) with I²C retry on boot
- ES8311 + microSD
- WiFiManager first-time setup (`F1Tracker-Setup`)

### Docs

- [README.md](README.md), [USER_GUIDE.md](USER_GUIDE.md), [TESTING_GUIDE.md](TESTING_GUIDE.md)

### Build

- PlatformIO (`photopainter` env), ESP32-S3, 16 MB flash, PSRAM
