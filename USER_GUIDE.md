# F1 Tracker Large — User guide

This document explains **how the device behaves**, **what appears on the e-paper**, and **every option on the web settings page** (`http://<device-ip>/`).

---

## Requirements for daily use

- **Wi‑Fi** connected to your home network (first-time setup uses the captive portal; see README).
- **Internet access** so the device can reach **HTTPS `api.jolpi.ca`** (Ergast-style F1 JSON).
- **Accurate time** via **NTP** after boot — needed for race countdown and for the refresh scheduler alignments.

Optional but recommended:

- A **microSD card** mounted at boot for WAV sounds, driver **nationality flags**, **circuit silhouettes**, and **constructor logos**.

---

## Where to change behavior

| Channel | What it controls |
|---------|-------------------|
| **`http://<device-ip>/`** | Refresh intervals, caches, Wi‑Fi, audio, boot splash, SD uploads, manual redraw/reboot |
| **Captive portal** (`192.168.4.1` during setup AP `F1Tracker-Setup` / `formula1`) | Initial SSID/password when no credentials stored |
| **Firmware rebuild only** | POSIX timezone string `MY_TZ` in `src/main.cpp` (defaults to US Eastern) |

### Important: Wi‑Fi “always on”

If **Keep Wi‑Fi on for this page** is **unchecked** (saved settings):

- After each scheduled refresh cycle the radio may **turn off** to save power.
- While Wi‑Fi is off you **cannot** open the settings page until the radio reconnects (next refresh cycle or reboot).

Leave Wi‑Fi always on if you want constant access to the admin UI.

---

## Boot sequence (what happens when you power on)

1. **PMIC / audio / SD** initialize; embedded fonts load from flash.
2. **Boot splash** (optional): F1 quote + logo on the display (`Startup` → boot splash checkbox).
3. **Wi‑Fi**: connects using saved credentials, or opens the setup portal (`F1Tracker-Setup`).
4. **Config HTTP server** starts on port **80** once STA has an IP — bookmark **`http://<ip>/`**.
5. **Boot sound** (optional): plays selected WAV from `/sound/` on SD **after** Wi‑Fi connects (typically the theme — it also triggers **once** when results first land for that GP, unless update chime overlaps that refresh).
6. **NTP**: waits until local time is valid (during this wait you can still hit the web UI).
7. First **full render**: loads calendar from API (unless caches apply), draws layout, refreshes e-paper.
8. **Loaded sound** (optional): plays after that first successful frame draw.
9. Depending on settings, **Wi‑Fi may disconnect** until the next refresh slot.

---

## How the software works (runtime overview)

```
Boot → Wi-Fi + web UI → NTP time sync
        ↓
Main loop (about once per minute wall clock):
  Serve HTTP (/ handles settings forms)
  If scheduled refresh fires → connect Wi-Fi → fetch calendar (+ standings/grid/results probes as needed)
  → compose framebuffer → e-paper full refresh → optional update WAV → maybe disconnect Wi-Fi
```

**Calendar fetch** picks **last completed race** and **next future race** from the season JSON.

**Standings** come from separate endpoints (drivers / constructors); caches reduce bandwidth.

**Qualifying “availability”** is probed (light JSON checks); once qualifying exists for a round, grid rows load from qualifying JSON.

**Results / podium** use results JSON for the **last** round when available.

Manual actions from the **SD & system** tab:

- **Redraw screen (no data update)** — same cached JSON already in RAM; may sync time from NTP if Wi‑Fi is up.
- **Force display refresh** — clears API caches, re-downloads data like a scheduled refresh.

---

## E-paper layout (800 × 480)

Rough mental model:

- **Header (black bar)** — date/time (respects **24‑hour clock** setting), device IP, optional battery gauge (PMIC), and **next scheduled refresh** text near the footer line.
- **Column 1 (narrow)** — **NEXT RACE** block (name, venue shorthand, countdown box), **LAST RACE**, optional **track map**, **podium** (TLAs).
- **Column 2** — Either **DRIVERS** championship table **or** **STARTING GRID** (qualifying order).
- **Column 3** — **CONSTRUCTORS** with colored bars and optional logos from SD.

