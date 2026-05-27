// F1 Tracker — Waveshare ESP32-S3 PhotoPainter 7.3" 6-color e-paper
// 800×480, 3-column layout

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <SPI.h>
#include <XPowersLib.h>
#include <Adafruit_GFX.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <ctype.h>
#include <cstring>
#include <cstdio>
#include "f1_fonts.h"
#include "f1_audio.h"
#include <esp_system.h>


// Boot diagnostics — explains USB reconnect dings after repeated MCU resets.
static void logEspResetReason() {
  esp_reset_reason_t rr = esp_reset_reason();
  const char* name = "?";
  switch (rr) {
    case ESP_RST_UNKNOWN:   name = "UNKNOWN";      break;
    case ESP_RST_POWERON:   name = "POWERON";      break;
    case ESP_RST_EXT:       name = "EXT_PIN";      break;
    case ESP_RST_SW:        name = "SW_RESET";     break;
    case ESP_RST_PANIC:     name = "PANIC";       break;
    case ESP_RST_INT_WDT:   name = "INT_WDT";     break;
    case ESP_RST_TASK_WDT:  name = "TASK_WDT";    break;
    case ESP_RST_WDT:       name = "WDT_OTHER";   break;
    case ESP_RST_DEEPSLEEP: name = "DEEP_SLEEP";  break;
    case ESP_RST_BROWNOUT:  name = "BROWNOUT";    break;
    case ESP_RST_SDIO:      name = "SDIO";        break;
    default:                                        break;
  }
  Serial.printf("[RST] Reset reason=%d (%s)\n", (int)rr, name);
}

// ═══════════════════════════════════════════════════════════════════
//  HARDWARE
// ═══════════════════════════════════════════════════════════════════
#define EPD_WIDTH   800
#define EPD_HEIGHT  480
#define EPD_CS       9
#define EPD_DC       8
#define EPD_RST     12
#define EPD_BUSY    13
#define EPD_SCK     10
#define EPD_MOSI    11
#define I2C_SDA     47
#define I2C_SCL     48

#define EPD_BLACK   0x0
#define EPD_WHITE   0x1
#define EPD_YELLOW  0x2
#define EPD_RED     0x3
#define EPD_BLUE    0x5
#define EPD_GREEN   0x6
// Pseudo-color: McLaren orange = red+yellow 2×2 dither
#define EPD_ORANGE    0xFF  // sentinel — McLaren dither
#define EPD_TEAL      0xFD  // sentinel — Mercedes (green / blue dither)
#define EPD_SILVER    0xFC  // sentinel — gray dither

// Set true to flash a yellow pixel grid over the display for layout debugging
#define DEBUG_GRID false

// ── Font scheme ────────────────────────────────────────────────────
#define FONT_HERO   u8g2_font_f1bold24_tf
#define FONT_LARGE  u8g2_font_f1bold18_tf
#define FONT_MED    u8g2_font_f1bold14_tf
#define FONT_SMALL  u8g2_font_f1bold10_tf

// ═══════════════════════════════════════════════════════════════════
//  LAYOUT  — pixel-exact, verified against 800×480
//
//  Header  : y=0,   h=48   black bar (title vcenter + date/time below)
//  Body    : y=BODY_Y .. (EPD_HEIGHT - FOOTER_H)
//  Footer  : EPD_HEIGHT - FOOTER_H .. EPD_HEIGHT-1
//
//  Columns (see #defines; DIV_W = RED_RULE_THICK):
//    Col1 x=0 .. COL1_W-1 | divider | Col2 | divider | Col3
//
// !!
//
//  Body rows (col2/3): 9 data rows (10th vertical slot is col1-only above footer).
//    COL_ROW_H = (EPD_HEIGHT - FOOTER_H - SECTION_ROW0_Y) / 10
// ═══════════════════════════════════════════════════════════════════
// Column boundaries  (800px wide): COL1 + DIV_W + COL2 + DIV_W + COL3 = EPD_WIDTH
// DIV_W must match RED_RULE_THICK so vertical rules read as full-weight; column X starts *after* each divider
//   (otherwise row fills overwrite the inner pixels and the bar looks half-thick).
// Col1: 0..219  Div1: 220..223  Col2: 224..497  Div2: 498..501  Col3: 502..799
// ═══════════════════════════════════════════════════════════════════
#define COL1_X      0
#define COL1_W    220
// Stroke weight for red horizontal rules — same as column dividers (DIV_W)
#define RED_RULE_THICK  4
#define DIV_W          RED_RULE_THICK
#define DIV1_X    (COL1_X + COL1_W)
#define COL3_W    298
#define COL2_X    (DIV1_X + DIV_W)
#define COL2_W    (EPD_WIDTH - COL2_X - DIV_W - COL3_W)
#define DIV2_X    (COL2_X + COL2_W)
#define COL3_X    (DIV2_X + DIV_W)

// Header / body
#define HDR_H      56
#define HDR_DATE_Y          24
#define HDR_TIME_Y          40
#define HDR_RIGHT_INSET     28   // IP / battery % from right edge
#define HDR_ROUND_RIGHT_PAD 8    // gap left of right checkered strip (strip = 24px)
#define HDR_DATETIME_LEFT_X 28   // date + time (past 24px checkered strip)
#define BODY_Y    (HDR_H)
#define FOOTER_H  20

// Section headers — NEXT RACE | LAST RACE | DRIVERS / STARTING GRID | CONSTRUCTORS
#define SECTION_HEADER_LABEL_NUDGE_DN  3   // optical center in black band (between red rules)
#define BODY_SECTION_TITLE_EXTRA_Y     0
#define SECTION_TITLE_Y                (BODY_Y + BODY_SECTION_TITLE_EXTRA_Y)
#define COL_TITLE_H                    36
#define SECTION_AFTER_TITLE_GAP        7   // below strip → first standings GP row / left ladder
#define SECTION_ROW0_Y                 (SECTION_TITLE_Y + COL_TITLE_H + SECTION_AFTER_TITLE_GAP)
// 10 standings rows in col2 (drivers) and col3 (constructors).
#define STANDINGS_DATA_ROWS  10
#define COL_ROW_H     ((EPD_HEIGHT - FOOTER_H - SECTION_ROW0_Y) / 10)
#define ROW_TEXT_BASELINE_DN 11  // driver/ctor name & pos: ry + COL_ROW_H - this
#define COL23_ROWS_BOTTOM_Y  (SECTION_ROW0_Y + STANDINGS_DATA_ROWS * COL_ROW_H)

// Col2 — driver row layout
#define D_BAR_X     COL2_X
#define D_POS_X    (COL2_X + 6)
#define D_POS_W     16
#define D_BADGE_X  (COL2_X + 26)
#define D_BADGE_W   34
#define D_BADGE_H   20
#define D_NAME_X   (COL2_X + 66)
#define D_PTS_X    (COL2_X + COL2_W - 4)

// Col3 — constructor row layout
#define C_BAR_X     COL3_X
#define C_POS_X    (COL3_X + 6)
#define C_POS_W     16
#define CTOR_LOGO_W 40
#define CTOR_LOGO_H 20
#define C_LOGO_X    (C_POS_X + C_POS_W + 4)
#define C_NAME_X    (C_LOGO_X + CTOR_LOGO_W + 6)
#define C_NAME_MAX_W  110
#define C_BAR_START  (C_NAME_X + C_NAME_MAX_W + 4)
#define C_BAR_W       70
#define C_PTS_X      (COL3_X + COL3_W - 4)

// Col1 left panel Y layout
// fur20 race name (~20px), fur11 circuit (~11px), countdown box with fur35 inside
#define C1_SEC1_Y    SECTION_TITLE_Y
// Pixels below "NEXT RACE" strip before GP row; LAST RACE chain follows C1_BOX (podium: POD_BASE_Y)
#define C1_AFTER_NEXT_HEADER_EXTRA_Y  2   // tighter below NEXT RACE strip → pull GP block up
// Top of FONT_LARGE GP line: extra Y below header strip (circuit uses gap below).
#define C1_GP_NAME_NUDGE_Y  16
// Vertical gap from GP line Y to circuit line Y (avoids overlap as NUDGE grows).
#define C1_GP_TO_CIRC_GAP   11
#define C1_AFTER_HEADER_BASE_Y  (C1_SEC1_Y + COL_TITLE_H + SECTION_AFTER_TITLE_GAP + C1_AFTER_NEXT_HEADER_EXTRA_Y)
#define C1_RACE_Y              (C1_AFTER_HEADER_BASE_Y + C1_GP_NAME_NUDGE_Y)
#define C1_CIRC_Y              (C1_RACE_Y + C1_GP_TO_CIRC_GAP)
#define C1_BOX_Y    (C1_CIRC_Y + 10)
#define C1_BOX_H     42
#define C1_SEC2_Y   (C1_BOX_Y + C1_BOX_H + 14)  // was +4
// Space below LAST RACE header strip before previous GP title (both GP + city move together).
#define C1_LAST_RACE_HEADER_TO_GP_Y  54
#define C1_LGPN_Y   (C1_SEC2_Y + C1_LAST_RACE_HEADER_TO_GP_Y)
#define C1_LCIRC_Y  (C1_LGPN_Y + 18)

#define POD_BASE_Y   (EPD_HEIGHT - FOOTER_H - 2)   // above footer bar
#define POD_1_W       66
#define POD_1_H       85
#define POD_2_W       52
#define POD_2_H       65
#define POD_3_W       52
#define POD_3_H       50
#define POD_1_X       (COL1_W/2 - POD_1_W/2)
#define POD_2_X       (POD_1_X - POD_2_W)   // flush to left edge of P1
#define POD_3_X       (POD_1_X + POD_1_W)   // flush to right edge of P1
#define POD_1_Y      (POD_BASE_Y - POD_1_H)
#define POD_2_Y      (POD_BASE_Y - POD_2_H)
#define POD_3_Y      (POD_BASE_Y - POD_3_H)

// ═══════════════════════════════════════════════════════════════════
//  F1 LOGO BITMAP  (36×18 px, PROGMEM, 1bpp row-major MSB-first)
//  Drawn in EPD_RED via display.drawBitmap at (18, 8)
// ═══════════════════════════════════════════════════════════════════
#define F1_LOGO_W 80
#define F1_LOGO_H 20
static const uint8_t F1_Logo[] PROGMEM = {
  0x00,0x00,0x00,0x7f,0xff,0xff,0xff,0xff,0x8f,0xfe,0x00,0x00,0x07,0xff,0xff,0xff,
  0xff,0xff,0x1f,0xfc,0x00,0x00,0x1f,0xff,0xff,0xff,0xff,0xfe,0x3f,0xf8,0x00,0x00,
  0x3f,0xff,0xff,0xff,0xff,0xfc,0x7f,0xf0,0x00,0x00,0x7f,0xff,0xff,0xff,0xff,0xf8,
  0xff,0xe0,0x00,0x01,0xff,0xff,0xff,0xff,0xff,0xf1,0xff,0xc0,0x00,0x03,0xff,0xff,
  0xff,0xff,0xff,0xe3,0xff,0x80,0x00,0x07,0xff,0xe0,0x00,0x00,0x00,0x07,0xff,0x00,
  0x00,0x0f,0xff,0x00,0x00,0x00,0x00,0x0f,0xfe,0x00,0x00,0x1f,0xfc,0x3f,0xff,0xff,
  0xff,0x1f,0xfc,0x00,0x00,0x3f,0xf8,0xff,0xff,0xff,0xfe,0x3f,0xf8,0x00,0x00,0x7f,
  0xf1,0xff,0xff,0xff,0xfc,0x7f,0xf0,0x00,0x00,0xff,0xe3,0xff,0xff,0xff,0xf8,0xff,
  0xe0,0x00,0x01,0xff,0xc7,0xff,0xff,0xff,0xf1,0xff,0xc0,0x00,0x03,0xff,0x8f,0xff,
  0xff,0xff,0xe3,0xff,0x80,0x00,0x07,0xff,0x3f,0xf8,0x00,0x00,0x07,0xff,0x00,0x00,
  0x0f,0xfe,0x7f,0xe0,0x00,0x00,0x0f,0xfe,0x00,0x00,0x1f,0xfc,0xff,0xc0,0x00,0x00,
  0x1f,0xfc,0x00,0x00,0x3f,0xf9,0xff,0x80,0x00,0x00,0x3f,0xf8,0x00,0x00,0x7f,0xf1,
  0xff,0x00,0x00,0x00,0x7f,0xf0,0x00,0x00
};

// ── Named team color constants ─────────────────────────────────────
#define TEAM_MERCEDES    EPD_TEAL
#define TEAM_FERRARI     EPD_RED
#define TEAM_MCLAREN     EPD_ORANGE
#define TEAM_REDBULL     EPD_BLUE
#define TEAM_WILLIAMS    EPD_BLUE
#define TEAM_ASTONMARTIN EPD_GREEN
#define TEAM_ALPINE      EPD_BLUE
#define TEAM_HAAS        EPD_WHITE
#define TEAM_RB          EPD_BLUE
#define TEAM_SAUBER      EPD_GREEN
#define TEAM_CADILLAC    EPD_WHITE

// ═══════════════════════════════════════════════════════════════════
//  TEAM → EPD COLOR MAP
// ═══════════════════════════════════════════════════════════════════
struct TeamColor { const char* id; uint8_t color; };
static const TeamColor TEAM_COLORS[] = {
  { "red_bull",     TEAM_REDBULL     },
  { "ferrari",      TEAM_FERRARI     },
  { "mclaren",      TEAM_MCLAREN     },
  { "mercedes",     EPD_TEAL         },
  { "williams",     TEAM_WILLIAMS    },
  { "aston_martin", TEAM_ASTONMARTIN },
  { "alpine",       TEAM_ALPINE      },
  { "haas",         EPD_SILVER       },
  { "rb",           TEAM_RB          },
  { "sauber",       TEAM_SAUBER      },
  { "kick_sauber",  TEAM_SAUBER      },
  { "audi",         EPD_SILVER       },
  { "cadillac",     EPD_SILVER       },
};

static uint8_t teamColor(const char* ctorId, const char* ctorName) {
  // Match on constructorId first (lowercase, underscored)
  for (auto& tc : TEAM_COLORS)
    if (strstr(ctorId, tc.id)) return tc.color;
  // Fallback: case-insensitive first-word match on name
  char lname[32]; int i=0;
  while (ctorName[i] && ctorName[i]!=' ' && i<31) { lname[i]=tolower(ctorName[i]); i++; }
  lname[i]=0;
  for (auto& tc : TEAM_COLORS)
    if (strstr(lname, tc.id)) return tc.color;
  return EPD_BLACK;
}

// Podium glyphs use font mode 1 with a solid key color; approximate dithered fills.
static uint8_t podiumGlyphBg(uint8_t tc) {
  if (tc == EPD_ORANGE) return EPD_RED;
  if (tc == EPD_TEAL) return EPD_BLUE;
  if (tc == EPD_SILVER) return EPD_BLACK;
  return tc;
}

static uint8_t podiumTextFg(uint8_t tc, const char* ctorId) {
  if (tc == EPD_WHITE) return EPD_BLACK;
  if (ctorId && ctorId[0] && strstr(ctorId, "mclaren")) return EPD_BLACK;
  return EPD_WHITE;
}

// First word of a string (stops at first space)
static String firstWord(const char* s) {
  if (!s||!s[0]) return "";
  String out; for (int i=0; s[i]&&s[i]!=' '; i++) out += s[i];
  return out;
}

static String constructorDisplayName(const char* ctorId, const char* ctorName) {
  String id = String(ctorId);
  String name = String(ctorName);
  id.toLowerCase();
  name.toLowerCase();
  if (id.indexOf("mercedes") >= 0 || name.indexOf("mercedes") >= 0) return "Mercedes";
  if (id.indexOf("ferrari") >= 0 || name.indexOf("ferrari") >= 0) return "Ferrari";
  if (id.indexOf("mclaren") >= 0 || name.indexOf("mclaren") >= 0) return "McLaren";
  if (id.indexOf("haas") >= 0 || name.indexOf("haas") >= 0) return "Haas";
  if (id.indexOf("alpine") >= 0 || name.indexOf("alpine") >= 0) return "Alpine";
  if (id.indexOf("red_bull") >= 0 || name.indexOf("red bull") >= 0) return "Red Bull";
  if (id == "rb" || name == "rb") return "RB";
  if (id.indexOf("audi") >= 0 || name.indexOf("audi") >= 0) return "Audi";
  if (id.indexOf("williams") >= 0 || name.indexOf("williams") >= 0) return "Williams";
  if (id.indexOf("cadillac") >= 0 || name.indexOf("cadillac") >= 0) return "Cadillac";
  return String(ctorName);
}

// ═══════════════════════════════════════════════════════════════════
//  PMIC
// ═══════════════════════════════════════════════════════════════════
XPowersAXP2101 pmu;
/** false if AXP never probed OK — skip all PMU reads (don't call PMU API on failed begin). */
static bool pmuProbeOk = false;

static void initPMIC() {
  pinMode(I2C_SDA, INPUT_PULLUP);
  pinMode(I2C_SCL, INPUT_PULLUP);
  Wire.begin(I2C_SDA, I2C_SCL);
  // Slow I²C while probing — avoids glitchy first transactions after flash / USB connect.
  Wire.setClock(100000);
  delay(80);

  const int k_pmu_tries = 15;
  bool ok = false;
  for (int i = 0; i < k_pmu_tries; i++) {
    if (pmu.begin(Wire, AXP2101_SLAVE_ADDRESS, I2C_SDA, I2C_SCL)) {
      ok = true;
      break;
    }
    Serial.printf("[PMIC] AXP2101 probe %d/%d failed, retry\n", i + 1, k_pmu_tries);
    delay(150 + i * 25);
  }

  if (!ok) {
    Serial.println(F("[PMIC] AXP2101 not responding on I²C."));
#if defined(F1_PMIC_SKIP_ON_FAIL)
    pmuProbeOk = false;
    Serial.println(F("[PMIC] F1_PMIC_SKIP_ON_FAIL: continuing without PMIC (battery % hidden)."));
    return;
#else
    Serial.println(F("[PMIC] Try: unplug USB, different cable/port, cold boot."));
    Serial.println(F("[PMIC] Wrong board? Add build_flags -DF1_PMIC_SKIP_ON_FAIL=1 (dev-only)."));
    while (true) delay(1000);
#endif
  }

  pmuProbeOk = true;
  pmu.setALDO1Voltage(1800); pmu.enableALDO1();
  pmu.setALDO2Voltage(3300); pmu.enableALDO2();
  pmu.setALDO3Voltage(3300); pmu.enableALDO3();
  pmu.setALDO4Voltage(3300); pmu.enableALDO4();
  pmu.setBLDO1Voltage(3300); pmu.enableBLDO1();
  pmu.setBLDO2Voltage(3300); pmu.enableBLDO2();
  delay(200);
  Wire.setClock(400000);
  Serial.println(F("PMIC ok"));
}

// ═══════════════════════════════════════════════════════════════════
//  FRAMEBUFFER + EPD LOW-LEVEL
// ═══════════════════════════════════════════════════════════════════
#define EPD_BUF_SIZE (EPD_WIDTH * EPD_HEIGHT / 2)
static uint8_t* epd_buf = nullptr;

static inline void epd_set_pixel(int x, int y, uint8_t color) {
  if ((unsigned)x >= EPD_WIDTH || (unsigned)y >= EPD_HEIGHT) return;
  int idx = (y * EPD_WIDTH + x) >> 1;
  if (x & 1) epd_buf[idx] = (epd_buf[idx] & 0xF0) | (color & 0x0F);
  else        epd_buf[idx] = (epd_buf[idx] & 0x0F) | (color << 4);
}
static void epd_fill(uint8_t color) {
  memset(epd_buf, (uint8_t)((color << 4) | color), EPD_BUF_SIZE);
}
static void epd_fill_rect(int x, int y, int w, int h, uint8_t c) {
  for (int j=y; j<y+h; j++) for (int i=x; i<x+w; i++) epd_set_pixel(i,j,c);
}
static void epd_hline(int x, int y, int len, uint8_t c) {
  for (int i=x; i<x+len; i++) epd_set_pixel(i,y,c);
}
static void epd_vline(int x, int y, int len, uint8_t c) {
  for (int j=y; j<y+len; j++) epd_set_pixel(x,j,c);
}
static void epd_rect(int x, int y, int w, int h, uint8_t c) {
  epd_hline(x,y,w,c); epd_hline(x,y+h-1,w,c);
  epd_vline(x,y,h,c); epd_vline(x+w-1,y,h,c);
}

