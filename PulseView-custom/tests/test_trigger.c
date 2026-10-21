/* SPDX-License-Identifier: GPL-3.0-or-later
 * Host test for the LAR2 trigger matcher: the fast SWAR path must agree with a
 * naive reference on every input, and it must be fast enough to scan a capture
 * stream while the DMA keeps filling the buffer.
 */
#include "lar2.h"
#include "trigger_match.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif

#define BUF 4096

static uint32_t rng_state = 0x12345678u;
static uint32_t rng(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static int naive_pattern(const uint8_t *b, uint32_t from, uint32_t to, uint8_t v, uint8_t m, uint32_t *hit)
{
    v &= m;
    for (uint32_t i = from; i < to; i++)
        if ((b[i] & m) == v) { *hit = i; return 1; }
    return 0;
}

static int naive_edge(const uint8_t *b, uint32_t from, uint32_t to, uint8_t ch, int rising, uint32_t *hit)
{
    uint8_t bit = (uint8_t)(1u << ch);
    for (uint32_t i = from; i < to; i++) {
        uint8_t cur = b[i] & bit;
        uint8_t prev = i ? (b[i - 1] & bit) : 0;
        if (rising ? (cur && !prev) : (!cur && prev)) { *hit = i; return 1; }
    }
    return 0;
}

static void fill_random(uint8_t *b, uint32_t n, int sparse)
{
    for (uint32_t i = 0; i < n; i++)
        b[i] = sparse ? (uint8_t)(rng() & 0x0f) : (uint8_t)rng();
}

int main(void)
{
    static uint8_t buf[BUF + 8];
    uint32_t a, c;

    /* 1. Pattern trigger against the reference, many random shapes. */
    for (unsigned iter = 0; iter < 20000; iter++) {
        uint32_t n = 1 + rng() % BUF;
        uint32_t from = rng() % n;
        uint32_t to = from + rng() % (n - from + 1);
        uint8_t value = (uint8_t)rng(), mask = (uint8_t)rng();
        int sparse = iter & 1;
        fill_random(buf, n, sparse);
        int fast = lar2_match_pattern(buf, from, to, value, mask, &a);
        int slow = naive_pattern(buf, from, to, value, mask, &c);
        assert(fast == slow);
        if (fast) assert(a == c);
    }

    /* 2. Edge trigger against the reference, both polarities, every channel. */
    for (unsigned iter = 0; iter < 20000; iter++) {
        uint32_t n = 1 + rng() % BUF;
        uint32_t from = rng() % n;
        uint32_t to = from + rng() % (n - from + 1);
        uint8_t ch = (uint8_t)(rng() % 8);
        int rising = iter & 1;
        fill_random(buf, n, iter & 2);
        int fast = lar2_match_edge(buf, from, to, ch, rising, &a);
        int slow = naive_edge(buf, from, to, ch, rising, &c);
        assert(fast == slow);
        if (fast) assert(a == c);
    }

    /* 3. A match that sits exactly on a four byte group boundary. */
    for (uint32_t k = 0; k < 8; k++) {
        memset(buf, 0x00, BUF);
        uint32_t at = 4 * k + 3;
        buf[at] = 0x5a;
        uint32_t hit;
        assert(lar2_match_pattern(buf, 0, BUF, 0x5a, 0xff, &hit) && hit == at);
        assert(lar2_match_pattern(buf, 0, BUF, 0x5a, 0x0f, &hit) && hit == at);
    }

    /* 4. Edges on every byte offset, rising and falling. */
    for (uint32_t at = 1; at < 64; at++) {
        memset(buf, 0x00, BUF);
        buf[at] = 0x01;
        uint32_t hit;
        assert(lar2_match_edge(buf, 0, BUF, 0, 1, &hit) && hit == at);
        memset(buf, 0x01, BUF);
        buf[at] = 0x00;
        assert(lar2_match_edge(buf, 0, BUF, 0, 0, &hit) && hit == at);
    }

    /* 5. The first sample of the window counts as an edge, as documented. */
    memset(buf, 0x00, BUF);
    buf[0] = 0x01;
    uint32_t hit;
    assert(lar2_match_edge(buf, 0, BUF, 0, 1, &hit) && hit == 0);

    /* 6. A don't-care mask must not report a match on the ignored bits. */
    memset(buf, 0x00, BUF);
    buf[10] = 0xf0;
    assert(!lar2_match_pattern(buf, 0, BUF, 0x0f, 0x0f, &hit));
    assert(lar2_match_pattern(buf, 0, BUF, 0xf0, 0xf0, &hit) && hit == 10);

    /* 7. Throughput: the matcher has to keep up with the DMA at 25 MS/s.
     * The PIO produces one byte per sample, so 25 MS/s means 25 MB/s of
     * buffer to scan. The data below never matches, so the whole buffer is
     * walked and the measured rate is the worst case. */
    static uint8_t big[1u << 20];
    for (uint32_t i = 0; i < sizeof(big); i++) big[i] = (uint8_t)(i & 0xfe);
    volatile uint32_t sink = 0;
    clock_t t0 = clock();
    for (unsigned rep = 0; rep < 8; rep++) {
        uint32_t h;
        sink += lar2_match_pattern(big, 0, sizeof(big), 0xff, 0xff, &h);
        sink += lar2_match_edge(big, 0, sizeof(big), 0, 1, &h);
    }
    double secs = (double)(clock() - t0) / CLOCKS_PER_SEC;
    double mbps = (double)sizeof(big) * 16.0 / secs / 1e6;
    printf("matcher throughput: %.1f MB/s (need > 25 MB/s for 25 MS/s capture)\n", mbps);
    assert(mbps > 25.0);
    assert(sink == 0);

    puts("LAR2 trigger matcher: pattern, edge, mask, boundary and throughput checks passed");
    return 0;
}
