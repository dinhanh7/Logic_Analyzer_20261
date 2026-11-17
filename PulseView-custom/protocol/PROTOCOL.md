# LAR2 v1.2 — hợp đồng firmware / libsigrok

Tất cả số nhiều byte đều **little-endian**, không truyền struct C trực tiếp.
USB CDC 115200/8N1, không flow control, DTR=1. Baud chỉ để cấu hình CDC, không quyết định tốc độ mẫu hay tốc độ USB. Không trộn debug text vào CDC.

## Frame

| Offset | Số byte | Trường |
|---|---:|---|
| 0 | 4 | ASCII `LAR2` |
| 4 | 1 | version = 1 |
| 5 | 1 | type |
| 6 | 2 | sequence |
| 8 | 4 | payload_length, 0…1024 |
| 12 | N | payload |
| 12+N | 4 | CRC32(header 12 byte + payload) |

CRC32/ISO-HDLC như `zlib.crc32`: polynomial phản chiếu `0xedb88320`, init `0xffffffff`, xorout `0xffffffff`. Vector ASCII `123456789` → `0xcbf43926`. CRC là bắt buộc; v1 này không có cờ bỏ CRC.

Mỗi hướng có sequence riêng, tăng cho **mọi** frame, quay vòng 65535→0. `HELLO` sequence=0 hủy capture đang chạy, bỏ cấu hình cũ và reset phiên: phản hồi ID sequence=0, lệnh PC kế tiếp sequence=1 và phản hồi thiết bị kế tiếp sequence=1. Không dùng sequence để nối response với request; RPC được tuần tự hóa. Sau lỗi sequence/CRC có thể cần mở phiên HELLO mới.

## Command / response

| Type | Tên | Payload |
|---|---|---|
| `0x01` | HELLO | rỗng |
| `0x02` | GET_CAPS | rỗng |
| `0x03` | CONFIG | 20 byte (v1) / 28 byte (v2), mô tả dưới |
| `0x04` | ARM | rỗng; yêu cầu CONFIG hợp lệ |
| `0x05` | ABORT | rỗng; hợp lệ cả khi đang capture/truyền |
| `0x06` | ANALYZE | 8 byte (op, arg:u16, arg32:u32, reserved:u8), xem dưới |
| `0x81` | ID | chuỗi UTF-8 không NUL, bắt đầu `RP2350-LA` |
| `0x82` | CAPS | số kênh, đơn vị mẫu, tính năng, buffer, danh sách rate |
| `0x83` | ACK | 1 byte: type command vừa xử lý |
| `0x90` | CAPTURE_INFO | 24 byte |
| `0x91` | SAMPLES | 1…1024 byte trạng thái GPIO theo thứ tự thời gian |
| `0x92` | CAPTURE_END | `u32 sample_count`, `u32 crc32(samples)` |
| `0x93` | RESULT | payload tùy loại ANALYZE (kết quả decode, status, v.v.) |
| `0xff` | ERROR | chuỗi lỗi UTF-8 ngắn, không NUL |

HELLO → ID; GET_CAPS → CAPS; CONFIG/ARM/ABORT → ACK hoặc ERROR. ARM ACK phải đi trước INFO. Firmware còn phục vụ ABORT trong lúc DMA chạy và giữa các sample chunk. Không có ACK từ PC cho sample chunk.

### ANALYZE (8 byte payload)

| Offset | Kiểu | Ý nghĩa |
|---|---|---|
| 0 | u8 | operation (`op`) |
| 1 | u16 | `arg` (slot, protocol mask, probe mode, v.v.) |
| 3 | u32 | `arg32` (baud rate cho decode, tolerance, v.v.) |
| 7 | u8 | reserved (0) |

Các giá trị `op` hợp lệ:

