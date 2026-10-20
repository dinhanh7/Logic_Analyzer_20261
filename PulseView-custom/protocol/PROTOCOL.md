# LAR2 v1 — hợp đồng firmware / libsigrok

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
| `0x03` | CONFIG | 20 byte, mô tả dưới |
| `0x04` | ARM | rỗng; yêu cầu CONFIG hợp lệ |
| `0x05` | ABORT | rỗng; hợp lệ cả khi đang capture/truyền |
| `0x81` | ID | chuỗi UTF-8 không NUL, bắt đầu `RP2350-LA` |
| `0x82` | CAPS | số kênh, đơn vị mẫu, tính năng, buffer, danh sách rate |
| `0x83` | ACK | 1 byte: type command vừa xử lý |
| `0x90` | CAPTURE_INFO | 24 byte |
| `0x91` | SAMPLES | 1…1024 byte trạng thái GPIO theo thứ tự thời gian |
| `0x92` | CAPTURE_END | `u32 sample_count`, `u32 crc32(samples)` |
| `0xff` | ERROR | chuỗi lỗi UTF-8 ngắn, không NUL |

HELLO → ID; GET_CAPS → CAPS; CONFIG/ARM/ABORT → ACK hoặc ERROR. ARM ACK phải đi trước INFO. Firmware còn phục vụ ABORT trong lúc DMA chạy và giữa các sample chunk. Không có ACK từ PC cho sample chunk.

### CAPS

| Offset | Kiểu | Giá trị v1 |
|---|---|---|
| 0 | u8 | channel_count=8 |
| 1 | u8 | sample_unit_size=1 |
| 2 | u16 | feature_flags=0: manual only |
| 4 | u32 | max_samples=262144 |
| 8 | u32 | rate_count=8 |
| 12 | u64[rate_count] | 1000,10000,100000,250000,500000,1000000,5000000,25000000 Hz |

### CONFIG (20 byte)

| Offset | Kiểu | Ý nghĩa |
|---|---|---|
| 0 | u64 | sample_rate_hz, phải thuộc CAPS |
| 8 | u32 | sample_count, 1…max_samples |
| 12 | u32 | pretrigger_count=0 |
| 16 | u8 | channel_mask, khác 0 |
| 17 | u8 | trigger_mode=0 (manual) |
| 18 | u16 | reserved=0 |

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

Driver v1 kiểm tra toàn bộ metadata trên trước khi chấp nhận SAMPLES. Không gửi INFO nhiều lần; không gửi SAMPLES trước INFO; tổng mẫu không vượt cấu hình; END chỉ hợp lệ khi đủ mẫu và CRC toàn capture trùng khớp. Mỗi mẫu là một thời điểm, mọi kênh dùng cùng PIO IN. Thời gian `sample_index / sample_rate_hz`, không lấy từ thời điểm USB nhận frame.

## Timeout và lỗi

- Handshake HELLO: 2 giây; các ACK/CAPS: 1 giây.
- Chờ INFO: thời gian capture theo count/rate + 5 giây.
- Sau INFO: tối đa 5 giây không nhận được frame hoàn chỉnh.
- CRC sai, sequence sai, metadata không khớp, overflow, ERROR, rút USB hoặc timeout: dừng acquisition và không đánh dấu capture hợp lệ.
- Người dùng Stop: ABORT và kết thúc session, không đợi đủ mẫu. Nếu không đồng bộ ACK được, driver mở lại cổng và HELLO.

Callback libsigrok có thể kết thúc với dữ liệu một phần mà CLI vẫn trả exit=0. Script `capture.sh` cần cả log `Capture verified:` trước khi xuất `.sr` cuối cùng.

## Khác với tài liệu khảo sát ban đầu

Các tài liệu trong `dinhanh/` mới đề xuất khung LAR2, chưa gán type số và chưa định nghĩa CAPS/CONFIG/ACK đầy đủ. File này chốt các giá trị đó, bắt buộc CRC, chốt ERROR dạng chuỗi và chỉ công bố manual capture. Nếu triển khai firmware khác, phải dùng hợp đồng này hoặc sửa đồng bộ cả driver, firmware và emulator.
