# PCB đế Pico 2 — Logic Analyzer 8 kênh, Layout A

**Rev A1 — 11/11/2026. Bản thiết kế để nhóm duyệt, chưa phát hành sản xuất.**

Đã vẽ schematic và PCB 2 lớp **160 × 110 mm**, đi dây, đổ đồng GND, bố trí 4 lỗ M3. Pico 2 đã hàn chân đực cắm vào **hai socket cái 1×20, bước 2,54 mm**, khoảng cách hai hàng **17,78 mm**. USB trên Pico quay ra mép trên; dùng USB này để cấp nguồn và giao tiếp PC.

Căn cứ: [spec](../SPEC_PCB.md), [phản hồi Nam Khánh](../REPLY_SPEC_PCB.md), [mạch bảo vệ LTspice](../../mach_bao_ve/ltspice/README.md). Không thay đổi source firmware hoặc file mô phỏng trong lần vẽ PCB này.

## Mở và duyệt

- Mở **[LogicAnalyzer_Pico2.kicad_pro](LogicAnalyzer_Pico2.kicad_pro)** trong KiCad 10, rồi mở Schematic Editor / PCB Editor.
- Schematic native: [LogicAnalyzer_Pico2.kicad_sch](LogicAnalyzer_Pico2.kicad_sch); PCB native: [LogicAnalyzer_Pico2.kicad_pcb](LogicAnalyzer_Pico2.kicad_pcb).
- [Schematic PDF](review/Schematic.pdf), [ảnh 3D mặt trên](review/PCB_3D_top.png), [ảnh 3D phối cảnh](review/PCB_3D_isometric.png).
- [Mặt trên SVG](review/PCB_top.svg), [mặt dưới SVG](review/PCB_bottom.svg), [bản đồ lắp linh kiện](review/Assembly.svg).
- [PDF mặt trên tỉ lệ 1:1](review/PCB_top_1to1.pdf), [PDF mặt dưới tỉ lệ 1:1](review/PCB_bottom_1to1.pdf): in **Actual size / 100%**, kiểm tra bằng thước trước khi đối chiếu socket.
- [BOM từng vị trí](BOM.csv), [BOM gộp và phần mua ngoài bo](BOM_grouped.csv).
- [Gerber + drill để duyệt](fabrication_review/). Các file này chưa được nhóm xác nhận để đặt làm bo.

Symbol, footprint và model cần dùng được kèm trong project bằng đường dẫn `${KIPRJMOD}`. Không cần thư viện riêng đã bị xóa ở `mach_bao_ve/kicad/`. Xem [nguồn thư viện](LIBRARY_SOURCES.md). Model PR02 là hình minh họa theo kích thước danh định; model Pico đi kèm thư viện KiCad minh họa cơ khí dòng Pico, không thể hiện chính xác linh kiện RP2350 trên Pico 2. Chưa dựng model TFT/hộp vì chưa có mã module và cơ khí thực tế.

## Chức năng và kết nối

### Tám kênh bảo vệ

Mỗi kênh có đường tín hiệu:

```text
J1 CHn → 1k PR02 → 1k PR02 → CLAMP_n → 330Ω → A_n → SN74LVC1G17 → 33Ω → GPn
                                                       │
                                                      47k
                                                       │
                                                      GND

BAV99: pin 1 = GND; pin 2 = Z_n; pin 3 = CLAMP_n
BZX85C3V3: cathode = Z_n; anode = GND
```

Tổng cộng **16 điện trở PR02 Cu-lead 1 kΩ/2 W**. Mã đề xuất theo cấu trúc mã Vishay là **PR02000201001FA100** (1%); cần đối chiếu mã đặt mua thực tế, không thay bằng PR02 FeCu công suất khác hoặc điện trở SMD thông thường. Footprint riêng: thân tối đa 10 × Ø3,9 mm, khoảng cách chân 17,78 mm, lỗ khoan 1,2 mm. Chừa khoảng trống giữa các thân điện trở; các điện trở/tụ tín hiệu còn lại dùng 0805 để dễ hàn tay, tụ khối dùng 1206.

