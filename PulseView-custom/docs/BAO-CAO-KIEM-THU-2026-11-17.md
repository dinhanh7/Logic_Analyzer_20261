# BÁO CÁO KIỂM THỬ VÀ ĐÁNH GIÁ MỨC ĐỘ HOÀN THIỆN

**Sản phẩm:** Bộ phân tích logic 8 kênh sử dụng RP2350 — Topic 2
**Mốc tổng hợp:** 17/11/2026
**Phiên bản mã nguồn được đánh giá:** `576873a`, nhánh `feature/trigger-v2`
**Phạm vi:** phần mềm, kết nối máy tính, thu tín hiệu, trigger, giải mã, streaming, giao diện và khoảng trống nghiệm thu.

**Cách đọc bản cập nhật:** mục 1–11 ghi mốc `576873a` và lần chạy ban đầu; mục 12 cập nhật kiểm chứng board/GUI thật sau khi mở quyền, được ưu tiên khi đánh giá trạng thái hiện tại.

**Lịch sử đính chính, trước khi mở quyền chạy thật:** hai nhiệm vụ xác minh kết nối GUI PulseView và decode trực tiếp trong GUI chưa được coi là đã thực hiện/nghiệm thu. Những câu “đã quan sát GUI” bên dưới là nội dung bàn giao cũ, không phải kết quả xác minh độc lập của agent. Theo yêu cầu mới, cả hai phải chạy lại trên máy thật. Script và thay đổi hỗ trợ nằm ở `docs/DEMO.md`; hiện chưa có PASS phần cứng/GUI mới cho thay đổi đó.

## 1. Kết luận chính cho người đọc

Sản phẩm đã hoạt động như một bộ phân tích logic mẫu thử có thể sử dụng để thu và đọc tín hiệu số. Pico 2 thu đồng thời 8 kênh, truyền dữ liệu đến máy tính, lưu file và giải mã được UART, I2C, SPI, CAN. Chế độ chụp vào bộ nhớ đã được thử ở mức lấy mẫu 25 triệu mẫu/giây. Chế độ truyền liên tục đã có kết quả 1 triệu mẫu/giây khi nén RLE với nguồn tín hiệu kiểm thử, không ghi nhận mất mẫu trong lần thử được bàn giao.

Phần lõi thu nhận và giải mã có bằng chứng kiểm thử khá tốt: có kiểm tra thuật toán, dữ liệu giả lập độc lập, driver C thực, tín hiệu ESP32 trên phần cứng và các lần lặp tại một số điểm biên. Tuy nhiên, sản phẩm **chưa đủ bằng chứng để kết luận hoàn thành toàn bộ yêu cầu đề tài**. Các điểm còn thiếu quan trọng là kiểm chứng bảo vệ 12 V DC/xung 200 V, nút bấm và hình ảnh TFT thực tế, cùng bộ hồ sơ bàn giao. Luồng PulseView với đầy đủ bốn decoder đã được chạy trực tiếp và lưu ảnh/annotation trong đợt cập nhật mục 12.

Không quy đổi mức hoàn thiện thành một phần trăm chung: một tính năng chưa nghiệm thu như bảo vệ đầu vào không thể được bù bằng số lượng lớn test phần mềm đã đạt. Báo cáo đánh giá riêng từng nhóm chức năng tại mục 10.

### 1.1. Cách hiểu các mức kết quả

| Mức | Ý nghĩa trong báo cáo |
|---|---|
| Đạt theo hồ sơ | Tài liệu hoặc bàn giao ghi đã chạy thành công; chưa chắc có raw log/ảnh đầy đủ được lưu lại |
| Đạt khi chạy lại | Có log của chính phiên tổng hợp báo cáo này |
| Đạt một phần | Một số bước đã được xác minh, các bước còn lại chưa có bằng chứng |
| Giới hạn quan sát được | Kết quả chỉ đúng trong cấu hình, nguồn phát, số lần và thời lượng đã ghi |
| Chưa nghiệm thu | Thiếu phép thử trực tiếp hoặc thiếu bằng chứng cần thiết |
| Không chạy được trong phiên này | Hạ tầng hiện tại ngăn phép thử; không thay thế kết quả bàn giao và cũng không được tính là PASS mới |

**Nguồn dữ liệu:** mã nguồn tại `576873a`; hồ sơ lịch sử đã sao chép vào [thư mục evidence](test-evidence-2026-11-17/README.md); nội dung bàn giao của người dùng ngày 17/11; log chạy lại ở mục 9. Bản sao tài liệu lịch sử giữ nguyên nội dung, bao gồm các mô tả cũ đã được hiệu chỉnh trong báo cáo này. Không phải mọi kết quả lịch sử đều có raw log hoặc ảnh đi kèm.

## 2. Sản phẩm làm gì và đã được thử như thế nào?

Bộ phân tích logic ghi lại mức 0/1 trên nhiều dây theo thời gian. Từ các mức này, phần mềm vẽ dạng sóng và đọc ra byte dữ liệu. Thiết bị không đo điện áp liên tục như oscilloscope; kết quả ở đây là trạng thái logic và thời điểm chuyển trạng thái.

Luồng kiểm thử phần cứng: **ESP32 phát tín hiệu → 8 đầu vào Pico 2 → PIO/DMA ghi RAM → USB LAR2 → host Python hoặc driver libsigrok → file dữ liệu, decoder và PulseView**. Màn TFT cung cấp thao tác và xem kết quả tại thiết bị.

- **Snapshot:** thu một cửa sổ vào RAM rồi đọc ra. Phù hợp để xem chi tiết tín hiệu nhanh; mức lấy mẫu cao nhất đang hỗ trợ là 25 MHz.
- **Trigger:** chờ một điều kiện, ví dụ CH7 chuyển từ 1 xuống 0, để đặt sự kiện cần xem vào cửa sổ thu. Pre-trigger là phần dữ liệu trước sự kiện.
- **Streaming:** liên tục gửi các khối dữ liệu sang máy tính. Đường truyền USB giới hạn chế độ này ở mức cấu hình tối đa 1 MHz; tốc độ thực tế còn phụ thuộc dữ liệu và nén.
- **RLE:** các mức logic lặp lại được biểu diễn ngắn hơn. Bus có nhiều khoảng nghỉ nén tốt hơn tín hiệu thay đổi liên tục.

### 2.1. Môi trường và sơ đồ 8 kênh đang sử dụng

| Thành phần | Cấu hình được dùng/ghi nhận |
|---|---|
| Bộ thu | Raspberry Pi Pico 2, RP2350; firmware Pico C/C++ SDK |
| Bộ phát | ESP32 DevKitC; console 115200 baud |
| Cổng bộ thu / bộ phát | `/dev/ttyACM0` / `/dev/ttyUSB0` trong các lần test phần cứng đã bàn giao |
| Máy tính | Host Python, libsigrok, libsigrokdecode, sigrok-cli, PulseView custom |
| Driver / định danh firmware | `rp2350-la` / `RP2350-LA/1.2+trg+an+str` |
| Màn hình hiện tại | ST7735 128×160 portrait, 6 màn; các tài liệu cũ có nhắc ILI9488 hoặc UI 4 màn |

| Kênh Pico | GPIO ESP32 | Vai trò |
|---|---|---|
| CH0 / GP0 | 26 | UART TX |
| CH1 / GP1 | 18 | I2C SCL |
| CH2 / GP2 | 19 | I2C SDA |
| CH3 / GP3 | 21 | SPI CS |
| CH4 / GP4 | 22 | SPI SCK |
| CH5 / GP5 | 23 | SPI MOSI |
| CH6 / GP6 | 25 | SPI MISO, dùng jumper MOSI–MISO trong bài loopback |
| CH7 / GP7 | 32 | CAN TX logic từ TWAI |

Các board dùng chung GND. Sơ đồ này phục vụ test mức logic từ ESP32. Phép thử CAN dùng TX logic/loopback, chưa chứng minh đo trực tiếp cặp CAN_H/CAN_L qua transceiver hoặc khả năng chịu nhiễu của bus vật lý. Các bài cũ sử dụng CH0/CH1 cho từng bus riêng phải được đọc theo sơ đồ của chính bài đó.

### 2.2. Các con số quan trọng, giải thích bằng ví dụ

