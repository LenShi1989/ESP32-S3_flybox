#ifndef RC_TELEM_H
#define RC_TELEM_H

#include <Arduino.h>

/* 由接收機收到的遙測 (解析自 KKBOX JSON) */
struct Telem {
  bool     valid;
  bool     armed;
  long     thr;
  float    roll, pitch, yaw;
  int      motor[4];
  uint32_t ms;
};

static Telem    gTelem = { false, false, 1000, 0, 0, 0, {0, 0, 0, 0}, 0 };
static bool     gLinkUp = false;         /* 與接收機的 WebSocket 是否連線 */
static String   gTelemetry = "{}";       /* 最新遙測原始 JSON */
static uint32_t gTelemetryMs = 0;

#endif
