#ifndef RECEIVER_SENSORS_H
#define RECEIVER_SENSORS_H

#include <Arduino.h>
#include <SoftwareSerial.h>
#include <string.h>
#include "config.h"

/* =============================================================
 * 外接感測器: GPS x2 (NMEA 0183) + 雷射測距 (LRFX0M20KHS)
 * -------------------------------------------------------------
 * ESP32 只有 3 組硬體 UART, 本機分配如下:
 *   UART0  Serial   : USB 序列監控 (115200)
 *   UART2  Serial2  : KK 飛控板 JSON (GPIO5 RX / GPIO17 TX)
 *   UART1  Serial1  : 雷射測距, 只收不送 (GPIO39 RX)
 *   SoftSerial      : GPS1 (GPIO36 RX), GPS2 (GPIO35 RX)
 *
 * GPIO39 / 36 / 35 是 WT32-ETH01 的「僅輸入」腳位, 剛好只當 RX 用,
 * 不會佔用 CFG(IO32) / 485_EN(IO33) / IO15 / IO14 / IO12 / IO4 / IO2。
 *
 * 接線 (三支模組都只送不接命令, 因此只需接 TX -> RX):
 *   GPS1 TXD -> IO36      GPS2 TXD -> IO35      雷射 TX -> IO39
 *   三支模組 GND 與本機 GND 一定要共地; GPS 依模組吃 5V 或 3.3V。
 *
 * GPS: YIC GT-504GGB (G-Mouse), 出廠 115200 8N1, 連續輸出 NMEA,
 *      解析 $xxGGA (定位/海拔/衛星)、$xxRMC (速度/航向)、
 *      $xxGLL (定位備援)。talker 可能是 GN / GP / GL / GB / GA。
 *
 * 雷射: LRFX0M20KHS, 每封包 4 bytes: 5C DL DH CHK
 *        DL/DH = 距離 cm (低位在前), CHK = (DL+DH) 的反相 / 二補數。
 *        例: 5C 02 11 EC -> 0x1102 = 4354 cm = 43.54 m (CHK = ~0x13)。
 *        資料表另提到 "8 bytes per frame" 與含品質位元組的格式,
 *        若實機不是 4 bytes, 只要改 LASER_FRAME_LEN / LASER_DIST_LO
 *        兩處即可 (檢查碼一律視為最後一個位元組)。
 * ============================================================= */

/* ---------- GPS 資料 ---------- */
struct GpsData {
  bool     valid;        /* 有有效定位 (座標可用) */
  uint8_t  quality;      /* GGA 定位品質: 0=無 1=單點 2=DGPS 4=RTK固定 5=RTK浮動 */
  uint8_t  sats;         /* 使用衛星數 */
  float    hdop;         /* 水平精度因子 */
  double   lat, lon;     /* 十進位度, 南緯 / 西經為負 */
  float    alt;          /* 海拔 (MSL, m) */
  float    geoid;        /* 大地水準面差距 (m) */
  float    kmh;          /* 地面速度 (km/h, 由 RMC 換算) */
  float    course;       /* 航向 (度) */
  uint32_t lastMs;       /* 最後收到 NMEA 句子的時間 */
  uint32_t sentences;    /* 已解析句子數 */
  uint32_t bad;          /* 檢查碼錯誤 / 丟棄的句子數 */
};

static GpsData gGps[GPS_COUNT];

/* ---------- 雷射測距資料 ---------- */
struct LaserData {
  uint16_t distanceCm;
  float    distanceM;
  uint32_t lastMs;
  uint32_t frames;       /* 有效封包數 */
  uint32_t bad;          /* 檢查碼錯誤 / 重新同步次數 */
};

static LaserData gLaser;

/* ---------- 序列埠物件 ----------
 * EspSoftwareSerial: 以 GPIO 中斷記錄位元邊緣, 主程式再組回位元組,
 * 不佔用硬體 UART / 定時器, 115200 8N1 可同時跑兩組。
 */
