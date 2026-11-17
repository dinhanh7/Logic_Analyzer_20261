# OPINION — Nhận xét hệ thống và bàn giao phiên sau

Cập nhật ngày **18/11/2026**. Phạm vi review nhanh: firmware, protocol, driver,
host tools, scripts và tests do dự án duy trì; các patch PulseView/libsigrokdecode;
nguồn ESP32 thực tế đã đưa vào `test-generator/esp32`. Không nhận đã audit toàn bộ
SDK và mã vendor. Kết quả chi tiết nằm ở [TESTING](TESTING.md),
[TEST-REPORT](docs/TEST-REPORT.md) và [results/LATEST](results/LATEST.txt).

## 1. Kết luận của reviewer

Kiến trúc **PIO + DMA + RAM + USB** phù hợp cho prototype này. Tiếp tục sửa có đo,
không cần viết lại sang RTOS/dual-core chỉ vì plan lịch sử từng đề xuất. Capture,
trigger, bốn decoder, TFT và PulseView đã có bằng chứng chạy thật. Tuy nhiên,
chưa đủ cơ sở gọi đây là logic analyzer thương mại hoặc nghiệm thu toàn bộ đề tài.

Ưu tiên trước buổi trình diễn là giữ bản demo đã kiểm: `DISPLAY=:0 bash scripts/demo.sh`,
mỗi màn TFT khoảng **2 giây**. Không đổi protocol, pin map hoặc decoder preset sát
buổi demo nếu chưa chạy lại test liên quan.

## 2. Những điểm nên giữ

- PIO lấy tám bit đồng thời; DMA và snapshot giúp tách tốc độ lấy mẫu khỏi USB.
- Decoder C production chạy được trên PC để so với oracle độc lập.
- Driver xác minh metadata, sequence và CRC trước khi đánh dấu capture hợp lệ.
- Demo kiểm annotation do chính PulseView tạo, không dùng CLI thay bằng chứng GUI.
- Stream kiểm cả tail, offset, counters và quan hệ số mẫu–thời gian.
- TFT có font rõ, cạnh vuông, thước/cursor và trạng thái pan/zoom riêng cho hai trang.
- USB keys dùng cùng hàng đợi với nút, thuận tiện khi chưa hàn nút vật lý.
- QA có dữ liệu lỗi, oracle, soak, recovery và artifact theo từng case.

## 3. Các lỗi quan trọng đã xử lý

| Mức độ | Vấn đề | Sửa và regression |
|---|---|---|
| High | PC bỏ phần tail khi đợi STOP; vẫn báo 0 drop | Drain đầy đủ và đối chiếu counters; emulator tail + hardware stream |
| High | IRQ snapshot còn pending, tạo slot stream giả 64 KiB | Clear DMA IRQ khi chuyển mode; snapshot→stream + rate/duration check |
| High | Đọc producer DMA quá thưa, có thể gửi vùng bị ghi đè | Refresh mỗi block, giữ margin, đếm gap; host-stall test |
| High | Trigger muộn làm lệch vị trí pre-trigger | Giữ cửa sổ chính xác, retry đổi pha; boundary test + 100 capture |
| High | GUI UART báo “decoder instance not found” | Patch session lifecycle dưới GIL; 40 session song song + GUI live |
| Medium | Autobaud coi idle đầu buffer là độ rộng bit | Bỏ run trước cạnh đầu; 12 tổ hợp fractional width |
| Medium | AUTO nhận clock I2C thành CAN | Gate từ ±25% xuống ±15%; test map dịch chuyển và nhiễu |
| Medium | ESP32 dùng trùng kênh LEDC, SQ2 đụng MISO | PAT dùng channel 2–5; GPIO25 dành cho MISO |
| Medium | UART-off không áp dụng trong burst; đổi CAN lỗi làm ngừng nguồn | Mutex/atomic, validate trước teardown, rollback và counters |
| Medium | Console mất lệnh gõ chậm, đọc đuôi dòng quá dài thành lệnh mới | Giữ partial line, bỏ tới newline, parse số chặt; thử thật |
| Medium | LOAD có thể vượt RAM; DIFF ảnh hưởng golden; so word không hợp cửa sổ lẻ | Giới hạn capture, so byte, đọc XIP trực tiếp; regression read-only |
| Medium | TFT mất cạnh ở biên pixel, dùng chung zoom, hiển thị rate cũ | So predecessor, view riêng, cap_rate thực; pixel test + USB tour |
| Low | Reset USB làm tty biến mất trước khi UF2 mount | Đợi mount có giới hạn; reset/reflash/reconnect thật |

Log trước sửa được giữ trong results. Rủi ro tìm qua source không được mô tả như
một lỗi đã tái hiện trên silicon nếu chưa có phép đo đó.

## 4. Việc để phiên sau, theo thứ tự ưu tiên

### P0 — Front-end, bảo vệ và BOM

Đây là phần đang ngăn kết luận đạt toàn bộ đề tài:

1. Vẽ schematic/netlist **as-built 8 kênh**, xác định đúng linh kiện đã lắp.
2. Chốt threshold, điện trở/điện dung đầu vào, bandwidth và mức clamp. Đối chiếu
   datasheet đúng part number, pulse rating và đường dòng về rail.
3. Kiểm tradeoff bảo vệ/băng thông: ví dụ R47 kΩ với tổng C giả sử 10 pF cho
   RC470 ns, f(-3 dB) khoảng339 kHz. Clock lấy mẫu25 MHz không chứng minh front-end25 MHz.
4. Đo 12 V DC và xung200 V bằng fixture phù hợp, ghi pulse width, source impedance,
   Vclamp, dòng, năng lượng và hoạt động trước/sau. Không đưa cao áp trực tiếp vào GPIO.