Ở 25 MHz, hai mẫu cách nhau 40 ns. 65.536 mẫu tạo cửa sổ khoảng 2,621 ms; pre-trigger 32.768 mẫu dành khoảng 1,311 ms trước sự kiện. Với UART 115200 baud, mỗi bit có khoảng 217 mẫu, thuận lợi để giải mã.

25 MHz là **tần số lấy mẫu**, không phải tốc độ bus tối đa được bảo đảm. Ví dụ SPI 1 MHz chỉ có một mẫu mỗi chu kỳ nếu streaming ở 1 MHz, nên có thể bỏ mất cạnh xung. Vì vậy bài streaming SPI hạ clock bộ phát xuống 100 kHz. Tương tự, CAN 250 kbps ở 1 MHz chỉ có 4 mẫu/bit, ít hơn nhiều so với 100 mẫu/bit khi chụp ở 25 MHz.

Bộ nhớ chứa tối đa 262.144 mẫu khi thu không trigger, tương đương 10,486 ms ở 25 MHz hoặc 262,144 ms ở 1 MHz. Với trigger, firmware hiện giới hạn số mẫu về tối đa 131.072 và pre-trigger về tối đa một nửa số mẫu để còn cửa sổ hợp lệ. Do đó giao diện báo dung lượng tối đa 262.144 không có nghĩa mọi cấu hình trigger cũng trả về được chừng ấy mẫu.

## 3. Kiểm thử phần mềm tự động

Các bài dưới đây đã được báo PASS trong lần bàn giao. Phiên tổng hợp có chạy lại và ghi trạng thái riêng ở mục 9. Test giả lập giúp kiểm tra lỗi có kiểm soát, nhưng không thay cho phép đo chất lượng điện, clock hoặc DMA trên board.

### 3.1. Đóng gói và toàn vẹn dữ liệu — `tests/test_protocol.c`

**Mục đích:** đảm bảo hai đầu hiểu cùng một gói LAR2, nhận đúng nội dung và từ chối gói hỏng.

**Đã kiểm:** CRC32 với chuỗi chuẩn `123456789`; tất cả độ dài payload 0–1024 byte; sequence 65535; đưa từng byte vào parser; so lại payload; cố tình sửa byte; từ chối payload khai báo 1025 byte. Test C được biên dịch với AddressSanitizer và UndefinedBehaviorSanitizer trong script kiểm thử.

**Đánh giá:** đã có kiểm tra biên độ dài và dữ liệu hỏng, tốt hơn việc chỉ gửi một gói hợp lệ. CRC hợp lệ cho biết không phát hiện lỗi trong đường dữ liệu được CRC bao phủ; không tự chứng minh GPIO đã được lấy mẫu đúng thời điểm hay không mất sự kiện trước khi ghi RAM.

### 3.2. Tìm điều kiện trigger — `tests/test_trigger.c`

**Đã kiểm:** 20.000 trường hợp pattern và 20.000 trường hợp cạnh với dữ liệu ngẫu nhiên, đối chiếu thuật toán tối ưu với bản tham chiếu đơn giản; các kênh CH0–CH7, hai chiều cạnh, mặt nạ don't-care, vị trí biên nhóm 4 byte, cạnh tại đầu vùng quét và các offset khác nhau. Có kiểm throughput với ngưỡng lớn hơn 25 MB/s trên máy chạy test.

**Kết quả:** khớp thuật toán tham chiếu theo hồ sơ; phiên chạy lại đo 494,2 MB/s trên host.

**Đánh giá:** độ đúng logic tìm mẫu được bao phủ tốt. Con số throughput host không phải benchmark RP2350 và không được dùng một mình để kết luận CPU trên board luôn theo kịp DMA.

### 3.3. Giải mã 4 giao thức — `tests/test_decode.c`

**Đã kiểm:** UART biết baud/tự dò baud trong đường API tương ứng; I2C write/read và ACK/NAK; SPI; CAN standard ID `0x123`, DLC và payload; chọn protocol; giới hạn chuỗi text; giải mã đồng thời theo map 8 kênh; danh sách packet, thứ tự byte và vị trí mẫu; trường hợp không có dữ liệu.

**Kết quả mẫu:** UART `LA1234`; I2C `0x68 W ACK 0x01 ACK 0x5A ACK`; SPI `DE AD BE EF`; CAN `ID=0x123 DLC=4 DATA=DE AD BE EF`.

**Đánh giá:** đã đối chiếu cả text và packet để màn decode không chỉ có chuỗi đúng mà còn có dữ liệu có cấu trúc. Một số waveform CAN tổng hợp dùng CRC placeholder, nên bộ test này không chứng minh kiểm tra đầy đủ CAN CRC, error frame, extended frame hay mọi biến thể giao thức.

**Điều kiện sử dụng quan trọng:** sơ đồ 8 kênh phải dùng `decode_all()` / `ANALYZE_DECODE_ALL` hoặc `decoders_run_map()` với map phù hợp. `decode()` / `decoders_run()` cũ dùng map một bus cố định; kết quả sai khi áp dụng map cũ cho dây 8 kênh không được quy thành lỗi phần cứng.

### 3.4. AUTO nhận dạng giao thức — `tests/test_autodetect.c`

**Đã kiểm:** UART chuyển sang CH5; I2C sang CH6/7; SPI sang CH1/2/3; CAN sang CH4; bốn bus cùng có trong dữ liệu. Test không chỉ kiểm tên protocol mà còn kiểm đúng map kênh. Khi nhiều bus cùng tồn tại, AUTO chỉ cần chọn một bus có thật với map đúng, không có cam kết liệt kê đồng thời cả bốn.

**Các bài loại trừ:** mọi kênh đứng yên, sóng vuông thuần, nhiễu giả ngẫu nhiên, một xung lẻ, cửa sổ quá ngắn; kiểm không nhầm UART với CAN hoặc I2C với SPI.

**Đánh giá:** có bao phủ tình huống dễ nhận nhầm, không chỉ waveform đẹp. UI hiện quét vùng 32.768 mẫu quanh trigger trước rồi mới fallback đầu buffer; selftest render kiểm tra lựa chọn cửa sổ này. Chưa có thống kê nhận dạng trên tập tín hiệu nhiễu thật hoặc hàng trăm lần thu ngẫu nhiên.

### 3.5. Nút bấm, font và thước thời gian — `tests/test_ui.c`, `tools/render_ui.c`

**Đã kiểm:** debounce 25 ms, nhấn ngắn, nhấn dài, xung dội 5 ms bị bỏ qua, hai nút đồng thời, không lặp sự kiện từ một lần nhấn; bảng font và bề rộng 21 ký tự vừa panel 128 pixel. Render harness biên dịch UI thực với stub và chạy `--selftest` cho caption thời gian/offset và cửa sổ AUTO.

**Đánh giá:** logic nút và định dạng đã có kiểm tra chạy được. Render phần mềm chưa chứng minh màn TFT thật đúng màu, không cắt chữ hoặc không nháy; nút mô phỏng chưa chứng minh dây hàn và tiếp điểm thật hoạt động.

### 3.6. Host Python và streaming giả lập

| Bộ test | Nội dung | Số test Python |
|---|---|---|
| `test_host_la.py` | Manual không trigger; cạnh lên/xuống; pattern; don't-care; chiều dài cửa sổ và lịch sử pre-trigger; matcher C/Python nhất quán | 7 |
| `test_stream.py` | RLE ra `.sr` đúng từng mẫu và metadata/drop; raw ra file đúng từng mẫu; từ chối rate vượt giới hạn USB trước khi chạy | 3 |

Các test dùng emulator/PTY để tái tạo dữ liệu biết trước. Chúng chứng minh khả năng cấu hình, nhận, giải nén và lưu dữ liệu theo mô hình; thông lượng và mất mẫu thật cần xem mục 6.

### 3.7. Driver libsigrok C thực — `tests/test_integration.py`

Bộ này chạy binary sigrok-cli thực với firmware giả lập độc lập qua PTY, không thay driver C bằng parser Python. Có 9 test method; một số method có nhiều subtest nên không đồng nhất số method với số phép kiểm.

