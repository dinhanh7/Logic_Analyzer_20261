/* SPDX-License-Identifier: GPL-3.0-or-later
 * Protocol auto-detection. See autodetect.h for the approach. */
#include <string.h>

#include "autodetect.h"

static inline unsigned bit_at(const uint8_t *buf, uint32_t i, unsigned ch)
{
    return (buf[i] >> ch) & 1u;
}

void la_profile_channels(const uint8_t *buf, uint32_t len, la_chprof out[8])
{
    uint32_t n = len < LA_AUTO_WINDOW ? len : LA_AUTO_WINDOW;
    memset(out, 0, sizeof(la_chprof) * 8);
    if (!buf || n < 2) return;

    for (unsigned ch = 0; ch < 8; ch++) {
        la_chprof *p = &out[ch];
        p->total = n;
        /* Idle level = level of the longest stable run (not sample 0: a
         * capture can start mid-frame or before the transmitter is live). */
        uint32_t min_run = n, max_run = 0, run = 1;
        uint32_t edges = 0, high = bit_at(buf, 0, ch);
        uint32_t best_run = 0;
        unsigned best_level = (unsigned)bit_at(buf, 0, ch);
        unsigned run_level = best_level;

        for (uint32_t i = 1; i < n; i++) {
            unsigned a = bit_at(buf, i - 1, ch);
            unsigned b = bit_at(buf, i, ch);
            if (b) high++;
            if (a == b) {
                run++;
            } else {
                edges++;
                if (run < min_run) min_run = run;
                if (run > max_run) max_run = run;
                if (run > best_run) { best_run = run; best_level = run_level; }
                run = 1;
                run_level = b;
            }
        }
        if (run > best_run) { best_run = run; best_level = run_level; }
        p->idle = (uint8_t)best_level;
        if (run < min_run) min_run = run;
        if (run > max_run) max_run = run;

        p->edges = edges;
        p->min_run = edges ? min_run : n;
        p->max_run = max_run;
        p->high = high;
        p->active = edges >= LA_AUTO_MIN_EDGES;

        uint32_t duty = n ? (high * 100u) / n : 0;
        p->clockish = p->active && duty >= 25 && duty <= 75 &&
                      p->min_run * 4u <= p->max_run && p->edges >= 8;
    }
}

/* Longest run of identical bits between the first and last edge.
 * Excludes the idle tail so a short burst in a long capture still shows
 * whether bit stuffing is being obeyed. */
static uint32_t longest_run_inside(const uint8_t *buf, uint32_t n, unsigned ch)
{
    uint32_t first = 0, last = 0;
    for (uint32_t i = 1; i < n; i++)
        if (bit_at(buf, i, ch) != bit_at(buf, i - 1, ch)) {
            if (!first) first = i;
            last = i;
        }
    if (!first || last <= first) return 0;

    uint32_t best = 1, run = 1;
    for (uint32_t i = first + 1; i <= last; i++) {
        if (bit_at(buf, i, ch) == bit_at(buf, i - 1, ch)) {
            run++;
            if (run > best) best = run;
        } else {
            run = 1;
        }
    }
    return best;
}

/* Observed shortest run must sit within ±15 % of the expected bit width for
 * the baud the UI is using. Without this gate the CAN decoder happily emits
 * a fake frame from any falling edge (UART start bits score higher as CAN
 * than as UART because one frame expands into ID+DLC+data packets). */
static int bit_matches(uint32_t observed, uint32_t expected)
{
    if (!observed || !expected) return 0;
    /* A 100 kHz I2C half-clock is 25% wider than a CAN250k bit.
     * The old inclusive 25% gate accepted that alias as a fabricated CAN
     * frame. Require a closer match; low-resolution ambiguous input should
     * remain unclassified rather than produce a confident wrong protocol. */
    return (uint64_t)observed * 100u >= (uint64_t)expected * 85u &&
           (uint64_t)observed * 100u <= (uint64_t)expected * 115u;
}

