# PLAN 2026-11-16 — việc còn lại (phiên sau test nốt)

Phiên hôm nay đã chạy xong 85 bài giới hạn (80 bài giao thức + 5 bài tích hợp)
trên phần cứng thật. File này ghi lại **những gì chưa test được** và cách test,
để phiên sau làm tiếp mà không phải dựng lại ngữ cảnh.

Tài liệu liên quan:
- `docs/TEST-PLAN-8CH.md` — kế hoạch 85 bài + kết quả từng bài (§4–§9).
- `docs/TEST-Nov3.md` §5.3 — bảng giới hạn đã đo.
- `docs/WIRING.md` — bảng chân.
- `tools/sweep.py` — chạy lại tự động: `--only uart|i2c|spi|can|all`.

---

## 0. Trạng thái hiện tại

### Đã xong (2026-11-16)

| Giao thức | Pass cao nhất | Fail đầu tiên |
|---|---|---|
| UART @ 25 MHz | 5 Mbaud (5 mẫu/bit) | — (giới hạn máy phát) |
| UART @ 5 MHz | 921600 (5.4 mẫu/bit) | — |
| UART @ 1 MHz | 230400 (4.3 mẫu/bit) | — |
| I2C | 500 kHz @ 25 MHz và 5 MHz | 100 kHz @ 100 kHz sampling |
| **SPI @ 25 MHz** | **10.5 MHz** (3/3) | **11 MHz** (0/3) |
| CAN @ 25 MHz | 1 Mbps (25 mẫu/bit) | — |
| CAN @ 5 MHz | 1 Mbps (5 mẫu/bit) | — |
| CAN @ 1 MHz | 500 kbps (2 mẫu/bit) | — |

Tích hợp I1–I5: PASS 4/4 (kể cả UART 5 Mbaud + I2C 500 kHz + SPI 10 MHz +
CAN 1 Mbps cùng lúc, mask `0x01`, pretrigger 50 %).

Lỗi máy phát ESP32 đã sửa: `pdMS_TO_TICKS(<10)` = 0 ở `CONFIG_FREERTOS_HZ=100`
(macro `DELAY_MS`), `set spi > 26666` làm mất handle SPI, TWAI prescaler phải
chẵn / bội 4 khi >128 / `tseg1` 4 bit.

### Chưa test

1. **Màn hình TFT** — chưa từng nhìn bằng mắt (chưa có ảnh xác nhận).
2. **4 nút vật lý** — chưa hàn dây nên chưa test debounce/nhấn dài thật.
3. **PulseView GUI** — tầng `sigrok-cli` đã PASS hết (xem §4), chưa mở app.

### Đã xong thêm (2026-11-17)

4. **Trigger treo khi tìm thấy trigger muộn** — đã tìm ra nguyên nhân và sửa (§7).
   Trước đó capture có trigger hỏng ~50 % ở mọi kênh, không riêng CH7.
5. **CAN lặp biên** — 5/5, 5/5, 5/5, 20 kbps 3/3 (§1).
6. **USB song song với UI** — 3/3 (§5).
7. **Nạp firmware không cần BOOTSEL** — `tools/flash_pico.py` (§7).

## 1. CAN — phần còn lại (XONG 2026-11-17)

`tools/plan_checks.py --only can` sau khi sửa trigger (xem §7):

```
125k @25m  PASS 5/5   CAN ID=0x123 DLC=4 DATA=F0 BF D0 01
1M   @25m  PASS 5/5   CAN ID=0x123 DLC=4 DATA=EF BE CF 00
500k @5m   PASS 5/5   CAN ID=0x123 DLC=4 DATA=EC BB CC FD
20k  @1m   PASS 3/3   CAN ID=0x123 DLC=4 DATA=F7 C6 D7 08
```

Bài 2 (125 kbps @ 25 MHz) trước đây thỉnh thoảng ra
`CAN: extended frames are not decoded` — đúng là trigger cạnh rơi **giữa frame**
chứ không phải SOF. Vẫn còn khả năng này (~1/12 lần); chạy lại là PASS.