static SoftwareSerial gGps1Ser(GPS1_RX_PIN, -1);
static SoftwareSerial gGps2Ser(GPS2_RX_PIN, -1);
static Stream* const gGpsStream[GPS_COUNT] = { &gGps1Ser, &gGps2Ser };

/* ---------- NMEA 解析 ---------- */
static inline int8_t nmeaHex(char c) {
  if (c >= '0' && c <= '9') return (int8_t)(c - '0');
  if (c >= 'A' && c <= 'F') return (int8_t)(c - 'A' + 10);
  if (c >= 'a' && c <= 'f') return (int8_t)(c - 'a' + 10);
  return -1;
}

/* 驗證 $...*hh 檢查碼; 沒有 '*' 的句子直接放行 */
static bool nmeaCheck(const char* s) {
  const char* star = strchr(s, '*');
  if (!star) return true;
  int8_t hi = nmeaHex(star[1]);
  int8_t lo = nmeaHex(star[2]);
  if (hi < 0 || lo < 0) return false;
  uint8_t sum = 0;
  for (const char* p = s + 1; p < star; p++) sum ^= (uint8_t)*p;
  return sum == (uint8_t)((hi << 4) | lo);
}

/* 依 ',' 切欄位 (就地改寫字串), '*' 之後視為結束; 回傳欄位數 */
static uint8_t nmeaSplit(char* s, char** f, uint8_t maxF) {
  uint8_t n = 0;
  f[n++] = s;
  for (; *s; s++) {
    if (*s == ',') {
      *s = 0;
      if (n < maxF) f[n++] = s + 1;
    } else if (*s == '*') {
      *s = 0;
      break;
    }
  }
  return n;
}

/* ddmm.mmmm / dddmm.mmmm -> 十進位度 */
static double nmeaDeg(const char* v, char hemi) {
  if (!v || !*v) return 0.0;
  double raw = atof(v);
  int deg = (int)(raw / 100.0);
  double out = (double)deg + (raw - (double)deg * 100.0) / 60.0;
  if (hemi == 'S' || hemi == 'W') out = -out;
  return out;
}

static void gpsHandleSentence(uint8_t idx, char* s) {
  if (s[0] != '$') return;
  GpsData& g = gGps[idx];
  if (!nmeaCheck(s)) { g.bad++; return; }

  char* f[16];
  uint8_t n = nmeaSplit(s, f, 16);
  if (n < 2) return;

  size_t len = strlen(f[0]);
  if (len < 6) return;
  const char* type = f[0] + len - 3;   /* GGA / RMC / GLL */

  g.lastMs = millis();
  g.sentences++;
  if (g.sentences == 1) Serial.printf("[GPS%u] 收到 %s\n", (unsigned)(idx + 1), f[0]);

  if (!strcmp(type, "GGA") && n >= 10) {
    g.quality = (uint8_t)constrain(atoi(f[6]), 0, 8);
    g.sats    = (uint8_t)constrain(atoi(f[7]), 0, 64);
    g.hdop    = atof(f[8]);
    g.alt     = atof(f[9]);
    g.geoid   = (n >= 12) ? atof(f[11]) : 0.0f;
    g.lat     = nmeaDeg(f[2], f[3][0]);
    g.lon     = nmeaDeg(f[4], f[5][0]);
    g.valid   = (g.quality > 0) && f[2][0] && f[4][0];

  } else if (!strcmp(type, "RMC") && n >= 9) {
    if (f[2][0] == 'A') {
      if (!g.valid && f[3][0] && f[5][0]) {
        g.lat = nmeaDeg(f[3], f[4][0]);
        g.lon = nmeaDeg(f[5], f[6][0]);
        g.valid = true;
      }
      g.kmh    = (float)atof(f[7]) * 1.852f;   /* knots -> km/h */
      g.course = (float)atof(f[8]);
    } else {
      g.valid = false;                          /* V = 無效定位 */
    }

  } else if (!strcmp(type, "GLL") && n >= 7) {
    if (f[6][0] == 'A' && f[1][0] && f[3][0]) {
      g.lat = nmeaDeg(f[1], f[2][0]);
      g.lon = nmeaDeg(f[3], f[4][0]);
      g.valid = true;
    }
  }
}

