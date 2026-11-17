/* SPDX-License-Identifier: GPL-3.0-or-later
 * RP2350 logic analyzer firmware — Topic 2 (8 channels, 25 MHz, trigger,
 * on-device decode, flash storage, signature comparator, capture diff).
 *
 * Capture path: one PIO state machine latches all eight channels in a single
 * cycle, DMA moves the samples into SRAM, the CPU scans the buffer for the
 * trigger condition and finally the window is streamed to the host over USB CDC.
 *
 * The capture region is filled linearly, so a trigger window is always
 * contiguous in memory and no sample is ever lost.
 */
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "pico/bootrom.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"
#include "hardware/sync.h"
#include "hardware/irq.h"
#include "tusb.h"
#include "logic_capture.pio.h"
#include "lar2.h"
#include "trigger_match.h"
#include "decoders.h"
#include "tft_st7735.h"
#include "buttons.h"
#include "ui.h"

#define CAPTURE_WORDS (LAR2_MAX_SAMPLES / 4u)
#define CAPTURE_BYTES (LAR2_MAX_SAMPLES)
#define DMA_LAG_BYTES 64u

/* Streaming (continuous capture to the host). The capture buffer is reused as
 * a four-slot ring: DMA fills one 64 KB slot while the CPU ships the previous
 * ones. Four slots = 192 ms of slack at 1 MHz, which absorbs USB jitter.
 * USB CDC full speed caps the useful rate at 1 MHz (1 byte/sample = 1 MB/s);
 * the PIO itself still runs at whatever CONFIG asked for. */
#define STREAM_SLOTS 4u
#define STREAM_SLOT_BYTES (CAPTURE_BYTES / STREAM_SLOTS)
#define STREAM_SLOT_WORDS (STREAM_SLOT_BYTES / 4u)
#define STREAM_CHUNK 500u        /* RLE worst case 2*500+5 = 1005 < 1024 */

/* Flash storage: use the last 1 MB of flash for session data.
 * Two slots of 512 KB: a slot is one header sector plus the samples, which
 * covers the full 256 KB capture buffer. The erase range must span every
 * byte that is programmed, otherwise the write silently lands in flash that
 * was never erased. */
#define FLASH_STORAGE_BASE (PICO_FLASH_SIZE_BYTES - 1024 * 1024)
#define FLASH_SECTOR_BYTES 4096  /* matches hardware/flash.h */
#define FLASH_PAGE_BYTES 256
#define FLASH_SESSION_BYTES (FLASH_SECTOR_BYTES * 128)  /* 512 KB per slot */
#define MAX_SESSIONS 2
#define SESSION_MAX_SAMPLES (FLASH_SESSION_BYTES - FLASH_SECTOR_BYTES)
#define SESSION_OFFSET(slot) (FLASH_STORAGE_BASE + (uint32_t)(slot) * FLASH_SESSION_BYTES)
#define SESSION_MAGIC 0x4C415345  /* "LASE" */

static uint32_t samples[CAPTURE_WORDS];
static struct lar2_parser parser;
static uint16_t tx_seq, rx_seq;
static uint64_t rate = 1000000;
static uint32_t count = 100000, sent, crc;
static uint8_t mask = 0xff;
static bool configured, capturing, sending;
static volatile bool streaming;
static volatile uint32_t stream_prod;   /* slots completed by DMA */
static uint32_t stream_cons;            /* slots shipped to the host */
static uint32_t stream_sent_samples;
static uint32_t stream_dropped;
static bool stream_rle;
static PIO pio = pio0;
static int sm, offset;
static int dma_ch;

/* Trigger state. */
static uint8_t trig_mode = LAR2_TRIGGER_MANUAL;
static uint8_t trig_channel, trig_edge, trig_value, trig_mask;
static uint32_t pretrigger, trigger_abs;
static uint32_t estimated_baud;
static bool trigger_found;
static uint32_t scan_pos;
static uint32_t trigger_restarts;
/* Delivered capture window: [win_base, win_base + win_len). For a manual
 * capture it is the whole buffer; for a triggered capture it starts at
 * trigger_abs - pretrigger. Every consumer (send, decode, flash, golden)
 * must use this window, not samples[0]. */
static const uint8_t *win_base;
static uint32_t win_len;

/* On-device decode results (returned via ANALYZE DECODE). 1 KB fits the
 * four-protocol simultaneous decode of the test map. */
static char decode_buf[1024];
static size_t decode_len;

/* Forward declarations for flash/sig/diff functions. */
static void flash_save_session(uint32_t slot, uint32_t sample_count,
                               uint64_t sample_rate, uint32_t channel_mask,
                               uint32_t trigger_index, uint32_t pretrigger,
                               uint8_t trigger_mode);
