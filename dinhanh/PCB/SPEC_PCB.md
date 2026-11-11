# Đặc tả PCB đế cắm Pico 2 — Logic Analyzer 8 kênh

**Trạng thái:** Đã có schematic và PCB đi dây Rev A1 theo Layout A tại [project KiCad](LogicAnalyzer_Pico2/README.md); ERC/DRC đạt. Cần nhóm duyệt các điểm ở §8.2 trước khi phát hành chế tạo.  
**Ngày lập:** 09/11/2026. **Cập nhật:** 11/11/2026 — bản 1.2.  
**Căn cứ cập nhật:** [REPLY_SPEC_PCB.md](REPLY_SPEC_PCB.md), ngày 09/11/2026.  
**Kiến trúc:** Pico 2 (RP2350A) đã hàn chân đực, cắm vào socket cái trên PCB; build mục tiêu `PICO_BOARD=pico2`.

## 1. Mục tiêu và phạm vi duyệt

PCB tích hợp đầu đo, mạch bảo vệ 8 kênh, đế Pico 2, kết nối TFT ST7735, nút bấm, đèn báo và điểm kiểm tra thành một thiết bị gọn, dễ lắp và sửa chữa.

Pico đảm nhiệm lấy mẫu PIO/DMA, lưu RAM, xử lý và giao tiếp USB. PCB đế tận dụng MCU, Flash, thạch anh, ổn áp và cổng USB đã có trên Pico.

| Hạng mục | Cấu hình bản đầu |
|---|---|
| Số kênh | 8 kênh CH0–CH7, đi qua bảo vệ độc lập trước GP0–GP7 |
| Thu mẫu | Lấy mẫu đồng thời vào RAM, truyền và hiển thị sau |
| Kết nối PC | Cổng micro-USB trên Pico; USB CDC, giao thức LAR2 v1.2 theo phản hồi firmware và driver `rp2350-la` |
| Giao diện tại chỗ | Một module TFT ST7735 1,8 inch, 128×160, SPI1 và 4 nút chức năng |
| Nguồn | USB trên Pico; lấy 3,3 V từ Pico cấp cho khối phụ trợ sau khi kiểm tra ngân sách dòng |
| PCB | Đề xuất 2 lớp; kích thước xác định sau khi chốt linh kiện và cách lắp hộp |
| Hỗ trợ kiểm thử | LED, RESET, test point và khả năng tiếp cận SWD |

**Quan hệ với đề bài:** Đề tài 1 yêu cầu tối thiểu 2 kênh, 1 kHz; đề tài 2 yêu cầu tối thiểu 8 kênh, 25 MHz và hiển thị trên PC hoặc màn hình đạt yêu cầu. Cả hai đều yêu cầu bảo vệ 12 V DC liên tục, xung đơn 200 V/1 ms, mô phỏng ảnh hưởng lên dạng sóng và luận giải timing.

**Cấu hình nhóm chốt theo §1, STT 1 của phản hồi:** Topic 1, 8 kênh, lấy mẫu 25 MS/s. Phản hồi ghi Topic 1 nhất quán ở phần tóm tắt và bảng quyết định; phần giải thích trong phản hồi nhắc mức tối thiểu 8 kênh/25 MHz thực chất thuộc Topic 2 của đề bài. Cấu hình phần cứng vẫn giữ 8 kênh/25 MS/s; TFT là màn hình phụ trợ, PC là giao diện hiển thị chính. Không coi TFT 128×160 là màn hình HD.

**Dải tần tín hiệu đầu vào mục tiêu:** 1 kHz–1 MHz; cần nghiệm thu trên toàn đường đo qua bảo vệ. Các mức 3–5 MHz chỉ phục vụ khảo sát thêm, không phải dải bảo đảm. **25 MS/s là tốc độ lấy mẫu, không phải băng thông tín hiệu đầu vào.** Sai số timing cho phép còn phải chốt.

**Ngân sách nhóm:** BOM mục tiêu ≤ 450.000 đ; chưa có báo giá xác nhận. Đây là mục tiêu nội bộ, không ghi là giới hạn giá thành bắt buộc của đề bài.

## 2. Các khối trên PCB

```text
Đầu đo CH0–CH7 / GND
          │
          ▼
8 mạch bảo vệ độc lập ── OUT0–OUT7 ──► GP0–GP7 của Pico 2
                                             │
                        ┌────────────────────┼────────────────────┐
                        ▼                    ▼                    ▼
                  USB trên Pico        TFT qua SPI1         Nút bấm / LED
                        │
                        ▼
                  PC / PulseView

Nguồn USB ──► Pico 2 ── 3V3 OUT ──► TFT và các IC đệm
GND đầu đo, bảo vệ, Pico và giao diện nối chung; không có cách ly điện.
```

### 2.1. Đế cắm Pico 2

