# Hướng dẫn custom PulseView cho Logic Analyzer Raspberry Pi Pico 2 (RP2350)

**Mục tiêu:** để PulseView nhận mạch logic analyzer do nhóm xây dựng trên Pico 2, cấu hình capture và hiển thị 8 kênh dữ liệu ở 25 MHz hoặc cao hơn.  
**Hướng triển khai:** custom tầng **`libsigrok` driver trước**, giữ giao diện PulseView nguyên bản ở giai đoạn đầu. Chỉ fork PulseView khi cần chức năng giao diện mà nó chưa có.

> Tài liệu này dùng khái niệm “PulseView custom” theo nghĩa: thiết bị RP2350 xuất hiện, capture và decode được trong PulseView. Không cần sửa mã nguồn GUI để đạt mục tiêu đó.

## 1. Kiến trúc cần đạt

```text
             USB CDC (dễ làm) hoặc USB vendor class (mở rộng)
┌─────────────────────┐  binary protocol  ┌─────────────────────────┐
│ Pico 2 / RP2350     │ ◄───────────────► │ libsigrok driver        │
│                     │                   │ tên: rp2350-la          │
│ PIO + DMA           │                   └────────────┬────────────┘
│ 8 GPIO → buffer     │                                │ logic datafeed
│ trigger + metadata  │                                ▼
└─────────────────────┘                         ┌─────────────┐
                                                │ PulseView   │
                                                │ waveform    │
                                                │ cursor      │
                                                │ UART/SPI/I²C│
                                                └─────────────┘
```

PulseView là frontend; `libsigrok` phụ trách driver, giao tiếp phần cứng, định dạng file và đưa mẫu logic tới frontend. Vì thế viết driver là cách tích hợp chuẩn. PulseView có thể giữ nguyên chức năng sẵn có: zoom, pan, cursor, lưu session `.sr` và protocol decoder.

## 2. Quyết định thiết kế trước khi viết code

| Quyết định | Chọn cho bản đầu | Lý do |
|---|---|---|
| Hình thức capture | Capture vào buffer của Pico, sau đó gửi cho PC. | Bài tập không bắt buộc realtime và cách này không phụ thuộc vào băng thông truyền liên tục. |
| Số kênh | 8 kênh logic, `CH0`…`CH7`. | Đáp ứng mức tối thiểu Topic 2; 1 sample đóng gói đúng 1 byte. |
| Tốc độ mẫu | 25 MHz cố định ở mốc đầu; sau đó thêm danh sách tốc độ hỗ trợ. | Dễ kiểm tra timing và giảm số biến cần debug. |
| Transport | USB CDC trước. | TinyUSB hỗ trợ sẵn ví dụ USB device cho Pico SDK; CDC giúp debug bằng serial terminal. |
| Payload | Binary, không phải text/CSV. | 8 kênh × 25 MHz tạo 25 MB/s dữ liệu thô. Text làm lượng dữ liệu tăng mạnh và không phù hợp. |
| Trigger v1 | Manual capture, sau đó trigger cạnh lên/xuống. | Chia nhỏ rủi ro: waveform/timing phải đúng trước trigger pattern. |
| Driver | `libsigrok` driver có tên `rp2350-la`. | PulseView sẽ nhận thiết bị theo cách tự nhiên; decoder có sẵn được dùng lại. |

## 3. Chia thành ba phần độc lập

| Phần | Nhiệm vụ | Khi nào hoàn thành |
|---|---|---|
| Firmware RP2350 | Capture đồng bộ bằng PIO/DMA, quản lý buffer/trigger và giao thức USB. | PC nhận được capture binary có số mẫu đúng. |
| Driver `libsigrok` | Cấu hình thiết bị, gửi lệnh, đọc capture, tạo logic datafeed. | `sigrok-cli` tạo được file `.sr` từ Pico. |
| PulseView | Chọn driver và hiển thị capture. | Mở trực tiếp thiết bị hoặc file `.sr`, thấy 8 waveform và decoder. |

