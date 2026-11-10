/*
 * WT32-ETH01 接收機 (Receiver) — KKBOX 飛控系統
 * ---------------------------------------------------------------
 * 版本 (Version): 2.1.1   (見 config.h 的 FW_VERSION)
 * 名稱 (Name):    WT32-ETH01 Receiver
 * ---------------------------------------------------------------
 * - 內建 LAN8720A RJ45 有線網路 + WiFi 無線網路 (可同時)
 * - 無藍牙: 遙控器一律經 WiFi WebSocket 連線 (2.1.0 移除藍牙功能)
 * - SPIFFS 網頁介面 (ESPAsyncWebServer) + WebSocket 即時雙向
 * - 與 KKBOX 飛控板以 Serial2 (GPIO5/GPIO17, 9600) 交換 JSON
 * - 網頁: 系統狀態 / 網路設定 / 馬達模式 / 使用者設定 / OTA
 * - ESPAsyncWebServer + AsyncWebSocket + ElegantOTA
 * ---------------------------------------------------------------
 * 網頁需先登入 (預設 admin / kkbox)。
 *
 * 註: 提醒 (WT32-ETH01) GPIO16 為 PHY 電源, 不可他用。
 */

#include <Arduino.h>
#include "soc/rtc_cntl_reg.h"   /* BOD (電壓不足偵測) 停用 */
#include "config.h"
#include "settings.h"
#include "kkbox.h"
#include "web.h"

void setup() {
  /* 停用電壓不足偵測 (BOD): 避免 WiFi/RJ45(PHY) 瞬間電流造成 brownout reset */
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("=================================");
  Serial.println(" " FW_NAME "  v" FW_VERSION);
  Serial.println("==========================================================");

  if (!SPIFFS.begin(true)) {
    Serial.println("[SPIFFS] 掛載失敗 (已嘗試格式化)");
  } else {
    Serial.printf("[SPIFFS] total=%u used=%u\n",
                  (unsigned)SPIFFS.totalBytes(), (unsigned)SPIFFS.usedBytes());
  }

  settingsLoad();

  /* ---------- RJ45 (LAN8720A) ---------- */
  bool ok = ETH.begin(ETH_PHY_LAN8720, ETH_ADDR, ETH_MDC, ETH_MDIO, ETH_POWER, ETH_CLOCK_GPIO0_IN);
  Serial.printf("[ETH] begin=%d dhcp=%d\n", ok, gSet.dhcp);
  if (!gSet.dhcp) {
    IPAddress ip, gw, mask, dns;
    ip.fromString(gSet.ip);
    gw.fromString(gSet.gw);
    mask.fromString(gSet.mask);
    dns.fromString(gSet.gw);
    ETH.config(ip, gw, mask, dns);
    Serial.printf("[ETH] static ip=%s\n", gSet.ip.c_str());
  }

  /* ---------- WiFi: STA + AP 共存 ---------- */
  /* 一律啟動 SoftAP, 確保任情況下都能進入設定頁; STA 則連線外部 WiFi */
  WiFi.mode(WIFI_AP_STA);
  WiFi.setHostname("kkbox-receiver");
  {
    String ap = apSsid();
    WiFi.softAP(ap.c_str(), AP_PASS);
    Serial.printf("[AP] %s -> %s\n", ap.c_str(), WiFi.softAPIP().toString().c_str());
  }
  if (gSet.ssid.length()) {
    WiFi.begin(gSet.ssid.c_str(), gSet.wpass.c_str());
    Serial.printf("[WiFi] connecting to %s ...\n", gSet.ssid.c_str());
  }

  /* ---------- 飛控板 UART & 網頁 ---------- */
  kkBegin();
  webSetup();

  Serial.println("[WEB] 伺服器已啟動 (port 80)");
}

void loop() {
  /* 讀取飛控板 JSON 行並廣播 (含串口監控頁所需之全部行) */
  String line;
  while (kkReadLine(line)) {
    gKkLines++;
    if (line.indexOf("\"t\":") >= 0) {
      gLastTelemetry   = line;
      gLastTelemetryMs = millis();
    } else if (line.indexOf("\"ack\":") >= 0) {
      gLastAck = line;
    } else if (line.indexOf("\"evt\":") >= 0) {
      gLastEvt = line;
    }
    /* t/ack/evt 以外 (如 err) 也一併廣播, 供 RS232 串口通訊頁即時顯示 RX */
    ws.textAll(line);
  }

  /* WiFi 斷線自動重連 (SoftAP 持續運作, 僅重連 STA) */
  static uint32_t lastWifiTry = 0;
  if (gSet.ssid.length() && WiFi.status() != WL_CONNECTED) {
    if (millis() - lastWifiTry > 10000) {
      lastWifiTry = millis();
      WiFi.disconnect();
      WiFi.begin(gSet.ssid.c_str(), gSet.wpass.c_str());
    }
  }

  ws.cleanupClients();
  ElegantOTA.loop();

  if (gRebootAt && millis() > gRebootAt) {
    Serial.println("[SYS] 重新啟動以套用設定...");
    delay(50);
    ESP.restart();
  }

  delay(1);
}
