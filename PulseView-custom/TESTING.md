# RP2350 8-channel Logic Analyzer — Kết quả kiểm thử và tài liệu nguồn cho slide

Ngày cập nhật: **18/11/2026**. Tài liệu này dùng để lập slide/thuyết trình và bàn giao
cho nhóm phát triển. Bảng số liệu cuối lấy từ QA v2 thực thi trên hai board thật;
không biến kế hoạch, mô phỏng hay test chưa chạy thành thành tích đã đạt.

- Kế hoạch định lượng: [TEST-PLAN-v2](docs/TEST-PLAN-v2.md).
- Báo cáo từng case, bằng chứng và defect: [TEST-REPORT](docs/TEST-REPORT.md).
- Hướng dẫn chạy trước thầy: [DEMO](docs/DEMO.md).
- Việc để phiên sau: [OPINION](OPINION.md).
- Kết quả mới nhất: xem [results/LATEST.txt](results/LATEST.txt).
- Bản TESTING cũ được giữ trong [history](docs/history/TESTING-before-qa-v2.md).

## 1. Giới thiệu sản phẩm

Sản phẩm là **logic analyzer 8 kênh dựa trên RP2350/Pico 2**, dùng để thu và xem
các mức logic số, tìm thời điểm sự kiện bằng trigger và đọc nội dung UART/I2C/SPI/CAN.
Thiết bị có hai cách quan sát: TFT ST7735 ngay trên board và PulseView trên PC.

Mục đích thực tế: hỗ trợ học tập, trình diễn và debug các bus logic 3,3V trên bàn
thí nghiệm, chẳng hạn xác định byte UART, địa chỉ/ACK I2C, dãy MOSI SPI và CAN ID.
Đây là phép đo mức số. Hình xung vuông trên TFT biểu diễn 0/1; thiết bị không đo
biên độ, overshoot, độ dốc cạnh analog như oscilloscope.

**Câu giới thiệu ngắn để đưa lên slide:**

> Logic analyzer RP2350 lấy mẫu đồng thời 8 kênh, tối đa 25 MS/s, có trigger và
> giải mã bốn giao thức; xem trực tiếp trên TFT hoặc PulseView. Đường USB được
> kiểm bằng CRC, sequence, bộ đếm hai đầu và thử nghiệm streaming liên tục.

## 2. Các con số cần hiểu đúng

| Thông số | Giá trị và ý nghĩa |
|---|---|
| Kênh capture | GP0–GP7 tương ứng CH0–CH7; một byte chứa trạng thái cả 8 kênh |
| Sample rate tối đa snapshot | 25.000.000 mẫu/giây, tương ứng bước thời gian 40 ns |
| Manual buffer | 262.144 mẫu = 256 KiB dữ liệu số |
| Triggered window tối đa | 131.072 mẫu; pre-trigger tối đa 50% |
| Cấu hình demo | 25 MHz,65.536 mẫu,pre32.768,CH0 falling khi đủ cửa sổ |
| Cửa sổ demo tại25 MHz | 2,62144 ms; mỗi nửa1,31072 ms |
| Streaming | Tối đa1 MHz với RLE cho traffic nén được; raw500 kHz đã kiểm |
| TFT | ST7735,128×160portrait,SPI20 MHz; sáu màn hình gồm standby |
| Bộ nhớ ELF build cuối | text88.796byte, BSS444.684byte; UF2file165.888byte. Không cộng flash và RAM để so với520KiBSRAM |
| PC software | Driver C libsigrok `rp2350-la`,sigrok-cli và PulseView custom |
| Host tools | Python; giao thức USB CDC LAR2 với CRC32 và sequence |

**Không chia RAM cho8 lần nữa:** mỗi mẫu đã gói tám bit của tám kênh.262.144byte
là262.144thời điểm cho cả8 kênh, không phải32.768thời điểm.

| Rate | Manual262144 | Trigger131072 | Demo65536 |
|---|---:|---:|---:|
| 1 MHz |262,144 ms|131,072 ms|65,536 ms|
| 5 MHz |52,4288 ms|26,2144 ms|13,1072 ms|
| 25 MHz |10,48576 ms|5,24288 ms|2,62144 ms|