static bool flash_load_session(uint32_t slot);
static void save_golden(void);
static int compare_with_golden(void);
static int diff_with_loaded_session(uint32_t slot);
static void start_sending(uint32_t sample_count, uint32_t trigger_index,
                          uint32_t pre, uint8_t flags);
static bool start_stream(uint64_t rate, uint8_t mask, bool rle);
static void stop_stream(void);

/* Global variables used in command() before their definition. */
static uint32_t golden_tolerance = 0;
static uint32_t golden_samples[16384];
static uint32_t golden_count;
static bool has_golden;

/* Signature comparator: golden reference stored in flash. */
/* placeholder - removed */

/* Flash session header. */
typedef struct {
    uint32_t magic;
    uint32_t sample_count;
    uint64_t sample_rate;
    uint32_t channel_mask;
    uint32_t trigger_index;
    uint32_t pretrigger;
    uint8_t  trigger_mode;
    uint8_t  reserved[3];
} session_header_t;

/* ---------- send_frame (USB CDC) ---------- */
static void send_frame(uint8_t type, const uint8_t *p, size_t n)
{
    uint8_t frame[LAR2_MAX_FRAME];
    size_t len = lar2_encode(frame, type, tx_seq++, p, n), pos = 0;
    absolute_time_t deadline = make_timeout_time_ms(3000);
    /* Do not gate on tud_cdc_connected(): it can read false while the host
     * port is open (DTR churn), and dropping the frame there left the host
     * waiting until its own timeout with no error at all. The deadline
     * bounds the case where nobody reads. */
    while (pos < len && !time_reached(deadline)) {
        tud_task();
        uint32_t wrote = tud_cdc_write(frame + pos, len - pos);
        pos += wrote; tud_cdc_write_flush();
        if (wrote) deadline = make_timeout_time_ms(3000);
    }
    if (pos != len) sending = false;
}
static void ack(uint8_t command) { send_frame(LAR2_ACK, &command, 1); }
static void error(const char *text) { send_frame(LAR2_ERROR, (const uint8_t *)text, strlen(text)); }

/* ---------- abort / start capture ---------- */
static void abort_capture(void)
{
    pio_sm_set_enabled(pio, sm, false);
    dma_channel_abort(dma_ch);
    pio_sm_clear_fifos(pio, sm);
    capturing = sending = false;
    trigger_found = false;
    scan_pos = 0;
    estimated_baud = 0;
}

static bool capture_faulted(void)
{
    return (pio->fdebug & (1u << (PIO_FDEBUG_RXSTALL_LSB + sm))) ||
           (dma_hw->ch[dma_ch].ctrl_trig &
            (DMA_CH0_CTRL_TRIG_READ_ERROR_BITS | DMA_CH0_CTRL_TRIG_WRITE_ERROR_BITS));
}

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
    estimated_baud = 0;
    win_base = (const uint8_t *)samples;
    win_len = count;
    pio_sm_set_enabled(pio, sm, true);
}

/* Estimate UART baud rate from available samples (shortest pulse width). */
static uint32_t estimate_baud(const uint8_t *buf, uint32_t from, uint32_t to)
{
    uint32_t min_pulse = 1000000;
    uint32_t last = from;
    for (uint32_t i = from + 1; i < to; i++) {
        if ((buf[i] & 1) != (buf[i - 1] & 1)) {
            uint32_t width = i - last;
            if (width >= 3 && width < min_pulse) min_pulse = width;
            last = i;
        }
    }
    if (min_pulse == 1000000 || min_pulse == 0) return 0;
    return (rate / min_pulse);
}

static bool find_uart_byte(const uint8_t *buf, uint32_t from, uint32_t to,
                          uint32_t baud_samples, uint8_t target, uint32_t *hit)
{
    uint32_t i = from, half = baud_samples / 2;
    while (i + 8 * baud_samples < to) {
        while (i < to && (buf[i] & 1)) i++;
        if (i >= to) break;
        if (i + 2 >= to || (buf[i + 1] & 1) || (buf[i + 2] & 1)) { i++; continue; }
        uint32_t mid = i + baud_samples + half;
        if (mid + 8 * baud_samples > to) break;
        uint8_t byte = 0;
        for (int b = 0; b < 8; b++) {
            uint32_t sample = mid + b * baud_samples;
            if (sample >= to) break;
            if (buf[sample] & 1) byte |= (1 << b);
        }
        uint32_t stop = mid + 8 * baud_samples;
        if (stop < to && (buf[stop] & 1) && byte == target) {
            *hit = i; return true;
        }
        i = mid + 8 * baud_samples;
    }
    return false;
}

