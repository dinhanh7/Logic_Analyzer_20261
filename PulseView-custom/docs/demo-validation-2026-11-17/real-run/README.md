# Kiểm chứng thật sau khi mở quyền — 17–18/11/2026

Cấu hình: Pico 2 `/dev/ttyACM0`, ESP32 `/dev/ttyUSB0`, desktop X11 1920×1080. Firmware đã flash: `RP2350-LA/1.2+trg+an+str+ui18+sync`. Snapshot qua driver 25 MHz, 65536 mẫu; GUI thực hiện live capture mới với cùng cấu hình và bốn decoder đã gán kênh.

| Kiểm tra | Kết quả | Bằng chứng |
|---|---|---|
| Build Pico 2 + PulseView custom | PASS | `build.log` |
| Flash và board quay lại | PASS | `flash.log` |
| Toàn bộ suite | PASS, không cần tắt LeakSanitizer | `suite.log` |
| ESP32 snapshot và streaming | 8/8 PASS | `esp32.log` |
| Demo qua driver + TFT + GUI live | PASS | `demo.log`, `summary.json` |
| PC decode capture thật | UART LA1234; I2C 0x68 / 0x01 / 0x5A; SPI DE AD BE EF 00 01 02 03; CAN ID 0x123 | `capture-1-*.log`, `capture.sr` |
| TFT real buffer | Đúng text, badge, screen ID và bảng byte cho cả bốn bus | `tft.json` |
| PulseView GUI | Đủ 8 kênh, bốn decoder sinh annotation; rate/count toolbar đúng 25 MHz/65536; trigger CH0 falling/pre 50% đọc lại đúng | `pulseview.json` |
| Quan sát GUI | Đã mở và xem ảnh cửa sổ thật | [Ảnh PulseView](pulseview.json.png) |

`esp32-first-late-window.log` giữ lần chạy đầu có CAN frame bị cắt khi trigger index 61177 trong cửa sổ 65536 mẫu. Test được bổ sung tối đa 6 lần thu lại chỉ khi lịch sử sau trigger không đủ; hai unit test bảo đảm không retry để che lỗi decode trong một cửa sổ đủ dài. `esp32.log` là lần chạy sau cùng PASS. Streaming trong script kiểm annotation; số drop không phải assertion trong phiên bản test này.

`capture.sr` là snapshot qua CLI/driver đã xác minh; TFT dùng chính buffer đó. Ảnh và JSON PulseView thuộc lần capture live sau đó, nên payload CAN tăng có thể khác snapshot. MISO CH6 không được dùng làm tiêu chí payload SPI; kiểm SPI dựa MOSI.

Ảnh được chụp từ chính widget Qt, ghi nguyên tử sau khi bố trí/zoom đã ổn định. JSON chỉ báo `screenshot_saved=true` khi ảnh của lần acquisition đó đã được ghi; không dùng ảnh tồn tại từ lần capture cũ để báo PASS. `SHA256SUMS` giữ checksum các artifact.

Chưa có camera chụp TFT hay test tiếp điểm bốn nút vật lý. Readback bảng/text không chứng nhận màu/backlight. Chạy lại trên desktop bằng `bash scripts/demo.sh`; đóng cửa sổ demo cũ trước khi chạy lại để tránh hai chương trình cùng sở hữu cổng.

Bản `+sync` tự cập nhật cửa sổ và packet cache TFT sau mỗi snapshot từ PC, kể cả khi DECODE đang mở; cache cũ không được dùng với RAM mới. Có selftest thay waveform trong cùng trang decode và build/flash lại trước các kết quả cuối ở đây.