25 MS/s là **tốc độ lấy mẫu**, không phải cam kết giải mã mọi bus25 MHz. SPI1 MHz
ở sample1 MHz chỉ có một mẫu/bit, dễ alias. Trong demo streaming, SPI được hạ100 kHz;
trong snapshot25 MHz dùng SPI1 MHz. CAN250kbps tại1 MHz có4 mẫu/bit; để đánh giá timing
và xem chi tiết nên dùng snapshot25 MHz.

## 3. Kiến trúc và luồng dữ liệu

```text
ESP32 / DUT3.3V → GP0..GP7 → PIO → DMA → SRAM
                                         ├→ decoder C → framebuffer → TFT
                                         └→ LAR2/USB CDC → host Python / libsigrok
                                                              └→ PulseView + sigrok decoders
```

PIO chốt tám kênh trong cùng một lệnh IN, DMA đưa dữ liệu vào SRAM. CPU xử lý
trigger/cửa sổ và đóng gói truyền USB. Pico dùng vòng lặp sự kiện Pico SDK C,
không có bằng chứng đã chuyển Pico sang FreeRTOS; FreeRTOS đang dùng ở ESP32.

**Snapshot:** thu nhanh vào RAM rồi truyền lên PC. Vì vậy25 MHz capture không
đồng nghĩa USB đang chuyển liên tục25MB/s.

**Streaming:** tái sử dụng vùng samples thành4slot×64 KiB. CPU gửi các block nhỏ,
RLE biểu diễn cặp độ dài/mức logic khi có lợi; nếu nén không có lợi, block chuyển
sang raw. Có khoảng đệm hữu ích ba slot, khoảng196,6 ms tại1 MHz. Nếu PC chậm quá,
phải ghi nhận mất mẫu, không được gửi bộ nhớ bị ghi đè như thể dữ liệu liên tục.

PC kiểm CRC từng frame, thứ tự sequence, offset mẫu, bộ đếm sent/lost/captured,
flag phần cứng và quan hệ số mẫu–thời gian capture. File có gap có thể được lưu
để chẩn đoán với phần thiếu được pad, nhưng JSON phải `valid:false` và lệnh trả
mã lỗi. Giá trị pad không phải mức logic đã đo.

TFT tạm hoãn redraw trong lúc streaming để ưu tiên USB. Sau STOP, ring được cố
định, xoay về thứ tự thời gian, rồi TFT có thể xem cửa sổ cuối. Không quảng cáo
TFT là màn hiển thị live cập nhật liên tục trong suốt stream.

## 4. Thiết lập đo thật

Hai board đã kết nối vào máy và chung GND:

| Kênh Pico | GPIO ESP32 | Tín hiệu / decoder |
|---|---|---|
| CH0 / GP0 |26|UART TX1152008N1,`LA1234\n`|
| CH1 / GP1 |18|I2CSCL|
| CH2 / GP2 |19|I2CSDA,address0x68,register0x01,data0x5A|
| CH3 / GP3 |21|SPICS|
| CH4 / GP4 |22|SPISCK|
| CH5 / GP5 |23|SPIMOSI,`DE AD BE EF 00 01 02 03`|
| CH6 / GP6 |25|SPIMISO; cần loopback23–25 nếu muốn cùng payload|
| CH7 / GP7 |32|CAN logicTX250kbps,standardID0x123,DLC4|

CH6 không có cạnh từ nguồn ngoài trong wiring hiện tại. Do đó đã kiểm14 tổ hợp
edge/polarity trên7kênh hoạt động; hai tổ hợp CH6 và bài kiểm đầy đủ8 nguồn/skew
được ghi BLOCKED. Hiển thị tám channel không thay cho chứng minh cả tám input
đã nhận nguồn chuẩn độc lập.

Tần số chuẩn tương đối được tạo bằng LEDC của ESP32, chẩn đoán `stress 1000`
đưa xung1kHz50% vào GPIO26 vốn đã nối CH0, đồng thời ngừng UART. Không cần thay
dây sang GPIO4. Đây là đối chiếu giữa hai MCU, chưa phải hiệu chuẩn bằng thiết
bị tham chiếu đã hiệu chuẩn.

