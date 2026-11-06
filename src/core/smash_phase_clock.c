/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_phase_clock.h"
#include <stdbool.h>
#include <string.h>
#define PMCR0 UINT32_C(0xff000084)
#define PRFC_HIGH UINT32_C(0xff100004)
#define PRFC_LOW UINT32_C(0xff100008)
#define FRQCR UINT32_C(0xffc00000)
#define RTC_HIGH UINT32_C(0xa0710000)
#define RTC_LOW UINT32_C(0xa0710004)
#define FIFO_STATUS UINT32_C(0xa05f688c)
#ifdef KUI_SMASH_PHASE_CLOCK_HOST_TEST
extern uint16_t kui_smash_phase_clock_test_read16(uint32_t);
extern uint32_t kui_smash_phase_clock_test_read32(uint32_t);
extern uint32_t kui_smash_phase_clock_test_sr(void);
#define read16 kui_smash_phase_clock_test_read16
#define read32 kui_smash_phase_clock_test_read32
#define read_sr kui_smash_phase_clock_test_sr
#else
static uint16_t read16(uint32_t p) {return *(volatile const uint16_t *)(uintptr_t)p;}
static uint32_t read32(uint32_t p) {return *(volatile const uint32_t *)(uintptr_t)p;}
static uint32_t read_sr(void) {uint32_t v;__asm__ volatile("stc sr,%0":"=r"(v));return v;}
#endif
static bool cycles(uint64_t *value) {
    for(unsigned i=0;i<4u;i++) {
        uint32_t high=read32(PRFC_HIGH)&0xffffu,low=read32(PRFC_LOW);
        if(high==(read32(PRFC_HIGH)&0xffffu)) {*value=(uint64_t)high<<32|low;return true;}
    }
    return false;
}
static uint32_t g2_busy(void) {
    uint32_t busy=(read32(FIFO_STATUS)&0x31u)<<8;
    for(unsigned i=0;i<4u;i++) {
        uint32_t base=UINT32_C(0xa05f7800)+32u*i;
        if((read32(base+20u)|read32(base+24u))&1u)busy|=1u<<i;
    }
    return busy;
}
static bool rtc_seconds(uint32_t *value) {
    for(unsigned i=0;i<4u;i++) {
        uint32_t high=read32(RTC_HIGH)&0xffffu,low=read32(RTC_LOW)&0xffffu;
        if(high==(read32(RTC_HIGH)&0xffffu)) {*value=high<<16|low;return true;}
    }
    return false;
}
static inline __attribute__((always_inline)) void sample_cycles(struct kui_smash_phase_stamp *out) {
    if(!out)return;
    memset(out,0,sizeof(*out));
    uint32_t pmcr=read16(PMCR0),frqcr=read16(FRQCR);
    out->pmcr=pmcr;out->frqcr=frqcr;
    uint64_t first=0u,last=0u;
    if(pmcr!=0xc023u)out->flags|=KUI_SMASH_PHASE_BAD_CPU_CONFIG;
    else if(!cycles(&first))out->flags|=KUI_SMASH_PHASE_TORN_COUNTER;
    else {
        uint32_t after=read16(PMCR0),frequency=read16(FRQCR);
        if(after!=pmcr || frequency!=frqcr)out->flags|=KUI_SMASH_PHASE_BAD_CPU_CONFIG;
        else if(!cycles(&last))out->flags|=KUI_SMASH_PHASE_TORN_COUNTER;
        else if(read16(PMCR0)!=pmcr || read16(FRQCR)!=frqcr)out->flags|=KUI_SMASH_PHASE_BAD_CPU_CONFIG;
        else if(!((last-first)&KUI_SMASH_PHASE_COUNTER_MASK))out->flags|=KUI_SMASH_PHASE_STOPPED_COUNTER;
        else {
            out->cycles_hi=(uint32_t)(last>>32);out->cycles_lo=(uint32_t)last;
            out->flags|=KUI_SMASH_PHASE_CYCLES;
            if((frqcr&0xfffu)==0xe0au)out->flags|=KUI_SMASH_PHASE_NOMINAL_MS;
        }
    }
}
void kui_smash_phase_clock_read_fast(struct kui_smash_phase_stamp *out) {
    sample_cycles(out);
}
void kui_smash_phase_clock_read(struct kui_smash_phase_stamp *out) {
    if(!out)return;
    sample_cycles(out);
    /* UI callers with live IRQs use PRFC0/KOS time, without touching RTC.
     * A masked cold stage cannot start a new G2 transfer between the idle
     * checks; armed/running hardware producers and queued FIFOs refuse. */
    out->g2_state=g2_busy();
    if((read_sr()&0xf0u)!=0xf0u || out->g2_state)out->flags|=KUI_SMASH_PHASE_BUSY_RTC;
    else {
        uint32_t a=0u,b=0u;
        bool stable=false;
        for(unsigned i=0;i<4u;i++)
            if(rtc_seconds(&a) && rtc_seconds(&b) && a==b) {stable=true;break;}
        out->g2_state|=g2_busy();
        if(out->g2_state)out->flags|=KUI_SMASH_PHASE_BUSY_RTC;
        else if(!stable)out->flags|=KUI_SMASH_PHASE_TORN_RTC;
        else {out->rtc_seconds=a;out->flags|=KUI_SMASH_PHASE_RTC;}
    }
}
void kui_smash_phase_clock_delta(const struct kui_smash_phase_stamp *a,
    const struct kui_smash_phase_stamp *b,struct kui_smash_phase_delta *out) {
    if(!out)return;
    memset(out,0,sizeof(*out));if(!a || !b)return;
    if((a->flags&b->flags&KUI_SMASH_PHASE_CYCLES) && a->pmcr==0xc023u && b->pmcr==a->pmcr &&
       a->frqcr==b->frqcr && !(a->cycles_hi&0xffff0000u) && !(b->cycles_hi&0xffff0000u)) {
        uint64_t first=(uint64_t)a->cycles_hi<<32|a->cycles_lo;
        uint64_t last=(uint64_t)b->cycles_hi<<32|b->cycles_lo;
        out->cycles=(last-first)&KUI_SMASH_PHASE_COUNTER_MASK;
        if(out->cycles)out->flags|=KUI_SMASH_PHASE_CYCLES;
        if(out->cycles && (a->flags&b->flags&KUI_SMASH_PHASE_NOMINAL_MS) &&
           (a->frqcr&0xfffu)==0xe0au && out->cycles/200000u<=UINT32_MAX) {
            out->cpu_ms=(uint32_t)(out->cycles/200000u);out->flags|=KUI_SMASH_PHASE_NOMINAL_MS;
        }
    }
    if(a->flags&b->flags&KUI_SMASH_PHASE_RTC) {
        out->rtc_seconds=b->rtc_seconds-a->rtc_seconds;out->flags|=KUI_SMASH_PHASE_RTC;
    }
    /* This diagnostic expects phases well below one hour. A reset between
     * matching endpoint configurations can otherwise resemble a 48-bit wrap.
     * The coarse independent RTC tolerates its two one-second boundaries. */
    if((out->flags&KUI_SMASH_PHASE_CYCLES) && out->cycles>UINT64_C(720000000000)) {
        out->flags&=~(KUI_SMASH_PHASE_CYCLES|KUI_SMASH_PHASE_NOMINAL_MS);
        out->flags|=KUI_SMASH_PHASE_BAD_DELTA;
    }
    if((out->flags&(KUI_SMASH_PHASE_NOMINAL_MS|KUI_SMASH_PHASE_RTC))==
       (KUI_SMASH_PHASE_NOMINAL_MS|KUI_SMASH_PHASE_RTC)) {
        uint64_t rtc_ms=(uint64_t)out->rtc_seconds*1000u;
        uint64_t cpu_ms=out->cpu_ms,difference=cpu_ms>rtc_ms?cpu_ms-rtc_ms:rtc_ms-cpu_ms;
        if(difference>2000u+rtc_ms/100u) {
            out->flags&=~KUI_SMASH_PHASE_NOMINAL_MS;
            out->flags|=KUI_SMASH_PHASE_CLOCK_DISAGREEMENT;
        }
    }
}
