# TEST PLAN v2 — RP2350 Logic Analyzer, 18/11/2026

## 1. Phạm vi và nguyên tắc nghiệm thu

Đánh giá firmware Pico SDK, decoder C thực tế, LAR2, host Python, driver libsigrok,
PulseView custom, TFT và ESP32 generator. Không thay kiến trúc PIO/DMA, không thêm
framework. Source vendor/SDK được pin theo `sources.lock.json`; review tập trung vào
mã do dự án duy trì. HEAD đầu phiên thực tế là `460d90e`, không phải HEAD lịch sử
`576873a` trong handoff. Các tối ưu chưa commit được ghi bằng SHA256 source/binary.

`PASS` chỉ cho phép thử đã thực thi và đạt điều kiện đã nêu. `FAIL` là kết quả đã
đo vi phạm tiêu chí. `BLOCKED`/`NOT-VERIFIED` là thiếu thiết bị, dây, nguồn chuẩn,
BOM hoặc chứng cứ. Không đổi FAIL thành PASS bằng cách bỏ test. Các phép thử giới
hạn/cố ý gây lỗi PASS khi lỗi được phát hiện và recovery đúng, không gọi file mất
mẫu là capture hợp lệ.

**Phân biệt:** exit 0 của runner = không có FAIL trong các phép thử đã chạy;
BLOCKED vẫn xuất hiện riêng và ngăn kết luận sản phẩm đạt toàn bộ đề tài. Không
có chứng nhận tiêu chuẩn công nghiệp cụ thể chỉ từ bộ test này.

## 2. Kiến trúc và bằng chứng

- `tests/run_qa.py`: runner standard library; mặc định full bench khi có board,
  `--offline` chạy nhóm A/PTY; `--soak-seconds 300` mặc định cho C3.
- `tests/qa_*.py`: vector độc lập, bridge gọi decoder C đang dùng trên Pico,
  sigrok oracle, đo mẫu, thiết bị/stream/GUI và đánh giá phần cứng.
- `tests/test_qa_offline.sh`: một lệnh offline, không cần mạng hay board.
- Mỗi run tạo `results/qa-v2-YYYYMMDD-HHMMSS/`, chứa manifest source/binary,
  summary JSON/CSV, log theo case, `.sr`, số đo và ảnh. Không ghi đè run trước.
- Mỗi case ghi ID, mục tiêu, điều kiện, kỳ vọng định lượng, thực tế, evidence,
  verdict và thời gian. `docs/TEST-REPORT.md` là báo cáo tổng hợp kết quả thật.
- Oracle sigrok và decoder firmware phải đọc **cùng dãy byte mẫu** trong `.sr`.
  Python không có API upload buffer tùy ý lên Pico: offline dùng bridge C liên kết
  đúng `firmware/decoders.c`, không gọi `decode_all()` rồi giả vờ đọc file trên board.
- `.sr` có 8 probe CH0..CH7, unit 1 byte. CRC transport chỉ chứng minh truyền đúng
  byte; không chứng minh clock tuyệt đối hay CAN CRC nếu decoder không kiểm CRC.

## 3. Môi trường và điều kiện tiên quyết

Ubuntu/X11 `DISPLAY=:0`, Python 3, Pillow/pyserial đã cài, C compiler, host stack
đã build; tất cả chạy offline. Pico serial `907CBC57E4828DD6`; tự tìm USB by-id để
không phụ thuộc tên ttyACM có thể đổi sau flash. ESP32 `/dev/ttyUSB0`, 115200.
Giữ một console ESP32 mở trong mỗi nhóm; đợi boot trước khi gửi lệnh, kiểm echo.
Đóng session serial khác; runner/demo chỉ đóng cửa sổ PulseView do chính demo tạo.

Dây hiện tại: CH0=ESP26 UART; CH1/2=18/19 I2C; CH3/4/5/6=21/22/23/25 SPI;
CH7=32 CAN TX; chung GND. CH6/MISO cần jumper 23–25 nếu muốn loopback. Không tự
nhận mọi kênh đã được kích bằng nguồn ngoài khi thiếu dây này.

