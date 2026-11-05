/* SPDX-License-Identifier: GPL-3.0-or-later
 * LAR2 v1: shared by the host driver and RP2350 firmware. */
#ifndef LAR2_H
#define LAR2_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define LAR2_MAX_PAYLOAD 1024
#define LAR2_MAX_FRAME (12 + LAR2_MAX_PAYLOAD + 4)
#define LAR2_MAX_SAMPLES 262144u
#define LAR2_NO_TRIGGER UINT32_MAX
#define LAR2_RATE_COUNT 8
/* CONFIG payload sizes: v1 is the original manual-only layout, v2 adds the
 * trigger parameters. Both are accepted by the firmware; a v1 host keeps
 * working unchanged because it never sends a trigger. */
#define LAR2_CONFIG_V1_SIZE 20u
#define LAR2_CONFIG_V2_SIZE 28u
/* Trigger modes carried in CONFIG.trigger_mode. */
#define LAR2_TRIGGER_MANUAL 0u
#define LAR2_TRIGGER_EDGE 1u
#define LAR2_TRIGGER_PATTERN 2u
#define LAR2_TRIGGER_UART 3u
/* Edge polarity carried in CONFIG.trigger_edge. */
#define LAR2_EDGE_RISING 0u
#define LAR2_EDGE_FALLING 1u
/* CAPTURE_INFO flags. */
#define LAR2_INFO_TRIGGER_FOUND 0x01u
#define LAR2_INFO_CAPTURE_ERROR 0x02u

/* LAR2 v1.2: ANALYZE runs a job on the last capture and answers with RESULT.
 * The payload is 8 bytes: op (u8), arg (u16), arg32 (u32), 1 reserved byte.
 * Every field starts on its own byte boundary, no overlap. */
#define LAR2_ANALYZE_SIZE 8u
#define LAR2_ANALYZE_DECODE      0u  /* arg = protocol mask, arg32 = baud (0 = auto) */
#define LAR2_ANALYZE_SAVE        1u  /* arg = slot */
#define LAR2_ANALYZE_LOAD        2u  /* arg = slot */
#define LAR2_ANALYZE_SIG_SAVE    3u
#define LAR2_ANALYZE_SIG_COMPARE 4u  /* arg32 = tolerated mismatching samples */
#define LAR2_ANALYZE_DIFF        5u  /* arg = slot */
#define LAR2_ANALYZE_SESSION     6u  /* arg = slot */
#define LAR2_ANALYZE_REPLAY      7u  /* stream the current window, no ACK */
#define LAR2_ANALYZE_PINPROBE    8u  /* arg = 0 pull-down, 1 pull-up, 2 float;
                                        returns RESULT[1] = CH0..CH7 levels */

/* Protocol selection bits for LAR2_ANALYZE_DECODE. */
#define LAR2_PROTO_UART 0x01u
#define LAR2_PROTO_I2C  0x02u
#define LAR2_PROTO_SPI  0x04u
#define LAR2_PROTO_CAN  0x08u
#define LAR2_PROTO_ALL  0x0fu

/* RESULT status byte. */
#define LAR2_RESULT_OK     0u
#define LAR2_RESULT_ERROR  1u
#define LAR2_RESULT_ABSENT 2u

/* Board identity. The host checks the "RP2350-LA" prefix and a minimum
 * length, so the suffix may advertise firmware features. */
#define LAR2_BOARD_ID "RP2350-LA/1.2+trg+an"
static const uint64_t lar2_rates[LAR2_RATE_COUNT] = {
    1000, 10000, 100000, 250000, 500000, 1000000, 5000000, 25000000
};
enum lar2_type {
    LAR2_HELLO=1, LAR2_GET_CAPS=2, LAR2_CONFIG=3, LAR2_ARM=4, LAR2_ABORT=5,
    LAR2_ANALYZE=6,
    LAR2_ID=0x81, LAR2_CAPS=0x82, LAR2_ACK=0x83,
    LAR2_INFO=0x90, LAR2_SAMPLES=0x91, LAR2_END=0x92, LAR2_RESULT=0x93,
    LAR2_ERROR=0xff
};
static inline uint16_t lar2_u16(const uint8_t *p) { return p[0] | ((uint16_t)p[1]<<8); }
static inline uint32_t lar2_u32(const uint8_t *p) { return lar2_u16(p) | ((uint32_t)lar2_u16(p+2)<<16); }
static inline uint64_t lar2_u64(const uint8_t *p) { return lar2_u32(p) | ((uint64_t)lar2_u32(p+4)<<32); }
static inline void lar2_put16(uint8_t *p, uint16_t v) { p[0]=v; p[1]=v>>8; }
static inline void lar2_put32(uint8_t *p, uint32_t v) { lar2_put16(p,v); lar2_put16(p+2,v>>16); }
static inline void lar2_put64(uint8_t *p, uint64_t v) { lar2_put32(p,v); lar2_put32(p+4,v>>32); }
static inline uint32_t lar2_crc_update(uint32_t crc, const uint8_t *p, size_t n) {
    while (n--) {
        crc ^= *p++;
        for (unsigned i=0; i<8; i++) crc=(crc>>1)^((0u-(crc&1u))&0xedb88320u);
    }
    return crc;
}
static inline uint32_t lar2_crc(const uint8_t *p, size_t n) { return ~lar2_crc_update(~0u,p,n); }
static inline size_t lar2_encode(uint8_t *out, uint8_t type, uint16_t seq, const uint8_t *p, size_t n) {
    if (n>LAR2_MAX_PAYLOAD) return 0;
    memcpy(out,"LAR2",4); out[4]=1; out[5]=type;
    lar2_put16(out+6,seq); lar2_put32(out+8,n);
    if (n) memcpy(out+12,p,n);
    lar2_put32(out+12+n,lar2_crc(out,12+n)); return 16+n;
}
struct lar2_parser { uint8_t buf[LAR2_MAX_FRAME]; size_t used, total; };
/* 1: complete frame remains in buf until next call, 0: incomplete, -1: invalid.
 * Initial noise is skipped, but malformed headers/CRC are reported. */
static inline int lar2_feed(struct lar2_parser *s, uint8_t b) {
    if (s->total && s->used==s->total) s->used=s->total=0;
    s->buf[s->used++]=b;
    if (s->used<=4) {
        while (s->used && memcmp(s->buf,"LAR2",s->used)) {
            memmove(s->buf,s->buf+1,--s->used);
        }
        return 0;
    }
    if (s->used==12) {
        uint32_t n=lar2_u32(s->buf+8);
        if (s->buf[4]!=1 || n>LAR2_MAX_PAYLOAD) { s->used=s->total=0; return -1; }
        s->total=16+n;
    }
    if (s->total && s->used==s->total) {
        if (lar2_crc(s->buf,s->total-4)!=lar2_u32(s->buf+s->total-4)) {
            s->used=s->total=0; return -1;
        }
        return 1;
    }
    return 0;
}
#endif
