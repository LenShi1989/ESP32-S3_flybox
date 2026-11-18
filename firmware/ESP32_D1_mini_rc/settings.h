#ifndef RC_SETTINGS_H
#define RC_SETTINGS_H

#include <Arduino.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <esp_mac.h>
#include "config.h"

/* =============================================================
 * 所有設定參數統一儲存在 SPIFFS 的 /config.json
 * （不再使用 NVS / Preferences）
 * ============================================================= */

#define CONFIG_PATH "/config.json"

/* SoftAP SSID = 前綴 + MAC 後 4 碼 (例: KKBOX-RC-A1B2) */
static inline String apSsid() {
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char buf[40];
  snprintf(buf, sizeof(buf), "%s%02X%02X", AP_SSID_PREFIX, mac[4], mac[5]);
  return String(buf);
}

struct Settings {
  /* 登入帳密 */
  String user;
  String pass;

  /* WiFi 站台 (2.4GHz) */
  String ssid;
  String wpass;

  /* 接收機 (WT32-ETH01) 連線 */
  String   rxHost;
  uint16_t rxPort;
  String   rxToken;

  /* 無人機 / 遙控器編號 (0~255) */
  uint8_t flyId;
  uint8_t rcId;

  /* 控制來源: CTRL_AUTO / CTRL_HARDWARE / CTRL_SOFTWARE */
  uint8_t ctrlSource;

  /* 搖桿校正 (ADC 原始值) */
  bool calValid;
  int  tmin, tmax;
  int  ymin, ymax;
  int  pmin, pmax;
  int  rmin, rmax;

  /* PID (本機保存; 下傳飛控板需經 WiFi 連線) */
  float rollKp, rollKi, rollKd;
  float pitchKp, pitchKi, pitchKd;
  float yawKp, yawKi, yawKd;
};

static Settings gSet;

static String randomHex(size_t n) {
  static const char* hex = "0123456789abcdef";
  String s;
  for (size_t i = 0; i < n; i++) s += hex[random(16)];
  return s;
}

static inline void settingsDefaults() {
  gSet.user = DEF_USER;
  gSet.pass = DEF_PASS;
  gSet.ssid = "";
  gSet.wpass = "";
  gSet.rxHost = "";
  gSet.rxPort = DEF_RX_PORT;
  gSet.rxToken = "";
  gSet.flyId = 1;
  gSet.rcId = 3;
  gSet.ctrlSource = CTRL_AUTO;
  gSet.calValid = false;
  gSet.tmin = gSet.ymin = gSet.pmin = gSet.rmin = DEF_ADC_MIN;
  gSet.tmax = gSet.ymax = gSet.pmax = gSet.rmax = DEF_ADC_MAX;
  gSet.rollKp = DEF_ROLL_KP;   gSet.rollKi = DEF_ROLL_KI;   gSet.rollKd = DEF_ROLL_KD;
  gSet.pitchKp = DEF_PITCH_KP; gSet.pitchKi = DEF_PITCH_KI; gSet.pitchKd = DEF_PITCH_KD;
  gSet.yawKp = DEF_YAW_KP;     gSet.yawKi = DEF_YAW_KI;     gSet.yawKd = DEF_YAW_KD;
}

/* 將所有設定寫入 /config.json */
static inline bool settingsSave() {
  JsonDocument doc;
  doc["user"] = gSet.user;
  doc["pass"] = gSet.pass;

  JsonObject wifi = doc["wifi"].to<JsonObject>();
  wifi["ssid"] = gSet.ssid;
  wifi["pass"] = gSet.wpass;

  JsonObject rx = doc["rx"].to<JsonObject>();
  rx["host"]  = gSet.rxHost;
  rx["port"]  = gSet.rxPort;
  rx["token"] = gSet.rxToken;

  JsonObject id = doc["id"].to<JsonObject>();
  id["drone"] = gSet.flyId;
  id["rc"]    = gSet.rcId;

  doc["control"]["source"] = gSet.ctrlSource;

  JsonObject cal = doc["cal"].to<JsonObject>();
  cal["valid"] = gSet.calValid;
  cal["tmin"] = gSet.tmin; cal["tmax"] = gSet.tmax;
  cal["ymin"] = gSet.ymin; cal["ymax"] = gSet.ymax;
  cal["pmin"] = gSet.pmin; cal["pmax"] = gSet.pmax;
  cal["rmin"] = gSet.rmin; cal["rmax"] = gSet.rmax;

  JsonObject pid = doc["pid"].to<JsonObject>();
  pid["roll"]["kp"]  = gSet.rollKp;  pid["roll"]["ki"]  = gSet.rollKi;  pid["roll"]["kd"]  = gSet.rollKd;
  pid["pitch"]["kp"] = gSet.pitchKp; pid["pitch"]["ki"] = gSet.pitchKi; pid["pitch"]["kd"] = gSet.pitchKd;
  pid["yaw"]["kp"]   = gSet.yawKp;   pid["yaw"]["ki"]   = gSet.yawKi;   pid["yaw"]["kd"]   = gSet.yawKd;

  File f = SPIFFS.open(CONFIG_PATH, "w");
  if (!f) { Serial.println("[CFG] 無法寫入 " CONFIG_PATH); return false; }
  serializeJsonPretty(doc, f);
  f.close();
  return true;
}

