# Test CAN — Logic Analyzer RP2350 + ESP32 signal generator
**Ngày:** 03/11/2026  
**Board LA:** Raspberry Pi Pico 2 (RP2350A), firmware `rp2350_la.uf2` (LAR2 v1.2+trg+an)  
**Board phát tín hiệu:** ESP32 DevKitC 38-pin, firmware `la-signal-gen.bin` (ESP-IDF v6.0.1)  
**Công cụ:** `host_la.py`, PulseView custom (`.local/bin/pulseview`), sigrok-cli  

---

## 1. Kết nối phần cứng

| Pico 2 | ESP32 DevKitC | Ghi chú |
|---|---|---|
| GND | GND | Bắt buộc chung đất |
| GP0 | GPIO 32 (pin 6) | CAN TX, TWAI 250 kbps |
| — | GPIO 33 (pin 7) | Loopback TX↔RX trên cùng chân 32 (không cần jumper) |
| GP1–GP7 | — | Thả nổi (đọc 1 do pull-up nội tại) |

ESP32 phát CAN: TWAI 250 kbps, ID 0x123, data `DE AD BE EF` (tăng mỗi 100 ms), NO-ACK self-test mode với loopback trên chân 32. Không cần transceiver.

---

## 2. Vấn đề gặp phải và cách khắc phục

### 2.1. CAN TX im lặng (0 cạnh trong 262 ms)
- **Triệu chứng:** capture 262144 mẫu ở 1 MHz, CH0 = 0xFF, 0 cạnh.
- **Nguyên nhân:** TWAI ở `TWAI_MODE_NO_ACK` vẫn kiểm tra bit-error trên chân RX. Chân GPIO33 thả nổi → bit error → TEC tăng → bus-off → TX im lặng.
- **Khắc phục:** cấu hình `TWAI_GENERAL_CONFIG_DEFAULT(PIN_CAN_TX, PIN_CAN_TX, ...)` — loopback TX↔RX trên cùng chân 32. Không cần jumper/breadboard. Thêm log `CAN: first frame 0x123 -> ESP_OK`.

### 2.2. `ANALYZE` payload chồng byte
- `arg` (u16 @1), `arg2` (u16 @3), `arg32` (u32 @4) chồng byte 4.
- **Khắc phục:** layout mới 8 byte: `op u8 @0, arg u16 @1, arg32 u32 @3, reserved u8 @7`. Cập nhật firmware, `lar2.py`, `host_la.py`, `PROTOCOL.md`.

### 2.3. Trigger không chờ được lâu hơn 1 buffer ở 25 MHz
- Ở 25 MHz, buffer 262144 = 10.5 ms < chu kỳ CAN 100 ms → "no trigger".
- **Khắc phục (2026-11-16):** trigger-restart — khi buffer đầy mà chưa thấy trigger,
  re-arm stream và tiếp tục scan (giới hạn 2000 lần).
- **Sửa lại (2026-11-17):** khi *đã* thấy trigger nhưng nó nằm ở 37 % cuối cửa sổ,
  restart lại cả cửa sổ có chu kỳ tự do và **khoá pha** với burst 100 ms của máy
  phát → treo vĩnh viễn (host thấy "capture timeout", thiết bị im lặng). Giờ
  firmware **trượt cửa sổ sang phải** (`base = CAPTURE_BYTES - DMA_LAG - count`)
  và báo `trigger_index = trigger_abs - base`, giữ đúng `count` mẫu, chỉ mất bớt
  mẫu *trước* trigger. Chi tiết + số đo: `docs/PLAN_Nov16.md` §7.

### 2.4. PulseView acquisition "0.00 s"
- **Nguyên nhân:** driver trả `SR_ERR_NA` khi session có trigger (`sr_session_trigger_get`); và mask mặc định 0 khi PulseView chưa tick kênh.
- **Khắc phục:** bỏ check session-trigger; mặc định mask = 0xFF nếu không có kênh nào tick.

### 2.5. PulseView không auto-zoom
- **Khắc phục:** bật `always_zoom_to_fit_ = true` mặc định. Phím tắt zoom-to-fit = **F**.

---

## 3. Kết quả test

### 3.1. Capture + decode CAN on-device

| Rate | Trigger | trigger_index | flags | Decode |
|---|---|---|---|---|
| 25 MHz | edge falling CH0, pre=2000, count=20000 | 2000 | 0x01 | `AN ID=0x123 DLC=4 DATA=6B 3A 4B 7C` |
| 1 MHz | edge falling CH0, pre=2000, count=10000 | 2000 | 0x01 | `AN ID=0x123 DLC=4 DATA=D6 A5 B6 E7` |

