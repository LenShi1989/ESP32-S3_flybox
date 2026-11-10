#ifndef FLYBOX_LINK_H
#define FLYBOX_LINK_H

#include <Arduino.h>
#include "config.h"

/* RS-232 橋接: 與 WT32-ETH01 接收機以 UART1 交換單行 JSON。
 * 接收機把飛控送出的每一行原樣廣播給 WebSocket 用戶端,
 * 因此遙測 / ack / evt / err 都必須各自成一行並以 '\n' 結尾。
 */

static char   gRxBuf[LINE_BUF_SIZE];
static size_t gRxLen = 0;

static inline void linkBegin() {
  Serial1.begin(RS232_BAUD, SERIAL_8N1, RS232_RX_PIN, RS232_TX_PIN);
  gRxLen = 0;
}

/* 讀取一行; 成功回傳 true 並複製到 out (含結尾 0) */
static inline bool linkReadLine(char* out, size_t n) {
  while (Serial1.available()) {
    char c = (char)Serial1.read();
    if (c == '\n' || c == '\r') {
      if (gRxLen > 0) {
        size_t len = gRxLen < n - 1 ? gRxLen : n - 1;
        memcpy(out, gRxBuf, len);
        out[len] = 0;
        gRxLen = 0;
        return true;
      }
    } else if (gRxLen < LINE_BUF_SIZE - 1) {
      gRxBuf[gRxLen++] = c;
    } else {
      gRxLen = 0;   /* 溢位: 丟棄此行 */
    }
  }
  return false;
}

/* 送出一行 (自動補 '\n'), s 需為單行 JSON, 不含換行 */
static inline void linkSendLine(const char* s) {
  Serial1.print(s);
  Serial1.print('\n');
}

#endif