/* ---------- GPS 位元組流 -> 行緩衝 -> 解析 ---------- */
struct GpsRx {
  char     buf[GPS_LINE_MAX];
  uint16_t len;
  bool     overflow;
};

static GpsRx gGpsRx[GPS_COUNT];

static void gpsFeed(uint8_t idx, char c) {
  GpsRx& r = gGpsRx[idx];
  if (c == '\r') return;
  if (c == '\n') {
    if (r.len > 6 && !r.overflow) {
      r.buf[r.len] = 0;
      gpsHandleSentence(idx, r.buf);
    }
    r.len = 0;
    r.overflow = false;
    return;
  }
  if (r.len >= GPS_LINE_MAX - 1) {   /* 過長: 丟棄整行, 等下一行 */
    r.overflow = true;
    r.len = 0;
    return;
  }
  r.buf[r.len++] = c;
}

/* ---------- 雷射測距 (二進位封包) ---------- */
static uint8_t gLaserBuf[LASER_FRAME_LEN];
static uint8_t gLaserLen = 0;

/* 檢查碼比對: 資料表範例 5C 02 11 EC 的 EC 是 0x02+0x11=0x13 的
 * 「反相」(~0x13), 但文字又寫成二補數 (0x100-0x13=0xED)。兩種都
 * 可能存在, 因此都接受, 避免實機用反相時所有封包都被丟掉。 */
static inline bool laserChkOk(uint8_t sum, uint8_t chk) {
  return chk == (uint8_t)(0u - sum) || chk == (uint8_t)~sum;
}

static void laserFeed(uint8_t b) {
  if (gLaserLen == 0) {
    if (b != LASER_HEAD) return;
  } else if (b == LASER_HEAD && gLaserLen == LASER_FRAME_LEN - 1) {
    /* 幾乎可確定舊封包位移: 以這個 0x5C 重新開始 */
    gLaserLen = 0;
  }
  gLaserBuf[gLaserLen++] = b;
  if (gLaserLen < LASER_FRAME_LEN) return;

  uint8_t sum = 0;
  for (uint8_t i = 1; i < LASER_FRAME_LEN - 1; i++) sum += gLaserBuf[i];

  if (laserChkOk(sum, gLaserBuf[LASER_FRAME_LEN - 1])) {
    uint16_t cm = (uint16_t)gLaserBuf[LASER_DIST_LO] |
                  ((uint16_t)gLaserBuf[LASER_DIST_LO + 1] << 8);
    gLaser.frames++;
    gLaser.lastMs     = millis();
    gLaser.distanceCm = cm;
    gLaser.distanceM  = cm / 100.0f;
    if (gLaser.frames == 1) Serial.printf("[LASER] 首筆距離 %u cm\n", (unsigned)cm);
    gLaserLen = 0;
    return;
  }

  /* 檢查碼錯誤: 丟掉第一個位元組, 若殘留 0x5C 就從那裡重新同步 */
  gLaser.bad++;
  uint8_t start = LASER_FRAME_LEN;
  for (uint8_t i = 1; i < LASER_FRAME_LEN; i++) {
    if (gLaserBuf[i] == LASER_HEAD) { start = i; break; }
  }
  gLaserLen = 0;
  for (uint8_t i = start; i < LASER_FRAME_LEN; i++) gLaserBuf[gLaserLen++] = gLaserBuf[i];
}

