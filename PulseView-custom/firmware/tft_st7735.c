/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "tft_st7735.h"
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"

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

static void tft_write_data16(uint16_t data) {
    uint8_t buf[2] = { (uint8_t)(data >> 8), (uint8_t)(data & 0xFF) };
    gpio_put(TFT_PIN_DC, 1);
    gpio_put(TFT_PIN_CS, 0);
    spi_write_blocking(TFT_SPI_PORT, buf, 2);
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
    spi_init(TFT_SPI_PORT, 40000000);
    gpio_set_function(TFT_PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(TFT_PIN_MOSI, GPIO_FUNC_SPI);

    gpio_init(TFT_PIN_CS);  gpio_set_dir(TFT_PIN_CS, GPIO_OUT); gpio_put(TFT_PIN_CS, 1);
    gpio_init(TFT_PIN_DC);  gpio_set_dir(TFT_PIN_DC, GPIO_OUT); gpio_put(TFT_PIN_DC, 1);
    gpio_init(TFT_PIN_RST); gpio_set_dir(TFT_PIN_RST, GPIO_OUT);
    gpio_init(TFT_PIN_BL);  gpio_set_dir(TFT_PIN_BL, GPIO_OUT); gpio_put(TFT_PIN_BL, 1);

    tft_reset();

    tft_write_cmd(0x01); sleep_ms(150);   /* SWRESET */
    tft_write_cmd(0x11); sleep_ms(120);   /* SLPOUT */
    tft_write_cmd(0x3A); tft_write_data8(0x05); /* COLMOD RGB565 */
    tft_write_cmd(0x36); tft_write_data8(0x08); /* MADCTL */
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

void tft_test_pattern(void) {
    int bar_w = TFT_WIDTH / 8;
    uint16_t colors[] = { 0xF800, 0x07E0, 0x001F, 0xFFE0, 0xF81F, 0x07FF, 0xFFFF, 0x0000 };
    for (int i = 0; i < 8; i++)
        tft_fill_rect(i * bar_w, 0, bar_w, TFT_HEIGHT, colors[i]);
}
