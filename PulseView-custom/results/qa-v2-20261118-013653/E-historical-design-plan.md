# PLAN — Logic Analyzer Topic 1 (PoC 1 người, 4 tuần)

> Mục tiêu: PoC cạnh tranh được với LA 200k ngoài thị trường, đạt chuẩn min
> Topic 1 (2 kênh, 1kHz, màn 128x64), dùng kit VDK phổ biến, có cả màn
> standalone + PC-app tương thích app có sẵn (PulseView / Saleae Logic).

## 0. Bản đồ đối thủ 5 phân khúc + chốt spec

### 0.1 Đối thủ rẻ hơn (~0–120k): Arduino-nano-as-LA / LA trần không vỏ

- Hàng Shopee "mini logic 8ch" trần truồng không vỏ mica: ~95.000–120.000đ,
  ruột vẫn CY7C68013A, 8ch/24MHz nhưng dây Dupont rời, không probe, không bảo vệ.
- Bản "0đ": Arduino Uno + firmware OLS cơ bản, max ~100kHz 4ch, depth vài KB.
- Ý nghĩa: PoC của mình **không được thua** nhóm này về giá trị sử dụng —
  phải có vỏ/hộp + màn + bảo vệ thì mới hơn được dù giá cao hơn ~100k.

### 0.2 Ngang giá (~135–250k): Saleae clone CY7C68013A — benchmark chính

- icdayroi 189k | banlinhkien 135k | imaker 155k | Shopee/Lazada 135–178k | Tiki 246k.
- Spec: 8ch, 24/16/12/8/4/2/1MHz…25kHz, Zin 100kΩ//5pF, stream realtime qua USB-HS,
  depth 10G mẫu (nhờ RAM PC), app Saleae Logic 1.x + PulseView (fx2lafw),
  decode CAN/DMX/I2C/I2S/Manchester/1-Wire/Async/SPI/UNI/O, trigger rising/high/falling/low.
- Điểm yếu chí mạng: **không bảo vệ quá áp thực sự** (chỉ ghi "overpressure" cho có),
  không màn hình, trigger chỉ edge/level đơn, phải cắm PC mới dùng được.

### 0.3 Cao hơn 1 (~1.7tr): DSLogic U2Basic Plus (DreamSourceLab)

- Giá ~$68 (~1.700.000đ) trên Motaco/AliExpress.
- Spec: 16ch, buffer mode 400MHz@4ch / 200MHz@8ch / 100MHz@16ch,
  stream mode 100MHz@3ch / 50MHz@6ch / 25MHz@12ch / 20MHz@16ch,
  RAM 64–256Mbit (16M mẫu/ch), USB2.0 Type-C, FPGA + DSView cross-platform.
- Hơn clone ở: FPGA thật, trigger 16-stage + protocol trigger, nén HW, dây shielded.
- Kém ở: vẫn không màn standalone, vẫn cần PC khỏe để stream.

### 0.4 Cao hơn 2 (~5–6tr): Ikalogic ScanaQuad SQ200 (Pháp)

- Giá €199 net (~5.400.000đ + VAT).
- Spec: **4ch**, 200MHz max, 4M mẫu/ch, vừa capture vừa generate pattern
  (mixed mode, playback), Vin liên tục ±5V (SQ200 ±15V bản mới), chịu xung ±50V/10ms,
  Zin 1MΩ//4pF, trigger edge/level/pulse + serial-protocol (UART byte, I2C addr…),
  output push-pull/open-drain 1.65–5.5V, app ScanaStudio cho phép tự viết decoder.
- Đây là hình mẫu cho PoC: ít kênh (4) nhưng trigger khôn + bảo vệ rõ ràng + 2 chiều.
  Mình copy triết lý này ở tầm giá 1/20.

### 0.5 Cao hơn 3–4 (~12–25tr): Saleae Logic 8 ($499) / Logic Pro 8 ($999) chính hãng

- Logic 8: 8ch đa năng A/D, digital 100MS/s (3ch) / 50MS/s (6ch) / 40MS/s (7ch),
  BW số 25MHz, analog 10MS/s 10-bit BW 1MHz, range 0–5V, stream 10B+ mẫu qua USB2,
  bảo vệ liên tục −25..+25V, 25+ decoder, Logic 2 Win/Mac/Linux + Python API.
