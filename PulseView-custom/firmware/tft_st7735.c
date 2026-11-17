/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "tft_st7735.h"
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "hardware/dma.h"

#define TFT_SPI_PORT spi1
#define TFT_PIN_SCK   10
#define TFT_PIN_MOSI  11
#define TFT_PIN_CS    13
#define TFT_PIN_DC    14
#define TFT_PIN_RST   15
#define TFT_PIN_BL    16

static void tft_write_cmd(uint8_t cmd) {
    gpio_put(TFT_PIN_DC, 0);
    gpio_put(TFT_PIN_CS, 0);
    spi_write_blocking(TFT_SPI_PORT, &cmd, 1);
    gpio_put(TFT_PIN_CS, 1);
}

static void tft_write_data8(uint8_t data) {
    gpio_put(TFT_PIN_DC, 1);
    gpio_put(TFT_PIN_CS, 0);
    spi_write_blocking(TFT_SPI_PORT, &data, 1);
    gpio_put(TFT_PIN_CS, 1);
}

static void tft_reset(void) {
    gpio_put(TFT_PIN_RST, 1);
    sleep_ms(5);
    gpio_put(TFT_PIN_RST, 0);
    sleep_ms(20);
    gpio_put(TFT_PIN_RST, 1);
    sleep_ms(150);
}