5. Lập BOM hiện tại với quantity và giá có nguồn. Plan cũ dùng4 kênh/OLED; cộng
   các hàng có thể tới453.000 VND, chưa chứng minh bản8 kênh/TFT ≤450.000 VND.

**Tiêu chí xong:** E1/E2/E3 có bằng chứng đo/tính đủ, không đổi verdict bằng cách
sửa chữ trong báo cáo. Tín hiệu3,3 V đã thử không chứng minh chịu5 V/12 V/200 V.

### P1 — Đủ tám ngõ vào và reference timing

- Nối loopback ESP23–25 hoặc nguồn MISO phù hợp để CH6 có cạnh; chạy lại B3/B8.
- Dùng nguồn cạnh chung và nguồn riêng từng kênh để đo mapping, skew và crosstalk.
  Cùng lệnh PIO IN không có nghĩa pad/front-end có skew bằng0.
- Đối chiếu với scope/reference đã hiệu chuẩn; hiện mới đo tương đối với ESP32.
- Chốt ngữ nghĩa pre=0/rising: matcher hiện coi mức trước sample0 là low. Nên quyết
  định giữ quy ước này hay chỉ bắt cạnh khi thật sự có hai mẫu; thêm test riêng.
- Không suy từ100 lượt thành công rằng mọi phase/tín hiệu đều không thể deadlock.

### P1 — Decoder và error semantics

- **CAN on-device:** chưa kiểm CRC/stuffing/EOF đầy đủ; cần spec và corpus cho
  DLC sai, frame bị cắt, extended và RTR. Text CAN hiện không là chứng nhận frame hợp lệ.
- **UART:** firmware hỗ trợ8N1; parity,7/9 bit, inversion và error annotation cần
  thêm hoặc ghi rõ không hỗ trợ. Không chỉ bổ sung menu nếu thuật toán chưa xử lý.
- **SPI:** on-device đang mode0/MOSI. Muốn thêm CPOL/CPHA, bit order, word length,
  MISO và chính sách CS phải có oracle test tương ứng.
- **I2C:** thêm repeated START, NAK, clock stretching,10-bit address và stuck-bus corpus.
- **AUTO:** là heuristic với cửa sổ32768 mẫu và packet cap64. Score không phải xác
  suất đúng; cần thêm trường hợp nhiều bus nhập nhằng và chính sách từ chối rõ.

### P1 — Streaming và recovery

- Giữ demo ở1 MHz RLE và raw500 kHz. Dữ liệu dày/raw1 MHz đã vượt thông lượng thực.
- Muốn tăng tốc phải profile USB/CPU và đếm mẫu thật. Chỉ chuyển DMA chained/ping-pong
  khi chứng minh ISR là nút thắt; chưa cần thay toàn bộ kiến trúc.
- UI hiện hoãn redraw/điều khiển khi stream. Nếu muốn live status hoặc STOP bằng
  nút, cần budget và end-of-stream contract rõ, kèm test không làm mất mẫu.
- Send deadline có thể cắt dở frame: host phải INVALID và HELLO recovery. Có thể
  thêm transport-abort/sticky fault để device kết thúc rõ và giữ diagnostic counters.
- Đã sửa vòng PIO giới hạn2^32, nhưng chưa soak thực hơn71,6 phút. Unit offset wrap
  và soak5 phút qua nhiều sequence wrap không thay phép thử dài ấy.
- Muốn kết luận không leak MCU cần heap/stack watermark, không chỉ RSS của PC.

### P2 — Driver, GUI, storage và maintainability

- Tách hàm trong `main.c` khi có nhu cầu cụ thể, tránh refactor hàng loạt trước demo.
- Driver CLI công bố max131072 khi trigger nên có thể từ chối yêu cầu262144 trước ARM;
  Python/firmware trực tiếp có clamping. Cần mô tả sự khác nhau cho người dùng.
- Duy trì patch libsigrokdecode và regression parallel khi cập nhật vendor; nên
  chuẩn bị gửi upstream. Không tự update thư viện sát buổi trình diễn.
- Chỉ đóng GUI cũ do chính demo tạo; không kill session khác của người dùng.
- Chưa có nghiệm thu Windows/macOS; Linux PASS không chứng minh đa nền tảng.
- Test save/erase/power-loss trên slot chuyên dụng. Phiên này chỉ read-only vì hai
  slot có dữ liệu; chưa có CRC/journal cho session flash.

### P2 — TFT và nút

- Chụp camera panel và kiểm tiếp điểm sau khi hàn nút GP8/9/12/22. USB queue không
  thay cho kiểm điện/tiếp điểm/debounce của nút thật.
- Cân nhắc nhãn samples/pixel rõ hơn; `Xn` dễ bị hiểu là hệ số phóng đại.
- Định nghĩa quyền sở hữu CONFIG giữa PC và board trước khi đồng bộ preset.
  Hiện CONFIG là lần thu on-device tiếp theo; WAVE/DECODE dùng capture vừa có.
- Tránh để UI capture/flash và PulseView cùng tranh cổng USB.

## 5. Quy trình bàn giao gọn

Đọc TESTING, TEST-REPORT và LATEST; chọn một mục có tiêu chí định lượng, sửa tối
thiểu và chạy offline/test nhóm liên quan. Nếu sửa firmware: build, flash và thử
hardware. Chạy full gate khi logic đã chốt; cuối cùng chạy demo2 giây/màn với hai
board thật, cập nhật báo cáo rồi commit/push `feature/trigger-v2`.

Không mở thêm hạng mục phát triển trong phiên hiện tại. Ưu tiên giữ bản demo đã
chạy thành công cho buổi trình bày ngày mai.
