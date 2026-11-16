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

/* Run every protocol selected by mask (LAR2_PROTO_*). */
unsigned decoders_run(const uint8_t *buf, uint32_t len, uint32_t rate,
                      uint8_t mask, uint32_t baud, la_text *t);
/* Same, with an explicit channel map and separate UART/CAN bit rates. */
unsigned decoders_run_map(const uint8_t *buf, uint32_t len, uint32_t rate,
                          uint8_t mask, uint32_t uart_baud, uint32_t can_baud,
                          const la_channel_map *map, la_text *t);
#endif