- Hai socket cái **1×20, bước 2,54 mm**; footprint phải đối chiếu bản vẽ cơ khí và board thật.
- Ký hiệu rõ chân 1, chiều cắm Pico và phía cổng USB trên silkscreen.
- Đặt cổng USB sát mép PCB, chừa chỗ cắm cáp và thao tác BOOTSEL.
- Kiểm tra chiều cao socket, khoảng hở dưới module và khả năng chạm linh kiện/mối hàn.
- Chừa khả năng tiếp cận SWD của Pico; các chân SWD nằm riêng, không mặc nhiên đi qua hai socket 20 chân.
- Có lỗ bắt vít PCB vào hộp, tránh bố trí linh kiện hoặc đường mạch dưới vùng siết vít.

### 2.2. Đầu nối tín hiệu đo

- Cung cấp CH0–CH7 và GND; chỉ đưa các kênh đo qua mạch bảo vệ rồi mới tới Pico.
- Chốt đầu nối **2×8, bước 2,54 mm**, một hàng tín hiệu và một hàng GND để mỗi kênh có chân đất bên cạnh.
- Pinout đã chốt: chân lẻ 1/3/5/7/9/11/13/15 lần lượt là CH0–CH7; các chân chẵn nối GND. Cần đối chiếu cách đánh số của footprint.
- Giữ pinout 2×8 thống nhất ở schematic, footprint và BOM; không dùng lại connector 1×10 của bản cũ. Chốt mã header và dây probe tương ứng trước khi đặt sản xuất.
- Ghi nhãn CH0–CH7, GND và chân 1 rõ ràng. Không đặt chân cấp nguồn DUT trong cùng đầu đo bản đầu.

### 2.3. Mạch bảo vệ 8 kênh

**Nguồn topology bảo vệ:** [mạch LTspice một kênh](../mach_bao_ve/ltspice/input_protection_1ch.asc), [netlist](../mach_bao_ve/ltspice/input_protection_1ch.cir) và [README](../mach_bao_ve/ltspice/README.md).

**Cấu hình PCB theo phản hồi:** mỗi kênh dùng **2×1 kΩ PR02 2 W nối tiếp**, tổng trở đầu vào 2 kΩ; nhân bản cho đủ 8 kênh. Đình Anh chịu trách nhiệm chốt mã đầy đủ, đặc tính chịu xung và footprint.

**Trạng thái hồ sơ mô phỏng tại workspace:** netlist LTspice hiện vẫn dùng một điện trở gộp `R6=2k`. Hai cấu hình tương đương về tổng trở lý tưởng; việc chia công suất, điện áp và ký sinh trên linh kiện thật cần tính/kiểm tra riêng. Cần cập nhật mô phỏng về hai điện trở và lưu kết quả trước khi chốt hồ sơ bảo vệ. Việc sửa spec này chưa thay đổi file LTspice.

```text
CHx_IN → 1k PR02 → 1k PR02 → CLAMP_x → 330 Ω → A_x → Buffer → 33 Ω → GPx

A_x → điện trở kéo xuống 47 kΩ → GND.

BAV99: diode dưới từ GND → CLAMP_x; diode trên từ CLAMP_x → Z_x.
Chiều mũi tên trên dòng BAV99 là chiều dẫn thuận của diode.

Z_x ── cathode Zener BZX85C3V3; anode Zener ── GND
Buffer SN74LVC1G17: VCC = 3V3; mỗi IC có tụ 100 nF về GND.
```

| Linh kiện | Mỗi kênh | Tổng 8 kênh | Yêu cầu |
|---|---:|---:|---|
| Điện trở đầu vào | 2 | 16 | 1 kΩ PR02 2 W nối tiếp; chốt đúng biến thể và mã chịu xung |
| Điện trở trước bộ đệm | 1 | 8 | 330 Ω |
| Điện trở kéo xuống | 1 | 8 | 47 kΩ |
| Điện trở đầu ra | 1 | 8 | 33 Ω, đặt gần chân Y |
| Diode dẫn hướng | 1 | 8 | BAV99, SOT-23 |
| Zener | 1 | 8 | BZX85C3V3; footprint theo mã linh kiện mua thực tế |
| Buffer Schmitt không đảo | 1 | 8 | SN74LVC1G17DBVR, SOT-23-5 |
| Tụ lọc nguồn | 1 | 8 | 100 nF, đặt sát VCC/GND của buffer |

**Quy tắc kết nối:**