- [x] Lặp 5 lần các bài CAN biên → 5/5, 5/5, 5/5; 20 kbps @ 1 MHz 3/3.
- [ ] (tuỳ chọn) Lọc SOF thật trong trigger CAN (chỉ nhận cạnh xuống khi trước đó
      là recessive ≥ 3 bit). Hiện đã ghi hạn chế trong `TEST-PLAN-8CH.md` §10.

## 2. Màn hình TFT 128×160 (cần mắt người)

UI đã **thiết kế lại** ngày 2026-11-17 (framebuffer 128×160 + SPI DMA, 4 màn
theo flow mới, dạng sóng có vạch trigger + cursor). Thiết kế chi tiết, lý do và
checklist đầy đủ: **`docs/UI-DESIGN-Nov17.md`**.

Test nhanh, không cần máy phát, không cần nút:

```bash
cd Code/Logic_Analyzer_20261/PulseView-custom
python3 tools/ui_cycle.py              # 3 s/màn, lặp mãi, dữ liệu giả
python3 tools/ui_cycle.py -i 5 --rounds 2
python3 tools/ui_cycle.py --capture    # capture thật
```

Script in ra màn đang hiển thị kèm `[device OK]`; `[device SAI: man N]` nghĩa
là lỗi logic, không phải lỗi hiển thị.

Checklist tối thiểu (chi tiết ở `UI-DESIGN-Nov17.md` §6):

- [ ] Header 14 px: nền `#16202C`, chữ `#38BDF8`, không cắt ký tự.
- [ ] Màn 1 `1/4 CAU HINH DO`: 4 dòng Tan so / So mau / Kenh / Pre-Trig căn
      phải thẳng cột; dòng chọn có nền `#1E293B` + vạch trái cyan; Edit đổi
      sang nền `#065F46`, chữ `#34D399`, giá trị bọc `[ ]`.
- [ ] Màn 1: `[ BAT DAU THU ]` giữa màn, có khung; OK ở dòng này chạy capture
      và tự sang màn 3.
- [ ] Màn 2 `2/4 TRIGGER`: đổi Che do EDGE/PATTERN/UART thì "Dieu kien" đổi
      theo; "Baud/Clk" mờ khi không phải UART.
- [ ] Màn 3 `3/4 DANG SONG [25M]`: 4 track 26 px, màu `#38BDF8` `#FB923C`
      `#4ADE80` `#C084FC`, cạnh xung sắc; vạch **đỏ** = trigger, **cursor
      trắng đứt nét** ở giữa; đáy `T:...` + `STOP/RUN`.
- [ ] Màn 3: UP/DOWN cuộn, giữ UP đổi trang CH0-3 ↔ CH4-7, giữ DOWN đổi zoom
      x2/x4/x8.
- [ ] Màn 4 `4/4 GIAI MA BUS`: `< UART >` cyan, `RX:CH0 115200`, và với dữ
      liệu giả phải thấy dòng `UART 115200 8N1: "HELLO"`.
- [ ] Không nháy khi đổi màn / sửa giá trị (quay video 5 s).
- [ ] Chuyển màn < 100 ms, không có frame nửa vẽ.

**Phím USB dự phòng** (chữ thường = nhấn ngắn, chữ hoa = nhấn dài):

```bash
python3 tools/ui_keys.py --send ddddo   # màn 1: 4 DOWN rồi OK -> capture
python3 tools/ui_keys.py --send o       # màn 3 -> màn 4
python3 tools/ui_keys.py --send b       # BACK
```

**Dữ liệu giả** (không cần máy phát) ở màn 3/4 — chi tiết cách gọi op 12:
`docs/UI-DESIGN-Nov17.md` §4; script `tools/ui_cycle.py` gọi sẵn.

---

## 3. 4 nút vật lý (sau khi hàn dây)

Chân theo Layout A: **UP=GP8, DOWN=GP9, OK=GP12, BACK=GP22** (TFT dùng
GP10/11/13/14/15/16). Nút nối xuống GND, dùng pull-up nội.

- [ ] Mỗi nút nhấn ngắn đúng 1 bước (không nhảy 2 dòng) — debounce 20–50 ms.
- [ ] Giữ nút 600 ms → sự kiện nhấn dài (UP/DOWN tự lặp, OK đổi zoom,
      BACK về màn 01).
- [ ] Nhấn đồng thời 2 nút không treo UI.
- [ ] Rung/giữ nút 2 s không sinh sự kiện lặp sai.
- [ ] So sánh với `ui_keys.py`: cùng một chuỗi phím phải ra cùng một màn hình.