// Pseudo-colors: 1px checker dithers (EPD has no native teal/orange/silver).
static void epd_fill_rect_color(int x, int y, int w, int h, uint8_t c) {
  if (c == EPD_ORANGE) {
    for (int j=y; j<y+h; j++)
      for (int i=x; i<x+w; i++)
        epd_set_pixel(i, j, ((i^j)&1) ? EPD_YELLOW : EPD_RED);
    return;
  }
  if (c == EPD_TEAL) {
    for (int j=y; j<y+h; j++)
      for (int i=x; i<x+w; i++)
        epd_set_pixel(i, j, ((i+j*2)%4)<1 ? EPD_BLUE : EPD_GREEN);
    return;
}
  if (c == EPD_SILVER) {
    for (int j=y; j<y+h; j++)
      for (int i=x; i<x+w; i++)
        epd_set_pixel(i, j, ((i^j)&1) ? EPD_WHITE : EPD_BLACK);
    return;
  }
  epd_fill_rect(x, y, w, h, c);
}

// track_reader declares epd_set_pixel(); definition must appear before this include
// logo_reader must stay after epd_set_pixel (same as track_reader)
#include "logo_reader.h"
#include "track_reader.h"

// SPI helpers — EPD on SPI; use paired begin/end (never leave a transaction open).
static inline void epd_spi_begin() {
  SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
}
static inline void epd_spi_end() { SPI.endTransaction(); }

static void epd_cmd(uint8_t c) { digitalWrite(EPD_DC,LOW); digitalWrite(EPD_CS,LOW); SPI.transfer(c); digitalWrite(EPD_CS,HIGH); }
static void epd_dat(uint8_t d) { digitalWrite(EPD_DC,HIGH); digitalWrite(EPD_CS,LOW); SPI.transfer(d); digitalWrite(EPD_CS,HIGH); }
static void waitBusy(uint8_t level, uint32_t ms) {
  uint32_t t=millis(); while(digitalRead(EPD_BUSY)!=level) { if(millis()-t>ms) return; delay(1); }
}

static void epd_init() {
  pinMode(EPD_CS,OUTPUT); digitalWrite(EPD_CS,HIGH);
  pinMode(EPD_DC,OUTPUT); digitalWrite(EPD_DC,HIGH);
  pinMode(EPD_RST,OUTPUT); digitalWrite(EPD_RST,HIGH);
  pinMode(EPD_BUSY,INPUT);
  SPI.begin(EPD_SCK,-1,EPD_MOSI);
  epd_spi_begin();
  digitalWrite(EPD_RST,HIGH); delay(20);
  digitalWrite(EPD_RST,LOW); delay(5);
  digitalWrite(EPD_RST,HIGH); delay(20);
  waitBusy(HIGH,5000); delay(30);
  epd_cmd(0xAA); epd_dat(0x49); epd_dat(0x55); epd_dat(0x20); epd_dat(0x08); epd_dat(0x09); epd_dat(0x18);
  epd_cmd(0x01); epd_dat(0x3F);
  epd_cmd(0x00); epd_dat(0x5F); epd_dat(0x69);
  epd_cmd(0x03); epd_dat(0x00); epd_dat(0x54); epd_dat(0x00); epd_dat(0x44);
  epd_cmd(0x05); epd_dat(0x40); epd_dat(0x1F); epd_dat(0x1F); epd_dat(0x2C);
  epd_cmd(0x06); epd_dat(0x6F); epd_dat(0x1F); epd_dat(0x17); epd_dat(0x49);
  epd_cmd(0x08); epd_dat(0x6F); epd_dat(0x1F); epd_dat(0x1F); epd_dat(0x22);
  epd_cmd(0x30); epd_dat(0x03);
  epd_cmd(0x50); epd_dat(0x3F);
  epd_cmd(0x60); epd_dat(0x02); epd_dat(0x00);
  epd_cmd(0x61); epd_dat(0x03); epd_dat(0x20); epd_dat(0x01); epd_dat(0xE0);
  epd_cmd(0x84); epd_dat(0x01);
  epd_cmd(0xE3); epd_dat(0x2F);
  epd_cmd(0x04); waitBusy(HIGH,10000);
  epd_spi_end();
  Serial.println(F("EPD init ok"));
}

static void epd_refresh() {
  epd_spi_begin();
  epd_cmd(0x04); waitBusy(HIGH,10000);   // power on
  epd_cmd(0x10);
  digitalWrite(EPD_DC,HIGH); digitalWrite(EPD_CS,LOW);
  SPI.transfer(epd_buf, EPD_BUF_SIZE);
  digitalWrite(EPD_CS,HIGH);
  epd_cmd(0x06); epd_dat(0x6F); epd_dat(0x1F); epd_dat(0x17); epd_dat(0x49);
  epd_cmd(0x12); epd_dat(0x00);
  uint32_t t0=millis();
  while(digitalRead(EPD_BUSY)==HIGH && millis()-t0<2000) delay(1);
  while(digitalRead(EPD_BUSY)==LOW  && millis()-t0<35000) delay(10);
  epd_cmd(0x02); epd_dat(0x00);           // power off
  while(digitalRead(EPD_BUSY)==LOW) delay(1);
  epd_spi_end();
}

// ═══════════════════════════════════════════════════════════════════
//  ADAFRUIT-GFX SHIM
// ═══════════════════════════════════════════════════════════════════
class EPDDisplay : public Adafruit_GFX {
public:
  EPDDisplay() : Adafruit_GFX(EPD_WIDTH, EPD_HEIGHT) {}
  void drawPixel(int16_t x, int16_t y, uint16_t color) override { epd_set_pixel(x,y,(uint8_t)color); }
};
static EPDDisplay display;
static U8G2_FOR_ADAFRUIT_GFX gfx;

static String truncateToPixelWidth(String text, uint16_t maxWidth) {
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  while (text.length() && w > maxWidth) {
    text.remove(text.length() - 1);
    display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  }
  return text;
}

// ═══════════════════════════════════════════════════════════════════
//  MISC
// ═══════════════════════════════════════════════════════════════════
enum screenAlignment { LEFT, RIGHT, CENTER };
bool wifiConnected = false;
unsigned long lastCheckMinute = 0;
static volatile bool forceDisplayRefreshPending = false;
static volatile bool screenRedrawNoDataPending = false;
static volatile bool apiNetworkFetchesDisabled = false;

// ═══════════════════════════════════════════════════════════════════
//  RUNTIME CONFIG (Preferences + http://<device-ip>/ )
// ═══════════════════════════════════════════════════════════════════
static uint8_t  cfgVol = 60;
static uint16_t cfgUpdRaceMin = 30;
static uint16_t cfgUpdGridMin = 30;
static uint8_t  cfgUpdHrNear = 2;
static uint8_t  cfgUpdHrFar = 6;
static uint32_t cfgCacheCalMs = 3600000UL;
static uint32_t cfgCacheStandMs = 3600000UL;
static uint32_t cfgCacheQualMs = 86400000UL;
static uint32_t cfgTtlQualAvailMs = 43200000UL;
static uint32_t cfgTtlResultsMs = 300000UL;
static bool     cfgWifiAlways = true;
static bool     cfgBootSplash = true;   // F1 quote + logo on startup; prefs "bootSplash"
static bool     cfgQuietHoursEn = false;
static uint8_t  cfgQuietStartH = 22;
static uint8_t  cfgQuietEndH   = 7;
static bool     cfgSoundBoot = true;
static bool     cfgSoundLoaded = true;
static bool     cfgSoundUpdate = true;
static String   cfgSoundBootFile   = "boot.wav";
static String   cfgSoundLoadedFile = "loaded.wav";
static String   cfgSoundUpdateFile = "update.wav";
static bool     cfgClock24h = false;   // false = 12h am/pm; prefs key "clock24h"
static long     cfgRaceInProgSec = 14400;
static long     cfgRaceWinBeforeSec = 21600;
static long     cfgRaceWinAfterSec = 21600;
static long     cfgGridShowSec = 64800;
static long     cfgPhaseGridSec = 86400;
static long     cfgPhaseMidSec = 172800;

WebServer configServer(80);

static File   sdUploadOut;
static size_t sdUploadBytes = 0;
static String sdUploadErr;
static String sdUploadRmPath;
static bool   sdUploadAwaitFirstFile = true;
static int    sdUploadOkCount       = 0;
static String sdUploadReportHtml;
static constexpr size_t kSdUploadMaxBytes = 12u * 1024u * 1024u;

static void loadDeviceConfig();
static void setupConfigWeb();
static void handleConfigServer();
static void handleSdUploadDone();
static void handleSdMkdir();
static void handleSdBrowse();
static void handleSdDeletePost();
static void handleSdUploadChunk();
static String htmlAttrEscape(const String& s) {
  String o;
  o.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '&') o += F("&amp;");
    else if (c == '"') o += F("&quot;");
    else if (c == '<') o += F("&lt;");
    else o += c;
  }
  return o;
}
static uint32_t clampU32(uint32_t v, uint32_t lo, uint32_t hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

const char API_BASE_TEMPLATE[]        PROGMEM = "https://api.jolpi.ca/ergast/f1/%s";
const char API_RACES_SUFFIX[]         PROGMEM = "/races.json";
const char API_DRIVER_STAND_SUFFIX[]  PROGMEM = "/driverstandings.json";
const char API_CONSTR_STAND_SUFFIX[]  PROGMEM = "/constructorstandings.json";

#define API_SCRATCH_DOC_SIZE 24576
static StaticJsonDocument<API_SCRATCH_DOC_SIZE> doc;

// ═══════════════════════════════════════════════════════════════════
//  API CACHE
// ═══════════════════════════════════════════════════════════════════
struct CacheEntry { String data; unsigned long timestamp; bool valid; };
struct { CacheEntry calendar, driverStandings, constructorStandings, qualifying, results; } apiCache;

struct AvailabilityCache {
  unsigned int round;
  bool qualifyingAvailable, resultsAvailable;
  unsigned long qualifyingTimestamp, resultsTimestamp;
  bool qualifyingValid, resultsValid;
};
static AvailabilityCache availabilityCache = {0,false,false,0,0,false,false};

static void invalidateStandingsCache() { apiCache.driverStandings.valid = apiCache.constructorStandings.valid = false; }
static void invalidateAllApiCaches() {
  apiCache.calendar.valid = apiCache.driverStandings.valid = apiCache.constructorStandings.valid = false;
  apiCache.qualifying.valid = apiCache.results.valid = false;
  availabilityCache = {0, false, false, 0, 0, false, false};
}
static bool getCachedData(CacheEntry* e, unsigned long ttl) {
  if (!e->valid) return false;
  if (millis()-e->timestamp > ttl) { e->valid=false; return false; }
  return true;
}
static void cacheData(CacheEntry* e, const String& d) { e->data=d; e->timestamp=millis(); e->valid=true; }

// ═══════════════════════════════════════════════════════════════════
//  CALENDAR GLOBALS
// ═══════════════════════════════════════════════════════════════════
unsigned lastRound=0, nextRound=0;
String lastDate, lastCircuit, lastCircuitId, lastLoc, lastGP, lastTime;
String nextDate, nextCircuit, nextCircuitId, nextLoc, nextGP, nextTime;

#define MY_TZ "EST5EDT,M3.2.0/2,M11.1.0/2"
#define NTP1  "pool.ntp.org"
#define NTP2  "time.nist.gov"
struct tm timeinfo;

Preferences prefs;
unsigned processedRound=0;
time_t   nextRaceEpoch=0;
String   nextRaceDateStr;

// ═══════════════════════════════════════════════════════════════════
//  FORWARD DECLS
// ═══════════════════════════════════════════════════════════════════
bool qualifyingAvailableForRound(unsigned round);
bool resultsAvailableForLastRound();
void DrawStartingGrid(unsigned round);
void DrawDriversStandings();
void DrawDrivers();
void DrawConstructors();
bool ensureWiFiConnected();
void disconnectWiFiIfIdle();
time_t isoUtcToEpoch(const String& ymd, const String& hms);

// ═══════════════════════════════════════════════════════════════════
//  UTILITIES
// ═══════════════════════════════════════════════════════════════════
static String utf8_substr(const String& s, int n) {
  if (!s.length()||n<=0) return "";
  const char* p=s.c_str(); String out; out.reserve(n*4); int seen=0;
  while(*p && seen<n) {
    uint8_t c=(uint8_t)*p;
    if((c&0xC0)==0x80){p++;continue;}
    int b=1; if((c&0xF8)==0xF0)b=4; else if((c&0xF0)==0xE0)b=3; else if((c&0xE0)==0xC0)b=2;
    for(int i=0;i<b&&*p;i++) out+=*p++; seen++;
  }
  return out;
}

static time_t timegm_utc(struct tm* tm) {
  int y=tm->tm_year+1900, m=tm->tm_mon+1;
  if(m<=2){y--;m+=12;}
  int64_t d=365LL*y+y/4-y/100+y/400+(153*(m-3)+2)/5+tm->tm_mday-719469;
  return d*86400+tm->tm_hour*3600+tm->tm_min*60+tm->tm_sec;
}

static String cleanTime(String t) {
  int z=t.indexOf('Z'); if(z>=0) t.remove(z);
  if(!t.length()) t="00:00:00"; return t;
}

time_t isoUtcToEpoch(const String& ymd, const String& hms) {
  if(!ymd.length()||!hms.length()) return 0;
  int y,m,d,hh=0,mm=0,ss=0;
  if(sscanf(ymd.c_str(),"%4d-%2d-%2d",&y,&m,&d)!=3) return 0;
  sscanf(hms.c_str(),"%2d:%2d:%2d",&hh,&mm,&ss);
  if(y<2000||y>2100||m<1||m>12||d<1||d>31) return 0;
  struct tm t={}; t.tm_year=y-1900; t.tm_mon=m-1; t.tm_mday=d;
  t.tm_hour=hh; t.tm_min=mm; t.tm_sec=ss; return timegm_utc(&t);
}

static String buildApiUrl(const char* suffix) {
  struct tm ct; if(!getLocalTime(&ct)) return "";
  char base[80]; snprintf_P(base,sizeof(base),API_BASE_TEMPLATE,String(1900+ct.tm_year).c_str());
  return String(base)+String(FPSTR(suffix));
}
static String seasonBaseUrl() {
  time_t now=time(nullptr); struct tm ti={}; localtime_r(&now,&ti);
  char base[96]; snprintf_P(base,sizeof(base),API_BASE_TEMPLATE,String(ti.tm_year+1900).c_str());
  return String(base);
}

static bool isRaceInProgress() {
  if(!nextRaceEpoch) return false;
  long d=(long)(nextRaceEpoch-time(nullptr));
  return (d<=0 && d>=-cfgRaceInProgSec);
}
static bool isInRaceWindow() {
  if(!nextRaceEpoch) return false;
  long d=(long)(nextRaceEpoch-time(nullptr));
  return (d<=cfgRaceWinBeforeSec && d>=-cfgRaceWinAfterSec);
}

/** Same predicate as `isInRaceWindow()` evaluated at arbitrary local-wall `nowEpoch` (scheduled next-update preview). */
static bool isInRaceWindowAt(time_t nowEpoch) {
  if (!nextRaceEpoch) return false;
  long d = (long)(nextRaceEpoch - nowEpoch);
  return (d <= cfgRaceWinBeforeSec && d >= -cfgRaceWinAfterSec);
}

// Local time string with ':' separator (strftime locale on device may use '.')
static void formatLocalTime(char* buf, size_t buflen, const struct tm* tm) {
  if (!buf || buflen < 6U || !tm) {
    if (buf && buflen) buf[0] = '\0';
    return;
  }
  if (cfgClock24h)
    snprintf(buf, buflen, "%02d:%02d", tm->tm_hour, tm->tm_min);
  else {
    int h = tm->tm_hour % 12;
    if (h == 0) h = 12;
    snprintf(buf, buflen, "%d:%02d %s", h, tm->tm_min,
             (tm->tm_hour < 12) ? "am" : "pm");
  }
}

static String nextRaceLocalDateMMDD() {
  time_t ep=nextRaceEpoch ? nextRaceEpoch : isoUtcToEpoch(nextDate,nextTime);
  if(!ep) return ""; struct tm loc={}; if(!localtime_r(&ep,&loc)) return "";
  char buf[16]; strftime(buf,sizeof(buf),"%m/%d",&loc); return String(buf);
}
static String nextRaceLocalTimeLower() {
  if(!nextRound||!nextRaceEpoch) return "";
  struct tm loc={}; if(!localtime_r(&nextRaceEpoch,&loc)) return "";
  char tb[32];
  formatLocalTime(tb, sizeof(tb), &loc);
  return String(tb);
}
static String nextRaceCountdownDH() {
  time_t ep=nextRaceEpoch ? nextRaceEpoch : isoUtcToEpoch(nextDate,nextTime);
  if(!ep) return "";
  long diff=(long)(ep-time(nullptr)); if(diff<=0) return "";
  long days=diff/86400L, hours=(diff%86400L)/3600L;
  if(!days&&!hours) return "<1h";
  String out;
  if (days) {
    out += String(days) + "d";
    if (hours) out += " ";
  }
  if (hours) {
    out += String(hours) + "h";
  }
  return out;
}

// ═══════════════════════════════════════════════════════════════════
//  WIFI
// ═══════════════════════════════════════════════════════════════════
bool ensureWiFiConnected() {
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    return true;
  }
  Serial.println(F("[WiFi] Reconnecting..."));
  WiFi.mode(WIFI_STA);
  delay(100);
  WiFi.begin();
  const int tries = 60;
  for (int i = 0; i < tries && WiFi.status() != WL_CONNECTED; i++) {
    delay(500);
    Serial.print(F("."));
  }
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.println(F("\n[WiFi] OK"));
    return true;
  }
  Serial.println(F("\n[WiFi] FAIL — will retry later (no reboot)"));
  wifiConnected = false;
  return false;
}
void disconnectWiFiIfIdle() {
  if (cfgWifiAlways) return;
  if(WiFi.status()==WL_CONNECTED){WiFi.disconnect(true); WiFi.mode(WIFI_OFF); wifiConnected=false; Serial.println(F("[WiFi] Off"));}
}

// ── Sound file paths (relative to /sound/) ─────────────────────────
static String normalizeSoundRel(String s) {
  s.trim();
  while (s.length() && s[0] == '/') s.remove(0, 1);
  if (s.startsWith(F("sound/"))) s.remove(0, 6);
  while (s.length() && s[0] == '/') s.remove(0, 1);
  return s;
}

static bool soundRelPathChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
         c == '.' || c == '_' || c == '-' || c == '/';
}

static bool soundRelPathOk(const String& rel) {
  if (rel.length() < 5 || rel.length() > 80) return false;
  if (rel.indexOf("..") >= 0) return false;
  if (rel[0] == '/' || rel[0] == '.') return false;
  for (unsigned i = 0; i < rel.length(); i++) {
    if (!soundRelPathChar(rel[i])) return false;
  }
  String low = rel;
  low.toLowerCase();
  return low.endsWith(F(".wav"));
}

static String loadSoundRelPref(const char* prefKey, const char* defRel) {
  String raw = prefs.getString(prefKey, defRel);
  String rel = normalizeSoundRel(raw);
  if (!rel.length()) rel = defRel;
  if (!soundRelPathOk(rel)) rel = defRel;
  return rel;
}

static void playSoundRelFile(const String& rel) {
  String path = String(F("/sound/")) + rel;
  playWAV(path.c_str());
}

// Earliest millis() allowed for race-results celebration (avoid duplicating boot.wav right after startup).
static uint32_t s_celebrateResultsNotBeforeMs = 0;

// When official results arrive for current lastRound for the first time, play boot clip (typically theme).
static bool tryPlayFirstRaceResultsCelebration() {
  if (!cfgSoundBoot || lastRound == 0) return false;
  uint32_t ms = millis();
  if (s_celebrateResultsNotBeforeMs &&
      (int32_t)(ms - s_celebrateResultsNotBeforeMs) < 0)
    return false;
  if (!resultsAvailableForLastRound()) return false;
  unsigned celebrated = prefs.getUInt("celebrRound", 0);
  if (celebrated == (unsigned)lastRound) return false;
  playSoundRelFile(cfgSoundBootFile);
  prefs.putUInt("celebrRound", lastRound);
  Serial.printf("[Audio] Race results in — celebration clip (boot), round %u\n", lastRound);
  return true;
}