The framebuffer is rotated 180° before sending to the panel driver — placement matches physical mounting.

---

## Display modes (what you see when)

### Next race block

- **Normal countdown**: shows local race date/time and days/hours until lights-out (`NOW` when race moment arrived).
- **Race in progress**: red banner **RACE IN PROGRESS** replaces normal countdown copy during the configured grace window **after** lights-out.

### Drivers column: standings vs starting grid

| Situation | Column header | Content |
|----------|----------------|--------|
| **Before next race**, within **N hours** of lights-out (your **Show starting grid when race is within N hours**), and qualifying exists | STARTING GRID | Positions from qualifying JSON |
| **After last race start**, results **not** posted yet, within **race-in-progress grace hours**, qualifying exists | STARTING GRID | Same grid for the round **currently underway** |
| Otherwise | DRIVERS | Championship standings |

So grid replaces standings close to the race until official results land.

### Left panel track graphic

If SD holds **`/tracks/<circuit>.raw`** matching Ergast’s **circuitId** (see firmware mapping), a monochrome silhouette appears under **LAST RACE**.

### Flags beside drivers

If SD holds **`/flags/<ISO>.raw`** for the driver’s nationality (two-letter ISO, lowercase filename convention used by generator tooling), a small palette bitmap draws beside the name. Missing flag falls back to a colored badge-only accent.

### Constructor logos

Rows reserve space for **`/logos/<mapped-name>.raw`** (same packed RAW header format as flags — see `src/logo_reader.h` for constructor → filename mapping). If the file is missing, only team colors fill the badge region.

### Podium (LAST RACE)

When results JSON lists **at least three** classified finishes **and** the device does **not** consider itself mid‑race (outside grace window), **TOP‑3 driver TLAs** appear with constructor-derived coloring.

---

## Refresh scheduler (how often + why)

The device aligns refreshes to the **wall clock minute**. Rough phases measured **relative to next race lights‑out**:

### Inside **race window** (hours **before** and **after** lights‑out)

- Controlled separately:
  - **Hours before lights out counted as race window**
  - **Hours after lights out counted as race window**
  - **Race “in progress” grace after lights out** — how long **Race in progress** / podium-blocking logic treats the session as live once lights-out passed.

While inside this window, refreshes occur **every N minutes** (**Race weekend window — every N minutes**).

### **Final phase** (between lights‑out and **Start “final phase” at N hours before**)

- Refreshes every **Final phase — every N minutes** (“Grid lookup…” setting).

Scheduler shortcut once qualifying **is definitely published**:

- If qualifying availability cache says “present”, polls drop back to the **hourly mid-phase** pattern until next boundary logic applies.

### **Mid phase** (between **final phase start N hours** and **far phase ends at N hours**)

- Refreshes only when **minute == 0** and **hour divisible by “Mid phase — every N hours”**.

### **Far phase** (more than **N hours** before lights‑out, beyond mid boundary)

- Same idea but uses **Far phase — every N hours** at **:00**.

### No known next race yet

- Falls back to hourly-style ticks using **mid** interval alignment.

The footer **Next Update: …** previews the next computed slot.

---

## Web settings page — tabs

Open **`http://<device-ip>/`** in a browser on the same LAN. Use the tab buttons across the top.

### Global save behavior

- **`Save all settings`** (top of page, POST `/save`) persists everything **except** boot splash is grouped with the main form but stored when you save from that flow — the UI notes boot splash submits with **Save all settings**.
- **`Apply sound settings now`** (POST `/save_sounds`) writes **volume**, clip toggles, WAV filenames, and **quiet hours** immediately without requiring full-tab iteration.

---

### Tab: **Schedule**

