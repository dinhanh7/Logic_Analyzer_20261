/* SPDX-License-Identifier: GPL-3.0-or-later
 * 128x160 ST7735 UI: four screens, full-frame framebuffer, 4-button state
 * machine.
 *
 * The panel is portrait. Everything is composed into a 128x160 RGB565
 * framebuffer held in panel byte order and pushed with one SPI DMA window
 * write, so a screen never shows a half-drawn frame: no flicker, no tearing
 * between a label and its value.
 *
 * Screen flow (BACK leaves edit mode before it changes screen):
 *
 *   CONFIG --BACK--> TRIGGER --BACK--> CONFIG
 *   CONFIG --OK on [BAT DAU THU]--> WAVEFORM --OK--> DECODE --BACK--> WAVEFORM
 *   WAVEFORM --BACK--> CONFIG
 *
 * Layout rules: 6x8 font, 21 characters per line, header bar 14 px, body rows
 * 20 px, waveform tracks 26 px.
 */
#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "buttons.h"
#include "decoders.h"
#include "font6x8.h"
#include "lar2.h"
#include "tft_st7735.h"

#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8u) << 8) | (((g) & 0xFCu) << 3) | ((b) >> 3)))

/* Palette: dark, high contrast, one accent. */
#define C_BG      RGB565(0x0A, 0x0E, 0x14)
#define C_HDR     RGB565(0x16, 0x20, 0x2C)
#define C_ACC     RGB565(0x38, 0xBD, 0xF8)
#define C_BROWSE  RGB565(0x1E, 0x29, 0x3B)
#define C_EDIT    RGB565(0x06, 0x5F, 0x46)
#define C_EDIT_T  RGB565(0x34, 0xD3, 0x99)
#define C_TEXT    RGB565(0xE2, 0xE8, 0xF0)
#define C_DIM     RGB565(0x64, 0x74, 0x8B)
#define C_TRIG    RGB565(0xEF, 0x44, 0x44)
#define C_CURSOR  RGB565(0xF8, 0xFA, 0xFC)

/* CH0-3 and the same four hues reused for CH4-7, as specified. */
static const uint16_t C_CH[4] = {
    RGB565(0x38, 0xBD, 0xF8), RGB565(0xFB, 0x92, 0x3C),
    RGB565(0x4A, 0xDE, 0x80), RGB565(0xC0, 0x84, 0xFC)
};

#define SCR_W TFT_WIDTH
#define SCR_H TFT_HEIGHT
#define HDR_H 14
#define ROW_Y0 22
#define ROW_H 20
#define ROW_BOX 18
#define BOTTOM_Y 138
#define HINT_Y 152

#define WAVE_TOP 30
#define WAVE_TRACK_H 26
#define WAVE_X0 16
#define WAVE_W (SCR_W - WAVE_X0)

ui_config ui_cfg;

typedef enum { SCR_CONFIG = 0, SCR_TRIGGER, SCR_WAVE, SCR_DECODE } ui_screen;

static ui_screen screen;

/* Config screen: row 0..3 are values, row 4 is the start button. */
static uint8_t cfg_row;
static bool cfg_edit;
static uint8_t rate_idx, count_idx, mask_idx, pre_idx;

/* Trigger screen. */
static uint8_t trig_row;
static bool trig_edit;

/* Waveform screen. */
static uint8_t wave_page;
static uint8_t zoom;                /* 1, 2, 4, 8 */
static uint32_t wave_t;
static uint32_t cap_trigger;

/* Decode screen. */
static uint8_t proto_sel;
static uint8_t dec_focus;           /* 0 = protocol, 1 = bus config */
static bool dec_edit;
static uint32_t dec_scroll;

static uint8_t event_count;

static const uint8_t *cap_samples;
static uint32_t cap_len;
static uint64_t cap_rate;
static bool cap_ok;
static bool capturing;

static bool capture_request;

static char dec_text[1024];