static void loadDeviceConfig() {
  cfgVol = prefs.getUChar("vol", 60);
  if (cfgVol > 100) cfgVol = 100;
  audioSetVolumePercent(cfgVol);

  cfgUpdRaceMin = (uint16_t)prefs.getUInt("updRace", 30);
  cfgUpdGridMin = (uint16_t)prefs.getUInt("updGrid", 30);
  cfgUpdRaceMin = (uint16_t)clampU32(cfgUpdRaceMin, 1, 1440);
  cfgUpdGridMin = (uint16_t)clampU32(cfgUpdGridMin, 1, 1440);

  cfgUpdHrNear = (uint8_t)prefs.getUInt("hrNear", 2);
  cfgUpdHrFar = (uint8_t)prefs.getUInt("hrFar", 6);
  cfgUpdHrNear = (uint8_t)clampU32(cfgUpdHrNear, 1, 24);
  cfgUpdHrFar = (uint8_t)clampU32(cfgUpdHrFar, 1, 24);

  uint32_t m;
  m = prefs.getUInt("cacheCal", 60);
  cfgCacheCalMs = clampU32(m, 1, 10080) * 60000UL;
  m = prefs.getUInt("cacheStand", 60);
  cfgCacheStandMs = clampU32(m, 1, 10080) * 60000UL;
  m = prefs.getUInt("cacheQual", 1440);
  cfgCacheQualMs = clampU32(m, 1, 43200) * 60000UL;
  m = prefs.getUInt("ttlQualAv", 720);
  cfgTtlQualAvailMs = clampU32(m, 1, 10080) * 60000UL;
  m = prefs.getUInt("ttlRes", 5);
  cfgTtlResultsMs = clampU32(m, 1, 1440) * 60000UL;

  cfgWifiAlways = prefs.getBool("wifiAlways", true);
  cfgBootSplash = prefs.getBool("bootSplash", true);
  cfgQuietHoursEn = prefs.getBool("quietEn", false);
  cfgQuietStartH = (uint8_t)prefs.getUChar("quietSh", 22);
  cfgQuietEndH = (uint8_t)prefs.getUChar("quietEh", 7);
  if (cfgQuietStartH > 23) cfgQuietStartH = 22;
  if (cfgQuietEndH > 23) cfgQuietEndH = 7;
  audioConfigureQuietHours(cfgQuietHoursEn, cfgQuietStartH, cfgQuietEndH);
  bool legacySounds = prefs.getBool("soundsOn", true);
  cfgSoundBoot = prefs.getBool("soundBoot", legacySounds);
  cfgSoundLoaded = prefs.getBool("soundLoaded", legacySounds);
  cfgSoundUpdate = prefs.getBool("soundUpdate", legacySounds);
  cfgSoundBootFile = loadSoundRelPref("soundBootFile", "boot.wav");
  cfgSoundLoadedFile = loadSoundRelPref("soundLoadedFile", "loaded.wav");
  cfgSoundUpdateFile = loadSoundRelPref("soundUpdateFile", "update.wav");
  cfgClock24h = prefs.getBool("clock24h", false);

  uint32_t h;
  h = prefs.getUInt("raceProgH", 4);
  cfgRaceInProgSec = (long)clampU32(h, 1, 48) * 3600L;
  h = prefs.getUInt("raceBefH", 6);
  cfgRaceWinBeforeSec = (long)clampU32(h, 1, 72) * 3600L;
  h = prefs.getUInt("raceAftH", 6);
  cfgRaceWinAfterSec = (long)clampU32(h, 1, 72) * 3600L;
  h = prefs.getUInt("gridShowH", 18);
  cfgGridShowSec = (long)clampU32(h, 1, 168) * 3600L;

  uint32_t pg = prefs.getUInt("phaseGrid", 24);
  uint32_t pm = prefs.getUInt("phaseMid", 48);
  if (pg < 1) pg = 1;
  if (pm <= pg) {
    pg = 24;
    pm = 48;
  }
  if (pm > 168) pm = 168;
  cfgPhaseGridSec = (long)pg * 3600L;
  cfgPhaseMidSec = (long)pm * 3600L;
}

static void reconnectSTA(const String& ssid, const String& pass) {
  Serial.printf("[WiFi] STA connect: %s\n", ssid.c_str());
  WiFi.persistent(true);
  WiFi.mode(WIFI_STA);
  delay(80);
  WiFi.disconnect(false, false);
  delay(120);
  WiFi.begin(ssid.c_str(), pass.c_str());
  int n = 0;
  while (WiFi.status() != WL_CONNECTED && n < 60) {
    delay(250);
    n++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.printf("[WiFi] OK IP %s\n", WiFi.localIP().toString().c_str());
  } else
    Serial.println(F("[WiFi] STA failed — check SSID/password or open config portal"));
}

static String sdUploadBasenameOnly(const String& clientName) {
  int lastSep = -1;
  for (unsigned i = 0; i < clientName.length(); i++) {
    char c = clientName[i];
    if (c == '/' || c == '\\') lastSep = (int)i;
  }
  String b = lastSep < 0 ? clientName : clientName.substring((unsigned)(lastSep + 1));
  b.trim();
  return b;
}

static bool sdUploadValidBasename(const String& b) {
  if (!b.length() || b.length() > 80) return false;
  if (b.indexOf("..") >= 0) return false;
  for (unsigned i = 0; i < b.length(); i++) {
    char c = b[i];
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '.' || c == '_' || c == '-';
    if (!ok) return false;
  }
  return true;
}

// One path segment for directory names (no dots — avoids ambiguous paths)
static bool sdUploadValidDirSegment(const String& seg) {
  if (!seg.length() || seg.length() > 32) return false;
  if (seg.indexOf("..") >= 0) return false;
  for (unsigned i = 0; i < seg.length(); i++) {
    char c = seg[i];
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '-';
    if (!ok) return false;
  }
  return true;
}

// relpath like "a" or "a/b" — each segment sdUploadValidDirSegment; max 8 segments
static bool sdUploadValidRelpath(const String& relpathRaw) {
  String rel = relpathRaw;
  rel.trim();
  while (rel.length() && rel.endsWith("/")) rel.remove(rel.length() - 1);
  if (!rel.length()) return true;
  int start = 0;
  int steps = 0;
  while (start < (int)rel.length()) {
    if (++steps > 8) return false;
    int slash = rel.indexOf('/', start);
    String seg = (slash < 0) ? rel.substring(start) : rel.substring(start, slash);
    seg.trim();
    if (!seg.length()) {
      start = (slash < 0) ? (int)rel.length() : slash + 1;
      continue;
    }
    if (!sdUploadValidDirSegment(seg)) return false;
    start = (slash < 0) ? (int)rel.length() : slash + 1;
  }
  return true;
}

static bool sdMkdirChain(const String& absPath) {
  if (!absPath.length() || absPath[0] != '/') return false;
  for (unsigned i = 1; i <= absPath.length();) {
    int slash = absPath.indexOf('/', (int)i);
    unsigned end = (slash < 0) ? absPath.length() : (unsigned)slash;
    String acc = absPath.substring(0, end);
    if (acc.length() < 2) return false;
    if (!SD.exists(acc)) {
      if (!SD.mkdir(acc.c_str())) {
        Serial.printf("[HTTP] SD mkdir failed: %s\n", acc.c_str());
        return false;
      }
    }
    if (slash < 0) break;
    i = (unsigned)slash + 1;
  }
  return true;
}

// folder: sound | flags | tracks | logos | root (root: relpath required — creates /segment/...)
static bool sdWebMkdir(const String& folder, const String& relpathRaw) {
  if (folder == "root") {
    String rel = relpathRaw;
    rel.trim();
    while (rel.length() && rel.endsWith("/")) rel.remove(rel.length() - 1);
    if (!rel.length()) return false;
    if (!sdUploadValidRelpath(rel)) return false;
    return sdMkdirChain("/" + rel);
  }
  if (folder != "sound" && folder != "flags" && folder != "tracks" && folder != "logos") return false;
  String rel = relpathRaw;
  rel.trim();
  while (rel.length() && rel.endsWith("/")) rel.remove(rel.length() - 1);
  if (!sdUploadValidRelpath(rel)) return false;
  if (!rel.length()) {
    String root = "/" + folder;
    if (SD.exists(root)) return true;
    return SD.mkdir(root.c_str());
  }
  return sdMkdirChain("/" + folder + "/" + rel);
}

static String urlQueryEncodePath(const String& p) {
  String o;
  o.reserve(p.length() + 24);
  for (size_t i = 0; i < p.length(); ++i) {
    unsigned char c = (unsigned char)p[i];
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
        c == '.')
      o += (char)c;
    else if (c == '/')
      o += F("%2F");
    else {
      char buf[4];
      snprintf(buf, sizeof(buf), "%%%02X", c);
      o += buf;
    }
  }
  return o;
}

// Valid SD web paths: / or /foo/bar with safe segments (same rules as upload mkdir).
static bool sdWebBrowsePathOk(const String& p) {
  if (!p.length() || p.length() > 220) return false;
  if (p[0] != '/') return false;
  if (p.indexOf("..") >= 0) return false;
  if (p == "/") return true;
  return sdUploadValidRelpath(p.substring(1));
}

static void handleSdUploadDone() {
  auto finishBatch = []() {
    sdUploadAwaitFirstFile = true;
    sdUploadOkCount        = 0;
    sdUploadReportHtml     = "";
  };

  if (sdUploadErr.length()) {
    String html =
        F("<!DOCTYPE html><html><head><meta charset=utf-8><meta name=viewport content=\"width=device-width,initial-scale=1\">"
          "<title>Upload failed</title>"
          "<style>body{font-family:system-ui,sans-serif;background:#111;color:#eee;max-width:560px;margin:24px auto;padding:16px}a{color:#f44}</style></head><body>");
    html += F("<h1>Upload failed</h1><p>");
    html += htmlAttrEscape(sdUploadErr);
    html += F("</p><p><a href=/>← Back to settings</a></p></body></html>");
    configServer.send(400, "text/html; charset=utf-8", html);
    finishBatch();
    return;
  }
  if (sdUploadOkCount == 0) {
    String html =
        F("<!DOCTYPE html><html><head><meta charset=utf-8><meta name=viewport content=\"width=device-width,initial-scale=1\">"
          "<title>Upload</title>"
          "<style>body{font-family:system-ui,sans-serif;background:#111;color:#eee;max-width:560px;margin:24px auto;padding:16px}a{color:#f44}</style></head><body>");
    html += F("<h1>No files uploaded</h1><p>Select one or more files and try again.</p><p><a href=/>← Back to settings</a></p></body></html>");
    configServer.send(400, "text/html; charset=utf-8", html);
    finishBatch();
    return;
  }
  String html =
      F("<!DOCTYPE html><html><head><meta charset=utf-8><meta name=viewport content=\"width=device-width,initial-scale=1\">"
        "<title>Upload OK</title>"
        "<style>body{font-family:system-ui,sans-serif;background:#111;color:#eee;max-width:560px;margin:24px auto;padding:16px}a{color:#4caf50}</style></head><body>");
  html += F("<h1>Upload complete</h1><p>");
  if (sdUploadOkCount > 1) {
    html += String(sdUploadOkCount);
    html += F(" files saved:</p><p style=font-size:.9rem;line-height:1.5>");
  } else {
    html += F("Saved:</p><p style=font-size:.9rem;line-height:1.5>");
  }
  html += sdUploadReportHtml;
  html += F("</p><p><a href=/>← Back to settings</a></p></body></html>");
  configServer.send(200, "text/html; charset=utf-8", html);
  finishBatch();
}

static void handleSdMkdir() {
  if (!audioSdMounted()) {
    String html =
        F("<!DOCTYPE html><html><head><meta charset=utf-8><meta name=viewport content=\"width=device-width,initial-scale=1\">"
          "<title>mkdir failed</title>"
          "<style>body{font-family:system-ui,sans-serif;background:#111;color:#eee;max-width:560px;margin:24px auto;padding:16px}a{color:#f44}</style></head><body>");
    html += F("<h1>mkdir failed</h1><p>SD card not mounted.</p><p><a href=/>← Back</a></p></body></html>");
    configServer.send(400, "text/html; charset=utf-8", html);
    return;
  }
  String folder = configServer.hasArg("folder") ? configServer.arg("folder") : "";
  String relpath = configServer.hasArg("relpath") ? configServer.arg("relpath") : "";
  if (!sdWebMkdir(folder, relpath)) {
    String html =
        F("<!DOCTYPE html><html><head><meta charset=utf-8><meta name=viewport content=\"width=device-width,initial-scale=1\">"
          "<title>mkdir failed</title>"
          "<style>body{font-family:system-ui,sans-serif;background:#111;color:#eee;max-width:560px;margin:24px auto;padding:16px}a{color:#f44}</style></head><body>");
    html += F("<h1>mkdir failed</h1><p>Invalid path or SD error. Base: <code>sound</code>, <code>flags</code>, <code>tracks</code>, <code>logos</code>, or <code>card root</code> "
              "(root requires a subpath such as <code>myfolder</code> or <code>a/b</code>). Segments: letters, digits, <code>-</code> <code>_</code> only.</p>");
    html += F("<p><a href=/>← Back to settings</a></p></body></html>");
    configServer.send(400, "text/html; charset=utf-8", html);
    return;
  }
  String show;
  if (folder == "root") {
    String rel = relpath;
    rel.trim();
    while (rel.length() && rel.endsWith("/")) rel.remove(rel.length() - 1);
    show = "/" + rel;
  } else {
    show = "/" + folder;
    String rel = relpath;
    rel.trim();
    while (rel.length() && rel.endsWith("/")) rel.remove(rel.length() - 1);
    if (rel.length()) show += "/" + rel;
  }
  Serial.printf("[HTTP] SD mkdir OK %s\n", show.c_str());
  configServer.sendHeader("Location", "/");
  configServer.send(303);
}

static void handleSdBrowse() {
  auto errPage = [](const char* title, const String& msg, int code) -> void {
    String html =
        F("<!DOCTYPE html><html><head><meta charset=utf-8><meta name=viewport content=\"width=device-width,initial-scale=1\">"
          "<title></title>"
          "<style>body{font-family:system-ui,sans-serif;background:#111;color:#eee;max-width:720px;margin:24px auto;padding:16px}a{color:#4caf50}code{background:#222;padding:2px 6px}</style></head><body>");
    html += F("<h1>");
    html += title;
    html += F("</h1><p>");
    html += msg;
    html += F("</p><p><a href=\"/sd\">← SD browser</a> · <a href=/>Settings</a></p></body></html>");
    configServer.send(code, "text/html; charset=utf-8", html);
  };

  if (!audioSdMounted()) {
    errPage("SD browser", String(F("SD card not mounted.")), 400);
    return;
  }

  String p = "/";
  if (configServer.hasArg("p")) {
    p = configServer.arg("p");
    p.trim();
    if (!p.length()) p = "/";
  }
  if (!sdWebBrowsePathOk(p)) {
    errPage("Bad path",
            String(F("Path must be <code>/</code> or a path with safe segments (letters, digits, <code>-</code> <code>_</code> only; "
                     "no <code>..</code>).")),
            400);
    return;
  }

  String page;
  page.reserve(4096);
  page += F("<!DOCTYPE html><html><head><meta charset=utf-8><meta name=viewport content=\"width=device-width,initial-scale=1\">"
            "<title>SD card</title>"
            "<style>body{font-family:system-ui,sans-serif;background:#111;color:#eee;max-width:720px;margin:24px auto;padding:16px}"
            "a{color:#4caf50}code{background:#222;padding:2px 6px}table{width:100%;border-collapse:collapse}"
            "th,td{text-align:left;border-bottom:1px solid #333;padding:8px 6px}th{color:#888;font-weight:600}"
            ".danger{background:#721c24;color:#fff;border:0;padding:6px 10px;border-radius:4px;cursor:pointer;font-size:.85rem}"
            ".hint{color:#aaa;font-size:.9rem}</style></head><body>");
  page += F("<h1>SD card browser</h1><p class=hint>Delete removes files or <strong>empty</strong> folders only. App folders <code>/sound</code>, <code>/flags</code>, <code>/tracks</code>, <code>/logos</code> cannot be removed here.</p>");
  page += F("<p>Path: <code>");
  page += htmlAttrEscape(p);
  page += F("</code></p><p>");

  if (p == "/") {
    page += F("<a href=/>Settings</a></p>");
  } else {
    String parent = "/";
    {
      int ls = p.lastIndexOf('/');
      if (ls > 0) parent = p.substring(0, ls);
    }
    page += F("<a href=\"/sd?p=");
    page += urlQueryEncodePath(parent);
    page += F("\">↑ Up</a> · <a href=\"/sd\">Top</a> · <a href=/>Settings</a></p>");
  }

  File dir = SD.open(p);
  if (!dir) {
    page += F("<p>Cannot open path.</p></body></html>");
    configServer.send(404, "text/html; charset=utf-8", page);
    return;
  }
  if (!dir.isDirectory()) {
    dir.close();
    page += F("<p>Not a directory.</p></body></html>");
    configServer.send(404, "text/html; charset=utf-8", page);
    return;
  }

  page += F("<table><thead><tr><th>Name</th><th>Action</th></tr></thead><tbody>");

  File child = dir.openNextFile();
  while (child) {
    String nm        = child.name();
    const bool isDir = child.isDirectory();
    child            = dir.openNextFile();

    String displayName = nm;
    {
      int ls = nm.lastIndexOf('/');
      if (ls >= 0) displayName = nm.substring((unsigned)(ls + 1));
    }
    if (!displayName.length()) continue;

    String fullPath;
    if (nm.length() && nm[0] == '/')
      fullPath = nm;
    else {
      fullPath = p;
      if (!fullPath.endsWith("/")) fullPath += "/";
      fullPath += nm;
    }
    if (!sdWebBrowsePathOk(fullPath)) continue;

    page += F("<tr><td>");
    if (isDir) {
      page += F("<a href=\"/sd?p=");
      page += urlQueryEncodePath(fullPath);
      page += F("\">");
      page += htmlAttrEscape(displayName);
      page += F("/</a>");
    } else {
      page += htmlAttrEscape(displayName);
    }
    page += F("</td><td>");
    const bool canDel = (fullPath != "/sound" && fullPath != "/flags" && fullPath != "/tracks" && fullPath != "/logos");
    if (canDel) {
      page += F("<form method=POST action=/sd/delete style=\"display:inline;margin:0\" onsubmit=\"return confirm('Delete this item permanently?');\">");
      page += F("<input type=hidden name=path value=\"");
      page += htmlAttrEscape(fullPath);
      page += F("\"><button type=submit class=danger>Delete</button></form>");
    } else {
      page += F("—");
    }
    page += F("</td></tr>");
  }
  dir.close();

  page += F("</tbody></table></body></html>");
  configServer.send(200, "text/html; charset=utf-8", page);
}

static void handleSdDeletePost() {
  auto errPage = [](const String& msg) -> void {
    String html =
        F("<!DOCTYPE html><html><head><meta charset=utf-8><meta name=viewport content=\"width=device-width,initial-scale=1\">"
          "<title>Delete failed</title>"
          "<style>body{font-family:system-ui,sans-serif;background:#111;color:#eee;max-width:560px;margin:24px auto;padding:16px}a{color:#f44}</style></head><body>");
    html += F("<h1>Delete failed</h1><p>");
    html += msg;
    html += F("</p><p><a href=\"/sd\">← SD browser</a></p></body></html>");
    configServer.send(400, "text/html; charset=utf-8", html);
  };

  if (!audioSdMounted()) {
    errPage(String(F("SD card not mounted.")));
    return;
  }
  if (!configServer.hasArg("path")) {
    errPage(String(F("Missing path.")));
    return;
  }
  String path = configServer.arg("path");
  path.trim();
  if (!sdWebBrowsePathOk(path)) {
    errPage(String(F("Invalid path.")));
    return;
  }
  if (path == "/" || path == "/sound" || path == "/flags" || path == "/tracks" || path == "/logos") {
    errPage(String(F("That folder cannot be deleted from the browser.")));
    return;
  }
  if (!SD.exists(path.c_str())) {
    String parent = "/";
    int ls        = path.lastIndexOf('/');
    if (ls > 0) parent = path.substring(0, ls);
    configServer.sendHeader("Location", String("/sd?p=") + urlQueryEncodePath(parent));
    configServer.send(303);
    return;
  }

  File f = SD.open(path);
  if (!f) {
    errPage(String(F("Cannot open path on SD.")));
    return;
  }
  const bool isDir = f.isDirectory();
  f.close();

  bool ok = isDir ? SD.rmdir(path.c_str()) : SD.remove(path.c_str());
  if (!ok) {
    if (isDir)
      errPage(String(F("Cannot remove (folder not empty or SD error). Remove files inside first.")));
    else
      errPage(String(F("Cannot remove file (SD error or read-only).")));
    return;
  }
  Serial.printf("[HTTP] SD delete OK %s (%s)\n", path.c_str(), isDir ? "DIR" : "FILE");

  String parent = "/";
  int ls        = path.lastIndexOf('/');
  if (ls > 0) parent = path.substring(0, ls);
  configServer.sendHeader("Location", String("/sd?p=") + urlQueryEncodePath(parent));
  configServer.send(303);
}

