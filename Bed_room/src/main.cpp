#include <DHT.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include <ESP32Servo.h>
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
#define LED_PIR 0
#define LED_HEATER 25
#define LED_FAN 33
#define LED_AC 32

#define SW_MODE 35     // Switch Auto/Manual
#define BTN_AC 26      // Button 1
#define BTN_FAN 23     // Button 2
#define BTN_HEATER 22  // Button 3
#define SERVO_PIN 21

// ================== KEYPAD ==================
const byte ROWS = 4; 
const byte COLS = 4; 

char keys[ROWS][COLS] = {
  {'1','2','3','A'}, {'4','5','6','B'}, {'7','8','9','C'}, {'*','0','#','D'}
};

byte rowPins[ROWS] = {19, 18, 5, 17}; 
byte colPins[COLS] = {16, 4, 2, 15}; 

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// ================== CẤU HÌNH WIFI & API ==================
const char* wokwi_ssid = "Wokwi-GUEST";
const char* wokwi_password = "";
const char* apiBaseUrl = "https://iot-smart-home-app.vercel.app";
String roomId = "";
bool wifiConnected = false;
unsigned long lastApiCall = 0;
const unsigned long apiInterval = 1000; // Gọi API mỗi 1 giây (định kỳ, không block nút)

// Biến global để share data với API task
struct SensorData {
  float tempRoom;
  bool hasMotion;
  int lightLevel;
  bool mode;
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
unsigned long lastManualButtonPress = 0; // Track thời gian bấm nút vật lý cuối cùng
const unsigned long MANUAL_BUTTON_PRIORITY_TIME = 20000; // Chặn controlTask trong 20 giây sau khi bấm nút vật lý (đủ thời gian gửi data lên DB và app cập nhật)
unsigned long lastDataSentTime = 0; // Track thời gian gửi data lên DB cuối cùng

// Biến để track thời gian gửi cuối cùng (dùng chung giữa loop() và apiTask())
unsigned long lastApiUpdate = 0;
volatile bool isSending = false; // Flag để biết apiTask() đang gửi HTTP
SemaphoreHandle_t dataMutex = NULL; // Mutex để đồng bộ truy cập sensorData

// ================== ĐỐI TƯỢNG & BIẾN ==================
DHT dht(DHTPIN, DHTTYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo doorServo;

String password = "1234";
String inputPassword = "";
bool isUnlocked = false;
int wrongCount = 0;
unsigned long lockUntil = 0;

float tempRoom = 0;
bool heaterOn = false;
bool acOn = false;
int fanLevel = 0; // 0, 1, 2, 3
bool lastAutoMode = true;

// ================== FUNCTION PROTOTYPES ==================
void handleSecurity();
void handleSmartHome();
void controlTask(void *pvParameters);
void handleManualButtons();
void connectWiFi();
void sendSensorDataNonBlocking();
String getRoomIdByName();
void apiTask(void *pvParameters); // FreeRTOS task function
void controlTask(void *pvParameters); // FreeRTOS task function để fetch control commands
void forceDataUpdate(); // Force gửi data lên DB ngay khi bấm nút vật lý

// ================== SETUP ==================
void setup() {
  Serial.begin(115200);

  Wire.begin(SDA_PIN, SCL_PIN); // Khởi tạo I2C cho LCD trên chân 12, 14
  
  dht.begin();
  lcd.init();
  lcd.backlight();
  
  pinMode(PIR_PIN, INPUT);
  pinMode(LDR_PIN, INPUT); 
  pinMode(SW_MODE, INPUT); 
  pinMode(BTN_AC, INPUT_PULLUP);
  pinMode(BTN_FAN, INPUT_PULLUP);
  pinMode(BTN_HEATER, INPUT_PULLUP);
  
  pinMode(LED_PIR, OUTPUT);
  pinMode(LED_HEATER, OUTPUT);
  pinMode(LED_FAN, OUTPUT);
  pinMode(LED_AC, OUTPUT);
  
  doorServo.attach(SERVO_PIN);
  doorServo.write(0); // Cửa đóng
  
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
        8192,              // Stack size (tăng lên 8KB để tránh overflow)
        NULL,              // Parameters
        1,                 // Priority (low)
        &apiTaskHandle     // Task handle
      );
      Serial.println("API Task created");
      
      // Tạo FreeRTOS task để fetch control commands từ app
      xTaskCreate(
        controlTask,       // Task function
        "Control_Task",    // Task name
        4096,              // Stack size
        NULL,              // Parameters
        1,                 // Priority (low)
        &controlTaskHandle // Task handle
      );
      Serial.println("Control Task created");
    }
  }
  
  lcd.setCursor(0, 0);
  lcd.print("NHAP MAT KHAU:");
}

