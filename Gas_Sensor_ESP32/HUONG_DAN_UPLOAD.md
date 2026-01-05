# Hướng dẫn Upload Code lên ESP32

## Cách 1: Sử dụng PlatformIO (Khuyến nghị)

### Bước 1: Cài đặt PlatformIO
1. Mở VS Code
2. Vào Extensions (Ctrl+Shift+X hoặc Cmd+Shift+X trên Mac)
3. Tìm "PlatformIO IDE" và cài đặt
4. Khởi động lại VS Code

### Bước 2: Mở Project
1. Trong VS Code, chọn **File → Open Folder**
2. Chọn thư mục `Gas_Sensor_ESP32`
3. Đợi PlatformIO tự động cài đặt dependencies

### Bước 3: Kết nối ESP32
1. Cắm cáp USB vào ESP32 và máy tính
2. Kiểm tra ESP32 có được nhận diện:
   - **Windows**: Mở Device Manager, tìm "Ports (COM & LPT)" → xem COM port (ví dụ: COM3)
   - **Mac/Linux**: Chạy lệnh `ls /dev/tty.*` hoặc `ls /dev/ttyUSB*` để xem port

### Bước 4: Cấu hình Port (nếu cần)
1. Mở file `platformio.ini`
2. Thêm dòng sau (thay COM3 bằng port của bạn):
```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_port = COM3  ; Windows: COM3, COM4, etc.
; monitor_port = /dev/ttyUSB0  ; Linux
; monitor_port = /dev/tty.usbserial-*  ; Mac
monitor_speed = 115200
lib_deps = 
    bblanchon/ArduinoJson@^6.21.3
```

### Bước 5: Upload Code
1. Nhấn nút **Upload** ở thanh dưới cùng (mũi tên →) hoặc nhấn **Ctrl+Alt+U** (Windows/Linux) hoặc **Cmd+Alt+U** (Mac)
2. Hoặc chạy lệnh trong terminal: `pio run --target upload`
3. Đợi quá trình compile và upload hoàn tất

### Bước 6: Mở Serial Monitor
1. Nhấn nút **Serial Monitor** (biểu tượng ổ cắm) hoặc nhấn **Ctrl+Alt+S** (Windows/Linux) hoặc **Cmd+Alt+S** (Mac)
2. Hoặc chạy lệnh: `pio device monitor`
3. Chọn baud rate: **115200**
4. Bạn sẽ thấy logs từ ESP32

## Cách 2: Sử dụng Arduino IDE

### Bước 1: Cài đặt Arduino IDE
1. Tải Arduino IDE từ https://www.arduino.cc/en/software
2. Cài đặt ESP32 board support:
   - File → Preferences
   - Thêm vào "Additional Board Manager URLs": `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
   - Tools → Board → Boards Manager → Tìm "esp32" → Install

### Bước 2: Cài đặt Libraries
1. Sketch → Include Library → Manage Libraries
2. Tìm và cài đặt:
   - **ArduinoJson** (bởi Benoit Blanchon)
   - **WiFi** (đã có sẵn)
   - **HTTPClient** (đã có sẵn)

### Bước 3: Cấu hình Board
1. Tools → Board → ESP32 Arduino → ESP32 Dev Module
2. Tools → Port → Chọn port của ESP32
3. Tools → Upload Speed → 115200

### Bước 4: Upload Code
1. Mở file `src/main.cpp` trong Arduino IDE
2. Nhấn nút **Upload** (mũi tên →)
3. Đợi upload hoàn tất

### Bước 5: Mở Serial Monitor
1. Tools → Serial Monitor
2. Chọn baud rate: **115200**

## Kiểm tra sau khi Upload

Sau khi upload thành công, mở Serial Monitor và bạn sẽ thấy:

```
Gas Sensor ESP32 Starting...
Connecting to WiFi........
WiFi connected! IP: 192.168.x.x
Room ID: 1234567890abcdef
Setup complete! Starting gas monitoring...
[GAS] Level: 150.5 ppm, Alert: NO
```

Nếu thấy lỗi:
- **WiFi connection failed**: Kiểm tra SSID và password
- **Failed to get room ID**: Kiểm tra API URL và tên phòng
- **Failed to send gas data**: Kiểm tra kết nối internet và API URL

## Troubleshooting

### ESP32 không được nhận diện
- Thử cáp USB khác
- Cài driver CH340/CP2102 (nếu cần)
- Giữ nút BOOT khi cắm USB (một số ESP32 cần)

### Upload bị lỗi
- Nhấn và giữ nút **BOOT** trên ESP32
- Nhấn nút **Upload** trong IDE
- Thả nút **BOOT** khi thấy "Connecting..."
- Hoặc thử giảm Upload Speed xuống 921600 hoặc 460800

### Serial Monitor không hiển thị
- Kiểm tra baud rate: phải là **115200**
- Kiểm tra port đã chọn đúng chưa
- Thử reset ESP32 (nhấn nút RESET)

