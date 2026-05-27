#pragma once
#include <Arduino.h>
#include <SD.h>

#define TRACK_W            200
#define TRACK_H            130
#define TRACK_BYTES_PER_ROW 25   // ceil(200/8)

static const char* getTrackFilename(const char* circuitId) {
  if (!circuitId) return nullptr;
  struct { const char* id; const char* file; } map[] = {
    { "americas",      "americas"      },
    { "bahrain",       "bahrain"       },
    { "baku",          "baku"          },
    { "catalunya",     "catalunya"     },
    { "hungaroring",   "hungaroring"   },
    { "interlagos",    "interlagos"    },
    { "jeddah",        "jeddah"        },
    { "las_vegas",     "las_vegas"     },
    { "losail",        "losail"        },
    { "marina_bay",    "marina_bay"    },
    { "albert_park",   "albert_park"   },
    { "rodriguez",     "rodriguez"     },
    { "miami",         "miami"         },
    { "monaco",        "monaco"        },
    { "villeneuve",    "villeneuve"    },
    { "monza",         "monza"         },
    { "shanghai",      "shanghai"      },
    { "silverstone",   "silverstone"   },
    { "spa",           "spa"           },
    { "red_bull_ring", "red_bull_ring" },
    { "suzuka",        "suzuka"        },
    { "yas_marina",    "yas_marina"    },
    { "zandvoort",     "zandvoort"     },
    { nullptr, nullptr }
  };
  for (int i = 0; map[i].id; i++)
    if (strstr(circuitId, map[i].id) || strstr(map[i].id, circuitId))
      return map[i].file;
  return nullptr;
}

static inline bool trackPixelAt(const uint8_t* img, int ox, int oy) {
  if ((unsigned)ox >= (unsigned)TRACK_W || (unsigned)oy >= (unsigned)TRACK_H) return false;
  uint8_t b = img[oy * TRACK_BYTES_PER_ROW + (ox >> 3)];
  return (b & (uint8_t)(1u << (7 - (ox & 7)))) != 0;
}

// layoutW: horizontal space allocated in column for centering (non-rot: TRACK centered in this width).
static void drawTrackFromSD(const char* circuitId, int layoutLeft, int layoutTop, uint8_t color,
                             int layoutMaxH, int layoutW) {
  const char* fname = getTrackFilename(circuitId);
  if (!fname) { Serial.printf("[Track] No match for: %s\n", circuitId); return; }
  char path[32];
  snprintf(path, sizeof(path), "/tracks/%s.raw", fname);
  File f = SD.open(path);
  if (!f) { Serial.printf("[Track] Not found: %s\n", path); return; }

  static uint8_t s_buf[TRACK_H * TRACK_BYTES_PER_ROW];
  const size_t need = sizeof(s_buf);
  size_t got = f.read(s_buf, need);
  f.close();
  if (got != need) {
    Serial.printf("[Track] Short read %s (%u/%u)\n", path, (unsigned)got, (unsigned)need);
    return;
  }

  // Montreal (Circuit Gilles Villeneuve): bitmap reads better rotated 90° CCW (left).
  const bool rotCcw90 = (strcmp(fname, "villeneuve") == 0);

  if (!rotCcw90) {
    int drawX = layoutLeft + (layoutW - TRACK_W) / 2;
    if (drawX < layoutLeft) drawX = layoutLeft;
    for (int ry = 0; ry < TRACK_H && ry < layoutMaxH; ry++) {
      const uint8_t* row = s_buf + ry * TRACK_BYTES_PER_ROW;
      for (int bx = 0; bx < TRACK_BYTES_PER_ROW; bx++) {
        uint8_t b = row[bx];
        for (int bit = 0; bit < 8; bit++) {
          if (b & (1 << (7 - bit)))
            epd_set_pixel(drawX + bx * 8 + bit, layoutTop + ry, color);
        }
      }
    }
    return;
  }

  // Rotated CCW 90°: logical size TRACK_H × TRACK_W (130 × 200). Tight-fit bbox, upscale, center.
  int minNx = TRACK_H, maxNx = -1, minNy = TRACK_W, maxNy = -1;
  for (int oy = 0; oy < TRACK_H; oy++) {
    for (int ox = 0; ox < TRACK_W; ox++) {
      if (!trackPixelAt(s_buf, ox, oy)) continue;
      const int nx = oy;
      const int ny = TRACK_W - 1 - ox;
      if (nx < minNx) minNx = nx;
      if (nx > maxNx) maxNx = nx;
      if (ny < minNy) minNy = ny;
      if (ny > maxNy) maxNy = ny;
    }
  }
  if (maxNx < minNx || maxNy < minNy) return;

  const int Wc = maxNx - minNx + 1;
  const int Hc = maxNy - minNy + 1;
  int s = 1;
  while (Wc * (s + 1) <= layoutW && Hc * (s + 1) <= layoutMaxH) ++s;

  const int outW = Wc * s;
  const int outH = Hc * s;
  const int dx = layoutLeft + (layoutW - outW) / 2;
  const int dy = layoutTop + (layoutMaxH - outH) / 2;

  for (int oy = 0; oy < TRACK_H; oy++) {
    for (int ox = 0; ox < TRACK_W; ox++) {
      if (!trackPixelAt(s_buf, ox, oy)) continue;
      const int nx = oy;
      const int ny = TRACK_W - 1 - ox;
      const int rx = nx - minNx;
      const int ry = ny - minNy;
      epd_fill_rect(dx + rx * s, dy + ry * s, s, s, color);
    }
  }
}