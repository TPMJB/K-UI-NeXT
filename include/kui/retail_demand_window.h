/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_RETAIL_DEMAND_WINDOW_H
#define KUI_RETAIL_DEMAND_WINDOW_H
#include "kui/retail_demand_diagnostic.h"
#include <stddef.h>

#define KUI_RETAIL_DEMAND_WINDOW_BINS 8u
#define KUI_RETAIL_DEMAND_WINDOW_CYCLES UINT32_C(400000000)
#define KUI_RETAIL_DEMAND_WINDOW_CLOCK_INVALID 1u
#define KUI_RETAIL_DEMAND_WINDOW_SATURATED 2u
#define KUI_RETAIL_DEMAND_WINDOW_BODY_CLIPPED 4u
#define KUI_RETAIL_DEMAND_WINDOW_GAP_CLIPPED 8u
#define KUI_RETAIL_DEMAND_WINDOW_CROSSED_BIN 16u
#define KUI_RETAIL_DEMAND_WINDOW_REALIGNED 32u
#define KUI_RETAIL_DEMAND_WINDOW_OVERLAP 64u

/* All counts are observations at existing outer GD/SCI entry and exit,
 * never a new byte/poll callback. kind=0 is GD; kind=1 is SCI handler. */
struct kui_retail_demand_window_visit {
    uint32_t kind,command,irq_blocks,call_blocks,output_bytes,caller_sr;
    uint32_t pending_dma_zero,no_progress;
};
struct kui_retail_demand_window_bin {
    uint32_t epoch,ordinal,first_seq,last_seq,flags,span_cycles;
    uint32_t gd,irq,modes,gd_lo,gd_hi,irq_lo,irq_hi,max_body;
    uint32_t irq_blocks,call_blocks,bytes,masked,dma_zero,zero,gap_max,bad_clock;
};
struct kui_retail_demand_window {
    struct kui_retail_demand_window_bin bin[KUI_RETAIL_DEMAND_WINDOW_BINS],untimed;
    uint32_t index,count,epoch,ordinal,sequence,flags,bad_clock,frozen,have_clock;
    uint32_t anchor_lo,anchor_hi,last_end_lo,last_end_hi,last_begin_lo,last_begin_hi;
};

static inline bool kui_retail_demand_window_clock_ok(const struct kui_retail_demand_clock *s) {
    return s && (s->pmcr&0xe13fu)==0xc023u && (s->frqcr&0xfffu)==0xe0au &&
        s->cycles_hi<=0xffffu;
}
static inline bool kui_retail_demand_window_after(uint32_t alo,uint32_t ahi,
    uint32_t blo,uint32_t bhi) {
    return ahi>bhi || (ahi==bhi && alo>=blo);
}
static inline void kui_retail_demand_window_add(uint32_t *out,uint32_t value,uint32_t *flags) {
    if(UINT32_MAX-*out<value) {*out=UINT32_MAX;*flags|=KUI_RETAIL_DEMAND_WINDOW_SATURATED;}
    else *out+=value;
}
static inline void kui_retail_demand_window_sum(uint32_t *lo,uint32_t *hi,
    uint32_t add_lo,uint32_t add_hi,uint32_t *flags) {
    uint32_t carry=add_lo>UINT32_MAX-*lo;
    if(UINT32_MAX-*hi<add_hi || UINT32_MAX-*hi-add_hi<carry) {
        *lo=*hi=UINT32_MAX;*flags|=KUI_RETAIL_DEMAND_WINDOW_SATURATED;
    } else {*lo+=add_lo;*hi+=add_hi+carry;}
}
static inline uint32_t kui_retail_demand_window_mode(uint32_t command) {
    return command==16u?1u:command==17u?2u:command==38u?4u:command==39u?8u:0u;
}
static inline void kui_retail_demand_window_clear(struct kui_retail_demand_window_bin *b,
    uint32_t epoch,uint32_t ordinal) {
    *b=(struct kui_retail_demand_window_bin){0};b->epoch=epoch;b->ordinal=ordinal;
}
static inline void kui_retail_demand_window_new_epoch(struct kui_retail_demand_window *w,
    const struct kui_retail_demand_clock *begin) {
    kui_retail_demand_window_add(&w->epoch,1u,&w->flags);
    w->index=0u;w->count=1u;w->ordinal=0u;w->have_clock=1u;
    w->anchor_lo=begin->cycles_lo;w->anchor_hi=begin->cycles_hi;
    w->last_end_lo=w->last_begin_lo=begin->cycles_lo;
    w->last_end_hi=w->last_begin_hi=begin->cycles_hi;
    kui_retail_demand_window_clear(&w->bin[0],w->epoch,0u);
}
/* End points belong to the right-closed bin (anchor,anchor+2s]. Bodies are
 * charged in full to their EXIT bin, not split into invented per-bin work.
 * CROSSED_BIN prevents interpreting such a bin as a strict CPU-share sample.
 * An observed gap beyond the ring's16s retention discards all old bins and
 * re-aligns eight real two-second intervals ending at this observation. This
 * rare case clears at most8bins; ordinary updates use one bounded division.
 * There is no64-bit division and no loop proportional to elapsed time. */
