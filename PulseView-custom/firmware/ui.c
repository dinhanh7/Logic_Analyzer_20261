/* SPDX-License-Identifier: GPL-3.0-or-later
 * 128x160 ST7735 UI: four screens, dirty-line redraw, no framebuffer.
 *
 * Rendering: every text line is composed into a 128x8 RGB565 band and flushed
 * with one tft_blit(), so a line is never shown half drawn -> no flicker. The
 * screen background is painted once on entry; after that only changed lines
 * (or waveform tracks) are rewritten.
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

#define C_BG     RGB565(0x0D, 0x13, 0x1A)
#define C_HEADER RGB565(0x2B, 0x7B, 0xB9)
#define C_SEL    RGB565(0x45, 0xB3, 0x76)
#define C_TEXT   RGB565(0xE0, 0xE6, 0xED)
#define C_FOOT   RGB565(0x7A, 0x86, 0x9A)

static const uint16_t C_CH[4] = {
    RGB565(0x4E, 0xA8, 0xDE), RGB565(0xF4, 0xA2, 0x61),
    RGB565(0x52, 0xB7, 0x88), RGB565(0x9D, 0x4E, 0xDD)
};

/* Fixed layout. Text rows are 8 px tall, menus use 16 px spacing. */
#define ROW_HEADER  2
#define ROW_MENU    24
#define ROW_FOOTER  148
#define MENU_LINES  5
#define WAVE_TOP    28
#define WAVE_TRACK_H 22
#define WAVE_X0     24
#define WAVE_W      (TFT_WIDTH - WAVE_X0)

#define UI_CAN_BAUD 250000u

typedef enum { SCR_CAPTURE, SCR_TRIGGER, SCR_WAVE, SCR_DECODE } ui_screen;

ui_config ui_cfg;

static ui_screen screen;
static uint8_t sel;
static bool editing;
static uint8_t pat_field;    /* 0 = value, 1 = mask */
static uint8_t wave_page;
static uint8_t zoom;         /* 1, 2, 4, 8 */
static uint32_t wave_t;
static uint8_t proto_sel;
static bool capturing;

static const uint8_t *cap_samples;
static uint32_t cap_len;
static uint64_t cap_rate;
static bool cap_ok;

static bool capture_request;

static char dec_text[512];

static const uint64_t rates[LAR2_RATE_COUNT] = {
    1000, 10000, 100000, 250000, 500000, 1000000, 5000000, 25000000
};
static const uint32_t counts[] = { 1000, 10000, 40000, 65536, 131072, 262144 };
static const uint32_t pres[] = { 0, 1000, 2000, 8192, 16384, 32768, 65536 };
static const uint8_t masks[] = { 0xFF, 0x0F, 0x03, 0x01 };

/* ---------- band canvas ---------- */
static uint16_t band[TFT_WIDTH * FONT6X8_H];

static void band_fill(uint16_t color)
{
    for (unsigned i = 0; i < TFT_WIDTH * FONT6X8_H; i++) band[i] = color;
}

static void band_glyph(int x, unsigned ch, uint16_t fg, uint16_t bg)
{
    if (ch < FONT6X8_FIRST || ch >= FONT6X8_FIRST + FONT6X8_COUNT) ch = '?';
    const uint8_t *g = font6x8[ch - FONT6X8_FIRST];
    for (int r = 0; r < FONT6X8_H; r++) {
        uint16_t *dst = band + r * TFT_WIDTH;
        for (int c = 0; c < FONT6X8_W; c++) {
            int px = x + c;
            if (px < 0 || px >= TFT_WIDTH) continue;
            dst[px] = (g[r] >> c) & 1u ? fg : bg;
        }
    }
}

static void band_str(int x, const char *s, uint16_t fg, uint16_t bg)
{
    for (; *s; s++, x += FONT6X8_W) band_glyph(x, (unsigned char)*s, fg, bg);
}