Các lệnh console được giữ trong một session và đợi ESP32 boot. Mở/đóng CP2102
liên tục có thể tác động reset; một command được gửi trước khi app sẵn sàng có
thể mất. Harness kiểm echo để không ngầm coi lệnh chưa được áp dụng là thành công.

## 5. Chúng ta đã làm gì theo từng giai đoạn?

### Giai đoạn nền — kiểm từng tầng

Đã có unit test cho LAR2, matcher trigger, decoder, AUTO, UI, host, streaming và
PTY integration của driver. ESP32 cung cấp bốn bus biết trước để kiểm đường thật.
Các commit lịch sử `1956e72`,`981b7a2`,`576873a` xử lý cửa sổ triggered và dựng bài
kiểm ESP32 snapshot/streaming. Những kết quả này là nền, chưa đủ chứng minh GUI
có annotation hay stream không bị thiếu tail.

### Giai đoạn demo thật

Đã thêm script demo, điều khiển TFT hiển thị capture thật, lưu `.pvs` gán đúng
channel/baud và tự Run trong PulseView. Commit `460d90e` là HEAD thực tế trước
lượt tối ưu/QA hiện tại, kế tiếp `ef6574d` và báo cáo `0149146`.

Không dùng `UI_DEMO` hay waveform tổng hợp thay thế nếu capture thật lỗi.
PulseView phải sinh annotation trong chính tiến trình GUI; CLI chỉ là oracle
bổ sung. JSON GUI đọc lại rate,count,trigger source/slope và các annotation visible.

### Giai đoạn review streaming và firmware

Đo baseline trước khi sửa đã thấy: RLE1 MHz30s thiếu424 mẫu ở PC so với counter Pico;
raw500 kHz15s thiếu968 mẫu. Công cụ cũ vẫn in0 drop vì đã bỏ các STREAM_BLOCK khi chờ
RESULT của STOP. Đã sửa drain tail và bắt buộc đối chiếu counters.

Review thêm ring producer, DMA IRQ, counter rollover, bộ nhớ stack và phản ứng khi
PC dừng đọc. Đã tăng stack trong giới hạn4 KiB của scratch bank, chuyển buffer
frame lồng nhau sang static, dùng CRC nibble table, chia nhỏ lần ship và kiểm
producer sau mỗi block. PIO streaming được đổi sang vòng vô hạn, tránh hết bộ đếm
X sau2^32 mẫu. Thời gian71,6 phút là suy từ counter cũ; không nhận đã soak71 phút thật.

### Giai đoạn tối ưu TFT/UX và nút

Font được làm lại nét mảnh có khoảng cách, màu chữ phụ rõ hơn, card ít nhiễu.
Màn3/4 vẽ segment ngang/dọc; so mức trước mỗi cột để không mất cạnh đúng biên pixel.
Mỗi trang nhớ pan/zoom riêng và tự mở vùng đầu tiên có hoạt động phù hợp.

Đã chạy test framebuffer/pixel, giới hạn cuộn, zoom giữ tâm, FIT, đổi trang rồi
quay lại, và tour nút trên board qua cùng event queue USB. Debounce được tách theo
nút, trạng thái giữ nút lúc boot được bỏ qua đúng tới lần thả đầu. Hai thao tác
liên tiếp trong một tick được gộp redraw. SPI chờ truyền hết bit cuối trước nhả CS.

### Giai đoạn review ESP32

Phát hiện SQ và PAT0 dùng trùng LEDC channel0; PAT chuyển sang2..5. GPIO25 vừa SQ2
vừa MISO nên tắt SQ2, giữ riêng MISO. Sửa `uart off` vẫn gửi trong burst, khóa UART
khi đổi baud, khóa I2C transaction và SPI reconfiguration, kiểm CAN rate trước
khi tháo driver, thêm rollback/counter lỗi, parse số chặt và sửa console gõ chậm.