- Từng `Z_x` là nút riêng, không nối với `3V3` và không gộp giữa các kênh.
- Chỉ `Y → 33 Ω → GPx` nối sang Pico. CLAMP/A/Z không nối trực tiếp GPIO.
- BAV99: pin 1 → GND; pin 2 → Z_x; pin 3 → CLAMP_x.
- SN74LVC1G17DBVR: pin 1 NC; pin 2 A; pin 3 GND; pin 4 Y; pin 5 VCC.
- Không thêm tầng 74HC245 hoặc thay mạch này bằng phương án kẹp vào rail 3,3 V nếu chưa đánh giá lại.
- `VIN`, `VCCSRC`, `CLOAD` và các linh kiện bên trong model SPICE không phải danh sách linh kiện để chép nguyên lên PCB. Nguồn VCCSRC được thay bằng rail 3,3 V thực; CLOAD đại diện tải ký sinh.

**Mục tiêu bảo vệ:** +12 V DC liên tục và một xung +200 V, độ rộng 1 ms. Đây là mục tiêu cần chứng minh, chưa phải thông số phần cứng đã nghiệm thu. Không suy rộng sang điện áp âm, xung lặp, ESD hoặc cách ly.

**Cơ sở chọn điện trở:** Phản hồi dẫn kết quả khoảng 9,62 W đỉnh / 9,57 mJ trên mỗi điện trở 1 kΩ ở xung 200 V/1 ms, tổng khoảng 19,2 mJ. Đây là số liệu tham chiếu danh định, cần truy xuất và chạy lại với cấu hình hai điện trở, dung sai và điều kiện thử đã chốt. Công suất ghi 2 W là định mức liên tục, không tự chứng minh chịu được xung này. Chọn đúng mã PR02 theo đường cong tải xung, điện áp, nhiệt và footprint; không thay bằng điện trở 0603 cùng trị số.

Model hiện là model đại diện, chưa bao gồm đầy đủ nhiệt, hư hỏng, Ioff, nguồn và ký sinh PCB. Chuỗi PWL hiện thử xung vuông 500 Hz ở 12 V, một xung 200 V/1 ms, rồi xung vuông 500 Hz ở 5 V; **chưa thay thế phép thử 12 V DC liên tục hoặc quét toàn dải tần tín hiệu**.

### 2.4. TFT ST7735 và nút bấm

- Chỉ dùng **một module TFT ST7735 1,8 inch, 128×160**, theo module đã được Nam Khánh thử. Không bố trí màn hình thứ hai.
- Giữ nguyên chân driver TFT: **SPI1 SCK=GP10, MOSI=GP11, CS=GP13, DC=GP14, RST=GP15, BL=GP16**. GP12 là nút OK, không thuộc khối TFT.
- Đầu nối cần **6 tín hiệu điều khiển + 3V3 + GND**, tổng 8 đường. Chốt pinout vật lý, mã module/header, kích thước và lỗ bắt vít theo module thực tế trước khi vẽ footprint.
- `BL` mức cao sáng theo giao diện firmware. Xác minh chân BL của module là tín hiệu điều khiển hay đầu cấp dòng LED: GP16 chỉ điều khiển; thêm tầng transistor/driver và hạn dòng nếu module yêu cầu, không mặc nhiên cấp dòng đèn nền trực tiếp từ GPIO.
- Bố cục TFT và số kênh mỗi trang theo firmware hiện hành; không lấy layout 128×64 trước đây làm yêu cầu nghiệm thu cho TFT.
- Bốn nút **UP=GP8, DOWN=GP9, OK=GP12, BACK=GP22**, nhấn nối GND; dùng pull-up nội bộ và debounce firmware theo phản hồi.
- Không bắt buộc bốn điện trở kéo lên 10 kΩ của bản cũ. Có thể chừa footprint DNP nếu cần dự phòng; ghi rõ trong BOM.
- Thêm nút **RESET** nối RUN xuống GND, tách biệt với các nút chức năng.
- Giữ chức năng OK xác nhận/Run/Stop theo trạng thái menu; firmware điều khiển tại chỗ còn phải bổ sung và kiểm tra với lệnh PC.
- Chừa khoảng cách thao tác và vị trí lắp màn hình/nút trên mặt hộp; có thể dùng header tới cụm giao diện tách bo.

### 2.5. Nguồn và USB

- Bản đầu dùng cổng micro-USB có sẵn trên Pico cho cả cấp nguồn và kết nối PC.
- Cấp nguồn khi dùng độc lập bằng adapter USB hoặc power bank; chưa cần pin, mạch sạc hay nguồn DC 12 V riêng.
- Lấy `3V3 OUT` từ Pico cấp cho buffer, TFT và LED. Lập ngân sách dòng theo đúng module và tình huống sử dụng; tài liệu Pico khuyến nghị tải ngoài trên chân này dưới 300 mA, còn phụ thuộc điều kiện hoạt động.
- Dự kiến thêm một tụ tổng 4,7–10 µF ở đầu cấp vào khối phụ trợ; chọn giá trị, điện áp và loại tụ khi chốt BOM.
- Không nối nguồn ngoài khác trực tiếp song song với 3V3 OUT của Pico.
- Chốt **USB-only**: không lắp nguồn ngoài, pin hoặc mạch sạc trong bản này; VSYS chỉ đưa ra test point. Nếu thay đổi phạm vi sau này, phải đánh giá đường cấp nguồn và chống cấp ngược riêng.
- Tính cả dòng đèn nền TFT, 8 buffer và LED. Không coi USB luôn cấp sẵn 500 mA trong mọi tình huống; kiểm tra nguồn/cáp, cấu hình USB và dòng tiêu thụ thực tế trước khi nghiệm thu.
- Không cần CH340/CP2102. Không cần đưa USB D+/D− qua PCB đế vì dùng cổng trên Pico; hai hàng GPIO không cung cấp các tín hiệu USB này.
- **12 V là điện áp sự cố ở đầu đo, không được cấp trực tiếp vào Pico.**