static void trigger_scan(void)
{
    if (trigger_found) return;
    uint32_t prod = produced_samples();
    if (prod > CAPTURE_BYTES) prod = CAPTURE_BYTES;
    if (prod <= scan_pos) return;
    uint32_t from = scan_pos;
    if (from < pretrigger) from = pretrigger;
    if (from < prod) {
        uint32_t hit;
        if (trig_mode == LAR2_TRIGGER_UART) {
            if (estimated_baud == 0 && prod >= pretrigger)
                estimated_baud = estimate_baud((const uint8_t *)samples, pretrigger, prod);
            if (estimated_baud > 0) {
                uint32_t baud_samples = rate / estimated_baud;
                if (baud_samples > 0 && find_uart_byte((const uint8_t *)samples, from, prod,
                                                      baud_samples, trig_value, &hit)) {
                    trigger_found = true; trigger_abs = hit;
                }
            }
        } else if (lar2_trigger_match((const uint8_t *)samples, from, prod, trig_mode,
                                      trig_channel, trig_edge, trig_value, trig_mask, &hit)) {
            trigger_found = true;
            trigger_abs = hit;
        }
    }
    scan_pos = prod;
}

/* ---------- command parser (LAR2) ---------- */
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
    if (capturing || sending) {
        /* RUNTIME is the diagnostic probe: it must answer while busy, that is
         * exactly when its state is interesting. */
        bool peek = type == LAR2_ANALYZE && n == LAR2_ANALYZE_SIZE &&
                    p[0] == LAR2_ANALYZE_RUNTIME;
        if (!peek) { error("busy"); return; }
    }
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
            } else if (trig_mode == 3) {
                /* UART byte trigger: trig_value = target byte */
            } else { error("invalid config"); break; }
            if (pretrigger >= count) { error("invalid config"); break; }
        }
        configured = true; ack(type); break;
    }
    case LAR2_ARM:
        if (n || !configured) { error("not configured"); break; }
        trigger_restarts = 0;
        ack(type); start_capture(); break;
    case LAR2_ANALYZE: {
        if (n != LAR2_ANALYZE_SIZE) { error("analyze length"); break; }
        uint8_t op = p[0];
        uint16_t arg = lar2_u16(p + 1);
        uint32_t arg32 = lar2_u32(p + 3);
        uint8_t result[16] = { LAR2_RESULT_OK, 0, 0, 0, 0, 0, 0, 0 };
        switch (op) {
        case LAR2_ANALYZE_DECODE:
            decode_len = 0;
            la_text t; la_text_init(&t, decode_buf, sizeof(decode_buf));
            decoders_run(win_base, win_len, (uint32_t)rate, arg, arg32, &t);
            decode_len = t.len;
            send_frame(LAR2_RESULT, (const uint8_t *)decode_buf, decode_len);
            break;
        case LAR2_ANALYZE_DECODE_ALL: {
            /* Comprehensive test capture: all four protocols on one 8 channel
             * window. arg carries the UART baud in units of 100, arg32 the CAN
             * bit rate; zero selects the signal generator defaults. */
            uint32_t uart_baud = arg ? (uint32_t)arg * 100u : 115200u;
            uint32_t can_baud = arg32 ? arg32 : 250000u;
            decode_len = 0;
            la_text t; la_text_init(&t, decode_buf, sizeof(decode_buf));
            decoders_run_map(win_base, win_len, (uint32_t)rate, LAR2_PROTO_ALL,
                             uart_baud, can_baud, &la_map_test8, &t);
            decode_len = t.len;
            send_frame(LAR2_RESULT, (const uint8_t *)decode_buf, decode_len);
            break;
        }
        case LAR2_ANALYZE_SAVE:
            if (arg >= MAX_SESSIONS) { result[0] = LAR2_RESULT_ERROR; send_frame(LAR2_RESULT, result, 1); break; }
            flash_save_session(arg, win_len, rate, mask, pretrigger, pretrigger, trig_mode);
            result[0] = LAR2_RESULT_OK; send_frame(LAR2_RESULT, result, 1);
            break;
        case LAR2_ANALYZE_LOAD:
            if (arg >= MAX_SESSIONS || !flash_load_session(arg)) {
                result[0] = LAR2_RESULT_ABSENT; send_frame(LAR2_RESULT, result, 1); break;
            }
            result[0] = LAR2_RESULT_OK; send_frame(LAR2_RESULT, result, 1);
            break;
        case LAR2_ANALYZE_SIG_SAVE:
            save_golden();
            result[0] = LAR2_RESULT_OK; send_frame(LAR2_RESULT, result, 1);
            break;
        case LAR2_ANALYZE_SIG_COMPARE: {
            golden_tolerance = arg32;
            decode_buf[0] = '\0';
            compare_with_golden();
            decode_len = strlen(decode_buf);
            send_frame(LAR2_RESULT, (const uint8_t *)decode_buf, decode_len);
            break;
        }
        case LAR2_ANALYZE_DIFF:
            decode_buf[0] = '\0';
            diff_with_loaded_session(arg);
            decode_len = strlen(decode_buf);
            send_frame(LAR2_RESULT, (const uint8_t *)decode_buf, decode_len);
            break;
        case LAR2_ANALYZE_SESSION: {
            if (arg >= MAX_SESSIONS) { result[0] = LAR2_RESULT_ERROR; send_frame(LAR2_RESULT, result, 1); break; }
            uint32_t offset = SESSION_OFFSET(arg);
            session_header_t hdr;
            memcpy(&hdr, (void *)(XIP_BASE + offset), sizeof(hdr));
            send_frame(LAR2_RESULT, (const uint8_t *)&hdr, sizeof(hdr));
            break;
        }
        case LAR2_ANALYZE_REPLAY:
            start_sending(win_len, LAR2_NO_TRIGGER, 0, 0);
            break;
        case LAR2_ANALYZE_STREAM_START:
            if (!configured) { result[0] = LAR2_RESULT_ERROR; send_frame(LAR2_RESULT, result, 1); break; }
            if (!start_stream(rate, mask, (arg & 1u) != 0)) {
                result[0] = LAR2_RESULT_ERROR;
                send_frame(LAR2_RESULT, result, 1);
                break;
            }
            send_frame(LAR2_RESULT, result, 1);
            break;
        case LAR2_ANALYZE_STREAM_STOP: {
            stop_stream();
            result[0] = LAR2_RESULT_OK;
            lar2_put32(result + 1, stream_sent_samples);
            lar2_put32(result + 5, stream_dropped);
            send_frame(LAR2_RESULT, result, 9);
            break;
        }
        case LAR2_ANALYZE_UI_TEXT: {
            /* What screen 5 is showing right now, so the host can check every
             * protocol without eyes on the panel. */
            const char *t = ui_decode_text();
            send_frame(LAR2_RESULT, (const uint8_t *)t, strlen(t));
            break;
        }
        case LAR2_ANALYZE_UI_TABLE: {
            /* The grid rows themselves, so a check covers the table the panel
             * draws and not only the decoder's summary line. */
            const char *t = ui_decode_table();
            send_frame(LAR2_RESULT, (const uint8_t *)t, strlen(t));
            break;
        }
        case LAR2_ANALYZE_UI_DEMO:
            ui_load_demo();
            result[0] = LAR2_RESULT_OK;
            send_frame(LAR2_RESULT, result, 1);
            break;
        case LAR2_ANALYZE_RUNTIME: {
            /* Why a triggered capture is not finishing: one block of state. */
            uint8_t st[32] = { LAR2_RESULT_OK };
            st[1] = (capturing ? 1 : 0) | (sending ? 2 : 0) |
                    (configured ? 4 : 0) | (trigger_found ? 8 : 0);
            lar2_put32(st + 2, produced_samples());
            lar2_put32(st + 6, scan_pos);
            lar2_put32(st + 10, trigger_abs);
            lar2_put32(st + 14, trigger_restarts);
            lar2_put32(st + 18, dma_channel_is_busy(dma_ch) ? 1u : 0u);
            lar2_put32(st + 22, pio->fdebug);
            lar2_put32(st + 26, count);
            st[30] = ui_screen_id();
            st[31] = ui_event_count();
            send_frame(LAR2_RESULT, st, 32);
            break;
        }
        case LAR2_ANALYZE_BOOTLOADER:
            /* The UF2 bootloader cannot be reached from software on this board
             * without BOOTSEL, so the running firmware hands itself over. Ack
             * first, then let the USB stack flush before rebooting. */
            result[0] = LAR2_RESULT_OK;
            send_frame(LAR2_RESULT, result, 1);
            busy_wait_ms(50);
            reset_usb_boot(0, 0);
            break;
        case LAR2_ANALYZE_PINPROBE: {
            /* Board bring-up aid: apply an internal pull to CH0..CH7 and read
             * the pads back. A floating input follows the pull, a driven input
             * does not, which tells "wire missing" from "signal idle". */
            for (uint i = 0; i < 23; i++) {
                if (arg == 0) gpio_pull_down(i);
                else if (arg == 1) gpio_pull_up(i);
                else gpio_disable_pulls(i);
            }
            busy_wait_us_32(50);
            uint32_t all = gpio_get_all();
            result[0] = LAR2_RESULT_OK;
            result[1] = (uint8_t)(all & 0xffu);
            /* Diagnostic tail: the full GPIO word and the pad registers of
             * CH0/CH1 show whether the pull bits are really set. */
            lar2_put32(result + 2, all);
            lar2_put32(result + 6, pads_bank0_hw->io[0]);
            lar2_put32(result + 10, pads_bank0_hw->io[1]);
            send_frame(LAR2_RESULT, result, 14);
            break;
        }
        default:
            result[0] = LAR2_RESULT_ERROR; send_frame(LAR2_RESULT, result, 1);
        }
        break;
    }
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

