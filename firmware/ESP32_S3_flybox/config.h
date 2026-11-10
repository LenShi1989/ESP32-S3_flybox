#ifndef FLYBOX_CONFIG_H
#define FLYBOX_CONFIG_H

#include <Arduino.h>

#define FW_NAME    "ESP32-S3 Flybox"
#define FW_VERSION "1.0.0"

/* =============================================================
 * ESP32-S3 飛控板 (M5Stack AtomS3R-Ext) 設定
 * -------------------------------------------------------------
 * 硬體:
 *   - MCU : ESP32-S3-PICO-1-N8R8 (8MB Flash / 8MB PSRAM)
 *   - IMU : BMI270 (I2C, SCL=GPIO0 / SDA=GPIO45, 位址 0x68)
 *           由 M5Unified 初始化, 不需在此設定接腳
 *   - 輸出: 4/8 顆 ESC (LEDC PWM 50Hz, 1000~2000us)
 *   - 通訊: RS-232 (UART1, 9600 8N1) <-> WT32-ETH01 接收機
 *
 * 重要: 下列腳位為預設值, 請依實際飛控板拉線修改。
 *       本 repo 的 docs 只有模組規格書與方塊圖, 沒有自製板的
 *       完整原理圖, 因此無法從文件推得唯一的腳位對應。
 * ============================================================= */

/* ---------- RS-232 與 WT32-ETH01 接收機通訊 ----------
 * 接收機端固定 9600 8N1 (見 WT32_ETH01_receiver/config.h 的 KK_BAUD),
 * 每行一筆 JSON, 以 '\n' 結尾。ESP32-S3 使用硬體 UART1,
 * 任意 GPIO 皆可對應, 因此鮑率可穩定跑 9600。
 */
#define RS232_TX_PIN 9    /* 飛控 TX -> 接收機 RX (Proto Board) */
#define RS232_RX_PIN 10   /* 飛控 RX <- 接收機 TX (Proto Board) */
#define RS232_BAUD   9600

/* ---------- 馬達 / ESC 輸出 ----------
 * Quad-X 位置 (機頭朝上):
 *
 *          機頭 ^
 *    M1(FL) CCW     M2(FR) CW
 *            \   /
 *             \ /
 *              X
 *             / \
 *            /   \
 *    M4(RL) CW      M3(RR) CCW
 *          機尾 v
 *
 * 8 槳 X8 同軸: M5=FL, M6=FR, M7=RR, M8=RL (同臂下槳, 轉向與上槳相反)
 * 腳位預設: M1..M4 = G5/G6/G7/G8, M5..M8 = G38/G39/G3/G4
 */
#define MOTOR_COUNT 4        /* 開機預設馬達數 (4 或 8) */
#define MAX_MOTORS  8

#define MOTOR1_PIN  5    /* FL, CCW */
#define MOTOR2_PIN  6    /* FR, CW  */
#define MOTOR3_PIN  7    /* RR, CCW */
#define MOTOR4_PIN  8    /* RL, CW  */
#define MOTOR5_PIN 38    /* FL 下槳, CW  */
#define MOTOR6_PIN 39    /* FR 下槳, CCW */
#define MOTOR7_PIN  3    /* RR 下槳, CW  */
#define MOTOR8_PIN  4    /* RL 下槳, CCW */

#define ESC_MIN       1000
#define ESC_MAX       2000
#define ESC_PWM_HZ    50
#define ESC_PWM_BITS  16     /* 50Hz 下 16bit 解析度: 1 LSB 約 0.3us */
#define ESC_PWM_PERIOD 20000.0f

/* ---------- 各馬達最低啟動 PWM (us) ----------
 * 每顆 ESC 開始轉動的油門下限不同; 解鎖且油門高於 ESC_MIN 時,
 * 各馬達輸出不會低於此值。預設 1000 (不補償), 可用
 * {"cmd":"motmin","index":1,"value":1050} 調整, index 0 = 全部;
 * 數值存於 NVS, 重開機仍保留。ESC 行程校正 (nopid) 期間不套用。
 */
#define MOTOR_MIN_US  1000
#define MOTOR_MIN_MIN 1000
#define MOTOR_MIN_MAX 1500

/* ---------- 機架設定 ---------- */
#define KK_FRAME 0   /* 0 = Quad-X, 1 = Quad-+ */

/* ---------- 控制回路 ---------- */
#define CTRL_HZ        100      /* PID / 混控 / PWM 更新頻率 */
#define CTRL_PERIOD_US (1000000UL / CTRL_HZ)
#define FAILSAFE_MS    2000     /* 超過此時間沒收到命令即自動上鎖 */

/* ---------- IMU 校正與濾波 ---------- */
#define GYRO_CAL_SAMPLES 400

/* 軸向對應: BMI270 的 x/y/z 直接對應 roll/pitch/yaw。
 * 若模組安裝方向使某軸相反, 把對應的 REVERSE 設為 1。 */
#define IMU_REVERSE_ROLL  0
#define IMU_REVERSE_PITCH 0
#define IMU_REVERSE_YAW   0

/* 一維卡爾曼濾波 (隨機漫步模型), 對三軸角速度各做一次:
 *   預測: x = x, P = P + Q
 *   更新: K = P / (P + R), x += K * (z - x), P = (1 - K) * P
 * 100Hz 下 Q=2.0 / R=2.0 的穩態增益約 0.62 (時間常數約 10ms),
 * 與舊板 GYRO_LPF=0.6 手感接近, 因此既有 PID 預設值可直接沿用。
 */
#define GYRO_KALMAN_ENABLE 1
#define GYRO_KALMAN_Q      2.0f
#define GYRO_KALMAN_R      2.0f
#define GYRO_KALMAN_P_INIT 1.0f
#define GYRO_LPF           0.6f   /* 僅 GYRO_KALMAN_ENABLE=0 時使用 */

/* ---------- PID 預設 (與 kkbox / 遙控器預設值一致) ---------- */
#define PID_OUT_LIMIT 100.0f  /* 每軸修正量上限 (us) */
#define PID_I_LIMIT   50.0f
#define PID_ROLL_KP   3.0f
#define PID_ROLL_KI   0.08f   /* 自動配平 (auto-trim) */
#define PID_ROLL_KD   8.0f
#define PID_PITCH_KP  3.0f
#define PID_PITCH_KI  0.08f   /* 自動配平 (auto-trim) */
#define PID_PITCH_KD  8.0f
#define PID_YAW_KP    4.0f
#define PID_YAW_KI    0.5f
#define PID_YAW_KD    0.0f

/* ---------- 自動配平 (auto-trim) ----------
 * roll/pitch 的積分項只在搖桿置中時累積, 依陀螺殘餘轉速緩慢學習配平量,
 * 抵銷機體/ESC 的固定偏壓; 搖桿離開中央即凍結 (保留已學習值)。
 */
#define TRIM_DEADBAND 10.0f   /* 搖桿命令絕對值小於此值視為置中 (deg/s) */

/* ---------- 序列緩衝 / 遙測 ---------- */
#define LINE_BUF_SIZE   192
/* 9600 下一幀約 160 bytes 約 167ms 傳輸時間, 200ms 留約 2 成餘裕。
 * 0 = 關閉遙測。 */
#define TEL_INTERVAL_MS 200

/* ---------- 板載 RGB LED (AtomS3R, 由 M5Unified 控制) ---------- */
#define USE_STATUS_LED 1

#endif