### 2.6. LED và khả năng kiểm tra

- Một LED **PWR** báo rail 3,3 V; một LED **STATUS** trên **GP26**, mắc GP26 → điện trở 1 kΩ → LED → GND, mức cao sáng. Ý nghĩa trạng thái xác định trong firmware.
- Mỗi LED có điện trở hạn dòng; dự kiến 1 kΩ, điều chỉnh theo màu LED và dòng mong muốn.
- Test point nguồn: `3V3`, `VSYS`, `GND`.
- Test point `OUT0…OUT7` để kiểm tra mức logic trước Pico.
- Trên ít nhất một kênh đại diện, thêm điểm đo `IN`, `CLAMP`, `A`, `Z`; giữ pad nhỏ để hạn chế tăng điện dung ký sinh.
- Giữ khả năng kết nối SWD. Nếu thêm header debug trên PCB đế, phải có cách nối thực tế tới chân SWD riêng của Pico.
- Có đầu `CAL_OUT` trên **GP27**, kèm GND, để phát xung 3,3 V thử kênh qua dây ngoài; **không nối cố định vào đầu vào đo**. Bố trí footprint trong bản đầu; nếu quyết định không lắp connector thì ghi DNP trong BOM.

## 3. Phân chân đã chốt — Layout A

Áp dụng Layout A trong §2.2 của phản hồi. **GP0–GP7 dành riêng cho capture; chân TFT giữ nguyên; UP, DOWN, BACK, STATUS, CAL_OUT được dời để tránh xung đột.** Số pin là số chân vật lý trên module Pico 2, không phải số GPIO.

| Chức năng | GPIO / tên chân | Pin Pico | Hướng / ghi chú |
|---|---|---:|---|
| CH0 | GP0 | 1 | Input sau bảo vệ; PIO base=0, `in pins, 8` |
| CH1 | GP1 | 2 | Input sau bảo vệ |
| CH2 | GP2 | 4 | Input sau bảo vệ |
| CH3 | GP3 | 5 | Input sau bảo vệ |
| CH4 | GP4 | 6 | Input sau bảo vệ |
| CH5 | GP5 | 7 | Input sau bảo vệ |
| CH6 | GP6 | 9 | Input sau bảo vệ |
| CH7 | GP7 | 10 | Input sau bảo vệ |
| BTN UP | GP8 | 11 | Input, pull-up nội bộ; nhấn nối GND |
| BTN DOWN | GP9 | 12 | Input, pull-up nội bộ; nhấn nối GND |
| TFT SCK | GP10 | 14 | SPI1 SCK, output |
| TFT MOSI | GP11 | 15 | SPI1 TX, output |
| BTN OK | GP12 | 16 | Input, pull-up nội bộ; nhấn nối GND |
| TFT CS | GP13 | 17 | Output, active low |
| TFT DC | GP14 | 19 | Output, chọn data/command |
| TFT RST | GP15 | 20 | Output, active low |
| TFT BL | GP16 | 21 | Output điều khiển đèn nền, mức cao sáng; kiểm tra tầng lái |
| BTN BACK | GP22 | 29 | Input, pull-up nội bộ; nhấn nối GND |
| LED STATUS | GP26 | 31 | Output qua 1 kΩ; mức cao sáng |
| CAL_OUT | GP27 | 32 | Output xung 3,3 V ra header test |
| Dự phòng | GP28 | 34 | Để trống |
| RESET | RUN | 30 | Input; nút xuống GND, dùng pull-up có sẵn |
| Nguồn phụ trợ | 3V3 OUT | 36 | Cấp buffer, TFT và LED |
| Nguồn hệ thống | VSYS | 39 | Chỉ đưa ra test point |
| GND | GND | 3, 8, 13, 18, 23, 28, 38 | Nối chung đầu đo, bảo vệ, Pico và giao diện |
| AGND | AGND | 33 | Nối GND trong thiết kế này; không coi là GPIO |

