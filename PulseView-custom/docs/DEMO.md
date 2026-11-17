# Demo Pico 2 + ESP32: một lệnh

Từ terminal trên **máy tính đang cắm hai board**, chạy:

```bash
cd /home/namkhanh/EmbeddedSystem/Code/Logic_Analyzer_20261/PulseView-custom
bash scripts/demo.sh
```

Cần ESP32 đã chạy firmware signal generator của dự án, Pico 2 chạy LAR2, tám dây tín hiệu đúng bảng dưới và chung GND. Đóng PulseView/serial monitor đang giữ hai cổng trước khi chạy lại; script không tự đóng ứng dụng của bạn. Lần đầu script có thể build phần hỗ trợ demo và nạp firmware Pico 2 mới nếu ID chưa có `+ui18+sync`. Những lần sau không tự flash lại nếu đã có hỗ trợ. Quá trình flash vẫn cần bootloader mount được như `tools/flash_pico.py` hiện có; nếu không mount được, xem `flash.log`.

| Kênh Pico / GP | GPIO ESP32 | Tín hiệu |
|---|---|---|
| CH0 / GP0 | 26 | UART TX |
| CH1 / GP1 | 18 | I2C SCL |
| CH2 / GP2 | 19 | I2C SDA |
| CH3 / GP3 | 21 | SPI CS |
| CH4 / GP4 | 22 | SPI CLK |
| CH5 / GP5 | 23 | SPI MOSI |
| CH6 / GP6 | 25 | SPI MISO; jumper ESP32 GPIO23–25 nếu demo loopback |
| CH7 / GP7 | 32 | CAN logic TX |

## Những gì script thực sự làm

1. Kiểm tra cổng, HELLO, 8 kênh và hỗ trợ 25 MHz. Nạp firmware hỗ trợ đưa capture thật lên TFT nếu cần. Thiết lập ESP32 về UART 115200, I2C 100 kHz, SPI 1 MHz, CAN 250 kbps, burst on.
2. Chạy `sigrok-cli --show`, lưu cấu hình thiết bị. Thu qua driver C `rp2350-la` ở 25 MHz, 65536 mẫu, pre-trigger 50%, cạnh xuống; thử tối đa 6 lần với các kênh trigger nếu cửa sổ không chứa đủ bốn giao thức.
3. Giải mã file thật bằng bốn decoder PC, kiểm UART `LA1234`, I2C địa chỉ 0x68, SPI `DE AD BE EF 00 01 02 03`, CAN ID 0x123. Không dùng dữ liệu demo dựng sẵn để thay thế khi thu lỗi.
4. Đưa buffer **chính lần thu này** lên TFT: hai trang sóng CH0–3/CH4–7 rồi bốn protocol. Đọc lại screen ID, badge, text và bảng byte thực sự dùng để vẽ. Kiểm nội dung từng bus; lưu `tft.json`. Mỗi trang dừng 2 giây. Cuối cùng TFT ở UART, các dòng byte đầu là `4C 41 31 32 33` (`LA123`); DOWN cuộn tới `34` và ký tự xuống dòng.
5. Nhả cổng, mở PulseView với driver thật, tự đặt 25 MHz/65536 mẫu, trigger source được chọn ở bước 2, slope falling, pre-trigger 50%. Tự gán bốn decoder: UART RX CH0/115200; I2C CH1/2; SPI CLK CH4, MISO CH6, MOSI CH5, CS CH3; CAN CH7/250000.
6. PulseView tự thực hiện một capture mới. Phiên bản custom ghi JSON chứa annotation sinh **bên trong GUI**, trạng thái 8 kênh, cấu hình driver và ảnh cửa sổ. Script chỉ báo `PASS` khi cả bốn annotation cần thiết đang visible và chứa dữ liệu đúng, cấu hình đọc lại đúng. Có thể thử thu lại tối đa 4 lần bằng tín hiệu Run của PulseView khi lần trước đã dừng.

Khi thành công, PulseView được giữ mở. Nhấn **Space/Run** để thu lại, **F** để fit toàn bộ, lăn chuột để zoom. Sau mỗi snapshot từ PC, firmware tự đồng bộ cửa sổ, vị trí trigger và bảng packet trên TFT. Khi nhấn Run thu lại trong PulseView, TFT đang ở DECODE sẽ giải mã lại capture mới; không dùng bảng byte cũ với RAM vừa bị ghi lại. Đóng PulseView trước khi chạy script demo lần nữa để tránh hai chương trình cùng mở cổng.

`PASS` chứng minh trạng thái và nội dung số đã kiểm. Việc màu panel, backlight, dây nút vật lý hoặc chất lượng hình ảnh ngoài đời cần quan sát trên TFT; script không có camera để tự xác nhận chúng.

## Kết quả lưu ở đâu?

Mỗi lần chạy tạo một thư mục mới `build/demo/YYYYMMDD-HHMMSS/`:

- `summary.json`: PASS/FAIL; GUI live, recorded hay chưa chạy; lỗi cụ thể nếu có.
- `device-show.log`, `esp32-console.log`: nhận diện board và cấu hình bộ phát.
- `capture.sr`: snapshot thật đã qua kiểm tra bốn decoder PC; các log `capture-N-*` giữ cả những lần cần thử lại.
- `tft.json`: text, bảng byte, badge của bốn bus trên TFT.
- `live.pvs`: cấu hình bốn decoder và live capture.
- `pulseview.json`: annotation do chính PulseView tạo và cấu hình đọc lại.
- `pulseview.json.png`: ảnh cửa sổ PulseView chụp bằng Qt; `pulseview.log`: log tiến trình GUI.

