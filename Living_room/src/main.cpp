#include <DHT.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

// ================== CẤU HÌNH CHÂN ==================
#define DHTPIN 13
#define DHTTYPE DHT22
#define SDA_PIN 12 
#define SCL_PIN 14 

#define PIR_PIN 27
#define LDR_PIN 34
#define DUST_PIN 36        // Potentiometer mô phỏng cảm biến bụi

#define LED_PIR 0
#define LED_HEATER 25
#define LED_FAN 33
#define LED_AC 32
#define LED_HUMIDIFIER 26  // LED máy tạo ẩm

// RGB LED cho mức độ bụi
#define RGB_RED 15
#define RGB_GREEN 2
#define RGB_BLUE 4

#define SW_MODE 35         // Switch Auto/Manual
#define BTN_AC 19          // Button 1 - Điều hòa
#define BTN_FAN 23         // Button 2 - Quạt
#define BTN_HEATER 22      // Button 3 - Lò sưởi

// ================== CẤU HÌNH WIFI & API ==================
// Nếu chạy trên Wokwi: dùng WiFi ảo "Wokwi-GUEST" (không mật khẩu)
// Nếu chạy trên ESP32 thật: đổi ssid/password thành WiFi nhà anh
const char* ssid = "Wokwi-GUEST";
const char* password = "";
const char* apiBaseUrl = "https://iot-smart-home-app.vercel.app";
String roomId = "";
bool wifiConnected = false;

// Biến global để share data với API task
struct SensorData {
  float tempRoom;
  bool hasMotion;
  int lightLevel;
  bool mode; // true = auto, false = manual
  bool heaterOn;
  bool acOn;
  int fanLevel;
  bool needUpdate;
};

SensorData sensorData = {0, false, 0, false, false, false, 0, false};
TaskHandle_t apiTaskHandle = NULL;

// Biến để nhận control commands từ app
struct ControlCommands {
  bool hasCommand;
  bool heaterOn;
  bool acOn;
  int fanLevel;
  bool mode; // true = auto, false = manual
};

ControlCommands serverCommands = {false, false, false, 0, false};
TaskHandle_t controlTaskHandle = NULL;
unsigned long lastManualButtonPress = 0;
const unsigned long MANUAL_BUTTON_PRIORITY_TIME = 20000; // 20 giây
unsigned long lastDataSentTime = 0;

// Biến để track thời gian gửi cuối cùng
unsigned long lastApiUpdate = 0;
volatile bool isSending = false;
SemaphoreHandle_t dataMutex = NULL;

// ================== ĐỐI TƯỢNG & BIẾN ==================
DHT dht(DHTPIN, DHTTYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2);

float tempRoom = 0;
float humidity = 0;
bool heaterOn = false;
bool acOn = false;
bool humidifierOn = false;
int fanLevel = 0; // 0, 1, 2, 3
bool autoMode = true; // Mode hiện tại (true = auto, false = manual)
bool lastAutoMode = true;

int dustLevel = 0;     // Giá trị từ 0-4095
String dustStatus = ""; // TOT, TRUNG BINH, KEM, XAU

int luxValue = 0;      // Giá trị ánh sáng

// ================== FUNCTION PROTOTYPES ==================
void handleDustSensor();
void handleCurtainControl();
void handleSmartHome();
void handleManualButtons();
void connectWiFi();
String getRoomIdByName();
void apiTask(void *pvParameters);
void controlTask(void *pvParameters);
void forceDataUpdate();