Không bắt đầu bằng sửa GUI PulseView. Nếu firmware hoặc protocol sai, sửa GUI không giúp tìm lỗi; `sigrok-cli` là công cụ kiểm tra driver gọn hơn PulseView.

## 4. Bước 1 — Xác định hợp đồng dữ liệu Pico ↔ PC

### 4.1. Quy ước mẫu logic

Mỗi byte là trạng thái tám input tại **một** thời điểm sample:

| Bit payload | Kênh trong PulseView |
|---:|---|
| bit 0 | CH0 |
| bit 1 | CH1 |
| bit 2 | CH2 |
| bit 3 | CH3 |
| bit 4 | CH4 |
| bit 5 | CH5 |
| bit 6 | CH6 |
| bit 7 | CH7 |

Quy ước này phải cố định trong schematic, firmware, driver và tài liệu test. Nếu GPIO vật lý không liền nhau, firmware phải remap về thứ tự bit trên trước khi gửi hoặc driver phải khai báo đúng mapping.

### 4.2. Frame transport

Không gửi một capture như một gói USB rất lớn. Chia thành frame, mỗi frame có header kiểm tra được:

| Trường | Kiểu | Mô tả |
|---|---:|---|
| `magic` | 4 byte ASCII | `LAR2` để đồng bộ đầu frame. |
| `version` | `uint8` | Phiên bản protocol, bắt đầu bằng `1`. |
| `type` | `uint8` | Lệnh, trả lời, metadata, hoặc sample chunk. |
| `sequence` | `uint16` | Số thứ tự frame, phát hiện mất frame. |
| `payload_length` | `uint32` little-endian | Số byte payload của frame. |
| `payload` | biến đổi | Nội dung theo `type`. |
| `crc32` | `uint32` | CRC của header và payload; bản đầu có thể tắt bằng cờ nếu chưa cần. |

Nên giới hạn `payload_length` ở 1024 hoặc 4096 byte để dễ đệm, recover và log lỗi.

### 4.3. Lệnh tối thiểu

| Hướng | `type` | Payload | Kết quả mong đợi |
|---|---|---|---|
| PC → Pico | `HELLO` | rỗng | Pico trả version firmware và ID board. |
| PC → Pico | `GET_CAPS` | rỗng | Trả số kênh, sample-rate hỗ trợ, buffer tối đa, trigger hỗ trợ. |
| PC → Pico | `CONFIG` | sample rate, số mẫu, số mẫu pre-trigger, mask kênh, trigger mode | Pico kiểm tra tham số và trả `OK`/`ERROR`. |
| PC → Pico | `ARM` | rỗng | Pico bắt đầu capture. |
| PC → Pico | `ABORT` | rỗng | Pico dừng capture và giải phóng trạng thái. |
| Pico → PC | `CAPTURE_INFO` | `sample_rate_hz`, `sample_count`, `trigger_index`, `channel_mask`, `sample_unit_size` | Có trước sample chunk. |
| Pico → PC | `SAMPLES` | Dãy byte sample theo thứ tự thời gian | Có thể có nhiều frame liên tiếp. |
| Pico → PC | `CAPTURE_END` | tổng byte hoặc CRC toàn capture | PC xác nhận capture hoàn chỉnh. |
| Pico → PC | `ERROR` | error code + text ngắn | Driver hủy acquisition với lỗi rõ ràng. |

`CAPTURE_INFO` là bắt buộc. Driver không được đo thời gian dựa trên lúc USB nhận data; timing của waveform luôn lấy từ `sample_rate_hz` và index mẫu.

### 4.4. Capture metadata đề xuất

