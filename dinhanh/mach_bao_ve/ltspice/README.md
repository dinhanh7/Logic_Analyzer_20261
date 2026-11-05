# LTspice — mạch bảo vệ một kênh CH1

Mở **`input_protection_1ch.asc`** bằng LTspice và nhấn **Run**.
Giữ toàn bộ file `.asy` và `protection_models.lib` cùng thư mục với `.asc`.
Không cần cài thêm thư viện symbol: các symbol riêng đã được kèm theo.

Đây là **một mạch duy nhất**, dùng các reference theo hình bạn gửi:

- R6, R7: 1 kΩ; khi làm thật dùng PR02 Cu-lead 2 W, 1%, chịu xung.
- R8: 330 Ω; R9: 47 kΩ; R10: 33 Ω.
- XD3: một BAV99 gồm hai diode trong cùng vỏ SOT-23.
- D4: một Zener BZX85C3V3, cathode nối Z, anode nối GND.
- XU2: bộ đệm Schmitt không đảo, mô hình gần đúng cho SN74LVC1G17DBVR.
- C2: 100 nF giữa VCC và GND; nguồn VCCSRC mặc định 3,3 V.
- CLOAD: 10 pF giả định cho chân Pico và dây nối; thêm để quan sát đầu ra có tải.
- Nguồn VIN là nguồn thử, không phải linh kiện trên PCB.

## Chọn phép thử

Nhấp phải dòng `.param CASE=2 VSUP=3.3 VLOGIC=3.3 FREQ=1Meg` và sửa `CASE`:

| CASE | Nguồn VIN | Mục đích |
|---:|---|---|
| 0 | 12 V DC không đổi | Kiểm tra điện áp kẹp và công suất ở trạng thái DC |
| 1 | Xung vuông 0–VLOGIC, tần số FREQ | Kiểm tra dạng xung trước/sau bộ đệm |
| 2 | Một xung +200 V, đỉnh phẳng 1 ms, sườn 20 ns | Kiểm tra đáp ứng quá áp; **mặc định** |

Với CASE=1, đổi `VLOGIC` thành 3.3, 5 hoặc 12; thử `FREQ=1k`, `100k`,
`1Meg`, `3Meg`, `5Meg`. Mô phỏng lấy 10 chu kỳ sau thời gian chờ 1 µs.
Đây là tần số tín hiệu vào; tần số lấy mẫu 25 MS/s của Pico không đặt vào tham số này.

Chỉ dùng CASE 0, 1 hoặc 2. Các tham số nguồn và thời gian mô phỏng bên dưới
được tính tự động từ CASE; không cần sửa nguồn VIN cho ba phép thử trên.

## Quan sát khi chạy

Nhấp các dây hoặc chọn **Add Trace**:

- `V(IN)`: điện áp từ DUT giả lập.
- `V(CLAMP)`: sau hai điện trở hạn dòng, tại pin giữa BAV99.
- `V(Z)`: nút kẹp riêng của Zener.
- `V(A)`: điện áp thực sự đưa vào bộ đệm.
- `V(OUT)`: tín hiệu sau bộ đệm và điện trở 33 Ω, về Pico.
- `-I(VIN)`: dòng nguồn đi vào mạch.

Khi thử 200 V nên đặt `V(IN)` ở một plot pane riêng, và `V(A)`, `V(CLAMP)`,
`V(OUT)` ở pane khác để xem rõ mức vài volt. Có thể dùng **Add Plot Pane**.
Các biểu thức công suất hữu ích:

```text
R6: V(IN,MID)*I(R6)
R7: V(MID,CLAMP)*I(R7)
D4: V(Z)*(-I(D4))
```

Điện áp Zener dương ở cathode nhưng dòng SPICE của D4 quy ước từ anode sang cathode,
nên công suất Zener dùng dấu trừ trên dòng. Với BAV99, dòng từng diode có thể xem
qua các linh kiện bên trong subcircuit XD3, tùy giao diện chọn trace của LTspice.

