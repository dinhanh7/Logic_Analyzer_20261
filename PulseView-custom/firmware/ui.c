/* SPDX-License-Identifier: GPL-3.0-or-later
 * 128x160 ST7735 UI: six screens, full-frame framebuffer, 4-button state
 * machine.
 *
 * The panel is portrait. Everything is composed into a 128x160 RGB565
 * framebuffer held in panel byte order and pushed with one SPI DMA window
 * write, so a screen never shows a half-drawn frame: no flicker, no tearing
 * between a label and its value.
 *
 * Screens and flow (BACK leaves edit mode before it changes screen):
 *
 *   0 STANDBY --any key--> 1 CONFIG --BACK--> 2 TRIGGER --BACK--> 1 CONFIG
 *   1 CONFIG --hold OK--> 3 WAVE CH0-3 --OK--> 4 WAVE CH4-7 --OK--> 5 DECODE
 *   5 DECODE --OK held--> 0 STANDBY   (BACK still walks 5 -> 4 -> 3 -> 1)
 *   1 CONFIG --BACK held 800ms--> 0 STANDBY
 *
 * STANDBY is host id 5; the five original screens keep ids 0..4 so the host
 * cycle test and RUNTIME.st[30] values do not move.
 *
 * Layout: 6x8 font (21 characters per line), header bar 14 px, list rows on a
 * 22 px pitch with an 18 px highlight box, waveform tracks 24 px with a
 * time ruler and a horizontal scrollbar below them.
 */
#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "autodetect.h"
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
#define C_DIM     RGB565(0x94, 0xA3, 0xB8)
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
#define ROW_Y0 24
#define ROW_H 22
#define ROW_BOX 18
#define BOTTOM_Y 138
#define HINT_Y 152

#define WAVE_TOP 26
#define WAVE_TRACK_H 24
#define WAVE_BOT (WAVE_TOP + 4 * WAVE_TRACK_H)   /* 122: last waveform row */
#define WAVE_X0 16
#define WAVE_W (SCR_W - WAVE_X0)

/* Below the tracks: a time ruler, then a horizontal scrollbar. Both end at
 * y=137 so the status bar at BOTTOM_Y=138 is untouched.
 *
 * The ruler is an oscilloscope scale, not a chart axis: the ticks are etched in
 * glass, so they never move when the waveform pans or zooms. The numbers live
 * in the caption bar below (time at the cursor, time per division). Four
 * divisions across the plot, majors 4 px and minors 2 px under the line. */
#define RULER_Y       (WAVE_BOT + 1)     /* 123 */
#define RULER_AXIS_Y  (RULER_Y + 4)      /* 127 */
#define RULER_MAJOR   4
#define RULER_MINOR   2
#define RULER_DIVS    4
#define WSBAR_X      WAVE_X0
#define WSBAR_Y      134
#define WSBAR_W      WAVE_W
#define WSBAR_H      4


ui_config ui_cfg;

typedef enum {
    SCR_CONFIG = 0,
    SCR_TRIGGER,
    SCR_WAVE_LO,        /* CH0-3 */
    SCR_WAVE_HI,        /* CH4-7 */
    SCR_DECODE,
    SCR_STANDBY         /* boot screen ("màn 0"), host id 5 */
} ui_screen;

static ui_screen screen;

/* STANDBY prompt blink. main.c pushes a monotonic millisecond clock; keeping
 * it here (instead of calling the pico SDK) lets the host render harness link
 * the same ui.c. blink_phase starts at 0xFF so the first frame always draws
 * the prompt regardless of the clock's parity. */
static uint32_t ui_now_ms;
static uint8_t blink_phase = 0xFF;

/* Config screen: rows 0..3 are values; holding OK starts the capture. */
static uint8_t cfg_row;
static bool cfg_edit;
static uint8_t rate_idx, count_idx, mask_idx, pre_idx;

/* Trigger screen. */
static uint8_t trig_row;
static bool trig_edit;

/* Waveform screens. `zoom_idx` indexes zoom_spp, the number of samples one
 * pixel column covers. Keeping it at 8 by default is what makes an edge look
 * square: at one sample per pixel a 115200 baud bit is 27 px wide, while
 * "whole buffer in 112 px" turns every edge into a diagonal ramp. Zooming
 * keeps the sample under the centre cursor; FIT is still one long-DOWN away. */
static uint8_t zoom_idx;
/* Samples per pixel. The last entry is not a fixed zoom: ZOOM_FIT means
 * "fit the whole capture", which makes the four-protocol demo visible at
 * once. The demo opens at X16 instead, centred on the trigger, so the
 * opening view is readable rather than the whole buffer in 112 px. */
static const uint8_t zoom_spp[] = { 1, 2, 4, 8, 16, 32, 64, 128 };
#define ZOOM_FIT 0xFF
#define ZOOM_N (sizeof(zoom_spp) / sizeof(zoom_spp[0]))
static uint32_t wave_t;
typedef struct { uint32_t start; uint8_t zoom; } wave_view;
static wave_view wave_views[2];
static bool render_deferred, render_pending;
static uint32_t cap_trigger;

/* Decode screen. */
static uint8_t proto_sel;
static uint8_t dec_focus;           /* 0 = protocol, 1 = bus config */
static bool dec_edit;
static uint32_t dec_scroll;         /* first visible packet row */
static uint8_t dec_sel;             /* selected packet, for the preview */
static la_pktlist dec_pkts;
static la_detect dec_auto;          /* last AUTO result (proto + channel map) */
static la_channel_map dec_map;      /* map the current decode is using */
static uint8_t dec_proto;           /* LAR2_PROTO_* actually decoded */

static uint8_t event_count;
static uint32_t frame_version;

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
#define RATE_N (sizeof(rates) / sizeof(rates[0]))

/* Every local capture uses a trigger; expose only windows the capture engine
 * can honour rather than silently showing 262K and returning 131K. */
static const uint32_t counts[] = { 16384, 65536, 131072 };
static const char *const count_lbl[] = { "16K", "64K", "131K" };
#define COUNT_N (sizeof(counts) / sizeof(counts[0]))

static const uint8_t masks[] = { 0xFF, 0x0F };
static const char *const mask_lbl[] = { "CH 0-7", "CH 0-3" };
#define MASK_N (sizeof(masks) / sizeof(masks[0]))

static const uint8_t pre_pct[] = { 0, 25, 50 };
#define PRE_N (sizeof(pre_pct) / sizeof(pre_pct[0]))

static const uint32_t bauds[] = { 0, 9600, 115200 };
static const char *const baud_lbl[] = { "AUTO", "9600", "115200" };
#define BAUD_N (sizeof(bauds) / sizeof(bauds[0]))
static uint8_t baud_idx;

/* Index 0 is AUTO: the mask is filled in by run_decode() after detection. */
static const uint8_t proto_mask[5] = {
    0, LAR2_PROTO_UART, LAR2_PROTO_I2C, LAR2_PROTO_SPI, LAR2_PROTO_CAN
};
static const char *const proto_name[5] = { "AUTO", "UART", "I2C", "SPI", "CAN" };
#define PROTO_N 5

static const char *proto_short(uint8_t p)
{
    switch (p) {
    case LAR2_PROTO_UART: return "UART";
    case LAR2_PROTO_I2C:  return "I2C";
    case LAR2_PROTO_SPI:  return "SPI";
    case LAR2_PROTO_CAN:  return "CAN";
    default:              return "?";
    }
}

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
    uint16_t v = __builtin_bswap16(c);
    for (int r = y; r < y + h; r++)
        for (int i = x; i < x + w; i++)
            fb[r * SCR_W + i] = v;
}

static void hline(int x, int y, int w, uint16_t c) { fill(x, y, w, 1, c); }
static void vline(int x, int y, int h, uint16_t c) { fill(x, y, 1, h, c); }

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

/* Text that can never bleed into the next column: it stops at `maxw` pixels
 * and clears its own cell first, so a shorter value cannot leave the tail of
 * the previous one behind. This is the whole fix for the overlap bug. */
static void text_clip(int x, int y, int maxw, const char *s, uint16_t fg,
                      uint16_t bg)
{
    int cells = maxw / FONT6X8_W;
    int i = 0;
    fill(x, y, cells * FONT6X8_W, FONT6X8_H, bg);
    for (; s[i] && i < cells; i++)
        glyph(x + i * FONT6X8_W, y, (unsigned char)s[i], fg);
}

