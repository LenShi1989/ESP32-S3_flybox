#ifndef RC_DISPLAY_H
#define RC_DISPLAY_H

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <string.h>
#include "config.h"
#include "settings.h"
#include "telem.h"
#include "input.h"

/* =============================================================
 * SSD1306 128x64 OLED
 * -------------------------------------------------------------
 * 版面 (size 1 字型, 每字 6x8 px):
 *   y0  : 標題列 (左: 頁名, 右: 頁碼)
 *   y9  : 分隔線
 *   y12/22/32/42/52 : 五行內容, 每行 8px, 最底結束於 y60
 *   => 128x64 全畫面可視, 不會再出現底部文字被切一半
 * -------------------------------------------------------------
 * VRC1 四段切換頁面:
 *   0 = 飛行遙測   1 = 硬體輸入
 *   2 = 網路資訊   3 = ID / 校正值
 * 注意: Adafruit 內建字型只支援 ASCII, 畫面文字一律用英文,
 *       中文只出現在網頁 (UTF-8) 與序列埠。
 * ============================================================= */

static Adafruit_SSD1306 oled(OLED_W, OLED_H, &Wire, -1);
static bool gOledOk = false;

/* 5 行內容區: 第 0 行 y=12, 行距 10px, 最後一行 y=52 (結束 y60) */
#define OLED_ROW_TOP 12
#define OLED_ROW_H   10

/* 將游標移到第 row 行、第 col 個字元 (每字 6px) */
static inline void oledAt(uint8_t row, uint8_t col = 0) {
  if (!gOledOk) return;
  oled.setCursor((int16_t)col * 6, (int16_t)OLED_ROW_TOP + (int16_t)row * OLED_ROW_H);
}

static inline void oledBegin() {
  Wire.begin(OLED_SDA, OLED_SCL);
  gOledOk = oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  if (!gOledOk) { Serial.println("[OLED] 啟動失敗"); return; }
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(0, 0);
  oled.println("KKBOX RC boot...");
  oled.display();
}

/* 共用的標題列: 左頁名, 右頁碼, 底線 */
static inline void oledHeader(const char* title, uint8_t page) {
  if (!gOledOk) return;
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(0, 0);
  oled.print(title);
  char b[8];
  snprintf(b, sizeof b, "%u/%u", (unsigned)(page + 1), (unsigned)OLED_PAGES);
  oled.setCursor(OLED_W - (int16_t)strlen(b) * 6, 0);
  oled.print(b);
  oled.drawFastHLine(0, 9, OLED_W, SSD1306_WHITE);
}

/* ---------- 開機: 選擇目標無人機 ID (SW1 按下確認) ---------- */
static inline void oledBootFlyId(uint8_t id, int raw) {
  if (!gOledOk) return;
  oled.clearDisplay();
  oled.setTextSize(2);
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(0, 0);
  oled.printf("FlyID %3u", (unsigned)id);
  oled.setTextSize(1);
  oled.setCursor(0, 24);
  oled.print("Turn VRC1 to select");
  oled.setCursor(0, 36);
  oled.print("Press SW1 to confirm");
  oled.setCursor(0, 48);
  oled.printf("VRC1 raw %4d", raw);
  oled.display();
}

/* ---------- 開機: 搖桿校正 (按住 SW1, 放開結束) ---------- */
static inline void oledBootCal() {
  if (!gOledOk) return;
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  /* 8 行 x 8px = 64px: 標題 + 3 行說明 + 4 個軸各一行 (每行 <= 11 字 = 66px,
     不再像原本 "T %4d-%4d Y %4d-%4d" 那樣超過 128px 被折行壓到下一行) */
  oled.setCursor(0, 0);  oled.print("Calibrating sticks"); // 18 字
  oled.setCursor(0, 8);  oled.print("Hold SW1, move all");   // 19 字
  oled.setCursor(0, 16); oled.print("sticks to both ends");  // 19 字
  oled.setCursor(0, 24); oled.print("Release SW1 = done");   // 18 字
  oled.setCursor(0, 32); oled.printf("T %4d-%4d", gCalTmin, gCalTmax);
  oled.setCursor(0, 40); oled.printf("Y %4d-%4d", gCalYmin, gCalYmax);
  oled.setCursor(0, 48); oled.printf("P %4d-%4d", gCalPmin, gCalPmax);
  oled.setCursor(0, 56); oled.printf("R %4d-%4d", gCalRmin, gCalRmax);
  oled.display();
}