void tft_init(void) {
    spi_init(TFT_SPI_PORT, 20000000);
    gpio_set_function(TFT_PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(TFT_PIN_MOSI, GPIO_FUNC_SPI);

    gpio_init(TFT_PIN_CS);  gpio_set_dir(TFT_PIN_CS, GPIO_OUT); gpio_put(TFT_PIN_CS, 1);
    gpio_init(TFT_PIN_DC);  gpio_set_dir(TFT_PIN_DC, GPIO_OUT); gpio_put(TFT_PIN_DC, 1);
    gpio_init(TFT_PIN_RST); gpio_set_dir(TFT_PIN_RST, GPIO_OUT);
    gpio_init(TFT_PIN_BL);  gpio_set_dir(TFT_PIN_BL, GPIO_OUT); gpio_put(TFT_PIN_BL, 1);

    tft_reset();

    tft_write_cmd(0x01); sleep_ms(150);   /* SWRESET */
    tft_write_cmd(0x11); sleep_ms(120);   /* SLPOUT */
    tft_write_cmd(0xB1);                   /* FRMCTR1 - frame rate */
    tft_write_data8(0x01); tft_write_data8(0x2C); tft_write_data8(0x2D);
    tft_write_cmd(0xB4);                   /* INVCTR - inversion control */
    tft_write_data8(0x07);
    tft_write_cmd(0xC0);                   /* PWCTR1 - power control 1 */
    tft_write_data8(0xA2); tft_write_data8(0x02); tft_write_data8(0x84);
    tft_write_cmd(0xC1);                   /* PWCTR2 - power control 2 */
    tft_write_data8(0xC5);
    tft_write_cmd(0xC2);                   /* PWCTR3 - power control 3 */
    tft_write_data8(0x0A); tft_write_data8(0x00);
    tft_write_cmd(0xC3);                   /* PWCTR4 - power control 4 */
    tft_write_data8(0x8A); tft_write_data8(0x2A);
    tft_write_cmd(0xC4);                   /* PWCTR5 - power control 5 */
    tft_write_data8(0x8A); tft_write_data8(0xEE);
    tft_write_cmd(0xC5);                   /* VMCTR1 - VCOM control */
    tft_write_data8(0x0E);
    tft_write_cmd(0x36); tft_write_data8(0x00); /* MADCTL: RGB mode */
    tft_write_cmd(0x3A); tft_write_data8(0x05); /* COLMOD: RGB565 */
    tft_write_cmd(0x29); sleep_ms(100);   /* DISPON */
}

static void tft_set_window(int x0, int y0, int x1, int y1) {
    tft_write_cmd(0x2A);
    tft_write_data8(0x00); tft_write_data8(x0);
    tft_write_data8(0x00); tft_write_data8(x1);
    tft_write_cmd(0x2B);
    tft_write_data8(0x00); tft_write_data8(y0);
    tft_write_data8(0x00); tft_write_data8(y1);
    tft_write_cmd(0x2C);
}

void tft_fill_color(uint16_t color) {
    tft_set_window(0, 0, TFT_WIDTH - 1, TFT_HEIGHT - 1);
    uint8_t hi = color >> 8, lo = color & 0xFF;
    gpio_put(TFT_PIN_DC, 1);
    gpio_put(TFT_PIN_CS, 0);
    for (int i = 0; i < TFT_WIDTH * TFT_HEIGHT; i++) {
        uint8_t buf[2] = { hi, lo };
        spi_write_blocking(TFT_SPI_PORT, buf, 2);
    }
    gpio_put(TFT_PIN_CS, 1);
}

void tft_fill_rect(int x, int y, int w, int h, uint16_t color) {
    if (x >= TFT_WIDTH || y >= TFT_HEIGHT) return;
    if (x + w > TFT_WIDTH) w = TFT_WIDTH - x;
    if (y + h > TFT_HEIGHT) h = TFT_HEIGHT - y;
    tft_set_window(x, y, x + w - 1, y + h - 1);
    uint8_t hi = color >> 8, lo = color & 0xFF;
    gpio_put(TFT_PIN_DC, 1);
    gpio_put(TFT_PIN_CS, 0);
    for (int i = 0; i < w * h; i++) {
        uint8_t buf[2] = { hi, lo };
        spi_write_blocking(TFT_SPI_PORT, buf, 2);
    }
    gpio_put(TFT_PIN_CS, 1);
}

void tft_draw_image(int x, int y, int w, int h, const uint16_t *data) {
    if (x + w > TFT_WIDTH) w = TFT_WIDTH - x;
    if (y + h > TFT_HEIGHT) h = TFT_HEIGHT - y;
    if (w <= 0 || h <= 0) return;
    tft_set_window(x, y, x + w - 1, y + h - 1);
    gpio_put(TFT_PIN_DC, 1);
    gpio_put(TFT_PIN_CS, 0);
    for (int i = 0; i < w * h; i++) {
        uint8_t buf[2] = { (uint8_t)(data[i] >> 8), (uint8_t)(data[i] & 0xFF) };
        spi_write_blocking(TFT_SPI_PORT, buf, 2);
    }
    gpio_put(TFT_PIN_CS, 1);
}

void tft_blit(int x, int y, int w, int h, const uint16_t *px) {
    if (x < 0 || y < 0 || w <= 0 || h <= 0) return;
    if (x + w > TFT_WIDTH) w = TFT_WIDTH - x;
    if (y + h > TFT_HEIGHT) h = TFT_HEIGHT - y;
    if (w <= 0 || h <= 0) return;
    tft_set_window(x, y, x + w - 1, y + h - 1);
    gpio_put(TFT_PIN_DC, 1);
    gpio_put(TFT_PIN_CS, 0);
    for (int i = 0; i < w * h; i++) {
        uint8_t buf[2] = { (uint8_t)(px[i] >> 8), (uint8_t)(px[i] & 0xFF) };
        spi_write_blocking(TFT_SPI_PORT, buf, 2);
    }
    gpio_put(TFT_PIN_CS, 1);
}

/* Full 128x160 frame, one window write, DMA from caller memory to the SPI TX
 * FIFO. `fb` must already be in panel byte order (big-endian per pixel); the
 * UI keeps its framebuffer that way so this path is a pure copy. */
void tft_flush_fb(const uint16_t *fb) {
    static int dma = -1;
    if (dma < 0) dma = (int)dma_claim_unused_channel(true);
    tft_set_window(0, 0, TFT_WIDTH - 1, TFT_HEIGHT - 1);
    gpio_put(TFT_PIN_DC, 1);
    gpio_put(TFT_PIN_CS, 0);
    dma_channel_config c = dma_channel_get_default_config((uint)dma);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_dreq(&c, spi_get_dreq(TFT_SPI_PORT, true));
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    dma_channel_configure((uint)dma, &c, &spi_get_hw(TFT_SPI_PORT)->dr,
                          (const uint8_t *)fb,
                          (size_t)TFT_WIDTH * TFT_HEIGHT * 2, true);
    dma_channel_wait_for_finish_blocking((uint)dma);
    while (spi_is_busy(TFT_SPI_PORT)) tight_loop_contents(); /* DMA done is not wire done */
    gpio_put(TFT_PIN_CS, 1);
}

void tft_test_pattern(void) {
    int bar_w = TFT_WIDTH / 8;
    uint16_t colors[] = { 0xF800, 0x07E0, 0x001F, 0xFFE0, 0xF81F, 0x07FF, 0xFFFF, 0x0000 };
    for (int i = 0; i < 8; i++)
        tft_fill_rect(i * bar_w, 0, bar_w, TFT_HEIGHT, colors[i]);
}
