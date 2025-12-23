#include <Arduino.h>
#include <DHTesp.h>
#include <LiquidCrystal_I2C.h>

// ================= CHÂN KẾT NỐI =================
const int LED_HEARTBEAT = 26;
const int LED_LO_SUOI   = 13;   
const int LED_BINH_NONGLANH = 25; 

const int DHT_PIN = 15;
const int PIR_PIN = 27;
const int PIR_LED = 4;
const int LDR_PIN = 34;      
const int SLIDER_DUST = 35;  

// ================= CẤU HÌNH THIẾT BỊ =================
#define I2C_ADDR     0x27
#define LCD_COLUMNS  20
#define LCD_LINES    4

DHTesp dhtSensor;
LiquidCrystal_I2C lcd(I2C_ADDR, LCD_COLUMNS, LCD_LINES);

// ================= BIẾN THỜI GIAN GIẢ LẬP =================
int fakeHour = 5; 
int fakeMinute = 55;
unsigned long lastTick = 0;

void setup() {
  Serial.begin(115200);
  delay(1000); 

  pinMode(LED_HEARTBEAT, OUTPUT);
  pinMode(LED_LO_SUOI, OUTPUT);
  pinMode(LED_BINH_NONGLANH, OUTPUT);
  pinMode(PIR_LED, OUTPUT);
  pinMode(PIR_PIN, INPUT);
  pinMode(LDR_PIN, INPUT);

  dhtSensor.setup(DHT_PIN, DHTesp::DHT22);
  lcd.init();
  lcd.backlight();
  lcd.clear();

  Serial.println("\n--- BATHROOM SYSTEM START ---");
  // In tiêu đề cột để nhìn Log cho thẳng
  Serial.println("TIME  | TEMP  | HUM   | PIR | DUST  | LDR | HEATER | W-HEATER");
  Serial.println("------------------------------------------------------------");
}

void loop() {
  static unsigned long lastUpdate = 0;
  bool timeChanged = false;

  // 1. CẬP NHẬT THỜI GIAN GIẢ LẬP
  if (millis() - lastTick >= 1000) {
    fakeMinute++; 
    if (fakeMinute >= 60) {
      fakeMinute = 0;
      fakeHour++;
    }
    if (fakeHour >= 24) fakeHour = 0;
    lastTick = millis();
    digitalWrite(LED_HEARTBEAT, !digitalRead(LED_HEARTBEAT));
    timeChanged = true; 
  }

  // 2. ĐỌC DỮ LIỆU CẢM BIẾN
  TempAndHumidity data = dhtSensor.getTempAndHumidity();
  int pirState = digitalRead(PIR_PIN);
  digitalWrite(PIR_LED, pirState);

  int ldrRaw = analogRead(LDR_PIN);
  int ldrPercent = map(ldrRaw, 4095, 0, 0, 100); 

  int dustADC = analogRead(SLIDER_DUST);
  float dustPM = ((float)dustADC * (3.3f / 4095.0f) - 0.6f) * 100.0f;
  if (dustPM < 0) dustPM = 0;

  // 3. LOGIC ĐIỀU KHIỂN
  bool heaterStatus = (data.temperature < 20.0 && pirState == HIGH);
  digitalWrite(LED_LO_SUOI, heaterStatus ? HIGH : LOW);

  bool waterHeaterStatus = (fakeHour == 6 && pirState == HIGH);
  digitalWrite(LED_BINH_NONGLANH, waterHeaterStatus ? HIGH : LOW);

  // 4. CHỈ CẬP NHẬT LCD VÀ SERIAL KHI THỜI GIAN NHẢY (1 giây 1 lần)
  if (timeChanged) {
    // In Serial trước, dùng nháy kép để đảm bảo không bị ngắt quãng
    Serial.print("\r"); // Đưa con trỏ về đầu dòng (nếu cần)
    Serial.flush();     // Đợi gửi hết dữ liệu cũ
    
    Serial.printf("%02d:%02d | %4.1fC | %4.1f%% | %3s | %5.1f | %3d%% | %-6s | %-8s\n", 
                  fakeHour, fakeMinute, 
                  data.temperature, data.humidity,
                  pirState ? "YES" : "NO",
                  dustPM, ldrPercent,
                  heaterStatus ? "ON" : "OFF",
                  waterHeaterStatus ? "ON" : "OFF");

    // Cập nhật LCD ngay sau đó
    lcd.setCursor(0, 0);
    lcd.printf("%02d:%02d PIR:%-3s LDR:%3d%%", fakeHour, fakeMinute, pirState ? "YES" : "NO", ldrPercent);
    lcd.setCursor(0, 1);
    lcd.printf("T:%2.1fC H:%2.1f%%", data.temperature, data.humidity);
    lcd.setCursor(0, 2);
    lcd.printf("Dust: %5.1f ug/m3", dustPM);
    lcd.setCursor(0, 3);
    lcd.printf("Htr:%-3s W-Heater:%-3s", heaterStatus ? "ON" : "OFF", waterHeaterStatus ? "ON" : "OFF");
  }

  delay(20); 
}