Nếu FAIL, script trả mã thoát 1 và giữ log để kiểm tra. Không tiếp tục trình bày là demo đã đạt. Nếu GUI đã mở nhưng kiểm tra timeout, cửa sổ được giữ lại để xem nguyên nhân.

## Tuỳ chọn cần thiết

```bash
# Cổng khác mặc định:
bash scripts/demo.sh --port /dev/ttyACM1 --esp32-port /dev/ttyUSB1

# Hiển thị chính snapshot đã kiểm trên PC; vẫn là dữ liệu thật vừa thu,
# PulseView mở file này và giải mã trong GUI, không thực hiện live acquisition:
bash scripts/demo.sh --recorded

# Chỉ kiểm phần cứng + TFT, không mở GUI; kết quả là PASS_HARDWARE_ONLY:
bash scripts/demo.sh --no-gui

# Không cho tự flash firmware cũ:
bash scripts/demo.sh --no-flash

# Dừng lâu hơn trên mỗi trang TFT:
bash scripts/demo.sh --dwell 5
```

Có thể build trước buổi demo để không mất thời gian:

```bash
bash scripts/build-demo.sh
```

Nếu thiếu hẳn build tree hoặc host stack, cần dựng môi trường dự án bằng `scripts/build.sh host` và build firmware theo README trước; build-demo chỉ build tăng dần từ cây đã cấu hình. Script dùng trực tiếp binary `build/pulseview/pulseview` có patch demo, không phụ thuộc một bản PulseView khác trên PATH.

## Thao tác TFT sau demo và các sửa UX

- CONFIG: OK ngắn sửa giá trị, UP/DOWN thay đổi, OK lưu; giữ OK khi đang duyệt để thu.
- Chỉ cung cấp 16K/64K/131K mẫu và pre-trigger 0/25/50%, đúng giới hạn trigger thực; mặc định 64K/50%/CH0 falling.
- WAVE: OK đi từ CH0–3 tới CH4–7 rồi DECODE; BACK quay lại. Trang sóng do script mở ở FIT để thấy toàn cửa sổ.
- DECODE: OK vào sửa PROTO, DOWN chuyển AUTO/UART/I2C/SPI/CAN, OK áp dụng; khi đang duyệt, UP/DOWN chọn dòng và preview theo byte. Giữ UP/DOWN đổi focus PROTO/baud. Giữ OK về STANDBY.
- Nhãn tốc độ cực đại trên STANDBY đã sửa từ 100 MSPS thành **25 MSPS**. `100MHz` là clock hệ thống, không phải rate lấy mẫu.
- Caption `T:`/`ST:` tính theo trigger thực được trả về, không dùng pre-trigger yêu cầu khi cửa sổ đã dịch.

## Kết quả kiểm chứng trên máy thật — 17–18/11/2026

Sau khi phiên được chuyển sang `danger-full-access`, đã truy cập được cả hai cổng và desktop X11. Đã nạp firmware `RP2350-LA/1.2+trg+an+str+ui18+sync` lên Pico 2 bằng script, không cần BOOTSEL thủ công.

- `bash scripts/test.sh`: toàn bộ PASS, bao gồm driver integration 9/9, demo 8/8 và 2 ca kiểm tra retry cửa sổ ESP32.
- `python3 tests/test_esp32_link.py`: 4 snapshot + 4 streaming PASS. Có một lần chạy trước gặp CAN trigger muộn làm cắt frame; giữ log lỗi và bổ sung retry hữu hạn chỉ khi số mẫu sau trigger không đủ. Decode sai trong cửa sổ đủ dài vẫn báo FAIL.
- `bash scripts/demo.sh`: đã chạy thật thành công, thu qua driver, đọc lại text/bảng byte TFT đúng UART/I2C/SPI/CAN, rồi PulseView tự capture và tạo đủ bốn annotation. Có kiểm trực tiếp cả cấu hình driver và giá trị ô rate/count trong toolbar.
- Đã xem ảnh GUI thật, thấy đủ 8 kênh và bốn hàng decoder. Cửa sổ demo rộng hơn, chiều cao track gọn hơn, ban đầu zoom vào vùng có bus để đọc dễ hơn.
- Đã sửa lỗi ô count làm tròn 65536 thành 50k; giá trị và chữ hiển thị nay đúng `65.536 k samples`. Nút Run tiếp theo giữ đúng số mẫu đó.

Bằng chứng chạy thật nằm trong [real-run](demo-validation-2026-11-17/real-run/README.md). Các log lỗi sandbox ở thư mục cha được giữ làm lịch sử, không phải trạng thái kiểm thử hiện tại.

TFT được kiểm qua dữ liệu thực dùng để vẽ, screen ID và bảng byte. Chưa có ảnh camera của panel hoặc phép thử tiếp điểm bốn nút vật lý; kết quả này không chứng nhận màu/backlight hay nút đã hàn. Các yêu cầu bảo vệ đầu vào và BOM vẫn cần nghiệm thu riêng.
