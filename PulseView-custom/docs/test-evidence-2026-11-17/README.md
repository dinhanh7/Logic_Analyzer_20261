# Bằng chứng cho báo cáo kiểm thử 17/11/2026

Báo cáo chính: [Markdown](../BAO-CAO-KIEM-THU-2026-11-17.md) và [Word](../BAO-CAO-KIEM-THU-2026-11-17.docx). Mã nguồn được đánh giá: `576873a`, nhánh `feature/trigger-v2`.

- `history/`: bản sao nguyên trạng sáu tài liệu từ `/home/namkhanh/EmbeddedSystem/docs/`, được đưa vào repo để truy xuất cùng báo cáo. Tài liệu có cả kế hoạch chưa làm, cấu hình cũ và kết luận đã được báo cáo chính hiệu chỉnh. Đây là hồ sơ ghi chép, không phải raw log của toàn bộ phép thử.
- `current/tests-default.log`: lần chạy `bash scripts/test.sh` dừng do LeakSanitizer không hoạt động dưới ptrace.
- `current/tests-no-lsan.log`: lần chạy `ASAN_OPTIONS=detect_leaks=0 bash scripts/test.sh`; C, host 7/7, stream 3/3, render selftest đạt; integration báo 19 failure kể cả subtest, log lặp lỗi khởi tạo libusb. Toàn lệnh không PASS.
- `current/firmware-build.log`: build tăng dần Pico 2 thành công bằng `cmake --build build/firmware-pico2 -j4`.
- `current/esp32-attempt.log`: gọi `python3 tests/test_esp32_link.py` nhưng không mở được `/dev/ttyACM0`; không có kết quả thu mới. Khi kiểm tra, `/dev/ttyUSB0` cũng không hiện diện.
- `SHA256SUMS`: checksum các bản sao lịch sử và bốn raw log; không bao gồm README hoặc chính file checksum.

Kết quả phần cứng 8/8, 0 drop, raw 500 kHz và trạng thái GUI cuối được dẫn từ bàn giao của người dùng. Bộ này không chứa raw capture `.sr`, JSON drop hoặc ảnh GUI của các lần bàn giao đó. Không diễn giải chúng thành phép thử vừa chạy lại.
