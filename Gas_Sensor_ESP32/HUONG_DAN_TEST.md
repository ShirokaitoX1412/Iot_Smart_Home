# Hướng dẫn Test Cảm biến Báo cháy

## Khi nào cảm biến báo cháy?

Cảm biến sẽ báo cháy khi **mức gas vượt quá ngưỡng** được cấu hình trong code:

```cpp
const float GAS_THRESHOLD = 300.0; // ppm - Ngưỡng cảnh báo
```

**Mặc định: 300 ppm** - Nếu mức gas > 300 ppm → Báo cháy

## Cách Test Cảm biến

### Cách 1: Test bằng hơi thở (Đơn giản nhất)
1. **Thổi hơi vào cảm biến** - Hơi thở có CO2 và hơi ẩm, cảm biến có thể phản ứng
2. **Quan sát Serial Monitor** - Xem giá trị gas tăng lên
3. **Nếu giá trị > 300 ppm** → Sẽ báo cháy

### Cách 2: Test bằng khí gas thật (Chính xác nhất)
1. **Bật bếp gas** (bếp gas nhà bếp)
2. **Đưa cảm biến gần** (không quá gần, cách 20-30cm)
3. **Quan sát giá trị tăng** trong Serial Monitor
4. **Khi > 300 ppm** → Sẽ báo cháy

### Cách 3: Test bằng cách giảm ngưỡng (Để test nhanh)
1. Mở file `src/main.cpp`
2. Tìm dòng:
```cpp
const float GAS_THRESHOLD = 300.0; // ppm
```
3. Giảm xuống để test (ví dụ: `50.0` hoặc `100.0`)
4. Upload lại code
5. Bây giờ cảm biến sẽ dễ báo cháy hơn

### Cách 4: Test bằng cách thổi khói
1. **Thắp nến hoặc đốt giấy** (cẩn thận!)
2. **Đưa khói vào cảm biến**
3. Cảm biến sẽ phản ứng với khói

## Điều chỉnh Ngưỡng Cảnh báo

### Ngưỡng thấp (Nhạy cảm hơn):
```cpp
const float GAS_THRESHOLD = 100.0; // Báo sớm hơn
```

### Ngưỡng cao (Ít nhạy hơn):
```cpp
const float GAS_THRESHOLD = 500.0; // Chỉ báo khi nguy hiểm thật sự
```

### Ngưỡng mặc định (Khuyến nghị):
```cpp
const float GAS_THRESHOLD = 300.0; // Cân bằng
```

## Các loại cảm biến gas và ngưỡng phù hợp

### MQ-2 (Phát hiện nhiều loại gas):
- **LPG (gas bếp)**: 200-500 ppm
- **Propane**: 200-500 ppm
- **Methane**: 200-500 ppm
- **Khói**: 100-300 ppm

### MQ-5 (Chuyên LPG/CNG):
- **LPG**: 200-500 ppm
- **CNG**: 200-500 ppm

### Ngưỡng khuyến nghị:
- **300 ppm**: Phù hợp cho hầu hết trường hợp
- **200 ppm**: Nhạy hơn, báo sớm hơn
- **500 ppm**: Ít báo nhầm, nhưng có thể báo muộn

## Kiểm tra trong Serial Monitor

Khi test, bạn sẽ thấy:

```
[GAS] Level: 55.92 ppm, Alert: NO
[GAS] Level: 120.45 ppm, Alert: NO
[GAS] Level: 350.20 ppm, Alert: YES  ← BÁO CHÁY!
[ALERT] Gas level exceeded threshold! Level: 350.20 ppm (threshold: 300.0 ppm)
[NOTIFY] Gas alert notification sent
[SEND] Gas data sent: 350.20 ppm, Alert: YES
```

## Test trong App

Khi cảm biến báo cháy:
1. **App sẽ hiển thị banner màu đỏ** với text "CẢNH BÁO CHÁY!"
2. **Background chuyển màu đỏ nhạt**
3. **Card cảm biến chuyển màu đỏ** và có animation pulse
4. **Push notification** sẽ được gửi (nếu đã bật)

## Lưu ý khi Test

⚠️ **An toàn:**
- Không để cảm biến quá gần nguồn lửa
- Không test trong phòng kín
- Thông gió tốt khi test
- Tắt nguồn gas ngay sau khi test xong

⚠️ **Hiệu chỉnh:**
- Mỗi cảm biến có độ nhạy khác nhau
- Cần test và điều chỉnh `GAS_THRESHOLD` phù hợp
- Nên test trong môi trường thực tế để xác định ngưỡng phù hợp

## Calibration (Hiệu chỉnh)

Nếu cảm biến đọc sai:
1. **Để cảm biến trong không khí sạch** 5-10 phút
2. **Quan sát giá trị baseline** (giá trị khi không có gas)
3. **Điều chỉnh công thức** trong hàm `readGasSensor()` nếu cần