Thử thật đã kiểm UART-off giữ mức cao262.144 mẫu, PWM1kHz50%, năm lệnh không hợp lệ,
mười lần đổi SPI, command gõ mỗi ký tự cách250 ms và dòng quá dài. CAN vẫn tiếp tục
queue, counters lỗi CAN/SPI bằng0 trong phép thử ấy. SQ/GPIO4 sau sửa cần nối dây
riêng nếu muốn đo trực tiếp duty trên chính pin đó.

### Giai đoạn QA v2 độc lập

Bổ sung vector Python độc lập với test C cũ, bridge liên kết decoder C production,
oracle sigrok và ma trận timing/trigger/recovery/GUI. Những lỗi chỉ lộ ra khi tăng
độ chặt được giữ bằng chứng trước sửa, không xóa khỏi lịch sử.

- Autobaud lấy idle đầu buffer làm bit-width: sửa bỏ đoạn trước cạnh đầu tiên.
- AUTO nhận clock I2C thành CAN: cửa sổ±25% quá rộng; thu về±15%, ưu tiên không đoán
  khi độ phân giải không đủ. Không vì vậy nhận CAN đã có CRC validation.
- Trigger muộn làm index lệch50%: không slide cửa sổ; thử lại với phase lệch nhỏ,
  giữ đúng pre-trigger và tránh khóa pha với burst100 ms.
- DMA IRQ từ snapshot còn pending: lúc bật stream IRQ tạo một slot64 KiB giả. Trường
  hợp10s từng báo10.065.540 mẫu dù device duration10,000015s và0 drop. Sửa xóa pending
  IRQ trước chuyển mode; thêm kiểm sample-rate/duration để không chỉ tin hai counter
  cùng sai. Đây là lý do kiểm số mẫu bằng thời gian quan trọng hơn chỉ đọc “0 drop”.
- GUI mất riêng decoder UART: race trên danh sách session libsigrokdecode khi nhiều
  stack tạo đồng thời. Patch bảo vệ lifecycle bằng GIL và kiểm40session song song.
- Reset USB có khoảng trống trước automount: công cụ flash nay đợi BOOTSEL mount,
  thay vì báo mất board ngay khi ttyACM vừa biến mất.

## 6. Kết quả định lượng cuối cùng

<!-- BEGIN QA GENERATED -->
Run [qa-v2-20261118-015654](results/qa-v2-20261118-015654/summary.json): **28 PASS,0 FAIL,2 BLOCKED,3 NOT-VERIFIED**.

- Soak 1 MHz RLE: **299,999,984 mẫu**, device duration**299.999987s**, **0 drop**, counters Pico/PC khớp,hardware flags0.
- Autobaud:12 tổ hợp,max error**0.095395%**.
- Trigger:100/100,index32768,max ARM→complete**0.499399s**.
- Đã kiểm50 manualcapture,4 protocol×3rates,GUIlive4 decoder,kill/stall/resetUSBvàrecovery.

Số đo frequency/bitwidth/jitter và bảng chi tiết:

Reference: LEDC ESP32 trên dây CH0 hiện có, không phải oscillator đã hiệu chuẩn. Jitter gồm quantization và sai khác hai clock, chưa tách jitter analog.

| Sample rate | Frequency đo (Hz) | Sai số (%) | Duty | Period mean (sample) | Jitter p-p/RMS (sample) |
|---|---:|---:|---:|---:|---:|
|1000000|1000.007663|0.000766|0.499996|999.992337|1.000 / 0.087202|
|5000000|1000.007692|0.000769|0.500000|4999.961538|1.000 / 0.192308|
|25000000|1000.004000|0.000400|0.500002|24999.900000|1.000 / 0.300000|

| Sample rate | UART bit lý thuyết | Mean đo | Sai số (%) | Jitter p-p/RMS (sample) |
|---|---:|---:|---:|---:|
|1000000|8.680556|8.666667|-0.160000|1.000 / 0.471405|
|5000000|43.402778|43.523810|0.278857|1.000 / 0.499433|
|25000000|217.013889|217.000000|-0.006400|2.000 / 0.617213|

