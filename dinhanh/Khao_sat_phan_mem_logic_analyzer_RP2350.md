# Khảo sát phần mềm cho Logic Analyzer dùng Raspberry Pi Pico 2 (RP2350)

**Phạm vi:** Topic 2 của học phần Hệ thống nhúng và thiết kế giao tiếp nhúng.  
**Mục tiêu phần cứng:** tối thiểu 8 kênh, 25 MHz; dữ liệu được capture theo khối rồi gửi sang PC để hiển thị/phân tích offline.  
**Ngày khảo sát:** 15/10/2026.

## 1. Tiêu chí đánh giá

Ở 8 kênh và 25 MHz, mỗi mẫu có thể đóng gói trong 1 byte. Luồng mẫu thô là **25 MB/s** (200 Mbit/s), chưa tính metadata và overhead truyền thông. Vì vậy phần mềm được đánh giá theo khả năng làm việc với *capture theo khối* trước; streaming liên tục là chức năng nâng cao.

| Tiêu chí | Ý nghĩa với sản phẩm RP2350 |
|---|---|
| Kết nối phần cứng tự làm | Có thể nhận trực tiếp dữ liệu từ Pico 2 hay phải viết driver/chuyển đổi? |
| Dạng dữ liệu | Có thể mang mẫu logic 8 bit, `sample_rate`, số kênh, vị trí trigger và tên kênh hay không? |
| Waveform/timing | Zoom/pan, cursor A/B, đo delta-time, tần số, độ rộng xung và quan hệ cạnh giữa các kênh. |
| Trigger | Cấu hình trigger và xác định phần nào thực thi ở RP2350, phần nào chỉ là hiển thị trên PC. |
| Decode protocol | Mức hỗ trợ UART, SPI, I²C trước; khả năng thêm decoder. |
| Lưu/xuất | Lưu capture, mở lại, xuất CSV/VCD/binary để đối chiếu kết quả. |
| Công sức tích hợp | Phần việc cần làm ngoài firmware capture. |
| Phù hợp bài tập | Có giúp chứng minh được 8 kênh/25 MHz và timing chính xác không? |

## 2. Bảng so sánh chi tiết