**Z_n là nút kẹp riêng từng kênh, không nối rail 3V3.** Chỉ ngõ Y của buffer qua điện trở 33 Ω tới Pico. Test point gồm OUT0…OUT7, 3V3, VSYS, GND; CH0 có thêm IN/CLAMP/A/Z. TP là pad đồng trên bo, không phải linh kiện phải mua.

Đồng GND mặt dưới nằm từ x = 65 mm sang vùng bảo vệ/Pico/giao diện; không trải dưới phần lớn vùng điện trở đầu vào. Có đường GND riêng rộng 0,8 mm vòng dưới vùng điện trở nối về đầu probe. Đây là thiết bị **chung GND với DUT/PC**, không cách ly.

### Đầu nối

| Đầu nối | Pin | Chức năng |
|---|---|---|
| J1 — 2×8 | 1,3,5,7,9,11,13,15 | CH0…CH7 |
| J1 — 2×8 | 2,4,6,8,10,12,14,16 | GND |
| J2 — 1×8 | 1 / 2 | GND / 3V3 |
| J2 | 3 / 4 | TFT SCK GP10 / MOSI GP11 |
| J2 | 5 / 6 | TFT RST GP15 / DC GP14 |
| J2 | 7 / 8 | TFT CS GP13 / BL sau tầng lái GP16 |
| J3 — 1×2 | 1 / 2 | CAL_OUT GP27 / GND |

**J2 là đầu cáp được gán tín hiệu rõ ràng, chưa phải đầu cắm trực tiếp tương thích mọi module ST7735.** Phải nối dây theo nhãn của module thật. Chưa đặt lỗ bắt vít cố định cho TFT; dùng gá/hộp riêng sau khi đo module.

### Nút, LED, nguồn

| Vị trí | Chức năng | Pico |
|---|---|---|
| SW1 / SW2 | UP / DOWN, nhấn nối GND | GP8 / GP9 |
| SW3 / SW4 | OK / BACK, nhấn nối GND | GP12 / GP22 |
| SW5 | RESET, nhấn nối GND | RUN |
| D17 | PWR từ 3V3 qua 1 kΩ | — |
| D18 | STATUS qua 1 kΩ, HIGH sáng | GP26 |
| J3 | Xung tự kiểm tra 3,3 V | GP27 |

Các nút chức năng dùng pull-up nội và debounce firmware; RUN đã có pull-up trên module. GP17…GP21 và GP28 để trống. CAL_OUT không nối sẵn vào đầu đo; nối bằng dây khi kiểm tra và không dùng để cấp nguồn DUT.

Nguồn duy nhất là USB trên Pico; rail 3V3 từ pin 36 cấp buffer, TFT, BL và LED. VSYS chỉ đưa ra TP10. Có C9/C10 10 µF/10 V và 100 nF cạnh mỗi buffer. Chưa đo tổng dòng; yêu cầu tải ngoài nằm trong ngân sách nguồn Pico, mục tiêu dưới 300 mA. Kiểm tra dòng module TFT, BL và sụt áp thực tế trước khi duyệt.

### Tầng lái BL bổ sung

Q1 BC817-40 và Q2 BC807-40 tạo công tắc cấp 3,3 V cho BL, **GP16 HIGH → sáng**, giữ logic Layout A. GPIO chỉ cấp dòng base khoảng 0,5 mA, không trực tiếp cấp dòng LED nền. Có điện trở kéo để BL tắt khi GPIO ở trạng thái thả nổi.

R47 mặc định **100 Ω, 0805, chọn công suất ≥0,25 W** để giới hạn dòng tối đa lý thuyết 33 mA nếu đầu BL chập GND. Với module có điện trở hoặc mạch BL riêng, độ sáng có thể thấp. Chỉ thay trị số sau khi biết cấu trúc BL và dòng thực tế; không tự lắp 0 Ω trước khi kiểm tra. Tầng này dùng với đầu BL nhận mức/dòng dương 3,3 V; nếu module có BL kiểu khác phải sửa trước khi cắm.

## Kết quả kiểm tra bản vẽ

KiCad **10.0.8**:

| Kiểm tra | Kết quả |
|---|---:|
| [ERC schematic](review/ERC.json) | 0 lỗi, 0 cảnh báo |
| [DRC PCB, gồm đối chiếu schematic](review/DRC.json) | 0 vi phạm |
| Kết nối PCB còn thiếu | 0 |
| Sai khác PCB với schematic | 0 |
| [Kiểm tra topology/pinout riêng](review/connectivity_check.json) | 84 kiểm tra đạt |

