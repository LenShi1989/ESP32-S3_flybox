#ifndef FLYBOX_CONFIG_H
#define FLYBOX_CONFIG_H

#include <Arduino.h>

#define FW_NAME    "ESP32-S3 Flybox"
#define FW_VERSION "2.3.3"

/* =============================================================
 * ESP32-S3 飛控板 (M5Stack AtomS3R-Ext) 設定
 * -------------------------------------------------------------
 * 硬體:
 *   - MCU : ESP32-S3-PICO-1-N8R8 (8MB Flash / 8MB PSRAM)
 *   - IMU : BMI270 六軸 (I2C 0x68, SCL=G0 / SDA=G45), 由 M5Unified 初始化
 *   - MAG : BMM150 三軸地磁, 掛在 BMI270 的 Sensor Hub (輔助 I2C)
 *           由 M5Unified 的 BMI270 驅動一併讀取, 不需另外接線
 *   - 輸出: 4/8 顆 ESC (LEDC PWM 50Hz, 1000~2000us)
 *   - 狀態燈: 板載 RGB + 外接 G39 GPIO LED (遙控器 led 命令控制)
 *   - 通訊: RS-232 (UART1, 115200 8N1) <-> WT32-ETH01 接收機
 *
 * 機體方向:
 *   USB Type-C 接口方向 = 機頭 (+X 前進方向)。
 *   開機預設 4 旋翼 Quad-X (X4), 馬達位置見下方。
 * ============================================================= */

/* ---------- RS-232 與 WT32-ETH01 接收機通訊 ----------
 * 接收機端固定 115200 8N1 (見 WT32_ETH01_receiver/config.h 的 KK_BAUD)。
 * 每行一筆 JSON, 以 '\n' 結尾。ESP32-S3 使用硬體 UART1, 接腳可任意對應。
 * 115200 才能容納含六軸姿態 + 三軸地磁的遙測封包。
 */
#define RS232_TX_PIN 1    /* 飛控 TX -> 接收機 RX (PORT.CUSTOM G1) */
#define RS232_RX_PIN 2    /* 飛控 RX <- 接收機 TX (PORT.CUSTOM G2) */
#define RS232_BAUD   115200

/* ---------- 馬達 / ESC 輸出 ----------
 * Quad-X 位置 (Type-C 機頭朝上):
 *
 *        Type-C / 機頭 ^
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
 */
#define MOTOR_COUNT 4        /* 開機預設: 4 旋翼 Quad-X (X4) */
#define MAX_MOTORS  8

#define MOTOR1_PIN  5    /* FL, CCW */
#define MOTOR2_PIN  6    /* FR, CW  */
#define MOTOR3_PIN  7    /* RR, CCW */
#define MOTOR4_PIN  8    /* RL, CW  */
#define MOTOR5_PIN 38    /* FL 下槳, CW  */
#define MOTOR6_PIN 39    /* FR 下槳, CCW (預設改為狀態燈, 見檔尾) */
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
#define KK_FRAME 0   /* 0 = Quad-X (預設 X4), 1 = Quad-+ */

/* ---------- 控制回路 ---------- */
#define CTRL_HZ        100      /* PID / 混控 / PWM 更新頻率 */
#define CTRL_PERIOD_US (1000000UL / CTRL_HZ)
#define CTRL_MAX_DT    0.05f    /* 單次迴圈最大 dt, 避免排程延遲造成積分爆衝 */
#define FAILSAFE_MS    2000     /* 超過此時間沒收到命令即自動上鎖 */

/* ---------- IMU 校正與濾波 ---------- */
#define GYRO_CAL_SAMPLES 400

/* BMM150 校正: 開始後請在時間內把機體各方向緩慢旋轉一圈以上。
 * 每軸變化量太小 (例如只繞單軸轉) 視為無效, 保留舊校正值。 */
#define BMM150_CAL_MS        20000
#define MAG_CAL_MIN_SPAN_UT  10.0f

