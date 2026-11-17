# UI 128×160 — thiết kế lại (2026-11-17)

Thay cho giao diện 4 màn kiểu "band 8 px" cũ. Mục tiêu: màn hình đọc được
ngoài trời/trong phòng, dạng sóng là trung tâm, 4 nút không cần suy nghĩ.

- Code: `firmware/ui.c` (toàn bộ UI), `tft_st7735.c::tft_flush_fb()` (đẩy frame).
- Script kiểm tra bằng mắt: `python3 tools/ui_cycle.py` (xem §6).
- Dữ liệu giả để test không cần máy phát: op `12` (`LAR2_ANALYZE_UI_DEMO`).

## 1. Nguyên tắc

| Quyết định | Lý do |
|---|---|
| Framebuffer 128×160 RGB565 trong SRAM (40 KB) | Mỗi lần đổi trạng thái chỉ cần **một** lần ghi cửa sổ → không có màn hình nửa vẽ, không nháy. |
| Đẩy frame bằng SPI DMA (`dma_channel_configure` → `spi_get_dreq`), 20 MHz | 40 960 byte ≈ 16 ms/frame ≈ 60 fps, CPU rảnh trong lúc truyền. |
| Framebuffer lưu **byte order của panel** (big-endian/pixel) | `tft_flush_fb()` thành copy thuần, không cần buffer trung gian 40 KB thứ hai. |
| Giữ màn dọc 128×160 | 4 track xếp dọc (26 px/track) dễ đọc hơn 4 track ngang trên 160×128; giữ nguyên MADCTL/định hướng đã đo. |
| Font 6×8 sẵn có, 21 ký tự/dòng | Đã có `font6x8`, đủ cho mọi dòng trong spec. |
| Chỉ vẽ lại khi có event | `ui_tick()` không flush nếu không có gì đổi → không chiếm SPI trong lúc capture. |

## 2. Sơ đồ chuyển màn (đúng spec)

```
        +------------------ BACK ------------------+
        |                                          v
  +-----------+  BACK   +-----------+       +--------------+
  | 1 CONFIG  |-------->| 2 TRIGGER |       | 4 DECODE     |
  +-----------+  <------+-----------+       +--------------+
        |                    ^                    ^
        | OK tren [BAT DAU]  |                    | OK
        v                    |                    |
  +--------------+  BACK     |                    |
  | 3 WAVEFORM   |-----------+                    |
  +--------------+--------------------------------+
```

- BACK: đang Edit → thoát Edit (giữ nguyên màn); đang Browse → chuyển màn.
- 1 CONFIG → 2 TRIGGER → 1 CONFIG.
- 1 CONFIG, dòng `[ BAT DAU THU ]`, OK → capture → 3 WAVEFORM.
- 3 WAVEFORM, OK → 4 DECODE; BACK → 1 CONFIG.
- 4 DECODE, BACK → 3 WAVEFORM.
- Mọi lần vào màn 1/2 đều đặt con trỏ về dòng đầu (dòng 0) cho hành vi lặp lại
  được; script test dựa vào điều này.

## 3. Bố cục từng màn

Chung: header 14 px (nền `#16202C`, chữ `#38BDF8`), body nền `#0A0E14`,
dòng danh sách cao 20 px, giá trị căn phải tại x=124.

### 3.1 Màn 1 — `1/4 CAU HINH DO`

```
1/4 CAU HINH DO
 Tan so            25M     <- dòng chọn: nền #1E293B + vạch trái 2px #38BDF8
 So mau           262K
 Kenh           CH 0-7
 Pre-Trig          25%
        [ BAT DAU THU ]     <- khung, chọn thì viền + nền sáng
        UP/DN  OK SUA  BACK
```

- Sửa giá trị: nền dòng `#065F46`, chữ `#34D399`, giá trị bọc `[ ]`… (spec:
  highlight xanh lá khi edit).
- **Khác spec có chủ ý**: "Tan so" chỉ có 8 mức phần cứng thật
  `1K/10K/100K/250K/500K/1M/5M/25M`. Spec ghi 10M/50M/100M nhưng PIO không
  tạo được các mức đó — thà thiếu lựa chọn hơn hiển thị mức không tồn tại.
- "So mau" = 16K/64K/131K/262K (buffer 262144 mẫu).
- "Pre-Trig" = % của N (0/25/50/75) → firmware nhận số mẫu.