Autobaud offline:12 tổ hợp; sai số lớn nhất **0.095395%**. Trigger100lần: max ARM→complete **0.499399s**; index32768. CH6không có nguồn cạnh nên B3chỉ kiểm14 polarity, không phải16/16.

<!-- END QA GENERATED -->

## 7. Kiểm thuật toán decoder: đã chứng minh điều gì?

**A1/A2:** dựng file `.sr` có đủ UART/I2C/SPI/CAN bằng bộ dựng độc lập, có CAN CRC
trong vector. Sigrok và decoder C phải đọc cùng bytes sample, đối chiếu byte-level:

| Protocol | Dữ liệu kỳ vọng |
|---|---|
| UART |`4C 41 31 32 33 34 0A` = `LA1234\n`|
| I2C |address0x68write,ACK,register0x01,ACK,data0x5A,ACK|
| SPI MOSI |`DE AD BE EF 00 01 02 03`|
| CAN |standardID0x123,DLC4,payload`DE AD BE EF`trên vector; payload thực tăng theo frame|

**A3:**32 vector×2lần gồm empty,all-zero/all-one,UART thiếu stop/sai parity,nhiễu1–2 mẫu,
24 seed noise,SPIclock không đều,CAN sai baud. Mỗi process có timeout,ASan/UBSan;
yêu cầu không crash/hang và output xác định. An toàn xử lý input xấu khác với
khả năng xác nhận input ấy là frame hợp lệ.

**A4:**12 tổ hợp3sample-rate×4baud, có samples/bit fractional; autobaud sai số lớn nhất
khoảng0,096% trên tập vector này. Đây không phải sai số cho mọi dạng dữ liệu UART;
autobaud phụ thuộc có cạnh và run đủ thông tin.

**A5:**4 protocol×map cố định/kênh dịch chuyển, cộng8 noise seed. Kiểm cả protocol và
channel map, không chỉ thấy số packet lớn hơn0. Không tuyên bố chứng minh mọi
nhiễu tương tự ngoài đời đều bị từ chối.

**Giới hạn cần nói rõ:** firmware UART là8N1, không kiểm parity; SPI on-device là
mode0/MOSI; CAN on-device hiệnstandard11-bit và chưa kiểm CRC đầy đủ. PulseView có
bộ decoder sigrok phong phú hơn. Giải mã đúng payload trên tập thử chưa chứng minh
mọi parity/SPI mode/CANerror frame đã được hỗ trợ.

## 8. Trigger và độ bền snapshot

Đã kiểm edge rising/falling trên các kênh có tín hiệu, pattern match, manual không
trigger, pre0/half/>half, count full-buffer bị clamp an toàn. Đã chạy100trigger
liên tiếp ở burst100 ms và50 manual capture liên tiếp, kiểm CRC,sequence,count.

Một pattern “không khớp” phải dựa vào tín hiệu bị giữ chắc chắn. Harness ban đầu
giả định CH6 kéo xuống là nguồn chuẩn, nhưng pin thiếu hoạt động ngoài có thể
floating/không ổn định ở25 MHz. Bài cuối dùng `uart off`, xác nhận CH0 idle-high
suốt262 ms rồi yêu cầu patternlow; timeout và HELLO recovery được kiểm thật.

Không nhận “không rò rỉ MCU” chỉ từ việc50/100lần thành công. Firmware sử dụng
buffer static; chưa có telemetry heap watermark toàn hệ thống. RSS host và thời
gian mỗi lần được ghi để phát hiện xu hướng rõ trong phạm vi thử.

## 9. Streaming, mất mẫu và recovery

Các lượt60s lặp đã kiểm traffic nén được1 MHz RLE,raw500 kHz và dữ liệu dày. Khi CH0
đổi mỗi hai mẫu ở1 MHz,RLE không còn lợi và chuyển raw. Tốc độ USB thực tế không
đủ cho1 MHz raw liên tục; phép đo ấy phải ghi CAPACITY_LIMIT/INVALID, không phải
PASS lossless.

