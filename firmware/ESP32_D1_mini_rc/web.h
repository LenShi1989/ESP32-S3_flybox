#ifndef RC_WEB_H
#define RC_WEB_H

#include <Arduino.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include <ESPAsyncWebServer.h>
#define ELEGANTOTA_USE_ASYNC_WEBSERVER 1
#include <ElegantOTA.h>
#include "config.h"
#include "settings.h"
#include "telem.h"
#include "input.h"
#include "link.h"

/* 本檔需在 .ino 內、server / localWs 定義之後 include */

/* ================= Session (登入權杖) ================= */
struct Session { String token; uint32_t born; };
static Session  gSessions[SESSION_MAX];
static uint32_t gRebootAt = 0;

static String cookieGet(AsyncWebServerRequest* r, const char* name) {
  if (!r->hasHeader("Cookie")) return "";
  String c = r->getHeader("Cookie")->value();
  String pat = String(name) + "=";
  int i = c.indexOf(pat);
  if (i < 0) return "";
  i += pat.length();
  int j = c.indexOf(';', i);
  return (j < 0) ? c.substring(i) : c.substring(i, j);
}
static void sessionAdd(const String& t) {
  for (int i = 0; i < SESSION_MAX; i++)
    if (gSessions[i].token.length() == 0) { gSessions[i] = {t, millis()}; return; }
  gSessions[0] = {t, millis()};
}
static void sessionDrop(const String& t) {
  for (int i = 0; i < SESSION_MAX; i++) if (gSessions[i].token == t) gSessions[i].token = "";
}
static bool tokenValid(const String& t) {
  if (t.length() == 0) return false;
  for (int i = 0; i < SESSION_MAX; i++) if (gSessions[i].token == t) return true;
  return false;
}
static bool isAuthed(AsyncWebServerRequest* r) {
  if (r->hasHeader("X-Auth") && tokenValid(r->getHeader("X-Auth")->value())) return true;
  if (r->hasParam("token") && tokenValid(r->getParam("token")->value())) return true;
  return tokenValid(cookieGet(r, "sess"));
}

/* ================= JSON ================= */
static String jsonEsc(const String& s) {
  String o;
  o.reserve(s.length() + 8);
  for (unsigned i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((uint8_t)c < 0x20) o += ' ';
    else o += c;
  }
  return o;
}

static String buildFsJson() {
  String s = "{\"total\":" + String(SPIFFS.totalBytes());
  s += ",\"used\":" + String(SPIFFS.usedBytes()) + ",\"files\":[";
  File root = SPIFFS.open("/");
  File f = root.openNextFile();
  bool first = true;
  while (f) {
    if (!first) s += ',';
    first = false;
    String n = f.name();
    if (!n.startsWith("/")) n = "/" + n;
    s += "{\"name\":\"" + n + "\",\"size\":" + String((uint32_t)f.size()) + "}";
    f = root.openNextFile();
  }
  s += "]}";
  return s;
}

