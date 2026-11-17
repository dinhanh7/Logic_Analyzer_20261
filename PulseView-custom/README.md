# RP2350 Logic Analyzer — PulseView custom

Prototype **8 kênh, snapshot25 MS/s**, trigger, decoder UART/I2C/SPI/CAN, TFT ST7735
và PulseView qua driver `rp2350-la`. Firmware Pico SDK C; host Python/libsigrok.

- [TESTING.md](TESTING.md): tài liệu chi tiết để làm slide, tính năng và số đo.
- [TEST-REPORT.md](docs/TEST-REPORT.md): kết quả QA thực thi, defect và bằng chứng.
- [TEST-PLAN-v2.md](docs/TEST-PLAN-v2.md): ma trận và tiêu chí nghiệm thu.
- [OPINION.md](OPINION.md): review và việc cần cải thiện ở phiên sau.
- [DEMO.md](docs/DEMO.md): chạy trình diễn; [LATEST](results/LATEST.txt): run QA cuối.

## Demo trên máy đang nối hai board

```bash
cd /home/namkhanh/EmbeddedSystem/Code/Logic_Analyzer_20261/PulseView-custom
DISPLAY=:0 bash scripts/demo.sh
```

Script nhận Pico theo USB by-id, thiết lập ESP32, kiểm streaming/capture/decode,
hiển thị TFT khoảng2 giây mỗi màn và mở PulseView live với bốn decoder đã gán
channel/baud. Chỉ báo PASS khi các bước bắt buộc đạt; giữ GUI mở sau khi thành công.
Script đóng GUI của lần demo trước, không đóng serial app/session khác của bạn.

## Test

```bash
bash tests/test_qa_offline.sh       # Không cần board/mạng
python3 tests/run_qa.py            # Full QA, có soak300s và reset USB
bash scripts/test.sh
python3 tests/test_esp32_link.py
```

Các run và lỗi trước sửa được lưu riêng trong `results/`. Exit0 của QA nghĩa không
có FAIL đã thực thi; BLOCKED/NOT-VERIFIED vẫn không phải chứng nhận sản phẩm.

## Build

Dùng môi trường/phụ thuộc đã cài như `THIRD_PARTY.md`; build có thể cần mạng ở
bước fetch source, còn các bài test không dùng mạng.

```bash
PICO_BOARD=pico2 bash scripts/build.sh all
bash scripts/build-demo.sh
python3 tools/flash_pico.py
```

ESP32 source tái lập nằm ở `test-generator/esp32`; build bằng ESP-IDF6.0.1.
Mỗi phép thử chỉ nên có một chương trình đang điều khiển cổng Pico.

## Giới hạn

25 MHz là tốc độ lấy mẫu snapshot, không phải streaming25 MHz. Dùng1 MHz RLE cho
traffic nén được; raw500 kHz đã kiểm, raw/dense1 MHz có thể mất mẫu và phải INVALID.
Chỉ có bằng chứng bus3,3V hiện tại; chưa nghiệm thu bảo vệ12V/200V,5V/input loading,
BOM as-built≤450.000 VND và fixture kích đủ8ngõ vào. Không nối cao áp trực tiếp vào GPIO.

[README lịch sử](docs/history/README-before-qa-v2.md) giữ các kết quả bring-up cũ;
trạng thái hiện tại ưu tiên TESTING/TEST-REPORT và artifacts mới nhất.