Bộ QA cuối có10sRLE,5sraw,5 phútRLE,SIGINT,SIGKILL,hoststall800 ms và4s,device reset
và capture lại. SIGINT phải giữ đuôi; kill không có file hoàn tất được chứng nhận.
Hoststall có thể mất mẫu/CRCđứt frame, nhưng phải báo lỗi và phục hồi bằng session
mới. Không tiếp tục dùng file lỗi như tín hiệu hợp lệ.

Sequenceu16 được kiểm xuyên rollover trong soak. Offsetu32 qua2^32được kiểm bằng
unit test, chưa chờ71 phút trên phần cứng trong phiên này.

## 10. PulseView: không chỉ là CLI

Script tạo preset có đúng UART RXCH0/115200,I2CSCLCH1/SDACH2,SPICLKCH4/MOSICH5/
CSCH3/MISOCH6,CANCH7/250000. PulseView thực hiện live acquisition từ driver thật.

Điều kiện thành công: tên board đúng,tám channel,rate25 MHz,count65536,trigger source,
slopefalling,pre50%,bốn nhóm annotation visible chứa dữ liệu đúng. Có JSON đọc lại
trong GUI, ảnh Qt của cửa sổ và ảnh X11 root trong D3. Xdotool dùng đưa cửa sổ lên
trước; channel/baud được gán qua native setup path, không dựa click theo tọa độ.

Các lỗi decoder thiếu instance được xử lý bằng patch thư viện, không né bằng
cách ghi nhận CLI thay GUI. Trường hợp mở recorded mode được đánh riêng với live;
không dùng cờ `-c` khiến PulseView capture xong rồi thoát.

## 11. TFT, trải nghiệm thao tác và hình minh chứng

| Màn | Nội dung |
|---|---|
| STANDBY |logo,25 MSPSmax,clockCPU100 MHz,buffer256Kpts|
| 1CONFIG |Rate,Samples,Chan,Pre-Trig;OKngắn sửa,giữOKrồi thả để thu|
| 2TRIGGER |Mode,Chan,Condition,Baud/Clk|
| 3WAVECH0–3 |4track,triggerđỏ,cursorgiữa,thước thời gian,scrollbar|
| 4WAVECH4–7 |4track còn lại,pan/zoom riêng|
| 5DECODE |AUTO/UART/I2C/SPI/CAN,channel/rate,preview vuông,bảngbyte/ASCII/time|

Màn3/4:UP/DOWNngắn pan nửa viewport;giữUPzoom in,giữDOWNzoom out tớiFIT;giữOKreset
về vùng hoạt động;OKngắn sang trang,BACKquay lại. X1 nghĩa một sample/pixel,X32
nghĩa32sample/pixel; hệ số càng lớn càng nhiều thời gian trên màn. `1div` là
thời gian mỗi khoảng chia,`T` là thời điểm cursor so với trigger.

Bảng DECODEchỉ hiện5dòng một lần; DOWNcuộn xem byte tiếp. `TIME` là gap giữa các
vùng packet do decoder ghi, không phải cam kết đo mọi khoảng idle analog trên bus.
CONFIG là preset lần thu tại board; thông tin rate ở WAVE/DECODE lấy từ capture
thực, tránh hiển thị25 MHz khi host vừa thu1 MHz.

Ảnh TFT là **framebuffer đọc trực tiếp từ Pico sau flushSPI**, không phải renderer
PC/mô phỏng. Điều này chứng minh dữ liệu/pixel gửi panel; chưa thay ảnh camera
kiểm màu/backlight hay tiếp điểm nút chưa hàn. Tour USB dùng cùng queue với nút,
kèm unit test debounce, không tự nhận nút vật lý đã nghiệm thu.

Demo giữ mỗi màn khoảng**2 giây**, không dùng tốc độ chuyển nhanh của unit test.

## 12. Đánh giá mức độ hoàn thiện

