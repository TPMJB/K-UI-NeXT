/* SPDX-License-Identifier: GPL-3.0-only */
#define _GNU_SOURCE
#define KUI_RETAIL_CE 1
#define KUI_ARMADA_CE_EXCERPT 1
#define KUI_ARMADA_CE_STREAM 1
#include "kui/armada_ce_excerpt_entry.h"
#include "kui/armada_ce_excerpt.h"
#include "kui/armada_ce_live_sound.h"
#include "kui/armada_ce_stream_transport.h"
#include "kui/gd_service.h"
#include "kui/retail_gd.h"
#include <assert.h>
#include <stdio.h>
#include <sys/mman.h>

static unsigned binds,queues,samples,bus_visits;
static uint32_t command_words[4];
static uint32_t audio_fault[5];
static bool pending;
static bool attached;
bool kui_armada_ce_stream_transport_attached(void) { return attached; }
static enum kui_armada_ce_live_status fake_bind(uint32_t ttb,uint32_t ccr,
    kui_armada_ce_ram_read32 reader,void *context,kui_armada_ce_ram_read_mapped mapped,
    struct kui_armada_ce_live_workspace *work,struct kui_armada_ce_live_sound *out) {
    (void)ttb;(void)ccr;(void)reader;(void)context;(void)mapped;(void)work;(void)out;
    ++binds;return KUI_ARMADA_CE_LIVE_MODULE;
}
int kui_armada_ce_excerpt_queue(const struct kui_armada_ce_excerpt_binding *binding,
        uint32_t command,uint32_t first,uint32_t last,uint32_t repeat) {
    assert(!binding);++queues;
    command_words[0]=command;command_words[1]=first;command_words[2]=last;command_words[3]=repeat;
    pending=true;return 1;
}
int kui_armada_ce_stream_needs_deferred(void) {return pending;}
void kui_armada_ce_stream_progress(uint32_t out[3]) {out[0]=123456u;out[1]=2u;out[2]=1u;}
void kui_armada_ce_stream_fault(uint32_t out[5]) {memcpy(out,audio_fault,sizeof(audio_fault));}
void kui_armada_ce_excerpt_snapshot(struct kui_armada_ce_excerpt_snapshot *out) {
    ++samples;memset(out,0,sizeof(*out));
}
void kui_armada_ce_excerpt_deferred(uint32_t unused) {(void)unused;assert(0);}
uint32_t kui_armada_ce_excerpt_array_read(uint32_t address) {(void)address;assert(0);return 0;}
uint32_t kui_armada_ce_stream_transport_event(uint32_t event,uintptr_t argument) {
    assert(event==KUI_ARMADA_CE_STREAM_BUS_IDLE && argument==0x8c008440u);
    ++bus_visits;return 17u;
}
#define kui_armada_ce_live_sound_bind_with_reader fake_bind
#include "../src/loader/armada_ce_excerpt_entry.c"
#undef kui_armada_ce_live_sound_bind_with_reader

static struct kui_retail_gd service;
static volatile uint32_t kui_retail_ce_deferred[2];
static unsigned rows,lines;
static void retail_display_values(const char *label,const uint32_t *values,unsigned count) {
    assert(label && values && count==5u);++rows;
}
static void retail_display_line(const char *label) {assert(label);++lines;}
static const struct kui_armada_ce_excerpt_entry_snapshot *observer_entry(
    uint32_t function,uint32_t command,int32_t result,uint32_t address);
#define kui_armada_ce_bind_probe observer_entry
#include "../src/loader/armada_ce_audio_observe.inc"
#undef kui_armada_ce_bind_probe
static const struct kui_armada_ce_excerpt_entry_snapshot *observer_entry(
    uint32_t function,uint32_t command,int32_t result,uint32_t address) {
    if(function==KUI_GD_REQUEST) {
        assert(address==(uint32_t)(uintptr_t)&armada_ce_excerpt_request);
        *(struct kui_armada_ce_excerpt_request *)(uintptr_t)0x8c008400u=armada_ce_excerpt_request;
        address=0x8c008400u;
    }
    return kui_armada_ce_bind_probe(function,command,result,address);
}

