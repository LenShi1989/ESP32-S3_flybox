/*
 * ESP32-S3 飛控板 (M5Stack AtomS3R-Ext) 韌體
 * ---------------------------------------------------------------
 * 版本 (Version): 2.3.3        (見 config.h 的 FW_VERSION)
 * 名稱 (Name):    ESP32-S3 Flybox
 * ---------------------------------------------------------------
 * - Arduino IDE 1.8.19 / ESP32 core 2.x 或 3.x
 * - PID 演算法 (三軸 rate PID) + 陀螺儀回授自主平衡
 * - IMU: BMI270 六軸 (I2C, 由 M5Unified 初始化), 開機自動校正零點
 * - MAG: BMM150 三軸地磁, 經 BMI270 Sensor Hub 讀取, 用於姿態 yaw
 * - 姿態: 互補濾波 (陀螺 + 加速度計 + 地磁), 遙測回報 roll/pitch/yaw
 * - RS-232 (UART1, 115200 8N1) 與 WT32-ETH01 接收機交換單行 JSON
 *   命令: ping / led / arm / disarm / calibrate (bmi270/bmm150) / nopid /
 *         throttle / hold / motors / setpoints / pid / motmin / telemetry / status
 * - 開機預設 4 槳 Quad-X (X4), 可執行中切換 8 槳 X8 同軸
 * - 狀態燈: 板載 RGB + 外接 G39 LED, 由遙控器 led 命令控制
 * - 機頭方向 = USB Type-C 接口方向
 * - 各馬達最低啟動 PWM 存於 NVS, 重開機仍保留
 * - 馬達輸出: ESP32Servo 庫 (50Hz, 1000-2000us)，簡化 PWM 控制
 * - Web 介面 (SPIFFS + mDNS + AsyncWebServer):
 *     系統狀態、WiFi 掃描/連線/清除、OTA 更新 (firmware.bin/spiffs.bin)、
 *     使用者帳密/Token/無人機 ID 設定、WebSocket 雙向即時通訊
 * - WiFi: SoftAP (flybox.local) + STA 同時運作
 *
 * 安全: 超過 FAILSAFE_MS 沒收到命令會自動上鎖; IMU 無回應時拒絕解鎖。
 * 序列命令請見 README.md。所有命令以 '\n' 結尾。
 */

#include <Arduino.h>
#include <math.h>
#include <M5Unified.h>
#include <Preferences.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include "config.h"
#include "jsonutil.h"
#include "pid.h"
#include "mixer.h"
#include "imu.h"
#include "motors.h"
#include "link.h"
#include "settings.h"
#include "web.h"

Imu  imu;
PID  pidRoll, pidPitch, pidYaw;

GyroData gyroNow = { 0.0f, 0.0f, 0.0f };

float setRoll  = 0.0f;
float setPitch = 0.0f;
float setYaw   = 0.0f;
long  throttleUs = ESC_MIN;
bool  armed = false;
bool  hold  = false;         /* 定高 (油門保持): 鎖定當前油門 */
bool  pidBypass = false;     /* 馬達校正: 略過 PID, 馬達僅輸出固定油門 */

uint16_t telIntervalMs = TEL_INTERVAL_MS;
uint8_t  ledMode  = 2;       /* 0=off 1=on 2=blink */
uint8_t  ledValue = 0;

uint16_t motorMin[MAX_MOTORS];
float    motorUs[MAX_MOTORS];

uint32_t lastCtrlUs = 0;
uint32_t lastTelMs  = 0;
uint32_t lastCmdMs  = 0;
uint32_t lastLedMs  = 0;

Preferences pref;

/* ---------------- 各馬達最低啟動 PWM (NVS) ---------------- */
static void motorMinSaveAll() {
  for (uint8_t i = 0; i < MAX_MOTORS; i++) {
    char k[6];
    snprintf(k, sizeof k, "mm%u", (unsigned)i);
    pref.putUShort(k, motorMin[i]);
  }
}

static void motorMinLoad() {
  pref.begin("flybox", false);
  bool fix = false;
  for (uint8_t i = 0; i < MAX_MOTORS; i++) {
    char k[6];
    snprintf(k, sizeof k, "mm%u", (unsigned)i);
    motorMin[i] = (uint16_t)pref.getUShort(k, MOTOR_MIN_US);
    if (motorMin[i] < MOTOR_MIN_MIN || motorMin[i] > MOTOR_MIN_MAX) {
      motorMin[i] = MOTOR_MIN_US;
      fix = true;
    }
  }
  if (fix) motorMinSaveAll();
}

