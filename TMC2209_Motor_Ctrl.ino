#include <WiFi.h>
#include <WebServer.h>
#include <TMCStepper.h>
#include <FastAccelStepper.h>
#include "index_html.h" // 引用獨立的網頁檔案
#include <ArduinoOTA.h>
#include "secrets.h"     // Wi-Fi / OTA 密碼 (不進 Git，範本見 secrets.example.h)
#include <algorithm>
#include <Preferences.h> // 轉速、轉向、電流存在 Flash (NVS)，斷電後保留

// =================【 韌體版本 】=================
const char* FW_VERSION = "v1.9.0";       // 每次更新韌體時請遞增
const char* FW_BUILD   = __DATE__ " " __TIME__; // 編譯時間 (自動產生)

// =================【 自發 Wi-Fi 熱點與硬體常數設定 】=================
const char* ap_ssid     = "TMC2209_Motor_Ctrl"; 
const char* ap_password = SECRET_AP_PASSWORD;   // 定義於 secrets.h

const float RSENSE_VALUE = 0.11f;        // 驅動器感測電阻值 (TMC2209 通常為 0.11)
const uint8_t DRIVER_ADDRESS = 0;        // UART 設備位址 (MS1, MS2 皆接地或浮空為 0)
const uint16_t DEFAULT_CURRENT_MA = 800; // 預設運轉電流 (mA)，可在網頁上修改並儲存
const uint16_t MIN_CURRENT_MA = 200;     // 網頁可設定的電流範圍 (RMS)
const uint16_t MAX_CURRENT_MA = 1200;    // 小型 TMC2209 模組散熱有限，長時間超過約 1.2A 容易過熱
const int MOTOR_STEPS_PER_REV = 200;     // 馬達每圈步數 (1.8度)
const float MIN_ALLOWED_RPM = 0.1;       // 最低轉速 (與網頁拉桿下限一致)
const float MAX_ALLOWED_RPM = 450.0;     // 最高安全轉速限制
// 超過此轉速改用 1/16 微步 + SpreadCycle (在停止時切換)。
// 刻意避開常用的 150 RPM：拉桿在 150 附近每格約差 0.6 RPM，界線設在 150 會讓「顯示 150」的轉速時常落到另一側
const float SPREADCYCLE_RPM_THRESHOLD = 160.0;
// 卡住偵測 (StallGuard4)：只在 StealthChop 且轉速足夠時有效，低速時馬達反電動勢太小、讀值不可靠
const float STALL_MIN_RPM = 30.0;
const float STALL_RATIO = 0.35;          // 負載讀值掉到基準值的 35% 以下視為卡住 (數值越大越敏感)
// 最近 6 次取樣 (約 300 ms) 中有 3 次低於門檻才判定：
// 單次雜訊不會誤判；馬達被卡住跳步時讀值會高低交錯，「連續 3 次」可能永遠不成立，所以不用連續
const uint8_t STALL_WINDOW = 6;
const uint8_t STALL_CONFIRM_SAMPLES = 3;
// =====================================================================

WebServer server(80);
const int stepPin = 14, dirPin = 12, enPin = 13;   
TMC2209Stepper driver(&Serial2, RSENSE_VALUE, DRIVER_ADDRESS); 
FastAccelStepperEngine engine = FastAccelStepperEngine();
FastAccelStepper *stepper = NULL;

int currentMicrostep = 16, targetDirection = 1, tempHour = 0, tempMin = 0, tempSec = 0; 
float targetRPM = 60.0;     
unsigned long timerDuration = 0, timerStartHour = 0;
bool isRunning = false;
bool isInterpolationEnabled = true; 
String plainTempStatus = "🟢 Temp Normal (< 120 deg C)";
int tempLevel = 0;           // 0 = 正常, 1 = 過熱預警, 2 = 故障 (過熱關斷 / UART 斷線)
bool isDriverFault = false;  // 故障時強制停止並禁止啟動，降溫或通訊恢復後自動解除
String faultMessage = "";    // 故障原因 (啟動被拒時回傳給網頁)
String wiringAlert = "";     // 接線警告文字，空字串代表正常
int wiringLevel = 0;         // 0 = 正常, 1 = 疑似開路 (只警告), 2 = 短路 (停機)
uint16_t actualCurrentMA = 0; // 驅動器回報的實際電流 (運轉中才有值)
uint8_t uartFailCount = 0;   // 連續 UART 失敗次數，避免單次雜訊誤判
bool isStealthActive = true; // 驅動器實際模式 (DRV_STATUS bit30)，由溫度檢查一併更新

