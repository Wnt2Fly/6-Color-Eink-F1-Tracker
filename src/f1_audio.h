#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <SD.h>
#include <SPI.h>
#include <time.h>
#include <driver/i2s.h>

// PhotoPainter: BCLK/Ws/DOUT/MCLK match boards/waveshare-s3-PhotoPainter/config.h —
// Playback data must leave GPIO 17 (→ ES8311 DSDIN). GPIO 18 is DIN (mic ADC → ESP).
#define I2S_MCLK   14
#define I2S_SCLK   15
#define I2S_LRCK   16
#define I2S_DOUT   17
#define I2S_DIN    18
#define PA_PIN     7

#ifndef I2C_SDA
#define I2C_SDA   47
#define I2C_SCL   48
#endif

#define SD_CS     38
#define SD_CLK    39
#define SD_MISO   40
#define SD_MOSI   41

#define ES8311_ADDR       0x18
#define AUDIO_SAMPLE_RATE 24000
// Matches esp_codec_dev ES8311: MCLK_DEFAULT_DIV=256 → 24000×256 Hz (see coeff table)
#define AUDIO_MCLK_HZ      (AUDIO_SAMPLE_RATE * 256)

// Playback loudness — ES8311 DAC digital volume linear 0…100 %
#ifndef AUDIO_VOLUME_PERCENT
#define AUDIO_VOLUME_PERCENT  60u
#endif

#define ES8311_DAC_MUTE_REG  0x31
#define ES8311_DAC_VOL_REG   0x32

static SPIClass sdSPI(HSPI);
static bool sdMounted = false;
static bool i2sReady  = false;
static bool es8311Ok  = false;
static uint8_t audioVolumePct = (uint8_t)AUDIO_VOLUME_PERCENT;

// Quiet hours — local TZ (same as status bar). Active when hour ∈ [start, end) or overnight if start > end.
static bool    g_audioQuietEn = false;
static uint8_t g_audioQuietSh = 22;
static uint8_t g_audioQuietEh = 7;

inline void audioConfigureQuietHours(bool en, uint8_t startH, uint8_t endH) {
  g_audioQuietEn = en;
  g_audioQuietSh = (uint8_t)(startH % 24);
  g_audioQuietEh = (uint8_t)(endH % 24);
}

inline bool audioInQuietHoursNow() {
  if (!g_audioQuietEn) return false;
  uint8_t s = g_audioQuietSh, e = g_audioQuietEh;
  if (s == e) return false;
  time_t now = time(nullptr);
  struct tm lt;
  if (!localtime_r(&now, &lt)) return false;
  int h = lt.tm_hour;
  if (s < e) return h >= (int)s && h < (int)e;
  return h >= (int)s || h < (int)e;
}

static void es8311_write(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(ES8311_ADDR);
  Wire.write(reg); Wire.write(val);
  Wire.endTransmission();
}

static uint8_t es8311_read(uint8_t reg) {
  Wire.beginTransmission(ES8311_ADDR);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom((uint8_t)ES8311_ADDR, (uint8_t)1);
  return Wire.available() ? Wire.read() : 0xFF;
}

// Maps 1…100 % → DAC_REG32; ≈matches previous 0xBF default at ~75 %
static inline uint8_t audioVolumePctToReg32(uint8_t pct) {
  if (pct >= 100) return 0xFF;
  return (uint8_t)(((uint16_t)pct * 255u + 99u) / 100u);
}

static inline void audioApplyVolumeRegs(bool printLog) {
  uint8_t pct = audioVolumePct;
  uint8_t m = es8311_read(ES8311_DAC_MUTE_REG);
  if (pct == 0) {
    m &= (uint8_t)0x9F;
    es8311_write(ES8311_DAC_MUTE_REG, (uint8_t)(m | 0x60));
    if (printLog) Serial.println(F("[Audio] Muted"));
    return;
  }
  es8311_write(ES8311_DAC_VOL_REG, audioVolumePctToReg32(pct));
  m &= (uint8_t)0x9F;
  es8311_write(ES8311_DAC_MUTE_REG, m);
  if (printLog) {
    Serial.printf("[Audio] Volume %u%% (REG32=0x%02X)\n", (unsigned)pct,
                  (unsigned)audioVolumePctToReg32(pct));
  }
}

