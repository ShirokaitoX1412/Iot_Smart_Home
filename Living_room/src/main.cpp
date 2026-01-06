#include <DHT.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

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

// ================== ĐỐI TƯỢNG & BIẾN ==================
DHT dht(DHTPIN, DHTTYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2);

float tempRoom = 0;
float humidity = 0;
bool heaterOn = false;
bool acOn = false;
bool humidifierOn = false;
int fanLevel = 0; // 0, 1, 2, 3
bool lastAutoMode = true;

int dustLevel = 0;     // Giá trị từ 0-4095
String dustStatus = ""; // TOT, TRUNG BINH, KEM, XAU

int luxValue = 0;      // Giá trị ánh sáng

// ================== WIFI & API ==================
const char* ssid = "Le Dinh Tuan T2";
const char* password = "11221122";
const char* apiBaseUrl = "https://iot-smart-home-app.vercel.app";
String roomId = "";
bool wifiConnected = false;

// Đồng bộ định kỳ với server (gửi sensor data + nhận lệnh điều khiển)
unsigned long lastSyncTime = 0;
const unsigned long SYNC_INTERVAL = 2000; // ms

// Ưu tiên nút vật lý sau khi bấm
unsigned long lastManualButtonPress = 0;
const unsigned long MANUAL_BUTTON_PRIORITY_TIME = 20000; // 20 giây

// ================== FUNCTION PROTOTYPES ==================
void handleDustSensor();
void handleCurtainControl();
void handleSmartHome();
void handleManualButtons();
void connectWiFi();
String getRoomIdByName();
void syncWithServer();
void sendSensorData();
void fetchControlCommands();

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

  // Kết nối WiFi và lấy roomId của Phòng Khách
  connectWiFi();
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    roomId = getRoomIdByName();
    if (roomId != "") {
      Serial.print("Room ID (Phòng Khách) found: ");
      Serial.println(roomId);
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

  // Đồng bộ với server định kỳ (không gọi quá nhiều)
  if (wifiConnected && roomId != "" && millis() - lastSyncTime >= SYNC_INTERVAL) {
    lastSyncTime = millis();
    syncWithServer();
  }
}