```c
// Tất cả số nguyên little-endian; chỉ là đặc tả protocol, không dùng
// trực tiếp struct này nếu compiler có thể tự padding.
struct capture_info_v1 {
    uint64_t sample_rate_hz;  // ví dụ 25000000
    uint32_t sample_count;    // số byte sample khi dùng 8 kênh
    uint32_t trigger_index;   // index mẫu trigger; 0xffffffff = không trigger
    uint32_t pretrigger_count;
    uint8_t  channel_count;   // 8
    uint8_t  sample_unit_size;// 1
    uint8_t  channel_mask;    // 0xff nếu 8 kênh đều bật
    uint8_t  flags;           // bit 0: capture-overflow, bit 1: trigger-found...
};
```

## 5. Bước 2 — Viết firmware Pico 2

### 5.1. Nền tảng

1. Tạo project bằng Pico SDK, đặt board là `pico2` hoặc platform là `rp2350`.
2. Lấy một ví dụ TinyUSB device từ `pico-examples` và xác nhận PC nhận được cổng CDC trước khi thêm logic analyzer.
3. Đặt USB, PIO/DMA capture và giao thức điều khiển vào các module riêng: `usb_transport`, `la_capture`, `la_protocol`, `main`.

Pico SDK tích hợp TinyUSB cho USB device; ví dụ của Raspberry Pi có target USB device cho RP2350. Không dùng USB để lấy timing sample: USB chỉ dùng để chuyển capture đã có.

### 5.2. Capture bằng PIO + DMA

1. Chọn tám GPIO input và xác định rõ điện áp logic ở phần hardware/protection.
2. Cấu hình PIO đọc tám pin trong cùng một lệnh sample; dùng clock divider để tạo rate 25 MHz.
3. Dùng DMA chuyển FIFO của PIO vào buffer RAM. CPU không đọc GPIO bằng vòng lặp vì timing và jitter không được kiểm soát.
4. Lưu `sample_count` thực tế, cờ DMA overflow/FIFO overflow và index trigger.
5. Sau khi capture hoàn tất, gửi `CAPTURE_INFO`, rồi nhiều frame `SAMPLES`, cuối cùng `CAPTURE_END`.

Ở 25 MHz, độ phân giải thời gian danh nghĩa là **40 ns/sample**. Đây là độ phân giải của dữ liệu; sai số thực tế còn gồm jitter clock, đồng bộ input, input buffer và mạch bảo vệ. Các thành phần đó cần được trình bày trong báo cáo timing.

### 5.3. Trigger theo mốc

| Phiên bản | Chức năng firmware | Giá trị `trigger_index` |
|---|---|---|
| v0 | Capture manual sau lệnh `ARM`. | `0xffffffff` |
| v1 | Trigger cạnh lên/xuống của một kênh. | Index mẫu đầu tiên thỏa điều kiện. |
| v2 | Trigger pattern: `(sample & mask) == value`. | Index mẫu thỏa pattern. |
| v3 | Pre-trigger circular buffer. | Tính chính xác vị trí trigger trong payload sau khi xoay buffer. |

Chỉ công bố trigger trong PulseView khi `trigger_index` và sample order đã được test đúng. Một trigger sai làm toàn bộ đánh giá timing mất tin cậy.

### 5.4. Firmware test trước khi làm driver

Tạo chế độ test nội bộ, không cần tín hiệu ngoài:

| Test | Tín hiệu | Điều kiện đạt |
|---|---|---|
| T0 | Mẫu đếm 8-bit hoặc pattern `0x55`, `0xaa`. | PC nhận đúng thứ tự bit và không mất byte. |
| T1 | Một GPIO toggle chậm. | Waveform có mức cao/thấp đúng, không đảo bit. |
| T2 | PWM có period biết trước. | Số sample mỗi period khớp period × sample rate. |
| T3 | UART từ một MCU khác. | Byte và timing có thể xác minh bằng logic analyzer/oscilloscope tham chiếu. |
| T4 | SPI/I²C. | Kênh mapping và cạnh clock đúng. |

## 6. Bước 3 — Tạo driver `libsigrok`

### 6.1. Chọn transport cho driver