/* ---------------- RS-232 輸出輔助 ---------------- */
static void ackCmd(const char* what) {
  char b[48];
  snprintf(b, sizeof b, "{\"ack\":\"%s\"}", what);
  linkSendLine(b);
}

/* ---------------- 狀態燈 (板載 RGB + 外接 GPIO) ---------------- */
static void ledWrite(bool on) {
#if USE_GPIO_STATUS_LED
#if STATUS_LED_ACTIVE_HIGH
  digitalWrite(STATUS_LED_PIN, on ? HIGH : LOW);
#else
  digitalWrite(STATUS_LED_PIN, on ? LOW : HIGH);
#endif
#endif

#if USE_STATUS_LED
  if (M5.Led.isEnabled()) {
    /* 未解鎖: 藍燈, 已解鎖: 綠燈 */
    if (on) M5.Led.setAllColor(0, armed ? 255 : 0, armed ? 0 : 255);
    else    M5.Led.setAllColor(0, 0, 0);
    M5.Led.display();
  }
#else
  (void)on;
#endif
}

/* 非阻塞 LED 顯示: 未解鎖慢閃, 已解鎖快閃 */
static void updateLed() {
  if (ledMode == 0) {
    ledValue = 0;
  } else if (ledMode == 1) {
    ledValue = 1;
  } else {
    uint16_t period = armed ? 25 : 500;
    if (millis() - lastLedMs >= period) {
      lastLedMs = millis();
      ledValue = !ledValue;
    }
  }
  ledWrite(ledValue != 0);
}

/* ---------------- 控制回路 ---------------- */
/* 固定頻率: 讀 IMU -> PID -> 混控 -> 輸出 ESC */
static void controlStep(float dt) {
  gyroNow = imu.update();

  float rOut = 0.0f, pOut = 0.0f, yOut = 0.0f;
  if (armed && !pidBypass) {
    /* 自動配平: 僅在 roll/pitch 搖桿置中時累積積分, 依陀螺殘餘轉速
     * 緩慢學習配平量; 搖桿離開中央即凍結 (保留配平值)。
     * yaw 積分維持常開。 */
    bool centered = setRoll  < TRIM_DEADBAND && setRoll  > -TRIM_DEADBAND &&
                    setPitch < TRIM_DEADBAND && setPitch > -TRIM_DEADBAND;
    rOut = pidRoll.update(setRoll,   gyroNow.roll,  dt, centered);
    pOut = pidPitch.update(setPitch, gyroNow.pitch, dt, centered);
    yOut = pidYaw.update(setYaw,     gyroNow.yaw,   dt);
  } else {
    pidRoll.reset();
    pidPitch.reset();
    pidYaw.reset();
  }

  float thr = armed ? (float)throttleUs : (float)ESC_MIN;
  mixerCompute(motorCount, thr, rOut, pOut, yOut, motorUs);

  /* 油門離開 1000us 後, 各馬達至少輸出其最低啟動 PWM, 避免低油門
   * 時因 ESC 死區不同而個別停轉。ESC 行程校正 (nopid) 期間不套用。 */
  bool motorStart = armed && !pidBypass && throttleUs > ESC_MIN;
  for (uint8_t i = 0; i < MAX_MOTORS; i++) {
    float v = (i < motorCount) ? motorUs[i] : (float)ESC_MIN;
    if (motorStart && (float)motorMin[i] > v) v = (float)motorMin[i];
    if (v < ESC_MIN) v = ESC_MIN;
    if (v > ESC_MAX) v = ESC_MAX;
    motorUs[i] = v;
    escWriteUs(i, v);
  }

  if (armed && (millis() - lastCmdMs > FAILSAFE_MS)) {
    armed = false;
    hold  = false;
    linkSendLine("{\"evt\":\"failsafe\"}");
  }
}

