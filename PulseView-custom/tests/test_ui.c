/* SPDX-License-Identifier: GPL-3.0-or-later
 * Host tests for the two pieces of the UI that have logic worth checking:
 * the debounce/long-press filter and the 6x8 font table. The drawing itself is
 * straight composition into a pixel band, so it is covered by the bench test.
 */
#include <assert.h>
#include <stdio.h>

#include "buttons.h"
#include "font6x8.h"

static void test_debounce_short(void)
{
    btn_filter f;
    btn_filter_init(&f);
    uint8_t s, l;

    /* Press at t=0, the level has to be stable for 25 ms before it counts. */
    btn_filter_feed(&f, 0x01, 0, &s, &l);
    assert(s == 0 && l == 0);
    btn_filter_feed(&f, 0x01, 10, &s, &l);
    assert(s == 0 && l == 0);
    btn_filter_feed(&f, 0x01, 25, &s, &l);
    assert(s == 0 && l == 0);            /* press is armed, no event yet */

    /* Release at t=100: a short press fires once the release is stable. */
    btn_filter_feed(&f, 0x00, 100, &s, &l);
    btn_filter_feed(&f, 0x00, 120, &s, &l);
    assert(s == 0);
    btn_filter_feed(&f, 0x00, 125, &s, &l);
    assert(s == 0x01 && l == 0);

    /* Nothing more from the same press. */
    btn_filter_feed(&f, 0x00, 200, &s, &l);
    assert(s == 0 && l == 0);
}

static void test_debounce_long(void)
{
    btn_filter f;
    btn_filter_init(&f);
    uint8_t s, l;

    btn_filter_feed(&f, 0x02, 0, &s, &l);
    btn_filter_feed(&f, 0x02, 25, &s, &l);      /* pressed, armed at t=25 */
    btn_filter_feed(&f, 0x00, 700, &s, &l);     /* held 675 ms */
    btn_filter_feed(&f, 0x00, 725, &s, &l);
    assert(s == 0 && l == 0x02);
}

static void test_bounce_is_ignored(void)
{
    btn_filter f;
    btn_filter_init(&f);
    uint8_t s, l, any = 0;

    /* A 5 ms contact bounce must not produce a press/release pair. */
    for (uint32_t t = 0; t < 200; t += 5) {
        uint8_t raw = (t >= 100 && t < 105) ? 0x04 : 0x00;
        btn_filter_feed(&f, raw, t, &s, &l);
        any |= s | l;
    }
    assert(any == 0);
}

static void test_two_buttons_at_once(void)
{
    btn_filter f;
    btn_filter_init(&f);
    uint8_t s, l;

    btn_filter_feed(&f, 0x03, 0, &s, &l);
    btn_filter_feed(&f, 0x03, 25, &s, &l);
    btn_filter_feed(&f, 0x00, 60, &s, &l);
    btn_filter_feed(&f, 0x00, 85, &s, &l);
    assert(s == 0x03 && l == 0);
}

static void test_font_table(void)
{
    /* Every printable glyph except space must have at least one pixel, and no
     * glyph may set a bit outside the six used columns. */
    for (int i = 0; i < FONT6X8_COUNT; i++) {
        unsigned lit = 0;
        for (int r = 0; r < FONT6X8_H; r++) {
            assert((font6x8[i][r] & 0xC0) == 0);
            for (int c = 0; c < 8; c++) lit += (font6x8[i][r] >> c) & 1u;
        }
        int code = FONT6X8_FIRST + i;
        if (code != ' ' && code != 0x7F) assert(lit > 0);
    }
    /* 21 columns of text must fit the 128 px panel. */
    assert(21 * FONT6X8_W <= 128);
}

int main(void)
{
    test_debounce_short();
    test_debounce_long();
    test_bounce_is_ignored();
    test_two_buttons_at_once();
    test_font_table();
    printf("test_ui: all assertions passed\n");
    return 0;
}
