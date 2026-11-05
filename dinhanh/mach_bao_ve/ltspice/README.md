# LTspice — mạch bảo vệ một kênh CH1

Mở **`input_protection_1ch.asc`** bằng LTspice và nhấn **Run**.
Giữ `input_sequence.pwl`, `simulation_commands.inc`, toàn bộ file `.asy` và `protection_models.lib` cùng thư mục với `.asc`.
Các ghi chú quanh mạch đã được bỏ; cấu hình mô phỏng nằm trong `simulation_commands.inc`.
Bên dưới mạch chỉ giữ hai lệnh `.include` và `.tran` để LTspice nạp cấu hình và chạy mô phỏng.
Không cần cài thêm thư viện symbol: các symbol riêng đã được kèm theo.

Đây là **một mạch duy nhất**, dùng các reference theo hình bạn gửi:

- R6: 2 kΩ, gộp hai điện trở R6/R7 1 kΩ nối tiếp trước đây.
  Khi lắp thật, chọn điện trở chịu xung khoảng 19,2 W trong 1 ms (19,2 mJ),
  điện áp trên điện trở khoảng 196 V; cần đối chiếu datasheet của đúng linh kiện.
  Việc gộp tương đương về điện trở trong mô phỏng, không tự bảo đảm khả năng chịu xung.
- R8: 330 Ω; R9: 47 kΩ; R10: 33 Ω.
- XD3: một BAV99 gồm hai diode trong cùng vỏ SOT-23.
- D4: một Zener BZX85C3V3, cathode nối Z, anode nối GND.
- XU2: bộ đệm Schmitt không đảo, mô hình gần đúng cho SN74LVC1G17DBVR.
- C2: 100 nF giữa VCC và GND; nguồn VCCSRC mặc định 3,3 V.
- CLOAD: 10 pF giả định cho chân Pico và dây nối; thêm để quan sát đầu ra có tải.
- Nguồn VIN là nguồn thử, không phải linh kiện trên PCB.

## Chuỗi sự kiện nguồn thử

Nguồn VIN chạy một chuỗi PWL duy nhất khi nhấn **Run**:

| Khoảng thời gian | Điện áp VIN |
|---|---:|
| 0–12 ms | Xung vuông 0–12 V, chu kỳ 2 ms, 6 chu kỳ |
| 12–12,00002 ms | Chuyển từ 0 V lên 200 V trong 20 ns |
| 12,00002–13,00002 ms | Giữ 200 V đúng 1 ms |
| 13,00002–13,00004 ms | Chuyển từ 200 V về 0 V trong 20 ns |
| 13,00004–25,00004 ms | Xung vuông 0–5 V, chu kỳ 2 ms, 6 chu kỳ |

Hai đoạn xung vuông có tần số 500 Hz, duty cycle danh định 50%: mỗi chu kỳ
gồm khoảng 1 ms mức cao và 1 ms mức thấp. Sườn lên/xuống dài 20 ns.
Chuỗi không lặp lại; kết thúc ở 0 V sau sáu chu kỳ 5 V.

```text
PWL FILE="input_sequence.pwl"
```

File `input_sequence.pwl` chứa hai cột thời gian (giây) và điện áp (volt).
`VSUP=3.3` cấp nguồn riêng cho bộ đệm. Mô phỏng kết thúc tại 25,00004 ms,
bước thời gian tối đa `TMAX=1u`; trình mô phỏng tự giảm bước quanh các sườn.
Khi đánh giá chi tiết overshoot, giảm `TMAX` và kiểm tra hội tụ kết quả.

## Quan sát khi chạy

Nhấp các dây hoặc chọn **Add Trace**:

- `V(IN)`: điện áp từ DUT giả lập.
- `V(CLAMP)`: sau điện trở hạn dòng R6, tại pin giữa BAV99.
- `V(Z)`: nút kẹp riêng của Zener.
- `V(A)`: điện áp thực sự đưa vào bộ đệm.
- `V(OUT)`: tín hiệu sau bộ đệm và điện trở 33 Ω, về Pico.
- `-I(VIN)`: dòng nguồn đi vào mạch.