**Chân không sử dụng:** GP17–GP21 và GP28 để trống, không route chức năng trong bản này. GP23–GP25 và GP29 không có trên hai hàng header GPIO của Pico 2, không gán cho ngoại vi PCB đế.

**Mở rộng:** không triển khai 16 kênh ở bản này. Nếu mở lên GP0–GP15, phải dời TFT và các nút đang dùng trong dải đó, đổi firmware/giao thức/driver và mẫu dữ liệu sang dạng phù hợp 16 kênh, đồng thời thêm 8 mạch bảo vệ. Với buffer cố định 256 KiB và mẫu 16-bit, độ sâu còn 131.072 mẫu, khoảng 5,24 ms ở 25 MS/s. Đây là thiết kế phiên bản khác, không chừa đường nối mở rộng làm thay đổi Layout A hiện tại.

## 4. Yêu cầu bố trí và đi dây

1. Bố trí theo luồng **đầu đo → điện trở hạn dòng → kẹp → buffer → socket Pico**. Không đặt đường đầu vào chưa bảo vệ chạy sát GPIO/đầu ra phía sau.
2. Tám kênh có cấu trúc bố trí tương tự; đường tín hiệu ngắn, hạn chế via và khác biệt ký sinh. Không cần kéo dài tất cả đường chỉ để khớp chiều dài nếu làm tăng tải/nhiễu không cần thiết.
3. Đặt R_OUT gần chân Y; tụ 100 nF sát VCC/GND của từng buffer.
4. Giữ GND hồi dòng liên tục cho vùng logic. Bố trí đường dòng sự cố từ diode/Zener về đầu nối GND, tránh chạy vòng qua vùng nguồn/Pico.
5. Xác định clearance/creepage tại vùng có thể gặp 200 V theo điện áp, điều kiện sử dụng, vật liệu và công nghệ PCB; không lấy DRC mặc định của mạch logic làm chứng minh đủ cách điện.
6. Tách đường SPI TFT khỏi các đường đầu vào; tránh chạy song song sát nhau trên đoạn dài.
7. Nhãn đầu đo, TFT, nguồn, nút và chiều cắm Pico phải đọc được sau khi lắp module.
8. Kiểm tra cơ khí bằng bản in 1:1 hoặc mô hình 3D trước khi đặt sản xuất.

## 5. BOM sơ bộ để ước lượng quy mô

| Nhóm | Số lượng dự kiến |
|---|---:|
| Pico 2 đã hàn chân, module cắm rời | 1 |
| Socket cái 1×20 | 2 |
| Đầu nối đầu đo 2×8, bước 2,54 mm | 1, pin lẻ CH0–CH7 / pin chẵn GND |
| Điện trở đầu vào 1 kΩ PR02 2 W | 16, mỗi kênh hai con nối tiếp |
| Điện trở 330 Ω / 47 kΩ / 33 Ω | 8 con mỗi loại |
| BAV99 / BZX85C3V3 / SN74LVC1G17DBVR | 8 con mỗi loại |
| Tụ 100 nF cho buffer | 8 |
| Tụ nguồn tổng | 1, giá trị chốt sau |
| Module TFT ST7735 1,8 inch 128×160 và đầu nối 8 đường | 1 bộ |
| Tầng điều khiển BL/hạn dòng nếu module yêu cầu | Chốt theo module đã thử |
| Nút chức năng / nút RESET | 4 / 1 |
| Điện trở kéo lên nút chức năng | Không lắp mặc định; dùng pull-up nội bộ, footprint dự phòng DNP nếu có |
| LED và điện trở hạn dòng | 2 bộ |
| Test point, lỗ bắt vít | Theo bố trí |
| Đầu CAL_OUT + GND trên GP27 | 1; chừa footprint, ghi DNP nếu không lắp |

**Mục tiêu BOM ≤ 450.000 đ theo quyết định nhóm; chưa xác nhận bằng báo giá.** Cần ghi rõ tổng có bao gồm Pico, TFT, dây probe và PCB gia công hay không. Trước khi duyệt BOM cuối, cần ghi mã đặt hàng, package/footprint, thông số chịu xung, nơi mua và chi phí thực tế. Đặc biệt không chọn footprint điện trở đầu vào trước khi chọn mã chịu xung.

## 6. Tình trạng firmware/phần mềm và yêu cầu đồng bộ

**Nguồn trạng thái mới:** Nam Khánh xác nhận trong §4.2 của [phản hồi](REPLY_SPEC_PCB.md). Các mục “đã có” dưới đây là thông tin từ người phụ trách firmware, không phải kết quả chạy kiểm thử lại trong lần sửa spec này.