static uint32_t score_uart(const uint8_t *buf, uint32_t n, uint32_t rate,
                           uint32_t baud, unsigned ch)
{
    la_channel_map m = la_map_test8;
    la_pktlist l;
    m.uart = (uint8_t)ch;
    return decoders_packets(buf, n, rate, LAR2_PROTO_UART, baud, 250000, &m, &l);
}

static uint32_t score_i2c(const uint8_t *buf, uint32_t n, uint32_t rate,
                          unsigned scl, unsigned sda, const la_chprof *pscl)
{
    /* Random bit noise produces min_run of 1 and a flood of false STARTs.
     * A real SCL half-period is several samples even at Fast-mode+. */
    if (!pscl || pscl->min_run < 4 || pscl->edges < 16) return 0;

    la_channel_map m = la_map_test8;
    la_pktlist l;
    m.i2c_scl = (uint8_t)scl;
    m.i2c_sda = (uint8_t)sda;
    unsigned pk = decoders_packets(buf, n, rate, LAR2_PROTO_I2C, 115200, 250000, &m, &l);
    if (pk < 2) return 0;

    /* A real transfer ACKs at least once. Noise mixes ACK and NAK freely;
     * requiring a clean ACK drops the garbage without hurting the demo. */
    unsigned acks = 0, bytes = 0;
    for (unsigned i = 0; i < l.n; i++) {
        if (l.pkt[i].flags & LA_PKT_ACK) acks++;
        if (l.pkt[i].flags & LA_PKT_NAK) continue;
        if (l.pkt[i].flags & (LA_PKT_ADDR | LA_PKT_ACK | LA_PKT_NAK |
                               LA_PKT_ID | LA_PKT_START | LA_PKT_STOP)) {
            if (l.pkt[i].flags & LA_PKT_ADDR) bytes++;
        } else {
            bytes++;                    /* LA_PKT_DATA == 0 */
        }
    }
    if (!acks || bytes < 2) return 0;
    return bytes;
}

static uint32_t score_spi(const uint8_t *buf, uint32_t n, uint32_t rate,
                          unsigned cs, unsigned sck, unsigned mosi,
                          const la_chprof *pcs, const la_chprof *psck)
{
    /* CS frames the burst: a few edges. SCK clocks the bits: many edges.
     * Noise has every line thrashing equally, so this ratio kills it. */
    if (!pcs || !psck) return 0;
    if (pcs->edges < 2 || pcs->edges > 64) return 0;
    if (psck->edges < 16 || psck->edges < pcs->edges * 4u) return 0;
    if (psck->min_run < 2) return 0;

    la_channel_map m = la_map_test8;
    la_pktlist l;
    m.spi_cs = (uint8_t)cs;
    m.spi_sck = (uint8_t)sck;
    m.spi_mosi = (uint8_t)mosi;
    return decoders_packets(buf, n, rate, LAR2_PROTO_SPI, 115200, 250000, &m, &l);
}

/* One CAN frame becomes many packets (ID hi/lo, DLC, data). Score the frame
 * as 1 + data bytes so it stays comparable with UART/SPI byte counts and
 * cannot drown a real UART decode by packet inflation alone. */
static uint32_t score_can(const uint8_t *buf, uint32_t n, uint32_t rate,
                          uint32_t baud, unsigned ch)
{
    la_channel_map m = la_map_test8;
    la_pktlist l;
    m.can = (uint8_t)ch;
    unsigned pk = decoders_packets(buf, n, rate, LAR2_PROTO_CAN, 115200, baud, &m, &l);
    if (pk < 3) return 0;               /* need ID + DLC at least */
    /* LA_PKT_DATA is 0x00, so test by exclusion: anything without LA_PKT_ID
     * is payload. */
    unsigned data = 0;
    for (unsigned i = 0; i < l.n; i++)
        if (!(l.pkt[i].flags & LA_PKT_ID)) data++;
    return 1u + data;
}

static void consider(la_detect *best, uint8_t proto, uint32_t score,
                     const la_channel_map *map)
{
    if (score <= best->score) return;
    best->score = score;
    best->proto = proto;
    best->map = *map;
}

