#include <Keypad.h>
#include <ESP32Servo.h>
#include <LiquidCrystal_I2C.h>
#include "DHT.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

/* ================= GPIO ================= */
#define PIR_PIN          4
#define SWITCH_MODE     18
#define BTN_HEATER       5
#define LED_PIR         17
#define LED_HEATER      19
#define LED_WARNING      2
#define LED_WATER_HEATER 16
#define SERVO_PIN       14
#define DHT_PIN         15

#define DHTTYPE DHT22
DHT dht(DHT_PIN, DHTTYPE);

/* ================= THIẾT BỊ ================= */
LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo doorServo;

/* ================= KEYPAD ================= */
const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
  {'1','2','3','A'}, {'4','5','6','B'},
  {'7','8','9','C'}, {'*','0','#','D'}
};

byte rowPins[ROWS] = {32, 33, 25, 26};
byte colPins[COLS] = {27, 12, 13, 23};

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

/* ================= CẤU HÌNH WIFI & API ================= */
const char* wokwi_ssid = "Wokwi-GUEST";
const char* wokwi_password = "";
const char* apiBaseUrl = "https://iot-smart-home-app.vercel.app";
String roomId = "";
bool wifiConnected = false;
unsigned long lastApiCall = 0;
const unsigned long apiInterval = 1000; // Gọi API mỗi 1 giây (định kỳ, không block nút)

// Biến global để share data với API task
struct SensorData {
  float roomTemp;
  float waterTemp;
  bool hasMotion;
  bool mode; // true = auto, false = manual
  bool heaterOn;
  bool waterHeaterOn;
  bool needUpdate;
};

SensorData sensorData = {0, 0, false, false, false, false, false};
TaskHandle_t apiTaskHandle = NULL;

// Biến để nhận control commands từ app
struct ControlCommands {
  bool hasCommand;
  bool heaterOn;
  bool waterHeaterOn;
  bool mode; // true = auto, false = manual
};

ControlCommands serverCommands = {false, false, false, false};
TaskHandle_t controlTaskHandle = NULL;
unsigned long lastManualButtonPress = 0; // Track thời gian bấm nút vật lý cuối cùng
const unsigned long MANUAL_BUTTON_PRIORITY_TIME = 20000; // Chặn controlTask trong 20 giây sau khi bấm nút vật lý
unsigned long lastDataSentTime = 0; // Track thời gian gửi data lên DB cuối cùng
String appPassword = ""; // Password từ app để unlock
unsigned long long lastUnlockRequestTime = 0; // Thời gian request unlock từ app

// Biến để track thời gian gửi cuối cùng (dùng chung giữa loop() và apiTask())
unsigned long lastApiUpdate = 0;
volatile bool isSending = false; // Flag để biết apiTask() đang gửi HTTP
SemaphoreHandle_t dataMutex = NULL; // Mutex để đồng bộ truy cập sensorData

/* ================= BIẾN HỆ THỐNG ================= */
String password = "1234";
String inputPassword = "";
bool isUnlocked = false; // Đổi từ isAuthenticated sang isUnlocked để đồng bộ với Bed_room
unsigned long lockUntil = 0;
int wrongCount = 0;

float roomTemp = 0;
float waterTemp = 40.0;
int currentHour = 19;

bool lastWaterState = false;
bool lastHeaterState = false;
bool manualHeaterToggle = false;
bool lastBtnState = HIGH;
bool autoMode = true; // Mode hiện tại (true = auto, false = manual) - có thể được set từ app hoặc switch vật lý
bool lastAutoMode = true; 

// ================== FUNCTION PROTOTYPES ==================
void handleKeypad();
void showLockedScreen();
void handleWaterHeater(bool hasPerson);
void manualMode();
void autoModeLogic(bool hasPerson);
void updateLCD();
void connectWiFi();
void sendSensorDataNonBlocking();
String getRoomIdByName();
void apiTask(void *pvParameters); // FreeRTOS task function
void controlTask(void *pvParameters); // FreeRTOS task function để fetch control commands
void forceDataUpdate(); // Force gửi data lên DB ngay khi bấm nút vật lý
void forceUnlockStatusUpdate(); // Force gửi trạng thái unlock lên DB
void sendMotionNotification(); // Gửi push notification khi phát hiện motion