| Tiêu chí | PulseView + sigrok | Saleae Logic 2 | DSView | KingstVIS | Ứng dụng PC tự xây dựng (đề xuất) |
|---|---|---|---|---|---|
| Loại phần mềm / giấy phép | GUI mã nguồn mở GPLv3+ trong hệ sinh thái sigrok; dùng `libsigrok` và `libsigrokdecode`. | Phần mềm thương mại, đi kèm hệ Logic của Saleae. | GUI mã nguồn mở GPLv3+, dựa trên sigrok nhưng phát triển cho thiết bị DreamSourceLab. | Phần mềm của hãng Kingst, đi kèm LA Kingst; cần kiểm tra đúng phiên bản/giấy phép khi cài. | Phần mềm do nhóm kiểm soát hoàn toàn; Python + PySide6/PyQt6 + PyQtGraph là lựa chọn thực dụng. |
| Hệ điều hành | Có gói/source cho Windows, Linux và macOS. | Windows, macOS, Linux theo bộ cài của Saleae. | Bản phát hành cho Windows, macOS, Ubuntu/Linux. | Thông thường Windows; mức hỗ trợ hệ điều hành tùy bản phát hành. | Tùy nhóm; Python/Qt hỗ trợ Windows, Linux, macOS. Nên ưu tiên Windows/Linux là đủ cho demo. |
| Nhận thiết bị tự làm RP2350 trực tiếp | **Không có sẵn.** PulseView chỉ thấy thiết bị mà `libsigrok` có driver. Cần viết driver `libsigrok` nhận USB CDC/UART hoặc USB vendor protocol của Pico 2. | Không nên giả định hỗ trợ thiết bị tự làm. Đây là phần mềm dành cho phần cứng Saleae; không phải hướng tích hợp chính của nhóm. | **Không có sẵn.** Mặc định hỗ trợ thiết bị DreamSourceLab. Muốn nhận Pico trực tiếp sẽ phải sửa/xây driver và kiểm thử tương tự sigrok. | **Không có sẵn.** Mặc định làm việc với phần cứng Kingst; xem như công cụ tham khảo UI/tính năng. | **Có.** Nhóm tự định nghĩa USB CDC/vendor protocol, header gói và cơ chế ACK/retry nếu cần. |
| Có thể dùng khi chưa viết driver | Có thể phân tích offline nếu chuyển capture sang định dạng/file mà sigrok đọc được, ví dụ raw binary kèm cấu hình mẫu hoặc session `.sr`. Không phải đường dùng trực tiếp “cắm Pico là chạy”. | Phù hợp quan sát trải nghiệm người dùng; không chọn làm đích nhập dữ liệu cho Pico khi chưa xác minh API/import thực tế. | Có thể mở/làm việc với file theo các khả năng của phần mềm, nhưng không nên đặt nó vào kiến trúc chính của Pico. | Chỉ phù hợp tham khảo hoặc dùng với phần cứng Kingst. | Có ngay từ đầu: đọc file capture của nhóm, sau đó thêm cổng USB để nhận trực tiếp. |
| Hiển thị waveform | Rất tốt: render nhanh ở nhiều mức zoom, nhóm trace và các hàng decoder. | Rất tốt, giao diện hoàn thiện để tham khảo luồng thao tác. | Tốt: hiển thị waveform, cursor, đo cạnh; có cả chế độ stream/loop tùy thiết bị. | Có waveform/cursor/decoder theo phần cứng hỗ trợ; nên trải nghiệm thực tế trước khi đưa thông số vào báo cáo. | Nhóm phải tự làm: kênh logic, thước thời gian, zoom/pan, cursor A/B, snap vào cạnh. Phạm vi vừa đủ cho demo 8 kênh. |
| Đo timing/pulse | Cursor và công cụ đo qua giao diện; có thể quan sát quan hệ thời gian giữa kênh. | Có cursor/measurement để tham khảo cách trình bày timing. | Có đo khoảng cách cạnh, duty cycle, tần số và cursor; changelog cho thấy hỗ trợ đo giữa các cạnh/kênh. | Có các phép đo tùy phiên bản. | Tự tính từ index mẫu: `Δt = (index_B − index_A) / sample_rate`. Có thể thêm width, period, duty, frequency sau MVP. |
| Trigger | Thiết lập trigger phụ thuộc driver/phần cứng. GUI không thay thế trigger thời gian thực trong RP2350. | Trigger do phần cứng Saleae và ứng dụng phối hợp. | Có simple/advanced/serial trigger với thiết bị DSLogic tương thích. | Có trigger tùy thiết bị Kingst. | Firmware RP2350 phải thực thi trigger cạnh/pattern và lưu `trigger_index`. PC chỉ gửi cấu hình và hiển thị điểm trigger. |
| Decoder UART / SPI / I²C | Có; `libsigrokdecode` còn có nhiều decoder khác và hỗ trợ xếp chồng decoder. UART có tùy chọn baud rate, data bit, parity, stop bit, bit order và đảo cực. | Có decoder giao thức phong phú, là tham chiếu tốt cho cách hiển thị frame/lỗi. | Có UART, SPI, I²C và nhiều decoder; phát hành 1.3.2 ghi nhận sửa decoder CAN/QSPI/DMX512/LIN. | Có các decoder thông dụng tùy phiên bản. | MVP nên làm **UART trước**, sau đó SPI, I²C. Decoder nhận buffer cùng `sample_rate`, các mapping kênh và tham số protocol. |
| Mở rộng decoder | Có thể viết decoder Python cho `libsigrokdecode`; phù hợp nếu muốn đóng góp dài hạn, nhưng phải học API/đóng gói. | Có Analyzer SDK cho hệ sinh thái Saleae, nhưng không giải quyết việc nhận dữ liệu từ RP2350. | Mã nguồn mở, nhưng khác biệt so với sigrok gốc và thiên về DSLogic. | Phụ thuộc SDK/tài liệu của Kingst. | Linh hoạt nhất cho bài tập: tách `decoder_uart.py`, `decoder_spi.py`, `decoder_i2c.py` khỏi giao diện; test bằng file capture. |
| Lưu/xuất dữ liệu | `libsigrok` có hỗ trợ các định dạng như session `.sr`, raw binary, CSV, VCD. | Có hệ file/export của Saleae; dùng trong môi trường Saleae. | Có lưu phiên `.dsl`, export vùng waveform và CSV/VCD theo các chức năng của DSView. | Định dạng hãng; cần xác nhận khi dùng thực tế. | Đề xuất định dạng riêng đơn giản: header JSON + raw packed samples; có `Export CSV` sau. Nếu cần tương thích, viết thêm exporter VCD/CSV. |
| Điểm mạnh | Miễn phí, mã nguồn mở, waveform/decoder mạnh; là chuẩn tham khảo tốt nhất và có đường mở rộng thành công cụ thực thụ. | Trải nghiệm người dùng tốt, tài liệu và cách hiển thị protocol là mẫu tham khảo tốt. | Nhiều tính năng LA nâng cao, mã nguồn công khai, hỗ trợ stream/loop và các decoder đa dạng. | Hữu ích để xem các chức năng thương mại phổ biến trên LA giá rẻ. | Rủi ro tích hợp thấp nhất; khớp tuyệt đối với packet protocol, số kênh và firmware RP2350 của nhóm. |
| Hạn chế chính | Viết driver C cho `libsigrok` là công việc đáng kể; không nên đưa vào MVP nếu thời gian ít. | Không tương thích mặc định với Pico tự thiết kế; không phù hợp làm nền tảng sản phẩm. | Thiết kế tập trung vào phần cứng DreamSourceLab; tích hợp Pico vẫn nhiều công sức. | Phụ thuộc hãng/phần cứng; không phù hợp làm nền tảng mở cho thiết bị tự chế. | Phải tự phát triển UI, decoder và parser; cần giới hạn phạm vi để hoàn thành đúng hạn. |
| Đánh giá cho nhóm | **Dùng để khảo sát và kiểm chứng offline; tích hợp trực tiếp là phương án mở rộng.** | **Chỉ khảo sát UI/tính năng.** | **Chỉ khảo sát UI/tính năng; không chọn làm nền tảng.** | **Chỉ khảo sát thị trường/UI.** | **Nên chọn làm phần mềm chính.** |

