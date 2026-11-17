/* SPDX-License-Identifier: GPL-3.0-or-later
 * Protocol auto-detection: work out which channels carry which bus, instead of
 * assuming the fixed CH0..CH7 layout.
 *
 * Two stages, and both matter:
 *
 *  1. Profile every channel cheaply (edges, shortest and longest stable run,
 *     duty). That narrows eight channels to a handful of candidates and, more
 *     importantly, tells a clock line from a data line.
 *
 *  2. Verify each candidate by actually running the real decoder on it and
 *     counting the packets it recovers. Guessing from waveform shape alone
 *     produces confident nonsense; making the decoder agree is the only
 *     evidence that the assignment is right.
 *
 * The one shape test that earns its place is the CAN bit-stuffing rule: a
 * conforming CAN frame never has more than five identical bits in a row, while
 * 8N1 UART has runs of six or more all the time (start bit plus eight zero
 * data bits). That separates the two single-wire protocols without decoding.
 */
#ifndef AUTODETECT_H
#define AUTODETECT_H

#include <stdint.h>
#include "decoders.h"

/* How much of the capture the detector looks at. Detection only needs enough
 * samples to see a few frames, and bounding it keeps the scan fast on a
 * 262144-sample buffer. */
#define LA_AUTO_WINDOW 32768u

/* A channel is only considered if it has at least this many edges: a line that
 * barely moves is either idle or noise, and either way it is not a bus.
 * Two is deliberately low: a chip select that frames a single SPI transaction
 * has exactly two edges, and it is the most useful line on the bus. */
#define LA_AUTO_MIN_EDGES 2u

typedef struct {
    uint32_t edges;      /* number of level transitions */
    uint32_t min_run;    /* shortest stable run, in samples */
    uint32_t max_run;    /* longest stable run, in samples */
    uint32_t high;       /* samples spent high */
    uint32_t total;      /* samples examined */
    uint8_t  idle;       /* level before the first edge */
    uint8_t  active;     /* edges >= LA_AUTO_MIN_EDGES */
    uint8_t  clockish;   /* regular edges and a duty near 50 % */
} la_chprof;

typedef struct {
    uint8_t  proto;      /* LAR2_PROTO_* that won, 0 when nothing was found */
    la_channel_map map;  /* channels that protocol should read */
    uint32_t score;      /* packets the winning assignment decoded */
    uint8_t  confident;  /* 1 when the winner beat every rival outright */
} la_detect;

/* Fill `out[0..7]` with the per-channel profile of the first LA_AUTO_WINDOW
 * samples. */
void la_profile_channels(const uint8_t *buf, uint32_t len, la_chprof out[8]);

/* Detect the bus on the capture. Returns a zeroed result when nothing decodes,
 * in which case the caller should fall back to its fixed channel map. */
la_detect decoders_autodetect(const uint8_t *buf, uint32_t len, uint32_t rate,
                              uint32_t uart_baud, uint32_t can_baud);

#endif
