#include <Arduino.h>
#include <DHTesp.h>
#include <LiquidCrystal_I2C.h>

// ================= LED heartbeat =================
const int LED1 = 26;

// ================= LED mô phỏng thiết bị =================
#define LED_LO_SUOI   13   // < 22°C
#define LED_QUAT      12   // 22 - 27°C
#define LED_DIEU_HOA  14   // > 27°C

// ================= DHT22 + LCD =================
#define I2C_ADDR     0x27
#define LCD_COLUMNS  20
#define LCD_LINES    4

const int DHT_PIN = 15;
DHTesp dhtSensor;
LiquidCrystal_I2C lcd(I2C_ADDR, LCD_COLUMNS, LCD_LINES);

// ================= LDR =================
const int ldrPin = 34;

// ================= PIR =================
const int PIR_PIN = 27;
const int PIR_LED = 4;
int pirState = LOW;

// ================= Dust Sensor =================
const int sliderPin = 35;

// =====================================================
void setup() {
  Serial.begin(115200);

  pinMode(LED1, OUTPUT);
  pinMode(LED_LO_SUOI, OUTPUT);
  pinMode(LED_QUAT, OUTPUT);
  pinMode(LED_DIEU_HOA, OUTPUT);

  pinMode(PIR_PIN, INPUT);
  pinMode(PIR_LED, OUTPUT);

  dhtSensor.setup(DHT_PIN, DHTesp::DHT22);

  lcd.init();
  lcd.backlight();
  lcd.clear();

  Serial.println("System initialized!");
}

// =====================================================
void loop() {

  // ===== LED heartbeat =====
  digitalWrite(LED1, !digitalRead(LED1));

  // ===== Đọc DHT =====
  TempAndHumidity data = dhtSensor.getTempAndHumidity();
  float temp = data.temperature;
  float hum  = data.humidity;

  String modeText = "ERROR";

  // ===== LOGIC NHIỆT ĐỘ =====
  if (!isnan(temp)) {
    if (temp < 22) {
      digitalWrite(LED_LO_SUOI, HIGH);
      digitalWrite(LED_QUAT, LOW);
      digitalWrite(LED_DIEU_HOA, LOW);
      modeText = "HEATER";
    }
    else if (temp <= 27) {
      digitalWrite(LED_LO_SUOI, LOW);
      digitalWrite(LED_QUAT, HIGH);
      digitalWrite(LED_DIEU_HOA, LOW);
      modeText = "FAN";
    }
    else {
      digitalWrite(LED_LO_SUOI, LOW);
      digitalWrite(LED_QUAT, LOW);
      digitalWrite(LED_DIEU_HOA, HIGH);
      modeText = "AC";
    }
  } else {
    digitalWrite(LED_LO_SUOI, LOW);
    digitalWrite(LED_QUAT, LOW);
    digitalWrite(LED_DIEU_HOA, LOW);
  }

  // ===== PIR =====
  pirState = digitalRead(PIR_PIN);
  digitalWrite(PIR_LED, pirState);

  // ===== Dust =====
  int   dustADC  = analogRead(sliderPin);
  float dustVolt = dustADC * (3.3 / 4095.0);
  float dustPM   = max(0.0f, (dustVolt - 0.6f) * 100.0f);

  // ===== LDR =====
  int   ldrRaw     = analogRead(ldrPin);
  float ldrVolt    = ldrRaw * (3.3 / 4095.0);
  int   ldrPercent = map(ldrRaw, 0, 4095, 0, 100);

  // ================= LCD 20x4 =================
  lcd.clear();

  // Dòng 1: Nhiệt độ
  lcd.setCursor(0, 0);
  lcd.print("Temp: ");
  if (!isnan(temp)) {
    lcd.print(temp, 1);
    lcd.print((char)223);
    lcd.print("C");
  } else {
    lcd.print("ERROR");
  }

  // Dòng 2: Độ ẩm
  lcd.setCursor(0, 1);
  lcd.print("Hum : ");
  if (!isnan(hum)) {
    lcd.print(hum, 1);
    lcd.print(" %");
  } else {
    lcd.print("ERROR");
  }

  // Dòng 3: Mode
  lcd.setCursor(0, 2);
  lcd.print("Mode: ");
  lcd.print(modeText);

  // Dòng 4: PIR
  lcd.setCursor(0, 3);
  lcd.print("PIR : ");
  lcd.print(pirState ? "MOTION" : "NO MOTION");

  // ================= SERIAL =================
  Serial.print("\r\n");
Serial.println("===== SYSTEM DATA =====");

Serial.printf(
  "DHT   : %6.1f C   %6.1f %%\r\n",
  temp,
  hum
);

Serial.printf(
  "LDR   : Raw %4d   %4.2f V   %3d %%\r\n",
  ldrRaw,
  ldrVolt,
  ldrPercent
);

Serial.printf(
  "Dust  : ADC %4d   %4.2f V   %5.1f ug/m3\r\n",
  dustADC,
  dustVolt,
  dustPM
);

Serial.printf(
  "PIR   : %-10s\r\n",
  pirState ? "MOTION" : "NO MOTION"
);

Serial.printf(
  "MODE  : %-10s\r\n",
  modeText.c_str()
);

Serial.println("=======================");


  delay(1000);
}
