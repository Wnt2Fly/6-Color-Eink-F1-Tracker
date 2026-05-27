# Changelog

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