// ================== SETUP ==================
void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);
  
  dht.begin();
  lcd.init();
  lcd.backlight();
  
  pinMode(PIR_PIN, INPUT);
  pinMode(LDR_PIN, INPUT); 
  pinMode(DUST_PIN, INPUT);
  pinMode(SW_MODE, INPUT); 
  pinMode(BTN_AC, INPUT_PULLUP);
  pinMode(BTN_FAN, INPUT_PULLUP);
  pinMode(BTN_HEATER, INPUT_PULLUP);
  
  pinMode(LED_PIR, OUTPUT);
  pinMode(LED_HEATER, OUTPUT);
  pinMode(LED_FAN, OUTPUT);
  pinMode(LED_AC, OUTPUT);
  pinMode(LED_HUMIDIFIER, OUTPUT);
  
  pinMode(RGB_RED, OUTPUT);
  pinMode(RGB_GREEN, OUTPUT);
  pinMode(RGB_BLUE, OUTPUT);

  lcd.setCursor(0, 0);
  lcd.print("HE THONG SMART");
  lcd.setCursor(0, 1);
  lcd.print("HOME READY");
  delay(2000);
  lcd.clear();

  tempRoom = dht.readTemperature();
  humidity = dht.readHumidity();
  Serial.println("Log: He thong khoi dong thanh cong.");

  // Kết nối WiFi và lấy roomId
  connectWiFi();
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    roomId = getRoomIdByName();
    if (roomId != "") {
      Serial.print("Room ID (Phòng Khách) found: ");
      Serial.println(roomId);
      
      // Tạo mutex để đồng bộ truy cập sensorData
      dataMutex = xSemaphoreCreateMutex();
      if (dataMutex == NULL) {
        Serial.println("Failed to create mutex!");
      }
      
      // Tạo FreeRTOS task để gửi API trong background
      xTaskCreate(
        apiTask,
        "API_Task",
        8192,
        NULL,
        1,
        &apiTaskHandle
      );
      
      // Tạo FreeRTOS task để fetch control commands
      xTaskCreate(
        controlTask,
        "Control_Task",
        8192,
        NULL,
        1,
        &controlTaskHandle
      );
      
      Serial.println("[SETUP] FreeRTOS tasks created");
    } else {
      Serial.println("Khong tim thay room 'Phòng Khách' trong database");
    }
  } else {
    Serial.println("WiFi not connected, skip roomId lookup");
  }
}

// ================== LOOP CHÍNH ==================
void loop() {
  handleDustSensor();
  handleCurtainControl();
  handleSmartHome();
  
  // Cập nhật sensor data để API task gửi (không block)
  if (apiTaskHandle != NULL && wifiConnected && roomId != "") {
    // Kiểm tra xem có thay đổi đáng kể không
    static float lastSentTemp = -999;
    static bool lastSentHeater = false;
    static bool lastSentAc = false;
    static int lastSentFan = -1;
    static int lastSentLight = -1;
    static bool lastSentMotion = false;
    static bool lastSentMode = false;
    
    // Đọc nhiệt độ TRỰC TIẾP từ DHT
    float currentTemp = dht.readTemperature();
    if (isnan(currentTemp)) {
      currentTemp = tempRoom;
    }
    
    // Đọc light và motion TRỰC TIẾP từ sensors
    int currentLight = analogRead(LDR_PIN);
    bool pirMotion = digitalRead(PIR_PIN);
    bool currentMotion = pirMotion; // Phòng khách: motion = PIR trực tiếp
    
    // Đọc mode TRỰC TIẾP từ switch
    bool currentMode = digitalRead(SW_MODE);
    
    // Kiểm tra xem có thay đổi đáng kể không
    bool hasSignificantChange = 
      (abs(currentTemp - lastSentTemp) > 0.5) ||
      (heaterOn != lastSentHeater) ||
      (acOn != lastSentAc) ||
      (fanLevel != lastSentFan) ||
      (lastSentLight != -1 && abs(currentLight - lastSentLight) > 200) ||
      (currentMotion != lastSentMotion) ||
      (currentMode != lastSentMode);
    
    // CHỈ gửi khi có thay đổi đáng kể
    if (hasSignificantChange && dataMutex != NULL) {
      if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        if (!sensorData.needUpdate && !isSending) {
          // Cập nhật data TRƯỚC khi đánh dấu needUpdate
          sensorData.tempRoom = currentTemp;
          sensorData.hasMotion = currentMotion;
          sensorData.lightLevel = currentLight;
          sensorData.mode = currentMode;
          sensorData.heaterOn = heaterOn;
          sensorData.acOn = acOn;
          sensorData.fanLevel = fanLevel;
          
          sensorData.needUpdate = true;
          
          Serial.print("[LOOP] Set needUpdate=true - ");
          Serial.print("Temp:"); Serial.print(currentTemp, 1);
          Serial.print(" Heater:"); Serial.print(heaterOn ? "ON" : "OFF");
          Serial.print(" AC:"); Serial.print(acOn ? "ON" : "OFF");
          Serial.print(" Fan:"); Serial.print(fanLevel);
          Serial.print(" Light:"); Serial.print(currentLight);
          Serial.print(" Motion:"); Serial.print(currentMotion ? "YES" : "NO");
          Serial.print(" Mode:"); Serial.println(currentMode ? "AUTO" : "MANUAL");
          
          lastSentTemp = currentTemp;
          lastSentHeater = heaterOn;
          lastSentAc = acOn;
          lastSentFan = fanLevel;
          lastSentLight = currentLight;
          lastSentMotion = currentMotion;
          lastSentMode = currentMode;
        }
        xSemaphoreGive(dataMutex);
      }
    }
  }
}