void setup() {
  Serial.begin(115200);
  dht.begin();
  pinMode(PIR_PIN, INPUT);
  pinMode(SWITCH_MODE, INPUT_PULLUP);
  pinMode(BTN_HEATER, INPUT_PULLUP);
  pinMode(LED_PIR, OUTPUT);
  pinMode(LED_HEATER, OUTPUT);
  pinMode(LED_WARNING, OUTPUT);
  pinMode(LED_WATER_HEATER, OUTPUT);

  doorServo.attach(SERVO_PIN);
  doorServo.write(0);
  lcd.init();
  lcd.backlight();
  
  // Kết nối WiFi (không block)
  connectWiFi();
  if (wifiConnected) {
    roomId = getRoomIdByName();
    if (roomId != "") {
      Serial.print("Room ID found: ");
      Serial.println(roomId);
      
      // Tạo mutex để đồng bộ truy cập sensorData
      dataMutex = xSemaphoreCreateMutex();
      if (dataMutex == NULL) {
        Serial.println("Failed to create mutex!");
      }
      
      // Tạo FreeRTOS task để gửi API trong background
      xTaskCreate(
        apiTask,           // Task function
        "API_Task",        // Task name
        8192,              // Stack size
        NULL,              // Parameters
        1,                 // Priority (low)
        &apiTaskHandle     // Task handle
      );
      Serial.println("API Task created");
      
      // Tạo FreeRTOS task để fetch control commands từ app
      xTaskCreate(
        controlTask,       // Task function
        "Control_Task",    // Task name
        8192,              // Stack size
        NULL,              // Parameters
        1,                 // Priority (low)
        &controlTaskHandle // Task handle
      );
      Serial.println("Control Task created");
      
      // Gửi trạng thái unlock ban đầu lên DB (isUnlocked = false khi khởi động)
      // Đợi một chút để đảm bảo tasks đã sẵn sàng
      delay(1000);
      forceUnlockStatusUpdate();
      Serial.println("[SETUP] Sent initial unlock status (false) to DB");
      
      // Clear password cũ trong DB khi khởi động để tránh nhận password cũ
      HTTPClient clearHttp;
      String clearUrl = String(apiBaseUrl) + "/api/rooms/" + roomId;
      clearHttp.begin(clearUrl);
      clearHttp.addHeader("Content-Type", "application/json");
      clearHttp.setTimeout(3000);
      clearHttp.setConnectTimeout(2000);
      DynamicJsonDocument clearDoc(256);
      clearDoc["unlockPassword"] = "";
      clearDoc["unlockRequestTime"] = 0;
      clearDoc["unlockMessage"] = "";
      String clearJson;
      serializeJson(clearDoc, clearJson);
      int clearCode = clearHttp.PUT(clearJson);
      clearHttp.end();
      Serial.print("[SETUP] Cleared old unlock data from DB, code: ");
      Serial.println(clearCode);
      
      // Reset lastUnlockRequestTime để đảm bảo chỉ nhận password mới
      lastUnlockRequestTime = 0;
      Serial.println("[SETUP] Reset lastUnlockRequestTime to 0");
    }
  }
}