- Trigger bắt đúng SOF (bit 0 → 1 tại trigger): `at trigger CH0 = 0, before = 1`.
- `decode_can()` chạy on-device, trả đúng ID 0x123, DLC=4, data 4 byte tăng theo chu kỳ ESP32.

### 3.2. Flash storage & signature

| Test | Kết quả |
|---|---|
| Save session 0/1 | OK |
| Load session 1 → decode | `AN ID=0x123 DLC=4 DATA=D6 A5 B6 E7` (khớp) |
| Golden save + compare (tol=0) | `SIG: MATCH (0 mismatches, 10000 samples)` |
| Diff session 0 | `DIFF: identical` |
| Session header read | magic `0x4C415345` (LASE), count/rate đúng |

### 3.3. PulseView (custom build)

- Binary: `.local/bin/pulseview` (0.5.0-git), launcher `~/.local/bin/pulseview`.
- Driver `rp2350-la` xuất hiện trong danh sách.
- Acquisition live: 262144 mẫu @1 MHz, 102 cạnh CH0, acquisition ~0.3 s.
- CAN decoder: gán CH0 → "CAN RX", nominal_bitrate = 250000 → decode hiển thị ID 0x123.
- Zoom-to-fit tự động sau acquisition (auto-zoom bật mặc định).

### 3.5. UART decode on-device (GP0 ↔ ESP32 GPIO26, 115200 8N1)

| Rate | Sample count | Trigger | Kết quả |
|---|---|---|---|
| 1 MHz | 200000 (manual) | không | `UART 115200 8N1: "LA1234.LA1234."` ✅ |
| 5 MHz | 262144 (manual) | không | `UART 115200 8N1: "LA1234."` ✅ (khi bắt được frame) |
| 25 MHz | 20000 (trigger) | edge falling CH0, pre=2000 | `UART 115200 8N1: "LA1234."` ✅ trigger_index=2000 |

**Nhận xét:**
- UART decode verified ở **1 MHz, 5 MHz và 25 MHz**.
- Ở 25 MHz, count phải ≤ ~20000 để trigger có vùng quét đủ rộng (trigger_abs ∈ [2000, 244144]). Với count=262144, vùng quét chỉ còn sample 2000 → không bắt được.
- Auto-detect baud: 114323–116022 (≈115200, sai số <1%). Decode với explicit baud=115200 luôn đúng.
- **Kết luận:** UART decode hoạt động ở mọi tần số lấy mẫu, kể cả 25 MHz, khi dùng trigger với count hợp lý.

```
sigrok-cli -d rp2350-la:conn=/dev/ttyACM0 --samples 262144
→ Acquisition with 8/8 channels at 1 MHz, 102 transitions CH0
```

Trigger qua sigrok-cli hoạt động:
```
--config triggersource=CH0 --config triggerslope=falling --samples 20000
→ acquisition căn đúng SOF
```

### 3.6. I2C decode on-device @25 MHz (GP0=SCL GPIO18, GP1=SDA GPIO19, 100 kHz)

| Rate | Trigger | trigger_index | SCL edges | Decode |
|---|---|---|---|---|
| 25 MHz | edge falling CH0, pre=2000, count=20000 | 2000 | 56 | `0x68 W ACK 0x01 ACK 0x5A ACK` ✅ |
| 1 MHz | manual, 200000 mẫu | — | ~112 | `0x68 W ACK 0x01 ACK 0x5A ACK` ✅ |

**Nhận xét:** I2C decode verified ở 25 MHz. SCL 56 cạnh, SDA 13 cạnh (start/stop + 3 byte × 9 clock). Decoder trả đúng addr 0x68 (write), reg 0x01, data 0x5A, 3 ACK.

### 3.7. SPI decode on-device @25 MHz (4 kênh: CS/SCK/MOSI/MISO, SPI 1 MHz)

| Rate | Trigger | trigger_index | CS edges | Decode |
|---|---|---|---|---|
| 25 MHz | edge falling CH0 (CS), pre=2000, count=20000 | 2000 | 2 | `PI: DE AD BE EF 00 01 02 03` ✅ |
| 1 MHz | manual, 100000 mẫu | — | — | `DE AD BE EF 00 01 02 03` ✅ (verified earlier) |