static void handleSdUploadChunk() {
  HTTPUpload& u = configServer.upload();
  if (u.status == UPLOAD_FILE_START) {
    if (sdUploadOut) sdUploadOut.close();
    if (sdUploadAwaitFirstFile) {
      sdUploadErr          = "";
      sdUploadReportHtml   = "";
      sdUploadOkCount      = 0;
      sdUploadAwaitFirstFile = false;
    }
    if (sdUploadErr.length()) return;

    sdUploadBytes = 0;
    sdUploadRmPath = "";

    if (!audioSdMounted()) {
      sdUploadErr = "SD card not mounted";
      return;
    }
    String folder = configServer.hasArg("folder") ? configServer.arg("folder") : "sound";
    String sub = configServer.hasArg("subpath") ? configServer.arg("subpath") : "";
    sub.trim();
    while (sub.length() && sub.endsWith("/")) sub.remove(sub.length() - 1);

    if (folder == "root") {
      if (!sub.length()) {
        sdUploadErr = "For card root, enter subpath to your folder (e.g. myfolder or myfolder/sub)";
        return;
      }
      if (!sdUploadValidRelpath(sub)) {
        sdUploadErr = "Invalid subpath (segments: letters, digits, - _ only; max 8 levels)";
        return;
      }
    } else if (folder != "sound" && folder != "flags" && folder != "tracks" && folder != "logos") {
      sdUploadErr = "Invalid destination folder";
      return;
    } else {
      if (!sdUploadValidRelpath(sub)) {
        sdUploadErr = "Invalid subpath (segments: letters, digits, - _ only; max 8 levels)";
        return;
      }
    }

    String mid;
    if (folder != "root" && sub.length()) mid = "/" + sub;
    String base = sdUploadBasenameOnly(String(u.filename));
    if (!sdUploadValidBasename(base)) {
      sdUploadErr = "Invalid filename (use letters, digits, . _ - only)";
      return;
    }

    if (folder == "root")
      sdUploadRmPath = "/" + sub + "/" + base;
    else
      sdUploadRmPath = "/" + folder + mid + "/" + base;
    {
      String parent = sdUploadRmPath;
      int ls = parent.lastIndexOf('/');
      if (ls > 0) {
        parent = parent.substring(0, ls);
        if (!sdMkdirChain(parent)) {
          sdUploadErr = "Cannot create parent directories on SD";
          sdUploadRmPath = "";
          return;
        }
      }
    }
    sdUploadOut    = SD.open(sdUploadRmPath, FILE_WRITE);
    if (!sdUploadOut) {
      sdUploadErr = "Cannot open file on SD (is the folder there? Is the card full?)";
      sdUploadRmPath = "";
      return;
    }
  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (sdUploadErr.length()) return;
    if (!sdUploadOut) {
      sdUploadErr = "SD write: file not open";
      return;
    }
    if (sdUploadBytes + (size_t)u.currentSize > kSdUploadMaxBytes) {
      sdUploadErr = "File too large (max 12 MB)";
      sdUploadOut.close();
      if (sdUploadRmPath.length()) SD.remove(sdUploadRmPath.c_str());
      sdUploadRmPath = "";
      return;
    }
    size_t n = sdUploadOut.write(u.buf, u.currentSize);
    if (n != (size_t)u.currentSize) {
      sdUploadErr = "SD write error";
      sdUploadOut.close();
      if (sdUploadRmPath.length()) SD.remove(sdUploadRmPath.c_str());
      sdUploadRmPath = "";
      return;
    }
    sdUploadBytes += n;
  } else if (u.status == UPLOAD_FILE_END) {
    if (sdUploadOut) sdUploadOut.close();
    if (sdUploadErr.length()) {
      if (sdUploadRmPath.length()) SD.remove(sdUploadRmPath.c_str());
    } else {
      String saved = sdUploadRmPath;
      sdUploadOkCount++;
      if (sdUploadReportHtml.length()) sdUploadReportHtml += F("<br>");
      sdUploadReportHtml += htmlAttrEscape(saved);
      sdUploadReportHtml += " (";
      sdUploadReportHtml += String((unsigned long)sdUploadBytes);
      sdUploadReportHtml += " bytes)";
      Serial.printf("[HTTP] SD upload OK %s (%u bytes)\n", saved.c_str(), (unsigned)sdUploadBytes);
    }
    sdUploadRmPath = "";
  } else if (u.status == UPLOAD_FILE_ABORTED) {
    if (sdUploadOut) sdUploadOut.close();
    if (sdUploadRmPath.length()) SD.remove(sdUploadRmPath.c_str());
    sdUploadErr    = "Upload aborted";
    sdUploadRmPath = "";
  }
}

// Live list of directories at SD "/" for the settings page (matches Browse)
static void appendSdRootFolderListHtml(String& page) {
  if (!audioSdMounted()) return;
  File root = SD.open("/");
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    return;
  }
  page += F("<h2>Folders at card root</h2>");
  page += F("<p class=hint>Directories currently on the SD volume. Upload: pick <strong>card root</strong> + subpath matching a folder name below.</p><ul style=margin:8px 0;padding-left:1.2rem>");
  bool any = false;
  File child = root.openNextFile();
  while (child) {
    String nm     = child.name();
    bool isDir    = child.isDirectory();
    child         = root.openNextFile();
    int ls        = nm.lastIndexOf('/');
    if (ls >= 0) nm = nm.substring((unsigned)(ls + 1));
    if (!nm.length() || nm[0] == '.') continue;
    String full = (nm.length() && nm[0] == '/') ? nm : String("/") + nm;
    if (!isDir || !sdWebBrowsePathOk(full)) continue;
    any = true;
    page += F("<li><code>");
    page += htmlAttrEscape(full);
    page += F("</code> — <a href=\"/sd?p=");
    page += urlQueryEncodePath(full);
    page += F("\">browse</a></li>");
  }
  root.close();
  if (!any)
    page += F("<li class=hint style=list-style:none;margin-left:-1rem>(none listed)</li>");
  page += F("</ul>");
}

static String fileEntryBaseName(File& ent) {
  String nm = ent.name();
  int ls = nm.lastIndexOf('/');
  if (ls >= 0) nm = nm.substring((unsigned)(ls + 1));
  return nm;
}

static constexpr int             kSoundWavListMax = 48;
static void collectWavsFromDir(const String& absDir, const String& relPrefix, String* out, int cap,
                               int* n) {
  if (*n >= cap) return;
  File dir = SD.open(absDir);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return;
  }
  for (;;) {
    File ent = dir.openNextFile();
    if (!ent) break;
    String base = fileEntryBaseName(ent);
    if (!base.length() || base[0] == '.') {
      ent.close();
      continue;
    }
    if (ent.isDirectory()) {
      String nextAbs = absDir + "/" + base;
      String nextRel = relPrefix + base + "/";
      ent.close();
      collectWavsFromDir(nextAbs, nextRel, out, cap, n);
    } else {
      String low = base;
      low.toLowerCase();
      if (low.endsWith(F(".wav"))) {
        if (*n < cap) {
          out[*n] = relPrefix + base;
          (*n)++;
        }
      }
      ent.close();
    }
  }
  dir.close();
}

static void sortWavStrings(String* a, int n) {
  for (int i = 0; i < n; i++)
    for (int j = i + 1; j < n; j++)
      if (a[i] > a[j]) {
        String t = a[i];
        a[i] = a[j];
        a[j] = t;
      }
}

static void appendSoundWavSelectHtml(String& page, const char* selectName, const char* selectId,
                                     const String& currentRel, const char* emptyListFallback) {
  String list[kSoundWavListMax];
  int n = 0;
  if (audioSdMounted()) collectWavsFromDir("/sound", "", list, kSoundWavListMax, &n);
  sortWavStrings(list, n);

  page += F("<label for=\"");
  page += selectId;
  page += F("\">WAV under <code>/sound/</code></label><select name=\"");
  page += selectName;
  page += F("\" id=\"");
  page += selectId;
  page += F("\">");

  bool foundCurrent = false;
  for (int i = 0; i < n; i++) {
    if (list[i] == currentRel) foundCurrent = true;
    page += F("<option value=\"");
    page += htmlAttrEscape(list[i]);
    page += F("\"");
    if (list[i] == currentRel) page += F(" selected");
    page += F(">");
    page += htmlAttrEscape(list[i]);
    page += F("</option>");
  }
  if (!foundCurrent && currentRel.length() && soundRelPathOk(currentRel)) {
    page += F("<option value=\"");
    page += htmlAttrEscape(currentRel);
    page += F("\" selected>");
    page += htmlAttrEscape(currentRel);
    page += F(" (saved)</option>");
    foundCurrent = true;
  }
  if (n == 0 && !foundCurrent) {
    page += F("<option value=\"");
    page += emptyListFallback;
    page += F("\" selected>");
    page += emptyListFallback;
    page += F(" — no .wav found on SD</option>");
  }
  page += F("</select>");
}

static void persistSoundPrefsFromRequest() {
  prefs.putBool("soundBoot", configServer.hasArg("soundBoot"));
  prefs.putBool("soundLoaded", configServer.hasArg("soundLoaded"));
  prefs.putBool("soundUpdate", configServer.hasArg("soundUpdate"));
  if (configServer.hasArg("soundBootFile")) {
    String rel = normalizeSoundRel(configServer.arg("soundBootFile"));
    if (!rel.length()) rel = "boot.wav";
    if (soundRelPathOk(rel)) prefs.putString("soundBootFile", rel);
  }
  if (configServer.hasArg("soundLoadedFile")) {
    String rel = normalizeSoundRel(configServer.arg("soundLoadedFile"));
    if (!rel.length()) rel = "loaded.wav";
    if (soundRelPathOk(rel)) prefs.putString("soundLoadedFile", rel);
  }
  if (configServer.hasArg("soundUpdateFile")) {
    String rel = normalizeSoundRel(configServer.arg("soundUpdateFile"));
    if (!rel.length()) rel = "update.wav";
    if (soundRelPathOk(rel)) prefs.putString("soundUpdateFile", rel);
  }
  prefs.putBool("quietEn", configServer.hasArg("quietEn"));
  {
    uint32_t q = configServer.hasArg("quietSh") ? (uint32_t)configServer.arg("quietSh").toInt()
                                                : (uint32_t)cfgQuietStartH;
    prefs.putUChar("quietSh", (uint8_t)clampU32(q, 0, 23));
    q = configServer.hasArg("quietEh") ? (uint32_t)configServer.arg("quietEh").toInt()
                                       : (uint32_t)cfgQuietEndH;
    prefs.putUChar("quietEh", (uint8_t)clampU32(q, 0, 23));
  }
}