| Cách | Khi nên dùng | Ưu / nhược điểm |
|---|---|---|
| USB CDC / serial | Bản đầu và demo môn học. | Firmware và debug đơn giản; driver được truyền rõ cổng `/dev/ttyACM*` hoặc `COM*`. Không tự scan cổng một cách đáng tin cậy. |
| USB vendor class + libusb | Khi cần thiết bị tự nhận dạng bằng VID/PID và control/bulk endpoint. | Tích hợp “chuyên nghiệp” hơn; cần USB descriptor, quyền truy cập driver trên Windows/Linux và code phức tạp hơn. |

Khuyến nghị: làm CDC trước. Driver nhận `conn` do người dùng chỉ định, sau đó mới chuyển sang USB vendor class nếu thời gian cho phép.

### 6.2. Các capability tối thiểu phải expose

Driver `rp2350-la` cần cho libsigrok/PulseView biết:

- 8 logic channels tên `CH0` đến `CH7`.
- Connection string: serial path ở bản CDC.
- Sample rate: đầu tiên chỉ `25000000`; sau khi test mới khai báo rate khác.
- Sample limit: số mẫu maximum có thể capture.
- Cho phép bật/tắt kênh; ở payload v1 vẫn có thể gửi đủ 8 bit.
- Acquisition start/stop.

`libsigrok` yêu cầu driver đăng ký cấu trúc `sr_dev_driver`, trong đó có các callback scan, open/close, config và start/stop acquisition. Với serial device, phần scan thường cần connection string do người dùng cung cấp.

### 6.3. Luồng acquisition trong driver

```text
PulseView / sigrok-cli
   │ config: sample rate, sample count, trigger
   ▼
rp2350-la driver → HELLO → GET_CAPS → CONFIG → ARM
   │
   ├─ nhận CAPTURE_INFO
   ├─ gửi SR_DF_HEADER vào libsigrok session
   ├─ mỗi SAMPLES chunk: gửi SR_DF_LOGIC, unitsize = 1
   └─ nhận CAPTURE_END: gửi SR_DF_END
```

Sample datafeed cần giữ đúng thứ tự thời gian và dùng `unitsize = 1`, vì mỗi sample có tám bit logic. `sample_rate` trong libsigrok được biểu diễn theo Hz; với mốc đầu là `25000000`.

### 6.4. Bố cục mã nguồn gợi ý

```text
third_party/libsigrok/
  src/hardware/rp2350-la/
    api.c          # đăng ký driver, scan/open/close/config
    protocol.c     # encode lệnh và parse frame LAR2
    acquisition.c  # state machine, nhận chunk, tạo datafeed
    protocol.h

firmware/
  src/
    la_capture.c
    la_protocol.c
    usb_transport.c
    main.c
  pio/
    logic_capture.pio
```

Tên file/build target có thể khác theo phiên bản `libsigrok`; bổ sung driver vào danh sách build của phiên bản nhóm đã pin. Không copy cấu hình build từ tutorial cũ mà không kiểm tra README của commit đang dùng.

### 6.5. State machine khuyến nghị

| State | Sự kiện hợp lệ | Hành động |
|---|---|---|
| `DISCONNECTED` | `open` | Mở CDC/libusb, gửi `HELLO`. |
| `IDLE` | `config`, `start` | Cache config, gửi `CONFIG` và `ARM`. |
| `WAIT_INFO` | `CAPTURE_INFO` | Kiểm tra metadata, khởi tạo session datafeed. |
| `RECEIVING` | `SAMPLES` | Kiểm `sequence`, length, CRC; gửi logic chunk. |
| `RECEIVING` | `CAPTURE_END` | Kiểm tổng số mẫu, gửi end và về `IDLE`. |
| Bất kỳ | timeout / `ERROR` / disconnect | Hủy acquisition, báo lỗi có nguyên nhân. |