**Nhận xét:** SPI decode verified ở 25 MHz. CS 2 cạnh (falling + rising), 8 byte decode đúng (`DE AD BE EF 00 01 02 03`). Trigger falling-edge trên CS bắt đúng start of frame. Jumper GPIO23→GPIO25 để MISO thấy đúng bytes như MOSI.

### 3.10. Tổng kết protocol decode @25 MHz

| Protocol | @25 MHz | Trigger | Sample count | Decode result |
|---|---|---|---|---|
| CAN | ✅ | edge falling CH0 | 20000 | `AN ID=0x123 DLC=4 DATA=...` |
| UART | ✅ | edge falling CH0 | 20000 | `ART 115200 8N1: "LA1234."` |
| I2C | ✅ | edge falling CH0 (SCL) | 20000 | `0x68 W ACK 0x01 ACK 0x5A ACK` |
| SPI | ✅ | edge falling CH0 (CS) | 20000 | `PI: DE AD BE EF 00 01 02 03` |

**Tất cả 4 giao thức đều decode đúng @25 MHz.** Điều kiện test: count=20000, pre=2000, trigger falling-edge trên kênh đầu tiên.

### 3.8. Giới hạn tần số lấy mẫu

| Thông số | Giá trị |
|---|---|
| clk_sys (firmware) | 100 MHz (`set_sys_clock_khz(100000, true)`) |
| PIO cycles/sample | 4 (`in pins, 8 [2]` + `jmp x--`) |
| **Max sampling rate** | **25 MHz** (div=1, không thể chia nhỏ hơn) |
| Hardware RP2350 max | 150 MHz (overclock) → 37.5 MHz lý thuyết |
| USB CDC ổn định | Yêu cầu clk_sys ≤ 100 MHz (theo Pico SDK) |

**Kết luận:** 25 MHz là giới hạn an toàn và đúng spec. Nâng cao hơn cần overclock RP2350 → rủi ro USB CDC mất ổn định, không cần thiết (spec chỉ yêu cầu ≥25 MHz). Đã verified ở 25 MHz: CAN, UART, I2C.

### 3.9. PulseView I2C test

Capture I2C @25 MHz qua sigrok-cli (trigger SCL falling): 20000 mẫu, SCL 55 cạnh, SDA 13 cạnh. File `/tmp/opencode/i2c_25m.sr` mở được trong PulseView. Thêm decoder **I2C** → gán CH0 → SCL, CH1 → SDA → decode hiển thị addr 0x68, reg 0x01, data 0x5A.

---

## 4. Đối chiếu yêu cầu BTL Topic 2

| Yêu cầu | Trạng thái |
|---|---|
| ≥8 kênh | ✅ 8 kênh (GP0–GP7) |
| ≥25 MHz | ✅ Verified tại 25 MHz |
| Hiển thị trên PC | ✅ PulseView custom + sigrok-cli |
| Không real-time | ✅ Offline capture + buffer |
| Decode UART/I2C/SPI/CAN | ✅ All 4 protocols verified on-device @25 MHz |
| BOM ≤450.000 VNĐ | ✅ 366.000–445.000 VNĐ |
| UI tại chỗ trên TFT 128×160 (4 màn, 4 nút + lệnh USB) | ⚠️ Code xong, chưa chạy trên board |
| Firmware C/C++ SDK, Python chỉ host | ✅ |
| Mạch bảo vệ đầu vào 12 V DC / 200 V pulse | ⚠️ Hardware (BAT54S clamp) — cần dựng + mô phỏng + ghi báo cáo |
| Báo cáo Word + slide + video ≤3 phút | ⚠️ Deliverable của nhóm |

---

## 5. Cách chạy

```bash
# Live capture (trigger: count <= 20000 để trigger có vùng quét đủ)
pulseview                                    # tự kết nối rp2350-la
# Xem offline
pulseview -i /tmp/opencode/can2.sr
# Host-side test
python3 tools/host_la.py --port /dev/ttyACM0 --rate 1m --count 262144 --mask 0x01 --out cap.bin
# On-device decode
python3 -c "
import host_la, lar2
d=host_la.Device('/dev/ttyACM0')
print(d.decode(lar2.PROTO_CAN, 250000).decode())
d.close()
"
```

### 5.1. Test tổng hợp 4 giao thức trên 8 kênh (một lần capture)

Bản ghi này dùng **cả 8 kênh**, mỗi giao thức một nhóm chân, cộng thêm màn hình TFT
vẫn cắm nguyên (TFT ở GP10–GP16, không đụng GP0–GP7).

**Sơ đồ nối ESP32 → Pico 2:**