/** After codec OK — 0 = mute, 1…100 % */
inline void audioSetVolumePercent(uint8_t pct) {
  if (!es8311Ok) return;
  if (pct > 100) pct = 100;
  audioVolumePct = pct;
  audioApplyVolumeRegs(true);
}

inline uint8_t audioGetVolumePercent() { return audioVolumePct; }

inline void audioRefreshOutputVolume() {
  if (!es8311Ok) return;
  audioApplyVolumeRegs(false);
}

/** Find PCM in WAV / read only `data` chunk length (don't trust `available()` on SD). */
static bool wavSeekPcmStart(File& f, uint32_t* pcmOffset, uint32_t* pcmByteLen,
                            uint16_t* channels, uint16_t* bitsPerSample,
                            uint32_t* sampleRate) {
  f.seek(0);
  uint8_t rh[12];
  if (f.read(rh, 12) != 12 || memcmp(rh, "RIFF", 4) != 0 ||
      memcmp(rh + 8, "WAVE", 4) != 0)
    return false;

  const uint32_t fsz = (uint32_t)f.size();
  uint16_t ch = 0, bd = 0;
  uint32_t rate = 0;
  bool haveFmt = false;

  while (true) {
    uint32_t pos = (uint32_t)f.position();
    if (pos + 8 > fsz || pos + 8 < pos) return false;
    char cid[4];
    if (f.read((uint8_t*)cid, 4) != 4) return false;
    uint32_t ckSz;
    if (f.read((uint8_t*)&ckSz, 4) != 4) return false;

    if (!memcmp(cid, "fmt ", 4)) {
      size_t mn = ckSz > 32 ? 32 : (size_t)ckSz;
      uint8_t fmt[32];
      if (ckSz < 16 || mn < 16 || f.read(fmt, mn) != (int)mn) return false;
      if (mn < ckSz) {
        uint32_t p = (uint32_t)f.position();
        uint32_t skip = ckSz - (uint32_t)mn;
        if (p + skip < p || p + skip > fsz) return false;
        if (!f.seek(p + skip)) return false;
      }
      uint16_t af = (uint16_t)(fmt[0] | (fmt[1] << 8));
      /* PCM (1) or WAVE_FORMAT_EXTENSIBLE (0xFFFE) — first 16 fmt bytes identical */
      if (af != 1 && af != (uint16_t)0xFFFE) return false;
      ch = (uint16_t)(fmt[2] | (fmt[3] << 8));
      rate =
          fmt[4] | (fmt[5] << 8) | (fmt[6] << 16) | ((uint32_t)fmt[7] << 24);
      bd = (uint16_t)(fmt[14] | (fmt[15] << 8));
      haveFmt = true;
      if (ckSz & 1u) {
        pos = (uint32_t)f.position();
        if (pos + 1 > fsz) return false;
        if (!f.seek(pos + 1)) return false;
      }
      continue;
    }
    if (!memcmp(cid, "data", 4)) {
      if (!haveFmt) return false;
      *pcmOffset = (uint32_t)f.position();
      *pcmByteLen = ckSz;
      *channels = ch;
      *bitsPerSample = bd;
      *sampleRate = rate;
      /* If RIFF lies, clamp playback to EOF */
      if (*pcmOffset + *pcmByteLen > fsz && *pcmOffset <= fsz)
        *pcmByteLen = fsz - *pcmOffset;
      return *pcmByteLen != 0;
    }
    pos = (uint32_t)f.position();
    uint32_t skip = ckSz + (ckSz & 1u);
    if (pos + skip < pos || pos + skip > fsz) return false;
    if (!f.seek(pos + skip)) return false;
  }
}

