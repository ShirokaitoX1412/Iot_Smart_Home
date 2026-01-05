#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ================== CẤU HÌNH CHÂN ==================
#define GAS_SENSOR_PIN 32  // GPIO 32 (D32) - chân analog để đọc cảm biến gas "Flying Fish"
#define LED_ALERT_PIN 2    // LED cảnh báo (tùy chọn)

// ================== CẤU HÌNH WIFI & API ==================
const char* ssid = "Le Dinh Tuan T2";
const char* password = "11221122";
const char* apiBaseUrl = "https://iot-smart-home-app.vercel.app"; // Hoặc URL local của bạn
String roomId = ""; // Sẽ được lấy từ database dựa trên tên phòng

// ================== CẤU HÌNH CẢM BIẾN GAS ==================
const float GAS_THRESHOLD = 150.0; // Ngưỡng cảnh báo (ppm) - Điều chỉnh theo cảm biến của bạn
const unsigned long SEND_INTERVAL = 5000; // Gửi data mỗi 5 giây
const unsigned long ALERT_CHECK_INTERVAL = 1000; // Kiểm tra cảnh báo mỗi 1 giây

unsigned long lastSendTime = 0;
unsigned long lastAlertCheckTime = 0;
float lastGasLevel = 0;
bool lastGasAlert = false;

// ================== HÀM KẾT NỐI WIFI ==================
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