/* Same, right-aligned inside the same cell box. */
static void text_clip_right(int x_right, int y, int maxw, const char *s,
                            uint16_t fg, uint16_t bg)
{
    int cells = maxw / FONT6X8_W;
    int n = (int)strlen(s);
    if (n > cells) n = cells;
    int x0 = x_right - cells * FONT6X8_W;
    fill(x0, y, cells * FONT6X8_W, FONT6X8_H, bg);
    for (int i = 0; i < n; i++)
        glyph(x_right - (n - i) * FONT6X8_W, y, (unsigned char)s[i], fg);
}

static int text_w(const char *s) { return FONT6X8_W * (int)strlen(s); }

static void text_right(int x_right, int y, const char *s, uint16_t fg)
{
    text(x_right - text_w(s), y, s, fg);
}

static void text_center(int y, const char *s, uint16_t fg)
{
    text((SCR_W - text_w(s)) / 2, y, s, fg);
}

static void header(const char *title)
{
    fill(0, 0, SCR_W, HDR_H, C_HDR);
    /* 21 characters fit at 6 px each; anything longer is clipped rather than
     * run off the panel edge, which is what cut "CH0-3 [25M]" in half. */
    text_clip(4, 3, SCR_W - 8, title, C_ACC, C_HDR);
    hline(0, HDR_H, SCR_W, C_BROWSE);
}

