/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LA_CAPTURE_BUFFER_H
#define LA_CAPTURE_BUFFER_H
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

static inline void la_reverse_bytes(uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n / 2; i++) {
        uint8_t tmp = p[i]; p[i] = p[n - 1 - i]; p[n - 1 - i] = tmp;
    }
}

/* Freeze a circular DMA buffer into chronological order without allocating
 * another 256 KB buffer. The caller must stop DMA before rotating. */
static inline void la_rotate_left(uint8_t *p, size_t n, size_t offset)
{
    if (!n) return;
    offset %= n;
    if (!offset) return;
    la_reverse_bytes(p, offset);
    la_reverse_bytes(p + offset, n - offset);
    la_reverse_bytes(p, n);
}
/* Keep the requested pretrigger exact. A late event must be retried instead
 * of silently shifting the trigger toward the end of the returned window. */
static inline bool la_trigger_window(uint32_t trigger, uint32_t pre,
                                     uint32_t count, uint32_t capacity,
                                     uint32_t lag, uint32_t *base)
{
    if (!count || pre >= count || trigger < pre || lag > capacity || count > capacity - lag)
        return false;
    uint32_t start = trigger - pre;
    if (start > capacity - lag - count) return false;
    *base = start;
    return true;
}
#endif