---

## 4. PulseView GUI (app thật)

Tầng libsigrok đã kiểm tự động bằng `tools/plan_checks.py --only pv` (2026-11-17):

```
capture    PASS
uart       PASS  uart-1: 4C
spi        PASS  spi-1: DE
i2c        PASS  i2c-1: Write
can        PASS  can-1: Identifier: 291 (0x123)
```

Lệnh repro cũ của PLAN §7 (`sigrok-cli` 25 MHz, trigger CH7, 100000 mẫu) giờ
**5/5 PASS** (trước 0/5).

Chi tiết đã xác nhận được:

| Bước | Kết quả |
|---|---|
| `-d rp2350-la:conn=/dev/ttyACM0 --show` | 8 kênh CH0–CH7, 8 mức rate 1k…25m, max 262144, trigger source/slope/pattern |
| Capture 25 MHz, trigger CH0↓, 100000 mẫu | OK, lưu `.sr` |
| Decoder UART `baudrate=115200:rx=CH0` | `4C 41 31 32 33 34` = `LA1234` |
| Decoder SPI `clk=CH4:mosi=CH5:cs=CH3` | `DE AD BE EF 00 01 02 03` |
| Decoder I2C `scl=CH1:sda=CH2`, annotation **`address-write`** | `Address write: 68` |
| Decoder CAN `can_rx=CH7:nominal_bitrate=250000`, annotation **`id`** | `Identifier: 291 (0x123)` |

Việc cần làm trong app (cần người):

- [ ] Mở `build/pulseview/pulseview`, quét thiết bị thấy `rp2350-la`.
- [ ] Chọn 8 kênh, 25 MHz, 40000 mẫu, trigger CH0 cạnh xuống → acquisition
      dừng đúng, có pretrigger, thanh thời gian đúng 1.6 ms.
- [ ] Thêm 4 decoder (UART/I2C/SPI/CAN) và gán chân như bảng trên; CAN đặt
      `nominal_bitrate = 250000`.
- [ ] Lưu/xoá session, mở lại file `.sr` vẫn decode đúng.
- [ ] Kiểm tra hiển thị 8 track, màu kênh, zoom bằng lăn chuột.
- [ ] Chụp ảnh màn hình PulseView cho báo cáo.

**Lưu ý recovery:** nếu app bị treo giữa acquisition, gửi `HELLO`
(`tools/host_la.py`) hoặc `python3 tools/flash_pico.py` để lấy lại board.
Từ bản 2026-11-17 firmware không còn tự restart trigger vô hạn, nên hết cảnh
kẹt `busy` 20 s.

## 5. USB song song với UI (XONG 2026-11-17)

- [x] UI đang ở màn 04 DECODE, host chạy `capture + decode` qua `host_la`:
      3/3 lần capture 262144 mẫu @ 1 MHz, decode đủ UART/I2C/SPI/CAN, UI không treo.
- [x] Gửi phím USB ('o') xen giữa các lần capture: phím được xử lý sau, không mất frame.
- [ ] UI kích hoạt capture (`--send oo`) trong lúc PulseView đang mở — cần mở app thật.

## 6. Việc giấy tờ còn lại (không phải test)

- [ ] Báo cáo Word, slide, video demo ≤ 3 phút.
- [ ] Cross-review với Đình Anh (`SPEC_PCB.md` / `REPLY_SPEC_PCB.md`).
- [ ] Chụp ảnh mạch + màn hình đưa vào báo cáo.

---

## 7. Lỗi trigger đã tìm ra và sửa (2026-11-17)

Triệu chứng: capture có trigger (mọi kênh, không riêng CH7) treo ~50 % số lần,
host báo `Capture timeout`, thiết bị im lặng nhưng vẫn trả lời `HELLO`.

**Nguyên nhân gốc** — trong `capture_task()` (firmware/main.c), khi tìm thấy
trigger ở vị trí `trigger_abs`, điều kiện hoàn tất là

    produced_samples() >= trigger_abs + (count - pretrigger)