| Nhóm kiểm tra | Điều đã kiểm |
|---|---|
| Dung lượng cực đại | 262.144 mẫu, chia nhỏ đường truyền thành mảnh 7 byte, so từng byte file với pattern độc lập |
| Rate và kích thước lẻ | 8 rate: 1 kHz, 10 kHz, 100 kHz, 250 kHz, 500 kHz, 1 MHz, 5 MHz, 25 MHz; 1031 mẫu |
| Bật/tắt kênh | Giữ vị trí bit CH0/CH7; không dồn lại bit khi tắt kênh khác |
| Chèn lỗi có chủ đích | Frame CRC, sequence, metadata, capture CRC, thiếu END/timeout |
| Cấu hình sai | Count 0, count 262145, rate không hỗ trợ |
| Xuất file an toàn | Wrapper không để lại file cuối hợp lệ khi capture sai |
| Trigger qua driver | Edge, pattern, đánh dấu vị trí trigger và đủ số mẫu |
| Decoder PC | UART 115200 trên file thu được, annotation khớp byte kỳ vọng |

**Đánh giá:** đây là bằng chứng mạnh cho đường firmware-protocol/driver/file/decoder trong môi trường mô hình. Script kiểm nội dung và thông báo `Capture verified`, vì mã thoát 0 của sigrok-cli không luôn phản ánh lỗi trong callback. Phiên báo cáo này chưa tái xác nhận được toàn bộ bộ test do lỗi khởi tạo libusb, chi tiết mục 9.

## 4. Kiểm thử thu tín hiệu và trigger trên phần cứng

### 4.1. Nhận board, nạp firmware và USB

Hồ sơ tháng 10 ghi đã build host stack và firmware Pico 2/Pico 2 W; từng gặp board chạy firmware cũ nên HELLO timeout. Sau nạp đúng UF2, driver nhận HELLO/GET_CAPS, đủ 8 kênh và 8 rate; thu đủ 100.000 mẫu ở 1 MHz và 262.144 mẫu ở 25 MHz, file có metadata và CRC hợp lệ.

Các capture ban đầu toàn `0x00`, vì vậy chỉ chứng minh handshake và truyền buffer, chưa chứng minh nhận cạnh tín hiệu. Những phép thử ESP32 về sau mới bổ sung bằng chứng có tín hiệu và giải mã. Hồ sơ Pico 2 W cũ không được đồng nhất với việc mọi phiên bản firmware hiện tại đều vừa được flash lại trên cả hai board.

### 4.2. Đo chu kỳ tín hiệu chuẩn

Hồ sơ `TESTING.md` ghi ESP32 phát sóng vuông danh định 1 kHz, thu ở 1 MHz. Hai cạnh lên tại mẫu 682 và 1682, cách nhau 1000 mẫu, quy đổi thành chu kỳ 1000 µs.

**Kết luận hợp lý:** phép đổi sample sang thời gian và chu kỳ đo được khớp nguồn phát trong ca thử này. Chưa đủ để chứng nhận sai số timing <1% trên toàn dải vì chưa có tham chiếu clock được hiệu chuẩn và thống kê sai số.

Hồ sơ cũ còn ghi duty 12,5% và giải thích do cắt cửa sổ. Cửa sổ 2000 mẫu ở 1 MHz tương đương hai chu kỳ 1 kHz danh định; riêng việc bắt đầu giữa pha không đủ giải thích duty 12,5% cho sóng liên tục duty 50%. Vì vậy không dùng số duty này để xác nhận độ chính xác duty; cần xem raw waveform hoặc đo lại.

### 4.3. Trigger cạnh và trigger pattern

- Cạnh lên CH0, tổng 10.000 mẫu, pre-trigger 2000: báo `trigger_index=2000`; mẫu trước là 0, mẫu tại trigger là 1. Chu kỳ giữa các cạnh vẫn 1000 mẫu.
- Pattern `value=0x01`, `mask=0x01`, cùng cửa sổ: báo trigger tại 2000 và bit CH0 đúng điều kiện. Pattern có thể khớp trước trigger vì chỉ bắt đầu xét sau khi đủ lịch sử pre-trigger.
- Pattern đa bit/mặt nạ phức tạp có kiểm tra phần mềm; chưa có xác nhận phần cứng đầy đủ cho trường hợp `0x5a/0x0f` trong hồ sơ hiện có.

**Đánh giá:** các ca cơ bản chứng minh trigger hoạt động trên board. Vị trí đúng trong một số lần thử không chứng minh hằng số DMA lag là tối ưu hoặc luôn an toàn ở mọi tình huống.

### 4.4. Lỗi trigger muộn đã tìm thấy và kiểm tra sau sửa

Ở 25 MHz, toàn buffer chỉ dài khoảng 10,5 ms, trong khi máy phát có burst cách nhau khoảng 100 ms. Bản cũ có thể tìm thấy trigger quá muộn, không còn đủ mẫu phía sau; cách restart cũ còn có thể lặp lệch pha và gây timeout.

Firmware đã sửa cách chọn cửa sổ và giới hạn count/pre-trigger. Test ESP32 sau cùng chọn 65.536 mẫu/pre 32.768 để giữ đủ dữ liệu sau trigger; commit `576873a` sửa chính cấu hình test này sau lần UART chỉ đọc được `LA1`.

| Phép thử hồi quy ghi trong hồ sơ 17/11 | Trước sửa | Sau sửa |
|---|---|---|
| CH7, 25 MHz, 100.000 mẫu, 12 lần | 3/12 thành công | 12/12 |
| 120.000 mẫu, 10 lần | Chưa ghi | 10/10; có lần `trigger_index=74922` |
| Lệnh sigrok-cli tái hiện lỗi, 5 lần | 0/5 | 5/5 |
| `plan_checks --only ch7` | 3/6 ca | 6/6 ca |
| Tích hợp I1–I5 | Có kết quả trước | 5/5; I4 lặp 5/5 |

**Giới hạn còn lại:** trigger_index thực tế có thể khác pre-trigger yêu cầu khi firmware phải đổi cửa sổ. Host cần đọc thông tin trả về. Edge trigger CAN có thể rơi vào cạnh giữa frame thay vì SOF; hồ sơ từng ghi khoảng 1/12 lần gặp tình huống này. Chưa có trigger CAN chuyên biệt xác nhận vùng idle trước SOF.

## 5. Kiểm thử giải mã 4 giao thức với ESP32

### 5.1. Các phép thử từng bus và bài end-to-end hiện hành

Các lần thử từng bus trước đây đã ghi UART tại 1/5/25 MHz, I2C 100 kHz tại 1/25 MHz, SPI 1 MHz khi chụp 25 MHz, CAN 250 kbps tại 1/25 MHz. Payload I2C đúng địa chỉ/thanh ghi/dữ liệu/ACK; SPI đúng chuỗi byte; CAN đúng ID/DLC, data thay đổi theo bộ phát.

Bài tự động `tests/test_esp32_link.py` ở commit đang đánh giá kiểm hai đường dữ liệu riêng: snapshot giải mã trên firmware, streaming giải mã trên PC. Trong snapshot, Python gọi `decode_all()` để nhận text từ firmware; việc text được in trên máy tính không có nghĩa thuật toán snapshot đó chạy trên PC.

| Snapshot hiện hành | Trigger falling | Rate lấy mẫu | Count / pre | Kết quả được bàn giao |
|---|---|---|---|---|
| UART 115200, 8N1 | CH0 | 25 MHz | 65536 / 32768 | `LA1234` |
| I2C 100 kHz | CH1 | 25 MHz | 65536 / 32768 | `0x68 W ACK 0x01 ACK 0x5A ACK` |
| SPI 1 MHz, mode 0 | CH3 | 5 MHz | 65536 / 32768 | `DE AD BE EF 00 01 02 03` |
| CAN 250 kbps | CH7 | 25 MHz | 65536 / 32768 | `CAN ID=0x123` |

**Kết quả bàn giao:** 4/4 snapshot PASS, cộng 4/4 streaming PASS ở mục 6, tổng 8 dòng PASS. Mỗi hàng snapshot là một lần thu có trigger phù hợp bus đó; không diễn giải bảng này thành một lần capture duy nhất cho cả bốn hàng.