void loop() {
  // 1. Kiểm tra trạng thái khóa hệ thống (Khi nhập sai)
  if (millis() < lockUntil) {
    int remain = (lockUntil - millis()) / 1000;
    lcd.setCursor(0, 1);
    lcd.print("KHOA: "); lcd.print(remain); lcd.print("s    ");
    return;
  }

  // 2. Nếu chưa mở khóa: Chỉ chạy Keypad, KHÔNG cho phép điều khiển từ app
  if (!isUnlocked) {
  handleKeypad();
    showLockedScreen();
    return;
  }
  
  // 3. Nếu đã mở khóa: Chạy logic điều khiển thiết bị đầy đủ

  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate > 1000) {
    float t = dht.readTemperature();
    if (!isnan(t)) roomTemp = t;
    lastUpdate = millis();
  }

  bool hasPerson = digitalRead(PIR_PIN);
  digitalWrite(LED_PIR, hasPerson);
  handleWaterHeater(hasPerson);

  // Đọc mode từ switch vật lý (mặc định)
  // LOW = manual (false), HIGH = auto (true) - giống code gốc
  // autoMode: true = auto, false = manual
  bool switchMode = (digitalRead(SWITCH_MODE) == HIGH); // HIGH = auto (true), LOW = manual (false)

  // Xử lý commands từ app TRƯỚC (chỉ khi đã unlock)
  unsigned long now = millis();
  bool recentlyPressedButton = (now - lastManualButtonPress) < MANUAL_BUTTON_PRIORITY_TIME;
  unsigned long timeSinceLastDataSent = (now - lastDataSentTime);
  bool dataJustSent = (lastDataSentTime > 0) && (timeSinceLastDataSent < 12000); // 12 giây
  
  if (serverCommands.hasCommand && !recentlyPressedButton && !dataJustSent && !isSending) {
    bool hasChange = (serverCommands.heaterOn != lastHeaterState) ||
                     (serverCommands.waterHeaterOn != lastWaterState) ||
                     (serverCommands.mode != autoMode);
    
    if (hasChange) {
      // Áp dụng commands từ app
      lastHeaterState = serverCommands.heaterOn;
      lastWaterState = serverCommands.waterHeaterOn;
      manualHeaterToggle = serverCommands.heaterOn;
      
      // Mode từ app - ưu tiên mode từ app khi có command
      if (serverCommands.mode != autoMode) {
        autoMode = serverCommands.mode;
        // KHÔNG cập nhật lastAutoMode ở đây
        // lastAutoMode sẽ được cập nhật từ switch vật lý ở đầu hàm
        // Điều này đảm bảo switch vật lý chỉ ghi đè khi thực sự thay đổi
        Serial.print("[Control] Mode changed from app: ");
        Serial.println(autoMode ? "AUTO" : "MANUAL");
      }
      
      // Update LED
      digitalWrite(LED_HEATER, lastHeaterState);
      digitalWrite(LED_WATER_HEATER, lastWaterState);
      
      // Cập nhật nhiệt độ nước khi bật/tắt từ app
      if (lastWaterState) {
        waterTemp += 0.1;
      } else if (waterTemp > 25) {
        waterTemp -= 0.05;
      }
      
      Serial.print("[Control] Applied from app: Mode=");
      Serial.print(autoMode ? "AUTO" : "MANUAL");
      Serial.print(" Heater=");
      Serial.print(lastHeaterState);
      Serial.print(" WaterHeater=");
      Serial.println(lastWaterState);
    }
    serverCommands.hasCommand = false;
  }
  
  // SAU KHI xử lý command từ app, mới kiểm tra switch vật lý
  // Chỉ cập nhật autoMode từ switch vật lý nếu switch thay đổi
  // (không ghi đè mode từ app)
  if (switchMode != lastAutoMode) {
    // Switch vật lý thay đổi
    autoMode = switchMode; // Cập nhật autoMode khi switch vật lý thay đổi
    Serial.print("[Switch] Mode changed to: ");
    Serial.println(autoMode ? "AUTO" : "MANUAL");
  }
  // Luôn cập nhật lastAutoMode thành giá trị switch vật lý (không phải giá trị từ app)
  // Điều này đảm bảo chỉ phát hiện thay đổi khi switch vật lý thực sự thay đổi
  lastAutoMode = switchMode;

  if (autoMode) {
    autoModeLogic(hasPerson);
  } else {
    manualMode();
    // Ở chế độ manual, vẫn cần cập nhật nhiệt độ nước dựa trên lastWaterState
    if (lastWaterState) {
      waterTemp += 0.1;
    } else if (waterTemp > 25) {
      waterTemp -= 0.05;
    }
  }

  updateLCD();
  
  // Cập nhật sensor data để API task gửi (không block)
  if (apiTaskHandle != NULL) {
    // Kiểm tra xem có thay đổi đáng kể không (để gửi ngay)
    static float lastSentRoomTemp = -999;
    static float lastSentWaterTemp = -999;
    static bool lastSentHeater = false;
    static bool lastSentWaterHeater = false;
    static bool lastSentMotion = false;
    static bool lastSentMode = -1; // Khởi tạo = -1 để force gửi lần đầu
    
    // Đọc nhiệt độ TRỰC TIẾP từ DHT
    float currentRoomTemp = dht.readTemperature();
    if (isnan(currentRoomTemp)) {
      currentRoomTemp = roomTemp;
}

    // Đọc motion TRỰC TIẾP từ sensor
    bool currentMotion = digitalRead(PIR_PIN);
    
    // KIỂM TRA MOTION TRƯỚC - gửi notification ngay khi phát hiện motion mới (không phụ thuộc vào hasSignificantChange)
    if (currentMotion && !lastSentMotion) {
      Serial.println("[LOOP] Motion detected (false -> true), sending push notification...");
      sendMotionNotification(); // Gọi ngay, không đợi hasSignificantChange
    }
    
    // Đọc mode từ switch vật lý để so sánh (nhưng sẽ gửi autoMode lên DB - giá trị thực tế)
    // Với INPUT_PULLUP: LOW = switch ON (nối GND), HIGH = switch OFF (không nối)
    // Logic: HIGH (switch OFF) = auto, LOW (switch ON) = manual
    bool switchMode = (digitalRead(SWITCH_MODE) == HIGH); // HIGH = auto (true), LOW = manual (false)
    
    // Kiểm tra xem có thay đổi đáng kể không
    // Dùng autoMode (giá trị thực tế) để so sánh, không phải switchMode
    bool motionChanged = (currentMotion != lastSentMotion);
    bool hasSignificantChange = 
      (abs(currentRoomTemp - lastSentRoomTemp) > 0.5) ||  // Nhiệt độ phòng thay đổi > 0.5°C
      (abs(waterTemp - lastSentWaterTemp) > 0.5) ||        // Nhiệt độ nước thay đổi > 0.5°C
      (lastHeaterState != lastSentHeater) ||               // Heater thay đổi
      (lastWaterState != lastSentWaterHeater) ||           // Water heater thay đổi
      motionChanged ||                                      // Motion thay đổi
      (lastSentMode == -1 || autoMode != lastSentMode);    // Mode thay đổi (dùng autoMode - giá trị thực tế) hoặc lần đầu
    
    // Debug log khi motion thay đổi
    if (motionChanged) {
      Serial.print("[LOOP] Motion change detected: ");
      Serial.print(lastSentMotion ? "YES" : "NO");
      Serial.print(" -> ");
      Serial.print(currentMotion ? "YES" : "NO");
      Serial.print(" (hasSignificantChange: ");
      Serial.print(hasSignificantChange);
      Serial.println(")");
    }
    
    // Dùng mutex để đồng bộ truy cập sensorData (tránh race condition)
    if (hasSignificantChange && dataMutex != NULL) {
      if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        // Chỉ set needUpdate nếu apiTask() đã xử lý xong request trước đó
        if (!sensorData.needUpdate && !isSending) {
          // Cập nhật data TRƯỚC khi đánh dấu needUpdate
          sensorData.roomTemp = currentRoomTemp;
          sensorData.waterTemp = waterTemp;
          sensorData.hasMotion = currentMotion;
          sensorData.mode = autoMode; // Dùng autoMode (giá trị thực tế, có thể từ app hoặc switch)
          sensorData.heaterOn = lastHeaterState;
          sensorData.waterHeaterOn = lastWaterState;
          
          // Đánh dấu cần update SAU khi đã cập nhật data
          sensorData.needUpdate = true;
          
          // Log motion change để debug
          if (currentMotion != lastSentMotion) {
            Serial.print("[LOOP] Motion changed: ");
            Serial.print(lastSentMotion ? "YES" : "NO");
            Serial.print(" -> ");
            Serial.println(currentMotion ? "YES" : "NO");
            // Note: sendMotionNotification() đã được gọi ở trên (trước khi vào block này)
          }
          
          // Log mode change để debug
          if (lastSentMode == -1 || autoMode != lastSentMode) {
            Serial.print("[LOOP] Mode changed: ");
            if (lastSentMode == -1) {
              Serial.print("INIT");
            } else {
              Serial.print(lastSentMode ? "AUTO" : "MANUAL");
            }
            Serial.print(" -> ");
            Serial.print(autoMode ? "AUTO" : "MANUAL");
            Serial.print(" (switchMode: ");
            Serial.print(switchMode ? "AUTO" : "MANUAL");
            Serial.println(")");
          }
          
          // Lưu giá trị đã gửi SAU KHI set needUpdate
          lastSentRoomTemp = currentRoomTemp;
          lastSentWaterTemp = waterTemp;
          lastSentHeater = lastHeaterState;
          lastSentWaterHeater = lastWaterState;
          lastSentMotion = currentMotion; // Cập nhật motion TRƯỚC khi lưu mode
          lastSentMode = autoMode; // Lưu autoMode (giá trị thực tế) để so sánh lần sau
        } else {
          // apiTask() đang xử lý, nhưng có thay đổi đáng kể
          // Chỉ cập nhật data (không set needUpdate) để apiTask() đọc được giá trị mới nhất
          sensorData.roomTemp = currentRoomTemp;
          sensorData.waterTemp = waterTemp;
          sensorData.hasMotion = currentMotion;
          sensorData.mode = autoMode; // Dùng autoMode (giá trị thực tế, có thể từ app hoặc switch)
          sensorData.heaterOn = lastHeaterState;
          sensorData.waterHeaterOn = lastWaterState;
        }
        xSemaphoreGive(dataMutex);
      }
    }
  }
}