static void band_flush(int y, int h)
{
    tft_blit(0, y, TFT_WIDTH, h, band);
}

/* One text line at (2, y). */
static void draw_line(int y, const char *text, uint16_t fg)
{
    band_fill(C_BG);
    band_str(2, text, fg, C_BG);
    band_flush(y, FONT6X8_H);
}

/* Menu row: optional '>' cursor, label and value, bracket while editing. */
static void draw_row(int y, bool selected, bool edit, const char *label,
                     const char *value, uint16_t value_fg)
{
    char line[32];
    band_fill(C_BG);
    if (selected) band_str(0, ">", C_SEL, C_BG);
    uint16_t label_fg = selected ? C_SEL : C_TEXT;
    band_str(8, label, label_fg, C_BG);
    int x = 8 + FONT6X8_W * (int)strlen(label);
    if (edit) {
        snprintf(line, sizeof(line), "[%s]", value);
        band_str(x, line, C_SEL, C_BG);
    } else {
        band_str(x, value, selected ? C_SEL : value_fg, C_BG);
    }
    band_flush(y, FONT6X8_H);
}

/* ---------- formatting ---------- */
static void fmt_rate(char *out, size_t n)
{
    if (ui_cfg.rate >= 1000000)
        snprintf(out, n, "%u MHZ", (unsigned)(ui_cfg.rate / 1000000u));
    else
        snprintf(out, n, "%u KHZ", (unsigned)(ui_cfg.rate / 1000u));
}

static void fmt_rate_short(char *out, size_t n)
{
    if (ui_cfg.rate >= 1000000)
        snprintf(out, n, "%uM", (unsigned)(ui_cfg.rate / 1000000u));
    else
        snprintf(out, n, "%uK", (unsigned)(ui_cfg.rate / 1000u));
}

static void fmt_channels(char *out, size_t n)
{
    switch (ui_cfg.mask) {
    case 0xFF: snprintf(out, n, "0-7"); break;
    case 0x0F: snprintf(out, n, "0-3"); break;
    case 0x03: snprintf(out, n, "0-1"); break;
    default:   snprintf(out, n, "0"); break;
    }
}

static const char *mode_name(void)
{
    switch (ui_cfg.trig_mode) {
    case LAR2_TRIGGER_EDGE: return "EDGE";
    case LAR2_TRIGGER_PATTERN: return "PATTERN";
    case LAR2_TRIGGER_UART: return "UART";
    default: return "MANUAL";
    }
}

/* ---------- screen 01: capture config ---------- */
static void draw_capture(void)
{
    char v[24];
    draw_line(ROW_HEADER, "CAU HINH", C_HEADER);

    fmt_rate(v, sizeof(v));
    draw_row(ROW_MENU + 0 * 16, sel == 0, editing && sel == 0, "FS: ", v, C_TEXT);
    snprintf(v, sizeof(v), "%u", (unsigned)ui_cfg.count);
    draw_row(ROW_MENU + 1 * 16, sel == 1, editing && sel == 1, "N: ", v, C_TEXT);
    char ch[8];
    fmt_channels(ch, sizeof(ch));
    draw_row(ROW_MENU + 2 * 16, sel == 2, false, "CH: ", ch, C_TEXT);
    snprintf(v, sizeof(v), "%u", (unsigned)ui_cfg.pretrigger);
    draw_row(ROW_MENU + 3 * 16, sel == 3, editing && sel == 3, "PRE: ", v, C_TEXT);

    band_fill(C_BG);
    band_str(8, "[ BAT DAU ]", sel == 4 ? C_SEL : C_TEXT, C_BG);
    band_flush(ROW_MENU + 4 * 16, FONT6X8_H);

    draw_line(ROW_FOOTER, "UP/DN OK BACK", C_FOOT);
}

