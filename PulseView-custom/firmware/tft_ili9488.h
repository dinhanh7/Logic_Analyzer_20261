/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef TFT_ILI9488_H
#define TFT_ILI9488_H
#include <stdint.h>

#define TFT_WIDTH  320
#define TFT_HEIGHT 480

void tft_init(void);
void tft_fill_color(uint16_t color);
void tft_fill_rect(int x, int y, int w, int h, uint16_t color);
void tft_test_pattern(void);
void tft_draw_image(int x, int y, int w, int h, const uint16_t *data);

#endif
