# Mạch bảo vệ đầu vào Logic Analyzer 8 kênh — Rev A

Mở `input_protection.kicad_pro` bằng KiCad 10, sau đó mở Schematic Editor.
Sơ đồ nguyên lý đầy đủ nằm trong `input_protection.kicad_sch`; bản xem nhanh là
`input_protection.pdf` (một trang A2). Thư viện symbol riêng được kèm theo nên
không phụ thuộc việc máy đã cấu hình thư viện symbol toàn cục hay chưa.
Đây là sơ đồ nguyên lý, chưa có thiết kế PCB.

Thiết kế này thay thế phương án linh kiện trong bản phác thảo
`../BVMach_SPEC.text`. Bản phác thảo được giữ nguyên để tham khảo.

## Mục tiêu và phạm vi

- 8 ngõ vào, ngõ ra 0/3,3 V nối sang Pico 2, nguồn lấy từ cùng miền 3,3 V với Pico.
- Mục tiêu bảo vệ của đề bài: **+12 V DC liên tục**, **một xung +200 V dài 1 ms**.
- Tín hiệu đo dự kiến: CMOS 3,3 V, 5 V và tín hiệu 0–12 V có chung GND.
- Dải đầu tiên cần xác nhận thực nghiệm: 1 kHz–1 MHz. Mô phỏng sơ bộ được mở rộng
  đến 3 MHz và 5 MHz để đánh giá giới hạn; chưa công bố băng thông bảo đảm 5 MHz.
- 25 MS/s là tốc độ lấy mẫu của hệ thống, không phải tần số xung tối đa đầu vào.
- Các con số dưới đây áp dụng cho mô hình nguồn thử và linh kiện được mô tả;
  chưa chứng nhận ESD, sét, xung lặp, điện áp âm hoặc sử dụng cách ly.

## Cấu trúc một kênh

```text
DUT ── 1k PR02 ── 1k PR02 ── CLAMP ── 330R ── A ── SN74LVC1G17 ── 33R ── Pico
                               │                 │
                               │                47k
                               │                 │
                               │                GND
                               │
                  BAV99: diode trên từ CLAMP sang Z
                         diode dưới từ GND sang CLAMP
                               │
                  Z ── cathode BZX85C3V3
                       anode ── GND
```

Hai diode BAV99 dẫn dòng vượt mức sang nhánh Zener hoặc về GND. Zener 3,3 V
nằm trên **nút kẹp riêng Z**, không nối với nguồn `+3V3`. Điện dung Zener vì vậy
không được mắc trực tiếp song song đường tín hiệu; điện dung diode dẫn hướng
và linh kiện thực tế vẫn phải được tính trong mô phỏng băng thông.

SN74LVC1G17 cấp nguồn 3,3 V có đầu vào chịu mức đến 5,5 V theo điều kiện hoạt động
khuyến nghị. Vì vậy nút A không nhất thiết phải bị kẹp ở 3,6 V như bản phác thảo.
**Chỉ đầu ra Y của bộ đệm được nối tới GPIO**, và đầu ra này theo nguồn 3,3 V.
Không nối CLAMP/A/Z tới GPIO hoặc tới nguồn Pico.

R 330 Ω hạn dòng vào diode kẹp âm bên trong bộ đệm khi cần; R 47 kΩ kéo A về LOW
khi để hở đầu đo. R 33 Ω đặt sát đầu ra bộ đệm để giảm ringing trên dây sang Pico.
Mỗi bộ đệm có tụ 100 nF, toàn khối thêm 4,7 µF.

Bỏ phương án kẹp BAT54S vào nguồn 3,3 V: dòng quá áp qua diode trên có thể nâng
nguồn/cấp nguồn ngược, không thể mặc nhiên coi là chống back-powering. Phương án
mới không dẫn dòng sự cố vào nguồn này; tính năng Ioff của bộ đệm hỗ trợ trạng thái
mất nguồn. Vẫn cần thử nghiệm chuyển nguồn và điện áp rò với linh kiện thực.

## BOM chính

Danh sách đầy đủ từng reference, footprint và datasheet nằm trong `BOM.csv`.

| Linh kiện | Mỗi kênh | Tổng | Giá trị / yêu cầu |
|---|---:|---:|---|
| Điện trở hạn dòng xung | 2 | 16 | 1 kΩ, 1%, Vishay **PR02 loại Cu-lead 2 W** |
| Điện trở vào bộ đệm | 1 | 8 | 330 Ω, 0603 |
| Điện trở kéo xuống | 1 | 8 | 47 kΩ, 0603 |
| Điện trở đầu ra | 1 | 8 | 33 Ω, 0603 |
| Diode dẫn hướng | 1 | 8 | Nexperia BAV99, SOT-23 |
| Zener | 1 | 8 | Vishay BZX85C3V3, DO-41 |
| Bộ đệm Schmitt | 1 | 8 | TI SN74LVC1G17DBVR, SOT-23-5 |
| Tụ decoupling | 1 | 8 | 100 nF, X7R, ≥10 V, 0603 |
| Tụ nguồn chung | — | 1 | 4,7 µF, X7R, ≥10 V, 0603; kiểm tra suy giảm điện dung DC |
| Đầu nối | — | 2 | Header 1×10, bước 2,54 mm |