- Pro 8: digital 500MS/s (4ch, cần USB3), BW 100MHz, analog 50MS/s 12-bit ±10V,
  ngưỡng chọn được (0.6/0.9/1.65V), hỗ trợ 1.2V logic.
- Ý nghĩa: không bao giờ đuổi theo MHz/BW/analog của nhóm này bằng MCU.
  Chỉ học ở họ: streaming bằng RAM PC, trigger-on-protocol, search, API tự động hóa.

### 0.6 Bảng so sánh nhanh

| Máy | Kênh | Sample max | Depth | Bảo vệ liên tục | Trigger | App/decode | Màn | Giá |
|---|---|---|---|---|---|---|---|---|
| Arduino-OLS 0đ | 4 | ~100kHz | vài KB | không | edge | OLS cơ bản | không | 0đ |
| Clone CY7C68013A | 8 | 24MHz | 10G (RAM PC) | ~không | edge/level | Logic 1.x + PulseView, ~10 protocol | không | 135–250k |
| **PoC mình** | **4 (8 thử)** | **12MHz (burst 24M 1–2ch)** | **200–350k (RAM RP2350)** | **12V + 200V/1ms** | **edge + pattern + pre/post** | **PulseView SUMP + Python CSV** | **OLED 128x64** | **~300–450k** |
| DSLogic U2Basic+ | 16 | 400M buf / 100M stream | 16M/ch + 16G stream | ESD diode array | 16-stage + protocol | DSView | không | ~1.7tr |
| SQ200 | 4 | 200MHz | 4M/ch | ±5V (±15V ver mới) | edge/level/pulse/serial | ScanaStudio, tự viết decoder | không | ~5.4tr |
| Saleae 8 / Pro 8 | 8 | 100M / 500M | 10B+ (RAM PC) | −25..+25V | protocol trigger + search | Logic 2, 25–60 protocol + API | không | ~12.5tr / ~25tr |

### 0.7 Chốt spec PoC (base bắt buộc + stretch nếu thừa giờ)

- Base (phải có để bảo vệ): 4ch, 1kHz–8MHz liên tục, depth ≥64k mẫu/ch,
  trigger edge + pattern 4-bit, pre/post 0–100%, OLED overview + PulseView UART/I2C,
  bảo vệ 12V DC + 200V/1ms có mô phỏng, báo cáo timing skew.
- Stretch (có thì cộng điểm, không có vẫn qua): burst 12–24MHz 1–2ch,
  mode 8ch ở ≤4MHz, decode SPI trên PulseView, đo f/duty/timing trên Python,
  playback 1ch (học SQ200, dùng PWM/DMA phát lại), hộp mica + probe gọn.
- Không cam kết (ghi rõ trong báo cáo để khỏi bị hỏi): stream realtime 24MHz8ch
  (cần USB-HS, ngoài tầm USB-FS), analog, ngưỡng lập trình được, BW >25MHz.

## 1. Tài nguyên VDK — push Pico 2 (RP2350) tới limit

### 1.1 RP2350 (Pico 2) — chốt mua, push tới limit

- Chốt mua: **Pico 2 RP2350 ~180–230k** (cytron 180–210k, raspberrypi.vn 230k,
  Hshop 195k hay hết hàng; alt Waveshare RP2350-Zero 162k). Pico 1 chỉ là fallback.
- CPU: dual Cortex-M33 **150MHz** (dùng M33, không dùng RISC-V cho SDK ổn định;
  core0 sample/trigger, core1 USB/màn). Giữ 150MHz mặc định, chỉ OC khi thử burst
  24MHz và ghi rõ OC trong báo cáo.
- RAM: **520KB SRAM / 10 bank** độc lập → trừ ~120KB cho FW+USB+OLED,
  còn ~400KB cho capture. 4ch pack 1 byte/mẫu → target **200–350k mẫu**
  (base ≥64k), gấp ~3–4 lần Pico 1.
