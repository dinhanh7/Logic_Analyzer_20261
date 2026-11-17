/* SPDX-License-Identifier: GPL-3.0-or-later
 * Host tests for protocol auto-detection.
 *
 * The point of these is the failure mode the user asked about: does AUTO
 * mistake one bus for another? So every case asserts the protocol AND the
 * channel map, and the negative cases assert that nothing is claimed at all
 * rather than a confident wrong answer.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "autodetect.h"
#include "decoders.h"

#define CAP 40000
static uint8_t wf[CAP];

static void wf_clear(void) { memset(wf, 0, sizeof wf); }

static void set(uint32_t pos, unsigned ch, int level)
{
    if (pos >= CAP) return;
    if (level) wf[pos] |= (uint8_t)(1u << ch);
    else wf[pos] &= (uint8_t)~(1u << ch);
}

static void span(uint32_t from, uint32_t to, unsigned ch, int level)
{
    if (to > CAP) to = CAP;
    for (uint32_t i = from; i < to; i++) set(i, ch, level);
}

/* ---------- builders (same wire formats the demo uses) ---------- */

static uint32_t build_uart(uint32_t pos, unsigned ch, const char *msg, uint32_t bit)
{
    span(pos, CAP, ch, 1);
    pos += bit;
    for (const char *p = msg; *p; p++) {
        span(pos, pos + bit, ch, 0);
        pos += bit;
        for (int k = 0; k < 8; k++, pos += bit)
            span(pos, pos + bit, ch, (*p >> k) & 1);
        span(pos, pos + bit, ch, 1);
        pos += bit;
    }
    return pos;
}

static uint32_t build_i2c(uint32_t pos, unsigned scl, unsigned sda, uint32_t half)
{
    span(pos, CAP, scl, 1);
    span(pos, CAP, sda, 1);
    pos += 4 * half;
    span(pos, pos + 2 * half, sda, 0);              /* START */
    pos += 2 * half;

    const uint8_t bytes[2] = { 0xA2, 0x5A };
    for (int k = 0; k < 2; k++) {
        for (int b = 7; b >= 0; b--) {
            span(pos, pos + half, scl, 0);
            span(pos, pos + 2 * half, sda, (bytes[k] >> b) & 1);
            pos += half;
            span(pos, pos + half, scl, 1);
            pos += half;
        }
        span(pos, pos + half, scl, 0);
        span(pos, pos + 2 * half, sda, 0);          /* ACK */
        pos += half;
        span(pos, pos + half, scl, 1);
        pos += half;
    }
    span(pos, pos + half, scl, 0);
    span(pos, pos + half, sda, 0);
    pos += half;
    span(pos, pos + half, scl, 1);
    pos += half;
    span(pos, pos + half, sda, 1);                  /* STOP */
    pos += half;
    return pos;
}