ESP32 tối ưu có `stress <hz>|off` đưa PWM 50% vào GPIO26/CH0, tạm ngừng UART.
B1 dùng nguồn này để không phải chuyển dây sang SQ/GPIO4; vẫn gửi `freq 1000`
nhưng không nhận GPIO4 là đã đo nếu không nối. Đây là phép đối chiếu với LEDC
ESP32, **không** phải hiệu chuẩn độc lập bằng oscillator/oscilloscope đã hiệu chuẩn.

## 4. Ma trận và tiêu chí

| ID | Phạm vi | Tiêu chí PASS / cách đo |
|---|---|---|
| A1 | `.sr` tổng hợp độc lập UART/I2C/SPI/CAN | Oracle sigrok trả đúng từng byte, address/ID/DLC và payload; log từng annotation |
| A2 | Decoder C trên đúng các file A1 | Packet C khớp từng byte/field với oracle; không chỉ so substring một frame |
| A3 | Stop/parity sai, hằng 0/1, empty, nhiễu 1–2 mẫu, SPI clock không đều, CAN sai baud | ASan/UBSan không lỗi; mỗi input lặp lại cho cùng output; timeout mỗi process; khả năng parity được đánh giá riêng vì firmware chỉ 8N1 |
| A4 | UART autobaud nhiều baud/rate fractional | Suy bit width Q16 qua baud thực được decoder C xuất; lệch <2%; lưu CSV |
| A5 | AUTO + moved channels + noise | Đúng protocol/map trên vector đủ thông tin; noise/idle không được đoán bus |
| B1 | Square 1 kHz, 1/5/25 MHz | Sai số chu kỳ trung bình so LEDC <0,5%; duty, min/max period, jitter peak-to-peak/RMS lưu CSV |
| B2 | UART115200, 1/5/25 MHz | Bit khoảng 8,68056/43,40278/217,01389 mẫu; sai số <2%; tại25 MHz sai số mean <1 mẫu |
| B3 | Edge rising/falling CH0..7,25 MHz,65536/pre32768 | index32768±1 và bit trước/sau đúng; kênh không có cạnh bị BLOCKED, không PASS |
| B4 | Pattern match/never-match + manual | Match đúng value/mask; never-match timeout hữu hạn, HELLO/ABORT phục hồi và capture tiếp đạt |
| B5 | pre0/half/>half, count262144+trigger | count thực131072; pre được clamp≤half; không deadlock; INFO đúng |
| B6 | Trigger100 ms,100 lần | Đủ100 capture, đúng trigger/data/sequence; mỗi lần<1s tính từ ARM (mạnh hơn chỉ đo sau event) |
| B7 | 4 protocol×1/5/25 MHz | Đủ nội dung/byte; SPI hạ100 kHz khi sample1 MHz để tránh alias; CAN250k dùng4sample/bit ở1 MHz |
| B8 | Tất cả8 ngõ có nguồn riêng, mapping/crosstalk/skew | Mỗi kênh phải có xung biết trước; thiếu wiring/nguồn chung cạnh thì BLOCKED; không suy skew từ PIO cùng bank |
| C1 | 1 MHz RLE10s | 0 drop, sender=receiver, flags0, số mẫu/device duration lệch<0,1%; oracle4 bus |
| C2 | raw500 kHz5s | 0 drop, counters khớp, UART đúng |
| C3 | RLE1 MHz300s liên tục | 0 drop, không sequence/CRC/fault; memory host bounded; `.sr` đọc được; không suy SRAM leak chỉ từ RAM host |
| C4 | SIGINT + kill host | SIGINT giữ tail/counters; sau kill HELLO và capture mới đúng, không treo |
| C5 | Host stall/UI | Stall có thể làm mất mẫu nhưng phải có counter/INVALID; sau drain/HELLO các phím và TFT vẫn đáp ứng; không claim redraw live trong stream |
| C6 | Stream2 MHz/5 MHz | Host từ chối trước khi ARM hoặc thiết bị ERROR rõ; không tạo file VALID sai |
| D1 | Driver `--show` | Tên board đúng, CH0..7, đủ8rate từ1 kHz đến25 MHz |
| D2 | Driver triggered snapshot |65536/pre50%, đủ4 decoder, CRC xác minh, trigger source/slope được đọc lại |
| D3 | GUI live | PulseView tự capture qua driver,8waveform, annotation4 bus visible, rate/count/source/slope đúng; chụp Qt và X11 root; thiếu GUI config là PARTIAL |
| D4 | `.sr` metadata |8probe đúng tên/bit, samplerate, length/unitsize1 byte, mở bằng hoststack được |
| E1 | Schematic/protection12V/200V | Phải có netlist/định mức/điều kiện xung và đo thực; tài liệu đề xuất không là chứng cứ; thiếu =>NOT-VERIFIED |
| E2 | BOM≤450.000VNĐ | Có bảng linh kiện/quantity/giá có nguồn và tổng; thiếu giá =>NOT-VERIFIED |
| E3 | Độ phân giải, buffer, tải,3.3V/5V |40 ns@25 MHz; manual262144,trigger≤131072; cửa sổ65536=2,62144 ms; impedance/capacitance/5V chưa đo ghi riêng |
| F1 |50snapshot liên tiếp | Không lỗi CRC/seq/length; đo thời gian/RSS; không gọi là chứng minh tuyệt đối không leak MCU |
| F2 | Reset/disconnect | Driver phát hiện session hỏng; nạp đúng firmware cũ và reconnect/capture thành công; không đòi tay rút dây nếu không khả thi |
| F3 | Gate cũ+mới | `scripts/test.sh`, ESP32link, fullQA: không FAIL; liệt kê riêng mọi BLOCKED/NOT-VERIFIED |

