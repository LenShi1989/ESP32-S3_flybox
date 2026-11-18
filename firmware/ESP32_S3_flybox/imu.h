#ifndef FLYBOX_IMU_H
#define FLYBOX_IMU_H

#include <Arduino.h>
#include <M5Unified.h>
#include "config.h"

/* 機體座標 (NED): +X 前 (Type-C), +Y 右, +Z 下 */
struct GyroData { float roll, pitch, yaw; };   /* deg/s */
struct Attitude  { float roll, pitch, yaw; };  /* deg, yaw 0~360 */
struct Axis3     { float x, y, z; };

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

/* BMI270 + BMM150 讀取器:
 *   1. 開機 / calibrate() 時量測陀螺靜止零點偏移。
 *   2. update() 讀取 M5Unified 的最新資料, 扣除零點、轉到機體座標,
 *      對角速度做卡爾曼濾波, 並以互補濾波估 roll / pitch / yaw。
 *   3. BMM150 位於 BMI270 的 Sensor Hub, M5Unified 會一併讀取;
 *      mag.ok() 表示地磁資料有效 (BMM150 有回應且量值合理)。
 */
class Imu {
public:
  Imu() : _ok(false), _magOk(false), _attInit(false), _lastUs(0) {
    _o[0] = _o[1] = _o[2] = 0.0f;
    _f[0] = _f[1] = _f[2] = 0.0f;
    _rate = { 0.0f, 0.0f, 0.0f };
    _att  = { 0.0f, 0.0f, 0.0f };
    _acc  = { 0.0f, 0.0f, 0.0f };
    _mag  = { 0.0f, 0.0f, 0.0f };
  }

  bool begin() {
    _ok = M5.Imu.isEnabled();
    return _ok;
  }
  bool ok() const    { return _ok; }
  bool magOk() const { return _magOk; }

  /* 量測陀螺靜止零點 (機體必須靜止放平); 同時重置濾波器與姿態 */
  void calibrate(uint16_t samples) {
    if (samples == 0) samples = 1;
    float sum[3] = { 0.0f, 0.0f, 0.0f };
    uint16_t n = 0;
    uint32_t start = millis();
    /* BMI270 約 100Hz 輸出: 每筆之間留 1ms, 並加上逾時保護,
     * 避免 IMU 無回應時卡在開機流程。 */
    while (n < samples && millis() - start < (uint32_t)samples * 20UL + 2000UL) {
      if (M5.Imu.update()) {
        auto d = M5.Imu.getImuData();
        sum[0] += d.gyro.x;
        sum[1] += d.gyro.y;
        sum[2] += d.gyro.z;
        n++;
      }
      delay(1);
    }
    if (n) {
      _o[0] = sum[0] / n;
      _o[1] = sum[1] / n;
      _o[2] = sum[2] / n;
    }
    _f[0] = _f[1] = _f[2] = 0.0f;
#if GYRO_KALMAN_ENABLE
    _kR.reset(); _kP.reset(); _kY.reset();
#endif
    _attInit = false;
    _lastUs  = 0;
  }

  /* 有新資料時更新濾波與姿態; 回傳目前濾波後的機體角速度 */
  GyroData update() {
    if (!M5.Imu.update()) return _rate;

    uint32_t now = micros();
    float dt = (_lastUs == 0) ? 0.0f : (now - _lastUs) * 1e-6f;
    _lastUs = now;
    if (dt > CTRL_MAX_DT) dt = CTRL_MAX_DT;

    auto d = M5.Imu.getImuData();

    /* ---- 角速度: 扣零點 -> 機體軸向 -> 濾波 ---- */
    float g[3] = {
      d.gyro.value[0] - _o[0],
      d.gyro.value[1] - _o[1],
      d.gyro.value[2] - _o[2]
    };
    float r = g[IMU_GYRO_ROLL_AXIS]  * IMU_GYRO_ROLL_SIGN;
    float p = g[IMU_GYRO_PITCH_AXIS] * IMU_GYRO_PITCH_SIGN;
    float y = g[IMU_GYRO_YAW_AXIS]   * IMU_GYRO_YAW_SIGN;

#if GYRO_KALMAN_ENABLE
    _f[0] = _kR.update(r);
    _f[1] = _kP.update(p);
    _f[2] = _kY.update(y);
#else
    _f[0] += GYRO_LPF * (r - _f[0]);
    _f[1] += GYRO_LPF * (p - _f[1]);
    _f[2] += GYRO_LPF * (y - _f[2]);
#endif
    _rate = { _f[0], _f[1], _f[2] };

    /* ---- 加速度計 (g): +X 前 / +Y 右 / +Z 下 ---- */
    _acc.x = d.accel.value[IMU_ACC_FORWARD_AXIS] * IMU_ACC_FORWARD_SIGN;
    _acc.y = d.accel.value[IMU_ACC_RIGHT_AXIS]   * IMU_ACC_RIGHT_SIGN;
    _acc.z = d.accel.value[IMU_ACC_DOWN_AXIS]    * IMU_ACC_DOWN_SIGN;

    /* ---- 地磁計 (uT): +X 前 / +Y 右 / +Z 下 ---- */
    _mag.x = d.mag.value[IMU_MAG_FORWARD_AXIS] * IMU_MAG_FORWARD_SIGN;
    _mag.y = d.mag.value[IMU_MAG_RIGHT_AXIS]   * IMU_MAG_RIGHT_SIGN;
    _mag.z = d.mag.value[IMU_MAG_DOWN_AXIS]    * IMU_MAG_DOWN_SIGN;

    float magNorm = sqrtf(_mag.x * _mag.x + _mag.y * _mag.y + _mag.z * _mag.z);
    /* BMM150 量測範圍約 ±1300uT; 地球磁場約 25~65uT。
     * 低於 5uT 視為沒有資料 (Sensor Hub 未回應時為 0)。 */
    _magOk = (magNorm > 5.0f && magNorm < 2000.0f);

    updateAttitude(dt);
    return _rate;
  }

