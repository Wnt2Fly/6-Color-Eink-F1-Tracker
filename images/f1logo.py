#!/usr/bin/env python3
"""
F1 Team Logo Downloader & 6-Color E-Paper Converter
Waveshare ESP32-S3 PhotoPainter (800x480, 6-color E6 panel)

Colors: Black, White, Red, Yellow, Blue, Green
Output: 24-bit BMP files ready for SD card → /logos/ folder
"""

import os
import sys
import struct
import requests
from PIL import Image

# ── Output config ─────────────────────────────────────────────────────────────
OUTPUT_DIR = "logos"
LOGO_W = 120   # pixels wide
LOGO_H = 40    # pixels tall

# ── The 6 EPD colors (RGB) ────────────────────────────────────────────────────
EPD_PALETTE = [
    (0,   0,   0  ),  # 0 = BLACK
    (255, 255, 255),  # 1 = WHITE
    (255, 215, 0  ),  # 2 = YELLOW  (closest to panel yellow)
    (228, 0,   27 ),  # 3 = RED
    (0,   0,   0  ),  # 4 = unused (maps to black)
    (0,   80,  200),  # 5 = BLUE
    (0,   160, 60 ),  # 6 = GREEN
]

# Palette as flat list for PIL
PIL_PALETTE_FLAT = []
for c in EPD_PALETTE:
    PIL_PALETTE_FLAT.extend(c)
# Pad to 256 colors
PIL_PALETTE_FLAT.extend([0] * (256 - len(EPD_PALETTE)) * 3)

# ── Team logo sources ─────────────────────────────────────────────────────────
# Primary: F1 official CDN, fallback: Wikipedia SVG → PNG render via Wikimedia
TEAMS = {
    "mercedes":    {
        "url": "https://media.formula1.com/image/upload/c_fit,h_64/q_auto/v1740000001/common/f1/2025/mercedes/2025mercedeslogolight.webp",
        "fallback": "https://upload.wikimedia.org/wikipedia/en/thumb/f/f0/Mercedes_AMG_Petronas_F1_Logo.svg/200px-Mercedes_AMG_Petronas_F1_Logo.svg.png",
        "color": (0, 210, 190),
        "epd": 6,   # GREEN (closest to teal)
    },
    "ferrari":     {
        "url": "https://media.formula1.com/image/upload/c_fit,h_64/q_auto/v1740000001/common/f1/2025/ferrari/2025ferrarilogolight.webp",
        "fallback": "https://upload.wikimedia.org/wikipedia/en/thumb/d/d4/Ferrari_logo_%28emblem%29.svg/200px-Ferrari_logo_%28emblem%29.svg.png",
        "color": (220, 0, 0),
        "epd": 3,   # RED
    },
    "mclaren":     {
        "url": "https://media.formula1.com/image/upload/c_fit,h_64/q_auto/v1740000001/common/f1/2025/mclaren/2025mclarenlogolight.webp",
        "fallback": "https://upload.wikimedia.org/wikipedia/en/thumb/6/66/McLaren_Racing_logo.svg/200px-McLaren_Racing_logo.svg.png",
        "color": (255, 135, 0),
        "epd": 2,   # YELLOW (orange → yellow, closest available)
    },
    "redbull":     {
        "url": "https://media.formula1.com/image/upload/c_fit,h_64/q_auto/v1740000001/common/f1/2025/redbull/2025redbulllogolight.webp",
        "fallback": "https://upload.wikimedia.org/wikipedia/en/thumb/e/e0/Red_Bull_Racing_logo.svg/200px-Red_Bull_Racing_logo.svg.png",
        "color": (30, 61, 190),
        "epd": 5,   # BLUE
    },
    "williams":    {
        "url": "https://media.formula1.com/image/upload/c_fit,h_64/q_auto/v1740000001/common/f1/2025/williams/2025williamslogolight.webp",
        "fallback": "https://upload.wikimedia.org/wikipedia/en/thumb/d/d4/Williams_Racing_logo.svg/200px-Williams_Racing_logo.svg.png",
        "color": (0, 90, 255),
        "epd": 5,   # BLUE
    },
    "astonmartin": {
        "url": "https://media.formula1.com/image/upload/c_fit,h_64/q_auto/v1740000001/common/f1/2025/astonmartin/2025astonmartinlogolight.webp",
        "fallback": "https://upload.wikimedia.org/wikipedia/en/thumb/0/0a/Aston_Martin_Aramco_logo.svg/200px-Aston_Martin_Aramco_logo.svg.png",
        "color": (0, 111, 98),
        "epd": 6,   # GREEN
    },
    "alpine":      {
        "url": "https://media.formula1.com/image/upload/c_fit,h_64/q_auto/v1740000001/common/f1/2025/alpine/2025alpinelogolight.webp",
        "fallback": "https://upload.wikimedia.org/wikipedia/en/thumb/1/12/Alpine_F1_Team_Logo.svg/200px-Alpine_F1_Team_Logo.svg.png",
        "color": (0, 144, 255),
        "epd": 5,   # BLUE
    },
    "haas":        {
        "url": "https://media.formula1.com/image/upload/c_fit,h_64/q_auto/v1740000001/common/f1/2025/haas/2025haaslogolight.webp",
        "fallback": "https://upload.wikimedia.org/wikipedia/en/thumb/0/00/Haas_F1_Team_logo.svg/200px-Haas_F1_Team_logo.svg.png",
        "color": (200, 0, 0),
        "epd": 3,   # RED
    },
    "rb":          {
        "url": "https://media.formula1.com/image/upload/c_fit,h_64/q_auto/v1740000001/common/f1/2025/rb/2025rblogolight.webp",
        "fallback": "https://upload.wikimedia.org/wikipedia/en/thumb/d/df/RB_F1_Team_logo_2024.svg/200px-RB_F1_Team_logo_2024.svg.png",
        "color": (30, 61, 190),
        "epd": 5,   # BLUE
    },
    "sauber":      {
        "url": "https://media.formula1.com/image/upload/c_fit,h_64/q_auto/v1740000001/common/f1/2025/sauber/2025sauberlogolight.webp",
        "fallback": "https://upload.wikimedia.org/wikipedia/en/thumb/1/1e/Stake_F1_Team_Kick_Sauber_logo.svg/200px-Stake_F1_Team_Kick_Sauber_logo.svg.png",
        "color": (0, 130, 60),
        "epd": 6,   # GREEN
    },
}

