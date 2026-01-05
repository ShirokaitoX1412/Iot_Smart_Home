# Hướng dẫn cài Driver cho ESP32 trên Mac

## Cách 1: Cài cả 2 driver phổ biến nhất (Khuyến nghị)

Vì không chắc ESP32 của bạn dùng chip nào, hãy cài cả 2 driver phổ biến nhất:

### Driver CP2102 (Silicon Labs)
1. Tải về: https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers
2. Mở file `.dmg` đã tải
3. Chạy file `.pkg` để cài đặt
4. Nhập password nếu được hỏi

### Driver CH340
1. Tải về: https://github.com/WCHSoftGroup/ch34xser_macos/releases
2. Tải file `.pkg` mới nhất
3. Chạy file để cài đặt
4. Nhập password nếu được hỏi

### Sau khi cài xong:
1. **Khởi động lại Mac** (quan trọng!)
2. Cắm lại ESP32 vào USB
3. Chạy lệnh để kiểm tra:
```bash
ls /dev/cu.* | grep -v "Bluetooth\|debug-console"
```

Nếu thấy port mới (ví dụ: `/dev/cu.usbserial-1410`), đó là ESP32!

## Cách 2: Tìm chip USB-to-Serial trên board

Nhìn vào board ESP32, tìm chip nhỏ gần cổng USB. Thường có tên:
- **CP2102** hoặc **CP2104** → Cài driver CP2102
- **CH340** hoặc **CH340G** → Cài driver CH340
- **FT232** → Cài driver FTDI (ít phổ biến hơn)

## Sau khi cài driver và tìm được port

1. Mở file `platformio.ini`
2. Thêm dòng (thay port bằng port bạn tìm được):
```ini
upload_port = /dev/cu.usbserial-1410
```

3. Upload lại code trong PlatformIO

## Kiểm tra nhanh

Chạy script này trong Terminal (từ thư mục project):
```bash
./check_port.sh
```

Hoặc chạy lệnh:
```bash
ls /dev/cu.* | grep -v "Bluetooth\|debug-console"
```

