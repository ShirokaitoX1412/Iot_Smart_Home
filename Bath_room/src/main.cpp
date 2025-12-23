#include <Arduino.h>
#include "DHT.h"
#include <ESP32Servo.h>

/* ===== Khai báo chân ===== */
#define DHTPIN 2 
#define DHTTYPE DHT22 

const int LDR_PIN   = 34; 
const int PIR_PIN   = 27; 
const int LED_PIN   = 25; 
const int SERVO_PIN = 26; 

const int LO_SUOI   = 13;
const int QUAT      = 12;
const int DIEU_HOA  = 11;

/* ===== Ngưỡng cài đặt ===== */
const int DARK_THRESHOLD = 300;   
const int SUNNY_LUX      = 900;   
const int PIR_THRESHOLD  = 2000;  

const int CURTAIN_OPEN   = 30;    
const int CURTAIN_CLOSE  = 180;   

DHT dht(DHTPIN, DHTTYPE);
Servo curtain;

void setup() {
  Serial.begin(9600);
  dht.begin();
  
  pinMode(LO_SUOI, OUTPUT);
  pinMode(QUAT, OUTPUT);
  pinMode(DIEU_HOA, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(PIR_PIN, INPUT);

  // Cấu hình cho Servo trên ESP32
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  curtain.setPeriodHertz(50);    
  curtain.attach(SERVO_PIN, 500, 2400); 
  
  curtain.write(CURTAIN_CLOSE); 

  Serial.println("=========================================");
  Serial.println("   HE THONG NHA THONG MINH KHOI DONG     ");
  Serial.println("=========================================");
}

void loop() {
  // --- 1. Đọc cảm biến ---
  float temp = dht.readTemperature();
  int luxValue = analogRead(LDR_PIN);  
  int pirValue = analogRead(PIR_PIN); 

  // --- 2. Logic điều khiển Nhiệt độ ---
  String thietBiNhiet = "";
  if (!isnan(temp)) {
    if (temp < 22) {
      digitalWrite(LO_SUOI, HIGH); digitalWrite(QUAT, LOW); digitalWrite(DIEU_HOA, LOW);
      thietBiNhiet = "Bat Lo Suoi";
    } else if (temp <= 27) {
      digitalWrite(LO_SUOI, LOW); digitalWrite(QUAT, HIGH); digitalWrite(DIEU_HOA, LOW);
      thietBiNhiet = "Bat Quat";
    } else {
      digitalWrite(LO_SUOI, LOW); digitalWrite(QUAT, LOW); digitalWrite(DIEU_HOA, HIGH);
      thietBiNhiet = "Bat Dieu Hoa";
    }
  }

  // --- 3. Logic điều khiển ĐÈN ---
  bool lightOn = false;
  String lyDoDen = "";
  if (luxValue < DARK_THRESHOLD) {
    if (pirValue > PIR_THRESHOLD) {
      lightOn = true;  
      lyDoDen = "Toi + Co nguoi";
    } else {
      lightOn = false; 
      lyDoDen = "Toi + Khong nguoi";
    }
  } else {
    lightOn = false;
    lyDoDen = "Troi dang sang";
  }
  digitalWrite(LED_PIN, lightOn ? HIGH : LOW);

  // --- 4. Logic điều khiển RÈM ---
  String trangThaiRem = "";
  if (luxValue < DARK_THRESHOLD) {
    curtain.write(CURTAIN_CLOSE);
    trangThaiRem = "DONG (Troi toi)";
  } else if (luxValue <= SUNNY_LUX) {
    curtain.write(CURTAIN_OPEN);
    trangThaiRem = "MO (Anh sang dep)";
  } else {
    curtain.write(CURTAIN_CLOSE);
    trangThaiRem = "DONG (Nang qua gat)";
  }

  // --- 5. Serial Monitor (In chi tiết trạng thái) ---
  Serial.println("\n--- CAP NHAT HE THONG ---");
  Serial.printf("[CAM BIEN] Lux: %d | PIR: %d | Temp: %.1fC\n", luxValue, pirValue, temp);
  
  Serial.print("[DIEU KHIEN] ");
  Serial.print("Den: " + String(lightOn ? "ON " : "OFF") + " (" + lyDoDen + ") | ");
  Serial.println("Rem: " + trangThaiRem);
  
  Serial.println("[NHIET DO] " + thietBiNhiet);
  Serial.println("-----------------------------------------");
  
  delay(2000); // Đợi 2 giây để dễ quan sát Serial
}