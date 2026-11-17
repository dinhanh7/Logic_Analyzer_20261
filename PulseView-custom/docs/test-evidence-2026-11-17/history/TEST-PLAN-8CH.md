# Test plan 8 kênh — Logic Analyzer RP2350 + ESP32 signal generator

**Mục tiêu:** xác định **giới hạn thật** của 8 kênh @ 25 MHz cho từng giao thức
(UART / I2C / SPI / CAN), không chỉ chứng minh "chạy được" ở một tốc độ đẹp.

**Cách làm:** mỗi giao thức có 20 bài, tốc độ (hoặc tỉ lệ lấy mẫu) tăng dần.
Bài cuối chắc chắn **phải hỏng** — nếu không hỏng thì chưa chạm giới hạn, phải
thêm bài cao hơn. Kết quả ghi vào cột `KQ`, sau đó tổng hợp vào §9 và
`docs/TEST-Nov3.md`.

**Ký hiệu kỳ vọng:** `P` = pass chắc, `M` = biên (có thể pass có thể fail, đây
là bài đáng giá nhất), `F` = fail dự kiến (để chứng minh ngưỡng).

---

## 1. Phần cứng

### 1.1. Nối dây 8 kênh (bắt buộc đủ 8 dây + GND)

| Pico 2 | ESP32 DevKitC | Tín hiệu | Ghi chú |
|---|---|---|---|
| GND | GND | — | chung đất, 1 dây là đủ |
| GP0 | GPIO26 | UART TX | 115200 8N1, `"LA1234\n"` |
| GP1 | GPIO18 | I2C SCL | bit-bang, mức 3.3 V |
| GP2 | GPIO19 | I2C SDA | bit-bang, ACK do ESP32 tự kéo |
| GP3 | GPIO21 | SPI CS | active low |
| GP4 | GPIO22 | SPI SCK | mode 0 |
| GP5 | GPIO23 | SPI MOSI | `DE AD BE EF 00 01 02 03` |
| GP6 | GPIO25 | SPI MISO | **jumper GPIO23 → GPIO25** |
| GP7 | GPIO32 | CAN TX | TWAI NO-ACK, loopback TX↔RX trên chân 32 |

- Không dùng breadboard: cắm trực tiếp jumper female–female.
- Chỉ 1 dây GPIO23→GPIO25 là jumper phụ duy nhất (MISO lấy lại MOSI).
- TFT + 4 nút trên Pico dùng GP8–GP16 và GP22 (BACK) ⇒ **không đụng** GP0–GP7.
  Lưu ý GPIO22 của ESP32 nối vào **GP4** của Pico, không liên quan GP22 của Pico.
- GND ESP32 và GND Pico 2 **phải** nối trước khi cắm tín hiệu.

### 1.2. Kiểm tra dây trước khi test

```bash
python3 tools/host_la.py --port /dev/ttyACM0 --pinprobe down
python3 tools/host_la.py --port /dev/ttyACM0 --pinprobe up
```

Cả 8 kênh phải đọc được `0x00` rồi `0xff` (không có kênh nào chết/đứt dây).
Nếu một kênh không đổi: dừng lại, sửa dây, đừng chạy tiếp.

---

## 2. Chuẩn bị firmware

### 2.1. ESP32 (bản mới có lệnh `set`)

```bash
cd ~/MyEspProjects/la-signal-gen
source ~/.espressif/v6.0.1/esp-idf/export.sh
idf.py -p /dev/ttyUSB0 flash monitor
```

Lệnh mới (đã thêm cho đợt test này):

```
set uart <baud>    300..5000000
set i2c <khz>      4..500      (thực tế = 500 / half_us, xem status)
set spi <khz>      100..40000
set can <kbps>     10..1000    (nạp lại driver TWAI, mất ~vài ms)
status             in tốc độ đang đặt
```

`set` có hiệu lực ngay ở lần phát kế tiếp, không cần reboot. `set can` giữ
nguyên bộ đếm data (data tăng dần), nên **đừng** so 4 byte data với giá trị cố
định — chỉ so `ID=0x123 DLC=4` và 4 byte tăng dần.

