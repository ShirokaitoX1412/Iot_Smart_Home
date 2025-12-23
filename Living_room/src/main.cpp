#include <Arduino.h>
#include "DHT.h"
#include <ESP32Servo.h>

#define DHTPIN 2 
#define DHTTYPE DHT22 

const int LDR_PIN   = 34; 
const int PIR_PIN   = 27; 
const int LED_PIN   = 25; 
const int SERVO_PIN = 26; 

const int LO_SUOI   = 13;
const int QUAT      = 12;
const int DIEU_HOA  = 14; // Đã đổi sang chân 14

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

  // Cấu hình Servo cho ESP32
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  curtain.setPeriodHertz(50);
  curtain.attach(SERVO_PIN, 500, 2400);
  curtain.write(CURTAIN_CLOSE);

  Serial.println("HE THONG DA KHOI DONG - KIEM TRA DIEU HOA (PIN 14)");
}

void loop() {
  float temp = dht.readTemperature();
  int luxValue = analogRead(LDR_PIN);  
  int pirValue = analogRead(PIR_PIN); 

  // --- Logic Nhiệt Độ ---
  if (!isnan(temp)) {
    Serial.print("Temp: " + String(temp) + "C -> ");
    if (temp < 22) {
      digitalWrite(LO_SUOI, HIGH); digitalWrite(QUAT, LOW); digitalWrite(DIEU_HOA, LOW);
      Serial.println("BAT LO SUOI");
    } else if (temp <= 27) {
      digitalWrite(LO_SUOI, LOW); digitalWrite(QUAT, HIGH); digitalWrite(DIEU_HOA, LOW);
      Serial.println("BAT QUAT");
    } else {
      digitalWrite(LO_SUOI, LOW); digitalWrite(QUAT, LOW); digitalWrite(DIEU_HOA, HIGH);
      Serial.println("BAT DIEU HOA"); // In ra Serial khi > 27 độ
    }
  }

  // --- Logic Đèn ---
  bool lightOn = (luxValue < DARK_THRESHOLD && pirValue > PIR_THRESHOLD);
  digitalWrite(LED_PIN, lightOn ? HIGH : LOW);

  // --- Logic Rèm ---
  if (luxValue < DARK_THRESHOLD || luxValue > SUNNY_LUX) {
    curtain.write(CURTAIN_CLOSE);
  } else {
    curtain.write(CURTAIN_OPEN);
  }

  delay(2000); 
}