/*
 * ESP32 D1 mini 遙控器 (RC) — KKBOX 飛控系統
 * ---------------------------------------------------------------
 * 版本 (Version): 2.3.3   (見 config.h 的 FW_VERSION)
 * 名稱 (Name):    ESP32 D1 mini RC
 * ---------------------------------------------------------------
 * - 只用 2.4G WiFi: WebSocket 用戶端連線 WT32-ETH01 接收機
 * - 硬體遙控: 四軸搖桿 (ADC1) + SW1~SW4 + VRC1 旋鈕
 * - 軟體遙控: 網頁模擬搖桿 (WiFi)
 * - VRC1 切換 SSD1306 OLED 顯示頁面 (4 頁)
 * - SPIFFS: 所有設定儲存在 /config.json
 * - 網頁介面: 系統狀態 / 網路設定 / PID / 模擬搖桿 / 硬體遙控 /
 *             馬達校正 / OTA / 使用者設定
 * ---------------------------------------------------------------
 * 開機流程 (沿用測試碼):
 *   1) 轉 VRC1 選無人機 ID -> 按下 SW1 確認
 *   2) 按住 SW1 期間持續校正搖桿, 放開 SW1 結束
 *   3) 按住 SW2 開機 = 跳過上述流程 (沿用已存的 ID/校正值)
 *
 * 接線變更: VRC1 由 GPIO13 改接 GPIO36 (ADC1), 因為 WiFi 開啟時
 *           ADC2 (GPIO13) 無法讀取。OLED 使用 SDA=17 / SCL=16。
 * 硬體開關: SW3 = ARM, SW4 = DISARM (按一下觸發, 需 WiFi 連線)
 * ---------------------------------------------------------------
 * 網頁預設帳密 admin / admin。
 */

#include <Arduino.h>
#include "soc/rtc_cntl_reg.h"   /* BOD (電壓不足偵測) 停用 */
#include <esp_system.h>
#include <esp_attr.h>
#include <nvs.h>
#include "config.h"
#include "settings.h"
#include <ESPAsyncWebServer.h>

/* 全域伺服器物件 (需在 link.h / web.h 之前定義) */
static AsyncWebServer server(WEB_PORT);
static AsyncWebSocket localWs("/ws");

#include "telem.h"
#include "input.h"
#include "display.h"
#include "link.h"
#include "web.h"
#include "rc_mdns.h"

static uint32_t lastOled  = 0;
static uint32_t lastInput = 0;
static uint32_t lastLed   = 0;
static bool     ledState  = false;

/* =============================================================
 * 開機健康檢查: 連續異常開機時清除損毀的 PHY 校正資料
 * -------------------------------------------------------------
 * ESP32 會把射頻校正資料存在 NVS 的 "phy" 命名空間。若該資料損毀,
 * 每次啟動 WiFi 都會在 PHY 初始化 (tx_pwctrl_init) 觸發
 * "Guru Meditation ... BREAKN instr" 當機, 而且無法從網頁復原。
 * 因此連續 3 次異常開機就清掉重校正; 正常開機則把計數歸零。
 * RTC_NOINIT 記憶體斷電後是隨機值, 所以用 magic 值確認有效性。
 * ============================================================= */
#define RC_BOOT_MAGIC 0x52434247u   /* "RCBG" */
RTC_NOINIT_ATTR static uint32_t gBootMagic;
RTC_NOINIT_ATTR static uint32_t gAbnormalBoots;

static const char* resetReasonName(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:   return "POWERON";
    case ESP_RST_EXT:       return "EXT";
    case ESP_RST_SW:        return "SW";
    case ESP_RST_PANIC:     return "PANIC";
    case ESP_RST_INT_WDT:   return "INT_WDT";
    case ESP_RST_TASK_WDT:  return "TASK_WDT";
    case ESP_RST_WDT:       return "WDT";
    case ESP_RST_BROWNOUT:  return "BROWNOUT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_SDIO:      return "SDIO";
    default:                return "UNKNOWN";
  }
}