Không cấp phát buffer lớn bằng kích thước do Pico gửi trước khi kiểm tra giới hạn. Driver xử lý theo chunk; chỉ dùng bộ đệm đủ một frame transport.

## 7. Bước 4 — Build và test driver trước PulseView

1. Pin version của `libsigrok`, `libsigrokdecode`, `sigrok-cli` và PulseView trong file `THIRD_PARTY.md` hoặc README của nhóm.
2. Build toàn bộ stack sigrok từ cùng một prefix phát triển để tránh PulseView nạp nhầm `libsigrok` của hệ điều hành.
3. Chạy `sigrok-cli` trước. Với CDC, ví dụ lệnh kiểm thử sẽ có dạng:

```bash
sigrok-cli --driver=rp2350-la:conn=/dev/ttyACM0 --show
sigrok-cli --driver=rp2350-la:conn=/dev/ttyACM0 \
  --config samplerate=25m:limit_samples=100000 \
  --output-file capture.sr
```

Tên chính xác của config option do driver nhóm khai báo; hai lệnh trên là mục tiêu về trải nghiệm, không phải lệnh có sẵn trước khi driver được viết.

4. Mở `capture.sr` bằng PulseView. Nếu file hiển thị đúng, lỗi còn lại (nếu có) chỉ là kết nối trực tiếp/thiết lập GUI, không phải PIO/DMA payload.
5. Khi `sigrok-cli` ổn, mở thiết bị trong PulseView, ví dụ:

```bash
pulseview -d rp2350-la:conn=/dev/ttyACM0
```

Lưu log driver với mức debug trong giai đoạn này. Cần log frame type, sequence, sample count, trigger index và error code; không log từng sample.

## 8. Bước 5 — Dùng decoder và kiểm chứng timing

1. Capture UART; thêm UART decoder trong PulseView, map RX/TX và chọn baud/parity/stop-bit chính xác.
2. Capture SPI; map CLK/MOSI/MISO/CS, chọn sampling edge và bit order.
3. Capture I²C; map SCL/SDA, quan sát start/address/data/ACK.
4. Đặt cursor A/B trên hai cạnh đã biết và xác nhận:

```text
delta_t = (sample_index_B - sample_index_A) / sample_rate_hz
```

Ví dụ, chênh 25 sample ở 25 MHz phải hiện gần 1 µs. Kết quả này phải được đối chiếu với tín hiệu test có period/tần số biết trước.

## 9. Khi nào mới sửa PulseView GUI

Sau khi driver đã capture đúng, có thể fork PulseView để thêm một hoặc nhiều mục sau:

| Nhu cầu | Cách custom hợp lý |
|---|---|
| Chọn nhanh Pico 2 LA | Thêm profile/config preset cho driver `rp2350-la`. |
| Hiển thị trạng thái hardware | Thêm panel chỉ báo buffer fill, firmware version, trigger found và overflow flag. |
| Cấu hình riêng | Form cho sample count, pre-trigger ratio, edge/pattern trigger; form gọi `config_set` của driver. |
| Đặt tên sản phẩm / logo | Fork PulseView và sửa branding, nhưng ghi rõ phần mềm dựa trên PulseView/sigrok. |
| Decoder protocol mới | Thường viết decoder Python cho `libsigrokdecode`, không cần sửa GUI. |

Tránh nhúng USB protocol trực tiếp vào PulseView. PulseView cần giao tiếp qua `libsigrok`; nếu bỏ qua lớp này, nhóm phải tự duy trì tất cả xử lý driver và mất khả năng dùng `sigrok-cli` để test.

## 10. Milestone triển khai đề xuất

