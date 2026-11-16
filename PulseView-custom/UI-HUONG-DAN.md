# Hướng dẫn sử dụng giao diện Logic Analyzer RP2350

Tài liệu này mô tả đầy đủ 6 màn hình trên TFT 128×160, luồng điều hướng, và
từng thao tác nút bấm (kể cả cổng điều khiển qua USB). Firmware có sẵn dữ liệu
mô phỏng 4 giao thức, nên có thể thử toàn bộ mà không cần máy phát tín hiệu.

---

## 1. Tổng quan các màn hình

| Màn (hiển thị) | ID firmware | Tên | Nội dung chính |
|---|---|---|---|
| Màn 0 | 5 | STANDBY (chờ) | Logo xung vuông + vạch trigger, tên thiết bị, thẻ thông tin, lời nhắc nhấp nháy |
| Màn 1/5 | 0 | CẤU HÌNH ĐO | Tần số, số mẫu, kênh, pre-trigger, dòng BAT DAU THU |
| Màn 2/5 | 1 | TRIGGER | Chế độ, kênh, điều kiện, baud/clock |
| Màn 3/5 | 2 | SÓNG CH0-3 | 4 track CH0..CH3, vạch trigger đỏ, cursor đo giữa |
| Màn 4/5 | 3 | SÓNG CH4-7 | 4 track CH4..CH7, cùng 4 màu, vạch trigger đỏ |
| Màn 5/5 | 4 | DECODE | 3 card chọn, dải sóng một gói, bảng dữ liệu 5 dòng |

> **Đánh số:** màn chờ là "màn 0" theo cách gọi của người dùng (không in số lên
> màn hình). Năm màn đo vẫn giữ nguyên nhãn 1/5..5/5. ID firmware của 5 màn cũ
> không đổi (0..4); màn chờ có ID = 5 để host test không bị lệch.

---

## 2. Luồng vòng lặp (từ lúc bật nguồn đến lúc quay lại)

```
            bật nguồn
               |
               v
        [0 STANDBY]  <-----------------------------------+
               |  bất kỳ nút nào (UP/DOWN/OK/BACK)       |
               v                                        |
        [1/5 CẤU HÌNH ĐO]                               |
               |  OK trên dòng "BAT DAU THU"            |
               v                                        |
        [3/5 SÓNG CH0-3]                                |
               |  OK                                    | OK giữ (>=600ms)
               v                                        |
        [4/5 SÓNG CH4-7]                                |
               |  OK                                    |
               v                                        |
        [5/5 DECODE] ------------------------------------+
```

- **Vào chương trình:** khi bật nguồn, màn chờ hiện ra. Nhấn **bất kỳ** nút
  nào cũng vào ngay màn cấu hình (màn hình được xóa sạch, không để lại ảnh
  logo - ghosting).
- **Kết thúc một phiên:** ở màn DECODE, **giữ OK** (~0.6 giây) quay về màn chờ,
  rồi có thể bắt đầu lại vòng lặp.
- **Lối tắt khác:** ở màn cấu hình, đang duyệt (không sửa giá trị), **giữ BACK
  0.8 giây** cũng quay về màn chờ.
- **Đi lùi bằng BACK:** ở các màn đo, BACK đi ngược lại từng màn
  DECODE → CH4-7 → CH0-3 → Cấu hình (xem bảng từng màn bên dưới).

---

## 3. Thao tác chi tiết theo từng màn

Quy ước chung:
- **Browse mode** (duyệt): UP/DOWN di chuyển dòng chọn, OK vào **edit** hoặc
  thực hiện hành động của dòng đó.
- **Edit mode** (sửa): UP/DOWN thay đổi **giá trị** dòng đang chọn, OK lưu và
  thoát edit, BACK hủy edit và thoát (chưa đổi màn).
- Nhấn giữ (long press) luôn là ~600 ms, **riêng BACK về màn chờ là 800 ms**.

### Màn 0 - STANDBY

| Nút | Hành động |
|---|---|
| Bất kỳ (UP / DOWN / OK / BACK), nhấn ngắn hoặc giữ | Vào ngay màn 1/5 CẤU HÌNH ĐO |

