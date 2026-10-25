/* SPDX-License-Identifier: GPL-3.0-only */
/* Gesture-window observations only. This file follows reader_capture.inc so
 * its typed retained-region view is available. No game clock, DMA register,
 * controller packet, reply, vector or storage state is modified here. */
#include "kui/native_loader_timeline.h"
#include <stddef.h>
__asm__(".global __kui_native_loader_timeline_worker\n"
        ".set __kui_native_loader_timeline_worker,1\n");
#if KUI_NATIVE_SCI_PRIORITY7
__asm__(".global __kui_native_sci_priority7_worker\n"
        ".set __kui_native_sci_priority7_worker,7\n");
#endif
#if KUI_NATIVE_SDK_SCI_ROUTE
__asm__(".global __kui_native_sdk_sci_route_worker\n"
        ".set __kui_native_sdk_sci_route_worker,1\n");
#endif
static struct kui_native_loader_timeline native_loader_timeline;
static struct kui_native_loader_event native_loader_event;
static struct kui_vmu_native_profile_sample native_loader_clock;

#ifdef KUI_VMU_NATIVE_PASSIVE_HOST_TEST
extern uint32_t kui_native_loader_timeline_test_vbr(void);
extern uint32_t kui_native_loader_timeline_test_read(uint32_t,unsigned);
extern void kui_vmu_native_persistent_test_clock(struct kui_vmu_native_profile_sample *,uint32_t *);
#define native_loader_vbr kui_native_loader_timeline_test_vbr
#define native_loader_read kui_native_loader_timeline_test_read
#else
static uint32_t native_loader_vbr(void) {
    uint32_t value;__asm__ volatile("stc vbr,%0":"=r"(value));return value;
}
static uint32_t native_loader_read(uint32_t address,unsigned width) {
    if(width==1u)return *(volatile const uint8_t *)(uintptr_t)address;
    if(width==2u)return *(volatile const uint16_t *)(uintptr_t)address;
    return *(volatile const uint32_t *)(uintptr_t)address;
}
#endif

static void native_loader_clock_read(void) {
#ifdef KUI_VMU_NATIVE_PASSIVE_HOST_TEST
    uint32_t unused;
    kui_vmu_native_persistent_test_clock(&native_loader_clock,&unused);
#else
    native_loader_clock.frqcr=native_loader_read(UINT32_C(0xffc00000),2u);
    native_loader_clock.tcor=native_loader_read(UINT32_C(0xffd80008),4u);
    native_loader_clock.tcr=native_loader_read(UINT32_C(0xffd80010),2u);
    native_loader_clock.tstr=native_loader_read(UINT32_C(0xffd80004),1u);
    native_loader_clock.counter=native_loader_read(UINT32_C(0xffd8000c),4u);
#endif
}

