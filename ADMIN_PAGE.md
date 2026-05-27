# Admin page — complete reference

The device runs a small **HTTP server on port 80** while Wi‑Fi is connected. Open:

**`http://<device-ip>/`**

The IP appears in the serial log at boot, in the **footer** of the e‑paper display (when connected), and at the top of the settings page.

**Requirements:** Phone or PC on the **same LAN** as the tracker. If **Keep Wi‑Fi on** is disabled, the radio may be off between scheduled refreshes — you cannot open this page until the next reconnect (or after a manual refresh forces Wi‑Fi on).

---

## Page layout

Five tabs (JavaScript switches visibility; no page reload):

| Tab | What it configures |
|-----|-------------------|
| **Schedule** | When the display fetches data and refreshes |
| **Audio & time** | WAV clips, volume, quiet hours, 12h/24h clock |
| **API cache** | How long JSON responses are reused |
| **Wi‑Fi** | Network, power-saving radio behavior |
| **SD & system** | Files on microSD, boot splash, manual display actions |

### Buttons that save or act

| Button / action | HTTP | What it does |
|-----------------|------|----------------|
| **Save all settings** (top + Wi‑Fi tab) | `POST /save` | Saves **Schedule**, **Audio & time** (incl. quiet hours + clock), **API cache**, **Wi‑Fi** (incl. `wifiAlways`). Also saves **boot splash** if that checkbox is in the main form. Redirects back to `/`. |
| **Apply sound settings now** | `POST /save_sounds` | Saves **volume**, sound checkboxes, WAV filenames, quiet hours only. Use when you changed audio without touching other tabs. |
| **Upload to SD** | `POST /upload` | Multipart file upload (see SD tab). |
| **Create folders** | `POST /mkdir` | Creates directories on SD. |
| **Browse and delete** | `GET /sd` | Separate file browser (see below). |
| **Redraw screen (no data update)** | `POST /refresh_screen` | Redraws e‑paper from **cached** data in RAM (no API fetch). |
| **Force display refresh** | `POST /refresh` | Clears API cache, downloads fresh data, full redraw (like a scheduled update). |
| **Reboot device** | `POST /reboot` | Restarts the ESP32-S3 (brief disconnect from this page). |

**Checkboxes:** Unchecked boxes are **not** sent in the form — saving turns that option **off**.

Settings are stored in flash under Preferences namespace **`f1tracker`** (survive reboot). WiFiManager also keeps STA credentials separately for `autoConnect` on boot.

---

## How the scheduler uses Schedule settings

All timing is measured from **next race lights‑out** (UTC from API, shown in local time).

```
Hours before lights-out
│
│  FAR phase (> phaseMid h)
│     └── refresh at :00 every hrFar hours
│
│  MID phase (phaseGrid h … phaseMid h)
│     └── refresh at :00 every hrNear hours
│
│  FINAL phase (< phaseGrid h)
│     └── refresh every updGrid minutes (grid hunt)
│
│  RACE WINDOW (raceBefH before … raceAftH after lights-out)
│     └── refresh every updRace minutes
```

**Display logic** (separate from refresh rate):

- **Starting grid** on screen when next race is within **gridShowH** hours and qualifying exists.
- **RACE IN PROGRESS** banner for **raceProgH** hours after lights-out.
- **Podium** hidden during that in‑progress window; shown when results exist afterward.

Footer **Next Update: …** shows the next scheduled refresh time from these rules.

---

## Tab: Schedule

### Refresh intervals

| UI label | Form name | Prefs key | Range | Default | Description |
|----------|-----------|-----------|-------|---------|-------------|
| Race weekend window — every N minutes | `updRace` | `updRace` | 1–1440 | **30** | Minutes between refreshes while inside the **race window** (see below). |
| Final phase (&lt; N h before race) — every N minutes | `updGrid` | `updGrid` | 1–1440 | **30** | Minutes between refreshes in **final phase** (aggressive polling for qualifying/grid). |
| Mid phase — at :00 every N hours | `hrNear` | `hrNear` | 1–24 | **2** | In **mid phase**, refresh only when local minute is **:00** and hour is divisible by this value. |
| Far phase — at :00 every N hours | `hrFar` | `hrFar` | 1–24 | **6** | In **far phase**, same :00 alignment but every N hours (e.g. 6 → 00:00, 06:00, 12:00, 18:00). |

**Special case:** Once qualifying is known available for the next round, final-phase polling may **ease** to the mid-phase hourly pattern until the race window starts (firmware optimization).

### Race window timing (hours)

Defines the **race window** used by `updRace` (high-frequency refresh).