- Lời nhắc `[ NHAN OK BAT DAU ]` đổi màu trắng/xám mờ mỗi 600 ms.

### Màn 1/5 - CẤU HÌNH ĐO

Các dòng: `Tan so`, `So mau`, `Kenh`, `Pre-Trig`, `BAT DAU THU`.

| Nút | Browse mode | Edit mode |
|---|---|---|
| UP / DOWN | Di chuyển giữa 5 dòng | Thay đổi giá trị dòng hiện tại |
| OK | Vào edit (trừ dòng BAT DAU THU) | Lưu giá trị, thoát edit |
| OK trên `BAT DAU THU` | Bắt đầu thu mẫu → màn 3/5 SÓNG CH0-3 | - |
| BACK, nhấn ngắn | → màn 2/5 TRIGGER | Hủy edit, thoát edit |
| BACK, giữ ≥800 ms | → màn 0 STANDBY | Hủy edit, thoát edit (chưa về chờ) |

### Màn 2/5 - TRIGGER

Các dòng: `Che do`, `Kenh`, `Dieu kien`, `Baud/Clk`.

| Nút | Browse mode | Edit mode |
|---|---|---|
| UP / DOWN | Di chuyển giữa 4 dòng | Thay đổi giá trị |
| OK | Vào edit | Lưu, thoát edit |
| BACK | → màn 1/5 CẤU HÌNH ĐO | Hủy edit, thoát edit |

- `Che do`: EDGE / PATTERN / UART. Với UART thì chỉnh được dòng `Baud/Clk`.

### Màn 3/5 và 4/5 - SÓNG CH0-3 / CH4-7

| Nút | Hành động |
|---|---|
| UP / DOWN, nhấn ngắn | Cuộn cửa sổ sóng sang trái/phải |
| DOWN, giữ | Thay đổi zoom: X1 → X2 → X4 → X8 → X16 → X32 → X64 → X128 → FIT → X1... |
| UP, giữ | Nhảy để vạch trigger nằm giữa màn hình |
| OK | Màn 3 → màn 4; màn 4 → màn 5/5 DECODE |
| BACK | Màn 4 → màn 3; màn 3 → màn 1/5 CẤU HÌNH ĐO |

- **Zoom FIT** = hiển thị toàn bộ buffer. **X16** là mức mở đầu với dữ liệu mô
  phỏng (tâm đúng vạch trigger, một bit UART ~13 px); **X8** là mức mặc định
  sau khi thu thật. Giữ DOWN đổi zoom **không nhảy về mẫu 0**: mẫu dưới con
  trỏ giữa được giữ nguyên vị trí.
- Vạch **đỏ** là vị trí trigger. Đường **trắng đứt nét** giữa màn là cursor đo;
  dữ liệu trượt bên dưới cursor.
- **Thước thời gian** dưới các track: vạch chia theo thang 1-2-5, nhãn là
  thời gian **tuyệt đối tính từ mẫu 0** (cùng đơn vị với cột TIME ở màn
  DECODE). Nhãn tự né nhau khi chật.
- **Thanh cuộn ngang** dưới cùng của vùng sóng: chiều dài thumb = tỉ lệ cửa
  sổ/buffer, vị trí = đang xem đoạn nào. FIT thì thumb phủ hết track.
- Dòng dưới cùng `T:<thời gian>` (ví dụ `T:80us`) là thời điểm của mẫu tại
  giữa màn; `STOP` / `RUN` / `FAIL` là trạng thái thu.

### Màn 5/5 - DECODE

Ba card chọn ở trên: **PROTO** (giao thức), **CHAN** (kênh), **RATE** (tốc độ).

| Nút | Browse mode | Edit mode |
|---|---|---|
| UP / DOWN, nhấn ngắn | Di chuyển dòng chọn trong bảng; dải sóng theo dòng đó | Đổi giá trị card đang sửa |
| UP / DOWN, giữ | **Đổi card đang chọn: PROTO ↔ bus config** (xem chú ý) | Đổi giá trị |
| OK, nhấn ngắn | Vào sửa card đang chọn | Lưu, chạy lại giải mã |
| **OK, giữ** | **→ màn 0 STANDBY** | → màn 0 STANDBY |
| BACK | → màn 4/5 SÓNG CH4-7 | Hủy sửa, thoát edit |