| Chức năng | Trạng thái theo phản hồi / yêu cầu tích hợp |
|---|---|
| PIO/DMA, RAM, USB CDC, LAR2 v1.2, driver PC | Đã có; Nam Khánh báo đã verify ở 25 MS/s với CAN/UART/I²C/SPI |
| Trigger cạnh / pattern / UART / pre-trigger | Đã có, Nam Khánh xác nhận verified ở 25 MS/s |
| Lưu nhiều phiên Flash | Đã có, 2 slot × 512 KiB; dùng Flash sẵn trên Pico |
| Golden compare, diff, giải mã tại chỗ | Đã có; decode 4 protocol theo phản hồi |
| TFT ST7735 | Đã có driver `tft_st7735.c`, đã thử trên board thật; giữ chân SPI1 ở Layout A |
| Nút bấm / LED / CAL_OUT | Chưa có driver; bổ sung theo Layout A và kiểm tra trên PCB |
| Run/Stop tại thiết bị | Chưa có; bổ sung và phối hợp với lệnh từ PC |
| Thông số f/T/duty trên TFT | Chốt bố cục và khả năng đo theo firmware mới; cần kiểm tra dữ liệu thiếu chu kỳ/không xác định được |
| Lưu/mở `.sr` trên PC | Có qua PulseView |

**Chênh lệch với nguồn đang có:** tại thời điểm sửa spec, `PulseView-custom/` trong workspace vẫn mô tả LAR2 v1/manual capture, mặc định CMake là `pico2_w` và chưa thấy file `tft_st7735.c`. Không dùng bản nguồn cũ đó để kết luận các chức năng mới chưa tồn tại, cũng không coi các báo cáo mới đã được kiểm chứng từ nguồn cũ.

Trước khi tích hợp PCB, Nam Khánh cần đồng bộ commit/source, cấu hình `PICO_BOARD=pico2`, hợp đồng LAR2 v1.2, driver/host tương ứng và log thử nghiệm. Giữ hồ sơ kiểm thử cũ như bằng chứng lịch sử; cập nhật tài liệu theo đúng phiên bản mới. Đây là công việc tiếp theo, chưa được thực hiện bởi lần sửa spec này.

Không cần thêm RAM, Flash, thẻ nhớ, FPGA hoặc chip USB–UART cho phạm vi hiện tại. Tính năng lưu Flash/so sánh/giải mã đã được đưa vào phạm vi firmware, không còn ghi là hạng mục mở rộng chưa có; kiểm thử lại chúng khi tích hợp board.

## 7. Kiểm tra và tiêu chí nghiệm thu dự kiến

| Hạng mục | Bằng chứng cần có |
|---|---|
| Cơ khí | Pico cắm đúng, không chạm linh kiện; cắm được USB, bấm được BOOTSEL/nút và lắp được hộp |
| Schematic/PCB | ERC/DRC đạt; kiểm tra pinout, footprint, cực diode, rail nguồn và đủ 8 kênh |
| Cấp nguồn | Đo 3,3 V, dòng tiêu thụ và trạng thái reset với Pico, TFT và buffer hoạt động |
| Nhận thiết bị PC | HELLO/GET_CAPS thành công; nhận đủ 8 kênh và cấu hình hiện hành |
| Mapping và logic | Cấp mẫu 0/3,3 V đã biết vào từng đầu đo; xác nhận đúng CH0–CH7, cả HIGH/LOW và các cạnh |
| Timing | Đo chu kỳ, độ rộng xung và lệch giữa kênh qua mạch bảo vệ; so với nguồn/thiết bị tham chiếu |
| Toàn vẹn truyền | Đủ số mẫu, metadata và CRC hợp lệ; dữ liệu khớp mẫu thử |
| Quá áp DC | Mô phỏng +12 V DC liên tục, đối chiếu điện áp/dòng/công suất với linh kiện và điều kiện công bố |
| Xung quá áp | Mô phỏng một xung +200 V/1 ms, kiểm tra đỉnh điện áp và năng lượng trên linh kiện; chưa tổ chức thử xung thật trong phạm vi này |
| Băng thông đầu vào | Quét tần số, mức logic và dung sai trong dải dự kiến; đánh giá méo xung và timing, không chỉ điện áp đỉnh |
| TFT/nút | Màn hình khởi tạo đúng, BL hoạt động; 4 nút không ảnh hưởng SPI; cấu hình, thu và xem đủ 8 kênh theo bố cục firmware khi dùng nguồn USB độc lập |
| LED/CAL_OUT | LED STATUS đúng lệnh; CAL_OUT trên GP27 có tần số đã biết, đo kiểm trước khi dùng tự kiểm tra |
| Flash/so sánh/giải mã tại chỗ | Kiểm tra lưu/đọc lại 2 slot, giữ dữ liệu qua reset, golden compare/diff và decode theo kịch bản Nam Khánh cung cấp |
| Giải mã PC | Chạy lại CAN/UART/I²C/SPI qua đầu đo đã bảo vệ; đối chiếu dữ liệu biết trước và log thử của firmware mới |