static inline void kui_retail_demand_window_advance(struct kui_retail_demand_window *w,
    uint32_t end_lo,uint32_t end_hi) {
    uint32_t low=end_lo-w->anchor_lo;
    uint32_t high=end_hi-w->anchor_hi-(end_lo<w->anchor_lo);
    uint32_t n=high?8u:(low>KUI_RETAIL_DEMAND_WINDOW_CYCLES?
        (low-1u)/KUI_RETAIL_DEMAND_WINDOW_CYCLES:0u);
    if(n>=8u) {
        w->flags|=KUI_RETAIL_DEMAND_WINDOW_REALIGNED;
        kui_retail_demand_window_add(&w->ordinal,8u,&w->flags);
        w->index=7u;w->count=8u;
        w->anchor_lo=end_lo-KUI_RETAIL_DEMAND_WINDOW_CYCLES;
        w->anchor_hi=end_hi-(end_lo<KUI_RETAIL_DEMAND_WINDOW_CYCLES);
        for(unsigned i=0;i<8u;i++) {
            kui_retail_demand_window_clear(&w->bin[i],w->epoch,w->ordinal-(7u-i));
            w->bin[i].span_cycles=KUI_RETAIL_DEMAND_WINDOW_CYCLES;
            w->bin[i].flags|=KUI_RETAIL_DEMAND_WINDOW_REALIGNED;
        }
    } else {
        if(n) {
            uint32_t step=n*KUI_RETAIL_DEMAND_WINDOW_CYCLES;
            uint32_t old=w->anchor_lo;w->anchor_lo+=step;
            w->anchor_hi+=w->anchor_lo<old;
            w->bin[w->index].span_cycles=KUI_RETAIL_DEMAND_WINDOW_CYCLES;
            for(unsigned i=0;i<n;i++) {
                w->index=(w->index+1u)&7u;if(w->count<8u) ++w->count;
                kui_retail_demand_window_add(&w->ordinal,1u,&w->flags);
                kui_retail_demand_window_clear(&w->bin[w->index],w->epoch,w->ordinal);
                w->bin[w->index].span_cycles=KUI_RETAIL_DEMAND_WINDOW_CYCLES;
            }
        }
        w->bin[w->index].span_cycles=end_lo-w->anchor_lo;
    }
}
static inline void kui_retail_demand_window_observation(struct kui_retail_demand_window_bin *b,
    const struct kui_retail_demand_window_visit *v,uint32_t sequence,
    uint32_t body_lo,uint32_t body_hi,bool timed,uint32_t gap_lo,uint32_t gap_hi) {
    if(!b->first_seq) b->first_seq=sequence;
    b->last_seq=sequence;
    kui_retail_demand_window_add(v->kind?&b->irq:&b->gd,1u,&b->flags);
    b->modes|=kui_retail_demand_window_mode(v->command);
    kui_retail_demand_window_add(&b->irq_blocks,v->irq_blocks,&b->flags);
    kui_retail_demand_window_add(&b->call_blocks,v->call_blocks,&b->flags);
    kui_retail_demand_window_add(&b->bytes,v->output_bytes,&b->flags);
    if(!v->kind && (v->caller_sr&0xf0u)) kui_retail_demand_window_add(&b->masked,1u,&b->flags);
    if(v->pending_dma_zero) kui_retail_demand_window_add(&b->dma_zero,1u,&b->flags);
    if(v->no_progress) kui_retail_demand_window_add(&b->zero,1u,&b->flags);
    if(timed) {
        kui_retail_demand_window_sum(v->kind?&b->irq_lo:&b->gd_lo,
            v->kind?&b->irq_hi:&b->gd_hi,body_lo,body_hi,&b->flags);
        uint32_t maximum=body_hi?UINT32_MAX:body_lo;
        if(maximum>b->max_body) b->max_body=maximum;
        if(body_hi) b->flags|=KUI_RETAIL_DEMAND_WINDOW_BODY_CLIPPED;
        maximum=gap_hi?UINT32_MAX:gap_lo;
        if(maximum>b->gap_max) b->gap_max=maximum;
        if(gap_hi) b->flags|=KUI_RETAIL_DEMAND_WINDOW_GAP_CLIPPED;
    } else {
        b->flags|=KUI_RETAIL_DEMAND_WINDOW_CLOCK_INVALID;
        kui_retail_demand_window_add(&b->bad_clock,1u,&b->flags);
    }
}
static inline void kui_retail_demand_window_record(struct kui_retail_demand_window *w,
    const struct kui_retail_demand_clock *begin,const struct kui_retail_demand_clock *end,
    const struct kui_retail_demand_window_visit *v) {
    if(!w || !v || w->frozen) return;
    kui_retail_demand_window_add(&w->sequence,1u,&w->flags);
    bool valid=kui_retail_demand_window_clock_ok(begin) && kui_retail_demand_window_clock_ok(end) &&
        kui_retail_demand_window_after(end->cycles_lo,end->cycles_hi,begin->cycles_lo,begin->cycles_hi) &&
        (end->cycles_lo!=begin->cycles_lo || end->cycles_hi!=begin->cycles_hi);
    bool overlap=valid && w->have_clock &&
        !kui_retail_demand_window_after(begin->cycles_lo,begin->cycles_hi,w->last_end_lo,w->last_end_hi);
    if(overlap) {valid=false;w->flags|=KUI_RETAIL_DEMAND_WINDOW_OVERLAP;}
    if(!valid) {
        w->flags|=KUI_RETAIL_DEMAND_WINDOW_CLOCK_INVALID;
        kui_retail_demand_window_add(&w->bad_clock,1u,&w->flags);
        w->have_clock=0u;w->count=0u;
        kui_retail_demand_window_observation(&w->untimed,v,w->sequence,0u,0u,false,0u,0u);
        return;
    }
    bool first=!w->have_clock;
    if(first) kui_retail_demand_window_new_epoch(w,begin);
    uint32_t gap_lo=first?0u:begin->cycles_lo-w->last_begin_lo;
    uint32_t gap_hi=first?0u:begin->cycles_hi-w->last_begin_hi-(begin->cycles_lo<w->last_begin_lo);
    kui_retail_demand_window_advance(w,end->cycles_lo,end->cycles_hi);
    struct kui_retail_demand_window_bin *b=&w->bin[w->index];
    if(!kui_retail_demand_window_after(begin->cycles_lo,begin->cycles_hi,w->anchor_lo,w->anchor_hi))
        b->flags|=KUI_RETAIL_DEMAND_WINDOW_CROSSED_BIN;
    uint32_t body_lo=end->cycles_lo-begin->cycles_lo;
    uint32_t body_hi=end->cycles_hi-begin->cycles_hi-(end->cycles_lo<begin->cycles_lo);
    kui_retail_demand_window_observation(b,v,w->sequence,body_lo,body_hi,true,gap_lo,gap_hi);
    w->last_end_lo=end->cycles_lo;w->last_end_hi=end->cycles_hi;
    w->last_begin_lo=begin->cycles_lo;w->last_begin_hi=begin->cycles_hi;
}
/* The existing terminal gesture supplies its one final read-only sample,
 * before cleanup. Freeze may age out old bins; it creates no visit/counter.
 * Invalid final time is explicit and does not invent elapsed seconds. */
static inline void kui_retail_demand_window_freeze(struct kui_retail_demand_window *w,
    const struct kui_retail_demand_clock *now) {
    if(!w || w->frozen) return;
    if(w->have_clock && kui_retail_demand_window_clock_ok(now) &&
       kui_retail_demand_window_after(now->cycles_lo,now->cycles_hi,w->last_end_lo,w->last_end_hi))
        kui_retail_demand_window_advance(w,now->cycles_lo,now->cycles_hi);
    else {w->flags|=KUI_RETAIL_DEMAND_WINDOW_CLOCK_INVALID;}
    w->frozen=1u;
}
static inline unsigned kui_retail_demand_window_count(const struct kui_retail_demand_window *w) {
    return w?w->count:0u;
}
static inline const struct kui_retail_demand_window_bin *kui_retail_demand_window_oldest(
    const struct kui_retail_demand_window *w,unsigned index) {
    return w && index<w->count?&w->bin[(w->index+8u-w->count+1u+index)&7u]:NULL;
}
#endif