| Control | Meaning |
|---------|---------|
| **Race weekend window — every N minutes** (`updRace`, 1–1440) | Refresh cadence while inside **race window** before/after lights-out (see race window hours below). |
| **Final phase (< N h before race) — every N minutes** (`updGrid`, 1–1440) | More frequent polls when within **phaseGrid** hours of lights-out — hunting qualifying/grid data. |
| **Mid phase — at :00 every N hours** (`hrNear`, 1–24) | Hourly-aligned cadence from **phaseGrid** away until **phaseMid** boundary. |
| **Far phase — at :00 every N hours** (`hrFar`, 1–24) | Same style **beyond phaseMid** hours before lights-out. |
| **Race “in progress” grace after lights out** (`raceProgH`, 1–48 h) | Hours after lights-out treated as “race still live” for UI / podium logic. |
| **Hours before lights out counted as race window** (`raceBefH`, 1–72) | Expands high-frequency refresh window **before** the race. |
| **Hours after lights out counted as race window** (`raceAftH`, 1–72) | Same **after** lights-out. |
| **Show starting grid when race is within N hours** (`gridShowH`, 1–168 h) | Switches driver column to qualifying grid near next race if qualifying exists. |
| **Start “final phase” at N hours** (`phaseGrid`, 1–167) | Beginning of aggressive grid polling window before lights-out. |
| **End “far phase” at N hours** (`phaseMid`, 2–168, must exceed phaseGrid) | Beyond this offset from lights-out, **far** hourly cadence applies. |

---

### Tab: **Audio & time**

| Control | Meaning |
|---------|---------|
| **Volume 0–100** (`vol`) | DAC digital volume; **0 = mute**. Applies immediately after save / apply sounds. |
| **Boot** + WAV dropdown (`soundBoot`, `soundBootFile`) | Play WAV **after Wi‑Fi connects** during boot. **Same clip** plays **once when official race results first appear** for the current completed round (`celebrRound` in prefs remembers so it doesn’t repeat). That celebration is blocked for ~22s after boot so it doesn’t double with the startup boot WAV. |
| **Loaded** + WAV (`soundLoaded`, `soundLoadedFile`) | Play once **after first full successful frame draw** following setup. |
| **Update** + WAV (`soundUpdate`, `soundUpdateFile`) | Play after **scheduled refresh**, **Force display refresh**, or **Redraw screen** when enabled. |
| **Quiet hours** (`quietEn`, `quietSh`, `quietEh`) | Suppress WAV playback during local-hour window (same TZ as status clock). Same calendar day: `[start, end)`; overnight supported when start \> end. |
| **24-hour time** (`clock24h`) | Header + next-race box time formatting. |

Dropdown lists scan **`/sound/`** on SD for `.wav` files (nested folders supported). Only `.wav` paths passing sanity checks are accepted.

---

### Tab: **API cache**

Values are **minutes** stored then converted internally.

| Control | Meaning |
|---------|---------|
| **Calendar JSON** (`cacheCal`) | TTL for full races calendar snapshot. |
| **Driver & constructor standings** (`cacheStand`) | TTL for standings blobs (also bounds cached reuse when probing results). |
| **Qualifying JSON** (`cacheQual`) | TTL for qualifying payload reuse once fetched. |
| **Remember qualifying availability probe** (`ttlQualAv`) | How long to trust “qualifying exists / absent” without hitting probe endpoints again — influences scheduler easing once grid is known. |
| **Remember results availability probe** (`ttlRes`) | Same idea for “results posted?” probes so podium polling backs off appropriately. |

Shorter TTL = fresher data, more API traffic.

---

### Tab: **Wi‑Fi**

| Control | Meaning |
|---------|---------|
| **Apply new network below** | Checkbox gate — must be checked on submit to apply SSID/password changes. |
| **SSID / Password** | STA credentials; blank password keeps previously saved password. |
| **Keep Wi‑Fi on for this page** (`wifiAlways`) | When **off**, firmware disconnects Wi‑Fi after idle work — **web UI unreachable until next reconnect**. |

---

### Tab: **SD & system**

#### Upload / folders / browse

