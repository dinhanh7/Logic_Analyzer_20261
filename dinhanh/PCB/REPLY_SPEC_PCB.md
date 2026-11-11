# Phản hồi & chốt đặc tả PCB

**Người phản hồi:** Nam Khánh  
**Người nhận:** Đình Anh   
**Ngày:** 09/11/2026  
**Đối tượng:** `SPEC_PCB.md'.

**Phạm vi:** chốt các mục §8, sơ đồ chân, khả năng mở rộng kênh, góp ý sửa SPEC.  
**Không thuộc phạm vi:** chọn mã điện trở / Zener / buffer, tính công suất, mô phỏng bảo vệ - Đình Anh chốt.

---

## 0. Tóm tắt

1. **8 kênh, 25 MS/s** - đủ yêu cầu Topic 1, hợp BOM, firmware đã verify.
2. **Chỉ dùng TFT ST7735 1,8"** — không có OLED. TFT đã test, firmware có driver sẵn.
3. **Sửa xung đột chân:** dời nút / LED / CAL_OUT khỏi GP10–GP16 (TFT) sang GP8, GP9, GP22, GP26, GP27; nút OK giữ nguyên GP12.
4. **Mở rộng 16 kênh:** dùng GP8–GP15, nhưng phải bỏ TFT khỏi GP10–GP15 và đổi firmware sang 16-bit → không làm bản này.
5. **Đình Anh apply Layout A** vào schematic, bỏ khối OLED, thêm footprint TFT.

---

## 1. Chốt các quyết định §8

| STT | Nội dung | Quyết định | Lý do | Người chốt |
|---:|---|---|---|---|
| 1 | Topic, số kênh, tốc độ | **Topic 1; 8 kênh; 25 MS/s.** Không tăng kênh bản này. | Đề bài tối thiểu 8 kênh / 25 MHz. Firmware đã verify 25 MHz với CAN/UART/I2C/SPI. Tăng lên 16 kênh làm BOM bảo vệ gần gấp đôi và phải đổi toàn bộ firmware sang mẫu 16-bit (xem §3). | **Nam Khánh**  |
| 2 | Phạm vi bản đầu | Đế Pico + bảo vệ 8 kênh + **TFT ST7735 1,8"** + 4 nút + RESET + LED + test point. **Không OLED.** | TFT đã test, driver có sẵn, không phải viết driver mới. Topic 1 chỉ bắt buộc hiển thị trên **PC hoặc màn hình HD**; PC đã đáp ứng, nên màn hình rời chỉ là tiện ích. OLED 128×64 còn thấp hơn TFT 128×160. | **Nam Khánh** (quyết định linh kiện lớn liên quan firmware) |
| 3 | Board Pico | **Pico 2 (RP2350A)**, build `PICO_BOARD=pico2`. | Board thực tế đang dùng là Pico 2; build cache đang để `pico2`. Socket 2×20 giống Pico 2 W nên footprint không đổi nếu sau này đổi board. | **Nam Khánh** |
| 4 | Điện trở đầu vào | **2× 1 kΩ PR02 2 W** nối tiếp. | Đúng thiết kế đã mô phỏng: mỗi con chịu 9,62 W peak / 9,57 mJ, tổng 19,2 W / 19,2 mJ. Nếu dùng 1× 2 kΩ thì một con phải chịu toàn bộ 19,2 mJ, rất dễ chọn nhầm 0603 → cháy khi có xung 200 V. | **Đình Anh** (mạch) — Nam Khánh xác nhận không ảnh hưởng firmware |
| 5 | Màn hình | **TFT ST7735 1,8" 128×160**, SPI1: GP10 SCK, GP11 MOSI, GP13 CS, GP14 DC, GP15 RST, GP16 BL. **Không OLED.** | Đã test trên board thật; firmware `tft_st7735.c` đang chạy. Giữ nguyên để không phát sinh việc viết driver mới và không rủi ro hỏng phần hiển thị đã ổn định. | **Nam Khánh** |
| 6 | Đầu nối / dây probe | **2×8, bước 2,54 mm**: chân lẻ 1/3/…/15 = CH0–CH7; chân chẵn = GND. | Mỗi kênh có GND bên cạnh → giảm nhiễu và dễ kẹp probe. BOM KiCad đang dùng 1×10 (chỉ 2 GND) → phải sửa thành 2×8. | Nhóm / **Đình Anh** |
| 7 | Phân chân GPIO | **Layout A** ở §2. | Tránh xung đột với TFT đang dùng GP10–GP16; giữ nguyên firmware đã test; chỉ đổi 5 chân. | **Nam Khánh** |
| 8 | Nguồn | **USB-only** cho bản đầu. | Đơn giản, không cần mạch sạc / chống cấp ngược. USB máy tính cấp 500 mA; Pico 3V3 tải ngoài dự kiến < 300 mA (8 buffer + TFT + LED). | Nhóm |
| 9 | CAL_OUT | **Có**, dùng GP27. | Cho phép tự phát xung 3,3 V để test từng kênh mà không cần nguồn ngoài. Nếu thiếu chân thì chỉ chừa footprint, không bắt buộc populate. | **Nam Khánh** |
| 10 | Cơ khí / ngân sách | Chốt sau; BOM mục tiêu **≤ 450.000 đ**. | Đề bài giới hạn tổng linh kiện mua mới. Các mục tốn nhất: 16× PR02 2 W và 8× SN74LVC1G17. | Nhóm + **Đình Anh** |
| 11 | Dải tần / sai số timing | Đảm bảo **1 kHz–1 MHz**; 3,3 V trên 3 MHz biên độ giảm; **25 MS/s là tốc độ lấy mẫu, không phải băng thông đầu vào**. | Mô phỏng bảo vệ: 3,3 V @5 MHz chỉ còn 2,42 V; @3 MHz còn 2,88 V. 5 V/12 V tốt hơn. Cần ghi đúng để không overclaim. | **Nam Khánh** + **Đình Anh** |

> Tóm tắt trách nhiệm: **Nam Khánh** chốt các mục liên quan firmware / pinout / linh kiện lớn (1, 2, 3, 5, 7, 9, 11). **Đình Anh** chốt các mục mạch / linh kiện / cơ khí (4, 6, 10). Mục 8 cả nhóm.

---

## 2. Sơ đồ chân — Layout A

### 2.1. Sơ đồ khối

```text
DUT CH0..CH7
     │
     ▼