// ================== LOOP CHÍNH ==================
void loop() {
  // 1. Kiểm tra trạng thái khóa hệ thống (Khi nhập sai)
  if (millis() < lockUntil) {
    int remain = (lockUntil - millis()) / 1000;
    lcd.setCursor(0, 1);
    lcd.print("KHOA: "); lcd.print(remain); lcd.print("s    ");
    return;
  }

  // 2. Nếu chưa mở khóa: Chỉ chạy Keypad
  if (!isUnlocked) {
    handleSecurity();
  } 
  // 3. Nếu đã mở khóa: Chạy logic điều khiển thiết bị
  else {
    handleSmartHome();
    
    // Cập nhật sensor data để API task gửi (không block)
    if (apiTaskHandle != NULL) {
      // Kiểm tra xem có thay đổi đáng kể không (để gửi ngay)
      static float lastSentTemp = -999;
      static bool lastSentHeater = false;
      static bool lastSentAc = false;
      static int lastSentFan = -1;
      static int lastSentLight = -1;
      static bool lastSentMotion = false;
      
      // Đọc nhiệt độ TRỰC TIẾP từ DHT
      float currentTemp = dht.readTemperature();
      if (isnan(currentTemp)) {
        currentTemp = tempRoom;
      }
      
      // Đọc light và motion TRỰC TIẾP từ sensors
      int currentLight = analogRead(LDR_PIN);
      bool pirMotion = digitalRead(PIR_PIN);
      
      // Motion chỉ được coi là true nếu: ánh sáng thấp (trời tối) VÀ có chuyển động từ PIR
      // Giống logic bật LED: chỉ bật LED khi trời tối và có motion
      bool currentMotion = (currentLight > 2500) && pirMotion;
      
      // Kiểm tra xem có thay đổi đáng kể không
      bool hasSignificantChange = 
        (abs(currentTemp - lastSentTemp) > 0.5) ||  // Nhiệt độ thay đổi > 0.5°C
        (heaterOn != lastSentHeater) ||             // Heater thay đổi
        (acOn != lastSentAc) ||                     // AC thay đổi
        (fanLevel != lastSentFan) ||                 // Fan level thay đổi
        (lastSentLight != -1 && abs(currentLight - lastSentLight) > 200) || // Light thay đổi > 200
        (currentMotion != lastSentMotion);           // Motion thay đổi
      
      // CHỈ gửi khi có thay đổi đáng kể, KHÔNG gửi định kỳ
      // Vì HTTP request mất ~8 giây, gửi định kỳ mỗi 1 giây là vô nghĩa
      // Dùng mutex để đồng bộ truy cập sensorData (tránh race condition)
      if (hasSignificantChange && dataMutex != NULL) {
        if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
          // Chỉ set needUpdate nếu apiTask() đã xử lý xong request trước đó
          if (!sensorData.needUpdate && !isSending) {
            // Cập nhật data TRƯỚC khi đánh dấu needUpdate
            sensorData.tempRoom = currentTemp;
            sensorData.hasMotion = currentMotion;
            sensorData.lightLevel = currentLight;
            sensorData.mode = digitalRead(SW_MODE);
            sensorData.heaterOn = heaterOn;
            sensorData.acOn = acOn;
            sensorData.fanLevel = fanLevel;
            
            // Đánh dấu cần update SAU khi đã cập nhật data
            sensorData.needUpdate = true;
            
            // Log tất cả các thông số đã thay đổi (so sánh TRƯỚC KHI cập nhật lastSent*)
            Serial.print("[LOOP] Set needUpdate=true - ");
            bool hasAnyChange = false;
            
            if (lastSentTemp == -999 || abs(currentTemp - lastSentTemp) > 0.5) {
              Serial.print("Temp:"); Serial.print(currentTemp, 1); Serial.print(" ");
              hasAnyChange = true;
            }
            if (heaterOn != lastSentHeater) {
              Serial.print("Heater:"); Serial.print(heaterOn ? "ON" : "OFF"); Serial.print(" ");
              hasAnyChange = true;
            }
            if (acOn != lastSentAc) {
              Serial.print("AC:"); Serial.print(acOn ? "ON" : "OFF"); Serial.print(" ");
              hasAnyChange = true;
            }
            if (fanLevel != lastSentFan) {
              Serial.print("Fan:"); Serial.print(fanLevel); Serial.print(" ");
              hasAnyChange = true;
            }
            if (lastSentLight == -1 || abs(currentLight - lastSentLight) > 200) {
              Serial.print("Light:"); Serial.print(currentLight); Serial.print(" ");
              hasAnyChange = true;
            }
            if (currentMotion != lastSentMotion) {
              Serial.print("Motion:"); Serial.print(currentMotion ? "YES" : "NO"); Serial.print(" ");
              hasAnyChange = true;
            }
            
            // Nếu không có thay đổi nào được detect, log tất cả giá trị hiện tại
            if (!hasAnyChange) {
              Serial.print("ALL - Temp:"); Serial.print(currentTemp, 1);
              Serial.print(" Heater:"); Serial.print(heaterOn ? "ON" : "OFF");
              Serial.print(" AC:"); Serial.print(acOn ? "ON" : "OFF");
              Serial.print(" Fan:"); Serial.print(fanLevel);
              Serial.print(" Light:"); Serial.print(currentLight);
              Serial.print(" Motion:"); Serial.print(currentMotion ? "YES" : "NO");
            }
            Serial.println();
            
            // Lưu giá trị đã gửi SAU KHI log (để lần sau so sánh đúng)
            lastSentTemp = currentTemp;
            lastSentHeater = heaterOn;
            lastSentAc = acOn;
            lastSentFan = fanLevel;
            lastSentLight = currentLight;
            lastSentMotion = currentMotion;
          } else {
            // apiTask() đang xử lý, nhưng có thay đổi đáng kể
            // Chỉ cập nhật data (không set needUpdate) để apiTask() đọc được giá trị mới nhất
            sensorData.tempRoom = currentTemp;
            sensorData.hasMotion = currentMotion;
            sensorData.lightLevel = currentLight;
            sensorData.mode = digitalRead(SW_MODE);
            sensorData.heaterOn = heaterOn;
            sensorData.acOn = acOn;
            sensorData.fanLevel = fanLevel;
          }
          xSemaphoreGive(dataMutex);
        }
      }
    }
  }
}