**Độ chặt của check:** script snapshot chỉ bắt buộc các substring `LA1234`, `0x68 W ACK`, `DE AD BE EF`, `CAN ID=0x123`; không assert toàn bộ payload, vị trí trigger đúng bằng pre-trigger hay DLC ở cả bốn hàng. Kết quả chi tiết được bàn giao đầy đủ hơn ngưỡng assert của script. Với CAN có payload tăng, kiểm ID hợp lý cho smoke test nhưng chưa kiểm toàn bộ nội dung từng frame.

### 5.2. Sweep tốc độ: 80 ca giao thức và 5 ca tích hợp

Hồ sơ 16–17/11 ghi đã chạy 20 ca UART, 20 I2C, 20 SPI, 20 CAN và 5 ca tích hợp. Phụ lục A liệt kê lại từng ca theo bảng nguồn để tra cứu. Đây là sweep khả năng và giới hạn; có ca cố ý làm sai baud, thiếu START hoặc lấy mẫu quá chậm. Do đó không được viết “85/85 đều giải mã thành công”.

Hồ sơ chạy lại sau sửa trigger ghi UART 19/20 (ca 9600 lệch pha, lặp lại đạt), I2C 17/20, SPI 16/20, CAN 19/20. Các số này là kết quả ghi trong hồ sơ, không phải tỷ lệ nghiệm thu tổng hợp: bảng nguồn có ký hiệu biên M, kỳ vọng đã được sửa sau đo và một ca CAN 10 kbps bị nguồn phát giới hạn. Cần xem từng ca và các ghi chú hiệu chỉnh thay vì cộng các con số thành phần trăm thành công của sản phẩm.

| Nhóm | Giới hạn/khả năng đã quan sát | Cách hiểu |
|---|---|---|
| UART @25 MHz | Tới 5 Mbaud | Chạm mức máy phát đã dùng; chưa xác định trần bộ thu |
| UART @5 MHz / @1 MHz | 921600 / 230400 baud | Các điểm đã đạt, không phải cam kết mọi cấu hình thấp mẫu/bit đều ổn định |
| I2C | Cấu hình tới 500 kHz tại rate 5/25 MHz | Bit-bang ESP32 có sai lệch; chưa có tần số thực đầy đủ cho mọi ca |
| SPI @25 MHz | 10,5 MHz: 3/3 đạt; 11 MHz: 0/3 | Biên quan sát trên bench, không phải thông số bảo đảm với mọi pha/dây/nhiễu |
| CAN @25 MHz / @5 MHz | Tới 1 Mbps | Đạt với CAN standard và nguồn TWAI dùng trong test |
| CAN @1 MHz | 500 kbps có kết quả đạt | Chỉ 2 mẫu/bit; không chọn làm cấu hình đo timing chuẩn |

Các bài không đạt do lấy mẫu thiếu: I2C 100 kHz khi lấy mẫu 100 kHz; SPI 1 MHz khi lấy mẫu 1 MHz; SPI từ 12 MHz trong sweep lấy mẫu 25 MHz. CAN 1 Mbps khi lấy mẫu 1 MHz có kết quả không ổn định và không được coi là chế độ tin cậy. Các kết quả này giúp xác định cấu hình dùng được.

Bài SPI 10,5 MHz tương ứng khoảng 2,38 **mẫu mỗi chu kỳ SCK**, không phải 2,38 mẫu mỗi nửa chu kỳ như một đoạn tài liệu cũ. Ngưỡng sát biên cần nhiều lần lặp/pha hơn trước khi quảng bá thành thông số sản phẩm.

### 5.3. Kiểm tra lặp CAN và tích hợp 8 kênh

| Ca lặp CAN sau sửa trigger | Kết quả hồ sơ |
|---|---|
| 125 kbps, lấy mẫu 25 MHz | 5/5 |
| 1 Mbps, lấy mẫu 25 MHz | 5/5 |
| 500 kbps, lấy mẫu 5 MHz | 5/5 |
| 20 kbps, lấy mẫu 1 MHz | 3/3 |

CAN 10/16 kbps trong bảng cũ không được dùng làm bằng chứng bộ thu đã đạt: ghi chú cập nhật nêu giới hạn cấu hình TWAI của máy phát. Điểm chậm có lần lặp rõ là 20 kbps.

| Ca tích hợp | Cấu hình/mục đích | Kết quả ghi nhận |
|---|---|---|
| I1 | UART 115200 + I2C 100 kHz + SPI 1 MHz + CAN 250 kbps, thu 25 MHz | Decode đủ 4 giao thức trong một capture |
| I2 | UART 5 Mbaud + I2C đặt 500 kHz + SPI 10 MHz + CAN 1 Mbps, thu 25 MHz | Decode đủ 4; giới hạn nguồn phát vẫn áp dụng |
| I3 | Thu 1 MHz/262144 mẫu không trigger, SPI hạ 100 kHz | Đủ 4; SPI 1 MHz ở rate này không đạt |
| I4 | Pre-trigger 50%, 40000 mẫu ở 25 MHz | Đủ 4, được ghi lặp 5/5 sau sửa |
| I5 | Mask 0x01 trên đường xuất, vẫn decode nội bộ | Đủ 4; firmware vẫn thu đủ 8 bit cho decoder |

**Đánh giá:** sản phẩm đã có bằng chứng nhiều bus cùng tồn tại trên 8 dây, không chỉ đổi một bus lần lượt vào CH0. I5 không có nghĩa PC nhận mọi kênh khi chỉ bật CH0; kết quả nói về dữ liệu firmware dùng để decode nội bộ.

## 6. Kiểm thử streaming và dữ liệu lưu trên PC

### 6.1. RLE 1 MHz trên board thật

Theo bàn giao, bài tự động thu 8 giây để kiểm UART/I2C/CAN, sau đó thu 5 giây cho SPI khi đặt `set spi 100`; cuối cùng khôi phục `set spi 1000`. Các file `.sr` được sigrok giải mã trên PC.

| Decoder PC | Ánh xạ/cấu hình | Annotation kỳ vọng đã thấy |
|---|---|---|
| UART | RX CH0, 115200 | `4C 41 31 32 33 34` = `LA1234` |
| I2C | SCL CH1, SDA CH2 | `Address write: 68` |
| SPI | CLK CH4, MOSI CH5, CS CH3; nguồn 100 kHz | `DE AD BE EF` |
| CAN | RX CH7, 250000 bps | `Identifier: 291` = `0x123` |

**Kết quả được bàn giao:** 4/4 giao thức đạt, 0 drop trong các lần thử này. Tệp tạm của script tự động được xóa khi kết thúc; bàn giao không cung cấp toàn bộ capture và JSON cho báo cáo này.

**Giới hạn check hiện có:** `test_esp32_link.py` kiểm substring decoder nhưng không assert trực tiếp `dropped == 0`. Công cụ streaming in drop và ghi JSON; mã thoát của công cụ vẫn có thể là 0 dù có drop. Vì vậy “8 dòng PASS” tự nó chưa chứng minh “0 drop”; số drop là kết quả quan sát riêng được bàn giao. 1 MHz RLE không được suy rộng thành cam kết 0 drop với mọi tín hiệu đầu vào, đặc biệt khi nén kém.

### 6.2. Raw 500 kHz và trường hợp quá tải

| Cấu hình đã thử tay theo bàn giao | Kết quả | Ý nghĩa |
|---|---|---|
| Raw 500 kHz, khoảng 3 giây | 1.499.132 mẫu, 0 drop, UART decode đúng | Có chế độ không nén hoạt động với cấu hình này |
| Raw 1 MHz, khoảng 3 giây | Mất khoảng 1,2 triệu mẫu | Đã chạm giới hạn đường truyền; không dùng để cam kết thu liên tục không mất mẫu |

Số mẫu raw 500 kHz gần 1,5 triệu phù hợp thời lượng khoảng 3 giây. Chênh lệch với đúng 1.500.000 không tự đồng nghĩa drop vì thời gian bắt đầu/dừng không nhất thiết bằng đúng 3,000 giây thu; cần đối chiếu offset và số liệu thiết bị. Chưa có chạy bền hàng giờ, tải USB cạnh tranh hoặc thống kê trên nhiều máy tính.

## 7. Kết nối PulseView và trải nghiệm giao diện