// ================== XỬ LÝ CẢM BIẾN BỤI ==================
void handleDustSensor() {
  dustLevel = analogRead(DUST_PIN);
  
  // Slide potentiometer: 0-4095 (đầy đủ dải ADC)
  // Chia đều thành 4 mức (mỗi mức 1024)
  // 0-1023: TỐT - TRẮNG (trái cùng)
  // 1024-2047: TRUNG BÌNH - XANH LÁ
  // 2048-3071: KÉM - VÀNG
  // 3072-4095: XẤU - ĐỎ (phải cùng)
  
  if (dustLevel <= 1023) {
    // TỐT - Màu TRẮNG (Red + Green + Blue)
    dustStatus = "TOT";
    digitalWrite(RGB_RED, HIGH);
    digitalWrite(RGB_GREEN, HIGH);
    digitalWrite(RGB_BLUE, HIGH);
    Serial.print("Bui: TOT - TRANG (");
  } 
  else if (dustLevel <= 2047) {
    // TRUNG BÌNH - Màu XANH LÁ (Green)
    dustStatus = "TB";
    digitalWrite(RGB_RED, LOW);
    digitalWrite(RGB_GREEN, HIGH);
    digitalWrite(RGB_BLUE, LOW);
    Serial.print("Bui: TRUNG BINH - XANH LA (");
  } 
  else if (dustLevel <= 3071) {
    // KÉM - Màu VÀNG (Red + Green)
    dustStatus = "KEM";
    digitalWrite(RGB_RED, HIGH);
    digitalWrite(RGB_GREEN, HIGH);
    digitalWrite(RGB_BLUE, LOW);
    Serial.print("Bui: KEM - VANG (");
  } 
  else {
    // XẤU - Màu ĐỎ (Red)
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
  // Đọc giá trị LDR (0-4095) và chuyển sang Lux ước tính
  int ldrValue = analogRead(LDR_PIN);
  // Ước tính Lux: 0 (tối) -> 4095 (sáng nhất) 
  // Giả sử 4095 ~ 1500 Lux
  luxValue = map(ldrValue, 0, 4095, 0, 1500);
  
  bool hasMotion = digitalRead(PIR_PIN);

  // LOGIC ĐIỀU KHIỂN ĐÈN PIR DỰA TRÊN ÁNH SÁNG
  if (luxValue < 300) {
    // Đèn bật nếu có người, tắt nếu không có người
    digitalWrite(LED_PIR, hasMotion ? HIGH : LOW);
  } else if (luxValue >= 300 && luxValue <= 900) {
    // Đèn tắt
    digitalWrite(LED_PIR, LOW);
  } else {
    // Đèn tắt
    digitalWrite(LED_PIR, LOW);
  }

  // Log trạng thái ánh sáng & chuyển động
  Serial.print("LDR: ");
  Serial.print(ldrValue);
  Serial.print(" -> Lux: ");
  Serial.print(luxValue);
  Serial.print(" | Den: ");
  Serial.println(hasMotion && luxValue < 300 ? "ON" : "OFF");
}

// ================== LOGIC ĐIỀU KHIỂN PHÒNG ==================
void handleSmartHome() {
  bool autoMode = digitalRead(SW_MODE);

  if (lastAutoMode && !autoMode) {
    heaterOn = false; 
    acOn = false; 
    fanLevel = 0;
    Serial.println("Mode: MANUAL - Temp Control Reset (Humidifier stays AUTO)");
  }
  lastAutoMode = autoMode;

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

  // --- LƯU Ý: Đèn PIR đã được xử lý trong handleCurtainControl() ---
  // Không cần xử lý lại ở đây để tránh xung đột

  // --- ĐIỀU KHIỂN MÁY TẠO ẨM (Luôn tự động) ---
  // Bật khi độ ẩm < 40% HOẶC bụi ở mức KÉM/XẤU (>= 2048)
  humidifierOn = (humidity < 40) || (dustLevel >= 2048);
  
  // --- XỬ LÝ AUTO/MANUAL CHO NHIỆT ĐỘ ---
  if (autoMode) {
    // Điều khiển nhiệt độ
    if (tempRoom >= 28) {
      acOn = true; heaterOn = false;
    } else if (tempRoom <= 18) {
      heaterOn = true; acOn = false;
    } else if (tempRoom >= 20 && tempRoom <= 22) {
      acOn = false; heaterOn = false;
    }
    
    // Điều khiển quạt
    fanLevel = (tempRoom > 25) ? 1 : 0;
    
  } else {
    handleManualButtons();
  }

  digitalWrite(LED_HEATER, heaterOn);
  digitalWrite(LED_AC, acOn);
  digitalWrite(LED_FAN, fanLevel > 0);
  digitalWrite(LED_HUMIDIFIER, humidifierOn);

  // Hiển thị LCD - Dòng 1: Chế độ, nhiệt độ, độ ẩm
  lcd.setCursor(0, 0);
  lcd.print(autoMode ? "AUTO" : "MAN ");
  lcd.print(" T:"); 
  lcd.print(tempRoom, 1); 
  lcd.print(" H:");
  lcd.print((int)humidity);
  lcd.print("  ");

  // Hiển thị LCD - Dòng 2: Mức bụi và trạng thái thiết bị
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
  static unsigned long lastPress = 0;
  if (millis() - lastPress < 250) return;

  if (digitalRead(BTN_AC) == LOW) {
    acOn = !acOn;
    if (acOn) heaterOn = false;
    lastPress = millis();
    lastManualButtonPress = millis();
    Serial.println("Manual: Toggle AC");
  }

  if (digitalRead(BTN_FAN) == LOW) {
    fanLevel++;
    if (fanLevel > 3) fanLevel = 0;
    lastPress = millis();
    lastManualButtonPress = millis();
    Serial.print("Manual: Fan Level "); Serial.println(fanLevel);
  }

  if (digitalRead(BTN_HEATER) == LOW) {
    heaterOn = !heaterOn;
    if (heaterOn) acOn = false;
    lastPress = millis();
    lastManualButtonPress = millis();
    Serial.println("Manual: Toggle Heater");
  }
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

// Lấy roomId cho phòng khách (type = livingroom, name = "Phòng Khách")
String getRoomIdByName() {
  HTTPClient http;
  String url = String(apiBaseUrl) + "/api/rooms"; // Lấy tất cả rooms
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
    if (httpCode > 0) {
      String errorPayload = http.getString();
      Serial.print("Error response: ");
      Serial.println(errorPayload);
    }
  }
  
  http.end();
  return result;
}

// Đồng bộ: gửi sensor data lên app + nhận lệnh điều khiển
void syncWithServer() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected, skip sync");
    return;
  }

  if (roomId == "") {
    Serial.println("Room ID is empty, skip sync");
    return;
  }

  sendSensorData();
  fetchControlCommands();
}

