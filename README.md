# ESP32-S3_flybox
自製飛控 — M5Stack AtomS3R-Ext (ESP32-S3-PICO-1-N8R8) + BMI270

## 系統架構

```
ESP32 D1 mini 遙控器 ──2.4G WiFi/WebSocket──> WT32-ETH01 接收機
                                                     │
                                             RS-232 9600 8N1
                                                     │
                                              ESP32-S3 飛控板
                                              (BMI270 + PID)
                                                     │
                                              ESC x4 / x8 (50Hz PWM)
```

- 飛控板以 PID (三軸 rate) 讀 BMI270 陀螺儀做自主平衡。
- 命令與遙測都是**單行 JSON**，以 `\n` 結尾，格式與接收機、遙控器既有協定相同。
- 接收機把飛控送出的每一行原樣廣播給 WebSocket 用戶端，因此不用改接收機。

## 目錄

```
firmware/ESP32_S3_flybox/
  ESP32_S3_flybox.ino   主程式：控制迴路、命令解析、遙測
  config.h              腳位、PID、ESC、濾波等所有設定
  imu.h                 BMI270 讀取、零點校正、卡爾曼濾波
  pid.h                 PID (微分作用於量測值、積分限幅)
  mixer.h               Quad-X / X8 同軸混控
  motors.h              8 通道 LEDC ESC 輸出
  link.h                UART1 單行 JSON 收發
  jsonutil.h            輕量 JSON 解析
firmware/WT32_ETH01_receiver/
  WT32_ETH01_receiver.ino  接收機主程式 (RJ45 + WiFi + WebSocket 橋接)
  config.h                 網路 / UART / 帳密設定
  kkbox.h                  飛控 UART 橋接 (Serial2, 9600)
  settings.h               NVS 設定、無人機 ID
  web.h                    網頁路由、WebSocket、OTA
  data/                    SPIFFS 網頁 (index.html / app.js / style.css)
firmware/ESP32_D1_mini_rc_rc/
  ESP32_D1_mini_rc_rc.ino  遙控器主程式 (WiFi WebSocket 用戶端)
  config.h                 腳位、搖桿、預設 PID、AP 設定
  settings.h               /config.json 設定、FlyID、搖桿校正
  input.h                  硬體搖桿 ADC 讀取與校正、SW1~SW4
  display.h                SSD1306 OLED 4 頁顯示
  link.h                   與接收機的 WebSocket 連線、送命令
  telem.h                  遙測解析
  web.h                    網頁路由、PID / 模擬搖桿 / 馬達校正 API
  data/                    SPIFFS 網頁 (index.html / app.js / style.css / config.json)
docs/                   模組規格書、系統方塊圖、調機測試清單
```

## 開發環境

- Arduino IDE 1.8.19
- ESP32 Arduino core 2.x 或 3.x（**兩者都支援**，LEDC API 已做版本切換）
- 函式庫：**M5Unified**（>= 0.2.17，含 BMI270 驅動）與 **M5GFX**（M5Unified 相依）
- 開發板選 **M5AtomS3R**；若用 ESP32 官方 core，選 **ESP32S3 Dev Module**
  並開啟 PSRAM（OPI PSRAM）；USB CDC On Boot 建議開啟

### 建置 / 上傳

1. Arduino IDE 開啟 `firmware/ESP32_S3_flybox/ESP32_S3_flybox.ino`
2. 工具 → 開發板 / 序列埠 設定好
3. 驗證後上傳；USB 序列埠 115200 可看除錯訊息

## 接腳（`config.h`）

| 功能 | 預設 GPIO | 說明 |
| --- | --- | --- |
| RS-232 TX | G9 | 飛控 TX → 接收機 RX |
| RS-232 RX | G10 | 飛控 RX ← 接收機 TX |
| M1 FL / M2 FR / M3 RR / M4 RL | G5 / G6 / G7 / G8 | 上槳 |
| M5 FL / M6 FR / M7 RR / M8 RL | G38 / G39 / G3 / G4 | X8 下槳 |
| BMI270 | SCL=G0, SDA=G45 | 板載，由 M5Unified 初始化 |

> **注意**：本 repo 的 `docs/` 只有模組規格書與方塊圖，沒有自製飛控板的完整原理圖，
> 無法從文件推得唯一腳位。上表是**預設值**，請依實際拉線修改 `config.h`。
> RS-232 請確認是否外接 MAX3232 之類的收發器，勿直接把 RS-232 準位接到 GPIO。

馬達位置 (Quad-X，機頭朝上)：

