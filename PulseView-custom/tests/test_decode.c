/* SPDX-License-Identifier: GPL-3.0-or-later
 * Host tests for the on-device decoders. Synthetic waveforms are generated
 * with known timing, so a regression in the bit timing shows up immediately
 * without a board on the desk. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "decoders.h"

#define CAP 200000
static uint8_t wf[CAP];
static size_t wf_n;

static void wf_reset(void) { wf_n = 0; }
static void wf_level(uint32_t n, uint8_t value)
{
    while (n-- && wf_n < CAP) wf[wf_n++] = value;
}

/* ---------- UART ---------- */
static uint64_t uart_acc;
static void uart_emit_bit(uint8_t value, uint32_t bw_q16)
{
    uart_acc += bw_q16;
    uint32_t end = (uint32_t)(uart_acc >> 16);
    while (wf_n < end && wf_n < CAP) wf[wf_n++] = value;
}

static size_t uart_build(const char *text, uint32_t rate, uint32_t baud)
{
    uint32_t bw = (uint32_t)(((uint64_t)rate << 16) / baud);
    wf_reset();
    uart_acc = 0;
    wf_level((uint32_t)(bw >> 16) * 2, 1);      /* idle high */
    uart_acc = (uint64_t)wf_n << 16;
    for (const char *c = text; *c; c++) {
        uint8_t byte = (uint8_t)*c;
        uart_emit_bit(0, bw);                    /* start bit */
        for (int b = 0; b < 8; b++) uart_emit_bit((byte >> b) & 1, bw);
        uart_emit_bit(1, bw);                    /* stop bit */
    }
    wf_level((uint32_t)(bw >> 16) * 4, 1);
    return wf_n;
}

static void test_uart(void)
{
    char text[256];
    la_text t;
    size_t n;

    /* Known baud: exact fractional bit timing. */
    n = uart_build("LA1234", 1000000, 115200);
    la_text_init(&t, text, sizeof(text));
    decode_uart(wf, (uint32_t)n, 1000000, 115200, &t);
    printf("uart(known baud): %s", text);
    assert(strstr(text, "UART 115200 8N1") != NULL);
    assert(strstr(text, "\"LA1234\"") != NULL);

    /* Auto baud: the estimate must still recover the payload. */
    la_text_init(&t, text, sizeof(text));
    decode_uart(wf, (uint32_t)n, 1000000, 0, &t);
    printf("uart(auto baud): %s", text);
    assert(strstr(text, "\"LA1234\"") != NULL);

    /* 25 MHz capture of a 1 Mbaud stream: 25 samples per bit. */
    n = uart_build("OK", 25000000, 1000000);
    la_text_init(&t, text, sizeof(text));
    decode_uart(wf, (uint32_t)n, 25000000, 1000000, &t);
    printf("uart(25MHz): %s", text);
    assert(strstr(text, "\"OK\"") != NULL);

    /* Silence must not invent bytes. */
    wf_reset();
    wf_level(500, 1);
    la_text_init(&t, text, sizeof(text));
    decode_uart(wf, 500, 1000000, 0, &t);
    assert(strstr(text, "no transitions") != NULL);
    la_text_init(&t, text, sizeof(text));
    decode_uart(wf, 500, 1000000, 115200, &t);
    assert(strstr(text, "no bytes") != NULL);
}

/* ---------- I2C ---------- */
/* SCL = CH0, SDA = CH1, half period = 5 samples at 1 MHz => 100 kHz. */
static void i2c_clock(uint8_t sda)
{
    wf_level(5, 0);              /* SCL low */
    wf_level(5, (uint8_t)(1 | (sda ? 2 : 0)));
}

static size_t i2c_build(const uint8_t *bytes, size_t count, int nack_last)
{
    wf_reset();
    wf_level(20, 3);                              /* idle, both high */
    wf_level(1, 1);                               /* START: SDA falls */
    for (size_t k = 0; k < count; k++) {
        for (int b = 7; b >= 0; b--) i2c_clock((bytes[k] >> b) & 1);
        /* ACK from the slave, or NAK on the final byte. */
        uint8_t ack = (nack_last && k + 1 == count) ? 1 : 0;
        wf_level(5, 0);                           /* SCL low, SDA released */
        wf_level(5, (uint8_t)(1 | (ack ? 2 : 0)));
    }
    wf_level(5, 0);                               /* SCL low, SDA low */
    wf_level(1, 1);                               /* SCL high, SDA low */
    wf_level(1, 3);                               /* STOP: SDA rises */
    wf_level(20, 3);
    return wf_n;
}

