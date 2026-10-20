/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "tusb.h"
#include "logic_capture.pio.h"
#include "lar2.h"

static uint32_t samples[LAR2_MAX_SAMPLES/4];
static struct lar2_parser parser;
static uint16_t tx_seq, rx_seq;
static uint64_t rate=1000000;
static uint32_t count=100000, sent, crc;
static uint8_t mask=0xff;
static bool configured, capturing, sending;
static PIO pio=pio0;
static uint sm, offset;
static int dma_ch;
static void send_frame(uint8_t type,const uint8_t *p,size_t n)
{
    uint8_t frame[LAR2_MAX_FRAME];
    size_t len=lar2_encode(frame,type,tx_seq++,p,n), pos=0;
    absolute_time_t deadline=make_timeout_time_ms(3000);
    while (pos<len && tud_cdc_connected() && !time_reached(deadline)) {
        tud_task();
        uint32_t wrote=tud_cdc_write(frame+pos,len-pos);
        pos+=wrote; tud_cdc_write_flush();
        if (wrote) deadline=make_timeout_time_ms(3000);
    }
    if (pos!=len) sending=false;
}
static void ack(uint8_t command) { send_frame(LAR2_ACK,&command,1); }
static void error(const char *text) { send_frame(LAR2_ERROR,(const uint8_t *)text,strlen(text)); }
static void abort_capture(void)
{
    pio_sm_set_enabled(pio,sm,false);
    dma_channel_abort(dma_ch);
    pio_sm_clear_fifos(pio,sm);
    capturing=sending=false;
}
static void start_capture(void)
{
    abort_capture();
    pio_sm_config c=logic_capture_program_get_default_config(offset);
    sm_config_set_in_pins(&c,0);
    sm_config_set_in_shift(&c,true,true,32);
    sm_config_set_out_shift(&c,true,false,32);
    sm_config_set_clkdiv_int_frac(&c,(uint32_t)(25000000/rate),0);
    pio_sm_init(pio,sm,offset,&c);
    pio_sm_set_consecutive_pindirs(pio,sm,0,8,false);
    pio_sm_clkdiv_restart(pio,sm);
    pio_sm_put_blocking(pio,sm,((count+3)&~3u)-1);
    pio_sm_exec(pio,sm,pio_encode_pull(false,true));
    pio_sm_exec(pio,sm,pio_encode_out(pio_x,32));
    dma_channel_config dc=dma_channel_get_default_config(dma_ch);
    channel_config_set_transfer_data_size(&dc,DMA_SIZE_32);
    channel_config_set_read_increment(&dc,false);
    channel_config_set_write_increment(&dc,true);
    channel_config_set_dreq(&dc,pio_get_dreq(pio,sm,false));
    pio->fdebug=1u<<(PIO_FDEBUG_RXSTALL_LSB+sm);
    dma_channel_configure(dma_ch,&dc,samples,&pio->rxf[sm],(count+3)/4,true);
    capturing=true;
    pio_sm_set_enabled(pio,sm,true);
}
static void command(void)
{
    uint8_t *f=parser.buf,*p=f+12;
    uint8_t type=f[5]; uint32_t n=lar2_u32(f+8);
    if (type==LAR2_HELLO && n==0 && lar2_u16(f+6)==0) {
        abort_capture(); tx_seq=0; rx_seq=1; configured=false;
        const char *id="RP2350-LA/1.0"; send_frame(LAR2_ID,(const uint8_t *)id,strlen(id)); return;
    }
    if (lar2_u16(f+6)!=rx_seq++) { abort_capture(); configured=false; error("sequence"); return; }
    if (type==LAR2_ABORT && !n) { abort_capture(); configured=false; ack(type); return; }
    if (capturing || sending) { error("busy"); return; }
    switch (type) {
    case LAR2_GET_CAPS: {
        if (n) { error("caps length"); break; }
        uint8_t caps[12+8*LAR2_RATE_COUNT]={8,1,0,0};
        lar2_put32(caps+4,LAR2_MAX_SAMPLES); lar2_put32(caps+8,LAR2_RATE_COUNT);
        for (unsigned i=0;i<LAR2_RATE_COUNT;i++) lar2_put64(caps+12+8*i,lar2_rates[i]);
        send_frame(LAR2_CAPS,caps,sizeof(caps)); break;
    }
    case LAR2_CONFIG: {
        configured=false;
        if (n!=20 || !lar2_u32(p+8) || lar2_u32(p+8)>LAR2_MAX_SAMPLES ||
            lar2_u32(p+12) || !p[16] || p[17] || p[18] || p[19]) { error("invalid config"); break; }
        bool supported=false;
        for (unsigned i=0;i<LAR2_RATE_COUNT;i++) if (lar2_u64(p)==lar2_rates[i]) supported=true;
        if (!supported) { error("unsupported rate"); break; }
        rate=lar2_u64(p); count=lar2_u32(p+8); mask=p[16]; configured=true; ack(type); break;
    }
    case LAR2_ARM:
        if (n || !configured) { error("not configured"); break; }
        ack(type); start_capture(); break;
    default: error("unknown command");
    }
}
static void capture_task(void)
{
    if (capturing && !dma_channel_is_busy(dma_ch)) {
        pio_sm_set_enabled(pio,sm,false); capturing=false;
        /* The PIO stops after the last requested word. RXSTALL cannot come
         * from extra samples after DMA completion, so it signals lost timing. */
        if ((pio->fdebug & (1u<<(PIO_FDEBUG_RXSTALL_LSB+sm))) ||
            (dma_hw->ch[dma_ch].ctrl_trig & (DMA_CH0_CTRL_TRIG_READ_ERROR_BITS | DMA_CH0_CTRL_TRIG_WRITE_ERROR_BITS))) {
            configured=false; error("capture FIFO stall/DMA error"); return;
        }
        uint8_t info[24]={0};
        lar2_put64(info,rate); lar2_put32(info+8,count); lar2_put32(info+12,LAR2_NO_TRIGGER);
        info[20]=8; info[21]=1; info[22]=mask;
        sending=true; sent=0; crc=~0u; send_frame(LAR2_INFO,info,sizeof(info));
    }
    if (sending) {
        uint8_t *data=(uint8_t *)samples;
        uint32_t n=count-sent;
        if (n>LAR2_MAX_PAYLOAD) n=LAR2_MAX_PAYLOAD;
        if (n) {
            crc=lar2_crc_update(crc,data+sent,n);
            send_frame(LAR2_SAMPLES,data+sent,n); sent+=n;
        } else {
            uint8_t end[8]; lar2_put32(end,count); lar2_put32(end+4,~crc);
            send_frame(LAR2_END,end,sizeof(end)); sending=false;
        }
    }
}
int main(void)
{
    set_sys_clock_khz(100000,true);
    stdio_init_all();
    for (uint i=0;i<8;i++) { pio_gpio_init(pio,i); gpio_disable_pulls(i); }
    sm=pio_claim_unused_sm(pio,true);
    offset=pio_add_program(pio,&logic_capture_program);
    dma_ch=dma_claim_unused_channel(true);
    bool connected=false;
    while (true) {
        tud_task();
        bool now=tud_cdc_connected();
        if (!now) {
            if (connected) { abort_capture(); configured=false; memset(&parser,0,sizeof(parser)); }
        } else {
            /* Process one command before capture_task; ABORT takes priority. */
            while (tud_cdc_available()) {
                uint8_t b; if (tud_cdc_read(&b,1)!=1) break;
                int r=lar2_feed(&parser,b);
                if (r<0) { abort_capture(); configured=false; error("frame CRC/header"); }
                if (r==1) { command(); break; }
            }
            capture_task();
        }
        connected=now; tight_loop_contents();
    }
}