### 7.1. Kết nối và cấu hình đã xác nhận trực tiếp

Driver `rp2350-la` nhận thiết bị qua `/dev/ttyACM0`. CLI `--show` liệt kê CH0–CH7, đủ 8 rate từ 1 kHz tới 25 MHz, giới hạn bộ nhớ 262144, các tùy chọn trigger source/slope/pattern. GUI mở được và hiển thị tên **RP2350 Pico 2 / 2 W Logic Analyzer**, đủ 8 kênh; đợt kiểm chứng trực tiếp sau cùng dùng 65536 mẫu và 25 MHz, với chữ hiển thị `65.536 k samples` khớp cấu hình thực. Có JSON đọc lại trigger CH0 falling/pre-trigger 50%.

Capture qua driver có trigger CH7 falling, 25 MHz, 65536 mẫu đã được bàn giao là giải mã đủ 4 giao thức bằng wrapper sigrok-cli. Điều này chứng minh stack driver và decoder dùng chung với PulseView làm việc với dữ liệu bộ thu.

### 7.2. Phần GUI từng dang dở và kết quả hoàn tất

Theo bàn giao cũ, mới mở Decoder Selector và thêm UART vào session, chưa hoàn thành bước chọn channel/baud và xem annotation cả bốn bus. Đợt cập nhật mục 12 đã hoàn thành trực tiếp bằng preset gán kênh/baud, live acquisition trong PulseView, đọc annotation từ GUI và chụp ảnh cửa sổ thật. Hiển thị và thay đổi trigger source/slope trên chính widget GUI cũng chưa có bộ ảnh/nghiệm thu đầy đủ; phần cấu hình này đã được kiểm ở driver/CLI.

Hồ sơ 03/11 có ghi GUI CAN hiển thị ID 0x123 và mở file I2C/gán SCL/SDA để thấy dữ liệu, dùng sơ đồ từng bus cũ. Đây là kết quả lịch sử hữu ích, nhưng chưa thay cho ảnh và quy trình GUI bốn decoder theo map hiện tại. CLI chứng minh thuật toán giải mã, còn cấu hình thuận tiện và annotation hiển thị đúng là một lớp nghiệm thu giao diện riêng.

**Mức hoàn thiện hiện tại:** kết nối, cấu hình và live decode đủ bốn bus trong GUI đã đạt; ảnh và JSON hiện tại được lưu trong bằng chứng chạy thật. Có file `.pvs` tái lập gán kênh/baud và `capture.sr` thật. Quy trình Save As/Open lại toàn bộ session bằng thao tác menu GUI vẫn chưa có ca nghiệm thu riêng.

## 8. Màn TFT, nút bấm, chạy song song và chức năng bổ sung

### 8.1. TFT và điều khiển UI

Hồ sơ cũ có kiểm màu đỏ, gradient và color bars. Hồ sơ UI 17/11 ghi kiểm trạng thái chuyển màn qua RUNTIME, capture do UI yêu cầu hoàn tất, dữ liệu demo giải mã `HELLO` và phím USB không bị nuốt. Những bài đó chứng minh luồng điều khiển nhưng không thay cho nghiệm thu hình ảnh của UI cuối cùng.

UI hiện có 6 màn và chữ tiếng Anh; Rate/Samples/Chan/Pre-Trig là 4 dòng cấu hình; OK ngắn để sửa, giữ OK để capture. Có AUTO/UART/I2C/SPI/CAN, caption `T:`, `1div:`, strip `ST:`. Các chỉ dẫn “4 DOWN + OK tới dòng Start” hoặc tên màn tiếng Việt trong tài liệu cũ đã lỗi thời; không dùng làm hướng dẫn vận hành firmware `576873a`.

**Chưa xác nhận đầy đủ:** độ đọc được ngoài đời, màu/orientation của bản cuối, chữ tràn, nhấp nháy, thời gian phản hồi được đo, bốn nút đã đấu/hàn và nhấn dài trên phần cứng. Các thao tác USB và test debounce chưa bao phủ những mục này.

### 8.2. USB chạy cùng UI

Hồ sơ `PLAN_Nov16.md` ghi 3/3 lần host capture 262144 mẫu ở 1 MHz khi UI đang ở màn decode, giải mã đủ UART/I2C/SPI/CAN; phím USB xen giữa các capture được xử lý, không treo UI.

Đã xác định UI capture cũng phát SAMPLES ra USB: host phải đọc hết, nếu không `send_frame` có thể chờ tới deadline khoảng 3 giây làm UI có vẻ bỏ phím. `ui_cycle.py` đã drain frame và HELLO lại sau capture.

**Đánh giá:** đã có kiểm phối hợp UI–USB cơ bản. Chưa có bằng chứng nghiệm thu tình huống người dùng giữ OK chụp ngay lúc PulseView đang acquisition hoặc có hai chương trình cùng sở hữu cổng.

### 8.3. Lưu flash, golden compare và diff

Hồ sơ CAN 03/11 ghi save session 0/1 thành công; load session 1 giải mã lại khớp; golden compare tolerance 0 báo `MATCH (0 mismatches, 10000 samples)`; diff báo identical; session header có magic LASE và count/rate đúng.

Đây là kết quả lịch sử trên cấu hình từng bus. Chưa có hồi quy phần cứng hiện tại cho lưu/đọc đầy đủ 8 kênh, mất điện giữa lúc ghi hoặc độ bền flash. Không xếp các chức năng phụ này ngang mức nghiệm thu của đường snapshot/stream đã có script ESP32 hiện hành.

## 9. Lịch sử chạy đầu phiên, trước khi mở quyền thực thi

Không sửa firmware, driver hay logic test. Chỉ tạo tài liệu và lưu bản sao bằng chứng. Build đã chạy thành công; không flash vì không sửa UI và không có board trong môi trường hiện tại.

| Lệnh/nhóm | Kết quả phiên này | Giải thích |
|---|---|---|
| `cmake --build build/firmware-pico2 -j4` | PASS | Target `rp2350_la` đạt 100%; là build tăng dần, không tuyên bố clean rebuild toàn stack |
| `bash scripts/test.sh` | Dừng, không PASS toàn bộ | LeakSanitizer báo không chạy được dưới ptrace trong harness |
| `ASAN_OPTIONS=detect_leaks=0 bash scripts/test.sh` | Đạt các nhóm trước integration; toàn lệnh vẫn FAIL | Chỉ tắt kiểm rò bộ nhớ, vẫn giữ ASan/UBSan; chi tiết bên dưới |
| C protocol/trigger/decode/autodetect/UI | PASS trong lần chạy có cấu hình ASAN nói trên | Tất cả assertion đã qua |
| Host / stream Python | 7/7 và 3/3 PASS | Có log |
| Render UI `--selftest` trong script | PASS | Caption format và AUTO window |
| Driver integration | Không tái xác nhận; runner báo FAIL | 9 test method, 19 failure gồm subtest; log lặp lỗi `libusb_init() returned LIBUSB_ERROR_OTHER` trước capture |
| `python3 tests/test_esp32_link.py` | Không chạy được phần thu | `FileNotFoundError: /dev/ttyACM0`; cả ttyACM0 và ttyUSB0 không hiện diện khi kiểm tra |

Bộ integration có một ca wrapper từ chối file trả `ok`, nhưng do khởi tạo stack đang lỗi, không dùng kết quả đó làm bằng chứng mới rằng chèn lỗi CRC đã được xử lý đúng. Log cho thấy lỗi hạ tầng xảy ra trước khi kiểm được các luồng mong muốn; chưa đủ cơ sở kết luận đây là hồi quy mã nguồn. Cũng không được viết toàn bộ test hiện tại “PASS” dựa trên kết quả bàn giao trước.

Raw log được lưu trong `test-evidence-2026-11-17/current/`; chúng cho phép phân biệt chính xác hai lần chạy suite và lần thử phần cứng. Cần chạy lại full suite không vướng libusb/sanitizer và test ESP32 trên máy có board trước khi đóng nghiệm thu phiên bản cuối.

## 10. Đánh giá độ hoàn thiện theo yêu cầu sản phẩm