static void test_i2c(void)
{
    char text[256];
    la_text t;
    const uint8_t frame[] = { 0xD0, 0x01, 0x5A };  /* 0x68 write, reg 1, data 0x5A */
    size_t n = i2c_build(frame, 3, 0);
    la_text_init(&t, text, sizeof(text));
    decode_i2c(wf, (uint32_t)n, 1000000, &t);
    printf("i2c: %s", text);
    assert(strstr(text, "I2C: 0x68 W ACK") != NULL);
    assert(strstr(text, "0x01 ACK") != NULL);
    assert(strstr(text, "0x5A ACK") != NULL);

    /* A read address reports R. */
    const uint8_t read[] = { 0xD1, 0x5A };
    n = i2c_build(read, 2, 1);
    la_text_init(&t, text, sizeof(text));
    decode_i2c(wf, (uint32_t)n, 1000000, &t);
    printf("i2c read: %s", text);
    assert(strstr(text, "0x68 R") != NULL);
    assert(strstr(text, "NAK") != NULL);

    /* Idle bus: no START, no output bytes. */
    wf_reset();
    wf_level(200, 3);
    la_text_init(&t, text, sizeof(text));
    decode_i2c(wf, 200, 1000000, &t);
    assert(strstr(text, "no transaction found") != NULL);
}

/* ---------- SPI ---------- */
/* CS = CH0, SCK = CH1, MOSI = CH2. 4 samples per SCK period at 4 MHz. */
static size_t spi_build(const uint8_t *bytes, size_t count)
{
    wf_reset();
    wf_level(20, 1);                              /* CS high, SCK low */
    for (size_t k = 0; k < count; k++) {
        for (int b = 7; b >= 0; b--) {
            uint8_t mosi = (uint8_t)(((bytes[k] >> b) & 1) << 2);
            wf_level(2, mosi);                    /* SCK low */
            wf_level(2, (uint8_t)(2 | mosi));     /* SCK high: sample */
        }
        wf_level(10, 0);                          /* CS still low between bytes */
    }
    wf_level(20, 1);                              /* CS high */
    return wf_n;
}

static void test_spi(void)
{
    char text[256];
    la_text t;
    const uint8_t frame[] = { 0xDE, 0xAD, 0xBE, 0xEF };
    size_t n = spi_build(frame, 4);
    la_text_init(&t, text, sizeof(text));
    decode_spi(wf, (uint32_t)n, 4000000, &t);
    printf("spi: %s", text);
    assert(strstr(text, "SPI: DE AD BE EF") != NULL);

    wf_reset();
    wf_level(200, 1);
    la_text_init(&t, text, sizeof(text));
    decode_spi(wf, 200, 4000000, &t);
    assert(strstr(text, "no transaction found") != NULL);
}

/* ---------- CAN ---------- */
/* CH0 only. 20 samples per bit at 5 MHz / 250 kbps. */
static uint32_t can_acc;
static void can_wire_bit(uint8_t b)
{
    can_acc += 20u << 16;
    uint32_t end = can_acc >> 16;
    while (wf_n < end && wf_n < CAP) wf[wf_n++] = b;
}

static void can_stuffed(uint8_t b, uint32_t *run, uint8_t *last)
{
    can_wire_bit(b);
    if (b == *last) (*run)++; else { *run = 1; *last = b; }
    if (*run == 5) { can_wire_bit((uint8_t)!b); *run = 1; *last = (uint8_t)!b; }
}

static size_t can_build(uint32_t id, const uint8_t *data, uint8_t dlc)
{
    wf_reset();
    can_acc = 0;
    wf_level(40, 1);                              /* idle recessive */
    can_acc = (uint64_t)wf_n << 16;
    uint32_t run = 0;
    uint8_t last = 1;

    can_stuffed(0, &run, &last);                  /* SOF */
    for (int b = 10; b >= 0; b--) can_stuffed((id >> b) & 1, &run, &last);
    can_stuffed(0, &run, &last);                  /* RTR */
    can_stuffed(0, &run, &last);                  /* IDE */
    can_stuffed(0, &run, &last);                  /* r0 */
    for (int b = 3; b >= 0; b--) can_stuffed((dlc >> b) & 1, &run, &last);
    for (uint8_t d = 0; d < dlc; d++)
        for (int b = 7; b >= 0; b--) can_stuffed((data[d] >> b) & 1, &run, &last);
    for (int b = 0; b < 15; b++) can_stuffed(1, &run, &last);   /* CRC placeholder */
    can_wire_bit(1);                              /* CRC delimiter */
    can_wire_bit(1);                              /* ACK slot */
    can_wire_bit(1);                              /* ACK delimiter */
    for (int b = 0; b < 7; b++) can_wire_bit(1);  /* EOF */
    wf_level(40, 1);
    return wf_n;
}