/* ---------------- 遙測 ---------------- */
static void sendTelemetry() {
  if (telIntervalMs == 0) return;
  uint32_t now = millis();
  if (now - lastTelMs < telIntervalMs) return;
  lastTelMs = now;

  const Attitude& att = imu.attitude();
  const Axis3&    acc = imu.accel();
  const Axis3&    mag = imu.mag();

  /* 單位: gyro=deg/s, att=deg (yaw 0~360), acc=mg, mag=0.1uT */
  char buf[384];
  int n = snprintf(buf, sizeof buf,
    "{\"t\":%lu,\"armed\":%d,\"mcount\":%u,\"hold\":%d,\"magok\":%d,"
    "\"gyro\":{\"roll\":%d,\"pitch\":%d,\"yaw\":%d},"
    "\"att\":{\"roll\":%d,\"pitch\":%d,\"yaw\":%d},"
    "\"acc\":{\"x\":%d,\"y\":%d,\"z\":%d},"
    "\"mag\":{\"x\":%d,\"y\":%d,\"z\":%d},"
    "\"set\":{\"roll\":%d,\"pitch\":%d,\"yaw\":%d,\"thr\":%ld},"
    "\"motor\":[",
    (unsigned long)now, armed ? 1 : 0, (unsigned)motorCount, hold ? 1 : 0,
    imu.magOk() ? 1 : 0,
    (int)lroundf(gyroNow.roll), (int)lroundf(gyroNow.pitch), (int)lroundf(gyroNow.yaw),
    (int)lroundf(att.roll), (int)lroundf(att.pitch), ((int)lroundf(att.yaw)) % 360,
    (int)lroundf(acc.x * 1000.0f), (int)lroundf(acc.y * 1000.0f), (int)lroundf(acc.z * 1000.0f),
    (int)lroundf(mag.x * 10.0f), (int)lroundf(mag.y * 10.0f), (int)lroundf(mag.z * 10.0f),
    (int)lroundf(setRoll), (int)lroundf(setPitch), (int)lroundf(setYaw), throttleUs);

  for (uint8_t i = 0; i < motorCount && n > 0 && n < (int)sizeof buf; i++) {
    n += snprintf(buf + n, sizeof buf - n, i ? ",%d" : "%d", (int)(motorUs[i] + 0.5f));
  }
  if (n > 0 && n < (int)sizeof buf - 2) snprintf(buf + n, sizeof buf - n, "]}");
  linkSendLine(buf);
}

/* ---------------- 命令解析 ---------------- */
static void applyPid(const char* axis, float kp, float ki, float kd) {
  PID* p = 0;
  if      (!strcmp(axis, "roll"))  p = &pidRoll;
  else if (!strcmp(axis, "pitch")) p = &pidPitch;
  else if (!strcmp(axis, "yaw"))   p = &pidYaw;
  if (p) p->setTunings(kp, ki, kd);
}

