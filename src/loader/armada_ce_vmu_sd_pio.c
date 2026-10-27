/* SPDX-License-Identifier: BSD-3-Clause
 * SCI synchronous mode, bit order and PA7 mapping adapted from KallistiOS
 * kernel/arch/dreamcast/hardware/sci.c, Copyright (C) 2025 Ruslan Rostovtsev.
 * Pinned upstream: fcfa7d869471591ca1c777543261a7bfea7cb726.
 * K-UI adaptation Copyright (C) 2026 K-UI contributors.
 * See LICENSES/LICENSE.KOS for the retained conditions and disclaimer.
 */
#include "kui/armada_ce_vmu_sd_pio.h"
#if KUI_ARMADA_CE_VMU_WRITE_RUNTIME || defined(KUI_ARMADA_CE_VMU_SD_PIO_TEST)
#define MAGIC UINT32_C(0x4b565031)
#define SMR UINT32_C(0xffe00000)
#define BRR UINT32_C(0xffe00004)
#define SCR UINT32_C(0xffe00008)
#define TDR UINT32_C(0xffe0000c)
#define SSR UINT32_C(0xffe00010)
#define RDR UINT32_C(0xffe00014)
#define PTR UINT32_C(0xffe00018)
#define STB UINT32_C(0xffc00004)
#define PCTR UINT32_C(0xff80002c)
#define PDTR UINT32_C(0xff800030)
#define CS UINT16_C(0x0080)
#define CS_MODE UINT32_C(0xc000)
#define CS_OUTPUT UINT32_C(0x4000)
#define TDRE 0x80u
#define RDRF 0x40u
#define ERRORS 0x38u
#define TEND 0x04u
#define POLLS 10000u

#ifdef KUI_ARMADA_CE_VMU_SD_PIO_TEST
extern uint32_t kui_armada_ce_vmu_sd_pio_test_read(uint32_t,unsigned);
extern void kui_armada_ce_vmu_sd_pio_test_write(uint32_t,uint32_t,unsigned);
extern uint32_t kui_armada_ce_vmu_sd_pio_test_sr(void);
extern void kui_armada_ce_vmu_sd_pio_test_set_sr(uint32_t);
extern void kui_armada_ce_vmu_sd_pio_test_delay(uint32_t);
#define READ(a,w) kui_armada_ce_vmu_sd_pio_test_read(a,w)
#define WRITE(a,v,w) kui_armada_ce_vmu_sd_pio_test_write(a,v,w)
#define armada_pio_sr_get kui_armada_ce_vmu_sd_pio_test_sr
#define armada_pio_sr_set kui_armada_ce_vmu_sd_pio_test_set_sr
#define armada_pio_delay kui_armada_ce_vmu_sd_pio_test_delay
#else
#define READ(a,w) ((w)==1u?*(volatile uint8_t *)(uintptr_t)(a): \
    (w)==2u?*(volatile uint16_t *)(uintptr_t)(a):*(volatile uint32_t *)(uintptr_t)(a))
static void armada_pio_write_mmio(uint32_t a,uint32_t v,unsigned w) {
    if(w==1u) *(volatile uint8_t *)(uintptr_t)a=(uint8_t)v;
    else if(w==2u) *(volatile uint16_t *)(uintptr_t)a=(uint16_t)v;
    else *(volatile uint32_t *)(uintptr_t)a=v;
}
#define WRITE armada_pio_write_mmio
static uint32_t armada_pio_sr_get(void) {uint32_t v;__asm__ volatile("stc sr,%0":"=r"(v));return v;}
static void armada_pio_sr_set(uint32_t v) {__asm__ volatile("ldc %0,sr"::"r"(v):"t","memory");}
static void armada_pio_delay(uint32_t n) {__asm__ volatile("1: dt %0\n\tbf 1b":"+r"(n)::"t","memory");}
#endif
#define rd8(a) ((uint8_t)READ(a,1u))
#define rd16(a) ((uint16_t)READ(a,2u))
#define rd32(a) READ(a,4u)
#define wr8(a,v) WRITE(a,(uint8_t)(v),1u)
#define wr16(a,v) WRITE(a,(uint16_t)(v),2u)
#define wr32(a,v) WRITE(a,(uint32_t)(v),4u)

