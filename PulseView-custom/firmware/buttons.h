/* SPDX-License-Identifier: GPL-3.0-or-later
 * Four push buttons with software debounce and short/long press events.
 *
 * The debounce core is plain C with no Pico dependency so tests/test_buttons.c
 * can drive it with a fake clock. The GPIO wrapper and the USB injection path
 * both feed the same event queue, which is how the buttons and the serial
 * commands stay equivalent.
 */
#ifndef BUTTONS_H
#define BUTTONS_H

#include <stdbool.h>
#include <stdint.h>

/* Layout A: active low, button pulls the pin to GND, internal pull-up. */
#define BTN_PIN_UP   8
#define BTN_PIN_DOWN 9
#define BTN_PIN_OK   12
#define BTN_PIN_BACK 22

typedef enum {
    BTN_UP = 0,
    BTN_DOWN = 1,
    BTN_OK = 2,
    BTN_BACK = 3,
    BTN_COUNT = 4
} btn_id;

typedef struct {
    btn_id id;
    bool long_press;
    uint32_t held_ms;           /* how long the button was held on release */
} btn_event;

#define BTN_DEBOUNCE_MS 25u
#define BTN_LONG_MS     600u
#define BTN_STANDBY_MS  800u    /* BACK held this long returns to STANDBY */

/* Pure debounce state. `raw` bit n set means button n is pressed. */
typedef struct {
    uint8_t stable;
    uint8_t cand;
    uint32_t cand_ms;
    uint32_t press_ms[BTN_COUNT];
    uint32_t last_held[BTN_COUNT];  /* hold time of the last release */
} btn_filter;

static inline void btn_filter_init(btn_filter *f)
{
    f->stable = 0;
    f->cand = 0;
    f->cand_ms = 0;
    for (unsigned i = 0; i < BTN_COUNT; i++) {
        f->press_ms[i] = 0;
        f->last_held[i] = 0;
    }
}

/* Feed one raw sample. Emits a bit per button that produced a short release
 * and a bit per button that produced a long release in this sample. */
static inline void btn_filter_feed(btn_filter *f, uint8_t raw, uint32_t now_ms,
                                   uint8_t *short_mask, uint8_t *long_mask)
{
    *short_mask = *long_mask = 0;
    if (raw != f->cand) {
        f->cand = raw;
        f->cand_ms = now_ms;
        return;
    }
    if (now_ms - f->cand_ms < BTN_DEBOUNCE_MS) return;

    uint8_t changed = (uint8_t)(f->stable ^ f->cand);
    if (!changed) return;
    for (unsigned i = 0; i < BTN_COUNT; i++) {
        uint8_t bit = (uint8_t)(1u << i);
        if (!(changed & bit)) continue;
        if (f->cand & bit) {
            f->press_ms[i] = now_ms;              /* just pressed */
        } else {
            uint32_t held = now_ms - f->press_ms[i];
            f->last_held[i] = held;
            if (held >= BTN_LONG_MS) *long_mask |= bit;
            else *short_mask |= bit;
        }
    }
    f->stable = f->cand;
}

void buttons_init(void);
/* Debounce + queue. Call from the main loop. */
void buttons_update(void);
bool buttons_get_event(btn_event *ev);
/* Push an event as if the physical button fired (used by the USB key path). */
void buttons_inject(btn_id id, bool long_press);
/* Push an event with an explicit hold time, so the host can drive the
 * 800 ms BACK->STANDBY tier that the physical debounce normally measures. */
void buttons_inject_held(btn_id id, uint32_t held_ms);

#endif