static void bootHealthCheck() {
  esp_reset_reason_t rr = esp_reset_reason();
  if (gBootMagic != RC_BOOT_MAGIC) {   /* 冷啟動: RTC 記憶體內容無效 */
    gBootMagic = RC_BOOT_MAGIC;
    gAbnormalBoots = 0;
  }
  bool bad = (rr == ESP_RST_PANIC || rr == ESP_RST_INT_WDT ||
              rr == ESP_RST_TASK_WDT || rr == ESP_RST_WDT);
  if (bad) { if (gAbnormalBoots < 10) gAbnormalBoots++; }
  else     { gAbnormalBoots = 0; }

  Serial.printf("[SYS] 重置原因: %s, 連續異常開機: %u\n",
                resetReasonName(rr), (unsigned)gAbnormalBoots);

  if (gAbnormalBoots >= 3) {
    nvs_handle_t h;
    if (nvs_open("phy", NVS_READWRITE, &h) == ESP_OK) {
      nvs_erase_all(h);
      nvs_commit(h);
      nvs_close(h);
      Serial.println("[SYS] 已清除 NVS 的 PHY 校正資料, 本次開機重新校正");
    }
    gAbnormalBoots = 0;
  }
}

/* =============================================================
 * 開機流程: 無人機 ID 選擇 + 搖桿校正
 * ============================================================= */
static void bootFlow() {
  /* --- 1) 轉 VRC1 選無人機 ID, SW1 = ON 確認 --- */
  Serial.println("[BOOT] 轉 VRC1 選擇無人機 ID, 按 SW1 確認");
  for (;;) {
    int raw = adcRead(PIN_VRC1);
    uint8_t id = (uint8_t)constrain(map((long)raw, 0, 4095, 0, 255), 0, 255);
    gSet.flyId = id;
    oledBootFlyId(id, raw);
    if (digitalRead(PIN_SW1) == LOW) break;
    delay(80);
  }
  Serial.printf("[BOOT] 已選 FlyID = %u, 開始搖桿校正\n", gSet.flyId);
  delay(50);

  /* --- 2) 按住 SW1 期間持續記錄四軸 min/max, 放開結束 --- */
  inputCalStart();
  while (digitalRead(PIN_SW1) == LOW) {
    inputRead();
    inputCalSample();
    oledBootCal();
    delay(20);
  }
  bool ok = inputCalFinish();

  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.println("Boot setup done");
  oled.printf("FlyID : %u\n", gSet.flyId);
  oled.printf("Cal   : %s\n", ok ? "OK" : "NOT CAL");
  oled.println(ok ? "Starting..." : "Hold SW1 longer next");
  oled.println("time to calibrate.");
  oled.display();
  delay(1200);

  settingsSaveIds();
  Serial.printf("[BOOT] 完成: FlyID=%u cal=%s\n", gSet.flyId, ok ? "OK" : "FAIL");
}

/* =============================================================
 * setup
 * ============================================================= */
