# RP2350 Logic Analyzer — PulseView custom

Bộ mã cho **đề tài 1**, Pico 2 / Pico 2 W (RP2350), Ubuntu Linux:

- Firmware PIO + DMA lấy đồng thời GP0–GP7, lưu RAM rồi chuyển qua USB CDC.
- Driver C `rp2350-la` tích hợp vào libsigrok, nhận capability từ firmware.
- PulseView build riêng, tiêu đề `RP2350 Logic Analyzer - PulseView`, launcher chọn thẳng thiết bị.
- Giữ waveform, zoom, cursor, lưu `.sr` và các decoder UART/SPI/I²C của PulseView.
- Giao thức LAR2 v1 có sequence, CRC từng frame, metadata và CRC toàn capture.
- Giả lập thiết bị bằng PTY, kiểm thử trực tiếp driver C mà không cần board.

Đề tài 1 chỉ yêu cầu ≥2 kênh và ≥1 kHz. Bản này có 8 kênh; các mức cấu hình là **1 kHz, 10 kHz, 100 kHz, 250 kHz, 500 kHz, 1 MHz, 5 MHz, 25 MHz**. Mức mặc định của driver là 1 MHz. Độ sâu 1–262.144 mẫu, mỗi mẫu 1 byte.

Firmware v1.1 đã có thêm **trigger cạnh, trigger mẫu bit và pre-trigger** (xem "Trigger trên firmware" bên dưới). Driver `rp2350-la` vẫn dùng profile manual-only nên chưa expose trigger trong PulseView; host tool `tools/host_la.py` dùng được ngay.

## Tiến độ thực hiện — cập nhật 22/10/2026

**Đã chạy được toàn bộ đường kết nối trên Pico 2 W thật: PIO/DMA → RAM → USB CDC → driver `rp2350-la` → PulseView.** Đã nạp firmware, nhận diện thiết bị, capture bằng CLI, lưu file `.sr` và capture trực tiếp trong giao diện PulseView. Bước tiếp theo là đưa tín hiệu có dạng sóng đã biết vào GPIO để kiểm chứng mức logic, mapping kênh và timing.

**Đợt này bổ sung trigger cho firmware** (edge, pattern, pre-trigger) theo hợp đồng LAR2 1.1, kèm host tool để chạy được không cần PulseView và bộ test host cho logic trigger. Phần trigger **chưa được kiểm chứng trên board thật**, cũng như driver chưa expose trigger trong PulseView.

### Những phần đã triển khai

| Phần | Công việc đã hoàn thành |
|---|---|
| Firmware RP2350 | Viết firmware Pico SDK cho GP0–GP7, lấy mẫu đồng thời bằng PIO, DMA chuyển vào RAM, truyền capture qua USB CDC; có cấu hình rate/số mẫu, ARM, ABORT và kiểm tra FIFO stall/DMA error. |
| Firmware trigger (v1.1) | Trigger cạnh, trigger mẫu bit có mặt nạ và pre-trigger. Vùng capture tuyến tính nên không rớt mẫu; CPU quét vùng capture theo thời gian thực, cửa sổ gửi đi liên tục và `trigger_index` bằng đúng số mẫu pre-trigger. CONFIG 28 byte mở rộng, CONFIG 20 byte cũ vẫn chạy. |
| Giao thức LAR2 v1 | Chốt định dạng binary và các lệnh; triển khai codec C dùng chung firmware/driver, sequence, CRC frame, metadata và CRC toàn capture. |
| Driver libsigrok | Viết driver C `rp2350-la`, đăng ký vào hệ thống build; nhận capability từ board, tạo CH0–CH7, cấu hình capture và đưa dữ liệu vào session libsigrok. |
| PulseView custom | Build PulseView với libsigrok custom và libsigrokdecode; sửa tiêu đề cửa sổ, tạo launcher chọn Pico. Waveform, cursor và decoder dùng chức năng có sẵn của PulseView/sigrok. |
| Công cụ sử dụng | Tạo script tải nguồn đúng commit, tích hợp driver, build, kiểm tra quyền serial, mở PulseView/CLI và lưu capture chỉ khi đủ mẫu, đúng CRC. |
| Kiểm thử | Viết emulator PTY và codec Python độc lập; kiểm thử driver C với dữ liệu chia nhỏ, cấu hình sai, CRC sai, mất frame, metadata sai và truyền thiếu dữ liệu. Thêm test C cho logic trigger (đối chiếu với tham chiếu naive, đo throughput) và test host cho đường trigger qua `host_la.py`. |

