/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "lar2.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    struct lar2_parser p={0};
    uint8_t frame[LAR2_MAX_FRAME],payload[1024];
    assert(lar2_crc((const uint8_t *)"123456789",9)==0xcbf43926);
    for (unsigned i=0;i<sizeof(payload);i++) payload[i]=i;
    for (unsigned n=0;n<=1024;n++) {
        size_t len=lar2_encode(frame,LAR2_SAMPLES,65535,payload,n);
        for (size_t i=0;i<len;i++) assert(lar2_feed(&p,frame[i])==(i==len-1));
        assert(lar2_u16(p.buf+6)==65535 && lar2_u32(p.buf+8)==n);
        assert(!memcmp(p.buf+12,payload,n));
    }
    frame[20]^=1;
    int r=0; for (unsigned i=0;i<LAR2_MAX_FRAME;i++) r=lar2_feed(&p,frame[i]);
    assert(r==-1);
    memset(&p,0,sizeof(p));
    lar2_encode(frame,LAR2_HELLO,0,NULL,0); lar2_put32(frame+8,1025);
    for (unsigned i=0;i<12;i++) r=lar2_feed(&p,frame[i]);
    assert(r==-1);
    puts("LAR2 C codec: CRC vector, all lengths, corruption and bounds passed");
}
