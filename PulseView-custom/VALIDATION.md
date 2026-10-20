# Kết quả kiểm tra

Môi trường: Ubuntu 22.04 x86_64, GCC 11 cho host, ARM GCC 10.3.1, Qt 5.15.3, Pico SDK 2.2.0. Ngày kiểm tra: 21/10/2026 (Asia/Ho_Chi_Minh).

| Hạng mục | Kết quả |
|---|---|
| Build libsigrok + C++ bindings | Thành công; driver `rp2350-la` có trong `--list-supported` |
| Build libsigrokdecode + sigrok-cli | Thành công, UART decoder sử dụng được |
| Build PulseView Qt | Thành công; binary `.local/bin/pulseview` |
| Firmware Pico 2 W | Thành công, `build/firmware-pico2_w/rp2350_la.uf2` |
| Firmware Pico 2 | Thành công, `build/firmware-pico2/rp2350_la.uf2` |
| C codec với ASan/UBSan | Pass CRC chuẩn, mọi payload 0…1024 byte, frame hỏng, từ chối payload vượt giới hạn |
| Driver nhận capture tối đa | 262144 mẫu, fragment 7 byte, nội dung `.sr` khớp từng byte pattern độc lập |
| Toàn bộ 8 sample rate | Pass với số mẫu lẻ 1031 |
| Tắt kênh | CH0/CH7 vẫn giữ vị trí bit, payload không bị pack lại |
| Fault injection | Phát hiện CRC frame sai, sequence sai, metadata sai, CRC capture sai, thiếu END/timeout |
| Cấu hình ngoài giới hạn | Từ chối rate không hỗ trợ và số mẫu ngoài giới hạn |
| Script capture | Không xuất file `.sr` cuối cùng khi dữ liệu không hợp lệ |
| UART decode thực tế bằng libsigrokdecode | Nhận được dữ liệu từ pattern UART 115200 baud, 8N1 |
| GUI mở file `.sr` | Smoke test offscreen, metadata 25 MHz được nhận |
| GUI kết nối trực tiếp driver | Cổng PTY; nhấn Space hai lần, hai capture 100000 mẫu @1 MHz được driver xác minh |

Log tại máy hiện tại: `build/test-results.log`, `build/gui-smoke.log`, `build/logs/firmware-pico2_w.log`, `build/logs/firmware-pico2.log`. Build host ban đầu có log trong `.deps/`; các lần chạy `scripts/build.sh host` tiếp theo ghi `build/logs/`.

Công cụ giả lập và decoder đã chạy với **driver C được build thực**, không thay driver bằng parser Python. Dữ liệu mẫu do emulator Python tạo độc lập; phần C dùng chung codec giữa firmware/driver được kiểm bằng sanitizer.

**Chưa kiểm tra trên phần cứng thật:** nạp UF2, bắt tín hiệu GPIO, USB transfer trên Pico, FIFO/DMA error khi chạy thực tế, độ chính xác clock/timing, chất lượng input buffer và mạch bảo vệ. Board được udev nhận là Pico 2 W tại `/dev/ttyACM0`; tài khoản chưa có quyền mở cổng, board chưa được đưa vào BOOTSEL. Không có kết luận rằng đường truyền hardware đã hoạt động hoặc đạt chính xác 25 MHz.

Lần demo đầu: nạp UF2, cấp quyền serial, capture nguồn vuông đã biết, kiểm count/timing, thử UART, sau đó đo 5/25 MHz và nhiều kênh đồng thời. Đề tài 1 có thể demo ở rate thấp hơn sau khi đã đo kiểm đạt yêu cầu.

## Kiểm tra lại với board sau khi cấp quyền serial

Ngày 21/10/2026: tài khoản `abuntu` đã thuộc nhóm `dialout`, có quyền đọc/ghi `/dev/ttyACM0`. Đã chạy `sigrok-cli -l 4 -d rp2350-la:conn=/dev/ttyACM0 --show`: mở cổng được nhưng HELLO LAR2 hết thời gian sau 2 giây, không nhận diện được thiết bị; chưa thể capture. Thử đọc CDC thêm không nhận được dữ liệu. Không có ứng dụng khác giữ cổng sau khi kết thúc kiểm tra.

USB hiện tại là `2e8a:f00f`, product `Pico_2W`. Firmware UF2 đã build có USB descriptor `2e8a:0009` (kiểm tra trực tiếp symbol `usbd_desc_device` trong ELF). Vì vậy board hiện chưa chạy đúng binary đã build trong dự án. Cần đưa board vào BOOTSEL và chép `build/firmware-pico2_w/rp2350_la.uf2`, sau đó kiểm tra lại HELLO/capture.

Log: `build/hardware-test/device-show.log`, `build/hardware-test/cdc-response.txt`. Chưa có capture hardware hợp lệ.

## Đã nạp và capture trên Pico 2 W thật

Ngày 21/10/2026: đã chuyển firmware cũ (đang in log quét Wi-Fi) vào BOOTSEL bằng USB CDC 1200 baud và hạ DTR. Board xuất hiện dưới USB ID `2e8a:000f`, ổ `RP2350`, Board-ID `RP2350`. Đã chép 52736 byte từ `build/firmware-pico2_w/rp2350_la.uf2` và fsync thành công. Sau reboot, board xuất hiện là `2e8a:0009`, serial `907CBC57E4828DD6`, cổng `/dev/ttyACM0`.

- HELLO/GET_CAPS qua driver C thành công, nhận 8 kênh và toàn bộ 8 sample rate.
- Capture cấu hình 1 MHz, 100000 mẫu: đủ mẫu, CRC frame/capture hợp lệ, lưu `build/hardware-test/pico-1m-100000.sr`.
- Capture cấu hình 25 MHz, 262144 mẫu: đủ mẫu, CRC frame/capture hợp lệ, lưu `build/hardware-test/pico-25m-262144.sr`.
- Kiểm tra archive `.sr`: metadata đúng rate, unitsize=1 và CH0…CH7; số byte logic khớp số mẫu.
- Cả hai capture đều chứa toàn byte `0x00`. Chưa có bằng chứng về nhận cạnh, bit mapping ngoài mạch hoặc độ chính xác timing khi đo tín hiệu.

Kết quả này cập nhật trạng thái bị chặn ở các mục trước: quyền serial, nạp UF2, handshake và đường truyền capture từ hardware đã hoạt động. Mạch bảo vệ, dạng sóng tín hiệu ngoài và timing cần kiểm tra riêng với nguồn tín hiệu đã biết; không suy ra độ chính xác 25 MHz chỉ từ metadata/CRC.

PulseView đã được mở trên desktop và kết nối trực tiếp Pico thật qua `/dev/ttyACM0`. Đã kích hoạt hai lần Run bằng Space; cả hai lần driver xác minh đủ 100000 mẫu @1 MHz và CRC hợp lệ. Log: `build/hardware-test/pulseview-hardware.log`. Giao diện được để mở cho người dùng tiếp tục thử tín hiệu.