[ Bảo vệ 8 kênh: 2×1k PR02 → BAV99/Zener → 330Ω → buffer → 33Ω ]
     │ OUT0..OUT7 → GP0..GP7
     ▼
[ Pico 2 (RP2350A) ]
     ├─ GP0..GP7    ◄── PIO capture (in pins, 8)
     ├─ GP10..GP16  ──► TFT ST7735 (SPI1)
     ├─ GP8/9/12/22 ◄── 4 nút UP/DOWN/OK/BACK
     ├─ GP26        ──► LED STATUS
     ├─ GP27        ──► CAL_OUT
     ├─ RUN         ◄── nút RESET
     └─ micro-USB   ◄──► PC / PulseView
```

 Chỉ một màn hình duy nhất là TFT ST7735.

### 2.2. Bảng chân đầy đủ

| Chức năng | GPIO | Pin Pico | Loại | Ghi chú |
|---|---:|---:|---|---|
| CH0 | GP0 | 1 | Input | Sau bảo vệ; PIO `in pins, 8`, base = 0 |
| CH1 | GP1 | 2 | Input | Sau bảo vệ |
| CH2 | GP2 | 4 | Input | Sau bảo vệ |
| CH3 | GP3 | 5 | Input | Sau bảo vệ |
| CH4 | GP4 | 6 | Input | Sau bảo vệ |
| CH5 | GP5 | 7 | Input | Sau bảo vệ |
| CH6 | GP6 | 9 | Input | Sau bảo vệ |
| CH7 | GP7 | 10 | Input | Sau bảo vệ |
| BTN UP | GP8 | 11 | Input | Pull-up nội bộ; nhấn nối GND; firmware debounce |
| BTN DOWN | GP9 | 12 | Input | Pull-up nội bộ; nhấn nối GND |
| TFT SCK | GP10 | 14 | Output | SPI1 SCK; **giữ nguyên firmware** |
| TFT MOSI | GP11 | 15 | Output | SPI1 TX |
| BTN OK | GP12 | 16 | Input | Pull-up nội bộ; nhấn nối GND |
| TFT CS | GP13 | 17 | Output | Active low |
| TFT DC | GP14 | 19 | Output | Data/Command select |
| TFT RST | GP15 | 20 | Output | Reset active low |
| TFT BL | GP16 | 21 | Output | Backlight, mức cao sáng |
| BTN BACK | GP22 | 29 | Input | Pull-up nội bộ; nhấn nối GND |
| LED STATUS | GP26 | 31 | Output | Qua 1 kΩ; mức cao sáng |
| CAL_OUT | GP27 | 32 | Output | Xung 3,3 V; chỉ ra header test, **không nối vào kênh đo** |
| Dự phòng | GP28 | 34 | — | ADC2, để trống |
| RESET | RUN | 30 | Input | Nút xuống GND; Pico có pull-up nội bộ |
| 3V3 | 3V3 OUT | 36 | Power | Cấp buffer + TFT + LED; không cấp ngược vào Pico |
| VSYS | VSYS | 39 | Power | Test point |
| GND | GND | 3, 8, 13, 18, 23, 28, 33, 38 | Power | Nối chung đầu đo, bảo vệ, Pico |

**Không dùng:** GP17–GP21 (trước đây là OLED — nay bỏ trống hoàn toàn), GP23–GP25 và GP29 (không ra header trên Pico 2).

### 2.3. Thay đổi so với spec cũ — vì sao phải sửa

Firmware hiện tại khởi tạo TFT ngay khi boot trên **GP10/11/13/14/15/16**. Spec cũ đặt nút/LED/CAL_OUT lên đúng các chân này → xung đột.

| Chức năng | Spec cũ | Layout A | Lý do chi tiết | Nếu giữ spec cũ |
|---|---|---|---|---|
| BTN UP | GP10 | **GP8** | GP10 là TFT SCK (SPI1). | Bấm nút kéo SCK xuống GND → mất clock, TFT sai/hỏng; nguy cơ chập output SPI. |
| BTN DOWN | GP11 | **GP9** | GP11 là TFT MOSI. | Bấm nút kéo MOSI xuống GND → sai dữ liệu TFT; chập output. |
| BTN OK | GP12 | GP12 | GP12 trống giữa MOSI và CS. | Giữ nguyên, không cần đổi. |
| BTN BACK | GP13 | **GP22** | GP13 là TFT CS. | Bấm nút kéo CS xuống GND → chọn TFT ngoài ý muốn; chập output. |
| LED STATUS | GP14 | **GP26** | GP14 là TFT DC. | LED nhấp nháy theo data/command thay vì trạng thái firmware. |
| CAL_OUT | GP16 | **GP27** | GP16 là TFT BL. | CAL_OUT bật/tắt đèn nền thay vì phát xung test. |
| GP15 | "dự phòng" | **TFT RST** | GP15 đang là reset TFT. | Nếu dùng GP15 cho nút/LED sẽ reset TFT liên tục. |
| OLED GP17–GP21 | Có | **Bỏ** | Chỉ dùng TFT, không lắp OLED. | Thêm linh kiện và driver không cần thiết. |

**Kết luận:** chỉ đổi **5 chân** (UP, DOWN, BACK, LED, CAL_OUT) và **bỏ khối OLED**. Khối TFT giữ nguyên hoàn toàn.

---

## 3. Khả năng mở rộng số kênh

### 3.1. Hiện trạng firmware

- PIO đọc **8 chân liền tiếp** bắt đầu từ base = 0: `in pins, 8`.
- Autopush 32 bit → **4 mẫu / word**, mỗi mẫu **1 byte**.
- Buffer 256 KB = **262.144 mẫu** = **10,49 ms @25 MHz**.
- Mỗi mẫu 1 byte, bit n = CHn. Decoder, flash, CRC, host tool đều giả định 8-bit.

### 3.2. Giới hạn của PIO — điều quan trọng nhất

PIO `in pins, N` chỉ đọc **N chân liền tiếp** từ `base`. Không thể khai báo chân rời rạc kiểu GP0, GP5, GP10… ở 25 MHz.

- Chọn được **cửa sổ base**: base = 0 → GP0–GP7; base = 8 → GP8–GP15.
- **Không** chọn được từng chân tùy ý.
- Muốn chân tùy ý phải đọc bằng CPU/SIO → không đạt 25 MHz.

Vì vậy mọi phương án mở rộng đều phải dùng **dải liền tiếp**.

### 3.3. Các phương án

| Phương án | Kênh | Chân | Cách làm firmware | RAM / độ sâu | BOM | Kết luận |
|---|---:|---|---|---|---|---|
| **A — hiện tại** | 8 | GP0–GP7 | `in pins, 8`, 1 byte/mẫu | 262.144 mẫu / 256 KB | Chuẩn |  **Chọn** |
| B — thêm 2 | 10 | GP0–GP9 | `in pins, 10`; packing 10-bit (1,33–4 byte/mẫu); phải dời 2 nút khỏi GP8/GP9 | Giảm độ sâu hoặc tăng RAM | +2 kênh bảo vệ |  Packing phức tạp, không đáng |
| C — 16 kênh, 1 SM | 16 | **GP0–GP15** | `in pins, 16`; mẫu 16-bit; 2 mẫu/word | Tối đa 131.072 mẫu / 256 KB (5,24 ms @25 MHz) | +8 kênh (gần gấp đôi) |  Vượt BOM, nhiều việc |
| D — 16 kênh, 2 SM | 16 | GP0–GP15 | 2 state machine + 2 DMA + interleave | Như C | +8 kênh |  Phức tạp hơn C |
| E — mux ngoài | 8+ | 8 chân + 74HC4051 | Chuyển kênh bằng mux | 1 byte/mẫu | Rẻ |  Mất 25 MHz/kênh |

### 3.4. Nếu mở rộng 16 kênh thì mở chân nào?

**Trả lời trực tiếp: GP8–GP15** (8 chân liền tiếp ngay sau GP0–GP7).

Điều kiện bắt buộc:

1. **Bỏ TFT khỏi GP10–GP15.** TFT phải dời sang GP16–GP21 (6 chân: BL, CS, SCK, MOSI, DC, RST). Khi đó không còn chỗ cho màn hình thứ hai — nhưng ta cũng chỉ dùng một TFT nên vẫn đủ.
2. **Firmware đổi sang mẫu 16-bit:**
   - `in pins, 16`, autopush 32 bit → 2 mẫu/word.
   - Buffer tối đa **131.072 mẫu** (256 KB) vì 262.144 × 2 byte = 512 KB > 520 KB SRAM.
   - Cửa sổ capture còn **5,24 ms @25 MHz** (thay vì 10,49 ms).
3. **Sửa protocol LAR2 + host tool + driver + decoder:**
   - Thêm trường `channels = 16`, `sample_width = 16` vào CAPS/INFO.
   - CRC tính trên mẫu 16-bit.
   - `lar2.py`, `host_la.py`, `driver/api.c`, PulseView 16 kênh.
   - Decoder đổi cách tách bit (hiện `buf[i] & 1` cho CH0).
   - Flash format đổi sang 16-bit.
4. **BOM tăng gần gấp đôi** phần bảo vệ: 16 kênh × (2× PR02 + BAV99 + Zener + buffer + tụ) → nguy cơ vượt 450.000 đ.

**Chỗ cho giao diện khi mở 16 kênh** (nếu vẫn giữ TFT):

| Chức năng | GPIO |
|---|---|
| Capture 16 kênh | GP0–GP15 |
| TFT ST7735 (6 chân) | GP16 BL, GP17 CS, GP18 SCK, GP19 MOSI, GP20 DC, GP21 RST |
| 4 nút | GP22, GP26, GP27, GP28 |
| LED STATUS | LED onboard GP25 (không ra header) |
| CAL_OUT | Bỏ (optional) hoặc đổi chỗ với 1 nút |

Nếu bỏ TFT thì có thể dùng GP16–GP22 cho nút/LED/CAL_OUT, nhưng như đã nói, bản này **giữ TFT**.

### 3.5. Khuyến nghị

- **Bản này giữ 8 kênh.** Đủ yêu cầu, BOM an toàn, firmware đã verify.
- Nếu nhóm vẫn muốn 16 kênh, phải **chốt trước khi Đình Anh vẽ PCB**: bỏ TFT khỏi GP10–GP15, chấp nhận BOM và firmware lớn hơn.
- **Không nên "vừa giữ TFT vừa mở 16 kênh"** vì GP10–GP15 trùng nhau.

### 3.6. Ảnh hưởng tới test 25 MHz

Các test hiện tại (CAN/UART/I2C/SPI) đều có tần số tín hiệu ≪ 25 MHz. Muốn chứng minh LA bắt được tín hiệu cao tần (12,5 MHz @25 MS/s) thì phải đo **trực tiếp vào GP0–GP7, không qua board bảo vệ**, vì mạch bảo vệ 3,3 V chỉ mô phỏng tới 5 MHz và đã suy giảm biên độ (2,42 V @5 MHz). Đây là giới hạn của mạch bảo vệ, không phải của firmware.

---

## 4. Góp ý sửa `SPEC_PCB.md`

### 4.1. §3 — thay bảng chân bằng Layout A

Thay toàn bộ bảng §3 bằng bảng ở §2.2 của file này. Các dòng phải sửa:

| Dòng trong spec | Sửa thành |
|---|---|
| UP = GP10 | **GP8** |
| DOWN = GP11 | **GP9** |
| OK = GP12 | Giữ GP12 |
| BACK = GP13 | **GP22** |
| LED STATUS = GP14 | **GP26** |
| CAL_OUT = GP16 | **GP27** |
| "GP15 dự phòng" | **GP15 = TFT RST** |
| OLED GP17–GP21 | **Bỏ hoàn toàn** |

### 4.2. §6 — bảng firmware đã lỗi thời

Bảng hiện tại ghi "Trigger cạnh/pattern chưa có" và "Lưu Flash/giải mã ngoài phạm vi" — **sai so với firmware hiện tại**. Thay bằng:

| Chức năng | Tình trạng thực tế |
|---|---|
| PIO/DMA, RAM, USB CDC, LAR2 **v1.2**, driver PC | Đã có; đã verify 25 MHz với CAN/UART/I2C/SPI |
| Trigger cạnh / pattern / UART / pre-trigger | **Đã có**, verified @25 MHz |
| Lưu nhiều phiên Flash, so sánh phiên, giải mã tại chỗ | **Đã có**: 2 slot × 512 KB, golden compare, diff, decode 4 protocol |
| TFT ST7735 | **Đã có** driver, đã test |
| Nút bấm / LED / CAL_OUT | Chưa có driver; sẽ bổ sung sau khi có board |
| Run/Stop tại thiết bị | Chưa có |
| Lưu/mở `.sr` trên PC | Có (PulseView) |

#
### 4.3. §2.2 — connector 2×8 vs BOM 1×10

Spec đề xuất **2×8** nhưng BOM KiCad đang dùng **1×10**. Đề nghị thống nhất **2×8** để mỗi kênh có GND bên cạnh. Sửa cả schematic và BOM.

### 4.4. Bỏ toàn bộ nội dung OLED

Vì chỉ dùng TFT, cần xóa/sửa các chỗ sau trong spec:

- §1 bảng "Giao diện tại chỗ": đổi "Module OLED 128×64 SPI" → **"TFT ST7735 1,8" 128×160, SPI"**.
- §2 sơ đồ khối: bỏ nhánh "OLED qua SPI", thay bằng "TFT qua SPI1".
- §2.4 tiêu đề "OLED và nút bấm" → **"TFT và nút bấm"**; bỏ mô tả module OLED.
- §5 BOM: bỏ dòng "Module OLED"; thêm dòng **"Module TFT ST7735 1,8""**.
- §7 tiêu chí nghiệm thu: bỏ "OLED/nút", thay bằng **"TFT/nút"**.

### 4.5. Board Pico 2, không phải Pico 2 W

§8 đang hỏi "Pico 2 hay Pico 2 W". Chốt: **Pico 2 (RP2350A)**. README cũ còn ghi Pico 2 W — cần cập nhật để tránh nhầm khi đặt footprint/kiểm tra.

---

## 5. Việc Đình Anh cần làm

Đình Anh apply các ý tưởng sau vào schematic/PCB:

1. **Thay bảng chân §3 bằng Layout A** ở §2.2 của file này:
   - Giữ CH0–CH7 = GP0–GP7.
   - Giữ TFT = GP10/11/13/14/15/16.
   - Dời nút: UP → GP8, DOWN → GP9, OK → GP12, BACK → GP22.
   - LED STATUS → GP26, CAL_OUT → GP27.
   - GP28 để trống.
2. **Bỏ hoàn toàn khối OLED** (GP17–GP21 để trống, không route).
3. **Thêm footprint TFT ST7735 1,8"** (6 chân: SCK, MOSI, CS, DC, RST, BL) nối đúng GP10/11/13/14/15/16; cấp 3V3 và GND.
4. **Đổi connector đầu đo thành 2×8** (chân lẻ CH0–CH7, chân chẵn GND), sửa cả BOM.
5. **Giữ 8 kênh bảo vệ**, điện trở đầu vào **2× 1 kΩ PR02 2 W** nối tiếp (không dùng 1× 2 kΩ 0603).
6. **Giữ test point** `OUT0…OUT7`, `3V3`, `VSYS`, `GND`; thêm điểm đo `IN/CLAMP/A/Z` trên một kênh đại diện.
7. **Cập nhật BOM**: bỏ OLED, thêm module TFT ST7735; kiểm tra tổng ≤ 450.000 đ.
8. **Gửi lại schematic/PDF** để Nam Khánh review trước khi layout PCB.

> Không cần thêm tầng 74HC245, không kẹp tín hiệu vào rail 3,3 V, không nối `CLAMP/A/Z` trực tiếp vào GPIO. Chỉ `Y → 33 Ω → GPx`.
