/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "kui/toy_loader_data_probe.h"
#include "retail_storage.h"
#include "sci_sd_bus.h"

static struct kui_retail_storage card;
static struct kui_retail_gd service;
static uint32_t saved_sr,caller[2],clock_tick,clock_good;
static uint8_t output[4096],context,bus_context;
static unsigned block_count,block_calls,read_calls;
static int read_result;
static bool block_result,invalid_block,backwards_clock,change_block,change_read,freeze_block;
static bool recursive_read,recursive_payload,inside_recursive;
static kui_toy_loader_data_probe_read_fn captured_read;
static kui_toy_loader_data_probe_block_fn captured_block;
static uint32_t frozen_copy[192];
static struct kui_sci_sd_diagnostic diagnostic;
static uint32_t dma_delta[3];
static bool reset_dma_counters,nonstandard_block;

uint32_t kui_toy_loader_data_probe_host_read(uint32_t address,unsigned bytes) {
    switch(address) {
    case 0xffc00000u: assert(bytes==2u);return 0xe0au;
    case 0xffd80008u: assert(bytes==4u);return UINT32_MAX;
    case 0xffd80010u: assert(bytes==2u);return 2u;
    case 0xffd80004u: assert(bytes==1u);return clock_good;
    case 0xffd8000cu: assert(bytes==4u);return clock_tick;
    default: assert(0);return 0u;
    }
}
static void tick(uint32_t amount) {clock_tick-=amount;}
static bool other_block(void *ctx,const uint8_t *tx,uint8_t *rx,size_t count,bool slow,uint16_t *crc) {
    (void)ctx;(void)tx;(void)rx;(void)count;(void)slow;(void)crc;return false;
}
static int other_read(void *ctx,uint32_t lba,uint32_t count,uint32_t bytes,void *out) {
    (void)ctx;(void)lba;(void)count;(void)bytes;(void)out;return -999;
}
static bool original_block(void *ctx,const uint8_t *tx,uint8_t *rx,size_t count,bool slow,uint16_t *crc) {
    assert(ctx==&bus_context && !tx && rx==output && count==(nonstandard_block?256u:512u) && !slow && crc);
    ++block_calls;
    if(recursive_payload && !inside_recursive) {
        inside_recursive=true;
        bool result=captured_block(ctx,tx,rx,count,slow,crc);
        assert(result==block_result);inside_recursive=false;
    }
    tick(10u);
    diagnostic.started+=dma_delta[0];diagnostic.success+=dma_delta[1];diagnostic.fallback+=dma_delta[2];
    if(reset_dma_counters) diagnostic.started=diagnostic.success=diagnostic.fallback=0u;
    if(block_result) {*crc=0xbeefu;rx[0]=0xabu;}
    if(invalid_block) clock_good=0u;
    if(backwards_clock) clock_tick+=20u;
    if(freeze_block) {
        freeze_block=false;
        clock_good=0u;kui_toy_loader_data_probe_freeze();
        assert(service.ops.read==other_read || service.ops.read!=captured_read);
        assert(card.device.sd.bus.transfer_block==original_block);
        memcpy(frozen_copy,kui_toy_loader_data_probe_words(),sizeof frozen_copy);
    }
    return block_result;
}
static int original_read(void *ctx,uint32_t lba,uint32_t count,uint32_t bytes,void *out) {
    assert(ctx==&context && lba==901u && count==2u && (bytes==2048u || bytes==2352u) && out==output);
    assert(service.ops.read==original_read); /* restored before entering low original */
    ++read_calls;
    if(recursive_read && !inside_recursive) {
        inside_recursive=true;
        assert(captured_read(ctx,lba,count,bytes,out)==read_result);
        inside_recursive=false;
    }
    tick(block_count?3u:9u);
    for(unsigned i=0;i<block_count;++i) {
        uint16_t crc=0x1234u;
        if(i) tick(5u);
        captured_block=card.device.sd.bus.transfer_block;
        assert(captured_block(&bus_context,NULL,output,nonstandard_block?256u:512u,false,&crc)==block_result);
        assert(crc==(block_result?0xbeefu:0x1234u));
        clock_good=1u;
    }
    if(block_count) tick(7u);
    if(change_block) card.device.sd.bus.transfer_block=other_block;
    if(change_read) service.ops.read=other_read;
    return read_result;
}
static const struct kui_toy_loader_data_probe_report *view(void) {
    return (const struct kui_toy_loader_data_probe_report *)(const void *)kui_toy_loader_data_probe_words();
}
static void initialize(void) {
    kui_toy_loader_data_probe_host_reset();
    card=(struct kui_retail_storage){0};service=(struct kui_retail_gd){0};
    card.transport=KUI_STORAGE_SCI;card.device.sd.bus.ctx=&bus_context;
    card.device.sd.bus.transfer_block=original_block;
    service.ops.context=&context;service.ops.read=original_read;
    service.command=KUI_GD_DMAREAD;service.token=17u;service.lba=899u;
    saved_sr=UINT32_C(0x100000f0);caller[0]=UINT32_C(0x8c012340);caller[1]=UINT32_C(0x8c111000);
    clock_tick=UINT32_MAX;clock_good=1u;block_count=2u;block_calls=read_calls=0u;
    read_result=0;block_result=true;invalid_block=backwards_clock=false;
    change_block=change_read=freeze_block=recursive_read=recursive_payload=inside_recursive=false;
    captured_read=NULL;captured_block=NULL;
    diagnostic=(struct kui_sci_sd_diagnostic){0};
    diagnostic.started=17u;diagnostic.success=19u;diagnostic.fallback=23u;
    dma_delta[0]=dma_delta[1]=1u;dma_delta[2]=0u;
    reset_dma_counters=nonstandard_block=false;
    kui_toy_loader_data_probe_host_bind(&card,&saved_sr,caller,original_read,original_block);
    kui_toy_loader_data_probe_host_bind_diagnostic(&diagnostic);
}
static int run(uint32_t phase,uint32_t bytes) {
    kui_toy_loader_data_probe_begin(&service,KUI_GD_EXEC,phase);
    captured_read=service.ops.read;
    int result=captured_read(&context,901u,2u,bytes,output);
    kui_toy_loader_data_probe_end(&service);
    return result;
}
static void partition_and_passthrough(void) {
    initialize();assert(run(0u,2048u)==0);
    const struct kui_toy_loader_data_probe_phase *p=&view()->phase[0];
    assert(service.ops.read==original_read && card.device.sd.bus.transfer_block==original_block);
    assert(read_calls==1u && block_calls==2u && output[0]==0xabu);
    assert(p->data_visits==1u && p->read_calls==1u && p->read_ok==1u && !p->read_failed);
    assert(p->payload_calls==2u && p->payload_ok==2u && p->payload_bytes==1024u);
    assert(p->read_body.samples==1u && p->read_body.ticks_total==35u);
    assert(p->first_payload_gap.samples==1u && p->first_payload_gap.ticks_total==3u);
    assert(p->payload_body.samples==2u && p->payload_body.ticks_total==20u);
    assert(p->inter_payload_gap.samples==1u && p->inter_payload_gap.ticks_total==5u);
    assert(p->tail_gap.samples==1u && p->tail_gap.ticks_total==7u);
    assert(p->read_body.ticks_total==p->first_payload_gap.ticks_total+p->payload_body.ticks_total+
        p->inter_payload_gap.ticks_total+p->tail_gap.ticks_total);
    const uint32_t expected[8]={17u,899u,901u,2u,UINT32_C(0x100000f0),UINT32_C(0x8c012340),35u,2u};
    assert(!memcmp(expected,p->worst_read,sizeof expected) && p->sr_buckets[31]==1u);
    assert(!p->invalid_intervals && !p->nonstandard_payload && !p->callback_conflicts);
    assert(view()->magic==KUI_TOY_LOADER_DATA_PROBE_MAGIC && view()->words==192u && view()->phase_words==88u);
    assert(kui_toy_loader_data_probe_word_count()==192u);
    assert(view()->version==2u && view()->feature_flags==1u && !view()->payload_mode && !view()->reserved);
    assert(!view()->payload_attempts && !view()->payload_declines && !view()->payload_publications && !view()->payload_failed);
    assert(p->dma_started==2u && p->dma_payload_ok==2u && !p->pio_fallback && !p->prestart_failed);
    assert(!p->dma_delta_invalid && !p->dma_counter_wraps && p->dma_payload_body.samples==2u);
    assert(p->dma_payload_body.ticks_total==20u && !p->pio_payload_body.samples);
    assert(!view()->phase[1].dma_started && !view()->phase[1].dma_delta_invalid);
    initialize();assert(run(1u,2048u)==0);assert(!view()->phase[0].data_visits && view()->phase[1].read_body.ticks_total==35u);
    initialize();clock_tick=5u;assert(run(0u,2048u)==0);assert(view()->phase[0].read_body.ticks_total==35u);
}
static void audio_and_raw_are_untouched(void) {
    initialize();service.command=KUI_RETAIL_GD_PLAY;
    assert(run(0u,2048u)==0);assert(service.ops.read==original_read);
    assert(!view()->phase[0].data_visits && !view()->phase[0].read_calls && !view()->phase[0].payload_calls);
    initialize();assert(run(0u,2352u)==0);
    assert(view()->phase[0].data_visits==1u && !view()->phase[0].read_calls && !view()->phase[0].payload_calls);
}
static void failure_and_no_payload(void) {
    initialize();read_result=-37;block_result=false;dma_delta[1]=0u;assert(run(0u,2048u)==-37);
    assert(view()->phase[0].read_failed==1u && view()->phase[0].payload_failed==2u);
    assert(view()->phase[0].dma_started==2u && !view()->phase[0].dma_payload_ok);
    assert(view()->phase[0].dma_payload_body.samples==2u && !view()->phase[0].dma_delta_invalid);
    assert(service.ops.read==original_read && card.device.sd.bus.transfer_block==original_block);
    initialize();block_count=0u;read_result=4;assert(run(0u,2048u)==4);
    const struct kui_toy_loader_data_probe_phase *p=&view()->phase[0];
    assert(p->read_failed==1u && p->no_payload_reads==1u && p->read_body.ticks_total==9u);
    assert(!p->first_payload_gap.samples && !p->tail_gap.samples && !p->payload_calls);
}
static void sr_and_scope(void) {
    initialize();service.pending=0u;
    for(unsigned i=0;i<32u;++i) {
        saved_sr=((i&16u)?UINT32_C(0x10000000):0u)|((i&15u)<<4u);
        kui_toy_loader_data_probe_begin(&service,KUI_GD_CHECK,0u);
        kui_toy_loader_data_probe_end(&service);
    }
    for(unsigned i=0;i<32u;++i) assert(view()->phase[0].sr_buckets[i]==1u);
    assert(view()->phase[0].data_visits==32u && !view()->phase[0].read_calls);
    kui_toy_loader_data_probe_begin(&service,KUI_GD_EXEC,0u);captured_read=service.ops.read;
    kui_toy_loader_data_probe_begin(&service,KUI_GD_CHECK,1u);
    kui_toy_loader_data_probe_end(&service);assert(service.ops.read==captured_read);
    kui_toy_loader_data_probe_end(&service);assert(service.ops.read==original_read);
    kui_toy_loader_data_probe_end(&service);assert(view()->nested_begin==1u && view()->unmatched_end==1u);
}
static void conflicts_and_reentrancy(void) {
    initialize();service.ops.read=other_read;
    kui_toy_loader_data_probe_begin(&service,KUI_GD_EXEC,0u);
    assert(service.ops.read==other_read);kui_toy_loader_data_probe_end(&service);
    assert(view()->phase[0].callback_conflicts==1u && !view()->phase[0].read_calls);
    initialize();card.device.sd.bus.transfer_block=other_block;
    kui_toy_loader_data_probe_begin(&service,KUI_GD_EXEC,0u);kui_toy_loader_data_probe_end(&service);
    assert(service.ops.read==original_read && card.device.sd.bus.transfer_block==other_block);
    assert(view()->phase[0].callback_conflicts==1u);
    initialize();change_block=true;assert(run(0u,2048u)==0);
    assert(card.device.sd.bus.transfer_block==other_block && view()->phase[0].callback_conflicts==1u);
    assert(!view()->phase[0].read_body.samples);
    initialize();change_read=true;assert(run(0u,2048u)==0);
    assert(service.ops.read==other_read && view()->phase[0].callback_conflicts==1u);
    initialize();recursive_read=true;assert(run(0u,2048u)==0);
    assert(read_calls==2u && view()->phase[0].reentrant_reads==1u && view()->phase[0].reentrant_payload==2u);
    assert(!view()->phase[0].read_body.samples && service.ops.read==original_read);
    assert(view()->phase[0].dma_delta_invalid==2u && !view()->phase[0].dma_started);
    initialize();recursive_payload=true;assert(run(0u,2048u)==0);
    assert(block_calls==4u && view()->phase[0].reentrant_payload==2u && !view()->phase[0].read_body.samples);
    assert(view()->phase[0].dma_delta_invalid==2u && !view()->phase[0].dma_started);
}
static void dma_classification_and_reset(void) {
    initialize();dma_delta[0]=dma_delta[1]=0u;dma_delta[2]=1u;
    assert(run(0u,2048u)==0);
    assert(view()->phase[0].pio_fallback==2u && view()->phase[0].pio_payload_body.ticks_total==20u);
    assert(!view()->phase[0].dma_started && !view()->phase[0].dma_delta_invalid);
    initialize();dma_delta[0]=dma_delta[1]=0u;dma_delta[2]=1u;block_result=false;read_result=-1;
    assert(run(0u,2048u)==-1);
    assert(view()->phase[0].pio_fallback==2u && view()->phase[0].pio_payload_body.samples==2u);
    assert(view()->phase[0].payload_failed==2u && !view()->phase[0].dma_delta_invalid);
    initialize();dma_delta[0]=dma_delta[1]=0u;block_result=false;read_result=-1;
    assert(run(0u,2048u)==-1);
    assert(view()->phase[0].prestart_failed==2u && !view()->phase[0].dma_delta_invalid);
    assert(!view()->phase[0].dma_payload_body.samples && !view()->phase[0].pio_payload_body.samples);
    /* Plausible but unsupported triples must not be labeled as PIO or DMA. */
    static const uint32_t rejected[][4]={
        {0u,0u,0u,1u},{1u,0u,0u,1u},{1u,1u,0u,0u},
        {0u,1u,0u,1u},{1u,1u,1u,1u},{2u,2u,0u,1u},
        {0u,0u,2u,1u},{1u,0u,1u,0u}};
    for(unsigned i=0;i<sizeof rejected/sizeof rejected[0];++i) {
        initialize();block_count=1u;
        memcpy(dma_delta,rejected[i],sizeof dma_delta);block_result=rejected[i][3]!=0u;
        assert(run(0u,2048u)==0);
        const struct kui_toy_loader_data_probe_phase *p=&view()->phase[0];
        assert(p->dma_delta_invalid==1u && !p->dma_started && !p->dma_payload_ok);
        assert(!p->pio_fallback && !p->prestart_failed && !p->dma_counter_wraps);
    }
    initialize();block_count=1u;diagnostic.started=diagnostic.success=UINT32_MAX;
    assert(run(0u,2048u)==0);
    assert(view()->phase[0].dma_started==1u && view()->phase[0].dma_payload_ok==1u);
    assert(view()->phase[0].dma_counter_wraps==2u && !view()->phase[0].dma_delta_invalid);
    initialize();block_count=1u;dma_delta[0]=dma_delta[1]=0u;dma_delta[2]=1u;
    diagnostic.fallback=UINT32_MAX;assert(run(0u,2048u)==0);
    assert(view()->phase[0].pio_fallback==1u && view()->phase[0].dma_counter_wraps==1u);
    initialize();block_count=1u;reset_dma_counters=true;assert(run(0u,2048u)==0);
    assert(view()->phase[0].dma_delta_invalid==1u && !view()->phase[0].dma_counter_wraps);
    initialize();diagnostic.phase=KUI_SCI_PHASE_FEED;assert(run(0u,2048u)==0);
    assert(view()->phase[0].dma_started==2u && diagnostic.phase==KUI_SCI_PHASE_FEED);
    initialize();kui_toy_loader_data_probe_host_bind_diagnostic(NULL);assert(run(0u,2048u)==0);
    assert(view()->phase[0].dma_delta_invalid==2u && !view()->phase[0].dma_started);
    initialize();nonstandard_block=true;assert(run(0u,2048u)==0);
    assert(view()->phase[0].nonstandard_payload==2u && view()->phase[0].payload_bytes==512u);
    assert(!view()->phase[0].dma_started && !view()->phase[0].dma_delta_invalid);
    initialize();assert(run(0u,2048u)==0);
    const uint32_t started=diagnostic.started,success=diagnostic.success,fallback=diagnostic.fallback;
    kui_toy_loader_data_probe_host_reset();
    assert(diagnostic.started==started && diagnostic.success==success && diagnostic.fallback==fallback);
    assert(!view()->phase[0].dma_started && !view()->phase[0].dma_counter_wraps);
    assert(run(0u,2048u)==0); /* reset unbinds diagnostic and never resets low counters */
    assert(view()->phase[0].dma_delta_invalid==2u && !view()->phase[0].dma_started);
}
static void invalid_saturated_and_frozen(void) {
    initialize();invalid_block=true;assert(run(0u,2048u)==0);
    assert(!view()->phase[0].read_body.samples && view()->phase[0].invalid_intervals>=3u);
    assert(view()->phase[0].dma_delta_invalid==2u && !view()->phase[0].dma_started);
    initialize();backwards_clock=true;assert(run(0u,2048u)==0);
    assert(view()->phase[0].invalid_intervals>=2u);
    initialize();struct kui_toy_loader_data_probe_report *mutable=(struct kui_toy_loader_data_probe_report *)(void *)kui_toy_loader_data_probe_words();
    mutable->phase[0].read_calls=UINT32_MAX;mutable->phase[0].payload_bytes=UINT32_MAX-1u;
    assert(run(0u,2048u)==0);assert(view()->saturated && view()->phase[0].read_calls==UINT32_MAX && view()->phase[0].payload_bytes==UINT32_MAX);
    initialize();kui_toy_loader_data_probe_begin(&service,KUI_GD_EXEC,0u);captured_read=service.ops.read;
    kui_toy_loader_data_probe_freeze();assert(service.ops.read==original_read);
    memcpy(frozen_copy,kui_toy_loader_data_probe_words(),sizeof frozen_copy);
    kui_toy_loader_data_probe_begin(&service,KUI_GD_CHECK,1u);kui_toy_loader_data_probe_end(&service);
    assert(!memcmp(frozen_copy,kui_toy_loader_data_probe_words(),sizeof frozen_copy));
    initialize();freeze_block=true;assert(run(0u,2048u)==0);
    assert(view()->frozen && service.ops.read==original_read && card.device.sd.bus.transfer_block==original_block);
    assert(!memcmp(frozen_copy,kui_toy_loader_data_probe_words(),sizeof frozen_copy));
    assert(!view()->phase[0].dma_started && !view()->phase[0].dma_delta_invalid);
}
int main(void) {
    partition_and_passthrough();audio_and_raw_are_untouched();failure_and_no_payload();
    sr_and_scope();conflicts_and_reentrancy();dma_classification_and_reset();invalid_saturated_and_frozen();
    puts("DATA probe production: exact callbacks/arguments/results, timing partition, SR bins, phase, audio exclusion, failure cleanup, wrap/profile rejection, conflict/reentrancy, saturation and frozen report PASS");
    return 0;
}