```
          機頭 ↑
    M1(FL) CCW     M2(FR) CW
            \   /
             \ /
              X
             / \
            /   \
    M4(RL) CW      M3(RR) CCW
          機尾 ↓
```

X8 同軸：M5=FL、M6=FR、M7=RR、M8=RL，同臂下槳轉向與上槳相反。

## 序列命令

送到飛控（接收機原樣轉送），每行一筆 JSON：

| 命令 | 範例 | 說明 |
| --- | --- | --- |
| ping | `{"cmd":"ping"}` | 連線測試 |
| arm | `{"cmd":"arm"}` | 解鎖（油門歸 1000us，IMU 無回應時拒絕） |
| disarm | `{"cmd":"disarm"}` | 上鎖 |
| throttle | `{"cmd":"throttle","value":1500}` | 油門 1000~2000us |
| setpoints | `{"cmd":"setpoints","roll":0,"pitch":0,"yaw":0}` | 角速度設定點 (deg/s) |
| hold | `{"cmd":"hold","on":1}` | 定高（保持當前油門） |
| motors | `{"cmd":"motors","count":8}` | 切換 4 / 8 槳 |
| pid | `{"cmd":"pid","axis":"roll","kp":3,"ki":0.08,"kd":8}` | 線上調 PID |
| motmin | `{"cmd":"motmin","index":1,"value":1050}` | 最低啟動 PWM（index 0=全部，存 NVS） |
| nopid | `{"cmd":"nopid","on":1}` | ESC 行程校正：略過 PID |
| calibrate | `{"cmd":"calibrate"}` | 重新校正陀螺零點（解鎖中拒絕） |
| telemetry | `{"cmd":"telemetry","value":200}` | 遙測週期 ms（0=關閉） |
| status | `{"cmd":"status"}` | 回報版本、油門、PID 以外的設定 |
| led | `{"cmd":"led","mode":"blink"}` | 板載 RGB LED：on / off / blink |

### 回應與遙測

```
{"ack":"arm"}
{"evt":"ready","fw":"ESP32-S3 Flybox","ver":"1.0.0"}
{"evt":"failsafe"}
{"err":"bad cmd"}
{"t":12345,"armed":1,"mcount":4,"hold":0,"gyro":{"roll":0,"pitch":0,"yaw":0},
 "set":{"roll":0,"pitch":0,"yaw":0,"thr":1500},"motor":[1520,1480,1480,1520]}
```

- 遙測預設每 200ms 一筆（9600 下約 160 bytes/筆，保留約 2 成餘裕）。
- 超過 `FAILSAFE_MS`（2000ms）沒收到任何命令會自動上鎖並送出 `failsafe`。
- 解鎖後 LED 快閃（綠），未解鎖慢閃（藍）。

## 接收機（WT32-ETH01）

`firmware/WT32_ETH01_receiver/` 是飛控與遙控器之間的橋接：

- RJ45 (LAN8720A) 有線網路 + WiFi STA + SoftAP 可同時運作。
- 網頁（SPIFFS）顯示飛控遙測：**陀螺儀 Roll / Pitch / Yaw 數值與長條圖**、馬達輸出、解鎖狀態。
- **無人機 ID 設定**（0–255，存 NVS），顯示於飛行狀態頁；`GET/POST /api/id`。
- 飛控送出的每一行 JSON 原樣經 WebSocket 廣播，遙控器與網頁看到的是同一份資料。
- 網頁另含網路設定、馬達模式、使用者帳密、SPIFFS 檔案瀏覽與 OTA。

### 接線（與飛控板對接）

| 接收機 | 飛控板 |
| --- | --- |
| Serial2 RX = G5 | UART1 TX = G9 |
| Serial2 TX = G17 | UART1 RX = G10 |
| GND | GND |

9600 8N1，交叉對接；若中間走真正的 RS-232 準位，兩端都要加收發器（如 MAX3232）。
接收機的 G16 是 PHY 電源，不可他用。

### 建置 / 上傳

1. 開發板選 **WT32-ETH01**（ESP32 core 的 `wt32-eth01`）
2. 函式庫：**ESP Async WebServer**（ESP32Async）、**Async TCP**、**ElegantOTA**；
   `build_opt.h` 已定義 `ELEGANTOTA_USE_ASYNC_WEBSERVER=1`
3. 先上傳程式，再把 `data/` 以 SPIFFS 映像方式上傳（Arduino IDE 需安裝
   ESP32 Sketch Data Upload 外掛，或使用 `mklittlefs` / `arduino-cli uploadfs`）
4. 序列埠 115200 可看開機與網路訊息