Các lệnh `.meas` đã đặt sẵn đo `A_MAX`, `CLAMP_MAX`, `OUT_MAX`, `IIN_MIN`.
Sau Run mở **SPICE Error Log** để xem. `IIN_MIN` âm do chiều quy ước nguồn VIN;
trị tuyệt đối của nó là dòng đầu vào đỉnh trong các phép thử dương này.

## Model và giới hạn diễn giải

**Bản vẽ đúng kết nối mạch, nhưng model không phải model được nhà sản xuất xác nhận.**

- BAV99 dùng model diode đại diện, Cjo=1,5 pF, Tt=4 ns.
- Zener có BV=3,3 V, IBV=80 mA; Rs=2 Ω và Cjo=500 pF là giá trị giả định.
- Bộ đệm dùng công tắc có hysteresis và nguồn hành vi: ngưỡng lên 2,20 V,
  ngưỡng xuống 1,10 V, điện dung vào 6 pF, trễ RC xấp xỉ 5 ns và Rout=20 Ω.
  Các số này minh họa cho nguồn 3,3 V, chưa phải đặc tính worst-case của IC TI.
- Model bộ đệm không mô phỏng Ioff, cấp nguồn ngược, dòng nguồn thật, quá áp
  phá hủy, nhiệt hoặc chuyển nguồn. Đổi VSUP không tự hiệu chỉnh ngưỡng Schmitt.
- Điện trở lý tưởng trong SPICE không tự cháy khi quá công suất. Phải đối chiếu
  công suất/năng lượng/điện áp đo được với đường cong chịu xung PR02.
- Nguồn DUT lý tưởng, chưa có dây probe, điện cảm đường mạch, sai số linh kiện
  hay mô hình nhiệt. Nhánh Z tách biệt nguồn VCC, đúng như sơ đồ gốc.
- Đặt Gear/maxord=1 để giảm ringing số trong mô hình hành vi. Khi nghiên cứu
  overshoot hay timing, cần giảm bước thời gian và kiểm tra hội tụ kết quả.

Không dùng kết quả mô hình này để kết luận mạch thật đã chắc chắn chịu 200 V/1 ms.
Nó phục vụ kiểm tra kết nối, nguyên lý kẹp và đáp ứng danh định; bước tiếp theo là
thay model xác nhận, quét dung sai/nhiệt/ký sinh và đối chiếu phép đo thực.

## Kiểm tra đã thực hiện

`check_schematic.py` đọc trực tiếp vị trí dây và chân symbol trong `.asc`, kiểm tra
đúng topology và xuất netlist `input_protection_1ch.cir`. Đặc biệt xác nhận:

```text
XD3: pin 1 → GND, pin 2 → Z, pin 3 → CLAMP
D4: anode → GND, cathode → Z
XU2: A → nút A; Y → R10 → OUT; nguồn → VCC/GND
```

Thứ tự port mô phỏng của XU2 là A, Y, VCC, GND; tương ứng chân vỏ 2, 4, 5, 3.
Chân 1 NC của IC không có port điện trong model. Tiền tố X biểu thị subcircuit SPICE.

Máy tạo file không có chương trình LTspice để chạy kiểm tra trực tiếp `.asc` trong
giao diện. Đã kiểm tra netlist tương đương bằng **ngspice 36** cho cả ba CASE;
kết quả lưu tại `verification_ngspice.txt`. Đây là kiểm tra chéo, không phải log LTspice.

Chạy lại kiểm tra cấu trúc bằng:

```bash
python3 check_schematic.py
```

## Datasheet tham khảo

- [TI SN74LVC1G17](https://www.ti.com/lit/ds/symlink/sn74lvc1g17.pdf)
- [Nexperia BAV99](https://assets.nexperia.com/documents/data-sheet/BAV99.pdf)
- [Vishay BZX85](https://www.vishay.com/docs/85607/bzx85.pdf)
- [Vishay PR01/PR02/PR03](https://www.vishay.com/docs/28729/pr010203.pdf)