| op | Tên | arg | arg32 | Mô tả |
|---|---|---|---|---|
| 0 | DECODE | protocol mask | baud (0=auto) | Chạy decoder on-device. Trả về text qua RESULT. |
| 1 | SAVE | slot | 0 | Lưu phiên capture hiện tại vào flash. |
| 2 | LOAD | slot | 0 | Nạp phiên từ flash vào RAM (không stream). |
| 3 | SIG_SAVE | 0 | 0 | Lưu buffer hiện tại làm golden reference. |
| 4 | SIG_COMPARE | 0 | tolerated mismatches | So sánh buffer hiện tại với golden ref. |
| 5 | DIFF | slot | 0 | So sánh buffer hiện tại với phiên trong flash. |
| 6 | SESSION | slot | 0 | Đọc header phiên từ flash (trả về qua RESULT). |
| 7 | REPLAY | 0 | 0 | Stream lại cửa sổ capture hiện tại (không ACK). |
| 8 | PINPROBE | 0=pull-down, 1=pull-up, 2=float | 0 | Đọc CH0..CH7 với pull áp dụng; RESULT[1]=mức 8 bit. |
| 9 | DECODE_ALL | UART baud / 100 (0 = 115200) | CAN baud (0 = 250000) | Decode cả 4 giao thức trên map 8 kênh cố định: UART CH0, I2C SCL/SDA CH1/CH2, SPI CS/SCK/MOSI/MISO CH3..CH6, CAN CH7. |

### RESULT

Payload tùy thuộc vào `op` của ANALYZE tương ứng:
- `DECODE`: text UTF-8, kết quả decode (UART/I2C/SPI/CAN).
- `DECODE_ALL`: text UTF-8 gồm 4 dòng, mỗi giao thức một dòng theo thứ tự UART, I2C, SPI, CAN.
- `SAVE/LOAD/SIG_SAVE`: 1 byte status (`RESULT_OK=0`, `RESULT_ERROR=1`, `RESULT_ABSENT=2`).
- `SIG_COMPARE`: text UTF-8 (MATCH/MISMATCH).
- `DIFF`: text UTF-8 (first mismatch hoặc "identical").
- `SESSION`: struct `session_header_t` (32 byte).
- `REPLAY`: stream SAMPLES + END như capture bình thường.
- `PINPROBE`: 1 byte status + 1 byte mức 8 bit (CH0..CH7) + 12 byte chẩn đoán (gpio_in u32, pad0 u32, pad1 u32).

### CAPS

| Offset | Kiểu | Giá trị v1 |
|---|---|---|
| 0 | u8 | channel_count=8 |
| 1 | u8 | sample_unit_size=1 |
| 2 | u16 | feature_flags=0: legacy field; features in ID |
| 4 | u32 | max_samples=262144 |
| 8 | u32 | rate_count=8 |
| 12 | u64[rate_count] | 1000,10000,100000,250000,500000,1000000,5000000,25000000 Hz |

`feature_flags` vẫn bằng 0 để driver v1 cũ tiếp tục kết nối được. Firmware có hỗ trợ
trigger được phát hiện qua hậu tố `+trg` trong chuỗi ID (xem mục "Nhận dạng tính năng").
Firmware có hỗ trợ ANALYZE được phát hiện qua hậu tố `+an` trong chuỗi ID.

### CONFIG (20 byte v1, 28 byte v2)

v1 giữ nguyên như cũ, host cũ gửi 20 byte thì firmware coi là capture manual.

| Offset | Kiểu | v1 | v2 |
|---|---|---|---|
| 0 | u64 | sample_rate_hz, phải thuộc CAPS | như v1 |
| 8 | u32 | sample_count, 1…max_samples | như v1 |
| 12 | u32 | pretrigger_count=0 | pretrigger_count, 0 ≤ pre < count |
| 16 | u8 | channel_mask, khác 0 | như v1 |
| 17 | u8 | trigger_mode=0 (manual) | trigger_mode |
| 18 | u8 | reserved=0 | trigger_channel (0…7) |
| 19 | u8 | reserved=0 | trigger_edge (0 = sườn lên, 1 = sườn xuống) |
| 20 | u8 | — | pattern_value |
| 21 | u8 | — | pattern_mask |
| 22 | u16 | — | reserved=0 |
| 24 | u32 | — | reserved=0 |