Trước khi sweep từng giao thức, tắt burst để mỗi giao thức phát riêng:

```
burst off
```

### 2.2. LA

```bash
cd ~/EmbeddedSystem/Code/Logic_Analyzer_20261/PulseView-custom
sudo /tmp/opencode/picotool-build/picotool reboot -f -u
sudo /tmp/opencode/picotool-build/picotool load -x build/firmware-pico2/rp2350_la.uf2
```

---

## 3. Quy ước chạy

Mọi bài dùng **trigger**, vì mỗi giao thức chỉ phát ~0.3 ms trong chu kỳ 100 ms:
capture không trigger ở 25 MHz (1.6 ms) gần như chắc chắn rơi vào vùng im lặng.

Lệnh mẫu (thay các giá trị trong bảng):

```bash
python3 tools/host_la.py --port /dev/ttyACM0 \
  --rate 25m --count 40000 --pre 2000 \
  --mask 0xff --trigger edge:0:fall \
  --decode-all --uart-baud 115200 --can-baud 250000
```

- `--rate` : `1m`, `5m`, `25m`, … (đúng 8 giá trị: 1k/10k/100k/250k/500k/1m/5m/25m)
- `--count`, `--pre` : số mẫu, số mẫu giữ trước trigger (`pre < count`)
- `--trigger edge:<CH>:rise|fall` : CH 0..7
- `--decode-all` : giải mã cả 4 giao thức theo map cố định ở §1.1
- `--uart-baud`, `--can-baud` : **phải khớp** tốc độ đang đặt trên ESP32

**PASS** khi:
1. Có dòng decode đúng như cột "Kỳ vọng" (sai số baud UART ≤ 1%),
2. `trigger index` ≠ `none`, `flags 0x00`,
3. 3 dòng còn lại của `--decode-all` in `no data` / `no transaction found`
   (giao thức không kích hoạt thì đúng là phải trống).

**FAIL** khi decode sai/không có byte, hoặc `no trigger`, hoặc sai số baud > 2%.
Ghi lại **tốc độ thật** đo được (UART in ra `UART <baud> 8N1`), không ghi số gõ vào.

Thời lượng một khung để chọn `count` cho đủ cửa sổ:

| Tín hiệu | Công thức | 115200 | 9600 | 1 Mbps | 125 kbps |
|---|---|---|---|---|---|
| UART 7 byte | 70 bit / baud | 0.61 ms | 7.3 ms | 70 µs | — |
| CAN 1 frame 4 byte | ~111 bit / baud | — | — | 111 µs | 888 µs |
| I2C 3 byte | ~56 × half_period | 0.28 ms | — | — | — |
| SPI 8 byte | 64 bit / f_sck | 64 µs @1 MHz | — | — | — |

---

## 4. UART — 20 bài

Baseline: `LA1234\n`, 8N1, CH0. Trigger `edge:0:fall` (start bit).
Kỳ vọng: `UART <baud> 8N1: "LA1234."` — LF in thành `.` (decoder chỉ in ASCII
32..126, buffer text 96 ký tự). Số baud in ra lệch ±1 % do làm tròn Q16, ví dụ
115200 → `115201`, 9600 → `9600`: chấp nhận, đừng coi là fail.