/* ---------- 狀態查詢 ---------- */
static inline bool gpsAlive(uint8_t i) {
  return gGps[i].lastMs && (millis() - gGps[i].lastMs) < (uint32_t)GPS_TIMEOUT_MS;
}
static inline bool gpsFixed(uint8_t i) {
  return gpsAlive(i) && gGps[i].valid;
}
static inline bool laserAlive() {
  return gLaser.lastMs && (millis() - gLaser.lastMs) < (uint32_t)LASER_TIMEOUT_MS;
}

/* ---------- 初始化 / 輪詢 ---------- */
static void sensorsBegin() {
  memset(gGps, 0, sizeof(gGps));
  memset(gGpsRx, 0, sizeof(gGpsRx));
  memset(&gLaser, 0, sizeof(gLaser));
  gLaserLen = 0;

#if SENSOR_ENABLE
  gGps1Ser.begin(GPS_BAUD);
  gGps2Ser.begin(GPS_BAUD);
  /* 只接 RX: TX 傳 -1, 避免佔用 UART1 預設 TX (GPIO10 = 快閃記憶體) */
  Serial1.begin(LASER_BAUD, SERIAL_8N1, LASER_RX_PIN, -1);

  Serial.printf("[GPS] GPS1 RX=IO%d, GPS2 RX=IO%d @ %d 8N1 (NMEA)\n",
                GPS1_RX_PIN, GPS2_RX_PIN, GPS_BAUD);
  Serial.printf("[LASER] UART1 RX=IO%d @ %d 8N1 (LRFX0M20KHS)\n",
                LASER_RX_PIN, LASER_BAUD);
#else
  Serial.println("[SENSOR] 已停用 (config.h SENSOR_ENABLE=0)");
#endif
}

static void sensorsLoop() {
#if SENSOR_ENABLE
  for (uint8_t i = 0; i < GPS_COUNT; i++) {
    Stream* st = gGpsStream[i];
    uint16_t guard = 0;
    while (st->available() && guard++ < 256) gpsFeed(i, (char)st->read());
  }

  uint16_t guard = 0;
  while (Serial1.available() && guard++ < 1024) laserFeed((uint8_t)Serial1.read());
#endif
}

/* ---------- 給網頁的 JSON ---------- */
static String sensorsJson() {
  const uint32_t now = millis();
  String s;
  s.reserve(640);
  s = "{\"ms\":" + String(now) + ",\"gps\":[";

  for (uint8_t i = 0; i < GPS_COUNT; i++) {
    const GpsData& g = gGps[i];
    bool alive = gpsAlive(i);
    bool fixed = alive && g.valid;
    if (i) s += ',';
    s += "{\"ok\":" + String(alive ? 1 : 0);
    s += ",\"valid\":" + String(fixed ? 1 : 0);
    s += ",\"q\":" + String((unsigned)g.quality);
    s += ",\"sats\":" + String((unsigned)g.sats);
    s += ",\"hdop\":" + String(g.hdop, 1);
    s += ",\"lat\":" + String(fixed ? g.lat : 0.0, 6);
    s += ",\"lon\":" + String(fixed ? g.lon : 0.0, 6);
    s += ",\"alt\":" + String(g.alt, 1);
    s += ",\"geoid\":" + String(g.geoid, 1);
    s += ",\"kmh\":" + String(g.kmh, 1);
    s += ",\"course\":" + String(g.course, 1);
    s += ",\"age\":" + String(alive ? (now - g.lastMs) : 999999UL);
    s += ",\"sent\":" + String(g.sentences) + "}";
  }

  bool lz = laserAlive();
  s += "],\"laser\":{\"ok\":" + String(lz ? 1 : 0);
  s += ",\"cm\":" + String(gLaser.distanceCm);
  s += ",\"m\":" + String(gLaser.distanceM, 2);
  s += ",\"age\":" + String(lz ? (now - gLaser.lastMs) : 999999UL);
  s += ",\"frames\":" + String(gLaser.frames);
  s += ",\"bad\":" + String(gLaser.bad) + "}}";
  return s;
}

#endif
