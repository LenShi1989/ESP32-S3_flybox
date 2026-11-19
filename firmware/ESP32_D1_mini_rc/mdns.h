#ifndef RC_MDNS_H
#define RC_MDNS_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ctype.h>
#include "config.h"

/* =============================================================
 * 輕量 mDNS: 只回應 <MDNS_HOST>.local 的 A 記錄查詢
 * -------------------------------------------------------------
 * 遙控器 flash 已接近上限, 引入 ESPmDNS 會增加約 30KB 而放不下,
 * 因此這裡自行實作區網解析所需的最小 mDNS 應答 (5353/UDP)。
 * 查詢端 (Windows / macOS / Android / iOS) 只要拿得到 A 記錄即可
 * 用 http://flybox-rc.local/ 開啟網頁。
 * ============================================================= */

static WiFiUDP   gMdnsUdp;
static const IPAddress MDNS_V4(224, 0, 0, 251);
static const uint16_t  MDNS_PORT_ID = 5353;

static inline bool mdnsBegin() {
  return gMdnsUdp.beginMulticast(MDNS_V4, MDNS_PORT_ID) != 0;
}

/* 比對 DNS QNAME 是否等於 <MDNS_HOST>.local (不分大小寫) */
static bool mdnsNameMatch(const uint8_t* p, size_t len) {
  String want = String(MDNS_HOST) + ".local";
  String got;
  got.reserve(32);
  size_t i = 0;
  while (i < len) {
    uint8_t l = p[i++];
    if (l == 0) break;
    if (l & 0xC0) return false;            /* 不處理壓縮指標 */
    if (i + l > len) return false;
    if (got.length()) got += '.';
    for (uint8_t k = 0; k < l; k++) got += (char)tolower((unsigned char)p[i + k]);
    i += l;
  }
  return got == want;
}

/* 收到查詢就回一筆 A 記錄 (unicast 回覆) */
static void mdnsLoop() {
  int avail = gMdnsUdp.parsePacket();
  if (avail < 12) return;

  uint8_t buf[256];
  int len = gMdnsUdp.read(buf, sizeof(buf));
  if (len < 12) return;
  if (buf[2] & 0x80) return;                       /* 略過回應封包 */

  uint16_t qd = ((uint16_t)buf[4] << 8) | buf[5];
  if (qd == 0) return;

  size_t off = 12, nameStart = 0, nameLen = 0;
  bool hit = false;
  for (uint16_t q = 0; q < qd && off < (size_t)len; q++) {
    nameStart = off;
    while (off < (size_t)len) {
      uint8_t l = buf[off];
      if (l == 0) { off++; break; }
      if (l & 0xC0) { off += 2; break; }
      off += 1 + l;
    }
    if (off + 4 > (size_t)len) return;
    uint16_t qtype = ((uint16_t)buf[off] << 8) | buf[off + 1];
    off += 4;                                      /* QTYPE + QCLASS */
    nameLen = off - 4 - nameStart;
    if (q == 0 && (qtype == 1 || qtype == 255) &&
        mdnsNameMatch(buf + nameStart, nameLen)) { hit = true; break; }
  }
  if (!hit || nameLen == 0 || nameLen > 200) return;

  /* 依查詢來源選擇要回報的 IP: SoftAP 網段回 AP IP, 其他回 STA IP */
  IPAddress ip  = WiFi.localIP();
  IPAddress apIp = WiFi.softAPIP();
  IPAddress src = gMdnsUdp.remoteIP();
  if (apIp[0] && src[0] == apIp[0] && src[1] == apIp[1] && src[2] == apIp[2]) ip = apIp;
  if (ip == IPAddress(0, 0, 0, 0)) return;

  uint8_t r[256];
  size_t o = 0;
  r[o++] = 0; r[o++] = 0;                          /* ID */
  r[o++] = 0x84; r[o++] = 0x00;                    /* QR + AA, NOERROR */
  r[o++] = 0; r[o++] = 1;                          /* QDCOUNT */
  r[o++] = 0; r[o++] = 1;                          /* ANCOUNT */
  r[o++] = 0; r[o++] = 0; r[o++] = 0; r[o++] = 0;  /* NS / AR */
  memcpy(r + o, buf + nameStart, nameLen); o += nameLen;
  r[o++] = 0; r[o++] = 1;                          /* TYPE A */
  r[o++] = 0; r[o++] = 1;                          /* CLASS IN */
  r[o++] = 0xC0; r[o++] = 0x0C;                    /* 名稱指標 -> 問題區 */
  r[o++] = 0; r[o++] = 1;                          /* TYPE A */
  r[o++] = 0x80; r[o++] = 1;                       /* CLASS IN + cache flush */
  r[o++] = 0; r[o++] = 0; r[o++] = 0; r[o++] = 120;/* TTL 120s */
  r[o++] = 0; r[o++] = 4;                          /* RDLENGTH */
  r[o++] = ip[0]; r[o++] = ip[1]; r[o++] = ip[2]; r[o++] = ip[3];

  gMdnsUdp.beginPacket(gMdnsUdp.remoteIP(), MDNS_PORT_ID);
  gMdnsUdp.write(r, o);
  gMdnsUdp.endPacket();
}

#endif