static const uint64_t rates[] = {
    1000, 10000, 100000, 250000, 500000, 1000000, 5000000, 25000000
};
static const char *const rate_lbl[] = {
    "1K", "10K", "100K", "250K", "500K", "1M", "5M", "25M"
};
/* The PIO samples a fixed set of rates; the UI only offers those. */
#define RATE_N (sizeof(rates) / sizeof(rates[0]))

static const uint32_t counts[] = { 16384, 65536, 131072, 262144 };
static const char *const count_lbl[] = { "16K", "64K", "131K", "262K" };
#define COUNT_N (sizeof(counts) / sizeof(counts[0]))

static const uint8_t masks[] = { 0xFF, 0x0F };
static const char *const mask_lbl[] = { "CH 0-7", "CH 0-3" };
#define MASK_N (sizeof(masks) / sizeof(masks[0]))

static const uint8_t pre_pct[] = { 0, 25, 50, 75 };
#define PRE_N (sizeof(pre_pct) / sizeof(pre_pct[0]))

static const uint32_t bauds[] = { 0, 9600, 115200 };
static const char *const baud_lbl[] = { "AUTO", "9600", "115200" };
#define BAUD_N (sizeof(bauds) / sizeof(bauds[0]))
static uint8_t baud_idx;

static const uint8_t proto_mask[4] = {
    LAR2_PROTO_UART, LAR2_PROTO_I2C, LAR2_PROTO_SPI, LAR2_PROTO_CAN
};
static const char *const proto_name[4] = { "UART", "I2C", "SPI", "CAN" };

/* ---------- graphics ---------- */
static uint16_t fb[SCR_W * SCR_H];

static inline void px(int x, int y, uint16_t c)
{
    if (x < 0 || y < 0 || x >= SCR_W || y >= SCR_H) return;
    fb[y * SCR_W + x] = __builtin_bswap16(c);
}

static void fill(int x, int y, int w, int h, uint16_t c)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCR_W) w = SCR_W - x;
    if (y + h > SCR_H) h = SCR_H - y;
    if (w <= 0 || h <= 0) return;
    for (int r = y; r < y + h; r++)
        for (int i = x; i < x + w; i++)
            fb[r * SCR_W + i] = __builtin_bswap16(c);
}

static void hline(int x, int y, int w, uint16_t c)
{
    fill(x, y, w, 1, c);
}

static void vline(int x, int y, int h, uint16_t c)
{
    fill(x, y, 1, h, c);
}

static void frame_rect(int x, int y, int w, int h, uint16_t c)
{
    hline(x, y, w, c);
    hline(x, y + h - 1, w, c);
    vline(x, y, h, c);
    vline(x + w - 1, y, h, c);
}

static void glyph(int x, int y, unsigned ch, uint16_t fg)
{
    if (ch < FONT6X8_FIRST || ch >= FONT6X8_FIRST + FONT6X8_COUNT) ch = '?';
    const uint8_t *g = font6x8[ch - FONT6X8_FIRST];
    for (int r = 0; r < FONT6X8_H; r++)
        for (int c = 0; c < FONT6X8_W; c++)
            if ((g[r] >> c) & 1u) px(x + c, y + r, fg);
}

static void text(int x, int y, const char *s, uint16_t fg)
{
    for (; *s; s++, x += FONT6X8_W) glyph(x, y, (unsigned char)*s, fg);
}

static int text_w(const char *s)
{
    return FONT6X8_W * (int)strlen(s);
}

static void text_right(int x_right, int y, const char *s, uint16_t fg)
{
    text(x_right - text_w(s), y, s, fg);
}

static void text_center(int y, const char *s, uint16_t fg)
{
    text((SCR_W - text_w(s)) / 2, y, s, fg);
}

static void flush(void)
{
    tft_flush_fb(fb);
}

static void header(const char *title)
{
    fill(0, 0, SCR_W, HDR_H, C_HDR);
    text(4, 3, title, C_ACC);
    hline(0, HDR_H, SCR_W, C_BROWSE);
}