| Hạng mục | Mức hoàn thiện | Cơ sở và phần còn thiếu |
|---|---|---|
| Thu ít nhất 8 kênh | Lõi chức năng đã được kiểm chứng | Map CH0–CH7, capture đa bus và I1–I5; chưa đo skew giữa kênh bằng thiết bị chuẩn |
| Lấy mẫu ít nhất 25 MHz | Đã có capture và decode ở cấu hình 25 MHz | Chưa chứng nhận sai số clock, pulse-width tối thiểu hoặc đáp ứng đầu vào toàn dải |
| Trigger | Hoạt động, có hồi quy sau sửa | Có giới hạn count/pre; trigger CAN cạnh có thể không phải SOF; chưa bao phủ mọi tình huống DMA |
| Decode UART/I2C/SPI/CAN | Đã kiểm trên waveform giả và ESP32 | Trong các format/map/baud đã thử; chưa nghiệm thu mọi biến thể, error frame, nhiễu hoặc CAN extended |
| Hiển thị trên PC | Đã kiểm chứng live GUI với bốn decoder | Có screenshot, annotation JSON, gán kênh/baud, rate/count/trigger đúng; Save/Open session bằng menu chưa kiểm riêng |
| Streaming | Hoạt động trong phạm vi đã thử | RLE 1 MHz và raw 500 kHz; raw 1 MHz có drop; chưa test bền dài hạn |
| Firmware C/C++ SDK, host Python | Đáp ứng cấu trúc triển khai | Pico C/C++ SDK + công cụ host Python; GUI dùng PulseView/Qt |
| TFT/nút bấm | Readback trên board đạt; phần vật lý chưa đủ | Text/bảng byte bốn bus đúng, tự đồng bộ sau PC capture; còn ảnh camera panel và test nút đã hàn |
| Bảo vệ 12 V DC / xung 200 V | Chưa nghiệm thu | Hồ sơ có ý tưởng clamp; thiếu kết quả mạch hoàn chỉnh chịu áp/xung và ảnh hưởng lên tín hiệu |
| BOM ≤450.000 VNĐ | Có dự toán, chưa chốt nghiệm thu | Hồ sơ cũ ước tính 366.000–445.000 VNĐ; chưa có BOM cuối với số lượng/đơn giá/chứng từ, nhất là mạch bảo vệ |
| Báo cáo, slide, video ≤3 phút | Đang hoàn thiện | Báo cáo này là báo cáo kiểm thử; chưa thay cho toàn bộ báo cáo đồ án, slide và video |

**Nhận định tổng thể:** thiết bị đạt mức mẫu thử chức năng có thể trình diễn bằng tín hiệu ESP32 và có nền tảng kiểm thử phần mềm đáng kể. Độ sẵn sàng cho demo cao hơn độ sẵn sàng nghiệm thu toàn bộ Topic 2. Chưa đủ căn cứ gọi là thiết bị đo đã được hiệu chuẩn hoặc sản phẩm hoàn thiện để sử dụng cho đầu vào 12 V/200 V.

### 10.1. Các thiếu hụt cần đóng trước nghiệm thu

| Ưu tiên | Việc cần hoàn tất | Tiêu chí đóng rõ ràng |
|---|---|---|
| 1 | Kiểm chứng bảo vệ đầu vào | Sơ đồ/BOM cuối; định nghĩa biên độ, độ rộng, trở kháng nguồn và số lần xung 200 V; kết quả đo trên mạch bảo vệ, mức tại chân thu, tình trạng trước/sau và ảnh hưởng timing |
| Đã đạt phần demo | GUI demo cuối | Đã có 8 kênh, bốn decoder, ảnh và JSON cấu hình/annotation. Còn ca Save/Open qua menu nếu cần nghiệm thu thao tác đó |
| 1 | Hồ sơ bàn giao | Báo cáo đồ án hoàn chỉnh, slide, video không quá 3 phút; dùng đúng thông số và hạn chế đã xác minh |
| 2 | TFT và nút thật | Ảnh/video 6 màn; mỗi nút nhấn ngắn/dài đúng; giữ OK chụp được; không tràn chữ hoặc kẹt thao tác |
| 2 | Chốt bằng chứng hồi quy | Full suite và ESP32 trên môi trường có thiết bị; giữ raw capture `.sr`, JSON drop, stdout/stderr và mã commit |
| 2 | Chốt BOM | Bảng vật tư thực tế gồm mạch bảo vệ, PCB, màn/nút/phụ kiện thuộc phạm vi đề tài; tổng không vượt 450.000 VNĐ |
| 3 | Chất lượng đo và độ bền | Nguồn thời gian tham chiếu độc lập; sai số/độ lệch kênh/duty; stream dài với tín hiệu nén tốt và kém; thống kê lỗi qua nhiều lần |

Không cần phát triển thêm tính năng để có nội dung báo cáo thuyết phục. Phần việc tạo giá trị lớn nhất hiện nay là hoàn tất bằng chứng cho các chức năng đã có và các yêu cầu phần cứng bắt buộc còn thiếu.

## 11. Lệnh tái lập và bằng chứng nên lưu

Chạy từ thư mục gốc `PulseView-custom`. Đóng chương trình đang giữ cổng trước khi chạy test phần cứng. Dùng wrapper sigrok-cli theo dự án để có môi trường thư viện đúng.

```bash
source scripts/env.sh
bash scripts/test.sh
cmake --build build/firmware-pico2 -j4
python3 tests/test_esp32_link.py
./scripts/run-sigrok-cli.sh -d rp2350-la:conn=/dev/ttyACM0 --show
```

Capture và giải mã PC theo map 8 kênh:

```bash
./scripts/run-sigrok-cli.sh -d rp2350-la:conn=/dev/ttyACM0 \
  --config samplerate=25000000 --config triggersource=CH7 \
  --config triggerslope=falling --config horiz_triggerpos=0.5 \
  --samples 65536 -o cap.sr
./scripts/run-sigrok-cli.sh -i cap.sr -P uart:rx=CH0:baudrate=115200 -A uart=rx-data
./scripts/run-sigrok-cli.sh -i cap.sr -P i2c:scl=CH1:sda=CH2 -A i2c=address-write
./scripts/run-sigrok-cli.sh -i cap.sr -P spi:clk=CH4:mosi=CH5:cs=CH3 -A spi=mosi-data
./scripts/run-sigrok-cli.sh -i cap.sr -P can:can_rx=CH7:nominal_bitrate=250000 -A can=id
```

Streaming và kiểm tra UI:

```bash
python3 tools/stream_capture.py -t 10 -r 1000000 -o run.sr
python3 tools/stream_capture.py -t 3 -r 500000 --no-rle -o raw500.sr
build/tests/render_ui --selftest
python3 tools/ui_cycle.py --protocols
python3 tools/ui_cycle.py --capture -i 1
```

Muốn giải mã SPI ở streaming 1 MHz, đặt ESP32 `set spi 100` trước thu và khôi phục `set spi 1000` sau thu. Không lấy một kết quả UART đúng làm bằng chứng mọi bus trong file đều đúng. Với mỗi lần thu, giữ file `.sr`, JSON chứa số drop, output từng decoder, mã commit và ảnh GUI. Một capture cụ thể có thể thiếu frame nếu burst nằm ngoài cửa sổ; kiểm tra vị trí trigger và đủ số mẫu sau trigger khi tái lập.

## 12. Cập nhật kiểm chứng trực tiếp sau khi mở quyền thực thi

Ngày 17/11/2026, sau khi chuyển phiên sang `danger-full-access`, đã truy cập được Pico 2 tại `/dev/ttyACM0`, ESP32 tại `/dev/ttyUSB0`, desktop 1920×1080 và mạng. Vì vậy trạng thái bị chặn ở mục 9 đã được giải quyết; các kết quả mục 9 vẫn được giữ đúng như lịch sử lần chạy trước.

Đã build/nạp firmware có `+ui18` và chạy trực tiếp script demo. Driver nhận tên `RP2350 Pico 2 / 2 W Logic Analyzer`; có 8 kênh CH0–CH7, cấu hình 25 MHz, 65536 mẫu, trigger falling và pre-trigger 50%. Đã kiểm cả cấu hình đọc từ driver và giá trị toolbar trong GUI; sửa lỗi widget làm tròn count 65536 thành 50k, nay hiển thị đúng `65.536 k samples`.