| Mốc | Đầu ra demonstrable | Không được chuyển mốc khi |
|---|---|---|
| M0 | Pico 2 xuất hiện như USB CDC, trả `HELLO`. | PC terminal nhận đúng firmware version. |
| M1 | Firmware capture manual 8 kênh, gửi binary có metadata. | T0–T2 của mục 5.4 đều đạt. |
| M2 | Driver đọc Pico, `sigrok-cli` tạo file `.sr`. | Sample count và sample rate của file đúng. |
| M3 | PulseView hiển thị 8 kênh, cursor đo đúng 1 µs ở test 25 sample. | Timing có đối chiếu tín hiệu chuẩn. |
| M4 | Trigger cạnh + UART decode. | Trigger index và byte UART đúng nhiều lần liên tiếp. |
| M5 | SPI/I²C, trigger pattern/pretrigger hoặc UI custom. | Chỉ làm sau khi M0–M4 ổn định. |

## 11. Các lỗi thường gặp

| Hiện tượng | Nguyên nhân hay gặp | Cách kiểm tra/sửa |
|---|---|---|
| Waveform nhanh/chậm sai | Driver dùng sai `sample_rate_hz`. | Log metadata, dùng 25 MHz và PWM period biết trước để test. |
| Kênh bị đảo/thay vị trí | Bit mapping GPIO ↔ payload ↔ channel không thống nhất. | Gửi pattern đếm và lập bảng mapping một lần. |
| Mất dữ liệu cuối capture | USB buffer đầy hoặc kết thúc DMA/USB sai thứ tự. | So sánh `sample_count`, byte đã nhận và CRC tổng. |
| PulseView không thấy Pico | Driver chưa được build/load, sai prefix, hoặc CDC cần truyền `conn`. | Dùng `sigrok-cli --show` trước; kiểm tra serial path và quyền thiết bị. |
| Có waveform nhưng decoder sai | Sai mapping, sampling edge, sample rate hoặc input protection làm méo cạnh. | Đối chiếu bằng protocol test đã biết và kiểm tra hardware. |
| Trigger nằm sai vị trí | Circular buffer chưa xoay đúng hoặc index theo word thay vì theo sample. | Gửi `trigger_index`, kiểm bằng test edge duy nhất. |
| PulseView crash | Driver gửi buffer hết hạn, length sai hoặc datafeed sequence sai. | Dùng buffer sống đến khi callback hoàn tất; kiểm frame length/unitsize. |

## 12. Giấy phép và cách nộp source

PulseView và `libsigrok` dùng GPLv3+. Nhóm được phép sửa và dùng trong bài tập. Nếu phân phối binary PulseView/libsigrok đã sửa cho người khác, cần giữ thông báo bản quyền/giấy phép GPL, ghi rõ thay đổi và cung cấp source tương ứng theo GPL.

Nên nộp cùng đồ án:

- firmware RP2350;
- patch hoặc fork `libsigrok` chứa driver `rp2350-la`;
- patch PulseView nếu có sửa GUI;
- file protocol specification;
- ít nhất một file capture `.sr` và test signal dùng để tái lập demo;
- README nêu version dependency, cách build và lệnh chạy demo.

## 13. Tài liệu gốc cần đọc

- [PulseView](https://sigrok.org/wiki/Pulseview) và [PulseView architecture](https://sigrok.org/wiki/PulseView/Architecture).
- [Sigrok Hardware Driver API](https://www.sigrok.org/wiki/Hardware_driver_API).
- [Sigrok formats and structures](https://sigrok.org/wiki/Formats_and_structures): datafeed logic và đơn vị sample rate.
- [Sigrok protocol decoder API](https://sigrok.org/wiki/Protocol_decoder_API).
- [Sigrok build instructions](https://sigrok.org/wiki/Building).
- [Pico C/C++ SDK](https://datasheets.raspberrypi.com/pico/raspberry-pi-pico-c-sdk.pdf): TinyUSB trong Pico SDK.
- [Raspberry Pi pico-examples](https://github.com/raspberrypi/pico-examples): USB device examples hỗ trợ RP2350; build `-DPICO_BOARD=pico2` chọn platform RP2350.
- [GNU GPLv3](https://www.gnu.org/licenses/gpl.en.html), mục 5–6: điều kiện khi phân phối phiên bản đã sửa.
