#ifndef FLYBOX_WEB_H
#define FLYBOX_WEB_H

#include <Arduino.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include "config.h"
#include "settings.h"

/* ================= 全域伺服器物件 ================= */
static AsyncWebServer  server(WEB_PORT);
static AsyncWebSocket  ws("/ws");

/* ================= Session (登入權杖) ================= */
struct Session { String token; uint32_t born; };
static Session gSessions[SESSION_MAX];

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
  for (int i = 0; i < SESSION_MAX; i++) {
    if (gSessions[i].token.length() == 0) { gSessions[i] = {t, millis()}; return; }
  }
  gSessions[0] = {t, millis()};
}

static void sessionDrop(const String& t) {
  for (int i = 0; i < SESSION_MAX; i++)
    if (gSessions[i].token == t) gSessions[i].token = "";
}

static bool tokenValid(const String& t) {
  if (t.length() == 0) return false;
  if (t == gSet.deviceToken) return true;
  for (int i = 0; i < SESSION_MAX; i++)
    if (gSessions[i].token == t) return true;
  return false;
}

static bool isAuthed(AsyncWebServerRequest* r) {
  if (r->hasHeader("X-Auth") && tokenValid(r->getHeader("X-Auth")->value())) return true;
  if (r->hasParam("token") && tokenValid(r->getParam("token")->value())) return true;
  return tokenValid(cookieGet(r, "sess"));
}

/* ================= JSON 組裝 ================= */
static String jsonEscape(const String& s) {
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

  /* WiFi */
  bool wc = (WiFi.status() == WL_CONNECTED);
  s += ",\"wifi\":{\"connected\":" + String(wc ? 1 : 0);
  s += ",\"ssid\":\"" + jsonEscape(wc ? WiFi.SSID() : gSet.ssid) + "\"";
  s += ",\"ip\":\"" + (wc ? WiFi.localIP().toString() : String("-")) + "\"";
  s += ",\"rssi\":" + String(wc ? WiFi.RSSI() : 0) + "}";

  /* SoftAP */
  wifi_mode_t m = WiFi.getMode();
  bool ap = (m == WIFI_AP || m == WIFI_AP_STA);
  s += ",\"ap\":{\"active\":" + String(ap ? 1 : 0);
  s += ",\"ssid\":\"" + jsonEscape(apSsid()) + "\",\"ip\":\"" + (ap ? WiFi.softAPIP().toString() : String("-")) + "\"}";

  s += ",\"droneId\":" + String(gSet.droneId);
  s += ",\"token\":\"" + gSet.deviceToken + "\"}";
  return s;
}

/* ================= 回應輔助 ================= */
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

/* ================= WebSocket 事件 ================= */
static void onWsEvent(AsyncWebSocket* s, AsyncWebSocketClient* c,
                      AwsEventType type, void* arg, uint8_t* data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    c->printf("{\"hello\":\"flybox\",\"ver\":\"" FW_VERSION "\"}");
  } else if (type == WS_EVT_DATA) {
    AwsFrameInfo* info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
      String msg = String((char*)data).substring(0, len);
      if (msg.startsWith("{")) {
        /* 這裡可以處理來自網頁的控制命令，透過 RS232 轉送給飛控板 */
        linkSend(msg);
      }
    }
  }
}

/* ================= OTA 上傳 (firmware.bin / spiffs.bin) ================= */
static int    gOtaKind  = 0;
static bool   gOtaAuth  = false;
static bool   gOtaOk    = false;
static bool   gOtaFail  = false;
static size_t gOtaBytes = 0;
static String gOtaErr;

static const esp_partition_t* otaSpiffsPart() {
  return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, NULL);
}

static void otaFail(const char* why) {
  if (!gOtaFail) Serial.printf("[OTA] 失敗: %s (已寫入 %u bytes)\n", why, (unsigned)gOtaBytes);
  gOtaFail = true;
  gOtaErr  = why;
  if (Update.isRunning()) Update.abort();
  if (gOtaKind == 2) SPIFFS.begin(false);
}