/* ---------- 開機: 已跳過流程 ---------- */
static inline void oledBootSkip() {
  if (!gOledOk) return;
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(0, 0);  oled.print("SW2 = ON");
  oled.setCursor(0, 10); oled.print("Skip boot flow");
  oled.setCursor(0, 20); oled.print("Use saved ID / cal");
  oled.setCursor(0, 34); oled.printf("FlyID   %3u", (unsigned)gSet.flyId);
  oled.setCursor(0, 44); oled.printf("Cal     %s", gSet.calValid ? "OK" : "NOT CAL");
  oled.setCursor(0, 54); oled.print("Starting...");
  oled.display();
}

/* ---------- 頁面 0: 飛行遙測 ---------- */
static inline void oledPageFlight() {
  oledHeader("FLIGHT", 0);

  oledAt(0);
  oled.printf("ID %3u  RC %3u  ", (unsigned)gSet.flyId, (unsigned)gSet.rcId);
  oled.print(gLinkUp ? "LINK" : "----");

  oledAt(1);
  oled.printf("%-6s THR %4ld  %s",
              gTelem.armed ? "ARMED" : "DISARM", gTelem.thr,
              inputSourceCode(gCtrlActive));

  oledAt(2);
  oled.printf("R %+3.0f  P %+3.0f  Y %+3.0f", gTelem.roll, gTelem.pitch, gTelem.yaw);

  oledAt(3);
  oled.printf("M1 %4d  M2 %4d", gTelem.motor[0], gTelem.motor[1]);

  oledAt(4);
  oled.printf("M3 %4d  M4 %4d", gTelem.motor[2], gTelem.motor[3]);

  oled.display();
}

/* ---------- 頁面 1: 硬體輸入 ---------- */
static inline void oledPageInput() {
  oledHeader("HARDWARE", 1);

  oledAt(0); oled.printf("THR %4d > %4ld", gHw.rawT, gHw.thr);
  oledAt(1); oled.printf("YAW %4d > %+4.0f", gHw.rawY, gHw.yaw);
  oledAt(2); oled.printf("PIT %4d > %+4.0f", gHw.rawP, gHw.pitch);
  oledAt(3); oled.printf("ROL %4d > %+4.0f", gHw.rawR, gHw.roll);
  oledAt(4); oled.printf("SW%d%d%d%d VRC%3u CAL %s",
                         gHw.sw1, gHw.sw2, gHw.sw3, gHw.sw4,
                         (unsigned)gHw.vrc1Id, gSet.calValid ? "OK" : "NO");

  oled.display();
}

/* ---------- 頁面 2: 網路資訊 ---------- */
static inline void oledPageNet() {
  bool wc = (WiFi.status() == WL_CONNECTED);
  oledHeader("NETWORK", 2);

  oledAt(0);
  oled.print("STA ");
  oled.print((wc ? WiFi.SSID() : gSet.ssid).substring(0, 13));

  oledAt(1);
  oled.print("IP  ");
  oled.print(wc ? WiFi.localIP().toString() : String("-"));

  oledAt(2);
  oled.printf("RSSI %d dBm", wc ? WiFi.RSSI() : 0);

  oledAt(3);
  oled.print("AP  ");
  oled.print(apSsid().substring(0, 15));

  oledAt(4);
  oled.print("RX  ");
  oled.print(gSet.rxHost.length() ? gSet.rxHost.substring(0, 10) : String("-"));
  oled.printf(":%u", (unsigned)gSet.rxPort);

  oled.display();
}

/* ---------- 頁面 3: ID / 校正值 ---------- */
static inline void oledPageCal() {
  oledHeader("ID / CAL", 3);

  oledAt(0);
  oled.printf("ID %3u RC %3u CAL %s", (unsigned)gSet.flyId, (unsigned)gSet.rcId,
              gSet.calValid ? "OK" : "NO");

  oledAt(1); oled.printf("THR %4d-%4d", gSet.tmin, gSet.tmax);
  oledAt(2); oled.printf("YAW %4d-%4d", gSet.ymin, gSet.ymax);
  oledAt(3); oled.printf("PIT %4d-%4d", gSet.pmin, gSet.pmax);
  oledAt(4); oled.printf("ROL %4d-%4d", gSet.rmin, gSet.rmax);

  oled.display();
}

/* ---------- 主更新 (依 VRC1 頁面) ---------- */
static inline void oledShow() {
  if (!gOledOk) return;
  switch (gPage) {
    case 1:  oledPageInput();  break;
    case 2:  oledPageNet();    break;
    case 3:  oledPageCal();    break;
    default: oledPageFlight(); break;
  }
}

#endif