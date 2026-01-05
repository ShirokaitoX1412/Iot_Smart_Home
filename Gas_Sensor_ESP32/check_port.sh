#!/bin/bash
# Script kiểm tra port ESP32 trên Mac

echo "=== Kiểm tra port ESP32 ==="
echo ""
echo "Các port hiện có (loại trừ Bluetooth và debug-console):"
echo ""

PORTS=$(ls /dev/cu.* 2>/dev/null | grep -v "Bluetooth\|debug-console")

if [ -z "$PORTS" ]; then
    echo "❌ Không tìm thấy port ESP32!"
    echo ""
    echo "Các bước tiếp theo:"
    echo "1. Đảm bảo ESP32 đã được cắm vào USB"
    echo "2. Cài driver CP2102: https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers"
    echo "3. Hoặc cài driver CH340: https://github.com/WCHSoftGroup/ch34xser_macos"
    echo "4. Khởi động lại Mac"
    echo "5. Cắm lại ESP32 và chạy script này"
else
    echo "✅ Tìm thấy các port sau:"
    echo "$PORTS"
    echo ""
    echo "Port ESP32 thường có tên:"
    echo "  - /dev/cu.usbserial-* (CP2102)"
    echo "  - /dev/cu.SLAB_USBtoUART (CP2102)"
    echo "  - /dev/cu.wchusbserial* (CH340)"
    echo ""
    echo "Nếu thấy port trên, hãy thêm vào platformio.ini:"
    echo "  upload_port = <port_name>"
fi

echo ""
echo "=== Kiểm tra USB devices ==="
system_profiler SPUSBDataType 2>/dev/null | grep -A 10 -i "serial\|uart\|cp210\|ch340\|silicon" | head -20 || echo "Không tìm thấy USB Serial device"

