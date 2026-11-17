/* SPDX-License-Identifier: GPL-3.0-or-later
 * LAR2 trigger matching, shared by the RP2350 firmware and the host tests.
 *
 * The matcher scans a raw sample buffer (one byte per sample, bit n = channel n)
 * and reports the first sample index that satisfies the trigger condition.
 * It never reports a miss that the naive reference would find, and it never
 * reports a false positive: the fast SWAR path is only a filter, every hit is
 * confirmed byte by byte.
 */
#ifndef LAR2_TRIGGER_MATCH_H
#define LAR2_TRIGGER_MATCH_H
#include <stdint.h>
#include <string.h>
#include "lar2.h"

/* Pattern trigger: (sample & mask) == (value & mask).
 * The expected value is masked as well, so the bits outside the mask are
 * don't-care in both directions and a host that sends an unmasked value
 * still gets the intended behaviour. */
static inline int lar2_match_pattern(const uint8_t *buf, uint32_t from, uint32_t to,
                                      uint8_t value, uint8_t mask, uint32_t *hit)
{
    value &= mask;
    const uint32_t m4 = (uint32_t)mask * 0x01010101u;
    const uint32_t v4 = (uint32_t)value * 0x01010101u;
    uint32_t i = from;

    for (; i + 4 <= to; i += 4) {
        uint32_t w;
        memcpy(&w, buf + i, 4);
        /* A byte of x is zero exactly when that sample matches the pattern. */
        uint32_t x = (w & m4) ^ v4;
        /* Filter only: the borrow of a zero byte can flag a neighbour, so the
         * candidates are confirmed one by one below. */
        uint32_t z = (x - 0x01010101u) & ~x & 0x80808080u;
        if (!z) continue;
        for (uint32_t k = 0; k < 4; k++) {
            if ((buf[i + k] & mask) == value) { *hit = i + k; return 1; }
        }
    }
    for (; i < to; i++) {
        if ((buf[i] & mask) == value) { *hit = i; return 1; }
    }
    return 0;
}

/* Edge trigger on one channel. rising != 0 selects a low-to-high edge. */
static inline int lar2_match_edge(const uint8_t *buf, uint32_t from, uint32_t to,
                                  uint8_t channel, int rising, uint32_t *hit)
{
    if (to <= from) return 0;
    const uint8_t bit = (uint8_t)(1u << (channel & 7u));
    const uint32_t b4 = (uint32_t)bit * 0x01010101u;
    /* The sample before the window is treated as low, so a high level on the
     * very first sample counts as a rising edge. */
    uint8_t prev = from ? buf[from - 1] : 0;
    uint32_t i = from;

    for (; i + 4 <= to; i += 4) {
        uint32_t w;
        memcpy(&w, buf + i, 4);
        /* Byte j of shifted holds the predecessor of byte j of w. */
        uint32_t shifted = (w << 8) | (uint32_t)prev;
        uint32_t cur = w & b4;
        uint32_t pred = shifted & b4;
        uint32_t m = rising ? (cur & ~pred) : (~cur & pred);
        if (m) {
            for (uint32_t k = 0; k < 4; k++) {
                uint8_t b = buf[i + k];
                uint8_t p = (i + k) ? buf[i + k - 1] : 0;
                int is_rise = (b & bit) && !(p & bit);
                int is_fall = !(b & bit) && (p & bit);
                if (rising ? is_rise : is_fall) { *hit = i + k; return 1; }
            }
        }
        prev = buf[i + 3];
    }
    for (; i < to; i++) {
        uint8_t b = buf[i];
        uint8_t p = i ? buf[i - 1] : 0;
        int is_rise = (b & bit) && !(p & bit);
        int is_fall = !(b & bit) && (p & bit);
        if (rising ? is_rise : is_fall) { *hit = i; return 1; }
    }
    return 0;
}

/* Dispatch on the trigger mode carried by CONFIG. */
static inline int lar2_trigger_match(const uint8_t *buf, uint32_t from, uint32_t to,
                                     uint8_t mode, uint8_t channel, uint8_t edge,
                                     uint8_t value, uint8_t mask, uint32_t *hit)
{
    if (mode == LAR2_TRIGGER_PATTERN)
        return lar2_match_pattern(buf, from, to, value, mask, hit);
    if (mode == LAR2_TRIGGER_EDGE)
        return lar2_match_edge(buf, from, to, channel, edge == LAR2_EDGE_RISING, hit);
    return 0;
}

#endif