static void handleCommand(char* s) {
  lastCmdMs = millis();

  char cmd[16];
  if (!jStr(s, "cmd", cmd, sizeof cmd)) return;

  if (!strcmp(cmd, "ping")) {
    ackCmd("ping");

  } else if (!strcmp(cmd, "led")) {
    char mode[8];
    if (jStr(s, "mode", mode, sizeof mode)) {
      if      (!strcmp(mode, "on"))    ledMode = 1;
      else if (!strcmp(mode, "off"))   ledMode = 0;
      else if (!strcmp(mode, "blink")) ledMode = 2;
    }
    char b[48];
    snprintf(b, sizeof b, "{\"ack\":\"led\",\"mode\":%u}", (unsigned)ledMode);
    linkSendLine(b);

  } else if (!strcmp(cmd, "arm")) {
    if (!imu.ok()) { linkSendLine("{\"err\":\"imu\"}"); return; }
    throttleUs = ESC_MIN;
    hold = false;
    armed = true;
    ackCmd("arm");

  } else if (!strcmp(cmd, "disarm")) {
    armed = false;
    hold = false;
    ackCmd("disarm");

  } else if (!strcmp(cmd, "calibrate")) {
    /* 校正期間機體必須靜止, 因此解鎖中不允許重新校正 */
    if (armed) { linkSendLine("{\"err\":\"armed\"}"); return; }

    char sensor[12] = "bmi270";
    jStr(s, "sensor", sensor, sizeof sensor);
    if (!strcmp(sensor, "bmm150")) {
      if (!imu.magCalStart(BMM150_CAL_MS)) {
        linkSendLine("{\"err\":\"magcal busy\"}");
        return;
      }
      char b[96];
      snprintf(b, sizeof b,
               "{\"ack\":\"calibrate\",\"sensor\":\"bmm150\",\"state\":\"start\",\"ms\":%u}",
               (unsigned)BMM150_CAL_MS);
      linkSendLine(b);
    } else {
      imu.calibrate(GYRO_CAL_SAMPLES);
      linkSendLine("{\"ack\":\"calibrate\",\"sensor\":\"bmi270\"}");
    }

  } else if (!strcmp(cmd, "nopid")) {
    pidBypass = jInt(s, "on", 0) ? true : false;
    ackCmd("nopid");

  } else if (!strcmp(cmd, "throttle")) {
    long v = jInt(s, "value", throttleUs);
    if (v < ESC_MIN) v = ESC_MIN;
    if (v > ESC_MAX) v = ESC_MAX;
    if (!hold) throttleUs = v;   /* 定高中: 保持當前油門, 忽略變更 */
    ackCmd("throttle");

  } else if (!strcmp(cmd, "hold")) {
    hold = jInt(s, "on", 0) ? true : false;
    ackCmd("hold");

  } else if (!strcmp(cmd, "motors")) {
    if (!motorsApplyCount((uint8_t)jInt(s, "count", motorCount))) {
      /* G39 狀態燈占用 M6, 無法使用 X8 */
      linkSendLine("{\"err\":\"motor6 led\"}");
      return;
    }
    ackCmd("motors");

  } else if (!strcmp(cmd, "setpoints")) {
    setRoll  = jFloat(s, "roll",  setRoll);
    setPitch = jFloat(s, "pitch", setPitch);
    setYaw   = jFloat(s, "yaw",   setYaw);
    ackCmd("setpoints");

  } else if (!strcmp(cmd, "pid")) {
    char axis[8] = "roll";
    jStr(s, "axis", axis, sizeof axis);
    applyPid(axis, jFloat(s, "kp", 0), jFloat(s, "ki", 0), jFloat(s, "kd", 0));
    ackCmd("pid");

  } else if (!strcmp(cmd, "motmin")) {
    long idx = jInt(s, "index", 1);
    long val = jInt(s, "value", MOTOR_MIN_US);
    if (val < MOTOR_MIN_MIN) val = MOTOR_MIN_MIN;
    if (val > MOTOR_MIN_MAX) val = MOTOR_MIN_MAX;

    if (idx == 0) {
      for (uint8_t i = 0; i < MAX_MOTORS; i++) motorMin[i] = (uint16_t)val;
      motorMinSaveAll();
    } else if (idx >= 1 && idx <= MAX_MOTORS) {
      motorMin[idx - 1] = (uint16_t)val;
      char k[6];
      snprintf(k, sizeof k, "mm%u", (unsigned)(idx - 1));
      pref.putUShort(k, motorMin[idx - 1]);
    }
    ackCmd("motmin");

  } else if (!strcmp(cmd, "telemetry")) {
    long v = jInt(s, "value", telIntervalMs);
    if (v < 0) v = 0;
    if (v > 1000) v = 1000;
    telIntervalMs = (uint16_t)v;
    ackCmd("telemetry");

  } else if (!strcmp(cmd, "status")) {
    char b[288];
    int n = snprintf(b, sizeof b,
      "{\"ack\":\"status\",\"fw\":\"" FW_NAME "\",\"ver\":\"" FW_VERSION "\","
      "\"armed\":%d,\"thr\":%ld,\"tel\":%u,\"mcount\":%u,\"hold\":%d,"
      "\"magok\":%d,\"mmin\":[",
      armed ? 1 : 0, throttleUs, (unsigned)telIntervalMs,
      (unsigned)motorCount, hold ? 1 : 0, imu.magOk() ? 1 : 0);
    for (uint8_t i = 0; i < MAX_MOTORS && n > 0 && n < (int)sizeof b; i++) {
      n += snprintf(b + n, sizeof b - n, i ? ",%u" : "%u", (unsigned)motorMin[i]);
    }
    if (n > 0 && n < (int)sizeof b - 2) snprintf(b + n, sizeof b - n, "]}");
    linkSendLine(b);

  } else {
    linkSendLine("{\"err\":\"bad cmd\"}");
  }
}

