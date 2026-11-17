/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "buttons.h"

#include "pico/stdlib.h"
#include "hardware/gpio.h"

static const uint8_t pin_of[BTN_COUNT] = {
    BTN_PIN_UP, BTN_PIN_DOWN, BTN_PIN_OK, BTN_PIN_BACK
};

#define QUEUE_LEN 8
static btn_event queue[QUEUE_LEN];
static uint8_t qhead, qtail;
static btn_filter filter;

static void push(btn_id id, bool long_press, uint32_t held_ms)
{
    uint8_t next = (uint8_t)((qhead + 1) % QUEUE_LEN);
    if (next == qtail) return;          /* full: drop the newest event */
    queue[qhead].id = id;
    queue[qhead].long_press = long_press;
    queue[qhead].held_ms = held_ms;
    qhead = next;
}

static uint8_t read_raw(void)
{
    uint8_t raw = 0;
    for (unsigned i = 0; i < BTN_COUNT; i++)
        if (!gpio_get(pin_of[i])) raw |= (uint8_t)(1u << i);   /* active low */
    return raw;
}

void buttons_init(void)
{
    for (unsigned i = 0; i < BTN_COUNT; i++) {
        gpio_init(pin_of[i]);
        gpio_set_dir(pin_of[i], GPIO_IN);
        gpio_pull_up(pin_of[i]);
    }
    btn_filter_init(&filter);
    /* Start from the real level so a button held during boot does not fire. */
    filter.stable = filter.cand = read_raw();
    filter.ignore = filter.stable;
    for (unsigned i = 0; i < BTN_COUNT; i++)
        filter.cand_ms[i] = to_ms_since_boot(get_absolute_time());
    qhead = qtail = 0;
}

void buttons_update(void)
{
    uint8_t short_mask, long_mask;
    btn_filter_feed(&filter, read_raw(),
                    to_ms_since_boot(get_absolute_time()), &short_mask, &long_mask);
    for (unsigned i = 0; i < BTN_COUNT; i++) {
        if (short_mask & (1u << i)) push((btn_id)i, false, filter.last_held[i]);
        if (long_mask & (1u << i)) push((btn_id)i, true, filter.last_held[i]);
    }
}

bool buttons_get_event(btn_event *ev)
{
    if (qtail == qhead) return false;
    *ev = queue[qtail];
    qtail = (uint8_t)((qtail + 1) % QUEUE_LEN);
    return true;
}

void buttons_inject(btn_id id, bool long_press)
{
    if (id < BTN_COUNT)
        push(id, long_press, long_press ? BTN_LONG_MS : 0u);
}

void buttons_inject_held(btn_id id, uint32_t held_ms)
{
    if (id < BTN_COUNT)
        push(id, held_ms >= BTN_LONG_MS, held_ms);
}