/* ---------- streaming ---------- */
static void __not_in_flash_func(stream_dma_isr)(void)
{
    if (!(dma_hw->ints0 & (1u << dma_ch))) return;
    dma_hw->ints0 = 1u << dma_ch;
    if (!streaming) return;
    /* Slot just filled is (prod % STREAM_SLOTS); arm the next one at once so
     * the PIO FIFO never stalls between slots. */
    uint32_t next = (stream_prod + 1u) % STREAM_SLOTS;
    stream_prod++;
    dma_channel_set_write_addr(dma_ch,
        (void *)((uint8_t *)samples + next * STREAM_SLOT_BYTES), false);
    dma_channel_set_trans_count(dma_ch, STREAM_SLOT_WORDS, true);
}

/* RLE: (run:u8, value:u8) pairs. Falls back to raw when compression does not
 * win, so a fast toggling line costs exactly one byte per sample. */
static void send_stream_slot(const uint8_t *p, uint32_t len, uint32_t offset)
{
    uint8_t block[LAR2_MAX_PAYLOAD];
    for (uint32_t i = 0; i < len; i += STREAM_CHUNK) {
        uint32_t n = len - i;
        if (n > STREAM_CHUNK) n = STREAM_CHUNK;
        uint8_t enc = 0;
        uint32_t out = n;
        if (stream_rle) {
            uint8_t *dst = block + 5;
            uint32_t o = 0;
            for (uint32_t k = 0; k < n;) {
                uint8_t v = p[i + k];
                uint32_t run = 1;
                while (k + run < n && p[i + k + run] == v && run < 255) run++;
                dst[o++] = (uint8_t)run;
                dst[o++] = v;
                k += run;
            }
            if (o < n) { enc = 1; out = o; }
        }
        block[0] = enc;
        lar2_put32(block + 1, offset + i);
        if (!enc) memcpy(block + 5, p + i, n);
        send_frame(LAR2_STREAM_BLOCK, block, 5 + out);
    }
}