Khi thử 200 V nên đặt `V(IN)` ở một plot pane riêng, và `V(A)`, `V(CLAMP)`,
`V(OUT)` ở pane khác để xem rõ mức vài volt. Có thể dùng **Add Plot Pane**.
Các biểu thức công suất hữu ích:

```text
R6: V(IN,CLAMP)*I(R6)
D4: V(Z)*(-I(D4))
```

Điện áp Zener dương ở cathode nhưng dòng SPICE của D4 quy ước từ anode sang cathode,
nên công suất Zener dùng dấu trừ trên dòng. Với BAV99, dòng từng diode có thể xem
qua các linh kiện bên trong subcircuit XD3, tùy giao diện chọn trace của LTspice.

Các lệnh `.meas` đã đặt sẵn đo `A_MAX`, `CLAMP_MAX`, `OUT_MAX`, `IIN_MIN`.
`DELAY_F` đo trễ cạnh xuống đầu tiên của đoạn 5 V, từ VIN = 2,5 V đến OUT = 1,65 V.
`BUFFER_F` đo từ A đi xuống qua 1,1 V đến OUT = 1,65 V.
Sau Run mở **SPICE Error Log** để xem. `IIN_MIN` âm do chiều quy ước nguồn VIN;
trị tuyệt đối của nó là dòng đầu vào đỉnh trong các phép thử dương này.

## Model và giới hạn diễn giải

**Bản vẽ đúng kết nối mạch, nhưng model không phải model được nhà sản xuất xác nhận.**

- BAV99 dùng model diode đại diện, Cjo=1,5 pF, Tt=4 ns.
- Zener có BV=3,3 V, IBV=80 mA; Rs=2 Ω và Cjo=500 pF là giá trị giả định.
- Bộ đệm dùng hai công tắc bổ sung có hysteresis và nguồn hành vi: ngưỡng lên 2,20 V,
  ngưỡng xuống 1,10 V, điện dung vào 6 pF, trễ RC xấp xỉ 5,5 ns và Rout=20 Ω.
  Các số này minh họa cho nguồn 3,3 V, chưa phải đặc tính worst-case của IC TI.
- Đã sửa lỗi model cũ: RSTATE = 1 MΩ làm tụ CDELAY = 7,2 pF xả chậm,
  gây trễ cạnh xuống khoảng 5 µs. Model hiện tại có nhánh kéo xuống 100 Ω
  khi trạng thái thấp, đối xứng với nhánh kéo lên. Đây là sửa model,
  không phải bổ sung linh kiện vào PCB. Mạch bảo vệ đầu vào vẫn có trễ RC riêng.
- Model bộ đệm không mô phỏng Ioff, cấp nguồn ngược, dòng nguồn thật, quá áp
  phá hủy, nhiệt hoặc chuyển nguồn. Đổi VSUP không tự hiệu chỉnh ngưỡng Schmitt.
- Điện trở lý tưởng trong SPICE không tự cháy khi quá công suất. Phải đối chiếu
  công suất/năng lượng/điện áp đo được với đường cong chịu xung của điện trở 2 kΩ được chọn.
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

`verification_ngspice.txt` lưu kết quả lịch sử của ba phép thử CASE riêng biệt,
trước khi đổi sang chuỗi PWL. Các số đó không phải kết quả mô phỏng chuỗi hiện tại.
Sau khi Run, xem log mới để đánh giá chuỗi xung vuông 12 V → xung 200 V → xung vuông 5 V.

Chạy lại kiểm tra cấu trúc bằng:

```bash
python3 check_schematic.py
```

## Datasheet tham khảo

- [TI SN74LVC1G17](https://www.ti.com/lit/ds/symlink/sn74lvc1g17.pdf)
- [Nexperia BAV99](https://assets.nexperia.com/documents/data-sheet/BAV99.pdf)
- [Vishay BZX85](https://www.vishay.com/docs/85607/bzx85.pdf)
- [Vishay PR01/PR02/PR03](https://www.vishay.com/docs/28729/pr010203.pdf)
