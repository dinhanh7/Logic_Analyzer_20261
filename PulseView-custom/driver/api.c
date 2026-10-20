/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <config.h>
#include "protocol.h"
#include <unistd.h>
static const uint32_t scanopts[]={SR_CONF_CONN};
static const uint32_t drvopts[]={SR_CONF_LOGIC_ANALYZER};
static const uint32_t devopts[]={
    SR_CONF_SAMPLERATE|SR_CONF_GET|SR_CONF_SET|SR_CONF_LIST,
    SR_CONF_LIMIT_SAMPLES|SR_CONF_GET|SR_CONF_SET|SR_CONF_LIST,
    SR_CONF_NUM_LOGIC_CHANNELS|SR_CONF_GET
};
static void clear_private(struct dev_context *d) { if (d->fd>=0) close(d->fd); g_free(d->path); }
static int dev_clear(const struct sr_dev_driver *di) { return std_dev_clear_with_callback(di,(std_dev_clear_callback)clear_private); }
static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
    const char *path=NULL;
    struct dev_context *d;
    struct sr_dev_inst *sdi;
    for (GSList *l=options;l;l=l->next) {
        struct sr_config *c=l->data;
        if (c->key==SR_CONF_CONN) path=g_variant_get_string(c->data,NULL);
    }
    if (!path) return NULL;
    d=g_malloc0(sizeof(*d)); d->fd=-1; d->path=g_strdup(path);
    if (rp2350_connect(d)!=SR_OK) { clear_private(d); g_free(d); return NULL; }
    close(d->fd); d->fd=-1;
    d->rate=d->rates[0];
    for (uint32_t i=0;i<d->num_rates;i++) if (d->rates[i]==1000000) d->rate=1000000;
    d->limit=MIN(100000u,d->max_samples);
    sdi=g_malloc0(sizeof(*sdi)); sdi->status=SR_ST_INACTIVE;
    sdi->vendor=g_strdup("RP2350"); sdi->model=g_strdup("Pico 2 / 2 W Logic Analyzer");
    sdi->version=g_strdup("LAR2 v1"); sdi->connection_id=g_strdup(path); sdi->priv=d;
    for (int i=0;i<8;i++) { char name[8]; g_snprintf(name,sizeof(name),"CH%d",i); sr_channel_new(sdi,i,SR_CHANNEL_LOGIC,TRUE,name); }
    return std_scan_complete(di,g_slist_append(NULL,sdi));
}
static int dev_open(struct sr_dev_inst *sdi) { return rp2350_connect(sdi->priv); }
static int dev_close(struct sr_dev_inst *sdi)
{
    struct dev_context *d=sdi->priv;
    if (d->fd>=0) { close(d->fd); d->fd=-1; }
    return SR_OK;
}
static int config_get(uint32_t key,GVariant **data,const struct sr_dev_inst *sdi,const struct sr_channel_group *cg)
{
    struct dev_context *d=sdi->priv; (void)cg;
    switch (key) {
    case SR_CONF_SAMPLERATE: *data=g_variant_new_uint64(d->rate); break;
    case SR_CONF_LIMIT_SAMPLES: *data=g_variant_new_uint64(d->limit); break;
    case SR_CONF_NUM_LOGIC_CHANNELS: *data=g_variant_new_int32(8); break;
    default: return SR_ERR_NA;
    }
    return SR_OK;
}
static int config_set(uint32_t key,GVariant *data,const struct sr_dev_inst *sdi,const struct sr_channel_group *cg)
{
    struct dev_context *d=sdi->priv;
    uint64_t value; (void)cg;
    if (d->active) return SR_ERR;
    if (key!=SR_CONF_LIMIT_SAMPLES && key!=SR_CONF_SAMPLERATE) return SR_ERR_NA;
    value=g_variant_get_uint64(data);
    if (key==SR_CONF_LIMIT_SAMPLES) {
        if (!value || value>d->max_samples) return SR_ERR_ARG;
        d->limit=value; return SR_OK;
    }
    for (uint32_t i=0;i<d->num_rates;i++) if (d->rates[i]==value) { d->rate=value; return SR_OK; }
    return SR_ERR_ARG;
}
static int config_list(uint32_t key,GVariant **data,const struct sr_dev_inst *sdi,const struct sr_channel_group *cg)
{
    const struct dev_context *d=sdi ? sdi->priv : NULL;
    switch (key) {
    case SR_CONF_SAMPLERATE:
        if (!d) return SR_ERR_ARG;
        *data=std_gvar_samplerates(d->rates,d->num_rates); return SR_OK;
    case SR_CONF_LIMIT_SAMPLES:
        if (!d) return SR_ERR_ARG;
        *data=g_variant_new("(tt)",(uint64_t)1,(uint64_t)d->max_samples); return SR_OK;
    default:
        return STD_CONFIG_LIST(key,data,sdi,cg,scanopts,drvopts,devopts);
    }
}
static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
    struct dev_context *d=sdi->priv;
    uint8_t cfg[20]={0};
    int ret;
    if (d->active || d->fd<0) return SR_ERR;
    if (sr_session_trigger_get(sdi->session)) { sr_err("LAR2 v1 supports manual capture only."); return SR_ERR_NA; }
    d->mask=0;
    for (GSList *l=sdi->channels;l;l=l->next) {
        struct sr_channel *ch=l->data;
        if (ch->enabled) d->mask|=1u<<ch->index;
    }
    if (!d->mask) return SR_ERR_ARG;
    lar2_put64(cfg,d->rate); lar2_put32(cfg+8,d->limit); cfg[16]=d->mask;
    if ((ret=rp2350_command(d,LAR2_CONFIG,cfg,sizeof(cfg)))!=SR_OK) return ret;
    if ((ret=rp2350_reply(d,LAR2_ACK,1000))!=SR_OK) return ret;
    if (lar2_u32(d->parser.buf+8)!=1 || d->parser.buf[12]!=LAR2_CONFIG) return SR_ERR_DATA;
    if ((ret=rp2350_command(d,LAR2_ARM,NULL,0))!=SR_OK) return ret;
    if ((ret=rp2350_reply(d,LAR2_ACK,1000))!=SR_OK) return ret;
    if (lar2_u32(d->parser.buf+8)!=1 || d->parser.buf[12]!=LAR2_ARM) return SR_ERR_DATA;
    d->received=0; d->data_crc=~0u; d->have_info=d->complete=FALSE;
    d->deadline=g_get_monotonic_time()+5000000+(gint64)(d->limit*1000000/d->rate);
    if ((ret=std_session_send_df_header(sdi))!=SR_OK) return ret;
    d->active=TRUE;
    ret=sr_session_source_add(sdi->session,d->fd,G_IO_IN|G_IO_ERR|G_IO_HUP,50,rp2350_receive,(void *)sdi);
    if (ret!=SR_OK) { d->active=FALSE; rp2350_command(d,LAR2_ABORT,NULL,0); std_session_send_df_end(sdi); }
    return ret;
}
static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
    struct dev_context *d=sdi->priv;
    if (!d->active) return SR_OK;
    d->active=FALSE;
    sr_session_source_remove(sdi->session,d->fd);
    if (!d->complete) {
        /* Reconnect performs a fresh HELLO before the next acquisition. */
        rp2350_command(d,LAR2_ABORT,NULL,0);
        if (rp2350_reply(d,LAR2_ACK,1000)!=SR_OK) {
            close(d->fd); d->fd=-1;
            if (rp2350_connect(d)!=SR_OK) sr_err("Reconnect required after abort.");
        }
    }
    return std_session_send_df_end(sdi);
}
static struct sr_dev_driver rp2350_la_driver_info={
    .name="rp2350-la", .longname="RP2350 Pico 2 / 2 W (LAR2 USB CDC)", .api_version=1,
    .init=std_init, .cleanup=std_cleanup, .scan=scan, .dev_list=std_dev_list, .dev_clear=dev_clear,
    .config_get=config_get, .config_set=config_set, .config_list=config_list,
    .dev_open=dev_open, .dev_close=dev_close,
    .dev_acquisition_start=dev_acquisition_start, .dev_acquisition_stop=dev_acquisition_stop
};
SR_REGISTER_DEV_DRIVER(rp2350_la_driver_info);
