# 🏠 Smart Home ESP32 – Living Room & Bedroom System

## 📌 Giới thiệu
Dự án mô phỏng **hệ thống Smart Home** sử dụng **ESP32** trên nền tảng **Wokwi Simulator**, bao gồm hai khu vực:

- **Living Room**: Điều khiển nhiệt độ, đèn, rèm cửa
- **Bedroom**: Giám sát môi trường và hiển thị thông tin trên LCD

Hệ thống sử dụng nhiều cảm biến (DHT22, LDR, PIR, Dust) và thiết bị chấp hành (LED, Servo, LCD).

---

## 🧩 Thành phần phần cứng

### 🔹 Vi điều khiển
- ESP32 DevKit C V4

### 🔹 Cảm biến
| Cảm biến | Chức năng |
|--------|-----------|
| DHT22 | Đo nhiệt độ & độ ẩm |
| LDR | Đo cường độ ánh sáng |
| PIR | Phát hiện chuyển động |
| Dust Sensor (Slider) | Mô phỏng bụi mịn |

### 🔹 Thiết bị chấp hành
| Thiết bị | Chức năng |
|--------|-----------|
| LED | Đèn, quạt, lò sưởi, điều hòa |
| Servo | Điều khiển rèm cửa |
| LCD 20x4 I2C | Hiển thị thông tin hệ thống |

---

## 🏠 Living Room – Chức năng

### 🔥 Điều khiển nhiệt độ (DHT22)
| Nhiệt độ | Thiết bị |
|--------|---------|
| < 22°C | Lò sưởi |
| 22 – 27°C | Quạt |
| > 27°C | Điều hòa |

### 💡 Điều khiển đèn
- Đèn **BẬT** khi:
  - Trời tối (LDR < 300)
  - Có chuyển động (PIR)

### 🪟 Điều khiển rèm cửa (Servo)
| Ánh sáng | Rèm |
|-------|-----|
| Quá tối / quá sáng | Đóng |
| Bình thường | Mở |

---

## 🛏️ Bedroom – Chức năng

### 📟 Hiển thị LCD 20x4
LCD hiển thị:
1. Nhiệt độ (°C)
2. Độ ẩm (%)
3. Chế độ hoạt động (HEATER / FAN / AC)
4. Trạng thái PIR (MOTION / NO MOTION)

### 🌡️ Điều khiển thiết bị theo nhiệt độ
Giống logic phòng khách:
- LED Lò sưởi / Quạt / Điều hòa tương ứng nhiệt độ

### 🌫️ Giám sát bụi
- Đọc giá trị ADC
- Quy đổi sang điện áp và µg/m³
- Hiển thị trên Serial Monitor

---

## 🧠 Logic hệ thống

### 🔁 Sơ đồ điều khiển nhiệt độ

### 📊 Serial Monitor
Hiển thị đầy đủ:
- DHT (Temp / Humidity)
- LDR (Raw / Volt / %)
- Dust (ADC / Volt / µg/m³)
- PIR
- Mode hệ thống

---

## 🔌 Sơ đồ chân ESP32 (quan trọng)

| Thiết bị | GPIO |
|--------|------|
| DHT22 (Living) | GPIO 2 |
| DHT22 (Bedroom) | GPIO 15 |
| LDR | GPIO 34 |
| PIR | GPIO 27 |
| Servo | GPIO 26 |
| LED Đèn | GPIO 25 |
| Lò sưởi | GPIO 13 |
| Quạt | GPIO 12 |
| Điều hòa | GPIO 14 |
| LCD SDA | GPIO 21 |
| LCD SCL | GPIO 22 |



## 🛠️ Thư viện sử dụng

```cpp
#include <Arduino.h>
#include <DHT.h>
#include <DHTesp.h>
#include <ESP32Servo.h>
#include <LiquidCrystal_I2C.h>