/* ---------- screen 02: trigger config ---------- */
static void draw_trigger(void)
{
    char v[24];
    draw_line(ROW_HEADER, "TRIGGER", C_HEADER);

    draw_row(ROW_MENU + 0 * 16, sel == 0, editing && sel == 0, "MODE: ", mode_name(), C_TEXT);
    snprintf(v, sizeof(v), "%u", ui_cfg.trig_channel);
    draw_row(ROW_MENU + 1 * 16, sel == 1, editing && sel == 1, "CH: ", v, C_TEXT);
    draw_row(ROW_MENU + 2 * 16, sel == 2, editing && sel == 2, "CANH: ",
             ui_cfg.trig_edge == LAR2_EDGE_FALLING ? "FALLING" : "RISING", C_TEXT);

    /* Pattern row: the active half is bracketed while editing. */
    if (editing && sel == 3)
        snprintf(v, sizeof(v), pat_field == 0 ? "[%02X]/%02X" : "%02X/[%02X]",
                 ui_cfg.trig_value, ui_cfg.trig_mask);
    else
        snprintf(v, sizeof(v), "%02X/%02X", ui_cfg.trig_value, ui_cfg.trig_mask);
    draw_row(ROW_MENU + 3 * 16, sel == 3, false, "PATTERN: ", v, C_TEXT);

    if (ui_cfg.trig_value == 0x4C)
        draw_row(ROW_MENU + 4 * 16, sel == 4, false, "UART: ", "AUTO", C_TEXT);
    else {
        snprintf(v, sizeof(v), "0X%02X", ui_cfg.trig_value);
        draw_row(ROW_MENU + 4 * 16, sel == 4, editing && sel == 4, "UART: ", v, C_TEXT);
    }

    draw_line(ROW_FOOTER, "UP/DN OK BACK", C_FOOT);
}

/* ---------- screen 03: waveform ---------- */
static unsigned sample_bit_at(uint32_t i, unsigned ch)
{
    if (!cap_samples || i >= cap_len) return 0;
    return (cap_samples[i] >> ch) & 1u;
}

/* Compose one 22 px track. High level sits on rows 6-7, low on 14-15, and a
 * column that contains a transition draws the vertical connector. */
static void draw_track(unsigned ch, int y, uint16_t color)
{
    uint32_t visible = cap_len ? cap_len / zoom : 0;
    if (!cap_samples || !visible) {
        band_fill(C_BG);
        band_flush(y + 6, 2);
        return;
    }
    uint32_t start = wave_t;
    if (start + visible > cap_len) start = cap_len > visible ? cap_len - visible : 0;
    uint32_t span = visible / WAVE_W;
    if (!span) span = 1;

    for (int half = 0; half < 2; half++) {
        band_fill(C_BG);
        int base = half * 8;
        for (int col = 0; col < WAVE_W; col++) {
            uint32_t lo = start + (uint32_t)col * span;
            if (lo >= cap_len) break;
            uint32_t hi = lo + span;
            if (hi > cap_len) hi = cap_len;
            unsigned level = sample_bit_at(lo, ch);
            bool edge = false;
            for (uint32_t i = lo + 1; i < hi && !edge; i++)
                if (sample_bit_at(i, ch) != level) edge = true;

            int px = WAVE_X0 + col;
            for (int r = 0; r < 8; r++) {
                int row = base + r;
                bool on;
                if (edge) on = (row >= 6 && row <= 15);
                else if (level) on = (row == 6 || row == 7);
                else on = (row == 14 || row == 15);
                if (on) band[r * TFT_WIDTH + px] = color;
            }
        }
        band_flush(y + half * 8, 8);
    }

    /* Label last so it is not covered by the waveform bands. */
    char label[8];
    snprintf(label, sizeof(label), "CH%u", ch);
    band_fill(C_BG);
    band_str(1, label, color, C_BG);
    band_flush(y + 7, FONT6X8_H);
}