HEADERS = {
    "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"
}

# ── Download helper ───────────────────────────────────────────────────────────
def download_image(name, team):
    for label, url in [("F1 CDN", team["url"]), ("Wikipedia", team["fallback"])]:
        try:
            print(f"  [{label}] {url[:70]}...")
            r = requests.get(url, headers=HEADERS, timeout=10)
            if r.status_code == 200 and len(r.content) > 500:
                from io import BytesIO
                img = Image.open(BytesIO(r.content))
                print(f"  ✓ Downloaded ({img.size[0]}×{img.size[1]}, {img.mode})")
                return img
            else:
                print(f"  ✗ HTTP {r.status_code}")
        except Exception as e:
            print(f"  ✗ Error: {e}")
    return None

# ── Floyd-Steinberg dithering to 6-color EPD palette ─────────────────────────
def color_distance(c1, c2):
    return sum((a - b) ** 2 for a, b in zip(c1, c2))

def nearest_epd_color(r, g, b):
    best_idx = 0
    best_dist = float('inf')
    for i, color in enumerate(EPD_PALETTE):
        if i == 4:  # skip unused slot
            continue
        d = color_distance((r, g, b), color)
        if d < best_dist:
            best_dist = d
            best_idx = i
    return best_idx, EPD_PALETTE[best_idx]

def dither_to_epd(img):
    """Floyd-Steinberg dither an image to the 6-color EPD palette."""
    img = img.convert("RGBA")
    w, h = img.size

    # Composite onto white background (removes transparency)
    bg = Image.new("RGBA", (w, h), (255, 255, 255, 255))
    bg.paste(img, mask=img.split()[3])
    img = bg.convert("RGB")

    # Work with float pixel array for error diffusion
    pixels = [[list(img.getpixel((x, y))) for x in range(w)] for y in range(h)]

    result_indices = []
    result_rgb = []

    for y in range(h):
        row_idx = []
        row_rgb = []
        for x in range(w):
            r, g, b = [max(0, min(255, int(v))) for v in pixels[y][x]]
            idx, nearest = nearest_epd_color(r, g, b)
            row_idx.append(idx)
            row_rgb.append(nearest)

            # Error diffusion
            er = r - nearest[0]
            eg = g - nearest[1]
            eb = b - nearest[2]

            def add_err(py, px, factor):
                if 0 <= py < h and 0 <= px < w:
                    pixels[py][px][0] = max(0, min(255, pixels[py][px][0] + er * factor))
                    pixels[py][px][1] = max(0, min(255, pixels[py][px][1] + eg * factor))
                    pixels[py][px][2] = max(0, min(255, pixels[py][px][2] + eb * factor))

            add_err(y,     x + 1, 7 / 16)
            add_err(y + 1, x - 1, 3 / 16)
            add_err(y + 1, x,     5 / 16)
            add_err(y + 1, x + 1, 1 / 16)

        result_indices.append(row_idx)
        result_rgb.append(row_rgb)

    # Build output image (RGB for BMP)
    out = Image.new("RGB", (w, h))
    for y in range(h):
        for x in range(w):
            out.putpixel((x, y), tuple(result_rgb[y][x]))

    return out, result_indices

