/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/native_reader_capture.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    struct kui_reader_capture c;
    kui_reader_capture_reset(&c);
    struct kui_reader_capture_sample s={.tick=100u,.valid=1u,.irq_blocks=UINT32_MAX-2u};
    kui_reader_capture_add(&c,&s);
    assert(c.samples==1u && !c.top_count && !c.gap_max);
    s.tick-=500000u;s.irq_blocks=3u;s.call_blocks=20u;s.call_max=200u;s.irq_max=30u;
    s.token=7u;s.pending=1u;s.crc=1u;
    kui_reader_capture_add(&c,&s);
    assert(c.gap_max==500000u && c.top[0].irq_blocks==6u && c.top[0].call_blocks==20u);
    assert(c.top[0].crc==1u && c.top[0].previous_token==0u && c.top[0].token==7u);
    for(unsigned i=1u;i<=8u;++i) {
        s.tick-=i*100u;s.call_max=i;s.irq_max=2u*i;++s.token;
        kui_reader_capture_add(&c,&s);
    }
    assert(c.top_count==4u && c.top[0].gap==500000u && c.top[1].gap==800u);
    assert(c.top[3].gap==600u && c.call_max==200u && c.irq_max==30u);
    s.valid=0u;s.tick-=1000000u;kui_reader_capture_add(&c,&s);
    s.valid=1u;s.tick-=1000000u;kui_reader_capture_add(&c,&s);
    assert(c.bad==2u && c.top[0].gap==500000u);
    s.tick+=1u;kui_reader_capture_add(&c,&s);
    assert(c.bad==3u && c.top[0].gap==500000u);
    s.tick-=100u;s.call_max=UINT32_C(0x80000000);kui_reader_capture_add(&c,&s);
    assert(c.bad==4u && c.call_max==200u && c.top[0].gap==500000u);
    kui_reader_capture_reset(&c);assert(!c.samples && !c.have_previous && !c.top_count);
    puts("reader capture: wrap, deltas, top retention, invalid-clock and reset passed");
}