int main(void) {
    /* Before stage attachment all entry visits are read-only, including
     * terminal snapshots and controls accepted by the bootstrap reader. */
    const struct kui_armada_ce_excerpt_entry_snapshot zero={0};
    const uint32_t before_arm[]={KUI_GD_REQUEST,KUI_GD_EXEC,KUI_GD_CHECK,UINT32_MAX};
    for(unsigned i=0;i<sizeof(before_arm)/sizeof(before_arm[0]);++i) {
        const struct kui_armada_ce_excerpt_entry_snapshot *unarmed=kui_armada_ce_bind_probe(
            before_arm[i],KUI_GD_STOP,1,0x8c008400u);
        assert(!memcmp(unarmed,&zero,sizeof(zero)) && !queues && !binds && !samples);
    }
    attached=true;
    void *page=(void *)(uintptr_t)0x8c008000u;
    assert(mmap(page,0x1000u,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)==page);
    /* Host include's observer static lives outside the target low window.
     * Exercise its reset/tick selection separately through a checked target
     * request, then exercise actual entry's masking contract directly. */
    struct kui_armada_ce_excerpt_request *request=(void *)(uintptr_t)0x8c008400u;
    *request=(struct kui_armada_ce_excerpt_request){4u,4u,15u,0u};
    const uint32_t commands[]={20u,21u,22u,23u,33u};
    for(unsigned i=0;i<5u;i++) {
        const struct kui_armada_ce_excerpt_entry_snapshot *s=kui_armada_ce_bind_probe(
            KUI_GD_REQUEST,commands[i],1,0x8c008400u);
        assert(!s->deferred && !binds && queues==i+1u);
        assert(command_words[0]==commands[i] && command_words[1]==4u &&
            command_words[2]==4u && command_words[3]==15u);
    }
    const struct kui_armada_ce_excerpt_entry_snapshot *s=kui_armada_ce_bind_probe(KUI_GD_EXEC,0,0,0);
    assert(s->deferred==(uint32_t)(uintptr_t)kui_armada_ce_excerpt_deferred && !binds);
    pending=false;s=kui_armada_ce_bind_probe(KUI_GD_CHECK,0,0,0);assert(!s->deferred);
    request->deferred_available=1u;
    s=kui_armada_ce_bind_probe(KUI_GD_REQUEST,20u,1,0x8c008400u);
    assert(s->deferred && !binds);
    unsigned previous=queues;
    (void)kui_armada_ce_bind_probe(KUI_GD_REQUEST,20u,0,0x8c008400u);
    (void)kui_armada_ce_bind_probe(KUI_GD_REQUEST,20u,1,0x8c008401u);
    assert(queues==previous && !binds);
    assert((uintptr_t)kui_armada_ce_bind_probe(KUI_ARMADA_CE_STREAM_BUS_EVENT,
        KUI_ARMADA_CE_STREAM_BUS_IDLE,0,0x8c008440u)==17u && bus_visits==1u);
    armada_ce_audio_record(KUI_GD_INIT,0,0);assert(command_words[0]==33u);
    armada_ce_audio_record(KUI_GD_RESET,0,0);assert(command_words[0]==33u);
    armada_ce_audio_record(KUI_GD_REQUEST,KUI_GD_COMMAND_INIT,1);assert(command_words[0]==33u);
    pending=true;kui_retail_ce_deferred[0]=0x02001234u;
    armada_ce_audio_kick(KUI_GD_EXEC);assert(kui_retail_ce_deferred[0]==0x02001234u);
    kui_retail_ce_deferred[0]=0;armada_ce_audio_kick(KUI_GD_EXEC);
    assert(kui_retail_ce_deferred[0]==(uint32_t)(uintptr_t)kui_armada_ce_excerpt_deferred);
    unsigned before=samples;armada_ce_audio_report();assert(samples==before+1u && rows==6u && !binds);
    armada_ce_audio_history(0u);armada_ce_audio_history(1u);
    assert(samples==before+3u && rows==20u && lines==5u && !binds);
    armada_ce_audio_history(2u);
    assert(samples==before+4u && rows==27u && lines==7u && !binds);
    const struct kui_armada_ce_stream_underrun_snapshot no_underrun={0};
    assert(!memcmp(&snapshot.underrun,&no_underrun,sizeof(no_underrun)));
    audio_fault[0]=7u;audio_fault[1]=0x0c2789b4u;
    audio_fault[2]=0x0dcd116cu;audio_fault[3]=0x0dcd116cu;audio_fault[4]=10u;
    s=kui_armada_ce_bind_probe(UINT32_MAX,0,0,0);
    assert(!memcmp(s->fault,audio_fault,sizeof(audio_fault)) && !binds);
    assert(!kui_armada_ce_stream_bind_deferred(NULL) && !binds);
    struct kui_armada_ce_excerpt_binding binding_out;
    /* This restored-context helper is the first permitted full bind. A host
     * has no privileged native SR; initial context refusal must precede both
     * full binding and any native value read. Synthesize its CPU words. */
    void *cpu=(void *)(uintptr_t)0xff000000u;
    assert(mmap(cpu,0x1000u,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)==cpu);
    void *process=(void *)(uintptr_t)0x8c145000u;
    assert(mmap(process,0x1000u,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)==process);
    assert(!kui_armada_ce_stream_bind_deferred(&binding_out) && !binds);
    assert(snapshot.binding[1]==KUI_ARMADA_CE_LIVE_OWNER &&
        snapshot.fault[2]==KUI_ARMADA_CE_STREAM_OWNER_PROCESS);
    struct kui_armada_ce_stream_trace_snapshot history;
    kui_armada_ce_stream_trace_snapshot(&history);
    assert(history.counts[0]==1u && history.events[0][0]==KUI_ARMADA_CE_STREAM_TRACE_DISCOVERY &&
        history.events[0][4]==KUI_ARMADA_CE_LIVE_OWNER && !history.counts[1]);
    puts("PASS Armada full-stream entry: scalar masked queue, retained PIO priority, bootstrap retry, bus dispatch and deferred-only binding");
    return 0;
}