static bool start_stream(uint64_t r, uint8_t m, bool rle)
{
    if (r > 1000000) return false;          /* USB CDC cannot keep up */
    abort_capture();
    rate = r; mask = m;
    count = CAPTURE_BYTES;
    trig_mode = LAR2_TRIGGER_MANUAL;
    pretrigger = 0;

    pio_sm_config c = logic_capture_program_get_default_config(offset);
    sm_config_set_in_pins(&c, 0);
    sm_config_set_in_shift(&c, true, true, 32);
    sm_config_set_out_shift(&c, true, false, 32);
    sm_config_set_clkdiv_int_frac(&c, (uint32_t)(25000000 / rate), 0);
    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_consecutive_pindirs(pio, sm, 0, 8, false);
    pio_sm_clkdiv_restart(pio, sm);
    pio_sm_put_blocking(pio, sm, 0xFFFFFFFFu);   /* X-- runs ~4G samples */
    pio_sm_exec(pio, sm, pio_encode_pull(false, true));
    pio_sm_exec(pio, sm, pio_encode_out(pio_x, 32));

    dma_channel_config dc = dma_channel_get_default_config(dma_ch);
    channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
    channel_config_set_read_increment(&dc, false);
    channel_config_set_write_increment(&dc, true);
    channel_config_set_dreq(&dc, pio_get_dreq(pio, sm, false));

    stream_prod = 0; stream_cons = 0;
    stream_sent_samples = 0; stream_dropped = 0;
    stream_rle = rle; streaming = true;
    pio->fdebug = 1u << (PIO_FDEBUG_RXSTALL_LSB + sm);
    dma_channel_configure(dma_ch, &dc, samples, &pio->rxf[sm], STREAM_SLOT_WORDS, true);
    dma_channel_set_irq0_enabled(dma_ch, true);
    pio_sm_set_enabled(pio, sm, true);
    return true;
}