Nghiệm thu dải đầu vào mục tiêu **1 kHz–1 MHz** với mức logic và sai số timing được nhóm chốt. Đánh giá 25 MS/s bằng tín hiệu có tần số/quan hệ thời gian đã biết; không cần tín hiệu đầu vào đạt 25 MHz. Đo trực tiếp GPIO, nếu có, chỉ đánh giá riêng khối lấy mẫu và không thay thế phép thử toàn đường đo qua bảo vệ. Các số liệu biên độ 2,88 V ở 3 MHz và 2,42 V ở 5 MHz trong phản hồi là tham chiếu mô hình, không phải băng thông phần cứng đã nghiệm thu.

## 8. Quyết định đã chốt và việc còn lại

### 8.1. Quyết định áp dụng theo phản hồi

- [x] **Topic 1, 8 kênh, 25 MS/s** theo phản hồi; không tăng số kênh bản này. Cấu hình vượt số kênh/tốc độ tối thiểu Topic 1; yêu cầu bảo vệ vẫn áp dụng đầy đủ.
- [x] **Pico 2 (RP2350A)**, build `PICO_BOARD=pico2`.
- [x] Phạm vi PCB: đế Pico + bảo vệ 8 kênh + một TFT ST7735 + 4 nút + RESET + LED + test point.
- [x] **TFT ST7735 1,8 inch 128×160, SPI1**, chân giữ theo Layout A; bỏ khối màn hình cũ, không route GP17–GP21.
- [x] Điện trở đầu vào PCB **2×1 kΩ PR02 2 W/kênh**, tổng 16 con; Đình Anh hoàn thiện chọn mã/tính toán/mô phỏng.
- [x] Đầu đo **2×8**, pin lẻ CH0–CH7, pin chẵn GND; thống nhất schematic/BOM.
- [x] Nút UP/DOWN/OK/BACK = **GP8/GP9/GP12/GP22**, pull-up nội bộ.
- [x] LED STATUS = **GP26**; CAL_OUT = **GP27**; GP28 để trống.
- [x] **USB-only**, không lắp pin, mạch sạc hoặc nguồn ngoài.
- [x] Mục tiêu BOM **≤ 450.000 đ**, dải tần đầu vào **1 kHz–1 MHz**; chưa coi là kết quả nghiệm thu.

### 8.2. Việc còn phải hoàn thiện trước khi phát hành PCB

- [ ] Khi nộp báo cáo, thống nhất tên Topic 1 theo phản hồi và mô tả đúng các mức tối thiểu trong đề bài; không suy ra Topic 2 chỉ từ cấu hình 8 kênh/25 MS/s.
- [ ] Nam Khánh đồng bộ source/commit firmware, TFT, LAR2 v1.2, driver/host và bằng chứng thử nghiệm; xác nhận bản build cho Pico 2.
- [ ] Đình Anh chốt mã PR02 1 kΩ 2 W, biến thể/package, Zener, BAV99, buffer và dữ liệu chịu xung.
- [ ] Cập nhật LTspice từ R6=2k sang hai điện trở 1k, kiểm tra lại DC/xung/tần số/timing và lưu kết quả.
- [ ] Chốt mã module TFT, pinout 8 đường, nguồn/dòng BL, tầng lái nếu cần, cơ khí và bố cục giao diện.
- [x] Đã vẽ schematic, PCB và BOM theo Layout A, connector 2×8 trong [project KiCad Rev A1](LogicAnalyzer_Pico2/README.md); kiểm tra ERC/DRC và parity đạt, chưa phát hành chế tạo.
- [ ] Đo/tính ngân sách nguồn: 8 buffer + TFT/BL + LED và Pico; chốt tụ nguồn.
- [ ] Chốt mã dây probe, socket, nút/LED, test point, kích thước PCB/hộp, khoảng cách cách điện và lỗ bắt vít.
- [ ] Chốt báo giá trong ngân sách, phạm vi các khoản chi và các linh kiện DNP nếu có.
- [ ] Chốt mức logic thử, sai số timing, độ lặp lại và các log/ảnh cần có để nghiệm thu dải 1 kHz–1 MHz.
- [ ] Nhóm review [schematic/PDF](LogicAnalyzer_Pico2/review/Schematic.pdf), [layout PCB](LogicAnalyzer_Pico2/review/PCB_top.svg) và cơ khí trước khi đặt sản xuất. Theo yêu cầu người dùng ngày 11/11, đã dựng cả layout để duyệt cùng schematic; ERC/DRC đều đạt, không coi đây là duyệt sản xuất.

### 8.3. Thay đổi của bản 1.1

