#ifndef FLYBOX_SETTINGS_H
#define FLYBOX_SETTINGS_H

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <esp_mac.h>
#include "config.h"

/* SoftAP SSID = 前綴 + MAC 後 4 碼 (例: Flybox-A1B2) */
static inline String apSsid() {
  uint8_t mac[6] = {0};
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

  /* 裝置連結 Token (供其他裝置連線 WebSocket 用) */
  String deviceToken;

  /* 本機無人機 ID (0~255), 顯示於系統狀態 */
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
  gPrefs.begin("flybox", false);
  gSet.user  = gPrefs.getString("user", DEF_USER);
  gSet.pass  = gPrefs.getString("pass", DEF_PASS);
  gSet.ssid  = gPrefs.getString("ssid", "");
  gSet.wpass = gPrefs.getString("wpass", "");
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

static inline void settingsSaveUser() {
  gPrefs.putString("user", gSet.user);
  gPrefs.putString("pass", gSet.pass);
}

static inline void settingsSaveId() {
  gPrefs.putUChar("droneid", gSet.droneId);
}

#endif