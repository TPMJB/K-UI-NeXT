/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/toy_decision_observer.h"
#include "kui/toy_pilot_clock.h"
#include "toy_pilot_sci.h"
#include <string.h>
#if !KUI_TOY_DECISION_PROFILE
#error Decision observer belongs only to isolated stripped AP profiles
#endif
/* Volatile selection keeps identical code/BSS geometry in profiles B/C.
 * Only this initialized word and the displayed build identity differ. */
static volatile uint32_t decision_enabled=KUI_TOY_DECISION_PROFILE;
static struct kui_toy_decision_report decision;
static struct {
    uint32_t active,nested,start,valid,epoch,token,credited,command,pending;
    uint32_t saved_sr,vbr,priority,last_end,last_valid,last_epoch,anchor;
    uint32_t call_blocks,irq_blocks,errors,retries,updater,have_progress;
    uint32_t last_token,last_credit,have_token,done_token,done_seen;
    struct kui_toy_decision_service service;
} visit;
#ifdef KUI_TOY_DECISION_HOST_TEST
#define READ(a,n) kui_toy_decision_host_read((a),(n))
#define VBR() kui_toy_decision_host_vbr()
#define CALLER_SR() kui_toy_decision_host_sr()
#define UPDATER() kui_toy_decision_host_updater()
#else
#include "toy_pilot_resident_symbols.h"
extern volatile uint32_t kui_toy_pilot_updater_entries;
static uint32_t read_mmio(uint32_t a,unsigned n) {
    return n==2u?*(volatile const uint16_t *)(uintptr_t)a:
        n==1u?*(volatile const uint8_t *)(uintptr_t)a:*(volatile const uint32_t *)(uintptr_t)a;
}
static uint32_t read_vbr(void) {uint32_t v;__asm__ volatile("stc vbr,%0":"=r"(v));return v;}
#define READ(a,n) read_mmio((a),(n))
#define VBR() read_vbr()
#define CALLER_SR() (*(volatile const uint32_t *)(uintptr_t)KUI_TOY_PILOT_LOW_PROBE_SR)
#define UPDATER() kui_toy_pilot_updater_entries
#endif
static int data(uint32_t c) {return c==KUI_GD_PIOREAD || c==KUI_GD_DMAREAD;}
static void initialize(void) {
    if(decision.magic)return;
    decision.magic=UINT32_C(0x54444f31);decision.version=1u;
    decision.enabled=decision_enabled==2u;decision.expected_vbr=kui_toy_pilot_sci_decision_vbr();
}
static uint32_t sample(uint32_t *tick) {
    uint32_t valid=kui_toy_pilot_clock_profile(READ(0xffc00000u,2u),
        READ(0xffd80008u,4u),READ(0xffd80010u,2u),READ(0xffd80004u,1u));
    *tick=~READ(0xffd8000cu,4u);
    if(!valid) {++decision.epoch;++decision.invalid_clocks;}
    return valid;
}
static void add(struct kui_toy_decision_bin *b,uint32_t *p,uint32_t n) {
    if(n>UINT32_MAX-*p){*p=UINT32_MAX;b->flags|=KUI_TOY_DECISION_SATURATED;}else *p+=n;
}
static void metric(struct kui_toy_decision_bin *b,uint32_t *total,uint32_t *max,uint32_t n) {
    add(b,total,n);if(n>*max)*max=n;
}
static struct kui_toy_decision_bin *select_bin(uint32_t tick,uint32_t valid) {
    struct kui_toy_decision_bin *b=&decision.bins[decision.head];
    if(!decision.count) {
        decision.count=1u;visit.anchor=valid?visit.start:tick;b->sequence=1u;b->epoch=decision.epoch;
        b->first_tick=valid?visit.start:tick;b->last_tick=tick;
    } else if(valid && b->epoch==decision.epoch && tick-visit.anchor<UINT32_C(0x80000000) &&
              tick-visit.anchor>=KUI_TOY_DECISION_BIN_TICKS) {
        uint32_t periods=(tick-visit.anchor)/KUI_TOY_DECISION_BIN_TICKS;
        uint32_t sequence=b->sequence+periods;
        visit.anchor+=periods*KUI_TOY_DECISION_BIN_TICKS;
        if(periods>=8u) {
            decision.expired_bins+=decision.count;
            memset(decision.bins,0,sizeof(decision.bins));decision.head=0u;
        } else decision.head=(decision.head+1u)&7u;
        b=&decision.bins[decision.head];memset(b,0,sizeof(*b));
        b->sequence=sequence;b->epoch=decision.epoch;b->first_tick=visit.start;
        if(periods>1u)b->flags|=KUI_TOY_DECISION_SKIPPED_BINS;
        decision.count=0u;
        for(unsigned i=0;i<8u;++i) {
            struct kui_toy_decision_bin *old=&decision.bins[i];
            if(old->sequence && sequence-old->sequence>=8u) {
                memset(old,0,sizeof(*old));++decision.expired_bins;
            }
            if(old->sequence)++decision.count;
        }
    } else if(valid && b->epoch!=decision.epoch) {
        uint32_t sequence=b->sequence+1u;
        decision.expired_bins+=decision.count;
        memset(decision.bins,0,sizeof(decision.bins));decision.head=0u;decision.count=1u;
        b=&decision.bins[decision.head];memset(b,0,sizeof(*b));
        b->sequence=sequence;b->epoch=decision.epoch;b->first_tick=visit.start;visit.anchor=visit.start;
        b->flags|=KUI_TOY_DECISION_BAD_CLOCK;
    }
    b->last_tick=tick;return b;
}
void kui_toy_decision_gd_begin(const struct kui_retail_gd *s,uint32_t f,uint32_t command) {
    initialize();if(decision_enabled!=2u || decision.frozen)return;
    if(visit.active) {++visit.nested;++decision.nested_calls;return;}
    visit.active=1u;visit.valid=sample(&visit.start);visit.epoch=decision.epoch;
    visit.token=s?s->token:0u;visit.credited=s?s->completed_bytes:0u;
    visit.command=s?s->command:0u;visit.pending=s?s->pending:0u;
    if(f==KUI_GD_REQUEST && data(command))visit.command=command;
    visit.saved_sr=CALLER_SR();visit.vbr=VBR();visit.priority=(READ(0xffd00008u,2u)>>4)&15u;
    if(!visit.have_progress) {
        const struct kui_toy_pilot_sci_stats *e=kui_toy_pilot_sci_snapshot();
        visit.call_blocks=e->call_blocks;visit.irq_blocks=e->irq_blocks;
        visit.errors=e->errors;visit.retries=e->retries;visit.updater=UPDATER();
        visit.have_progress=1u;
    }
    memset(&visit.service,0,sizeof(visit.service));
}
int kui_toy_decision_service_active(void) {
    return decision_enabled==2u && !decision.frozen && visit.active && !visit.nested;
}
void kui_toy_decision_service_commit(const struct kui_toy_decision_service *s) {
    if(kui_toy_decision_service_active())visit.service=*s;
}
void kui_toy_decision_gd_end(const struct kui_retail_gd *s,uint32_t f,uint32_t command,int32_t result) {
    initialize();if(decision_enabled!=2u || decision.frozen)return;
    if(visit.nested){--visit.nested;return;}
    if(!visit.active)return;
    uint32_t end,valid=sample(&end);
    valid=valid && visit.valid && visit.epoch==decision.epoch && end-visit.start<UINT32_C(0x80000000);
    struct kui_toy_decision_bin *b=select_bin(end,valid);
    add(b,&b->gd_calls,1u);
    if(valid) {
        add(b,&b->gd_valid,1u);metric(b,&b->gd_ticks,&b->gd_max,end-visit.start);
        if(visit.start-visit.anchor>=UINT32_C(0x80000000))b->flags|=KUI_TOY_DECISION_CROSS_BIN;
        if(visit.last_valid && visit.last_epoch==decision.epoch && visit.start-visit.last_end<UINT32_C(0x80000000)) {
            add(b,&b->gap_samples,1u);metric(b,&b->gap_ticks,&b->gap_max,visit.start-visit.last_end);
        }
    } else b->flags|=KUI_TOY_DECISION_BAD_CLOCK;
    if(data(visit.command)) {
        add(b,&b->data_calls,1u);
        uint32_t bucket=visit.saved_sr&UINT32_C(0x10000000)?3u:
            ((visit.saved_sr>>4)&15u)==15u?2u:((visit.saved_sr>>4)&15u)>=7u?1u:0u;
        add(b,bucket==0u?&b->mask_low:bucket==1u?&b->mask_high:bucket==2u?&b->mask15:&b->mask_bl,1u);
        add(b,visit.vbr==decision.expected_vbr?&b->route_own:&b->route_other,1u);
        if(!b->priority)b->priority=0x10000u|visit.priority|(visit.priority<<8);
        else if(((b->priority>>8)&15u)!=visit.priority)b->priority|=0x1000000u;
        b->priority=(b->priority&~0xf00u)|(visit.priority<<8);
    }
    if(!decision.first_vbr)decision.first_vbr=visit.vbr;
    decision.last_vbr=visit.vbr;
    if(s && data(visit.command)) {
        uint32_t accepted=f==KUI_GD_REQUEST && result>0 && data(command) && s->token==(uint32_t)result;
        uint32_t before=visit.have_token && visit.last_token==s->token?visit.last_credit:visit.credited;
        if(accepted) {
            before=0u;visit.done_seen=0u;add(b,&b->accepted_data,1u);
            b->read_modes|=command==KUI_GD_PIOREAD?1u:2u;
            if(s->count>b->max_request)b->max_request=s->count;
            decision.last_command=command;decision.last_lba=s->lba;
            decision.last_count=s->count;decision.last_sector_bytes=s->sector_bytes;
        }
        if((s->token==visit.token || accepted) && s->completed_bytes>=before)
            add(b,&b->bytes,s->completed_bytes-before);
        if((s->token==visit.token || accepted) && !s->pending && !s->error &&
           s->status==KUI_GD_COMPLETED && (!visit.done_seen || visit.done_token!=s->token)) {
            add(b,&b->completed_data,1u);visit.done_token=s->token;visit.done_seen=1u;
        }
        visit.last_token=s->token;visit.last_credit=s->completed_bytes;visit.have_token=1u;
    }
    if(f==KUI_GD_INIT || f==KUI_GD_RESET)visit.have_token=visit.done_seen=0u;
    const struct kui_toy_pilot_sci_stats *e=kui_toy_pilot_sci_snapshot();
    uint32_t updater=UPDATER();
    if(visit.have_progress) {
        add(b,&b->call_blocks,e->call_blocks-visit.call_blocks);
        add(b,&b->irq_blocks,e->irq_blocks-visit.irq_blocks);
        add(b,&b->errors,e->errors-visit.errors);add(b,&b->retries,e->retries-visit.retries);
        add(b,&b->updater_entries,updater-visit.updater);
    }
    visit.call_blocks=e->call_blocks;visit.irq_blocks=e->irq_blocks;
    visit.errors=e->errors;visit.retries=e->retries;visit.updater=updater;visit.have_progress=1u;
    const struct kui_toy_decision_service *v=&visit.service;
    add(b,&b->service_calls,v->calls);add(b,&b->wait_steps,v->wait_steps);
    add(b,&b->credit_steps,v->credit_steps);add(b,&b->zero_services,v->zero_calls);
    if(valid && v->elapsed_ticks<UINT32_C(0x80000000)) {
        add(b,&b->service_valid,v->calls);metric(b,&b->service_ticks,&b->service_max,v->elapsed_ticks);
        add(b,&b->wait_ticks,v->wait_ticks);add(b,&b->credit_ticks,v->credit_ticks);
        add(b,&b->other_ticks,v->other_ticks);
    }
    visit.last_end=end;visit.last_valid=valid;visit.last_epoch=decision.epoch;
    visit.active=0u;
}
void kui_toy_decision_freeze(void) {
    initialize();if(decision.frozen)return;
    decision.last_sample_age=UINT32_MAX;
    if(decision_enabled==2u) {
        decision.freeze_valid=sample(&decision.freeze_tick);
        if(decision.freeze_valid && visit.last_valid && visit.last_epoch==decision.epoch &&
           decision.freeze_tick-visit.last_end<UINT32_C(0x80000000))
            decision.last_sample_age=decision.freeze_tick-visit.last_end;
        decision.count=0u;
        for(unsigned i=0;i<8u;++i) {
            struct kui_toy_decision_bin *b=&decision.bins[i];
            if(!b->sequence)continue;
            if(!decision.freeze_valid)b->flags|=KUI_TOY_DECISION_BAD_CLOCK;
            else if(b->epoch!=decision.epoch || decision.freeze_tick-b->last_tick>=
                    KUI_TOY_DECISION_BIN_TICKS*8u) {
                memset(b,0,sizeof(*b));++decision.expired_bins;
            }
            if(b->sequence)++decision.count;
        }
    }
    decision.frozen=1u;
}
const struct kui_toy_decision_report *kui_toy_decision_snapshot(void) {initialize();return &decision;}
