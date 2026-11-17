# Sơ đồ nối chân — Logic Analyzer RP2350 + TFT ILI9488 + ESP32 DevKitC

## Mục đích
Kiểm chứng toàn bộ hệ thống Logic Analyzer: LA bắt tín hiệu từ ESP32, hiển thị trên TFT 3,5 inch, đồng thời truyền dữ liệu lên PC qua USB.

---

## 1. Nguồn điện

### Nguồn cấp cho các module

| Module | Nguồn | Cách cấp | Ghi chú |
|---|---|---|---|
| Pico 2 RP2350 | 5V USB (cổng micro-USB trên board) | Cắm cáp USB vào máy tính | Có thể cấp qua chân VSYS (pin 39) nếu dùng nguồn ngoài |
| ESP32 DevKitC | 5V USB (cổng micro-USB trên board) | Cắm cáp USB vào máy tính | Có thể cấp qua chân 5V (pin VIN) |
| TFT ILI9488 3.5" | 3,3V – 5V (tùy module) | Từ chân 3V3 của Pico 2 (pin 36) | Nếu module có regulator riêng thì cấp 5V từ VSYS |

### Sơ đồ nguồn

```
Máy tính USB ──┬──► Pico 2 (micro-USB) ──► 3V3 (pin 36) ──► TFT (VCC)
               │
               └──► ESP32 (micro-USB) ──► 3,3V (pin 3V3) ──► (không cần thêm)
```

### Lưu ý nguồn
- **Chung GND** giữa Pico 2, ESP32 và TFT — bắt buộc.
- Nếu TFT module cần 5V, cấp từ chân VSYS của Pico 2 (pin 39) thay vì 3V3.
- Dòng điện: Pico 2 (~100 mA) + TFT (~150 mA) + ESP32 (~200 mA) = tổng ~450 mA. USB port máy tính cung cấp đủ (500 mA), nhưng nếu dùng hub USB thì cần hub có nguồn ngoài.
- Không cấp quá 5,5 V vào chân VSYS của Pico 2.

---

## 2. Pico 2 RP2350 — Logic Analyzer + TFT

### Pinout Pico 2 (RP2350A, 30 GPIO)

```
                    ┌──────────────┐
                    │   Pico 2     │
                    │  (RP2350A)   │
               GP0  │ 1         40 │ VBUS
               GP1  │ 2         39 │ VSYS
               GND  │ 3         38 │ GND
               GP2  │ 4         37 │ 3V3_EN
               GP3  │ 5         36 │ 3V3_OUT
               GP4  │ 6         35 │ ADC_VREF
               GP5  │ 7         34 │ GP28
               GND  │ 8         33 │ GND
               GP6  │ 9         32 │ GP27
               GP7  │ 10        31 │ GP26
               GP8  │ 11        30 │ RUN
               GP9  │ 12        29 │ GP22
               GND  │ 13        28 │ GND
               GP10 │ 14        27 │ GP21
               GP11 │ 15        26 │ GP20
               GP12 │ 16        25 │ GP19
               GP13 │ 17        24 │ GP18
               GND  │ 18        23 │ GND
               GP14 │ 19        22 │ GP17
               GP15 │ 20        21 │ GP16
                    └──────────────┘
```

### Bảng nối chân Pico 2

| Chức năng | GPIO Pico 2 | Pin vật lý | Mô tả |
|---|---|---|---|
| **Capture CH0** | GP0 | 1 | Kênh đo 0 |
| **Capture CH1** | GP1 | 2 | Kênh đo 1 |
| **Capture CH2** | GP2 | 4 | Kênh đo 2 |
| **Capture CH3** | GP3 | 5 | Kênh đo 3 |
| **Capture CH4** | GP4 | 6 | Kênh đo 4 (stretch 8 kênh) |
| **Capture CH5** | GP5 | 7 | Kênh đo 5 |
| **Capture CH6** | GP6 | 9 | Kênh đo 6 |
| **Capture CH7** | GP7 | 10 | Kênh đo 7 |
| **GND** | GND | 3, 8, 13, 18, 23, 28, 33, 38 | Nối chung đất |
| **TFT SCK** | GP10 | 14 | SPI1 SCK @ 40 MHz |
| **TFT MOSI** | GP11 | 15 | SPI1 MOSI |
| **TFT CS** | GP13 | 17 | SPI1 CS (active low) |
| **TFT A0** | GP14 | 19 | Data/Command select |
| **TFT RST** | GP15 | 20 | Reset (active low) |
| **TFT LED** | GP16 | 21 | Backlight (active high) |
| **VCC (3,3V)** | 3V3_OUT | 36 | Cấp nguồn TFT |
| **VSYS (5V)** | VSYS | 39 | Cấp nguồn TFT nếu cần 5V |
| **USB** | micro-USB | — | Cấp nguồn + dữ liệu lên PC |

