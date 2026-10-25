/* SPDX-License-Identifier: GPL-3.0-or-later
 * RP2350 logic analyzer firmware (LAR2 v1 protocol).
 *
 * Capture path: one PIO state machine latches all eight channels in a single
 * cycle, DMA moves the samples into SRAM, the CPU scans the buffer for the
 * trigger condition and finally the window is streamed to the host over USB CDC.
 *
 * The capture region is filled linearly, so a trigger window is always
 * contiguous in memory and no sample is ever lost: there is no ring buffer
 * that would need a gapless re-arm.
 */
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "tusb.h"
#include "logic_capture.pio.h"
#include "lar2.h"
#include "tft_st7735.h"
#include "test_image.h"
#include "trigger_match.h"

#define CAPTURE_WORDS (LAR2_MAX_SAMPLES / 4u)
#define CAPTURE_BYTES (LAR2_MAX_SAMPLES)
/* How far behind the DMA write pointer the trigger scan is allowed to read.
 * The write address points at the next word, so one word can still be in
 * flight; a word that is being written reads back as its previous content,
 * which would look like a false edge to the matcher. Sixteen words of slack
 * is far more than the one word that can be in flight, and it only delays the
 * trigger detection by a few samples, it never shifts the trigger index. */
#define DMA_LAG_BYTES 64u

static uint32_t samples[CAPTURE_WORDS];
static struct lar2_parser parser;
static uint16_t tx_seq, rx_seq;
static uint64_t rate = 1000000;
static uint32_t count = 100000, sent, crc;
static uint8_t mask = 0xff;
static bool configured, capturing, sending;
static PIO pio = pio0;
static int sm, offset;
static int dma_ch;

/* Trigger configuration and state. */
static uint8_t trig_mode = LAR2_TRIGGER_MANUAL;
static uint8_t trig_channel, trig_edge, trig_value, trig_mask;
static uint32_t pretrigger;      /* samples kept before the trigger */
static uint32_t trigger_abs;     /* absolute sample index of the trigger */
static bool trigger_found;
static uint32_t scan_pos;        /* samples already handed to the matcher */
static const uint8_t *tx_base;   /* first sample to send */

static void send_frame(uint8_t type, const uint8_t *p, size_t n)
{
    uint8_t frame[LAR2_MAX_FRAME];
    size_t len = lar2_encode(frame, type, tx_seq++, p, n), pos = 0;
    absolute_time_t deadline = make_timeout_time_ms(3000);
    while (pos < len && tud_cdc_connected() && !time_reached(deadline)) {
        tud_task();
        uint32_t wrote = tud_cdc_write(frame + pos, len - pos);
        pos += wrote; tud_cdc_write_flush();
        if (wrote) deadline = make_timeout_time_ms(3000);
    }
    if (pos != len) sending = false;
}
static void ack(uint8_t command) { send_frame(LAR2_ACK, &command, 1); }
static void error(const char *text) { send_frame(LAR2_ERROR, (const uint8_t *)text, strlen(text)); }

static void abort_capture(void)
{
    pio_sm_set_enabled(pio, sm, false);
    dma_channel_abort(dma_ch);
    pio_sm_clear_fifos(pio, sm);
    capturing = sending = false;
    trigger_found = false;
    scan_pos = 0;
}

/* True when the capture lost timing: the PIO FIFO overflowed or the DMA hit a
 * bus error. Both mean the samples in memory cannot be trusted. */
static bool capture_faulted(void)
{
    return (pio->fdebug & (1u << (PIO_FDEBUG_RXSTALL_LSB + sm))) ||
           (dma_hw->ch[dma_ch].ctrl_trig &
            (DMA_CH0_CTRL_TRIG_READ_ERROR_BITS | DMA_CH0_CTRL_TRIG_WRITE_ERROR_BITS));
}

/* Samples already written by the DMA, minus the in-flight margin. */
static uint32_t produced_samples(void)
{
    uint32_t base = (uint32_t)(uintptr_t)samples;
    uint32_t off = dma_hw->ch[dma_ch].write_addr - base;
    if (off > CAPTURE_BYTES) off = CAPTURE_BYTES;
    return off > DMA_LAG_BYTES ? off - DMA_LAG_BYTES : 0;
}

