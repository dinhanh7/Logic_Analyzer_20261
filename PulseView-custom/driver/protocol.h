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
};
SR_PRIV int rp2350_connect(struct dev_context *d);
SR_PRIV int rp2350_command(struct dev_context *d, uint8_t type, const uint8_t *p, size_t n);
SR_PRIV int rp2350_reply(struct dev_context *d, uint8_t type, int timeout_ms);
SR_PRIV int rp2350_receive(int fd, int revents, void *cb_data);
#endif
