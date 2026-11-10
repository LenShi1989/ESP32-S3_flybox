#ifndef RECEIVER_SETTINGS_H
#define RECEIVER_SETTINGS_H

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <esp_mac.h>
#include "config.h"

/* SoftAP SSID = 前綴 + MAC 後 4 碼 (例: KKBOX-Receiver-A1B2) */
static inline String apSsid() {
  uint8_t mac[6] = {0};
  /* 直接讀 eFuse 基礎 MAC, 避免 WiFi 驅動尚未啟動時 macAddress() 回傳 0 */
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char buf[40];
  snprintf(buf, sizeof(buf), "%s%02X%02X", AP_SSID_PREFIX, mac[4], mac[5]);
  return String(buf);
}

/* 以 NVS (Preferences) 儲存設定 */
struct Settings {
  /* 登入帳密 */
  String user;
  String pass;

  /* WiFi 站台 */
  String ssid;
  String wpass;

  /* RJ45 固定 IP */
  bool   dhcp;
  String ip;
  String gw;
  String mask;

  /* 裝置連結 Token (供 ESP32 D1 mini RC 連線 WS 用) */
  String deviceToken;

  /* 本機無人機 ID (0~255), 顯示於飛行狀態頁 */
  uint8_t droneId;
};

static Preferences gPrefs;
static Settings    gSet;

/* 產生隨機 hex 字串 */
static String randomHex(size_t n) {
  static const char* hex = "0123456789abcdef";
  String s;
  for (size_t i = 0; i < n; i++) s += hex[random(16)];
  return s;
}

static inline void settingsLoad() {
  gPrefs.begin("kkbox-rx", false);
  gSet.user  = gPrefs.getString("user", DEF_USER);
  gSet.pass  = gPrefs.getString("pass", DEF_PASS);
  gSet.ssid  = gPrefs.getString("ssid", "");
  gSet.wpass = gPrefs.getString("wpass", "");
  gSet.dhcp  = gPrefs.getBool("dhcp", true);
  gSet.ip    = gPrefs.getString("ip", "192.168.1.50");
  gSet.gw    = gPrefs.getString("gw", "192.168.1.1");
  gSet.mask  = gPrefs.getString("mask", "255.255.255.0");
  gSet.deviceToken = gPrefs.getString("dtok", "");
  if (gSet.deviceToken.length() == 0) {
    gSet.deviceToken = randomHex(16);
    gPrefs.putString("dtok", gSet.deviceToken);
  }
  gSet.droneId = gPrefs.getUChar("droneid", 1);
}

static inline void settingsSaveWifi() {
  gPrefs.putString("ssid", gSet.ssid);
  gPrefs.putString("wpass", gSet.wpass);
}
static inline void settingsSaveNet() {
  gPrefs.putBool("dhcp", gSet.dhcp);
  gPrefs.putString("ip", gSet.ip);
  gPrefs.putString("gw", gSet.gw);
  gPrefs.putString("mask", gSet.mask);
}
static inline void settingsSaveUser() {
  gPrefs.putString("user", gSet.user);
  gPrefs.putString("pass", gSet.pass);
}
static inline void settingsSaveId() {
  gPrefs.putUChar("droneid", gSet.droneId);
}

#endif