/* ---------- generic list row ---------- */
static void row(int y, const char *label, const char *value,
                bool selected, bool edit, bool enabled)
{
    if (selected) {
        fill(0, y - 2, SCR_W, ROW_BOX, edit ? C_EDIT : C_BROWSE);
        vline(0, y - 2, ROW_BOX, C_ACC);
        vline(1, y - 2, ROW_BOX, C_ACC);
    }
    text(7, y + 2, label, selected && !edit ? C_TEXT : (enabled ? C_TEXT : C_DIM));
    uint16_t vc = !enabled ? C_DIM : (edit ? C_EDIT_T : (selected ? C_ACC : C_TEXT));
    text_right(SCR_W - 4, y + 2, value, vc);
}

/* ---------- formatting ---------- */
static uint32_t pretrigger_samples(void)
{
    return (uint32_t)((uint64_t)ui_cfg.count * pre_pct[pre_idx] / 100u);
}

static void apply_config(void)
{
    ui_cfg.rate = rates[rate_idx];
    ui_cfg.count = counts[count_idx];
    ui_cfg.mask = masks[mask_idx];
}

static const char *mode_name(void)
{
    switch (ui_cfg.trig_mode) {
    case LAR2_TRIGGER_PATTERN: return "PATTERN";
    case LAR2_TRIGGER_UART: return "UART";
    default: return "EDGE";
    }
}

static const char *cond_name(char *buf, size_t n)
{
    switch (ui_cfg.trig_mode) {
    case LAR2_TRIGGER_PATTERN:
        snprintf(buf, n, "0x%02X/0xFF", ui_cfg.trig_value);
        break;
    case LAR2_TRIGGER_UART:
        snprintf(buf, n, "0x%02X", ui_cfg.trig_value);
        break;
    default:
        return ui_cfg.trig_edge == LAR2_EDGE_FALLING ? "FALLING" : "RISING";
    }
    return buf;
}

static const char *bus_config(char *buf, size_t n)
{
    switch (proto_sel) {
    case 0:
        snprintf(buf, n, "RX:CH0 %s", baud_idx ? baud_lbl[baud_idx] : "AUTO");
        break;
    case 1: snprintf(buf, n, "SCL:CH1 SDA:CH2"); break;
    case 2: snprintf(buf, n, "SCK:CH4 MOSI:CH5"); break;
    default: snprintf(buf, n, "RX:CH7 250K"); break;
    }
    return buf;
}

/* ---------- screen 1: capture config ---------- */
static void draw_config(void)
{
    char buf[24];
    header("1/4 CAU HINH DO");
    fill(0, HDR_H, SCR_W, SCR_H - HDR_H, C_BG);

    row(ROW_Y0 + 0 * ROW_H, "Tan so", rate_lbl[rate_idx],
        cfg_row == 0, cfg_edit && cfg_row == 0, true);
    row(ROW_Y0 + 1 * ROW_H, "So mau", count_lbl[count_idx],
        cfg_row == 1, cfg_edit && cfg_row == 1, true);
    row(ROW_Y0 + 2 * ROW_H, "Kenh", mask_lbl[mask_idx],
        cfg_row == 2, cfg_edit && cfg_row == 2, true);
    snprintf(buf, sizeof buf, "%u%%", pre_pct[pre_idx]);
    row(ROW_Y0 + 3 * ROW_H, "Pre-Trig", buf,
        cfg_row == 3, cfg_edit && cfg_row == 3, true);

    /* Start button: framed, centred, highlighted when selected. */
    const char *btn = "[ BAT DAU THU ]";
    int bw = text_w(btn) + 8;
    int bx = (SCR_W - bw) / 2;
    int by = 108;
    if (cfg_row == 4) {
        fill(bx, by, bw, 16, C_BROWSE);
        frame_rect(bx, by, bw, 16, C_ACC);
        text_center(by + 4, btn, C_ACC);
    } else {
        frame_rect(bx, by, bw, 16, C_DIM);
        text_center(by + 4, btn, C_TEXT);
    }

    text_center(HINT_Y, "UP/DN  OK SUA  BACK", C_DIM);
}