/* ============ LOGIC AUTO: SỬA LỖI TẮT Ở 27 ĐỘ ============ */
void autoModeLogic(bool hasPerson) {
  bool shouldHeat = lastHeaterState;
  String reason = "";

  // Logic tự động local
  if (hasPerson) {
    // 1. Nếu quá lạnh (<20): Luôn bật
    if (roomTemp < 20.0) {
      shouldHeat = true;
      reason = "QUÁ LẠNH (<20C)";
    } 
    // 2. Nếu đang dưới 27 độ: Tiếp tục sưởi
    else if (roomTemp < 27.0) {
      shouldHeat = true;
      reason = "ĐANG SƯỞI (<27C)";
    }
    // 3. Nếu ĐÃ ĐẠT >= 27 độ: Tắt ngay lập tức
    else if (roomTemp >= 27.0) {
      shouldHeat = false;
    }
  } else {
    // Không có người: Tắt lò sưởi
    shouldHeat = false;
  }

  digitalWrite(LED_HEATER, shouldHeat);

  if (shouldHeat != lastHeaterState) {
    Serial.print("LOG LO SUOI (AUTO): ");
    if (shouldHeat) Serial.println(reason);
    else Serial.println("TAT (DA DAT 27C HOAC VANG NGUOI)");
    lastHeaterState = shouldHeat;
    manualHeaterToggle = shouldHeat;
  }
}

void manualMode() {
  // Điều khiển bằng nút local - luôn hoạt động
  static unsigned long lastPress = 0;
  unsigned long now = millis();
  bool currentBtnState = digitalRead(BTN_HEATER);

  // Debounce ngắn hơn và áp dụng ngay
  if (currentBtnState == LOW && lastBtnState == HIGH && now - lastPress > 50) {
    Serial.println("[BUTTON] HEATER pressed - START");
    manualHeaterToggle = !manualHeaterToggle;
    digitalWrite(LED_HEATER, manualHeaterToggle);
    lastHeaterState = manualHeaterToggle;
    lastPress = now;
    lastManualButtonPress = now; // Track thời gian bấm nút
    Serial.print("[BUTTON] HEATER pressed - DONE, Heater=");
    Serial.println(manualHeaterToggle ? "ON" : "OFF");
    
    // Force gửi data lên DB ngay để tránh controlTask fetch data cũ
    forceDataUpdate();
  }
  lastBtnState = currentBtnState;
}