static void otaChunk(AsyncWebServerRequest* r, int kind, String filename,
                     size_t index, uint8_t* data, size_t len, bool final) {
  if (!index) {
    gOtaKind = kind; gOtaOk = false; gOtaFail = false; gOtaBytes = 0; gOtaErr = "";
    gOtaAuth = isAuthed(r);
    if (!gOtaAuth) { gOtaErr = "unauthorized"; return; }

    const esp_partition_t* part = (kind == 1)
      ? esp_ota_get_next_update_partition(NULL) : otaSpiffsPart();
    if (!part) { otaFail(kind == 1 ? "找不到 OTA 分割區" : "找不到 spiffs 分割區"); return; }
    size_t clen = r->contentLength();
    if (clen && clen > part->size + 1024) {
      Serial.printf("[OTA] 拒絕上傳: %u bytes 超過分割區 %u bytes\n",
                    (unsigned)clen, (unsigned)part->size);
      otaFail("映像大於分割區, 請用 tools/spiffs.bat 重新產生");
      return;
    }

    Serial.printf("[OTA] %s upload start: %s (分割區 %u bytes)\n",
                  kind == 1 ? "firmware" : "spiffs", filename.c_str(),
                  (unsigned)part->size);
    if (kind == 2) SPIFFS.end();
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, kind == 1 ? U_FLASH : U_SPIFFS)) {
      String e = Update.errorString();
      Serial.printf("[OTA] begin 失敗: %s\n", e.c_str());
      otaFail(e.c_str());
      return;
    }
  }
  if (!gOtaAuth || gOtaFail || gOtaKind != kind || !Update.isRunning()) return;
  if (len && Update.write(data, len) != len) {
    String e = Update.errorString();
    Serial.printf("[OTA] 寫入失敗 (offset %u): %s\n", (unsigned)gOtaBytes, e.c_str());
    otaFail(e.c_str());
    return;
  }
  gOtaBytes += len;
  if (final) {
    if (Update.end(true)) {
      gOtaOk = true;
      Serial.printf("[OTA] %s ok: %u bytes\n",
                    kind == 1 ? "firmware" : "spiffs", (unsigned)gOtaBytes);
      if (kind == 2) {
        if (SPIFFS.begin(false)) {
          Serial.println("[OTA] 新 spiffs 映像已掛載");
        } else {
          gOtaOk = false; gOtaErr = "新 spiffs 映像無法掛載";
          Serial.println("[OTA] 新 spiffs 映像掛載失敗");
        }
      }
    } else {
      String e = Update.errorString();
      Serial.printf("[OTA] end 失敗: %s\n", e.c_str());
      otaFail(e.c_str());
    }
  }
}

static void otaDone(AsyncWebServerRequest* r, int kind) {
  if (!gOtaAuth) { sendErr(r, "unauthorized", 401); return; }
  if (!gOtaOk || gOtaKind != kind) {
    String body = "{\"ok\":false,\"err\":\"ota failed\"";
    if (gOtaErr.length()) body += ",\"reason\":\"" + jsonEscape(gOtaErr) + "\"";
    body += "}";
    sendJson(r, body, 500);
    return;
  }
  sendJson(r, String("{\"ok\":true,\"bytes\":") + (unsigned)gOtaBytes +
              ",\"md5\":\"" + Update.md5String() + "\"}");
  gRebootAt = millis() + 1500;
}