# ── Write raw 4bpp file for direct framebuffer blitting ───────────────────────
def write_raw_4bpp(indices, path):
    """Write packed 4bpp raw file — two pixels per byte, high nibble first."""
    w = len(indices[0])
    h = len(indices)
    with open(path, 'wb') as f:
        # 4-byte header: width (2 bytes) + height (2 bytes)
        f.write(struct.pack('<HH', w, h))
        for row in indices:
            for i in range(0, w, 2):
                hi = row[i] & 0x0F
                lo = row[i + 1] & 0x0F if i + 1 < w else 0x01  # pad with white
                f.write(bytes([(hi << 4) | lo]))
    print(f"  → RAW 4bpp: {path} ({w}×{h}, {os.path.getsize(path)} bytes)")

# ── Main ──────────────────────────────────────────────────────────────────────
def load_local_or_download(name, team):
    """Check for local file first, then try download."""
    # Look for any image file with this team name in current dir or input/
    for folder in [".", "input", "logos_src"]:
        for ext in [".png", ".jpg", ".jpeg", ".webp", ".bmp", ".svg"]:
            path = os.path.join(folder, f"{name}{ext}")
            if os.path.exists(path):
                try:
                    img = Image.open(path)
                    print(f"  ✓ Loaded local file: {path} ({img.size[0]}×{img.size[1]})")
                    return img
                except Exception as e:
                    print(f"  ✗ Could not open {path}: {e}")
    # Fall back to download
    return download_image(name, team)

def main():
    os.makedirs(OUTPUT_DIR, exist_ok=True)

    print(f"\nF1 Logo Converter — target size {LOGO_W}×{LOGO_H}px")
    print(f"Output: ./{OUTPUT_DIR}/")
    print(f"\nTIP: Put logo files in ./input/ folder named like:")
    print(f"     ferrari.png, mclaren.png, redbull.png etc.")
    print(f"     Script checks for local files before downloading.\n{'─'*60}")

    success = []
    failed = []

    for name, team in TEAMS.items():
        print(f"\n▶ {name.upper()}")

        img = load_local_or_download(name, team)

        if img is None:
            print(f"  ✗ FAILED — using color block fallback")
            # Generate a solid color block as fallback
            color = team["color"]
            img = Image.new("RGB", (LOGO_W, LOGO_H), color)
            # Add team name text
            from PIL import ImageDraw, ImageFont
            draw = ImageDraw.Draw(img)
            draw.rectangle([0, 0, LOGO_W-1, LOGO_H-1], outline=(255,255,255), width=2)
            draw.text((LOGO_W//2, LOGO_H//2), name[:3].upper(),
                     fill=(255,255,255), anchor="mm")
            failed.append(name)
        else:
            success.append(name)

        # Resize with aspect ratio preserved, pad with white
        img_rgb = img.convert("RGBA")
        img_rgb.thumbnail((LOGO_W, LOGO_H), Image.LANCZOS)
        padded = Image.new("RGBA", (LOGO_W, LOGO_H), (255, 255, 255, 255))
        offset = ((LOGO_W - img_rgb.width) // 2, (LOGO_H - img_rgb.height) // 2)
        padded.paste(img_rgb, offset, img_rgb.split()[3] if img_rgb.mode == 'RGBA' else None)

        print(f"  Dithering to 6-color EPD palette...")
        dithered, indices = dither_to_epd(padded)

        # Save BMP (for viewing/verification)
        bmp_path = os.path.join(OUTPUT_DIR, f"{name}.bmp")
        dithered.save(bmp_path)
        print(f"  → BMP: {bmp_path}")

        # Save raw 4bpp (for ESP32 direct blit)
        raw_path = os.path.join(OUTPUT_DIR, f"{name}.raw")
        write_raw_4bpp(indices, raw_path)

    print(f"\n{'─'*60}")
    print(f"✓ Done: {len(success)} succeeded, {len(failed)} used fallback")
    if failed:
        print(f"  Fallbacks: {', '.join(failed)}")
    print(f"\nSD card setup:")
    print(f"  Copy all .raw files to SD card under /logos/")
    print(f"  e.g. ferrari.raw → SD:/logos/ferrari.raw")
    print(f"\nESP32 read call (add to your sketch):")
    print(f'  loadLogo("/logos/ferrari.raw", x, y);')

if __name__ == "__main__":
    main()