// ================== XỬ LÝ CẢM BIẾN BỤI ==================
void handleDustSensor() {
  dustLevel = analogRead(DUST_PIN);
  
  if (dustLevel <= 1023) {
    dustStatus = "TOT";
    digitalWrite(RGB_RED, HIGH);
    digitalWrite(RGB_GREEN, HIGH);
    digitalWrite(RGB_BLUE, HIGH);
    Serial.print("Bui: TOT - TRANG (");
  } 
  else if (dustLevel <= 2047) {
    dustStatus = "TB";
    digitalWrite(RGB_RED, LOW);
    digitalWrite(RGB_GREEN, HIGH);
    digitalWrite(RGB_BLUE, LOW);
    Serial.print("Bui: TRUNG BINH - XANH LA (");
  } 
  else if (dustLevel <= 3071) {
    dustStatus = "KEM";
    digitalWrite(RGB_RED, HIGH);
    digitalWrite(RGB_GREEN, HIGH);
    digitalWrite(RGB_BLUE, LOW);
    Serial.print("Bui: KEM - VANG (");
  } 
  else {
    dustStatus = "XAU";
    digitalWrite(RGB_RED, HIGH);
    digitalWrite(RGB_GREEN, LOW);
    digitalWrite(RGB_BLUE, LOW);
    Serial.print("Bui: XAU - DO (");
  }
  
  Serial.print(dustLevel);
  Serial.println(")");
}

// ================== ĐIỀU KHIỂN RÈM VÀ ĐÈN ==================
void handleCurtainControl() {
  int ldrValue = analogRead(LDR_PIN);
  luxValue = map(ldrValue, 0, 4095, 0, 1500);
  
  bool hasMotion = digitalRead(PIR_PIN);
  
  // LOGIC ĐIỀU KHIỂN ĐÈN PIR DỰA TRÊN ÁNH SÁNG
  if (luxValue < 300) {
    digitalWrite(LED_PIR, hasMotion ? HIGH : LOW);
  } else {
    digitalWrite(LED_PIR, LOW);
  }
  
  Serial.print("LDR: ");
  Serial.print(ldrValue);
  Serial.print(" -> Lux: ");
  Serial.print(luxValue);
  Serial.print(" | Den: ");
  Serial.println(hasMotion && luxValue < 300 ? "ON" : "OFF");
}

