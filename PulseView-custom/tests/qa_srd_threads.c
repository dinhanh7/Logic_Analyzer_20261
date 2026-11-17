/* Regression: concurrent GUI stacks must remain visible in libsigrokdecode. */
#include <libsigrokdecode/libsigrokdecode.h>
#include <glib.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
static pthread_barrier_t barrier;
static atomic_uint failures, annotations;
static void output(struct srd_proto_data *p, void *unused) {
    (void)p;(void)unused;atomic_fetch_add(&annotations,1);
}
static void *worker(void *unused) {
    (void)unused;
    for(unsigned round=0;round<10;round++) {
        struct srd_session *session=NULL;
        pthread_barrier_wait(&barrier);
        if(srd_session_new(&session)!=SRD_OK){atomic_fetch_add(&failures,1);return NULL;}
        pthread_barrier_wait(&barrier);
        GHashTable *opts=g_hash_table_new_full(g_str_hash,g_str_equal,NULL,(GDestroyNotify)g_variant_unref);
        g_hash_table_insert(opts,"baudrate",g_variant_ref_sink(g_variant_new_int64(115200)));
        struct srd_decoder_inst *di=srd_inst_new(session,"uart",opts);
        g_hash_table_destroy(opts);
        if(!di){atomic_fetch_add(&failures,1);return NULL;}
        GHashTable *channels=g_hash_table_new_full(g_str_hash,g_str_equal,NULL,(GDestroyNotify)g_variant_unref);
        g_hash_table_insert(channels,"rx",g_variant_ref_sink(g_variant_new_int32(0)));
        srd_inst_channel_set_all(di,channels);g_hash_table_destroy(channels);
        srd_session_metadata_set(session,SRD_CONF_SAMPLERATE,g_variant_new_uint64(1843200));
        srd_pd_output_callback_add(session,SRD_OUTPUT_ANN,output,NULL);
        if(srd_session_start(session)!=SRD_OK)atomic_fetch_add(&failures,1);
        uint8_t data[2048];memset(data,1,sizeof data);unsigned at=100;
        const char *message="LA1234";
        for(unsigned i=0;i<6;i++)for(unsigned bit=0;bit<10;bit++) {
            unsigned level=bit==0?0:bit==9?1:((message[i]>>(bit-1))&1);
            memset(data+at,level,16);at+=16;
        }
        if(srd_session_send(session,0,sizeof data,data,sizeof data,1)!=SRD_OK)atomic_fetch_add(&failures,1);
        srd_session_send_eof(session);
        pthread_barrier_wait(&barrier);
        srd_session_destroy(session);
        pthread_barrier_wait(&barrier);
    }
    return NULL;
}
int main(void) {
    if(srd_init(NULL)!=SRD_OK||srd_decoder_load("uart")!=SRD_OK)return 2;
    pthread_barrier_init(&barrier,NULL,4);pthread_t threads[4];
    for(unsigned i=0;i<4;i++)if(pthread_create(&threads[i],NULL,worker,NULL))return 3;
    for(unsigned i=0;i<4;i++)pthread_join(threads[i],NULL);
    printf("sessions=40 failures=%u annotations=%u\n",atomic_load(&failures),atomic_load(&annotations));
    pthread_barrier_destroy(&barrier);srd_exit();
    return atomic_load(&failures)||atomic_load(&annotations)<240;
}