### 使用

- 預設帳密 `admin` / `admin`（`config.h` 的 `DEF_USER` / `DEF_PASS`，登入後可改）
- SoftAP：`KKBOX-Receiver-XXXX`（XXXX = MAC 後四碼），密碼 `12345678`
- 網頁：`http://<接收機 IP>/`，WebSocket：`/ws`（需 `?token=` 或登入 cookie）
- 遙控器用的裝置 token 在「使用者設定」頁可重新產生
- 常用 API：`/api/status`、`/api/telemetry`、`/api/id`、`/api/command`（POST 單行 JSON 轉送飛控）

## 遙控器（ESP32 D1 mini RC）

`firmware/ESP32_D1_mini_rc_rc/` 是操作端：只用 2.4GHz WiFi，以 WebSocket 用戶端連接收機，
把命令轉給飛控板。

- **虛擬控制搖桿**：網頁「模擬搖桿」頁，觸控/滑鼠拖曳即時送出 `setpoints` 與 `throttle`。
- **PID 參數校正**：網頁「PID 設定」頁，roll / pitch / yaw 各自 Kp / Ki / Kd，
  下傳 `{"cmd":"pid","axis":...}`。
- **PWM 馬達最低啟動數值設定**：網頁「馬達校正」頁，M1–M8 各自設定並下傳 `motmin`。
- 另有硬體遙控（四軸搖桿 ADC1 + SW1~SW4 + VRC1 旋鈕）、SSD1306 OLED 4 頁顯示、
  ESC 行程校正（`nopid`）與 OTA。

### 接腳（`config.h`）

| 功能 | GPIO |
| --- | --- |
| 油門 / 偏航 / 俯仰 / 橫滾 | G39 / G35 / G33 / G34（ADC1） |
| VRC1（選 FlyID / 切換 OLED 頁） | G36（ADC1） |
| SW1 / SW2 / SW3(ARM) / SW4(DISARM) | G10 / G18 / G19 / G23（LOW = ON） |
| OLED SDA / SCL（SSD1306 0x3C） | G17 / G16 |
| 板載 LED | G2 |

> WiFi 開啟時 ADC2 不可用，搖桿與 VRC1 一律接 ADC1；不要接回 G13（ADC2）。

### 開機流程

1. 轉 VRC1 選無人機 ID，按 SW1 確認
2. 按住 SW1 期間持續校正四軸搖桿 min/max，放開結束
3. 按住 SW2 開機 = 跳過上述流程（沿用已存的 ID 與校正值）

### 建置 / 上傳

1. 開發板選 **WEMOS D1 MINI ESP32**（`d1_mini32`）
2. 函式庫：**Adafruit GFX**、**Adafruit SSD1306**、**ArduinoJson**、
   **ESP Async WebServer**、**Async TCP**、**ElegantOTA**、**WebSockets**（Links2004）
3. 上傳程式後，把 `data/` 以 SPIFFS 映像上傳（`config.json` 為預設設定）
4. 網頁：`http://192.168.10.1/`（SoftAP `KKBOX-RC-XXXX`，密碼 `12345678`），帳密 `admin` / `admin`
5. 先在網頁「網路設定」填入接收機 IP / token，之後才會建立 WebSocket 連線

> Flash 使用率約 99%（1.30MB / 1.31MB），再加功能要先換分割區配置。

## 控制參數

- 控制迴路 `CTRL_HZ = 100`，與舊 ATmega168 飛控相同，既有 PID 預設值可直接沿用。
- PID 預設：roll/pitch `3.0 / 0.08 / 8.0`、yaw `4.0 / 0.5 / 0.0`；
  輸出限幅 100us、積分限幅 50us。
- roll/pitch 的 KI 兼作自動配平：搖桿置中（< 10 deg/s）才累積積分。
- 陀螺濾波預設為卡爾曼（Q=2.0 / R=2.0），可改用一階低通 `GYRO_LPF`。
- 若馬達轉向相反，改 `IMU_REVERSE_ROLL/PITCH/YAW` 或調整 ESC 設定。

## 調機

首次上機請照 [`docs/調機測試清單.md`](docs/調機測試清單.md) 逐項確認（拆槳、馬達序號、
混控方向、PID 調參、最低啟動 PWM、failsafe）。

## 安全提醒

- 首次測試請拆槳；確認馬達序號、轉向與混控方向正確再裝槳。
- `arm` 後油門即時生效，請先把機體固定好。
- ESC 行程校正（`nopid`）期間不套用最低啟動 PWM，可回到 1000us。
