#include <Keypad.h>
#include <ESP32Servo.h>
#include <LiquidCrystal_I2C.h>
#include "DHT.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

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
const char* apiBaseUrl = "https://smart-home-iot.loca.lt";
String roomId = "";
bool wifiConnected = false;
unsigned long lastApiCall = 0;
const unsigned long apiInterval = 2000; // Gọi API mỗi 2 giây (định kỳ, không block nút)

/* ================= BIẾN HỆ THỐNG ================= */
String password = "1234";
String inputPass = "";
bool isAuthenticated = false; 
unsigned long lockUntil = 0;
int failCount = 0;

float roomTemp = 0; 
float waterTemp = 40.0; 
int currentHour = 19; 

bool lastWaterState = false;
bool lastHeaterState = false;
bool manualHeaterToggle = false;
bool lastBtnState = HIGH; 

// ================== FUNCTION PROTOTYPES ==================
void handleKeypad();
void showLockedScreen();
void handleWaterHeater(bool hasPerson);
void manualMode();
void autoMode(bool hasPerson);
void updateLCD();
void connectWiFi();
void sendSensorDataNonBlocking();
String getRoomIdByName();

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
    }
  }
}

void loop() {
  handleKeypad();

  if (!isAuthenticated) { showLockedScreen(); return; }

  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate > 1000) {
    float t = dht.readTemperature();
    if (!isnan(t)) roomTemp = t;
    lastUpdate = millis();
  }

  bool hasPerson = digitalRead(PIR_PIN);
  digitalWrite(LED_PIR, hasPerson);
  handleWaterHeater(hasPerson);

  if (digitalRead(SWITCH_MODE) == LOW) {
    manualMode();
  } else {
    autoMode(hasPerson);
  }
  
  updateLCD();
  
  // Gửi API định kỳ - CHỈ khi không có nút nào được bấm gần đây
  static unsigned long lastButtonPress = 0;
  
  // Kiểm tra xem có nút nào được bấm không
  bool buttonPressed = (digitalRead(BTN_HEATER) == LOW);
  
  if (buttonPressed) {
    lastButtonPress = millis();
  }
  
  // Chỉ gửi API nếu:
  // 1. Đã qua 5 giây kể từ lần gửi cuối
  // 2. VÀ không có nút nào được bấm trong 2 giây gần đây
  unsigned long timeSinceLastButton = millis() - lastButtonPress;
  if (wifiConnected && roomId != "" && 
      millis() - lastApiCall > apiInterval &&
      timeSinceLastButton > 2000) {
    sendSensorDataNonBlocking();
    lastApiCall = millis();
  }
}

/* ============ LOGIC AUTO: SỬA LỖI TẮT Ở 27 ĐỘ ============ */
void autoMode(bool hasPerson) {
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
    Serial.print("[BUTTON] HEATER pressed - DONE, Heater=");
    Serial.println(manualHeaterToggle ? "ON" : "OFF");
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
    if (inputPass == password) { 
      isAuthenticated = true; 
      doorServo.write(90); 
      digitalWrite(LED_WARNING, LOW); // Tắt đèn cảnh báo nếu trước đó có sai
      lcd.clear(); 
      failCount = 0; // Reset số lần sai
    } 
    else { 
      inputPass = ""; 
      failCount++;
      lcd.clear(); 
      lcd.print("WRONG PASS"); 
      
      // BẬT ĐÈN CẢNH BÁO
      digitalWrite(LED_WARNING, HIGH); 
      delay(1000); 
      digitalWrite(LED_WARNING, LOW); // Tắt đèn để chuẩn bị nhập lại
      lcd.clear();
    }
  } else if (key == '*') { 
    inputPass = ""; 
    digitalWrite(LED_WARNING, LOW);
  } else if (inputPass.length() < 4) { 
    inputPass += key; 
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
  lcd.print(digitalRead(SWITCH_MODE) == LOW ? "MODE: MANUAL   " : "MODE: AUTO     ");
}

void showLockedScreen() {
  lcd.setCursor(0,0);
  lcd.print("ENTER PASSWORD:");
  lcd.setCursor(0,1);
  lcd.print("PASS: "); lcd.print(inputPass); lcd.print("    ");
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

// Gửi data không block - timeout rất ngắn
void sendSensorDataNonBlocking() {
  if (!wifiConnected || roomId == "") return;
  
  HTTPClient http;
  String url = String(apiBaseUrl) + "/api/rooms/" + roomId + "/data";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(300); // Timeout cực ngắn - 300ms để không block
  
  DynamicJsonDocument doc(512);
  doc["roomTemp"] = roomTemp;
  doc["waterTemp"] = waterTemp;
  doc["hasMotion"] = digitalRead(PIR_PIN);
  doc["mode"] = (digitalRead(SWITCH_MODE) == LOW) ? "manual" : "auto";
  doc["heaterOn"] = lastHeaterState;
  doc["waterHeaterOn"] = lastWaterState;
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  // Gửi với timeout ngắn - sẽ timeout nếu chậm nhưng không block lâu
  unsigned long start = millis();
  int httpCode = http.POST(jsonString);
  unsigned long elapsed = millis() - start;
  
  http.end();
  
  if (elapsed > 100) {
    Serial.print("[API] Sent in ");
    Serial.print(elapsed);
    Serial.println("ms");
  }
}
