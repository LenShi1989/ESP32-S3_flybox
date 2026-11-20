#ifndef RC_MDNS_H
#define RC_MDNS_H

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include "config.h"

/* =============================================================
 * mDNS: 區網內用 http://<MDNS_HOST>.local/ 連線, 不必記 IP
 * -------------------------------------------------------------
 * 使用 ESP32 core 內建的 ESPmDNS, 不再自行收發 5353/UDP 封包:
 *   - 啟動時會把「已經有 IP」的介面 (SoftAP / STA) 一起加入,
 *     之後取得 IP 的介面也會自動加入, AP+STA 共存不會漏掉
 *   - 開機主動公告 (announce), 不必等對方查詢, 解析更可靠
 *   - 由 esp_netif 設定多播 MAC 濾波, 比自行 setsockopt 加入群組穩定
 *   - 另外提供 _http._tcp 服務記錄, Bonjour 瀏覽器也看得到
 * 檔名用 rc_mdns.h: 若叫 mdns.h 會蓋掉 ESP-IDF 的 mdns.h,
 * 讓 ESPmDNS.h 編譯失敗。程式碼比手寫版多約 30KB; min_spiffs 的
 * APP 分割區 1.95MB、目前使用 65%, 還有約 650KB 餘裕。
 * ============================================================= */

static bool     gMdnsOk      = false;
static uint32_t gMdnsRetryAt = 0;

/* 等 SoftAP 或 STA 取得 IP 後才啟動 (mDNS 需要介面位址才能回 A 記錄);
 * 失敗每 5 秒重試一次, 避免開機時介面尚未就緒就永久失效。 */
static bool mdnsBegin() {
  if (gMdnsOk) return true;
  if (millis() < gMdnsRetryAt) return false;
  if (WiFi.softAPIP() == IPAddress(0, 0, 0, 0) &&
      WiFi.localIP()  == IPAddress(0, 0, 0, 0)) return false;

  gMdnsRetryAt = millis() + 5000;
  if (!MDNS.begin(MDNS_HOST)) {
    Serial.println("[mDNS] 啟動失敗, 5 秒後重試");
    return false;
  }
  MDNS.addService("http", "tcp", WEB_PORT);
  gMdnsOk = true;
  Serial.printf("[mDNS] http://%s.local/ (AP %s / STA %s)\n", MDNS_HOST,
                WiFi.softAPIP().toString().c_str(),
                WiFi.localIP().toString().c_str());
  return true;
}

/* loop 內呼叫: 開機時還沒有 IP 也能在介面就緒後自動啟動 */
static void mdnsLoop() {
  mdnsBegin();
}

#endif