**Không thay PR02 bằng điện trở 0603 thông thường cùng trị số.** Hai điện trở
đầu vào chịu công suất tức thời khoảng 10 W/con khi có xung 200 V. Datasheet
PR02 có đường cong tải xung riêng; footprint axial được chọn cho đúng họ linh kiện.
Hai điện trở nối tiếp chia điện áp và năng lượng xung. Kiểm tra kích thước, loại
chân và mã đặt hàng chính xác trước khi mua/đặt PCB.

## Đầu nối và chân IC

| Chân | J1 — DUT | J2 — Pico |
|---:|---|---|
| 1…8 | CH0_IN…CH7_IN | CH0_OUT…CH7_OUT |
| 9 | GND | +3V3 từ Pico |
| 10 | GND | GND |

J2 là đầu nối tín hiệu, **không mô phỏng vị trí chân vật lý của board Pico**.
Gán CH0…CH7 sang GPIO phù hợp với firmware của nhóm khi đi dây.

- BAV99: pin 1 = anode diode dưới → GND; pin 2 = cathode diode trên → Z;
  pin 3 = điểm giữa → CLAMP.
- BZX85: cathode → Z; anode → GND.
- SN74LVC1G17DBVR: 1 NC, 2 A, 3 GND, 4 Y, 5 VCC. Pin 1 đã đánh dấu không nối.
- Nhãn **Z_0…Z_7 độc lập**, tuyệt đối không gộp với `+3V3`.

## Tính toán kiểm tra sơ bộ

Với xung nguồn lý tưởng +200 V, bỏ qua điện áp kẹp để chặn trên dòng:

```text
Rseries,min = 2 × 1000 × 0,99 = 1980 Ω
Imax       ≤ 200 / 1980 = 101,1 mA
P_R,bound  ≤ (101,1 mA)² × 1010 Ω ≈ 10,32 W/con
E_R,bound  ≤ 10,32 W × 1 ms ≈ 10,32 mJ/con
```

Đây là chặn trên bảo thủ, không phải công suất liên tục. So với đường cong PR02
ở 1 ms trong datasheet, mức này nằm dưới đường tải xung có tỷ số chu kỳ/thời gian
xung 1000. Thiết kế chỉ yêu cầu xung đơn; không suy rộng thành chịu 200 V DC hoặc
xung lặp tùy ý. Điện áp khoảng 100 V/con cũng thấp hơn điện áp làm việc của PR02.

Ở +12 V DC, chặn trên dòng tương ứng 6,06 mA; công suất mỗi điện trở <38 mW.
Mô phỏng danh định cho dòng thấp hơn vì điện áp bị giữ trên nhánh kẹp.

BAV99 có giới hạn xung đơn 1 ms là 1 A tại điều kiện datasheet; dòng dự kiến khoảng
0,1 A. Zener được chọn 1,3 W, còn công suất xung ước lượng khoảng 0,35–0,4 W.
Điều kiện tản nhiệt/đường chân trong datasheet vẫn phải được tuân thủ khi làm PCB.

BZX85C3V3 được quy định 3,1–3,5 V ở 80 mA. Ở khoảng 100 mA, phải xét độ tăng áp
động và nhiệt độ; không lấy 3,3 V cố định để kết luận bảo vệ. Dùng 20 Ω làm ước lượng
độ dốc quanh điểm thử cho ngân sách điện áp khoảng 3,9 V trên Zener, cộng tới
1,25 V trên BAV99 cho khoảng 5,2 V tại CLAMP. **Ước lượng này chưa phải bảo đảm
worst-case trên toàn nhiệt độ/tốc độ cạnh**; cần mô hình vendor và thử overshoot.
Mục tiêu ở A là giữ trong dải khuyến nghị của bộ đệm, không dựa vào absolute maximum.

Đầu vào kéo xuống tạo phân áp: khi diode chưa dẫn,
`VA ≈ Vin × 47k / (47k + 2k + 330) ≈ 0,953 × Vin`.
Không công bố hỗ trợ logic 1,8 V hoặc toàn bộ chuẩn TTL chỉ từ phép tính này;
phải đối chiếu ngưỡng Schmitt của đúng IC với biên độ tín hiệu DUT.

## Kết quả kiểm tra và mô phỏng