static uint32_t build_spi(uint32_t pos, unsigned cs, unsigned sck, unsigned mosi,
                          uint32_t half)
{
    const uint8_t bytes[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
    span(pos, CAP, cs, 1);
    span(pos, CAP, sck, 0);
    pos += 2 * half;
    uint32_t frame = 2 * half + 4u * 8u * 2u * half;
    span(pos, pos + frame, cs, 0);
    pos += half;
    for (int k = 0; k < 4; k++)
        for (int b = 7; b >= 0; b--) {
            span(pos, pos + half, sck, 0);
            span(pos, pos + 2 * half, mosi, (bytes[k] >> b) & 1);
            pos += half;
            span(pos, pos + half, sck, 1);
            pos += half;
        }
    span(pos, CAP, cs, 1);
    pos += 2 * half;
    return pos;
}

static uint32_t build_can(uint32_t pos, unsigned ch, uint32_t id,
                          const uint8_t *data, unsigned dlc, uint32_t bit)
{
    span(pos, CAP, ch, 1);
    pos += 4 * bit;

    uint8_t logical[128];
    unsigned n = 0, k;
    logical[n++] = 0;                               /* SOF */
    for (int b = 10; b >= 0; b--) logical[n++] = (id >> b) & 1u;
    logical[n++] = 0;                               /* RTR */
    logical[n++] = 0;                               /* IDE */
    logical[n++] = 0;                               /* r0 */
    for (int b = 3; b >= 0; b--) logical[n++] = (dlc >> b) & 1u;
    for (k = 0; k < dlc; k++)
        for (int b = 7; b >= 0; b--) logical[n++] = (data[k] >> b) & 1u;

    uint8_t last = 1;
    unsigned run = 0;
    for (k = 0; k < n; k++) {
        uint8_t v = logical[k];
        span(pos, pos + bit, ch, v);
        pos += bit;
        if (v == last) run++;
        else { run = 1; last = v; }
        if (run == 5) {                             /* stuffed bit */
            span(pos, pos + bit, ch, !v);
            pos += bit;
            run = 1;
            last = (uint8_t)!v;
        }
    }
    span(pos, CAP, ch, 1);
    return pos;
}

/* ---------- cases ---------- */

static const char *proto_name(uint8_t p)
{
    switch (p) {
    case LAR2_PROTO_UART: return "UART";
    case LAR2_PROTO_I2C:  return "I2C";
    case LAR2_PROTO_SPI:  return "SPI";
    case LAR2_PROTO_CAN:  return "CAN";
    default:              return "none";
    }
}

static void expect(const char *what, la_detect d, uint8_t proto)
{
    printf("%-34s -> %-4s score=%u\n", what, proto_name(d.proto), d.score);
    assert(d.proto == proto);
}

/* Each protocol alone, on channels that are NOT the default map, so a pass
 * proves the detector found the channel and not the hardcoded layout. */
static void test_uart_moved(void)
{
    wf_clear();
    build_uart(500, 5, "HELLO", 217);
    la_detect d = decoders_autodetect(wf, CAP, 25000000, 115200, 250000);
    expect("UART alone on CH5", d, LAR2_PROTO_UART);
    assert(d.map.uart == 5);
}

static void test_i2c_moved(void)
{
    wf_clear();
    build_i2c(500, 6, 7, 40);
    la_detect d = decoders_autodetect(wf, CAP, 25000000, 115200, 250000);
    expect("I2C alone on CH6/CH7", d, LAR2_PROTO_I2C);
    assert(d.map.i2c_scl == 6 && d.map.i2c_sda == 7);
}

static void test_spi_moved(void)
{
    wf_clear();
    build_spi(500, 1, 2, 3, 20);
    la_detect d = decoders_autodetect(wf, CAP, 25000000, 115200, 250000);
    expect("SPI alone on CH1/2/3", d, LAR2_PROTO_SPI);
    assert(d.map.spi_cs == 1 && d.map.spi_sck == 2 && d.map.spi_mosi == 3);
}

static void test_can_moved(void)
{
    static const uint8_t data[6] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xC0, 0xFF };
    wf_clear();
    build_can(500, 4, 0x123, data, 6, 100);
    la_detect d = decoders_autodetect(wf, CAP, 25000000, 115200, 250000);
    expect("CAN alone on CH4", d, LAR2_PROTO_CAN);
    assert(d.map.can == 4);
}

/* The hard case: all four buses at once on non-default channels. The detector
 * must separate them, which is where a shape-only guess falls apart. */
static void test_all_four(void)
{
    static const uint8_t can_data[6] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xC0, 0xFF };
    wf_clear();
    build_uart(200, 2, "HELLO", 217);
    build_i2c(6000, 0, 1, 40);
    build_spi(12000, 5, 6, 7, 20);
    build_can(20000, 3, 0x123, can_data, 6, 100);

    la_detect d = decoders_autodetect(wf, CAP, 25000000, 115200, 250000);
    printf("%-34s -> %-4s score=%u\n", "all four at once", proto_name(d.proto), d.score);
    /* One winner is all the screen can show; it must be a real bus and its
     * channels must be the ones that bus actually sits on. */
    assert(d.proto != 0);
    switch (d.proto) {
    case LAR2_PROTO_UART: assert(d.map.uart == 2); break;
    case LAR2_PROTO_I2C:  assert(d.map.i2c_scl == 0 && d.map.i2c_sda == 1); break;
    case LAR2_PROTO_SPI:  assert(d.map.spi_cs == 5 && d.map.spi_sck == 6); break;
    case LAR2_PROTO_CAN:  assert(d.map.can == 3); break;
    default: assert(0);
    }
}

