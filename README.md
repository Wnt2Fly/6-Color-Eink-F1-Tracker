# F1 Tracker (Large / PhotoPainter)

Firmware for the **Waveshare ESP32-S3 PhotoPainter** board with the **7.3″ six-color e-paper panel** (800×480). It shows Formula 1 calendar context, driver and constructor standings, qualifying grid near race time, results/podium, countdowns, and optional startup/update sounds from SD card.

![Platform](https://img.shields.io/badge/Platform-ESP32--S3-orange) ![Framework](https://img.shields.io/badge/Framework-Arduino-00979D) ![Build](https://img.shields.io/badge/Build-PlatformIO-ffc515)

## Features

- **Race-aware UI** — Next / last race blocks, countdown to lights-out, podium when results exist  
- **Standings** — Top drivers and constructors with team-colored accents  
- **Grid mode** — Starting grid when qualifying is available and the schedule says show-grid  
- **Caching** — HTTP responses cached with configurable TTLs to limit API load  
- **Wi-Fi provisioning** — [WiFiManager](https://github.com/tzapu/WiFiManager) captive portal on first boot  
- **Web configuration** — HTTP server on the device IP for intervals, audio, quiet hours, flags, and more  
- **Audio** — I²S + ES8311 codec; WAV clips from microSD (`sound/` samples can be copied to the card)

Data is fetched from the Ergast-compatible **[jolpi.ca](https://api.jolpi.ca/)** proxy (`api.jolpi.ca/ergast/f1/...`).

## Hardware

This repository targets **Waveshare’s PhotoPainter ESP32-S3 kit** (integrated driver, e-paper, codec, SD slot). Pin definitions in firmware match that board—**do not expect a bare ESP32 + random EPD to work without porting**.

| Function | GPIO (as in `src/main.cpp`) |
|----------|------------------------------|
| EPD CS | 9 |
| EPD DC | 8 |
| EPD RST | 12 |
| EPD BUSY | 13 |
| EPD SCK | 10 |
| EPD MOSI | 11 |
| I²C SDA / SCL | 47 / 48 |
| I²S / PA | See `src/f1_audio.h` (MCLK 14, BCLK 15, LRCK 16, DOUT 17, etc.) |
| SD (dedicated SPI) | CS 38, CLK 39, MISO 40, MOSI 41 |

**Flash:** firmware is built for **16 MB** external flash and PSRAM-enabled S3 variants (see `platformio.ini`).

**Using the device** (e-paper behavior, scheduler, every admin setting): **[USER_GUIDE.md](USER_GUIDE.md)**.

## Prerequisites

1. **[PlatformIO](https://platformio.org/)** — VS Code extension or PlatformIO CLI  
2. **USB cable** — Data-capable, connected to the PhotoPainter USB port  
3. **Drivers** — USB UART/JTAG drivers for your OS (often CP210x or built-in USB-CDC depending on board revision)

## Build and upload (PlatformIO)

From the project root (`F1 Tracker Large`):

```bash
pio run -t upload
```

Serial monitor (115200 baud):

```bash
pio device monitor
```

Or combined:

```bash
pio run -t upload && pio device monitor
```

### Serial port

If upload fails with “could not open port”, set your port in `platformio.ini`:

```ini
upload_port = COM5        ; Windows example
monitor_port = COM5
```

On Linux/macOS use e.g. `/dev/ttyACM0` or `/dev/ttyUSB0`.

## First-time setup

1. **Flash** the firmware (above).  
2. On boot, if no saved Wi-Fi credentials exist, the device opens an access point:  
   - **SSID:** `F1Tracker-Setup`  
   - **Password:** `formula1`  
3. Connect a phone or PC to that AP; open the captive portal or browse to **`http://192.168.4.1`** and choose your network.  
4. After it joins your LAN, note the IP shown on the serial log or e-paper status area when relevant.  
5. Open **`http://<device-ip>/`** in a browser for full configuration (update cadence, audio files, quiet hours, etc.).

### Time zone

Default POSIX TZ string is **`EST5EDT,M3.2.0/2,M11.1.0/2`** (`MY_TZ` in `src/main.cpp`). Change it there if your wall-clock locale differs.

### Sounds (optional)

The codec can play WAV files from the SD card. Example assets live under `sound/` in this repo (boot, loaded, update themes). Copy the WAVs you want onto the card and align filenames with the web UI / preferences.

## Repository layout

| Path | Purpose |
|------|---------|
| `src/main.cpp` | Firmware entry, e-paper driver glue, UI, HTTP/API, web UI |
| `src/f1_fonts.c`, `src/f1_fonts.h` | Embedded Formula 1–style bitmap fonts |
| `src/f1_audio.h` | I²S, ES8311, SD WAV playback |
| `platformio.ini` | Board, flags, library dependencies |
| `sound/` | Sample WAV/MP3 assets for SD |
| `misc/` | Helper scripts (e.g. flag assets), raw/BMP resources |
| `images/` | Conversion tooling / reference bitmaps |

The PlatformIO filter excludes `EPD_7in3e.cpp` if present; the active panel path is integrated with the main sketch.

## Dependencies (managed by PlatformIO)

Declared in `platformio.ini`:

- XPowersLib  
- Adafruit GFX  
- U8g2_for_Adafruit_GFX  
- ArduinoJson 6.x  
- WiFiManager  

## Documentation

- **[USER_GUIDE.md](USER_GUIDE.md)** — How the firmware behaves, what appears on screen, and every web UI setting (`http://<device-ip>/`).
- **[TESTING_GUIDE.md](TESTING_GUIDE.md)** — Manual QA checklist (Wi‑Fi, NTP, scheduler, caching, audio). Some items were written for older hardware variants; serial logs (`[F1]`, `[WiFi]`, `[Sched]`, etc.) remain authoritative on PhotoPainter.

## Troubleshooting

| Symptom | Things to check |
|---------|------------------|
| Upload fails | Correct COM port; USB cable; boot/flash mode per Waveshare docs |
| Blank or corrupt EPD | Power supply while refreshing; BUSY/SPI wiring (fixed on PhotoPainter module) |
| No Wi-Fi after setup | Re-enter portal by erasing flash or clearing WiFiManager storage per ESP-IDF/Arduino prefs docs |
| No sound | SD mounted; WAV format; volume / quiet hours in web UI; `f1_audio.h` pins |
| Stale F1 data | HTTPS/API reachable; cache TTLs in web UI; jolpi.ca availability |

## Credits

- **Timing & standings data** — Ergast-style JSON via [jolpi.ca](https://api.jolpi.ca/)  
- **Graphics stack** — Adafruit GFX + U8g2 bridge  
- **Provisioning** — WiFiManager  

---

**Enjoy the build.**
