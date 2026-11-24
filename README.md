# ESP32-S3_flybox

自製飛控 — M5Stack AtomS3R-Ext (ESP32-S3-PICO-1-N8R8) + BMI270 六軸 + BMM150 地磁

## 系統架構

```
ESP32 D1 mini 遙控器 ──2.4G WiFi/WebSocket──> WT32-ETH01 接收機
                                             (GPS x2 + 雷射測距)
                                                     │
                                             RS-232 115200 8N1
                                                     │
                                              ESP32-S3 飛控板
                                              (BMI270 + PID)
                                                     │
                                              ESC x4 / x8 (50Hz PWM)
```

- 飛控板以 PID (三軸 rate) 讀 BMI270 陀螺儀做自主平衡。
- 機頭方向 = USB Type-C 接口方向；開機預設 4 槳 Quad-X (X4)。
- BMI270 加速度計 + 陀螺儀做六軸互補濾波姿態，BMM150 地磁 (經 BMI270 Sensor Hub) 修正 yaw。
- 接收機另接兩顆 GPS 與一顆雷射測距，資料不經飛控，由接收機網頁側欄即時顯示（`/api/sensors`）。
- 命令與遙測都是**單行 JSON**，以 `\n` 結尾，格式與接收機、遙控器既有協定相同。
- 接收機把飛控送出的每一行原樣廣播給 WebSocket 用戶端，新增欄位只要網頁端解析，橋接程式不必改動。

## 目錄

```
├── docs/                              # 規格書、接腳圖、方塊圖、調機測試清單
│   ├── 調機測試清單.md
│   ├── 飛控系統方塊圖.pptx
│   ├── ESP32-D1-MINI/                 # D1 mini 接腳圖 / 規格書
│   ├── ESP32-S3飛控/                  # AtomS3R-Ext 規格書 / 原理圖
│   ├── GPS模組/                       # YIC-GT-504GGB 規格書
│   ├── WT32-ETH01/                    # 接腳圖 / V1.4 規格書
│   └── 雷射測距感測器模組/            # LRFX0M20KHS datasheet
├── firmware/
│   ├── ESP32_S3_flybox/               # 飛控板
│   │   ├── ESP32_S3_flybox.ino        # 主程式：控制迴路、命令解析、遙測、Web 伺服器
│   │   ├── config.h                   # 腳位、PID、ESC、濾波、Web 等所有設定
│   │   ├── imu.h                      # BMI270/BMM150 讀取、零點校正、濾波、姿態融合
│   │   ├── pid.h                      # PID (微分作用於量測值、積分限幅)
│   │   ├── mixer.h                    # Quad-X / X8 同軸混控
│   │   ├── motors.h                   # 8 通道 ESC 輸出 (ESP32Servo 庫)
│   │   ├── link.h                     # UART1 單行 JSON 收發
│   │   ├── jsonutil.h                 # 輕量 JSON 解析
│   │   ├── settings.h                 # NVS 設定 (WiFi、帳密、Token、無人機 ID)
│   │   ├── web.h                      # 網頁路由、WebSocket、OTA、API
│   │   ├── partitions.csv             # ESP32-S3 8MB Flash 分割表 (3MB SPIFFS)
│   │   └── data/                      # SPIFFS 網頁 (index.html / app.js / style.css)
│   ├── WT32_ETH01_receiver/           # 接收機
│   │   ├── WT32_ETH01_receiver.ino    # 接收機主程式 (RJ45 + WiFi + WebSocket 橋接)
│   │   ├── config.h                   # 網路 / UART / 帳密設定
│   │   ├── kkbox.h                    # 飛控 UART 橋接 (Serial2, 115200)
│   │   ├── sensors.h                  # 雙 GPS (NMEA 0183) 與雷射測距讀取、/api/sensors
│   │   ├── settings.h                 # NVS 設定、無人機 ID
│   │   ├── web.h                      # 網頁路由、WebSocket、OTA
│   │   ├── partitions.csv             # min_spiffs 分割表 (OTA / spiffs.bat 共用)
│   │   └── data/                      # SPIFFS 網頁 (index.html / app.js / style.css)
│   └── ESP32_D1_mini_rc/              # 遙控器
│       ├── ESP32_D1_mini_rc.ino       # 遙控器主程式 (WiFi WebSocket 用戶端)
│       ├── config.h                   # 腳位、搖桿、預設 PID、AP 設定
│       ├── settings.h                 # /config.json 設定、FlyID、搖桿校正
│       ├── input.h                    # 硬體搖桿 ADC 讀取與校正、SW1~SW4
│       ├── display.h                  # SSD1306 OLED 4 頁顯示
│       ├── link.h                     # 與接收機的 WebSocket 連線、送命令
│       ├── telem.h                    # 遙測解析
│       ├── web.h                      # 網頁路由、PID / 模擬搖桿 / 馬達校正 / OTA API
│       ├── partitions.csv             # min_spiffs 分割表 (OTA / spiffs.bat 共用)
│       ├── rc_mdns.h                  # mDNS (flybox-rc.local)：ESPmDNS，取得 IP 後才啟動
│       └── data/                      # SPIFFS 網頁 (index.html / app.js / style.css / config.json)
├── test/
│   └── led/led.ino                    # LED 測試草稿
└── tools/
    ├── spiffs.bat                     # 產生 spiffs.bin (SPIFFS 網頁檔映像，可 OTA 上傳)
    └── make_spiffs.ps1                # spiffs.bat 的 PowerShell 實作
```