## 3. Kết luận đề xuất kiến trúc

Chọn **ứng dụng PC tự xây dựng** làm phần mềm chính cho sản phẩm. Dùng **PulseView + sigrok** làm chuẩn tham chiếu kỹ thuật: cách hiển thị waveform, cursor và decoder; đồng thời là phương án phân tích offline khi có thời gian chuyển đổi định dạng hoặc viết driver.

```text
Tín hiệu 8 kênh
      │
      ▼
RP2350: PIO/DMA capture → RAM/buffer → trigger_index + sample_rate
      │ USB (CDC hoặc vendor endpoint), gửi theo từng khối
      ▼
PC app: parser → bộ nhớ capture → waveform 8 kênh → cursor/đo timing
                                            └→ UART/SPI/I²C decoder
      │
      └→ lưu file capture / xuất CSV hoặc VCD
```

**Nguyên tắc quan trọng:** PC không được tự suy ra timing từ thời điểm nhận USB. Timing được xác định bằng `sample_rate` do firmware ghi vào metadata và index mẫu. Với tốc độ 25 MHz, độ phân giải thời gian danh nghĩa là **40 ns/mẫu**. Độ không đảm bảo khi nhận diện cạnh và mọi kết quả đo phải được nhóm luận giải theo clock capture, input buffer và thiết kế đồng bộ của firmware.

## 4. Phạm vi MVP nên cam kết

| Mức | Chức năng | Điều kiện hoàn thành |
|---|---|---|
| M1 – bắt buộc | Nhận một capture 8 kênh từ Pico 2; đọc `sample_rate`, số mẫu, `trigger_index`; hiển thị waveform. | Capture 25 MHz được mở lại và thấy đúng mức logic trên 8 kênh. |
| M2 – bắt buộc | Zoom/pan, bật/tắt/đặt tên kênh, cursor A/B, đo `Δt`. | Cho phép chứng minh khoảng thời gian/cạnh giữa các kênh. |
| M3 – nên có | Lưu/mở capture; export CSV hoặc VCD. | Một capture có thể gửi kèm báo cáo để tái lập phân tích. |
| M4 – giá trị gia tăng | UART decoder và bảng frame; sau đó SPI hoặc I²C. | Decode đúng với tín hiệu test đã biết trước. |
| M5 – chỉ làm khi còn thời gian | Trigger pattern, hiển thị capture liên tiếp, tích hợp PulseView trực tiếp qua driver `libsigrok`. | Không ảnh hưởng tiến độ M1–M4. |

## 5. Định dạng gói dữ liệu đề xuất

Không gửi text từng mẫu, vì sẽ phá băng thông. Dùng binary packet:

