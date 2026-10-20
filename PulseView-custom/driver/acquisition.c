/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <config.h>
#include "protocol.h"
#include <errno.h>
#include <unistd.h>
static int handle_frame(const struct sr_dev_inst *sdi)
{
    struct dev_context *d=sdi->priv;
    const uint8_t *f=d->parser.buf, *p=f+12;
    uint32_t n=lar2_u32(f+8);
    if (lar2_u16(f+6)!=d->rx_seq++) { sr_err("LAR2 sequence gap."); return SR_ERR_DATA; }
    sr_dbg("RX type=0x%02x seq=%u length=%u",f[5],lar2_u16(f+6),n);
    switch (f[5]) {
    case LAR2_INFO:
        if (d->have_info || n!=24 || lar2_u64(p)!=d->rate || lar2_u32(p+8)!=d->limit ||
            lar2_u32(p+12)!=LAR2_NO_TRIGGER || lar2_u32(p+16)!=0 || p[20]!=8 || p[21]!=1 || p[22]!=d->mask || p[23]!=0)
            return SR_ERR_DATA;
        d->have_info=TRUE;
        { struct sr_config c={.key=SR_CONF_SAMPLERATE,.data=g_variant_ref_sink(g_variant_new_uint64(d->rate))};
          struct sr_datafeed_meta meta={.config=g_slist_append(NULL,&c)};
          struct sr_datafeed_packet packet={.type=SR_DF_META,.payload=&meta};
          int r=sr_session_send(sdi,&packet);
          g_slist_free(meta.config); g_variant_unref(c.data); if (r!=SR_OK) return r; }
        break;
    case LAR2_SAMPLES:
        if (!d->have_info || !n || n>d->limit-d->received) return SR_ERR_DATA;
        { struct sr_datafeed_logic logic={.length=n,.unitsize=1,.data=(void *)p};
          struct sr_datafeed_packet packet={.type=SR_DF_LOGIC,.payload=&logic};
          if (sr_session_send(sdi,&packet)!=SR_OK) return SR_ERR; }
        d->received+=n; d->data_crc=lar2_crc_update(d->data_crc,p,n);
        break;
    case LAR2_END:
        if (!d->have_info || n!=8 || lar2_u32(p)!=d->received || d->received!=d->limit || lar2_u32(p+4)!=~d->data_crc)
            return SR_ERR_DATA;
        d->complete=TRUE;
        sr_info("Capture verified: %u samples at %" G_GUINT64_FORMAT " Hz.",d->received,d->rate);
        return 1;
    case LAR2_ERROR:
        sr_err("Firmware error: %.*s",(int)n,(const char *)p); return SR_ERR_DATA;
    default: return SR_ERR_DATA;
    }
    d->deadline=g_get_monotonic_time()+5000000;
    return SR_OK;
}
SR_PRIV int rp2350_receive(int fd,int revents,void *cb_data)
{
    const struct sr_dev_inst *sdi=cb_data;
    struct dev_context *d=sdi->priv;
    uint8_t buf[4096];
    ssize_t n;
    if (!d->active) return TRUE;
    if (revents&(G_IO_ERR|G_IO_HUP|G_IO_NVAL)) { sr_err("CDC disconnected."); goto fail; }
    if (g_get_monotonic_time()>d->deadline) { sr_err("Capture timeout/truncated transfer."); goto fail; }
    /* Bound work per callback so Stop stays responsive during a fast transfer. */
    for (int reads=0;reads<16;reads++) {
        n=read(fd,buf,sizeof(buf));
        if (n<0 && errno==EINTR) continue;
        if (n<0 && errno==EAGAIN) break;
        if (n<0) goto fail;
        if (!n) break;
        for (ssize_t i=0;i<n;i++) {
            int r=lar2_feed(&d->parser,buf[i]);
            if (r<0) { sr_err("LAR2 invalid frame/CRC."); goto fail; }
            if (r==1) {
                r=handle_frame(sdi);
                if (r<0) { sr_err("LAR2 invalid metadata, sequence, length or capture CRC."); goto fail; }
                if (r==1) { sr_dev_acquisition_stop((struct sr_dev_inst *)sdi); return TRUE; }
            }
        }
    }
    return TRUE;
fail:
    sr_dev_acquisition_stop((struct sr_dev_inst *)sdi);
    return TRUE;
}