| Hạng mục | Đánh giá |
|---|---|
| Đường capture số,host,CRC,trigger,4 bus mặc định | Có bằng chứng chạy thật,đạt trong phạm vi QA |
| GUI có waveform và decode | Kiểm bằng chính PulseView live,JSONvà ảnh |
| Streaming điển hình | Có soak và fault injection; không nhận raw1 MHzlossless |
| TFT/logic nút không có nút vật lý | Pixel/state/USBqueue đã kiểm;tiếp điểm thực chưa có |
| Đủ8 nguồn ngoài,crosstalk,skew | Chưa đủ fixture/wiring,CH6thiếu cạnh |
| Absolute timing calibration | Chưa có reference ngoài đã hiệu chuẩn |
| 5V,inputloading,bảo vệ12V/200V | CHƯA KIỂM CHỨNG BẰNG ĐO |
| BOM≤450000 VND | Chưa có BOMas-built8 kênh/TFTvà hóa đơn |
| Decoder mọi mode/error | Có giới hạn parity,SPImode/MISO,CAN CRC/extended |

**Kết luận dùng trong thuyết trình:** đã có prototype hoạt động và demo số đáng tin
trong cấu hình được kiểm. Chưa đủ căn cứ tuyên bố nghiệm thu toàn bộ đề tài hoặc
đạt chất lượng LA thương mại/phổ dụng. Không gắn một phần trăm hoàn thiện tùy ý.

## 13. Phần bảo vệ và BOM: trả lời thẳng khi được hỏi

PLANlịch sử đề xuất4 kênh vớiR47k,BAT54S,SMAJ5V0A,LVC245vàOLED. Đó không phải
schematicas-built8 kênh/TFT hay kết quả đo xung cao áp.

Ví dụ tính toán định hướng, chưa là nghiệm thu:200V qua47kΩ choI≈4,26 mA,
P tức thời≈0,851W,năng lượng≈0,851 mJ cho xung chữ nhật1 ms. Phải kiểm pulse rating,
điện áp làm việc,clamp levelvà đường dòng về rail. Không thể chỉ thấy công suất
TVS cao rồi kết luận input MCU an toàn.

Nếu tổngC giả sử10 pF,R47kΩ choRC470 ns vàf(-3dB)≈339 kHz. Điều này cảnh báo tradeoff
protection/bandwidth; sample clock25 MHz không tự chứng minh front-end25 MHz. Cần
mô phỏng và đo băng thông/threshold trên chính cấu hình đã lắp.

BOM lịch sử cộng các hàng không gồmST-Link cho338.000–453.000 VND,khác khoảng
363.000–440.000đ nêu trong cùng plan. Cấu hình ấy còn khác sản phẩm hiện tại.
Vì thế cần BOMmới,quantity8 kênh/giá có nguồn và tổngas-built trước khi claim≤450k.

## 14. Điểm nổi bật nên nhấn trên slide

- Một nền tảng rẻ có cả hiển thị cầm tay và PulseView,không chỉ xuất file raw.
- PIO/DMA giữ việc lấy mẫu tách khỏi tốc độ vẽ TFT và tốc độ USB.
- Dùng dữ liệu ESP32 biết trước và oracle độc lập để kiểm,có kiểmbyte-level.
- Chất lượng dữ liệu không chỉ dựa “0 drop”:cóCRC,sequence,offset,countervàduration.
- Kiểm lỗi có chủ đích:stall,kill,reset,framexấu,nhiễu,buffer boundary.
- Sửa lỗi có regression và giữ log trước/sau; phân biệtPASS/BLOCKED/NOT-VERIFIED.
- TFT có thước,cursor,zoom/pan và AUTO,bảngbytegiúp giải thích hoạt động trên board.

## 15. Gợi ý bố cục slide và lời nói