Tổng v2 = 28 byte. Firmware nhận cả hai kích thước: 20 byte là manual, 28 byte là
bản có trigger. Driver v1 gửi 20 byte nên không bị ảnh hưởng.

`trigger_mode`: 0 = manual, 1 = edge, 2 = pattern, 3 = UART byte.
Mẫu: `(sample & pattern_mask) == (pattern_value & pattern_mask)`. Bit n của sample là
kênh n. Firmware tự mask `pattern_value`, nên host gửi giá trị chưa mask vẫn cho
kết quả mong muốn.

HELLO → ID; GET_CAPS → CAPS; CONFIG/ARM/ABORT → ACK hoặc ERROR. ARM ACK phải đi trước INFO. Firmware còn phục vụ ABORT trong lúc DMA chạy và giữa các sample chunk. Không có ACK từ PC cho sample chunk.

### CAPS

| Offset | Kiểu | Giá trị v1 |
|---|---|---|
| 0 | u8 | channel_count=8 |
| 1 | u8 | sample_unit_size=1 |
| 2 | u16 | feature_flags=0: legacy field; features in ID |
| 4 | u32 | max_samples=262144 |
| 8 | u32 | rate_count=8 |
| 12 | u64[rate_count] | 1000,10000,100000,250000,500000,1000000,5000000,25000000 Hz |

`feature_flags` vẫn bằng 0 để driver v1 cũ tiếp tục kết nối được. Firmware có hỗ trợ
trigger được phát hiện qua hậu tố `+trg` trong chuỗi ID (xem mục "Nhận dạng tính năng").

### CONFIG (20 byte v1, 28 byte v2)

v1 giữ nguyên như cũ, host cũ gửi 20 byte thì firmware coi là capture manual.

| Offset | Kiểu | v1 | v2 |
|---|---|---|---|
| 0 | u64 | sample_rate_hz, phải thuộc CAPS | như v1 |
| 8 | u32 | sample_count, 1…max_samples | như v1 |
| 12 | u32 | pretrigger_count=0 | pretrigger_count, 0 ≤ pre < count |
| 16 | u8 | channel_mask, khác 0 | như v1 |
| 17 | u8 | trigger_mode=0 (manual) | trigger_mode |
| 18 | u8 | reserved=0 | trigger_channel (0…7) |
| 19 | u8 | reserved=0 | trigger_edge (0 = sườn lên, 1 = sườn xuống) |
| 20 | u8 | — | pattern_value |
| 21 | u8 | — | pattern_mask |
| 22 | u16 | — | reserved=0 |
| 24 | u32 | — | reserved=0 |

Tổng v2 = 28 byte. Firmware nhận cả hai kích thước: 20 byte là manual, 28 byte là
bản có trigger. Driver v1 gửi 20 byte nên không bị ảnh hưởng.

`trigger_mode`: 0 = manual, 1 = edge, 2 = pattern.
Mẫu: `(sample & pattern_mask) == (pattern_value & pattern_mask)`. Bit n của sample là
kênh n. Firmware tự mask `pattern_value`, nên host gửi giá trị chưa mask vẫn cho
kết quả mong muốn.

Mask dùng để xác nhận kênh được bật trong GUI. Firmware vẫn lấy và truyền đủ tám bit; không nén/tái đánh số kênh. Bit 0=GP0/CH0, …, bit 7=GP7/CH7, kể cả khi một số kênh bị tắt.

### CAPTURE_INFO (24 byte)

| Offset | Kiểu | Ý nghĩa |
|---|---|---|
| 0 | u64 | sample_rate_hz thực tế đã chọn |
| 8 | u32 | sample_count |
| 12 | u32 | trigger_index=0xffffffff (không trigger) |
| 16 | u32 | pretrigger_count=0 |
| 20 | u8 | channel_count=8 |
| 21 | u8 | sample_unit_size=1 |
| 22 | u8 | channel_mask, khớp CONFIG |
| 23 | u8 | flags=0; capture lỗi phải trả ERROR |