static void setupConfigWeb() {
  configServer.on("/", HTTP_GET, []() {
    String ip = WiFi.localIP().toString();
    auto num = [](uint32_t v) -> String { return String(v); };
    auto chk = [](bool on) -> const char* { return on ? " checked" : ""; };
    long gridH = cfgGridShowSec / 3600;
    long progH = cfgRaceInProgSec / 3600;
    long befH = cfgRaceWinBeforeSec / 3600;
    long aftH = cfgRaceWinAfterSec / 3600;
    long phG = cfgPhaseGridSec / 3600;
    long phM = cfgPhaseMidSec / 3600;
    long cCal = cfgCacheCalMs / 60000UL;
    long cSt = cfgCacheStandMs / 60000UL;
    long cQl = cfgCacheQualMs / 60000UL;
    long tQa = cfgTtlQualAvailMs / 60000UL;
    long tRs = cfgTtlResultsMs / 60000UL;

    String ssidForm = prefs.getString("wifiSsid", "");
    if (!ssidForm.length()) ssidForm = WiFi.SSID();

    String page;
    page.reserve(10240);
    page += F("<!DOCTYPE html><html><head><meta charset=utf-8><meta name=viewport content=\"width=device-width,initial-scale=1\">"
              "<title>F1 Tracker</title>"
              "<style>"
              ":root{--bg:#0d0d0d;--card:#161616;--border:#2a2a2a;--muted:#8a9aab;--accent:#e10600}"
              "*{box-sizing:border-box}"
              "body{font-family:system-ui,Segoe UI,sans-serif;max-width:720px;margin:0 auto;padding:20px 18px 32px;background:var(--bg);color:#eee}"
              "h1{font-size:1.35rem;font-weight:700;margin:0 0 4px}"
              ".sub{color:var(--muted);font-size:.85rem;margin:0 0 16px}"
              "h2{font-size:.95rem;margin:18px 0 10px;color:var(--accent);font-weight:600}"
              "h2:first-child{margin-top:0}"
              "label{display:block;margin:12px 0 4px;color:var(--muted);font-size:.78rem;text-transform:uppercase;letter-spacing:.04em}"
              "input[type=number],input[type=text],input[type=password],select{width:100%;max-width:440px;padding:10px 12px;background:#121212;border:1px solid var(--border);color:#fff;border-radius:8px}"
              ".row{display:flex;align-items:center;gap:10px;margin:10px 0}"
              ".row label{margin:0;text-transform:none;letter-spacing:0}"
              "button,.btn{margin-top:12px;padding:12px 20px;background:var(--accent);color:#fff;border:none;border-radius:8px;font-weight:600;cursor:pointer;font-size:.95rem}"
              "button.secondary,.btn.secondary{background:#333}"
              "button.danger{background:#8b0000}"
              ".hint{font-size:.75rem;color:#666;margin:4px 0 0;line-height:1.4}"
              ".nav{display:flex;flex-wrap:wrap;gap:6px;margin:16px 0 8px;padding:4px;background:var(--card);border-radius:10px;border:1px solid var(--border)}"
              ".nav button{flex:1;min-width:108px;margin:0;padding:10px 8px;background:transparent;color:var(--muted);border-radius:8px;font-weight:600;font-size:.78rem}"
              ".nav button.on{background:#222;color:#fff;box-shadow:0 0 0 1px var(--border)}"
              ".tabp{display:none;background:var(--card);border:1px solid var(--border);border-radius:12px;padding:16px 18px 20px;margin-bottom:16px}"
              ".tabp.vis{display:block}"
              "footer{margin-top:8px;font-size:.72rem;color:#555;line-height:1.45}"
              "code{font-size:.85em;background:#222;padding:2px 6px;border-radius:4px}"
              "</style></head><body>");
    page += F("<h1>F1 Tracker</h1><p class=sub>IP ");
    page += ip;
    page += F(" · settings</p><nav class=nav aria-label=Sections>");
    page += F("<button type=button class=on data-i=0>Schedule</button>");
    page += F("<button type=button data-i=1>Audio &amp; time</button>");
    page += F("<button type=button data-i=2>API cache</button>");
    page += F("<button type=button data-i=3>Wi‑Fi</button>");
    page += F("<button type=button data-i=4>SD &amp; system</button>");
    page += F("</nav>");

    page += F("<form id=mainSave method=POST action=/save>");
    page += F("<div class=row style=flex-wrap:wrap;justify-content:space-between;align-items:center;margin:4px 0 14px;gap:10px>"
              "<span class=hint style=margin:0>Save schedule, audio, API, Wi‑Fi; startup lives on <strong>SD &amp; system</strong>.</span>"
              "<button type=submit>Save all settings</button></div>");
    page += F("<div class='tabp vis' data-tab=0><h2>Display refresh schedule</h2>");
    page += F("<label>Race weekend window — every N minutes</label><input type=number name=updRace min=1 max=1440 value=");
    page += num(cfgUpdRaceMin);
    page += F("><p class=hint>While within “race window” before/after lights out.</p>");
    page += F("<label>Final phase (&lt; ");
    page += num((uint32_t)phG);
    page += F(" h before race) — every N minutes</label><input type=number name=updGrid min=1 max=1440 value=");
    page += num(cfgUpdGridMin);
    page += F("><p class=hint>Grid lookup and frequent polls before qualifying data lands.</p>");
    page += F("<label>Mid phase — at :00 every N hours (from ");
    page += num((uint32_t)phG);
    page += F(" h to ");
    page += num((uint32_t)phM);
    page += F(" h before)</label><input type=number name=hrNear min=1 max=24 value=");
    page += num(cfgUpdHrNear);
    page += F(">");
    page += F("<label>Far phase — at :00 every N hours (&gt; ");
    page += num((uint32_t)phM);
    page += F(" h before)</label><input type=number name=hrFar min=1 max=24 value=");
    page += num(cfgUpdHrFar);
    page += F(">");

    page += F("<h2>Race window timing (hours)</h2>");
    page += F("<label>Race “in progress” grace after lights out</label><input type=number name=raceProgH min=1 max=48 value=");
    page += num((uint32_t)progH);
    page += F(">");
    page += F("<label>Hours before lights out counted as race window</label><input type=number name=raceBefH min=1 max=72 value=");
    page += num((uint32_t)befH);
    page += F(">");
    page += F("<label>Hours after lights out counted as race window</label><input type=number name=raceAftH min=1 max=72 value=");
    page += num((uint32_t)aftH);
    page += F(">");
    page += F("<label>Show starting grid when race is within N hours</label><input type=number name=gridShowH min=1 max=168 value=");
    page += num((uint32_t)gridH);
    page += F(">");

    page += F("<h2>Schedule phase boundaries (hours before lights out)</h2>");
    page += F("<label>Start “final phase” at N hours (frequent grid polls)</label><input type=number name=phaseGrid min=1 max=167 value=");
    page += num((uint32_t)phG);
    page += F(">");
    page += F("<label>End “far phase” at N hours (“mid” uses hourly schedule above)</label><input type=number name=phaseMid min=2 max=168 value=");
    page += num((uint32_t)phM);
    page += F("><p class=hint>Must be greater than “final phase” start.</p></div>");

    page += F("<div class=tabp data-tab=1><h2>Audio</h2><label>Volume 0–100 (%)</label><input type=number name=vol min=0 max=100 value=");
    page += num(cfgVol);
    page += F("><p class=hint>0 mutes WAV output.</p>");
    page += F("<p class=hint style=margin-top:10px>Pick a WAV from <code>/sound/</code> (SD must be mounted to list files). "
              "Ticking a box does <strong>not</strong> apply by itself — use <strong>Apply sound settings now</strong> or <strong>Save all settings</strong>.</p>");
    page += F("<h3 style=font-size:.88rem;margin:14px 0 8px>Sound clips</h3>");
    page += F("<div class=row><input type=checkbox name=soundBoot id=sboot value=1");
    page += chk(cfgSoundBoot);
    page += F("><label for=sboot>Boot — after Wi‑Fi connects</label></div>");
    appendSoundWavSelectHtml(page, "soundBootFile", "sbf", cfgSoundBootFile, "boot.wav");
    page += F("<div class=row><input type=checkbox name=soundLoaded id=sloaded value=1");
    page += chk(cfgSoundLoaded);
    page += F("><label for=sloaded>Loaded — after first screen draw</label></div>");
    appendSoundWavSelectHtml(page, "soundLoadedFile", "slf", cfgSoundLoadedFile, "loaded.wav");
    page += F("<div class=row><input type=checkbox name=soundUpdate id=supdate value=1");
    page += chk(cfgSoundUpdate);
    page += F("><label for=supdate>Update — after scheduled refresh, force data refresh, or manual redraw (no data)</label></div>");
    appendSoundWavSelectHtml(page, "soundUpdateFile", "suf", cfgSoundUpdateFile, "update.wav");
    page += F("<h3 style=font-size:.88rem;margin:14px 0 8px>Quiet hours</h3>");
    page += F("<div class=row><input type=checkbox name=quietEn id=quieten value=1");
    page += chk(cfgQuietHoursEn);
    page += F("><label for=quieten>Silence WAV clips during this window (local time, same TZ as the clock)</label></div>");
    page += F("<label>Start hour (0–23)</label><input type=number name=quietSh id=quietsh min=0 max=23 value=");
    page += num((uint32_t)cfgQuietStartH);
    page += F(">");
    page += F("<label>End hour (0–23, exclusive)</label><input type=number name=quietEh id=quieteh min=0 max=23 value=");
    page += num((uint32_t)cfgQuietEndH);
    page += F("><p class=hint>Same day: 9 and 17 → 09:00–16:59 quiet. Overnight: 22 and 7 → 22:00–06:59 quiet. If start = end, no time is silenced.</p>");
    page += F("<button type=\"submit\" formaction=\"/save_sounds\" formmethod=\"post\" class=\"secondary\">Apply sound settings now</button>");
    page += F("<h2>Clock</h2>");
    page += F("<div class=row><input type=checkbox name=clock24h id=c24 value=1");
    page += chk(cfgClock24h);
    page += F("><label for=c24>24-hour time (header and next-race box)</label></div></div>");

    page += F("<div class=tabp data-tab=2><h2>API cache (minutes)</h2>");
    page += F("<label>Calendar JSON</label><input type=number name=cacheCal min=1 max=10080 value=");
    page += num((uint32_t)cCal);
    page += F(">");
    page += F("<label>Driver &amp; constructor standings</label><input type=number name=cacheStand min=1 max=10080 value=");
    page += num((uint32_t)cSt);
    page += F(">");
    page += F("<label>Qualifying JSON</label><input type=number name=cacheQual min=1 max=43200 value=");
    page += num((uint32_t)cQl);
    page += F(">");
    page += F("<label>Remember qualifying availability probe</label><input type=number name=ttlQualAv min=1 max=10080 value=");
    page += num((uint32_t)tQa);
    page += F(">");
    page += F("<label>Remember results availability probe</label><input type=number name=ttlRes min=1 max=1440 value=");
    page += num((uint32_t)tRs);
    page += F("></div>");

    page += F("<div class=tabp data-tab=3><h2>Wi‑Fi</h2>");
    page += F("<p class=hint>Changing network briefly disconnects Wi‑Fi; if the IP changes, use the address on the e‑paper header or Serial.</p>");
    page += F("<div class=row><input type=checkbox name=wifiApply id=wfap value=1><label for=wfap>Apply new network below</label></div>");
    page += F("<label>SSID (max 32)</label><input type=text name=wifiSsid maxlength=32 value=\"");
    page += htmlAttrEscape(ssidForm);
    page += F("\">");
    page += F("<label>Password (max 63; leave blank to keep last saved password)</label><input type=password name=wifiPass maxlength=63 autocomplete=off>");

    page += F("<div class=row><input type=checkbox name=wifiAlways id=w value=1");
    page += chk(cfgWifiAlways);
    page += F("><label for=w>Keep Wi‑Fi on for this page (disable to save power; no web UI while off)</label></div>");

    page += F("<p class=hint style=margin-top:16px>Applies schedule, audio (incl. quiet hours), API, and Wi‑Fi. Use <strong>Save all settings</strong> for startup splash (SD &amp; system tab).</p>");
    page += F("<button type=submit>Save all settings</button></div></form>");

    page += F("<div class=tabp data-tab=4><h2>Upload to SD card</h2>");
    page += F("<p class=hint>Files go under <code>/sound</code>, <code>/flags</code>, <code>/tracks</code>, <code>/logos</code>, or any path under <code>/</code> via <strong>card root</strong>. "
              "Optional subfolder (same rules as mkdir). Filename: letters, digits, <code>.</code> <code>-</code> <code>_</code>. "
              "Max 12&nbsp;MB per file. Multi-select: Ctrl/Cmd+click.</p>");
    if (audioSdMounted()) {
      appendSdRootFolderListHtml(page);
      page += F("<form method=POST action=/upload enctype=multipart/form-data>"
                "<label>Destination folder</label>"
                "<select name=folder id=sdfol>"
                "<option value=sound>sound</option><option value=flags>flags</option><option value=tracks>tracks</option>"
                "<option value=logos>logos (constructor .raw)</option>"
                "<option value=root>card root (subpath required)</option></select>"
                "<label>Subfolder (optional)</label><input type=text name=subpath maxlength=128 placeholder=\"e.g. backup or nest/here\">"
                "<label>Files</label><input type=file name=file required multiple>"
                "<button type=submit>Upload to SD</button></form>");
      page += F("<h2>Create folder on SD</h2>");
      page += F("<p class=hint>Creates <code>/sound/…</code>, <code>/flags/…</code>, <code>/tracks/…</code>, <code>/logos/…</code>, or at <strong>card root</strong> (e.g. <code>/backup</code>, <code>/org/misc</code>). "
                "Segments: letters, digits, <code>-</code> <code>_</code> only. For sound/flags/tracks/logos, empty subpath = ensure that base folder exists only. <strong>Card root</strong> requires a subpath (cannot create <code>/</code>).</p>");
      page += F("<form method=POST action=/mkdir>"
                "<label>Base</label>"
                "<select name=folder>"
                "<option value=sound>sound</option><option value=flags>flags</option><option value=tracks>tracks</option>"
                "<option value=logos>logos</option>"
                "<option value=root>card root</option></select>"
                "<label>Subpath</label><input type=text name=relpath maxlength=128 placeholder=\"sound/flags/tracks/logos: optional; root: required\">"
                "<button type=submit class=secondary>Create folders</button></form>");
      page += F("<p><a href=/sd>Browse and delete files on SD</a> (whole card with safe paths).</p>");
    } else {
      page += F("<p class=hint><strong>SD not mounted</strong> — upload and mkdir are unavailable.</p>");
    }

    page += F("<h2>Startup</h2>");
    page += F("<div class=row><input type=checkbox name=bootSplash id=bootsplash value=1 form=mainSave");
    page += chk(cfgBootSplash);
    page += F("><label for=bootsplash>Show F1 boot splash on power-up (logo + quote). Submits with <strong>Save all settings</strong> (top of page).</label></div>");

    page += F("<h2>System</h2>");
    page += F("<p class=hint><strong>Redraw e‑paper only</strong> — same data as in memory (no API calls). Clock updates from NTP if time sync runs; works offline if time is already valid. Plays the <strong>Update</strong> sound when that option is enabled.</p>");
    page += F("<form method=POST action=/refresh_screen onsubmit=\"return confirm('Redraw the display using cached data only?');\">");
    page += F("<button type=submit class=secondary>Redraw screen (no data update)</button></form>");
    page += F("<p class=hint><strong>Force display refresh</strong> — clears in‑memory API cache, re‑fetches data, redraws the e‑paper (same as a scheduled update). Wi‑Fi must be on.</p>");
    page += F("<form method=POST action=/refresh onsubmit=\"return confirm('Redraw the e-paper and refresh data now?');\">");
    page += F("<button type=submit class=secondary>Force display refresh</button></form>");
    page += F("<p class=hint>Restart the device (brief disconnect from this page; you may need to reconnect).</p>");
    page += F("<form method=POST action=/reboot onsubmit=\"return confirm('Reboot the F1 Tracker now?');\">");
    page += F("<button type=submit class=danger>Reboot device</button></form>");

    page += F("<footer>Changes apply immediately where noted. Audio volume updates without reboot.</footer></div>");

    page += F("<script>(function(){var n=document.querySelectorAll('.nav button'),t=document.querySelectorAll('.tabp');"
              "function s(i){for(var j=0;j<t.length;j++){t[j].classList.toggle('vis',j===i);n[j].classList.toggle('on',j===i);}}"
              "for(var k=0;k<n.length;k++)(function(i){n[i].onclick=function(){s(i);};})(k);s(0);})();</script>");
    page += F("</body></html>");
    configServer.send(200, "text/html; charset=utf-8", page);
  });

  configServer.on("/save", HTTP_POST, []() {
    auto getU = [](const String& name, uint32_t def) -> uint32_t {
      if (!configServer.hasArg(name)) return def;
      return (uint32_t)configServer.arg(name).toInt();
    };
    uint32_t v;
    v = getU("vol", cfgVol);
    if (v > 100) v = 100;
    prefs.putUChar("vol", (uint8_t)v);

    v = getU("updRace", cfgUpdRaceMin);
    prefs.putUInt("updRace", clampU32(v, 1, 1440));
    v = getU("updGrid", cfgUpdGridMin);
    prefs.putUInt("updGrid", clampU32(v, 1, 1440));

    v = getU("hrNear", cfgUpdHrNear);
    prefs.putUInt("hrNear", clampU32(v, 1, 24));
    v = getU("hrFar", cfgUpdHrFar);
    prefs.putUInt("hrFar", clampU32(v, 1, 24));

    v = getU("cacheCal", cfgCacheCalMs / 60000UL);
    prefs.putUInt("cacheCal", clampU32(v, 1, 10080));
    v = getU("cacheStand", cfgCacheStandMs / 60000UL);
    prefs.putUInt("cacheStand", clampU32(v, 1, 10080));
    v = getU("cacheQual", cfgCacheQualMs / 60000UL);
    prefs.putUInt("cacheQual", clampU32(v, 1, 43200));
    v = getU("ttlQualAv", cfgTtlQualAvailMs / 60000UL);
    prefs.putUInt("ttlQualAv", clampU32(v, 1, 10080));
    v = getU("ttlRes", cfgTtlResultsMs / 60000UL);
    prefs.putUInt("ttlRes", clampU32(v, 1, 1440));

    prefs.putBool("wifiAlways", configServer.hasArg("wifiAlways"));
    prefs.putBool("bootSplash", configServer.hasArg("bootSplash"));
    persistSoundPrefsFromRequest();
    prefs.putBool("clock24h", configServer.hasArg("clock24h"));

    v = getU("raceProgH", (uint32_t)(cfgRaceInProgSec / 3600));
    prefs.putUInt("raceProgH", clampU32(v, 1, 48));
    v = getU("raceBefH", (uint32_t)(cfgRaceWinBeforeSec / 3600));
    prefs.putUInt("raceBefH", clampU32(v, 1, 72));
    v = getU("raceAftH", (uint32_t)(cfgRaceWinAfterSec / 3600));
    prefs.putUInt("raceAftH", clampU32(v, 1, 72));
    v = getU("gridShowH", (uint32_t)(cfgGridShowSec / 3600));
    prefs.putUInt("gridShowH", clampU32(v, 1, 168));

    uint32_t pg = getU("phaseGrid", (uint32_t)(cfgPhaseGridSec / 3600));
    uint32_t pm = getU("phaseMid", (uint32_t)(cfgPhaseMidSec / 3600));
    pg = clampU32(pg, 1, 167);
    pm = clampU32(pm, 2, 168);
    if (pm <= pg) {
      pg = 24;
      pm = 48;
    }
    prefs.putUInt("phaseGrid", pg);
    prefs.putUInt("phaseMid", pm);

    loadDeviceConfig();

    if (configServer.hasArg("wifiApply")) {
      String ns = configServer.hasArg("wifiSsid") ? configServer.arg("wifiSsid") : "";
      ns.trim();
      if (ns.length() > 0) {
        if (configServer.hasArg("wifiPass") && configServer.arg("wifiPass").length() > 0)
          prefs.putString("wifiKey", configServer.arg("wifiPass"));
        String wp = prefs.getString("wifiKey", "");
        prefs.putString("wifiSsid", ns);
        reconnectSTA(ns, wp);
      }
    }

    configServer.sendHeader("Location", "/");
    configServer.send(303);
  });

  configServer.on("/save_sounds", HTTP_POST, []() {
    uint32_t v;
    if (configServer.hasArg("vol")) {
      v = (uint32_t)configServer.arg("vol").toInt();
      if (v > 100) v = 100;
      prefs.putUChar("vol", (uint8_t)v);
    }
    persistSoundPrefsFromRequest();
    loadDeviceConfig();
    configServer.sendHeader("Location", "/");
    configServer.send(303);
  });

  configServer.on("/mkdir", HTTP_GET, []() {
    configServer.sendHeader("Location", "/");
    configServer.send(303, "text/plain", "");
  });
  configServer.on("/mkdir", HTTP_POST, handleSdMkdir);

  configServer.on("/upload", HTTP_GET, []() {
    // POST only for actual upload; browsers open this URL with GET — send them to the form on /
    configServer.sendHeader("Location", "/");
    configServer.send(303, "text/plain", "Open the home page and use \"Upload to SD\" (POST only).\r\n");
  });
  configServer.on("/upload", HTTP_POST, handleSdUploadDone, handleSdUploadChunk);

  configServer.on("/sd", HTTP_GET, handleSdBrowse);
  configServer.on("/sd/delete", HTTP_GET, []() {
    configServer.sendHeader("Location", "/sd");
    configServer.send(303, "text/plain", "");
  });
  configServer.on("/sd/delete", HTTP_POST, handleSdDeletePost);

  configServer.on("/reboot", HTTP_GET, []() {
    configServer.sendHeader("Location", "/");
    configServer.send(303);
  });
  configServer.on("/refresh", HTTP_GET, []() {
    configServer.sendHeader("Location", "/");
    configServer.send(303);
  });
  configServer.on("/refresh", HTTP_POST, []() {
    forceDisplayRefreshPending = true;
    Serial.println(F("[HTTP] Force display refresh requested"));
    configServer.send(200, "text/html; charset=utf-8",
                       "<!DOCTYPE html><html><head><meta charset=utf-8><meta name=viewport "
                       "content=\"width=device-width,initial-scale=1\"><title>Refreshing</title>"
                       "<style>body{font-family:system-ui,sans-serif;background:#111;color:#eee;"
                       "text-align:center;padding:48px 20px}</style></head><body>"
                       "<h1>Refresh started</h1><p>The display will update shortly. Return to "
                       "<a href=\"/\" style=color:#e10600>settings</a>.</p></body></html>");
  });
  configServer.on("/refresh_screen", HTTP_GET, []() {
    configServer.sendHeader("Location", "/");
    configServer.send(303);
  });
  configServer.on("/refresh_screen", HTTP_POST, []() {
    screenRedrawNoDataPending = true;
    Serial.println(F("[HTTP] Redraw screen (no data) requested"));
    configServer.send(200, "text/html; charset=utf-8",
                       "<!DOCTYPE html><html><head><meta charset=utf-8><meta name=viewport "
                       "content=\"width=device-width,initial-scale=1\"><title>Redraw</title>"
                       "<style>body{font-family:system-ui,sans-serif;background:#111;color:#eee;"
                       "text-align:center;padding:48px 20px}</style></head><body>"
                       "<h1>Redraw started</h1><p>The e‑paper will refresh from cached data. Return to "
                       "<a href=\"/\" style=color:#e10600>settings</a>.</p></body></html>");
  });
  configServer.on("/reboot", HTTP_POST, []() {
    Serial.println(F("[HTTP] Reboot requested via web UI"));
    configServer.send(200, "text/html; charset=utf-8",
                       "<!DOCTYPE html><html><head><meta charset=utf-8><meta name=viewport "
                       "content=\"width=device-width,initial-scale=1\"><title>Rebooting</title>"
                       "<style>body{font-family:system-ui,sans-serif;background:#111;color:#eee;"
                       "text-align:center;padding:48px 20px}</style></head><body>"
                       "<h1>Rebooting…</h1><p>Wait a few seconds, then open the device IP again.</p></body></html>");
    delay(250);
    ESP.restart();
  });

  configServer.begin();
  Serial.println(F("[HTTP] Config server on :80"));
}

static inline void handleConfigServer() {
  if (WiFi.status() == WL_CONNECTED) configServer.handleClient();
}

// ═══════════════════════════════════════════════════════════════════
//  HTTP / JSON
// ═══════════════════════════════════════════════════════════════════
template<typename F>
static bool httpDeserializeWithFilter(Stream& s, StaticJsonDocument<API_SCRATCH_DOC_SIZE>& tgt, F* filter) {
  DeserializationError err = filter ? deserializeJson(tgt,s,DeserializationOption::Filter(*filter)) : deserializeJson(tgt,s);
  if(err){Serial.printf("[ERR] JSON: %s\n",err.c_str()); return false;} return true;
}
template<typename F>
static bool httpGetJsonRaw(const char* url, F* filter=nullptr) {
  if (apiNetworkFetchesDisabled) return false;
  if (!ensureWiFiConnected()) return false;
  doc.clear();
  Serial.printf("[HTTP] %s\n",url);
  HTTPClient http; http.useHTTP10(true); http.begin(url);
  http.setConnectTimeout(20000); http.setTimeout(25000);
  http.addHeader("User-Agent","F1Tracker/PhotoPainter");
  int code=http.GET(); Serial.printf("[HTTP] %d\n",code);
  if(code!=HTTP_CODE_OK){http.end(); return false;}
  bool ok=httpDeserializeWithFilter(http.getStream(),doc,filter);
  http.end(); return ok;
}
template<typename F>
static bool httpGetJsonWithRetry(const char* url, int tries=3, int backoff=500, F* filter=nullptr) {
  for(int i=0;i<tries;i++){if(httpGetJsonRaw(url,filter)) return true; delay(backoff); backoff*=2;} return false;
}

template<typename F> static bool fetchCalendarWithCache(F* filter=nullptr) {
  String url=buildApiUrl(API_RACES_SUFFIX);
  if(getCachedData(&apiCache.calendar,cfgCacheCalMs)){
    DeserializationError e=filter?deserializeJson(doc,apiCache.calendar.data,DeserializationOption::Filter(*filter)):deserializeJson(doc,apiCache.calendar.data);
    if(!e) return true;
  }
  if(!httpGetJsonWithRetry(url.c_str(),3,500,filter)) return false;
  String j; serializeJson(doc,j); cacheData(&apiCache.calendar,j); return true;
}
template<typename F> static bool fetchDriverStandingsWithCache(F* filter=nullptr) {
  String url=buildApiUrl(API_DRIVER_STAND_SUFFIX);
  if(getCachedData(&apiCache.driverStandings,cfgCacheStandMs)){
    DeserializationError e=filter?deserializeJson(doc,apiCache.driverStandings.data,DeserializationOption::Filter(*filter)):deserializeJson(doc,apiCache.driverStandings.data);
    if(!e) return true;
  }
  if(!httpGetJsonWithRetry(url.c_str(),3,500,filter)) return false;
  String j; serializeJson(doc,j); cacheData(&apiCache.driverStandings,j); return true;
}
template<typename F> static bool fetchConstructorStandingsWithCache(F* filter=nullptr) {
  String url=buildApiUrl(API_CONSTR_STAND_SUFFIX);
  if(getCachedData(&apiCache.constructorStandings,cfgCacheStandMs)){
    DeserializationError e=filter?deserializeJson(doc,apiCache.constructorStandings.data,DeserializationOption::Filter(*filter)):deserializeJson(doc,apiCache.constructorStandings.data);
    if(!e) return true;
  }
  if(!httpGetJsonWithRetry(url.c_str(),3,500,filter)) return false;
  String j; serializeJson(doc,j); cacheData(&apiCache.constructorStandings,j); return true;
}
template<typename F> static bool fetchQualifyingWithCache(unsigned int round, F* filter=nullptr) {
  String url=seasonBaseUrl()+"/"+String(round)+"/qualifying.json";
  if(getCachedData(&apiCache.qualifying,cfgCacheQualMs)){
    DeserializationError e=filter?deserializeJson(doc,apiCache.qualifying.data,DeserializationOption::Filter(*filter)):deserializeJson(doc,apiCache.qualifying.data);
    if(!e) return true;
  }
  if(!httpGetJsonWithRetry(url.c_str(),2,400,filter)) return false;
  String j; serializeJson(doc,j); cacheData(&apiCache.qualifying,j); return true;
}

// ═══════════════════════════════════════════════════════════════════
//  TEXT HELPERS
//  drawStr places text with its TOP-LEFT at (x,y).
//  Alignment adjusts x. bg=EPD_WHITE means transparent (white bg).
// ═══════════════════════════════════════════════════════════════════
static void drawStr(int x, int y, const char* s, screenAlignment align, uint8_t fg, uint8_t bg=EPD_BLACK) {
  if(!s || !s[0]) return;
  gfx.setForegroundColor(fg);
  gfx.setBackgroundColor(bg);
  int tw = gfx.getUTF8Width(s);
  if(align == RIGHT)  x -= tw;
  if(align == CENTER) x -= tw/2;
  gfx.setCursor(x, y);
  gfx.print(s);
}
static void drawStrS(int x, int y, const String& s, screenAlignment align, uint8_t fg, uint8_t bg=EPD_BLACK) {
  drawStr(x,y,s.c_str(),align,fg,bg);
}