## 軟體版本

三塊板子共用同一版號，各板 `config.h` 的 `FW_VERSION` 必須一致；網頁「系統狀態」會顯示此值。

| 韌體   | 目錄                            | `FW_VERSION` |
| ------ | ------------------------------- | ------------ |
| 飛控板 | `firmware/ESP32_S3_flybox/`     | `2.4.0`      |
| 接收機 | `firmware/WT32_ETH01_receiver/` | `2.4.0`      |
| 遙控器 | `firmware/ESP32_D1_mini_rc/`    | `2.4.0`      |

### v2.4.0 重點

- 飛控板開機自動做 ESC 行程校正（2000us 兩秒 → 1000us），見 `ESC_CAL_*`。
- 接收機新增雙 GPS 與雷射測距，網頁側欄顯示 GPS 定位與地面高度。
- 遙控器修正未校正搖桿的 Pitch/Roll 行程對應。

## 開發環境

- Arduino IDE 1.8.19
- ESP32 Arduino core 2.x 或 3.x（兩者都支援，**馬達輸出使用 ESP32Servo 庫，無需手動處理 LEDC 版本差異**）
- 函式庫：**M5Unified**（>= 0.2.17，含 BMI270 驅動）與 **M5GFX**（M5Unified 相依）
- **ESP32Servo**（馬達 PWM 輸出）
- 開發板選 **M5AtomS3R**；若用 ESP32 官方 core，選 **ESP32S3 Dev Module**
  並開啟 PSRAM（OPI PSRAM）；USB CDC On Boot 建議開啟

### Arduino IDE 1.8.19 開發板選擇

推薦設定
M5Stack → M5AtomS3R

官方 Arduino 開發文件指定使用 M5AtomS3R，AtomS3R 與 AtomS3R-Ext 使用相同主控晶片。

### 安裝步驟

1. 開啟 Arduino IDE 1.8.19。
2. 進入「檔案 → 偏好設定」。
3. 在額外的開發板管理員網址加入：

```sh
https://static-cdn.m5stack.com/resource/arduino/package_m5stack_index.json
```

4. 開啟「工具 → 開發板 → 開發板管理員」。
5. 搜尋並安裝 M5Stack。
6. 選擇：
   工具 → 開發板 → M5Stack → M5AtomS3R

   建議燒錄參數

   | 設定項目         | 建議值                |
   | ---------------- | --------------------- |
   | Board            | M5AtomS3R             |
   | CPU Frequency    | 240MHz                |
   | Flash Size       | 8MB                   |
   | Flash Mode       | QIO 80MHz             |
   | PSRAM            | OPI PSRAM             |
   | Partition Scheme | 8M with spiffs        |
   | USB CDC On Boot  | Enabled               |
   | USB Mode         | Hardware CDC and JTAG |
   | Upload Speed     | 921600                |
   | Upload Mode      | UART0 / Hardware CDC  |

注意：上述選項會依 M5Stack 開發板套件版本略有不同。若沒有某個選項，不要自行修改底層 Flash 或 PSRAM 設定。

### 建置 / 上傳

1. Arduino IDE 開啟 `firmware/ESP32_S3_flybox/ESP32_S3_flybox.ino`
2. 工具 → 開發板 / 序列埠 設定好
3. **需安裝函式庫**：**ESP Async WebServer** (ESP32Async)、**Async TCP** (ESP32)、**ESPmDNS** (ESP32 core 內建)、**ESP32Servo**
4. 驗證後上傳；USB 序列埠 115200 可看除錯訊息

## 接腳（`config.h`）

