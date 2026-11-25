#ifndef RECEIVER_CONFIG_H
#define RECEIVER_CONFIG_H

/* =============================================================
 * WT32-ETH01 接收機 (Receiver) 設定
 * -------------------------------------------------------------
 * 版本 (Version): 2.3.3   (見 FW_VERSION)
 * 名稱 (Name):    WT32-ETH01 Receiver
 * -------------------------------------------------------------
 * 功能:
 *   - 透過 Serial2 (GPIO5=RX, GPIO17=TX) 與 ESP32-S3 Flybox 飛控板以 JSON 通訊
 *   - 內建 LAN8720A RJ45 有線網路 + WiFi 無線網路 (可同時)
 *   - SPIFFS 網頁介面 (ESPAsyncWebServer) + WebSocket 即時雙向
 *   - WiFi / RJ45 / 馬達模式 / 使用者 / OTA 設定 (PID 設定在遙控器)
 *   - 藍牙功能已於 2.1.0 移除: 遙控器一律經 WiFi WebSocket 連線
 *   - 外接感測器: GPS x2 (NMEA 0183) + 雷射測距 (LRFX0M20KHS),
 *     資料由 /api/sensors 提供, 顯示於網頁側邊欄
 * ============================================================= */

#include <Arduino.h>

#define FW_NAME    "WT32-ETH01 Receiver"
#define FW_VERSION "2.4.0"

/* ---------- 與 ESP32-S3 Flybox 飛控板的 UART ---------- */
#define KK_RX_PIN   5
#define KK_TX_PIN   17
/* 115200: 與 ESP32-S3 飛控板 (硬體 UART1) 一致, 可容納含姿態/地磁的遙測。
 * 若仍需連接舊 ESP32-S3 Flybox 飛控板 (8MHz SoftwareSerial, 上限 9600), 請改回 9600
 * 並同步調整飛控 config.h 的 RS232_BAUD 與遙測間隔。 */
#define KK_BAUD     115200

/* ---------- 外接感測器 (GPS x2 + 雷射測距) ----------
 * 三支腳都只當 RX (模組 TX -> 本機 RX, 必須共地), 因此選用
 * WT32-ETH01 的「僅輸入」腳 IO39 / IO36 / IO35, 不佔用其他 IO。
 *   雷射 LRFX0M20KHS : 硬體 UART1, RX = IO39 (只收不送)
 *   GPS1 (GT-504GGB) : 軟體序列,   RX = IO36
 *   GPS2 (GT-504GGB) : 軟體序列,   RX = IO35
 *
 * GPS 模組出廠 115200 8N1, 連續輸出 NMEA 0183
 * ($GNGGA / $GNRMC / $GNGLL, talker 也可能是 GP/GL/GB/GA)。
 *
 * 雷射模組資料表出廠值是 460800, 本專案統一用 115200:
 *   - 模組已是 115200: 直接使用 (預設值)
 *   - 模組仍是 460800: 先改成 115200 (送 5A 06 02 80 04 73),
 *     或把 LASER_BAUD 改成 460800。
 * 不需要外接感測器時把 SENSOR_ENABLE 設為 0, 不會佔用任何腳位。
 */
#define SENSOR_ENABLE     1
#define GPS_COUNT         2
#define GPS1_RX_PIN       36
#define GPS2_RX_PIN       35
#define GPS_BAUD          115200
#define GPS_TIMEOUT_MS    3000     /* 超過此時間沒 NMEA 視為離線 */
#define GPS_LINE_MAX      128      /* 單行 NMEA 最大長度 */

#define LASER_RX_PIN      39
#define LASER_BAUD        115200
#define LASER_TIMEOUT_MS  1000     /* 超過此時間沒封包視為離線 */
#define LASER_HEAD        0x5C     /* 封包開頭固定 0x5C */
#define LASER_FRAME_LEN   4        /* 5C DL DH CHK */
#define LASER_DIST_LO     1        /* 距離低位元組在封包中的位置 */

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
#define AP_SSID_PREFIX  "Flybox-Receiver-"
#define AP_PASS         "12345678"    /* SoftAP 密碼, 至少 8 碼 */
/* mDNS 主機名: 區網內可用 http://flybox-receiver.local/ 連線 (不必知道 IP) */
#define MDNS_HOST       "flybox-receiver"
#define SESSION_MAX     8

/* ---------- 預設帳密 ---------- */
#define DEF_USER  "admin"
#define DEF_PASS  "admin"

/* ESP32-S3 Flybox motor[] 順序固定為 M1..M8：M1=FL(CCW), M2=FR(CW),
 * M3=RR(CCW), M4=RL(CW); M5..M8 為各位置 X8 下槳。 */

#endif