---

## 3. TFT ST7735 1.8 inch (128×160)

### Bảng nối chân TFT → Pico 2

| Chân TFT | Chân Pico 2 | GPIO | Mô tả |
|---|---|---|---|
| VCC | 3V3 (pin 36) hoặc VSYS (pin 39) | — | Nguồn TFT (3,3V hoặc 5V tùy module) |
| GND | GND (pin 3, 8, ...) | — | Đất chung |
| CS | GP13 (pin 17) | GP13 | Chip Select, active low |
| RESET | GP15 (pin 20) | GP15 | Reset, active low |
| AO | GP14 (pin 19) | GP14 | Data/Command: 0=command, 1=data |
| SCK | GP10 (pin 14) | GP10 | SPI clock |
| SDI (MOSI) | GP11 (pin 15) | GP11 | SPI data in |
| SDO (MISO) | — | — | Không dùng (chỉ viết) |
| LED (BL) | GP16 (pin 21) | GP16 | Backlight, active high |

### Lưu ý TFT
- **MADCTL = 0x00** (RGB, không xoay). Nếu ảnh bị xoay/mirror, thử 0x60/0xC0.
- **SPI 20 MHz**, RGB565 big-endian; không có MISO.
- **SPI 40 MHz** có thể quá nhanh cho một số module giá rẻ → nếu màn trắng, hạ xuống 20 MHz trong `tft_init()`.
- Nếu module TFT có regulator riêng (chấp nhận 5V), cấp từ VSYS (pin 39). Nếu chỉ chấp nhận 3,3V, cấp từ 3V3 (pin 36).
- Không nối MISO nếu module không dùng — tiết kiệm 1 chân.

### 3.1. Bốn nút bấm (chưa đấu dây — dùng lệnh USB thay thế)

| Nút | GPIO | Pin Pico 2 | Chức năng | Nối dây |
|---|---|---|---|---|
| UP / LEFT | GP8 | 11 | Lên / cuộn trái / tăng giá trị | nút nối GP8 ↔ GND |
| DOWN / RIGHT | GP9 | 12 | Xuống / cuộn phải / giảm giá trị | nút nối GP9 ↔ GND |
| OK | GP12 | 16 | Xác nhận / vào chế độ sửa | nút nối GP12 ↔ GND |
| BACK | GP22 | 29 | Quay lại / hủy sửa | nút nối GP22 ↔ GND |

Pull-up nội bật sẵn trong `buttons_init()`, không cần điện trở ngoài. Chân GND của
TFT nằm ở **pin 18**; GP12 là **pin 16** — hai pin cạnh nhau, đừng nối nhầm (nếu
GP12 bị kéo xuống GND thì nút OK không bao giờ nhấn được).

Khi chưa có nút, điều khiển bằng lệnh USB (`python3 tools/ui_keys.py`):
`u`/`d`/`o`/`b` = nhấn ngắn, `U`/`D`/`O`/`B` = nhấn giữ.

---

## 4. ESP32 DevKitC 38 pin — bộ phát tín hiệu kiểm thử

### Pinout ESP32 DevKitC (38 pin)

```
                    ┌──────────────┐
                    │ ESP32        │
                    │ DevKitC      │
               EN  │ 1         38 │ 5V
               VP  │ 2         37 │ CMD
               VN  │ 3         36 │ SD3
               34  │ 4         35 │ SD2
               35  │ 5         34 │ SD1
               32  │ 6         33 │ SD0
               33  │ 7         32 │ CLK
               25  │ 8         31 │ GND
               26  │ 9         30 │ 19
               27  │ 10        29 │ 18
               14  │ 11        28 │ 5
               12  │ 12        27 │ 17
               GND │ 13        26 │ 16
               13  │ 14        25 │ 4
               9   │ 15        24 │ 0
               10  │ 16        23 │ 2
               11  │ 17        22 │ 15
               VCC │ 18        21 │ 8
               3V3 │ 19        20 │ 7
                    └──────────────┘
```

### Bảng nối chân ESP32 DevKitC