- Flash board: 4MB (chứa FW + font + cấu hình, KHÔNG dùng làm buffer vì chậm).
- PIO: **3 block × 4 state machine = 12 SM**. Dùng 1 SM sample 4–8 chân
  đồng thời theo 1 clock → skew < 1 sample period.
  **Bộ chia chỉ nhận số nguyên** — từ sysclk 150MHz ra được đúng các mốc
  **1 / 2 / 3 / 5 / 6 / 10 / 15 MHz** (÷150, 75, 50, 30, 25, 15, 10).
  Muốn có 4 / 8 / 12 / 24 MHz thì **đổi tần số hệ thống qua PLL** trước khi cấu
  hình PIO (VD sysclk 144MHz → ÷12 = 12MHz, ÷36 = 4MHz). Mỗi mức rate đi kèm một
  sysclock riêng; jitter vẫn ±1 sysclk (~6.7ns @150MHz).
- DMA: 16 kênh — 1 kênh PIO→RAM chạy nền, CPU rảnh lo trigger/USB/màn.
- USB: **FS 12Mbps**, thực tế CDC ~0.8–1MB/s. Burst 350KB ≈ 0.4s —
  chấp nhận được vì offline burst. Đây là lý do **không stream realtime 24M×8ch
  (=24MB/s)**: thiếu ~25× băng thông. Ghi con số này vào báo cáo để biện hộ.
- GPIO: 30 chân đa năng, 5V-tolerant **không** → bắt buộc buffer + kẹp ngoài.
- Công cụ: Pico C-SDK + fork `pico-sump`/`μLA` (port sang RP2350), PulseView qua SUMP.

Bảng depth thực tế (4ch, 1 byte/mẫu, RAM capture ~400KB → target 350k mẫu):

| Sample | 350k mẫu ≈ thời gian cửa sổ | Dùng cho |
|---|---|---|
| 1MHz | 350ms | UART 115200, I2C 100k |
| 4MHz | ~87ms | SPI 500k, I2C 400k |
| 8MHz | ~44ms | SPI 1M |
| 12MHz | ~29ms | SPI 1–2M burst |
| 24MHz 1ch | >100ms (pack bit) | đo xung nhanh, demo aliasing |

Thang push limit trên Pico 2 (thử từ cao xuống thấp, nhiễu thì dừng ở mốc sạch):
`24MHz/1ch → 12MHz/4ch → 8MHz/4ch → 4MHz/8ch → 1MHz safe`.
Tiêu chí pass mỗi mốc: bắt PWM chuẩn 3 mốc tần số, sai số <2%, waveform trên
PulseView không râu/nhiễu, rerun 10 lần không rớt mẫu. Rớt mốc nào thì chốt mốc dưới
làm spec chính thức trong báo cáo (trung thực > gồng MHz).

### 1.2 Linh kiện chi tiết + alternative (kèm lý do chọn/loại)

**Buffer số:**
- Chốt: **74LVC245 DIP-20** (10–20k; robocon/3M/Banlinhkien). Vcc 2–6V, 8-bit 2 chiều,
  trễ ~7ns, Iout ±35mA — đủ làm lớp bảo vệ thứ 2 + cách ly.
  **Chọn LVC thay vì HC vì LVC chịu được ngõ vào 5V ở mọi VCC, còn HC chỉ chịu VCC+0,5V** —
  nếu tín hiệu vào có thể là 5V thì HC@3,3V là vượt abs max.
  Mua DIP để cắm breadboard. **Cấp nguồn VCC = 3.3V, chân DIR cố định A→B và /OE
  nối GND để chỉ đệm một chiều** (không cần điều khiển, tránh tranh chấp bus).
- Alt: 74LVC245 (3V3 native, nhanh hơn, ngưỡng đẹp hơn cho 1.8–3.3V — nên dùng nếu
  đo target 1.8V; giá ~10–20k). 74HC125 (1 chiều, 4 buffer — mua nếu hết 245).
- Loại: TXB0108/TXS0108 (auto-direction, dễ tự dao động với probe dài + không chịu
  quá áp → không dùng cho đầu vào LA).

**Mạch bảo vệ mỗi kênh (×4):**
- R_series: **47k 1206** (chịu ~200V package rating, 0.25W). Tính xung 200V/1ms:
  I≈200/47k≈4.3mA, P≈0.86W tức thời / E≈0.86mJ → R + diode/TVS chịu được,
  không cháy. Alt 100k nếu muốn an toàn hơn nhưng méo nặng hơn ở MHz.
