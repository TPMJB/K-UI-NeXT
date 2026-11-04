/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_RETAIL_DEMAND_DIAGNOSTIC_H
#define KUI_RETAIL_DEMAND_DIAGNOSTIC_H
#include <stdbool.h>
#include <stdint.h>

/* Separate console experiments only. 1 takes a terminal copy of existing
 * counters, 2 additionally observes GD and SCI-handler bodies. Neither
 * profile changes a read budget, interrupt level, timer or completion. */
#ifndef KUI_RETAIL_DEMAND_DIAGNOSTIC
#define KUI_RETAIL_DEMAND_DIAGNOSTIC 0
#endif
#if KUI_RETAIL_DEMAND_DIAGNOSTIC < 0 || KUI_RETAIL_DEMAND_DIAGNOSTIC > 2
#error "Demand diagnostic profile must be 0, 1 or 2"
#endif
#define KUI_RETAIL_DEMAND_EVENT UINT32_C(0xfffffffd)
#ifndef KUI_RETAIL_DEMAND_PROFILE_ID
#define KUI_RETAIL_DEMAND_PROFILE_ID KUI_RETAIL_DEMAND_DIAGNOSTIC
#endif
#define KUI_RETAIL_DEMAND_VERSION 1u
#define KUI_RETAIL_DEMAND_PAGES 5u
#define KUI_RETAIL_DEMAND_ROWS 7u
enum kui_retail_demand_event {
    KUI_RETAIL_DEMAND_GD_BEGIN=1u,
    KUI_RETAIL_DEMAND_GD_END,
    KUI_RETAIL_DEMAND_IRQ_BEGIN,
    KUI_RETAIL_DEMAND_IRQ_END,
    KUI_RETAIL_DEMAND_REPORT
};
/* High-controller-owned page. Addresses name immutable P1 text; all numeric
 * fields are local authored observations. Terminal reporting performs no
 * guest read, sound API, card operation or clock sample. */
struct kui_retail_demand_page {
    uint32_t title,labels[KUI_RETAIL_DEMAND_ROWS];
    uint32_t rows[KUI_RETAIL_DEMAND_ROWS][5];
};
/* A read-only clock sample. TMU0 is owned by CE and normally wraps at each
 * 25-ms tick. Its modulo difference is ONLY a lower bound, even if UNF did
 * not change. A retained elapsed-cycle performance counter can disambiguate
 * longer bodies; an absent/changed profile must never become a zero time. */
struct kui_retail_demand_clock {
    uint32_t ticks,tcor,tcr,tstr,frqcr,pmcr,cycles_lo,cycles_hi;
};
struct kui_retail_demand_elapsed {
    uint32_t modulo,cycles,flags;
};
#define KUI_RETAIL_DEMAND_TMU_VALID 1u
#define KUI_RETAIL_DEMAND_TMU_WRAP 2u
#define KUI_RETAIL_DEMAND_CYCLES_VALID 4u
#define KUI_RETAIL_DEMAND_CYCLES_CLIPPED 8u
static inline struct kui_retail_demand_elapsed kui_retail_demand_elapsed(
    const struct kui_retail_demand_clock *a,const struct kui_retail_demand_clock *b) {
    struct kui_retail_demand_elapsed out={0u,0u,0u};
    if(a->tcor==b->tcor && (a->tcr&0x27u)==(b->tcr&0x27u) &&
       (a->tcr&7u)<=4u && (a->tstr&1u) && (b->tstr&1u) &&
       (a->frqcr&0xfffu)==0xe0au && (b->frqcr&0xfffu)==0xe0au &&
       a->ticks<=a->tcor && b->ticks<=b->tcor) {
        out.modulo=a->ticks>=b->ticks?a->ticks-b->ticks:
            a->ticks+(a->tcor-b->ticks)+1u;
        out.flags=KUI_RETAIL_DEMAND_TMU_VALID;
        if(a->ticks<b->ticks || (!(a->tcr&0x100u) && (b->tcr&0x100u)))
            out.flags|=KUI_RETAIL_DEMAND_TMU_WRAP;
    }
    /* PMCR1 mode0x23 counts elapsed CPU cycles. Only inspect the already
     * configured counter; no write enables or resets it. High words reject
     * counter reset and distinguish a low-word wrap from a short body. */
    if((a->pmcr&0xe13fu)==0xc023u && (b->pmcr&0xe13fu)==0xc023u &&
       (a->frqcr&0xfffu)==0xe0au && (b->frqcr&0xfffu)==0xe0au &&
       a->cycles_hi<=0xffffu && b->cycles_hi<=0xffffu &&
       (b->cycles_hi>a->cycles_hi ||
        (b->cycles_hi==a->cycles_hi && b->cycles_lo>=a->cycles_lo))) {
        uint32_t high=b->cycles_hi-a->cycles_hi-(b->cycles_lo<a->cycles_lo);
        out.cycles=b->cycles_lo-a->cycles_lo;
        if(out.cycles || high) out.flags|=KUI_RETAIL_DEMAND_CYCLES_VALID;
        if(high) {out.cycles=UINT32_MAX;out.flags|=KUI_RETAIL_DEMAND_CYCLES_CLIPPED;}
    }
    return out;
}
/* event's low byte is the enum; GD events carry function in bits8..15.
 * BEGIN value is the original caller SR, END value is returned R0.
 * REPORT carries page in bits8..15. region is only the admitted low resident
 * region, never a guest destination; rejected pointers are not followed. */
const struct kui_retail_demand_page *kui_armada_ce_demand_event(
    uint32_t event,uint32_t value,uintptr_t region);
#endif