// ================== LOGIC ĐIỀU KHIỂN PHÒNG ==================
void handleSmartHome() {
  // Đọc mode từ switch vật lý
  bool switchMode = digitalRead(SW_MODE);
  
  // Chỉ cập nhật autoMode từ switch vật lý nếu switch thay đổi
  if (switchMode != lastAutoMode) {
    if (lastAutoMode && !switchMode) {
      heaterOn = false; acOn = false; fanLevel = 0;
      Serial.println("[Switch] Mode changed to MANUAL - Devices Reset");
    }
    autoMode = switchMode;
    Serial.print("[Switch] Mode changed to: ");
    Serial.println(autoMode ? "AUTO" : "MANUAL");
  }
  lastAutoMode = switchMode;

  float t = dht.readTemperature();
  float h = dht.readHumidity();
  
  if (!isnan(t)) {
    if (t > 100) {
      Serial.println("!!! CANH BAO CHAY: NHIET DO > 100 !!!");
      heaterOn = false; acOn = false;
    }
    if (heaterOn) tempRoom += 0.05;
    if (acOn) tempRoom -= 0.05;
    if (abs(t - tempRoom) > 1.0) tempRoom = t; 
  }
  
  if (!isnan(h)) {
    humidity = h;
  }

  // ĐIỀU KHIỂN MÁY TẠO ẨM (Luôn tự động)
  humidifierOn = (humidity < 40) || (dustLevel >= 2048);
  
  // --- XỬ LÝ AUTO/MANUAL ---
  // Kiểm tra control commands từ app (chỉ áp dụng nếu không có bấm nút vật lý gần đây)
  unsigned long now = millis();
  bool recentlyPressedButton = (now - lastManualButtonPress) < MANUAL_BUTTON_PRIORITY_TIME;
  
  // CHỈ áp dụng commands từ app nếu:
  // 1. KHÔNG có bấm nút vật lý trong 20 giây
  // 2. VÀ không đang gửi HTTP (isSending = false)
  // 3. VÀ đã qua ít nhất 12 giây kể từ lần gửi data cuối cùng (đủ thời gian HTTP request ~7s + app cập nhật ~5s)
  unsigned long timeSinceLastDataSent = (now - lastDataSentTime);
  bool dataJustSent = (lastDataSentTime > 0) && (timeSinceLastDataSent < 12000); // 12 giây
  
  if (serverCommands.hasCommand && !recentlyPressedButton && !dataJustSent && !isSending) {
    // Kiểm tra thay đổi bao gồm cả mode
    bool hasChange = (serverCommands.heaterOn != heaterOn) ||
                     (serverCommands.acOn != acOn) ||
                     (serverCommands.fanLevel != fanLevel) ||
                     (serverCommands.mode != autoMode);
    
    if (hasChange) {
      // Áp dụng commands từ app
      heaterOn = serverCommands.heaterOn;
      acOn = serverCommands.acOn;
      fanLevel = serverCommands.fanLevel;
      // Mode từ app - ưu tiên mode từ app khi có command
      if (serverCommands.mode != autoMode) {
        autoMode = serverCommands.mode;
        Serial.print("[Control] Mode changed from app: ");
        Serial.println(autoMode ? "AUTO" : "MANUAL");
      }
      
      // Update LED
      digitalWrite(LED_HEATER, heaterOn);
      digitalWrite(LED_AC, acOn);
      digitalWrite(LED_FAN, fanLevel > 0);
      
      Serial.print("[Control] Applied from app: Mode=");
      Serial.print(autoMode ? "AUTO" : "MANUAL");
      Serial.print(" H=");
      Serial.print(heaterOn);
      Serial.print(" AC=");
      Serial.print(acOn);
      Serial.print(" F=");
      Serial.println(fanLevel);
    } else {
      // Giá trị từ app giống với giá trị hiện tại, không cần áp dụng
      Serial.println("[Control] Commands from app match current state, skipping");
    }
    // Clear command flag sau khi xử lý
    serverCommands.hasCommand = false;
  } else if (serverCommands.hasCommand && (recentlyPressedButton || dataJustSent || isSending)) {
    // Có commands từ app nhưng vừa bấm nút vật lý, vừa gửi data, hoặc đang gửi HTTP → CHẶN HOÀN TOÀN
    if (recentlyPressedButton) {
      Serial.println("[Control] BLOCKED - button pressed recently");
    } else if (isSending) {
      Serial.println("[Control] BLOCKED - HTTP request in progress");
    } else {
      Serial.println("[Control] BLOCKED - data just sent, ignoring to avoid loop");
    }
    serverCommands.hasCommand = false; // Clear để không check lại
  }
  
  if (autoMode) {
    // Logic tự động
    if (tempRoom >= 28) {
      acOn = true; heaterOn = false;
    } else if (tempRoom <= 18) {
      heaterOn = true; acOn = false;
    } else if (tempRoom >= 20 && tempRoom <= 22) {
      acOn = false; heaterOn = false;
    }
    fanLevel = (tempRoom > 25) ? 1 : 0;
    
    digitalWrite(LED_HEATER, heaterOn);
    digitalWrite(LED_AC, acOn);
    digitalWrite(LED_FAN, fanLevel > 0);
  } else {
    handleManualButtons();
  }

  digitalWrite(LED_HUMIDIFIER, humidifierOn);

  // Hiển thị LCD
  lcd.setCursor(0, 0);
  lcd.print(autoMode ? "AUTO" : "MAN ");
  lcd.print(" T:"); 
  lcd.print(tempRoom, 1); 
  lcd.print(" H:");
  lcd.print((int)humidity);
  lcd.print("  ");

  lcd.setCursor(0, 1);
  lcd.print(dustStatus);
  if (dustStatus == "TB") {
    lcd.print("  ");
  } else if (dustStatus == "KEM") {
    lcd.print(" ");
  } else {
    lcd.print("   ");
  }
  
  lcd.print(heaterOn ? "H" : " ");
  lcd.print(acOn ? "A" : " ");
  lcd.print(humidifierOn ? "M" : " ");
  lcd.print(" F:");
  lcd.print(fanLevel);
  lcd.print("  ");

  delay(200);
}

