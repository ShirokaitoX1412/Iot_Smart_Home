# Troubleshooting - ESP32 không được nhận diện trên Mac

## Vấn đề: PlatformIO chọn sai port (Bluetooth thay vì ESP32)

### Giải pháp 1: Kiểm tra cáp USB
**Quan trọng**: Không phải cáp USB nào cũng hỗ trợ truyền dữ liệu!

1. **Thử cáp USB khác** - Đảm bảo cáp có thể truyền dữ liệu (không chỉ sạc)
2. **Thử cổng USB khác** trên máy tính
3. **Kiểm tra cáp có dây data** - Một số cáp chỉ có dây sạc (power only)

### Giải pháp 2: Cài đặt Driver

ESP32 thường dùng một trong các chip USB-to-Serial sau:

#### CP2102 (Silicon Labs)
1. Tải driver: https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers
2. Cài đặt và khởi động lại Mac
3. Cắm lại ESP32

#### CH340
1. Tải driver: https://github.com/WCHSoftGroup/ch34xser_macos
2. Cài đặt và khởi động lại Mac
3. Cắm lại ESP32

#### Cách kiểm tra ESP32 dùng chip nào:
- Xem trên board ESP32 (thường ghi CP2102 hoặc CH340)
- Hoặc thử cài cả 2 driver

### Giải pháp 3: Kiểm tra ESP32 có được nhận diện không

Sau khi cài driver và cắm lại ESP32, chạy lệnh:
```bash
ls /dev/cu.* | grep -v "Bluetooth\|debug-console"
```

Nếu thấy port mới (ví dụ: `/dev/cu.usbserial-1410`), đó là ESP32!

### Giải pháp 4: Upload bằng cách khác

Nếu vẫn không tìm được port, có thể ESP32 đã có firmware cũ và cần vào chế độ download:

1. **Giữ nút BOOT** trên ESP32
2. **Nhấn và thả nút RESET** (vẫn giữ BOOT)
3. **Thả nút BOOT**
4. **Nhấn Upload** trong PlatformIO ngay lập tức

### Giải pháp 5: Kiểm tra System Information

1. Mở **System Information** (Ứng dụng → Tiện ích → System Information)
2. Chọn **USB** ở sidebar bên trái
3. Tìm thiết bị có tên:
   - "CP2102"
   - "CH340"
   - "Silicon Labs"
   - "USB Serial"
   - Hoặc tên board ESP32

Nếu không thấy, ESP32 chưa được nhận diện → Cần cài driver hoặc thử cáp khác.

### Giải pháp 6: Thử ESP32 khác

Nếu có ESP32 khác, thử cắm vào để xác định vấn đề là ở ESP32 hay máy tính.

## Sau khi tìm được port

1. Mở `platformio.ini`
2. Uncomment và điền port:
```ini
upload_port = /dev/cu.usbserial-1410  ; Thay bằng port bạn tìm được
```

3. Upload lại code

## Lưu ý

- **Đèn sáng** chỉ có nghĩa ESP32 có nguồn, không có nghĩa đã được nhận diện bởi máy tính
- **Cáp USB** phải hỗ trợ data transfer (không chỉ sạc)
- **Driver** phải được cài đúng cho chip USB-to-Serial của ESP32

