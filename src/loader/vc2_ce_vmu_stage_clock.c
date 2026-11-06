/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vc2_ce_vmu_stage_clock.h"
#define TSTR UINT32_C(0xffd80004)
#define TCOR2 UINT32_C(0xffd80020)
#define TCNT2 UINT32_C(0xffd80024)
#define TCR2 UINT32_C(0xffd80028)
#ifdef KUI_VC2_STAGE_CLOCK_TEST
extern uint32_t kui_vc2_stage_clock_read(uint32_t,unsigned);
extern void kui_vc2_stage_clock_write(uint32_t,uint32_t,unsigned);
extern uint32_t kui_vc2_stage_clock_sr(void);
#define READ(a,w) kui_vc2_stage_clock_read(a,w)
#define WRITE(a,v,w) kui_vc2_stage_clock_write(a,v,w)
#define SR() kui_vc2_stage_clock_sr()
#else
static uint32_t clock_read(uint32_t a,unsigned w) {
    return w==1u?*(volatile uint8_t *)(uintptr_t)a:w==2u?
        *(volatile uint16_t *)(uintptr_t)a:*(volatile uint32_t *)(uintptr_t)a;
}
static void clock_write(uint32_t a,uint32_t v,unsigned w) {
    if(w==1u)*(volatile uint8_t *)(uintptr_t)a=(uint8_t)v;
    else if(w==2u)*(volatile uint16_t *)(uintptr_t)a=(uint16_t)v;
    else *(volatile uint32_t *)(uintptr_t)a=v;
}
static uint32_t clock_sr(void) {uint32_t sr;__asm__ volatile("stc sr,%0":"=r"(sr));return sr;}
#define READ clock_read
#define WRITE clock_write
#define SR clock_sr
#endif
bool kui_vc2_ce_vmu_stage_clock_start(struct kui_vc2_ce_vmu_stage_clock *c) {
    if(!c || c->active || (SR()&UINT32_C(0x400000f0))!=UINT32_C(0x400000f0))return false;
    c->valid=0u;uint32_t start=READ(TSTR,1u),control=READ(TCR2,2u);
    /* Never consume a live owner or a pending underflow/capture event. */
    if((start&4u) || (control&UINT32_C(0x03c0)))return false;
    c->constant=READ(TCOR2,4u);c->counter=READ(TCNT2,4u);c->control=(uint16_t)control;
    c->active=1u;
    WRITE(TCR2,2u,2u);WRITE(TCOR2,UINT32_MAX,4u);WRITE(TCNT2,UINT32_MAX,4u);
    WRITE(TSTR,READ(TSTR,1u)|4u,1u);
    c->valid=(READ(TSTR,1u)&4u) && READ(TCR2,2u)==2u;
    if(!c->valid)(void)kui_vc2_ce_vmu_stage_clock_stop(c);
    return c->valid!=0u;
}
uint32_t kui_vc2_ce_vmu_stage_clock_ticks(struct kui_vc2_ce_vmu_stage_clock *c) {
    if(!c || !c->active || !c->valid)return 0u;
    if(!(READ(TSTR,1u)&4u) || READ(TCR2,2u)!=2u) {c->valid=0u;return 0u;}
    return UINT32_MAX-READ(TCNT2,4u);
}
bool kui_vc2_ce_vmu_stage_clock_stop(struct kui_vc2_ce_vmu_stage_clock *c) {
    if(!c || !c->active)return false;
    WRITE(TSTR,READ(TSTR,1u)&~4u,1u);
    WRITE(TCOR2,c->constant,4u);WRITE(TCNT2,c->counter,4u);WRITE(TCR2,c->control,2u);
    bool ok=!(READ(TSTR,1u)&4u) && READ(TCOR2,4u)==c->constant &&
        READ(TCNT2,4u)==c->counter && READ(TCR2,2u)==c->control;
    c->active=0u;if(!ok)c->valid=0u;return ok;
}
uint32_t kui_vc2_ce_vmu_stage_clock_ms(uint32_t ticks) {
    return (uint32_t)(((uint64_t)ticks*64000u)/UINT32_C(49874880));
}