> **Chú ý:** trước đây "giữ OK" dùng để đổi giữa PROTO và bus config. Nay tác
> vụ đó chuyển sang **giữ UP/DOWN**, còn **giữ OK** được dành riêng để quay về
> màn chờ. Muốn đổi giao thức: chọn card PROTO, nhấn OK vào edit, UP/DOWN để
> chọn UART/I2C/SPI/CAN, OK để áp dụng.

---

## 4. Bảng phím USB tương đương

Cổng USB điều khiển nhận các ký tự sau, ánh xạ đúng sang nút vật lý:

| Ký tự | Nút | Loại |
|---|---|---|
| `u` / `U` | UP | Nhấn ngắn / giữ 600 ms |
| `d` / `D` | DOWN | Nhấn ngắn / giữ 600 ms |
| `o` / `O` | OK | Nhấn ngắn / giữ 600 ms (ở DECODE = về chờ) |
| `b` | BACK | Nhấn ngắn |
| `B` | BACK | **Giữ 800 ms** - lối tắt về màn chờ (từ màn cấu hình) |

Chữ thường = nhấn ngắn, chữ hoa = nhấn giữ; riêng `B` mang ngưỡng 800 ms.

---

## 5. Hiểu bảng dữ liệu màn DECODE

Bảng 5 dòng, mỗi dòng là **một gói/byte** do bộ giải mã tạo ra:

| Cột | Ý nghĩa |
|---|---|
| `#` | Số thứ tự gói (01, 02, ...) |
| `BYTE` | Giá trị byte dạng `0xNN`; `--` cho các dòng ACK/NAK (không có byte) |
| `ASCII` | Ký tự nếu in được (`'H'`), hoặc từ riêng của giao thức: `ACK`, `NAK`, `ADR`, `ID-hi`, `ID-lo`, `DLC` |
| `TIME` | Khoảng cách tới gói trước đó (`0`, `86us`, `1.2ms`, ...) |

- Dòng đang chọn có nền xanh; dải sóng phía trên chiếu đúng gói đó (thước bit
  ở dưới dải sóng, thanh màu cho biết gói nằm ở đâu trong toàn giao dịch).
- Thanh cuộn bên phải báo vị trí trong danh sách gói (`PKT:<n>` ở header).
- Ví dụ với UART "HELLO": `01|0x48|'H'|0`, `02|0x45|'E'|4us`, ...

---

## 6. Ngưỡng nhấn giữ

| Hành động | Ngưỡng | Áp dụng |
|---|---|---|
| Long press mặc định | 600 ms | Zoom/dổi kênh (màn sóng), đổi card (DECODE) |
| BACK về màn chờ | 800 ms | Chỉ ở màn 1/5 CẤU HÌNH ĐO, khi đang duyệt |

Debounce nút là 25 ms. Một lần nhả nút chỉ tạo **một** sự kiện (ngắn hoặc dài),
và firmware nhận kèm thời gian giữ thực tế để phân biệt 600 ms và 800 ms.

---

## 7. Dữ liệu mô phỏng (không cần phần cứng)

Lệnh `ui_load_demo()` nạp 60 000 mẫu @25 MHz mang 4 giao thức cùng lúc:

- **CH0:** UART 115200 8N1, nội dung "HELLO"
- **CH1/CH2:** I2C ghi `0xA2` `0x5A` (có ACK)
- **CH3/CH4/CH5:** SPI CS/SCK/MOSI, các byte `DE AD BE EF`
- **CH6:** xung vuông 50 kHz
- **CH7:** CAN chuẩn ID=`0x123` DLC=2, data `DE AD` (có bit stuffing)

Vì vậy màn 3/4/5 luôn có tín hiệu thật để quan sát ngay sau khi nạp demo.