/* ---------------- 初始化 ---------------- */
void setup() {
  auto cfg = M5.config();
  cfg.internal_imu = true;
  cfg.clear_display = false;
  M5.begin(cfg);
  Serial.begin(115200);

  Serial.printf("\n[SYS] %s v%s\n", FW_NAME, FW_VERSION);
  Serial.println("[SYS] 機頭 = USB Type-C, 預設 4 槳 Quad-X (X4)");

#if USE_GPIO_STATUS_LED
  pinMode(STATUS_LED_PIN, OUTPUT);
  ledWrite(false);
#endif

  if (!imu.begin()) {
    Serial.println("[IMU] BMI270 初始化失敗, 將拒絕解鎖");
  } else {
    Serial.println("[IMU] BMI270 六軸就緒 (I2C SCL=G0 / SDA=G45)");
  }

  linkBegin();
  Serial.printf("[RS232] UART1 %d 8N1 TX=G%d RX=G%d\n",
                RS232_BAUD, RS232_TX_PIN, RS232_RX_PIN);

  /* ---- WiFi 初始化 ---- */
  settingsLoad();
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apSsid().c_str(), AP_PASS);
  Serial.printf("[WiFi] SoftAP 啟動: %s\n", apSsid().c_str());
  if (gSet.ssid.length() > 0) {
    WiFi.begin(gSet.ssid.c_str(), gSet.wpass.c_str());
    Serial.printf("[WiFi] 連線中: %s\n", gSet.ssid.c_str());
  } else {
    Serial.println("[WiFi] 未設定 SSID, 僅 SoftAP 模式");
  }

  motorMinLoad();
  motorsInit();
  motorsApplyCount(MOTOR_COUNT);

#if USE_GPIO_STATUS_LED
  Serial.printf("[LED] 外接狀態燈 G%d, %s\n", STATUS_LED_PIN,
                MOTOR6_OUTPUT_ENABLED ? "M6 仍可用" : "M6 已停用 (X8 不可用)");
#endif

  pidRoll.setTunings(PID_ROLL_KP, PID_ROLL_KI, PID_ROLL_KD);
  pidPitch.setTunings(PID_PITCH_KP, PID_PITCH_KI, PID_PITCH_KD);
  pidYaw.setTunings(PID_YAW_KP, PID_YAW_KI, PID_YAW_KD);
  pidRoll.setOutputLimits(-PID_OUT_LIMIT, PID_OUT_LIMIT);
  pidPitch.setOutputLimits(-PID_OUT_LIMIT, PID_OUT_LIMIT);
  pidYaw.setOutputLimits(-PID_OUT_LIMIT, PID_OUT_LIMIT);
  pidRoll.setIntegralLimit(PID_I_LIMIT);
  pidPitch.setIntegralLimit(PID_I_LIMIT);
  pidYaw.setIntegralLimit(PID_I_LIMIT);

  /* 開機校正: 機體必須靜止放平 */
  imu.calibrate(GYRO_CAL_SAMPLES);
  /* 等 BMM150 第一筆有效資料 (最多約 200ms), 以便開機訊息與初始 yaw */
  for (uint8_t i = 0; i < 20 && !imu.magOk(); i++) {
    imu.update();
    delay(10);
  }
  Serial.printf("[IMU] 地磁 BMM150 (BMI270 Sensor Hub): %s\n",
                imu.magOk() ? "就緒" : "無資料");

  /* ---- Web 伺服器啟動 ---- */
  webSetup();

  lastCtrlUs = micros();
  lastCmdMs  = millis();
  lastTelMs  = millis();

  linkSendLine("{\"evt\":\"ready\",\"fw\":\"" FW_NAME "\",\"ver\":\"" FW_VERSION "\"}");
  Serial.println("[SYS] 就緒");
}

/* ---------------- 主迴圈 ---------------- */
void loop() {
  char line[LINE_BUF_SIZE];
  while (linkReadLine(line, sizeof line)) handleCommand(line);

  uint32_t nowUs = micros();
  uint32_t durUs = nowUs - lastCtrlUs;
  if (durUs >= CTRL_PERIOD_US) {
    lastCtrlUs = nowUs;
    float dt = durUs * 1e-6f;
    if (dt > CTRL_MAX_DT) dt = CTRL_MAX_DT;
    controlStep(dt);
  }

  /* BMM150 校正時間到: 回報結果 (校正值已寫入 NVS) */
  if (imu.magCalDone()) {
    char b[80];
    snprintf(b, sizeof b, "{\"evt\":\"magcal\",\"sensor\":\"bmm150\",\"ok\":%d}",
             imu.magCalValid() ? 1 : 0);
    linkSendLine(b);
  }

  updateLed();
  sendTelemetry();
  webLoop();
}
