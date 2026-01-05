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
const float GAS_THRESHOLD = 300.0; // Ngưỡng cảnh báo (ppm) - điều chỉnh theo cảm biến của bạn
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
  String url = String(apiBaseUrl) + "/api/rooms?name=Phòng Ngủ"; // Thay đổi tên phòng nếu cần
  http.begin(url);
  http.setTimeout(5000);
  
  int httpCode = http.GET();
  String result = "";
  
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, payload);
    
    if (doc.is<JsonArray>() && doc.size() > 0) {
      result = doc[0]["_id"].as<String>();
      Serial.print("Room ID: ");
      Serial.println(result);
    }
  } else {
    Serial.print("Failed to get room ID, code: ");
    Serial.println(httpCode);
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
  
  // Cảm biến "Flying Fish" - công thức chuyển đổi
  // Nếu cảm biến trả về trực tiếp giá trị ppm qua ADC:
  // ppm = rawValue; // Hoặc rawValue * hệ số
  
  // Hoặc nếu cần chuyển từ voltage sang ppm:
  // Với MQ-2/MQ-5 tương tự: dùng công thức logarit
  // R = (3.3 - voltage) * RL / voltage  (RL = load resistor, thường 10kΩ)
  // ppm = pow(10, ((log10(R/R0) - b) / m))  (R0, b, m từ datasheet)
  
  // Công thức đơn giản cho cảm biến analog (cần hiệu chỉnh):
  // Giả sử: 0V = 0 ppm, 3.3V = 1000 ppm
  float ppm = (voltage / 3.3) * 1000.0;
  
  // Hoặc nếu cảm biến đã có công thức riêng, dùng rawValue trực tiếp:
  // float ppm = rawValue; // Nếu cảm biến đã scale sẵn
  
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
  
  int httpCode = http.POST(jsonString);
  
  if (httpCode == HTTP_CODE_OK) {
    Serial.println("[NOTIFY] Gas alert notification sent");
  } else {
    Serial.print("[NOTIFY] Failed to send alert, code: ");
    Serial.println(httpCode);
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
  
  // Log giá trị (mỗi giây)
  static unsigned long lastLogTime = 0;
  if (now - lastLogTime >= 1000) {
    Serial.print("[GAS] Level: ");
    Serial.print(gasLevel);
    Serial.print(" ppm, Alert: ");
    Serial.println(gasAlert ? "YES" : "NO");
    lastLogTime = now;
  }
  
  delay(100); // Delay nhỏ để tránh đọc quá nhanh
}

