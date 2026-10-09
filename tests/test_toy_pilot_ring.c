/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/toy_pilot_ring.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void independent_clock_ratios(void) {
    /* Timer and PCM clocks, independently computed in a wider domain. Early
     * deadlines and late playback estimates must each round conservatively. */
    for(uint32_t frames=0;frames<=32768u;frames++) {
        uint32_t expected=(uint32_t)((uint64_t)frames*781250u/44100u);
        assert(kui_toy_ring_ticks(frames)==expected);
    }
    for(uint32_t ticks=0;ticks<=290249u;ticks++) {
        uint32_t expected=(uint32_t)(((uint64_t)ticks*44100u+781249u)/781250u);
        assert(kui_toy_ring_frames(ticks)==expected);
    }
}
static void both_channels_need_two_distinct_publications(void) {
    struct kui_toy_ring_probe p;
    kui_toy_ring_probe_begin(&p,100u,100u,1000u);
    for(unsigned i=0;i<8u;i++) assert(!kui_toy_ring_probe_observe(&p,100u,100u,1000u+i,1000u+i));
    assert(!kui_toy_ring_probe_observe(&p,101u,100u,1010u,1010u));
    assert(!kui_toy_ring_probe_observe(&p,102u,100u,1020u,1020u));
    assert(!kui_toy_ring_probe_observe(&p,102u,101u,1030u,1030u));
    assert(kui_toy_ring_probe_observe(&p,102u,102u,1040u,1040u));
    /* A start/reset initializes the tables to zero; one old changed pair
     * after that is insufficient to establish a fresh ownership sample. */
    kui_toy_ring_probe_begin(&p,0u,0u,2000u);
    assert(!kui_toy_ring_probe_observe(&p,32000u,32000u,2010u,2010u));
    assert(!kui_toy_ring_probe_observe(&p,32000u,32000u,2020u,2020u));
}
static void coherent_stereo_half_and_age(void) {
    struct kui_toy_ring_probe p;
    kui_toy_ring_probe_begin(&p,100u,164u,1000u);
    assert(!kui_toy_ring_probe_observe(&p,101u,165u,1100u,1100u));
    assert(kui_toy_ring_probe_observe(&p,102u,166u,1200u,1200u));
    kui_toy_ring_probe_begin(&p,100u,165u,1000u);
    assert(!kui_toy_ring_probe_observe(&p,101u,166u,1100u,1100u));
    assert(!kui_toy_ring_probe_observe(&p,102u,167u,1200u,1200u));
    /* Adjacent numerical samples on opposite halves cannot authorize either
     * half, including the physical wrap where a subtraction appears huge. */
    kui_toy_ring_probe_begin(&p,16381u,16384u,1000u);
    assert(!kui_toy_ring_probe_observe(&p,16382u,16385u,1100u,1100u));
    assert(!kui_toy_ring_probe_observe(&p,16383u,16386u,1200u,1200u));
    kui_toy_ring_probe_begin(&p,32765u,0u,1000u);
    assert(!kui_toy_ring_probe_observe(&p,32766u,1u,1100u,1100u));
    assert(!kui_toy_ring_probe_observe(&p,32767u,2u,1200u,1200u));
    for(unsigned too_old=0;too_old<2u;too_old++) {
        uint32_t began=UINT32_MAX-100u;
        kui_toy_ring_probe_begin(&p,100u,100u,began);
        assert(!kui_toy_ring_probe_observe(&p,101u,101u,began+10u,began+10u));
        bool good=kui_toy_ring_probe_observe(&p,102u,102u,began+39063u+too_old,began+39063u+too_old);
        assert(good==!too_old);
    }
    kui_toy_ring_probe_begin(&p,100u,100u,1000u);
    assert(!kui_toy_ring_probe_observe(&p,101u,101u,999u,999u));
    struct kui_toy_ring_probe before=p;
    assert(!kui_toy_ring_probe_observe(&p,32768u,100u,1010u,1010u));
    assert(!memcmp(&before,&p,sizeof(p)));
    assert(!kui_toy_ring_probe_observe(&p,100u,UINT32_MAX,1010u,1010u));
    assert(!memcmp(&before,&p,sizeof(p)));
}
static void delayed_capture_uses_two_back_observation(void) {
    /* Independent ARM schedule: L1 captures at1700, publishes at1900;
     * L2 captures at1950, BEFORE SH sees L1 at2000, publishes at2300.
     * R1 captures at2200/publishes at2400; R2 captures at2450 BEFORE
     * SH sees R1 at2500, publishes at2800. The2500 host time cannot
     * safely anchor R2. The last observation of R0 at2000 can. */
    for(unsigned wrap=0;wrap<2u;wrap++) {
        uint32_t origin=wrap?UINT32_MAX-3000u:0u;
        struct kui_toy_ring_probe p;
        kui_toy_ring_probe_begin(&p,0u,0u,origin+1000u);
        assert(!kui_toy_ring_probe_observe(&p,0u,0u,origin+1500u,origin+1520u));
        assert(!kui_toy_ring_probe_observe(&p,100u,0u,origin+2000u,origin+2020u));
        assert(!kui_toy_ring_probe_observe(&p,120u,110u,origin+2500u,origin+2520u));
        assert(p.capture_bound[0]==origin+1500u);
        assert(kui_toy_ring_probe_observe(&p,160u,150u,origin+3000u,origin+3020u));
        uint32_t age=kui_toy_ring_probe_age(&p,origin+3020u);
        assert(age==1020u); /* Both safe lower bounds are2000. */
        assert(age>=3020u-2450u && age>=3020u-2700u);
        assert(age<3020u-1000u); /* Fresher than the original probe anchor. */
        assert(kui_toy_ring_probe_observe(&p,190u,150u,origin+3500u,origin+3520u));
        assert(p.capture_bound[0]==origin+2500u && p.capture_bound[1]==origin+2000u);
        assert(kui_toy_ring_probe_age(&p,origin+3520u)==1520u); /* Older R bound. */
        struct kui_toy_ring_probe unchanged=p;
        assert(!kui_toy_ring_probe_observe(&p,200u,190u,origin+3600u,origin+3590u));
        assert(!memcmp(&p,&unchanged,sizeof(p)));
        assert(!kui_toy_ring_probe_observe(&p,200u,190u,origin+41064u,origin+41064u));
        assert(!memcmp(&p,&unchanged,sizeof(p))); /* Global50ms expiry remains. */
    }
}
static void logical_block_straddle_retains_the_same_probe_contract(void) {
    struct kui_toy_ring_probe p;
    /* Logical ownership may straddle a4096-frame boundary. Acceptance is
     * still the original <=64-frame/same16384-half/two-publication proof;
     * the worker must use the lower capture for retirement. */
    kui_toy_ring_probe_begin(&p,4092u,4072u,1000u);
    assert(!kui_toy_ring_probe_observe(&p,4094u,4074u,1100u,1100u));
    assert(kui_toy_ring_probe_observe(&p,4097u,4077u,1200u,1200u));
    assert(p.previous[0]/KUI_TOY_RING_BLOCK!=p.previous[1]/KUI_TOY_RING_BLOCK);
    assert(kui_toy_ring_probe_age(&p,1200u)==200u);
}
int main(void) {
    independent_clock_ratios();both_channels_need_two_distinct_publications();
    coherent_stereo_half_and_age();delayed_capture_uses_two_back_observation();
    logical_block_straddle_retains_the_same_probe_contract();
    puts("Toy ring scalar evidence: conservative clocks, independently timed stereo publications, delayed capture lower bounds, phase/half boundaries, age and invalid positions pass");
    return 0;
}