static void native_loader_snapshot(const struct kui_vmu_native_profile_sample *clock,
    uint32_t kind,uint32_t function,uint32_t original_sr,
    const kui_retail_async_region_type *r) {
    const struct kui_retail_gd *g=&r->shared.service;
    native_loader_event.tick=clock->counter;
    native_loader_event.flags=kind|(kui_vmu_native_profile_sample_valid(clock)?
        KUI_NATIVE_TIMELINE_CLOCK_VALID:KUI_NATIVE_TIMELINE_CLOCK_BREAK);
    if(g->command==KUI_GD_PIOREAD || g->command==KUI_GD_DMAREAD)
        native_loader_event.flags|=KUI_NATIVE_TIMELINE_READ;
    if(g->pending)native_loader_event.flags|=KUI_NATIVE_TIMELINE_PENDING;
    if(!g->pending && g->status==KUI_GD_COMPLETED)
        native_loader_event.flags|=KUI_NATIVE_TIMELINE_COMPLETE;
    if(g->error || g->status==KUI_GD_FAILED)
        native_loader_event.flags|=KUI_NATIVE_TIMELINE_FAILED;
    native_loader_event.function=function;
    native_loader_event.result=kind==KUI_NATIVE_TIMELINE_EXIT?
        (uint32_t)g->diag.last_result:(uint32_t)g->status;
    native_loader_event.token=g->token;
    native_loader_event.lba=g->lba;
    native_loader_event.count=g->count;
    native_loader_event.progress=g->completed_bytes;
    native_loader_event.caller_sr=original_sr;
    native_loader_event.vbr=native_loader_vbr();
    native_loader_event.iprb=native_loader_read(UINT32_C(0xffd00008),2u);
    native_loader_event.chcr=native_loader_read(UINT32_C(0xffa0001c),4u);
    native_loader_event.tcr=native_loader_read(UINT32_C(0xffa00018),4u);
    native_loader_event.dmaor=native_loader_read(UINT32_C(0xffa00040),4u);
    native_loader_event.ssr=native_loader_read(UINT32_C(0xffe00010),1u);
    native_loader_event.state=(r->shared.stream.state&255u)|
        ((r->engine.active!=0u)<<8)|((r->engine.hooked!=0u)<<9)|
        ((r->engine.release.armed!=0u)<<10)|
        ((native_loader_event.vbr==(uint32_t)(uintptr_t)r-UINT32_C(0x100))<<11)|
        ((g->command&255u)<<16);
    kui_native_loader_timeline_add(&native_loader_timeline,&native_loader_event);
}

/* The low GD entry already owns the serialization lock and IMASK15. Keep
 * staging in private worker BSS: a 64-byte automatic event must never consume
 * the guarded 352-byte resident stack. Profile descriptor installation is
 * independently admitted by the stage; the retained-region identity is
 * checked again before observing any of its fields. */
void __attribute__((noipa)) kui_native_loader_timeline_visit(
    uint32_t exit,uint32_t function,uint32_t original_sr,void *region) {
    if(!diagnostic.active || config.diagnostics!=KUI_VMU_NATIVE_DIAGNOSTICS_PROFILE ||
       !region || region!=reader_capture_region())return;
    native_loader_clock_read();
    native_loader_snapshot(&native_loader_clock,exit?KUI_NATIVE_TIMELINE_EXIT:
        KUI_NATIVE_TIMELINE_ENTER,function,original_sr,region);
}

static void native_loader_timeline_start(void) {
    kui_native_loader_timeline_reset(&native_loader_timeline);
}
static void native_loader_timeline_heartbeat(const struct kui_vmu_native_profile_sample *clock) {
    if(!diagnostic.active || config.diagnostics!=KUI_VMU_NATIVE_DIAGNOSTICS_PROFILE)return;
    const kui_retail_async_region_type *r=reader_capture_region();
    if(r)native_loader_snapshot(clock,KUI_NATIVE_TIMELINE_MAPLE,UINT32_MAX,state.complete_sr,r);
}

#ifndef KUI_VMU_NATIVE_PASSIVE_HOST_TEST
/* Reports run only after the intentional stop, on the worker's private
 * stack. Every page uses at most sixteen body rows. All numbers are hex. */