/* ---------- 機體軸向對應 (Type-C = 機頭) ----------
 * M5Unified 已對 AtomS3R-Ext 做固定軸向校正, getImuData() 的
 * accel / gyro 為校正後的板載座標 (M5Stack 官方 "IMU 3-Axis
 * Orientation Diagram" 即為此座標):
 *   板載 +X = 機頭 (USB Type-C 方向), +Y = 左舷, +Z = 板面朝上
 * 注意: 地磁 BMM150 的校正後座標與 accel 不同 (BMM150 在板上
 * 旋轉了 90 度), M5Unified 只做了 X/Z 反轉, 因此底下地磁軸向
 * 需另外對應: mX = -板載Y, mY = -板載X, mZ = -板載Z。
 *
 * 本飛控採用 NED 機體座標 (右手系):
 *   +X = 前進 (USB Type-C), +Y = 右, +Z = 下
 * 因此 (accel / gyro):
 *   前 = +板載X, 右 = -板載Y, 下 = -板載Z
 *
 * 角速度 (deg/s, 右手定則):
 *   roll  繞 +X: 右傾 (右翼下壓) 為正
 *   pitch 繞 +Y: 機頭抬起 為正
 *   yaw   繞 +Z: 機頭向右 (由上往下看順時針) 為正
 * 若實機轉動方向相反, 只改對應的 SIGN, 不必改 PID 或混控。
 * 校正方式: 見 docs/調機測試清單.md 的 "IMU 方向" 一節。
 */
#define IMU_GYRO_ROLL_AXIS   0
#define IMU_GYRO_ROLL_SIGN  ( 1.0f)
#define IMU_GYRO_PITCH_AXIS  1
#define IMU_GYRO_PITCH_SIGN  (-1.0f)
#define IMU_GYRO_YAW_AXIS    2
#define IMU_GYRO_YAW_SIGN    (-1.0f)

/* 加速度計機體軸 (單位 g): +X 前, +Y 右, +Z 下 */
#define IMU_ACC_FORWARD_AXIS  0
#define IMU_ACC_FORWARD_SIGN ( 1.0f)
#define IMU_ACC_RIGHT_AXIS    1
#define IMU_ACC_RIGHT_SIGN   (-1.0f)
#define IMU_ACC_DOWN_AXIS     2
#define IMU_ACC_DOWN_SIGN    (-1.0f)

/* 地磁計機體軸 (單位 uT): 前 = -mY, 右 = +mX, 下 = +mZ */
#define IMU_MAG_FORWARD_AXIS  1
#define IMU_MAG_FORWARD_SIGN (-1.0f)
#define IMU_MAG_RIGHT_AXIS    0
#define IMU_MAG_RIGHT_SIGN   ( 1.0f)
#define IMU_MAG_DOWN_AXIS     2
#define IMU_MAG_DOWN_SIGN    ( 1.0f)

/* ---------- 姿態融合 (六軸 + 地磁) ----------
 * 陀螺儀積分 -> 加速度計修正 roll/pitch (互補濾波),
 * 地磁計修正 yaw (傾斜補償方位角)。時間常數越大越平滑、反應越慢。
 */
#define ATT_TAU_S          0.5f   /* roll/pitch 加速度修正時間常數 (秒) */
#define ATT_MAG_TAU_S      2.0f   /* yaw 地磁修正時間常數 (秒) */
#define ATT_ACC_MIN_G      0.5f   /* 加速度量低於此值視為自由落體, 不修正 */
#define ATT_ACC_MAX_G      1.5f   /* 加速度量高於此值視為劇烈運動, 不修正 */
#define MAG_DECLINATION_DEG 0.0f  /* 磁偏角 (東正西負), 台灣約 -2 ~ -4 度 */
#define MAG_HEADING_SIGN    1.0f  /* 方位角相反時改為 -1 */

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
#define LINE_BUF_SIZE   256
/* 115200 下含姿態與地磁的遙測約 300~350 bytes, 每 200ms 一筆仍有大量餘裕。
 * 0 = 關閉遙測。 */
#define TEL_INTERVAL_MS 200

/* ---------- 板載 RGB LED (AtomS3R, 由 M5Unified 控制) ---------- */
#define USE_STATUS_LED 1

/* ---------- 外接 GPIO 狀態燈 ----------
 * G39 外接 LED (G39 -> 限流電阻 -> LED -> GND, 高電位亮),
 * 與板載 RGB 同步, 由遙控器「模擬搖桿」頁或序列命令控制:
 *   {"cmd":"led","mode":"on"|"off"|"blink"}
 * 未解鎖慢閃, 已解鎖快閃 (與板載 RGB 相同)。
 *
 * 注意: G39 原本是 X8 的 M6 (FR 下槳)。啟用狀態燈後 M6 不再輸出,
 * 因此要求 8 槳時會回 {"err":"motor6 led"} 並維持原槳數。
 * 若要把 M6 改到其他腳位, 修改 MOTOR6_PIN 使兩者不同即可恢復 X8。
 */
#define USE_GPIO_STATUS_LED    1
#define STATUS_LED_PIN         39
#define STATUS_LED_ACTIVE_HIGH 1

#endif