bool isMicrostepChangePending = false;
int pendingMicrostep = 16;
bool isSpreadCycle = false;   // 目前設定給驅動器的模式 (en_spreadCycle)
bool pendingSpread = false;

// 卡住偵測狀態：OFF (轉速範圍外) → WAIT (加速中/取基準) → ARMED (監測中) → STALL (已觸發)
enum StallState { STALL_OFF, STALL_WAIT, STALL_ARMED, STALL_TRIPPED };
StallState stallState = STALL_OFF;
uint16_t sgValue = 0;          // 最新的 SG_RESULT (越小代表負載越重)
uint16_t sgBaseline = 0;       // 正常運轉時的平均值
uint32_t sgSum = 0;
uint8_t sgSamples = 0;
int sgMinPct = -1;             // 本次運轉中負載讀值的最低百分比 (最小餘裕)，-1 = 尚未開始監測
int sgRecentPct[STALL_WINDOW];  // 最近幾次取樣的百分比，用來排除單次突波
uint8_t sgRecentCount = 0;
// 除錯用：最近 40 次原始 SG_RESULT (約 2 秒)，由 /sg 提供，方便調整門檻
const uint8_t SG_HIST_LEN = 40;
uint16_t sgHist[SG_HIST_LEN];
uint32_t sgSeq = 0;
unsigned long stallSettleAt = 0;

Preferences prefs;
uint16_t currentMA = DEFAULT_CURRENT_MA;
const int PRESET_COUNT = 3;
float presetRpm[PRESET_COUNT] = { 30.0, 60.0, 150.0 };  // 常用轉速快捷鍵 (網頁長按可改)
bool isSettingsDirty = false;       // 設定有變更、尚未寫入 Flash
unsigned long settingsChangedAt = 0;

// 開機時讀回上次儲存的設定；讀到異常值時改用預設值
void loadSettings() {
  prefs.begin("motor", true);
  float rpm = prefs.getFloat("rpm", targetRPM);
  int dir = prefs.getInt("dir", targetDirection);
  uint16_t ma = prefs.getUShort("ma", DEFAULT_CURRENT_MA);
  for (int i = 0; i < PRESET_COUNT; i++) {
    float p = prefs.getFloat(("p" + String(i)).c_str(), presetRpm[i]);
    if (p > 0.0f) presetRpm[i] = constrain(p, MIN_ALLOWED_RPM, MAX_ALLOWED_RPM);
  }
  prefs.end();

  if (rpm > 0.0f) targetRPM = constrain(rpm, MIN_ALLOWED_RPM, MAX_ALLOWED_RPM);
  targetDirection = (dir == -1) ? -1 : 1;
  currentMA = constrain(ma, MIN_CURRENT_MA, MAX_CURRENT_MA);
  Serial.printf("Settings loaded: %.1f RPM, dir %d, %u mA\n", targetRPM, targetDirection, currentMA);
}

void markSettingsDirty() {
  isSettingsDirty = true;
  settingsChangedAt = millis();
}

// 最後一次變更後 3 秒才寫入，避免拖拉桿或連按 +/- 時頻繁寫 Flash。
// 只在馬達停止時寫入：寫 Flash 期間程式會短暫停住，運轉中寫入可能讓馬達頓一下
void saveSettingsIfDue() {
  if (!isSettingsDirty || millis() - settingsChangedAt < 3000) return;
  if (stepper && stepper->isRunning()) return;
  prefs.begin("motor", false);
  prefs.putFloat("rpm", targetRPM);
  prefs.putInt("dir", targetDirection);
  prefs.putUShort("ma", currentMA);
  for (int i = 0; i < PRESET_COUNT; i++) prefs.putFloat(("p" + String(i)).c_str(), presetRpm[i]);
  prefs.end();
  isSettingsDirty = false;
}