static void stop_stream(void)
{
    if (!streaming) return;
    streaming = false;
    dma_channel_set_irq0_enabled(dma_ch, false);
    pio_sm_set_enabled(pio, sm, false);
    dma_channel_abort(dma_ch);
    pio_sm_clear_fifos(pio, sm);
    dma_hw->ints0 = 1u << dma_ch;
}

static void stream_task(void)
{
    if (!streaming) return;
    while (stream_cons < stream_prod) {
        uint32_t pending = stream_prod - stream_cons;
        if (pending >= STREAM_SLOTS) {
            /* The writer lapped the sender: skip the overwritten slots and
             * report how many samples were lost rather than shipping stale
             * data as if it were contiguous. */
            uint32_t skip = pending - (STREAM_SLOTS - 1u);
            stream_cons += skip;
            stream_dropped += skip * STREAM_SLOT_BYTES;
        }
        uint32_t slot = stream_cons % STREAM_SLOTS;
        uint32_t offset = stream_cons * STREAM_SLOT_BYTES;
        send_stream_slot((const uint8_t *)samples + slot * STREAM_SLOT_BYTES,
                         STREAM_SLOT_BYTES, offset);
        stream_sent_samples += STREAM_SLOT_BYTES;
        stream_cons++;
    }
}

static void capture_task(void)
{
    if (capturing) {
        bool dma_done = !dma_channel_is_busy(dma_ch);
        if (trig_mode == LAR2_TRIGGER_MANUAL) {
            if (dma_done) {
                pio_sm_set_enabled(pio, sm, false); capturing = false;
                if (capture_faulted()) { configured = false; error("capture FIFO stall/DMA error"); return; }
                win_base = (const uint8_t *)samples; win_len = count;
                start_sending(count, LAR2_NO_TRIGGER, 0, 0);
            }
        } else {
            trigger_scan();
            if (trigger_found) {
                /* Keep the trigger inside the window and keep the sample count
                 * exact. When the trigger lands too late for the requested
                 * pretrigger, slide the window right instead of starting over:
                 * re-arming a full buffer has a free-running period, and a
                 * periodic signal (a CAN burst every 100 ms, say) can lock that
                 * period to its own so the late trigger repeats forever and the
                 * capture never completes. */
                uint32_t max_prod = CAPTURE_BYTES - DMA_LAG_BYTES;
                uint32_t base = trigger_abs >= pretrigger ? trigger_abs - pretrigger : 0;
                if (base + count > max_prod && count <= max_prod) base = max_prod - count;
                if (base + count <= max_prod) {
                    if (produced_samples() >= base + count) {
                        pio_sm_set_enabled(pio, sm, false);
                        dma_channel_abort(dma_ch);
                        pio_sm_clear_fifos(pio, sm);
                        capturing = false;
                        if (capture_faulted()) { configured = false; error("capture FIFO stall/DMA error"); return; }
                        win_base = (const uint8_t *)samples + base;
                        win_len = count;
                        start_sending(count, trigger_abs - base, trigger_abs - base,
                                      LAR2_INFO_TRIGGER_FOUND);
                    }
                } else if (dma_done) {
                    /* Only reachable when count is nearly the whole buffer, so
                     * no window can hold both the trigger and that many
                     * samples. Re-arm and hope for an earlier trigger. */
                    if (++trigger_restarts > 2000) {
                        pio_sm_set_enabled(pio, sm, false); capturing = false;
                        configured = false; error("trigger too late"); return;
                    }
                    start_capture();
                }
            } else if (dma_done) {
                /* The window filled before the trigger arrived. A logic
                 * analyzer must be able to wait longer than one buffer, so
                 * re-arm the stream and keep scanning. */
                if (++trigger_restarts > 2000) {
                    pio_sm_set_enabled(pio, sm, false); capturing = false;
                    configured = false; error("no trigger"); return;
                }
                start_capture();
            }
        }
    }
    if (sending) {
        uint32_t n = win_len - sent;
        if (n > LAR2_MAX_PAYLOAD) n = LAR2_MAX_PAYLOAD;
        if (n) {
            crc = lar2_crc_update(crc, win_base + sent, n);
            send_frame(LAR2_SAMPLES, (const uint8_t *)(win_base + sent), n); sent += n;
        } else {
            uint8_t end[8]; lar2_put32(end, win_len); lar2_put32(end + 4, ~crc);
            send_frame(LAR2_END, end, sizeof(end)); sending = false;
        }
    }
}

