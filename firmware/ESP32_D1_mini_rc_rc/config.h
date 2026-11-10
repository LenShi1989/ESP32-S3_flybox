#ifndef RC_CONFIG_H
#define RC_CONFIG_H

/* =============================================================
 * ESP32 D1 mini 遙控器 (RC) 設定 — 純 2.4G WiFi 版
 * -------------------------------------------------------------
 * 版本 (Version): 2.1.3   (見 FW_VERSION)
 * 名稱 (Name):    ESP32 D1 mini RC
 * -------------------------------------------------------------
 * 功能:
 *   - 只用 2.4G WiFi: WebSocket 用戶端連線 WT32-ETH01 接收機
 *   - 硬體遙控: 四軸搖桿 (ADC) + SW1~SW4 + VRC1 旋鈕
 *   - 軟體遙控: 網頁模擬搖桿 (WiFi)
 *   - VRC1 切換 SSD1306 OLED 顯示頁面
 *   - SPIFFS: 所有設定儲存在 /config.json
 *   - 開機流程: VRC1 選無人機 ID -> SW1 確認/按住校正 -> 放開結束
 *               按住 SW2 開機 = 沿用已存 ID/校正值
 * ============================================================= */

#include <Arduino.h>

#define FW_NAME "ESP32 D1 mini RC"
#define FW_VERSION "2.1.3"

/* ---------- OLED (SSD1306 I2C, 沿用測試碼接線) ---------- */
#define OLED_SDA 17
#define OLED_SCL 16
#define OLED_ADDR 0x3C
#define OLED_W 128
#define OLED_H 64
#define OLED_REFRESH_MS 200

/* ---------- 類比搖桿 (僅使用 ADC1；WiFi 開啟時 ADC2 無法讀取) ---------- */
#define PIN_THROTTLE 39   /* 油門 (VTX) */
#define PIN_YAW      35   /* 偏航 (VTY) */
#define PIN_PITCH    33   /* 俯仰 (VPIT) */
#define PIN_ROLL     34   /* 橫滾 (VROL) */
/* VRC1 原為 GPIO13 (ADC2)，改用 WiFi 後改接 GPIO36 (ADC1/VP) */
#define PIN_VRC1     36   /* 旋鈕 1: 選無人機 ID / 切換 OLED 頁面 */
/* VRC2/VRC4 為 ADC2，WiFi 模式下不可用；保留定義僅供接線參考 */
#define PIN_VRC2     25
#define PIN_VRC3     32
#define PIN_VRC4     12

/* ---------- 開關 (INPUT_PULLUP, LOW = ON) ---------- */
#define PIN_SW1 10
#define PIN_SW2 18
#define PIN_SW3 19
#define PIN_SW4 23

#define PIN_LED 2         /* 板載 LED, HIGH = 亮 */

/* ---------- 搖桿對應 ---------- */
#define STICK_RANGE      200.0f   /* 角速率滿刻度 deg/s */
#define STICK_DEADBAND   0.05f    /* 置中死區 (正規化 0~0.5) */
#define THR_MIN_US       1000
#define THR_MAX_US       2000
#define ADC_SAMPLES      4        /* 每次讀值平均次數 */
/* PITCH 搖桿與實機方向相反: 推桿向前 (ADC 往低) 應為正, 故對應後取負號 */
#define STICK_REVERSE_PITCH 1

/* ---------- 遙控命令傳送 ---------- */
#define LINK_SEND_MS     50       /* 20Hz 連續傳送硬體搖桿命令 */

/* ---------- OLED 頁面 (VRC1 4 段切換) ---------- */
#define OLED_PAGES       4
#define VRC1_HYST        80       /* 頁面邊界遲滯 (ADC counts) */

/* ---------- 控制來源 ---------- */
#define CTRL_AUTO     0   /* 自動: 軟體搖桿優先, 實體搖桿移動才交還硬體 */
#define CTRL_HARDWARE 1   /* 只送硬體搖桿 */
#define CTRL_SOFTWARE 2   /* 只由網頁軟體搖桿控制 */

/* ---------- Web / AP ---------- */
#define WEB_PORT 80
/* SoftAP 一律啟動, 與 STA 共存; SSID = 前綴 + MAC 後 4 碼 */
#define AP_SSID_PREFIX "KKBOX-RC-"
#define AP_PASS "12345678"
/* RC 的 SoftAP 使用 192.168.10.x, 與接收機 (192.168.4.x) 區隔 */
#define AP_IP_ADDR "192.168.10.1"
#define SESSION_MAX 8

/* ---------- 預設帳密 ---------- */
#define DEF_USER "admin"
#define DEF_PASS "admin"

/* ---------- 預設接收機連線 ---------- */
#define DEF_RX_PORT 80

/* ---------- 預設 PID (與 kkbox/config.h 一致) ---------- */
/* roll/pitch KI 即自動配平積分 (auto-trim): 搖桿置中時緩慢學習配平量 */
#define DEF_ROLL_KP   3.0f
#define DEF_ROLL_KI   0.08f
#define DEF_ROLL_KD   8.0f
#define DEF_PITCH_KP  3.0f
#define DEF_PITCH_KI  0.08f
#define DEF_PITCH_KD  8.0f
#define DEF_YAW_KP    4.0f
#define DEF_YAW_KI    0.5f
#define DEF_YAW_KD    0.0f

/* ---------- 預設校正值 (尚未校正時仍可開機顯示) ---------- */
#define DEF_ADC_MIN 800
#define DEF_ADC_MAX 3200
/* 尚未校正的軸: 以開機第一次讀值為中央, 至少給這個半行程 (±) */
#define AUTO_SPAN_HALF ((DEF_ADC_MAX - DEF_ADC_MIN) / 2)

/* KKBOX motor[] 順序固定為 M1..M8：M1=FL(CCW), M2=FR(CW),
 * M3=RR(CCW), M4=RL(CW); M5..M8 為各位置 X8 下槳。 */

#endif