Mã nguồn, script và tài liệu nằm trong thư mục này. Binary host đã build ở `.local/`; firmware UF2 cho Pico 2 và Pico 2 W nằm trong `build/firmware-pico2/` và `build/firmware-pico2_w/`. **Chỉ Pico 2 W đã được nạp và thử trên board thật; bản Pico 2 mới được kiểm tra biên dịch.**

### Đã chạy và kiểm tra tới đâu

| Phép kiểm tra | Kết quả thực tế |
|---|---|
| Kiểm thử tự động trên host | **7/7 bài integration test đạt**; bộ kiểm codec C với ASan/UBSan cũng đạt. Đã kiểm cả 8 rate bằng emulator, capture tối đa 262.144 mẫu, số mẫu lẻ, tắt kênh và các ca lỗi truyền. |
| UART bằng dữ liệu giả lập | libsigrokdecode giải mã được pattern UART 115.200 baud, 8N1. Chưa phải phép thử UART lấy từ chân GPIO thật. |
| Nạp firmware lên Pico 2 W | Đã chuyển firmware cũ đang quét Wi-Fi vào BOOTSEL qua USB CDC 1200 baud/hạ DTR, rồi chép `rp2350_la.uf2`. Board khởi động lại với USB ID `2e8a:0009`. |
| Nhận diện board thật | Driver mở được `/dev/ttyACM0`, HELLO/GET_CAPS thành công, nhận CH0–CH7, 8 rate hỗ trợ và buffer tối đa 262.144 mẫu. Tài khoản đã có quyền `dialout`. |
| Capture thật bằng CLI ở cấu hình 1 MHz | Nhận đủ **100.000 mẫu**, metadata đúng, CRC frame và CRC toàn capture hợp lệ; đã lưu `.sr`. |
| Capture thật bằng CLI ở cấu hình 25 MHz | Nhận đủ **262.144 mẫu**, metadata đúng, CRC frame và CRC toàn capture hợp lệ; đã lưu `.sr`. |
| Capture thật trong PulseView | Đã chạy **hai capture liên tiếp**, mỗi lần 100.000 mẫu ở cấu hình 1 MHz, đều được driver xác minh. PulseView được để mở sau phép thử. |
| Dữ liệu GPIO đã nhận | Hai file capture CLI đều chứa toàn byte `0x00`, tức cả 8 kênh đọc mức 0 trong các lần thử này. Chưa ghi nhận dạng sóng có cạnh từ nguồn tín hiệu ngoài. |

Board đã thử có serial `907CBC57E4828DD6`. Tên cổng USB và trạng thái cửa sổ PulseView có thể thay đổi sau khi rút/cắm board hoặc khởi động lại máy.

Bằng chứng tại workspace hiện tại:

- [Capture thật 1 MHz / 100.000 mẫu](build/hardware-test/pico-1m-100000.sr) và [log xác minh](build/hardware-test/pico-1m-100000.sr.log).
- [Capture thật 25 MHz / 262.144 mẫu](build/hardware-test/pico-25m-262144.sr) và [log xác minh](build/hardware-test/pico-25m-262144.sr.log).
- [Log nhận diện board sau nạp](build/hardware-test/device-show-flashed.log), [log capture trong PulseView](build/hardware-test/pulseview-hardware.log) và [kết quả kiểm thử tự động](build/test-results.log).
- [VALIDATION.md](VALIDATION.md) ghi chi tiết quá trình kiểm tra. Các file trong `build/` là artifact tại máy này, được bỏ qua bởi Git; cần lưu riêng nếu dùng làm bằng chứng cho báo cáo.

### Những việc còn phải làm

