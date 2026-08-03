#include <WiFi.h>
#include <WebServer.h>
#include <TMCStepper.h>
#include <FastAccelStepper.h>
#include "index_html.h" 
#include <ArduinoOTA.h>

// =================【 自發 Wi-Fi 熱點與硬體常數設定 】=================
const char* ap_ssid     = "TMC2209_Motor_Ctrl"; 
const char* ap_password = "YOUR_WIFI_PASSWORD";           

const float RSENSE_VALUE = 0.11f;        // 驅動器感測電阻值 (TMC2209 通常為 0.11)
const uint8_t DRIVER_ADDRESS = 0;        // UART 設備位址 (MS1, MS2 皆接地或浮空為 0)
const uint16_t DEFAULT_CURRENT_MA = 500; // 預設運轉電流 (mA)
const int MOTOR_STEPS_PER_REV = 200;     // 馬達每圈步數 (1.8度)
const float MAX_ALLOWED_RPM = 450.0;     // 最高安全轉速限制
const uint32_t TPWMTHRS_RPM_THRESHOLD = 150; // 切換至 SpreadCycle 的轉速門檻 (RPM)
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
bool isInterpolationEnabled = true; // 記錄插值狀態
String plainTempStatus = "🟢 Temp Normal (< 120 deg C)"; 

// --- 用於非同步切換微步的狀態標記 ---
bool isMicrostepChangePending = false;
int pendingMicrostep = 16;

void convertStepsToTimeStr(long totalSeconds, String &outStr) {
  int h = totalSeconds / 3600;
  int m = (totalSeconds % 3600) / 60;
  int s = totalSeconds % 60;
  
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%dh %dm %ds", h, m, s);
  outStr = buffer; 
}

void checkTMC2209Temperature() {
  uint32_t drv_status = driver.DRV_STATUS(); 
  if ((drv_status >> 25) & 0x1) plainTempStatus = "🔴 Thermal Shutdown (> 150 deg C)"; 
  else if ((drv_status >> 24) & 0x1) plainTempStatus = "🟡 Driver Overheat (> 120 deg C)"; 
  else plainTempStatus = "🟢 Temp Normal (< 120 deg C)"; 
}

void applySpeedAndRun() {
  if (!stepper) return;
  float stepsPerSec = (targetRPM / 60.0) * MOTOR_STEPS_PER_REV * currentMicrostep;
  stepper->setSpeedInHz((uint32_t)stepsPerSec);
  stepper->setAcceleration((uint32_t)(stepsPerSec * 0.5)); 
  
  if (isRunning) {
    if (targetDirection == 1) stepper->runForward();
    else stepper->runBackward();
  }
}

void updateMotorParameters() {
  if (!stepper) return;
  if (targetRPM > MAX_ALLOWED_RPM) targetRPM = MAX_ALLOWED_RPM;

  int optimalMicrostep = 16;
  if (targetRPM <= 45.0) {
    optimalMicrostep = 64; 
  } else if (targetRPM <= 150.0) {
    optimalMicrostep = 32; 
  } else {
    optimalMicrostep = 16; 
  }

  if (optimalMicrostep != currentMicrostep) {
    if (isRunning && stepper->isRunning()) {
      stepper->stopMove();
      pendingMicrostep = optimalMicrostep;
      isMicrostepChangePending = true;
      return;
    } else {
      currentMicrostep = optimalMicrostep;
      driver.microsteps(currentMicrostep);
      delay(10);
    }
  }

  applySpeedAndRun();
}

void handleRoot() {
  String page = String(INDEX_HTML);
  
  String btnHtml = isRunning ? "<button class='btn-stop' onclick='triggerAction(\"stop\")'>■ STOP MOTOR</button>" : "<button class='btn-start' onclick='triggerAction(\"start\")'>▶ START MOTOR</button>";
  String badgeHtml = isRunning ? "<span class='status-badge badge-run'>▶ RUNNING</span>" : "<span class='status-badge badge-stop'>■ STOPPED</span>";
  
  page.replace("<div id='action_btn_area'></div>", btnHtml);
  page.replace("<span id='status_box_placeholder'></span>", badgeHtml);
  
  int rpmInt = (int)targetRPM;
  page.replace("id='rpm_txt'>60", "id='rpm_txt'>" + String(rpmInt));
  page.replace("value='60'", "value='" + String(rpmInt) + "'");
  
  server.send(200, "text/html", page); 
}

void handleLiveStream() {
  WiFiClient client = server.client();
  client.println("HTTP/1.1 200 OK\nContent-Type: text/event-stream\nCache-Control: no-cache\nConnection: keep-alive\n");
  
  String t_str = "Continuous"; 
  if (timerDuration > 0) {
    long r = isRunning ? ((long)timerDuration - (millis() - timerStartHour)) / 1000 : timerDuration / 1000;
    if (r < 0) r = 0;
    convertStepsToTimeStr(r, t_str);
  }

  // =================【 1. 讀取與計算 TMC2209 診斷狀態 】=================
  // 檢查插值狀態 (若驅動器有回應或軟體設定成功)
  bool isIntpol = isInterpolationEnabled; 
  
  // 檢查 CoolStep 是否啟用
  bool isCoolStep = (driver.semin() > 0); 
  
  // 判斷當前轉速是否已超越切換門檻
  String modeStr = (targetRPM >= TPWMTHRS_RPM_THRESHOLD) ? "SpreadCycle" : "StealthChop";
  // ====================================================================

  // =================【 2. 打包成 JSON 傳給前端 】=================
  client.print("retry: 1000\n");
  client.print("data: {");
  client.print("\"timer\":\"" + t_str + "\",");
  client.print("\"temp\":\"" + plainTempStatus + "\",");
  client.print("\"running\":" + String(isRunning ? "true" : "false") + ",");
  client.print("\"microstep\":" + String(currentMicrostep) + ",");
  client.print("\"intpol\":" + String(isIntpol ? "true" : "false") + ",");
  client.print("\"coolstep\":" + String(isCoolStep ? "true" : "false") + ",");
  client.print("\"mode\":\"" + modeStr + "\"");
  client.print("}\n\n");
  client.flush();
}