static bool armada_pio_valid(const struct kui_armada_ce_vmu_sd_pio *s) {
    return s && s->self==(uintptr_t)s && s->magic==MAGIC && s->acquired<=1u && s->captured<=1u;
}
static int armada_pio_fault(struct kui_armada_ce_vmu_sd_pio *s,unsigned why,uint32_t registers) {
    if(!s->fault) {s->detail=(uint32_t)s->stage<<16|registers;s->fault=why;}
    return (int)s->fault;
}
/* Only call after wake succeeds and while the exclusive port is still live.
 * Keep unsafe/owner/context refusals on the no-MMIO fault path above. */
static int armada_pio_sci_fault(struct kui_armada_ce_vmu_sd_pio *s,unsigned why) {
    if(s->fault) return (int)s->fault;
    uint8_t status=rd8(SSR),scr=rd8(SCR);
    return armada_pio_fault(s,why,(uint32_t)scr<<8|status);
}
static bool armada_pio_context(uint32_t sr,bool masked) {
    uint32_t bits=sr&UINT32_C(0x500000f0);
    return masked?bits==UINT32_C(0x400000f0):
        bits==UINT32_C(0x40000000) || bits==UINT32_C(0x400000f0);
}
static uint8_t armada_pio_reverse(uint8_t b) {
    b=(uint8_t)((b>>4)|(b<<4));
    b=(uint8_t)(((b&0xccu)>>2)|((b&0x33u)<<2));
    return (uint8_t)(((b&0xaau)>>1)|((b&0x55u)<<1));
}
static bool armada_pio_wait_flag(struct kui_armada_ce_vmu_sd_pio *s,uint8_t flags) {
    uint8_t status=0;
    for(unsigned n=0;n<POLLS;++n) {
        status=rd8(SSR);
        if(status&ERRORS) break;
        if((status&flags)==flags) return true;
    }
    (void)armada_pio_fault(s,KUI_ARMADA_CE_VMU_SD_PIO_TIMEOUT,(uint32_t)rd8(SCR)<<8|status);
    wr8(SCR,0);return false;
}
int kui_armada_ce_vmu_sd_pio_init(struct kui_armada_ce_vmu_sd_pio *s) {
    if(!s) return KUI_ARMADA_CE_VMU_SD_PIO_ARGUMENT;
#ifndef KUI_ARMADA_CE_VMU_SD_PIO_TEST
    if(((uintptr_t)s&3u) || (uintptr_t)s<UINT32_C(0xac000000) ||
       (uintptr_t)s>UINT32_C(0xad000000)-sizeof(*s))
        return KUI_ARMADA_CE_VMU_SD_PIO_ARGUMENT;
#endif
    const uint8_t *p=(const uint8_t *)(const void *)s;
    for(size_t i=0;i<sizeof(*s);++i) if(p[i]) return KUI_ARMADA_CE_VMU_SD_PIO_STATE;
    s->self=(uintptr_t)s;s->magic=MAGIC;return KUI_ARMADA_CE_VMU_SD_PIO_OK;
}
bool kui_armada_ce_vmu_sd_pio_healthy(const struct kui_armada_ce_vmu_sd_pio *s) {
    return armada_pio_valid(s) && s->acquired && s->captured && !s->fault;
}
int kui_armada_ce_vmu_sd_pio_acquire(struct kui_armada_ce_vmu_sd_pio *s) {
    if(!armada_pio_valid(s)) return KUI_ARMADA_CE_VMU_SD_PIO_STATE;
    if(s->fault) return (int)s->fault;
    s->stage=KUI_ARMADA_CE_VMU_SD_PIO_STAGE_OWNER;
    if(s->acquired) return armada_pio_fault(s,KUI_ARMADA_CE_VMU_SD_PIO_STATE,0);
    s->stage=KUI_ARMADA_CE_VMU_SD_PIO_STAGE_ACQUIRE_CONTEXT;
    if(!armada_pio_context(armada_pio_sr_get(),true)) return armada_pio_fault(s,KUI_ARMADA_CE_VMU_SD_PIO_CONTEXT,0);
    /* A released reader may have restored module standby. No SCI register
     * can be sampled until that owned module is running. Record this partial
     * wake before mutation; release can restore it without a SCI snapshot. */
    s->stage=KUI_ARMADA_CE_VMU_SD_PIO_STAGE_WAKE;
    s->standby=rd8(STB)&1u;s->captured=0u;
    if(s->standby) {
        s->acquired=1u;wr8(STB,rd8(STB)&~1u);
        if(rd8(STB)&1u) return armada_pio_fault(s,KUI_ARMADA_CE_VMU_SD_PIO_RESTORE,0);
    }
    s->stage=KUI_ARMADA_CE_VMU_SD_PIO_STAGE_IDLE;
    uint8_t scr=rd8(SCR),status=rd8(SSR);
    if((scr&0xf4u) || (status&(RDRF|ERRORS)) || (status&(TDRE|TEND))!=(TDRE|TEND))
        return armada_pio_fault(s,KUI_ARMADA_CE_VMU_SD_PIO_IDLE,(uint32_t)scr<<8|status);
    s->scr=scr;s->smr=rd8(SMR);s->brr=rd8(BRR);s->ptr=rd8(PTR);
    s->mode=rd32(PCTR)&CS_MODE;s->data=rd16(PDTR)&CS;s->captured=1u;
    /* Ownership is recorded before the first possible hardware mutation. */
    s->acquired=1u;
    s->stage=KUI_ARMADA_CE_VMU_SD_PIO_STAGE_SETUP;
    wr8(SCR,0);wr16(PDTR,rd16(PDTR)|CS);
    wr32(PCTR,(rd32(PCTR)&~CS_MODE)|CS_OUTPUT);
    wr8(PTR,0);wr8(SMR,0x80u);wr8(BRR,31u);armada_pio_delay(1024u);
    s->slow=1u;s->selected=0u;
    if(rd8(STB)&1u) return armada_pio_fault(s,KUI_ARMADA_CE_VMU_SD_PIO_RESTORE,0);
    if(rd8(SCR) || rd8(PTR) || rd8(SMR)!=0x80u || rd8(BRR)!=31u ||
       (rd32(PCTR)&CS_MODE)!=CS_OUTPUT || !(rd16(PDTR)&CS))
        return armada_pio_sci_fault(s,KUI_ARMADA_CE_VMU_SD_PIO_RESTORE);
    return KUI_ARMADA_CE_VMU_SD_PIO_OK;
}
int kui_armada_ce_vmu_sd_pio_release(struct kui_armada_ce_vmu_sd_pio *s) {
    if(!armada_pio_valid(s)) return KUI_ARMADA_CE_VMU_SD_PIO_STATE;
    s->stage=KUI_ARMADA_CE_VMU_SD_PIO_STAGE_OWNER;
    if(!s->acquired) return s->fault?(int)s->fault:armada_pio_fault(s,KUI_ARMADA_CE_VMU_SD_PIO_STATE,0);
    s->stage=KUI_ARMADA_CE_VMU_SD_PIO_STAGE_RELEASE_CONTEXT;
    if(!armada_pio_context(armada_pio_sr_get(),true)) return armada_pio_fault(s,KUI_ARMADA_CE_VMU_SD_PIO_CONTEXT,0);
    if(s->captured && (rd8(STB)&1u)) {
        /* A stopped module cannot be sampled or restored. Preserve ownership
         * until restoration is verified; the caller must quarantine the loan.
         * In particular, SETUP may already have recorded this same fault. */
        s->stage=KUI_ARMADA_CE_VMU_SD_PIO_STAGE_STANDBY;
        return armada_pio_fault(s,KUI_ARMADA_CE_VMU_SD_PIO_RESTORE,0);
    }
    if(!s->captured) {
        /* A failed wake or idle check changed only module standby. */
        s->stage=KUI_ARMADA_CE_VMU_SD_PIO_STAGE_STANDBY;
        wr8(STB,(rd8(STB)&~1u)|s->standby);
        if((rd8(STB)&1u)!=s->standby) (void)armada_pio_fault(s,KUI_ARMADA_CE_VMU_SD_PIO_RESTORE,0);
        s->acquired=0u;return (int)s->fault;
    }
    s->stage=KUI_ARMADA_CE_VMU_SD_PIO_STAGE_RELEASE_TEND;
    if(!s->fault) (void)armada_pio_wait_flag(s,TEND);
    s->stage=KUI_ARMADA_CE_VMU_SD_PIO_STAGE_RESTORE;
    wr8(SCR,0);wr16(PDTR,rd16(PDTR)|CS);
    uint8_t status=rd8(SSR);
    if(status&RDRF) (void)rd8(RDR);
    if(status&(RDRF|ERRORS)) wr8(SSR,status&~(RDRF|ERRORS));
    wr8(SMR,s->smr);wr8(BRR,s->brr);wr8(PTR,s->ptr);
    wr32(PCTR,(rd32(PCTR)&~CS_MODE)|s->mode);
    wr16(PDTR,(rd16(PDTR)&(uint16_t)~CS)|s->data);wr8(SCR,s->scr);
    if(rd8(SMR)!=s->smr || rd8(BRR)!=s->brr || rd8(PTR)!=s->ptr || rd8(SCR)!=s->scr ||
       (rd32(PCTR)&CS_MODE)!=s->mode || (rd16(PDTR)&CS)!=s->data)
        (void)armada_pio_sci_fault(s,KUI_ARMADA_CE_VMU_SD_PIO_RESTORE);
    s->stage=KUI_ARMADA_CE_VMU_SD_PIO_STAGE_STANDBY;
    wr8(STB,(rd8(STB)&~1u)|s->standby);
    if((rd8(STB)&1u)!=s->standby) (void)armada_pio_fault(s,KUI_ARMADA_CE_VMU_SD_PIO_RESTORE,0);
    s->acquired=0u;s->selected=0u;return (int)s->fault;
}
static bool armada_pio_owner(struct kui_armada_ce_vmu_sd_pio *s) {
    if(!armada_pio_valid(s)) return false;
    s->stage=KUI_ARMADA_CE_VMU_SD_PIO_STAGE_OWNER;
    if(!s->acquired || !s->captured) { (void)armada_pio_fault(s,KUI_ARMADA_CE_VMU_SD_PIO_STATE,0);return false; }
    s->stage=KUI_ARMADA_CE_VMU_SD_PIO_STAGE_TRANSFER_CONTEXT;
    if(!armada_pio_context(armada_pio_sr_get(),false)) { (void)armada_pio_fault(s,KUI_ARMADA_CE_VMU_SD_PIO_CONTEXT,0);return false; }
    return true;
}
static void armada_pio_select_card(void *ctx,bool selected) {
    struct kui_armada_ce_vmu_sd_pio *s=ctx;
    if(!armada_pio_owner(s) || (s->fault && selected)) return;
    s->stage=KUI_ARMADA_CE_VMU_SD_PIO_STAGE_SELECT;
    if(!s->fault && !armada_pio_wait_flag(s,TEND)) return;
    /* Only the GPIO read/modify/write is masked, preserving unrelated pins. */
    uint32_t before=armada_pio_sr_get();armada_pio_sr_set(before|0xf0u);
    uint16_t data=rd16(PDTR);
    wr16(PDTR,selected?(data&(uint16_t)~CS):(data|CS));
    bool okay=((rd16(PDTR)&CS)==(selected?0u:CS));
    armada_pio_sr_set(before);
    if(!okay) (void)armada_pio_sci_fault(s,KUI_ARMADA_CE_VMU_SD_PIO_RESTORE);
    else s->selected=selected?1u:0u;
}
static uint8_t armada_pio_transfer(void *ctx,uint8_t data,bool slow) {
    struct kui_armada_ce_vmu_sd_pio *s=ctx;
    if(!armada_pio_valid(s)) return 0xffu;
    /* Protocol token deadlines must progress even after a latched failure
     * makes every later byte a no-I/O 0xff result. */
    s->work+=slow?256u:8u;
    if(!armada_pio_owner(s) || s->fault) return 0xffu;
    if(s->slow!=(slow?1u:0u)) {
        s->stage=KUI_ARMADA_CE_VMU_SD_PIO_STAGE_SPEED;
        if(!armada_pio_wait_flag(s,TEND)) return 0xffu;
        wr8(SCR,0);wr8(BRR,slow?31u:0u);armada_pio_delay(1024u);
        if(rd8(BRR)!=(slow?31u:0u)) { (void)armada_pio_sci_fault(s,KUI_ARMADA_CE_VMU_SD_PIO_RESTORE);return 0xffu; }
        s->slow=slow?1u:0u;
    }
    s->stage=KUI_ARMADA_CE_VMU_SD_PIO_STAGE_SCR;
    wr8(SCR,0x30u);
    if(rd8(SCR)!=0x30u) { (void)armada_pio_sci_fault(s,KUI_ARMADA_CE_VMU_SD_PIO_RESTORE);return 0xffu; }
    s->stage=KUI_ARMADA_CE_VMU_SD_PIO_STAGE_TDRE;
    if(!armada_pio_wait_flag(s,TDRE)) return 0xffu;
    wr8(TDR,armada_pio_reverse(data));wr8(SSR,0x7cu);
    s->stage=KUI_ARMADA_CE_VMU_SD_PIO_STAGE_RDRF;
    if(!armada_pio_wait_flag(s,RDRF)) return 0xffu;
    uint8_t received=rd8(RDR);wr8(SSR,0xbcu);
    s->stage=KUI_ARMADA_CE_VMU_SD_PIO_STAGE_CLEAR;
    uint8_t status=rd8(SSR);
    if(status&(RDRF|ERRORS)) {
        (void)armada_pio_fault(s,KUI_ARMADA_CE_VMU_SD_PIO_RESTORE,(uint32_t)rd8(SCR)<<8|status);wr8(SCR,0);return 0xffu;
    }
    s->stage=KUI_ARMADA_CE_VMU_SD_PIO_STAGE_TEND;
    if(!armada_pio_wait_flag(s,TEND)) return 0xffu;
    return armada_pio_reverse(received);
}
static uint32_t armada_pio_ticks(void *ctx) {
    const struct kui_armada_ce_vmu_sd_pio *s=ctx;return armada_pio_valid(s)?s->work:0u;
}
int kui_armada_ce_vmu_sd_pio_bus(struct kui_armada_ce_vmu_sd_pio *s,struct kui_loader_sd_bus *out) {
    if(!armada_pio_valid(s) || !out) return KUI_ARMADA_CE_VMU_SD_PIO_ARGUMENT;
    uintptr_t a=(uintptr_t)s,b=(uintptr_t)out;
    if(a<=b?b-a<sizeof(*s):a-b<sizeof(*out)) return KUI_ARMADA_CE_VMU_SD_PIO_ARGUMENT;
    *out=(struct kui_loader_sd_bus){s,NULL,NULL,armada_pio_select_card,armada_pio_transfer,armada_pio_ticks,NULL};
    return KUI_ARMADA_CE_VMU_SD_PIO_OK;
}
#endif