/* ---------- screen 2: trigger ---------- */
static void draw_trigger(void)
{
    char buf[24];
    header("2/4 TRIGGER");
    fill(0, HDR_H, SCR_W, SCR_H - HDR_H, C_BG);

    row(ROW_Y0 + 0 * ROW_H, "Che do", mode_name(),
        trig_row == 0, trig_edit && trig_row == 0, true);

    snprintf(buf, sizeof buf, "CH%u", ui_cfg.trig_channel);
    row(ROW_Y0 + 1 * ROW_H, "Kenh", buf,
        trig_row == 1, trig_edit && trig_row == 1, true);

    char cond[16];
    if (trig_edit && trig_row == 2 && ui_cfg.trig_mode == LAR2_TRIGGER_PATTERN)
        snprintf(buf, sizeof buf, "[0x%02X]/0xFF", ui_cfg.trig_value);
    else
        snprintf(buf, sizeof buf, "%s", cond_name(cond, sizeof cond));
    row(ROW_Y0 + 2 * ROW_H, "Dieu kien", buf,
        trig_row == 2, trig_edit && trig_row == 2, true);

    bool uart = ui_cfg.trig_mode == LAR2_TRIGGER_UART;
    row(ROW_Y0 + 3 * ROW_H, "Baud", baud_lbl[baud_idx],
        trig_row == 3, trig_edit && trig_row == 3, uart);

    text_center(HINT_Y, "UP/DN  OK SUA  BACK", C_DIM);
}

/* ---------- screen 3: waveform ---------- */
static uint32_t visible_samples(void);

static unsigned sample_bit(uint32_t i, unsigned ch)
{
    if (!cap_samples || i >= cap_len) return 0;
    return (cap_samples[i] >> ch) & 1u;
}

static void draw_track(unsigned ch, int y)
{
    uint16_t color = C_CH[ch & 3u];
    uint32_t visible = cap_len ? cap_len / zoom : 0;
    if (visible > cap_len) visible = cap_len;
    uint32_t start = wave_t;
    if (start + visible > cap_len) start = cap_len > visible ? cap_len - visible : 0;
    uint32_t span = WAVE_W ? visible / WAVE_W : 0;
    if (!span) span = 1;

    const int hi = y + 6, lo = y + 18;
    fill(WAVE_X0, y, WAVE_W, WAVE_TRACK_H - 1, C_BG);

    char label[4];
    snprintf(label, sizeof label, "%u", ch);

    for (int col = 0; col < WAVE_W; col++) {
        uint32_t a = start + (uint32_t)col * span;
        if (a >= cap_len) break;
        uint32_t b = a + span;
        if (b > cap_len) b = cap_len;
        unsigned level = sample_bit(a, ch);
        bool edge = false;
        for (uint32_t i = a + 1; i < b && !edge; i++)
            if (sample_bit(i, ch) != level) edge = true;
        int x = WAVE_X0 + col;
        if (edge) {
            vline(x, hi, lo - hi + 1, color);
        } else {
            fill(x, level ? hi : lo, 1, 2, color);
        }
    }
    hline(WAVE_X0, y + WAVE_TRACK_H - 1, WAVE_W, C_BROWSE);
    text(2, y + 8, label, color);
}