static void draw_wave(void)
{
    char h[24], sub[24], st[24];
    char fs[8];
    fmt_rate_short(fs, sizeof(fs));
    snprintf(h, sizeof(h), "%s FS:%s", capturing ? "RUN" : (cap_ok ? "STOP" : "FAIL"), fs);
    draw_line(ROW_HEADER, h, capturing ? C_SEL : C_HEADER);

    unsigned first = wave_page ? 4 : 0;
    snprintf(sub, sizeof(sub), "CH%u-%u PAGE %u/2", first, first + 3, wave_page + 1);
    draw_line(16, sub, C_FOOT);

    for (unsigned i = 0; i < 4; i++)
        draw_track(first + i, WAVE_TOP + (int)i * WAVE_TRACK_H, C_CH[i]);

    snprintf(st, sizeof(st), "T:%u ZOOM:X%u", (unsigned)wave_t, zoom);
    draw_line(120, st, C_FOOT);
    draw_line(ROW_FOOTER, "LEFT/RIGHT BACK", C_FOOT);
}

/* ---------- screen 04: decode ---------- */
static const uint8_t proto_mask[4] = {
    LAR2_PROTO_UART, LAR2_PROTO_I2C, LAR2_PROTO_SPI, LAR2_PROTO_CAN
};
static const char *proto_name[4] = { "UART", "I2C", "SPI", "CAN" };
static const char *proto_pins[4] = {
    "UART RX: CH0", "I2C SCL: CH1", "SPI SCK: CH4", "CAN RX: CH7"
};

static void run_decode(void)
{
    dec_text[0] = '\0';
    if (!cap_samples || !cap_len) {
        snprintf(dec_text, sizeof(dec_text), "NO CAPTURE. BACK, THEN\n[BAT DAU].");
        return;
    }
    la_text t;
    la_text_init(&t, dec_text, sizeof(dec_text));
    decoders_run_map(cap_samples, cap_len, (uint32_t)cap_rate, proto_mask[proto_sel],
                     0, UI_CAN_BAUD, &la_map_test8, &t);
}

/* Draw uppercase text wrapped at 21 characters, max `max_lines` rows. */
static void draw_text_block(int y, const char *text, uint16_t fg, int max_lines)
{
    const char *p = text;
    for (int i = 0; i < max_lines && *p; i++) {
        char line[24];
        size_t n = 0;
        while (*p && *p != '\n' && n < 21) {
            char c = *p++;
            if (c >= 'a' && c <= 'z') c = (char)(c - 32);
            line[n++] = c;
        }
        if (*p == '\n') p++;
        line[n] = '\0';
        draw_line(y + i * 10, line, fg);
    }
}

static void draw_decode(void)
{
    draw_line(ROW_HEADER, "DECODE", C_HEADER);

    /* 2x2 protocol grid: UART I2C on one row, SPI CAN on the next. */
    for (int row = 0; row < 2; row++) {
        band_fill(C_BG);
        for (int col = 0; col < 2; col++) {
            int idx = row * 2 + col;
            int x = col ? 68 : 8;
            bool on = idx == proto_sel;
            if (on) band_str(x - 8, ">", C_SEL, C_BG);
            band_str(x, proto_name[idx], on ? C_SEL : C_TEXT, C_BG);
        }
        band_flush(22 + row * 14, FONT6X8_H);
    }

    draw_line(56, proto_pins[proto_sel], C_TEXT);
    draw_line(66, proto_sel == 0 ? "BAUD: AUTO" : "CH MAP: 8CH", C_FOOT);
    draw_line(80, "DATA:", C_TEXT);
    draw_text_block(92, dec_text, C_TEXT, 5);
    draw_line(ROW_FOOTER, "CHON BUS OK", C_FOOT);
}

/* ---------- screen switching ---------- */
static void refresh(void)
{
    switch (screen) {
    case SCR_CAPTURE: draw_capture(); break;
    case SCR_TRIGGER: draw_trigger(); break;
    case SCR_WAVE:    draw_wave(); break;
    case SCR_DECODE:  draw_decode(); break;
    }
}