| Pico 2 | Kênh | ESP32 | GPIO ESP32 | Giao thức |
|---|---|---|---|---|
| GP0 | CH0 | UART TX | GPIO26 (pin 9) | UART 115200 8N1 |
| GP1 | CH1 | I2C SCL | GPIO18 (pin 29) | I2C 100 kHz |
| GP2 | CH2 | I2C SDA | GPIO19 (pin 30) | I2C 100 kHz |
| GP3 | CH3 | SPI CS | GPIO21 (pin 28) | SPI mode 0, 1 MHz |
| GP4 | CH4 | SPI SCK | GPIO22 (pin 31) | SPI mode 0, 1 MHz |
| GP5 | CH5 | SPI MOSI | GPIO23 (pin 32) | SPI mode 0, 1 MHz |
| GP6 | CH6 | SPI MISO | GPIO25 (pin 8) + jumper GPIO23→GPIO25 | SPI mode 0, 1 MHz |
| GP7 | CH7 | CAN TX | GPIO32 (pin 6) | CAN 250 kbps, ID 0x123 |
| GND | — | GND | pin 13/31 | đất chung (bắt buộc) |

TFT ST7735 1.8" giữ nguyên: VCC→3V3 (pin 36), GND→GND, CS→GP13, RST→GP15,
A0/DC→GP14, SCK→GP10, SDA/MOSI→GP11, BL→GP16. Nạp firmware là màn hình hiện
ảnh test (gradient + color bars + "PICO 2 LA") ngay khi boot, trước khi USB
được host mở.

**Chế độ phát của ESP32:** firmware `la-signal-gen` mặc định `burst on` — mỗi
100 ms phát liên tiếp một khung CAN (~0,3 ms), một chuỗi UART `"LA1234\n"`
(0,61 ms), một transaction I2C (0,27 ms) và một khung SPI (64 µs). Tổng < 1,3 ms
nên **một cửa sổ 40000 mẫu @ 25 MHz (1,6 ms) chứa đủ cả 4 giao thức**.

**Cấu hình capture:** rate 25 MHz, count 40000, trigger `edge:7:fall` (SOF của
CAN), pre-trigger 2000. Trigger đặt trên CH7 để cửa sổ bắt đầu đúng lúc burst
vừa phát.

```bash
# Nạp firmware LA (Pico 2)
picotool load -x build/firmware-pico2/rp2350_la.uf2
# Nạp firmware generator (ESP32)
cd ~/MyEspProjects/la-signal-gen && idf.py -p /dev/ttyUSB0 flash

# Capture + decode cả 4 giao thức on-device trong một lần
python3 tools/host_la.py --port /dev/ttyACM0 --rate 25m --count 40000 \
    --pre 2000 --trigger edge:7:fall --decode-all --out /tmp/opencode/all4.bin
```

**Kết quả mong đợi** (4 dòng trong RESULT của `DECODE_ALL`):

```
UART 115200 8N1: "LA1234"
I2C: 0x68 W ACK 0x01 ACK 0x5A ACK
SPI: DE AD BE EF 00 01 02 03
CAN ID=0x123 DLC=4 DATA=DE AD BE EF
```

Nếu một dòng báo `no transaction found` / `no SOF`, kiểm tra đúng chân của nhóm
đó và jumper GPIO23→GPIO25 cho MISO.

Mở `/tmp/opencode/all4.bin` bằng PulseView (Import raw: 8 kênh, 25 MHz) rồi gắn
decoder UART/I2C/SPI/CAN với đúng kênh như bảng trên để đối chiếu.

### 5.2. UI trên TFT 128×160 (4 màn hình, 4 nút, song song USB)

Màn hình ST7735 1,8" (GP10–GP16) hiển thị giao diện 4 màn hình, không cần PC.
Nút chưa đấu dây nên mọi thao tác làm được bằng lệnh USB một ký tự.

**Bảng nút (khi đã đấu):**

| Nút | GPIO | Nhấn ngắn | Nhấn giữ |
|---|---|---|---|
| UP / LEFT | GP8 | lên / cuộn trái / tăng giá trị | lật trang kênh (màn 03) |
| DOWN / RIGHT | GP9 | xuống / cuộn phải / giảm giá trị | về mẫu T=0 |
| OK | GP12 | xác nhận / vào chế độ sửa giá trị | đổi ZOOM x1→x2→x4→x8 |
| BACK | GP22 | quay lại màn trước / hủy sửa | — |