// Codec init aligned with Espressif es8311.c: 24kHz, stereo 16‑bit Philips I2S, MCLK=f_s×256
static bool initES8311() {
  delay(10);
  Wire.beginTransmission(ES8311_ADDR);
  if (Wire.endTransmission() != 0) {
    Serial.printf("[Audio] ES8311 not found\n");
    es8311Ok = false;
    return false;
  }

  // Coefficients for MCLK_HZ == 6144000, rate == 24000 (first matching row in es8311.c)
  const uint8_t kPreDiv       = 0x01;
  const uint8_t kPreMultiCode = 0;           // ×1 → bits [4:3] of REG02 = 0
  const uint8_t kAdcDiv       = 0x01;
  const uint8_t kDacDiv       = 0x01;
  const uint8_t kFsMode       = 0x00;
  const uint8_t kAdcOsr       = 0x10;
  const uint8_t kDacOsr       = 0x10;
  const uint8_t kLrckH       = 0x00;
  const uint8_t kLrckL       = 0xff;
  /* BCLK = MCLK / kBclkDiv — 6144000/8 = 768 kHz matches I2S 24k stereo 16-bit (32 clocks/LR tick) */
  const uint8_t kBclkDiv     = 0x08;

  es8311_write(0x00, 0x1F); delay(50);
  es8311_write(0x00, 0x00); delay(50);

  es8311_write(0x44, 0x08);
  es8311_write(0x44, 0x08);

  es8311_write(0x01, 0x30);
  es8311_write(0x02, 0x00);
  es8311_write(0x03, 0x10);
  es8311_write(0x16, 0x24);
  es8311_write(0x04, 0x10);
  es8311_write(0x05, 0x00);
  es8311_write(0x0B, 0x00);
  es8311_write(0x0C, 0x00);
  es8311_write(0x10, 0x1F);
  es8311_write(0x11, 0x7F);
  es8311_write(0x00, 0x80);

  // I2S slave: clear bit 6 of REG00
  es8311_write(0x00, es8311_read(0x00) & (uint8_t)~0x40);

  // Use external MCLK from ESP (don’t gate bit 7); no MCLK inversion
  uint8_t r01 = 0x3F;
  es8311_write(0x01, r01);

  // No BCLK inversion
  uint8_t r06inv = es8311_read(0x06);
  r06inv &= (uint8_t)~0x20;
  es8311_write(0x06, r06inv);

  es8311_write(0x13, 0x10);
  es8311_write(0x1B, 0x0A);
  es8311_write(0x1C, 0x6A);
  es8311_write(0x44, 0x58);   // internal ref (Espressif “no_dac_ref == false”)

  // ----- es8311_config_sample (6144000 / 24000 row) -----
  uint8_t r02 = es8311_read(0x02);
  r02 = (uint8_t)((r02 & 7) | ((((uint8_t)(kPreDiv)-1)) << 5) | (uint8_t)(kPreMultiCode << 3));
  es8311_write(0x02, r02);

  es8311_write(0x05, (uint8_t)(((kAdcDiv - 1) << 4) | ((kDacDiv - 1) << 0)));

  r02 = es8311_read(0x03);
  r02 = (uint8_t)((r02 & 0x80) | (kFsMode << 6) | kAdcOsr);
  es8311_write(0x03, r02);

  r02 = es8311_read(0x04);
  r02 = (uint8_t)((r02 & 0x80) | kDacOsr);
  es8311_write(0x04, r02);

  r02 = es8311_read(0x07);
  es8311_write(0x07, (uint8_t)((r02 & 0xC0) | kLrckH));

  es8311_write(0x08, kLrckL);

  uint8_t bclkLo = kBclkDiv < 19 ? (uint8_t)(kBclkDiv - 1) : kBclkDiv;
  uint8_t r06 = es8311_read(0x06);
  es8311_write(0x06, (uint8_t)((r06 & 0xE0) | bclkLo));

  // 16‑bit Philips I2S (REG09 dac / REG0a adc SDP)
  uint8_t dac9 = es8311_read(0x09);
  uint8_t adcA = es8311_read(0x0A);
  dac9 = (uint8_t)((dac9 & (uint8_t)~0x1C) | 0x0C);
  adcA = (uint8_t)((adcA & (uint8_t)~0x1C) | 0x0C);
  dac9 &= 0xFC;
  adcA &= 0xFC;
  dac9 |= 0x0C;
  adcA |= 0x0C;
  es8311_write(0x09, dac9);
  es8311_write(0x0A, adcA);

  // es8311_start — DAC‑only playback: SDP bit6 low on DAC channel, kept on ADC
  dac9 = es8311_read(0x09);
  adcA = es8311_read(0x0A);
  dac9 = (uint8_t)((dac9 & 0xBF) | 0x40);
  adcA = (uint8_t)((adcA & 0xBF) | 0x40);
  dac9 &= (uint8_t)~0x40;
  es8311_write(0x09, dac9);
  es8311_write(0x0A, adcA);

  es8311_write(0x17, 0xBF);
  es8311_write(0x0E, 0x02);
  es8311_write(0x12, 0x00);
  es8311_write(0x14, 0x1A);
  r02 = es8311_read(0x14);
  es8311_write(0x14, (uint8_t)(r02 & (uint8_t)~0x40));
  es8311_write(0x0D, 0x01);
  es8311_write(0x15, 0x40);
  es8311_write(0x37, 0x08);
  es8311_write(0x45, 0x00);
  es8311_write(0x0F, 0x9C);
  delay(100);

  pinMode(PA_PIN, OUTPUT);
  digitalWrite(PA_PIN, LOW);
  delay(20);
  digitalWrite(PA_PIN, HIGH);
  delay(50);

  es8311Ok = true;
  audioApplyVolumeRegs(false);

  /* Let ES8311 lock to BCLK/LRCK — REG0B[1:0]==2 is NORMAL; avoid starting silent playback */
  for (int i = 0; i < 50; i++) {
    uint8_t st = es8311_read(0x0B);
    if ((st & 3u) == 2u) break;
    delay(10);
  }

  uint8_t r0b = es8311_read(0x0B);
  Serial.printf("[ES8311] MCLK=%lu REG01=0x%02X REG02=0x%02X REG06=0x%02X REG0B=0x%02X %s\n",
    (unsigned long)AUDIO_MCLK_HZ,
    es8311_read(0x01), es8311_read(0x02), es8311_read(0x06), r0b,
    (r0b & 0x03) == 0x02 ? "NORMAL" : "NOT READY");

  Serial.printf("[Audio] Default volume=%u%%\n", (unsigned)audioVolumePct);

  Serial.println("[Audio] ES8311 ok");
  return true;
}