Thay màn hình bằng TFT ST7735/SPI1; áp dụng Layout A; đổi 1×2 kΩ thành 2×1 kΩ PR02 trong cấu hình PCB; chốt connector 2×8, USB-only và ngân sách; cập nhật trạng thái firmware theo Nam Khánh; sửa liên kết tương đối vì tài liệu đã chuyển vào `dinhanh/PCB/`. Lần cập nhật này chỉ sửa spec, không thay schematic, BOM chế tạo, firmware hoặc file mô phỏng.

### 8.4. Bản triển khai KiCad Rev A1 — 11/11/2026

- Project: [LogicAnalyzer_Pico2](LogicAnalyzer_Pico2/README.md), có schematic/PCB native, PDF, BOM, ảnh 3D và Gerber/drill để duyệt.
- PCB **160×110 mm**, 2 lớp, FR4 1,6 mm, 4 lỗ Ø3,2 mm. Đế Pico 2 là hai socket cái 1×20 P2,54, cách hàng 17,78 mm; USB ra mép trên.
- Dùng footprint PR02 riêng, thân tối đa 10×Ø3,9 mm, pitch 17,78 mm, lỗ 1,2 mm. Mã Cu-lead 1% đề xuất `PR02000201001FA100`, cần kiểm tra mã mua và dữ liệu chịu xung.
- Linh kiện tín hiệu chuyển sang **0805** để dễ hàn; tụ khối 10 µF dùng 1206. Không thay đổi trị số bảo vệ 2×1 kΩ / 330 Ω / 47 kΩ / 33 Ω.
- **J2 là header cáp 1×8 có nhãn**, pin 1 GND, 2 3V3, 3 SCK, 4 MOSI, 5 RST, 6 DC, 7 CS, 8 BL. Chưa có pinout/cơ khí module TFT thật nên không khẳng định cắm trực tiếp hoặc đặt lỗ bắt màn hình cố định.
- BL được lái qua Q1 BC817-40/Q2 BC807-40, GP16 HIGH sáng; R47 mặc định 100 Ω/≥0,25 W giới hạn dòng. Cần đối chiếu cấu trúc BL của module, không mặc định thay R47 bằng 0 Ω.
- Kết quả KiCad 10.0.8: **ERC 0 lỗi/0 cảnh báo; DRC 0 vi phạm, 0 kết nối thiếu, 0 sai khác schematic**; 84 kiểm tra topology/pinout đạt. Không dùng ngoại lệ DRC để che lỗi.
- Sửa cách dẫn phản hồi: cả tóm tắt và bảng chốt của `REPLY_SPEC_PCB.md` hiện đều ghi **Topic 1**. Cấu hình vẫn 8 kênh/25 MS/s. Những điều kiện về chịu xung, dòng nguồn, timing, module TFT, chi phí và cơ khí còn phải nghiệm thu; kết quả ERC/DRC không thay thế các phép kiểm tra này.

## 9. Tài liệu căn cứ

1. [Phản hồi Nam Khánh](REPLY_SPEC_PCB.md): căn cứ quyết định phạm vi, Layout A và trạng thái firmware mới.
2. [Đề bài học phần 2026.1](<../../Documents/BTL Hệ thống nhúng và thiết kế giao tiếp nhúng 2026.1.pdf>), mục 3: yêu cầu kỹ thuật.
3. [Báo cáo nhóm Hiệp](../../Documents/tham_khao/nhom_Hiep.pdf), trang 28–30: tổ chức LCD/nút/LED, nguồn, debug và bố trí PCB. Nhóm tham khảo dùng MCU trên bo riêng; ở thiết kế này các khối đã có trên module Pico được tận dụng.
4. [Báo cáo nhóm Phúc](../../Documents/tham_khao/nhom_Phuc.pdf), trang PDF 51: tách front-end, thu mẫu, nguồn và phần mềm host; không sao chép phần FPGA/Ethernet vào PCB đế Pico.
5. [README PulseView-custom](../../PulseView-custom/README.md), [firmware](../../PulseView-custom/firmware/main.c), [giao thức LAR2](../../PulseView-custom/protocol/PROTOCOL.md).
6. [Hồ sơ bảo vệ LTspice](../mach_bao_ve/ltspice/README.md), [kết quả timing mô hình](../mach_bao_ve/ltspice/verification_timing.txt).
7. [Raspberry Pi Pico 2 Datasheet](https://datasheets.raspberrypi.com/pico/pico-2-datasheet.pdf): cơ khí, pinout, nguồn và SWD.
8. [TI SN74LVC1G17 Datasheet](https://www.ti.com/lit/ds/symlink/sn74lvc1g17.pdf): chân IC, ngưỡng, nguồn và giới hạn điện.
9. [Nexperia BAV99](https://assets.nexperia.com/documents/data-sheet/BAV99.pdf), [Vishay BZX85](https://www.vishay.com/docs/85607/bzx85.pdf): kiểm tra chân, thông số điện và package trước khi chốt BOM.