void handleWaterHeater(bool hasPerson) {
  // Logic tự động local
  bool shouldHeatWater = lastWaterState;
  if (hasPerson && currentHour >= 18 && currentHour <= 20) {
    if (waterTemp < 43.0) shouldHeatWater = true;
    else if (waterTemp >= 45.0) shouldHeatWater = false;
  } else {
    shouldHeatWater = false;
  }

  digitalWrite(LED_WATER_HEATER, shouldHeatWater);
  if (shouldHeatWater != lastWaterState) {
    Serial.print("LOG BINH NONG LANH: ");
    Serial.println(shouldHeatWater ? "BAT" : "TAT");
    lastWaterState = shouldHeatWater;
  }

  if (shouldHeatWater) waterTemp += 0.1;
  else if (waterTemp > 25) waterTemp -= 0.05;
}

void handleKeypad() {
  char key = keypad.getKey();
  if (!key) return;

  if (key == '#') {
    if (inputPassword == password) { 
      isUnlocked = true; 
      wrongCount = 0;
      inputPassword = "";
      doorServo.write(90);
      digitalWrite(LED_WARNING, LOW); // Tắt đèn cảnh báo nếu trước đó có sai
      lcd.clear();
      lcd.print("DOOR OPENED!");
      Serial.println("Log: Mat khau dung. He thong kich hoat.");
      
      // Force gửi trạng thái unlock lên DB ngay
      forceUnlockStatusUpdate();
      
      delay(1500);
    } 
    else { 
      inputPassword = ""; 
      wrongCount++;
      lcd.clear();
      lcd.print("WRONG PASS");
      
      // BẬT ĐÈN CẢNH BÁO
      digitalWrite(LED_WARNING, HIGH);
      delay(1000);
      digitalWrite(LED_WARNING, LOW); // Tắt đèn để chuẩn bị nhập lại
      lcd.clear();
      
      // Khóa hệ thống nếu nhập sai quá 3 lần
      if (wrongCount >= 3) {
        lockUntil = millis() + 30000; // Khóa 30 giây
        wrongCount = 0;
        Serial.println("Log: Nhap sai qua 3 lan. He thong bi khoa 30s.");
    }
  }
  } else if (key == '*') { 
    inputPassword = ""; 
    digitalWrite(LED_WARNING, LOW);
  } else if (inputPassword.length() < 4) { 
    inputPassword += key; 
  }
}

void updateLCD() {
  static unsigned long lastLCD = 0;
  if (millis() - lastLCD < 500) return;
  lastLCD = millis();

  lcd.setCursor(0,0);
  lcd.print("T:"); lcd.print(roomTemp,1); lcd.print("C W:"); 
  lcd.print(waterTemp,1); lcd.print("C");
  lcd.setCursor(0,1);
  lcd.print(autoMode ? "MODE: AUTO     " : "MODE: MANUAL   ");
}

void showLockedScreen() {
  lcd.setCursor(0,0);
  lcd.print("NHAP MAT KHAU:");
  lcd.setCursor(0,1);
  lcd.print("PASS: "); lcd.print(inputPassword); lcd.print("    ");
}

/* ================= WIFI & API FUNCTIONS ================= */
void connectWiFi() {
  if (wifiConnected) return;
  
  Serial.println("Connecting to Wokwi-GUEST...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(wokwi_ssid, wokwi_password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  Serial.println("");
  
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.print("WiFi connected! IP: ");
    Serial.println(WiFi.localIP());
  } else {
    wifiConnected = false;
    Serial.println("WiFi connection failed!");
  }
}

String getRoomIdByName() {
  if (!wifiConnected) return "";
  
  HTTPClient http;
  String url = String(apiBaseUrl) + "/api/rooms";
  http.begin(url);
  http.setTimeout(10000);
  
  int httpCode = http.GET();
  String roomIdResult = "";
  
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    DynamicJsonDocument doc(2048);
    deserializeJson(doc, payload);
    
    JsonArray rooms = doc.as<JsonArray>();
    for (JsonObject room : rooms) {
      String type = room["type"].as<String>();
      if (type == "bathroom") {
        roomIdResult = room["_id"].as<String>();
        break;
      }
    }
  } else {
    Serial.printf("GET rooms failed, code: %d\n", httpCode);
  }
  
  http.end();
  return roomIdResult;
}

// Hàm cũ - không dùng nữa, giữ lại để tránh lỗi compile
void sendSensorDataNonBlocking() {
  // Không làm gì - đã được thay thế bởi apiTask
}

