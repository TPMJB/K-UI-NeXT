/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/native_loader_timeline.h"
#include "kui/gd_service.h"
#include <stddef.h>
#include <string.h>

#define HALF UINT32_C(0x7fffffff)
static void add32(struct kui_native_loader_timeline *c,uint32_t *p,uint32_t n) {
    if(n>UINT32_MAX-*p) {*p=UINT32_MAX;c->saturated=1u;}
    else *p+=n;
}
/* Explicit words avoid a compiler-generated large automatic structure copy. */
typedef uint32_t timeline_word __attribute__((__may_alias__));
static void words(void *destination,const void *source,unsigned n) {
    unsigned char *d=destination;const unsigned char *s=source;
    while(n--) {
        *(timeline_word *)(void *)d=*(const timeline_word *)(const void *)s;
        d+=4u;s+=4u;
    }
}
void kui_native_loader_timeline_reset(struct kui_native_loader_timeline *c) {
    if(c)memset(c,0,sizeof(*c));
}
static unsigned bin_at(struct kui_native_loader_timeline *c,uint32_t tick) {
    uint32_t elapsed=c->origin_tick-tick;
    if(elapsed>HALF)return KUI_NATIVE_TIMELINE_BINS;
    uint32_t offset=elapsed/KUI_NATIVE_TIMELINE_BIN_TICKS;
    if(offset>=KUI_NATIVE_TIMELINE_BINS || c->epoch_bin>=KUI_NATIVE_TIMELINE_BINS-offset) {
        add32(c,&c->overflow,1u);return KUI_NATIVE_TIMELINE_BINS;
    }
    unsigned index=(unsigned)(c->epoch_bin+offset);
    while(c->bin_count<=index) {
        struct kui_native_loader_bin *b=&c->bins[c->bin_count];
        b->epoch=c->epoch;
        b->tick=c->origin_tick-(c->bin_count-c->epoch_bin)*KUI_NATIVE_TIMELINE_BIN_TICKS;
        ++c->bin_count;
    }
    return index;
}
static void duration_bins(struct kui_native_loader_timeline *c,uint32_t begin,uint32_t end) {
    uint32_t remaining=begin-end;
    if(remaining>HALF)return;
    while(remaining) {
        unsigned at=bin_at(c,begin);
        if(at>=KUI_NATIVE_TIMELINE_BINS)return;
        uint32_t into=c->origin_tick-begin;
        uint32_t take=KUI_NATIVE_TIMELINE_BIN_TICKS-into%KUI_NATIVE_TIMELINE_BIN_TICKS;
        if(take>remaining)take=remaining;
        add32(c,&c->bins[at].gd_ticks,take);
        remaining-=take;begin-=take;
    }
}
static void save_request(struct kui_native_loader_timeline *c,uint32_t tick,uint32_t flags) {
    struct kui_native_loader_request *a=&c->active;
    if(!c->have_request)return;
    a->flags|=flags;a->end_tick=tick;
    if(!(a->flags&KUI_NATIVE_TIMELINE_BAD_CLOCK) && a->epoch==c->epoch && a->begin_tick-tick<=HALF) {
        a->total_ticks=a->begin_tick-tick;
        if(a->inside_ticks>a->total_ticks) {
            a->inside_ticks=a->total_ticks;a->flags|=KUI_NATIVE_TIMELINE_PARTIAL;
        }
        a->outside_ticks=a->total_ticks-a->inside_ticks;
        if(!(a->flags&(KUI_NATIVE_TIMELINE_REPLACED|KUI_NATIVE_TIMELINE_REQUEST_FAILED))) {
            uint32_t gap=c->chain_end-a->begin_tick;
            if(c->chain_length && c->chain_epoch==a->epoch && gap<=KUI_NATIVE_TIMELINE_CHAIN_GAP) {
                add32(c,&c->chain_length,1u);
            } else {c->chain_length=1u;c->chain_begin=a->begin_tick;c->chain_inside=0u;}
            c->chain_epoch=a->epoch;c->chain_end=tick;
            add32(c,&c->chain_inside,a->inside_ticks);
            a->chain_length=c->chain_length;a->chain_ticks=c->chain_begin-tick;
            a->chain_inside=c->chain_inside;
            if(a->chain_length>c->max_chain_length)c->max_chain_length=a->chain_length;
            if(a->chain_ticks>c->max_chain_ticks) {
                c->max_chain_ticks=a->chain_ticks;c->max_chain_inside=a->chain_inside;
            }
        } else c->chain_length=0u;
    } else {a->flags|=KUI_NATIVE_TIMELINE_BAD_CLOCK;c->chain_length=0u;}
    words(&c->requests[c->request_next],a,sizeof(*a)/4u);
    c->request_next=(c->request_next+1u)%KUI_NATIVE_TIMELINE_REQUESTS;
    if(c->request_count<KUI_NATIVE_TIMELINE_REQUESTS)++c->request_count;
    c->have_request=0u;
}
static void break_clock(struct kui_native_loader_timeline *c) {
    if(c->have_request)save_request(c,c->last_tick,KUI_NATIVE_TIMELINE_BAD_CLOCK);
    if(c->have_enter)add32(c,&c->unmatched,1u);
    c->have_clock=c->have_enter=c->have_exit=0u;
    c->chain_length=0u;
    add32(c,&c->clock_breaks,1u);
}
static void begin_request(struct kui_native_loader_timeline *c,const struct kui_native_loader_event *e,
                          uint32_t begin,uint32_t flags) {
    if(c->have_request)save_request(c,begin,KUI_NATIVE_TIMELINE_REPLACED);
    memset(&c->active,0,sizeof(c->active));
    c->active.epoch=c->epoch;c->active.token=e->token;
    c->active.lba=e->lba;c->active.count=e->count;
    c->active.begin_tick=begin;c->active.progress=e->progress;c->active.result=e->result;
    c->active.flags=flags;c->have_request=1u;c->last_token=e->token;
    unsigned at=bin_at(c,begin);
    if(at<KUI_NATIVE_TIMELINE_BINS)add32(c,&c->bins[at].read_starts,1u);
}
static void request_observation(struct kui_native_loader_timeline *c,const struct kui_native_loader_event *e,
                                uint32_t kind,uint32_t paired,uint32_t elapsed) {
    if(kind==KUI_NATIVE_TIMELINE_EXIT && !e->result &&
       (e->function==KUI_GD_INIT || e->function==KUI_GD_RESET)) {
        if(c->have_request) {
            if(paired) {add32(c,&c->active.calls,1u);add32(c,&c->active.inside_ticks,elapsed);}
            c->active.result=e->result;
            save_request(c,e->tick,KUI_NATIVE_TIMELINE_REPLACED);
        }
        c->last_token=0u;c->chain_length=0u;return;
    }
    uint32_t known_start=kind==KUI_NATIVE_TIMELINE_EXIT && paired && e->function==KUI_GD_REQUEST &&
        (int32_t)e->result>0 && e->result==e->token;
    uint32_t new_token=(e->flags&KUI_NATIVE_TIMELINE_READ) && e->token && e->count && e->token!=c->last_token;
    uint32_t started_partial=0u;
    if(new_token && (known_start || (e->flags&KUI_NATIVE_TIMELINE_PENDING))) {
        started_partial=!known_start;
        begin_request(c,e,known_start?c->entered.tick:e->tick,
                      known_start?0u:KUI_NATIVE_TIMELINE_PARTIAL);
    }
    if(!c->have_request)return;
    if(c->active.token!=e->token) {
        save_request(c,e->tick,KUI_NATIVE_TIMELINE_REPLACED);
        if(!e->token)c->last_token=0u;
        return;
    }
    c->active.progress=e->progress;c->active.result=e->result;
    if(kind==KUI_NATIVE_TIMELINE_EXIT && paired) {
        add32(c,&c->active.calls,1u);
        if(!started_partial)add32(c,&c->active.inside_ticks,elapsed);
    }
    if(e->flags&(KUI_NATIVE_TIMELINE_COMPLETE|KUI_NATIVE_TIMELINE_FAILED)) {
        unsigned at=bin_at(c,e->tick);
        if(at<KUI_NATIVE_TIMELINE_BINS)add32(c,&c->bins[at].read_completes,1u);
        save_request(c,e->tick,(e->flags&KUI_NATIVE_TIMELINE_FAILED)?KUI_NATIVE_TIMELINE_REQUEST_FAILED:0u);
    } else if(!(e->flags&(KUI_NATIVE_TIMELINE_READ|KUI_NATIVE_TIMELINE_PENDING))) {
        /* An observed cleared/non-read handle is no longer the pending read,
         * even if its reset/abort call preceded capture or was unmatched. */
        save_request(c,e->tick,KUI_NATIVE_TIMELINE_REPLACED);
        c->last_token=0u;
    }
}
static void longest_call(struct kui_native_loader_timeline *c,const struct kui_native_loader_event *e,
                         uint32_t elapsed,uint32_t gap) {
    unsigned at=0u;
    while(at<c->longest_count && c->longest[at].elapsed>=elapsed)++at;
    if(at>=KUI_NATIVE_TIMELINE_LONGEST)return;
    if(c->longest_count<KUI_NATIVE_TIMELINE_LONGEST)++c->longest_count;
    for(unsigned n=c->longest_count-1u;n>at;--n)
        words(&c->longest[n],&c->longest[n-1u],sizeof(c->longest[n])/4u);
    words(&c->longest[at].begin,&c->entered,sizeof(*e)/4u);
    words(&c->longest[at].end,e,sizeof(*e)/4u);
    c->longest[at].elapsed=elapsed;c->longest[at].gap=gap;c->longest[at].epoch=c->epoch;
}
void kui_native_loader_timeline_add(struct kui_native_loader_timeline *c,const struct kui_native_loader_event *e) {
    if(!c || !e)return;
    add32(c,&c->events,1u);
    uint32_t kind=e->flags&KUI_NATIVE_TIMELINE_KIND_MASK;
    if(kind<KUI_NATIVE_TIMELINE_ENTER || kind>KUI_NATIVE_TIMELINE_MAPLE) {
        add32(c,&c->invalid,1u);break_clock(c);return;
    }
    if(e->flags&KUI_NATIVE_TIMELINE_CLOCK_BREAK)break_clock(c);
    if(!(e->flags&KUI_NATIVE_TIMELINE_CLOCK_VALID)) {
        add32(c,&c->invalid,1u);
        if(c->bin_count)add32(c,&c->bins[c->bin_count-1u].invalid,1u);
        if(c->have_clock)break_clock(c);
        return;
    }
    if(c->have_clock && c->last_tick-e->tick>HALF)break_clock(c);
    if(!c->have_clock) {
        add32(c,&c->epoch,1u);c->origin_tick=e->tick;c->epoch_bin=c->bin_count;c->have_clock=1u;
    }
    c->last_tick=e->tick;add32(c,&c->valid,1u);
    unsigned at=bin_at(c,e->tick);
    if(kind==KUI_NATIVE_TIMELINE_MAPLE) {
        if(at<KUI_NATIVE_TIMELINE_BINS)add32(c,&c->bins[at].maples,1u);
        request_observation(c,e,kind,0u,0u);return;
    }
    if(kind==KUI_NATIVE_TIMELINE_ENTER) {
        if(c->have_enter) {
            add32(c,&c->unmatched,1u);
            if(c->have_request)c->active.flags|=KUI_NATIVE_TIMELINE_PARTIAL;
        }
        request_observation(c,e,kind,0u,0u);
        words(&c->entered,e,sizeof(*e)/4u);c->have_enter=1u;return;
    }
    if(!c->have_enter || c->entered.function!=e->function) {
        add32(c,&c->unmatched,1u);c->have_enter=0u;
        request_observation(c,e,kind,0u,0u);return;
    }
    uint32_t elapsed=c->entered.tick-e->tick;
    if(elapsed>HALF) {add32(c,&c->invalid,1u);break_clock(c);return;}
    uint32_t gap=c->have_exit?c->last_exit-c->entered.tick:0u;
    if(gap>HALF)gap=0u;
    if(elapsed>c->max_call_ticks)c->max_call_ticks=elapsed;
    if(gap>c->max_gap_ticks)c->max_gap_ticks=gap;
    if(at<KUI_NATIVE_TIMELINE_BINS)add32(c,&c->bins[at].calls,1u);
    duration_bins(c,c->entered.tick,e->tick);
    longest_call(c,e,elapsed,gap);
    request_observation(c,e,kind,1u,elapsed);
    c->last_exit=e->tick;c->have_exit=1u;c->have_enter=0u;
}
