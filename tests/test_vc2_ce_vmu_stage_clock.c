/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vc2_ce_vmu_stage_clock.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint32_t start,control,constant,counter,sr,writes,reads;
uint32_t kui_vc2_stage_clock_sr(void) {return sr;}
uint32_t kui_vc2_stage_clock_read(uint32_t address,unsigned width) {
    ++reads;
    switch(address) {
    case 0xffd80004u:assert(width==1u);return start;
    case 0xffd80020u:assert(width==4u);return constant;
    case 0xffd80024u:assert(width==4u);return counter;
    default:assert(address==0xffd80028u && width==2u);return control;
    }
}
void kui_vc2_stage_clock_write(uint32_t address,uint32_t value,unsigned width) {
    ++writes;
    switch(address) {
    case 0xffd80004u:assert(width==1u);start=value;break;
    case 0xffd80020u:assert(width==4u);constant=value;break;
    case 0xffd80024u:assert(width==4u);counter=value;break;
    default:assert(address==0xffd80028u && width==2u);control=value;break;
    }
}
static void fresh(struct kui_vc2_ce_vmu_stage_clock *clock) {
    memset(clock,0,sizeof(*clock));start=3u;control=0x20u;constant=123u;counter=987u;
    sr=0x500000f0u;writes=reads=0u;
}
int main(void) {
    struct kui_vc2_ce_vmu_stage_clock clock;
    fresh(&clock);assert(kui_vc2_ce_vmu_stage_clock_start(&clock));
    assert(start==7u && control==2u && constant==UINT32_MAX && counter==UINT32_MAX);
    counter-=779295u;assert(kui_vc2_ce_vmu_stage_clock_ticks(&clock)==779295u);
    assert(kui_vc2_ce_vmu_stage_clock_ms(779294u)==999u);
    assert(kui_vc2_ce_vmu_stage_clock_ms(779295u)==1000u);
    assert(kui_vc2_ce_vmu_stage_clock_stop(&clock));
    assert(!clock.active && start==3u && control==0x20u && constant==123u && counter==987u);
    for(unsigned bit=0u;bit<16u;bit++)if((1u<<bit)&0x3c0u) {
        fresh(&clock);control|=1u<<bit;unsigned before=control;
        assert(!kui_vc2_ce_vmu_stage_clock_start(&clock));
        assert(!writes && control==before && start==3u);
    }
    fresh(&clock);start|=4u;assert(!kui_vc2_ce_vmu_stage_clock_start(&clock) && !writes);
    const uint32_t refused[]={0u,0x40000000u,0x400000e0u,0x100000f0u};
    for(unsigned i=0;i<sizeof(refused)/sizeof(refused[0]);i++) {
        fresh(&clock);sr=refused[i];assert(!kui_vc2_ce_vmu_stage_clock_start(&clock));
        assert(!reads && !writes);
    }
    fresh(&clock);assert(kui_vc2_ce_vmu_stage_clock_start(&clock));
    control|=0x100u;assert(!kui_vc2_ce_vmu_stage_clock_ticks(&clock) && !clock.valid);
    assert(kui_vc2_ce_vmu_stage_clock_stop(&clock));
    assert(start==3u && control==0x20u && constant==123u && counter==987u);
    puts("PASS cold-stage clock: idle/flag-clear admission, no IRQ ownership, exact restore and real tick conversion");
    return 0;
}