static void draw_wave(void)
{
    char buf[24];
    snprintf(buf, sizeof buf, "3/4 DANG SONG [%s]",
             rate_lbl[rate_idx]);
    header(buf);
    fill(0, HDR_H, SCR_W, SCR_H - HDR_H, C_BG);

    unsigned first = wave_page ? 4 : 0;
    snprintf(buf, sizeof buf, "PG%u/2 CH%u-%u  Z:X%u", wave_page + 1,
             first, first + 3, zoom);
    text(4, 17, buf, C_DIM);

    for (unsigned i = 0; i < 4; i++)
        draw_track(first + i, WAVE_TOP + (int)i * WAVE_TRACK_H);

    /* Measurement cursor: fixed at the centre so scrolling moves the data
     * under it, which is how a handheld LA is normally used. */
    int cx = WAVE_X0 + WAVE_W / 2;
    for (int y = WAVE_TOP; y < WAVE_TOP + 4 * WAVE_TRACK_H; y += 4) {
        vline(cx, y, 2, C_CURSOR);
    }

    /* Trigger marker (red) when it falls inside the visible window. */
    uint32_t visible = visible_samples();
    uint32_t span = visible / WAVE_W;
    if (!span) span = 1;
    if (cap_trigger >= wave_t && cap_trigger < wave_t + visible) {
        int tx = WAVE_X0 + (int)((cap_trigger - wave_t) / span);
        if (tx < SCR_W) vline(tx, WAVE_TOP, 4 * WAVE_TRACK_H, C_TRIG);
    }

    fill(0, BOTTOM_Y, SCR_W, 12, C_HDR);
    uint32_t tpos = wave_t + visible / 2;
    snprintf(buf, sizeof buf, "T:%u", (unsigned)tpos);
    text(4, BOTTOM_Y + 2, buf, C_TEXT);
    text_right(SCR_W - 4, BOTTOM_Y + 2, capturing ? "RUN" : (cap_ok ? "STOP" : "FAIL"),
               capturing ? C_EDIT_T : C_TEXT);
    text_center(HINT_Y, "OK MA   BACK VE", C_DIM);
}

/* ---------- screen 4: decode ---------- */
static void run_decode(void)
{
    dec_text[0] = '\0';
    if (!cap_samples || !cap_len) {
        snprintf(dec_text, sizeof dec_text, "NO CAPTURE. QUAY LAI MAN 1 VA BAT DAU THU.");
        return;
    }
    la_text t;
    la_text_init(&t, dec_text, sizeof dec_text);
    decoders_run_map(cap_samples, cap_len, (uint32_t)cap_rate, proto_mask[proto_sel],
                     bauds[baud_idx], 250000, &la_map_test8, &t);
}

/* Split the decoder output into 21-character lines counted from `skip`. */
static void draw_decode_lines(int y, uint32_t skip, int rows)
{
    const char *p = dec_text;
    uint32_t line = 0;
    int drawn = 0;
    char buf[24];
    /* Keep the newlines the decoders emit; wrap hard at 21 characters. */
    while (*p && drawn < rows) {
        size_t n = 0;
        while (*p && *p != '\n' && n < 21) buf[n++] = *p++;
        if (*p == '\n') p++;
        else if (*p && n == 21) {
            /* long line continues on the next row */
        }
        buf[n] = '\0';
        if (line >= skip) {
            text(4, y + drawn * 8, buf, C_TEXT);
            drawn++;
        }
        line++;
    }
}

static void draw_decode(void)
{
    char buf[24];
    header("4/4 GIAI MA BUS");
    fill(0, HDR_H, SCR_W, SCR_H - HDR_H, C_BG);

    if (dec_focus == 0 && dec_edit) fill(0, 18, SCR_W, ROW_BOX, C_EDIT);
    if (dec_focus == 1 && dec_edit) fill(0, 30, SCR_W, ROW_BOX, C_EDIT);
    if (dec_focus == 0) vline(0, 18, ROW_BOX, C_ACC);
    if (dec_focus == 1) vline(0, 30, ROW_BOX, C_ACC);

    snprintf(buf, sizeof buf, "< %s >", proto_name[proto_sel]);
    text(7, 20, "Chuan", C_DIM);
    text_right(SCR_W - 4, 20, buf, dec_edit && dec_focus == 0 ? C_EDIT_T : C_ACC);
    text(7, 32, "Cau hinh", C_DIM);
    text_right(SCR_W - 4, 32, bus_config(buf, sizeof buf),
               dec_edit && dec_focus == 1 ? C_EDIT_T : C_TEXT);

    hline(0, 44, SCR_W, C_BROWSE);
    draw_decode_lines(48, dec_scroll, 13);
    text_center(HINT_Y, "OK CHON  DN CUON", C_DIM);
}