/* Negative cases: nothing here is a bus, and AUTO must say so rather than
 * invent one. */
static void test_silent(void)
{
    wf_clear();
    la_detect d = decoders_autodetect(wf, CAP, 25000000, 115200, 250000);
    expect("all channels idle", d, 0);
}

static void test_square_wave_only(void)
{
    wf_clear();
    for (uint32_t i = 0; i < CAP; i++)
        if ((i / 250u) & 1u) wf[i] |= 0x40u;        /* CH6 50 kHz */
    la_detect d = decoders_autodetect(wf, CAP, 25000000, 115200, 250000);
    expect("plain square wave, no bus", d, 0);
}

static void test_noise(void)
{
    wf_clear();
    uint32_t s = 12345;
    for (uint32_t i = 0; i < CAP; i++) {
        s = s * 1103515245u + 12345u;
        wf[i] = (uint8_t)((s >> 16) & 0xFF);
    }
    la_detect d = decoders_autodetect(wf, CAP, 25000000, 115200, 250000);
    expect("random noise on all 8", d, 0);
}

/* A single stray edge must not read as one UART byte. */
static void test_one_edge(void)
{
    wf_clear();
    span(0, CAP, 3, 1);
    span(1000, 1005, 3, 0);
    la_detect d = decoders_autodetect(wf, CAP, 25000000, 115200, 250000);
    expect("one stray edge on CH3", d, 0);
}

/* Too short a capture to judge. */
static void test_too_short(void)
{
    wf_clear();
    build_uart(0, 0, "H", 217);
    la_detect d = decoders_autodetect(wf, 40, 25000000, 115200, 250000);
    expect("capture too short", d, 0);
}

/* Explicit confusion checks: the wrong protocol must not win. */
static void test_no_confusion(void)
{
    static const uint8_t can_data[6] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xC0, 0xFF };
    la_detect d;

    wf_clear();
    build_uart(500, 5, "HELLO", 217);
    d = decoders_autodetect(wf, CAP, 25000000, 115200, 250000);
    assert(d.proto == LAR2_PROTO_UART);
    assert(d.proto != LAR2_PROTO_CAN);

    wf_clear();
    build_can(500, 4, 0x123, can_data, 6, 100);
    d = decoders_autodetect(wf, CAP, 25000000, 115200, 250000);
    assert(d.proto == LAR2_PROTO_CAN);
    assert(d.proto != LAR2_PROTO_UART);

    wf_clear();
    build_spi(500, 1, 2, 3, 20);
    d = decoders_autodetect(wf, CAP, 25000000, 115200, 250000);
    assert(d.proto == LAR2_PROTO_SPI);
    assert(d.proto != LAR2_PROTO_I2C);

    wf_clear();
    build_i2c(500, 6, 7, 40);
    d = decoders_autodetect(wf, CAP, 25000000, 115200, 250000);
    assert(d.proto == LAR2_PROTO_I2C);
    assert(d.proto != LAR2_PROTO_SPI);

    printf("%-34s -> ok\n", "no cross-protocol confusion");
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);   /* keep output when an assert aborts */
    test_uart_moved();
    test_i2c_moved();
    test_spi_moved();
    test_can_moved();
    test_all_four();
    test_silent();
    test_square_wave_only();
    test_noise();
    test_one_edge();
    test_too_short();
    test_no_confusion();
    printf("test_autodetect: all assertions passed\n");
    return 0;
}