/* One list row: highlight box + left accent bar + label/value. */
static void list_row(int y, const char *label, const char *value,
                     bool selected, bool edit, bool enabled)
{
    if (selected) {
        fill(0, y - 3, SCR_W, ROW_BOX, edit ? C_EDIT : C_BROWSE);
        vline(0, y - 3, ROW_BOX, C_ACC);
        vline(1, y - 3, ROW_BOX, C_ACC);
    }
    text(7, y, label, enabled ? C_TEXT : C_DIM);
    uint16_t vc = !enabled ? C_DIM : (edit ? C_EDIT_T : (selected ? C_ACC : C_TEXT));
    text_right(SCR_W - 4, y, value, vc);
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
    ui_cfg.pretrigger = pretrigger_samples();
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

/* ---------- screen 0: standby / boot ---------- */
/* Two interleaved square waves with a red trigger line, as the device's
 * signature. Drawn from samples, not pre-baked, so the edges stay square. */
static void logo_wave(int yhi, int ylo, unsigned shift, uint16_t color)
{
    int last = -1;
    for (int x = 4; x < 124; x++) {
        unsigned cur = (((unsigned)(x - 4) + shift) / 12u) & 1u;
        int y = cur ? yhi : ylo;
        px(x, y, color);
        px(x, y + (cur ? 1 : -1), color);   /* 2 px rail */
        if (last >= 0 && (int)cur != last)
            vline(x, yhi, ylo - yhi + 1, color);
        last = (int)cur;
    }
}

static void draw_standby(void)
{
    /* Every pixel is painted, so nothing from the previous screen survives. */
    fill(0, 0, SCR_W, SCR_H, C_BG);

    /* Logo: cyan + green square waves, y16..48, red trigger across both. */
    logo_wave(20, 30, 0, C_ACC);
    logo_wave(34, 44, 12, C_EDIT_T);
    vline(68, 16, 33, C_TRIG);
    hline(64, 16, 9, C_TRIG);               /* trigger marker on top */

    /* Device name + hardware subtitle. */
    text_center(56, "PICO-LA 8CH", C_CURSOR);
    text_center(65, "RP2350 | 100MHz", C_ACC);

    /* Info card. Colours are inline so this works before the decode-screen
     * palette constants (declared further down) come into scope. */
    fill(8, 86, 112, 27, RGB565(0x16, 0x20, 0x2C));
    frame_rect(8, 86, 112, 27, RGB565(0x33, 0x41, 0x55));
    text(14, 91, "Max Fs : 25 MSPS", C_TEXT);
    text(14, 102, "Buffer : 256K Pts", C_TEXT);

    /* Blinking ready prompt: dim on even 600 ms phases, white on odd. */
    text_center(128, "[ PRESS OK TO START ]",
                blink_phase ? C_CURSOR : C_DIM);
}

/* ---------- screen 1: capture config ---------- */
static void draw_config(void)
{
    char buf[24];
    header("1/5 CONFIG");
    fill(0, HDR_H, SCR_W, SCR_H - HDR_H, C_BG);

    list_row(ROW_Y0 + 0 * ROW_H, "Rate", rate_lbl[rate_idx],
             cfg_row == 0, cfg_edit && cfg_row == 0, true);
    list_row(ROW_Y0 + 1 * ROW_H, "Samples", count_lbl[count_idx],
             cfg_row == 1, cfg_edit && cfg_row == 1, true);
    list_row(ROW_Y0 + 2 * ROW_H, "Chan", mask_lbl[mask_idx],
             cfg_row == 2, cfg_edit && cfg_row == 2, true);
    snprintf(buf, sizeof buf, "%u%%", pre_pct[pre_idx]);
    list_row(ROW_Y0 + 3 * ROW_H, "Pre-Trig", buf,
             cfg_row == 3, cfg_edit && cfg_row == 3, true);

    /* No start row: hold OK on any row to arm a capture, short OK edits. */
    text_center(HINT_Y, cfg_edit ? "UP/DN:CHANGE OK:SAVE" : "OK:EDIT  HOLD OK:RUN", C_DIM);
}

/* ---------- screen 2: trigger ---------- */
static void draw_trigger(void)
{
    char buf[24], cond[16];
    header("2/5 TRIGGER");
    fill(0, HDR_H, SCR_W, SCR_H - HDR_H, C_BG);

    list_row(ROW_Y0 + 0 * ROW_H, "Mode", mode_name(),
             trig_row == 0, trig_edit && trig_row == 0, true);

    snprintf(buf, sizeof buf, "CH%u", ui_cfg.trig_channel);
    list_row(ROW_Y0 + 1 * ROW_H, "Chan", buf,
             trig_row == 1, trig_edit && trig_row == 1, true);

    if (trig_edit && trig_row == 2 && ui_cfg.trig_mode == LAR2_TRIGGER_PATTERN)
        snprintf(buf, sizeof buf, "[0x%02X]/0xFF", ui_cfg.trig_value);
    else
        snprintf(buf, sizeof buf, "%s", cond_name(cond, sizeof cond));
    list_row(ROW_Y0 + 2 * ROW_H, "Condition", buf,
             trig_row == 2, trig_edit && trig_row == 2, true);

    list_row(ROW_Y0 + 3 * ROW_H, "Baud/Clk", baud_lbl[baud_idx],
             trig_row == 3, trig_edit && trig_row == 3,
             ui_cfg.trig_mode == LAR2_TRIGGER_UART);

    text_center(HINT_Y, "UP/DN  OK EDIT  BACK", C_DIM);
}

/* ---------- screens 3/4: waveform ---------- */
static bool zoom_is_fit(void) { return zoom_idx == ZOOM_FIT; }

static uint32_t visible_samples(void)
{
    if (zoom_is_fit()) return cap_len ? cap_len : 1;
    uint32_t v = (uint32_t)WAVE_W * zoom_spp[zoom_idx];
    return (cap_len && v > cap_len) ? cap_len : v;
}

/* "FIT" or "X8", for the header line. */
static const char *zoom_label(char *buf, size_t n)
{
    if (zoom_is_fit()) snprintf(buf, n, "FIT");
    else snprintf(buf, n, "X%u", zoom_spp[zoom_idx]);
    return buf;
}

static unsigned sample_bit(uint32_t i, unsigned ch)
{
    if (!cap_samples || i >= cap_len) return 0;
    return (cap_samples[i] >> ch) & 1u;
}

/* One track. The horizontal run of a column sits on the level of that
 * column's first sample; a column that contains any transition gets a full
 * vertical bar instead, which is what keeps a decimated view honest. */
static void draw_track(unsigned ch, int y)
{
    uint16_t color = C_CH[ch & 3u];
    uint32_t visible = visible_samples();
    const int hi = y + 5, lo = y + 17;
    fill(WAVE_X0, y, WAVE_W, WAVE_TRACK_H - 1, C_BG);

    char label[4];
    snprintf(label, sizeof label, "%u", ch);

    for (int col = 0; col < WAVE_W; col++) {
        uint32_t a = wave_t + (uint32_t)((uint64_t)visible * (unsigned)col / WAVE_W);
        if (a >= cap_len) break;
        uint32_t b = wave_t + (uint32_t)((uint64_t)visible * (unsigned)(col + 1) / WAVE_W);
        if (b <= a) b = a + 1;
        if (b > cap_len) b = cap_len;
        unsigned level = sample_bit(a ? a - 1 : a, ch);
        bool edge = false;
        for (uint32_t i = a; i < b && !edge; i++)
            if (sample_bit(i, ch) != level) edge = true;
        int x = WAVE_X0 + col;
        if (edge) {
            fill(x, hi, 1, lo - hi + 1, color);
        } else {
            fill(x, level ? hi : lo, 1, 2, color);
        }
    }
    hline(WAVE_X0, y + WAVE_TRACK_H - 1, WAVE_W, C_BROWSE);
    text(2, y + 8, label, color);
}

static void reset_wave_views(void)
{
    for (unsigned page = 0; page < 2; page++) {
        uint32_t at = 0, shortest = UINT32_MAX;
        bool found = false;
        for (unsigned ch = page * 4; ch < page * 4 + 4 && !found; ch++) {
            uint32_t previous = 0;
            unsigned edges = 0;
            for (uint32_t i = 1; i < cap_len; i++) {
                if (sample_bit(i, ch) == sample_bit(i - 1, ch)) continue;
                if (!edges) at = i;
                else if (i - previous < shortest) shortest = i - previous;
                previous = i;
                found = true;
                if (++edges == 128) break;
            }
        }
        uint8_t z = ZOOM_FIT;
        if (found) {
            z = 0;
            if (shortest == UINT32_MAX) shortest = 96;
            while ((unsigned)z + 1 < ZOOM_N && zoom_spp[z + 1] <= shortest / 6u) z++;
        }
        uint32_t visible = z == ZOOM_FIT ? cap_len : WAVE_W * zoom_spp[z];
        if (visible > cap_len) visible = cap_len;
        uint32_t start = at > visible / 4 ? at - visible / 4 : 0;
        if (start > cap_len - visible) start = cap_len - visible;
        wave_views[page] = (wave_view){start, z};
    }
    unsigned page = screen == SCR_WAVE_HI ? 1u : 0u;
    wave_t = wave_views[page].start;
    zoom_idx = wave_views[page].zoom;
}

static const char *rate_badge(char *buf, size_t n);

/* ---------- time ruler + scrollbar (below the tracks) ---------- */

/* Compact duration, at most five characters of value: "400ns", "8.6u", "12.5m",
 * "1.25m", "2.50s". Two of these plus their labels have to fit on one 128 px
 * line at six pixels per character. */
void ui_format_time(uint64_t ns, char *buf, size_t n)
{
    if (ns < 1000) {
        snprintf(buf, n, "%uns", (unsigned)ns);
    } else if (ns < 1000000) {
        unsigned us = (unsigned)(ns / 1000);
        if (us < 10) snprintf(buf, n, "%u.%uu", us, (unsigned)((ns % 1000) / 100));
        else snprintf(buf, n, "%uu", us);
    } else if (ns < 1000000000ull) {
        unsigned ms = (unsigned)(ns / 1000000);
        unsigned frac = (unsigned)((ns % 1000000) / 1000);
        if (ms < 10) snprintf(buf, n, "%u.%02um", ms, frac / 10);
        else if (ms < 100) snprintf(buf, n, "%u.%um", ms, frac / 100);
        else snprintf(buf, n, "%um", ms);
    } else {
        unsigned sec = (unsigned)(ns / 1000000000ull);
        unsigned cs = (unsigned)((ns % 1000000000ull) / 10000000ull);
        if (sec < 10) snprintf(buf, n, "%u.%02us", sec, cs);
        else if (sec < 100) snprintf(buf, n, "%u.%us", sec, cs / 10);
        else snprintf(buf, n, "%us", sec);
    }
}

/* Signed time of `sample` relative to the trigger (sample 0 when the capture
 * was manual, which is where cap_trigger already points). */
void ui_format_offset(uint32_t sample, uint32_t ref, uint32_t rate,
                      char *buf, size_t n)
{
    char t[10];
    if (!rate) { snprintf(buf, n, "-"); return; }
    if (sample == ref) { snprintf(buf, n, "0"); return; }
    if (sample > ref) {
        ui_format_time((uint64_t)(sample - ref) * 1000000000ull / rate, t, sizeof t);
        snprintf(buf, n, "+%s", t);
    } else {
        ui_format_time((uint64_t)(ref - sample) * 1000000000ull / rate, t, sizeof t);
        snprintf(buf, n, "-%s", t);
    }
}

/* Etched scale under the tracks: one line, four divisions, five majors and a
 * minor in between. No labels on purpose; the caption bar carries the two
 * numbers that matter. */
static void draw_time_ruler(void)
{
    fill(WAVE_X0, RULER_Y, WAVE_W, RULER_AXIS_Y + RULER_MAJOR + 1 - RULER_Y, C_BG);
    if (!cap_rate || !cap_len) return;

    hline(WAVE_X0, RULER_AXIS_Y, WAVE_W, C_DIM);
    int div = WAVE_W / RULER_DIVS;
    for (int i = 0; i <= RULER_DIVS; i++) {
        int x = WAVE_X0 + i * div;
        if (x >= SCR_W) x = SCR_W - 1;
        fill(x, RULER_AXIS_Y + 1, 1, RULER_MAJOR, C_DIM);
    }
    for (int i = 0; i < RULER_DIVS; i++) {
        int x = WAVE_X0 + i * div + div / 2;
        if (x < SCR_W) fill(x, RULER_AXIS_Y + 1, 1, RULER_MINOR, C_DIM);
    }
}

/* Horizontal pan indicator: thumb length = window/buffer, position = view. */
static void draw_scrollbar(void)
{
    fill(WSBAR_X, WSBAR_Y, WSBAR_W, WSBAR_H, C_BROWSE);
    if (!cap_len) return;
    uint32_t visible = visible_samples();
    uint32_t vis = visible > cap_len ? cap_len : visible;
    int th = (int)((uint64_t)WSBAR_W * vis / cap_len);
    if (th < 4) th = 4;
    if (th > WSBAR_W) th = WSBAR_W;
    uint32_t max = cap_len > vis ? cap_len - vis : 0;
    int tx = WSBAR_X;
    if (max) tx += (int)((uint64_t)(WSBAR_W - th) * (wave_t > max ? max : wave_t) / max);
    fill(tx, WSBAR_Y, th, WSBAR_H, C_ACC);
}

static void draw_wave(bool high_page)
{
    char buf[24];
    unsigned first = high_page ? 4 : 0;
    /* 21 characters is the whole header; "3/5 WAVE CH0-3 25M" is exactly 20. */
    snprintf(buf, sizeof buf, "%u/5 WAVE CH%u-%u %s",
             high_page ? 4u : 3u, first, first + 3, rate_badge((char[8]){0}, 8));
    header(buf);
    fill(0, HDR_H, SCR_W, SCR_H - HDR_H, C_BG);

    char zl[8];
    snprintf(buf, sizeof buf, "Z:%s", zoom_label(zl, sizeof zl));
    text(3, 17, buf, C_DIM);
    text(45, 17, "U/D:PAN", C_DIM);
    /* Capture state moved up here to leave the caption bar for the two time
     * values. */
    text_right(SCR_W - 3, 17, capturing ? "RUN" : (!cap_len ? "WAIT" : (cap_ok ? "STOP" : "FAIL")),
               capturing ? C_EDIT_T : (cap_ok ? C_DIM : C_TRIG));

    for (unsigned i = 0; i < 4; i++)
        draw_track(first + i, WAVE_TOP + (int)i * WAVE_TRACK_H);

    /* Trigger marker (red), drawn under nothing else. */
    uint32_t visible = visible_samples();
    if (cap_trigger >= wave_t && cap_trigger < wave_t + visible) {
        int tx = WAVE_X0 + (int)((uint64_t)(cap_trigger - wave_t) * WAVE_W / visible);
        if (tx < SCR_W) vline(tx, WAVE_TOP, 4 * WAVE_TRACK_H, C_TRIG);
    }

    /* Measurement cursor: fixed at the centre, the data scrolls under it. */
    int cx = WAVE_X0 + WAVE_W / 2;
    for (int y = WAVE_TOP; y < WAVE_TOP + 4 * WAVE_TRACK_H; y += 4)
        vline(cx, y, 2, C_CURSOR);

    draw_time_ruler();
    draw_scrollbar();

    /* Clean-Before-Draw: the bar is repainted every frame, so panning or
     * zooming can never leave the previous digits behind. Exactly two values:
     * the time at the centre cursor and the time covered by one division. */
    fill(0, BOTTOM_Y, SCR_W, 12, C_HDR);
    char tb[12], db[12];
    ui_format_offset(wave_t + visible / 2, cap_trigger, (uint32_t)cap_rate, tb, sizeof tb);
    snprintf(buf, sizeof buf, "T:%s", tb);
    text(3, BOTTOM_Y + 2, buf, C_TEXT);
    if (cap_rate) {
        ui_format_time((uint64_t)visible * 1000000000ull
               / ((uint64_t)cap_rate * RULER_DIVS), db, sizeof db);
        snprintf(buf, sizeof buf, "1div:%s", db);
        text_right(SCR_W - 3, BOTTOM_Y + 2, buf, C_ACC);
    }

    text_center(HINT_Y, "HOLD U/D:ZOOM OK:>", C_DIM);
}

/* ---------- screen 5: decode ----------
 * Compact instrument layout, three bands, no dead space:
 *
 *   y  0..15   header: "DECODE" left, packet-count badge right
 *   y 17..39   three selector cards: protocol | channel | rate
 *   y 41..75   mini waveform of the selected packet, with a field bar
 *   y 77..159  framed data grid: # HEX ASC DT, 5 rows, scrollbar
 *
 * Every cell is drawn with text_clip(), so a value can never run into the
 * column beside it and a shorter value clears the one it replaces.
 */
#define DEC_HDR_H   16
#define CARD_Y      17
#define CARD_H      22
#define STRIP_Y     41
#define STRIP_H     30
#define GRID_Y      73
#define GRID_HDR_H  12
#define GRID_ROW_H  13
#define GRID_ROWS   5
#define GRID_X      2
#define GRID_W      124
#define SBAR_X      126

/* Column x and width, in pixels. 6 px per character. */
#define COL_IDX_X   4
#define COL_IDX_W   18      /* 3 chars: "01" */
#define COL_HEX_X   24
#define COL_HEX_W   30      /* 5 chars: "0x48" */
#define COL_ASC_X   56
#define COL_ASC_W   36      /* 6 chars: "'H'" / "ID-hi" */
#define COL_DT_X    94
#define COL_DT_W    32      /* 5 chars: "82us" */

static const uint16_t C_CARD_BD  = RGB565(0x33, 0x41, 0x55);
static const uint16_t C_CARD_ON  = RGB565(0x02, 0x84, 0xC7);
static const uint16_t C_STRIP_BG = RGB565(0x0F, 0x17, 0x2A);
static const uint16_t C_STRIP_BD = RGB565(0x1E, 0x29, 0x3B);
static const uint16_t C_ROW_SEL  = RGB565(0x1D, 0x4E, 0xD8);
static const uint16_t C_BADGE    = RGB565(0x2D, 0xD4, 0xBF);

static void run_decode(void)
{
    dec_text[0] = '\0';
    dec_pkts.n = 0;
    dec_proto = 0;
    dec_map = la_map_test8;
    memset(&dec_auto, 0, sizeof dec_auto);
    if (!cap_samples || !cap_len) {
        snprintf(dec_text, sizeof dec_text, "NO CAPTURE. GO TO SCREEN 1.");
        return;
    }

    uint32_t uart_baud = bauds[baud_idx];
    uint32_t can_baud = 250000;

    if (proto_sel == 0) {
        /* AUTO: detect protocol + channels, then decode with that map. The
         * bit-time gate needs a concrete UART baud; when the card says AUTO
         * fall back to 115200, which is what the demo uses. */
        uint32_t detect_baud = uart_baud ? uart_baud : 115200u;
        /* The detector only looks at the first LA_AUTO_WINDOW samples. On a
         * triggered capture those can be entirely pre-trigger idle, so start
         * the scan half a window before the trigger instead. If that finds
         * nothing, try the start of the buffer before giving up. */
        uint32_t det_start = 0;
        if (cap_len > LA_AUTO_WINDOW) {
            if (cap_trigger > LA_AUTO_WINDOW / 2u)
                det_start = cap_trigger - LA_AUTO_WINDOW / 2u;
            if (det_start + LA_AUTO_WINDOW > cap_len)
                det_start = cap_len - LA_AUTO_WINDOW;
        }
        dec_auto = decoders_autodetect(cap_samples + det_start, cap_len - det_start,
                                       (uint32_t)cap_rate, detect_baud, can_baud);
        if (!dec_auto.proto && det_start)
            dec_auto = decoders_autodetect(cap_samples, cap_len, (uint32_t)cap_rate,
                                           detect_baud, can_baud);
        if (!dec_auto.proto) {
            snprintf(dec_text, sizeof dec_text, "AUTO: no bus found");
            return;
        }
        dec_proto = dec_auto.proto;
        dec_map = dec_auto.map;
    } else {
        dec_proto = proto_mask[proto_sel];
    }

    la_text t;
    la_text_init(&t, dec_text, sizeof dec_text);
    decoders_run_map(cap_samples, cap_len, (uint32_t)cap_rate, dec_proto,
                     uart_baud, can_baud, &dec_map, &t);
    decoders_packets(cap_samples, cap_len, (uint32_t)cap_rate, dec_proto,
                     uart_baud, can_baud, &dec_map, &dec_pkts);
    if (dec_sel >= dec_pkts.n) dec_sel = dec_pkts.n ? (uint8_t)(dec_pkts.n - 1) : 0;
}

/* "115200" -> "115k", "1000000" -> "1M". The card is 42 px, so 4 characters. */
static const char *rate_badge(char *buf, size_t n)
{
    uint32_t r = (uint32_t)cap_rate;
    if (!r || r > 25000000u) { snprintf(buf, n, "-"); return buf; }
    if (r >= 1000000 && r % 1000000 == 0) snprintf(buf, n, "%uM", (unsigned)(r / 1000000));
    else if (r >= 1000) snprintf(buf, n, "%uk", (unsigned)(r / 1000));
    else snprintf(buf, n, "%u", (unsigned)r);
    return buf;
}

/* The channel(s) the active protocol listens on (dynamic after AUTO). */
static const char *chan_badge(char *buf, size_t n)
{
    uint8_t p = dec_proto ? dec_proto : proto_mask[proto_sel];
    switch (p) {
    case LAR2_PROTO_UART: snprintf(buf, n, "CH:%u", dec_map.uart); break;
    case LAR2_PROTO_I2C:  snprintf(buf, n, "%u/%u", dec_map.i2c_scl, dec_map.i2c_sda); break;
    case LAR2_PROTO_SPI:  snprintf(buf, n, "%u-%u", dec_map.spi_cs, dec_map.spi_mosi); break;
    case LAR2_PROTO_CAN:  snprintf(buf, n, "CH:%u", dec_map.can); break;
    default:              snprintf(buf, n, "-"); break;
    }
    return buf;
}

/* PROTO card value: "AUTO:CAN" after a successful detect, else the fixed name. */
static const char *proto_badge(char *buf, size_t n)
{
    if (proto_sel != 0) {
        snprintf(buf, n, "%s", proto_name[proto_sel]);
        return buf;
    }
    if (dec_auto.proto) snprintf(buf, n, "AUTO:%s", proto_short(dec_auto.proto));
    else                snprintf(buf, n, "AUTO:?");
    return buf;
}

/* One selector card. Focused+editing fills it; focused only outlines it. */
static void card(int x, int w, const char *label, const char *value,
                 bool focused, bool editing)
{
    uint16_t bg = editing ? C_CARD_ON : C_HDR;
    fill(x, CARD_Y, w, CARD_H, bg);
    frame_rect(x, CARD_Y, w, CARD_H, focused ? C_ACC : C_CARD_BD);
    /* Label on top, value under it: two 8 px lines inside 22 px. */
    text_clip(x + 3, CARD_Y + 3, w - 6, label, C_DIM, bg);
    text_clip(x + 3, CARD_Y + 12, w - 6, value,
              editing ? C_TEXT : (focused ? C_ACC : C_TEXT), bg);
}

/* Mini waveform of the selected row, zoomed to that row's own bits.
 *
 * The old version squeezed the whole transaction into 118 px. A UART byte is
 * ten bit times, so at 25 MHz/115200 that is 2170 samples in 118 px: eighteen
 * samples per column, every edge a two-pixel ramp. That is the "curved" edge:
 * decimation, not the signal. Here the window is the row's own extent plus a
 * bit of air, so a bit is several pixels wide and an edge is one column.
 *
 * When a bit is still narrower than a pixel (a very fast bus), the edge is
 * drawn as a vertical line and the run is drawn at the level it holds, which
 * is the honest way to decimate: no diagonal, ever.
 */
static void draw_strip(void)
{
    fill(GRID_X, STRIP_Y, GRID_W, STRIP_H, C_STRIP_BG);
    frame_rect(GRID_X, STRIP_Y, GRID_W, STRIP_H, C_STRIP_BD);

    if (!dec_pkts.n || dec_sel >= dec_pkts.n || !cap_samples) {
        text_clip(GRID_X + 4, STRIP_Y + 12, GRID_W - 8, "no packet",
                  C_DIM, C_STRIP_BG);
        return;
    }

    const la_packet *p = &dec_pkts.pkt[dec_sel];
    uint32_t bitw = dec_pkts.bitw ? dec_pkts.bitw : 1;

    /* Window = this row, padded by one bit either side so the start and stop
     * edges are visible rather than clipped at the frame.
     *
     * Rows record the *centre* of their first and last bit, not the edge, so
     * padding from `at` directly lands half a bit inside the row and cuts off
     * the leading edge. That is why the CAN strip was missing a falling edge:
     * the SOF edge sits at 34400 while the first ID row starts at 34550, and
     * one bit of padding back only reached 34450. Snap the window out to the
     * bit grid instead, measured from the start of the transaction. */
    uint32_t at = p->at, en = p->end > p->at ? p->end : p->at + bitw;
    uint32_t grid = dec_pkts.win_at;
    uint32_t a, b;
    if (at > grid && bitw) {
        uint32_t k0 = (at - grid) / bitw;            /* bit holding `at` */
        a = grid + (k0 > 0 ? (k0 - 1) * bitw : 0);   /* one whole bit of air */
        /* If that lands on the very first bit (a CAN SOF row), the edge would
         * sit on the left border and read as "no edge". Step one more bit back
         * so the idle level before it is on screen. */
        if (a <= grid) a = a > bitw ? a - bitw : 0;
    } else {
        a = at > bitw ? at - bitw : 0;
    }
    if (en > grid && bitw) {
        uint32_t k1 = (en - grid + bitw - 1) / bitw; /* bit holding `en` */
        b = grid + (k1 + 1) * bitw;
    } else {
        b = en + bitw;
    }
    if (b > cap_len) b = cap_len;
    if (b <= a) b = a + 1;

    int wx = GRID_X + 3, wy = STRIP_Y + 4;
    int ww = GRID_W - 6, wh = 13;
    unsigned ch = dec_pkts.ch;
    int hi = wy, lo = wy + wh - 1;

    /* One column per pixel. A column that contains any transition is drawn as
     * a full-height bar, which is a vertical edge and never a slope. */
    for (int col = 0; col < ww; col++) {
        uint32_t s0 = a + (uint32_t)((uint64_t)(b - a) * (unsigned)col / (unsigned)ww);
        uint32_t s1 = a + (uint32_t)((uint64_t)(b - a) * (unsigned)(col + 1) / (unsigned)ww);
        if (s1 <= s0) s1 = s0 + 1;
        if (s1 > cap_len) s1 = cap_len;
        /* Compare against the sample BEFORE the column, not the first sample
         * in it. If a transition lands exactly on s0, reading level at s0 gives
         * the post-edge value and the loop finds no change: the edge vanishes.
         * That is the "missing falling edge" seen on the CAN data rows. A
         * transition is a change between consecutive samples, so walk s0-1..s1
         * and flag the column on any such change. */
        unsigned level = s0 ? ((cap_samples[s0 - 1] >> ch) & 1u)
                            : ((cap_samples[s0] >> ch) & 1u);
        bool edge = false;
        for (uint32_t i = s0; i < s1; i++) {
            unsigned cur = (cap_samples[i] >> ch) & 1u;
            if (cur != level) { edge = true; break; }
            level = cur;
        }
        int x = wx + col;
        if (edge) fill(x, hi, 1, wh, C_ACC);
        else fill(x, level ? hi : lo, 1, 2, C_ACC);
    }

    /* Bit ruler along the bottom of the mini waveform: one tick per bit, so
     * the row reads as a time scale and not as an abstract squiggle. The axis
     * line is always there; the ticks are skipped when they would merge. */
    int axis_y = STRIP_Y + 18;
    hline(wx, axis_y, ww, C_DIM);
    uint32_t nbits = bitw ? (b - a) / bitw : 0;
    if (nbits) {
        /* One tick per bit; on a long row every k-th bit, so the scale keeps
         * roughly a dozen marks instead of turning into a solid block. */
        uint32_t step = (nbits + 11u) / 12u;
        if (!step) step = 1;
        for (uint32_t k = 0; k <= nbits; k += step) {
            int tx = wx + (int)((uint64_t)k * (unsigned)ww / nbits);
            if (tx >= wx + ww) tx = wx + ww - 1;
            fill(tx, axis_y + 1, 1, 2, C_DIM);
        }
    }

    /* Two values only, under the ruler: where the packet starts and how much
     * time one division (one bit here) covers. */
    char tb[12], line[24];
    ui_format_offset(at, cap_trigger, (uint32_t)(dec_pkts.rate ? dec_pkts.rate : cap_rate),
            tb, sizeof tb);
    snprintf(line, sizeof line, "ST:%s", tb);
    text(GRID_X + 4, STRIP_Y + 21, line, C_TEXT);
    if (dec_pkts.rate && bitw) {
        ui_format_time((uint64_t)bitw * 1000000000ull / dec_pkts.rate, tb, sizeof tb);
        snprintf(line, sizeof line, "1div:%s", tb);
        text_right(GRID_X + GRID_W - 4, STRIP_Y + 21, line, C_ACC);
    }
}

/* "+86u" / "+1.2m" / "+0" from a sample gap. */
static const char *gap_text(uint32_t gap, uint32_t rate, char *buf, size_t n)
{
    if (!rate) { snprintf(buf, n, "-"); return buf; }
    if (!gap) snprintf(buf, n, "0");
    else ui_format_time((uint64_t)gap * 1000000000ull / rate, buf, n);
    return buf;
}

/* What goes in the ASC column: the character, or the protocol's own word. */
static void asc_text(const la_packet *p, char *buf, size_t n)
{
    if (p->flags & LA_PKT_ACK) { snprintf(buf, n, "ACK"); return; }
    if (p->flags & LA_PKT_NAK) { snprintf(buf, n, "NAK"); return; }
    if (p->flags & LA_PKT_ADDR) { snprintf(buf, n, "ADR"); return; }
    if (p->flags & LA_PKT_ID) {
        /* A CAN header is three rows; say which part each one is. */
        snprintf(buf, n, "%s", p->flags & LA_PKT_ID_DLC ? "DLC" :
                              (p->flags & LA_PKT_ID_HI ? "ID-hi" : "ID-lo"));
        return;
    }
    if (p->value >= 32 && p->value < 127) snprintf(buf, n, "'%c'", p->value);
    else snprintf(buf, n, ".");
}

static void draw_grid(void)
{
    /* Frame + header band. */
    fill(GRID_X, GRID_Y, GRID_W, GRID_HDR_H, C_BROWSE);
    frame_rect(GRID_X, GRID_Y, GRID_W, GRID_HDR_H + GRID_ROWS * GRID_ROW_H,
               C_STRIP_BD);

    /* Headers are words, not abbreviations: the old HEX/ASC/DT told nobody
     * what the column meant. */
    text_clip(COL_IDX_X, GRID_Y + 2, COL_IDX_W, "#", C_DIM, C_BROWSE);
    text_clip(COL_HEX_X, GRID_Y + 2, COL_HEX_W, "BYTE", C_DIM, C_BROWSE);
    text_clip(COL_ASC_X, GRID_Y + 2, COL_ASC_W, "ASCII", C_DIM, C_BROWSE);
    text_clip(COL_DT_X, GRID_Y + 2, COL_DT_W, "TIME", C_DIM, C_BROWSE);

    for (int r = 0; r < GRID_ROWS; r++) {
        int y = GRID_Y + GRID_HDR_H + r * GRID_ROW_H;
        uint32_t idx = dec_scroll + (uint32_t)r;
        bool sel = dec_pkts.n && idx == dec_sel;
        uint16_t bg = sel ? C_ROW_SEL : C_BG;
        uint16_t fg = sel ? RGB565(0xFF, 0xFF, 0xFF) : C_TEXT;

        /* Clean-Before-Draw: the whole row is repainted every frame. */
        fill(GRID_X + 1, y, GRID_W - 2, GRID_ROW_H, bg);
        hline(GRID_X + 1, y + GRID_ROW_H - 1, GRID_W - 2, C_STRIP_BD);

        if (idx >= dec_pkts.n) continue;

        const la_packet *p = &dec_pkts.pkt[idx];
        char b[12];

        snprintf(b, sizeof b, "%02u", (unsigned)(idx + 1));
        text_clip(COL_IDX_X, y + 3, COL_IDX_W, b, fg, bg);

        if (p->flags & (LA_PKT_ACK | LA_PKT_NAK)) snprintf(b, sizeof b, "--");
        else snprintf(b, sizeof b, "0x%02X", p->value);
        text_clip(COL_HEX_X, y + 3, COL_HEX_W, b, fg, bg);

        asc_text(p, b, sizeof b);
        text_clip(COL_ASC_X, y + 3, COL_ASC_W, b, fg, bg);

        gap_text(p->gap, dec_pkts.rate, b, sizeof b);
        text_clip(COL_DT_X, y + 3, COL_DT_W, b, fg, bg);
    }

    /* Scrollbar: a 2 px thumb whose length is the visible fraction. */
    int track_y = GRID_Y + GRID_HDR_H;
    int track_h = GRID_ROWS * GRID_ROW_H;
    if (dec_pkts.n > GRID_ROWS) {
        int th = track_h * GRID_ROWS / (int)dec_pkts.n;
        if (th < 4) th = 4;
        int maxs = (int)dec_pkts.n - GRID_ROWS;
        int ty = track_y + (maxs ? (track_h - th) * (int)dec_scroll / maxs : 0);
        fill(SBAR_X, track_y, 2, track_h, C_BROWSE);
        fill(SBAR_X, ty, 2, th, C_ACC);
    } else {
        fill(SBAR_X, track_y, 2, track_h, C_BROWSE);
    }
}

static void draw_decode(void)
{
    char buf[24];

    /* Clean-Before-Draw: one clear of the whole body, then every band paints
     * its own rectangle. Nothing is ever drawn over stale pixels. */
    fill(0, 0, SCR_W, SCR_H, C_BG);

    /* Band 1: header + packet badge. */
    fill(0, 0, SCR_W, DEC_HDR_H, C_HDR);
    text(4, 4, "DECODE", C_ACC);
    snprintf(buf, sizeof buf, "PKT:%u", dec_pkts.n);
    text_clip_right(SCR_W - 4, 4, 42, buf, C_BADGE, C_HDR);
    hline(0, DEC_HDR_H - 1, SCR_W, C_BROWSE);

    /* Band 2: three selector cards. PROTO is wider so "AUTO:CAN" fits. */
    {
        char pbuf[12];
        card(2, 60, "PROTO", proto_badge(pbuf, sizeof pbuf), dec_focus == 0,
             dec_edit && dec_focus == 0);
    }
    card(64, 32, "CHAN", chan_badge(buf, sizeof buf), false, false);
    card(98, 30, "RATE", rate_badge(buf, sizeof buf), false, false);

    /* Band 3: mini waveform of the selected packet. */
    draw_strip();

    /* Band 4: the framed data grid. */
    draw_grid();

    /* Footer: what the buttons do here, and what a row is. */
    text_clip(2, 150, 124, dec_edit ? "UP/DN:change OK:save" : "UP/DN:row  OK:edit", C_DIM, C_BG);
}

/* ---------- screen switching ---------- */
static void refresh(void)
{
    if (render_deferred) { render_pending = true; return; }
    switch (screen) {
    case SCR_STANDBY:  draw_standby(); break;
    case SCR_CONFIG:   draw_config(); break;
    case SCR_TRIGGER:  draw_trigger(); break;
    case SCR_WAVE_LO:  draw_wave(false); break;
    case SCR_WAVE_HI:  draw_wave(true); break;
    case SCR_DECODE:   draw_decode(); break;
    }
    tft_flush_fb(fb);
    frame_version++;
}

static void enter(ui_screen s)
{
    if (screen == SCR_WAVE_LO || screen == SCR_WAVE_HI)
        wave_views[screen == SCR_WAVE_HI] = (wave_view){wave_t, zoom_idx};
    screen = s;
    if (s == SCR_WAVE_LO || s == SCR_WAVE_HI) {
        wave_view v = wave_views[s == SCR_WAVE_HI];
        wave_t = v.start; zoom_idx = v.zoom;
    }
    switch (s) {
    case SCR_STANDBY: blink_phase = 0xFF; break;
    case SCR_CONFIG:  cfg_edit = false; cfg_row = 0; break;
    case SCR_TRIGGER: trig_edit = false; trig_row = 0; break;
    case SCR_DECODE:
        dec_edit = false;
        dec_focus = 0;
        run_decode();
        dec_scroll = 0;
        dec_sel = 0;
        break;
    default: break;
    }
    refresh();
}

/* ---------- value editing ---------- */
static void cycle_config(signed dir)
{
    switch (cfg_row) {
    case 0: rate_idx = (uint8_t)((rate_idx + dir + RATE_N) % RATE_N); break;
    case 1: count_idx = (uint8_t)((count_idx + dir + COUNT_N) % COUNT_N); break;
    case 2: mask_idx = (uint8_t)((mask_idx + dir + MASK_N) % MASK_N); break;
    case 3: pre_idx = (uint8_t)((pre_idx + dir + PRE_N) % PRE_N); break;
    default: break;
    }
    apply_config();
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
        proto_sel = (uint8_t)((proto_sel + dir + PROTO_N) % PROTO_N);
        run_decode();
        dec_scroll = 0;
        dec_sel = 0;
    } else if (proto_sel == 0 || proto_sel == 1) {
        /* Baud applies to AUTO (UART bit-time gate) and manual UART. */
        baud_idx = (uint8_t)((baud_idx + dir + BAUD_N) % BAUD_N);
        run_decode();
        dec_scroll = 0;
        dec_sel = 0;
    }
}