| Trường | Kích thước gợi ý | Nội dung |
|---|---:|---|
| Magic + phiên bản | 4 + 1 byte | Nhận diện đúng giao thức phần mềm/firmware. |
| `channel_count` | 1 byte | Ban đầu là 8. |
| `sample_rate_hz` | 32 bit | Ví dụ `25000000`. |
| `sample_count` | 32 bit | Tổng số mẫu trong capture. |
| `trigger_index` | 32 bit | Mẫu trigger; có thể dùng giá trị đặc biệt nếu không trigger. |
| `pretrigger_count` | 32 bit | Số mẫu trước trigger, nếu firmware hỗ trợ. |
| `payload_bytes` | 32 bit | Số byte dữ liệu sau header. |
| Samples | 1 byte/mẫu với 8 kênh | Bit 0…7 ứng với CH0…CH7 tại cùng thời điểm lấy mẫu. |
| CRC32 (tùy chọn) | 32 bit | Phát hiện dữ liệu lỗi khi truyền/lưu. |

Ở 25 MHz, **1 MiB dữ liệu mẫu lưu được xấp xỉ 41,94 ms** (`1,048,576 / 25,000,000`). Số liệu này giúp nhóm chọn độ sâu capture phù hợp trước khi thiết kế buffer RAM và thời gian gửi USB.

## 6. Kế hoạch khảo sát/demo phần mềm

1. Cài PulseView, mở một capture UART/I²C/SPI mẫu; ghi ảnh giao diện waveform, cursor và decoder để đưa vào slide.
2. Thử nhập một file raw 8-bit do nhóm tạo, đặt sample rate 25 MHz và xác nhận thước thời gian hiển thị đúng. Đây là thử nghiệm *offline*, không chứng minh Pico được PulseView nhận trực tiếp.
3. Làm PC app theo M1–M2 trước, dùng một file capture giả lập có mẫu vuông để hoàn thiện UI khi firmware chưa xong.
4. Kết nối Pico 2 qua USB, so sánh một waveform/period đã biết với generator hoặc MCU phát chuẩn.
5. Chỉ sau khi M1–M3 ổn định mới thêm UART và các decoder khác.

## 7. Nguồn tham khảo

- [PulseView – tính năng và mã nguồn](https://sigrok.org/wiki/Pulseview): GUI Qt, render waveform, protocol decoder và trace group.
- [PulseView manual](https://sigrok.org/doc/pulseview/0.4.2/manual.html): thu thập, phân tích, xử lý và export dữ liệu analog/logic.
- [Sigrok – phần cứng được hỗ trợ](https://sigrok.org/wiki/Supported_hardware): danh sách thiết bị/driver hiện có; RP2350 tự thiết kế không tự xuất hiện nếu chưa có driver.
- [Sigrok Hardware Driver API](https://www.sigrok.org/wiki/Hardware_driver_API): driver cần đăng ký với `libsigrok` và cài đặt scan/open/config/start/stop acquisition.
- [Sigrok protocol decoders](https://sigrok.org/wiki/Protocol_decoders): danh sách decoder được hỗ trợ.
- [UART decoder options của sigrok](https://sigrok.org/wiki/Protocol_decoder:Uart): baud rate, data bit, parity, stop bit, bit order, đảo cực.
- [Sigrok input/output formats](https://sigrok.org/wiki/Input_output_formats): session `.sr`, raw binary, CSV, VCD và các định dạng khác.
- [DSView repository](https://github.com/DreamSourceLab/DSView): DSView là GUI cho các thiết bị DreamSourceLab, dựa trên sigrok và GPLv3+.
- [DSView download/changelog](https://www.dreamsourcelab.com/download/): bản 1.3.2, hỗ trợ Windows/macOS/Linux; các tính năng waveform export, measurement, stream/loop và decoder.
- [PyQtGraph documentation](https://pyqtgraph.readthedocs.io/en/latest/): thư viện đồ họa/GUI Python trên Qt và NumPy, có tài liệu về hiệu năng.

> **Ghi chú báo cáo:** Không ghi Saleae Logic 2, DSView hay KingstVIS là “hỗ trợ Pico 2” nếu chưa có thử nghiệm kết nối hoặc tài liệu driver cụ thể. Các công cụ này được khảo sát để rút ra yêu cầu chức năng; tính năng của phần cứng thương mại không phải là tính năng mặc định của sản phẩm nhóm.