TFT dùng snapshot thật vừa thu qua driver: text và bảng byte UART `LA1234`, I2C `0x68 W ACK 0x01 ACK 0x5A ACK`, SPI `DE AD BE EF 00 01 02 03`, CAN ID `0x123`/DLC 4 đều đúng. Có kiểm screen ID và badge cho từng protocol. GUI PulseView sau đó tự thu một capture mới, được cấu hình sẵn bốn decoder và sinh annotation đúng cho cả bốn. Đã xem ảnh GUI thật, đủ 8 track và bốn hàng decode; dữ liệu annotation được lấy từ chính tiến trình PulseView, không suy ra từ kết quả CLI.

Toàn bộ `bash scripts/test.sh` PASS trên môi trường đã mở quyền, bao gồm 9 test driver integration, 8 test demo mới và 2 ca kiểm tra retry cửa sổ. Bài ESP32 đạt 4 snapshot + 4 streaming. Một lần thử trước gặp trigger CAN quá muộn và frame bị cắt; raw log lỗi được giữ. Test đã thêm tối đa 6 lần thu lại chỉ khi metadata chứng minh không đủ mẫu sau trigger; không retry dữ liệu sai trong cửa sổ đủ dài để biến lỗi thành PASS.

Bản hoàn thiện `+ui18+sync` rạng sáng 18/11 còn đồng bộ TFT sau mỗi snapshot từ PC, cập nhật cả cửa sổ, trigger và packet cache khi trang DECODE đang mở. Có regression test thay dữ liệu đầu vào khi giữ nguyên trang decode để phát hiện byte cũ còn sót. Hai nhiệm vụ kết nối/decode trực tiếp trên GUI nay có bằng chứng chạy mới. Phần đọc lại TFT đã kiểm trên board thật; màu, backlight và nút vật lý vẫn chưa có camera/phép thử tiếp điểm. Bảo vệ 12 V/200 V, BOM và các yêu cầu độ chính xác đo chưa được bổ sung bằng phép thử mới trong đợt này.

Xem [hướng dẫn demo](DEMO.md) và [bằng chứng chạy thật](demo-validation-2026-11-17/real-run/README.md). Chạy `bash scripts/demo.sh` trên máy cắm hai board để lặp lại quy trình; kết quả PASS/FAIL, log, capture và ảnh được giữ trong `build/demo/`.

## Phụ lục A. Danh mục 80 ca sweep đã ghi trong hồ sơ

Các bảng dưới được rút từ bảng kết quả nguồn `history/TEST-PLAN-8CH.md`, giữ số thứ tự để tra cứu. P = nguồn ghi giải mã đạt, F = không giải mã đạt, M = biên/không ổn định. F trong bài âm có thể đúng kỳ vọng; P ở cấu hình sai hoặc ngoài khả năng máy phát vẫn cần kiểm tra, không tự động trở thành bằng chứng đạt. Ký hiệu là kết quả lịch sử, không phải lần chạy mới trong phiên báo cáo.

Cấu hình viết `rate / N / pre / trigger`; `25m` là lấy mẫu 25 MHz; `CH0↓` là cạnh xuống CH0. Có những kỳ vọng cũ không còn đúng hoặc kết quả giữa các lần chạy khác nhau; các hiệu chỉnh tại mục 5 được ưu tiên. Đặc biệt: CAN ca 16 (10 kbps) không được nghiệm thu; UART ca 7 có ghi M ở bảng cũ nhưng các lần sau đã PASS; CAN ca 2 được bổ sung lặp 5/5.

### A.1. UART — 20 ca

| Ca | Mục tiêu | Cấu hình ESP32 | Cấu hình bộ thu | KQ nguồn |
|---|---|---|---|---|
| 1 | chậm nhất, cửa sổ dài | `set uart 9600` | 1m / 10000 / 500 / CH0↓ | P |
| 2 | chậm | `set uart 19200` | 1m / 5000 / 200 / CH0↓ | P |
| 3 | chậm | `set uart 38400` | 1m / 5000 / 200 / CH0↓ | P |
| 4 | chậm | `set uart 57600` | 1m / 4000 / 100 / CH0↓ | P |
| 5 | **baseline 8 kênh** | `set uart 115200` | 25m / 40000 / 2000 / CH0↓ | P |
| 6 | baseline ở 5 MHz | `set uart 115200` | 5m / 40000 / 2000 / CH0↓ | P |
| 7 | 115200 @ 1 MHz — biên dưới | `set uart 115200` | 1m / 40000 / 2000 / CH0↓ | M |
| 8 | 230400 @ 25 MHz | `set uart 230400` | 25m / 40000 / 2000 / CH0↓ | P |
| 9 | 230400 @ 5 MHz | `set uart 230400` | 5m / 40000 / 2000 / CH0↓ | P |
| 10 | 230400 @ 1 MHz | `set uart 230400` | 1m / 40000 / 2000 / CH0↓ | P |
| 11 | 460800 @ 25 MHz | `set uart 460800` | 25m / 40000 / 2000 / CH0↓ | P |
| 12 | 460800 @ 5 MHz | `set uart 460800` | 5m / 40000 / 2000 / CH0↓ | P |
| 13 | 921600 @ 25 MHz | `set uart 921600` | 25m / 40000 / 2000 / CH0↓ | P |
| 14 | 921600 @ 5 MHz | `set uart 921600` | 5m / 40000 / 2000 / CH0↓ | P |
| 15 | 1 Mbaud @ 25 MHz | `set uart 1000000` | 25m / 40000 / 2000 / CH0↓ | P |
| 16 | 1.5 Mbaud | `set uart 1500000` | 25m / 40000 / 2000 / CH0↓ | P |
| 17 | 2 Mbaud | `set uart 2000000` | 25m / 40000 / 2000 / CH0↓ | P |
| 18 | 3 Mbaud | `set uart 3000000` | 25m / 40000 / 2000 / CH0↓ | P |
| 19 | 5 Mbaud (max ESP32) | `set uart 5000000` | 25m / 40000 / 2000 / CH0↓ | P |
| 20 | cửa sổ dài, không trigger, nhiều message | `set uart 115200` | 25m / 262144 / 0 / none | P |

### A.2. I2C — 20 ca

| Ca | Mục tiêu | Cấu hình ESP32 | Cấu hình bộ thu | KQ nguồn |
|---|---|---|---|---|
| 1 | baseline 100 kHz @ 1 MHz | `set i2c 100` | 1m / 100000 / 1000 / CH1↑ | P |
| 2 | baseline @ 25 MHz | `set i2c 100` | 25m / 40000 / 2000 / CH1↑ | P |
| 3 | 100 kHz @ 100 kHz — dưới Nyquist | `set i2c 100` | 100k / 100000 / 1000 / CH1↑ | F |
| 4 | 100 kHz @ 250 kHz — biên | `set i2c 100` | 250k / 100000 / 1000 / CH1↑ | M |
| 5 | 100 kHz, cửa sổ dài, không trigger | `set i2c 100` | 1m / 262144 / 0 / none | P |
| 6 | 4 kHz — chậm nhất | `set i2c 4` | 1m / 20000 / 1000 / CH1↑ | P |
| 7 | 10 kHz | `set i2c 10` | 1m / 10000 / 1000 / CH1↑ | P |
| 8 | 25 kHz | `set i2c 25` | 1m / 10000 / 500 / CH1↑ | P |
| 9 | 50 kHz | `set i2c 50` | 1m / 10000 / 500 / CH1↑ | P |
| 10 | 125 kHz | `set i2c 125` | 5m / 40000 / 2000 / CH1↑ | P |
| 11 | 150 kHz | `set i2c 150` | 5m / 40000 / 2000 / CH1↑ | P |
| 12 | 250 kHz @ 5 MHz | `set i2c 250` | 5m / 40000 / 2000 / CH1↑ | P |
| 13 | 250 kHz @ 25 MHz | `set i2c 250` | 25m / 40000 / 2000 / CH1↑ | P |
| 14 | 250 kHz @ 1 MHz | `set i2c 250` | 1m / 40000 / 2000 / CH1↑ | P |
| 15 | 500 kHz (half 1 µs) @ 25 MHz | `set i2c 500` | 25m / 40000 / 2000 / CH1↑ | P |
| 16 | 500 kHz @ 5 MHz | `set i2c 500` | 5m / 40000 / 2000 / CH1↑ | P |
| 17 | pre = 1 ⇒ mất START | `set i2c 100` | 25m / 40000 / 1 / CH1↑ | F |
| 18 | cửa sổ 50 % trước trigger | `set i2c 100` | 25m / 40000 / 20000 / CH1↑ | P |
| 19 | cửa sổ quá ngắn (1000 mẫu) | `set i2c 100` | 25m / 1000 / 999 / CH1↑ | F |
| 20 | lặp 5 lần @ 500 kHz — độ ổn định | `set i2c 500` | 25m / 40000 / 2000 / CH1↑ ×5 | P |