| # | Mục tiêu | ESP32 | LA (rate / N / pre / trig) | Kỳ vọng | KQ |
|---|---|---|---|---|---|
| 1 | chậm nhất, cửa sổ dài | `set uart 9600` | 1m / 10000 / 500 / CH0↓ | P `UART 9600 ... "LA1234."` | | P |
| 2 | chậm | `set uart 19200` | 1m / 5000 / 200 / CH0↓ | P | | P |
| 3 | chậm | `set uart 38400` | 1m / 5000 / 200 / CH0↓ | P | | P |
| 4 | chậm | `set uart 57600` | 1m / 4000 / 100 / CH0↓ | P | | P |
| 5 | **baseline 8 kênh** | `set uart 115200` | 25m / 40000 / 2000 / CH0↓ | P | | P |
| 6 | baseline ở 5 MHz | `set uart 115200` | 5m / 40000 / 2000 / CH0↓ | P (43 mẫu/bit) | | P |
| 7 | 115200 @ 1 MHz — biên dưới | `set uart 115200` | 1m / 40000 / 2000 / CH0↓ | M (8.6 mẫu/bit, làm tròn 8) | | M |
| 8 | 230400 @ 25 MHz | `set uart 230400` | 25m / 40000 / 2000 / CH0↓ | P (108 mẫu/bit) | | P |
| 9 | 230400 @ 5 MHz | `set uart 230400` | 5m / 40000 / 2000 / CH0↓ | M (21.7 mẫu/bit) | | P |
| 10 | 230400 @ 1 MHz | `set uart 230400` | 1m / 40000 / 2000 / CH0↓ | F (4.3 mẫu/bit) | | P |
| 11 | 460800 @ 25 MHz | `set uart 460800` | 25m / 40000 / 2000 / CH0↓ | P | | P |
| 12 | 460800 @ 5 MHz | `set uart 460800` | 5m / 40000 / 2000 / CH0↓ | M (10.8) | | P |
| 13 | 921600 @ 25 MHz | `set uart 921600` | 25m / 40000 / 2000 / CH0↓ | P (27.1) | | P |
| 14 | 921600 @ 5 MHz | `set uart 921600` | 5m / 40000 / 2000 / CH0↓ | F (5.4) | | P |
| 15 | 1 Mbaud @ 25 MHz | `set uart 1000000` | 25m / 40000 / 2000 / CH0↓ | P (25.0) | | P |
| 16 | 1.5 Mbaud | `set uart 1500000` | 25m / 40000 / 2000 / CH0↓ | M (16.67→16) | | P |
| 17 | 2 Mbaud | `set uart 2000000` | 25m / 40000 / 2000 / CH0↓ | F (12.5→12) | | P |
| 18 | 3 Mbaud | `set uart 3000000` | 25m / 40000 / 2000 / CH0↓ | F (8.3) | | P |
| 19 | 5 Mbaud (max ESP32) | `set uart 5000000` | 25m / 40000 / 2000 / CH0↓ | F (5.0) | | P |
| 20 | cửa sổ dài, không trigger, nhiều message | `set uart 115200` | 25m / 262144 / 0 / none | P, text lặp `LA1234.` tới 96 ký tự rồi cắt | | P |

`--decode-all` **luôn cần** `--uart-baud` đúng: truyền 0 thì firmware dùng
115200 mặc định, không tự dò. Auto-baud chỉ có ở op `ANALYZE DECODE` cũ
(map CH0..CH3), không dùng cho sơ đồ 8 kênh này.

Ghi thêm: bài 16–19 nếu ESP32 báo lỗi chia tần (sai số > 2%) thì đó là **giới
hạn của máy phát**, ghi rõ vào KQ, đừng quy cho LA. Ở 25 MHz, UART limit lý
thuyết nằm ở ~1.5–2 Mbaud (dưới 16 mẫu/bit thì làm tròn bit width bắt đầu trôi).

---

## 5. I2C — 20 bài

Baseline: `0x68 W ACK 0x01 ACK 0x5A ACK`, CH1=SCL, CH2=SDA. Trigger
`edge:1:rise` (clock đầu tiên). `--mask 0xff` để vẫn thấy 2 dây.

Kỳ vọng: `I2C: 0x68 W ACK 0x01 ACK 0x5A ACK`.