| 功能                          | 預設 GPIO            | 說明                        |
| ----------------------------- | -------------------- | --------------------------- |
| RS-232 TX                     | G1                   | 飛控 TX → 接收機 RX         |
| RS-232 RX                     | G2                   | 飛控 RX ← 接收機 TX         |
| M1 FL / M2 FR / M3 RR / M4 RL | G5 / G6 / G7 / G8    | 上槳                        |
| M5 FL / M6 FR / M7 RR / M8 RL | G38 / G39 / G3 / G4  | X8 下槳（G39 預設為狀態燈） |
| 外接狀態燈                    | G39                  | 與板載 RGB 同步，遙控器控制 |
| BMI270                        | SCL=G0, SDA=G45      | 板載，由 M5Unified 初始化   |
| BMM150                        | 經 BMI270 Sensor Hub | 板載，由 M5Unified 讀取     |

> **注意**：腳位依 AtomS3R-Ext 官方規格：M1–M4 = G5–G8、M5–M8 = G38/G39/G3/G4，
> RS-232 使用 PORT.CUSTOM 的 G1/G2。若實際拉線不同，請修改 `config.h`。
> RS-232 請確認是否外接 MAX3232 之類的收發器，勿直接把 RS-232 準位接到 GPIO。
>
> **狀態燈**：G39 預設作為外接狀態燈（`STATUS_LED_PIN`），與板載 RGB 同步，
> 可用遙控器「模擬搖桿」頁的「狀態燈 G39」按鈕控制。因為 G39 同時是 X8 的 M6，
> 啟用狀態燈後 8 槳模式會被拒絕（`{"err":"motor6 led"}`）；若要把 M6 改到其他腳位，
> 修改 `MOTOR6_PIN` 即可恢復 X8。

馬達位置 (Quad-X，Type-C 機頭朝上)：

```
    Type-C / 機頭 ↑
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

| 命令      | 範例                                                  | 說明                                    |
| --------- | ----------------------------------------------------- | --------------------------------------- |
| ping      | `{"cmd":"ping"}`                                      | 連線測試                                |
| arm       | `{"cmd":"arm"}`                                       | 解鎖（油門歸 1000us，IMU 無回應時拒絕） |
| disarm    | `{"cmd":"disarm"}`                                    | 上鎖                                    |
| throttle  | `{"cmd":"throttle","value":1500}`                     | 油門 1000~2000us                        |
| setpoints | `{"cmd":"setpoints","roll":0,"pitch":0,"yaw":0}`      | 角速度設定點 (deg/s)                    |
| hold      | `{"cmd":"hold","on":1}`                               | 定高（保持當前油門）                    |
| motors    | `{"cmd":"motors","count":8}`                          | 切換 4 / 8 槳                           |
| pid       | `{"cmd":"pid","axis":"roll","kp":3,"ki":0.08,"kd":8}` | 線上調 PID                              |
| motmin    | `{"cmd":"motmin","index":1,"value":1050}`             | 最低啟動 PWM（index 0=全部，存 NVS）    |
| nopid     | `{"cmd":"nopid","on":1}`                              | ESC 行程校正：略過 PID                  |
| calibrate | `{"cmd":"calibrate","sensor":"bmi270"}`               | BMI270 陀螺零點 + Roll/Pitch 歸零（解鎖中拒絕） |
| calibrate | `{"cmd":"calibrate","sensor":"bmm150"}`               | BMM150 地磁校正：20 秒內旋轉機體        |
| calibrate | `{"cmd":"calibrate","sensor":"level"}`                | 清除 Roll/Pitch 歸零校正（存 NVS）      |
| telemetry | `{"cmd":"telemetry","value":200}`                     | 遙測週期 ms（0=關閉）                   |
| status    | `{"cmd":"status"}`                                    | 回報版本、油門、PID 以外的設定          |
| led       | `{"cmd":"led","mode":"blink"}`                        | 板載 RGB + G39 狀態燈：on / off / blink |

### 回應與遙測

```
{"ack":"arm"}
{"evt":"ready","fw":"ESP32-S3 Flybox","ver":"2.4.0"}
{"evt":"failsafe"}
{"err":"bad cmd"}
{"t":12345,"armed":1,"mcount":4,"hold":0,"magok":1,
 "gyro":{"roll":0,"pitch":0,"yaw":0},
 "att":{"roll":0,"pitch":0,"yaw":90},
 "acc":{"x":0,"y":0,"z":-1000},
 "mag":{"x":210,"y":-35,"z":420},
 "set":{"roll":0,"pitch":0,"yaw":0,"thr":1500},"motor":[1520,1480,1480,1520]}