// Formula1 ':' glyph reads like '.' — gfxPrintTimeWithColonGfx() draws real colon dots (header + next-race).
static void gfxPrintTimeWithColonGfx(const char* t, uint8_t dotColor) {
  if (!t || !t[0]) return;
  const char* sep = strchr(t, ':');
  if (!sep) {
    gfx.print(t);
    return;
  }
  char left[28];
  size_t n = (size_t)(sep - t);
  if (n >= sizeof(left)) n = sizeof(left) - 1;
  memcpy(left, t, n);
  left[n] = '\0';
  gfx.print(left);
  const int asc = gfx.getFontAscent();
  const int dot = max(2, asc / 5);
  const int baseline = gfx.getCursorY();
  const int cx = gfx.getCursorX() + 1;
  const int topY = baseline - asc + dot;
  const int botY = baseline - (asc * 2) / 5;
  epd_fill_rect(cx, topY, dot, dot, dotColor);
  epd_fill_rect(cx, botY, dot, dot, dotColor);
  gfx.setCursor(cx + dot + 4, baseline);
  gfx.print(sep + 1);
}

// Pixel width for the same layout as gfxPrintTimeWithColonGfx(); FONT_SMALL must be active.
static int gfxTimeWithColonGfxWidth(const char* t) {
  if (!t || !t[0]) return 0;
  const char* sep = strchr(t, ':');
  if (!sep) return gfx.getUTF8Width(t);
  char left[28];
  size_t n = (size_t)(sep - t);
  if (n >= sizeof(left)) n = sizeof(left) - 1;
  memcpy(left, t, n);
  left[n] = '\0';
  const int asc = gfx.getFontAscent();
  const int dot = max(2, asc / 5);
  return gfx.getUTF8Width(left) + 1 + dot + 4 + gfx.getUTF8Width(sep + 1);
}

// ═══════════════════════════════════════════════════════════════════
//  CHECKERED STRIPS
// ═══════════════════════════════════════════════════════════════════
static void drawCheckers(int x, int y, int w, int h, int sq) {
  for(int cy=y; cy<y+h; cy+=sq)
    for(int cx=x; cx<x+w; cx+=sq) {
      bool dark=(((cx-x)/sq)+((cy-y)/sq))&1;
      epd_fill_rect(cx,cy,min(sq,x+w-cx),min(sq,y+h-cy),dark?EPD_BLACK:EPD_WHITE);
    }
}

// ═══════════════════════════════════════════════════════════════════
//  FETCH CALENDAR
// ═══════════════════════════════════════════════════════════════════
static void FetchCalendar() {
  lastRound=nextRound=0;
  StaticJsonDocument<512> filter;
  filter["MRData"]["RaceTable"]["Races"][0]["date"]=true;
  filter["MRData"]["RaceTable"]["Races"][0]["time"]=true;
  filter["MRData"]["RaceTable"]["Races"][0]["round"]=true;
  filter["MRData"]["RaceTable"]["Races"][0]["raceName"]=true;
  filter["MRData"]["RaceTable"]["Races"][0]["Circuit"]["circuitId"]=true;
  filter["MRData"]["RaceTable"]["Races"][0]["Circuit"]["circuitName"]=true;
  filter["MRData"]["RaceTable"]["Races"][0]["Circuit"]["Location"]["locality"]=true;
  filter["MRData"]["RaceTable"]["Races"][0]["Circuit"]["Location"]["country"]=true;
  if(!fetchCalendarWithCache(&filter)){Serial.println(F("[ERR] calendar")); return;}
  if(!doc["MRData"]["RaceTable"]["Races"].is<JsonArray>()) return;

  JsonArray races=doc["MRData"]["RaceTable"]["Races"].as<JsonArray>();
  time_t nowEpoch=time(nullptr);
  for(JsonObject race:races){
    const char* dateStr=race["date"]|"";
    String t=cleanTime(String(race["time"]|"00:00:00"));
    const char* rndStr=race["round"]|""; unsigned rnd=rndStr[0]?(unsigned)atoi(rndStr):0;
    if(!dateStr||!rnd||!dateStr[0]) continue;
    const char* circuit=race["Circuit"]["circuitName"]|"";
    const char* GPname=race["raceName"]|"";
    String loc=String(race["Circuit"]["Location"]["locality"].as<const char*>())+", "+race["Circuit"]["Location"]["country"].as<const char*>();
    struct tm tmR={}; if(sscanf(dateStr,"%4d-%2d-%2d",&tmR.tm_year,&tmR.tm_mon,&tmR.tm_mday)!=3) continue;
    tmR.tm_year-=1900; tmR.tm_mon-=1;
    int h,m,s; if(sscanf(t.c_str(),"%2d:%2d:%2d",&h,&m,&s)!=3) continue;
    tmR.tm_hour=h; tmR.tm_min=m; tmR.tm_sec=s; tmR.tm_isdst=0;
    time_t ep=timegm_utc(&tmR);
    // A race belongs to the past if its epoch is strictly before now.
    // (race_in_progress grace window is handled separately by isRaceInProgress())
    if(ep < nowEpoch){
      if(rnd > lastRound){
        lastRound=rnd; lastDate=String(dateStr); lastTime=t;
        lastGP=String(GPname); lastGP.replace("Grand Prix","GP");
        lastCircuit=String(circuit); lastLoc=loc;
        lastCircuitId=String(race["Circuit"]["circuitId"]|"");
      }
    } else if(!nextRound){
      // First race with ep >= now is the next race
      nextRound=rnd; nextDate=String(dateStr); nextTime=t;
      nextGP=String(GPname); nextGP.replace("Grand Prix","GP");
      nextCircuit=String(circuit); nextCircuitId=String(race["Circuit"]["circuitId"]|"");
      nextLoc=loc;
      break;
    }
  }
  if(nextRound){nextRaceEpoch=isoUtcToEpoch(nextDate,nextTime);nextRaceDateStr=nextDate;prefs.putUInt("nextRound",nextRound);prefs.putULong64("nextEpoch",(uint64_t)nextRaceEpoch);prefs.putString("nextDate",nextRaceDateStr);}
  if(lastRound) prefs.putUInt("lastRound",lastRound);
  Serial.printf("[CAL] Last R%u %s | Next R%u %s\n",lastRound,lastGP.c_str(),nextRound,nextGP.c_str());
}

// ═══════════════════════════════════════════════════════════════════
//  QUALIFYING / RESULTS AVAILABILITY
// ═══════════════════════════════════════════════════════════════════
bool qualifyingAvailableForRound(unsigned round) {
  if(!round) return false;
  if(availabilityCache.qualifyingValid && availabilityCache.round==round &&
     (millis()-availabilityCache.qualifyingTimestamp)<cfgTtlQualAvailMs)
    return availabilityCache.qualifyingAvailable;
  StaticJsonDocument<256> f;
  f["MRData"]["RaceTable"]["Races"][0]["QualifyingResults"][0]["Driver"]["familyName"]=true;
  if(getCachedData(&apiCache.qualifying,cfgCacheQualMs)){
    if(!deserializeJson(doc,apiCache.qualifying.data,DeserializationOption::Filter(f)) &&
       doc["MRData"]["RaceTable"]["Races"].is<JsonArray>() &&
       doc["MRData"]["RaceTable"]["Races"][0]["QualifyingResults"].is<JsonArray>()){
      bool av=doc["MRData"]["RaceTable"]["Races"][0]["QualifyingResults"].as<JsonArray>().size()>0;
      availabilityCache={round,av,availabilityCache.resultsAvailable,millis(),availabilityCache.resultsTimestamp,true,availabilityCache.resultsValid};
      return av;
    }
  }
  String url=seasonBaseUrl()+"/"+String(round)+"/qualifying/";
  if(!httpGetJsonWithRetry(url.c_str(),2,400,&f)){
    availabilityCache={round,false,availabilityCache.resultsAvailable,millis(),availabilityCache.resultsTimestamp,true,availabilityCache.resultsValid}; return false;
  }
  bool av=doc["MRData"]["RaceTable"]["Races"].is<JsonArray>() &&
          doc["MRData"]["RaceTable"]["Races"][0]["QualifyingResults"].is<JsonArray>() &&
          doc["MRData"]["RaceTable"]["Races"][0]["QualifyingResults"].as<JsonArray>().size()>0;
  availabilityCache={round,av,availabilityCache.resultsAvailable,millis(),availabilityCache.resultsTimestamp,true,availabilityCache.resultsValid};
  return av;
}

bool resultsAvailableForLastRound() {
  if(!lastRound) return false;
  if(availabilityCache.resultsValid && availabilityCache.round==lastRound &&
     (millis()-availabilityCache.resultsTimestamp)<cfgTtlResultsMs)
    return availabilityCache.resultsAvailable;
  StaticJsonDocument<512> f;
  f["MRData"]["RaceTable"]["Races"][0]["Results"][0]=true;
  f["MRData"]["RaceTable"]["Races"][0]["Results"][1]=true;
  f["MRData"]["RaceTable"]["Races"][0]["Results"][2]=true;
  auto checkDoc=[&]()->bool{
    if(!doc["MRData"]["RaceTable"]["Races"].size()) return false;
    if(!doc["MRData"]["RaceTable"]["Races"][0]["Results"].is<JsonArray>()) return false;
    return doc["MRData"]["RaceTable"]["Races"][0]["Results"].as<JsonArray>().size()>=3;
  };
  if(getCachedData(&apiCache.results,cfgCacheStandMs)){
    if(!deserializeJson(doc,apiCache.results.data,DeserializationOption::Filter(f))){
      bool av=checkDoc(); availabilityCache.round=lastRound; availabilityCache.resultsAvailable=av;
      availabilityCache.resultsTimestamp=millis(); availabilityCache.resultsValid=true; return av;
    }
  }
  String url=seasonBaseUrl()+"/"+String(lastRound)+"/results/";
  if(!httpGetJsonWithRetry(url.c_str(),3,500,&f)){
    availabilityCache.round=lastRound; availabilityCache.resultsAvailable=false;
    availabilityCache.resultsTimestamp=millis(); availabilityCache.resultsValid=true; return false;
  }
  String j; serializeJson(doc,j); cacheData(&apiCache.results,j);
  bool av=checkDoc(); availabilityCache.round=lastRound; availabilityCache.resultsAvailable=av;
  availabilityCache.resultsTimestamp=millis(); availabilityCache.resultsValid=true; return av;
}

// ═══════════════════════════════════════════════════════════════════
//  DARK THEME HELPERS
// ═══════════════════════════════════════════════════════════════════

// Section header — black strip, red top border; optional matching bottom rule
static void drawSectionHeader(int x, int y, int w, const char* label, bool redBottomRule = true) {
  const int h = COL_TITLE_H;
  epd_fill_rect(x, y, w, h, EPD_BLACK);
  epd_fill_rect(x, y, w, RED_RULE_THICK, EPD_RED);
  if (redBottomRule)
    epd_fill_rect(x, y + h - RED_RULE_THICK, w, RED_RULE_THICK, EPD_RED);
  gfx.setFont(FONT_MED);
  {
    const int asc = gfx.getFontAscent();
    const int dsc = gfx.getFontDescent();  // typically <= 0
    const int innerTop = y + RED_RULE_THICK;
    const int innerH = redBottomRule ? (h - 2 * RED_RULE_THICK) : (h - RED_RULE_THICK);
    const int innerMid = innerTop + innerH / 2;
    const int textY = innerMid + (asc + dsc) / 2 + SECTION_HEADER_LABEL_NUDGE_DN;
    drawStr(x + w/2, textY, label, CENTER, EPD_WHITE, EPD_BLACK);
  }
}

// Team color bar: 4px wide, full row height
static void drawTeamBar(int x, int ry, uint8_t tc) {
  epd_fill_rect_color(x, ry, 4, COL_ROW_H - 1, tc);
}

// Badge: colored rect + white TLA text centered inside — FONT_SMALL
static void drawBadge(int badgeX, int ry, uint8_t tc, const String& badge) {
  const int BW = D_BADGE_W, BH = D_BADGE_H;
  int badgeY = ry + (COL_ROW_H - BH) / 2;
  epd_fill_rect_color(badgeX, badgeY, BW, BH, tc);
  uint8_t fg = (tc == EPD_WHITE) ? EPD_BLACK : EPD_WHITE;
  gfx.setFont(FONT_SMALL);
  gfx.setFontMode(1);  // transparent — no background fill
  gfx.setForegroundColor(fg);
  int tw = gfx.getUTF8Width(badge.c_str());
  gfx.setCursor(badgeX + (BW - tw) / 2, badgeY + (BH + gfx.getFontAscent()) / 2);
  gfx.print(badge);
  gfx.setFontMode(0);  // restore opaque
}

// 1px row separator — white line at bottom of row (visible on black bg)
static void drawRowSep(int x, int y, int w) {
  epd_fill_rect(x, y + COL_ROW_H - 1, w, 1, EPD_WHITE);
}

static void formatNextUpdateFooter(char* buf, size_t buflen);

// ═══════════════════════════════════════════════════════════════════
//  HEADER  (y=0..HDR_H-1)
//  Checkered both edges | date/time left | F1 logo + TRACKER centered | R# of 24 upper right | red bars at HDR_H
//  (IP and battery — see DrawFooter)
// ═══════════════════════════════════════════════════════════════════
static void DrawHeader() {
  epd_fill_rect(0, 0, EPD_WIDTH, HDR_H, EPD_BLACK);

  // Checkered flags both edges, 8×8 squares
  drawCheckers(0,            0, 24, HDR_H, 8);
  drawCheckers(EPD_WIDTH-24, 0, 24, HDR_H, 8);

  // Date + time — left column
  char datebuf[16], timebuf[16];
  strftime(datebuf, sizeof(datebuf), "%m/%d/%Y", &timeinfo);
  formatLocalTime(timebuf, sizeof(timebuf), &timeinfo);
  gfx.setFont(FONT_MED);
  gfx.setForegroundColor(EPD_WHITE); gfx.setBackgroundColor(EPD_BLACK);
  gfx.setCursor(HDR_DATETIME_LEFT_X, HDR_DATE_Y); gfx.print(datebuf);
  gfx.setCursor(HDR_DATETIME_LEFT_X, HDR_TIME_Y);
  gfxPrintTimeWithColonGfx(timebuf, EPD_WHITE);

  // Round — upper right (right-aligned, vertically centered like title block)
  if (nextRound || lastRound) {
    char roundBuf[16];
    snprintf(roundBuf, sizeof(roundBuf), "Race %u of 24", nextRound ? nextRound : lastRound);
    gfx.setFont(FONT_SMALL);
    gfx.setForegroundColor(EPD_WHITE);
    gfx.setBackgroundColor(EPD_BLACK);
    int rtw = gfx.getUTF8Width(roundBuf);
    {
      const int asc = gfx.getFontAscent();
      const int dsc = gfx.getFontDescent();
      const int roundY = HDR_H / 2 + (asc + dsc) / 2;
      gfx.setCursor(EPD_WIDTH - 24 - HDR_ROUND_RIGHT_PAD - rtw, roundY);
    }
    gfx.print(roundBuf);
  }

  // F1 logo + TRACKER — horizontally centered as a group; text vertically aligned to logo midline
  gfx.setFont(u8g2_font_f1bold24_tf);
  const char* title = "STANDINGS TRACKER";
  int titleW = gfx.getUTF8Width(title);
  int totalW = F1_LOGO_W + 8 + titleW;
  int startX = (EPD_WIDTH - totalW) / 2;
  int logoY = (HDR_H - F1_LOGO_H) / 2;
  display.drawBitmap(startX, logoY, F1_Logo, F1_LOGO_W, F1_LOGO_H, EPD_RED, EPD_BLACK);
  gfx.setForegroundColor(EPD_WHITE); gfx.setBackgroundColor(EPD_BLACK);
  {
    const int asc = gfx.getFontAscent();
    const int dsc = gfx.getFontDescent();
    const int titleBaseline = HDR_H / 2 + (asc + dsc) / 2;
    gfx.setCursor(startX + F1_LOGO_W + 8, titleBaseline);
  }
  gfx.print(title);

  epd_fill_rect(0, HDR_H, EPD_WIDTH, RED_RULE_THICK, EPD_RED);
}

// ═══════════════════════════════════════════════════════════════════
//  COLUMN DIVIDERS  (red vertical lines)
// ═══════════════════════════════════════════════════════════════════
static void DrawDividers() {
  int top = HDR_H;
  int len = COL23_ROWS_BOTTOM_Y - top;  // col2/3 content stops after 9 rows
  epd_fill_rect(DIV1_X, top, DIV_W, len, EPD_RED);
  epd_fill_rect(DIV2_X, top, DIV_W, len, EPD_RED);
}

// ═══════════════════════════════════════════════════════════════════
//  FOOTER  — IP left | Next Update … (white, right) | battery gauge right
// ═══════════════════════════════════════════════════════════════════
static void DrawFooter() {
  const int fh = FOOTER_H;
  epd_fill_rect(0, EPD_HEIGHT - fh, EPD_WIDTH, fh, EPD_BLACK);

  gfx.setFont(FONT_SMALL);
  gfx.setFontMode(1);
  gfx.setBackgroundColor(EPD_BLACK);

  String ipStr = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : String("--");
  gfx.setForegroundColor(EPD_WHITE);
  gfx.setCursor(HDR_DATETIME_LEFT_X, EPD_HEIGHT - 6);
  gfx.print(ipStr);
  const int ipEnd = HDR_DATETIME_LEFT_X + gfx.getUTF8Width(ipStr.c_str());

  char batBuf[16] = "";
  int bx = 0, twBat = 0;
  int pct = 0;
  int packRight = EPD_WIDTH - 10;
  if (pmuProbeOk && pmu.isBatteryConnect()) {
    pct         = pmu.getBatteryPercent();
    if (pmu.isCharging())
      snprintf(batBuf, sizeof(batBuf), "+%d%%", pct);
    else
      snprintf(batBuf, sizeof(batBuf), "%d%%", pct);
    gfx.setForegroundColor(EPD_WHITE);
    twBat = gfx.getUTF8Width(batBuf);
    const int bw = 18;
    bx = EPD_WIDTH - HDR_RIGHT_INSET - bw - 2;
    packRight = bx - 8 - twBat;
  }

  char nuBuf[96];
  formatNextUpdateFooter(nuBuf, sizeof(nuBuf));
  gfx.setFont(FONT_SMALL);
  gfx.setFontMode(1);
  gfx.setBackgroundColor(EPD_BLACK);
  const int minNextX = ipEnd + 10;
  uint16_t nuMaxW = (uint16_t)max(40, packRight - minNextX - 2);
  String nuStr(nuBuf);
  while (nuStr.length() > strlen("Next Update:") &&
         (unsigned)gfx.getUTF8Width(nuStr.c_str()) > nuMaxW)
    nuStr.remove(nuStr.length() - 1);
  int nuW = gfx.getUTF8Width(nuStr.c_str());
  int nuX = packRight - nuW;
  if (nuX < minNextX) nuX = minNextX;
  gfx.setForegroundColor(EPD_WHITE);
  gfx.setCursor(nuX, EPD_HEIGHT - 6);
  gfx.print(nuStr);

  if (batBuf[0]) {
    gfx.setForegroundColor(EPD_WHITE);
    int tw = gfx.getUTF8Width(batBuf);
    const int bw = 18, bh = 10;
    int by = EPD_HEIGHT - 16;
    epd_rect(bx, by, bw, bh, EPD_WHITE);
    epd_fill_rect(bx + bw, by + 3, 2, 4, EPD_WHITE);
    int fillW = max(0, (int)((bw - 4) * pct / 100));
    if (fillW > 0) epd_fill_rect(bx + 2, by + 2, fillW, bh - 4, EPD_WHITE);
    gfx.setCursor(bx - 4 - tw, EPD_HEIGHT - 6);
    gfx.print(batBuf);
  }

  gfx.setFontMode(0);
}