| # | Mục tiêu | ESP32 | LA (rate / N / pre / trig) | Kỳ vọng | KQ |
|---|---|---|---|---|---|
| 1 | baseline 100 kHz @ 1 MHz | `set i2c 100` | 1m / 100000 / 1000 / CH1↑ | P | | P |
| 2 | baseline @ 25 MHz | `set i2c 100` | 25m / 40000 / 2000 / CH1↑ | P | | P |
| 3 | 100 kHz @ 100 kHz — dưới Nyquist | `set i2c 100` | 100k / 100000 / 1000 / CH1↑ | F (1 mẫu/clock) | | F |
| 4 | 100 kHz @ 250 kHz — biên | `set i2c 100` | 250k / 100000 / 1000 / CH1↑ | M (2.5 mẫu/clock) | | M |
| 5 | 100 kHz, cửa sổ dài, không trigger | `set i2c 100` | 1m / 262144 / 0 / none | P (262 ms ⇒ 2–3 transaction) | | P |
| 6 | 4 kHz — chậm nhất | `set i2c 4` | 1m / 20000 / 1000 / CH1↑ | P (7 ms/transaction) | | P |
| 7 | 10 kHz | `set i2c 10` | 1m / 10000 / 1000 / CH1↑ | P | | P |
| 8 | 25 kHz | `set i2c 25` | 1m / 10000 / 500 / CH1↑ | P | | P |
| 9 | 50 kHz | `set i2c 50` | 1m / 10000 / 500 / CH1↑ | P | | P |
| 10 | 125 kHz | `set i2c 125` | 5m / 40000 / 2000 / CH1↑ | P (half 4 µs) | | P |
| 11 | 150 kHz | `set i2c 150` | 5m / 40000 / 2000 / CH1↑ | P (half 3 µs) | | P |
| 12 | 250 kHz @ 5 MHz | `set i2c 250` | 5m / 40000 / 2000 / CH1↑ | P | | P |
| 13 | 250 kHz @ 25 MHz | `set i2c 250` | 25m / 40000 / 2000 / CH1↑ | P | | P |
| 14 | 250 kHz @ 1 MHz | `set i2c 250` | 1m / 40000 / 2000 / CH1↑ | P (10 mẫu/bit) | | P |
| 15 | 500 kHz (half 1 µs) @ 25 MHz | `set i2c 500` | 25m / 40000 / 2000 / CH1↑ | M — đo lại tần số thật | | P |
| 16 | 500 kHz @ 5 MHz | `set i2c 500` | 5m / 40000 / 2000 / CH1↑ | M | | P |
| 17 | pre = 1 ⇒ mất START | `set i2c 100` | 25m / 40000 / 1 / CH1↑ | **F** `no transaction found` | | F |
| 18 | cửa sổ 50 % trước trigger | `set i2c 100` | 25m / 40000 / 20000 / CH1↑ | P | | P |
| 19 | cửa sổ quá ngắn (1000 mẫu) | `set i2c 100` | 25m / 1000 / 999 / CH1↑ | **F** `no transaction found` | | F |
| 20 | lặp 5 lần @ 500 kHz — độ ổn định | `set i2c 500` | 25m / 40000 / 2000 / CH1↑ ×5 | ghi số lần pass/5 | | P |

Bài 17 và 19 là **bài âm** có chủ đích: chứng minh cần pretrigger để bắt START,
và cần đủ cửa sổ. Nếu chúng lại PASS thì decoder đang đọc sai, phải xem lại.

Giới hạn I2C thực tế nằm ở **ESP32 bit-bang**, không phải LA: `esp_rom_delay_us(1)`
không chính xác tuyệt đối nên trên 250 kHz chu kỳ bị méo. Vì vậy mỗi bài ghi
**tần số đo được từ dữ liệu capture** (`host_la.py --out` rồi đo cạnh SCL), không
tin con số `set i2c`.

---

## 6. SPI — 20 bài

Baseline: mode 0, CH3=CS, CH4=SCK, CH5=MOSI, CH6=MISO, khung
`DE AD BE EF 00 01 02 03`. Trigger `edge:3:fall` (CS xuống).
Kỳ vọng: `SPI: DE AD BE EF 00 01 02 03 |`.