static void test_can(void)
{
    char text[256];
    la_text t;
    const uint8_t data[] = { 0xDE, 0xAD, 0xBE, 0xEF };
    size_t n = can_build(0x123, data, 4);
    la_text_init(&t, text, sizeof(text));
    decode_can(wf, (uint32_t)n, 5000000, 250000, &t);
    printf("can: %s", text);
    assert(strstr(text, "CAN ID=0x123 DLC=4 DATA=DE AD BE EF") != NULL);

    /* Without a bit rate the decoder must say so instead of guessing. */
    la_text_init(&t, text, sizeof(text));
    decode_can(wf, (uint32_t)n, 5000000, 0, &t);
    assert(strstr(text, "needs the bit rate") != NULL);

    /* Idle recessive line: no SOF. */
    wf_reset();
    wf_level(500, 1);
    la_text_init(&t, text, sizeof(text));
    decode_can(wf, 500, 5000000, 250000, &t);
    assert(strstr(text, "no SOF") != NULL);
}

/* ---------- shared helpers ---------- */
static void test_text_builder(void)
{
    char small[16];
    la_text t;
    la_text_init(&t, small, sizeof(small));
    la_text_addf(&t, "0123456789");
    la_text_addf(&t, "abcdef");
    assert(t.len < sizeof(small));
    assert(strlen(small) < sizeof(small));
    printf("text truncation: \"%s\" (%zu bytes)\n", small, strlen(small));
}

static void test_mask_selection(void)
{
    char text[256];
    la_text t;
    const uint8_t frame[] = { 0xDE, 0xAD };
    size_t n = spi_build(frame, 2);
    la_text_init(&t, text, sizeof(text));
    unsigned lines = decoders_run(wf, (uint32_t)n, 4000000, LAR2_PROTO_SPI, 0, &t);
    assert(lines == 1);
    assert(strstr(text, "SPI:") != NULL);
    assert(strstr(text, "UART") == NULL);

    la_text_init(&t, text, sizeof(text));
    assert(decoders_run(NULL, 0, 1000000, LAR2_PROTO_ALL, 0, &t) == 0);
    assert(strstr(text, "no samples") != NULL);
}

/* ---------- four protocols on one 8 channel capture ---------- */
/* The real bench test puts UART on CH0, I2C on CH1/CH2, SPI on CH3..CH6 and
 * CAN on CH7, all running at once. This builds the same thing synthetically at
 * 25 MHz so a channel map regression fails without a board on the desk. */
#define T8_LEN 24000
static uint8_t ch_bits[8][T8_LEN];

static void t8_clear(void)
{
    for (unsigned c = 0; c < 8; c++)
        for (uint32_t i = 0; i < T8_LEN; i++) ch_bits[c][i] = 1;   /* idle high */
}

static void t8_level(unsigned ch, uint32_t from, uint32_t samples, unsigned level)
{
    for (uint32_t i = from; i < from + samples && i < T8_LEN; i++)
        ch_bits[ch][i] = level ? 1 : 0;
}

/* UART 8N1 on `ch` at 115200, 25 MHz sample clock. */
static uint32_t t8_uart(uint32_t pos, unsigned ch, const char *text)
{
    const uint32_t bit = 25000000u / 115200u;
    for (const char *c = text; *c; c++) {
        uint8_t byte = (uint8_t)*c;
        t8_level(ch, pos, bit, 0); pos += bit;
        for (int b = 0; b < 8; b++) { t8_level(ch, pos, bit, (byte >> b) & 1); pos += bit; }
        t8_level(ch, pos, bit, 1); pos += bit;
    }
    return pos;
}

/* I2C 100 kHz on scl/sda: START, then address, register and data with ACKs. */
static uint32_t t8_i2c(uint32_t pos, unsigned scl, unsigned sda, const uint8_t *bytes,
                       size_t count)
{
    const uint32_t half = 25000000u / 100000u / 2;   /* 125 samples */
    t8_level(scl, pos, half, 1); t8_level(sda, pos, half, 1);
    pos += half;
    t8_level(sda, pos, half, 0);                     /* START */
    t8_level(scl, pos, half, 1);
    pos += half;
    for (size_t k = 0; k < count; k++) {
        for (int b = 7; b >= 0; b--) {
            t8_level(scl, pos, half, 0);
            /* SDA holds its level across the whole clock, not just the low half. */
            t8_level(sda, pos, 2 * half, (bytes[k] >> b) & 1);
            pos += half;
            t8_level(scl, pos, half, 1);
            pos += half;
        }
        t8_level(scl, pos, half, 0);                 /* ACK slot */
        t8_level(sda, pos, 2 * half, 0);
        pos += half;
        t8_level(scl, pos, half, 1);
        pos += half;
    }
    t8_level(scl, pos, half, 0);
    t8_level(sda, pos, half, 0);
    pos += half;
    t8_level(scl, pos, half, 1);                     /* STOP */
    t8_level(sda, pos, half, 0);
    pos += half;
    t8_level(sda, pos, half, 1);
    pos += half;
    return pos;
}