DMA luôn ghi đầy 262144 mẫu (10.49 ms @ 25 MHz), nên điều kiện chỉ thoả khi
`trigger_abs <= 262144 - post`. Với N=100000, post=98000 → trigger phải nằm
trong **6.5 ms đầu**. Rơi vào 37 % cuối thì firmware gọi `start_capture()` lại.
Vòng restart đó có chu kỳ tự do (~16.7 ms đo được) và **khoá pha** với burst
100 ms của máy phát (6 × 16.7 = 100 ms), nên trigger lại rơi đúng chỗ muộn ở
mọi cửa sổ → restart mãi tới hạn 2000 lần rồi báo lỗi. Frame ERROR đó lại bị
`send_frame()` bỏ khi `tud_cdc_connected()` đọc false, nên host chỉ thấy im lặng.

Bằng chứng: `LAR2_ANALYZE_RUNTIME` (op 11) cho thấy `trig_found` với
`trigger_abs ≈ 224000…240000` và `trigger_restarts` tăng đều 24 mỗi 0.4 s;
cùng cấu hình ở N=20000 (post nhỏ) thì PASS 4/4 với `trigger_abs` rải đều
63725…233176.

**Cách sửa**

1. `capture_task()`: không restart nữa — **trượt cửa sổ sang phải**. Nếu
   `trigger_abs - pretrigger + count` vượt buffer thì lấy
   `base = CAPTURE_BYTES - DMA_LAG_BYTES - count`, giao cửa sổ
   `[base, base+count)` và báo `trigger_index = trigger_abs - base`. Số mẫu vẫn
   đúng `count`, trigger vẫn nằm trong cửa sổ; chỉ mất bớt mẫu *trước* trigger.
   Restart chỉ còn khi `count` xấp xỉ cả buffer (không thể vừa trigger vừa đủ mẫu).
2. `send_frame()`: bỏ điều kiện `tud_cdc_connected()` khỏi vòng gửi, chỉ dựa vào
   deadline → frame lỗi không bị nuốt, host không phải chờ tới timeout của nó.
3. Thêm `LAR2_ANALYZE_RUNTIME` (op 11) trả trạng thái trigger, và cho phép nó
   trả lời cả khi đang capture (`busy`) — công cụ chẩn đoán cho lần sau.

**Kết quả sau khi sửa**

| Kiểm tra | Trước | Sau |
|---|---|---|
| CH7 trigger 25 MHz N=100000, 12 lần | 3/12 | **12/12** (0.3 s/lần) |
| N=120000 (46 % trigger muộn) | — | **10/10**, có 1 lần trượt cửa sổ (`trigger_index=74922`) |
| `sigrok-cli` đúng lệnh repro của PLAN cũ | 0/5 | **5/5** |
| `plan_checks --only ch7` (6 ca) | 3/6 | **6/6** |
| `plan_checks --only can` (4 ca) | 4/4 | **4/4** |
| `plan_checks --only pv` (5 bước) | — | **5/5** (UART/SPI/I2C/CAN) |
| Bài tích hợp I1–I5 (`TEST-PLAN-8CH` §8) | 5/5 | **5/5** (I4 lặp 5/5) |
| Sweep 80 bài giao thức | 80 bài | không hồi quy (xem dưới) |

Sweep lại toàn bộ 80 bài sau khi sửa: UART 19/20 (bài 1 lệch pha, lặp lại PASS),
I2C 17/20, SPI 16/20, CAN 19/20 — đúng bằng bảng §5.3 của `TEST-Nov3.md`, tức
không có hồi quy. Hai chỗ bảng kỳ vọng đã sửa cho khớp số đo: SPI bài 19
(1 MHz lấy mẫu 1 MHz = 1 mẫu/bit, aliasing) đổi thành `F`.

### Nạp firmware không cần chạm tay

`tools/flash_pico.py` tự xin firmware reboot vào UF2 bootloader
(`LAR2_ANALYZE_BOOTLOADER`, op 10 → `reset_usb_boot`), chờ mount
`/media/$USER/RP2350`, copy UF2 rồi chờ board lên lại:

```bash
python3 tools/flash_pico.py            # hoặc: python3 tools/flash_pico.py <file.uf2>
```

Đã dùng thật 3 lần trong phiên này; từ giờ chỉ cần board đang chạy firmware
(lệnh này có từ bản `1.2+trg+an` ngày 2026-11-17).