| # | Mục tiêu | ESP32 | LA (rate / N / pre / trig) | Kỳ vọng | KQ |
|---|---|---|---|---|---|
| 1 | 100 kHz @ 1 MHz | `set spi 100` | 1m / 100000 / 1000 / CH3↓ | P | P |
| 2 | 100 kHz @ 25 MHz | `set spi 100` | 25m / 40000 / 2000 / CH3↓ | P | P |
| 3 | 250 kHz | `set spi 250` | 5m / 40000 / 2000 / CH3↓ | P | P |
| 4 | 500 kHz | `set spi 500` | 5m / 40000 / 2000 / CH3↓ | P (10 mẫu/clock)| P |
| 5 | 1 MHz @ 5 MHz — baseline cũ | `set spi 1000` | 5m / 40000 / 2000 / CH3↓ | P | P |
| 6 | 1 MHz @ 25 MHz | `set spi 1000` | 25m / 40000 / 2000 / CH3↓ | P (25 mẫu/clock)| P |
| 7 | 2 MHz | `set spi 2000` | 25m / 40000 / 2000 / CH3↓ | P (12.5)| P |
| 8 | 3 MHz | `set spi 3000` | 25m / 40000 / 2000 / CH3↓ | P (8.3)| P |
| 9 | 4 MHz | `set spi 4000` | 25m / 40000 / 2000 / CH3↓ | P (6.25)| P |
| 10 | 5 MHz | `set spi 5000` | 25m / 40000 / 2000 / CH3↓ | M (5.0)| P |
| 11 | 6 MHz | `set spi 6000` | 25m / 40000 / 2000 / CH3↓ | M (4.2)| P |
| 12 | 8 MHz | `set spi 8000` | 25m / 40000 / 2000 / CH3↓ | M (3.1)| P |
| 13 | 10 MHz | `set spi 10000` | 25m / 40000 / 2000 / CH3↓ | F (2.5)| P |
| 14 | 12 MHz | `set spi 12000` | 25m / 40000 / 2000 / CH3↓ | F (2.08)| F |
| 15 | 16 MHz | `set spi 16000` | 25m / 40000 / 2000 / CH3↓ | F (1.56)| F |
| 16 | 20 MHz | `set spi 20000` | 25m / 40000 / 2000 / CH3↓ | F (1.25)| F |
| 17 | 26 MHz (max ESP32 = 80/3) | `set spi 26666` | 25m / 40000 / 2000 / CH3↓ | F (0.96)| F |
| 18 | mask chỉ 4 dây SPI | `set spi 1000` | 25m / 40000 / 2000 / CH3↓, `--mask 0x78` | P | P |
| 19 | 1 MHz @ 1 MHz — dưới Nyquist | `set spi 1000` | 1m / 262144 / 0 / none | **F** (1 mẫu/clock, mất byte)| F |
| 20 | bỏ jumper MISO 23↔25 | `set spi 1000` | 25m / 40000 / 2000 / CH3↓ | P — MOSI vẫn đúng, chứng minh decoder không phụ thuộc MISO| P |

**Kết quả đo (25 MHz, máy thật):** PASS liên tục tới **10 MHz** (2.5 mẫu/clock),
FAIL từ **12 MHz** (2.08 mẫu/clock). Biên thực tế nằm giữa 10 và 12 MHz; SPI
là giao thức **đo trực tiếp giới hạn lấy mẫu** vì mỗi cạnh SCK đều là dữ liệu.
Kỳ vọng ban đầu (F tại 10 MHz) đã sai — sửa theo số đo.

---

## 7. CAN — 20 bài

Baseline: TWAI NO-ACK, ID `0x123`, DLC 4, data tăng dần mỗi frame 100 ms,
CH7. Trigger `edge:7:fall` (SOF, dominant = 0). **Phải truyền đúng `--can-baud`**,
CAN decoder không tự dò baud.

Kỳ vọng: `CAN ID=0x123 DLC=4 DATA=<4 byte tăng dần>` (không so giá trị tuyệt đối).