/* Centre the view on a sample, clamped to the buffer. FIT-safe: when the
 * whole capture is visible the clamp lands at 0. */
static void wave_centre_on(uint32_t target)
{
    uint32_t visible = visible_samples();
    uint32_t max = cap_len > visible ? cap_len - visible : 0;
    wave_t = target > visible / 2 ? target - visible / 2 : 0;
    if (wave_t > max) wave_t = max;
}

static void wave_scroll(signed dir)
{
    uint32_t visible = visible_samples();
    uint32_t step = visible / 2;
    if (!step) step = 1;
    uint32_t max = cap_len > visible ? cap_len - visible : 0;
    if (dir > 0) wave_t = (wave_t + step > max) ? max : wave_t + step;
    else wave_t = (wave_t > step) ? wave_t - step : 0;
}

/* ---------- event dispatch ---------- */
static void event(btn_id id, bool long_press, uint32_t held_ms)
{
    if (event_count != 255) event_count++;

    switch (screen) {
    case SCR_STANDBY:
        /* Any of the four buttons boots us into the config screen. */
        enter(SCR_CONFIG);
        break;

    case SCR_CONFIG:
        if (id == BTN_UP || id == BTN_DOWN) {
            signed dir = id == BTN_UP ? -1 : +1;
            if (cfg_edit) cycle_config(dir);
            else cfg_row = (uint8_t)((cfg_row + dir + 4) % 4);
            refresh();
        } else if (id == BTN_OK) {
            /* The start row is gone: hold OK in browse mode to arm a capture,
             * short OK still edits the focused row. */
            if (!cfg_edit && long_press) {
                if (!capturing && !capture_request) {
                    apply_config();
                    capture_request = true;
                    capturing = true;
                    cap_ok = true;
                    cap_trigger = 0;
                    zoom_idx = 3;               /* X8: an edge is square */
                    wave_t = 0;
                    enter(SCR_WAVE_LO);
                }
            } else {
                cfg_edit = !cfg_edit;
                refresh();
            }
        } else {
            if (cfg_edit) { cfg_edit = false; refresh(); }
            else if (long_press && held_ms >= BTN_STANDBY_MS) enter(SCR_STANDBY);
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

    case SCR_WAVE_LO:
    case SCR_WAVE_HI:
        if (id == BTN_UP || id == BTN_DOWN) {
            bool up = id == BTN_UP;
            if (long_press) {
                uint32_t centre = wave_t + visible_samples() / 2;
                if (up) {
                    if (zoom_is_fit()) zoom_idx = ZOOM_N - 1;
                    else if (zoom_idx) zoom_idx--;
                } else if (!zoom_is_fit()) {
                    if ((unsigned)zoom_idx + 1 >= ZOOM_N) zoom_idx = ZOOM_FIT;
                    else zoom_idx++;
                }
                wave_centre_on(centre);
            } else {
                wave_scroll(up ? -1 : +1);
            }
            refresh();
        } else if (id == BTN_OK) {
            if (long_press) { reset_wave_views(); refresh(); }
            else enter(screen == SCR_WAVE_LO ? SCR_WAVE_HI : SCR_DECODE);
        } else {
            if (screen == SCR_WAVE_LO) enter(SCR_CONFIG);
            else enter(SCR_WAVE_LO);
        }
        break;

    case SCR_DECODE:
        if (id == BTN_UP || id == BTN_DOWN) {
            signed dir = id == BTN_UP ? -1 : +1;
            /* Long UP/DOWN in browse mode swaps which selector OK edits:
             * PROTO <-> bus config (baud for UART). This used to be long OK,
             * which is now reserved for returning to STANDBY. */
            if (long_press && !dec_edit) {
                dec_focus ^= 1;
            } else if (dec_edit) {
                cycle_decode(dir);
            } else if (dec_pkts.n) {
                /* Move the selection; the window follows so the selected row
                 * is always on screen, which is what the preview shows. */
                int sel = (int)dec_sel + dir;
                if (sel < 0) sel = 0;
                if (sel > (int)dec_pkts.n - 1) sel = (int)dec_pkts.n - 1;
                dec_sel = (uint8_t)sel;
                if (dec_sel < dec_scroll) dec_scroll = dec_sel;
                if (dec_sel >= dec_scroll + GRID_ROWS)
                    dec_scroll = (uint32_t)dec_sel - GRID_ROWS + 1;
            }
            refresh();
        } else if (id == BTN_OK) {
            if (long_press) {
                /* OK held ends the session and returns to the boot screen. */
                enter(SCR_STANDBY);
                break;              /* enter() already flushed the frame */
            }
            if (dec_edit) {
                dec_edit = false;
                run_decode();
                dec_scroll = 0;
                dec_sel = 0;
            } else {
                dec_edit = true;
            }
            refresh();
        } else {
            if (dec_edit) { dec_edit = false; refresh(); }
            else enter(SCR_WAVE_HI);
        }
        break;
    }
}

/* ---------- demo data ---------- */
/* 60000 samples at 25 MHz carrying all four protocols at once on the
 * la_map_test8 channels, so screens 3/4/5 have real captures with no
 * generator attached:
 *
 *   CH0 UART 115200 8N1 "HELLO"
 *   CH1/CH2 I2C write 0xA0 0x5A
 *   CH3/CH4/CH5 SPI CS/SCK/MOSI, bytes DE AD BE EF
 *   CH6 50 kHz square (a plain digital line for the waveform view)
 *   CH7 CAN 250 kbit/s standard frame ID=0x123 DLC=2 DE AD (with stuffing)
 */
#define DEMO_LEN 60000
static uint8_t demo_buf[DEMO_LEN];

static void demo_set(uint32_t pos, uint8_t ch, bool level)
{
    if (pos >= DEMO_LEN) return;
    if (level) demo_buf[pos] |= (uint8_t)(1u << ch);
    else demo_buf[pos] &= (uint8_t)~(1u << ch);
}

static void demo_span(uint32_t from, uint32_t to, uint8_t ch, bool level)
{
    if (to > DEMO_LEN) to = DEMO_LEN;
    for (uint32_t i = from; i < to; i++) demo_set(i, ch, level);
}

/* 8N1: start bit, 8 data bits LSB first, stop bit. */
static uint32_t demo_uart(uint32_t pos, const char *msg, uint32_t bit)
{
    demo_span(pos, DEMO_LEN, 0, true);                 /* idle high */
    pos += bit;                                        /* idle before SOF */
    for (const char *p = msg; *p; p++) {
        demo_span(pos, pos + bit, 0, false);           /* start */
        pos += bit;
        for (int k = 0; k < 8; k++, pos += bit)
            demo_span(pos, pos + bit, 0, (bool)((*p >> k) & 1));
        demo_span(pos, pos + bit, 0, true);            /* stop */
        pos += bit;
    }
    return pos;
}

/* START, address byte with ACK, data byte with ACK, STOP.
 *
 * Levels are held with demo_span, never poked one sample at a time: a level
 * written at a single index is high again on the very next sample, which is
 * what made the decoder see a START and a STOP one sample apart. SDA changes
 * while SCL is low and is sampled at the rise, so it is written across both
 * halves of the bit. */
static uint32_t demo_i2c(uint32_t pos, uint32_t half)
{
    /* Idle: both lines high. */
    demo_span(pos, DEMO_LEN, 1, true);
    demo_span(pos, DEMO_LEN, 2, true);
    pos += 4 * half;

    demo_span(pos, pos + 2 * half, 2, false);          /* START: SDA falls */
    pos += 2 * half;

    const uint8_t bytes[2] = { 0xA2, 0x5A };           /* 0x51 write, data */
    for (int k = 0; k < 2; k++) {
        for (int b = 7; b >= 0; b--) {
            demo_span(pos, pos + half, 1, false);      /* SCL low */
            demo_span(pos, pos + 2 * half, 2, (bool)((bytes[k] >> b) & 1));
            pos += half;
            demo_span(pos, pos + half, 1, true);       /* rise: sampled here */
            pos += half;
        }
        demo_span(pos, pos + half, 1, false);          /* ACK clock */
        demo_span(pos, pos + 2 * half, 2, false);      /* slave pulls low */
        pos += half;
        demo_span(pos, pos + half, 1, true);
        pos += half;
    }

    demo_span(pos, pos + half, 1, false);              /* STOP clock */
    demo_span(pos, pos + half, 2, false);
    pos += half;
    demo_span(pos, pos + half, 1, true);
    pos += half;
    demo_span(pos, pos + half, 2, true);               /* STOP: SDA rises */
    pos += half;
    return pos;
}

/* CS low for the whole frame, mode 0: MOSI is sampled on the SCK rise. */
static uint32_t demo_spi(uint32_t pos, uint32_t half)
{
    const uint8_t bytes[4] = { 0xDE, 0xAD, 0xBE, 0xEF };

    demo_span(pos, DEMO_LEN, 3, true);                 /* CS idle high */
    demo_span(pos, DEMO_LEN, 4, false);                /* SCK idle low */
    pos += 2 * half;

    /* CS must stay low for the whole frame, so hold it across the frame. */
    uint32_t frame = 2 * half + 4u * 8u * 2u * half;
    demo_span(pos, pos + frame, 3, false);
    pos += half;

    for (int k = 0; k < 4; k++) {
        for (int b = 7; b >= 0; b--) {
            demo_span(pos, pos + half, 4, false);      /* SCK low */
            demo_span(pos, pos + 2 * half, 5, (bool)((bytes[k] >> b) & 1));
            pos += half;
            demo_span(pos, pos + half, 4, true);       /* rise: sampled */
            pos += half;
        }
    }
    demo_span(pos, DEMO_LEN, 3, true);                 /* CS rises */
    pos += 2 * half;
    return pos;
}

/* Standard 11-bit data frame. Bit stuffing is part of the wire format, so a
 * frame built without it would decode as garbage: after five identical bits
 * insert the opposite one. */
static uint32_t demo_can(uint32_t pos, uint32_t bit)
{
    demo_span(pos, DEMO_LEN, 7, true);                 /* bus idle recessive */
    pos += 4 * bit;

    uint8_t logical[96];
    unsigned n = 0, k;
    logical[n++] = 0;                                  /* SOF dominant */
    for (int b = 10; b >= 0; b--) logical[n++] = (uint8_t)((0x123u >> b) & 1u);
    logical[n++] = 0;                                  /* RTR = data frame */
    logical[n++] = 0;                                  /* IDE = standard */
    logical[n++] = 0;                                  /* r0 */
    for (int b = 3; b >= 0; b--) logical[n++] = (uint8_t)((6u >> b) & 1u);
    const uint8_t data[6] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xC0, 0xFF };
    for (k = 0; k < sizeof data; k++)
        for (int b = 7; b >= 0; b--) logical[n++] = (uint8_t)((data[k] >> b) & 1u);

    uint8_t last = 1;
    unsigned run = 0;
    for (k = 0; k < n; k++) {
        uint8_t v = logical[k];
        demo_span(pos, pos + bit, 7, (bool)v);
        pos += bit;
        if (v == last) run++;
        else { run = 1; last = v; }
        if (run == 5) {                                /* stuffed bit */
            demo_span(pos, pos + bit, 7, (bool)!v);
            pos += bit;
            run = 1;
            last = (uint8_t)!v;
        }
    }
    demo_span(pos, DEMO_LEN, 7, true);
    return pos;
}

