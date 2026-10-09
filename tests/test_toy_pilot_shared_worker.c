/* SPDX-License-Identifier: GPL-3.0-only */
/* The real eight-block worker must distinguish an asynchronous lease wait
 * from card failure. Its physical stereo/ARM fixture remains independent
 * from the shared SCI engine and supplies a precisely scheduled grant. */
#define KUI_TOY_PILOT_SHARED_SCI 1
#include "toy_pilot_async_fixture.h"

uint8_t __toy_pilot_gd_stack_bottom[64],__toy_pilot_gd_stack_top[64];
static struct kui_toy_pilot_sci_stats transport;
static int grant;
static bool audio_held;
static unsigned claims,releases,cancels;

void kui_toy_pilot_sci_init(const struct kui_retail_manifest *m,
    const struct kui_loader_sd *c,enum kui_loader_sd_result (*a)(void),void (*r)(void)) {
    (void)m; (void)c; (void)a; (void)r;
}
int kui_toy_pilot_sci_pump(struct kui_retail_gd *s) { (void)s; return KUI_TOY_SCI_PENDING; }
void kui_toy_pilot_sci_cancel(struct kui_retail_gd *s) { (void)s; ++cancels; }
int kui_toy_pilot_sci_audio_acquire(void) {
    assert((sr&0xf0u)==0xf0u && !audio_held); ++claims;
    if(grant==KUI_TOY_SCI_OK) audio_held=true;
    return grant;
}
void kui_toy_pilot_sci_audio_release(void) {
    assert((sr&0xf0u)==0xf0u && audio_held); audio_held=false; ++releases;
}
void kui_toy_pilot_sci_audio_cancel(void) { ++cancels; }
const struct kui_toy_pilot_sci_stats *kui_toy_pilot_sci_snapshot(void) { return &transport; }
uint32_t kui_toy_pilot_sci_irq(void) { return 1u; }
static int leased_raw(uint32_t lba,uint32_t sectors,void *out) {
    assert(audio_held && (data_command==16u || data_command==17u));
    /* The old fixture verifies every PCM sample and its normal callback
     * contract. In this experiment the arbiter grants a boundary while the
     * logical GD handle can remain pending, so hide only that old premise. */
    uint32_t saved=data_command; data_command=0u;
    int result=raw(lba,sectors,out); data_command=saved;
    return result;
}
static void prepare_wait(uint32_t command) {
    prepare(200u); grant=KUI_TOY_SCI_PENDING; audio_held=false;
    claims=releases=cancels=0u; data_command=command;
    owner.config.read_raw=(uint32_t)(uintptr_t)leased_raw;
    for(unsigned n=0;n<20u;n++) visit(13021u);
    assert(owner.stats.state==KUI_TOY_PILOT_PREFILL && !owner.stop_wait && claims);
    assert(!owner.stats.fault && !owner.stats.raw_errors && !reports && !reads && !copies);
}
static void pending_then_grant(uint32_t command) {
    prepare_wait(command);
    unsigned calls=owner.stats.raw_calls,raw_timing=owner.stats.raw_read_timing_calls;
    uint32_t frame=owner.fill_frame,stream=owner.ring_fill_stream,before_sr=sr;
    for(unsigned n=0;n<20u;n++) {
        fill_quantum();
        assert(sr==before_sr && !owner.stats.fault && !owner.stats.raw_errors && !reports);
        assert(!audio_held && !reads && !copies && !releases);
        assert(owner.stats.raw_calls==calls && owner.stats.raw_read_timing_calls==raw_timing);
        assert(owner.fill_frame==frame && owner.ring_fill_stream==stream);
        assert(owner.model.banks[0].filled==0u && !owner.raw_generation);
    }
    grant=KUI_TOY_SCI_OK; strict_source_reads=true; expected_read_lba=50u;
    unsigned grants=claims;
    for(unsigned n=0;n<7u;n++) fill_quantum();
    assert(sr==before_sr && !owner.stats.fault && !reports && !audio_held);
    assert(reads==7u && releases==7u && claims==grants+7u && owner.stats.raw_calls==calls+7u);
    assert(owner.ring_fill_stream==7u*588u && owner.fill_frame==7u*588u);
    assert(owner.model.banks[0].state==KUI_TOY_PILOT_BANK_READY && owner.model.banks[0].filled==4096u);
    assert(owner.model.banks[1].state==KUI_TOY_PILOT_BANK_FILLING && owner.model.banks[1].filled==20u);
    for(uint32_t f=0;f<7u*588u;f++) {
        assert((uint16_t)sample(owner.stats.sound_address+f*2u)==(uint16_t)f);
        assert((uint16_t)sample(owner.stats.sound_address+65536u+f*2u)==(uint16_t)~f);
    }
    /* The sector split across blocks reuses its cached tail; neither lease
     * grants nor physical raw calls can grow for the second fragment. */
    assert(owner.stats.raw_read_timing_calls==raw_timing+7u);
}
static void fault_is_not_pending(void) {
    prepare_wait(16u); grant=KUI_TOY_SCI_FAULT;
    unsigned calls=owner.stats.raw_calls;
    fill_quantum();
    assert(owner.stats.fault==KUI_TOY_PILOT_FAULT_CARD && owner.stats.raw_errors==1u);
    assert(owner.stats.raw_calls==calls && !reads && !copies && !audio_held && !releases);
}
static void revoke_pending_epoch(void) {
    prepare_wait(16u); unsigned before=cancels,calls=owner.stats.raw_calls;
    assert(kui_toy_pilot_request(CMD_PAUSE,0u,0u,0u)==3u);
    assert(cancels>before); before=cancels;
    grant=KUI_TOY_SCI_OK; fill_quantum();
    assert(!reads && !copies && owner.stats.raw_calls==calls && !audio_held);
    kui_toy_pilot_worker_revoke(); assert(cancels>before);
    fill_quantum();
    assert(!reads && !copies && owner.stats.raw_calls==calls && !audio_held);
}
int main(void) {
    pending_then_grant(16u); pending_then_grant(17u); fault_is_not_pending(); revoke_pending_epoch();
    puts("Shared SCI worker: pending lease leaves PCM/generation/raw counters intact; granted callbacks remain once-only and stereo ordered");
    return 0;
}