// ================== NÚT BẤM THỦ CÔNG ==================
void handleManualButtons() {
  static unsigned long lastPressAC = 0;
  static unsigned long lastPressFAN = 0;
  static unsigned long lastPressHEATER = 0;
  static bool lastBtnAC = HIGH;
  static bool lastBtnFAN = HIGH;
  static bool lastBtnHEATER = HIGH;
  
  unsigned long now = millis();
  bool btnAC = digitalRead(BTN_AC);
  bool btnFAN = digitalRead(BTN_FAN);
  bool btnHEATER = digitalRead(BTN_HEATER);
  
  if (btnAC == LOW && lastBtnAC == HIGH && now - lastPressAC > 50) {
    acOn = !acOn;
    if (acOn) heaterOn = false;
    lastPressAC = now;
    lastManualButtonPress = now;
    digitalWrite(LED_AC, acOn);
    digitalWrite(LED_HEATER, heaterOn);
    Serial.println("Manual: Toggle AC");
    forceDataUpdate();
  }
  lastBtnAC = btnAC;

  if (btnFAN == LOW && lastBtnFAN == HIGH && now - lastPressFAN > 50) {
    fanLevel++;
    if (fanLevel > 3) fanLevel = 0;
    lastPressFAN = now;
    lastManualButtonPress = now;
    digitalWrite(LED_FAN, fanLevel > 0);
    Serial.print("Manual: Fan Level "); Serial.println(fanLevel);
    forceDataUpdate();
  }
  lastBtnFAN = btnFAN;

  if (btnHEATER == LOW && lastBtnHEATER == HIGH && now - lastPressHEATER > 50) {
    heaterOn = !heaterOn;
    if (heaterOn) acOn = false;
    lastPressHEATER = now;
    lastManualButtonPress = now;
    digitalWrite(LED_HEATER, heaterOn);
    digitalWrite(LED_AC, acOn);
    Serial.println("Manual: Toggle Heater");
    forceDataUpdate();
  }
  lastBtnHEATER = btnHEATER;
}

// ================== WIFI & API IMPLEMENTATION ==================

void connectWiFi() {
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.print("WiFi connected! IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println();
    Serial.println("WiFi connection failed!");
  }
}

String getRoomIdByName() {
  HTTPClient http;
  String url = String(apiBaseUrl) + "/api/rooms";
  http.begin(url);
  http.setTimeout(10000);
  
  int httpCode = http.GET();
  String result = "";
  
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    DynamicJsonDocument doc(4096);
    DeserializationError error = deserializeJson(doc, payload);
    
    if (error) {
      Serial.print("JSON parse error: ");
      Serial.println(error.c_str());
    } else if (doc.is<JsonArray>()) {
      JsonArray rooms = doc.as<JsonArray>();
      
      for (JsonObject room : rooms) {
        String type = room["type"].as<String>();
        String name = room["name"].as<String>();
        
        if (type == "livingroom" || name == "Phòng Khách") {
          result = room["_id"].as<String>();
          Serial.print("Room ID found: ");
          Serial.println(result);
          Serial.print("Room name: ");
          Serial.println(name);
          break;
        }
      }
      
      if (result == "") {
        Serial.println("Room 'Phòng Khách' (livingroom) not found in database");
      }
    }
  } else {
    Serial.print("Failed to get room ID, code: ");
    Serial.println(httpCode);
  }
  
  http.end();
  return result;
}

void forceDataUpdate() {
  if (dataMutex != NULL && wifiConnected && roomId != "") {
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      sensorData.tempRoom = tempRoom;
      sensorData.hasMotion = digitalRead(PIR_PIN);
      sensorData.lightLevel = analogRead(LDR_PIN);
      sensorData.mode = digitalRead(SW_MODE);
      sensorData.heaterOn = heaterOn;
      sensorData.acOn = acOn;
      sensorData.fanLevel = fanLevel;
      sensorData.needUpdate = true;
      xSemaphoreGive(dataMutex);
      Serial.println("[Force] Data update triggered");
    }
  }
}