static void demo_build(void)
{
    memset(demo_buf, 0, sizeof demo_buf);
    demo_span(0, DEMO_LEN, 6, false);
    for (uint32_t i = 0; i < DEMO_LEN; i++)            /* CH6: 50 kHz */
        if ((i / 250u) & 1u) demo_buf[i] |= 0x40u;

    demo_uart(2000, "HELLO", 25000000u / 115200u);     /* 217 samples/bit */
    demo_i2c(14000, 40);                               /* 312 kHz SCL */
    demo_spi(24000, 20);                               /* 625 kHz SCK */
    demo_can(34000, 100);                              /* 250 kbit/s */
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
    count_idx = 0;                                  /* 16K */
    apply_config();
    wave_t = 0;
    zoom_idx = 4;                                   /* X16: a bit is ~13 px */
    reset_wave_views();
    dec_scroll = 0;
    proto_sel = 0;                                  /* AUTO on every demo load */
    memset(&dec_auto, 0, sizeof dec_auto);
    enter(SCR_WAVE_LO);
}

/* ---------- public API ---------- */
void ui_init(void)
{
    cap_samples = NULL; cap_len = 0; cap_trigger = 0; cap_rate = 0;
    wave_views[0] = (wave_view){0, 4}; wave_views[1] = (wave_view){0, 0};
    rate_idx = (uint8_t)(RATE_N - 1);               /* 25 MHz */
    count_idx = 1;                                  /* 64K, enough post-trigger UART */
    mask_idx = 0;                                   /* CH 0-7 */
    pre_idx = 2;                                    /* 50 % */
    baud_idx = 2;                                   /* 115200 */
    zoom_idx = 4;                                   /* X16: a bit is ~13 px */
    wave_centre_on(cap_trigger);
    proto_sel = 0;
    cap_ok = true;
    cfg_row = 0;
    trig_row = 0;

    apply_config();
    ui_cfg.trig_mode = LAR2_TRIGGER_EDGE;
    ui_cfg.trig_channel = 0;
    ui_cfg.trig_edge = LAR2_EDGE_FALLING;             /* UART start bit on CH0 */
    ui_cfg.trig_value = 0x55;
    ui_cfg.trig_mask = 0xFF;

    buttons_init();
    enter(SCR_STANDBY);
}