void convertStepsToTimeStr(long totalSeconds, String &outStr) {
  int h = totalSeconds / 3600;
  int m = (totalSeconds % 3600) / 60;
  int s = totalSeconds % 60;
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%dh %dm %ds", h, m, s);
  outStr = buffer; 
}

// 設定微步與驅動模式。只能在馬達停止時呼叫：
// 高速運轉中由 SpreadCycle 切回 StealthChop 時，StealthChop 的電流調節尚未適應該轉速，馬達會卡住
void applyDriverMode(int microstep, bool spread) {
  currentMicrostep = microstep;
  isSpreadCycle = spread;
  driver.microsteps(microstep);
  driver.en_spreadCycle(spread);
  delay(10);
}

void checkTMC2209Temperature() {
  // UART 讀不到時 DRV_STATUS 會回傳 0 (看起來像「正常」)，所以先確認通訊：
  // IOIN 的版本欄位 (bit 31..24) 在 TMC2209 固定為 0x21
  uint32_t ioin = driver.IOIN();
  if (driver.CRCerror || (ioin >> 24) != 0x21) {
    if (uartFailCount < 255) uartFailCount++;
    if (uartFailCount >= 3) {
      plainTempStatus = "⚪ Driver UART Error (temp unknown)";
      tempLevel = 2;
      isDriverFault = true;
      faultMessage = plainTempStatus;
    }
    return;
  }
  uartFailCount = 0;

  // TMC2209 DRV_STATUS: bit0 otpw, bit1 ot, bit8 t120, bit9 t143, bit10 t150, bit11 t157
  uint32_t s = driver.DRV_STATUS();
  bool otpw = s & (1UL << 0);
  bool ot   = s & (1UL << 1);
  bool t120 = s & (1UL << 8);
  bool t143 = s & (1UL << 9);
  bool t150 = s & (1UL << 10);
  bool t157 = s & (1UL << 11);
  isStealthActive = s & (1UL << 30);

  // bit16..20 CS_ACTUAL：驅動器實際使用的電流級數 (0~31)，換算成 mA；停止時驅動器關閉，不顯示
  actualCurrentMA = isRunning ? driver.cs2rms((s >> 16) & 0x1F) : 0;

  bool thermalFault = false;
  if (ot || t150) {
    plainTempStatus = t157 ? "🔴 Thermal Shutdown (> 157 deg C)" : "🔴 Thermal Shutdown (> 150 deg C)";
    tempLevel = 2;
    thermalFault = true;
  } else if (otpw || t120) {
    plainTempStatus = t143 ? "🟡 Driver Overheat (> 143 deg C)" : "🟡 Driver Overheat (> 120 deg C)";
    tempLevel = 1;
  } else {
    plainTempStatus = "🟢 Temp Normal (< 120 deg C)";
    tempLevel = 0;
  }

  // 接線檢查 — bit2/3 s2ga/s2gb：線圈對地短路，bit4/5 s2vsa/s2vsb：線圈對電源短路 (低側)，
  // bit6/7 ola/olb：線圈開路。短路時驅動器會自行關閉輸出，旗標在下次重新致能 (按 START) 時清除
  bool shortA = s & ((1UL << 2) | (1UL << 4));
  bool shortB = s & ((1UL << 3) | (1UL << 5));
  bool openA = s & (1UL << 6);
  bool openB = s & (1UL << 7);

  // 開路旗標在高速與靜止時可能誤報 (datasheet 建議只在慢速運轉時參考)，
  // 所以只在等速運轉、StealthChop 區間內判斷，並需連續 3 秒才警告
  static uint8_t openCount = 0;
  bool atSpeed = false;
  if (stepper && isRunning && !isMicrostepChangePending && !isSpreadCycle) {
    float targetHz = (targetRPM / 60.0f) * MOTOR_STEPS_PER_REV * currentMicrostep;
    atSpeed = abs(stepper->getCurrentSpeedInMilliHz()) / 1000.0f >= targetHz * 0.95f;
  }
  if (atSpeed && (openA || openB)) {
    if (openCount < 255) openCount++;
  } else if (!isRunning || atSpeed) {
    openCount = 0;
  }

  if (shortA || shortB) {
    wiringAlert = String("Short circuit on motor coil ") + (shortA && shortB ? "A and B" : shortA ? "A" : "B") +
                  ". Power off and check the motor wiring.";
    wiringLevel = 2;
  } else if (openCount >= 3) {
    wiringAlert = String("Motor coil ") + (openA && openB ? "A and B" : openA ? "A" : "B") +
                  " seems disconnected. Power off and check the motor cable.";
    wiringLevel = 1;
  } else if (!isRunning || atSpeed) {
    wiringAlert = "";
    wiringLevel = 0;
  }

  isDriverFault = thermalFault || wiringLevel == 2;
  faultMessage = thermalFault ? plainTempStatus : wiringAlert;
}

