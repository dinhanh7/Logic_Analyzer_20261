/* Host renderer for the TFT UI: compiles firmware/ui.c against stubs, catches
 * the frame it flushes, and writes it out as a PNG. This is how the layout gets
 * checked without a camera pointed at the panel. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "ui.h"
#include "tft_st7735.h"
#include "buttons.h"

/* ---------- captured frame ---------- */
static uint16_t frame[TFT_WIDTH * TFT_HEIGHT];
static int frame_valid;

void tft_flush_fb(const uint16_t *fb)
{
    memcpy(frame, fb, sizeof frame);
    frame_valid = 1;
}
void tft_init(void) {}
void tft_fill_color(uint16_t c) { (void)c; }
void tft_fill_rect(int x, int y, int w, int h, uint16_t c) { (void)x;(void)y;(void)w;(void)h;(void)c; }
void tft_test_pattern(void) {}
void tft_draw_image(int x, int y, int w, int h, const uint16_t *d) { (void)x;(void)y;(void)w;(void)h;(void)d; }
void tft_blit(int x, int y, int w, int h, const uint16_t *p) { (void)x;(void)y;(void)w;(void)h;(void)p; }

/* ---------- buttons: the harness drives events directly ---------- */
static btn_event evq[64];
static int evq_head, evq_tail;

void buttons_init(void) { evq_head = evq_tail = 0; }
void buttons_update(void) {}
bool buttons_get_event(btn_event *ev)
{
    if (evq_head == evq_tail) return false;
    *ev = evq[evq_tail++];
    return true;
}
void buttons_inject(btn_id id, bool long_press)
{
    if (evq_head >= 64) return;
    evq[evq_head].id = id;
    evq[evq_head].long_press = long_press;
    evq_head++;
}

/* ---------- PNG out ---------- */
static void be32(FILE *f, uint32_t v)
{
    fputc((int)(v >> 24), f); fputc((int)(v >> 16) & 0xFF, f);
    fputc((int)(v >> 8) & 0xFF, f); fputc((int)v & 0xFF, f);
}

static uint32_t crc_table[256];
static void crc_init(void)
{
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[i] = c;
    }
}
static uint32_t crc_buf(const uint8_t *b, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) c = crc_table[(c ^ b[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

static void chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len)
{
    be32(f, len);
    fwrite(type, 1, 4, f);
    if (len) fwrite(data, 1, len, f);
    uint8_t *all = malloc(len + 4);
    memcpy(all, type, 4);
    if (len) memcpy(all + 4, data, len);
    be32(f, crc_buf(all, len + 4));
    free(all);
}

static int write_png(const char *path, int scale)
{
    int w = TFT_WIDTH * scale, h = TFT_HEIGHT * scale;
    uint8_t *raw = malloc((size_t)h * (1 + (size_t)w * 3));
    if (!raw) return 1;
    for (int y = 0; y < h; y++) {
        uint8_t *row = raw + (size_t)y * (1 + (size_t)w * 3);
        row[0] = 0;
        for (int x = 0; x < w; x++) {
            /* The framebuffer holds panel byte order: swap back to RGB565. */
            uint16_t v = __builtin_bswap16(frame[(y / scale) * TFT_WIDTH + (x / scale)]);
            uint8_t r = (uint8_t)((v >> 11) & 0x1F), g = (uint8_t)((v >> 5) & 0x3F),
                    b = (uint8_t)(v & 0x1F);
            row[1 + x * 3 + 0] = (uint8_t)((r << 3) | (r >> 2));
            row[1 + x * 3 + 1] = (uint8_t)((g << 2) | (g >> 4));
            row[1 + x * 3 + 2] = (uint8_t)((b << 3) | (b >> 2));
        }
    }

    FILE *f = fopen(path, "wb");
    if (!f) { free(raw); return 1; }
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    fwrite(sig, 1, 8, f);
    uint8_t ihdr[13];
    ihdr[0] = (uint8_t)(w >> 24); ihdr[1] = (uint8_t)(w >> 16);
    ihdr[2] = (uint8_t)(w >> 8);  ihdr[3] = (uint8_t)w;
    ihdr[4] = (uint8_t)(h >> 24); ihdr[5] = (uint8_t)(h >> 16);
    ihdr[6] = (uint8_t)(h >> 8);  ihdr[7] = (uint8_t)h;
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);

    /* zlib stream, stored (uncompressed) blocks: no dependency on zlib. */
    size_t rawlen = (size_t)h * (1 + (size_t)w * 3);
    uint8_t *z = malloc(rawlen + rawlen / 65535 * 5 + 64);
    size_t zp = 0;
    z[zp++] = 0x78; z[zp++] = 0x01;
    size_t off = 0;
    while (off < rawlen) {
        size_t n = rawlen - off;
        if (n > 65535) n = 65535;
        int final = (off + n >= rawlen);
        z[zp++] = (uint8_t)final;
        z[zp++] = (uint8_t)(n & 0xFF);
        z[zp++] = (uint8_t)(n >> 8);
        z[zp++] = (uint8_t)(~n & 0xFF);
        z[zp++] = (uint8_t)((~n >> 8) & 0xFF);
        memcpy(z + zp, raw + off, n);
        zp += n;
        off += n;
    }
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < rawlen; i++) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
    be32(f, 0); /* placeholder replaced below */
    /* write IDAT with the right length instead of the placeholder */
    fseek(f, -4, SEEK_CUR);
    uint32_t adler = (b << 16) | a;
    z[zp++] = (uint8_t)(adler >> 24); z[zp++] = (uint8_t)(adler >> 16);
    z[zp++] = (uint8_t)(adler >> 8);  z[zp++] = (uint8_t)adler;
    chunk(f, "IDAT", z, (uint32_t)zp);
    chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(z);
    free(raw);
    return 0;
}

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : "ui.png";
    int scale = argc > 2 ? atoi(argv[2]) : 2;
    crc_init();

    int proto = argc > 3 ? atoi(argv[3]) : 0;

    ui_init();
    ui_load_demo();                 /* 3/5 WAVE CH0-3 */
    ui_handle_command('o');         /* -> 4/5 */
    ui_handle_command('o');         /* -> 5/5 DECODE */
    /* OK enters edit on the protocol card, DOWN steps it, OK applies. */
    for (int i = 0; i < proto; i++) {
        ui_handle_command('o');
        ui_handle_command('d');
        ui_handle_command('o');
    }
    ui_tick();
    if (!frame_valid) { fprintf(stderr, "no frame flushed\n"); return 1; }
    if (write_png(out, scale)) { fprintf(stderr, "png write failed\n"); return 1; }
    printf("%s  screen=%u\n%s\n", out, ui_screen_id(), ui_decode_table());

    return 0;
}