- Chia áp 10:1 (VD 9k1/1k): hạ 12V→~1.2V vào diode, hạ biên TTL 5V→0.5V? —
  thực tế chọn tỉ lệ sao cho TTL 2–5V vẫn qua ngưỡng HC (Vih~3.5V@5V Vcc) → mô phỏng
  chốt tỉ lệ cuối, có thể thêm jumper bypass chia áp cho mode 3V3 nhạy.
- Kẹp: **BAT54S (KL4) SOT-23** 200mA/30V, Vf~0.35V@100mA, trr 5ns (350đ–1.5k/con;
  mua 10 con). Kẹp về 3V3/GND, nhanh, rò 2µA không ảnh hưởng ngưỡng.
- TVS: **SMAJ5V0A** (Vrwm 5V, Vc~9V@43A, Pppm 400W) hút xung 200V còn sót sau R.
  Mua 5–10 con (~2–5k/con, thegioiic/chipn24).
- Tụ bù 10–47pF // R_series nếu méo ở 8–12MHz (trị số chốt bằng mô phỏng tran).
- Alt đã loại: Zener 3V6 thay TVS (chậm, gối mềm), TVS array DVIULC6 (xịn như DSLogic
  nhưng khó mua lẻ), opto-cách ly (trễ lớn, méo timing → loại).

**Màn hình:**
- Chốt: **OLED SSD1306 0.96" 128x64 SPI 7 chân** (88k thegioichip; 65k bản I2C chỉ mua
  nếu hết SPI; 48k order CN 7–9 ngày). SPI ~8MHz refresh nhanh, tách bus riêng với DUT.
- Alt: OLED 1.3" SH1106 SPI (120k, to hơn, driver khác) — upgrade nếu muốn demo đẹp.
  TFT ILI9341 2.8" 320x240 (đẹp nhưng tốn RAM/CPU, code nặng → chỉ làm nếu thừa tuần 4).
- Loại: LCD1602 (không vẽ waveform), OLED I2C (chung bus, chậm, dễ nhiễu khi sample nhanh).

**VDK alternative (giữ trong báo cáo để thể hiện đã khảo sát):**
- STM32F411 BlackPill (125–151k): M4F 100MHz, RAM 128KB, USB-FS, Timer+DMA đọc IDR.
  Được ~2–8MHz, không có PIO nên jitter cao hơn. Dùng khi thầy bắt STM32 đúng chữ.
- STM32F103 BluePill (~80–100k): rẻ nhưng RAM 20KB → depth ~10k mẫu, USB cũ — loại.
- ESP32-S3 (~150–200k): 240MHz + PSRAM sâu, nhưng jitter WiFi + ADC/DMA phức tạp — loại.
- RP2040/Pico 1 (~80–140k): M0+ 133MHz, RAM 264KB, 2 PIO — fallback nếu Pico 2
  hết hàng/cháy (spec hạ còn 64–100k mẫu, vẫn qua min).

**Phụ kiện:** breadboard + DuPont + test-hook (~50–80k, hshop/nshop/thegioichip/lkcg);
ST-Link V2 mini 44–96k **chỉ mua nếu dùng STM32** (Pico nạp qua USB drag-drop);
MCU thứ 2 phát test (tận dụng đồ cũ, hoặc Pico 1 TQ 80k).

Tổng theo BOM: **363.000–440.000đ**, cam kết NFR **≤ 450.000đ** (đã có đồ nghề thì
chỉ ~310–390k).

## 2. BOM tóm tắt (giá tra 10/2026)

| Linh kiện | Giá | Mua ở đâu |
|---|---|---|
| Pico 2 RP2350 (**chốt mua**) | 180–230k | cytrontech.vn (180–210k), raspberrypi.vn (230k), Hshop 195k (hay hết); alt Waveshare RP2350-Zero 162k |
| OLED 0.96 SPI SSD1306 | 65–88k | thegioichip 88k (SPI), dientu360 65k |
| 74LVC245 DIP-20 | 10–20k | robocon, 3M, Banlinhkien |
| BAT54S ×10, SMAJ5V0A ×5, R 1206 + tụ pF | ~30k | ritech, lkcg, thegioichip, chipn24 |
| ST-Link mini (nếu STM32) | 44–96k | dientutuyetnga 44k, fuvitech 96k |
| Breadboard + DuPont + hook | 50–80k | hshop, nshop, thegioichip, lkcg |
| LED 3mm ×4 + trở 1k (chỉ thị mức 4 kênh) | 3–5k | dùng chung đơn hàng điện trở |

