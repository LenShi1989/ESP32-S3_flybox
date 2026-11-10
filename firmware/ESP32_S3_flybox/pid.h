#ifndef FLYBOX_PID_H
#define FLYBOX_PID_H

#include <Arduino.h>

/* 標準 PID: 微分作用於量測值 (避免 setpoint 跳變造成微分衝擊),
 * 積分項輸出限幅作為抗積分飽和 (anti-windup)。
 */
class PID {
public:
  PID()
    : _kp(0), _ki(0), _kd(0),
      _iTerm(0), _prevMeas(0),
      _outMin(-400), _outMax(400), _iLimit(0),
      _havePrev(false) {}

  void setTunings(float kp, float ki, float kd) { _kp = kp; _ki = ki; _kd = kd; }
  void setOutputLimits(float lo, float hi) { _outMin = lo; _outMax = hi; }
  void setIntegralLimit(float l) { _iLimit = l; }

  void reset() { _iTerm = 0; _prevMeas = 0; _havePrev = false; }

  float update(float setpoint, float measured, float dt, bool allowI = true) {
    if (dt <= 0) return 0;

    float error = setpoint - measured;

    float d = 0;
    if (_havePrev) d = (measured - _prevMeas) / dt;
    _prevMeas = measured;
    _havePrev = true;

    if (allowI) {
      _iTerm += _ki * error * dt;
      if (_iLimit > 0) {
        if (_iTerm >  _iLimit) _iTerm =  _iLimit;
        if (_iTerm < -_iLimit) _iTerm = -_iLimit;
      }
    }

    float out = _kp * error + _iTerm - _kd * d;
    if (out > _outMax) out = _outMax;
    if (out < _outMin) out = _outMin;
    return out;
  }

  float kp() const { return _kp; }
  float ki() const { return _ki; }
  float kd() const { return _kd; }

private:
  float _kp, _ki, _kd;
  float _iTerm;
  float _prevMeas;
  float _outMin, _outMax, _iLimit;
  bool  _havePrev;
};

#endif
