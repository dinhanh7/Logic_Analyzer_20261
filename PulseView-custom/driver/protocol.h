/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef RP2350_PROTOCOL_H
#define RP2350_PROTOCOL_H
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "lar2.h"
#define LOG_PREFIX "rp2350-la"
struct dev_context {
    char *path;
    int fd;
    uint16_t tx_seq, rx_seq;
    struct lar2_parser parser;
    uint64_t rate, limit, rates[32];
    uint32_t max_samples, num_rates, received, data_crc;
    uint8_t mask;
    gboolean active, have_info, complete;
    gint64 deadline;
    /* Trigger configuration, translated from the libsigrok config options. */
    uint8_t trig_mode;        /* 0 manual, 1 edge, 2 pattern */
    uint8_t trig_channel, trig_edge, trig_value, trig_mask;
    double horiz_triggerpos;  /* fraction of the window kept before the trigger */
    uint32_t trigger_index;   /* trigger position inside the window */
    gboolean trigger_sent;
};
SR_PRIV int rp2350_connect(struct dev_context *d);
SR_PRIV int rp2350_command(struct dev_context *d, uint8_t type, const uint8_t *p, size_t n);
SR_PRIV int rp2350_reply(struct dev_context *d, uint8_t type, int timeout_ms);
SR_PRIV int rp2350_receive(int fd, int revents, void *cb_data);
#endif