| Chức năng | GPIO ESP32 | Pin vật lý | Mô tả |
|---|---|---|---|
| **SQ** | GPIO 4 | 25 | xung vuông 50 %, chu kỳ do LEDC sinh ra, 1 kHz mặc định |
| **SQ2** | GPIO 25 | 8 | cùng timer, duty 25 % → mẫu 2 kênh biết trước |
| **PAT0** | GPIO 13 | 14 | duty 12,5 % |
| **PAT1** | GPIO 14 | 11 | duty 25 % |
| **PAT2** | GPIO 16 | 26 | duty 37,5 % |
| **PAT3** | GPIO 17 | 27 | duty 50 % → mẫu 4 bit lặp lại mỗi chu kỳ |
| **I2C SCL** | GPIO 18 | 29 | bus I2C 100 kHz, dữ liệu biết trước |
| **I2C SDA** | GPIO 19 | 30 | |
| **SPI CS** | GPIO 21 | 28 | SPI mode 0, 1 MHz, khung 8 byte cố định |
| **SPI SCK** | GPIO 22 | 31 | |
| **SPI MOSI** | GPIO 23 | 32 | |
| **SPI MISO** | GPIO 25 | 8 | nối jumper GPIO23→GPIO25 để MISO thấy đúng bytes như MOSI |
| **UART TX** | GPIO 26 | 9 | 115200 8N1, gửi `"LA1234\n"` mỗi 100 ms |
| **UART RX** | GPIO 27 | 10 | không dùng |
| **CAN TX** | GPIO 32 | 6 | TWAI 250 kbps, ID 0x123, data DE AD BE EF (NO-ACK self-test, loopback trên cùng chân 32) |
| **CAN RX** | GPIO 33 | — | loopback (cùng chip, không cần transceiver) |
| **GND** | GND | 13, 31 | đất chung |
| **VCC (3,3V)** | 3V3 | 19 | không dùng (mức 3,3 V tự sinh) |

### Lưu ý ESP32
- **Tránh GPIO 0, 2, 5, 12, 15** (strapping pins) — không dùng cho tín hiệu.
- **Tránh GPIO 6-11** (flash) — không dùng.
- **GPIO 34-39** là input-only — không dùng cho output.
- Console là UART0 (USB serial qua CP2102/CH340), tín hiệu là UART1.
- Mức điện áp 3,3 V nên cắm thẳng vào GP của Pico 2.

---

## 5. Sơ đồ tổng quan hệ thống

```
┌─────────────────────────────────────────────────────────────────────────┐
│                          MÁY TÍNH                                     │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────────────────┐ │
│  │ PulseView    │  │ host_la.py   │  │ idf.py monitor (ESP32)      │ │
│  │ (xem sóng)   │  │ (test trigger│  │ (console ESP32)             │ │
│  └──────┬───────┘  └──────┬───────┘  └──────────────┬───────────────┘ │
│         │                 │                         │                 │
└─────────┼─────────────────┼─────────────────────────┼─────────────────┘
          │ USB             │ USB                     │ USB
          │                 │                         │
┌─────────┴─────────┐  ┌────┴────────────┐  ┌─────────┴──────────────┐
│ Pico 2 RP2350     │  │ Pico 2 RP2350   │  │ ESP32 DevKitC         │
│ (Logic Analyzer)  │  │ (TFT display)   │  │ (Signal generator)     │
│                   │  │                 │  │                        │
│ GP0-GP3 ────────────────────────────────── GPIO4,25,13,14,16,17 │
│                   │  │                 │  │ GPIO18,19              │
│ GP10-GP16 ────────► TFT ILI9488      │  │ GPIO21-23,25           │
│                   │  │                 │  │ GPIO26 (UART TX)       │
│ GND ───────────────────────────────────── GND                   │
└───────────────────┘  └─────────────────┘  └────────────────────────┘
```

### Lưu ý về sơ đồ tổng quan
- **Pico 2 là Logic Analyzer chính** — capturer tín hiệu từ ESP32 qua GP0-GP3.
- **TFT ILI9488** nối trực tiếp vào Pico 2 (GP10-GP16) — hiển thị waveform tại chỗ.
- **ESP32** phát tín hiệu kiểm thử → nối vào GP0-GP3 của Pico 2.
- **Chung GND** giữa tất cả module — bắt buộc.

---

## 6. Thứ tự test đề xuất

### Bước 1: Kiểm tra TFT còn sống
1. Nạp firmware Pico 2 (`rp2350_la.uf2`).
2. Nối TFT theo bảng nối chân mục 3.
3. Cấp nguồn → màn hình hiện hình test (gradient + color bars + text "PICO 2 LA").

### Bước 2: Kiểm tra capture cơ bản
1. Nối SQ (GPIO4 của ESP32) → GP0 của Pico 2, chung GND.
2. Nạp firmware ESP32 (`idf.py -p /dev/ttyUSB0 flash`).
3. Chạy `python3 tools/host_la.py --port /dev/ttyACM0 --rate 1m --count 2000 --out cap.bin`
4. Mở `cap.bin` bằng PulseView → thấy xung vuông, mỗi chu kỳ đúng 1000 mẫu ở 1 MHz.

