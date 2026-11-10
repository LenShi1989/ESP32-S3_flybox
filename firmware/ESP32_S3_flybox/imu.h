#ifndef FLYBOX_IMU_H
#define FLYBOX_IMU_H

#include <Arduino.h>
#include <M5Unified.h>
#include "config.h"

struct GyroData {
  float roll;
  float pitch;
  float yaw;
};

/* 一維卡爾曼濾波 (隨機漫步模型): 單軸角速度去噪。
 * 預測: x = x, P = P + Q
 * 更新: K = P / (P + R), x += K * (z - x), P = (1 - K) * P
 * Q 大 -> 信任量測、反應快; R 大 -> 信任模型、較平滑。
 */
class Kalman1D {
public:
  Kalman1D() : _x(0), _p(0) {}
  void reset() { _x = 0; _p = 0; }
  float update(float z) {
    if (_p <= 0.0f) {          /* 第一筆: 直接以量測初始化 */
      _x = z;
      _p = GYRO_KALMAN_P_INIT;
      return _x;
    }
    _p += GYRO_KALMAN_Q;
    float k = _p / (_p + GYRO_KALMAN_R);
    _x += k * (z - _x);
    _p *= (1.0f - k);
    return _x;
  }

private:
  float _x, _p;
};

/* BMI270 陀螺儀讀取器 (I2C 由 M5Unified 初始化, SCL=GPIO0 / SDA=GPIO45):
 * 1. 開機 / calibrate() 時量測靜止零點偏移。
 * 2. read() 扣掉零點、套用軸向反向, 再做卡爾曼濾波
 *    (GYRO_KALMAN_ENABLE=0 時改回一階低通 GYRO_LPF)。
 * M5Unified 的角速度單位為 deg/s, 與 PID 設定點一致。
 */
class Gyro {
public:
  Gyro() : _ok(false), _oR(0), _oP(0), _oY(0), _fR(0), _fP(0), _fY(0) {}

  bool begin() {
    _ok = M5.Imu.isEnabled();
    return _ok;
  }
  bool ok() const { return _ok; }

  void calibrate(uint16_t samples) {
    if (samples == 0) samples = 1;
    float sR = 0, sP = 0, sY = 0;
    uint16_t n = 0;
    uint32_t start = millis();
    /* BMI270 約 100Hz 輸出: 每筆之間留 1ms, 並加上逾時保護,
     * 避免 IMU 無回應時卡在開機流程。 */
    while (n < samples && millis() - start < (uint32_t)samples * 20UL + 2000UL) {
      if (M5.Imu.update()) {
        auto d = M5.Imu.getImuData();
        sR += d.gyro.x;
        sP += d.gyro.y;
        sY += d.gyro.z;
        n++;
      }
      delay(1);
    }
    if (n) {
      _oR = sR / n;
      _oP = sP / n;
      _oY = sY / n;
    }
    _fR = _fP = _fY = 0;
#if GYRO_KALMAN_ENABLE
    _kR.reset(); _kP.reset(); _kY.reset();
#endif
  }

  GyroData read() {
    M5.Imu.update();
    auto d = M5.Imu.getImuData();

    float r = d.gyro.x - _oR;
    float p = d.gyro.y - _oP;
    float y = d.gyro.z - _oY;

#if IMU_REVERSE_ROLL
    r = -r;
#endif
#if IMU_REVERSE_PITCH
    p = -p;
#endif
#if IMU_REVERSE_YAW
    y = -y;
#endif

#if GYRO_KALMAN_ENABLE
    _fR = _kR.update(r);
    _fP = _kP.update(p);
    _fY = _kY.update(y);
#else
    _fR += GYRO_LPF * (r - _fR);
    _fP += GYRO_LPF * (p - _fP);
    _fY += GYRO_LPF * (y - _fY);
#endif

    GyroData out = { _fR, _fP, _fY };
    return out;
  }

  float offsetRoll()  const { return _oR; }
  float offsetPitch() const { return _oP; }
  float offsetYaw()   const { return _oY; }

private:
  bool  _ok;
  float _oR, _oP, _oY;
  float _fR, _fP, _fY;
#if GYRO_KALMAN_ENABLE
  Kalman1D _kR, _kP, _kY;
#endif
};

#endif