// FreeRTOS Task để gửi API trong background (KHÔNG BLOCK main loop)
void apiTask(void *pvParameters) {
  Serial.println("[API Task] Started - API calls will run in background");
  
  while (true) {
    // Đợi cho đến khi cần update
    if (wifiConnected && roomId != "" && dataMutex != NULL) {
      // Dùng mutex để đồng bộ truy cập sensorData
      if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        if (sensorData.needUpdate) {
          // Copy data TRƯỚC khi đánh dấu (để lấy giá trị mới nhất)
          float roomTemp = sensorData.roomTemp;
          float waterTemp = sensorData.waterTemp;
          bool motion = sensorData.hasMotion;
          bool mode = sensorData.mode;
          bool heater = sensorData.heaterOn;
          bool waterHeater = sensorData.waterHeaterOn;
          
          // Đánh dấu đang gửi HTTP và đã lấy data
          isSending = true;
          sensorData.needUpdate = false;
          
          xSemaphoreGive(dataMutex);
          
          Serial.print("[API Task] Sending data...");
      
          // Gửi HTTP request (có thể mất vài giây, nhưng không block main loop)
          HTTPClient http;
          String url = String(apiBaseUrl) + "/api/rooms/" + roomId + "/data";
          http.begin(url);
          http.addHeader("Content-Type", "application/json");
          http.setTimeout(5000); // Timeout 5 giây
          http.setConnectTimeout(3000); // Timeout kết nối 3 giây
          
          DynamicJsonDocument doc(256);
          doc["roomTemp"] = roomTemp;
          doc["waterTemp"] = waterTemp;
          doc["hasMotion"] = motion;
          doc["mode"] = mode ? "auto" : "manual"; // mode từ sensorData (autoMode)
          doc["heaterOn"] = heater;
          doc["waterHeaterOn"] = waterHeater;
          doc["isUnlocked"] = isUnlocked; // Gửi trạng thái unlock lên DB
          
          // Debug log để kiểm tra mode được gửi
          Serial.print("[API Task] Sending mode: ");
          Serial.print(mode ? "auto" : "manual");
          Serial.print(" (mode value=");
          Serial.print(mode);
          Serial.println(")");
          
          String jsonString;
          serializeJson(doc, jsonString);
          
          unsigned long start = millis();
          int httpCode = http.POST(jsonString);
          unsigned long elapsed = millis() - start;
          
          http.end();
          
          if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_CREATED) {
            Serial.print("[API] OK ");
            Serial.print(elapsed);
            Serial.println("ms");
            // Cập nhật thời gian gửi data lên DB
            lastDataSentTime = millis();
          } else {
            Serial.print("[API] Fail ");
            Serial.print(httpCode);
            Serial.print(" (");
            Serial.print(elapsed);
            Serial.println("ms)");
          }
          
          // Cập nhật thời gian gửi cuối cùng và đánh dấu đã xử lý xong
          lastApiUpdate = millis();
          isSending = false;
          
          // Delay nhỏ để đảm bảo loop() đọc được giá trị mới (tránh race condition)
          vTaskDelay(pdMS_TO_TICKS(100));
          
          // Sau khi gửi xong, check ngay xem có needUpdate mới không
          // Nếu có, sẽ gửi tiếp ngay (bỏ qua delay)
          if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            if (sensorData.needUpdate && !isSending) {
              xSemaphoreGive(dataMutex);
              Serial.println("[API Task] New update detected, sending immediately...");
              continue; // Quay lại đầu vòng lặp để gửi tiếp
            }
            xSemaphoreGive(dataMutex);
          }
        } else {
          xSemaphoreGive(dataMutex);
        }
      }
    }
    
    // Delay để không chiếm CPU - FreeRTOS sẽ tự động switch tasks
    vTaskDelay(pdMS_TO_TICKS(100)); // Giảm xuống 0.1 giây để check nhanh hơn
  }
}