### Bước 3: Kiểm tra trigger
1. `python3 tools/host_la.py --port /dev/ttyACM0 --rate 1m --count 10000 --pre 2000 --trigger edge:0:rise`
2. Kiểm tra `trigger index: 2000`; mẫu ở index 2000 phải CAO, mẫu 1999 phải THẤP.

### Bước 4: Kiểm tra I2C decode
1. Nối I2C SCL (GPIO18) → GP0, I2C SDA (GPIO19) → GP1.
2. Capture ở 1 MHz.
3. PulseView → Add I2C decoder → map SCL=CH0, SDA=CH1 → giải mã ra địa chỉ 0x68, thanh ghi 0x01, dữ liệu 0x5A.

### Bước 5: Kiểm tra SPI decode
1. Nối SPI CS (GPIO21) → GP0, SCK (GPIO22) → GP1, MOSI (GPIO23) → GP2, MISO (GPIO25) → GP3.
2. Nối jumper GPIO23→GPIO25 để MISO thấy đúng bytes MOSI.
3. Capture ở 1 MHz → PulseView giải mã đúng 8 byte `DE AD BE EF 00 01 02 03`.

### Bước 6: Kiểm tra CAN decode
1. Nối CAN TX (GPIO32) → GP0.
2. Capture ở 1 MHz → PulseView giải mã CAN frame với ID 0x123.

### Bước 7: Kiểm tra UART decode
1. Nối UART TX (GPIO26) → GP0.
2. Capture ở 1 MHz → PulseView giải mã đúng chuỗi `"LA1234"`.

---

## 7. Test tổng hợp: 4 giao thức trên 8 kênh + TFT

Đây là bài test gộp: một lần capture 25 MHz lấy đồng thời UART, I2C, SPI, CAN
trong khi màn hình TFT vẫn cắm và hiển thị.

| Pico 2 | Kênh | Nối tới ESP32 | GPIO ESP32 | Ghi chú |
|---|---|---|---|---|
| GP0 | CH0 | UART TX | GPIO26 (pin 9) | 115200 8N1, `"LA1234\n"` |
| GP1 | CH1 | I2C SCL | GPIO18 (pin 29) | 100 kHz |
| GP2 | CH2 | I2C SDA | GPIO19 (pin 30) | 100 kHz |
| GP3 | CH3 | SPI CS | GPIO21 (pin 28) | mode 0, 1 MHz |
| GP4 | CH4 | SPI SCK | GPIO22 (pin 31) | |
| GP5 | CH5 | SPI MOSI | GPIO23 (pin 32) | |
| GP6 | CH6 | SPI MISO | GPIO25 (pin 8) | cần jumper GPIO23→GPIO25 |
| GP7 | CH7 | CAN TX | GPIO32 (pin 6) | 250 kbps, ID 0x123 |
| GND | — | GND | pin 13 hoặc 31 | bắt buộc chung đất |

TFT giữ nguyên như mục 3 (GP10 SCK, GP11 MOSI, GP13 CS, GP14 DC, GP15 RST,
GP16 BL) — không trùng chân capture nào.

Trên ESP32, firmware `la-signal-gen` mặc định `burst on`: mỗi 100 ms phát liên
tiếp CAN → UART → I2C → SPI, tổng < 1,3 ms, nên một cửa sổ 40000 mẫu @ 25 MHz
(1,6 ms) chứa đủ bốn giao thức. Gõ `burst off` trên console ESP32 để quay lại
chế độ mỗi giao thức chạy riêng mỗi 100 ms.

```bash
python3 tools/host_la.py --port /dev/ttyACM0 --rate 25m --count 40000 \
    --pre 2000 --trigger edge:7:fall --decode-all --out /tmp/opencode/all4.bin
```

Kỳ vọng 4 dòng: `UART 115200 8N1: "LA1234"`, `I2C: 0x68 W ACK 0x01 ACK 0x5A ACK`,
`SPI: DE AD BE EF 00 01 02 03`, `CAN ID=0x123 DLC=4 DATA=DE AD BE EF`.
Chi tiết và cách xử lý khi một dòng không ra: `docs/TEST-Nov3.md` mục 5.1.

---

## Lưu ý an toàn

- **Không cấp quá 5,5 V** vào chân VSYS của Pico 2.
- **Không cấp 5 V trực tiếp** vào chân GPIO của Pico 2 (chỉ chịu 3,3 V).
- **Chung GND** giữa tất cả module — bắt buộc.
- **Không mở 2 chương trình** cùng lúc trên cổng `/dev/ttyACM0` (Pico 2) hoặc `/dev/ttyUSB0` (ESP32).
- Nếu TFT module cần 5 V, cấp từ VSYS (pin 39) thay vì 3V3 (pin 36).
EOF
echo "WIRING.md updated"; wc -l /home/namkhanh/MyEspProjects/la-signal-gen/WIRING.md