static void start_capture(void)
{
    abort_capture();
    pio_sm_config c = logic_capture_program_get_default_config(offset);
    sm_config_set_in_pins(&c, 0);
    sm_config_set_in_shift(&c, true, true, 32);
    sm_config_set_out_shift(&c, true, false, 32);
    sm_config_set_clkdiv_int_frac(&c, (uint32_t)(25000000 / rate), 0);
    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_consecutive_pindirs(pio, sm, 0, 8, false);
    pio_sm_clkdiv_restart(pio, sm);
    /* Manual capture stops after the requested number of samples. A triggered
     * capture fills the whole region and the trigger scan decides when to
     * stop, so the trigger can be found late in the region. */
    uint32_t words = (trig_mode == LAR2_TRIGGER_MANUAL) ? (count + 3u) / 4u : CAPTURE_WORDS;
    if (words > CAPTURE_WORDS) words = CAPTURE_WORDS;
    pio_sm_put_blocking(pio, sm, words * 4u - 1u);
    pio_sm_exec(pio, sm, pio_encode_pull(false, true));
    pio_sm_exec(pio, sm, pio_encode_out(pio_x, 32));
    dma_channel_config dc = dma_channel_get_default_config(dma_ch);
    channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
    channel_config_set_read_increment(&dc, false);
    channel_config_set_write_increment(&dc, true);
    channel_config_set_dreq(&dc, pio_get_dreq(pio, sm, false));
    pio->fdebug = 1u << (PIO_FDEBUG_RXSTALL_LSB + sm);
    dma_channel_configure(dma_ch, &dc, samples, &pio->rxf[sm], words, true);
    capturing = true;
    trigger_found = false;
    scan_pos = 0;
    tx_base = (const uint8_t *)samples;
    pio_sm_set_enabled(pio, sm, true);
}

/* Look for the trigger in the part of the buffer that the DMA has produced
 * since the last call. The trigger is only accepted once the pre-trigger
 * buffer is full, so the window always has the requested history. */
static void trigger_scan(void)
{
    if (trigger_found) return;         /* the first match wins */
    uint32_t prod = produced_samples();
    if (prod > CAPTURE_BYTES) prod = CAPTURE_BYTES;
    if (prod <= scan_pos) return;
    uint32_t from = scan_pos;
    if (from < pretrigger) from = pretrigger;
    if (from < prod) {
        uint32_t hit;
        if (lar2_trigger_match((const uint8_t *)samples, from, prod, trig_mode,
                               trig_channel, trig_edge, trig_value, trig_mask, &hit)) {
            trigger_found = true;
            trigger_abs = hit;
        }
    }
    scan_pos = prod;
}

static void command(void)
{
    uint8_t *f = parser.buf, *p = f + 12;
    uint8_t type = f[5]; uint32_t n = lar2_u32(f + 8);
    if (type == LAR2_HELLO && n == 0 && lar2_u16(f + 6) == 0) {
        abort_capture(); tx_seq = 0; rx_seq = 1; configured = false;
        const char *id = LAR2_BOARD_ID;
        send_frame(LAR2_ID, (const uint8_t *)id, strlen(id));
        return;
    }
    if (lar2_u16(f + 6) != rx_seq++) { abort_capture(); configured = false; error("sequence"); return; }
    if (type == LAR2_ABORT && !n) { abort_capture(); configured = false; ack(type); return; }
    if (capturing || sending) { error("busy"); return; }
    switch (type) {
    case LAR2_GET_CAPS: {
        if (n) { error("caps length"); break; }
        uint8_t caps[12 + 8 * LAR2_RATE_COUNT] = {8, 1, 0, 0};
        lar2_put32(caps + 4, LAR2_MAX_SAMPLES); lar2_put32(caps + 8, LAR2_RATE_COUNT);
        for (unsigned i = 0; i < LAR2_RATE_COUNT; i++) lar2_put64(caps + 12 + 8 * i, lar2_rates[i]);
        send_frame(LAR2_CAPS, caps, sizeof(caps)); break;
    }
    case LAR2_CONFIG: {
        configured = false;
        if (n != LAR2_CONFIG_V1_SIZE && n != LAR2_CONFIG_V2_SIZE) { error("invalid config"); break; }
        if (!lar2_u32(p + 8) || lar2_u32(p + 8) > LAR2_MAX_SAMPLES || !p[16]) { error("invalid config"); break; }
        bool supported = false;
        for (unsigned i = 0; i < LAR2_RATE_COUNT; i++) if (lar2_u64(p) == lar2_rates[i]) supported = true;
        if (!supported) { error("unsupported rate"); break; }
        rate = lar2_u64(p); count = lar2_u32(p + 8); mask = p[16];
        trig_mode = LAR2_TRIGGER_MANUAL; pretrigger = 0;
        trig_channel = trig_edge = trig_value = trig_mask = 0;
        if (n == LAR2_CONFIG_V1_SIZE) {
            /* v1: manual capture only, the reserved bytes must stay zero. */
            if (lar2_u32(p + 12) || p[17] || p[18] || p[19]) { error("invalid config"); break; }
        } else {
            pretrigger = lar2_u32(p + 12);
            trig_mode = p[17]; trig_channel = p[18]; trig_edge = p[19];
            trig_value = p[20]; trig_mask = p[21];
            if (lar2_u16(p + 22) || lar2_u32(p + 24)) { error("invalid config"); break; }
            if (trig_mode == LAR2_TRIGGER_MANUAL) {
                if (pretrigger) { error("invalid config"); break; }
            } else if (trig_mode == LAR2_TRIGGER_EDGE) {
                if (trig_channel > 7 || trig_edge > 1) { error("invalid config"); break; }
            } else if (trig_mode == LAR2_TRIGGER_PATTERN) {
                if (trig_channel > 7) { error("invalid config"); break; }
            } else { error("invalid config"); break; }
            if (pretrigger >= count) { error("invalid config"); break; }
        }
        configured = true; ack(type); break;
    }
    case LAR2_ARM:
        if (n || !configured) { error("not configured"); break; }
        ack(type); start_capture(); break;
    default: error("unknown command");
    }
}