| UI label | Form name | Prefs key | Range | Default | Description |
|----------|-----------|-----------|-------|---------|-------------|
| Race “in progress” grace after lights out | `raceProgH` | `raceProgH` | 1–48 | **4** | After lights-out, treat session as live this many hours (banner, podium rules). |
| Hours before lights out counted as race window | `raceBefH` | `raceBefH` | 1–72 | **6** | Start race-window refreshes this many hours **before** lights-out. |
| Hours after lights out counted as race window | `raceAftH` | `raceAftH` | 1–72 | **6** | Continue race-window refreshes this many hours **after** lights-out. |
| Show starting grid when race is within N hours | `gridShowH` | `gridShowH` | 1–168 | **18** | Switch driver column to **STARTING GRID** when within N hours of next race and qualifying data exists. |

### Schedule phase boundaries (hours before lights out)

| UI label | Form name | Prefs key | Range | Default | Description |
|----------|-----------|-----------|-------|---------|-------------|
| Start “final phase” at N hours | `phaseGrid` | `phaseGrid` | 1–167 | **24** | Below this many hours to lights-out → **final phase** (`updGrid`). |
| End “far phase” at N hours | `phaseMid` | `phaseMid` | 2–168 | **48** | Above this many hours → **far phase** (`hrFar`). Between `phaseGrid` and `phaseMid` → **mid phase** (`hrNear`). Must be **greater than** `phaseGrid` (else firmware resets to 24 / 48). |

---

## Tab: Audio & time

### Volume

| UI label | Form name | Prefs key | Range | Default | Description |
|----------|-----------|-----------|-------|---------|-------------|
| Volume 0–100 (%) | `vol` | `vol` | 0–100 | **60** | ES8311 DAC digital volume. **0 = mute.** Applied on save / apply sounds. |

### Sound clips

Each clip has an **enable checkbox** and a **WAV dropdown** (files found under `/sound/` on SD). Paths are stored relative to `/sound/` (e.g. `boot.wav`, `themes/f1.wav`).

| UI label | Enable key | File key | Default file | When it plays |
|----------|------------|----------|----------------|---------------|
| **Boot** — after Wi‑Fi connects | `soundBoot` | `soundBootFile` | `boot.wav` | Once per boot, after WiFiManager connects. |
| **Loaded** — after first screen draw | `soundLoaded` | `soundLoadedFile` | `loaded.wav` | After first successful full e‑paper draw in `setup()`. |
| **Update** — after refresh / redraw | `soundUpdate` | `soundUpdateFile` | `update.wav` | After scheduled refresh, **Force display refresh**, or **Redraw screen** (if enabled). **Skipped** if results celebration plays that cycle. |

**Results celebration (not a separate checkbox):** When official **race results first appear** for the current completed GP, firmware plays the **same WAV as Boot** once per round. Remembered in prefs **`celebrRound`**. Blocked for ~22 seconds after boot so it does not double with startup boot WAV.

**Apply sound settings now** — Use this if you only changed audio; checkboxes alone do not apply until you submit.

### Quiet hours

| UI label | Form name | Prefs key | Default | Description |
|----------|-----------|-----------|---------|-------------|
| Silence WAV clips during this window | `quietEn` | `quietEn` | off | When enabled, all WAV playback suppressed in local time. |
| Start hour (0–23) | `quietSh` | `quietSh` | **22** | Inclusive start hour. |
| End hour (0–23, exclusive) | `quietEh` | `quietEh` | **7** | Exclusive end. Example: 22 → 7 = 22:00–06:59 quiet. Same start/end = no quiet time. |

### Clock

| UI label | Form name | Prefs key | Default | Description |
|----------|-----------|-----------|---------|-------------|
| 24-hour time | `clock24h` | `clock24h` | off (12h) | Header date/time and next-race “lights out” line use 24h or 12h am/pm. |

---

## Tab: API cache

All values are **minutes**. Shorter = fresher data, more HTTP traffic and power use.

| UI label | Form name | Prefs key | Range | Default | Description |
|----------|-----------|-----------|-------|---------|-------------|
| Calendar JSON | `cacheCal` | `cacheCal` | 1–10080 | **60** | Reuse full season calendar response. |
| Driver & constructor standings | `cacheStand` | `cacheStand` | 1–10080 | **60** | Reuse standings JSON; also used when reusing cached results probes. |
| Qualifying JSON | `cacheQual` | `cacheQual` | 1–43200 | **1440** (24 h) | Reuse qualifying payload once downloaded. |
| Remember qualifying availability probe | `ttlQualAv` | `ttlQualAv` | 1–10080 | **720** (12 h) | How long to remember “qualifying exists / not” without re-probing. Affects scheduler backoff when grid is found. |
| Remember results availability probe | `ttlRes` | `ttlRes` | 1–1440 | **5** | How long to remember “results posted / not” without re-probing. |