1. **Bài toán:** cần xem nhiều đường số và đọc nội dung bus trên PC/lẫn thiết bị.
2. **Mục tiêu:**8 kênh,25 MS/s,4 protocol,TFT/PulseView;đặt rõ phạm vi3,3Vđã thử.
3. **Kiến trúc:** một sơ đồPIO→DMA→RAM→TFT/USB→PC.
4. **Thiết lập test:** ảnh/wiringPico+ESP32,bảngchannelvàpayload chuẩn.
5. **Test có oracle:** cùng `.sr`,so từng byte C/sigrok;không chỉ nhìn đẹp.
6. **Timing/trigger:** lấy bảng cuối ở mục6,ghi sai số relativevà100 capture.
7. **Streaming:**5 phút/0 drop,counters khớp;đặt cạnh biểu đồ giới hạnraw để minh bạch.
8. **PulseView:** ảnhD3có đủ waveformvà bốn hàng annotation.
9. **TFT/UX:** hai ảnhWAVEvà một ảnhDECODE;font,cạnhvuông,scrollbar,zoomriêng.
10. **Lỗi đã sửa:**tailmấtmẫu,DMA IRQ64KiBgiả,GUIrace,autobaud/AUTO,triggerlate.
11. **Giới hạn:**protection/BOM/skew/parity/CAN CRC;không dùngslide“100%hoàn thiện”.
12. **Demo/kết luận:** chạy một lệnh,giữ2 giây/màn,có logPASSvà cửa sổGUIgiữ mở.

Lời kết gợi ý: “Nhóm đã chứng minh đường dữ liệu số và trải nghiệm demo bằng đo
thật và kiểm độc lập. Phần tiếp theo tập trung nghiệm thu front-end,bảo vệ,BOMvà
các mode/error protocol còn thiếu.”

## 16. Quy trình demo ngày mai

```bash
cd /home/namkhanh/EmbeddedSystem/Code/Logic_Analyzer_20261/PulseView-custom
DISPLAY=:0 bash scripts/demo.sh
```

Script tự nhận Pico bằng USBby-id,đưa ESP32 về cấu hình chuẩn,kiểm stream1 MHz RLE,
thu snapshot qua driver,kiểm TFT bốn protocol,giữ mỗi màn2 giây,rồi mở PulseView để
capture và sinh annotation thật. Chỉ báoPASSkhi tất cả bước bắt buộc đúng.

Script đóng cửa sổ do chính các lần demo trước tạo; không tự đóng serial monitor
hay PulseViewsession khác của bạn. Nếu có ứng dụng khác giữ cổng,hãy đóng nó rồi
chạy lại. Không chạy hostcapture/flash/UIkeys song song khi PulseView đang giữcổng.

Sau PASS:để cửa sổPulseView mở,nhấnSpace/Runđể thu lại,Fđể fit,lăn chuột đểzoom.
TFT cuối ởUART,byte`4C 41 ...`tương ứngLA1234. Các log,`.sr`,ảnh và summarymỗi lần
nằm riêng trong `build/demo/`; mất dữ liệu hoặc thiếuannotation thì lệnh phải FAIL.

Nếu chỉ kiểm phần cứng khôngmởGUI,dùng`--no-gui`;kết quả ấy làPASS_HARDWARE_ONLY,
không trình bày thành đã chứng minh PulseView. `--recorded`được ghi rõ làGUIđọc
file thật vừa thu,chứ không phải livecapture.

## 17. Lệnh QA để nhóm phát triển chạy lại

```bash
bash tests/test_qa_offline.sh
python3 tests/run_qa.py
bash scripts/test.sh
python3 tests/test_esp32_link.py
python3 tools/test_ui_hardware.py
python3 tools/test_generator_hardware.py
```

RunnerQAfullmặc định cósoak300s,vìvậy cần vài phút. Exit0nghĩa khôngFAILtrong
caseđã chạy; cácBLOCKED/NOT-VERIFIEDvẫn giữ nguyên. XemOPINIONđể phân công phiên sau.


### Ghi chú nhãn và nguồn số liệu

Điều kiện A3 trong run gốc có nhãn34vector bị ghi nhầm; actual JSON và log xác nhận
**32vector ×2 =64lượt**. Đây là lỗi nhãn harness, không có34vector được nhận PASS.
Lần demo mặc định cuối và ảnh mới nhất được gom tại [results/demo-final](results/demo-final/).


**Cách kết thúc demo:** sau PASS, launcher mặc định chờ tới khi đóng PulseView.
Điều này giữ GUI gắn với phiên trình diễn thay vì để thành tiến trình mồ côi.
`--detach-gui` chỉ phục vụ automation; ngày mai dùng lệnh mặc định ở trên.