| # | Mục tiêu | ESP32 | LA (rate / N / pre / trig) | `--can-baud` | Kỳ vọng | KQ |
|---|---|---|---|---|---|---|
| 1 | 125 kbps @ 1 MHz | `set can 125` | 1m / 262144 / 0 / none | 125000 | P (8 mẫu/bit) | | P |
| 2 | 125 kbps @ 25 MHz | `set can 125` | 25m / 40000 / 2000 / CH7↓ | 125000 | P (200) | | M |
| 3 | 125 kbps @ 500 kHz | `set can 125` | 500k / 262144 / 0 / none | 125000 | P (4.0) | | P |
| 4 | 125 kbps @ 250 kHz | `set can 125` | 250k / 262144 / 0 / none | 125000 | M (2.0) | | P |
| 5 | 250 kbps @ 1 MHz | `set can 250` | 1m / 100000 / 1000 / CH7↓ | 250000 | P (4.0) | | P |
| 6 | 250 kbps @ 5 MHz | `set can 250` | 5m / 40000 / 2000 / CH7↓ | 250000 | P (20) | | P |
| 7 | 250 kbps @ 25 MHz — baseline | `set can 250` | 25m / 40000 / 2000 / CH7↓ | 250000 | P (100) | | P |
| 8 | 500 kbps @ 1 MHz | `set can 500` | 1m / 100000 / 1000 / CH7↓ | 500000 | M (2.0) | | P |
| 9 | 500 kbps @ 5 MHz | `set can 500` | 5m / 40000 / 2000 / CH7↓ | 500000 | P (10) | | P |
| 10 | 500 kbps @ 25 MHz | `set can 500` | 25m / 40000 / 2000 / CH7↓ | 500000 | P (50) | | P |
| 11 | 800 kbps @ 5 MHz | `set can 800` | 5m / 40000 / 2000 / CH7↓ | 800000 | M (6.25) | | P |
| 12 | 800 kbps @ 25 MHz | `set can 800` | 25m / 40000 / 2000 / CH7↓ | 800000 | P (31.25) | | P |
| 13 | 1 Mbps @ 5 MHz | `set can 1000` | 5m / 40000 / 2000 / CH7↓ | 1000000 | P (5.0) | | P |
| 14 | 1 Mbps @ 25 MHz | `set can 1000` | 25m / 40000 / 2000 / CH7↓ | 1000000 | P (25) | | P |
| 15 | 1 Mbps @ 1 MHz | `set can 1000` | 1m / 100000 / 1000 / CH7↓ | 1000000 | F (1 mẫu/bit) | | M |
| 16 | 10 kbps — chậm nhất | `set can 10` | 1m / 262144 / 1000 / CH7↓ | 10000 | P (11 ms/frame) | | P |
| 17 | sai baud khi decode | `set can 250` | 25m / 40000 / 2000 / CH7↓ | **500000** | **F** (ID/DLC rác) | | F |
| 18 | mask chỉ CH7 | `set can 250` | 25m / 40000 / 2000 / CH7↓ | 250000 | P, `--mask 0x80` | | P |
| 19 | cửa sổ hẹp đúng 1 frame | `set can 250` | 25m / 20000 / 5000 / CH7↓ | 250000 | P (0.8 ms) | | P |
| 20 | cửa sổ dài, không trigger | `set can 250` | 1m / 262144 / 0 / none | 250000 | P (262 ms, 2–3 frame) | | P |

Bài 17 là bài âm: nếu vẫn ra ID đúng thì decoder đang bỏ qua `--can-baud`, phải
kiểm tra lại. Bài 4, 8, 11 cho biết ngưỡng "2 mẫu/bit có đủ không".

---

## 8. Bài tích hợp (sau khi biết giới hạn từng giao thức)

Đặt burst lại và chạy cả 4 giao thức trong một cửa sổ:

```
burst on
```