Mẹo: 245 lấy DIP, OLED hỏi rõ bản SPI 7 chân, đặt hàng tuần 1.
Flash 4 MB của Pico 2 dùng chung cho code và lưu phiên → tách vùng, không ghi đè firmware.

## 3. Sơ đồ (4 hình — vẽ lại bằng draw.io/KiCad khi làm báo cáo)

**3.1 Khối HW:**
```
Probe x4 → [R47k + /10 + BAT54S + TVS + C_bù] → 74LVC245 → GP0..GP3 (Pico, 1 port)
Pico USB-C → PC | Pico SPI1 → OLED | Pico GPIO → 4 LED chỉ thị mức
Pico Flash → lưu phiên đo + mẫu tham chiếu
Pico UART/PWM → (loopback test với MCU2)
```

**3.2 Luồng FW (state machine):**
```
IDLE → ARMED (chờ trigger edge/pattern) → CAPTURE (PIO+DMA → ring buf, giữ pre-N%)
→ TRIGGERED (lấy thêm post-M%) → DONE → BURST (gửi SUMP qua CDC) → IDLE
Core0: sample/trigger | Core1: USB-CDC + OLED + nút/menu
```

**3.3 Luồng dữ liệu PC:**
```
Pico (SUMP: ID → set rate/ch → run → dump 100kB) → PulseView (wave + decode UART/I2C/SPI)
↘ đồng thời Python script (serialemplot: f/duty/skew + xuất CSV cho báo cáo)
```

**3.4 Luồng test/bảo vệ:**
```
LTspice (DC sweep + tran 1k/1M/12M + xung 200V/1ms) → số liệu Vclamp/I/TVSpower/rise-fall
→ đối chiếu đo thật (12V DC sống, bắt sóng lại OK) → bảng phạm vi bảo vệ trong báo cáo
```

Nguyên tắc timing: 1 SM-PIO latch cả port 1 lệnh → mọi kênh cùng timestamp,
skew < 1 sample; clock từ crystal, không poll.

## 4. Kế hoạch 4 tuần — 1 người, mỗi tuần 2 chặng (tổng ~32 step)

### TUẦN 1 — DESIGN (8 step, xong là có slide 11/11)

- [ ] W1-1. Viết spec 1 trang (bảng 0.7) + bảng so sánh 3 LA thật (clone + SQ200 + DSLogic).
- [ ] W1-2. research `pico-sump` + `μLA` + SUMP protocol (ghi lại 5 lệnh tối thiểu cần).
- [ ] W1-3. Vẽ schematic EasyEDA/KiCad: 4 kênh bảo vệ + 245 + Pico + OLED SPI riêng bus.
- [ ] W1-4. LTspice DC sweep −50..+50V → chốt tỉ lệ chia áp + V_tới_245.
- [ ] W1-5. LTspice tran vuông 1k/1M/8M/12M → đo rise/fall/overshoot → chốt R/C_bù.
- [ ] W1-6. LTspice xung 200V/1ms → I_R, P_TVS, Vclamp → bảng phạm vi bảo vệ.
- [ ] W1-7. Dựng toolchain: Pico SDK build được, PulseView + driver, Python serial/plot chạy.
- [ ] W1-8. Đặt mua linh kiện + mượn/chuẩn bị MCU2 phát test. Test hello OLED+CDC.
- Done: schematic + 3 plot LTspice + toolchain chạy.

### TUẦN 2 — CAPTURE CORE + HW (8 step)

- [ ] W2-1. Lắp 1 kênh bảo vệ trên breadboard, đo nguội (thông mạch, chập) rồi cấp 3V3/5V thử.
- [ ] W2-2. Nhân lên 4 kênh + 245, đi dây probe ngắn, tách mass sao gọn.
- [ ] W2-3. FW PIO sample 1k/100k/1M cố định, dump thô qua CDC, Python vẽ được sóng.
- [ ] W2-4. Thêm các mốc còn lại (4M/8M/12M) bằng cách đổi sysclock qua PLL, chọn kênh 4/8, hiệu chuẩn sai số bằng PWM chuẩn.
- [ ] W2-5. Ring buffer + trigger edge (rising/falling) + run/stop.
- [ ] W2-6. MCU2 phát PWM/UART 9k6–115k2 → LA bắt, đối chiếu f/baud, ghi log sai số.
- [ ] W2-7. Đo skew 2 kênh cùng nguồn + jitter (chụp hình làm số liệu).
- [ ] W2-8. Test sống 12V DC vào 1 kênh → đo lại bắt sóng. (Xung 200V chỉ mô phỏng + xung thấp an toàn.)
- Done: bắt sóng thật + gửi PC được.

