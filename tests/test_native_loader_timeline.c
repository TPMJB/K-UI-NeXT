/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/native_loader_timeline.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;
#define CHECK(c) do {++checks;assert(c);} while(0)
static struct kui_native_loader_timeline trace;
static struct kui_native_loader_event event;

static void sample(uint32_t kind,uint32_t tick,uint32_t fn,uint32_t token,uint32_t flags) {
    memset(&event,0,sizeof(event));
    event.flags=kind|KUI_NATIVE_TIMELINE_CLOCK_VALID|flags;
    event.tick=tick;event.function=fn;event.token=token;
    event.lba=45000u+token;event.count=73u;event.progress=token*512u;
    event.caller_sr=0x700000f1u;event.vbr=0x8c00f400u;event.iprb=0xa50fu;
    event.chcr=0x4911u;event.tcr=513u;event.dmaor=0x8201u;event.ssr=0x84u;
    event.state=0xb03u;
    if(fn==0u && kind==KUI_NATIVE_TIMELINE_EXIT)event.result=token;
    kui_native_loader_timeline_add(&trace,&event);
}
static void call(uint32_t before,uint32_t after,uint32_t fn,uint32_t token,uint32_t flags) {
    if(fn==0u)sample(KUI_NATIVE_TIMELINE_ENTER,before,fn,
        trace.have_request?trace.active.token:trace.last_token,
        trace.have_request?KUI_NATIVE_TIMELINE_READ|KUI_NATIVE_TIMELINE_PENDING:0u);
    else sample(KUI_NATIVE_TIMELINE_ENTER,before,fn,token,flags&~KUI_NATIVE_TIMELINE_COMPLETE);
    sample(KUI_NATIVE_TIMELINE_EXIT,after,fn,token,flags);
}
static uint32_t loader_ticks(void) {
    uint32_t total=0u;
    for(unsigned i=0;i<trace.bin_count;++i)total+=trace.bins[i].gd_ticks;
    return total;
}
static void test_calls_and_request_chain(void) {
    kui_native_loader_timeline_reset(&trace);
    /* An accepted REQUEST, a later EXEC and CHECK: real observed request
     * time1000ticks, of which400were in the three loader calls. */
    call(100000u,99900u,0u,1u,KUI_NATIVE_TIMELINE_READ|KUI_NATIVE_TIMELINE_PENDING);
    call(99500u,99300u,2u,1u,KUI_NATIVE_TIMELINE_READ|KUI_NATIVE_TIMELINE_PENDING);
    call(99100u,99000u,1u,1u,KUI_NATIVE_TIMELINE_READ|KUI_NATIVE_TIMELINE_COMPLETE);
    CHECK(trace.request_count==1u && !trace.have_request);
    const struct kui_native_loader_request *r=&trace.requests[0];
    CHECK(r->begin_tick==100000u && r->end_tick==99000u && r->total_ticks==1000u);
    CHECK(r->inside_ticks==400u && r->outside_ticks==600u && r->calls==3u);
    CHECK(r->lba==45001u && r->count==73u && !r->flags);
    CHECK(trace.max_call_ticks==200u && trace.max_gap_ticks==400u && loader_ticks()==400u);
    CHECK(trace.longest_count==3u && trace.longest[0].elapsed==200u);
    CHECK(trace.longest[0].begin.caller_sr==0x700000f1u);
    CHECK(trace.longest[0].end.vbr==0x8c00f400u && trace.longest[0].end.chcr==0x4911u);
    CHECK(trace.longest[0].end.dmaor==0x8201u && trace.longest[0].end.state==0xb03u);
    /* A distinct read begins after a small outside gap and ends200ticks
     * later. Its chain includes the50tick proximity gap, not just IO time. */
    call(98950u,98900u,0u,2u,KUI_NATIVE_TIMELINE_READ|KUI_NATIVE_TIMELINE_PENDING);
    call(98800u,98750u,1u,2u,KUI_NATIVE_TIMELINE_READ|KUI_NATIVE_TIMELINE_COMPLETE);
    r=&trace.requests[1];
    CHECK(r->chain_length==2u && r->chain_ticks==1250u && r->chain_inside==500u);
    CHECK(trace.max_chain_length==2u && trace.max_chain_ticks==1250u && trace.max_chain_inside==500u);
    CHECK(trace.bins[0].calls==5u && trace.bins[0].read_starts==2u && trace.bins[0].read_completes==2u);
    sample(KUI_NATIVE_TIMELINE_MAPLE,98700u,0u,0u,0u);
    CHECK(trace.bins[0].maples==1u && trace.valid==11u && !trace.invalid);
    /* A long gap starts a different proximity chain. */
    call(50000u,49900u,0u,3u,KUI_NATIVE_TIMELINE_READ|KUI_NATIVE_TIMELINE_COMPLETE);
    CHECK(trace.requests[2].chain_length==1u && trace.requests[2].total_ticks==100u);
}
static void test_bin_splitting_and_wrap(void) {
    kui_native_loader_timeline_reset(&trace);
    uint32_t origin=100u;
    sample(KUI_NATIVE_TIMELINE_MAPLE,origin,0u,0u,0u);
    uint32_t before=origin-KUI_NATIVE_TIMELINE_BIN_TICKS+10u;
    call(before,before-30u,2u,0u,0u);
    CHECK(trace.bin_count==2u && trace.bins[0].gd_ticks==10u && trace.bins[1].gd_ticks==20u);
    CHECK(trace.bins[0].maples==1u && trace.bins[1].calls==1u);
    CHECK(trace.epoch==1u && !trace.clock_breaks && trace.max_call_ticks==30u);
    CHECK(trace.bins[1].tick==origin-KUI_NATIVE_TIMELINE_BIN_TICKS);
    /* The raw downcounter wraps from5 through0 toUINT32_MAX. */
    kui_native_loader_timeline_reset(&trace);
    call(5u,UINT32_MAX-4u,2u,0u,0u);
    CHECK(trace.max_call_ticks==10u && loader_ticks()==10u && trace.epoch==1u);
}
static void test_epochs_and_invalid_clocks(void) {
    kui_native_loader_timeline_reset(&trace);
    call(1000u,900u,0u,1u,KUI_NATIVE_TIMELINE_READ|KUI_NATIVE_TIMELINE_PENDING);
    sample(KUI_NATIVE_TIMELINE_ENTER,850u,2u,1u,KUI_NATIVE_TIMELINE_READ|KUI_NATIVE_TIMELINE_PENDING);
    event.flags=KUI_NATIVE_TIMELINE_EXIT;event.tick=800u;
    kui_native_loader_timeline_add(&trace,&event);
    CHECK(trace.invalid==1u && trace.clock_breaks==1u && !trace.have_request);
    CHECK(trace.request_count==1u && (trace.requests[0].flags&KUI_NATIVE_TIMELINE_BAD_CLOCK));
    CHECK(!trace.requests[0].total_ticks && trace.requests[0].inside_ticks==100u);
    CHECK(trace.unmatched==1u && loader_ticks()==100u);
    sample(KUI_NATIVE_TIMELINE_EXIT,700u,2u,1u,0u);
    CHECK(trace.epoch==2u && trace.unmatched==2u && loader_ticks()==100u);
    call(600u,500u,0u,2u,KUI_NATIVE_TIMELINE_READ|KUI_NATIVE_TIMELINE_COMPLETE);
    CHECK(trace.requests[1].epoch==2u && trace.requests[1].chain_length==1u);
    /* A counter that jumps backwards/up without wrapping within half the
     * range starts another epoch and cannot form a huge call duration. */
    sample(KUI_NATIVE_TIMELINE_ENTER,400u,2u,0u,0u);
    sample(KUI_NATIVE_TIMELINE_EXIT,450u,2u,0u,0u);
    CHECK(trace.epoch==3u && trace.clock_breaks==2u && trace.max_call_ticks==100u);
    /* The wrapper may explicitly report profile changes even when the new
     * current sample is itself an admitted profile. */
    event.flags=KUI_NATIVE_TIMELINE_MAPLE|KUI_NATIVE_TIMELINE_CLOCK_VALID|KUI_NATIVE_TIMELINE_CLOCK_BREAK;
    event.tick=300u;kui_native_loader_timeline_add(&trace,&event);
    CHECK(trace.epoch==4u && trace.clock_breaks==3u && !trace.have_enter);
}
static void test_partial_replaced_and_failed(void) {
    kui_native_loader_timeline_reset(&trace);
    sample(KUI_NATIVE_TIMELINE_MAPLE,10000u,1u,11u,KUI_NATIVE_TIMELINE_READ|KUI_NATIVE_TIMELINE_PENDING);
    call(9900u,9800u,1u,11u,KUI_NATIVE_TIMELINE_READ|KUI_NATIVE_TIMELINE_COMPLETE);
    CHECK(trace.requests[0].flags==KUI_NATIVE_TIMELINE_PARTIAL);
    CHECK(trace.requests[0].total_ticks==200u && trace.requests[0].inside_ticks==100u);
    /* An already completed handle on subsequent CHECK must not fabricate a
     * second request from the same token. */
    call(9700u,9600u,1u,11u,KUI_NATIVE_TIMELINE_READ|KUI_NATIVE_TIMELINE_COMPLETE);
    CHECK(trace.request_count==1u);
    call(9500u,9400u,0u,12u,KUI_NATIVE_TIMELINE_READ|KUI_NATIVE_TIMELINE_PENDING);
    /* A new accepted read replacing a still-open handle marks the old
     * summary instead of attributing new-token progress to it. */
    call(9300u,9200u,0u,13u,KUI_NATIVE_TIMELINE_READ|KUI_NATIVE_TIMELINE_PENDING);
    CHECK(trace.request_count==2u && (trace.requests[1].flags&KUI_NATIVE_TIMELINE_REPLACED));
    sample(KUI_NATIVE_TIMELINE_ENTER,9100u,1u,13u,KUI_NATIVE_TIMELINE_READ|KUI_NATIVE_TIMELINE_PENDING);
    event.flags=KUI_NATIVE_TIMELINE_EXIT|KUI_NATIVE_TIMELINE_CLOCK_VALID|KUI_NATIVE_TIMELINE_READ|KUI_NATIVE_TIMELINE_FAILED;
    event.tick=9000u;event.result=UINT32_MAX;kui_native_loader_timeline_add(&trace,&event);
    CHECK(trace.request_count==3u && (trace.requests[2].flags&KUI_NATIVE_TIMELINE_REQUEST_FAILED));
    CHECK(trace.requests[2].result==UINT32_MAX && !trace.requests[2].chain_length);
    /* A read that becomes complete before the next call begins is closed
     * at that first observation; the CHECK's body is not added to it. */
    call(8900u,8800u,0u,14u,KUI_NATIVE_TIMELINE_READ|KUI_NATIVE_TIMELINE_PENDING);
    sample(KUI_NATIVE_TIMELINE_ENTER,8700u,1u,14u,KUI_NATIVE_TIMELINE_READ|KUI_NATIVE_TIMELINE_COMPLETE);
    CHECK(trace.request_count==4u && trace.requests[3].inside_ticks==100u);
    CHECK(trace.requests[3].outside_ticks==100u);
}
static void test_limits_and_stable_longest(void) {
    kui_native_loader_timeline_reset(&trace);
    uint32_t tick=100000u;
    for(uint32_t i=1u;i<=24u;++i) {
        call(tick,tick-i,0u,i,KUI_NATIVE_TIMELINE_READ|KUI_NATIVE_TIMELINE_COMPLETE);
        tick-=100u;
    }
    CHECK(trace.request_count==16u && trace.request_next==8u && trace.requests[7].token==24u);
    CHECK(trace.requests[8].token==9u && trace.longest_count==8u);
    for(unsigned i=0;i<8u;++i) {
        CHECK(trace.longest[i].elapsed==24u-i);
        CHECK(trace.longest[i].end.token==24u-i && trace.longest[i].begin.function==0u);
    }
    CHECK(trace.max_chain_length==24u && trace.max_chain_ticks>trace.max_call_ticks);
    /* Reset is complete, including all retained contexts and epoch state. */
    kui_native_loader_timeline_reset(&trace);
    CHECK(!trace.request_count && !trace.events && !trace.longest_count && !trace.bin_count);
    sample(KUI_NATIVE_TIMELINE_MAPLE,100u,0u,0u,0u);
    sample(KUI_NATIVE_TIMELINE_MAPLE,100u-KUI_NATIVE_TIMELINE_BINS*KUI_NATIVE_TIMELINE_BIN_TICKS,0u,0u,0u);
    CHECK(trace.overflow==1u && trace.bin_count==1u); /* no out-of-bounds33rd bin */
    trace.events=UINT32_MAX;sample(KUI_NATIVE_TIMELINE_MAPLE,99u-KUI_NATIVE_TIMELINE_BINS*KUI_NATIVE_TIMELINE_BIN_TICKS,0u,0u,0u);
    CHECK(trace.events==UINT32_MAX && trace.saturated);
    kui_native_loader_timeline_add(NULL,&event);kui_native_loader_timeline_add(&trace,NULL);
    kui_native_loader_timeline_reset(NULL);
}
static void test_lifecycle_and_token_reuse(void) {
    for(unsigned function=3u;function<=9u;function+=6u) {
        kui_native_loader_timeline_reset(&trace);
        call(10000u,9900u,0u,1u,KUI_NATIVE_TIMELINE_READ|KUI_NATIVE_TIMELINE_PENDING);
        sample(KUI_NATIVE_TIMELINE_ENTER,9800u,function,1u,KUI_NATIVE_TIMELINE_READ|KUI_NATIVE_TIMELINE_PENDING);
        sample(KUI_NATIVE_TIMELINE_EXIT,9700u,function,0u,0u);
        CHECK(!trace.have_request && !trace.last_token && trace.request_count==1u);
        CHECK(trace.requests[0].flags&KUI_NATIVE_TIMELINE_REPLACED);
        CHECK(trace.requests[0].total_ticks==300u && trace.requests[0].inside_ticks==200u);
        call(9600u,9500u,0u,1u,KUI_NATIVE_TIMELINE_READ|KUI_NATIVE_TIMELINE_COMPLETE);
        CHECK(trace.request_count==2u && trace.requests[1].token==1u && !trace.requests[1].flags);
        CHECK(trace.requests[1].chain_length==1u && trace.requests[1].total_ticks==100u);
    }
    /* ABORT's failure flag closes the read. An unrelated non-read token
     * replacement cannot leave an old read open across later game work. */
    kui_native_loader_timeline_reset(&trace);
    call(10000u,9900u,0u,1u,KUI_NATIVE_TIMELINE_READ|KUI_NATIVE_TIMELINE_PENDING);
    call(9800u,9700u,8u,1u,KUI_NATIVE_TIMELINE_READ|KUI_NATIVE_TIMELINE_FAILED);
    CHECK(trace.requests[0].flags&KUI_NATIVE_TIMELINE_REQUEST_FAILED);
    call(9600u,9500u,0u,2u,KUI_NATIVE_TIMELINE_READ|KUI_NATIVE_TIMELINE_PENDING);
    sample(KUI_NATIVE_TIMELINE_MAPLE,9400u,0u,0u,0u);
    CHECK(trace.request_count==2u && !trace.have_request && !trace.last_token);
    CHECK(trace.requests[1].flags&KUI_NATIVE_TIMELINE_REPLACED);
}
int main(void) {
    test_calls_and_request_chain();test_bin_splitting_and_wrap();test_epochs_and_invalid_clocks();
    test_partial_replaced_and_failed();test_limits_and_stable_longest();test_lifecycle_and_token_reuse();
    printf("Native loader timeline: %u checks; %zu-byte state, call/idle split, request chains, clocks/wrap/epochs, rings and exact hardware contexts passed\n",checks,sizeof(trace));
    return 0;
}