// ================== LOGIC BẢO MẬT ==================
void handleSecurity() {
  char key = keypad.getKey();
  if (key) {
    if (key == '#') {
      if (inputPassword == password) {
        isUnlocked = true;
        doorServo.write(90);
        lcd.clear();
        lcd.print("DOOR OPENED!");
        Serial.println("Log: Mat khau dung. He thong kich hoat.");
        delay(1500);
        tempRoom = dht.readTemperature();
      } else {
        wrongCount++;
        inputPassword = "";
        lcd.clear();
        if (wrongCount >= 3) {
          Serial.println("Log: Sai 3 lan. Canh bao RED. Khoa 10p.");
          lockUntil = millis() + 600000; 
        } else {
          Serial.println("Log: Sai pass. Canh bao YELLOW. Khoa 30s.");
          lockUntil = millis() + 30000;
        }
      }
    } else if (key == '*') {
      inputPassword = "";
      lcd.setCursor(0, 1);
      lcd.print("                ");
    } else {
      inputPassword += key;
      lcd.setCursor(0, 1);
      lcd.print(inputPassword);
    }
  }
}

// ================== LOGIC ĐIỀU KHIỂN PHÒNG ==================
void handleSmartHome() {
  bool autoMode = digitalRead(SW_MODE);
  if (lastAutoMode && !autoMode) {
    heaterOn = false; acOn = false; fanLevel = 0;
    Serial.println("Mode: MANUAL - Devices Reset");
  }
  lastAutoMode = autoMode;

  float t = dht.readTemperature();
  if (!isnan(t)) {
    if (t > 100) {
      Serial.println("!!! CANH BAO CHAY: NHIET DO > 100 !!!");
      heaterOn = false; acOn = false;
    }

    if (heaterOn) tempRoom += 0.05;
    if (acOn) tempRoom -= 0.05;

    if (abs(t - tempRoom) > 1.0) tempRoom = t; 
  }

  // --- LOGIC CẢM BIẾN ÁNH SÁNG & CHUYỂN ĐỘNG ---
  int lightVal = analogRead(LDR_PIN);
  bool hasMotion = digitalRead(PIR_PIN);

  // Nếu trời tối (giá trị cao) và có chuyển động -> Bật LED
  if (lightVal > 2500 && hasMotion) {
    digitalWrite(LED_PIR, HIGH);
  } else {
    digitalWrite(LED_PIR, LOW);
  }

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
    bool hasChange = (serverCommands.heaterOn != heaterOn) ||
                     (serverCommands.acOn != acOn) ||
                     (serverCommands.fanLevel != fanLevel);
    
    if (hasChange) {
      // Áp dụng commands từ app
      heaterOn = serverCommands.heaterOn;
      acOn = serverCommands.acOn;
      fanLevel = serverCommands.fanLevel;
      // Mode từ app (nếu có)
      if (serverCommands.mode != autoMode) {
        autoMode = serverCommands.mode;
        Serial.print("Mode changed from app: ");
        Serial.println(autoMode ? "AUTO" : "MANUAL");
      }
      
      // Update LED
      digitalWrite(LED_HEATER, heaterOn);
      digitalWrite(LED_AC, acOn);
      digitalWrite(LED_FAN, fanLevel > 0);
      
      Serial.print("[Control] Applied from app: H=");
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
  } else if (autoMode) {
    // Logic tự động local
    if (tempRoom >= 28) {
      acOn = true; heaterOn = false;
    } else if (tempRoom <= 18) {
      heaterOn = true; acOn = false;
    } else if (tempRoom >= 20 && tempRoom <= 22) {
      acOn = false; heaterOn = false;
    }
    fanLevel = (tempRoom > 25) ? 1 : 0;
    // Update LED cho auto mode
    digitalWrite(LED_HEATER, heaterOn);
    digitalWrite(LED_AC, acOn);
    digitalWrite(LED_FAN, fanLevel > 0);
  } else {
    // Manual mode - chỉ dùng nút vật lý
    // LED đã được set trong handleManualButtons() để phản hồi ngay
    handleManualButtons();
  } 

  lcd.setCursor(0, 0);
  lcd.print(autoMode ? "AUTO  " : "MANUAL");
  lcd.print(" T:"); lcd.print(tempRoom, 1); lcd.print("C  ");

  lcd.setCursor(0, 1);
  lcd.print("F:"); lcd.print(fanLevel);
  lcd.print(" H:"); lcd.print(heaterOn ? "ON " : "OFF");
  lcd.print(" A:"); lcd.print(acOn ? "ON " : "OFF");

  delay(200); // Giữ delay như code gốc
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
  
  // Detect edge (HIGH -> LOW) với debounce ngắn hơn
  if (btnAC == LOW && lastBtnAC == HIGH && now - lastPressAC > 50) {
    Serial.println("[BUTTON] AC pressed - START");
    acOn = !acOn;
    if (acOn) heaterOn = false;
    lastPressAC = now;
    lastManualButtonPress = now; // Track thời gian bấm nút
    Serial.print("[BUTTON] AC pressed - DONE, AC=");
    Serial.println(acOn ? "ON" : "OFF");
    // Áp dụng ngay lập tức
    digitalWrite(LED_AC, acOn);
    digitalWrite(LED_HEATER, heaterOn);
    // Force gửi data lên DB ngay để tránh controlTask fetch data cũ
    forceDataUpdate();
  }
  lastBtnAC = btnAC;

  if (btnFAN == LOW && lastBtnFAN == HIGH && now - lastPressFAN > 50) {
    Serial.println("[BUTTON] FAN pressed - START");
    fanLevel++;
    if (fanLevel > 3) fanLevel = 0;
    lastPressFAN = now;
    lastManualButtonPress = now; // Track thời gian bấm nút
    Serial.print("[BUTTON] FAN pressed - DONE, Level=");
    Serial.println(fanLevel);
    // Áp dụng ngay lập tức
    digitalWrite(LED_FAN, fanLevel > 0);
    // Force gửi data lên DB ngay để tránh controlTask fetch data cũ
    forceDataUpdate();
  }
  lastBtnFAN = btnFAN;

  if (btnHEATER == LOW && lastBtnHEATER == HIGH && now - lastPressHEATER > 50) {
    Serial.println("[BUTTON] HEATER pressed - START");
    heaterOn = !heaterOn;
    if (heaterOn) acOn = false;
    lastPressHEATER = now;
    Serial.print("[BUTTON] HEATER pressed - DONE, Heater=");
    Serial.println(heaterOn ? "ON" : "OFF");
    // Áp dụng ngay lập tức
    digitalWrite(LED_HEATER, heaterOn);
    digitalWrite(LED_AC, acOn);
  }
  lastBtnHEATER = btnHEATER;
}

