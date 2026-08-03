# 🚀 ESP32 TMC2209 Web Controller

基於 ESP32 與 TMC2209 步進馬達驅動器的深色現代化 Web 控制面板。支援動態微步切換、256 微步插值 (Interpolation)、CoolStep 自動電流調節、自動關機計時器以及 OTA 無線更新。

![TMC2209 Web UI Dashboard](https://via.placeholder.com/800x600.png?text=TMC2209+Control+Panel+UI) <!-- 可在此貼上你的 UI 截圖連結 -->

---

## ✨ 特色功能 (Features)

* **現代化 UI**：深色科技感介面，支援即時狀態監控（溫控、驅動模式、RPM、計時器）。
* **動態微步切換 (Microstep Gear)**：根據轉速自動在 `1/64`、`1/32` 與 `1/16` 微步間非同步無縫切換。
* **高階 TMC2209 優化**：
  * **Microplyer (256 插值)**：自動平滑化脈衝，極致降低運轉高頻噪音。
  * **CoolStep**：根據負載動態調節電流，有效降低馬達與驅動器發熱。
  * **StealthChop / SpreadCycle 自動切換**：低速極致靜音，超過 150 RPM 自動切換至高扭力模式。
* **智慧定時器 (Auto Shutdown Timer)**：設定倒數時間，時間到自動停止馬達並恢復預設模式。
* **過熱安全保護**：即時讀取晶片狀態，超過 150°C 自動強制停止馬達。
* **OTA 無線燒錄**：支援透過 Wi-Fi 直接進行韌體更新。

---

## 🔌 硬體接線 (Pinout & Hardware)

| ESP32 GPIO | TMC2209 / 功能 | 說明 |
| :--- | :--- | :--- |
| **GPIO 14** | STEP | 步進訊號脈衝 |
| **GPIO 12** | DIR | 馬達轉向控制 |
| **GPIO 13** | EN | 驅動器啟用腳位 |
| **GPIO 16 (RX2)**| TX | UART 訊號輸入 (連至 TMC2209 TX) |
| **GPIO 17 (TX2)**| RX | UART 訊號輸出 (需串聯 1kΩ 電阻至 UART 線) |

> ⚠️ **注意**：使用單線 UART 模式時，請確保 ESP32 TX2 (GPIO 17) 腳位上有串聯一顆 **1kΩ 電阻** 再連接至 TMC2209 的 UART 腳位。

---

## 🛠️ 第三方函式庫需求 (Dependencies)

請在 Arduino IDE 的「函式庫管理員」中安裝以下 Library：

1. **`TMCStepper`** by teemuatlut
2. **`FastAccelStepper`** by Gin66
3. **`ArduinoOTA`** (ESP32 核心內建)

---

## 🚀 快速開始 (Quick Start)

1. 下載或 Clone 本專案至本地資料夾：
   ```bash
   git clone [https://github.com/your-username/TMC2209-ESP32-Web-Control.git](https://github.com/your-username/TMC2209-ESP32-Web-Control.git)