| # | Mục tiêu | ESP32 | LA | Kỳ vọng | KQ |
|---|---|---|---|---|---|
| I1 | 4 giao thức cùng lúc, cấu hình mặc định | `set uart 115200` `set i2c 100` `set spi 1000` `set can 250` | 25m / 40000 / 2000 / CH0↓ `--uart-baud 115200 --can-baud 250000` | 4 dòng decode đúng | **PASS 4/4** |
| I2 | mọi giao thức ở tốc độ tốt nhất đã tìm được | `set uart 5000000` `set i2c 500` `set spi 10000` `set can 1000` | 25m / 40000 / 2000 / CH0↓ `--uart-baud 5000000 --can-baud 1000000` | 4 dòng đúng | **PASS 4/4** (UART 5 Mbaud, I2C 500 kHz, SPI 10 MHz, CAN 1 Mbps cùng lúc) |
| I3 | cửa sổ tối đa | như I1 nhưng `set spi 100` | 1m / 262144 / 0 / none | 4 dòng đúng, 2 burst trong 262 ms | **PASS 4/4** với SPI 100 kHz; SPI 1 MHz @ 1 MHz sampling FAIL (dưới Nyquist) |
| I4 | 50 % pretrigger | như I1 | 25m / 40000 / 20000 / CH0↓ | 4 dòng đúng | **PASS 4/4** |
| I5 | chỉ 1 kênh (mask 0x01) | như I1 | 25m / 40000 / 2000 / CH0↓ `--mask 0x01` | UART đúng, có thể thiếu I2C/SPI/CAN nếu mask ảnh hưởng capture | **PASS 4/4** — mask chỉ lọc dữ liệu trả về, decoder vẫn thấy đủ 8 bit |

I5 kiểm tra mask không làm mất dữ liệu decode (firmware luôn lấy đủ 8 bit/kênh).

---

## 9. Tổng hợp giới hạn

Điền sau khi chạy:

Đo ngày 2026-11-16, firmware LAR2 1.2, ESP32 ESP-IDF v6.0.1, nhiệt độ phòng.

| Giao thức | Pass cao nhất | Fail thấp nhất | Biên | Ghi chú |
|---|---|---|---|---|
| UART baud @ 25 MHz | **5 Mbaud** (5 mẫu/bit) | chưa có | **giới hạn máy phát** | 9600…5 Mbaud đều PASS; ESP32 không phát nhanh hơn 5 Mbaud nên chưa chạm trần LA |
| UART baud @ 5 MHz | **921600** (5.4 mẫu/bit) | chưa test | ~4 mẫu/bit | 1 Mbaud @ 5 MHz chưa chạy |
| UART baud @ 1 MHz | **230400** (4.3 mẫu/bit) | chưa test | ~4 mẫu/bit | 115200 @ 1 MHz cũng PASS (8.6) |
| I2C clock | **500 kHz** @ 25 MHz và 5 MHz | — | — | 100 kHz @ 100 kHz sampling FAIL đúng dự đoán; 250 kHz sampling ra thêm byte rác nhưng vẫn có transaction |
| SPI SCK @ 25 MHz | **10.5 MHz** (2.38 mẫu/clock) | **11 MHz** (2.27) | 10.5–11 MHz | đo thêm 10.5 (3/3 PASS) và 11 (0/3): trần lấy mẫu thực của LA |
| CAN bit rate @ 25 MHz | **1 Mbps** (25 mẫu/bit) | chưa test | — | 125…1000 kbps đều PASS |
| CAN bit rate @ 5 MHz | **1 Mbps** (5 mẫu/bit) | — | 4–5 mẫu/bit | 800 kbps (6.25) và 1 Mbps (5) PASS |
| CAN bit rate @ 1 MHz | **500 kbps** (2 mẫu/bit) | — | 2 mẫu/bit | 1 Mbps @ 1 MHz có PASS nhưng chỉ 1 mẫu/bit ⇒ không tin cậy |