static void start_sending(uint32_t sample_count, uint32_t trigger_index, uint32_t pre,
                          uint8_t flags)
{
    uint8_t info[24] = {0};
    lar2_put64(info, rate); lar2_put32(info + 8, sample_count);
    lar2_put32(info + 12, trigger_index); lar2_put32(info + 16, pre);
    info[20] = 8; info[21] = 1; info[22] = mask; info[23] = flags;
    sending = true; sent = 0; crc = ~0u;
    send_frame(LAR2_INFO, info, sizeof(info));
}

static void capture_task(void)
{
    if (capturing) {
        bool dma_done = !dma_channel_is_busy(dma_ch);
        if (trig_mode == LAR2_TRIGGER_MANUAL) {
            if (dma_done) {
                pio_sm_set_enabled(pio, sm, false); capturing = false;
                /* The PIO stops after the last requested word. RXSTALL cannot
                 * come from extra samples after DMA completion, so it signals
                 * lost timing. */
                if (capture_faulted()) {
                    configured = false; error("capture FIFO stall/DMA error"); return;
                }
                start_sending(count, LAR2_NO_TRIGGER, 0, 0);
            }
        } else {
            trigger_scan();
            if (trigger_found) {
                uint32_t post = count - pretrigger;
                if (produced_samples() >= trigger_abs + post) {
                    pio_sm_set_enabled(pio, sm, false);
                    dma_channel_abort(dma_ch);
                    pio_sm_clear_fifos(pio, sm);
                    capturing = false;
                    /* A stalled FIFO or a DMA bus error means the window in
                     * memory is not trustworthy, so report it instead of
                     * sending damaged samples. */
                    if (capture_faulted()) {
                        configured = false; error("capture FIFO stall/DMA error"); return;
                    }
                    /* The window is contiguous in the linear capture region:
                     * pretrigger samples before the trigger plus post samples
                     * after it. The trigger sits at offset pretrigger. */
                    tx_base = (const uint8_t *)samples + (trigger_abs - pretrigger);
                    start_sending(count, pretrigger, pretrigger, LAR2_INFO_TRIGGER_FOUND);
                } else if (dma_done) {
                    pio_sm_set_enabled(pio, sm, false); capturing = false;
                    configured = false; error("trigger too late"); return;
                }
            } else if (dma_done) {
                pio_sm_set_enabled(pio, sm, false); capturing = false;
                configured = false; error("no trigger"); return;
            }
        }
    }
    if (sending) {
        uint32_t n = count - sent;
        if (n > LAR2_MAX_PAYLOAD) n = LAR2_MAX_PAYLOAD;
        if (n) {
            crc = lar2_crc_update(crc, tx_base + sent, n);
            send_frame(LAR2_SAMPLES, (const uint8_t *)(tx_base + sent), n); sent += n;
        } else {
            uint8_t end[8]; lar2_put32(end, count); lar2_put32(end + 4, ~crc);
            send_frame(LAR2_END, end, sizeof(end)); sending = false;
        }
    }
}

int main(void)
{
    set_sys_clock_khz(100000, true);
    tft_init();
    tft_fill_color(0x0000);
    tft_draw_image((TFT_WIDTH - TEST_IMG_W) / 2,
                    (TFT_HEIGHT - TEST_IMG_H) / 2,
                    TEST_IMG_W, TEST_IMG_H, test_image);
    stdio_init_all();
    for (uint i = 0; i < 8; i++) { pio_gpio_init(pio, i); gpio_disable_pulls(i); }
    sm = pio_claim_unused_sm(pio, true);
    offset = pio_add_program(pio, &logic_capture_program);
    dma_ch = dma_claim_unused_channel(true);
    bool connected = false;
    while (true) {
        tud_task();
        bool now = tud_cdc_connected();
        if (!now) {
            if (connected) { abort_capture(); configured = false; memset(&parser, 0, sizeof(parser)); }
        } else {
            /* Process one command before capture_task; ABORT takes priority. */
            while (tud_cdc_available()) {
                uint8_t b; if (tud_cdc_read(&b, 1) != 1) break;
                int r = lar2_feed(&parser, b);
                if (r < 0) { abort_capture(); configured = false; error("frame CRC/header"); }
                if (r == 1) { command(); break; }
            }
            capture_task();
        }
        connected = now; tight_loop_contents();
    }
}