void setup() {
  /* 停用電壓不足偵測 (BOD): 避免 WiFi/OLED 瞬間電流造成 brownout reset */
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("==========================================================");
  Serial.printf("  %s %s\n", FW_NAME, FW_VERSION);
  Serial.println("==========================================================");

  bootHealthCheck();

  inputBegin();

  if (!SPIFFS.begin(true)) Serial.println("[SPIFFS] 掛載失敗");
  settingsLoad();
  oledBegin();

  delay(30);
  /* --- 3) 按住 SW2 開機 = 跳過 ID/校正流程 --- */
  bool skip = (digitalRead(PIN_SW2) == LOW);
  if (skip && gSet.calValid) {
    Serial.println("[BOOT] SW2 = ON, 跳過流程, 使用已存 ID / 校正值");
    oledBootSkip();
    delay(1000);
  } else {
    if (skip) Serial.println("[BOOT] SW2 = ON 但尚無有效校正, 仍進入校正流程");
    bootFlow();
  }

  /* ---------- WiFi: STA + AP 共存 (僅 2.4GHz) ---------- */
  /* 設定都存在 SPIFFS, 關閉 WiFi 自身寫 NVS: 迴圈每 10 秒重連一次,
   * 若每次都寫 NVS 會加速磨損, 也會和 PHY 校正資料搶 NVS 空間。 */
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.setHostname(MDNS_HOST);
  {
    IPAddress apIp, apMask;
    apIp.fromString(AP_IP_ADDR);
    apMask.fromString("255.255.255.0");
    WiFi.softAPConfig(apIp, apIp, apMask);
    String ap = apSsid();
    WiFi.softAP(ap.c_str(), AP_PASS);
    Serial.printf("[AP] %s -> %s\n", ap.c_str(), WiFi.softAPIP().toString().c_str());
  }
  /* AP 與 STA 分開啟動, 避免兩次射頻啟動的電流峰值疊在一起 */
  delay(100);
  if (gSet.ssid.length()) {
    WiFi.begin(gSet.ssid.c_str(), gSet.wpass.c_str());
    Serial.printf("[WiFi] connecting to %s ...\n", gSet.ssid.c_str());
  }

  /* ---------- mDNS (ESPmDNS: 區網用 http://flybox-rc.local/) ---------- */
  if (!mdnsBegin()) Serial.println("[mDNS] 尚未取得 IP, 取得後自動啟動");

  webSetup();
  if (gSet.rxHost.length()) linkBegin();
  else Serial.println("[LINK] 尚未設定接收機, 請由網頁設定");

  if (gOledOk) { oled.clearDisplay(); oled.display(); }
  Serial.println("[WEB] 伺服器已啟動 (port 80)");
  Serial.printf("[INPUT] 校正=%s, 控制來源=%s, FlyID=%u\n",
                gSet.calValid ? "OK" : "未校正",
                inputSourceName(gSet.ctrlSource), gSet.flyId);
}

/* =============================================================
 * loop
 * ============================================================= */
void loop() {
  /* WiFi STA 斷線自動重連 (SoftAP 持續運作) */
  static uint32_t lastWifiTry = 0;
  if (gSet.ssid.length() && WiFi.status() != WL_CONNECTED) {
    if (millis() - lastWifiTry > 10000) {
      lastWifiTry = millis();
      WiFi.disconnect();
      WiFi.begin(gSet.ssid.c_str(), gSet.wpass.c_str());
    }
  }

  linkLoop();
  mdnsLoop();
  localWs.cleanupClients();

  /* ---------- 20Hz: 讀硬體搖桿並送出命令 ---------- */
  if (millis() - lastInput >= LINK_SEND_MS) {
    lastInput = millis();
    inputRead();

    static bool prevSw3 = false, prevSw4 = false;
    if (gHw.sw3 && !prevSw3 && linkUp()) linkSend("{\"cmd\":\"arm\"}");
    if (gHw.sw4 && !prevSw4 && linkUp()) linkSend("{\"cmd\":\"disarm\"}");
    prevSw3 = gHw.sw3;
    prevSw4 = gHw.sw4;

    if (inputHardwareActive()) {
      char b[112];
      snprintf(b, sizeof b,
               "{\"cmd\":\"setpoints\",\"roll\":%.0f,\"pitch\":%.0f,\"yaw\":%.0f}",
               inputRoll(), inputPitch(), inputYaw());
      linkSend(b);
      snprintf(b, sizeof b, "{\"cmd\":\"throttle\",\"value\":%ld}", inputThr());
      linkSend(b);
    }
  }

  /* ---------- LED: 已連線 300/700, 未連線 100/900 ---------- */
  uint32_t onMs  = gLinkUp ? 300 : 100;
  uint32_t offMs = gLinkUp ? 700 : 900;
  if (millis() - lastLed >= (ledState ? onMs : offMs)) {
    lastLed = millis();
    ledState = !ledState;
    digitalWrite(PIN_LED, ledState ? HIGH : LOW);
  }

  /* ---------- OLED: 約 5Hz 更新, 頁面由 VRC1 決定 ---------- */
  if (millis() - lastOled > OLED_REFRESH_MS) {
    lastOled = millis();
    oledShow();
  }

  if (gRebootAt && millis() > gRebootAt) {
    Serial.println("[SYS] 重新啟動...");
    delay(50);
    ESP.restart();
  }

  delay(1);
}