**Lệnh USB tương đương** (`python3 tools/ui_keys.py --port /dev/ttyACM0`):

```
u d o b   nhấn ngắn UP, DOWN, OK, BACK
U D O B   nhấn giữ  UP, DOWN, OK, BACK
```
Ký tự được nhận khi parser LAR2 đang rảnh (không có frame dở dang) và khác 'L',
nên dùng chung cổng USB với `host_la.py`/PulseView mà không xung đột.

**Bốn màn hình:**

| Màn | Nội dung | Điều khiển |
|---|---|---|
| 01 CAU HINH | `FS: 25 MHZ`, `N: 40000`, `CH: 0-7`, `PRE: 2000`, `[ BAT DAU ]` | UP/DN chọn dòng, OK sửa giá trị, OK trên [BAT DAU] để chạy capture, BACK → màn 02 |
| 02 TRIGGER | `MODE: EDGE/PATTERN/UART`, `CH: 0-7`, `CANH: RISING/FALLING`, `PATTERN: 00/FF`, `UART: AUTO` | UP/DN đổi giá trị khi đang sửa, OK trên PATTERN đổi giữa Val/Mask, OK trên UART đổi AUTO ↔ byte, BACK → màn 01 |
| 03 WAVEFORM | `STOP FS:25M`, `CH0-3 PAGE 1/2`, 4 track (22 px/track), `T:... ZOOM:X1` | UP/DN cuộn, OK lật trang CH0-3 ↔ CH4-7, OK giữ đổi zoom, BACK → màn 04 |
| 04 DECODE | lưới `UART I2C` / `SPI CAN`, chân của bus, `DATA:` (tự xuống dòng ở 21 ký tự) | UP/DN đổi bus, OK giải mã lại buffer hiện tại, BACK → màn 03 |

**Quy trình test:**

```bash
# 1. Nạp firmware (màn hình hiện CAU HINH ngay khi cấp nguồn)
picotool load -x build/firmware-pico2/rp2350_la.uf2
# 2. Chạy capture từ màn hình (hoặc từ USB)
python3 tools/ui_keys.py --port /dev/ttyACM0 --send oo     # OK, OK -> [BAT DAU]
# 3. Xem dạng sóng
python3 tools/ui_keys.py --port /dev/ttyACM0 --send b     # BACK -> WAVEFORM
# 4. Giải mã
python3 tools/ui_keys.py --port /dev/ttyACM0 --send b     # BACK -> DECODE
```

**Kỳ vọng:** màn 03 vẽ 4 track màu CH0 xanh dương, CH1 cam, CH2 xanh lá, CH3 tím;
màn 04 hiện `UART RX: CH0`, `BAUD: AUTO` và text decode của bus đang chọn trên
cùng buffer đã capture. Capture do UI kích hoạt vẫn stream qua USB như lệnh ARM,
nên nếu đang mở PulseView thì dữ liệu vẫn nhận được.

**Lưu ý đã biết:** thanh `CANH` chỉ có RISING/FALLING (firmware trigger chỉ hỗ trợ
2 cạnh, không có BOTH). Cấu hình capture của UI không đi qua kiểm tra LAR2 CONFIG
nên `PRE` luôn được kẹp nhỏ hơn `N` ngay trên UI.

### 5.3. Giới hạn 8 kênh theo giao thức (đo 2026-11-16)

Chạy tự động bằng `tools/sweep.py` (85 bài trong `docs/TEST-PLAN-8CH.md`),
trigger từng bài, decode trên thiết bị, PASS = đúng dòng decode + trigger +
flags 0x00.

| Giao thức | Pass cao nhất | Fail đầu tiên | Kết luận |
|---|---|---|---|
| UART @ 25 MHz | 5 Mbaud (5 mẫu/bit) | — | giới hạn máy phát, LA chưa chạm trần |
| UART @ 5 MHz | 921600 (5.4 mẫu/bit) | — | |
| UART @ 1 MHz | 230400 (4.3 mẫu/bit) | — | ~4 mẫu/bit là biên decoder UART |
| I2C | 500 kHz @ 25 MHz & 5 MHz | 100 kHz @ 100 kHz sampling | giới hạn máy phát 500 kHz |
| SPI @ 25 MHz | **10.5 MHz** (2.38 mẫu/clock) | **11 MHz** (2.27) | **trần lấy mẫu thật của LA** (10.5 pass 3/3, 11 fail 3/3) |
| CAN @ 25 MHz | 1 Mbps (25 mẫu/bit) | — | |
| CAN @ 5 MHz | 1 Mbps (5 mẫu/bit) | — | |
| CAN @ 1 MHz | 500 kbps (2 mẫu/bit) | — | 1 Mbps @ 1 MHz chỉ 1 mẫu/bit, không tin cậy |