// ═══════════════════════════════════════════════════════════════════
//  LEFT PANEL  (x=0..COL1_W-1 = 0..218)
// ═══════════════════════════════════════════════════════════════════
static void DrawLeftPanel() {
  const int cx = COL1_X, cw = COL1_W;

  // ── NEXT RACE section header ──
  drawSectionHeader(cx, C1_SEC1_Y, cw, "NEXT RACE");

  if(isRaceInProgress()) {
    epd_fill_rect(cx, C1_SEC1_Y + COL_TITLE_H + 2, cw, 26, EPD_RED);
    gfx.setFont(FONT_MED);
    gfx.setFontMode(1);
    drawStr(cx+cw/2, C1_SEC1_Y + COL_TITLE_H + SECTION_AFTER_TITLE_GAP, "RACE IN PROGRESS", CENTER, EPD_WHITE, EPD_RED);
    gfx.setFontMode(0);
  } else if(nextRound) {
    // Race name — FONT_LARGE
    gfx.setFont(FONT_LARGE);
    {
      String gpTrunc = truncateToPixelWidth(nextGP, cw - 6);
      drawStrS(cx + cw/2, C1_RACE_Y, gpTrunc, CENTER, EPD_WHITE, EPD_BLACK);
    }

    // Circuit — FONT_SMALL, locality first word only
    gfx.setFont(FONT_SMALL);
    {
      String circ = nextLoc.length() ? nextLoc : nextCircuit;
      int commaIdx = circ.indexOf(',');
      if(commaIdx > 0) circ = circ.substring(0, commaIdx);
      int spaceIdx = circ.indexOf(' ');
      if(spaceIdx > 0) circ = circ.substring(0, spaceIdx);
      circ.toUpperCase();
      drawStrS(cx + cw/2, C1_CIRC_Y, circ, CENTER, EPD_WHITE, EPD_BLACK);
    }

    // Countdown box
    int bx = cx + 2, bw = cw - 4;
    epd_fill_rect(bx, C1_BOX_Y, bw, C1_BOX_H, EPD_BLACK);

    String rtime = nextRaceLocalDateMMDD() + " " + nextRaceLocalTimeLower();
    String cd = nextRaceCountdownDH();
    if(!cd.length()) cd = "NOW";

    const int innerTop = C1_BOX_Y + 3;
    const int innerBot = C1_BOX_Y + C1_BOX_H;
    const int innerMidX = bx + bw / 2;

    gfx.setFont(FONT_SMALL);
    const int sAsc = gfx.getFontAscent();
    const int sDsc = gfx.getFontDescent();
    gfx.setFont(FONT_HERO);
    const int hAsc = gfx.getFontAscent();
    const int hDsc = gfx.getFontDescent();
    const int gapSmallHero = 2;
    const int gapHeroSmall = 2;
    const int blockH =
        sAsc + (-sDsc) + gapSmallHero + hAsc + (-hDsc) + gapHeroSmall + sAsc + (-sDsc);

    const int padTop =
        innerTop + max(0, ((innerBot - innerTop) - blockH) / 2);
    const int blLightsOut = padTop + sAsc;
    const int blCountdown =
        blLightsOut + (-sDsc) + gapSmallHero + hAsc;
    const int blRtime =
        blCountdown + (-hDsc) + gapHeroSmall + sAsc;

    gfx.setFont(FONT_SMALL);
    gfx.setFontMode(1);
    drawStr(innerMidX, blLightsOut, "LIGHTS OUT", CENTER, EPD_WHITE, EPD_BLACK);

    gfx.setFont(FONT_HERO);
    gfx.setFontMode(1);
    gfx.setForegroundColor(EPD_RED);
    gfx.setBackgroundColor(EPD_BLACK);
    drawStr(innerMidX, blCountdown, cd.c_str(), CENTER, EPD_RED, EPD_BLACK);

    gfx.setFont(FONT_SMALL);
    gfx.setFontMode(1);
    gfx.setForegroundColor(EPD_WHITE);
    gfx.setBackgroundColor(EPD_BLACK);
    {
      const int rw = gfxTimeWithColonGfxWidth(rtime.c_str());
      gfx.setCursor(innerMidX - rw / 2, blRtime);
      gfxPrintTimeWithColonGfx(rtime.c_str(), EPD_WHITE);
    }
    gfx.setFontMode(0);
  }

  // ── LAST RACE section header ──
  drawSectionHeader(cx, C1_SEC2_Y, cw, "LAST RACE", false);

  if(lastGP.length()) {
    gfx.setFont(FONT_LARGE);
    {
      String lgpTrunc = truncateToPixelWidth(lastGP, cw - 6);
      drawStrS(cx + cw/2, C1_LGPN_Y, lgpTrunc, CENTER, EPD_WHITE, EPD_BLACK);
    }
    gfx.setFont(FONT_SMALL);
    {
      String lcirc = lastLoc.length() ? lastLoc : lastCircuit;
      int commaIdx = lcirc.indexOf(',');
      if(commaIdx > 0) lcirc = lcirc.substring(0, commaIdx);
      int spaceIdx = lcirc.indexOf(' ');
      if(spaceIdx > 0) lcirc = lcirc.substring(0, spaceIdx);
      lcirc.toUpperCase();
      drawStrS(cx + cw/2, C1_LCIRC_Y, lcirc, CENTER, EPD_WHITE, EPD_BLACK);
    }
  }

  // ── Track diagram — clipped above podium (centered in column; Montreal scaled + rotated in track_reader)
  if(lastCircuitId.length()) {
    int trackY = C1_LCIRC_Y - 21;
    int trackMaxH = POD_1_Y - trackY - 4;
    const int tinset = 2;
    const int trackLayoutLeft = cx + tinset;
    const int trackLayoutW = cw - 2 * tinset;
    if(trackMaxH > 20)
      drawTrackFromSD(lastCircuitId.c_str(), trackLayoutLeft, trackY, EPD_WHITE, trackMaxH, trackLayoutW);
  }

  // ── PODIUM boxes — driver TLAs only (no surnames); transparent text ──
  String tla0, tla1, tla2;
  bool haveNames = false;
  uint8_t tcPod0 = EPD_BLACK, tcPod1 = EPD_BLACK, tcPod2 = EPD_BLACK;
  const char* cid0 = "";
  const char* cid1 = "";
  const char* cid2 = "";
  if(lastRound && !isRaceInProgress()) {
    StaticJsonDocument<768> f;
    for (int i = 0; i < 3; i++) {
      f["MRData"]["RaceTable"]["Races"][0]["Results"][i]["Driver"]["code"] = true;
      f["MRData"]["RaceTable"]["Races"][0]["Results"][i]["Constructor"]["constructorId"] = true;
      f["MRData"]["RaceTable"]["Races"][0]["Results"][i]["Constructor"]["name"] = true;
    }
    bool useCache = false;
    if(getCachedData(&apiCache.results, cfgCacheStandMs))
      if(!deserializeJson(doc, apiCache.results.data, DeserializationOption::Filter(f))) useCache = true;
    if(!useCache){
      String url = seasonBaseUrl()+"/"+String(lastRound)+"/results/";
      if(httpGetJsonWithRetry(url.c_str(),3,500,&f)){String j;serializeJson(doc,j);cacheData(&apiCache.results,j);}
    }
    if(resultsAvailableForLastRound() &&
       doc["MRData"]["RaceTable"]["Races"].size()>0 &&
       doc["MRData"]["RaceTable"]["Races"][0]["Results"].is<JsonArray>()){
      JsonArray pod = doc["MRData"]["RaceTable"]["Races"][0]["Results"].as<JsonArray>();
      if((int)pod.size() >= 3){
        tla0 = String(pod[0]["Driver"]["code"] | "");
        tla1 = String(pod[1]["Driver"]["code"] | "");
        tla2 = String(pod[2]["Driver"]["code"] | "");
        JsonObject c0 = pod[0]["Constructor"];
        JsonObject c1 = pod[1]["Constructor"];
        JsonObject c2 = pod[2]["Constructor"];
        cid0 = c0["constructorId"] | "";
        cid1 = c1["constructorId"] | "";
        cid2 = c2["constructorId"] | "";
        tcPod0 = teamColor(cid0, c0["name"] | "");
        tcPod1 = teamColor(cid1, c1["name"] | "");
        tcPod2 = teamColor(cid2, c2["name"] | "");
        haveNames = true;
      }
    }
  }

  epd_fill_rect_color(POD_2_X, POD_2_Y, POD_2_W, POD_2_H, tcPod1);
  epd_fill_rect_color(POD_1_X, POD_1_Y, POD_1_W, POD_1_H, tcPod0);
  epd_fill_rect_color(POD_3_X, POD_3_Y, POD_3_W, POD_3_H, tcPod2);
  epd_hline(POD_2_X, POD_BASE_Y, POD_3_X + POD_3_W - POD_2_X, EPD_WHITE);

  if (haveNames) {
    // u8g2_SetFont clears transparent mode — setFontMode(1) must follow each setFont (same as drawDriverRow)

    gfx.setBackgroundColor(podiumGlyphBg(tcPod1));
    gfx.setForegroundColor(podiumTextFg(tcPod1, cid1));
    gfx.setFont(FONT_MED);
    gfx.setFontMode(1);
    int tw = gfx.getUTF8Width(tla1.c_str());
    gfx.setCursor(POD_2_X + (POD_2_W - tw) / 2, POD_2_Y + 26);
    gfx.print(tla1);
    gfx.setFont(FONT_SMALL);
    gfx.setFontMode(1);
    tw = gfx.getUTF8Width("P2");
    gfx.setCursor(POD_2_X + (POD_2_W - tw) / 2, POD_2_Y + POD_2_H - 6);
    gfx.print("P2");

    gfx.setBackgroundColor(podiumGlyphBg(tcPod0));
    gfx.setForegroundColor(podiumTextFg(tcPod0, cid0));
    gfx.setFont(FONT_MED);
    gfx.setFontMode(1);
    tw = gfx.getUTF8Width(tla0.c_str());
    gfx.setCursor(POD_1_X + (POD_1_W - tw) / 2, POD_1_Y + 36);
    gfx.print(tla0);
    gfx.setFont(FONT_SMALL);
    gfx.setFontMode(1);
    tw = gfx.getUTF8Width("P1");
    gfx.setCursor(POD_1_X + (POD_1_W - tw) / 2, POD_1_Y + POD_1_H - 6);
    gfx.print("P1");

    gfx.setBackgroundColor(podiumGlyphBg(tcPod2));
    gfx.setForegroundColor(podiumTextFg(tcPod2, cid2));
    gfx.setFont(FONT_MED);
    gfx.setFontMode(1);
    tw = gfx.getUTF8Width(tla2.c_str());
    gfx.setCursor(POD_3_X + (POD_3_W - tw) / 2, POD_3_Y + 20);
    gfx.print(tla2);
    gfx.setFont(FONT_SMALL);
    gfx.setFontMode(1);
    tw = gfx.getUTF8Width("P3");
    gfx.setCursor(POD_3_X + (POD_3_W - tw) / 2, POD_3_Y + POD_3_H - 6);
    gfx.print("P3");

    gfx.setFontMode(0);
  }
}

// ═══════════════════════════════════════════════════════════════════
//  COL 2 — shared row drawing helper
// ═══════════════════════════════════════════════════════════════════
static const char* natToISO(const char* nat) {
  if(!strcmp(nat,"British"))       return "gb";
  if(!strcmp(nat,"Dutch"))         return "nl";
  if(!strcmp(nat,"Monegasque"))    return "mc";
  if(!strcmp(nat,"Spanish"))       return "es";
  if(!strcmp(nat,"Finnish"))       return "fi";
  if(!strcmp(nat,"Australian"))    return "au";
  if(!strcmp(nat,"German"))        return "de";
  if(!strcmp(nat,"French"))        return "fr";
  if(!strcmp(nat,"Canadian"))      return "ca";
  if(!strcmp(nat,"Mexican"))       return "mx";
  if(!strcmp(nat,"Chinese"))       return "cn";
  if(!strcmp(nat,"Thai"))          return "th";
  if(!strcmp(nat,"Japanese"))      return "jp";
  if(!strcmp(nat,"New Zealander")) return "nz";
  if(!strcmp(nat,"Italian"))       return "it";
  if(!strcmp(nat,"Danish"))        return "dk";
  if(!strcmp(nat,"Belgian"))       return "be";
  if(!strcmp(nat,"Brazilian"))     return "br";
  if(!strcmp(nat,"American"))      return "us";
  return nullptr;
}

// Badge text: white on most team colors; black on white badges; black for these drivers on papaya/teal/etc.
static uint8_t driverBadgeTextColor(const char* familyName, uint8_t teamColor) {
  if (teamColor == EPD_WHITE) return EPD_BLACK;
  if (familyName && familyName[0]) {
    String fn = String(familyName);
    fn.toLowerCase();
    if (fn.indexOf("norris") >= 0 || fn.indexOf("piastri") >= 0 || fn.indexOf("piastre") >= 0 ||
        fn.indexOf("bearman") >= 0)
      return EPD_BLACK;
  }
  return EPD_WHITE;
}

static void drawDriverRow(int i, const char* pos, const char* fam,
                          const char* code, const char* pts,
                          uint8_t tc, const char* nationality="") {
  int ry = SECTION_ROW0_Y + i * COL_ROW_H;

  // Row background
  epd_fill_rect(COL2_X, ry, COL2_W, COL_ROW_H, EPD_BLACK);

  gfx.setFontMode(1);  // transparent — all row text

  // Position number
  gfx.setFont(FONT_MED);
  gfx.setFontMode(1);
  gfx.setForegroundColor(EPD_WHITE);
  char posbuf[4]; snprintf(posbuf, sizeof(posbuf), "%s", pos);
  int tw = gfx.getUTF8Width(posbuf);
  gfx.setCursor(D_POS_X + D_POS_W - tw, ry + COL_ROW_H - ROW_TEXT_BASELINE_DN);
  gfx.print(posbuf);

  const int badgeY = ry + (COL_ROW_H - D_BADGE_H) / 2;
  epd_fill_rect_color(D_BADGE_X, badgeY, D_BADGE_W, D_BADGE_H, tc);
  gfx.setFont(FONT_SMALL);
  gfx.setFontMode(1);
  gfx.setForegroundColor(driverBadgeTextColor(fam, tc));
  String badge = code[0] ? String(code) : utf8_substr(String(fam), 3);
  badge.toUpperCase();
  tw = gfx.getUTF8Width(badge.c_str());
  gfx.setCursor(D_BADGE_X + (D_BADGE_W - tw) / 2,
                badgeY + (D_BADGE_H + gfx.getFontAscent()) / 2);
  gfx.print(badge);

  // Driver surname
  gfx.setFont(FONT_LARGE);
  gfx.setFontMode(1);
  gfx.setForegroundColor(EPD_WHITE);
  int nameX    = D_BADGE_X + D_BADGE_W + 5;
  int maxNameW = D_PTS_X - nameX - 32 - 6;  // reserve 32px flag + 6px gap
  String lastName = truncateToPixelWidth(String(fam), maxNameW);
  gfx.setCursor(nameX, ry + COL_ROW_H - ROW_TEXT_BASELINE_DN);
  gfx.print(lastName);
  int nameEndX = nameX + gfx.getUTF8Width(lastName.c_str()) + 6;
  int flagX    = nameEndX;

  // Flag — 24×16px between name and points
  int flagY = ry + (COL_ROW_H - 16) / 2;
  const char* iso = natToISO(nationality);
  if(iso && sdMounted) {
    char flagPath[32];
    snprintf(flagPath, sizeof(flagPath), "/flags/%s.raw", iso);
    drawLogo(flagPath, flagX, flagY, epd_buf);
  } else {
    epd_fill_rect_color(flagX, flagY + 2, 24, 12, tc);
  }

  // Points — right-aligned
  if(pts && pts[0]) {
    gfx.setFont(FONT_MED);
    gfx.setFontMode(1);
    gfx.setForegroundColor(EPD_WHITE);
    tw = gfx.getUTF8Width(pts);
    gfx.setCursor(D_PTS_X - tw, ry + COL_ROW_H - ROW_TEXT_BASELINE_DN);
    gfx.print(pts);
  }

  gfx.setFontMode(0);  // restore

  // White separator line
  epd_fill_rect(COL2_X, ry + COL_ROW_H - 1, COL2_W, 1, EPD_WHITE);
}

// ── DRIVER STANDINGS ──
void DrawDriversStandings() {
  StaticJsonDocument<768> filter;
  for(int i=0;i<STANDINGS_DATA_ROWS;i++){
    filter["MRData"]["StandingsTable"]["StandingsLists"][0]["DriverStandings"][i]["position"]=true;
    filter["MRData"]["StandingsTable"]["StandingsLists"][0]["DriverStandings"][i]["points"]=true;
    filter["MRData"]["StandingsTable"]["StandingsLists"][0]["DriverStandings"][i]["Driver"]["familyName"]=true;
    filter["MRData"]["StandingsTable"]["StandingsLists"][0]["DriverStandings"][i]["Driver"]["code"]=true;
    filter["MRData"]["StandingsTable"]["StandingsLists"][0]["DriverStandings"][i]["Driver"]["nationality"]=true;
    filter["MRData"]["StandingsTable"]["StandingsLists"][0]["DriverStandings"][i]["Constructors"][0]["constructorId"]=true;
    filter["MRData"]["StandingsTable"]["StandingsLists"][0]["DriverStandings"][i]["Constructors"][0]["name"]=true;
  }
  if(!fetchDriverStandingsWithCache(&filter)) return;
  if(!doc["MRData"]["StandingsTable"]["StandingsLists"].size()) return;
  if(!doc["MRData"]["StandingsTable"]["StandingsLists"][0]["DriverStandings"].is<JsonArray>()) return;

  JsonArray ds = doc["MRData"]["StandingsTable"]["StandingsLists"][0]["DriverStandings"].as<JsonArray>();
  int count = min(STANDINGS_DATA_ROWS, (int)ds.size());
  for(int i=0; i<count; i++){
    JsonObject d = ds[i];
    drawDriverRow(i,
      d["position"]|"",
      d["Driver"]["familyName"]|"",
      d["Driver"]["code"]|"",
      d["points"]|"",
      teamColor(d["Constructors"][0]["constructorId"]|"", d["Constructors"][0]["name"]|""),
      d["Driver"]["nationality"]|"");
  }
}

// ── STARTING GRID ──
void DrawStartingGrid(unsigned round) {
  if(!round) return;
  StaticJsonDocument<768> filter;
  for(int i=0;i<STANDINGS_DATA_ROWS;i++){
    filter["MRData"]["RaceTable"]["Races"][0]["QualifyingResults"][i]["position"]=true;
    filter["MRData"]["RaceTable"]["Races"][0]["QualifyingResults"][i]["Driver"]["familyName"]=true;
    filter["MRData"]["RaceTable"]["Races"][0]["QualifyingResults"][i]["Driver"]["code"]=true;
    filter["MRData"]["RaceTable"]["Races"][0]["QualifyingResults"][i]["Driver"]["nationality"]=true;
    filter["MRData"]["RaceTable"]["Races"][0]["QualifyingResults"][i]["Constructor"]["constructorId"]=true;
    filter["MRData"]["RaceTable"]["Races"][0]["QualifyingResults"][i]["Constructor"]["name"]=true;
  }
  if(!fetchQualifyingWithCache(round, &filter)) return;
  if(!doc["MRData"]["RaceTable"]["Races"].size()) return;
  if(!doc["MRData"]["RaceTable"]["Races"][0]["QualifyingResults"].is<JsonArray>()) return;

  JsonArray q = doc["MRData"]["RaceTable"]["Races"][0]["QualifyingResults"].as<JsonArray>();
  int rows = min(STANDINGS_DATA_ROWS, (int)q.size());
  for(int i=0; i<rows; i++){
    JsonObject row = q[i];
    drawDriverRow(i,
      row["position"]|"",
      row["Driver"]["familyName"]|"",
      row["Driver"]["code"]|"",
      "",  // no points for qualifying
      teamColor(row["Constructor"]["constructorId"]|"", row["Constructor"]["name"]|""),
      row["Driver"]["nationality"]|"");
  }
}

