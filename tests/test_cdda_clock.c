/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/cdda_clock.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>

typedef bool (*conversion)(uint32_t, uint32_t *);
struct reference {
    conversion convert;
    uint32_t numerator, denominator;
    bool ceil;
};
static const struct reference references[] = {
    {kui_cdda_ticks_to_us,1000000u,KUI_CDDA_TMU_HZ,false},
    {kui_cdda_ms_to_ticks,KUI_CDDA_TMU_HZ,1000u,true},
    {kui_cdda_ticks_to_frames,KUI_CDDA_SAMPLE_HZ,KUI_CDDA_TMU_HZ,false},
    {kui_cdda_frames_to_ticks,KUI_CDDA_TMU_HZ,KUI_CDDA_SAMPLE_HZ,false},
    {kui_cdda_frames_to_ticks_ceil,KUI_CDDA_TMU_HZ,KUI_CDDA_SAMPLE_HZ,true}
};
static unsigned checks;

/* The host reference deliberately uses a wide product and the unreduced
 * physical ratios, independently of the production quotient/remainder split. */
static void check_value(const struct reference *ref, uint32_t value) {
    uint64_t product=(uint64_t)value*ref->numerator;
    uint64_t expected=product/ref->denominator;
    if(ref->ceil && product%ref->denominator) ++expected;
    uint32_t result=0xa5a5a5a5u;
    bool valid=ref->convert(value,&result);
    assert(valid==(expected<=UINT32_MAX));
    if(valid) assert(result==(uint32_t)expected);
    else assert(result==0xa5a5a5a5u);
    ++checks;
}

int main(void) {
    assert(KUI_CDDA_TMU_HZ==12468720u && KUI_CDDA_SAMPLE_HZ==44100u);
    for(unsigned r=0;r<sizeof(references)/sizeof(references[0]);++r) {
        assert(!references[r].convert(0,NULL));
        assert(!references[r].convert(UINT32_MAX,NULL));
        /* Small values exercise truncation and ceil around sample/tick edges;
         * the opposite end checks full-width inputs and output overflow. */
        for(uint32_t v=0;v<4096u;++v) {
            check_value(&references[r],v);
            check_value(&references[r],UINT32_MAX-v);
        }
        uint32_t random=0x7f4a7c15u;
        for(unsigned n=0;n<20000u;++n) {
            random=random*1664525u+1013904223u;
            check_value(&references[r],random);
        }
    }
    uint32_t result;
    assert(kui_cdda_ticks_to_us(KUI_CDDA_TMU_HZ,&result) && result==1000000u);
    assert(kui_cdda_ms_to_ticks(1000,&result) && result==KUI_CDDA_TMU_HZ);
    assert(kui_cdda_ms_to_ticks(8,&result) && result==99750u);
    assert(kui_cdda_ms_to_ticks(10,&result) && result==124688u);
    assert(kui_cdda_ms_to_ticks(190,&result) && result==2369057u);
    assert(kui_cdda_ticks_to_frames(KUI_CDDA_TMU_HZ,&result) && result==44100u);
    assert(kui_cdda_frames_to_ticks(44100,&result) && result==KUI_CDDA_TMU_HZ);
    assert(kui_cdda_frames_to_ticks(8192,&result) && result==2316184u);
    assert(kui_cdda_frames_to_ticks_ceil(8192,&result) && result==2316185u);

    /* Exercise the largest accepted input and its overflowing successor. */
    uint32_t max_ms=(uint32_t)((uint64_t)UINT32_MAX*1000u/KUI_CDDA_TMU_HZ);
    check_value(&references[1],max_ms);
    check_value(&references[1],max_ms+1u);
    assert(kui_cdda_ms_to_ticks(max_ms,&result));
    assert(!kui_cdda_ms_to_ticks(max_ms+1u,&result));
    for(unsigned r=3;r<=4;++r) {
        uint64_t numerator=(uint64_t)UINT32_MAX*KUI_CDDA_SAMPLE_HZ;
        if(!references[r].ceil) numerator+=KUI_CDDA_SAMPLE_HZ-1u;
        uint32_t max_frames=(uint32_t)(numerator/KUI_CDDA_TMU_HZ);
        check_value(&references[r],max_frames);
        check_value(&references[r],max_frames+1u);
        assert(references[r].convert(max_frames,&result));
        assert(!references[r].convert(max_frames+1u,&result));
    }

    /* Unsigned tick differences preserve durations across one counter wrap. */
    uint32_t before=UINT32_MAX-100u, after=20u, duration=after-before;
    assert(duration==121u);
    assert(kui_cdda_ticks_to_us(duration,&result) && result==9u);
    uint32_t aliased=KUI_CDDA_TMU_HZ;
    assert(kui_cdda_ticks_to_us(aliased,&aliased) && aliased==1000000u);
    printf("CDDA clock: %u comparisons with 64-bit reference passed\n",checks);
}