### TUẦN 3 — MÀN + PC-APP + TRIGGER (8 step)

- [ ] W3-1. OLED SPI: hiện overview nén + thông số (rate/ch/trig state).
- [ ] W3-2. Menu 3 nút (rate / trigger / run-stop), giữ <200 dòng vẽ.
- [ ] W3-3. SUMP tối thiểu (ID, set rate, run, dump) → PulseView hiện 4 kênh.
- [ ] W3-4. Trigger pattern 4-bit + mặt nạ don't-care + pre/post % (mặc định 20%).
- [ ] W3-5. Python 1 file: plot 4 kênh + đo f/duty/skew + export CSV.
- [ ] W3-6. Decode trên PulseView: UART đúng baud, I2C đọc sensor thật.
- [ ] W3-7. Thử SPI 1MHz 4 kênh + burst 24MHz 1–2ch (ghi rõ OC nếu có).
- [ ] W3-8. Demo aliasing cố ý (phát 5M sample 2M) + chụp hình giải thích Nyquist.
- [ ] W3-9. Giải mã tại chỗ (stretch): đo rộng xung → gợi ý baud → hiển thị text UART trên OLED. Làm UART trước, I2C sau, SPI bỏ nếu thiếu thời gian.
- [ ] W3-10. Đo tham số xung ngay trên OLED (f, duty, độ rộng xung) + 4 LED chỉ thị mức 4 kênh.
- [ ] W3-11. Lưu phiên đo vào Flash: tách vùng, ghi/đọc, duyệt danh sách phiên trên OLED.
- [ ] W3-12. Bộ máy so khớp byte: so sánh với mẫu tham chiếu (ĐẠT/LỆCH + vị trí lệch). Dùng chung cho cả W3-13.
- [ ] W3-13. So sánh hai phiên bất kỳ (chọn 2 phiên, liệt kê vùng lệch + mốc căn chỉnh tay).
- Done: dùng như LA 200k + màn standalone + lưu phiên + so sánh (nếu kịp).

### TUẦN 4 — TEST + ĐÓNG GÓI (8 step)

- [ ] W4-1. Chạy lại full 9 kịch bản quay video: UART đúng/sai baud, I2C sensor, SPI 4ch, aliasing, 12V sống, giải mã tại chỗ, lưu/xem lại phiên, so sánh mẫu tham chiếu, so sánh 2 phiên.
- [ ] W4-2. Đo chốt skew/jitter/f_max không méo, xuất bảng số liệu báo cáo.
- [ ] W4-3. Gọn hóa breadboard + hộp mica + nhãn kênh/GND (chống chạm khi demo).
- [ ] W4-4. Cắt video <3 phút (capture → decode → bảo vệ → màn standalone).
- [ ] W4-5. Viết báo cáo Word theo template, tên `HọTên - 4 kênh 12MHz`.
- [ ] W4-6. Slide ngắn: spec → HW → FW → test → kết luận + hướng RP2350/FPGA.
- [ ] W4-7. Tự review chéo: đọc lại yêu cầu bảo vệ + timing, vá chỗ thiếu.
- [ ] W4-8. Chuẩn bị câu trả lời vấn đáp theo từng step (1 người = tất cả).
- Done: báo cáo + slide + video + máy demo sống.

## 5. Dự phòng (mỗi subsystem 1 plan B, kích hoạt trong 1 buổi)