/* ================= 設定精靈 / 路由 ================= */
static void webSetup() {
  if (!SPIFFS.begin(false)) {
    Serial.println("[SPIFFS] 掛載失敗");
    return;
  }
  Serial.printf("[SPIFFS] 總計 %u bytes, 已用 %u bytes\n",
                (unsigned)SPIFFS.totalBytes(), (unsigned)SPIFFS.usedBytes());

  if (MDNS.begin(MDNS_HOST)) {
    Serial.printf("[mDNS] 啟動: http://%s.local/\n", MDNS_HOST);
  }

  ws.onEvent(onWsEvent);
  ws.handleHandshake([](AsyncWebServerRequest* r) -> bool {
    if (r->hasParam("token") && tokenValid(r->getParam("token")->value())) return true;
    return tokenValid(cookieGet(r, "sess"));
  });
  server.addHandler(&ws);

  /* ---- 登入 / 登出 ---- */
  server.on("/api/login", HTTP_POST, [](AsyncWebServerRequest* r) {
    if (arg(r, "user") == gSet.user && arg(r, "pass") == gSet.pass) {
      String t = randomHex(24);
      sessionAdd(t);
      AsyncWebServerResponse* resp = r->beginResponse(200, "application/json",
        "{\"ok\":true,\"token\":\"" + t + "\"}");
      resp->addHeader("Set-Cookie", "sess=" + t + "; Path=/; HttpOnly");
      r->send(resp);
    } else {
      sendErr(r, "bad credentials", 401);
    }
  });
  server.on("/api/logout", HTTP_POST, [](AsyncWebServerRequest* r) {
    sessionDrop(cookieGet(r, "sess"));
    AsyncWebServerResponse* resp = r->beginResponse(200, "application/json", "{\"ok\":true}");
    resp->addHeader("Set-Cookie", "sess=; Path=/; Max-Age=0");
    r->send(resp);
  });

  /* ---- 狀態 / 檔案系統 ---- */
  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* r) {
    requireAuth(r); if (r->getResponse()) return;
    sendJson(r, buildStatusJson());
  });
  server.on("/api/fs", HTTP_GET, [](AsyncWebServerRequest* r) {
    requireAuth(r); if (r->getResponse()) return;
    sendJson(r, buildFsJson());
  });

  /* ---- WiFi 掃描 / 設定 ---- */
  server.on("/api/scan", HTTP_GET, [](AsyncWebServerRequest* r) {
    requireAuth(r); if (r->getResponse()) return;
    int n = WiFi.scanNetworks();
    String s = "[";
    for (int i = 0; i < n; i++) {
      if (i) s += ',';
      s += "{\"ssid\":\"" + jsonEscape(WiFi.SSID(i)) + "\",\"rssi\":" + String(WiFi.RSSI(i)) +
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
      settingsSaveWifi(); WiFi.disconnect(true);
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

  /* ---- 使用者設定 ---- */
  server.on("/api/user", HTTP_GET, [](AsyncWebServerRequest* r) {
    requireAuth(r); if (r->getResponse()) return;
    sendJson(r, "{\"user\":\"" + gSet.user + "\",\"token\":\"" + gSet.deviceToken + "\"}");
  });
  server.on("/api/user", HTTP_POST, [](AsyncWebServerRequest* r) {
    requireAuth(r); if (r->getResponse()) return;
    String nu = arg(r, "user", gSet.user.c_str());
    String np = arg(r, "pass");
    if (arg(r, "cur") == gSet.pass) {
      if (arg(r, "newtoken") == "1") gSet.deviceToken = randomHex(16), gPrefs.putString("dtok", gSet.deviceToken);
      if (arg(r, "newuser").length()) gSet.user = arg(r, "user");
      if (arg(r, "newpass").length()) gSet.pass = arg(r, "pass");
      settingsSaveUser();
      sendOk(r);
    } else {
      sendErr(r, "wrong password", 401);
    }
  });

  /* ---- 無人機 ID ---- */
  server.on("/api/id", HTTP_GET, [](AsyncWebServerRequest* r) {
    requireAuth(r); if (r->getResponse()) return;
    sendJson(r, "{\"id\":" + String(gSet.droneId) + "}");
  });
  server.on("/api/id", HTTP_POST, [](AsyncWebServerRequest* r) {
    requireAuth(r); if (r->getResponse()) return;
    gSet.droneId = (uint8_t)constrain(arg(r, "id", "1").toInt(), 0, 255);
    settingsSaveId();
    sendOk(r);
  });

  /* ---- RS232 命令轉送 ---- */
  server.on("/api/command", HTTP_POST, [](AsyncWebServerRequest* r) {
    requireAuth(r); if (r->getResponse()) return;
    String cmd = arg(r, "cmd");
    if (cmd.length() && cmd[0] == '{') { linkSend(cmd); sendOk(r); }
    else sendErr(r, "bad command");
  });

  /* ---- OTA: 韌體 (firmware.bin) ---- */
  server.on("/api/ota/firmware", HTTP_POST,
    [](AsyncWebServerRequest* r) { otaDone(r, 1); },
    [](AsyncWebServerRequest* r, String name, size_t index, uint8_t* data,
       size_t len, bool final) { otaChunk(r, 1, name, index, data, len, final); });

  /* ---- OTA: SPIFFS 網頁檔映像 (spiffs.bin) ---- */
  server.on("/api/ota/spiffs", HTTP_POST,
    [](AsyncWebServerRequest* r) { otaDone(r, 2); },
    [](AsyncWebServerRequest* r, String name, size_t index, uint8_t* data,
       size_t len, bool final) { otaChunk(r, 2, name, index, data, len, final); });

  /* ---- 靜態檔案 (SPIFFS) ---- */
  server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html").setCacheControl("no-store");

  server.onNotFound([](AsyncWebServerRequest* r) {
    if (r->method() == HTTP_OPTIONS) { r->send(204); return; }
    sendErr(r, "not found", 404);
  });

  server.begin();
  Serial.printf("[Web] 伺服器啟動, port %d\n", WEB_PORT);
}

static void webLoop() {
  ws.cleanupClients();
  if (gRebootAt && millis() >= gRebootAt) {
    Serial.println("[SYS] 重新啟動...");
    ESP.restart();
  }
}

#endif