  const GyroData& rates()    const { return _rate; }
  const Attitude& attitude() const { return _att; }
  const Axis3&    accel()    const { return _acc; }
  const Axis3&    mag()      const { return _mag; }

private:
  static float wrap180(float a) {
    while (a >  180.0f) a -= 360.0f;
    while (a < -180.0f) a += 360.0f;
    return a;
  }
  static float wrap360(float a) {
    while (a >= 360.0f) a -= 360.0f;
    while (a <    0.0f) a += 360.0f;
    return a;
  }

  /* 傾斜補償方位角: 以目前 roll/pitch 把地磁向量轉到水平面,
   * 回傳以磁北為 0 度、順時針增加的方位角 (含磁偏角)。 */
  float headingFromMag() const {
    const float sr = sinf(_att.roll  * DEG_TO_RAD);
    const float cr = cosf(_att.roll  * DEG_TO_RAD);
    const float sp = sinf(_att.pitch * DEG_TO_RAD);
    const float cp = cosf(_att.pitch * DEG_TO_RAD);
    const float xh = _mag.x * cp + _mag.y * sr * sp + _mag.z * cr * sp;
    const float yh = _mag.y * cr - _mag.z * sr;
    return wrap360(atan2f(-yh, xh) * RAD_TO_DEG + MAG_DECLINATION_DEG);
  }

  void updateAttitude(float dt) {
    const float accNorm = sqrtf(_acc.x * _acc.x + _acc.y * _acc.y + _acc.z * _acc.z);
    const bool  accOk   = (accNorm > ATT_ACC_MIN_G && accNorm < ATT_ACC_MAX_G);

    float rollAcc = 0.0f, pitchAcc = 0.0f;
    if (accOk) {
      /* 靜止時加速度計量到的是「向上」的比力, 機體 +Z 向下 =>
       * 平放時 _acc.z = -1g。roll 右傾為正、pitch 抬頭為正。 */
      rollAcc  = atan2f(-_acc.y, -_acc.z) * RAD_TO_DEG;
      pitchAcc = atan2f(_acc.x, sqrtf(_acc.y * _acc.y + _acc.z * _acc.z)) * RAD_TO_DEG;
    }

    /* 第一筆有效資料: 直接以加速度計 / 地磁計初始化 */
    if (!_attInit) {
      if (!accOk) return;
      _att.roll  = rollAcc;
      _att.pitch = pitchAcc;
      _att.yaw   = _magOk ? headingFromMag() : 0.0f;
      _attInit = true;
      return;
    }

    /* 陀螺儀積分 */
    _att.roll  = wrap180(_att.roll  + _f[0] * dt);
    _att.pitch = wrap180(_att.pitch + _f[1] * dt);
    _att.yaw   = wrap360(_att.yaw   + _f[2] * dt);

    /* 加速度計修正 roll/pitch (只在接近 1g 時修正) */
    if (accOk) {
      const float k = dt / (ATT_TAU_S + dt);
      _att.roll  = wrap180(_att.roll  + k * wrap180(rollAcc  - _att.roll));
      _att.pitch = wrap180(_att.pitch + k * wrap180(pitchAcc - _att.pitch));
    }
    if (_att.pitch >  90.0f) _att.pitch =  90.0f;
    if (_att.pitch < -90.0f) _att.pitch = -90.0f;

    /* 地磁計修正 yaw (需要有效的地磁資料) */
    if (_magOk) {
      const float k = dt / (ATT_MAG_TAU_S + dt);
      const float target = wrap360(MAG_HEADING_SIGN * headingFromMag());
      _att.yaw = wrap360(_att.yaw + k * wrap180(target - _att.yaw));
    }
  }

  bool  _ok;
  bool  _magOk;
  bool  _attInit;
  float _o[3];        /* 陀螺零點 (M5Unified 板載座標) */
  float _f[3];        /* 濾波後角速度 (機體座標) */
  uint32_t _lastUs;
  GyroData _rate;
  Attitude _att;
  Axis3    _acc;
  Axis3    _mag;
#if GYRO_KALMAN_ENABLE
  Kalman1D _kR, _kP, _kY;
#endif
};

#endif
