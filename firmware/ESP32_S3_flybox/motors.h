#ifndef FLYBOX_MOTORS_H
#define FLYBOX_MOTORS_H

#include <Arduino.h>
#include <ESP32Servo.h>
#include "config.h"

/* 8 顆 ESC 使用 ESP32Servo 庫產生 50Hz PWM (1000~2000us)。
 * ESP32Servo 內部使用 LEDC 硬體，支援最多 16 通道，足夠 8 顆馬達。
 * 未使用的通道仍持續輸出 ESC_MIN，避免 ESC 收不到訊號。
 */

static const uint8_t MOTOR_PINS[MAX_MOTORS] = {
  MOTOR1_PIN, MOTOR2_PIN, MOTOR3_PIN, MOTOR4_PIN,
  MOTOR5_PIN, MOTOR6_PIN, MOTOR7_PIN, MOTOR8_PIN
};

static Servo motorServos[MAX_MOTORS];

/* 狀態燈與馬達腳位衝突時，該馬達停用（不初始化 Servo，也不輸出）。
 * 預設 G39 = MOTOR6，啟用外接狀態燈後 M6 停用，因此 X8 無法使用。 */
#if USE_GPIO_STATUS_LED && (STATUS_LED_PIN == MOTOR6_PIN)
  #define MOTOR6_OUTPUT_ENABLED 0
#else
  #define MOTOR6_OUTPUT_ENABLED 1
#endif

static inline bool motorOutputEnabled(uint8_t i) {
#if !MOTOR6_OUTPUT_ENABLED
  return i != 5;   /* index 5 = M6 */
#else
  (void)i;
  return true;
#endif
}

static uint8_t motorCount = MOTOR_COUNT;

static inline void escWriteUs(uint8_t i, float us) {
  if (!motorOutputEnabled(i)) return;
  if (us < ESC_MIN) us = ESC_MIN;
  if (us > ESC_MAX) us = ESC_MAX;
  motorServos[i].writeMicroseconds((int)us);
}

/* 對所有啟用中的馬達輸出同一個 PWM (ESC 行程校正用) */
static inline void escWriteAllUs(float us) {
  for (uint8_t i = 0; i < MAX_MOTORS; i++) escWriteUs(i, us);
}

/* 開機 ESC 行程校正: 先全油門, 再降到最低油門。
 * 阻塞式, 僅可在 setup() 內、控制迴圈啟動前呼叫。
 * 校正期間馬達會短暫收到全油門訊號, 務必拆槳或固定好機體。 */
static inline void motorsEscCalibrate() {
#if ESC_CAL_ENABLE
  Serial.printf("[ESC] 行程校正: 輸出 %dus 維持 %dms (請拆槳)\n",
                ESC_CAL_HIGH_US, ESC_CAL_TIME_MS);
  escWriteAllUs(ESC_CAL_HIGH_US);
  delay(ESC_CAL_TIME_MS);
  escWriteAllUs(ESC_MIN);
  Serial.printf("[ESC] 已降到 %dus, 等待 %dms 完成初始化\n",
                ESC_MIN, ESC_CAL_LOW_MS);
  delay(ESC_CAL_LOW_MS);
#else
  escWriteAllUs(ESC_MIN);
#endif
}

static inline void motorsInit() {
  // ESP32Servo 預設使用所有可用的 LEDC 通道，需先配置足夠的定時器
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);

  for (uint8_t i = 0; i < MAX_MOTORS; i++) {
    if (!motorOutputEnabled(i)) continue;
    motorServos[i].setPeriodHertz(ESC_PWM_HZ);    // 50Hz
    motorServos[i].attach(MOTOR_PINS[i], ESC_MIN, ESC_MAX);  // 1000-2000us
    motorServos[i].writeMicroseconds(ESC_MIN);
  }
}

/* 切換 4 / 8 槳；未使用的馬達維持 ESC_MIN，不 detach。
 * 回傳 false 表示 M6 被狀態燈占用，無法使用 X8（維持原槳數）。 */
static inline bool motorsApplyCount(uint8_t count) {
  if (count != 8) count = 4;
#if !MOTOR6_OUTPUT_ENABLED
  if (count == 8) return false;
#endif
  motorCount = count;
  for (uint8_t i = count; i < MAX_MOTORS; i++) escWriteUs(i, ESC_MIN);
  return true;
}

#endif