static bool initSD_Audio() {
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  delay(10);
  sdSPI.begin(SD_CLK, SD_MISO, SD_MOSI, SD_CS);
  delay(10);
  if (!SD.begin(SD_CS, sdSPI)) {
    Serial.println("[SD] Mount failed");
    return false;
  }
  sdMounted = true;
  Serial.println("[SD] Mounted ok");
  return true;
}

inline bool audioSdMounted() { return sdMounted; }

static bool initAudio() {
  i2s_config_t cfg = {};
  cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate          = AUDIO_SAMPLE_RATE;
  cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1 | ESP_INTR_FLAG_IRAM;
  cfg.dma_buf_count        = 8;
  cfg.dma_buf_len          = 512;
  cfg.use_apll             = true;
  cfg.fixed_mclk           = AUDIO_MCLK_HZ;
  cfg.tx_desc_auto_clear   = true;
  /* 16-bit slots (default bits_per_chan) → BCLK = 24000×32 Hz; paired with ES8311 kBclkDiv=8 */

  i2s_pin_config_t pins = {};
  pins.mck_io_num   = I2S_MCLK;  // wired on PhotoPainter — helps ES8311 PLL / clocking
  pins.bck_io_num   = I2S_SCLK;
  pins.ws_io_num    = I2S_LRCK;
  pins.data_out_num = I2S_DOUT;
  pins.data_in_num  = I2S_PIN_NO_CHANGE;

  esp_err_t err = i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("[Audio] i2s install failed: %s\n", esp_err_to_name(err));
    return false;
  }
  err = i2s_set_pin(I2S_NUM_0, &pins);
  if (err != ESP_OK) {
    Serial.printf("[Audio] i2s set_pin failed: %s\n", esp_err_to_name(err));
    return false;
  }
  /* set_clk before start: calling after start breaks TX on ESP32‑S3 legacy driver */
  /* 16-bit slot × stereo — must match cfg (no 32-bit slots) */
  err = i2s_set_clk(I2S_NUM_0, AUDIO_SAMPLE_RATE,
                    ((uint32_t)16 << 16) | (uint32_t)I2S_BITS_PER_SAMPLE_16BIT,
                    I2S_CHANNEL_STEREO);
  if (err != ESP_OK) Serial.printf("[Audio] i2s_set_clk warning: %s\n", esp_err_to_name(err));
  err = i2s_start(I2S_NUM_0);
  if (err != ESP_OK) Serial.printf("[Audio] i2s_start warning: %s\n", esp_err_to_name(err));

  // PA asserted in initES8311() after codec clocks / routing are valid

  i2sReady = true;
  Serial.println("[Audio] I2S ok");
  return true;
}