### 3.2 Màn 2 — `2/4 TRIGGER`

```
2/4 TRIGGER
 Che do            EDGE
 Kenh               CH0
 Dieu kien      RISING      <- PATTERN: 0x55/0xFF | UART: 0x55
 Baud/Clk        115200      <- mờ (#64748B) khi mode != UART
        UP/DN  OK SUA  BACK
```

- "Dieu kien" đổi nghĩa theo mode: EDGE → RISING/FALLING; PATTERN → byte/mask
  (mask cố định 0xFF = đủ 8 kênh); UART → byte đồng bộ.
- "Baud/Clk" chỉ ảnh hưởng **bộ giải mã** (màn 4). Trigger UART trong firmware
  tự ước lượng baud rồi tìm byte, không dùng giá trị này — ghi rõ ở đây để
  không ai tưởng nó đổi ngưỡng trigger.

### 3.3 Màn 3 — `3/4 DANG SONG [25M]`

```
3/4 DANG SONG [25M]
PG1/2 CH0-3  Z:X1
 0 ┌──────────────────────────────────────────────┐
 1 │  dạng sóng 4 track, 26 px/track              │  <- vạch đỏ = trigger
 2 │  cursor trắng đứt nét ở giữa (x=72)          │
 3 └──────────────────────────────────────────────┘
T:65536                              STOP
        OK MA   BACK VE
```

- Vùng vẽ: y=30..134 (vùng xung chiếm 65 % chiều cao, phần còn lại là 2 thanh
  thông số 1 dòng — spec yêu cầu ≥70 % diện tích; đây là 128×104/128×160).
- Gutter trái 16 px chỉ vẽ số kênh (0..7) để không ăn chỗ dạng sóng.
- Màu kênh: CH0 `#38BDF8`, CH1 `#FB923C`, CH2 `#4ADE80`, CH3 `#C084FC`;
  trang 2 (CH4-7) dùng lại đúng 4 màu đó (spec: CHn và CHn+4 cùng màu).
- Vạch đỏ `#EF4444` = vị trí trigger (`trigger_index` từ firmware, trước đây
  bị bỏ đi `(void)trigger_index`).
- Cursor trắng đứt nét cố định giữa màn hình; `T:` dưới đáy là số mẫu tại
  cursor, cuộn là dời dữ liệu dưới cursor.
- Trạng thái phải: `RUN` (xanh lá) / `STOP` / `FAIL` (đỏ).
- Nút: UP/DOWN ngắn cuộn ±1/8 màn; UP giữ = đổi trang CH; DOWN giữ = zoom
  x1→x2→x4→x8; OK = sang giải mã; BACK = về màn 1.

### 3.4 Màn 4 — `4/4 GIAI MA BUS`

```
4/4 GIAI MA BUS
 Chuan          < UART >
 Cau hinh   RX:CH0 115200
──────────────────────────
UART 115200 8N1: "HELLO"
        OK CHON  DN CUON
```

- OK ngắn = vào/ra chế độ sửa của ô đang focus; OK giữ = đổi ô focus
  (Chuan ↔ Cau hinh) — vì UP/DOWN đã dành cho cuộn dữ liệu như spec.
- UP/DOWN khi không sửa: cuộn danh sách decode (13 dòng × 21 ký tự).
- BACK: thoát sửa, hoặc về màn 3.
- **Khác spec có chủ ý**: không có hexdump `[Index] HEX (ASCII)` cho cả 4
  giao thức, vì `decoders.c` chỉ trả text đã chuẩn hoá cho từng bus
  (`UART …: "HELLO"`, `I2C: 0x68 W ACK …`, `SPI: DE AD BE EF …`,
  `CAN ID=0x123 DLC=4 DATA=…`). Muốn hexdump thật phải đổi 4 decoder + test
  giải mã; text hiện tại đã gọn 1 dòng/bus và đúng thứ người dùng cần đọc.

## 4. Dữ liệu giả (test không cần máy phát)

`LAR2_ANALYZE_UI_DEMO` (op 12) dựng 40 000 mẫu @ 25 MHz:

- CH0: khung UART 115200 8N1 `"HELLO"` (217 mẫu/bit) → màn 4 decode ra đúng
  `UART 115200 8N1: "HELLO"` (đã kiểm bằng `gcc` + `firmware/decoders.c`).