```

- 遙測欄位：`gyro` 為 deg/s；`att` 為度 (yaw 0~360)；`acc` 為 mg；`mag` 為 0.1µT；
  `magok` 表示 BMM150 是否有效。
- 每次開機都會自動執行 BMI270 校正（陀螺零點 + 以開機時的姿態作為 Roll/Pitch 零點），
  因此常溫靜置開機後 `att.roll` / `att.pitch` 即為 0；開機時機體必須靜止放平。
- BMI270 校正（`sensor:"bmi270"`）會量陀螺零點並以當前姿態作為 Roll/Pitch 零點，
  回 `{"ack":"calibrate","sensor":"bmi270","roll":0,"pitch":0}`；校正值存於 NVS，
  重開機仍保留（機體靜止放平後校正，解鎖中回 `{"err":"armed"}`）。
- BMM150 校正開始會回 `{"ack":"calibrate","sensor":"bmm150","state":"start","ms":20000}`，
  時間到再送 `{"evt":"magcal","sensor":"bmm150","ok":1}`；校正值存於 NVS，重開機仍保留。
  若 `ok` 為 0 表示旋轉幅度不足，會保留舊校正值。
- 遙測預設每 200ms 一筆（115200 下約 350 bytes/筆，仍有大量餘裕）。
- 超過 `FAILSAFE_MS`（2000ms）沒收到任何命令會自動上鎖並送出 `failsafe`。
- 解鎖後 LED 快閃（綠），未解鎖慢閃（藍）；G39 外接狀態燈與板載 RGB 同步。

---

## 飛控板網頁介面 (ESP32-S3 Flybox)

`firmware/ESP32_S3_flybox/` 自 v2.3.3 起內建獨立網頁介面，**可直接連線飛控板進行設定與監控**，不需經過接收機：

### 功能

- **系統狀態**：韌體版本、運作時間、可用記憶體、WiFi 連線資訊 (SSID/IP/RSSI)、SoftAP 狀態、SPIFFS 檔案目錄與容量、RS232 通訊狀態
- **WiFi 設定**：掃描附近 SSID、手動輸入 SSID/密碼、清除連線設定 (重開機生效)
- **OTA 更新**：上傳 `firmware.bin` (韌體) 或 `spiffs.bin` (網頁檔映像)，完成自動重開機
- **使用者設定**：變更登入帳號密碼、設定無人機 ID (0~255)、檢視/複製裝置連結 Token

### 連線方式

1. **SoftAP (預設)**：開機自動啟動 `Flybox-XXXX` (MAC 後四碼)，密碼 `12345678`  
   連線後開啟 `http://192.168.4.1/` 或 `http://flybox.local/` (mDNS)
2. **WiFi STA**：在「WiFi 設定」頁掃描並連線路由器，連線成功後飛控板會重開機，之後可用路由器分派的 IP 或 mDNS 連線

### 預設帳密

- `admin` / `admin` (可在 `config.h` 的 `DEF_USER` / `DEF_PASS` 修改，或網頁登入後變更)

### API 端點

| 端點                | 方法     | 說明                                               |
| ------------------- | -------- | -------------------------------------------------- |
| `/api/status`       | GET      | 系統狀態 (JSON)                                    |
| `/api/fs`           | GET      | SPIFFS 檔案列表與容量                              |
| `/api/scan`         | GET      | WiFi 掃描結果                                      |
| `/api/wifi`         | POST     | 連線/清除 WiFi (`ssid`/`pass` 或 `action=clear`)   |
| `/api/user`         | GET/POST | 讀取/變更帳號密碼/Token                            |
| `/api/id`           | GET/POST | 讀取/設定無人機 ID                                 |
| `/api/command`      | POST     | 轉送單行 JSON 給飛控主迴路 (如 `{"cmd":"status"}`) |
| `/api/ota/firmware` | POST     | 韌體 OTA 上傳 (multipart/form-data, `file`)        |
| `/api/ota/spiffs`   | POST     | SPIFFS 映像 OTA 上傳                               |
| `/ws`               | WS       | WebSocket (即時雙向，同接收機協定)                 |

### WebSocket 協定

- 連線：`ws://<IP>/ws?token=<deviceToken>` 或攜帶登入 cookie
- 送出：單行 JSON 字串，直接透過 RS-232 轉送飛控主迴路
- 收到：飛控回傳的遙測/ack/evt/err 等 JSON

### 使用方式

1. 先編譯一次專案 (產生分割表)
2. 執行 `tools\spiffs.bat -Flybox` 產生 `firmware\ESP32_S3_flybox\build\spiffs.bin`
3. 燒錄韌體，再透過網頁「OTA 更新」上傳 `spiffs.bin`
4. 瀏覽器開啟 `http://flybox.local/` 或 IP 位址
5. 登入後即可進行 WiFi 設定、OTA、帳號管理等操作

---

## 接收機（WT32-ETH01）

`firmware/WT32_ETH01_receiver/` 是飛控與遙控器之間的橋接：

- RJ45 (LAN8720A) 有線網路 + WiFi STA + SoftAP 可同時運作。
- 網頁（SPIFFS）顯示飛控遙測：**三軸即時角度長條圖**、**BMI270 六軸姿態 (Roll/Pitch/Yaw + 加速度計)**、
  **BMM150 三軸地磁與磁方位**、馬達輸出、解鎖狀態。
