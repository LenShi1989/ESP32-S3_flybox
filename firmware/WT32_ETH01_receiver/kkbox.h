#ifndef RECEIVER_KKBOX_H
#define RECEIVER_KKBOX_H

#include <Arduino.h>
#include "config.h"

/* -------------------------------------------------------------
 * KKBOX 飛控板 UART 橋接
 * 飛控板送出 JSON (遙測 / ack / evt), 每行一筆以 '\n' 結尾。
 * 接收機把每行原樣廣播給 WebSocket 用戶端, 並保留最新遙測。
 * ------------------------------------------------------------- */

static String gLastTelemetry = "{}";
static uint32_t gLastTelemetryMs = 0;
static String gLastAck = "";
static String gLastEvt = "";
static uint32_t gKkLines = 0;         /* 累計收到行數 */

static String gRxBuf;

static inline void kkBegin() {
  Serial2.begin(KK_BAUD, SERIAL_8N1, KK_RX_PIN, KK_TX_PIN);
  gLastTelemetryMs = millis();
}

/* 送出命令字串 (自動補 '\n'); s 需為單行 JSON, 不含換行 */
static inline void kkSend(const String& s) {
  Serial2.print(s);
  Serial2.print('\n');
}

/* 讀取一行; 成功回傳 true 並填入 out */
static inline bool kkReadLine(String& out) {
  while (Serial2.available()) {
    char c = (char)Serial2.read();
    if (c == '\n' || c == '\r') {
      if (gRxBuf.length() > 0) {
        out = gRxBuf;
        gRxBuf = "";
        return true;
      }
    } else if (gRxBuf.length() < 512) {
      gRxBuf += c;
    } else {
      gRxBuf = "";   /* 溢位保護 */
    }
  }
  return false;
}

#endif