void ui_set_now_ms(uint32_t ms)
{
    ui_now_ms = ms;
}

void ui_tick(void)
{
    buttons_update();
    btn_event ev;
    render_deferred = true;
    while (buttons_get_event(&ev)) event(ev.id, ev.long_press, ev.held_ms);

    /* Blink the standby prompt only when the 600 ms phase actually flips,
     * and only while standby is on screen: one flush per phase, no flicker
     * and no redraw on an unrelated key press. */
    uint8_t ph = (uint8_t)((ui_now_ms / 600u) & 1u);
    if (screen == SCR_STANDBY && ph != blink_phase) {
        blink_phase = ph;
        refresh();
    }
    render_deferred = false;
    if (render_pending) { render_pending = false; refresh(); }
}

uint8_t ui_screen_id(void)
{
    return (uint8_t)screen;
}

uint32_t ui_frame_version(void) { return frame_version; }

size_t ui_copy_frame(uint16_t offset, uint8_t *out, size_t len)
{
    if (offset >= sizeof fb) return 0;
    if (len > sizeof fb - offset) len = sizeof fb - offset;
    memcpy(out, (const uint8_t *)fb + offset, len);
    return len;
}

void ui_get_view(ui_view_state *out)
{
    *out = (ui_view_state){wave_t, visible_samples(), cap_len, cap_trigger, cap_rate,
                          (uint8_t)screen, zoom_idx, proto_sel, event_count};
}