- KiCad 10.0.8 đã đọc và xuất PDF thành công.
- ERC: **0 lỗi, 0 cảnh báo**, không thêm exclusion để che lỗi.
- `check_netlist.py` kiểm tra cả 8 kênh từ XML netlist: đúng chân đầu nối,
  BAV99, Zener, bộ đệm; không có diode kẹp nối vào nguồn `+3V3`.
- Đã chạy ngspice 36, 17 trường hợp: +12 V DC, +200 V/1 ms và tín hiệu
  0–3,3/5/12 V ở 1 kHz, 100 kHz, 1 MHz, 3 MHz, 5 MHz.

| Trường hợp | Kết quả mô hình danh định |
|---|---|
| +12 V DC | A ≈ 3,813 V; dòng nguồn ≈ 4,08 mA; mỗi điện trở đầu vào ≈ 16,65 mW |
| +200 V/1 ms | A đỉnh ≈ 4,323 V; CLAMP đỉnh ≈ 4,356 V; dòng nguồn đỉnh ≈ 98,1 mA |
| Năng lượng xung tại R1 | ≈ 9,57 mJ; công suất đỉnh ≈ 9,62 W |
| 0–3,3 V, 1 MHz | A cao ≈ 3,131 V, thấp gần 0 V |
| 0–3,3 V, 3 MHz | A cao ≈ 2,880 V, thấp ≈ 1 mV |
| 0–3,3 V, 5 MHz | A cao ≈ 2,416 V: biên dự trữ nhỏ, chưa công bố là dải bảo đảm |

Các file `.cir`, `.log`, `results.csv` và hình `simulation/waveforms.png` được kèm theo.
**Mô hình diode là mô hình đại diện tự khai báo, không phải model SPICE đã xác nhận
của nhà sản xuất.** Tham số Zener được neo BV/IBV ở điểm thử, điện dung Zener giả
định 500 pF, điện dung vào bộ đệm 6 pF, ký sinh tại CLAMP 2 pF. Nguồn DUT lý tưởng,
sườn xung sự cố 20 ns, sườn tín hiệu đo 5 ns, duty 50%, nhiệt độ ngspice mặc định 27°C.

Mô phỏng này kiểm tra phần analog trước bộ đệm. Kiểm tra ngưỡng 2,4/0,8 V chỉ là
phép sàng lọc biên độ, chưa chứng minh độ rộng xung, timing hay dạng sóng đầu ra U.
Chưa bao gồm mô hình IC TI, dây probe, nguồn, tolerance, điện cảm đường mạch,
nhiệt và tuổi thọ linh kiện. Khi thay đổi nguồn thử/linh kiện, phải chạy lại.

Để hoàn thành phần chứng minh theo đề bài cần mô phỏng với model đã xác nhận,
quét nhiệt độ/tolerance/độ rộng xung và parasitic, đo độ trễ lên/xuống, biến dạng
duty và sai lệch giữa kênh, sau đó đo trên mạch thực. Đặc biệt thử lúc mất nguồn,
lúc đóng nguồn và khi nhiều kênh cùng chịu xung. Mạch hiện là **bản thiết kế nguyên
lý có kiểm tra sơ bộ**, chưa phải sản phẩm đã chứng nhận chịu xung 200 V.

## Chạy lại

```bash
kicad-cli sch erc --exit-code-violations --format json -o erc_report.json input_protection.kicad_sch
kicad-cli sch export pdf -o input_protection.pdf input_protection.kicad_sch
kicad-cli sch export netlist --format kicadxml -o input_protection.net.xml input_protection.kicad_sch
python3 check_netlist.py
python3 simulation/run_simulation.py
```

Script mô phỏng cần `ngspice`, `numpy`, `matplotlib`. Có thể chạy từng `.cir` bằng
`ngspice -b <file.cir>`; script chính tái tạo đường dẫn dữ liệu theo thư mục hiện tại.
Script tạo schematic cần `sexpdata` và đường dẫn thư viện KiCad phù hợp qua biến
`LA_KICAD_SYMBOL_DIR`; chỉ dùng khi muốn tạo lại toàn bộ bản vẽ, vì thao tác đó
sẽ ghi đè schematic và các chỉnh sửa thủ công.

## Nguồn linh kiện

- [TI — SN74LVC1G17 datasheet](https://www.ti.com/lit/ds/symlink/sn74lvc1g17.pdf)
- [Nexperia — BAV99 datasheet](https://assets.nexperia.com/documents/data-sheet/BAV99.pdf)
- [Vishay — BZX85 datasheet](https://www.vishay.com/docs/85607/bzx85.pdf)
- [Vishay — PR01/PR02/PR03, đặc biệt trang 9–10](https://www.vishay.com/docs/28729/pr010203.pdf)
