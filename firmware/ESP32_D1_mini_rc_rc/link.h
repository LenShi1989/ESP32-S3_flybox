#ifndef RC_LINK_H
#define RC_LINK_H

#include <Arduino.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include "settings.h"
#include "telem.h"

/* 本檔需在 .ino 內、localWs (AsyncWebSocket) 定義之後 include */

/* 純 2.4G WiFi: 以 WebSocket 用戶端連線 WT32-ETH01 接收機 */
static WebSocketsClient linkWs;

/* 飛控板最後一筆回應 (ack): 供 /api/status 與網頁顯示 */
static String   gLastAck   = "";
static uint32_t gLastAckMs = 0;

static uint32_t gTxCount   = 0;
static uint32_t gRxCount   = 0;
static String   gLastTx    = "";

static void linkRelay(const String& msg) {
  localWs.textAll(msg);
}

static inline bool linkUp() {
  return gLinkUp;
}

static void linkParse(const String& msg) {
  JsonDocument doc;
  if (deserializeJson(doc, msg)) return;
  if (!doc["gyro"].is<JsonObject>()) return;

  JsonObject g = doc["gyro"];
  JsonObject s = doc["set"];
  gTelem.roll  = g["roll"]  | 0.0f;
  gTelem.pitch = g["pitch"] | 0.0f;
  gTelem.yaw   = g["yaw"]   | 0.0f;
  gTelem.thr   = s["thr"]   | (long)1000;
  gTelem.armed = (doc["armed"] | 0) != 0;

  JsonArray m = doc["motor"];
  int i = 0;
  for (JsonVariant v : m) { if (i < 4) gTelem.motor[i++] = v.as<int>(); }

  gTelemetry   = msg;
  gTelemetryMs = millis();
  gTelem.ms    = gTelemetryMs;
  gTelem.valid = true;
}

static void onLinkEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      gLinkUp = true;
      Serial.println("[LINK] 已連線接收機");
      break;
    case WStype_DISCONNECTED:
      gLinkUp = false;
      Serial.println("[LINK] 已斷線");
      break;
    case WStype_TEXT: {
      String msg((char*)payload);
      if (length) msg = msg.substring(0, length);
      gRxCount++;
      if (msg.indexOf("\"ack\"") >= 0) { gLastAck = msg; gLastAckMs = millis(); }
      linkRelay(msg);
      if (msg.indexOf("\"gyro\"") >= 0) linkParse(msg);
      break;
    }
    default:
      break;
  }
}

static inline void linkBegin() {
  String url = "/ws";
  if (gSet.rxToken.length()) url += "?token=" + gSet.rxToken;
  linkWs.begin(gSet.rxHost.c_str(), gSet.rxPort, url.c_str());
  linkWs.onEvent(onLinkEvent);
  linkWs.setReconnectInterval(3000);
  Serial.printf("[LINK] 目標 %s:%u%s\n", gSet.rxHost.c_str(), gSet.rxPort,
                gSet.rxToken.length() ? " (token)" : "");
}

static inline void linkLoop() {
  linkWs.loop();
}

/* 傳送控制命令 (單行 JSON) 給接收機 -> 飛控板 */
static inline bool linkSend(String json) {
  if (!gLinkUp) return false;
  linkWs.sendTXT(json);
  gTxCount++;
  gLastTx = json;
  return true;
}

#endif