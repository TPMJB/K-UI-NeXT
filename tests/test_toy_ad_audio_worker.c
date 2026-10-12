/* SPDX-License-Identifier: GPL-3.0-only */
/* Compile the production worker against the existing independent physical
 * PCM consumer. Only its MMIO clock and raw callback are interposed here;
 * card bytes, stereo copies, generation gates, queues and ring proof all run
 * through the actual worker. */
#define KUI_TOY_PILOT_WORKER_TEST 1
#define kui_toy_pilot_worker_test_read ad_test_read
#ifndef TOY_AD_WORKER_SOURCE
#define TOY_AD_WORKER_SOURCE "../src/loader/toy_pilot_worker.c"
#endif
#include TOY_AD_WORKER_SOURCE
#undef kui_toy_pilot_worker_test_read
#define TOY_WORKER_SOURCE "../include/kui/toy_pilot.h"
#include "toy_pilot_async_fixture.h"

#ifndef EXPECT_QUANTA
#error The runner must supply an independently expected quantum allowance
#endif
#ifndef EXPECT_SLOW_READS
#error The runner must supply independently expected 5000-tick read admission
#endif

uint8_t __toy_pilot_gd_stack_bottom[64],__toy_pilot_gd_stack_top[64];
static bool stopped_clock;
static uint32_t stopped_tick,control_after_raw;
static unsigned checks;

