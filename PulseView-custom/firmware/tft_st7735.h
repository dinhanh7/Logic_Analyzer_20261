/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef TFT_ST7735_H
#define TFT_ST7735_H
#include <stdint.h>

#define TFT_WIDTH  128
#define TFT_HEIGHT 160

void tft_init(void);
void tft_fill_color(uint16_t color);
void tft_fill_rect(int x, int y, int w, int h, uint16_t color);
void tft_test_pattern(void);
void tft_draw_image(int x, int y, int w, int h, const uint16_t *data);
/* Copy a row-major RGB565 block to the panel. Same as tft_draw_image but the
 * buffer is caller owned, which is how the UI composes one text row at a time
 * without a full screen framebuffer. */
void tft_blit(int x, int y, int w, int h, const uint16_t *px);
/* Push a whole 128x160 frame with one window write over SPI DMA. The buffer
 * holds RGB565 words in panel byte order (high byte first). */
void tft_flush_fb(const uint16_t *fb);

#endif
