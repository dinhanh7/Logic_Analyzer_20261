/* SPDX-License-Identifier: GPL-3.0-or-later
 * 6x8 monospace font, ASCII 0x20..0x7F.
 */
#ifndef FONT6X8_H_INCLUDED
#define FONT6X8_H_INCLUDED
#include <stdint.h>

#define FONT6X8_W 6
#define FONT6X8_H 8
#define FONT6X8_FIRST 0x20
#define FONT6X8_COUNT 96

extern const uint8_t font6x8[FONT6X8_COUNT][FONT6X8_H];

#endif