/* SPI mode 0 on cs/sck/mosi/miso at 1 MHz. */
static uint32_t t8_spi(uint32_t pos, unsigned cs, unsigned sck, unsigned mosi,
                       unsigned miso, const uint8_t *bytes, size_t count)
{
    const uint32_t half = 25000000u / 1000000u / 2;  /* 12 samples */
    const uint32_t span = (uint32_t)count * 8u * 2u * half;
    t8_level(cs, pos, span, 0);                      /* CS low for the frame */
    for (size_t k = 0; k < count; k++) {
        for (int b = 7; b >= 0; b--) {
            uint8_t bit = (uint8_t)((bytes[k] >> b) & 1);
            t8_level(sck, pos, half, 0);
            t8_level(mosi, pos, 2 * half, bit);      /* data stable all period */
            t8_level(miso, pos, 2 * half, bit);
            pos += half;
            t8_level(sck, pos, half, 1);
            pos += half;
        }
    }
    t8_level(cs, pos, half, 1);
    pos += half;
    return pos;
}

/* CAN 250 kbps standard data frame on `ch`, 100 samples per bit. */
static uint32_t t8_can(uint32_t pos, unsigned ch, uint32_t id, const uint8_t *data,
                       uint8_t dlc)
{
    const uint32_t bit = 25000000u / 250000u;        /* 100 samples */
    uint8_t raw[128];
    size_t n = 0;
    raw[n++] = 0;                                    /* SOF */
    for (int b = 10; b >= 0; b--) raw[n++] = (uint8_t)((id >> b) & 1);
    raw[n++] = 0;                                    /* RTR */
    raw[n++] = 0;                                    /* IDE */
    raw[n++] = 0;                                    /* r0 */
    for (int b = 3; b >= 0; b--) raw[n++] = (uint8_t)((dlc >> b) & 1);
    for (uint8_t d = 0; d < dlc; d++)
        for (int b = 7; b >= 0; b--) raw[n++] = (uint8_t)((data[d] >> b) & 1);
    for (int b = 0; b < 15; b++) raw[n++] = 1;       /* CRC placeholder */
    raw[n++] = 1; raw[n++] = 1; raw[n++] = 1;        /* delimiters + ACK */
    for (int b = 0; b < 7; b++) raw[n++] = 1;        /* EOF */

    uint8_t run = 0, last = 1;
    for (size_t i = 0; i < n; i++) {
        t8_level(ch, pos, bit, raw[i]);
        pos += bit;
        if (raw[i] == last) run++; else { run = 1; last = raw[i]; }
        if (run == 5) {                              /* stuffed bit */
            t8_level(ch, pos, bit, (uint8_t)!last);
            pos += bit;
            run = 1; last = (uint8_t)!last;
        }
    }
    return pos;
}

static void test_map8(void)
{
    static const uint8_t i2c_bytes[] = { 0xD0, 0x01, 0x5A };
    static const uint8_t spi_bytes[] = { 0xDE, 0xAD };
    static const uint8_t can_data[] = { 0xDE, 0xAD, 0xBE, 0xEF };
    char text[768];
    la_text t;

    t8_clear();
    t8_uart(100, 0, "LA");
    t8_i2c(100, 1, 2, i2c_bytes, 3);
    t8_spi(100, 3, 4, 5, 6, spi_bytes, 2);
    t8_can(100, 7, 0x123, can_data, 4);

    for (uint32_t i = 0; i < T8_LEN; i++) {
        uint8_t v = 0;
        for (unsigned c = 0; c < 8; c++) v |= (uint8_t)((ch_bits[c][i] & 1u) << c);
        wf[i] = v;
    }

    la_text_init(&t, text, sizeof(text));
    unsigned lines = decoders_run_map(wf, T8_LEN, 25000000, LAR2_PROTO_ALL,
                                      115200, 250000, &la_map_test8, &t);
    printf("map8:\n%s", text);
    assert(lines == 4);
    assert(strstr(text, "UART 115200 8N1: \"LA\"") != NULL);
    assert(strstr(text, "I2C: 0x68 W ACK") != NULL);
    assert(strstr(text, "SPI: DE AD") != NULL);
    assert(strstr(text, "CAN ID=0x123 DLC=4 DATA=DE AD BE EF") != NULL);

    /* The legacy map reads everything from CH0 and must find only UART. */
    la_text_init(&t, text, sizeof(text));
    decoders_run(wf, T8_LEN, 25000000, LAR2_PROTO_ALL, 0, &t);
    assert(strstr(text, "UART") != NULL);
    assert(strstr(text, "0x123") == NULL);
}