### A.3. SPI — 20 ca

| Ca | Mục tiêu | Cấu hình ESP32 | Cấu hình bộ thu | KQ nguồn |
|---|---|---|---|---|
| 1 | 100 kHz @ 1 MHz | `set spi 100` | 1m / 100000 / 1000 / CH3↓ | P |
| 2 | 100 kHz @ 25 MHz | `set spi 100` | 25m / 40000 / 2000 / CH3↓ | P |
| 3 | 250 kHz | `set spi 250` | 5m / 40000 / 2000 / CH3↓ | P |
| 4 | 500 kHz | `set spi 500` | 5m / 40000 / 2000 / CH3↓ | P |
| 5 | 1 MHz @ 5 MHz — baseline cũ | `set spi 1000` | 5m / 40000 / 2000 / CH3↓ | P |
| 6 | 1 MHz @ 25 MHz | `set spi 1000` | 25m / 40000 / 2000 / CH3↓ | P |
| 7 | 2 MHz | `set spi 2000` | 25m / 40000 / 2000 / CH3↓ | P |
| 8 | 3 MHz | `set spi 3000` | 25m / 40000 / 2000 / CH3↓ | P |
| 9 | 4 MHz | `set spi 4000` | 25m / 40000 / 2000 / CH3↓ | P |
| 10 | 5 MHz | `set spi 5000` | 25m / 40000 / 2000 / CH3↓ | P |
| 11 | 6 MHz | `set spi 6000` | 25m / 40000 / 2000 / CH3↓ | P |
| 12 | 8 MHz | `set spi 8000` | 25m / 40000 / 2000 / CH3↓ | P |
| 13 | 10 MHz | `set spi 10000` | 25m / 40000 / 2000 / CH3↓ | P |
| 14 | 12 MHz | `set spi 12000` | 25m / 40000 / 2000 / CH3↓ | F |
| 15 | 16 MHz | `set spi 16000` | 25m / 40000 / 2000 / CH3↓ | F |
| 16 | 20 MHz | `set spi 20000` | 25m / 40000 / 2000 / CH3↓ | F |
| 17 | 26 MHz (max ESP32 = 80/3) | `set spi 26666` | 25m / 40000 / 2000 / CH3↓ | F |
| 18 | mask chỉ 4 dây SPI | `set spi 1000` | 25m / 40000 / 2000 / CH3↓, `--mask 0x78` | P |
| 19 | 1 MHz @ 1 MHz — dưới Nyquist | `set spi 1000` | 1m / 262144 / 0 / none | F |
| 20 | bỏ jumper MISO 23↔25 | `set spi 1000` | 25m / 40000 / 2000 / CH3↓ | P |

### A.4. CAN — 20 ca

| Ca | Mục tiêu | Cấu hình ESP32 | Cấu hình bộ thu | KQ nguồn |
|---|---|---|---|---|
| 1 | 125 kbps @ 1 MHz | `set can 125` | 1m / 262144 / 0 / none | P |
| 2 | 125 kbps @ 25 MHz | `set can 125` | 25m / 40000 / 2000 / CH7↓ | M |
| 3 | 125 kbps @ 500 kHz | `set can 125` | 500k / 262144 / 0 / none | P |
| 4 | 125 kbps @ 250 kHz | `set can 125` | 250k / 262144 / 0 / none | P |
| 5 | 250 kbps @ 1 MHz | `set can 250` | 1m / 100000 / 1000 / CH7↓ | P |
| 6 | 250 kbps @ 5 MHz | `set can 250` | 5m / 40000 / 2000 / CH7↓ | P |
| 7 | 250 kbps @ 25 MHz — baseline | `set can 250` | 25m / 40000 / 2000 / CH7↓ | P |
| 8 | 500 kbps @ 1 MHz | `set can 500` | 1m / 100000 / 1000 / CH7↓ | P |
| 9 | 500 kbps @ 5 MHz | `set can 500` | 5m / 40000 / 2000 / CH7↓ | P |
| 10 | 500 kbps @ 25 MHz | `set can 500` | 25m / 40000 / 2000 / CH7↓ | P |
| 11 | 800 kbps @ 5 MHz | `set can 800` | 5m / 40000 / 2000 / CH7↓ | P |
| 12 | 800 kbps @ 25 MHz | `set can 800` | 25m / 40000 / 2000 / CH7↓ | P |
| 13 | 1 Mbps @ 5 MHz | `set can 1000` | 5m / 40000 / 2000 / CH7↓ | P |
| 14 | 1 Mbps @ 25 MHz | `set can 1000` | 25m / 40000 / 2000 / CH7↓ | P |
| 15 | 1 Mbps @ 1 MHz | `set can 1000` | 1m / 100000 / 1000 / CH7↓ | M |
| 16 | 10 kbps — chậm nhất | `set can 10` | 1m / 262144 / 1000 / CH7↓ | P cũ; chưa nghiệm thu (nguồn phát) |
| 17 | sai baud khi decode | `set can 250` | 25m / 40000 / 2000 / CH7↓ | F |
| 18 | mask chỉ CH7 | `set can 250` | 25m / 40000 / 2000 / CH7↓ | P |
| 19 | cửa sổ hẹp đúng 1 frame | `set can 250` | 25m / 20000 / 5000 / CH7↓ | P |
| 20 | cửa sổ dài, không trigger | `set can 250` | 1m / 262144 / 0 / none | P |

## Phụ lục B. Nguồn và mức độ truy xuất bằng chứng

| Nguồn | Nội dung dùng trong báo cáo | Giới hạn |
|---|---|---|
| Mã nguồn `576873a`, `tests/`, `scripts/test.sh` | Phạm vi thực sự của assert, cấu hình và đường chạy | Có test không đồng nghĩa mọi test vừa PASS |
| Bàn giao người dùng ngày 17/11/2026 | Kết quả ESP32 8/8, raw 500 kHz, trạng thái GUI cuối | Chưa có raw capture/ảnh và toàn bộ stdout của lần bàn giao trong bộ evidence này |
| `history/TESTING.md` | Timing, trigger, pattern, màu TFT, USB và CAN ban đầu | Một số kết luận cũ vượt quá phạm vi phép thử; đã hiệu chỉnh ở mục 4 |
| `history/TEST-Nov3.md` | Decode từng bus, GUI CAN/I2C lịch sử, flash/golden/diff | Có cả đề xuất tương lai; không coi đề xuất là đã triển khai |
| `history/TEST-PLAN-8CH.md` | 80 ca sweep + I1–I5, biên tốc độ | Bảng cập nhật nhiều lần; có kỳ vọng/ghi chú cũ mâu thuẫn; không phải raw log |
| `history/PLAN_Nov16.md` | Kết quả lặp CAN, trigger trước/sau sửa, UI+USB | Checklist chưa đánh dấu không được coi là đã xong; UI đã đổi sau nội dung cũ |
| `history/UI-DESIGN-Nov17.md`, `history/WIRING.md` | Luồng UI và đấu nối lịch sử | Bản cuối 6 màn/nhãn tiếng Anh/giữ OK khác mô tả cũ |
| `current/*.log` | Những lệnh thực sự chạy trong phiên viết báo cáo | Có lỗi hạ tầng và thiếu board; không tuyên bố full regression PASS |

Các bản sao lịch sử và raw log có danh sách SHA256 tại `test-evidence-2026-11-17/SHA256SUMS`. Báo cáo này tổng hợp bằng chứng sẵn có, không tạo lại hoặc giả lập log PASS cho những lần thử không chạy được.