// Gửi dữ liệu cảm biến lên /api/rooms/{id}/data
void sendSensorData() {
  HTTPClient http;
  String url = String(apiBaseUrl) + "/api/rooms/" + roomId + "/data";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(5000);
  http.setConnectTimeout(3000);

  bool hasMotion = digitalRead(PIR_PIN);
  bool autoMode = digitalRead(SW_MODE); // HIGH/LOW -> auto/manual (mapping giống logic hiện tại)

  DynamicJsonDocument doc(512);
  doc["tempRoom"] = tempRoom;
  doc["hasMotion"] = hasMotion;
  doc["lightLevel"] = luxValue;
  doc["mode"] = autoMode ? "auto" : "manual";
  doc["heaterOn"] = heaterOn;
  doc["acOn"] = acOn;
  doc["fanLevel"] = fanLevel;
  doc["isUnlocked"] = true; // Phòng khách luôn không khóa

  String jsonString;
  serializeJson(doc, jsonString);

  Serial.print("[LivingRoom] Sending data: ");
  Serial.println(jsonString);

  unsigned long start = millis();
  int httpCode = http.POST(jsonString);
  unsigned long elapsed = millis() - start;

  if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_CREATED) {
    Serial.print("[LivingRoom] Data OK ");
    Serial.print(elapsed);
    Serial.println("ms");
  } else {
    Serial.print("[LivingRoom] Data FAIL ");
    Serial.print(httpCode);
    Serial.print(" (");
    Serial.print(elapsed);
    Serial.println("ms)");
  }

  http.end();
}

// Lấy trạng thái điều khiển từ app (heaterOn, acOn, fanLevel)
void fetchControlCommands() {
  // Nếu vừa bấm nút vật lý thì ưu tiên trạng thái tại thiết bị, bỏ qua command từ app một thời gian
  if (millis() - lastManualButtonPress < MANUAL_BUTTON_PRIORITY_TIME) {
    Serial.println("[LivingRoom] Skipping server commands due to recent manual button press");
    return;
  }

  HTTPClient http;
  String url = String(apiBaseUrl) + "/api/rooms/" + roomId;
  http.begin(url);
  http.setTimeout(4000);
  http.setConnectTimeout(2000);

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.print("[LivingRoom] Control GET failed: ");
    Serial.println(httpCode);
    http.end();
    return;
  }

  String payload = http.getString();
  http.end();

  DynamicJsonDocument doc(2048);
  DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    Serial.print("[LivingRoom] JSON deserialize error: ");
    Serial.println(error.c_str());
    return;
  }

  bool serverHeaterOn = doc["heaterOn"].is<bool>() ? doc["heaterOn"].as<bool>() : heaterOn;
  bool serverAcOn = doc["acOn"].is<bool>() ? doc["acOn"].as<bool>() : acOn;
  int serverFanLevel = doc["fanLevel"].is<int>() ? doc["fanLevel"].as<int>() : fanLevel;

  bool changed = false;

  if (serverHeaterOn != heaterOn) {
    heaterOn = serverHeaterOn;
    if (heaterOn) acOn = false; // Đảm bảo không bật cả 2 cùng lúc
    changed = true;
    Serial.print("[LivingRoom] Apply heaterOn from app: ");
    Serial.println(heaterOn ? "ON" : "OFF");
  }

  if (serverAcOn != acOn) {
    acOn = serverAcOn;
    if (acOn) heaterOn = false;
    changed = true;
    Serial.print("[LivingRoom] Apply acOn from app: ");
    Serial.println(acOn ? "ON" : "OFF");
  }

  if (serverFanLevel != fanLevel) {
    fanLevel = serverFanLevel;
    if (fanLevel < 0) fanLevel = 0;
    if (fanLevel > 3) fanLevel = 3;
    changed = true;
    Serial.print("[LivingRoom] Apply fanLevel from app: ");
    Serial.println(fanLevel);
  }

  if (!changed) {
    Serial.println("[LivingRoom] No control changes from app");
  }
}