| Hỏng ở đâu | Dấu hiệu | Plan B (đã chuẩn bị trước) |
|---|---|---|
| Pico cháy/hết hàng | không enumerate USB | chuyển STM32F411 BlackPill (Timer+DMA, spec hạ còn 2–8MHz, vẫn qua min) |
| OLED SPI hết hàng | chỉ còn I2C | dùng I2C tạm ở 400kHz, giảm refresh (vẽ khi STOP, không vẽ realtime lúc sample) |
| SUMP không tương thích PulseView bản mới | dump rác | rơi về Python plot + CSV, decode UART thủ công (đọc bit theo baud) — vẫn đủ điểm decode cơ bản |
| Bảo vệ méo ở 12MHz | rise > 30% period | thêm C_bù / hạ max còn 8MHz và khai trung thực trong báo cáo (điểm trung thực > điểm MHz ảo) |
| USB-CDC rớt ở burst dài | mất byte | chia burst 16KB/chunk + checksum + retry, giảm depth còn 64k |
| Breadboard nhiễu ở MHz cao | ringing/串扰 | rút ngắn dây <5cm, thêm 1 mass/kênh, hạ xuống 4M để demo rồi ghi nhận xét |
| Cháy timeline tuần 3 | chưa có SUMP | cắt stretch (8ch, playback, TFT), giữ base + video 5 test |
| Không có scope đối chứng | không đo được skew | dùng 2 output cùng MCU2 + đo bằng chính LA ở rate cao nhất, đối chiếu logic thay vì tuyệt đối |
| Flash đầy / ghi quá nhiều lần | không lưu được phiên mới | giới hạn số lần ghi mỗi phiên + báo "Flash đầy" trên OLED, xóa phiên cũ thủ công |
| Bộ so khớp byte chậm hơn thời gian thực | không kịp hiện kết quả | so khớp trên dữ liệu đã nén trong Flash, giới hạn 64k mẫu/phiên cho chế độ so sánh |

## 6. Rủi ro thực tế + nhược điểm + cách biện hộ

**Rủi ro kỹ thuật:**
1. R_series 47k + C_in (probe + 245 ~10–15pF) tạo LPF ~200–300kHz–vài MHz → méo ở 12MHz.
   Khắc phục: C_bù, jumper bypass chia áp cho mode nhạy, khai f_max thực đo được.
2. USB-FS nghẽn → không realtime 24M/8ch. Khắc phục: offline burst (đề cho phép),
   show phép tính 24MB/s >> 1MB/s trong báo cáo.
3. RAM nông (100k mẫu ≈ 8ms @12MHz) → mất sự kiện dài. Khắc phục: pre/post trigger
   + giảm rate khi bắt protocol chậm + ghi rõ cửa sổ thời gian/mỗi rate.
4. Ngưỡng TTL cố định (HC ~2.5–3.5V) → đo 1.8V kém. Khắc phục: đề xuất LVC245 cho 1.8V
   ở hướng phát triển; demo với 3V3/5V.
5. Breadboard: ringing, crosstalk, rơi dây lúc demo. Khắc phục: dây ngắn, bó mass,
   mang theo bộ dây + ảnh chụp wiring backup.
6. TVS/diode hàn SOT/SMA khó cho người mới. Khắc phục: mua breakout/SOD-DIP adapter
   hoặc nhờ hàn, giữ 1 kênh mẫu đã hàn sẵn.
7. OC để lên 24MHz gây nóng/sai USB. Khắc phục: chỉ OC khi demo burst, mặc định 133MHz.

**Nhược điểm hệ thống (nói trước trong báo cáo để khỏi bị trừ):**
- Depth nông hơn clone (100k vs 10G) vì không có USB-HS + RAM PC streaming.
- Không analog, không ngưỡng lập trình, không BW cao như Saleae/DSLogic.
- OLED 0.96" chỉ overview, không thay PC khi decode dài.
- Bảo vệ 200V mới chứng minh mô phỏng + test DC 12V; xung 200V thật không tự tạo ở nhà
  vì nguy hiểm — ghi rõ giới hạn này (trung thực được điểm).

**Câu biện hộ khi vấn đáp:**
- "Sao không 24MHz 8ch như hàng chợ?" → đưa phép tính USB-FS + RAM, nhấn mạnh đề
  cho phép offline và min chỉ 1kHz; PoC chọn 4ch/12MHz để giữ skew + bảo vệ, đúng
  triết lý SQ200 (ít kênh nhưng trigger/bảo vệ tốt).
- "Bảo vệ có thật không?" → show 3 plot LTspice + test 12V sống + bảng phạm vi,
  thừa nhận xung 200V chỉ mô phỏng vì an toàn.
- "Tương thích app có sẵn?" → cắm PulseView live + mở Python CSV, không demo app tự viết nửa vời.
