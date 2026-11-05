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

int main(void)
{
    test_text_builder();
    test_uart();
    test_i2c();
    test_spi();
    test_can();
    test_mask_selection();
    printf("test_decode: all assertions passed\n");
    return 0;
}