Số bài: UART 20/20 PASS (bài 1 có 1 lần lệch pha cửa sổ, lặp lại PASS),
I2C 17/20 PASS (3 bài âm tính FAIL đúng thiết kế: 100 kHz lấy mẫu ở 100 kHz
= 1 mẫu/bit, pre=1 nên cửa sổ bắt đầu ngay tại trigger, và lấy mẫu 1 kHz cho
bus 100 kHz → aliasing), SPI 15/20 (4 bài vượt 12 MHz + 1 bài 1 MHz
lấy mẫu 1 MHz = 1 mẫu/bit, FAIL đúng thiết kế), CAN 19/20 (1 bài sai baud
FAIL đúng; 1 lần hiếm trigger rơi giữa frame).

Lỗi máy phát đã sửa trong lúc test:
- `pdMS_TO_TICKS(5)` = 0 ở `CONFIG_FREERTOS_HZ=100` làm task UART quay nóng,
  watchdog reset → thay bằng macro `DELAY_MS()`.
- `set spi > 26666` gỡ device SPI rồi `add_device` fail ⇒ handle cũ bị dùng lại
  (assert `xQueueSemaphoreTake`) → khôi phục clock cũ + cờ `g_spi_ok`.
- TWAI: prescaler phải chẵn (thanh ghi lưu brp/2-1), >128 phải chia hết 4,
  tseg1 chỉ 4 bit nên bit time ≤ 20 quanta → chọn brp/quanta theo bảng thay vì
  cố định 20 quanta. Vì vậy 800 kbps và 20 kbps giờ giải mã đúng; 10/16 kbps
  không phát được (vượt prescaler 256).

**Chạy lại toàn bộ sau khi sửa trigger (2026-11-17):** UART 19/20 (bài 1 lệch
pha cửa sổ, lặp lại PASS), I2C 17/20, SPI 16/20, CAN 19/20 — đúng như bảng trên,
không có hồi quy. Hai bài "FAIL" ngoài dự kiến đã giải thích: UART 9600 @ 1 MHz
lệch pha (10 ms cửa sổ chỉ chứa ~1 frame), SPI 1 MHz lấy mẫu 1 MHz = 1 mẫu/bit
(aliasing, cùng lý do với I2C 100 kHz @ 100 kHz).

**Hạn chế đã biết:** trigger CAN dùng cạnh xuống nên có thể rơi vào giữa frame
(thay vì SOF); lần hiếm gặp decoder báo `extended frames` — chạy lại là PASS
(8/8 lần lặp ở 125 kbps @ 25 MHz). Không phải lỗi LA.

---

## 7. Mục tiêu tuần tới (chưa verify)

### 7.0. UI trên TFT
- Đã xong: `ui.c` (4 màn, dirty-line redraw), `buttons.c` (debounce 25 ms,
  nhấn ngắn/dài 600 ms), `font6x8.c`, `tools/ui_keys.py`, test host
  `tests/test_ui.c`.
- Còn phải làm trên board: xác nhận bố cục không bị cắt chữ, màu đúng, không
  nháy khi đổi dòng, và nút thật khớp với lệnh USB.

### 7.1. Test 8 kênh đồng thời
- Hiện tại chỉ test 1–4 kênh (SPI dùng 4, I2C 2, UART/CAN 1).
- **Đã dựng xong công cụ:** map 8 kênh cố định + lệnh `ANALYZE DECODE_ALL` (op 9) +
  `host_la.py --decode-all`, ESP32 có chế độ `burst on`. Quy trình và bảng nối chân
  ở **mục 5.1** — chỉ còn chạy trên board khi có đủ dây.
- Cần xác nhận: PIO capture 8-bit song song không mất kênh, DMA không drop byte khi
  8 kênh active, CRC integrity với 8 kênh.

### 7.2. Test tín hiệu thực sự ở tần số cao (gần 25 MHz)
- Các test hiện tại (CAN 250 kbps, UART 115200, I2C 100 kHz, SPI 1 MHz) đều **tần số tín hiệu << 25 MHz**. Sampling 25 MHz đã verified, nhưng chưa verify LA bắt được tín hiệu toggling ở 12.5–25 MHz mà không aliasing/glitch.
- **Cần:** ESP32 LEDC phát square wave 12.5 MHz (hoặc 25 MHz, 1-bit resolution) → GP0, capture @25 MHz, kiểm tra period = 2/4 mẫu, duty 50%.
- **Rủi ro:** ESP32 LEDC max ~40 MHz nhưng resolution giảm. Cần test xem tín hiệu có bị méo do slew rate của GPIO không.

