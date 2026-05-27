#pragma once
// Reads raw 4bpp images from SD. Included from main.cpp after epd_set_pixel is defined.

#include <SD.h>

// ── F1 logo / flags: LE uint16 w,h + 4bpp packed rows (high nibble = even column)
static void drawLogo(const char* path, int destX, int destY, uint8_t* /*buf*/) {
  if (!SD.exists(path)) return;

  File f = SD.open(path, FILE_READ);
  if (!f) return;

  uint8_t hdr[4];
  if (f.read(hdr, 4) != 4) {
    f.close();
    return;
  }
  int w = hdr[0] | (hdr[1] << 8);
  int h = hdr[2] | (hdr[3] << 8);

  if (w <= 0 || h <= 0 || w > 200 || h > 200) {
    f.close();
    return;
  }

  int rowBytes = (w + 1) / 2;
  uint8_t rowBuf[100];
  for (int row = 0; row < h; row++) {
    if (f.read(rowBuf, rowBytes) != (size_t)rowBytes) break;
    for (int col = 0; col < w; col++) {
      uint8_t b   = rowBuf[col / 2];
      uint8_t pix = (col & 1) ? (b & 0x0F) : (b >> 4);
      epd_set_pixel(destX + col, destY + row, pix);
    }
  }
  f.close();
}

// ── Constructor logos: /logos/<name>.raw — same 4-byte header + 4bpp packed
static const char* getConstructorLogoFile(const char* ctorId) {
  if (!ctorId) return nullptr;
  struct {
    const char* id;
    const char* file;
  } map[] = {
      {"ferrari", "ferrari"},
      {"mclaren", "mclaren"},
      {"mercedes", "mercedes"},
      {"red_bull", "red_bull"},
      {"williams", "williams"},
      {"aston_martin", "aston_martin"},
      {"alpine", "alpine"},
      {"haas", "haas"},
      {"rb", "rb"},
      {"audi", "audi"},
      {"kick_sauber", "audi"},
      {"sauber", "audi"},
      {"cadillac", "cadillac"},
      {nullptr, nullptr},
  };
  for (int i = 0; map[i].id; i++)
    if (strstr(ctorId, map[i].id) || strstr(map[i].id, ctorId)) return map[i].file;
  return nullptr;
}

static void drawConstructorLogo(const char* ctorId, int x, int y) {
  const char* fname = getConstructorLogoFile(ctorId);
  if (!fname) return;
  char path[40];
  snprintf(path, sizeof(path), "/logos/%s.raw", fname);
  File f = SD.open(path);
  if (!f) return;

  uint8_t hdr[4];
  if (f.read(hdr, 4) != 4) { f.close(); return; }
  int w = hdr[0] | (hdr[1] << 8);
  int h = hdr[2] | (hdr[3] << 8);
  if (w <= 0 || h <= 0 || w > 64 || h > 32) { f.close(); return; }

  int rowBytes = (w + 1) / 2;
  uint8_t rowBuf[32];
  bool isMclaren = ctorId && strstr(ctorId, "mclaren");
  bool isFerrari = ctorId && strstr(ctorId, "ferrari");
  uint8_t skipColor = isMclaren ? EPD_WHITE : EPD_BLACK;
  for (int row = 0; row < h; row++) {
    if (f.read(rowBuf, rowBytes) != (size_t)rowBytes) break;
    for (int col = 0; col < w; col++) {
      uint8_t b   = rowBuf[col / 2];
      uint8_t pix = (col & 1) ? (b & 0x0F) : (b >> 4);
      if (!isFerrari && pix == skipColor) continue;
      epd_set_pixel(x + col, y + row, pix);
    }
  }
  f.close();
}