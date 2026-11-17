# Plan — Topic 2 Upgrade: Logic Analyzer 8 kênh 25 MHz trên Pico 2 (RP2350)

## 1. Mục tiêu

Nâng cấp thiết bị từ Topic 1 (4 kênh, 8 MHz) lên Topic 2 (8 kênh, 25 MHz) bằng
Raspberry Pi Pico 2 (RP2350), giữ nguyên kiến trúc PIO+DMA+USB-CDC, thêm RTOS
để tăng tốc xử lý, và đạt các tiêu chuẩn của đề tài 2:
- Số kênh ≥ 8, tốc độ ≥ 25 MHz, hiển thị trên màn hình PC (PulseView).
- Trigger edge/pattern, decode SPI/I2C/UART/CAN.
- Mạch bảo vệ đầu vào 12V DC / 200V xung (đã có từ Topic 1).

## 2. Những gì đã có (Topic 1)

- Firmware: PIO+DMA capture 8 kênh, LAR2 protocol v1.1, trigger edge/pattern+pre-trigger, TFT ST7735 1.8".
- Driver: libsigrok `rp2350-la`, trigger qua config option chuẩn.
- Host tools: host_la.py, emulator, test suite 9/9 pass.
- Verified trên board thật: timing accuracy, trigger index, UART signal, I2C decode, CRC integrity, TFT display.

## 3. Những gì cần thêm/sửa cho Topic 2

### 3.1. Hardware (không đổi phần cứng, chỉ dùng đủ 8 kênh)
- GP0-GP7 = 8 kênh capture (đã dùng, zero skew vì cùng 1 bank).
- GP10-GP16 = TFT (giữ nguyên).
- GP17-29 = free (dùng cho trigger input, nút bấm, LED).
- **Giới hạn bộ nhớ:** RP2350 chỉ có 520 KB SRAM → 8 kênh × 25 MHz = 25 MB/s → buffer 520KB/8 = 65K mẫu = 2,6 ms. Đây là giới hạn phần cứng, không thể tăng mà không thêm SDRAM.
- **Giải pháp:** dùng PIO ring mode (32 KB ring) để capture liên tục và chỉ lưu cửa sổ trigger → tăng thời gian chờ trigger lên vô hạn, nhưng cửa sổ gửi đi vẫn bị giới hạn bởi RAM.

### 3.2. Firmware (reorganize + RTOS)
- **RTOS (FreeRTOS trên RP2350):** tách task:
  - Task 1 (Core 0): PIO+DMA capture + trigger scan (ưu tiên cao nhất).
  - Task 2 (Core 1): USB-CDC streaming (LAR2 protocol).
  - Task 3 (Core 1): TFT display update (overview waveform).
  - Task 4 (Core 0): nút bấm/menu + storage.
- **Capture region:** giữ 256 KB (CAPTURE_BYTES = 262144) cho trigger scan.
- **Window:** pre + post = count ≤ 262144, trigger_index = pre.
- **Trigger enhancement:** thêm protocol-aware trigger (trigger khi xuất hiện byte UART 0xAA, địa chỉ I2C 0x68) — đây là điểm ăn điểm so với clone 200k.
- **Decode on-device:** UART/I2C/SPI decode tại chỗ, hiển thị text trên TFT.
- **Flash storage:** lưu phiên đo vào Flash 4 MB (session save/load).
- **Signature comparator:** so sánh dạng sóng với mẫu tham chiếu (golden reference).
- **Capture diff:** so sánh hai phiên đo bất kỳ.

### 3.3. Protocol (LAR2 v2)
- CONFIG v2 (28 byte): đã có, thêm trigger params.
- CAPTURE_INFO: trigger_index, pretrigger_count, flags.
- Thêm frame type mới: `0x06` = CONFIG_TRIGGER (pattern trigger riêng).

### 3.4. Driver (libsigrok)
- Expose trigger trong PulseView (đã có qua config option).
- Thêm decode giao thức trên PulseView (đã có qua libsigrokdecode).

### 3.5. ESP32 Generator (test source)
- Phát tín hiệu theo các giao thức: UART, I2C, SPI.
- Tối ưu phát theo từng mức tần số yêu cầu (1 MHz, 5 MHz, 25 MHz).
- Bit-bang I2C 100 kHz (đã có), hardware SPI 1 MHz (đã có).
- Thêm SPI ở 5 MHz, 25 MHz để test LA ở tốc độ cao.

## 4. Kế hoạch thực hiện (4 tuần)

### Tuần 1: Reorganize firmware + RTOS
- [ ] Tách firmware thành module: `capture.c`, `trigger.c`, `protocol.c`, `display.c`, `storage.c`.
- [ ] Tích hợp FreeRTOS: 4 task, 2 core.
- [ ] Verify build + test suite vẫn pass.
- [ ] Commit branch `feature/topic2-reorg`.

### Tuần 2: Protocol-aware trigger + on-device decode
- [ ] Thêm trigger theo byte UART (0xAA) và địa chỉ I2C (0x68).
- [ ] Implement UART decode tại chỗ (idle-high, start bit, bit width).
- [ ] Implement I2C decode tại chỗ (START, address, ACK, data).
- [ ] Hiển thị kết quả decode trên TFT.
- [ ] Test trên board thật.

### Tuần 3: Flash storage + Signature comparator + Capture diff
- [ ] Implement session save/load vào Flash.
- [ ] Implement signature comparator (golden reference).
- [ ] Implement capture diff (so sánh 2 phiên).
- [ ] Test trên board thật.

### Tuần 4: Polish + Test + Demo
- [ ] Tối ưu ESP32 generator: phát SPI ở 5/25 MHz.
- [ ] Test đầy đủ 8 kênh × 25 MHz.
- [ ] Verify timing accuracy, trigger index, decode.
- [ ] Quay video demo ≤3 phút.
- [ ] Viết báo cáo Word + slide.

## 5. Rủi ro

| Rủi ro | Mức độ | Khắc phục |
|---|---|---|
| Buffer nông (65K mẫu @25 MHz = 2,6 ms) | Cao | Dùng ring mode PIO (32 KB) để capture liên tục, chỉ lưu cửa sổ trigger |
| USB FS 12 Mbps không streaming realtime | Cao | Offline capture (đề cho phép) |
| FreeRTOS overhead trên capture task | Trung bình | Capture task ưu tiên cao nhất, không bị preempt |
| SPI 40 MHz TFT quá nhanh | Thấp | Đã hạ xuống 20 MHz, đã verify |
| 8 kênh trên breadboard nhiễu | Trung bình | Dây ngắn <5cm, mass riêng mỗi kênh |

## 6. Tiêu chí nghiệm thu Topic 2

- 8 kênh capture đồng thời ở 25 MHz, skew < 1 sample.
- Trigger edge + pattern + protocol-aware.
- Decode UART/I2C/SPI tại chỗ + trên PulseView.
- Buffer ≥ 65.000 mẫu @25 MHz (2,6 ms).
- Mạch bảo vệ 12V DC / 200V xung (đã có).
- Báo cáo + slide + video demo ≤3 phút.
