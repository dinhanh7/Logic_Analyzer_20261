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

/* Run every protocol selected by mask (LAR2_PROTO_*). */
unsigned decoders_run(const uint8_t *buf, uint32_t len, uint32_t rate,
                      uint8_t mask, uint32_t baud, la_text *t);
#endif
