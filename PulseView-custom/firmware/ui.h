/* SPDX-License-Identifier: GPL-3.0-or-later
 * On-device UI: six screens on the 128x160 ST7735 (boot STANDBY + the five
 * measurement screens), four buttons, and a serial key path that injects the
 * same button events.
 *
 * The UI never touches PIO, DMA or SPI directly. It owns the measurement
 * configuration, asks main.c for a capture through ui_take_capture_request(),
 * and main.c hands the finished window back through ui_set_capture().
 */
#ifndef UI_H
#define UI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint64_t rate;
    uint32_t count;
    uint32_t pretrigger;
    uint8_t  mask;          /* channel mask, same as LAR2 CONFIG */
    uint8_t  trig_mode;     /* LAR2_TRIGGER_* */
    uint8_t  trig_channel;
    uint8_t  trig_edge;
    uint8_t  trig_value;
    uint8_t  trig_mask;
} ui_config;

extern ui_config ui_cfg;

void ui_init(void);
/* main.c pushes a monotonic millisecond clock so the STANDBY prompt can blink
 * without ui.c depending on the pico SDK. Call before ui_tick() each loop. */
void ui_set_now_ms(uint32_t ms);
/* Poll the buttons and redraw whatever changed. Call from the main loop. */
void ui_tick(void);
/* USB side of the parallel control: 'u'/'d'/'o'/'b' are a short press of
 * UP/DOWN/OK/BACK, the uppercase letter is a long press. 'B' is special: it
 * injects BACK held 800 ms, the STANDBY shortcut. */
void ui_handle_command(char c);
/* Consume a pending capture request (hold OK on the CONFIG screen). */
bool ui_take_capture_request(void);
/* Which screen is on the panel: 0 capture, 1 trigger, 2 waveform CH0-3,
 * 3 waveform CH4-7, 4 decode, 5 STANDBY (the boot screen, "màn 0"). Reported
 * through LAR2_ANALYZE_RUNTIME so a host can verify navigation without eyes
 * on the TFT. The original five ids are unchanged for the cycle test. */
uint8_t ui_screen_id(void);
/* Number of UI events dispatched since boot, saturating at 255. */
uint8_t ui_event_count(void);
/* Text the decode screen is currently showing. Read-only, valid until the
 * next decode. Exposed through LAR2_ANALYZE_UI_TEXT so a host can check all
 * four protocols without looking at the panel. */
const char *ui_decode_text(void);
/* The decode grid as the panel draws it: one line per visible row, columns
 * separated by '|'. Lets the host check the table without eyes on the panel. */
const char *ui_decode_table(void);
/* Decode-screen protocol selector (0 = AUTO) and the last AUTO result, exposed
 * so a host can verify the PROTO card badge without eyes on the panel. */
uint8_t ui_proto_sel(void);
uint8_t ui_dec_auto_proto(void);
const char *ui_decode_proto_badge(void);

/* Time formatting used by the waveform/decode caption bars, exposed so the host
 * harness can check the rounding and unit ladder without a panel. Both keep the
 * value to five characters ("400ns", "8.6u", "1.25m", "2.50s"). */
void ui_format_time(uint64_t ns, char *buf, size_t n);
void ui_format_offset(uint32_t sample, uint32_t ref, uint32_t rate,
                      char *buf, size_t n);
/* Load the built-in mock waveform (UART "HELLO" on CH0 + square waves) and
 * jump to the waveform screen, so the UI can be checked with no signal
 * generator attached. */
void ui_load_demo(void);
/* main.c reports a finished capture so the waveform and decode screens can
 * work on it. `samples` must stay valid until the next capture. */
void ui_set_capture(const uint8_t *samples, uint32_t len, uint64_t rate,
                    uint32_t trigger_index, bool ok);

#endif