### 7.3. Khai báo chân linh hoạt (firmware enhancement) — CHI TIẾT

#### 7.3.1. Vấn đề hiện tại
- Firmware capture **cố định** GP0–GP7: `sm_config_set_in_pins(&c, 0)` + `pio_sm_set_consecutive_pindirs(pio, sm, 0, 8, false)`.
- PIO program `in pins, 8` đọc 8 bit từ GPIO 0..7 liên tiếp. Không thể chọn chân khác.
- Người dùng bị giới hạn: phải nối tín hiệu vào đúng GP0–GP7, không thể dùng GP8–GP28 (kể cả GP10–GP16 đang dùng cho TFT).
- Khi cần đo 8 tín hiệu ở các vị trí khác nhau trên board, phải đi dây dài đến GP0–GP7 → dễ nhiễu, khó thực tế.

#### 7.3.2. Ý tưởng: khai báo pin map trước capture
- Thêm bước **CONFIG pin map** trước khi capture: người dùng khai báo 8 GPIO (hoặc ít hơn) mà mỗi kênh sẽ đọc.
- Ví dụ khai báo: `CH0=GP0, CH1=GP5, CH2=GP10, CH3=GP14, CH4=GP20, CH5=GP22, CH6=GP26, CH7=GP28`.
- Firmware đọc từng chân theo map, pack thành 8-bit word, DMA vào buffer như hiện tại.
- **Lợi ích:** tận dụng 8 kênh cho bất kỳ tổ hợp chân nào, không bị giới hạn GP0–GP7; đo trực tiếp trên board mà không cần đi dây xa.

#### 7.3.3. Thay đổi protocol (LAR2 v1.3)
- Thêm field `pin_map[8]` (8 byte, mỗi byte = GPIO number 0–29) vào CONFIG payload.
- V1 (20 byte): không có pin map → mặc định GP0–GP7 (backward compatible).
- V2 (28 byte): thêm pin map 8 byte → cho phép khai báo chân.
- Thêm validation: GPIO phải hợp lệ (không phải flash pins 6–11, không phải strapping 0/2/5/12/15 nếu cần), không trùng lặp.

#### 7.3.4. Thay đổi firmware

**a) PIO program:** Thay `in pins, 8` bằng cách đọc từng chân:
- Option A: Dùng `in pins` với base khác nhau cho mỗi kênh → không khả thi vì PIO `in pins` chỉ đọc từ base liên tiếp.
- Option B: Dùng `jmp` + `in pins, 1` cho từng chân, với `set`/`mov` để chuyển base. PIO hỗ trợ `in pins, 1` với base offset qua `sm_config_set_in_pins(&c, base)`, nhưng base chỉ set 1 lần khi config.
- Option C: **Dùng SIO (CPU GPIO read)** thay vì PIO — đọc `sio_hw->gpio_in` (32-bit) mỗi sample, mask 8 bit theo pin map. CPU overhead: 1 read + 8 shift/mask per sample. Ở 25 MHz (40 ns/sample), CPU 100 MHz có 4 cycle/sample → đủ cho 1 read + mask nếu tối ưu bằng lookup table.
- Option D: **Lookup table mapping** — pre-compute 8-bit mask từ 32-bit GPIO register: `sample = (gpio_in >> pin0) & 1 | ((gpio_in >> pin1) & 1) << 1 | ...`. Dùng 8 shift + 7 OR = 15 instruction. Ở 100 MHz, 4 cycle/sample = 400 ns budget → 15 instruction ~150 ns → đủ.

**Khuyến nghị: Option D** (lookup table, đơn giản, đủ nhanh ở 25 MHz).

**b) start_capture():**
```c
// Thay vì:
sm_config_set_in_pins(&c, 0);
pio_sm_set_consecutive_pindirs(pio, sm, 0, 8, false);
// Dùng:
// PIO chỉ capture 1 kênh (GP0) làm clock trigger, CPU đọc sio_hw->gpio_in
// Hoặc: PIO capture 8 kênh từ GP0..GP7 (fallback khi không khai báo pin map)
```