// ================== WIFI & API FUNCTIONS ==================
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
      if (type == "bedroom") {
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
          float temp = sensorData.tempRoom;
          bool motion = sensorData.hasMotion;
          int light = sensorData.lightLevel;
          bool mode = sensorData.mode;
          bool heater = sensorData.heaterOn;
          bool ac = sensorData.acOn;
          int fan = sensorData.fanLevel;
          
          // Đánh dấu đang gửi HTTP và đã lấy data
          isSending = true;
          sensorData.needUpdate = false;
          
          xSemaphoreGive(dataMutex);
          
          Serial.print("[API Task] Sending data, temp=");
          Serial.println(temp);
      
      // Gửi HTTP request (có thể mất vài giây, nhưng không block main loop)
      HTTPClient http;
      String url = String(apiBaseUrl) + "/api/rooms/" + roomId + "/data";
      http.begin(url);
      http.addHeader("Content-Type", "application/json");
      http.setTimeout(5000); // Timeout 5 giây
      http.setConnectTimeout(3000); // Timeout kết nối 3 giây
      
      // Giảm JSON document size
      DynamicJsonDocument doc(256);
      doc["tempRoom"] = temp;
      doc["hasMotion"] = motion;
      doc["lightLevel"] = light;
      doc["mode"] = mode ? "auto" : "manual";
      doc["heaterOn"] = heater;
      doc["acOn"] = ac;
      doc["fanLevel"] = fan;
      
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
        DynamicJsonDocument doc(512);
        deserializeJson(doc, payload);
        
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
  float currentTemp = dht.readTemperature();
  if (isnan(currentTemp)) {
    currentTemp = tempRoom;
  }
  int currentLight = analogRead(LDR_PIN);
  bool pirMotion = digitalRead(PIR_PIN);
  bool currentMotion = (currentLight > 2500) && pirMotion;
  
  // Force update sensorData và set needUpdate = true
  if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    sensorData.tempRoom = currentTemp;
    sensorData.hasMotion = currentMotion;
    sensorData.lightLevel = currentLight;
    sensorData.mode = digitalRead(SW_MODE);
    sensorData.heaterOn = heaterOn;
    sensorData.acOn = acOn;
    sensorData.fanLevel = fanLevel;
    sensorData.needUpdate = true;
    xSemaphoreGive(dataMutex);
    
    Serial.println("[FORCE] Data update triggered after button press");
  }
}
