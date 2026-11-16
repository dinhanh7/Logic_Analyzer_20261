/* SPDX-License-Identifier: GPL-3.0-or-later
 * On-device protocol decoders. Pure C with no Pico SDK dependency so the
 * exact same code runs on the RP2350 and in the host test suite. */
#ifndef DECODERS_H
#define DECODERS_H
#include <stddef.h>
#include <stdint.h>
#include "lar2.h"

/* Append-only text builder with a hard capacity. */
typedef struct {
    char *buf;
    size_t cap;
    size_t len;
} la_text;

void la_text_init(la_text *t, char *buf, size_t cap);
void la_text_addf(la_text *t, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

/* buf holds one byte per sample, bit n = channel n.
 * baud is only used by the UART and CAN decoders; 0 means "estimate" for
 * UART and "unknown" (reported as an error) for CAN. */
void decode_uart(const uint8_t *buf, uint32_t len, uint32_t rate, uint32_t baud, la_text *t);
void decode_i2c(const uint8_t *buf, uint32_t len, uint32_t rate, la_text *t);
void decode_spi(const uint8_t *buf, uint32_t len, uint32_t rate, la_text *t);
void decode_can(const uint8_t *buf, uint32_t len, uint32_t rate, uint32_t baud, la_text *t);

/* Channel-explicit variants: the same decoders, but each protocol line is
 * read from the channel named here instead of the default CH0..CH3. */
void decode_uart_ch(const uint8_t *buf, uint32_t len, uint32_t rate, uint32_t baud,
                    unsigned ch, la_text *t);
void decode_i2c_ch(const uint8_t *buf, uint32_t len, uint32_t rate,
                   unsigned scl_ch, unsigned sda_ch, la_text *t);
void decode_spi_ch(const uint8_t *buf, uint32_t len, uint32_t rate,
                   unsigned cs_ch, unsigned sck_ch, unsigned mosi_ch,
                   unsigned miso_ch, la_text *t);
void decode_can_ch(const uint8_t *buf, uint32_t len, uint32_t rate, uint32_t baud,
                   unsigned ch, la_text *t);

/* Which capture channel carries each protocol line. */
typedef struct {
    uint8_t uart;
    uint8_t i2c_scl, i2c_sda;
    uint8_t spi_cs, spi_sck, spi_mosi, spi_miso;
    uint8_t can;
} la_channel_map;

/* Every line on CH0..CH3: the historical single-protocol layout. */
extern const la_channel_map la_map_single;
/* Four protocols at once: UART CH0, I2C CH1/2, SPI CH3-6, CAN CH7. */
extern const la_channel_map la_map_test8;

/* ---------- structured packets ----------
 * The text decoders above collapse a transfer into one summary line, which is
 * all a log needs but not enough for a table: the decode screen shows one row
 * per byte with its hex value, its ASCII, and the time since the byte before.
 * These extract that, keeping the sample index of every byte so the UI can
 * also draw the real waveform behind the selected row. */
#define LA_PKT_MAX 64

/* What the byte is, so the row can say ACK/NAK instead of a bare number. */
#define LA_PKT_DATA 0x00
#define LA_PKT_ADDR 0x01
#define LA_PKT_ACK  0x02
#define LA_PKT_NAK  0x04
#define LA_PKT_START 0x08
#define LA_PKT_STOP  0x10
#define LA_PKT_ID    0x20   /* CAN identifier / DLC header, not payload */
#define LA_PKT_ID_HI 0x40   /* CAN ID, high byte */
#define LA_PKT_ID_DLC 0x80  /* CAN DLC field */

typedef struct {
    uint8_t value;      /* the byte as it appeared on the wire */
    uint8_t flags;      /* LA_PKT_* */
    uint32_t at;        /* sample index of the byte's first bit */
    uint32_t end;       /* sample index just past its last bit */
    uint32_t gap;       /* samples since the previous packet's end */
} la_packet;

typedef struct {
    la_packet pkt[LA_PKT_MAX];
    unsigned n;
    uint32_t rate;
    unsigned ch;        /* channel the payload rides on, for the preview */
    /* The whole transaction on the wire, for the overview bar. */
    uint32_t win_at, win_end;
    /* Samples per bit. The preview zooms to one byte, and without this it
     * cannot tell a 10-bit UART byte from a 4-bit CAN DLC field, so every
     * edge ends up a few pixels wide and reads as a slope instead of a step. */
    uint32_t bitw;
} la_pktlist;

/* Extract the bytes of one protocol (a single LAR2_PROTO_* value). Returns the
 * number of packets found; the list is empty when the decode found nothing. */
unsigned decoders_packets(const uint8_t *buf, uint32_t len, uint32_t rate,
                          uint8_t proto, uint32_t uart_baud, uint32_t can_baud,
                          const la_channel_map *map, la_pktlist *out);

/* Run every protocol selected by mask (LAR2_PROTO_*). */
unsigned decoders_run(const uint8_t *buf, uint32_t len, uint32_t rate,
                      uint8_t mask, uint32_t baud, la_text *t);
/* Same, with an explicit channel map and separate UART/CAN bit rates. */
unsigned decoders_run_map(const uint8_t *buf, uint32_t len, uint32_t rate,
                          uint8_t mask, uint32_t uart_baud, uint32_t can_baud,
                          const la_channel_map *map, la_text *t);
#endif
