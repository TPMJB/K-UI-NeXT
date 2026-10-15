/* SPDX-License-Identifier: GPL-3.0-only */
/* The production low reader, production audio observer and production high
 * transport across bootstrap/relay ordering. Reuse CE's existing SCI/card
 * fixture, rather than hand-mirroring initialization or the GD read engine. */
#define _GNU_SOURCE
#include "kui/armada_ce_excerpt_entry.h"
#include "kui/armada_ce_excerpt.h"
#include "kui/armada_ce_live_sound.h"
#include "kui/armada_ce_stream_transport.h"
#include <sys/mman.h>

/* Inspect the real high transport BSS: the failed console's first altered
 * byte was its bus.version. Include the implementation only in this target. */
#define finish transport_finish
#include "../src/loader/armada_ce_stream_transport.c"
#undef finish

#define main existing_ce_reader_main
#include "test_retail_async_ce.c"
#undef main

static unsigned audio_visits,rows,lines,wakes;
static volatile uint32_t kui_retail_ce_deferred[2];
static struct {
    uint32_t binds,queues,samples,pending,command,first,last,repeat;
} player_mock;

static enum kui_armada_ce_live_status fake_bind(uint32_t ttb,uint32_t ccr,
    kui_armada_ce_ram_read32 reader,void *context,kui_armada_ce_ram_read_mapped mapped,
    struct kui_armada_ce_live_workspace *work,struct kui_armada_ce_live_sound *out) {
    (void)ttb;(void)ccr;(void)reader;(void)context;(void)mapped;(void)work;(void)out;
    ++player_mock.binds;return KUI_ARMADA_CE_LIVE_MODULE;
}
int kui_armada_ce_excerpt_queue(const struct kui_armada_ce_excerpt_binding *binding,
    uint32_t command,uint32_t first,uint32_t last,uint32_t repeat) {
    CHECK(binding==NULL);++player_mock.queues;player_mock.pending=1u;
    player_mock.command=command;player_mock.first=first;
    player_mock.last=last;player_mock.repeat=repeat;return 1;
}
int kui_armada_ce_stream_needs_deferred(void) {return player_mock.pending!=0u;}
void kui_armada_ce_stream_progress(uint32_t out[3]) {out[0]=123456u;out[1]=2u;out[2]=1u;}
void kui_armada_ce_stream_fault(uint32_t out[5]) {memset(out,0,5u*sizeof(*out));}
void kui_armada_ce_excerpt_snapshot(struct kui_armada_ce_excerpt_snapshot *out) {
    ++player_mock.samples;memset(out,0,sizeof(*out));
}
void kui_armada_ce_excerpt_deferred(uint32_t unused) {(void)unused;CHECK(false);}
uint32_t kui_armada_ce_excerpt_array_read(uint32_t address) {(void)address;CHECK(false);return 0;}
#define kui_armada_ce_live_sound_bind_with_reader fake_bind
#include "../src/loader/armada_ce_excerpt_entry.c"
#undef kui_armada_ce_live_sound_bind_with_reader

static void zero_state(const void *address,size_t bytes) {
    const uint8_t *state=address;
    for(size_t i=0;i<bytes;++i) CHECK(state[i]==0u);
}
static void pristine(void) {
    /* These are the actual controller entry/binder/transport BSS objects.
     * The scalar player mock catches any premature queue or sample call. */
    zero_state(&t,sizeof(t));zero_state(&snapshot,sizeof(snapshot));
    zero_state(&audio,sizeof(audio));zero_state(&fresh,sizeof(fresh));
    zero_state(&workspace,sizeof(workspace));zero_state(&live,sizeof(live));
    zero_state(&deferred_workspace,sizeof(deferred_workspace));
    zero_state(&deferred_live,sizeof(deferred_live));
    zero_state(&player_mock,sizeof(player_mock));
    CHECK(!kui_armada_ce_stream_transport_attached());
}
static const struct kui_armada_ce_excerpt_entry_snapshot *observer_entry(
    uint32_t function,uint32_t command,int32_t result,uint32_t address) {
    ++audio_visits;
    if(function==KUI_GD_REQUEST) {
        CHECK(result>0 && address!=0u);
        /* The authored low resident request has a fixed physical address on
         * console. The included host observer owns an ordinary static, so
         * map only this ABI object into the actual entry's admitted span. */
        *(struct kui_armada_ce_excerpt_request *)(uintptr_t)0x8c008400u=
            *(const struct kui_armada_ce_excerpt_request *)(uintptr_t)address;
        address=0x8c008400u;
    }
    return kui_armada_ce_bind_probe(function,command,result,address);
}
void kui_retail_ce_stream_wake(void) {++wakes;take_events();}
void retail_display_values(const char *label,const uint32_t *values,unsigned count) {
    CHECK(label && values && count==5u);++rows;
}
void retail_display_line(const char *label) {CHECK(label);++lines;}
#define service R.shared.service
#define kui_armada_ce_bind_probe observer_entry
#include "../src/loader/armada_ce_audio_observe.inc"
#undef kui_armada_ce_bind_probe
#undef service