// FreeRTOS Task để fetch control commands từ app (KHÔNG BLOCK main loop)
void controlTask(void *pvParameters) {
  Serial.println("[Control Task] Started - Fetching control commands in background");
  
  while (true) {
    if (wifiConnected && roomId != "") {
      HTTPClient http;
      String url = String(apiBaseUrl) + "/api/rooms/" + roomId;
      http.begin(url);
      http.setTimeout(3000);
      http.setConnectTimeout(2000);
      
      int httpCode = http.GET();
      
      if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        DynamicJsonDocument doc(1024); // Tăng size để đảm bảo đủ chỗ
        DeserializationError error = deserializeJson(doc, payload);
        
        if (error) {
          Serial.print("[Control Task] JSON deserialize error: ");
          Serial.println(error.c_str());
        }
        
        // Check unlock password từ app - CHỈ khi chưa mở khóa
        if (!isUnlocked && doc.containsKey("unlockPassword") && doc.containsKey("unlockRequestTime")) {
          String newPassword = doc["unlockPassword"].as<String>();
          // Parse thành unsigned long long để chứa timestamp milliseconds (số lớn)
          unsigned long long requestTime = doc["unlockRequestTime"].as<unsigned long long>();
          
          Serial.print("[Control Task] Found unlockPassword: '");
          Serial.print(newPassword);
          Serial.print("' (length: ");
          Serial.print(newPassword.length());
          Serial.print("), requestTime: ");
          if (requestTime > 0xFFFFFFFF) {
            Serial.print((unsigned long)(requestTime >> 32));
            Serial.print(":");
          }
          Serial.print((unsigned long)(requestTime & 0xFFFFFFFF));
          Serial.print(", lastUnlockRequestTime: ");
          if (lastUnlockRequestTime > 0xFFFFFFFF) {
            Serial.print((unsigned long)(lastUnlockRequestTime >> 32));
            Serial.print(":");
          }
          Serial.print((unsigned long)(lastUnlockRequestTime & 0xFFFFFFFF));
          Serial.println();
          
          // Nếu password rỗng và requestTime = 0, reset lastUnlockRequestTime để cho phép nhận password mới
          if (newPassword.length() == 0 && requestTime == 0) {
            if (lastUnlockRequestTime > 0) {
              Serial.println("[Control Task] Password cleared in DB, resetting lastUnlockRequestTime");
              lastUnlockRequestTime = 0;
            }
          }
          
          // Chỉ xử lý nếu password không rỗng và là request mới (tránh xử lý lại request cũ)
          if (newPassword.length() > 0 && requestTime > lastUnlockRequestTime) {
            // Trim whitespace từ password
            newPassword.trim();
            appPassword = newPassword;
            lastUnlockRequestTime = requestTime;
            Serial.print("[Control Task] Received unlock password from app: '");
            Serial.print(appPassword);
            Serial.print("' (length: ");
            Serial.print(appPassword.length());
            Serial.println(")");
            
            // Verify password và unlock nếu đúng
            Serial.print("[Control Task] Comparing passwords - appPassword: '");
            Serial.print(appPassword);
            Serial.print("', stored password: '");
            Serial.print(password);
            Serial.print("' (length: ");
            Serial.print(password.length());
            Serial.println(")");
            
            if (appPassword == password) {
              isUnlocked = true;
              wrongCount = 0;
              inputPassword = "";
              doorServo.write(90);
              Serial.println("[Control Task] Password correct! System unlocked from app.");
              
              // Force gửi trạng thái unlock lên DB ngay
              forceUnlockStatusUpdate();
              
              // Clear unlock request và gửi thông báo success từ DB
              HTTPClient clearHttp;
              String clearUrl = String(apiBaseUrl) + "/api/rooms/" + roomId;
              clearHttp.begin(clearUrl);
              clearHttp.addHeader("Content-Type", "application/json");
              clearHttp.setTimeout(3000);
              clearHttp.setConnectTimeout(2000);
              DynamicJsonDocument clearDoc(256);
              clearDoc["unlockPassword"] = "";
              clearDoc["unlockRequestTime"] = 0;
              clearDoc["unlockMessage"] = "success"; // Thông báo thành công
              String clearJson;
              serializeJson(clearDoc, clearJson);
              
              int clearCode = clearHttp.PUT(clearJson);
              clearHttp.end();
              
              if (clearCode == HTTP_CODE_OK) {
                Serial.println("[Control Task] ✓ Success message sent to app successfully (HTTP 200)");
              } else {
                Serial.print("[Control Task] ✗ Failed to send success message, HTTP code: ");
                Serial.println(clearCode);
              }
            } else {
              Serial.println("[Control Task] Password incorrect from app - sending error message to DB...");
              
              // Gửi thông báo lỗi và clear unlock request từ DB để tránh xử lý lại
              HTTPClient errorHttp;
              String errorUrl = String(apiBaseUrl) + "/api/rooms/" + roomId;
              errorHttp.begin(errorUrl);
              errorHttp.addHeader("Content-Type", "application/json");
              errorHttp.setTimeout(3000);
              errorHttp.setConnectTimeout(2000);
              DynamicJsonDocument errorDoc(256);
              errorDoc["unlockPassword"] = ""; // Clear password
              errorDoc["unlockRequestTime"] = 0; // Clear request time
              errorDoc["unlockMessage"] = "error"; // Thông báo lỗi
              String errorJson;
              serializeJson(errorDoc, errorJson);
              
              int errorCode = errorHttp.PUT(errorJson);
              errorHttp.end();
              
              if (errorCode == HTTP_CODE_OK) {
                Serial.println("[Control Task] ✓ Error message sent to app successfully (HTTP 200)");
              } else {
                Serial.print("[Control Task] ✗ Failed to send error message to app, HTTP code: ");
                Serial.println(errorCode);
                // Nếu không gửi được, vẫn clear lastUnlockRequestTime để tránh xử lý lại
                lastUnlockRequestTime = requestTime;
                Serial.println("[Control Task] Cleared lastUnlockRequestTime to prevent reprocessing");
              }
            }
          } else {
            if (newPassword.length() == 0) {
              Serial.println("[Control Task] Password is empty, skipping");
            } else if (requestTime <= lastUnlockRequestTime) {
              Serial.print("[Control Task] Request time not new (");
              Serial.print(requestTime);
              Serial.print(" <= ");
              Serial.print(lastUnlockRequestTime);
              Serial.println("), skipping");
            }
          }
        } else {
          if (!isUnlocked) {
            Serial.println("[Control Task] No unlockPassword or unlockRequestTime in response");
          }
        }
        
        // Kiểm tra xem có control commands không
        // CHỈ set hasCommand nếu:
        // 1. ĐÃ MỞ KHÓA (isUnlocked = true)
        // 2. KHÔNG có bấm nút vật lý gần đây
        // 3. VÀ không đang gửi HTTP (isSending = false)
        // 4. VÀ đã qua ít nhất 12 giây kể từ lần gửi data cuối cùng
        unsigned long now = millis();
        bool recentlyPressedButton = (now - lastManualButtonPress) < MANUAL_BUTTON_PRIORITY_TIME;
        unsigned long timeSinceLastDataSent = (now - lastDataSentTime);
        bool dataJustSent = (lastDataSentTime > 0) && (timeSinceLastDataSent < 12000); // 12 giây
        
        if (isUnlocked && !recentlyPressedButton && !dataJustSent && !isSending && 
            (doc.containsKey("heaterOn") || doc.containsKey("waterHeaterOn") || doc.containsKey("mode"))) {
          serverCommands.hasCommand = true;
          if (doc.containsKey("heaterOn")) {
            serverCommands.heaterOn = doc["heaterOn"];
          }
          if (doc.containsKey("waterHeaterOn")) {
            serverCommands.waterHeaterOn = doc["waterHeaterOn"];
          }
          if (doc.containsKey("mode")) {
            String modeStr = doc["mode"];
            serverCommands.mode = (modeStr == "auto");
          }
          
          Serial.print("[Control Task] Received commands: Heater=");
          Serial.print(serverCommands.heaterOn);
          Serial.print(" WaterHeater=");
          Serial.println(serverCommands.waterHeaterOn);
        }
      }
      
      http.end();
    }
    
    // Fetch mỗi 2 giây
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

// Force gửi data lên DB ngay khi bấm nút vật lý
void forceDataUpdate() {
  if (apiTaskHandle == NULL || dataMutex == NULL) return;
  
  // Đọc data hiện tại
  float currentRoomTemp = dht.readTemperature();
  if (isnan(currentRoomTemp)) {
    currentRoomTemp = roomTemp;
  }
  bool currentMotion = digitalRead(PIR_PIN);
  
  // Force update sensorData và set needUpdate = true
  if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    sensorData.roomTemp = currentRoomTemp;
    sensorData.waterTemp = waterTemp;
    sensorData.hasMotion = currentMotion;
    sensorData.mode = autoMode; // Dùng autoMode (giá trị thực tế)
    sensorData.heaterOn = lastHeaterState;
    sensorData.waterHeaterOn = lastWaterState;
    sensorData.needUpdate = true;
    xSemaphoreGive(dataMutex);
    
    Serial.println("[FORCE] Data update triggered after button press");
  }
}