**Force display refresh** clears in-memory caches and fetches fresh data regardless of TTL.

---

## Tab: Wi‑Fi

| UI label | Form name | Prefs key | Description |
|----------|-----------|-----------|-------------|
| **Apply new network below** | `wifiApply` | — | Must be **checked** when you submit **Save all settings** for SSID/password to take effect. |
| SSID | `wifiSsid` | `wifiSsid` | Max 32 characters. Shown pre-filled from saved pref or current `WiFi.SSID()`. |
| Password | `wifiPass` | `wifiKey` | Max 63 characters. **Leave blank** to keep the previously saved password. Only stored when you type a new password. |
| Keep Wi‑Fi on for this page | `wifiAlways` | `wifiAlways` | Default **on**. When **off**, firmware calls `WiFi.disconnect` + `WIFI_OFF` after each refresh — **admin page unreachable** until next scheduled reconnect. Saves power. |

Changing network may change the device IP — use the new address on the display footer or serial log.

**First-time Wi‑Fi** (no saved credentials) uses WiFiManager AP **`F1Tracker-Setup`** / **`formula1`** — not this form. See [USER_GUIDE.md](USER_GUIDE.md).

---

## Tab: SD & system

### Upload to SD card

Only shown when SD is mounted at boot.

| Field | Description |
|-------|-------------|
| **Destination folder** | `sound`, `flags`, `tracks`, `logos`, or **card root** (root requires subpath). |
| **Subfolder** | Optional nested path (safe characters only). |
| **Files** | One or more files; max **12 MB** each. Multi-select: Ctrl/Cmd+click. |

| Folder | Typical content |
|--------|-----------------|
| `sound` | `.wav` clips for admin dropdowns |
| `flags` | `XX.raw` nationality flags (ISO code) |
| `tracks` | `<circuit>.raw` silhouettes |
| `logos` | `<team>.raw` constructor badges |

**Filename rules:** letters, digits, `.`, `-`, `_` only.

### Create folder on SD

| Field | Description |
|-------|-------------|
| **Base** | Same bases as upload, or **card root**. |
| **Subpath** | For standard bases, optional (empty = ensure base folder exists). For **card root**, subpath **required** (e.g. `backup` → `/backup`). |

Path segments: letters, digits, `-`, `_` only; no `..`.

### Browse and delete (`/sd`)

Link opens **`/sd`** (and `?p=/path` for subfolders).

| Feature | Behavior |
|---------|----------|
| Navigation | Click folder names to descend; **↑ Up**, **Top**, **Settings** links. |
| Delete file | **Delete** button per file — removes that file. |
| Delete folder | Only **empty** directories. |
| Protected | Cannot delete top-level `/sound`, `/flags`, `/tracks`, `/logos` from browser. |

### Startup

| UI label | Form name | Prefs key | Default | Description |
|----------|-----------|-----------|---------|-------------|
| Show F1 boot splash on power-up | `bootSplash` | `bootSplash` | **on** | Full-screen F1 logo + “It’s lights out and away we go!” before Wi‑Fi setup. Saved with **Save all settings** (checkbox is attached to main form via `form=mainSave`). |

### System actions

| Button | Description |
|--------|-------------|
| **Redraw screen (no data update)** | Re-renders the framebuffer from **data already in memory** (no HTTP). NTP may run if Wi‑Fi is up. Plays **Update** sound if enabled. Use after layout tweaks or to refresh clock without API load. |
| **Force display refresh** | Invalidates API caches, runs `FetchCalendar` + standings/grid/results logic, full e‑paper refresh. Requires Wi‑Fi. Plays **Update** sound unless results celebration fires. |
| **Reboot device** | `ESP.restart()` — full reboot; reconnect to new IP if DHCP changes. |

---

## Quick defaults summary (factory / first flash)

| Area | Default |
|------|---------|
| Refresh race window | 30 min |
| Refresh final phase | 30 min |
| Mid / far hourly | every 2 h / every 6 h at :00 |
| Phase boundaries | 24 h / 48 h before lights-out |
| Race window | 6 h before, 6 h after; in-progress 4 h |
| Grid on screen | within 18 h |
| Wi‑Fi always on | **yes** |
| Boot splash | **yes** |
| Sounds | boot, loaded, update **on** |
| Volume | 60% |
| Quiet hours | off |
| 24h clock | off (12h) |
| API cache | 60 / 60 / 1440 min; qual avail 720 min; results avail 5 min |

---

See also: [USER_GUIDE.md](USER_GUIDE.md) (display modes, boot flow), [README.md](README.md) (hardware & build).