la_detect decoders_autodetect(const uint8_t *buf, uint32_t len, uint32_t rate,
                              uint32_t uart_baud, uint32_t can_baud)
{
    la_detect best;
    memset(&best, 0, sizeof best);
    if (!buf || len < 64 || !rate) return best;

    uint32_t n = len < LA_AUTO_WINDOW ? len : LA_AUTO_WINDOW;

    la_chprof prof[8];
    la_profile_channels(buf, len, prof);

    unsigned act[8], n_act = 0;
    for (unsigned ch = 0; ch < 8; ch++)
        if (prof[ch].active) act[n_act++] = ch;

    uint32_t uart_bit = uart_baud ? rate / uart_baud : 0;
    uint32_t can_bit  = can_baud  ? rate / can_baud  : 0;

    /* ---------- multi-wire first ----------
     * A real SPI/I2C bus has several active lines; letting them win before
     * single-wire guesses keeps a MOSI line from being claimed as UART. */

    for (unsigned c = 0; c < n_act; c++) {
        for (unsigned d = 0; d < n_act; d++) {
            unsigned scl = act[c], sda = act[d];
            if (scl == sda) continue;
            uint32_t s = score_i2c(buf, n, rate, scl, sda, &prof[scl]);
            if (s > best.score) {
                la_channel_map m = la_map_test8;
                m.i2c_scl = (uint8_t)scl;
                m.i2c_sda = (uint8_t)sda;
                consider(&best, LAR2_PROTO_I2C, s, &m);
            }
        }
    }

    for (unsigned c = 0; c < n_act; c++) {
        unsigned sck = act[c];
        for (unsigned d = 0; d < n_act; d++) {
            unsigned mosi = act[d];
            if (mosi == sck) continue;
            for (unsigned si = 0; si < n_act; si++) {
                unsigned cs = act[si];
                if (cs == sck || cs == mosi) continue;
                uint32_t sc = score_spi(buf, n, rate, cs, sck, mosi,
                                        &prof[cs], &prof[sck]);
                if (sc > best.score) {
                    la_channel_map m = la_map_test8;
                    m.spi_cs = (uint8_t)cs;
                    m.spi_sck = (uint8_t)sck;
                    m.spi_mosi = (uint8_t)mosi;
                    consider(&best, LAR2_PROTO_SPI, sc, &m);
                }
            }
        }
    }

    /* ---------- UART ---------- */
    for (unsigned i = 0; i < n_act; i++) {
        unsigned ch = act[i];
        if (!prof[ch].idle) continue;           /* 8N1 idles high */
        /* When the UI picked a baud, the shortest run must match it. When
         * baud is AUTO (0) the UART decoder measures bit time itself. */
        if (uart_bit && !bit_matches(prof[ch].min_run, uart_bit)) continue;
        uint32_t s = score_uart(buf, n, rate, uart_baud, ch);
        if (s > best.score) {
            la_channel_map m = la_map_test8;
            m.uart = (uint8_t)ch;
            consider(&best, LAR2_PROTO_UART, s, &m);
        }
    }

    /* ---------- CAN ---------- */
    for (unsigned i = 0; i < n_act; i++) {
        unsigned ch = act[i];
        if (!can_bit || !bit_matches(prof[ch].min_run, can_bit)) continue;
        /* Stuffing: no in-frame run longer than five bit times. UART with
         * matching bit time (rare) still fails this; HELLO alone is only
         * four zeros so the bit-time gate above is the real separator. */
        uint32_t run = longest_run_inside(buf, n, ch);
        uint32_t bit_est = prof[ch].min_run ? prof[ch].min_run : 1;
        if (run && run / bit_est > 5) continue;
        uint32_t s = score_can(buf, n, rate, can_baud, ch);
        if (s > best.score) {
            la_channel_map m = la_map_test8;
            m.can = (uint8_t)ch;
            consider(&best, LAR2_PROTO_CAN, s, &m);
        }
    }

    /* A single packet is a coincidence (one stray edge ≈ one UART byte). */
    if (best.score < 2) {
        memset(&best, 0, sizeof best);
        return best;
    }
    best.confident = 1;
    return best;
}