void handleAPI() {
  if (server.hasArg("rpm")) targetRPM = server.arg("rpm").toFloat();
  if (server.hasArg("dir")) targetDirection = server.arg("dir").toInt();
  if (server.hasArg("t_h")) tempHour = server.arg("t_h").toInt();
  if (server.hasArg("t_m")) tempMin = server.arg("t_m").toInt();
  if (server.hasArg("t_s")) tempSec = server.arg("t_s").toInt();
  if (server.hasArg("t_h") || server.hasArg("t_m") || server.hasArg("t_s")) {
    timerDuration = (unsigned long)(tempHour * 3600 + tempMin * 60 + tempSec) * 1000;
    if (isRunning && timerDuration > 0) timerStartHour = millis();
  }
  updateMotorParameters();
  server.send(200, "text/plain", "OK");
}

void handleStart() {
  if (server.hasArg("rpm")) {
    targetRPM = server.arg("rpm").toFloat();
  }
  
  isRunning = true;
  if (stepper) stepper->setCurrentPosition(0);
  
  updateMotorParameters();
  
  if (timerDuration > 0) timerStartHour = millis();
  server.send(200, "text/plain", "OK");
}

void handleStop() {
  isRunning = false;
  isMicrostepChangePending = false; 
  
  timerDuration = 0;    // 重設倒數時間
  timerStartHour = 0;   // 重設起始時間
  
  if (stepper) stepper->stopMove();
  server.send(200, "text/plain", "OK");
}

void setup() {
  Serial.begin(115200);
  pinMode(enPin, OUTPUT); digitalWrite(enPin, HIGH); 
  
  // 初始化 UART2 (RX2: GPIO 16, TX2: GPIO 17)
  Serial2.begin(115200, SERIAL_8N1, 16, 17); 
  delay(200);

  // =================【 TMC2209 初始化與效能優化 】=================
  driver.begin(); 
  driver.toff(4); 
  driver.rms_current(DEFAULT_CURRENT_MA); 
  driver.microsteps(currentMicrostep); 

  // 1. 開啟 256 微步插值
  driver.intpol(true); 
  isInterpolationEnabled = true;

  // 2. 開啟 CoolStep 動態調節
  driver.semin(4); 
  driver.semax(2); 

  // 3. 設定 StealthChop 與 SpreadCycle 自動切換門檻
  driver.en_spreadCycle(false); 
  driver.TPWMTHRS(200); 

  // 4. 測試 UART 連線並印出至 Serial Monitor
  uint8_t result = driver.test_connection();
  if (result == 0) {
    Serial.println("✅ TMC2209 UART 通訊成功！");
  } else {
    Serial.printf("⚠️ TMC2209 UART 回應異常 (代碼: %d)，請檢查接線與 1K 電阻。\n", result);
  }
  // ===============================================================

  engine.init();
  stepper = engine.stepperConnectToPin(stepPin);
  if (stepper != NULL) {
    stepper->setDirectionPin(dirPin);
    stepper->setEnablePin(enPin);
    stepper->setAutoEnable(true); 
    Serial.println("✅ HardDrive engine initialized!");
  }

  WiFi.softAP(ap_ssid, ap_password);
  server.on("/", handleRoot);
  server.on("/api", handleAPI); 
  server.on("/live-stream", handleLiveStream); 
  server.on("/start", handleStart);
  server.on("/stop", handleStop);
  server.begin();

  // 設定 OTA
  ArduinoOTA.setHostname("ESP32-TMC2209-Control"); 
  ArduinoOTA.setPassword("admin123");              

  ArduinoOTA.onStart([]() {
    if (stepper) stepper->stopMove();
    Serial.println("Start updating firmware...");
  });

  ArduinoOTA.begin(); 
}

void loop() {
  ArduinoOTA.handle(); 
  server.handleClient();
  
  if (isMicrostepChangePending && stepper != NULL && !stepper->isRunning()) {
    currentMicrostep = pendingMicrostep;
    driver.microsteps(currentMicrostep);
    delay(10); 
    isMicrostepChangePending = false;
    
    if (isRunning) applySpeedAndRun(); 
  }

  static unsigned long lastCheck = 0;
  if (millis() - lastCheck >= 1000) {
    lastCheck = millis();
    checkTMC2209Temperature(); 
    if (plainTempStatus.indexOf("🔴") >= 0 && isRunning) {
      isRunning = false; if (stepper) stepper->stopMove();
    }
  }

  // 倒數計時器到期歸零
  if (isRunning && timerDuration > 0 && (millis() - timerStartHour >= timerDuration)) {
    isRunning = false; 
    timerDuration = 0;    
    timerStartHour = 0;   
    if (stepper) stepper->stopMove();
  }
}