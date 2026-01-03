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
const char* apiBaseUrl = "https://smart-home-iot.loca.lt";
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
void handleManualButtons();
void connectWiFi();
void sendSensorDataNonBlocking();
String getRoomIdByName();
void apiTask(void *pvParameters); // FreeRTOS task function

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
      // Đánh dấu cần update TRƯỚC (để apiTask biết sắp có data mới)
      static unsigned long lastUpdate = 0;
      bool shouldUpdate = (millis() - lastUpdate > apiInterval);
      
      if (shouldUpdate) {
        // Đọc nhiệt độ TRỰC TIẾP từ DHT ngay trước khi gửi (để lấy giá trị mới nhất)
        // Không dùng tempRoom vì nó có thể bị điều chỉnh bởi heater/AC
        float currentTemp = dht.readTemperature();
        if (isnan(currentTemp)) {
          // Nếu đọc lỗi, dùng tempRoom làm fallback
          currentTemp = tempRoom;
        }
        
        // Cập nhật data TRƯỚC khi đánh dấu needUpdate
        // Gửi giá trị đọc TRỰC TIẾP từ DHT (giá trị mới nhất từ Wokwi)
        sensorData.tempRoom = currentTemp;
        sensorData.hasMotion = digitalRead(PIR_PIN);
        sensorData.lightLevel = analogRead(LDR_PIN);
        sensorData.mode = digitalRead(SW_MODE);
        sensorData.heaterOn = heaterOn;
        sensorData.acOn = acOn;
        sensorData.fanLevel = fanLevel;
        
        // Đánh dấu cần update SAU khi đã cập nhật data
        sensorData.needUpdate = true;
        lastUpdate = millis();
        Serial.print("[LOOP] Set needUpdate=true, temp=");
        Serial.println(sensorData.tempRoom);
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
  if (autoMode) {
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

  // Không delay để phản hồi nhanh nhất
  // delay(50);
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
    Serial.print("[BUTTON] AC pressed - DONE, AC=");
    Serial.println(acOn ? "ON" : "OFF");
    // Áp dụng ngay lập tức
    digitalWrite(LED_AC, acOn);
    digitalWrite(LED_HEATER, heaterOn);
  }
  lastBtnAC = btnAC;

  if (btnFAN == LOW && lastBtnFAN == HIGH && now - lastPressFAN > 50) {
    Serial.println("[BUTTON] FAN pressed - START");
    fanLevel++;
    if (fanLevel > 3) fanLevel = 0;
    lastPressFAN = now;
    Serial.print("[BUTTON] FAN pressed - DONE, Level=");
    Serial.println(fanLevel);
    // Áp dụng ngay lập tức
    digitalWrite(LED_FAN, fanLevel > 0);
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
    if (sensorData.needUpdate && wifiConnected && roomId != "") {
      Serial.println("[API Task] Detected needUpdate, sending data...");
      
      // Copy data TRƯỚC khi đánh dấu (để lấy giá trị mới nhất)
      float temp = sensorData.tempRoom;
      bool motion = digitalRead(PIR_PIN);
      int light = analogRead(LDR_PIN);
      bool mode = digitalRead(SW_MODE);
      bool heater = heaterOn;
      bool ac = acOn;
      int fan = fanLevel;
      
      // Đánh dấu đã bắt đầu xử lý (sau khi copy data)
      sensorData.needUpdate = false;
      
      // Gửi HTTP request (có thể mất vài giây, nhưng không block main loop)
      HTTPClient http;
      String url = String(apiBaseUrl) + "/api/rooms/" + roomId + "/data";
      http.begin(url);
      http.addHeader("Content-Type", "application/json");
      // Set timeout cho cả connection và response
      http.setTimeout(3000); // Timeout 3 giây cho toàn bộ request
      http.setConnectTimeout(2000); // Timeout kết nối 2 giây
      
      // Thêm timeout cho write (gửi data)
      WiFiClient *client = http.getStreamPtr();
      if (client) {
        client->setTimeout(3000);
      }
      
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
      
      // Kiểm tra nếu request mất quá lâu (timeout không hoạt động)
      if (elapsed > 5000) {
        Serial.print("[API] WARNING: Request took ");
        Serial.print(elapsed);
        Serial.println("ms (timeout may not be working, likely due to slow localtunnel)");
      }
      
      if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_CREATED) {
        Serial.print("[API] OK ");
        Serial.print(elapsed);
        Serial.println("ms");
      } else {
        Serial.print("[API] Fail ");
        Serial.print(httpCode);
        Serial.print(" (");
        Serial.print(elapsed);
        Serial.println("ms)");
      }
      
      // Đánh dấu đã xử lý xong (sau khi gửi HTTP)
      sensorData.needUpdate = false;
      
      // Sau khi gửi xong, check ngay xem có needUpdate mới không (không delay)
      // Nếu có, sẽ gửi tiếp ngay (bỏ qua delay)
      if (sensorData.needUpdate) {
        continue; // Quay lại đầu vòng lặp để gửi tiếp
      }
    }
    
    // Delay để không chiếm CPU - FreeRTOS sẽ tự động switch tasks
    vTaskDelay(pdMS_TO_TICKS(200)); // Giảm xuống 0.2 giây để check nhanh hơn
  }
}