1. Nối nguồn xung 3,3 V có tần số biết trước và chung GND; kiểm tra từng CH0–CH7 để xác nhận bắt được mức cao/thấp, cạnh và đúng vị trí bit.
2. Đo chu kỳ, độ rộng xung và độ lệch giữa các kênh; đối chiếu oscilloscope/LA tham chiếu ở các rate sử dụng. **Capture đủ mẫu và đúng CRC tại cấu hình 25 MHz chưa chứng minh độ chính xác timing 25 MHz trên mạch thật.**
3. Capture và decode UART từ GPIO thật, sau đó thử SPI/I²C. Hiện mới kiểm thử UART bằng dữ liệu giả lập; chưa xác minh decoder với các bus thật của hệ thống.
4. Thiết kế, mô phỏng và kiểm chứng mạch đệm/bảo vệ đầu vào theo đề bài; ghi phạm vi bảo vệ và ảnh hưởng tới dạng xung trong báo cáo. Phần mềm không thay thế hạng mục phần cứng này.
5. **Kiểm chứng trigger trên board thật**: đưa xung chuẩn vào CH0, đo `trigger index` bằng `tools/host_la.py` và đối chiếu với vị trí cạnh thật. Chưa có phép thử này.
6. Nếu mở rộng: expose trigger trong driver để PulseView dùng được, thêm pre-trigger vòng đệm để chờ trigger lâu hơn, hoặc hỗ trợ Windows.

## Trigger trên firmware (v1.1)

Firmware chấp nhận CONFIG 28 byte thay vì 20 byte để bật trigger. Host cũ gửi CONFIG 20
byte vẫn chạy capture manual như cũ, nên driver `rp2350-la` không phải sửa gì.

- **Trigger cạnh**: sườn lên hoặc sườn xuống trên một kênh.
- **Trigger mẫu bit**: `(sample & mask) == value`, có bit don't-care.
- **Pre-trigger**: số mẫu giữ lại trước thời điểm bắt, 0 ≤ pre < count. Cửa sổ gửi đi
  luôn dài đúng `count` mẫu và điểm bắt nằm ở chỉ số `pre` trong cửa sổ đó.

Cách chạy từ máy tính, không cần PulseView:

```bash
# capture manual (giống driver)
python3 tools/host_la.py --port /dev/ttyACM0 --rate 1m --count 100000 --out cap.bin

# bắt theo sườn lên của CH0, giữ lại 20.000 mẫu trước thời điểm bắt
python3 tools/host_la.py --port /dev/ttyACM0 --rate 1m --count 100000 --pre 20000 \
    --trigger edge:0:rise --out cap.bin

# bắt theo mẫu bit: giá trị 0x5a, mặt nạ 0x0f (bit cao không quan tâm)
python3 tools/host_la.py --port /dev/ttyACM0 --rate 1m --count 100000 --pre 20000 \
    --trigger pattern:0x5a:0x0f --out cap.bin
```

Lệnh in ra `trigger index`, `pre-trigger samples` và kiểm tra CRC toàn capture; file
`.bin` là dữ liệu thô một byte một mẫu, bit n là kênh n, đúng bố cục PulseView nhận.

Giới hạn cần nói trong báo cáo: firmware chỉ chờ trigger trong một vùng capture 262.144
mẫu, tức 10,49 ms ở 25 MHz hay 262 ms ở 1 MHz. Trigger không xuất hiện trong vùng này
thì thiết bị trả lỗi `no trigger`.

## Chạy trên máy hiện tại

Stack đã được build vào `.local/`; toolchain phục vụ build đang nằm trong `.deps/`. Dùng các script bên dưới để nạp đúng thư viện custom thay vì libsigrok của hệ điều hành.

**1. Nạp firmware bằng BOOTSEL**

**Pico 2 W trên máy này đã được nạp firmware và capture thành công; không cần nạp lại để tiếp tục dùng.** Khi cần nạp lại hoặc thiết lập board khác, giữ BOOTSEL khi cắm USB, chờ ổ `RP2350` xuất hiện rồi chép:

```text
build/firmware-pico2_w/rp2350_la.uf2
```

Nếu dùng Pico 2 không Wi-Fi, dùng `build/firmware-pico2/rp2350_la.uf2`. Không dùng cho Pico/RP2040. Board tự khởi động lại sau khi chép; firmware cũ trên flash được thay thế. Firmware này không cung cấp REPL MicroPython và không in log text vào CDC.