/* The decode screen's table needs one row per byte with its own sample
 * position; these check that the structured layer agrees with the text layer
 * on the same waveform, which is the only way the table can be trusted. */
static void test_packets(void)
{
    static const uint8_t i2c_bytes[] = { 0xD0, 0x01, 0x5A };
    static const uint8_t spi_bytes[] = { 0xDE, 0xAD };
    static const uint8_t can_data[] = { 0xDE, 0xAD, 0xBE, 0xEF };
    la_pktlist l;

    /* Same four-protocol capture the text test uses, rebuilt here so this
     * test does not depend on test_map8's locals. */
    t8_clear();
    t8_uart(100, 0, "LA");
    t8_i2c(100, 1, 2, i2c_bytes, 3);
    t8_spi(100, 3, 4, 5, 6, spi_bytes, 2);
    t8_can(100, 7, 0x123, can_data, 4);
    for (uint32_t i = 0; i < T8_LEN; i++) {
        uint8_t v = 0;
        for (unsigned c = 0; c < 8; c++) v |= (uint8_t)((ch_bits[c][i] & 1u) << c);
        wf[i] = v;
    }

    /* UART "LA" on CH0 of the 8-channel map. */
    unsigned n = decoders_packets(wf, T8_LEN, 25000000, LAR2_PROTO_UART,
                                  115200, 250000, &la_map_test8, &l);
    printf("packets uart: n=%u\n", n);
    assert(n == 2);
    assert(l.pkt[0].value == 'L');
    assert(l.pkt[1].value == 'A');
    assert(l.pkt[0].at < l.pkt[1].at);          /* in capture order */
    assert(l.pkt[1].gap > 0);                   /* time since the byte before */
    assert(l.ch == la_map_test8.uart);

    /* I2C: address byte, its ACK, data byte, its ACK. */
    n = decoders_packets(wf, T8_LEN, 25000000, LAR2_PROTO_I2C,
                         115200, 250000, &la_map_test8, &l);
    printf("packets i2c: n=%u\n", n);
    assert(n >= 4);
    assert(l.pkt[0].flags & LA_PKT_ADDR);
    assert(l.pkt[0].value == 0xD0);             /* 0x68 << 1 | W */
    assert(l.pkt[1].flags & LA_PKT_ACK);
    assert(l.pkt[2].value == 0x01);
    assert(l.pkt[4].value == 0x5A);

    /* SPI: the four frame bytes, MSB first. */
    n = decoders_packets(wf, T8_LEN, 25000000, LAR2_PROTO_SPI,
                         115200, 250000, &la_map_test8, &l);
    printf("packets spi: n=%u\n", n);
    assert(n == 2);
    assert(l.pkt[0].value == 0xDE);
    assert(l.pkt[1].value == 0xAD);

    /* CAN: ID high, ID low, DLC, then the payload. */
    n = decoders_packets(wf, T8_LEN, 25000000, LAR2_PROTO_CAN,
                         115200, 250000, &la_map_test8, &l);
    printf("packets can: n=%u\n", n);
    assert(n == 3 + 4);
    assert(l.pkt[0].flags & LA_PKT_ID);
    assert(l.pkt[0].value == 0x01);             /* 0x123 >> 8 */
    assert(l.pkt[1].value == 0x23);
    assert(l.pkt[2].value == 4);                /* DLC */
    assert(l.pkt[3].value == 0xDE);
    assert(l.pkt[6].value == 0xEF);

    /* No capture, no packets: the screen must not index an empty list. */
    n = decoders_packets(NULL, 0, 25000000, LAR2_PROTO_UART,
                         115200, 250000, &la_map_test8, &l);
    assert(n == 0 && l.n == 0);
}

int main(void)
{
    test_text_builder();
    test_uart();
    test_i2c();
    test_spi();
    test_can();
    test_mask_selection();
    test_map8();
    test_packets();
    printf("test_decode: all assertions passed\n");
    return 0;
}