bool ui_show_capture(uint8_t screen_id, uint8_t protocol)
{
    if (screen_id < SCR_WAVE_LO || screen_id > SCR_DECODE || protocol >= PROTO_N ||
        !cap_ok || !cap_samples || !cap_len)
        return false;
    proto_sel = protocol;
    baud_idx = 2; /* Bench/demo contract: UART 115200, CAN 250000. */
    enter((ui_screen)screen_id);
    return true;
}

uint8_t ui_event_count(void)
{
    return event_count;
}

uint8_t ui_proto_sel(void) { return proto_sel; }

uint8_t ui_dec_auto_proto(void) { return dec_auto.proto; }

const char *ui_decode_proto_badge(void)
{
    static char buf[16];
    return proto_badge(buf, sizeof buf);
}

const char *ui_decode_text(void)
{
    return dec_text;
}

/* Rebuild the visible grid as text, using the same formatters the panel uses,
 * so a host check and the screen cannot disagree about a row. */
const char *ui_decode_table(void)
{
    static char out[512];
    size_t pos = 0;
    out[0] = '\0';

    for (int r = 0; r < GRID_ROWS; r++) {
        uint32_t idx = dec_scroll + (uint32_t)r;
        if (idx >= dec_pkts.n) break;
        const la_packet *p = &dec_pkts.pkt[idx];
        char asc[12], dt[12], hex[12];
        if (p->flags & (LA_PKT_ACK | LA_PKT_NAK)) snprintf(hex, sizeof hex, "--");
        else snprintf(hex, sizeof hex, "0x%02X", p->value);
        asc_text(p, asc, sizeof asc);
        gap_text(p->gap, dec_pkts.rate, dt, sizeof dt);
        pos += (size_t)snprintf(out + pos, sizeof(out) - pos, "%s%02u|%s|%s|%s",
                                r ? "\n" : "", (unsigned)(idx + 1), hex, asc, dt);
        if (pos >= sizeof(out)) break;
    }
    if (!pos) snprintf(out, sizeof out, "(no packets)");
    return out;
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
    case 'B': buttons_inject_held(BTN_BACK, BTN_STANDBY_MS); break;
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
    dec_scroll = 0;
    /* Each channel page opens on its own first activity at a readable zoom. */
    reset_wave_views();
    if (screen == SCR_DECODE) {
        dec_sel = 0;
        run_decode();
        refresh();
    } else if (screen == SCR_WAVE_LO || screen == SCR_WAVE_HI) refresh();
}