static void playWAV(const char* path) {
  if (!sdMounted || !i2sReady) {
    Serial.println(F("[Audio] playWAV: SD or I2S not ready"));
    return;
  }
  if (!es8311Ok) {
    Serial.println(F("[Audio] playWAV: ES8311 not initialised"));
    return;
  }
  if (audioInQuietHoursNow()) {
    Serial.printf("[Audio] Quiet hours — skip %s\n", path);
    return;
  }

  File f = SD.open(path);
  if (!f) {
    Serial.printf("[Audio] Not found: %s\n", path);
    return;
  }

  uint32_t pcmOff = 0, pcmLen = 0;
  uint16_t ch = 0, bits = 0;
  uint32_t rate = 0;
  if (!wavSeekPcmStart(f, &pcmOff, &pcmLen, &ch, &bits, &rate)) {
    Serial.printf("[Audio] Bad WAV (need RIFF/WAVE + fmt + data): %s\n", path);
    f.close();
    return;
  }
  if (!f.seek(pcmOff)) {
    Serial.printf("[Audio] WAV seek PCM start failed\n");
    f.close();
    return;
  }

  uint32_t pcmEnd = pcmOff + pcmLen;
  if (bits != 16) {
    Serial.printf("[Audio] WAV must be 16-bit PCM (got %hu)\n", (unsigned short)bits);
    f.close();
    return;
  }
  if (ch != 1 && ch != 2) {
    Serial.printf("[Audio] WAV must be mono or stereo (got %hu ch)\n", (unsigned short)ch);
    f.close();
    return;
  }

  Serial.printf("[WAV] %s: %luHz %huch %hubit data=%lu B @%lu\n",
                path, (unsigned long)rate, (unsigned short)ch,
                (unsigned short)bits, (unsigned long)pcmLen,
                (unsigned long)pcmOff);

  if (rate != AUDIO_SAMPLE_RATE)
    Serial.printf("[Audio] Warn: file %lu Hz != I2S %d Hz\n",
                  (unsigned long)rate, AUDIO_SAMPLE_RATE);
  if (audioVolumePct == 0)
    Serial.println(F("[Audio] Volume 0% — still muted"));

  audioRefreshOutputVolume();

  uint8_t buf[512];
  static int16_t monoDupStereo[512];
  size_t written, totalWritten = 0;
  unsigned long t0 = millis();

  while ((uint32_t)f.position() < pcmEnd && millis() - t0 < 120000) {
    uint32_t posNow = (uint32_t)f.position();
    if (posNow >= pcmEnd) break;
    size_t space = (size_t)(pcmEnd - posNow);
    size_t ask = sizeof(buf);
    if (space < ask) ask = space;
    int n = f.read(buf, ask);
    if (n <= 0) break;

    if (ch == 2) {
      n &= ~3;
      if (n <= 0) continue;
      esp_err_t werr =
          i2s_write(I2S_NUM_0, buf, (size_t)n, &written, portMAX_DELAY);
      if (werr != ESP_OK)
        Serial.printf("[Audio] i2s_write stereo: %s\n", esp_err_to_name(werr));
      totalWritten += written;
      yield();
    } else {
      n &= ~1;
      int nSamp = n / 2;
      if (nSamp <= 0) continue;
      if (nSamp > 256) nSamp = 256;
      const uint8_t* p = buf;
      for (int i = 0, o = 0; i < nSamp; i++) {
        int16_t s = (int16_t)(p[0] | (p[1] << 8));
        p += 2;
        monoDupStereo[o++] = s;
        monoDupStereo[o++] = s;
      }
      size_t nbytes = (size_t)nSamp * 4;
      esp_err_t werr =
          i2s_write(I2S_NUM_0, monoDupStereo, nbytes, &written, portMAX_DELAY);
      if (werr != ESP_OK)
        Serial.printf("[Audio] i2s_write mono: %s\n", esp_err_to_name(werr));
      totalWritten += written;
      yield();
    }
  }
  f.close();
  Serial.printf("[Audio] Done — I2S %u DMA bytes out in %lums\n",
                (unsigned)totalWritten, millis() - t0);
}

static void playBoot()   { playWAV("/sound/boot.wav"); }
static void playUpdate() { playWAV("/sound/update.wav"); }
static void playLoaded() { playWAV("/sound/loaded.wav"); }