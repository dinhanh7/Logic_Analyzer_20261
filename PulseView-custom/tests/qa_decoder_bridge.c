/* Host bridge to the exact allocation-free decoder/autodetect used by Pico. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "decoders.h"
#include "autodetect.h"
static uint8_t data[LAR2_MAX_SAMPLES];
static void quoted(const char *s) {
    putchar('"');
    for (;*s;s++) {
        unsigned char c=(unsigned char)*s;
        if (c=='"'||c=='\\') { putchar('\\');putchar(c); }
        else if (c<32) printf("\\u%04x",c);
        else putchar(c);
    }
    putchar('"');
}
int main(int argc,char **argv) {
    if(argc!=5)return 2;
    FILE *f=fopen(argv[1],"rb");if(!f)return 3;
    size_t len=fread(data,1,sizeof data,f);if(fgetc(f)!=EOF)return 4;fclose(f);
    uint32_t rate=(uint32_t)strtoul(argv[2],NULL,10),uart=(uint32_t)strtoul(argv[3],NULL,10),can=(uint32_t)strtoul(argv[4],NULL,10);
    char text[2048];la_text t;la_text_init(&t,text,sizeof text);
    decoders_run_map(data,(uint32_t)len,rate,LAR2_PROTO_ALL,uart,can,&la_map_test8,&t);
    printf("{\"text\":");quoted(text);printf(",\"packets\":[");
    for(unsigned p=0;p<4;p++) {
        la_pktlist l;decoders_packets(data,(uint32_t)len,rate,(uint8_t)(1u<<p),uart,can,&la_map_test8,&l);
        if(p)putchar(',');printf("[");
        for(unsigned i=0;i<l.n;i++) {
            if(i)putchar(',');la_packet *q=&l.pkt[i];
            printf("{\"value\":%u,\"flags\":%u,\"at\":%u,\"end\":%u}",q->value,q->flags,q->at,q->end);
        }
        putchar(']');
    }
    la_detect d=decoders_autodetect(data,(uint32_t)len,rate,uart,can);
    printf("],\"detect\":{\"proto\":%u,\"score\":%u,\"map\":[%u,%u,%u,%u,%u,%u,%u,%u]}}\n",d.proto,d.score,d.map.uart,d.map.i2c_scl,d.map.i2c_sda,d.map.spi_cs,d.map.spi_sck,d.map.spi_mosi,d.map.spi_miso,d.map.can);
    return 0;
}