static String buildStatusJson() {
  String s = "{\"fw\":\"" FW_NAME "\",\"ver\":\"" FW_VERSION "\"";
  s += ",\"uptime\":" + String(millis() / 1000);
  s += ",\"heap\":" + String(ESP.getFreeHeap());

  bool wc = (WiFi.status() == WL_CONNECTED);
  s += ",\"wifi\":{\"connected\":" + String(wc ? 1 : 0);
  s += ",\"ssid\":\"" + jsonEsc(wc ? WiFi.SSID() : gSet.ssid) + "\"";
  s += ",\"ip\":\"" + (wc ? WiFi.localIP().toString() : String("-")) + "\"";
  s += ",\"rssi\":" + String(wc ? WiFi.RSSI() : 0) + "}";

  wifi_mode_t m = WiFi.getMode();
  bool ap = (m == WIFI_AP || m == WIFI_AP_STA);
  s += ",\"ap\":{\"active\":" + String(ap ? 1 : 0);
  s += ",\"ssid\":\"" + jsonEsc(apSsid()) + "\",\"ip\":\"" + (ap ? WiFi.softAPIP().toString() : String("-")) + "\"}";

  s += ",\"link\":{\"up\":" + String(linkUp() ? 1 : 0);
  s += ",\"host\":\"" + jsonEsc(gSet.rxHost) + "\",\"port\":" + String(gSet.rxPort);
  s += ",\"age\":" + String(gTelemetryMs ? (millis() - gTelemetryMs) : 999999);
  s += ",\"tx\":" + String(gTxCount) + ",\"rx\":" + String(gRxCount) + "}";

  s += ",\"id\":{\"drone\":" + String(gSet.flyId) + ",\"rc\":" + String(gSet.rcId) + "}";
  s += ",\"control\":{\"source\":" + String(gSet.ctrlSource);
  s += ",\"active\":" + String(gCtrlActive) + "}";
  s += ",\"cal\":{\"valid\":" + String(gSet.calValid ? 1 : 0);
  s += ",\"active\":" + String(gCalActive ? 1 : 0) + "}";
  s += ",\"page\":" + String(gPage) + ",\"vrc1\":" + String(gHw.vrc1Id);
  s += ",\"kkAck\":\"" + jsonEsc(gLastAck) + "\"";
  s += ",\"oled\":" + String(gOledOk ? 1 : 0) + "}";
  return s;
}

static String buildHwJson() {
  String s = "{\"raw\":{\"t\":" + String(gHw.rawT) + ",\"y\":" + String(gHw.rawY);
  s += ",\"p\":" + String(gHw.rawP) + ",\"r\":" + String(gHw.rawR);
  s += ",\"vrc1\":" + String(gHw.rawVrc1) + "}";
  s += ",\"out\":{\"thr\":" + String(gHw.thr);
  s += ",\"roll\":" + String(gHw.roll, 0) + ",\"pitch\":" + String(gHw.pitch, 0);
  s += ",\"yaw\":" + String(gHw.yaw, 0) + "}";
  s += ",\"sw\":{\"s1\":" + String(gHw.sw1 ? 1 : 0) + ",\"s2\":" + String(gHw.sw2 ? 1 : 0);
  s += ",\"s3\":" + String(gHw.sw3 ? 1 : 0) + ",\"s4\":" + String(gHw.sw4 ? 1 : 0) + "}";
  s += ",\"id\":" + String(gHw.vrc1Id) + ",\"zone\":" + String(gHw.vrc1Zone);
  s += ",\"fly\":" + String(gSet.flyId) + ",\"rc\":" + String(gSet.rcId);
  s += ",\"page\":" + String(gPage);
  s += ",\"source\":" + String(gSet.ctrlSource) + ",\"active\":" + String(gCtrlActive);
  s += ",\"cal\":{\"valid\":" + String(gSet.calValid ? 1 : 0);
  s += ",\"active\":" + String(gCalActive ? 1 : 0);
  s += ",\"tmin\":" + String(gSet.tmin) + ",\"tmax\":" + String(gSet.tmax);
  s += ",\"ymin\":" + String(gSet.ymin) + ",\"ymax\":" + String(gSet.ymax);
  s += ",\"pmin\":" + String(gSet.pmin) + ",\"pmax\":" + String(gSet.pmax);
  s += ",\"rmin\":" + String(gSet.rmin) + ",\"rmax\":" + String(gSet.rmax) + "}}";
  return s;
}

/* ================= 輔助 ================= */
static void sendJson(AsyncWebServerRequest* r, const String& body, int code = 200) {
  r->send(code, "application/json", body);
}
static void sendOk(AsyncWebServerRequest* r) { sendJson(r, "{\"ok\":true}"); }
static void sendErr(AsyncWebServerRequest* r, const char* msg, int code = 400) {
  sendJson(r, String("{\"err\":\"") + msg + "\"}", code);
}
static void requireAuth(AsyncWebServerRequest* r) {
  if (!isAuthed(r)) sendErr(r, "unauthorized", 401);
}
static String arg(AsyncWebServerRequest* r, const char* name, const char* def = "") {
  if (r->hasParam(name, true)) return r->getParam(name, true)->value();
  if (r->hasParam(name))       return r->getParam(name)->value();
  return String(def);
}

