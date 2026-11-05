/* SPDX-License-Identifier: GPL-3.0-or-later
 * On-device protocol decoders (UART, I2C, SPI, CAN).
 *
 * Every decoder works on the same capture layout: one byte per sample with
 * bit n carrying channel n. They are deliberately allocation free and only
 * append text, so the firmware and the host tests share one implementation.
 */
#include "decoders.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void la_text_init(la_text *t, char *buf, size_t cap)
{
    t->buf = buf;
    t->cap = cap;
    t->len = 0;
    if (cap) buf[0] = '\0';
}

void la_text_addf(la_text *t, const char *fmt, ...)
{
    if (!t || !t->cap || t->len + 1 >= t->cap) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(t->buf + t->len, t->cap - t->len, fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    size_t room = t->cap - t->len - 1;
    t->len += ((size_t)n < room) ? (size_t)n : room;
}

static inline unsigned sample_bit(const uint8_t *buf, uint32_t i, unsigned ch)
{
    return (buf[i] >> ch) & 1u;
}

/* ---------- UART ---------- */
/* Samples per bit in Q16. When the host knows the baud rate it is exact;
 * otherwise the shortest run in the stream is used, which is one bit for any
 * byte other than 0x00/0xff. */
static uint32_t uart_bit_width_q16(const uint8_t *buf, uint32_t len, uint32_t rate,
                                   uint32_t baud)
{
    if (baud) return (uint32_t)(((uint64_t)rate << 16) / baud);

    uint32_t min_run = UINT32_MAX, start = 0, i;
    for (i = 1; i < len; i++) {
        if (sample_bit(buf, i, 0) != sample_bit(buf, i - 1, 0)) {
            uint32_t w = i - start;
            if (w >= 2 && w < min_run) min_run = w;
            start = i;
        }
    }
    if (min_run == UINT32_MAX) return 0;

    /* Average the runs that are plausibly one bit long: sampling jitter and
     * a non integer samples-per-bit ratio split one bit over two lengths. */
    uint64_t sum = 0;
    uint32_t n = 0;
    start = 0;
    for (i = 1; i < len; i++) {
        if (sample_bit(buf, i, 0) != sample_bit(buf, i - 1, 0)) {
            uint32_t w = i - start;
            if (w >= min_run && w * 2 <= min_run * 3) { sum += w; n++; }
            start = i;
        }
    }
    return n ? (uint32_t)((sum << 16) / n) : 0;
}

void decode_uart(const uint8_t *buf, uint32_t len, uint32_t rate, uint32_t baud, la_text *t)
{
    if (len < 20) { la_text_addf(t, "UART: no data\n"); return; }
    uint32_t bw = uart_bit_width_q16(buf, len, rate, baud);
    if (!bw) { la_text_addf(t, "UART: no transitions\n"); return; }

    uint64_t found_baud = ((uint64_t)rate << 16) / bw;
    char text[96];
    size_t pos = 0;
    uint32_t i = 1;

    while (i + 1 < len && pos < sizeof(text) - 1) {
        if (sample_bit(buf, i, 0) || !sample_bit(buf, i - 1, 0)) { i++; continue; }
        uint32_t start = i;
        uint8_t byte = 0;
        int ok = 1;
        for (int b = 0; b < 8; b++) {
            uint32_t s = start + (uint32_t)(((uint64_t)bw * (2u * (unsigned)b + 3u)) >> 17);
            if (s >= len) { ok = 0; break; }
            if (sample_bit(buf, s, 0)) byte |= (uint8_t)(1u << b);
        }
        uint32_t stop = start + (uint32_t)(((uint64_t)bw * 19u) >> 17);
        if (!ok || stop >= len || !sample_bit(buf, stop, 0)) {
            i = start + (uint32_t)(bw >> 16);   /* resync one bit later */
            if (i <= start) i = start + 1;
            continue;
        }
        text[pos++] = (byte >= 32 && byte < 127) ? (char)byte : '.';
        /* Resume at the middle of the stop bit: the next start bit may begin
         * one sample earlier than the nominal ten bit times. */
        i = stop;
    }
    text[pos] = '\0';
    if (pos)
        la_text_addf(t, "UART %llu 8N1: \"%s\"\n", (unsigned long long)found_baud, text);
    else
        la_text_addf(t, "UART: no bytes at %llu bps\n", (unsigned long long)found_baud);
}

/* ---------- I2C ---------- */
/* SCL = CH0, SDA = CH1. Bytes are MSB first, ACK is the ninth clock. */
static int i2c_wait_rise(const uint8_t *buf, uint32_t *i, uint32_t len)
{
    while (*i < len) {
        if (sample_bit(buf, *i, 0) && !sample_bit(buf, *i - 1, 0)) return 1;
        (*i)++;
    }
    return 0;
}

void decode_i2c(const uint8_t *buf, uint32_t len, uint32_t rate, la_text *t)
{
    (void)rate;
    if (len < 20) { la_text_addf(t, "I2C: no data\n"); return; }

    char out[160];
    size_t pos = 0;
    unsigned bytes = 0;
    uint32_t i = 1;

    while (i + 1 < len && bytes < 8) {
        /* START: SDA falls while SCL is high. */
        if (!(sample_bit(buf, i, 0) && !sample_bit(buf, i, 1) && sample_bit(buf, i - 1, 1))) {
            i++;
            continue;
        }
        i++;
        int first = 1;
        for (;;) {
            uint8_t byte = 0;
            int ok = 1;
            for (int b = 0; b < 8; b++) {
                if (!i2c_wait_rise(buf, &i, len)) { ok = 0; break; }
                i++;
                if (sample_bit(buf, i - 1, 1)) byte |= (uint8_t)(1u << (7 - b));
            }
            if (!ok) break;
            int ack = 0;
            if (!i2c_wait_rise(buf, &i, len)) break;
            i++;
            ack = !sample_bit(buf, i - 1, 1);

            if (first) {
                pos += (size_t)snprintf(out + pos, sizeof(out) - pos, "0x%02X %c %s",
                                        byte >> 1, (byte & 1) ? 'R' : 'W',
                                        ack ? "ACK" : "NAK");
                first = 0;
            } else if (pos + 24 < sizeof(out)) {
                pos += (size_t)snprintf(out + pos, sizeof(out) - pos, " 0x%02X %s",
                                        byte, ack ? "ACK" : "NAK");
            }
            bytes++;
            if (!ack) break;
            /* STOP (SDA rises) or repeated START ends the transfer. */
            if (i + 1 < len && sample_bit(buf, i, 0)) {
                if (sample_bit(buf, i, 1) && !sample_bit(buf, i - 1, 1)) break;
                if (!sample_bit(buf, i, 1) && sample_bit(buf, i - 1, 1)) break;
            }
        }
    }

    if (bytes) la_text_addf(t, "I2C: %s\n", out);
    else la_text_addf(t, "I2C: no transaction found\n");
}

/* ---------- SPI ---------- */
/* CS = CH0, SCK = CH1, MOSI = CH2, MISO = CH3, mode 0 (sample on rising). */
void decode_spi(const uint8_t *buf, uint32_t len, uint32_t rate, la_text *t)
{
    (void)rate;
    if (len < 20) { la_text_addf(t, "SPI: no data\n"); return; }

    char out[160];
    size_t pos = 0;
    unsigned frames = 0;
    uint32_t i = 1;

    while (i < len && frames < 4 && pos + 24 < sizeof(out)) {
        /* CS falling edge starts a transaction. */
        if (sample_bit(buf, i, 0) || !sample_bit(buf, i - 1, 0)) { i++; continue; }
        frames++;
        i++;
        unsigned nbits = 0, nbytes = 0;
        uint8_t byte = 0;
        while (i < len && !sample_bit(buf, i, 0)) {
            if (sample_bit(buf, i, 1) && !sample_bit(buf, i - 1, 1)) {
                byte = (uint8_t)((byte << 1) | sample_bit(buf, i, 2));
                if (++nbits == 8) {
                    if (nbytes < 8) {
                        pos += (size_t)snprintf(out + pos, sizeof(out) - pos, "%s%02X",
                                                nbytes ? " " : "", byte);
                    }
                    nbytes++;
                    nbits = 0;
                    byte = 0;
                }
            }
            i++;
        }
        if (nbits) {
            pos += (size_t)snprintf(out + pos, sizeof(out) - pos, " %s[%u bits]",
                                    nbytes ? "" : "", nbits);
        }
        if (pos + 8 < sizeof(out)) pos += (size_t)snprintf(out + pos, sizeof(out) - pos, " |");
    }

    if (frames) la_text_addf(t, "SPI: %s\n", out);
    else la_text_addf(t, "SPI: no transaction found\n");
}

/* ---------- CAN ---------- */
/* CH0 carries the single ended TX line: dominant = 0, recessive = 1.
 * Only standard 11 bit data frames are decoded; the CRC field is skipped
 * because it needs no information the host does not already have. */
typedef struct {
    const uint8_t *buf;
    uint32_t len;
    uint32_t bw;     /* samples per bit, Q16 */
    uint32_t start;  /* first sample of the SOF bit */
    uint32_t bit;    /* next bit index, 0 = SOF */
    uint32_t run;    /* consecutive identical bits on the wire */
    uint8_t last;
    int overflow;
} can_reader;

static uint8_t can_next_bit(can_reader *r)
{
    if (r->run == 5) {          /* bit stuffing: drop the inserted bit */
        r->bit++;
        r->run = 1;
        r->last ^= 1u;
    }
    uint32_t idx = r->start + (uint32_t)(((uint64_t)r->bw * (2u * r->bit + 1u)) >> 17);
    r->bit++;
    uint8_t b = 1;
    if (idx < r->len) b = (uint8_t)sample_bit(r->buf, idx, 0);
    else r->overflow = 1;
    if (b == r->last) r->run++;
    else { r->run = 1; r->last = b; }
    return b;
}

void decode_can(const uint8_t *buf, uint32_t len, uint32_t rate, uint32_t baud, la_text *t)
{
    if (!baud) { la_text_addf(t, "CAN: needs the bit rate (pass baud)\n"); return; }
    if (len < 50) { la_text_addf(t, "CAN: no data\n"); return; }

    uint32_t bw = (uint32_t)(((uint64_t)rate << 16) / baud);
    if (!bw) { la_text_addf(t, "CAN: sample rate too low for this baud\n"); return; }

    uint32_t start = 0;
    int found = 0;
    for (uint32_t i = 1; i < len; i++) {
        if (!sample_bit(buf, i, 0) && sample_bit(buf, i - 1, 0)) { start = i; found = 1; break; }
    }
    if (!found) { la_text_addf(t, "CAN: no SOF\n"); return; }

    can_reader r = { buf, len, bw, start, 1, 1, 0, 0 };
    uint32_t id = 0;
    for (int k = 0; k < 11; k++) id = (id << 1) | can_next_bit(&r);
    uint8_t rtr = can_next_bit(&r);
    uint8_t ide = can_next_bit(&r);
    (void)can_next_bit(&r);                 /* r0 */
    uint8_t dlc = 0;
    for (int k = 0; k < 4; k++) dlc = (uint8_t)((dlc << 1) | can_next_bit(&r));

    if (r.overflow) { la_text_addf(t, "CAN: truncated frame\n"); return; }
    if (ide) { la_text_addf(t, "CAN: extended frames are not decoded\n"); return; }
    if (rtr) { la_text_addf(t, "CAN ID=0x%03lX RTR\n", (unsigned long)id); return; }
    if (dlc > 8) dlc = 8;

    char out[96];
    size_t pos = (size_t)snprintf(out, sizeof(out), "CAN ID=0x%03lX DLC=%u DATA=",
                                  (unsigned long)id, dlc);
    for (unsigned d = 0; d < dlc && pos + 4 < sizeof(out); d++) {
        uint8_t byte = 0;
        for (int k = 0; k < 8; k++) byte = (uint8_t)((byte << 1) | can_next_bit(&r));
        pos += (size_t)snprintf(out + pos, sizeof(out) - pos, "%02X ", byte);
    }
    if (r.overflow) la_text_addf(t, "CAN: truncated frame\n");
    else la_text_addf(t, "%s\n", out);
}

unsigned decoders_run(const uint8_t *buf, uint32_t len, uint32_t rate,
                      uint8_t mask, uint32_t baud, la_text *t)
{
    if (!buf || !len) { la_text_addf(t, "no samples\n"); return 0; }
    unsigned n = 0;
    if (mask & LAR2_PROTO_UART) { decode_uart(buf, len, rate, baud, t); n++; }
    if (mask & LAR2_PROTO_I2C)  { decode_i2c(buf, len, rate, t); n++; }
    if (mask & LAR2_PROTO_SPI)  { decode_spi(buf, len, rate, t); n++; }
    if (mask & LAR2_PROTO_CAN)  { decode_can(buf, len, rate, baud, t); n++; }
    if (!n) la_text_addf(t, "no protocol selected\n");
    return n;
}

