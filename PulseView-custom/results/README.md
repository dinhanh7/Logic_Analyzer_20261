# Bằng chứng kiểm thử RP2350 Logic Analyzer

Đọc [LATEST.txt](LATEST.txt) để tìm **run QA đầy đủ cuối không có FAIL**.
Báo cáo tổng hợp: [TEST-REPORT](../docs/TEST-REPORT.md). Tài liệu slide:
[TESTING](../TESTING.md). Không lấy một run đang chạy hoặc run lỗi cũ làm kết luận cuối.

## Cấu trúc mỗi thư mục `qa-v2-YYYYMMDD-HHMMSS/`

| File/nhóm | Nội dung |
|---|---|
| `manifest.json` | Commit đầu run, trạng thái dirty, SHA256 source/binary/library, port và Python |
| `run.json` | Counts PASS/FAIL/BLOCKED/NOT-VERIFIED, exit code và phạm vi ma trận |
| `summary.json`, `summary.csv` | Mục tiêu, điều kiện, kỳ vọng, thực tế, verdict và evidence của từng case |
| `cases/ID.json` | Bản ghi theo case để link trực tiếp từ báo cáo cuối |
| `measurements.csv` | Số đo frequency, bit-width và stream được trích để lập biểu đồ/slide |
| `A1-*`, `A2-*` | Cùng `.raw/.sr`, output sigrok và packet decoder C để so byte-level |
| `A3-*` | Empty/constant/truncated/parity/noise/irregular-clock vectors; raw một byte/mẫu |
| `A4-*`, `A5-*` | Autobaud fractional-rate và moved-channel/noise corpus |
| `B1/B2-*` | Capture timing thật và số đo mean/error/jitter |
| `B3-*`, `B5-*`, `B6-*`, `B7-*`, `B8-*` | Edge/clamp/100capture/rate-matrix và channel activity |
| `B4-*` | Pattern/manual/timeout và nguồn UART-off driven-high cho negative test |
| `C1/C2/C3.sr/.json/.log` | Stream10s,raw5s,soak300s; counters và `/usr/bin/time -v` |
| `C4-*`, `C5-*`, `C5b-*`, `C6-*` | SIGINT/kill/stall800ms/stall4s/rate rejection/recovery |
| `D1-*`, `D2/`, `D4-*` | Capability, driver capture, decoder logs và `.sr` metadata |
| `D3-live/`, `D3-root.png` | Demo thật, Qt screenshot, TFT framebuffer, annotation JSON và ảnh X11 root |
| `D5-*` | Regression40session decoder song song |
| `E*` | Inventory, plan phần cứng lịch sử và đánh giá bảo vệ/BOM/giới hạn |
| `F1-*`, `F2-*`, `F4-*`, `F3-*` | 50capture, resetUSB, storage read-only và gate cũ |

`.raw` giữ bit n là CHn, không đổi vị trí khi channel mask hẹp. `.sr` là ZIP
sigrok session có metadata và logic data; dữ liệu của một negative test hoặc có
gap không được coi hợp lệ chỉ vì file vẫn mở được. Luôn đọc JSON/verdict đi kèm.

Các run trước LATEST có thể FAIL hoặc chỉ chạy vài nhóm. Chúng được giữ để thấy
lỗi trước sửa và lỗi giả định của harness. Ví dụ A4 idle-prefix, A5 I2C→CAN,
triggerlate và pendingDMAIRQ64KiB giả. Điều kiện A3 từng ghi34 là lỗi nhãn;
`actual.vectors=32` và64lượt mới là số thực đã chạy; không có34vector được ngầm nhận.

## `review-20261118/`

Bằng chứng giai đoạn review trước QA cuối: baseline mất STOP-tail424/968mẫu,
benchmark capacity60s với firmware thử nghiệm trước `opt3`, USB controls/TFT
framebuffer tour, thử ESP32 và build memory. Số liệu capacity không là cam kết
lossless của firmware cuối; current acceptance dùng LATEST. Nguồn ESP32 ban đầu
được giữ để đối chiếu các thay đổi. Không ghi đè hay xóa session flash của người dùng.

## `demo-final/`

Lần chạy cuối bằng đúng `DISPLAY=:0 bash scripts/demo.sh`, giữ TFT2giây/màn,
stream+snapshot+TFT+PulseView live. `summary.json=PASS` và GUI được giữ mở trên máy.
`SHA256SUMS` ở thư mục này bảo vệ các file đã gom. Đây là artifact trình diễn;
không thay cho các mục bảo vệ/BOM vẫn NOT-VERIFIED.