Với capture có trigger, `trigger_index` là **vị trí của xung trigger bên trong cửa sổ
đã gửi**, tức bằng `pretrigger_count`. Host không cần tự tính lại từ dữ liệu thô.

Driver v1 kiểm tra toàn bộ metadata trên trước khi chấp nhận SAMPLES. Không gửi INFO nhiều lần; không gửi SAMPLES trước INFO; tổng mẫu không vượt cấu hình; END chỉ hợp lệ khi đủ mẫu và CRC toàn capture trùng khớp. Mỗi mẫu là một thời điểm, mọi kênh dùng cùng PIO IN. Thời gian `sample_index / sample_rate_hz`, không lấy từ thời điểm USB nhận frame.

### Ngữ nghĩa trigger hiện tại

Firmware hiện tại giữ cửa sổ triggered tối đa131072 mẫu, pre-trigger tối đa50%.
`count=262144` gửi trực tiếp qua Python/LAR2 được clamp; driver công bố limit
trigger131072 nên CLI có thể từ chối sớm yêu cầu lớn hơn.

Trigger chỉ được chấp nhận sau khi có đủ pre-history. Khi event quá muộn để
đủ cả pre và post trong RAM, firmware re-arm với phase backoff nhỏ thay vì slide
và làm lệch trigger. Cửa sổ thành công có trigger_index = pre đã clamp. Nếu chưa
thấy event, re-arm hữu hạn; host được timeout/HELLO/ABORT để phục hồi. Giữa các
lần re-arm có khoảng trống, không phải pre-trigger ring liên tục25MHz vô hạn.

ID hiện tại: `RP2350-LA/1.2+trg+an+str+ui18+sync+opt3`. Frame header vẫn version1.
Các extension giữ tương thích prefix, opcode và layout cũ; feature flags CAPS0
không có nghĩa firmware hiện chỉ manual. Xem `lar2.h` là nguồn chính xác.

## Timeout và lỗi

- Handshake HELLO: 2 giây; các ACK/CAPS: 1 giây.
- Chờ INFO: thời gian capture theo count/rate + 5 giây.
- Sau INFO: tối đa 5 giây không nhận được frame hoàn chỉnh.
- CRC sai, sequence sai, metadata không khớp, overflow, ERROR, rút USB hoặc timeout: dừng acquisition và không đánh dấu capture hợp lệ.
- Người dùng Stop: ABORT và kết thúc session, không đợi đủ mẫu. Nếu không đồng bộ ACK được, driver mở lại cổng và HELLO.

Callback libsigrok có thể kết thúc với dữ liệu một phần mà CLI vẫn trả exit=0. Script `capture.sh` cần cả log `Capture verified:` trước khi xuất `.sr` cuối cùng.

## Khác với tài liệu khảo sát ban đầu

Các tài liệu trong `dinhanh/` mới đề xuất khung LAR2, chưa gán type số và chưa định nghĩa CAPS/CONFIG/ACK đầy đủ. File này chốt các giá trị đó, bắt buộc CRC, chốt ERROR dạng chuỗi và chỉ công bố manual capture. Nếu triển khai firmware khác, phải dùng hợp đồng này hoặc sửa đồng bộ cả driver, firmware và emulator.

Phiên bản 1.1 bổ sung trigger cạnh và trigger mẫu bit cùng pre-trigger, theo hợp
đồng CONFIG 28 byte mở rộng. Driver v1 không đổi, vẫn gửi CONFIG 20 byte và nhận
capture manual như trước.

## Phím tắt USB (kênh phụ, không phải frame LAR2)

UI trên TFT nhận thêm các ký tự đơn khi parser LAR2 đang rảnh (`parser.used == 0`)
và ký tự khác `L` — do đó không thể lẫn với header `LAR2`:

| Ký tự | Ý nghĩa |
|---|---|
| `u` / `d` / `o` / `b` | nhấn ngắn UP / DOWN / OK / BACK |
| `U` / `D` / `O` / `B` | nhấn giữ UP / DOWN / OK / BACK |

Đây là đường điều khiển song song với 4 nút vật lý: cả hai cùng đẩy sự kiện vào
một hàng đợi duy nhất trong `ui.c`. Ký tự lạ bị bỏ qua. Host tool:
`tools/ui_keys.py`.

### Present the real capture on the TFT (firmware `+ui18`)

`ANALYZE` op **18** (`UI_SHOW_CAPTURE`): `arg` is screen 2 (CH0–3),
3 (CH4–7), or 4 (decode). `arg32` is selector 0=AUTO, 1=UART, 2=I2C,
3=SPI, 4=CAN. UART uses 115200 baud; CAN uses 250000, matching the bench.
Returns `RESULT` with status OK, or `ERROR` if no completed snapshot exists,
a capture is in progress, or the selection is invalid. CONFIG, a new capture,
and streaming invalidate the snapshot. The snapshot becomes available after
END is sent. The command never loads the synthetic UI_DEMO waveform.

The waveform screens open at FIT. The decode screen recomputes both text and
packet rows from the capture window and real trigger index. Use UI_TEXT,
UI_TABLE, UI_STATE and RUNTIME to read back the result. See `docs/DEMO.md`.

Firmware `+ui18+sync` also refreshes the TFT capture pointer, trigger, text
and packet cache after each completed host snapshot. The decode page stays
on its selected protocol while displaying the new acquisition.


## Streaming và UI extension hiện tại

`STREAM_BLOCK=0x94`: `encoding:u8, sample_offset:u32, body[]`; encoding0 là raw,
encoding1 là cặp `run:u8,value:u8`. Offset modulo2^32, sequence modulo2^16. Raw
block hiện tối đa491mẫu; host nhận tối đa500 để giữ tương thích. Tám bit sample
vẫn giữ vị trí CH0..CH7 kể cả khi mask chỉ bật vài channel.

| ANALYZE op | Ý nghĩa |
|---|---|
|10|BOOTLOADER|
|11|RUNTIME,32byte diagnostic reply|
|12|UI_DEMO, chỉ dùng fixture mô phỏng|
|13/14/15|UI_TEXT / UI_TABLE / UI_STATE|
|16|STREAM_START; rate từ CONFIG; argbit0 bật RLE; max1MHz|
|17|STREAM_STOP; freeze DMA,flush tail rồi gửi counters|
|18|UI_SHOW_CAPTURE:arg=screen2/3/4,arg32=AUTO0/UART1/I2C2/SPI3/CAN4|
|19|UI_FRAME:arg=byteoffset,arg32=length≤1000; replystatus,width,height,versionu32,offsetu16,totalu16,pixelsBE565|
|20|UI_VIEW:29byte gồmstatus + start/visible/count/triggeru32,rateu64,screen/zoom/proto/eventsu8|

STREAM_STOP hiện trả25byte, little endian:

| Offset | Kiểu | Nội dung |
|---|---|---|
|0|u8|status|
|1|u32|samples sent|
|5|u32|samples dropped|
|9|u32|captured sample span|
|13|u32|hardware fault flags|
|17|u64|capture duration microseconds|

9byte đầu giữ layout counters cũ. Host phải drain mọi STREAM_BLOCK trước RESULT,
đối chiếu sender/receiver, kiểm offset/sequence/CRC và rate/duration. Gap hoặc
fault phải INVALID. RLE1MHz chỉ lossless với dữ liệu nén đủ, không cam kết raw1MHz.
Khi stream, chỉ STOP/RUNTIME và HELLO/ABORT được dùng; UI redraw hoãn tới sau STOP.
Snapshot completion có thể để pending DMA IRQ: firmware phải clear trước bật
stream IRQ để không tạo slot64KiB giả.