**Giới hạn máy phát (không phải LA):** UART ≤ 5 Mbaud; I2C ≤ 500 kHz (bit-bang);
SPI ≤ 26.666 MHz (SPI2 = 80 MHz / 3); CAN 20…1000 kbps (10 và 16 kbps vượt
prescaler 256 của TWAI trên ESP32).

**Kết luận:** trong 4 giao thức, chỉ **SPI** chạm trần lấy mẫu thật của LA
(10 MHz PASS / 12 MHz FAIL @ 25 MHz). UART và CAN vẫn còn dư địa ở 25 MHz;
I2C bị giới hạn bởi máy phát.

Quy tắc kết luận:
- **Giới hạn của LA** = tốc độ cao nhất còn PASS **khi baud/frame của ESP32 vẫn
  đúng** (kiểm tra bằng cách đo tần số thật từ capture, không tin `status`).
- Nếu ESP32 sai trước LA (UART chia tần, I2C bit-bang méo), ghi
  **"giới hạn máy phát"** — không được ghi vào giới hạn LA.
- 3 lần lặp ở tốc độ biên; 2/3 pass ⇒ coi là pass nhưng ghi rõ "không ổn định".

Sau khi có số, cập nhật `docs/TEST-Nov3.md`:
- §5.3 "Giới hạn 8 kênh theo giao thức" (bảng trên),
- §7.0 checklist: giới hạn đã đo, ngày, firmware, nhiệt độ phòng.

---

## 10. Phụ lục — vì sao các ngưỡng đó

- Mỗi mẫu = 40 ns @ 25 MHz. Số mẫu/bit = `rate / baud`.
- UART/CAN: decoder lấy mẫu giữa bit nên cần ≥ 4 mẫu/bit để bù làm tròn
  `bit_width` (Q16). Đo thực tế: UART 230400 @ 1 MHz (4.3 mẫu/bit) vẫn PASS,
  CAN 500 kbps @ 1 MHz (2 mẫu/bit) PASS. Các bài 1.5/2/3/5 Mbaud @ 25 MHz đều
  PASS vì vẫn ≥ 5 mẫu/bit — kỳ vọng FAIL ban đầu là sai, đã sửa theo số đo.
- SPI: decoder bắt cạnh lên SCK, cần ≥ 1 mẫu mức thấp + 1 mẫu mức cao mỗi chu kỳ
  ⇒ trần cứng lý thuyết `f_sck < 12.5 MHz`; đo được 10.5 MHz PASS / 11 MHz FAIL,
  tức ~2.3 mẫu mỗi nửa chu kỳ (không phải 6–8 MHz như dự đoán ban đầu).
- I2C: decoder bắt cạnh SCL và lấy SDA tại đó, không cần baud; giới hạn nằm ở
  chất lượng cạnh SCL (bit-bang) chứ không ở LA.
- Trigger restart: buffer đầy trước khi trigger ⇒ re-arm, tối đa 2000 lần. Ở
  25 MHz buffer 262144 = 10.5 ms < 100 ms chu kỳ ⇒ **luôn** dùng trigger; nếu
  thấy `no trigger` sau ~2000 lần thì kiểm tra dây trước, không phải lỗi LA.
- Vị trí trigger trong cửa sổ (2026-11-17): trigger phải nằm trong
  `CAPTURE_BYTES - (count - pretrigger)`. Nếu tìm thấy muộn hơn, firmware
  **trượt cửa sổ sang phải** và giữ đúng `count` mẫu — nghĩa là số mẫu *trước*
  trigger có thể ít hơn `pre` đã yêu cầu. `RESULT`/`INFO.trigger_index` vẫn là
  vị trí thật trong cửa sổ trả về (đôi khi > `pre`, ví dụ 74922 thay vì 2000).
  Không còn trường hợp treo `capture timeout` như bản trước.
- Bài 19 của SPI (1 MHz SPI lấy mẫu 1 MHz = 1 mẫu/bit) là aliasing: kết quả phụ
  thuộc điểm lấy mẫu, nên kỳ vọng đã đổi thành FAIL (giống I2C 100 kHz @ 100 kHz).