- **外接感測器**：兩顆 GPS（NMEA 0183，115200）與一顆雷射測距（115200），
  側欄「GPS 定位資訊」（經緯度 6 位小數、衛星數、定位狀態）與「地面高度資訊」
  （對地高度、GPS 海拔）每秒更新；原始資料為 `GET /api/sensors`。
- **無人機 ID 設定**（0–255，存 NVS），顯示於飛行狀態頁；`GET/POST /api/id`。
- 飛控送出的每一行 JSON 原樣經 WebSocket 廣播，遙控器與網頁看到的是同一份資料。
- 網頁另含網路設定、馬達模式、使用者帳密、SPIFFS 檔案瀏覽與 OTA。

### 接線（與飛控板對接）

| 接收機           | 飛控板        |
| ---------------- | ------------- |
| Serial2 RX = G5  | UART1 TX = G1 |
| Serial2 TX = G17 | UART1 RX = G2 |
| GND              | GND           |

115200 8N1，交叉對接；若中間走真正的 RS-232 準位，兩端都要加收發器（如 MAX3232）。
接收機的 G16 是 PHY 電源，不可他用。

### 接線（外接感測器）

| 感測器                | 模組 TX | 接收機 RX              | Baud   |
| --------------------- | ------- | ---------------------- | ------ |
| GPS1（YIC GT-504GGB） | TX      | IO36（SoftwareSerial） | 115200 |
| GPS2（YIC GT-504GGB） | TX      | IO35（SoftwareSerial） | 115200 |
| 雷射（LRFX0M20KHS）   | TX      | IO39（UART1）          | 115200 |

- 三支腳都只當 RX（SoftwareSerial 送出腳設 -1、UART1 不送），選 IO35/36/39 是
  WT32-ETH01 的輸入腳，不佔用其他 IO；請與模組共地。
- GPIO 與 baud 都在 `config.h`：`SENSOR_ENABLE`、`GPS1_RX_PIN`/`GPS2_RX_PIN`/`GPS_BAUD`、
  `LASER_RX_PIN`/`LASER_BAUD`；不接感測器時把 `SENSOR_ENABLE` 設 0。
- 雷射模組出廠是 460800：先送 `5A 06 02 80 04 73` 改成 115200，或把 `LASER_BAUD` 改 460800。
  若模組實際是 8 bytes/frame（資料表兩種寫法並存），改 `LASER_FRAME_LEN=8`、`LASER_DIST_LO=3`。

### 建置 / 上傳

1. 開發板選 **WT32-ETH01**（ESP32 core 的 `wt32-eth01`）
2. 函式庫：**ESP Async WebServer**（ESP32Async）、**Async TCP**；
   OTA 由韌體自行實作（`/api/ota/firmware`、`/api/ota/spiffs`），不需 ElegantOTA
   **另需安裝 ESPSoftwareSerial**（工具 → 管理庫 → 搜尋 ESPSoftwareSerial 安裝，
   否則 `#include <SoftwareSerial.h>` 編譯會失敗）
3. 先上傳程式，再把 `data/` 以 SPIFFS 映像方式上傳（Arduino IDE 需安裝
   ESP32 Sketch Data Upload 外掛，或使用 `mklittlefs` / `arduino-cli uploadfs`）
4. 序列埠 115200 可看開機與網路訊息

### 使用

- 預設帳密 `admin` / `admin`（`config.h` 的 `DEF_USER` / `DEF_PASS`，登入後可改）
- SoftAP：`Flybox-Receiver-XXXX`（XXXX = MAC 後四碼），密碼 `12345678`
- 網頁：`http://<接收機 IP>/`，或區網 mDNS `http://flybox-receiver.local/`
  （接收機是 `flybox-receiver.local`，遙控器才是 `flybox-rc.local`，兩者不要搞混）
- WebSocket：`/ws`（需 `?token=` 或登入 cookie）
- 遙控器用的裝置 token 在「使用者設定」頁可重新產生
- 常用 API：`/api/status`、`/api/telemetry`、`/api/sensors`（GPS 與雷射原始資料）、
  `/api/id`、`/api/command`（POST 單行 JSON 轉送飛控）、
  `/api/ota/firmware`、`/api/ota/spiffs`（POST `firmware.bin` / `spiffs.bin`，需登入）

### OTA 更新（韌體 / 網頁檔）

兩塊板子都用**內建 OTA 頁面**（`/api/ota/firmware`、`/api/ota/spiffs`，需登入），
不需要 ElegantOTA：

- **firmware.bin**：Arduino IDE／arduino-cli 編譯出的 `.bin`，寫入備用 OTA 分割區，
  完成後自動重開機；上傳失敗仍會從舊分割區開機。