Không thêm ngoại lệ DRC để bỏ qua lỗi. Quy tắc bo: clearance tín hiệu mặc định 0,22 mm; net trước hai điện trở đầu vào 0,75 mm; rộng dây nhỏ nhất 0,20 mm, danh định 0,25 mm; via tối thiểu 0,60/0,30 mm; cách mép đồng 0,50 mm. Các trị số này là lựa chọn layout prototype, không phải chứng nhận cách điện hay mức đo CAT.

Đã dùng đường đi ngắn trong từng front-end; dây buffer→Pico có chiều dài khác nhau, chưa cân bằng timing bằng đo thực nghiệm. Kiểm tra 84 điều kiện dùng netlist xuất thật để xác nhận pin probe, hai điện trở nối tiếp, các nút Z riêng, mapping Layout A và việc không nối nút trước buffer vào chân capture.

Đi dây phần còn lại bằng **Freerouting 2.5.0 chạy cục bộ**, sau đó sửa copper bằng KiCad và kiểm tra lại DRC. Không gửi thiết kế lên dịch vụ route đám mây. File DSN/SES được giữ trong `review/` để truy vết; **PCB native hiện tại đã có chỉnh sửa sau SES**, nên không import lại SES lên bản này.

## Những điểm cần nhóm chốt trước khi đặt làm

1. **Đối chiếu Pico/socket thật:** in PDF 1:1, thử khoảng cách chân và chiều cắm, chiều cao socket, USB/BOOTSEL/SWD. Không cắm ngược Pico. Lỗ H1…H4 là Ø3,2 mm tại (4;4), (156;4), (4;106), (156;106) mm, tính từ góc trên trái.
2. **Module TFT thật:** thứ tự chân, nguồn 3,3 V, cấu trúc/dòng BL, dây nối J2 và chỗ gá màn hình. Không cấp 5 V lên J2.
3. **Linh kiện bảo vệ:** xác nhận mã PR02 Cu, đường cong chịu xung/điện áp, mã BAV99/BZX85C3V3/SN74LVC1G17DBVR và package. Kiểm tra điện áp chịu được của đầu nối/dây probe được mua. Trị số 2×1 kΩ trên PCB khớp tổng R=2 kΩ của mô hình cũ, nhưng hồ sơ LTspice vẫn cần cập nhật thành hai linh kiện và xét dung sai/ký sinh.
4. **Điện và timing:** kiểm thử 0/3,3 V trước, mapping từng kênh, dòng nguồn, TFT/nút/BL; đánh giá +12 V DC và mô phỏng xung +200 V/1 ms với linh kiện thực tế; xác minh dải 1 kHz–1 MHz và lệch thời gian giữa kênh. **25 MS/s không có nghĩa băng thông đầu vào 25 MHz. ERC/DRC không chứng minh chịu quá áp.**
5. **Cơ khí và chi phí:** duyệt kích thước 160×110 mm, cách gá TFT/hộp, báo giá PCB/linh kiện và mục tiêu ≤450.000 đ. Chưa có báo giá để xác nhận đạt ngân sách.

## Script và tái tạo

Các script nằm trong `scripts/`; đây là công cụ tạo bản ban đầu, **chạy lại có thể ghi đè sửa tay**. Hai file native hiện tại là kết quả bàn giao chính.

Thứ tự dựng lại: `generate_schematic.py` (Python có `sexpdata`) → KiCad CLI xuất `review/netlist.xml` → `generate_board.py` (Python có `pcbnew`) → Freerouting xuất `review/routed.ses` → `finish_board.py` → `refine_board.py` → `add_preview_models.py` → chạy lại ERC/DRC và `verify_connectivity.py`. Phải xuất lại PDF/Gerber sau mỗi thay đổi.

`generate_board.py` tạo phần bố trí và dây cục bộ, không tự tái tạo phần route từ Freerouting. KiCad đang cài qua AppDir có thể chạy các script PCB bằng `AppRun python3.11 <script>`. Các file native mở bằng KiCad không cần Python hay Freerouting.
