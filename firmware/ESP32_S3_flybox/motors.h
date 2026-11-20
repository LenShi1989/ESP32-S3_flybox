#ifndef FLYBOX_MOTORS_H
#define FLYBOX_MOTORS_H

#include <Arduino.h>
#if __has_include(<esp_arduino_version.h>)
  #include <esp_arduino_version.h>
#endif
#include "config.h"

/* 8 顆 ESC 以 LEDC 產生 50Hz PWM (1000~2000us)。
 * ESP32-S3 有 8 個 LEDC 通道, 剛好對應最多 8 顆馬達。
 * 未使用的通道仍持續輸出 ESC_MIN, 避免 ESC 收不到訊號。
 */

static const uint8_t MOTOR_PINS[MAX_MOTORS] = {
  MOTOR1_PIN, MOTOR2_PIN, MOTOR3_PIN, MOTOR4_PIN,
  MOTOR5_PIN, MOTOR6_PIN, MOTOR7_PIN, MOTOR8_PIN
};

/* 狀態燈與馬達腳位衝突時, 該馬達停用 (不初始化 LEDC, 也不輸出)。
 * 預設 G39 = MOTOR6, 啟用外接狀態燈後 M6 停用, 因此 X8 無法使用。 */
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

static inline uint32_t usToDuty(float us) {
  if (us < ESC_MIN) us = ESC_MIN;
  if (us > ESC_MAX) us = ESC_MAX;
  const float maxDuty = (float)((1UL << ESC_PWM_BITS) - 1);
  return (uint32_t)(us * maxDuty / ESC_PWM_PERIOD + 0.5f);
}

static inline void escWriteDuty(uint8_t i, uint32_t duty) {
  if (!motorOutputEnabled(i)) return;   /* 腳位被狀態燈占用 */
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(MOTOR_PINS[i], duty);        /* core 3.x: 以腳位為索引 */
#else
  ledcWrite(i, duty);                    /* core 2.x: 以通道為索引 */
#endif
}

static inline void escWriteUs(uint8_t i, float us) {
  escWriteDuty(i, usToDuty(us));
}

static inline void motorsInit() {
  for (uint8_t i = 0; i < MAX_MOTORS; i++) {
    if (!motorOutputEnabled(i)) continue;
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcAttach(MOTOR_PINS[i], ESC_PWM_HZ, ESC_PWM_BITS);
#else
    ledcSetup(i, ESC_PWM_HZ, ESC_PWM_BITS);
    ledcAttachPin(MOTOR_PINS[i], i);
#endif
    escWriteUs(i, ESC_MIN);
  }
}

/* 切換 4 / 8 槳; 未使用的馬達維持 ESC_MIN, 不 detach。
 * 回傳 false 表示 M6 被狀態燈占用, 無法使用 X8 (維持原槳數)。 */
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