// 轉速或運轉狀態改變時重新取基準 (卡住後保留 STALL 狀態，直到下次按 START)
void resetStallMonitor() {
  if (stallState != STALL_TRIPPED) stallState = STALL_OFF;
  sgBaseline = 0;
  sgSum = 0;
  sgSamples = 0;
  stallSettleAt = 0;
  sgRecentCount = 0;
  if (stallState != STALL_TRIPPED) sgMinPct = -1;  // 卡住時保留最低值，方便查看
}

// 每 50 ms 讀一次 SG_RESULT：到達目標轉速並穩定 1 秒後，取 1 秒平均為基準，之後讀值過低就停機
void monitorStall() {
  static unsigned long lastSample = 0;
  if (millis() - lastSample < 50) return;
  lastSample = millis();

  if (!stepper || !isRunning || isMicrostepChangePending || stallState == STALL_TRIPPED) return;
  if (isSpreadCycle || targetRPM < STALL_MIN_RPM) {
    stallState = STALL_OFF;
    return;
  }

  // 等加速完成：實際速度達到目標的 95%
  float targetHz = (targetRPM / 60.0f) * MOTOR_STEPS_PER_REV * currentMicrostep;
  float actualHz = abs(stepper->getCurrentSpeedInMilliHz()) / 1000.0f;
  if (stallState == STALL_OFF) stallState = STALL_WAIT;
  if (stallState == STALL_WAIT && sgBaseline == 0) {
    if (actualHz < targetHz * 0.95f) { stallSettleAt = 0; return; }
    if (stallSettleAt == 0) { stallSettleAt = millis() + 1000; return; }
    if (millis() < stallSettleAt) return;
  }

  sgValue = driver.SG_RESULT();
  if (driver.CRCerror) return;
  sgHist[sgSeq % SG_HIST_LEN] = sgValue;
  sgSeq++;

  if (stallState == STALL_WAIT) {
    sgSum += sgValue;
    if (++sgSamples >= 20) {
      sgBaseline = max<uint32_t>(sgSum / sgSamples, 1);
      stallState = STALL_ARMED;
      Serial.printf("StallGuard armed: baseline %u at %.1f RPM\n", sgBaseline, targetRPM);
    }
    return;
  }

  // ARMED
  int pct = (int)((uint32_t)sgValue * 100 / sgBaseline);
  sgRecentPct[sgRecentCount % STALL_WINDOW] = pct;
  if (sgRecentCount < 255) sgRecentCount++;
  if (sgRecentCount < STALL_WINDOW) return;

  // 取視窗內第 STALL_CONFIRM_SAMPLES 小的值：它低於門檻，就代表至少有 3 次低於門檻。
  // 最小餘裕也用同一個值，畫面上的數字可直接對照門檻
  int sorted[STALL_WINDOW];
  memcpy(sorted, sgRecentPct, sizeof(sorted));
  std::sort(sorted, sorted + STALL_WINDOW);
  int sustained = sorted[STALL_CONFIRM_SAMPLES - 1];
  if (sgMinPct < 0 || sustained < sgMinPct) sgMinPct = sustained;

  if (sustained < STALL_RATIO * 100) {
    stepper->forceStop();
    isRunning = false;
    clearTimer();
    stallState = STALL_TRIPPED;
    Serial.printf("⛔ Stall detected: SG %u (baseline %u) -> motor stopped\n", sgValue, sgBaseline);
  }
}