/* ---------- Flash storage ---------- */
static void flash_save_session(uint32_t slot, uint32_t sample_count,
                              uint64_t sample_rate, uint32_t channel_mask,
                              uint32_t trigger_index, uint32_t pretrigger,
                              uint8_t trigger_mode)
{
    if (slot >= MAX_SESSIONS) return;
    if (sample_count > SESSION_MAX_SAMPLES) sample_count = SESSION_MAX_SAMPLES;
    uint32_t offset = SESSION_OFFSET(slot);
    session_header_t hdr = { SESSION_MAGIC, sample_count, sample_rate, channel_mask,
                             trigger_index, pretrigger, trigger_mode, {0} };
    /* Erase the header sector plus every sector the samples will touch.
     * Programming flash that was never erased silently keeps the old bits. */
    uint32_t sample_sectors = (sample_count + FLASH_SECTOR_BYTES - 1) / FLASH_SECTOR_BYTES;
    uint32_t span = FLASH_SECTOR_BYTES + sample_sectors * FLASH_SECTOR_BYTES;
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(offset, span);
    /* Write header (aligned to page boundary). */
    uint8_t page[FLASH_PAGE_BYTES];
    memcpy(page, &hdr, sizeof(hdr));
    memset(page + sizeof(hdr), 0, FLASH_PAGE_BYTES - sizeof(hdr));
    flash_range_program(offset, page, FLASH_PAGE_BYTES);
    /* Write samples at next sector boundary, page by page. */
    uint32_t samples_offset = offset + FLASH_SECTOR_BYTES;
    for (uint32_t i = 0; i < sample_count; i += FLASH_PAGE_BYTES) {
        uint32_t chunk = sample_count - i;
        if (chunk > FLASH_PAGE_BYTES) chunk = FLASH_PAGE_BYTES;
        memcpy(page, win_base + i, chunk);
        memset(page + chunk, 0, FLASH_PAGE_BYTES - chunk);
        flash_range_program(samples_offset + i, page, FLASH_PAGE_BYTES);
    }
    restore_interrupts(ints);
}

static bool flash_load_session(uint32_t slot)
{
    if (slot >= MAX_SESSIONS) return false;
    uint32_t offset = SESSION_OFFSET(slot);
    session_header_t hdr;
    memcpy(&hdr, (void *)(XIP_BASE + offset), sizeof(hdr));
    if (hdr.magic != SESSION_MAGIC || hdr.sample_count == 0 ||
        hdr.sample_count > SESSION_MAX_SAMPLES) return false;
    memcpy(samples, (void *)(XIP_BASE + offset + FLASH_SECTOR_BYTES), hdr.sample_count);
    rate = hdr.sample_rate;
    count = hdr.sample_count;
    mask = hdr.channel_mask;
    pretrigger = hdr.pretrigger;
    win_base = (const uint8_t *)samples;
    win_len = count;
    return true;
}

/* ---------- Signature comparator ---------- */
/* Golden buffer reduced to 64K samples (64 KB) to fit in SRAM. */

static int compare_with_golden(void)
{
    if (!has_golden) { snprintf(decode_buf, sizeof(decode_buf), "SIG: no golden ref"); return -1; }
    uint32_t capture_words = (win_len + 3) / 4;
    if (capture_words > CAPTURE_WORDS) capture_words = CAPTURE_WORDS;
    uint32_t compare_words = capture_words < golden_count ? capture_words : golden_count;
    uint32_t mismatches = 0;
    uint32_t first_mismatch = 0;
    const uint32_t *cur = (const uint32_t *)win_base;
    for (uint32_t i = 0; i < compare_words; i++) {
        if (cur[i] != golden_samples[i]) {
            if (mismatches == 0) first_mismatch = i * 4;
            mismatches++;
        }
    }
    if (mismatches <= golden_tolerance) {
        snprintf(decode_buf, sizeof(decode_buf), "SIG: MATCH (%u mismatches <= %u tol, %u samples)", mismatches, golden_tolerance, compare_words * 4);
    } else {
        snprintf(decode_buf, sizeof(decode_buf), "SIG: MISMATCH %u/%u samples at sample %u", mismatches, compare_words * 4, first_mismatch);
    }
    return (int)mismatches;
}

static void save_golden(void)
{
    uint32_t words = (win_len + 3) / 4;
    if (words > 16384) words = 16384;
    memcpy(golden_samples, win_base, words * sizeof(uint32_t));
    golden_count = words;
    has_golden = true;
}