- CH1 1 kHz, CH2 10 kHz, CH3 100 kHz, CH4 ~1 MHz, CH5 pulse train,
  CH6 ~200 kHz, CH7 2.5 kHz.

Sau khi nạp demo, UI nhảy thẳng màn 3, trigger giả đặt ở mẫu 2000.

## 5. Những gì đã kiểm bằng máy

| Kiểm tra | Cách | Kết quả |
|---|---|---|
| Thứ tự màn | `RUNTIME` byte 30 sau từng phím | 0→2→3→2→0→1→0 đúng spec |
| Capture thật qua UI | 4 DOWN + OK từ màn 1 (25 MHz/262K/CH0↑) | hoàn tất, màn 3 có dữ liệu |
| Decode từ dữ liệu giả | `decoders_run_map` chạy bằng `gcc` trên mẫu giả | `UART 115200 8N1: "HELLO"` |
| Phím USB không bị nuốt | gửi 1 phím xen giữa các frame LAR2 | OK (xem §7) |
| Bố cục/màu/nháy | **chưa** — cần mắt người | §6 |

## 6. Cách test bằng mắt

```bash
cd Code/Logic_Analyzer_20261/PulseView-custom
python3 tools/ui_cycle.py              # 3 s/màn, lặp mãi, dùng dữ liệu giả
python3 tools/ui_cycle.py -i 5        # chậm hơn
python3 tools/ui_cycle.py --rounds 2  # 2 vòng rồi dừng
python3 tools/ui_cycle.py --capture   # capture thật (cần máy phát đang chạy)
```

Script in ra màn đang hiển thị + `[device OK]` khi trạng thái firmware khớp.
Nếu thấy `[device SAI]` thì lỗi ở logic chứ không phải màn hình.

Checklist mắt người:

- [ ] Header: nền xanh đen, chữ cyan, không bị cắt ký tự.
- [ ] Màn 1: 4 dòng giá trị căn phải thẳng cột; dòng chọn có nền + vạch trái;
      vào Edit thì nền chuyển xanh lá, giá trị có `[ ]`.
- [ ] Màn 1: `[ BAT DAU THU ]` nằm giữa, có khung.
- [ ] Màn 2: đổi mode EDGE/PATTERN/UART thì "Dieu kien" đổi theo; "Baud/Clk"
      mờ khi không phải UART.
- [ ] Màn 3: 4 track rõ, màu đúng 4 màu, cạnh xung sắc (không nhoè).
- [ ] Màn 3: vạch đỏ (trigger) và cursor trắng đứt nét nhìn thấy được.
- [ ] Màn 3: cuộn UP/DOWN mượt, giữ UP đổi trang CH4-7 (màu vẫn 4 màu đó),
      giữ DOWN đổi zoom x2/x4/x8, dữ liệu vẫn đúng.
- [ ] Màn 4: `< UART >` cyan, dòng cấu hình, và ít nhất 1 dòng decode
      `UART 115200 8N1: "HELLO"` với dữ liệu giả.
- [ ] Không nháy khi chuyển màn / khi sửa giá trị (quay video 5 s).
- [ ] Không có viền rác, không lệch 1 px ở header/footer.

## 7. Lỗi cũ đã sửa trong lần này

1. **Phím bị nuốt sau mỗi frame LAR2** — `parser.used == parser.total` sau khi
   xử lý frame, nên byte kế tiếp bị coi là "đang giữa frame" và đưa vào parser
   thay vì xử lý như phím. Hậu quả: gửi 1 phím xen giữa 2 lệnh host thì phím
   mất. Sửa: reset `parser.used/total` ngay sau `command()`.
2. **CH4-7 vẽ bằng màu CH0-3** — `C_CH[i]` thay vì `C_CH[first+i]`.
3. **Không có đường về màn 1/2** — trước đây WAVE↔DECODE khép kín, muốn đổi
   Fs phải reset board.
4. **Footer ghi "LEFT/RIGHT"** trong khi chỉ có UP/DOWN.
5. **`trigger_index` bị bỏ** — không có vạch trigger trên dạng sóng.
6. **Con trỏ dòng không reset khi vào lại màn** — cùng một chuỗi phím cho kết
   quả khác nhau tuỳ lần trước; script test không lặp lại được.