### Các điểm không được diễn giải sai

B2 trong brief có typo `8680 mẫu @1 MHz`: đúng là **8,68056 mẫu** (8680 ns), không
phải8680sample. Một bit25 MHz≈217,014sample; cần nhiều bit để lấy mean. Quantization
jitter±1sample khác với jitter nguồn/clock; không tách được hai thành phần nếu
thiếu reference ngoài.

1 MHz RLE chỉ có thể lossless nếu dữ liệu nén đủ. Tín hiệu dày làm RLE fallback raw
có thể vượt USB FS thực tế; QA đo thêm capacity và giữ trạng thái INVALID nếu có
mất mẫu. Không quảng cáo 1 MHz raw lossless. Test5 phút C3 dùng traffic bus thực.

UART firmware hiện8N1; parity validation không được coi đã hỗ trợ. CAN firmware
hiện standard11-bit, chưa kiểm CRC như oracle sigrok; phải ghi giới hạn ấy. SPI
firmware mode0/MOSI; MISO/CPOL/CPHA khác cần nghiệm thu riêng.

`topic2-upgrade.md` là plan lịch sử: chưa có RTOS trong Pico hiện tại, `0x06` là
ANALYZE, không phải CONFIG_TRIGGER; SRAM520 KiB chứa262144 **mẫu8bit**, không phải
chia tiếp cho8 lần nữa. Không lấy các câu “protection đã có” trong plan làm PASS E1.

## 5. Lịch chạy và rủi ro

P0 inventory nguồn và log hiện tại → P1 plan này → P2/A offline mới → P3 sửa defect
có regression → P4/B,D hardware → P5/C soak và recovery,F → P6/E tài liệu + report
→ P7 gate cuối → P8 commit/push. Chạy serial theo thứ tự để tránh tranh cổng;
soak300s không chạy acquisition khác đồng thời. Demo thực hiện **cuối cùng**, dwell
TFT2 giây mỗi trang, để giữ PulseView mở sẵn cho buổi trình bày.

Tổng dự kiến15–30 phút sau khi harness sẵn, tùy defect. Không dùng internet cho test.
Không đưa12V/200V vào GP0..7 khi chưa xác minh front-end. Đầu vào trực tiếp RP2350
không được mặc định là5V tolerant. Bằng chứng ảnh TFT là framebuffer gửi panel;
không thay cho camera kiểm backlight/màu/tiếp điểm nút vật lý.