// FreeRTOS Task để gửi API trong background
void apiTask(void *pvParameters) {
  Serial.println("[API Task] Started");
  
  while (true) {
    if (wifiConnected && roomId != "" && dataMutex != NULL) {
      if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        if (sensorData.needUpdate) {
          float temp = sensorData.tempRoom;
          bool motion = sensorData.hasMotion;
          int light = sensorData.lightLevel;
          bool mode = sensorData.mode;
          bool heater = sensorData.heaterOn;
          bool ac = sensorData.acOn;
          int fan = sensorData.fanLevel;
          
          isSending = true;
          sensorData.needUpdate = false;
          
          xSemaphoreGive(dataMutex);
          
          Serial.print("[API Task] Sending data...");
          
          HTTPClient http;
          String url = String(apiBaseUrl) + "/api/rooms/" + roomId + "/data";
          http.begin(url);
          http.addHeader("Content-Type", "application/json");
          http.setTimeout(5000);
          http.setConnectTimeout(3000);
          
          DynamicJsonDocument doc(256);
          doc["tempRoom"] = temp;
          doc["hasMotion"] = motion;
          doc["lightLevel"] = light;
          doc["mode"] = mode ? "auto" : "manual";
          doc["heaterOn"] = heater;
          doc["acOn"] = ac;
          doc["fanLevel"] = fan;
          doc["isUnlocked"] = true;
          
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
            lastDataSentTime = millis();
          } else {
            Serial.print("[API] Fail ");
            Serial.print(httpCode);
            Serial.print(" (");
            Serial.print(elapsed);
            Serial.println("ms)");
          }
          
          lastApiUpdate = millis();
          isSending = false;
          
          vTaskDelay(pdMS_TO_TICKS(100));
          
          if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            if (sensorData.needUpdate && !isSending) {
              xSemaphoreGive(dataMutex);
              continue;
            }
            xSemaphoreGive(dataMutex);
          }
        } else {
          xSemaphoreGive(dataMutex);
        }
      }
    }
    
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// FreeRTOS Task để fetch control commands từ app
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
        DynamicJsonDocument doc(1024);
        DeserializationError error = deserializeJson(doc, payload);
        
        if (error) {
          Serial.print("[Control Task] JSON deserialize error: ");
          Serial.println(error.c_str());
        } else {
          // Kiểm tra xem có control commands không
          // CHỈ set hasCommand nếu:
          // 1. KHÔNG có bấm nút vật lý gần đây
          // 2. VÀ không đang gửi HTTP (isSending = false)
          // 3. VÀ đã qua ít nhất 12 giây kể từ lần gửi data cuối cùng (đủ thời gian HTTP request ~7s + app cập nhật ~5s)
          unsigned long now = millis();
          bool recentlyPressedButton = (now - lastManualButtonPress) < MANUAL_BUTTON_PRIORITY_TIME;
          unsigned long timeSinceLastDataSent = (now - lastDataSentTime);
          bool dataJustSent = (lastDataSentTime > 0) && (timeSinceLastDataSent < 12000); // 12 giây
          
          if (!recentlyPressedButton && !dataJustSent && !isSending && (doc.containsKey("heaterOn") || doc.containsKey("acOn") || doc.containsKey("fanLevel"))) {
            serverCommands.hasCommand = true;
            if (doc.containsKey("heaterOn")) {
              serverCommands.heaterOn = doc["heaterOn"];
            }
            if (doc.containsKey("acOn")) {
              serverCommands.acOn = doc["acOn"];
            }
            if (doc.containsKey("fanLevel")) {
              serverCommands.fanLevel = doc["fanLevel"];
            }
            if (doc.containsKey("mode")) {
              String modeStr = doc["mode"];
              serverCommands.mode = (modeStr == "auto");
            }
            
            Serial.print("[Control Task] Received commands: H=");
            Serial.print(serverCommands.heaterOn);
            Serial.print(" AC=");
            Serial.print(serverCommands.acOn);
            Serial.print(" F=");
            Serial.println(serverCommands.fanLevel);
          }
        }
      }
      
      http.end();
    }
    
    vTaskDelay(pdMS_TO_TICKS(2000)); // Check mỗi 2 giây
  }
}