void DrawDrivers() {
  bool showGrid=false;
  if(nextRaceEpoch>0 && nextRound>0){
    long diff=(long)(nextRaceEpoch-time(nullptr));
    if(diff>0 && diff<=cfgGridShowSec && qualifyingAvailableForRound(nextRound)) showGrid=true;
  }
  if(!showGrid && lastRound>0){
    time_t lep=isoUtcToEpoch(lastDate,lastTime);
    // Pre-race grid: only while results are not out yet (avoid grid for hours after the race finishes)
    if(time(nullptr)>=lep && time(nullptr)<=lep+cfgRaceInProgSec &&
       qualifyingAvailableForRound(lastRound) &&
       !resultsAvailableForLastRound()){
      drawSectionHeader(COL2_X, SECTION_TITLE_Y, COL2_W, "STARTING GRID");
      DrawStartingGrid(lastRound); return;
    }
  }
  if(showGrid){
    drawSectionHeader(COL2_X, SECTION_TITLE_Y, COL2_W, "STARTING GRID");
    DrawStartingGrid(nextRound);
  } else {
    drawSectionHeader(COL2_X, SECTION_TITLE_Y, COL2_W, "DRIVERS");
    DrawDriversStandings();
  }
}

// ═══════════════════════════════════════════════════════════════════
//  COL 3 — CONSTRUCTOR STANDINGS
// ═══════════════════════════════════════════════════════════════════
void DrawConstructors() {
  StaticJsonDocument<512> filter;
  for(int i=0;i<STANDINGS_DATA_ROWS;i++){
    filter["MRData"]["StandingsTable"]["StandingsLists"][0]["ConstructorStandings"][i]["position"]=true;
    filter["MRData"]["StandingsTable"]["StandingsLists"][0]["ConstructorStandings"][i]["points"]=true;
    filter["MRData"]["StandingsTable"]["StandingsLists"][0]["ConstructorStandings"][i]["Constructor"]["name"]=true;
    filter["MRData"]["StandingsTable"]["StandingsLists"][0]["ConstructorStandings"][i]["Constructor"]["constructorId"]=true;
  }
  if(!fetchConstructorStandingsWithCache(&filter)) return;
  if(!doc["MRData"]["StandingsTable"]["StandingsLists"].size()) return;
  if(!doc["MRData"]["StandingsTable"]["StandingsLists"][0]["ConstructorStandings"].is<JsonArray>()) return;

  JsonArray cs=doc["MRData"]["StandingsTable"]["StandingsLists"][0]["ConstructorStandings"].as<JsonArray>();
  int count=min(STANDINGS_DATA_ROWS,(int)cs.size());

  // Scale bars to leader (index 0 = most points)
  float maxPts=1.0f;
  for(int i=0;i<count;i++){float p=atof(cs[i]["points"]|"0"); if(p>maxPts) maxPts=p;}

  drawSectionHeader(COL3_X, SECTION_TITLE_Y, COL3_W, "CONSTRUCTORS");

  for(int i=0; i<count; i++){
    JsonObject c = cs[i];
    const char* pos    = c["position"]|"";
    const char* nm     = c["Constructor"]["name"]|"";
    const char* ctorId = c["Constructor"]["constructorId"]|"";
    const char* pts    = c["points"]|"";
    uint8_t tc = teamColor(ctorId, nm);
    float ptsF = atof(pts);
    int ry = SECTION_ROW0_Y + i * COL_ROW_H;

    // 1. Row background
    epd_fill_rect(COL3_X, ry, COL3_W, COL_ROW_H, EPD_BLACK);

    gfx.setFontMode(1);  // transparent — all row text

    gfx.setFont(FONT_MED);
    gfx.setFontMode(1);

// 3. Position number — white
uint8_t posFg = EPD_WHITE;
    drawStr(C_POS_X + C_POS_W, ry + COL_ROW_H - ROW_TEXT_BASELINE_DN, pos, RIGHT, posFg, EPD_BLACK);

    int logoY = ry + (COL_ROW_H - CTOR_LOGO_H) / 2;
    epd_fill_rect_color(C_LOGO_X, logoY, CTOR_LOGO_W, CTOR_LOGO_H, tc);
    drawConstructorLogo(ctorId, C_LOGO_X, logoY);

    // 4. Team name — FONT_LARGE, clipped to C_NAME_MAX_W
    gfx.setFont(FONT_LARGE);
    gfx.setFontMode(1);
    String displayName = truncateToPixelWidth(constructorDisplayName(ctorId, nm), C_NAME_MAX_W);
    drawStrS(C_NAME_X, ry + COL_ROW_H - ROW_TEXT_BASELINE_DN, displayName, LEFT, EPD_WHITE, EPD_BLACK);

    // 5. Points bar — black track then colored fill
    int barW = (int)(C_BAR_W * ptsF / maxPts);
    if(barW < 4 && ptsF > 0) barW = 4;
    if(barW > C_BAR_W) barW = C_BAR_W;
    int barY = ry + (COL_ROW_H - 6) / 2;
    epd_fill_rect(C_BAR_START, barY, C_BAR_W, 6, EPD_BLACK);
    if(barW > 0) epd_fill_rect_color(C_BAR_START, barY, barW, 6, tc);

    // 6. Points number — same size as driver standings (FONT_MED)
    gfx.setFont(FONT_MED);
    gfx.setFontMode(1);
    drawStr(C_PTS_X, ry + COL_ROW_H - ROW_TEXT_BASELINE_DN, pts, RIGHT, EPD_WHITE, EPD_BLACK);

    gfx.setFontMode(0);  // restore

    // 7. White separator line
    epd_fill_rect(COL3_X, ry + COL_ROW_H - 1, COL3_W, 1, EPD_WHITE);
  }
}

// ═══════════════════════════════════════════════════════════════════
//  WIFI-MANAGER CALLBACK
// ═══════════════════════════════════════════════════════════════════
void configModeCallback(WiFiManager* wm) {
  memset(epd_buf, 0x00, 192000);
  gfx.setFont(FONT_LARGE);
  drawStr(EPD_WIDTH/2, 120, "WiFi Setup Required", CENTER, EPD_RED, EPD_BLACK);
  gfx.setFont(FONT_MED);
  drawStr(EPD_WIDTH/2, 180, "Connect to:  F1Tracker-Setup", CENTER, EPD_WHITE, EPD_BLACK);
  drawStr(EPD_WIDTH/2, 220, "Password:  formula1",           CENTER, EPD_WHITE, EPD_BLACK);
  drawStr(EPD_WIDTH/2, 280, "Open:  192.168.4.1",            CENTER, EPD_WHITE, EPD_BLACK);
  epd_refresh();
}

// ═══════════════════════════════════════════════════════════════════
//  FULL FRAME RENDER
// ═══════════════════════════════════════════════════════════════════
static void drawDebugGrid() {
  // Vertical lines every 50px, labelled every 100px
  for(int x = 0; x < EPD_WIDTH; x += 50) {
    epd_vline(x, 0, EPD_HEIGHT, EPD_YELLOW);
    if(x % 100 == 0) {
      char buf[8]; sprintf(buf, "%d", x);
      gfx.setFont(u8g2_font_helvB08_tf);
      gfx.setForegroundColor(EPD_YELLOW);
      gfx.setBackgroundColor(EPD_BLACK);
      gfx.setCursor(x + 1, 12);
      gfx.print(buf);
    }
  }
  // Horizontal lines every 50px, labelled every 100px
  for(int y = 0; y < EPD_HEIGHT; y += 50) {
    epd_hline(0, y, EPD_WIDTH, EPD_YELLOW);
    if(y % 100 == 0) {
      char buf[8]; sprintf(buf, "%d", y);
      gfx.setFont(u8g2_font_helvB08_tf);
      gfx.setForegroundColor(EPD_YELLOW);
      gfx.setBackgroundColor(EPD_BLACK);
      gfx.setCursor(2, y + 10);
      gfx.print(buf);
    }
  }
}

// Header F1 mark, scaled (1bpp MSB-first rows, same semantics as drawBitmap: set = red)
static void drawF1LogoScaled(int16_t x0, int16_t y0, int scale) {
  if (scale < 1) scale = 1;
  const int rowBytes = (F1_LOGO_W + 7) / 8;
  for (int row = 0; row < F1_LOGO_H; row++) {
    for (int col = 0; col < F1_LOGO_W; col++) {
      uint8_t b = pgm_read_byte(&F1_Logo[row * rowBytes + col / 8]);
      int bit = 7 - (col & 7);
      bool on = (b >> bit) & 1;
      uint8_t c = on ? EPD_RED : EPD_BLACK;
      epd_fill_rect(x0 + col * scale, y0 + row * scale, scale, scale, c);
    }
  }
}

static void drawBootScreen() {
  memset(epd_buf, 0x00, EPD_BUF_SIZE);
  constexpr int kBootLogoScale = 4;
  const int lw = F1_LOGO_W * kBootLogoScale;
  const int lh = F1_LOGO_H * kBootLogoScale;
  const int logoX = (EPD_WIDTH - lw) / 2;
  const int logoY = 72;
  drawF1LogoScaled(logoX, logoY, kBootLogoScale);

  gfx.setFontMode(0);
  gfx.setForegroundColor(EPD_WHITE);
  gfx.setBackgroundColor(EPD_BLACK);
  gfx.setFont(u8g2_font_f1bold24_tf);
  const int lineGap = 30;
  int yQuote = logoY + lh + 36;
  drawStr(EPD_WIDTH / 2, yQuote, "It's lights out and", CENTER, EPD_WHITE, EPD_BLACK);
  drawStr(EPD_WIDTH / 2, yQuote + lineGap, "away we go!", CENTER, EPD_WHITE, EPD_BLACK);
}

static void rotateFramebuffer180() {
  int total = EPD_WIDTH * EPD_HEIGHT;
  for (int i = 0; i < total / 2; i++) {
    int j = total - 1 - i;
    int iIdx = i / 2, iNib = i % 2;
    int jIdx = j / 2, jNib = j % 2;
    uint8_t iVal = iNib ? (epd_buf[iIdx] & 0x0F) : (epd_buf[iIdx] >> 4);
    uint8_t jVal = jNib ? (epd_buf[jIdx] & 0x0F) : (epd_buf[jIdx] >> 4);
    if (iNib) epd_buf[iIdx] = (epd_buf[iIdx] & 0xF0) | (jVal & 0x0F);
    else      epd_buf[iIdx] = (epd_buf[iIdx] & 0x0F) | (jVal << 4);
    if (jNib) epd_buf[jIdx] = (epd_buf[jIdx] & 0xF0) | (iVal & 0x0F);
    else      epd_buf[jIdx] = (epd_buf[jIdx] & 0x0F) | (iVal << 4);
  }
}

static void renderFrame(bool fetchCalendar) {
  memset(epd_buf, 0x00, 192000);  // black background
  getLocalTime(&timeinfo);
  if (fetchCalendar)
    FetchCalendar();

  DrawHeader();
  DrawDividers();
  DrawLeftPanel();
  DrawDrivers();
  DrawConstructors();
  DrawFooter();

  if(DEBUG_GRID) drawDebugGrid();
  rotateFramebuffer180();
}

// ═══════════════════════════════════════════════════════════════════
//  SCHEDULER
// ═══════════════════════════════════════════════════════════════════

// Core "would we refresh on this clock minute?" logic (no lastCheckMinute gate).
static bool scheduleFiresAtMinute(int tm_min, int tm_hour, time_t epochLocal) {
  if (!nextRaceEpoch)
    return (tm_min == 0 && tm_hour % cfgUpdHrNear == 0);
  long diff = (long)(nextRaceEpoch - epochLocal);
  if (isInRaceWindowAt(epochLocal))
    return (tm_min % cfgUpdRaceMin == 0);
  if (diff > 0 && diff <= cfgPhaseGridSec) {
    if (tm_min % cfgUpdGridMin == 0) {
      if (availabilityCache.qualifyingValid && availabilityCache.round == nextRound &&
          availabilityCache.qualifyingAvailable &&
          (millis() - availabilityCache.qualifyingTimestamp) < cfgTtlQualAvailMs)
        return (tm_min == 0 && tm_hour % cfgUpdHrNear == 0);
      return true;
    }
  } else if (diff > cfgPhaseGridSec && diff <= cfgPhaseMidSec)
    return (tm_min == 0 && tm_hour % cfgUpdHrNear == 0);
  else if (diff > cfgPhaseMidSec)
    return (tm_min == 0 && tm_hour % cfgUpdHrFar == 0);
  return (tm_min == 0 && tm_hour % cfgUpdHrNear == 0);
}

static bool shouldUpdateNow(int curMin) {
  if (curMin == (int)lastCheckMinute) return false;
  return scheduleFiresAtMinute(curMin, timeinfo.tm_hour, time(nullptr));
}

static time_t computeNextScheduledUpdateEpoch() {
  time_t now = time(nullptr);
  time_t t = now - (now % 60) + 60;
  const time_t limit = now + 10 * 86400;
  for (; t <= limit; t += 60) {
    struct tm lt;
    if (!localtime_r(&t, &lt)) continue;
    // Do not apply lastCheckMinute here — that gate only prevents double-fires within one wall minute on device.
    // Using it in preview skips every future :00 when lastCheckMinute==0 (hourly schedule).
    if (scheduleFiresAtMinute(lt.tm_min, lt.tm_hour, t)) return t;
  }
  return 0;
}

static void formatNextUpdateFooter(char* buf, size_t buflen) {
  if (!buf || buflen < 12) return;
  time_t when = computeNextScheduledUpdateEpoch();
  if (!when) {
    snprintf(buf, buflen, "Next Update: --");
    return;
  }
  struct tm lt;
  if (!localtime_r(&when, &lt)) {
    snprintf(buf, buflen, "Next Update: --");
    return;
  }
  char dayPart[20];
  strftime(dayPart, sizeof(dayPart), "%a %m/%d ", &lt);
  char timePart[24];
  formatLocalTime(timePart, sizeof(timePart), &lt);
  snprintf(buf, buflen, "Next Update: %s%s", dayPart, timePart);
}


// ═══════════════════════════════════════════════════════════════════
//  SETUP
// ═══════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200); delay(2000);
  Serial.println(F("[F1] PhotoPainter start"));
  logEspResetReason();

  initPMIC();

  // I2S first so BCLK is running when ES8311 initialises
  initSD_Audio();    // SD on HSPI
  initAudio();       // I2S + BCLK running, PA enabled
  initES8311();      // codec init while BCLK is present

  prefs.begin("f1tracker", false);
  loadDeviceConfig();

  epd_buf=(uint8_t*)ps_malloc(EPD_BUF_SIZE);
  if(!epd_buf){Serial.println(F("PSRAM alloc failed")); delay(2000); ESP.restart();}
  Serial.printf("[MEM] buf=%p (%u bytes)\n", epd_buf, EPD_BUF_SIZE);

  epd_init();
  gfx.begin(display);
  gfx.setFontMode(0);
  gfx.setFontDirection(0);
  gfx.setBackgroundColor(EPD_BLACK);
  gfx.setFont(FONT_MED);

  if (cfgBootSplash) {
    drawBootScreen();
    rotateFramebuffer180();
    epd_refresh();
  }

  WiFiManager wm;
  wm.setAPCallback(configModeCallback);
  wm.setConfigPortalTimeout(300);
  Serial.println(F("[WiFi] Connecting..."));
  if(!wm.autoConnect("F1Tracker-Setup","formula1")){
    memset(epd_buf, 0x00, 192000);
    gfx.setFont(FONT_LARGE);
    drawStr(EPD_WIDTH/2,150,"WiFi Failed - Restarting",CENTER,EPD_RED);
    epd_refresh(); delay(3000); ESP.restart();
  }
  wifiConnected=true;
  setupConfigWeb();
  if (WiFi.status() == WL_CONNECTED)
    prefs.putString("wifiSsid", WiFi.SSID());
  Serial.printf("[WiFi] %s  |  Config: http://%s/\n",
                WiFi.localIP().toString().c_str(), WiFi.localIP().toString().c_str());

  Serial.println(F("[Audio] >>> Playing boot WAV"));
  if (cfgSoundBoot) playSoundRelFile(cfgSoundBootFile);
  else Serial.println(F("[Audio] Sounds off — skip boot WAV"));
  Serial.println(F("[Audio] >>> Boot WAV complete"));

  configTzTime(MY_TZ,NTP1,NTP2);
  while(!getLocalTime(&timeinfo)){
    handleConfigServer();
    delay(100);
    Serial.print(".");
  }
  Serial.println(F("\n[Time] Synced"));

  processedRound=prefs.getUInt("processedRound",0);
  nextRound=prefs.getUInt("nextRound",0);
  nextRaceEpoch=(time_t)prefs.getULong64("nextEpoch",0);
  nextRaceDateStr=prefs.getString("nextDate","");

  // Invalidate stale cached next-race: if the saved epoch is already in the
  // past, clear it so FetchCalendar() will pick the real next future round.
  if(nextRaceEpoch > 0 && nextRaceEpoch < time(nullptr)) {
    nextRound = 0; nextRaceEpoch = 0; nextRaceDateStr = "";
    prefs.putUInt("nextRound", 0);
    prefs.putULong64("nextEpoch", 0);
    prefs.putString("nextDate", "");
    Serial.println(F("[Prefs] Stale nextRace cleared"));
  }

  // Force fresh calendar fetch on first boot (ignore any cached response)
  apiCache.calendar.valid = false;

  renderFrame(true);
  epd_refresh();
  if (cfgSoundLoaded) playSoundRelFile(cfgSoundLoadedFile);
    if(lastRound){processedRound=lastRound; prefs.putUInt("processedRound",processedRound);}
  lastCheckMinute=timeinfo.tm_min;
  // Don’t trigger results celebration until well after startup boot WAV (same file slot).
  s_celebrateResultsNotBeforeMs = millis() + 22000UL;
  disconnectWiFiIfIdle();
  Serial.println(F("[Setup] Done"));
}

// ═══════════════════════════════════════════════════════════════════
//  LOOP
// ═══════════════════════════════════════════════════════════════════
void loop() {
  uint32_t periodStart = millis();
  while (millis() - periodStart < 60000) {
    handleConfigServer();
    if (!getLocalTime(&timeinfo)) {
      if (!ensureWiFiConnected()) {
        delay(2000);
        continue;
      }
      if (!getLocalTime(&timeinfo)) {
        delay(200);
        continue;
      }
    }
    int curMin = timeinfo.tm_min;
    if (screenRedrawNoDataPending) {
      screenRedrawNoDataPending = false;
      lastCheckMinute = (unsigned long)curMin;
      Serial.printf("\n[Redraw] %02d:%02d (cached data only)\n", timeinfo.tm_hour, curMin);
      apiNetworkFetchesDisabled = true;
      renderFrame(false);
      apiNetworkFetchesDisabled = false;
      epd_refresh();
      if (cfgSoundUpdate) playSoundRelFile(cfgSoundUpdateFile);
      disconnectWiFiIfIdle();
    } else if (forceDisplayRefreshPending) {
      forceDisplayRefreshPending = false;
      lastCheckMinute = (unsigned long)curMin;
      Serial.printf("\n[Force] %02d:%02d\n", timeinfo.tm_hour, curMin);
      if (!ensureWiFiConnected()) {
        Serial.println(F("[Force] no WiFi — skipping"));
        delay(200);
      } else {
        invalidateAllApiCaches();
        FetchCalendar();
        bool newRound = (processedRound != lastRound && lastRound != 0);
        if (newRound) invalidateStandingsCache();
        resultsAvailableForLastRound();
        renderFrame(true);
        epd_refresh();
        bool celebr = tryPlayFirstRaceResultsCelebration();
        if (cfgSoundUpdate && !celebr) playSoundRelFile(cfgSoundUpdateFile);
        processedRound = lastRound;
        prefs.putUInt("processedRound", processedRound);
        disconnectWiFiIfIdle();
      }
    } else if (shouldUpdateNow(curMin)) {
      lastCheckMinute = curMin;
      Serial.printf("\n[Sched] %02d:%02d\n", timeinfo.tm_hour, curMin);
      if (!ensureWiFiConnected()) {
        Serial.println(F("[Sched] no WiFi — skipping this cycle"));
        delay(200);
        continue;
      }
      FetchCalendar();
      bool newRound = (processedRound != lastRound && lastRound != 0);
      if (newRound) invalidateStandingsCache();
      resultsAvailableForLastRound();
      renderFrame(true);
      epd_refresh();
      bool celebr = tryPlayFirstRaceResultsCelebration();
      if (cfgSoundUpdate && !celebr) playSoundRelFile(cfgSoundUpdateFile);
      processedRound = lastRound;
      prefs.putUInt("processedRound", processedRound);
      disconnectWiFiIfIdle();
    }
    delay(50);
  }
}