- **spiffs.bin**：`data/` 打包的網頁檔映像，直接覆寫 SPIFFS 分割區；
  遙控器會先備份 `/config.json`，寫入後自動還原（設定不會流失）。

產生網頁檔映像（各板子輸出到自己的 `build/`）：

| 目標     | 指令                                                    | 輸出                                            |
| -------- | ------------------------------------------------------- | ----------------------------------------------- |
| 三塊板子 | `tools\spiffs.bat -All`                                 | 下列三個檔案                                    |
| 接收機   | `tools\spiffs.bat`                                      | `firmware\WT32_ETH01_receiver\build\spiffs.bin` |
| 飛控板   | `tools\spiffs.bat -Flybox`                              | `firmware\ESP32_S3_flybox\build\spiffs.bin`     |
| 遙控器   | `tools\spiffs.bat -Data firmware\ESP32_D1_mini_rc\data` | `firmware\ESP32_D1_mini_rc\build\spiffs.bin`    |

上傳方式：板子網頁 →「OTA 更新」→ 選檔 → 上傳（顯示百分比進度），完成後自動重開機。

腳本特性：

- `-Size auto`（預設）先讀 `<sketch>\partitions.csv`，沒有就讀該 sketch 上次編譯留下的
  `<sketch>\build\<fqbn>\partitions.csv`（板子實際使用的分割表）；
  兩者都沒有時直接報錯，請先編譯一次或改用 `-Size`
- 自動尋找 Arduino ESP32 core 內附的 `mkspiffs.exe`；找不到時可用
  `-Mkspiffs <路徑>`，或設定環境變數 `MKSPIFFS`
- 產生後印出映像大小、分割區大小、資料使用率、SHA256 與 USB 燒錄位移，
  方便核對上傳的映像
- `spiffs.bin`、`build/`、`*.ino.*.bin` 已列入 `.gitignore`
- `spiffs.bin` 必須與 SPIFFS 分割區一樣大。兩個 sketch 資料夾都放了 `partitions.csv`
  （`min_spiffs`：APP 1.95MB × 2、SPIFFS 128KB、位移 `0x3D0000`），Arduino 建置時
  會優先用它，所以不再受 IDE「Partition Scheme」選項影響；映像大於分割區時，網頁會
  回報「映像大於分割區」並拒絕寫入，不會寫壞現有網頁檔
- 換分割表（含第一次改用本版 `partitions.csv`）一定要用 USB 重新燒錄一次：
  OTA 只會更新 APP／SPIFFS 內容，不會搬動分割表
- 映像會整塊覆蓋 SPIFFS：上傳中斷會使網頁檔損毀（開機自動格式化 → 網頁空白），
  此時請改用 USB 重新上傳；`firmware.bin` 中斷則不影響現有版本

> **ESP32-S3 飛控板分割表 (8MB Flash)**：`firmware/ESP32_S3_flybox/partitions.csv` 為
> APP 2MB × 2、SPIFFS 3MB (0x410000, 0x300000)、coredump 0xF0000。
> Arduino 建置時會優先使用此表，不受 IDE「Partition Scheme」影響。

## 遙控器（ESP32 D1 mini RC）

`firmware/ESP32_D1_mini_rc/` 是操作端：只用 2.4GHz WiFi，以 WebSocket 用戶端連接收機，
把命令轉給飛控板。

- **虛擬控制搖桿**：網頁「模擬搖桿」頁，觸控/滑鼠拖曳即時送出 `setpoints` 與 `throttle`。
- **IMU 校正**：遙控器「模擬搖桿」頁的「IMU 校正」，可分別校正 BMI270 陀螺零點與
  BMM150 地磁（硬鐵 / 軟鐵）；BMM150 校正時需在 20 秒內把機體各方向緩慢旋轉。
- **PID 參數校正**：網頁「PID 設定」頁，roll / pitch / yaw 各自 Kp / Ki / Kd，
  下傳 `{"cmd":"pid","axis":...}`。
- **PWM 馬達最低啟動數值設定**：網頁「馬達校正」頁，M1–M8 各自設定並下傳 `motmin`。
- **未校正搖桿的行程對應**：Pitch/Roll 在尚未做搖桿校正時，以開機讀值為中心、
  依實際推過的行程換算；推過 `AUTO_SPAN_MIN`（200 counts）就採用實際值，推到機械端點
  即為滿刻度，回中必定為 0。若搖桿行程明顯不對稱，還是做一次完整校正最準。
- 另有硬體遙控（四軸搖桿 ADC1 + SW1~SW4 + VRC1 旋鈕）、SSD1306 OLED 4 頁顯示、
  ESC 行程校正（`nopid`）與 OTA。