static void bootstrap_observers(void) {
    const uint32_t accepted[]={KUI_RETAIL_GD_PLAY,KUI_RETAIL_GD_PLAY2,
        KUI_RETAIL_GD_PAUSE,KUI_RETAIL_GD_RELEASE,KUI_GD_STOP};
    armada_ce_audio_record(KUI_GD_INIT,0u,0);
    armada_ce_audio_record(KUI_GD_RESET,0u,0);
    armada_ce_audio_record(KUI_GD_REQUEST,KUI_GD_COMMAND_INIT,1);
    for(unsigned i=0;i<sizeof(accepted)/sizeof(accepted[0]);++i)
        armada_ce_audio_record(KUI_GD_REQUEST,accepted[i],1);
    armada_ce_audio_kick(KUI_GD_EXEC);
    armada_ce_audio_kick(KUI_GD_CHECK);
    armada_ce_audio_report();
    pristine();
    CHECK(kui_retail_ce_deferred[0]==0u && kui_retail_ce_deferred[1]==0u);
}

int main(void) {
    kui_armada_ce_stream_transport_test_reset();
    void *page=(void *)(uintptr_t)0x8c008000u;
    CHECK(mmap(page,0x1000u,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)==page);
    setup(2000u,false); /* invokes actual kui_retail_async_init */
    manifest.storage_transport=KUI_STORAGE_SCI;
    CHECK(kui_retail_manifest_validate(&manifest)==KUI_GAME_OK);
    pristine();bootstrap_observers();

    /* Bootstrap 2 loads the real image through the real low GD/data reader.
     * Controller BSS must remain exactly zero, including at INIT/RESET and
     * repeated EXEC/CHECK visits before the relay's integrity validation. */
    hw.table[0]=hw.table[1]=0u;
    card.nac_first=3u*256u;
    mode(2048u);
    int32_t token=request(KUI_GD_PIOREAD,45001u,40u,OUTPUT);
    CHECK(finish(token,0u)==KUI_GD_COMPLETED);
    reference_read(45001u,40u);
    CHECK(!memcmp(ram+(OUTPUT-BEGIN),expected,40u*2048u));
    CHECK(!R.engine.isr && !hw.table[0] && !hw.table[1]);
    pristine();bootstrap_observers();

    /* MMU/handler state alone cannot arm the controller before the relay. */
    hw.mmucr=1u;hw.table[0]=0x1234u;hw.table[1]=CE_DEFAULT;
    token=request(KUI_GD_DMAREAD,45041u,8u,OUTPUT);
    CHECK(finish(token,0u)==KUI_GD_COMPLETED && !R.engine.isr);
    pristine();bootstrap_observers();
    hw.table[0]=CE_DEFAULT;
    (void)gd(KUI_GD_EXEC,0u,0u);
    CHECK(R.engine.isr && installed() && !kui_armada_ce_stream_transport_attached());
    pristine();bootstrap_observers();

    /* The stage sends the immutable low descriptor to the actual fixed high
     * entry only after all code/BSS/source guards. No low state flag or
     * native API is needed; successful attachment intentionally begins BSS. */
    extern const struct kui_armada_ce_stream_bus kui_retail_ce_stream_bus;
    CHECK((uintptr_t)&kui_retail_ce_stream_bus<=UINT32_MAX);
    CHECK((uintptr_t)kui_armada_ce_bind_probe(KUI_ARMADA_CE_STREAM_BUS_EVENT,
        KUI_ARMADA_CE_STREAM_BUS_ATTACH,0,(uint32_t)(uintptr_t)&kui_retail_ce_stream_bus)==1u);
    CHECK(kui_armada_ce_stream_transport_attached() && t.stats.attached==1u);
    const struct kui_armada_ce_stream_bus admitted=t.bus;
    for(unsigned i=0;i<16u;++i)
        CHECK((uintptr_t)kui_armada_ce_bind_probe(KUI_ARMADA_CE_STREAM_BUS_EVENT,
            KUI_ARMADA_CE_STREAM_BUS_ATTACH,0,
            (uint32_t)(uintptr_t)&kui_retail_ce_stream_bus)==1u);
    CHECK(!memcmp(&admitted,&t.bus,sizeof(admitted)));

    /* Accepted controls route only after arm. Existing data callbacks keep
     * priority over the audio deferred slot; reset remains scalar STOP. */
    R.shared.service.outputs[0]=4u;R.shared.service.outputs[1]=4u;
    R.shared.service.outputs[2]=15u;
    rows=0u;
    armada_ce_audio_record(KUI_GD_REQUEST,KUI_RETAIL_GD_PLAY,1);
    CHECK(player_mock.queues==1u && player_mock.command==KUI_RETAIL_GD_PLAY);
    CHECK(player_mock.first==4u && player_mock.last==4u && player_mock.repeat==15u);
    CHECK(kui_retail_ce_deferred[0]==(uint32_t)(uintptr_t)kui_armada_ce_excerpt_deferred);
    kui_retail_ce_deferred[0]=0x02001234u;
    armada_ce_audio_record(KUI_GD_RESET,0u,0);
    CHECK(player_mock.command==KUI_GD_STOP && kui_retail_ce_deferred[0]==0x02001234u);
    unsigned visits=audio_visits;armada_ce_audio_kick(KUI_GD_EXEC);
    CHECK(audio_visits==visits && kui_retail_ce_deferred[0]==0x02001234u);
    kui_retail_ce_deferred[0]=0u;
    armada_ce_audio_kick(KUI_GD_EXEC);
    CHECK(kui_retail_ce_deferred[0]==(uint32_t)(uintptr_t)kui_armada_ce_excerpt_deferred);
    armada_ce_audio_report();CHECK(rows==6u);
    armada_ce_audio_history(0u);armada_ce_audio_history(1u);
    CHECK(rows==20u && lines==5u);

    /* The adopted SCI descriptor still reads full audio bytes after the
     * delayed handoff. No detached synthetic bus or separate card model. */
    static uint8_t source_audio[2u*2352u] __attribute__((aligned(32)));
    CHECK(kui_armada_ce_stream_transport_init(&manifest,(uintptr_t)source_audio,
                                             (uintptr_t)source_audio+sizeof(source_audio)));
    CHECK(kui_armada_ce_stream_transport_submit(1u,3u,2u,(uintptr_t)source_audio,sizeof(source_audio))==
          KUI_ARMADA_CE_STREAM_TRANSPORT_PENDING);
    enum kui_armada_ce_stream_transport_status state=KUI_ARMADA_CE_STREAM_TRANSPORT_PENDING;
    for(unsigned i=0;i<100000u && state==KUI_ARMADA_CE_STREAM_TRANSPORT_PENDING;++i) {
        elapse();state=kui_armada_ce_stream_transport_pump();
    }
    CHECK(state==KUI_ARMADA_CE_STREAM_TRANSPORT_DONE);
    CHECK(kui_retail_image_read(&reference,3u,2u,KUI_GAME_SECTOR_RAW,
                                expected,sizeof(expected))==KUI_GAME_OK);
    CHECK(!memcmp(source_audio,expected,sizeof(source_audio)));
    printf("Armada stream bootstrap/relay handoff: %u checks passed\n",checks);
    return 0;
}