uint32_t ad_test_read(uint32_t address,unsigned width) {
    if(stopped_clock && address==TIMER_TCNT) { ++timer_reads;return ~stopped_tick; }
    return kui_toy_pilot_worker_test_read(address,width);
}
static int controlled_raw(uint32_t lba,uint32_t count,void *out) {
    int result=raw(lba,count,out);
    uint32_t action=control_after_raw;control_after_raw=0u;
    if(action==1u) ++owner.mailbox.generation;
    if(action==2u) assert(kui_toy_pilot_request(CMD_PAUSE,0u,0u,0u));
    if(action==3u) kui_toy_pilot_worker_revoke();
    return result;
}
static void clean(void) {
    assert(!owner.stats.fault && !reports && !owner.stats.recovery_last_reason);
    assert(!owner.stats.raw_errors && !owner.stats.queue_errors &&
           !owner.stats.stale_actions && !owner.stats.active_bank_writes);
    for(unsigned i=0u;i<8u;i++) assert(!owner.stats.recovery_counts[i]);
    ++checks;
}
static void serial_setup(void) {
    serial_cursor=true;serial_period=7813u;serial_gap=8u;serial_right_delay=1000u;
}
static void fresh_prefill(void) {
    stopped_clock=false;control_after_raw=0u;prepare(200u);
    owner.config.read_raw=(uint32_t)(uintptr_t)controlled_raw;
    strict_source_reads=true;data_command=16u;
    for(unsigned n=0u;n<30u && (owner.handled_generation!=2u || owner.stop_wait);n++)
        visit(13021u);
    assert(owner.stats.state==KUI_TOY_PILOT_PREFILL && !owner.stop_wait);
    assert(!reads && !copies && !owner.fill_frame && !owner.ring_fill_stream);
    data_command=0u;clean();
}
static void one_visit_admission(void) {
    fresh_prefill();uint32_t saved=sr;visit(0u);clean();
    assert(sr==saved && reads==EXPECT_QUANTA);
    assert(owner.fill_frame==EXPECT_QUANTA*588u && owner.ring_fill_stream==EXPECT_QUANTA*588u);
    assert(owner.stats.raw_calls==reads && owner.stats.raw_bytes==reads*2352u);

    fresh_prefill();raw_ticks=5000u;visit(0u);clean();
    assert(reads==EXPECT_SLOW_READS && owner.fill_frame==EXPECT_SLOW_READS*588u);
    /* The first admitted sector may exceed the allowance, but the worker
     * must return before admitting another slow callback. */
    fresh_prefill();raw_ticks=16000u;visit(0u);clean();
    assert(reads==1u && owner.fill_frame==588u);

    fresh_prefill();stopped_clock=true;stopped_tick=now;raw_ticks=16000u;
    visit(0u);clean();
    assert(reads==EXPECT_QUANTA && owner.fill_frame==EXPECT_QUANTA*588u);
    stopped_clock=false;
}
static void source_sector_split_is_once_only(void) {
    fresh_prefill();uint32_t saved=sr;
    for(unsigned n=0u;n<7u;n++) { fill_quantum();clean();assert(sr==saved); }
    assert(reads==7u && owner.stats.raw_calls==7u && owner.fill_frame==4116u);
    assert(owner.model.banks[0].state==KUI_TOY_PILOT_BANK_READY);
    assert(owner.model.banks[0].filled==4096u && owner.model.banks[1].filled==20u);
    for(uint32_t f=0u;f<4116u;f++) {
        assert((uint16_t)sample(owner.stats.sound_address+f*2u)==(uint16_t)f);
        assert((uint16_t)sample(owner.stats.sound_address+65536u+f*2u)==(uint16_t)~f);
    }
    assert(owner.stats.raw_read_timing_calls==7u);++checks;
}
static void full_ring_finite_samples(void) {
    const uint32_t lengths[]={1u,7u,55u,56u,200u};
    for(unsigned k=0u;k<sizeof(lengths)/sizeof(*lengths);k++) {
        stopped_clock=false;control_after_raw=0u;prepare(lengths[k]);
        strict_source_reads=true;serial_setup();
        for(unsigned n=0u;n<2500u && owner.stats.state!=KUI_TOY_PILOT_EOF;n++) {
            visit(7813u);clean();
        }
        assert(owner.stats.state==KUI_TOY_PILOT_EOF && owner.stats.finite_ends==1u);
        assert(reads==lengths[k] && starts==1u && !running && !stop_queued);
        assert(checked_frames>=source_frames && silent_frames);
        uint32_t old_reads=reads,old_copies=copies,address=owner.stats.sound_address;
        for(unsigned n=0u;n<12u;n++) visit(7813u);
        assert(reads==old_reads && copies==old_copies && owner.stats.sound_address==address);
        assert(owner.stats.sound_bytes==KUI_TOY_PILOT_SOUND_BYTES);clean();
    }
}
static void start_repeat(void) {
    stopped_clock=false;control_after_raw=0u;prepare(200u);
    strict_source_reads=repeat_pcm=true;serial_setup();physical_phase[1]=24u;
    assert(kui_toy_pilot_request(CMD_PLAY,2u,2u,15u)==3u);ready();clean();
}
static void repeating_full_ring_and_pause_fence(void) {
    start_repeat();
    for(unsigned n=0u;n<450u;n++) { visit(7813u);clean(); }
    assert(checked_frames>4u*32768u && owner.ring_consumed>4u*32768u);
    assert(starts==1u && running && !silent_frames);
    assert(owner.ring_played>=owner.ring_consumed && owner.ring_played-owner.ring_consumed<=64u);
    uint32_t old_reads=reads,old_copies=copies,address=owner.stats.sound_address;
    assert(kui_toy_pilot_request(CMD_PAUSE,0u,0u,0u)==4u);visit(0u);
    assert(stop_queued && owner.stop_wait && reads==old_reads && copies==old_copies);
    visit(0u);assert(!running && stopped_live_voice);
    for(unsigned n=0u;n<160u && owner.stats.applied_generation!=4u;n++) {
        visit(7813u);assert(reads==old_reads && copies==old_copies);
    }
    assert(owner.stats.applied_generation==4u && owner.stats.state==KUI_TOY_PILOT_PAUSED);
    assert(now-stop_dispatched>=kui_toy_ring_ticks(32768u));
    assert(owner.stats.sound_address==address && owner.stats.sound_bytes==KUI_TOY_PILOT_SOUND_BYTES);
    clean();
}
static void deferred_fragment_reuses_raw(void) {
    start_repeat();defer_right_copy=true;
    for(unsigned n=0u;n<160u && defer_right_copy;n++) { visit(7813u);clean(); }
    assert(!defer_right_copy && retry_pending);
    assert(owner.model.banks[deferred_bank].filled==deferred_filled);
    assert(owner.fill_frame==deferred_fill_frame && owner.ring_fill_stream==deferred_stream);
    assert(!memcmp(owner.raw,deferred_raw,sizeof(deferred_raw)));
    for(unsigned n=0u;n<12u && retry_pending;n++) { visit(7813u);clean(); }
    assert(!retry_pending && owner.ring_fill_stream>deferred_stream);
    ++checks;
}
static void changed_generation_never_publishes_stale_PCM(void) {
    for(uint32_t action=1u;action<=3u;action++) {
        fresh_prefill();control_after_raw=action;uint32_t saved=sr,address=owner.stats.sound_address;
        visit(0u);
        assert(sr==saved && reads==1u && !copies && !owner.fill_frame && !owner.ring_fill_stream);
        assert(!owner.raw_generation && owner.stats.stale_actions==1u);
        assert(!owner.stats.fault && !reports && owner.stats.sound_address==address);
        assert(owner.stats.sound_bytes==KUI_TOY_PILOT_SOUND_BYTES);
        if(action==3u) {
            assert(owner.disabled && !owner.sdk_ready);
            for(unsigned n=0u;n<12u;n++) visit(7813u);
            assert(reads==1u && !copies);
        }
        ++checks;
    }
    fresh_prefill();revoke_before_mask=true;uint32_t saved=sr;visit(0u);
    assert(sr==saved && diagnostic_revokes==1u && owner.disabled && !reads && !copies);
    assert(!owner.stats.fault && !reports);++checks;
}
int main(void) {
    one_visit_admission();source_sector_split_is_once_only();full_ring_finite_samples();
    repeating_full_ring_and_pause_fence();deferred_fragment_reuses_raw();
    changed_generation_never_publishes_stale_PCM();
    printf("AD audio profile %u: production refill, stereo/ring/lifecycle checks %u passed\n",
        KUI_TOY_PILOT_AD_FOLLOWUP_PROFILE,checks);
    return 0;
}