/* ---------- screen switching ---------- */
static void refresh(void)
{
    switch (screen) {
    case SCR_CONFIG: draw_config(); break;
    case SCR_TRIGGER: draw_trigger(); break;
    case SCR_WAVE: draw_wave(); break;
    case SCR_DECODE: draw_decode(); break;
    }
    flush();
}

static void enter(ui_screen s)
{
    screen = s;
    switch (s) {
    case SCR_CONFIG: cfg_edit = false; cfg_row = 0; break;
    case SCR_TRIGGER: trig_edit = false; trig_row = 0; break;
    case SCR_DECODE: dec_edit = false; dec_focus = 0; run_decode(); break;
    default: break;
    }
    refresh();
}

/* ---------- value editing ---------- */
static void cycle_config(signed dir)
{
    switch (cfg_row) {
    case 0: rate_idx = (uint8_t)((rate_idx + dir + RATE_N) % RATE_N); break;
    case 1:
        count_idx = (uint8_t)((count_idx + dir + COUNT_N) % COUNT_N);
        ui_cfg.count = counts[count_idx];
        break;
    case 2: mask_idx = (uint8_t)((mask_idx + dir + MASK_N) % MASK_N); break;
    case 3: pre_idx = (uint8_t)((pre_idx + dir + PRE_N) % PRE_N); break;
    default: break;
    }
    apply_config();
    ui_cfg.pretrigger = pretrigger_samples();
}

static void cycle_trigger(signed dir)
{
    switch (trig_row) {
    case 0: {
        static const uint8_t order[] = {
            LAR2_TRIGGER_EDGE, LAR2_TRIGGER_PATTERN, LAR2_TRIGGER_UART
        };
        int i = 0;
        for (int k = 0; k < 3; k++) if (order[k] == ui_cfg.trig_mode) i = k;
        i = (i + dir + 3) % 3;
        ui_cfg.trig_mode = order[i];
        break;
    }
    case 1:
        ui_cfg.trig_channel = (uint8_t)((ui_cfg.trig_channel + dir + 8) % 8);
        break;
    case 2:
        if (ui_cfg.trig_mode == LAR2_TRIGGER_EDGE)
            ui_cfg.trig_edge = ui_cfg.trig_edge == LAR2_EDGE_FALLING
                                   ? LAR2_EDGE_RISING : LAR2_EDGE_FALLING;
        else
            ui_cfg.trig_value = (uint8_t)(ui_cfg.trig_value + dir);
        break;
    case 3:
        baud_idx = (uint8_t)((baud_idx + dir + BAUD_N) % BAUD_N);
        break;
    default: break;
    }
}

static void cycle_decode(signed dir)
{
    if (dec_focus == 0) {
        proto_sel = (uint8_t)((proto_sel + dir + 4) % 4);
        run_decode();
        dec_scroll = 0;
    } else if (proto_sel == 0) {
        baud_idx = (uint8_t)((baud_idx + dir + BAUD_N) % BAUD_N);
        run_decode();
        dec_scroll = 0;
    }
}

/* ---------- waveform scrolling ---------- */
static uint32_t visible_samples(void)
{
    uint32_t v = cap_len ? cap_len / zoom : 0;
    return v > cap_len ? cap_len : v;
}

static void wave_scroll(signed dir)
{
    uint32_t visible = visible_samples();
    uint32_t step = visible / 8;
    if (!step) step = 1;
    uint32_t max = cap_len > visible ? cap_len - visible : 0;
    if (dir > 0) wave_t = (wave_t + step > max) ? max : wave_t + step;
    else wave_t = (wave_t > step) ? wave_t - step : 0;
}