**2. Quyền cổng serial**

Máy hiện tại đã có quyền serial và đã nạp firmware. Khi thiết lập tài khoản/máy khác, thêm tài khoản vào nhóm `dialout` nếu chưa có quyền:

```bash
sudo usermod -aG dialout "$USER"
```

Đăng xuất rồi đăng nhập lại để cả terminal và ứng dụng nhận nhóm mới. Có thể mở terminal tạm bằng `newgrp dialout` sau khi thêm nhóm. Không chạy PulseView bằng root.

```bash
cd /media/abuntu/NVME_UBUNTU/Working/20261/Logic_Analyzer_20261/PulseView-custom
./scripts/doctor.sh
./scripts/run-sigrok-cli.sh -d rp2350-la:conn=/dev/ttyACM0 --show
./scripts/run-pulseview.sh /dev/ttyACM0
```

Cổng có thể đổi thành `/dev/ttyACM1` sau khi nạp. Ưu tiên đường dẫn thật trong `/dev/serial/by-id/` nếu có. Không mở terminal serial/Thonny đồng thời với PulseView.

**3. Capture trong PulseView**

Chọn 1 MHz và 100.000 samples ở thanh công cụ, bật các kênh cần xem, nhấn **Run** hoặc Space. Có thể thay đổi rate và số mẫu trong giới hạn firmware. Dùng **Zoom to fit**, cursor và **Add protocol decoder** để phân tích. UART: chọn RX tương ứng và baud của nguồn; SPI: map CLK/MOSI/MISO/CS và chọn mode; I²C: map SCL/SDA. Mức 25 MHz chỉ lấy được khoảng 10,49 ms với buffer tối đa.

Capture bằng CLI có kiểm chứng toàn vẹn:

```bash
./scripts/capture.sh /dev/ttyACM0 capture.sr 1m 100000
source scripts/env.sh
pulseview -i capture.sr
```

`capture.sh` chỉ xuất file cuối khi driver xác nhận đủ mẫu và đúng CRC; log nằm ở `capture.sr.log`. Với CLI gốc/PulseView, dữ liệu nhận trước lỗi có thể vẫn còn trong session: **không coi file đó là capture hợp lệ nếu log thiếu `Capture verified:`**. libsigrok không chuyển lỗi callback acquisition thành mã thoát lỗi của `sigrok-cli`; do đó không chỉ kiểm tra exit code.

## Đấu nối

| Kênh | GPIO | Chân vật lý Pico 2 / 2 W |
|---|---|---|
| CH0 | GP0 | 1 |
| CH1 | GP1 | 2 |
| CH2 | GP2 | 4 |
| CH3 | GP3 | 5 |
| CH4 | GP4 | 6 |
| CH5 | GP5 | 7 |
| CH6 | GP6 | 9 |
| CH7 | GP7 | 10 |
| GND | GND | 3 hoặc 8 |

Nối chung GND với nguồn tín hiệu. Các chân này là input, tắt pull-up/pull-down; kênh bỏ trống có thể nhiễu. **GPIO RP2350 chỉ nhận mức phù hợp 3,3 V; không đưa trực tiếp 5 V, 12 V hoặc xung cao áp vào board.** Mạch đệm/chuyển mức và bảo vệ đầu vào là phần cứng cần làm riêng. Mã phần mềm này không đáp ứng thay cho yêu cầu bảo vệ 12 V liên tục / xung 200 V trong đề bài.

Kiểm chứng lần đầu: dùng nguồn vuông 1 kHz mức 3,3 V trên CH0. Ở 1 MHz phải được khoảng 1.000 mẫu/chu kỳ và 500 mẫu/nửa chu kỳ. Cursor cách 25 mẫu ở 25 MHz phải hiện 1 µs. Sau đó thử UART 115.200 baud tại 1 MHz hoặc cao hơn, rồi SPI/I²C. Đo đối chiếu với oscilloscope/LA tham chiếu, ghi cả giới hạn tín hiệu, trễ buffer bảo vệ và sai số clock trong báo cáo.

