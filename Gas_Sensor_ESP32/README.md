# Gas Sensor ESP32 - Hướng dẫn tích hợp

## Tổng quan
Code này dùng để đọc cảm biến gas (MQ-2, MQ-5, v.v.) với ESP32 và gửi data lên server, đồng thời gửi push notification khi phát hiện gas vượt ngưỡng.

## Cấu hình

### 1. Cập nhật thông tin WiFi và API
Mở file `src/main.cpp` và cập nhật:
```cpp
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const char* apiBaseUrl = "https://iot-smart-home-app.vercel.app"; // Hoặc URL của bạn
```

### 2. Cập nhật tên phòng
Trong hàm `getRoomIdByName()`, thay đổi tên phòng:
```cpp
String url = String(apiBaseUrl) + "/api/rooms?name=Phòng Ngủ"; // Thay đổi nếu cần
```

### 3. Cấu hình cảm biến gas

#### Chân kết nối:
- **GAS_SENSOR_PIN**: Chân ADC để đọc cảm biến (mặc định: GPIO 34)
- **LED_ALERT_PIN**: LED cảnh báo (mặc định: GPIO 2, tùy chọn)

#### Ngưỡng cảnh báo:
```cpp
const float GAS_THRESHOLD = 300.0; // ppm - Điều chỉnh theo cảm biến của bạn
```

#### Công thức chuyển đổi:
Hàm `readGasSensor()` hiện tại dùng công thức đơn giản. Bạn cần hiệu chỉnh theo datasheet của cảm biến:

**Ví dụ với MQ-2:**
```cpp
float readGasSensor() {
  int rawValue = analogRead(GAS_SENSOR_PIN);
  float voltage = (rawValue / 4095.0) * 3.3;
  
  // Công thức cho MQ-2 (cần hiệu chỉnh):
  // R = (3.3 - voltage) * RL / voltage
  // ppm = pow(10, ((log10(R/R0) - b) / m))
  // Hoặc dùng lookup table từ datasheet
  
  return ppm;
}
```

**Nếu cảm biến đã có ADC tích hợp và trả về trực tiếp giá trị ppm:**
```cpp
float readGasSensor() {
  // Đọc trực tiếp từ cảm biến
  return analogRead(GAS_SENSOR_PIN); // Hoặc giá trị từ serial/I2C
}
```

## Cài đặt

1. **Cài đặt PlatformIO** (nếu chưa có)
2. **Mở project** trong VS Code với PlatformIO extension
3. **Cập nhật** thông tin WiFi và API trong `src/main.cpp`
4. **Kết nối** cảm biến gas vào GPIO 34 (hoặc chân ADC khác)
5. **Upload code** lên ESP32

## Hoạt động

- **Đọc cảm biến**: Mỗi 100ms
- **Gửi data lên server**: Mỗi 5 giây (hoặc khi có thay đổi > 10 ppm)
- **Kiểm tra cảnh báo**: Mỗi 1 giây
- **Gửi push notification**: Ngay khi phát hiện gas vượt ngưỡng (chỉ 1 lần khi chuyển từ false -> true)

## Serial Monitor

Bạn sẽ thấy các log:
```
[GAS] Level: 150.5 ppm, Alert: NO
[ALERT] Gas level exceeded threshold! Level: 350.2 ppm (threshold: 300.0 ppm)
[NOTIFY] Gas alert notification sent
[SEND] Gas data sent: 350.2 ppm, Alert: YES
```

## Tích hợp với code Bed_room hiện tại

Nếu bạn muốn tích hợp cảm biến gas vào code Bed_room hiện tại thay vì dùng ESP32 riêng:

1. Thêm chân cảm biến gas vào `#define`:
```cpp
#define GAS_SENSOR_PIN 34
```

2. Thêm biến global:
```cpp
float gasLevel = 0;
bool gasAlert = false;
const float GAS_THRESHOLD = 300.0;
```

3. Thêm hàm đọc cảm biến:
```cpp
float readGasSensor() {
  // Code đọc cảm biến (như trên)
}
```

4. Trong `loop()`, thêm logic đọc gas:
```cpp
gasLevel = readGasSensor();
gasAlert = gasLevel > GAS_THRESHOLD;

// Gửi notification khi phát hiện alert mới
static bool lastGasAlert = false;
if (gasAlert && !lastGasAlert) {
  sendGasAlertNotification(gasLevel);
}
lastGasAlert = gasAlert;
```

5. Trong `apiTask()`, thêm gas data vào JSON:
```cpp
doc["gasLevel"] = gasLevel;
doc["gasAlert"] = gasAlert;
```

6. Thêm hàm gửi notification:
```cpp
void sendGasAlertNotification(float gasLevel) {
  if (!wifiConnected || roomId == "") return;
  
  HTTPClient http;
  String url = String(apiBaseUrl) + "/api/rooms/" + roomId + "/notify";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  
  DynamicJsonDocument doc(128);
  doc["gasAlert"] = true;
  doc["gasLevel"] = gasLevel;
  
  String jsonString;
  serializeJson(doc, jsonString);
  http.POST(jsonString);
  http.end();
}
```

## Lưu ý

- **Hiệu chỉnh cảm biến**: Mỗi loại cảm biến gas có công thức chuyển đổi khác nhau. Bạn cần tham khảo datasheet để hiệu chỉnh chính xác.
- **Calibration**: Nên calibrate cảm biến trong không khí sạch trước khi sử dụng.
- **Ngưỡng cảnh báo**: Điều chỉnh `GAS_THRESHOLD` phù hợp với môi trường và loại gas bạn muốn phát hiện.

