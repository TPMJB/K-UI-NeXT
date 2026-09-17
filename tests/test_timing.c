/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/timing.h"
#include <assert.h>
#include <stdio.h>

struct clock {uint64_t now;unsigned calls;};
static uint64_t now_us(void *ctx) {
    struct clock *c=ctx;++c->calls;return c->now;
}
int main(void) {
    struct kui_timing t;
    struct clock c={UINT32_MAX-10,0};
    kui_timing_start(&t,now_us,&c);
    c.now+=5;kui_timing_phase(&t,KUI_TIME_CAPTURE);
    uint64_t start=kui_timing_begin(&t);
    c.now+=100;kui_timing_end(&t,KUI_TIME_DISC,start,2352);
    start=kui_timing_begin(&t);
    c.now+=20;kui_timing_end(&t,KUI_TIME_SHA256,start,2352);
    start=kui_timing_begin(&t);
    c.now+=10;kui_timing_end(&t,KUI_TIME_DISC,start,0); /* failed attempt */
    c.now+=30;kui_timing_phase(&t,KUI_TIME_VERIFY);
    assert(t.elapsed[KUI_TIME_SETUP]==5 && t.elapsed[KUI_TIME_CAPTURE]==160);
    assert(t.samples[KUI_TIME_CAPTURE][KUI_TIME_DISC].us==110);
    assert(t.samples[KUI_TIME_CAPTURE][KUI_TIME_DISC].calls==2);
    assert(t.samples[KUI_TIME_CAPTURE][KUI_TIME_DISC].bytes==2352);
    start=kui_timing_begin(&t);
    c.now+=40;kui_timing_end(&t,KUI_TIME_SHA256,start,4704);
    c.now+=2;kui_timing_finish(&t);
    assert(t.elapsed[KUI_TIME_VERIFY]==42);
    assert(t.samples[KUI_TIME_CAPTURE][KUI_TIME_SHA256].us==20);
    assert(t.samples[KUI_TIME_CAPTURE][KUI_TIME_SHA256].bytes==2352);
    assert(t.samples[KUI_TIME_VERIFY][KUI_TIME_SHA256].us==40);
    assert(t.samples[KUI_TIME_VERIFY][KUI_TIME_SHA256].bytes==4704);
    unsigned calls=c.calls;
    c.now+=100;kui_timing_finish(&t);
    assert(c.calls==calls && t.elapsed[KUI_TIME_VERIFY]==42);

    /* Restart clears prior capture/verify accounting; long runs retain 64 bits. */
    kui_timing_start(&t,now_us,&c);
    assert(!t.elapsed[KUI_TIME_CAPTURE] && !t.samples[KUI_TIME_VERIFY][KUI_TIME_SHA256].bytes);
    kui_timing_phase(&t,KUI_TIME_RESUME);
    start=kui_timing_begin(&t);
    c.now+=UINT64_C(5000000000);
    kui_timing_end(&t,KUI_TIME_READ,start,UINT64_C(5000000000));
    kui_timing_finish(&t);
    assert(t.elapsed[KUI_TIME_RESUME]==UINT64_C(5000000000));
    assert(t.samples[KUI_TIME_RESUME][KUI_TIME_READ].bytes==UINT64_C(5000000000));

    /* Callers without a profiling clock remain supported. */
    calls=c.calls;kui_timing_start(&t,NULL,&c);
    assert(kui_timing_begin(&t)==0);
    kui_timing_phase(&t,KUI_TIME_CAPTURE);
    kui_timing_end(&t,KUI_TIME_SHA256,0,2352);kui_timing_finish(&t);
    assert(!t.active && !t.elapsed[KUI_TIME_CAPTURE]);
    assert(!t.samples[KUI_TIME_CAPTURE][KUI_TIME_SHA256].calls && c.calls==calls);
    puts("PASS timing: phase isolation, retry accounting, restart, 64-bit durations, optional clock");
    return 0;
}
