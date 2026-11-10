#ifndef FLYBOX_JSONUTIL_H
#define FLYBOX_JSONUTIL_H

#include <Arduino.h>

/* 輕量 JSON 解析工具 (不做動態記憶體配置, 不使用 ArduinoJson)。
 * 僅支援本專案命令用的平面鍵值解析; 遙測輸出直接以 snprintf 串流。
 * 接收機送來的命令都很短 (一行一個物件), 不需要完整 JSON 解析器。
 */

/* 回傳 "key" 後、冒號後的值起點; 找不到回傳 0 */
static inline const char* jFind(const char* json, const char* key) {
  for (const char* p = json; *p; p++) {
    if (*p != '"') continue;
    const char* q = p + 1;
    const char* k = key;
    while (*k && *q == *k) { q++; k++; }
    if (*k || *q != '"') continue;      /* 只比對到一半或不是結束引號 */
    q++;
    while (*q == ' ' || *q == '\t') q++;
    if (*q != ':') return 0;
    q++;
    while (*q == ' ' || *q == '\t') q++;
    return q;
  }
  return 0;
}

static inline bool jHas(const char* json, const char* key) {
  return jFind(json, key) != 0;
}

/* 只解析 [+-]十進位整數 */
static inline long jInt(const char* json, const char* key, long def) {
  const char* p = jFind(json, key);
  if (!p) return def;
  bool neg = false;
  if (*p == '-') { neg = true; p++; }
  else if (*p == '+') p++;
  if (*p < '0' || *p > '9') return def;
  long v = 0;
  while (*p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
  return neg ? -v : v;
}

/* 解析 [+-]整數[.小數] (最多 4 位小數), 足夠 PID / 設定點使用 */
static inline float jFloat(const char* json, const char* key, float def) {
  const char* p = jFind(json, key);
  if (!p) return def;
  uint8_t neg = 0;
  if (*p == '-') { neg = 1; p++; }
  else if (*p == '+') p++;
  long ip = 0;
  while (*p >= '0' && *p <= '9') ip = ip * 10 + (*p++ - '0');
  long fp = 0, div = 1;
  if (*p == '.') {
    p++;
    while (*p >= '0' && *p <= '9') { fp = fp * 10 + (*p++ - '0'); div *= 10; }
  }
  float v = (float)ip + (float)fp / (float)div;
  return neg ? -v : v;
}

static inline bool jStr(const char* json, const char* key, char* out, size_t n) {
  const char* p = jFind(json, key);
  if (!p || *p != '"') return false;
  p++;
  size_t i = 0;
  while (*p && *p != '"' && i < n - 1) out[i++] = *p++;
  out[i] = 0;
  return true;
}

#endif