// Gửi push notification khi phát hiện motion
void sendMotionNotification() {
  if (!wifiConnected || roomId == "") return;
  
  // Gọi API notify trong background (không block)
  HTTPClient http;
  String url = String(apiBaseUrl) + "/api/rooms/" + roomId + "/notify";
  http.begin(url);
  http.setTimeout(3000); // Timeout ngắn vì chỉ cần trigger
  http.setConnectTimeout(2000);
  
  int httpCode = http.POST("{}"); // Empty body
  
  if (httpCode == HTTP_CODE_OK) {
    Serial.println("[NOTIFY] Push notification sent");
  } else {
    Serial.print("[NOTIFY] Failed to send notification, code: ");
    Serial.println(httpCode);
  }
  
  http.end();
}

// Force gửi trạng thái unlock lên DB ngay
void forceUnlockStatusUpdate() {
  if (apiTaskHandle == NULL || dataMutex == NULL || !wifiConnected || roomId == "") return;
  
  // Đọc data hiện tại
  float currentRoomTemp = dht.readTemperature();
  if (isnan(currentRoomTemp)) {
    currentRoomTemp = roomTemp;
  }
  bool currentMotion = digitalRead(PIR_PIN);
  
  // Force update sensorData với isUnlocked và set needUpdate = true
  if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    sensorData.roomTemp = currentRoomTemp;
    sensorData.waterTemp = waterTemp;
    sensorData.hasMotion = currentMotion;
    sensorData.mode = autoMode; // Dùng autoMode (giá trị thực tế)
    sensorData.heaterOn = lastHeaterState;
    sensorData.waterHeaterOn = lastWaterState;
    sensorData.needUpdate = true;
    xSemaphoreGive(dataMutex);
    
    Serial.print("[FORCE] Unlock status update triggered, isUnlocked=");
    Serial.println(isUnlocked);
  }
}