static void enter(ui_screen s)
{
    screen = s;
    editing = false;
    sel = 0;
    tft_fill_color(C_BG);
    if (s == SCR_DECODE) run_decode();
    refresh();
}

/* ---------- value cycling ---------- */
static int index_of_u64(const uint64_t *tab, int n, uint64_t v)
{
    for (int i = 0; i < n; i++) if (tab[i] == v) return i;
    return 0;
}

static int index_of_u32(const uint32_t *tab, int n, uint32_t v)
{
    for (int i = 0; i < n; i++) if (tab[i] == v) return i;
    return 0;
}

static void cycle_capture_value(signed dir)
{
    switch (sel) {
    case 0: {
        int i = index_of_u64(rates, LAR2_RATE_COUNT, ui_cfg.rate);
        i = (i + dir + LAR2_RATE_COUNT) % LAR2_RATE_COUNT;
        ui_cfg.rate = rates[i];
        break;
    }
    case 1: {
        int n = (int)(sizeof(counts) / sizeof(counts[0]));
        int i = index_of_u32(counts, n, ui_cfg.count);
        i = (i + dir + n) % n;
        ui_cfg.count = counts[i];
        if (ui_cfg.pretrigger >= ui_cfg.count)
            ui_cfg.pretrigger = ui_cfg.count / 2;
        break;
    }
    case 2: {
        int n = (int)(sizeof(masks) / sizeof(masks[0]));
        int i = 0;
        for (int k = 0; k < n; k++) if (masks[k] == ui_cfg.mask) i = k;
        i = (i + dir + n) % n;
        ui_cfg.mask = masks[i];
        break;
    }
    case 3: {
        int n = (int)(sizeof(pres) / sizeof(pres[0]));
        int i = index_of_u32(pres, n, ui_cfg.pretrigger);
        i = (i + dir + n) % n;
        if (pres[i] >= ui_cfg.count) i = 0;
        ui_cfg.pretrigger = pres[i];
        break;
    }
    default: break;
    }
}

static void cycle_trigger_value(signed dir)
{
    switch (sel) {
    case 0: {
        /* EDGE -> PATTERN -> UART -> EDGE */
        static const uint8_t order[] = {
            LAR2_TRIGGER_EDGE, LAR2_TRIGGER_PATTERN, LAR2_TRIGGER_UART
        };
        int n = 3, i = 0;
        for (int k = 0; k < n; k++) if (order[k] == ui_cfg.trig_mode) i = k;
        i = (i + dir + n) % n;
        ui_cfg.trig_mode = order[i];
        break;
    }
    case 1:
        ui_cfg.trig_channel = (uint8_t)((ui_cfg.trig_channel + dir + 8) % 8);
        break;
    case 2:
        ui_cfg.trig_edge = ui_cfg.trig_edge == LAR2_EDGE_FALLING
                               ? LAR2_EDGE_RISING : LAR2_EDGE_FALLING;
        break;
    case 3:
        if (pat_field == 0)
            ui_cfg.trig_value = (uint8_t)(ui_cfg.trig_value + dir);
        else
            ui_cfg.trig_mask = (uint8_t)(ui_cfg.trig_mask + dir);
        break;
    case 4:
        ui_cfg.trig_value = (uint8_t)(ui_cfg.trig_value + dir);
        break;
    default: break;
    }
}

/* ---------- waveform scrolling ---------- */
static void wave_scroll(signed dir)
{
    uint32_t visible = cap_len ? cap_len / zoom : 0;
    uint32_t step = visible / 8;
    if (!step) step = 1;
    if (dir > 0) {
        uint32_t max = cap_len > visible ? cap_len - visible : 0;
        wave_t = (wave_t + step > max) ? max : wave_t + step;
    } else {
        wave_t = (wave_t > step) ? wave_t - step : 0;
    }
}