### 接腳（`config.h`）

| 功能                               | GPIO                              |
| ---------------------------------- | --------------------------------- |
| 油門 / 偏航 / 俯仰 / 橫滾          | G39 / G35 / G33 / G34（ADC1）     |
| VRC1（選 FlyID / 切換 OLED 頁）    | G36（ADC1）                       |
| SW1 / SW2 / SW3(ARM) / SW4(DISARM) | G10 / G18 / G19 / G23（LOW = ON） |
| OLED SDA / SCL（SSD1306 0x3C）     | G17 / G16                         |
| 板載 LED                           | G2                                |

> WiFi 開啟時 ADC2 不可用，搖桿與 VRC1 一律接 ADC1；不要接回 G13（ADC2）。

### 開機流程

0. 通電即啟動 SoftAP（`Flybox-RC-XXXX`，`http://192.168.10.1`），不必等 ID 選擇與搖桿校正完成，
   網頁在開機當下就能連、能看狀態與做設定；接收機 WebSocket 連線則在主迴圈啟動後才建立
1. 轉 VRC1 選無人機 ID，按 SW1 確認
2. 按住 SW1 期間持續校正四軸搖桿 min/max，放開結束
3. 按住 SW2 開機 = 跳過上述流程（沿用已存的 ID 與校正值）

### 建置 / 上傳

1. 開發板選 **WEMOS D1 MINI ESP32**（`d1_mini32`）
2. 函式庫：**Adafruit GFX**、**Adafruit SSD1306**、**ArduinoJson**、
   **ESP Async WebServer**、**Async TCP**、**WebSockets**（Links2004）（OTA 內建，不需 ElegantOTA）
3. 上傳程式後，把 `data/` 以 SPIFFS 映像上傳（`config.json` 為預設設定）；
   或先執行 `tools\spiffs.bat -Data firmware\ESP32_D1_mini_rc\data`，
   再由網頁「OTA 更新」上傳 `firmware\ESP32_D1_mini_rc\build\spiffs.bin`
4. 網頁：`http://192.168.10.1/` 或 `http://flybox-rc.local/`（SoftAP `Flybox-RC-XXXX`，
   密碼 `12345678`），帳密 `admin` / `admin`
5. 先在網頁「網路設定」填入接收機 IP / token，之後才會建立 WebSocket 連線

> 分割表用 `min_spiffs`（APP 1.95MB × 2、SPIFFS 128KB，位移 `0x3D0000`）：
> 韌體約 1.35MB（68%）、網頁檔約 63KB（49%）。OTA 上傳的 `spiffs.bin` 必須是
> 131072 bytes（由 `tools\spiffs.bat` 依實際分割表產生）。

## 控制參數

- 馬達 PWM 輸出：使用 **ESP32Servo 庫**（50Hz, 1000~2000us），內部採用 LEDC 硬體，支援 16 通道，程式碼簡潔且跨 core 2.x/3.x 相容。
- 開機 ESC 行程校正：`motorsInit()` 後由 `motorsEscCalibrate()` 對所有馬達輸出
  `ESC_CAL_HIGH_US`(2000us) 維持 `ESC_CAL_TIME_MS`(2s)，再降至 `ESC_MIN`(1000us)
  並等 `ESC_CAL_LOW_MS` 完成初始化；`ESC_CAL_ENABLE=0` 時開機直接輸出 `ESC_MIN`。
  這是 ESC 自己的行程學習，與遙控器網頁「馬達校正」的 `motmin`（最低啟動 PWM）不同。
- 控制迴路 `CTRL_HZ = 100`，與舊 ATmega168 飛控相同，既有 PID 預設值可直接沿用。
- PID 預設：roll/pitch `3.0 / 0.08 / 8.0`、yaw `4.0 / 0.5 / 0.0`；
  輸出限幅 100us、積分限幅 50us。
- roll/pitch 的 KI 兼作自動配平：搖桿置中（< 10 deg/s）才累積積分。
- 陀螺濾波預設為卡爾曼（Q=2.0 / R=2.0），可改用一階低通 `GYRO_LPF`。
- 機體軸向對應在 `config.h` 的 `IMU_GYRO_*_AXIS` / `_SIGN` 與 `IMU_ACC_*`。
  M5Unified 校正後的板載座標為 **+X = 機頭 (USB Type-C)、+Y = 左舷、+Z = 朝上**，
  本飛控再轉成 NED：前 = +X、右 = -Y、下 = -Z。陀螺儀正方向定義為
  **roll 右傾、pitch 抬頭、yaw 機頭向右**；若實機轉動方向相反，只改對應的 SIGN，
  不必改 PID 或混控。