/* ---------- Capture diff ---------- */
/* Compare two sessions from flash by loading each into samples buffer alternately. */
static int diff_with_loaded_session(uint32_t slot)
{
    if (slot >= MAX_SESSIONS) { snprintf(decode_buf, sizeof(decode_buf), "DIFF: bad slot"); return -1; }
    /* Load session from flash into a temporary buffer (reuse golden buffer). */
    uint32_t offset = SESSION_OFFSET(slot);
    session_header_t hdr;
    memcpy(&hdr, (void *)(XIP_BASE + offset), sizeof(hdr));
    if (hdr.magic != SESSION_MAGIC) { snprintf(decode_buf, sizeof(decode_buf), "DIFF: no session in slot %u", slot); return -1; }
    uint32_t limit_samples = win_len < hdr.sample_count ? win_len : hdr.sample_count;
    uint32_t limit_words = (limit_samples + 3) / 4;
    if (limit_words > 16384) limit_words = 16384;
    /* Load from flash into golden buffer temporarily. */
    uint32_t samples_offset = offset + FLASH_SECTOR_BYTES;
    memcpy(golden_samples, (void *)(XIP_BASE + samples_offset), limit_words * sizeof(uint32_t));
    /* Compare current samples with loaded session. */
    const uint32_t *cur = (const uint32_t *)win_base;
    uint32_t first_diff = 0;
    int found = 0;
    for (uint32_t i = 0; i < limit_words; i++) {
        if (cur[i] != golden_samples[i]) { first_diff = i; found = 1; break; }
    }
    if (!found) {
        snprintf(decode_buf, sizeof(decode_buf), "DIFF: identical");
    } else {
        snprintf(decode_buf, sizeof(decode_buf), "DIFF: first mismatch at sample %u (A=0x%08X B=0x%08X)",
                 first_diff * 4, cur[first_diff], golden_samples[first_diff]);
    }
    return found;
}

/* ---------- main ---------- */
int main(void)
{
    set_sys_clock_khz(100000, true);
    tft_init();
    /* The UI owns the panel from here on: capture config, trigger config,
     * waveform and on-device decode. */
    ui_init();
    stdio_init_all();
    for (uint i = 0; i < 8; i++) { pio_gpio_init(pio, i); gpio_disable_pulls(i); }
    sm = pio_claim_unused_sm(pio, true);
    offset = pio_add_program(pio, &logic_capture_program);
    dma_ch = dma_claim_unused_channel(true);
    /* Slot-completion interrupt for streaming; the snapshot path never
     * enables it, so it stays quiet until ANALYZE_STREAM_START. */
    irq_set_exclusive_handler(DMA_IRQ_0, stream_dma_isr);
    irq_set_enabled(DMA_IRQ_0, true);
    bool connected = false;
    bool ui_pending = false;
    while (true) {
        tud_task();
        bool now = tud_cdc_connected();
        if (!now) {
            if (connected) {
                stop_stream();
                abort_capture();
                configured = false;
                memset(&parser, 0, sizeof(parser));
            }
        } else {
            while (tud_cdc_available()) {
                uint8_t b; if (tud_cdc_read(&b, 1) != 1) break;
                /* Single key commands are the button side of the parallel
                 * control. They only make sense while no LAR2 frame is being
                 * assembled, and 'L' can never be a key. */
                if (!parser.used && b != 'L' && strchr("udobUDOB", b)) {
                    ui_handle_command((char)b);
                    continue;
                }
                int r = lar2_feed(&parser, b);
                if (r < 0) { abort_capture(); configured = false; error("frame CRC/header"); }
                if (r == 1) {
                    command();
                    /* Release the parser now: until the next byte arrives
                     * used == total, and the serial-key check below treats a
                     * non-empty parser as "mid-frame", so the first key after
                     * every LAR2 frame was silently swallowed. */
                    parser.used = parser.total = 0;
                    break;
                }
            }
        }
        /* UI capture: the buttons and the serial keys feed the same event
         * queue, and a request runs the very same capture path as LAR2 ARM. */
        if (!capturing && !sending && ui_take_capture_request()) {
            rate = ui_cfg.rate; count = ui_cfg.count; mask = ui_cfg.mask;
            pretrigger = ui_cfg.pretrigger; trig_mode = ui_cfg.trig_mode;
            trig_channel = ui_cfg.trig_channel; trig_edge = ui_cfg.trig_edge;
            trig_value = ui_cfg.trig_value; trig_mask = ui_cfg.trig_mask;
            trigger_restarts = 0;
            start_capture();
            ui_pending = true;
        }
        stream_task();
        capture_task();
        if (ui_pending && !capturing) {
            ui_pending = false;
            ui_set_capture(win_base, win_len, rate, pretrigger, !capture_faulted());
        }
        /* Streaming owns the CPU budget: a TFT redraw in the middle of a slot
         * can stall USB long enough to overrun the ring. The panel simply
         * keeps showing the last screen for the duration of the capture. */
        if (!streaming) {
            ui_set_now_ms(to_ms_since_boot(get_absolute_time()));
            ui_tick();
        }
        connected = now; tight_loop_contents();
    }
}