static void native_loader_timeline_row(unsigned prefix,unsigned row,
    const uint32_t *values,unsigned count) {
    char line[49];unsigned n=0u;
    if(prefix!=UINT32_MAX) {
        line[n++]=(char)('0'+prefix);line[n++]=(char)('0'+row);line[n++]=' ';
    }
    for(unsigned i=0u;i<count;i++) {
        for(unsigned digit=0u;digit<8u;digit++)
            line[n++]="0123456789ABCDEF"[(values[i]>>(28u-4u*digit))&15u];
        line[n++]=' ';
    }
    line[n]=0;retail_display_line(line);
}
static void native_loader_timeline_call(const struct kui_native_loader_call *call,unsigned rank) {
    uint32_t values[5];
    for(unsigned row=0u;row<7u;row++) {
        for(unsigned col=0u;col<5u;col++) {
            unsigned word=row*5u+col;
            if(word<3u)memcpy(&values[col],(const uint8_t *)call+
                offsetof(struct kui_native_loader_call,elapsed)+word*4u,4u);
            else if(word<19u)memcpy(&values[col],(const uint8_t *)&call->begin+(word-3u)*4u,4u);
            else memcpy(&values[col],(const uint8_t *)&call->end+(word-19u)*4u,4u);
        }
        native_loader_timeline_row(rank,row,values,5u);
    }
}
static void native_loader_timeline_report(const struct retail_display_state *display) {
    const struct kui_native_loader_timeline *t=&native_loader_timeline;
    retail_display_restore(display);
    retail_display_line("GD TIMELINE / READ ONLY");
    retail_display_line("HEX TICKS / 781250HZ");
    retail_display_values("EVENTS VALID BAD EPOCH",(uint32_t[4]){
        t->events,t->valid,t->invalid,t->epoch},4u);
    retail_display_values("CLKBREAK UNMATCHED OVER SAT",(uint32_t[4]){
        t->clock_breaks,t->unmatched,t->overflow,t->saturated},4u);
    retail_display_values("MAX CALL GAP CHAINLEN",(uint32_t[3]){
        t->max_call_ticks,t->max_gap_ticks,t->max_chain_length},3u);
    retail_display_values("MAX CHAIN TOTAL/INSIDE TICKS",(uint32_t[2]){
        t->max_chain_ticks,t->max_chain_inside},2u);
    retail_display_values("ACTIVE TOK LBA N FLAGS",(uint32_t[4]){
        t->have_request?t->active.token:0u,t->active.lba,t->active.count,t->active.flags},4u);
    retail_display_values("STORED CALL REQ BINS",(uint32_t[3]){
        t->longest_count,t->request_count,t->bin_count},3u);
    retail_display_pause(240u);
    for(unsigned first=0u;first<t->bin_count;first+=14u) {
        retail_display_restore(display);
        retail_display_line("GD BINS / 0.625S");
        retail_display_line("I EPOCH GD TICKS CALLS MAPLE");
        for(unsigned i=first;i<first+14u && i<t->bin_count;i++) {
            const struct kui_native_loader_bin *b=&t->bins[i];
            uint32_t values[5]={i,b->epoch,b->gd_ticks,b->calls,b->maples};
            native_loader_timeline_row(UINT32_MAX,0u,values,5u);
        }
        retail_display_pause(240u);
    }
    for(unsigned first=0u;first<t->longest_count && first<4u;first+=2u) {
        retail_display_restore(display);
        retail_display_line("LONGEST4 GD CALLS / ENTRY EXIT");
            for(unsigned i=first;i<first+2u && i<t->longest_count;i++)
            native_loader_timeline_call(&t->longest[i],i);
        retail_display_pause(240u);
    }
    unsigned shown=t->request_count<8u?t->request_count:8u;
    for(unsigned first=0u;first<shown;first+=4u) {
        retail_display_restore(display);
        retail_display_line("RECENT8 READS / 3 ROWS EACH");
        retail_display_line("R0 TOK LBA N TICKS");
        for(unsigned i=first;i<first+4u && i<shown;i++) {
            unsigned at=(t->request_next+KUI_NATIVE_TIMELINE_REQUESTS-shown+i)%KUI_NATIVE_TIMELINE_REQUESTS;
            const struct kui_native_loader_request *r=&t->requests[at];
            uint32_t values[4]={r->token,r->lba,r->count,r->total_ticks};
            native_loader_timeline_row(i-first,0u,values,4u);
            values[0]=r->inside_ticks;values[1]=r->outside_ticks;
            values[2]=r->calls;values[3]=r->progress;
            native_loader_timeline_row(i-first,1u,values,4u);
            values[0]=r->epoch;values[1]=r->flags;
            values[2]=r->chain_length;values[3]=r->chain_ticks;
            native_loader_timeline_row(i-first,2u,values,4u);
        }
        retail_display_line("R1 GD IN/OUT CALLS BYTES");
        retail_display_line("R2 EPOCH FLAGS CHAINLEN TICKS");
        retail_display_pause(240u);
    }
}
#endif