// ================== HÀM LẤY ROOM ID ==================
String getRoomIdByName() {
  HTTPClient http;
  String url = String(apiBaseUrl) + "/api/rooms"; // Lấy tất cả rooms
  http.begin(url);
  http.setTimeout(10000);
  
  int httpCode = http.GET();
  String result = "";
  
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, payload);
    
    if (error) {
      Serial.print("JSON parse error: ");
      Serial.println(error.c_str());
    } else if (doc.is<JsonArray>()) {
      JsonArray rooms = doc.as<JsonArray>();
      
      // Tìm room có type = "bedroom" hoặc name = "Phòng Ngủ"
      for (JsonObject room : rooms) {
        String type = room["type"].as<String>();
        String name = room["name"].as<String>();
        
        if (type == "bedroom" || name == "Phòng Ngủ") {
          result = room["_id"].as<String>();
          Serial.print("Room ID found: ");
          Serial.println(result);
          Serial.print("Room name: ");
          Serial.println(name);
          break;
        }
      }
      
      if (result == "") {
        Serial.println("Room 'Phòng Ngủ' (bedroom) not found in database");
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

// ================== HÀM ĐỌC CẢM BIẾN GAS ==================
float readGasSensor() {
  // Đọc giá trị analog từ GPIO 32 (D32)
  // ESP32 ADC: 0-4095 (12-bit) tương ứng 0-3.3V
  int rawValue = analogRead(GAS_SENSOR_PIN);
  
  // Chuyển đổi sang voltage (0-3.3V)
  float voltage = (rawValue / 4095.0) * 3.3;
  
  // DEBUG: Log raw values để kiểm tra
  static unsigned long lastDebugTime = 0;
  static int debugCount = 0;
  if (millis() - lastDebugTime > 2000) { // Log mỗi 2 giây
    Serial.print("[DEBUG] Raw ADC: ");
    Serial.print(rawValue);
    Serial.print(" / Voltage: ");
    Serial.print(voltage, 3);
    Serial.print("V");
    Serial.println();
    lastDebugTime = millis();
    debugCount++;
  }
  
  // CẢM BIẾN GAS THƯỜNG HOẠT ĐỘNG THEO 2 CÁCH:
  // 1. Voltage tăng khi có gas (cảm biến tăng điện trở)
  // 2. Voltage giảm khi có gas (cảm biến giảm điện trở)
  
  // THỬ CÔNG THỨC 1: Giả sử cảm biến tăng voltage khi có gas
  // Với cảm biến MQ-2/MQ-5: R = (Vcc - Vout) * RL / Vout
  // ppm = f(R/R0) - công thức logarit phức tạp
  
  // CÔNG THỨC ĐƠN GIẢN CHO TEST:
  // Giả sử: rawValue 0-4095 tương ứng 0-1000 ppm
  // Hoặc: voltage 0-3.3V tương ứng 0-1000 ppm
  
  // THỬ 1: Dùng rawValue trực tiếp (scale)
  float ppm1 = (rawValue / 4095.0) * 1000.0;
  
  // THỬ 2: Dùng voltage (nếu cảm biến output voltage tỷ lệ với gas)
  float ppm2 = (voltage / 3.3) * 1000.0;
  
  // THỬ 3: Cảm biến phản ứng ngược (voltage giảm khi có gas)
  // Giả sử: 3.3V = 0 ppm, 0V = 1000 ppm
  float ppm3 = ((3.3 - voltage) / 3.3) * 1000.0;
  
  // THỬ 4: Dùng rawValue với offset (nếu cảm biến có baseline)
  // Giả sử baseline là 2000 (khi không có gas)
  float baseline = 2000.0;
  float ppm4 = max(0.0, (rawValue - baseline) * 0.5); // Scale factor tùy chỉnh
  
  // CÔNG THỨC ĐÚNG: Dựa vào log, rawValue thấp (46-287) = không có gas
  // Khi có gas, rawValue sẽ TĂNG (cảm biến MQ thường tăng điện trở khi có gas)
  // Baseline: Giả sử khi không có gas, rawValue ~ 150-200
  // Khi có gas, rawValue tăng lên 300-1000+
  float baselineRaw = 150.0; // Giá trị khi không có gas (từ log: 46-287, trung bình ~150)
  float maxRaw = 1000.0; // Giá trị tối đa khi có nhiều gas
  float ppm5 = max(0.0, ((rawValue - baselineRaw) / (maxRaw - baselineRaw)) * 1000.0);
  
  // CHỌN CÔNG THỨC PHÙ HỢP
  // Dùng ppm5: Tính từ rawValue với baseline
  float ppm = ppm5;
  
  // Đảm bảo ppm không âm
  if (ppm < 0) ppm = 0;
  
  return ppm;
}

// ================== HÀM GỬI DATA LÊN SERVER ==================
void sendGasData(float gasLevel, bool gasAlert) {
  if (roomId == "") {
    Serial.println("Room ID not set, skipping send");
    return;
  }
  
  HTTPClient http;
  String url = String(apiBaseUrl) + "/api/rooms/" + roomId + "/data";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(5000);
  
  DynamicJsonDocument doc(256);
  doc["gasLevel"] = gasLevel;
  doc["gasAlert"] = gasAlert;
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  int httpCode = http.POST(jsonString);
  
  if (httpCode == HTTP_CODE_OK || httpCode == 200) {
    Serial.print("[SEND] Gas data sent: ");
    Serial.print(gasLevel);
    Serial.print(" ppm, Alert: ");
    Serial.println(gasAlert ? "YES" : "NO");
  } else {
    Serial.print("[SEND] Failed to send gas data, code: ");
    Serial.println(httpCode);
  }
  
  http.end();
}

// ================== HÀM GỬI PUSH NOTIFICATION KHI CÓ CẢNH BÁO ==================
void sendGasAlertNotification(float gasLevel) {
  if (roomId == "") return;
  
  HTTPClient http;
  String url = String(apiBaseUrl) + "/api/rooms/" + roomId + "/notify";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(3000);
  
  DynamicJsonDocument doc(128);
  doc["gasAlert"] = true;
  doc["gasLevel"] = gasLevel;
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  Serial.print("[NOTIFY] Sending gas alert notification... Level: ");
  Serial.println(gasLevel);
  
  int httpCode = http.POST(jsonString);
  
  if (httpCode == HTTP_CODE_OK || httpCode == 200) {
    Serial.println("[NOTIFY] ✓ Gas alert notification sent successfully");
  } else {
    Serial.print("[NOTIFY] ✗ Failed to send alert, code: ");
    Serial.println(httpCode);
    if (httpCode > 0) {
      String response = http.getString();
      Serial.print("[NOTIFY] Response: ");
      Serial.println(response);
    }
  }
  
  http.end();
}

// ================== SETUP ==================
void setup() {
  Serial.begin(115200);
  
  pinMode(GAS_SENSOR_PIN, INPUT);
  pinMode(LED_ALERT_PIN, OUTPUT);
  digitalWrite(LED_ALERT_PIN, LOW);
  
  Serial.println("Gas Sensor ESP32 Starting...");
  
  // Kết nối WiFi
  connectWiFi();
  
  if (WiFi.status() == WL_CONNECTED) {
    // Lấy Room ID
    roomId = getRoomIdByName();
    
    if (roomId != "") {
      Serial.println("Setup complete! Starting gas monitoring...");
    } else {
      Serial.println("Warning: Could not get Room ID. Check room name in getRoomIdByName()");
    }
  }
}

// ================== LOOP ==================
void loop() {
  // Kiểm tra WiFi
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected, reconnecting...");
    connectWiFi();
    if (WiFi.status() == WL_CONNECTED && roomId == "") {
      roomId = getRoomIdByName();
    }
    delay(1000);
    return;
  }
  
  unsigned long now = millis();
  
  // Đọc cảm biến gas
  float gasLevel = readGasSensor();
  bool gasAlert = gasLevel > GAS_THRESHOLD;
  
  // Điều khiển LED cảnh báo
  digitalWrite(LED_ALERT_PIN, gasAlert ? HIGH : LOW);
  
  // Kiểm tra cảnh báo mới (chỉ gửi notification khi chuyển từ false -> true)
  if (now - lastAlertCheckTime >= ALERT_CHECK_INTERVAL) {
    if (gasAlert && !lastGasAlert) {
      Serial.print("[ALERT] Gas level exceeded threshold! Level: ");
      Serial.print(gasLevel);
      Serial.print(" ppm (threshold: ");
      Serial.print(GAS_THRESHOLD);
      Serial.println(" ppm)");
      
      // Gửi push notification ngay lập tức
      sendGasAlertNotification(gasLevel);
    }
    
    lastGasAlert = gasAlert;
    lastAlertCheckTime = now;
  }
  
  // Gửi data lên server định kỳ
  if (now - lastSendTime >= SEND_INTERVAL) {
    // Chỉ gửi nếu có thay đổi đáng kể hoặc có cảnh báo
    if (abs(gasLevel - lastGasLevel) > 10.0 || gasAlert) {
      sendGasData(gasLevel, gasAlert);
      lastGasLevel = gasLevel;
    }
    
    lastSendTime = now;
  }
  
  // Log giá trị (mỗi giây) với thông tin chi tiết
  static unsigned long lastLogTime = 0;
  if (now - lastLogTime >= 1000) {
    int rawValue = analogRead(GAS_SENSOR_PIN);
    float voltage = (rawValue / 4095.0) * 3.3;
    
    Serial.print("[GAS] Raw: ");
    Serial.print(rawValue);
    Serial.print(" | Voltage: ");
    Serial.print(voltage, 2);
    Serial.print("V | Level: ");
    Serial.print(gasLevel, 1);
    Serial.print(" ppm | Alert: ");
    Serial.println(gasAlert ? "YES" : "NO");
    lastLogTime = now;
  }
  
  delay(100); // Delay nhỏ để tránh đọc quá nhanh
}

