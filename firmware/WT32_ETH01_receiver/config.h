#ifndef RECEIVER_CONFIG_H
#define RECEIVER_CONFIG_H

/* =============================================================
 * WT32-ETH01 接收機 (Receiver) 設定
 * -------------------------------------------------------------
 * 版本 (Version): 2.1.1   (見 FW_VERSION)
 * 名稱 (Name):    WT32-ETH01 Receiver
 * -------------------------------------------------------------
 * 功能:
 *   - 透過 Serial2 (GPIO5=RX, GPIO17=TX) 與 KKBOX 飛控板以 JSON 通訊
 *   - 內建 LAN8720A RJ45 有線網路 + WiFi 無線網路 (可同時)
 *   - SPIFFS 網頁介面 (ESPAsyncWebServer) + WebSocket 即時雙向
 *   - WiFi / RJ45 / 馬達模式 / 使用者 / OTA 設定 (PID 設定在遙控器)
 *   - 藍牙功能已於 2.1.0 移除: 遙控器一律經 WiFi WebSocket 連線
 * ============================================================= */

#include <Arduino.h>

#define FW_NAME    "WT32-ETH01 Receiver"
#define FW_VERSION "2.1.1"

/* ---------- 與 KKBOX 飛控板的 UART ---------- */
#define KK_RX_PIN   5
#define KK_TX_PIN   17
/* 9600: 與飛控板 (ESP32-S3 硬體 UART1) 一致; 舊 KKBOX 飛控板為 8MHz SoftwareSerial, 也只能 9600 */
#define KK_BAUD     9600

/* ---------- 內建 LAN8720A RJ45 (WT32-ETH01) ---------- */
#define ETH_ADDR    1
#define ETH_MDC     23
#define ETH_MDIO    18
#define ETH_POWER   16
/* 一般 WT32-ETH01 使用振盪器輸入到 GPIO0 */
/* ETH_CLOCK_GPIO0_IN 由 ESP32 core 的 ETH.h 提供 */

/* ---------- Web / AP ---------- */
#define WEB_PORT        80
/* SoftAP 一律啟動, 與 STA 共存; SSID = 前綴 + MAC 後 4 碼 (見 settings.h apSsid()) */
#define AP_SSID_PREFIX  "KKBOX-Receiver-"
#define AP_PASS         "12345678"    /* SoftAP 密碼, 至少 8 碼 */
#define SESSION_MAX     8

/* ---------- 預設帳密 ---------- */
#define DEF_USER  "admin"
#define DEF_PASS  "admin"

/* KKBOX motor[] 順序固定為 M1..M8：M1=FL(CCW), M2=FR(CW),
 * M3=RR(CCW), M4=RL(CW); M5..M8 為各位置 X8 下槳。 */

#endif