Firmware đặt `clk_sys=100 MHz`; mỗi mẫu gồm 4 chu kỳ PIO. Các rate công bố đều dùng divider nguyên. PIO dừng sau số mẫu làm tròn lên bội 4 để DMA nhận đủ word; chỉ truyền đúng số mẫu được yêu cầu. Có kiểm tra FIFO stall/DMA error. Đã capture trên board thật tại cấu hình 25 MHz với đủ mẫu và đúng CRC; **độ chính xác timing với tín hiệu ngoài chưa được đo kiểm**.

## Demo không cần phần cứng

Terminal A:

```bash
python3 tools/emulator.py --pattern square
```

Công cụ in cổng `/dev/pts/N`. Terminal B:

```bash
./scripts/run-pulseview.sh /dev/pts/N
```

Chọn 25 MHz: CH0 đổi mức sau mỗi 25 mẫu (1 µs), chu kỳ 2 µs. Các kênh sau chia đôi tần số lần lượt. Thử `--pattern uart`, chọn 1 MHz rồi UART decoder CH0, 115200, 8N1: chuỗi `RP2350 OK\r\n`. Đây là dữ liệu giả lập, không phải bằng chứng GPIO đã capture.

```bash
./scripts/test.sh
```

Kết quả đã kiểm tra được ghi ở [VALIDATION.md](VALIDATION.md).

## Build lại / cài trên máy Ubuntu khác

```bash
./scripts/install-deps-ubuntu.sh    # cần quyền sudo khi cài gói trên máy mới
./scripts/build.sh host
PICO_BOARD=pico2_w ./scripts/build.sh firmware
PICO_BOARD=pico2 ./scripts/build.sh firmware
./scripts/test.sh
```

`JOBS=4` mặc định; giảm `JOBS=2` nếu máy ít RAM. Nguồn upstream được tải vào `third_party/` đúng commit trong [sources.lock.json](sources.lock.json). Script giữ thay đổi có sẵn và dừng nếu HEAD không đúng commit. Build cài vào `.local/`, không thay thư viện hệ thống. Log nằm ở `build/logs/`. `.deps/` là bộ công cụ đã giải nén riêng cho workspace hiện tại; nếu di chuyển dự án sang đường dẫn khác, dùng các gói hệ thống và build lại, không sao chép nguyên bộ toolchain đã relocate.

Driver hiện dùng POSIX `termios/poll` cho CDC trên Linux (PTY test cũng dùng đường này). Chưa hỗ trợ Windows/COM hoặc USB vendor class. Muốn port Windows cần thay transport bằng libserialport và kiểm thử lại. Không cần đăng ký VID/PID cho driver CDC; người dùng chỉ định `conn`.

## Bố cục và nguồn

- `driver/`: mã driver được duy trì; `scripts/integrate.py` chép vào libsigrok và thêm build registration.
- `protocol/lar2.h`, [protocol/PROTOCOL.md](protocol/PROTOCOL.md): codec C và hợp đồng binary.
- `protocol/trigger_match.h`: logic so khớp trigger, dùng chung cho firmware và test host.
- `firmware/`: Pico SDK, PIO, DMA, TinyUSB CDC; cùng firmware dùng cho Pico 2/2 W.
- `patches/`: thay đổi tiêu đề PulseView; giao tiếp phần cứng luôn đi qua libsigrok.
- `tools/`, `tests/`: codec Python độc lập, giả lập, host tool `host_la.py` và kiểm thử driver.
- `scripts/`: build, launch, kiểm tra quyền và capture đã xác minh.

Mã tùy chỉnh: GPL-3.0-or-later, xem [LICENSE](LICENSE). PulseView upstream theo GPL-2.0-or-later; giữ thông tin tác giả và giấy phép của từng dự án khi phân phối. Nguồn chính thức: [libsigrok](https://github.com/sigrokproject/libsigrok), [PulseView](https://github.com/sigrokproject/pulseview), [libsigrokdecode](https://github.com/sigrokproject/libsigrokdecode), [Pico SDK](https://github.com/raspberrypi/pico-sdk). Phiên bản đã pin và phạm vi thay đổi xem [THIRD_PARTY.md](THIRD_PARTY.md).