/* ---------- event dispatch ---------- */
static void event(btn_id id, bool long_press)
{
    switch (screen) {
    case SCR_CAPTURE:
        if (id == BTN_UP) {
            if (editing) cycle_capture_value(+1);
            else sel = (uint8_t)((sel + MENU_LINES - 1) % MENU_LINES);
            refresh();
        } else if (id == BTN_DOWN) {
            if (editing) cycle_capture_value(-1);
            else sel = (uint8_t)((sel + 1) % MENU_LINES);
            refresh();
        } else if (id == BTN_OK) {
            if (sel == 4) {
                if (!capturing && !capture_request) {
                    capture_request = true;
                    capturing = true;
                    enter(SCR_WAVE);
                }
            } else {
                editing = !editing;
                refresh();
            }
        } else {
            if (editing) { editing = false; refresh(); }
            else enter(SCR_TRIGGER);
        }
        break;

    case SCR_TRIGGER:
        if (id == BTN_UP) {
            if (editing) cycle_trigger_value(+1);
            else sel = (uint8_t)((sel + MENU_LINES - 1) % MENU_LINES);
            refresh();
        } else if (id == BTN_DOWN) {
            if (editing) cycle_trigger_value(-1);
            else sel = (uint8_t)((sel + 1) % MENU_LINES);
            refresh();
        } else if (id == BTN_OK) {
            if (sel == 3 && editing) pat_field ^= 1;      /* Val <-> Mask */
            else if (sel == 4 && editing) {
                /* toggle between the AUTO sync byte and an explicit byte */
                ui_cfg.trig_value = ui_cfg.trig_value == 0x4C ? 0x00 : 0x4C;
            } else {
                editing = !editing;
            }
            refresh();
        } else {
            if (editing) { editing = false; refresh(); }
            else enter(SCR_CAPTURE);
        }
        break;

    case SCR_WAVE:
        if (id == BTN_UP) {
            if (long_press) { wave_page ^= 1; }
            else wave_scroll(-1);
            refresh();
        } else if (id == BTN_DOWN) {
            if (long_press) { wave_t = 0; }
            else wave_scroll(+1);
            refresh();
        } else if (id == BTN_OK) {
            if (long_press) {
                zoom = (uint8_t)(zoom >= 8 ? 1 : zoom * 2);
                wave_t = 0;
            } else {
                wave_page ^= 1;
            }
            refresh();
        } else {
            enter(SCR_DECODE);
        }
        break;

    case SCR_DECODE:
        if (id == BTN_UP) {
            proto_sel = (uint8_t)((proto_sel + 3) % 4);
            run_decode();
            refresh();
        } else if (id == BTN_DOWN) {
            proto_sel = (uint8_t)((proto_sel + 1) % 4);
            run_decode();
            refresh();
        } else if (id == BTN_OK) {
            run_decode();
            refresh();
        } else {
            enter(SCR_WAVE);
        }
        break;
    }
}

/* ---------- public API ---------- */
void ui_init(void)
{
    ui_cfg.rate = 25000000;
    ui_cfg.count = 40000;
    ui_cfg.pretrigger = 2000;
    ui_cfg.mask = 0xFF;
    ui_cfg.trig_mode = LAR2_TRIGGER_EDGE;
    ui_cfg.trig_channel = 0;
    ui_cfg.trig_edge = LAR2_EDGE_RISING;
    ui_cfg.trig_value = 0x4C;
    ui_cfg.trig_mask = 0xFF;
    zoom = 1;
    proto_sel = 0;
    cap_ok = true;
    buttons_init();
    enter(SCR_CAPTURE);
}

void ui_tick(void)
{
    buttons_update();
    btn_event ev;
    while (buttons_get_event(&ev)) event(ev.id, ev.long_press);
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
    (void)trigger_index;
    cap_ok = ok;
    wave_t = 0;
    if (screen == SCR_WAVE) refresh();
}