/* ================= WebSocket (本機 UI) ================= */
static void onLocalWs(AsyncWebSocket* s, AsyncWebSocketClient* c,
                      AwsEventType type, void* arg, uint8_t* data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    c->printf("{\"hello\":\"rc\",\"ver\":\"" FW_VERSION "\"}");
    if (gTelemetryMs) c->text(gTelemetry);
  } else if (type == WS_EVT_DATA) {
    AwsFrameInfo* info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
      String msg = String((char*)data).substring(0, len);
      if (msg.startsWith("{")) {
        if (!inputHandleCmd(msg)) { c->text("{\"err\":\"hardware mode\"}"); return; }
        if (linkUp()) linkSend(msg);  /* 轉送接收機 -> 飛控板 */
        else c->text("{\"err\":\"link down\"}");
      }
    }
  }
}

/* ================= 路由 ================= */
static void webSetup() {
  localWs.onEvent(onLocalWs);
  localWs.handleHandshake([](AsyncWebServerRequest* r) -> bool {
    if (r->hasParam("token") && tokenValid(r->getParam("token")->value())) return true;
    return tokenValid(cookieGet(r, "sess"));
  });
  server.addHandler(&localWs);

  server.on("/api/login", HTTP_POST, [](AsyncWebServerRequest* r) {
    if (arg(r, "user") == gSet.user && arg(r, "pass") == gSet.pass) {
      String t = randomHex(24);
      sessionAdd(t);
      AsyncWebServerResponse* resp = r->beginResponse(200, "application/json",
        "{\"ok\":true,\"token\":\"" + t + "\"}");
      resp->addHeader("Set-Cookie", "sess=" + t + "; Path=/; HttpOnly");
      r->send(resp);
    } else sendErr(r, "bad credentials", 401);
  });
  server.on("/api/logout", HTTP_POST, [](AsyncWebServerRequest* r) {
    sessionDrop(cookieGet(r, "sess"));
    AsyncWebServerResponse* resp = r->beginResponse(200, "application/json", "{\"ok\":true}");
    resp->addHeader("Set-Cookie", "sess=; Path=/; Max-Age=0");
    r->send(resp);
  });

  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* r) {
    requireAuth(r); if (r->getResponse()) return;
    sendJson(r, buildStatusJson());
  });
  server.on("/api/telemetry", HTTP_GET, [](AsyncWebServerRequest* r) {
    requireAuth(r); if (r->getResponse()) return;
    sendJson(r, gTelemetry);
  });
  server.on("/api/command", HTTP_POST, [](AsyncWebServerRequest* r) {
    requireAuth(r); if (r->getResponse()) return;
    String cmd = arg(r, "cmd");
    if (!cmd.length() || cmd[0] != '{') { sendErr(r, "bad command"); return; }
    if (!inputHandleCmd(cmd)) { sendErr(r, "hardware mode", 409); return; }
    if (!linkUp()) { sendErr(r, "link down", 503); return; }
    linkSend(cmd); sendOk(r);
  });

  server.on("/api/fs", HTTP_GET, [](AsyncWebServerRequest* r) {
    requireAuth(r); if (r->getResponse()) return;
    sendJson(r, buildFsJson());
  });

  /* ---- 硬體輸入即時值 / 校正 ---- */
  server.on("/api/hardware", HTTP_GET, [](AsyncWebServerRequest* r) {
    requireAuth(r); if (r->getResponse()) return;
    sendJson(r, buildHwJson());
  });
  server.on("/api/cal", HTTP_POST, [](AsyncWebServerRequest* r) {
    requireAuth(r); if (r->getResponse()) return;
    String a = arg(r, "action");
    if (a == "start") { inputCalStart(); sendOk(r); }
    else if (a == "stop") {
      bool ok = inputCalFinish();
      sendJson(r, String("{\"ok\":true,\"valid\":") + (ok ? "1" : "0") + "}");
    } else sendErr(r, "bad action");
  });

  /* ---- 無人機 / 遙控器 ID ---- */
  server.on("/api/id", HTTP_GET, [](AsyncWebServerRequest* r) {
    requireAuth(r); if (r->getResponse()) return;
    sendJson(r, "{\"drone\":" + String(gSet.flyId) + ",\"rc\":" + String(gSet.rcId) + "}");
  });
  server.on("/api/id", HTTP_POST, [](AsyncWebServerRequest* r) {
    requireAuth(r); if (r->getResponse()) return;
    gSet.flyId = (uint8_t)constrain(arg(r, "drone", String(gSet.flyId).c_str()).toInt(), 0, 255);
    gSet.rcId  = (uint8_t)constrain(arg(r, "rc", String(gSet.rcId).c_str()).toInt(), 0, 255);
    settingsSaveIds();
    sendOk(r);
  });

  /* ---- 控制來源 ---- */
  server.on("/api/control", HTTP_POST, [](AsyncWebServerRequest* r) {
    requireAuth(r); if (r->getResponse()) return;
    int src = arg(r, "source", "0").toInt();
    if (src < CTRL_AUTO || src > CTRL_SOFTWARE) { sendErr(r, "bad source"); return; }
    gSet.ctrlSource = (uint8_t)src;
    settingsSaveControl();
    sendOk(r);
  });

  /* ---- 完整設定檔 (config.json) ---- */
  server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest* r) {
    requireAuth(r); if (r->getResponse()) return;
    sendJson(r, settingsRawJson());
  });

  server.on("/api/scan", HTTP_GET, [](AsyncWebServerRequest* r) {
    requireAuth(r); if (r->getResponse()) return;
    int n = WiFi.scanNetworks();
    String s = "[";
    for (int i = 0; i < n; i++) {
      if (i) s += ',';
      s += "{\"ssid\":\"" + jsonEsc(WiFi.SSID(i)) + "\",\"rssi\":" + String(WiFi.RSSI(i)) +
           ",\"enc\":" + String(WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? 1 : 0) + "}";
    }
    s += "]";
    WiFi.scanDelete();
    sendJson(r, s);
  });
  server.on("/api/wifi", HTTP_POST, [](AsyncWebServerRequest* r) {
    requireAuth(r); if (r->getResponse()) return;
    if (arg(r, "action") == "clear") {
      gSet.ssid = ""; gSet.wpass = "";
      settingsSaveWifi();
      WiFi.disconnect(true);
      sendJson(r, "{\"ok\":true}");
      gRebootAt = millis() + 5000;
      return;
    }
    gSet.ssid  = arg(r, "ssid");
    gSet.wpass = arg(r, "pass");
    WiFi.mode(WIFI_AP_STA);
    WiFi.disconnect();
    delay(20);
    WiFi.begin(gSet.ssid.c_str(), gSet.wpass.c_str());
    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) delay(100);
    if (WiFi.status() == WL_CONNECTED) {
      settingsSaveWifi();
      String ip = WiFi.localIP().toString();
      Serial.printf("[WiFi] 已連線 %s -> %s, 5 秒後重啟\n", gSet.ssid.c_str(), ip.c_str());
      sendJson(r, "{\"ok\":true,\"ip\":\"" + ip + "\"}");
      gRebootAt = millis() + 5000;
    } else {
      sendErr(r, "WiFi 連線失敗, 請確認 SSID / 密碼");
    }
  });

  /* 接收機連線設定 */
  server.on("/api/link", HTTP_GET, [](AsyncWebServerRequest* r) {
    requireAuth(r); if (r->getResponse()) return;
    String s = "{\"host\":\"" + jsonEsc(gSet.rxHost) + "\",\"port\":" + String(gSet.rxPort) +
               ",\"token\":\"" + jsonEsc(gSet.rxToken) + "\"}";
    sendJson(r, s);
  });
  server.on("/api/link", HTTP_POST, [](AsyncWebServerRequest* r) {
    requireAuth(r); if (r->getResponse()) return;
    gSet.rxHost  = arg(r, "host", gSet.rxHost.c_str());
    gSet.rxPort  = (uint16_t)arg(r, "port", "80").toInt();
    gSet.rxToken = arg(r, "token", gSet.rxToken.c_str());
    if (gSet.rxPort == 0) gSet.rxPort = DEF_RX_PORT;
    settingsSaveLink();
    linkWs.disconnect();
    if (gSet.rxHost.length()) linkBegin();
    sendOk(r);
  });

  /* ---- PID (值存於本機 config.json, 下傳經 WiFi) ---- */
  server.on("/api/pid", HTTP_GET, [](AsyncWebServerRequest* r) {
    requireAuth(r); if (r->getResponse()) return;
    char b[320];
    snprintf(b, sizeof b,
      "{\"roll\":{\"kp\":%.3f,\"ki\":%.3f,\"kd\":%.3f},"
      "\"pitch\":{\"kp\":%.3f,\"ki\":%.3f,\"kd\":%.3f},"
      "\"yaw\":{\"kp\":%.3f,\"ki\":%.3f,\"kd\":%.3f}}",
      gSet.rollKp, gSet.rollKi, gSet.rollKd,
      gSet.pitchKp, gSet.pitchKi, gSet.pitchKd,
      gSet.yawKp, gSet.yawKi, gSet.yawKd);
    sendJson(r, b);
  });
  server.on("/api/pid", HTTP_POST, [](AsyncWebServerRequest* r) {
    requireAuth(r); if (r->getResponse()) return;
    String a = arg(r, "axis", "roll");
    float kp = arg(r, "kp", "0").toFloat();
    float ki = arg(r, "ki", "0").toFloat();
    float kd = arg(r, "kd", "0").toFloat();
    if (a == "roll")  { gSet.rollKp = kp; gSet.rollKi = ki; gSet.rollKd = kd; }
    if (a == "pitch") { gSet.pitchKp = kp; gSet.pitchKi = ki; gSet.pitchKd = kd; }
    if (a == "yaw")   { gSet.yawKp = kp; gSet.yawKi = ki; gSet.yawKd = kd; }
    settingsSavePid();
    char b[96];
    snprintf(b, sizeof b, "{\"cmd\":\"pid\",\"axis\":\"%s\",\"kp\":%.3f,\"ki\":%.3f,\"kd\":%.3f}",
             a.c_str(), kp, ki, kd);
    bool sent = linkSend(b);
    sendJson(r, String("{\"ok\":true,\"sent\":") + (sent ? "1" : "0") + "}");
  });

  /* 使用者設定 */
  server.on("/api/user", HTTP_GET, [](AsyncWebServerRequest* r) {
    requireAuth(r); if (r->getResponse()) return;
    sendJson(r, "{\"user\":\"" + jsonEsc(gSet.user) + "\"}");
  });
  server.on("/api/user", HTTP_POST, [](AsyncWebServerRequest* r) {
    requireAuth(r); if (r->getResponse()) return;
    if (arg(r, "cur") != gSet.pass) { sendErr(r, "wrong password", 401); return; }
    String u = arg(r, "user"); if (u.length()) gSet.user = u;
    String p = arg(r, "pass"); if (p.length()) gSet.pass = p;
    settingsSaveUser();
    sendOk(r);
  });

  ElegantOTA.begin(&server);

  /* no-store: 避免瀏覽器快取舊版網頁 */
  server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html").setCacheControl("no-store");

  server.onNotFound([](AsyncWebServerRequest* r) {
    if (r->method() == HTTP_OPTIONS) { r->send(204); return; }
    sendErr(r, "not found", 404);
  });

  server.begin();
}

#endif