void handleSgHistory() {
  String json = "{\"seq\":" + String(sgSeq) + ",\"base\":" + String(sgBaseline) + ",\"v\":[";
  uint8_t n = min<uint32_t>(sgSeq, SG_HIST_LEN);
  for (uint8_t i = 0; i < n; i++) {
    if (i) json += ",";
    json += String(sgHist[(sgSeq - n + i) % SG_HIST_LEN]);
  }
  json += "]}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

void applySpeedAndRun() {
  if (!stepper) return;
  resetStallMonitor();
  
  // 計算每秒需要的脈衝數 (浮點數)
  float stepsPerSec = (targetRPM / 60.0) * (float)MOTOR_STEPS_PER_REV * (float)currentMicrostep;
  
  // 轉為整數 Hz，若大於 0 但算出來是 0，則強制補上最低 1 Hz
  uint32_t speedHz = (uint32_t)round(stepsPerSec);
  if (speedHz == 0 && targetRPM > 0.0f) speedHz = 1;
  
  // 計算加速度，並確保加速度最低為 1
  uint32_t accel = (uint32_t)round(stepsPerSec * 0.2);
  if (accel == 0) accel = 1;
  
  stepper->setSpeedInHz(speedHz);
  stepper->setAcceleration(accel); 
  
  if (isRunning) {
    if (targetDirection == 1) stepper->runForward();
    else stepper->runBackward();
  }
}

void updateMotorParameters() {
  if (!stepper) return;
  targetRPM = constrain(targetRPM, MIN_ALLOWED_RPM, MAX_ALLOWED_RPM);

  int optimalMicrostep = 16;
  if (targetRPM <= 45.0) {
    optimalMicrostep = 64; 
  } else if (targetRPM <= SPREADCYCLE_RPM_THRESHOLD) {
    optimalMicrostep = 32; 
  } else {
    optimalMicrostep = 16; 
  }

  bool wantSpread = targetRPM > SPREADCYCLE_RPM_THRESHOLD;

  if (optimalMicrostep != currentMicrostep || wantSpread != isSpreadCycle) {
    if (isRunning && stepper->isRunning()) {
      // 運轉中：先減速停止，由 loop() 在停止後套用新設定再重新啟動
      stepper->stopMove();
      pendingMicrostep = optimalMicrostep;
      pendingSpread = wantSpread;
      isMicrostepChangePending = true;
      return;
    }
    applyDriverMode(optimalMicrostep, wantSpread);
  }
  // 減速途中又調回原本的區間：取消待切換，直接恢復運轉
  isMicrostepChangePending = false;

  applySpeedAndRun();
}

// 停止或計時結束時清除計時器，連同時/分/秒一起歸零，避免下次只改一欄時混入舊值
void clearTimer() {
  timerDuration = 0;
  timerStartHour = 0;
  tempHour = tempMin = tempSec = 0;
}

void handleRoot() {
  String page = String(INDEX_HTML);
  
  page.replace("<div id='fw_ver' class='fw-ver'></div>", "<div id='fw_ver' class='fw-ver'>FW " + String(FW_VERSION) + " &middot; " + String(FW_BUILD) + "</div>");
  
  // 讓 RPM 顯示支援小數點
  String rpmStr = (targetRPM < 10.0) ? String(targetRPM, 1) : String((int)round(targetRPM));  // 與網頁 JS 的 Math.round 一致
  page.replace("id='rpm_txt'>60", "id='rpm_txt'>" + rpmStr);

  // 把目前的轉向、計時器、電流與運轉狀態帶給網頁，開啟頁面時直接顯示正確狀態
  char stateJson[192];
  snprintf(stateJson, sizeof(stateJson),
           "{\"dir\":%d,\"t_h\":%d,\"t_m\":%d,\"t_s\":%d,\"ma\":%u,\"run\":%s,\"presets\":[%g,%g,%g]}",
           targetDirection, tempHour, tempMin, tempSec, currentMA, isRunning ? "true" : "false",
           presetRpm[0], presetRpm[1], presetRpm[2]);
  page.replace("{/*SERVER_STATE*/}", stateJson);
  
  server.send(200, "text/html", page); 
}

// 即時狀態 (網頁每秒輪詢一次)。
// 取代原本的 SSE：WebServer 一次只能處理一個連線，SSE 每次送完就斷線重連，瀏覽器會一直出現連線錯誤
void handleStatus() {
  String t_str = "Continuous";
  if (timerDuration > 0) {
    long r = isRunning ? ((long)timerDuration - (millis() - timerStartHour)) / 1000 : timerDuration / 1000;
    if (r < 0) r = 0;
    convertStepsToTimeStr(r, t_str);
  }

  String json = "{";
  json += "\"timer\":\"" + t_str + "\",";
  json += "\"temp\":\"" + plainTempStatus + "\",";
  json += "\"temp_level\":" + String(tempLevel) + ",";
  json += "\"running\":" + String(isRunning ? "true" : "false") + ",";
  json += "\"microstep\":" + String(currentMicrostep) + ",";
  json += "\"intpol\":" + String(isInterpolationEnabled ? "true" : "false") + ",";
  json += "\"mode\":\"" + String(isStealthActive ? "StealthChop" : "SpreadCycle") + "\",";
  const char* stallNames[] = { "off", "wait", "armed", "stall" };
  json += "\"stall\":\"" + String(stallNames[stallState]) + "\",";
  json += "\"sg\":" + String(sgValue) + ",";
  json += "\"sg_base\":" + String(sgBaseline) + ",";
  json += "\"wiring\":\"" + wiringAlert + "\",";
  json += "\"wiring_level\":" + String(wiringLevel) + ",";
  json += "\"cur_ma\":" + String(actualCurrentMA) + ",";
  json += "\"sg_min\":" + String(sgMinPct);
  json += "}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

// 診斷頁：顯示 TMC2209 原始暫存器值，用來確認 UART 接線
void handleDiag() {
  uint32_t ioin = driver.IOIN();
  bool ioinCrc = driver.CRCerror;
  uint32_t drv = driver.DRV_STATUS();
  bool drvCrc = driver.CRCerror;
  uint32_t gconf = driver.GCONF();
  char buf[256];
  snprintf(buf, sizeof(buf),
    "IOIN=0x%08lX (version=0x%02lX, expect 0x21) crc_err=%d\n"
    "DRV_STATUS=0x%08lX crc_err=%d\n"
    "GCONF=0x%08lX\n"
    "uart_fail_count=%u\n",
    (unsigned long)ioin, (unsigned long)(ioin >> 24), ioinCrc,
    (unsigned long)drv, drvCrc, (unsigned long)gconf, uartFailCount);
  String out = buf;

  // 掃描 UART 位址 0~3 (由 MS1/MS2 決定)，並測試 RX/TX 腳位對調
  const int pinSets[2][2] = { {16, 17}, {17, 16} };
  for (int p = 0; p < 2; p++) {
    Serial2.end();
    Serial2.begin(115200, SERIAL_8N1, pinSets[p][0], pinSets[p][1]);
    delay(20);
    for (uint8_t a = 0; a < 4; a++) {
      TMC2209Stepper probe(&Serial2, RSENSE_VALUE, a);
      uint32_t v = probe.IOIN();
      snprintf(buf, sizeof(buf), "scan rx=%d tx=%d addr=%u: IOIN=0x%08lX crc_err=%d%s\n",
        pinSets[p][0], pinSets[p][1], a, (unsigned long)v, probe.CRCerror,
        (!probe.CRCerror && (v >> 24) == 0x21) ? "  <-- FOUND" : "");
      out += buf;
    }
  }
  Serial2.end();
  Serial2.begin(115200, SERIAL_8N1, 16, 17);

  // 回音測試：單線 UART 時，TX 送出的位元組會經 1k 電阻回到 RX
  while (Serial2.available()) Serial2.read();
  const uint8_t pattern[4] = { 0xA5, 0x5A, 0x0F, 0xF0 };
  Serial2.write(pattern, sizeof(pattern));
  Serial2.flush();
  delay(10);
  int echoCount = 0;
  while (Serial2.available()) { Serial2.read(); echoCount++; }
  snprintf(buf, sizeof(buf), "echo test: sent 4 bytes, received %d\n", echoCount);
  out += buf;

  server.send(200, "text/plain", out);
}

// 讀取 rpm 參數：非數字、0 或負數視為無效並保留原值，其餘限制在 MIN~MAX 之間
bool parseRpmArg() {
  if (!server.hasArg("rpm")) return true;
  float v = server.arg("rpm").toFloat();
  if (!(v > 0.0f)) return false;  // toFloat() 解析失敗時回傳 0；NaN 也會在這裡被擋下
  targetRPM = constrain(v, MIN_ALLOWED_RPM, MAX_ALLOWED_RPM);
  return true;
}

void handleAPI() {
  // 儲存快捷鍵：/api?preset=0&set=150
  if (server.hasArg("preset")) {
    int i = server.arg("preset").toInt();
    float v = server.arg("set").toFloat();
    if (i < 0 || i >= PRESET_COUNT || !(v > 0.0f)) {
      server.send(400, "text/plain", "Invalid preset");
      return;
    }
    presetRpm[i] = constrain(v, MIN_ALLOWED_RPM, MAX_ALLOWED_RPM);
    markSettingsDirty();
    server.send(200, "text/plain", String(presetRpm[i], 1));
    return;
  }
  if (server.hasArg("ma")) {
    long ma = server.arg("ma").toInt();
    if (ma < MIN_CURRENT_MA || ma > MAX_CURRENT_MA) {
      server.send(400, "text/plain", "Current must be " + String(MIN_CURRENT_MA) + "-" + String(MAX_CURRENT_MA) + " mA");
      return;
    }
    // 運轉中突然改變電流可能失步，只允許停止時修改
    if (isRunning) {
      server.send(409, "text/plain", "Stop the motor before changing current");
      return;
    }
    currentMA = ma;
    driver.rms_current(currentMA);
    markSettingsDirty();
  }
  if (!parseRpmArg()) {
    server.send(400, "text/plain", "Invalid rpm");
    return;
  }
  if (server.hasArg("rpm") || server.hasArg("dir")) markSettingsDirty();
  if (server.hasArg("dir")) targetDirection = (server.arg("dir").toInt() == -1) ? -1 : 1;
  if (server.hasArg("t_h")) tempHour = max(0L, server.arg("t_h").toInt());
  if (server.hasArg("t_m")) tempMin = max(0L, server.arg("t_m").toInt());
  if (server.hasArg("t_s")) tempSec = max(0L, server.arg("t_s").toInt());
  if (server.hasArg("t_h") || server.hasArg("t_m") || server.hasArg("t_s")) {
    timerDuration = (unsigned long)(tempHour * 3600 + tempMin * 60 + tempSec) * 1000;
    if (isRunning && timerDuration > 0) timerStartHour = millis();
  }
  updateMotorParameters();
  server.send(200, "text/plain", "OK");
}

void handleStart() {
  if (isDriverFault) {
    server.send(503, "text/plain", faultMessage);
    return;
  }
  if (!parseRpmArg()) {
    server.send(400, "text/plain", "Invalid rpm");
    return;
  }
  if (server.hasArg("rpm")) markSettingsDirty();

  stallState = STALL_OFF;  // 清除上次的卡住狀態
  isRunning = true;
  if (stepper) stepper->setCurrentPosition(0);
  
  updateMotorParameters();
  
  if (timerDuration > 0) timerStartHour = millis();
  server.send(200, "text/plain", "OK");
}

void handleStop() {
  isRunning = false;
  isMicrostepChangePending = false;
  stallState = STALL_OFF;
  clearTimer();

  if (stepper) stepper->stopMove();
  server.send(200, "text/plain", "OK");
}

void setup() {
  Serial.begin(115200);
  Serial.printf("TMC2209 Motor Ctrl %s (build %s)\n", FW_VERSION, FW_BUILD);
  pinMode(enPin, OUTPUT); digitalWrite(enPin, HIGH); 
  
  Serial2.begin(115200, SERIAL_8N1, 16, 17); 
  delay(200);

  loadSettings();

  driver.begin();
  driver.toff(4);
  driver.rms_current(currentMA);
  driver.microsteps(currentMicrostep); 

  driver.intpol(true); 
  isInterpolationEnabled = true;

  // 不使用 CoolStep：明確關閉 (OTA 重開時驅動器不斷電，會保留上一版韌體寫入的設定)
  driver.semin(0);

  driver.en_spreadCycle(false); 
  driver.TPWMTHRS(0);  // 關閉晶片依速度自動切換，模式改由 applyDriverMode() 在停止時切換

  uint8_t result = driver.test_connection();
  if (result == 0) {
    Serial.println("✅ TMC2209 UART 通訊成功！");
  } else {
    Serial.printf("⚠️ TMC2209 UART 回應異常 (代碼: %d)，請檢查接線與 1K 電阻。\n", result);
  }

  engine.init();
  stepper = engine.stepperConnectToPin(stepPin);
  if (stepper != NULL) {
    stepper->setDirectionPin(dirPin);
    stepper->setEnablePin(enPin);
    stepper->setAutoEnable(true);
    Serial.println("✅ HardDrive engine initialized!");
    updateMotorParameters();  // 依讀回的轉速先設好微步與模式 (停止狀態，不會啟動)
  }

  WiFi.softAP(ap_ssid, ap_password);
  server.on("/", handleRoot);
  server.on("/api", handleAPI); 
  server.on("/status", handleStatus);
  server.on("/sg", handleSgHistory);
  server.on("/start", handleStart);
  server.on("/stop", handleStop);
  server.on("/diag", handleDiag);
  server.begin();

  ArduinoOTA.setHostname("ESP32-TMC2209-Control"); 
  ArduinoOTA.setPassword(SECRET_OTA_PASSWORD);              

  ArduinoOTA.onStart([]() {
    if (stepper) stepper->stopMove();
    Serial.println("Start updating firmware...");
  });

  ArduinoOTA.begin(); 
}

void loop() {
  ArduinoOTA.handle();
  server.handleClient();
  saveSettingsIfDue();
  monitorStall();
  
  if (isMicrostepChangePending && stepper != NULL && !stepper->isRunning()) {
    applyDriverMode(pendingMicrostep, pendingSpread);
    isMicrostepChangePending = false;
    
    if (isRunning) applySpeedAndRun(); 
  }

  static unsigned long lastCheck = 0;
  if (millis() - lastCheck >= 1000) {
    lastCheck = millis();
    checkTMC2209Temperature();
    if (isDriverFault && isRunning) {
      isRunning = false;
      isMicrostepChangePending = false;
      if (stepper) stepper->stopMove();
      Serial.println("⛔ " + faultMessage + " -> motor stopped");
    }
  }

  if (isRunning && timerDuration > 0 && (millis() - timerStartHour >= timerDuration)) {
    isRunning = false;
    clearTimer();
    if (stepper) stepper->stopMove();
  }
}