- **Upload to SD** — multipart upload into **`sound`**, **`flags`**, **`tracks`**, **`logos`**, or **card root** (requires safe subpath). Max **12 MB per file**. Filenames: alphanumeric plus `_ - .`.
- **Create folder on SD** — Creates nested paths under chosen base or absolute segments under `/`.
- **Browse and delete** (`/sd`) — Lists safe paths; delete files or **empty** folders. Top-level app folders `/sound`, `/flags`, `/tracks`, `/logos` cannot be deleted from UI.

#### Startup

| Control | Meaning |
|---------|---------|
| **Show F1 boot splash on power-up** (`bootSplash`) | Quote + logo frame before Wi‑Fi setup continues. |

Requires **Save all settings** from main form.

#### System actions

| Button | Endpoint | Effect |
|--------|----------|--------|
| **Redraw screen (no data update)** | POST `/refresh_screen` | Renders from existing memory caches **without** HTTP JSON refresh; still allows NTP if Wi‑Fi connected. |
| **Force display refresh** | POST `/refresh` | Clears caches, hits APIs like scheduled refresh, redraws panel. Needs Wi‑Fi. |
| **Reboot device** | POST `/reboot` | Soft restart ESP32-S3. |

---

## MicroSD asset cheat sheet

Place files on FAT-formatted SD (typical layout):

| Path pattern | Used for |
|--------------|----------|
| `/sound/*.wav` | Boot / loaded / update clips referenced by admin dropdowns |
| `/flags/XX.raw` | Driver nationality bitmaps (`XX` = ISO country code) |
| `/tracks/<name>.raw` | Circuit silhouette (`track_reader.h` maps Ergast `circuitId` → basename) |
| `/logos/<name>.raw` | Constructor badge artwork (`logo_reader.h` maps constructor id → basename) |

RAW bitmap conventions differ slightly **track** (fixed-width bitmap rows) vs **logo/flag** (16-bit LE width/height header + 4bpp packed); use repo tooling under `misc/` / `images/` when generating assets.

---

## Data source & privacy

- Public **REST JSON** only — no login shipped in firmware.
- Device acts as HTTP client; **no cloud account** is created by this project.
- Wi‑Fi credentials live in ESP flash (`Preferences` namespace `f1tracker`).

---

## If something looks wrong

- Confirm **time synced** (wrong TZ → wrong countdown & scheduler).
- Hit **Force display refresh** once when standings/grid seem stale.
- Check SD mount if **sound**, **flags**, or **tracks** never appear.
- Serial log **115200 baud** prints Wi‑Fi, HTTP, calendar summary lines (`[CAL]`, `[HTTP]`, `[Sched]`, `[Redraw]`, `[Force]`).

### Windows USB connect/disconnect sounds repeating

On ESP32‑S3 boards with **native USB** (this project uses `ARDUINO_USB_CDC_ON_BOOT`), each **full chip reset** drops and re‑enumerates the USB device — Windows plays the USB “ding” every time. Hearing that **over and over** almost always means the MCU is **resetting in a loop** (or the cable/port is marginal), not benign “sleep”.

**Check the next boot line** on serial: **`[RST] Reset reason=… (…)`**:

| Reason | Likely cause |
|--------|----------------|
| **`BROWNOUT`** | Brief undervoltage — e‑paper refresh + radio + audio spiking on a **weak USB port/cable**. Try a short data cable, powered hub, or monitor power (PhotoPainter + PMIC). |
| **`PANIC` / `TASK_WDT` / `INT_WDT`** | Crash or watchdog — grab the **full serial backtrace** above the reset; report it or open an issue with that log. |
| **`SW_RESET`** | Deliberate **`ESP.restart()`** (only from **Reboot device** in the web UI or rare failure paths like Wi‑Fi setup giving up). |

**Also try:** leave **Keep Wi‑Fi on** enabled overnight (less radio power cycling — some setups are touchy with USB+Wi‑Fi). In Windows **Device Manager** → USB hubs → **disable “Allow the computer to turn off this device…”** (USB selective suspend) so the host isn’t constantly powering the port.

For systematic QA, see **TESTING_GUIDE.md**.