- BMM150 在板上相對 BMI270 旋轉了 90°，M5Unified 只反轉 X/Z，因此地磁另用
  `IMU_MAG_*_AXIS` / `_SIGN` 對應（前 = -mY、右 = +mX、下 = +mZ）。
- 姿態融合：roll/pitch 互補濾波時間常數 `ATT_TAU_S = 0.5s`，yaw 地磁修正 `ATT_MAG_TAU_S = 2.0s`；
  `MAG_DECLINATION_DEG` 可設定磁偏角（台灣約 -2 ~ -4 度），`MAG_HEADING_SIGN`
  可整組反向。

## 調機

首次上機請照 [`docs/調機測試清單.md`](docs/調機測試清單.md) 逐項確認（拆槳、馬達序號、
混控方向、PID 調參、最低啟動 PWM、failsafe）。

## 安全提醒

- 首次測試請拆槳；確認馬達序號、轉向與混控方向正確再裝槳。
- 飛控開機即做 ESC 行程校正：所有馬達輸出 2000us 兩秒後降到 1000us
  （`ESC_CAL_ENABLE` / `ESC_CAL_TIME_MS`），這段期間馬達會全油門，務必先拆槳。
- `arm` 後油門即時生效，請先把機體固定好。
- 由遙控器觸發的 ESC 行程校正（`nopid`）期間不套用最低啟動 PWM，可回到 1000us；
  與開機自動校正（`ESC_CAL_*`）是兩回事。

## 疑難排解

### 開機後在啟動 WiFi 時當機（`tx_pwctrl_init` / `BREAKN instr`）

遙控器（D1 mini RC）完成 ID／校正流程後，若一啟動 WiFi 就出現
`Guru Meditation Error: ... Debug exception reason: BREAKN instr`，把 backtrace
位址用 `addr2line` 反查會落在 `tx_pwctrl_init` → `register_chipv7_phy` →
`esp_phy_enable`。這是 ESP32 射頻校正階段，與網頁、OTA、mDNS 程式無關
（當機發生在 WiFi 任務，不是 loopTask）。常見原因有兩個：

1. **供電瞬間不足（最常見）**：細 USB 線、機殼前方 USB、沒獨立電源的 HUB，
   或電池電壓偏低時，射頻校正的電流峰值會讓 3.3V 瞬間下陷。換短線／主機板
   後方 USB／自帶電源的 HUB，並在 3V3 與 GND 之間加 100–470µF 電容。
   遙控器韌體啟動時會關閉 brownout 偵測（避免誤重置），所以電壓下陷不會出現
   `Brownout detector was triggered`，而是直接當在 PHY，容易被誤認為程式問題。
2. **NVS 內的 PHY 校正資料損毀**：用 USB 清除 NVS 後重開，讓 PHY 重新校正。

```powershell
# 清除 NVS（含 PHY 校正資料與 WiFi 設定；SPIFFS 的 /config.json 不受影響）
# esptool 版本目錄 (5.3.1) 請依實際安裝調整；COM3 改成遙控器的序列埠
& "$env:LOCALAPPDATA\Arduino15\packages\esp32\tools\esptool_py\5.3.1\esptool.exe" `
    --chip esp32 -p COM3 erase-region 0x9000 0x4000
```

遙控器 2.3.1 起開機時會印出 `[SYS] 重置原因: ...`；若連續 3 次異常開機
（PANIC / WDT），會自動清除 NVS 的 `phy` 命名空間並重新校正，避免卡在開機當機。

### `http://flybox-rc.local/` 或 `http://flybox-receiver.local/` 打不開

mDNS 依賴用戶端的解析器，先確認是同一個區網，並改用 IP（`192.168.10.1`、
接收機取得的 IP）交叉測試，即可分辨是 mDNS 還是網路問題。

- 遙控器的 mDNS 由 `rc_mdns.h` 使用 ESP32 core 內建的 ESPmDNS：等 SoftAP 或 STA
  取得 IP 後才啟動（失敗每 5 秒重試），AP+STA 共存時兩個介面都會加入群組，
  所以從 `Flybox-RC-XXXX` 連進來也解析得到。檔名不叫 `mdns.h`，是為了避免
  蓋掉 ESP-IDF 的同名標頭、讓 `ESPmDNS.h` 編譯失敗。
- 接收機用 ESPmDNS，開機後等 RJ45／WiFi 取得 IP 才啟動，失敗每 5 秒重試；
  序列埠會印出 `[mDNS] http://flybox-receiver.local/ (ETH … / STA … / AP …)`。
- 名稱不同：遙控器是 `flybox-rc.local`，接收機是 `flybox-receiver.local`。
- 少數系統（舊版 Windows、部分 Android 瀏覽器）不支援 `.local` 解析，
  請直接使用 IP。