/* ---------- event dispatch ---------- */
static void event(btn_id id, bool long_press)
{
    if (event_count != 255) event_count++;

    switch (screen) {
    case SCR_CONFIG:
        if (id == BTN_UP || id == BTN_DOWN) {
            signed dir = id == BTN_UP ? -1 : +1;
            if (cfg_edit) cycle_config(dir);
            else {
                cfg_row = (uint8_t)((cfg_row + dir + 5) % 5);
                if (cfg_row == 4) cfg_edit = false;
            }
            refresh();
        } else if (id == BTN_OK) {
            if (cfg_row == 4) {
                if (!capturing && !capture_request) {
                    apply_config();
                    ui_cfg.pretrigger = pretrigger_samples();
                    capture_request = true;
                    capturing = true;
                    cap_ok = true;
                    cap_trigger = 0;
                    wave_t = 0;
                    zoom = 1;
                    wave_page = 0;
                    enter(SCR_WAVE);
                }
            } else {
                cfg_edit = !cfg_edit;
                refresh();
            }
        } else {
            if (cfg_edit) { cfg_edit = false; refresh(); }
            else enter(SCR_TRIGGER);
        }
        break;

    case SCR_TRIGGER:
        if (id == BTN_UP || id == BTN_DOWN) {
            signed dir = id == BTN_UP ? -1 : +1;
            if (trig_edit) cycle_trigger(dir);
            else trig_row = (uint8_t)((trig_row + dir + 4) % 4);
            refresh();
        } else if (id == BTN_OK) {
            trig_edit = !trig_edit;
            refresh();
        } else {
            if (trig_edit) { trig_edit = false; refresh(); }
            else enter(SCR_CONFIG);
        }
        break;

    case SCR_WAVE:
        if (id == BTN_UP || id == BTN_DOWN) {
            bool up = id == BTN_UP;
            if (long_press && up) {
                wave_page ^= 1;
            } else if (long_press) {
                zoom = (uint8_t)(zoom >= 8 ? 1 : zoom * 2);
                wave_t = 0;
            } else {
                wave_scroll(up ? -1 : +1);
            }
            refresh();
        } else if (id == BTN_OK) {
            enter(SCR_DECODE);
        } else {
            enter(SCR_CONFIG);
        }
        break;

    case SCR_DECODE:
        if (id == BTN_UP || id == BTN_DOWN) {
            signed dir = id == BTN_UP ? -1 : +1;
            if (dec_edit) cycle_decode(dir);
            else if (dir > 0) dec_scroll++;
            else if (dec_scroll) dec_scroll--;
            refresh();
        } else if (id == BTN_OK) {
            if (long_press) {
                dec_focus ^= 1;
                dec_edit = false;
            } else if (dec_edit) {
                dec_edit = false;
                run_decode();
                dec_scroll = 0;
            } else {
                dec_edit = true;
            }
            refresh();
        } else {
            if (dec_edit) { dec_edit = false; refresh(); }
            else enter(SCR_WAVE);
        }
        break;
    }
}

/* ---------- demo data ---------- */
/* 40000 samples at 25 MHz: a real UART "HELLO" frame on CH0 (so the decoder
 * screen has something to show) and square waves on CH1..CH7. This lets the
 * whole UI be exercised with no signal generator attached. */
#define DEMO_LEN 40000
static uint8_t demo_buf[DEMO_LEN];

static void demo_build(void)
{
    const uint32_t bit = 25000000u / 115200u;      /* 217 samples */
    memset(demo_buf, 0x01, sizeof demo_buf);       /* CH0 idle high */
    uint32_t pos = 2000;
    const char *msg = "HELLO";
    for (const char *p = msg; *p; p++) {
        for (uint32_t j = 0; j < bit && pos < DEMO_LEN; j++, pos++)
            demo_buf[pos] &= (uint8_t)~0x01u;      /* start bit */
        for (int k = 0; k < 8; k++) {
            bool one = (bool)((*p >> k) & 1);
            for (uint32_t j = 0; j < bit && pos < DEMO_LEN; j++, pos++) {
                if (one) demo_buf[pos] |= 0x01u;
                else demo_buf[pos] &= (uint8_t)~0x01u;
            }
        }
        for (uint32_t j = 0; j < bit && pos < DEMO_LEN; j++, pos++)
            demo_buf[pos] |= 0x01u;                /* stop bit */
    }
    for (uint32_t i = 0; i < DEMO_LEN; i++) {
        if ((i / 12500u) & 1u) demo_buf[i] |= 0x02u;   /* 1 kHz */
        if ((i / 1250u) & 1u)  demo_buf[i] |= 0x04u;   /* 10 kHz */
        if ((i / 125u) & 1u)   demo_buf[i] |= 0x08u;   /* 100 kHz */
        if ((i / 12u) & 1u)    demo_buf[i] |= 0x10u;   /* ~1 MHz */
        if ((i % 1000u) < 300u) demo_buf[i] |= 0x20u;  /* pulse train */
        if ((i / 62u) & 1u)    demo_buf[i] |= 0x40u;   /* ~200 kHz */
        if ((i % 5000u) < 2500u) demo_buf[i] |= 0x80u; /* 2.5 kHz */
    }
}