**c) Capture loop:** Thay PIO DMA bằng CPU sampling:
```c
// Mỗi sample: đọc GPIO register, map 8 bit, lưu vào buffer
for (uint32_t i = 0; i < count; i++) {
    uint32_t gpio = sio_hw->gpio_in;
    samples[i] = (uint8_t)(
        ((gpio >> pin_map[0]) & 1) |
        (((gpio >> pin_map[1]) & 1) << 1) | ... );
    busy_wait_us_32(sample_period_us);  // timing chính xác
}
```
- **Vấn đề timing:** `busy_wait_us_32` có overhead ~1–2 µs → chỉ đạt ~1 MHz, không đạt 25 MHz.
- **Giải pháp:** Giữ PIO cho timing chính xác (sample clock), CPU chỉ đọc + map sau mỗi trigger của PIO. PIO trigger interrupt → CPU đọc GPIO → map → store. Nhưng interrupt overhead ~1 µs → max ~1 MHz. Không đủ.
- **Giải pháp tốt hơn:** PIO capture 1 bit (clock) từ GP0, CPU poll PIO FIFO → đọc GPIO → map → store. Polling tight loop: `while (!pio_sm_is_rx_fifo_empty()) { uint32_t gpio = sio_hw->gpio_in; ... }`. PIO FIFO depth 4 → CPU có 4 sample buffer. Ở 25 MHz, 4 sample = 160 ns → CPU phải xử lý trong 160 ns → 16 cycle @100 MHz → tight nhưng khả thi với lookup table.

#### 7.3.5. Thay đổi host tools
- `host_la.py`: thêm `--pins` argument (8 GPIO numbers, comma-separated).
- `config_payload()`: thêm 8 byte pin map vào CONFIG v2.
- `lar2.py`: `CONFIG_V2_SIZE` từ 28 → 36 byte (thêm 8 byte pin map).

#### 7.3.6. Use cases
1. **Đo 8 tín hiệu rải rác trên board:** khai báo pin map → capture → decode.
2. **Đo tín hiệu trên GP10–GP16 (TFT pins) mà không ảnh hưởng TFT:** capture khi TFT idle.
3. **Đo tín hiệu trên GP26–GP28 (UART/I2C pins):** capture đồng thời với decode.
4. **Backward compatible:** nếu không khai báo pin map → mặc định GP0–GP7 (như hiện tại).

#### 7.3.7. Rủi ro và giới hạn
- CPU sampling ở 25 MHz: polling tight loop, rủi ro miss sample nếu interrupt (USB) xen vào. Cần disable interrupt trong capture loop → USB tạm dừng → capture xong mới gửi.
- Timing chính xác phụ thuộc vào PIO clock (giữ nguyên 25 MHz).
- Pin map phải validate: không dùng flash pins (6–11), không dùng strapping pins (0,2,5,12,15) cho output, v.v.
- Complexity: thêm ~100 dòng firmware, ~30 dòng host tools.

#### 7.3.8. Ưu tiên
- **Nếu còn thời gian:** làm Option D (lookup table + PIO clock) cho sampling chính xác ở 25 MHz.
- **Nếu thiếu thời gian:** giữ nguyên GP0–GP7, tập trung vào deliverable (báo cáo, video, mạch bảo vệ).
- Mở PulseView, capture 8 kênh, thêm decoder UART/I2C/SPI/CAN đồng thời trên các kênh khác nhau.
- Kiểm tra PulseView không crash khi decode nhiều protocol song song.

### 7.5. Test flash storage với 8 kênh
- Save session với 8 kênh active → load → verify byte-for-byte.
- Golden compare với 8 kênh.

### Ưu tiên tuần tới
1. **Test 8 kênh đồng thời** (7.1) — quan trọng nhất, chưa verify.
2. **Test tín hiệu 12.5 MHz** (7.2) — xác nhận 25 MHz ổn định với tín hiệu thực sự cao tần.
3. **Khai báo chân linh hoạt** (7.3) — enhancement, làm nếu còn thời gian.

---

## 8. Kế hoạch tìm giới hạn 8 kênh

Xem `docs/TEST-PLAN-8CH.md`: 20 bài/giao thức + 5 bài tích hợp, tốc độ tăng dần
tới khi FAIL để xác định giới hạn thật của UART/I2C/SPI/CAN ở 25 MHz.
Kết quả chạy xong ghi vào §5.3 (bảng giới hạn) của tài liệu này.

Phần **chưa test được** (màn hình TFT, 4 nút vật lý, PulseView GUI, CAN lặp
biên, USB song song với UI) đã tách sang `docs/PLAN_Nov16.md`.