/* 從 /config.json 讀取設定；檔案不存在時建立預設檔 */
static inline void settingsLoad() {
  settingsDefaults();

  if (!SPIFFS.exists(CONFIG_PATH)) {
    Serial.println("[CFG] 找不到設定檔, 建立預設 config.json");
    settingsSave();
    return;
  }

  File f = SPIFFS.open(CONFIG_PATH, "r");
  if (!f) { Serial.println("[CFG] 讀取失敗, 使用預設值"); return; }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) {
    Serial.printf("[CFG] JSON 解析失敗: %s (使用預設值)\n", err.c_str());
    return;
  }

  gSet.user    = doc["user"] | DEF_USER;
  gSet.pass    = doc["pass"] | DEF_PASS;
  gSet.ssid    = doc["wifi"]["ssid"] | "";
  gSet.wpass   = doc["wifi"]["pass"] | "";
  gSet.rxHost  = doc["rx"]["host"]  | "";
  gSet.rxPort  = doc["rx"]["port"]  | (uint16_t)DEF_RX_PORT;
  gSet.rxToken = doc["rx"]["token"] | "";
  gSet.flyId   = doc["id"]["drone"] | (uint8_t)1;
  gSet.rcId    = doc["id"]["rc"]    | (uint8_t)3;
  gSet.ctrlSource = doc["control"]["source"] | (uint8_t)CTRL_AUTO;
  gSet.calValid = doc["cal"]["valid"] | false;
  gSet.tmin = doc["cal"]["tmin"] | DEF_ADC_MIN;
  gSet.tmax = doc["cal"]["tmax"] | DEF_ADC_MAX;
  gSet.ymin = doc["cal"]["ymin"] | DEF_ADC_MIN;
  gSet.ymax = doc["cal"]["ymax"] | DEF_ADC_MAX;
  gSet.pmin = doc["cal"]["pmin"] | DEF_ADC_MIN;
  gSet.pmax = doc["cal"]["pmax"] | DEF_ADC_MAX;
  gSet.rmin = doc["cal"]["rmin"] | DEF_ADC_MIN;
  gSet.rmax = doc["cal"]["rmax"] | DEF_ADC_MAX;
  gSet.rollKp  = doc["pid"]["roll"]["kp"]  | DEF_ROLL_KP;
  gSet.rollKi  = doc["pid"]["roll"]["ki"]  | DEF_ROLL_KI;
  gSet.rollKd  = doc["pid"]["roll"]["kd"]  | DEF_ROLL_KD;
  gSet.pitchKp = doc["pid"]["pitch"]["kp"] | DEF_PITCH_KP;
  gSet.pitchKi = doc["pid"]["pitch"]["ki"] | DEF_PITCH_KI;
  gSet.pitchKd = doc["pid"]["pitch"]["kd"] | DEF_PITCH_KD;
  gSet.yawKp   = doc["pid"]["yaw"]["kp"]   | DEF_YAW_KP;
  gSet.yawKi   = doc["pid"]["yaw"]["ki"]   | DEF_YAW_KI;
  gSet.yawKd   = doc["pid"]["yaw"]["kd"]   | DEF_YAW_KD;

  if (gSet.ctrlSource > CTRL_SOFTWARE) gSet.ctrlSource = CTRL_AUTO;
  if (gSet.rxPort == 0) gSet.rxPort = DEF_RX_PORT;

  Serial.printf("[CFG] 已載入 %s (FlyID=%u, RCID=%u, cal=%s)\n",
                CONFIG_PATH, gSet.flyId, gSet.rcId, gSet.calValid ? "OK" : "未校正");
}

/* 以下為相容舊呼叫點的儲存函式：全部寫回 config.json */
static inline void settingsSaveWifi()     { settingsSave(); }
static inline void settingsSaveLink()     { settingsSave(); }
static inline void settingsSaveUser()     { settingsSave(); }
static inline void settingsSavePid()      { settingsSave(); }
static inline void settingsSaveIds()      { settingsSave(); }
static inline void settingsSaveCal()      { settingsSave(); }
static inline void settingsSaveControl()  { settingsSave(); }

/* 回傳 config.json 原始內容 (供網頁檢視) */
static inline String settingsRawJson() {
  File f = SPIFFS.open(CONFIG_PATH, "r");
  if (!f) return "{}";
  String s;
  s.reserve(f.size() + 1);
  while (f.available()) s += (char)f.read();
  f.close();
  return s;
}

#endif