void ui_load_demo(void)
{
    demo_build();
    capturing = false;
    cap_samples = demo_buf;
    cap_len = DEMO_LEN;
    cap_rate = 25000000;
    cap_trigger = 2000;
    cap_ok = true;
    rate_idx = (uint8_t)(RATE_N - 1);              /* 25 MHz */
    count_idx = 0;                                  /* 16K view of the demo */
    apply_config();
    ui_cfg.pretrigger = pretrigger_samples();
    wave_t = 0;
    zoom = 1;
    wave_page = 0;
    dec_scroll = 0;
    enter(SCR_WAVE);
}

/* ---------- public API ---------- */
void ui_init(void)
{
    rate_idx = (uint8_t)(RATE_N - 1);               /* 25 MHz */
    count_idx = 3;                                  /* 262K */
    mask_idx = 0;                                   /* CH 0-7 */
    pre_idx = 1;                                    /* 25 % */
    baud_idx = 2;                                   /* 115200 */
    zoom = 1;
    proto_sel = 0;
    cap_ok = true;
    cfg_row = 0;
    trig_row = 0;

    ui_cfg.rate = rates[rate_idx];
    ui_cfg.count = counts[count_idx];
    ui_cfg.mask = masks[mask_idx];
    ui_cfg.pretrigger = pretrigger_samples();
    ui_cfg.trig_mode = LAR2_TRIGGER_EDGE;
    ui_cfg.trig_channel = 0;
    ui_cfg.trig_edge = LAR2_EDGE_RISING;
    ui_cfg.trig_value = 0x55;
    ui_cfg.trig_mask = 0xFF;

    buttons_init();
    enter(SCR_CONFIG);
}

void ui_tick(void)
{
    buttons_update();
    btn_event ev;
    while (buttons_get_event(&ev)) event(ev.id, ev.long_press);
}

uint8_t ui_screen_id(void)
{
    return (uint8_t)screen;
}

uint8_t ui_event_count(void)
{
    return event_count;
}

void ui_handle_command(char c)
{
    switch (c) {
    case 'u': buttons_inject(BTN_UP, false); break;
    case 'U': buttons_inject(BTN_UP, true); break;
    case 'd': buttons_inject(BTN_DOWN, false); break;
    case 'D': buttons_inject(BTN_DOWN, true); break;
    case 'o': buttons_inject(BTN_OK, false); break;
    case 'O': buttons_inject(BTN_OK, true); break;
    case 'b': buttons_inject(BTN_BACK, false); break;
    case 'B': buttons_inject(BTN_BACK, true); break;
    default: break;
    }
}

bool ui_take_capture_request(void)
{
    if (!capture_request) return false;
    capture_request = false;
    return true;
}

void ui_set_capture(const uint8_t *samples, uint32_t len, uint64_t rate,
                    uint32_t trigger_index, bool ok)
{
    capturing = false;
    cap_samples = samples;
    cap_len = len;
    cap_rate = rate;
    cap_trigger = trigger_index;
    cap_ok = ok;
    wave_t = 0;
    dec_scroll = 0;
    if (screen == SCR_WAVE) refresh();
}
