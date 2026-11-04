/* SPDX-License-Identifier: GPL-3.0-only
 * SCI synchronous mode, bit order and PA7 mapping adapted from KallistiOS
 * kernel/arch/dreamcast/hardware/sci.c, Copyright (C) 2025 Ruslan Rostovtsev.
 * Pinned upstream: fcfa7d869471591ca1c777543261a7bfea7cb726.
 * K-UI adaptation Copyright (C) 2026 K-UI contributors.
 * See LICENSES/LICENSE.KOS for the retained conditions and disclaimer.
 */
#include "kui/report_sci_pio.h"
/* Private terminal-report derivative of armada_ce_vmu_sd_pio.c at aa8ef45.
 * The CE/native VMU adapters are unchanged. No DMA, timer or cache writes. */
#include <string.h>
#define MAGIC UINT32_C(0x52535031)
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

#ifdef KUI_REPORT_SCI_PIO_TEST
extern uint32_t kui_report_sci_pio_test_read(uint32_t,unsigned);
extern void kui_report_sci_pio_test_write(uint32_t,uint32_t,unsigned);
extern uint32_t kui_report_sci_pio_test_sr(void);
extern void kui_report_sci_pio_test_set_sr(uint32_t);
extern void kui_report_sci_pio_test_delay(uint32_t);
#define READ(a,w) kui_report_sci_pio_test_read(a,w)
#define WRITE(a,v,w) kui_report_sci_pio_test_write(a,v,w)
#define report_pio_sr_get kui_report_sci_pio_test_sr
#define report_pio_sr_set kui_report_sci_pio_test_set_sr
#define report_pio_delay kui_report_sci_pio_test_delay
#else
#define READ(a,w) ((w)==1u?*(volatile uint8_t *)(uintptr_t)(a): \
    (w)==2u?*(volatile uint16_t *)(uintptr_t)(a):*(volatile uint32_t *)(uintptr_t)(a))
static void report_pio_write_mmio(uint32_t a,uint32_t v,unsigned w) {
    if(w==1u) *(volatile uint8_t *)(uintptr_t)a=(uint8_t)v;
    else if(w==2u) *(volatile uint16_t *)(uintptr_t)a=(uint16_t)v;
    else *(volatile uint32_t *)(uintptr_t)a=v;
}
#define WRITE report_pio_write_mmio
static uint32_t report_pio_sr_get(void) {uint32_t v;__asm__ volatile("stc sr,%0":"=r"(v));return v;}
static void report_pio_sr_set(uint32_t v) {__asm__ volatile("ldc %0,sr"::"r"(v):"t","memory");}
static void report_pio_delay(uint32_t n) {__asm__ volatile("1: dt %0\n\tbf 1b":"+r"(n)::"t","memory");}
#endif
#define rd8(a) ((uint8_t)READ(a,1u))
#define rd16(a) ((uint16_t)READ(a,2u))
#define rd32(a) READ(a,4u)
#define wr8(a,v) WRITE(a,(uint8_t)(v),1u)
#define wr16(a,v) WRITE(a,(uint16_t)(v),2u)
#define wr32(a,v) WRITE(a,(uint32_t)(v),4u)

static bool report_pio_valid(const struct kui_report_sci_pio *s) {
    return s && s->self==(uintptr_t)s && s->magic==MAGIC && s->acquired<=1u && s->captured<=1u;
}
static int report_pio_fault(struct kui_report_sci_pio *s,unsigned why,uint32_t registers) {
    if(!s->fault) {s->detail=(uint32_t)s->stage<<16|registers;s->fault=why;}
    return (int)s->fault;
}
/* Only call after wake succeeds and while the exclusive port is still live.
 * Keep unsafe/owner/context refusals on the no-MMIO fault path above. */
static int report_pio_sci_fault(struct kui_report_sci_pio *s,unsigned why) {
    if(s->fault) return (int)s->fault;
    uint8_t status=rd8(SSR),scr=rd8(SCR);
    return report_pio_fault(s,why,(uint32_t)scr<<8|status);
}
static bool report_pio_context(uint32_t sr) {
    return (sr&UINT32_C(0x500000f0))==UINT32_C(0x400000f0);
}
static uint8_t report_pio_reverse(uint8_t b) {
    b=(uint8_t)((b>>4)|(b<<4));
    b=(uint8_t)(((b&0xccu)>>2)|((b&0x33u)<<2));
    return (uint8_t)(((b&0xaau)>>1)|((b&0x55u)<<1));
}
static bool report_pio_wait_flag(struct kui_report_sci_pio *s,uint8_t flags) {
    uint8_t status=0;
    for(unsigned n=0;n<POLLS;++n) {
        status=rd8(SSR);
        if(status&ERRORS) break;
        if((status&flags)==flags) return true;
    }
    (void)report_pio_fault(s,KUI_REPORT_SCI_PIO_TIMEOUT,(uint32_t)rd8(SCR)<<8|status);
    wr8(SCR,0);return false;
}
int kui_report_sci_pio_init(struct kui_report_sci_pio *s) {
    if(!s) return KUI_REPORT_SCI_PIO_ARGUMENT;
#ifndef KUI_REPORT_SCI_PIO_TEST
    if(((uintptr_t)s&3u) || (uintptr_t)s<UINT32_C(0xac000000) ||
       (uintptr_t)s>UINT32_C(0xad000000)-sizeof(*s))
        return KUI_REPORT_SCI_PIO_ARGUMENT;
#endif
    const uint8_t *p=(const uint8_t *)(const void *)s;
    for(size_t i=0;i<sizeof(*s);++i) if(p[i]) return KUI_REPORT_SCI_PIO_STATE;
    s->self=(uintptr_t)s;s->magic=MAGIC;return KUI_REPORT_SCI_PIO_OK;
}
bool kui_report_sci_pio_healthy(const struct kui_report_sci_pio *s) {
    return report_pio_valid(s) && s->acquired && s->captured && !s->fault;
}
int kui_report_sci_pio_acquire(struct kui_report_sci_pio *s) {
    if(!report_pio_valid(s)) return KUI_REPORT_SCI_PIO_STATE;
    if(s->fault) return (int)s->fault;
    s->stage=KUI_REPORT_SCI_PIO_STAGE_OWNER;
    if(s->acquired) return report_pio_fault(s,KUI_REPORT_SCI_PIO_STATE,0);
    s->stage=KUI_REPORT_SCI_PIO_STAGE_ACQUIRE_CONTEXT;
    if(!report_pio_context(report_pio_sr_get())) return report_pio_fault(s,KUI_REPORT_SCI_PIO_CONTEXT,0);
    /* A released reader may have restored module standby. No SCI register
     * can be sampled until that owned module is running. Record this partial
     * wake before mutation; release can restore it without a SCI snapshot. */
    s->stage=KUI_REPORT_SCI_PIO_STAGE_WAKE;
    s->standby=rd8(STB)&1u;s->captured=0u;
    if(s->standby) {
        s->acquired=1u;wr8(STB,rd8(STB)&~1u);
        if(rd8(STB)&1u) return report_pio_fault(s,KUI_REPORT_SCI_PIO_RESTORE,0);
    }
    s->stage=KUI_REPORT_SCI_PIO_STAGE_IDLE;
    uint8_t scr=rd8(SCR),status=rd8(SSR);
    if((scr&0xf4u) || (status&(RDRF|ERRORS)) || (status&(TDRE|TEND))!=(TDRE|TEND))
        return report_pio_fault(s,KUI_REPORT_SCI_PIO_IDLE,(uint32_t)scr<<8|status);
    s->scr=scr;s->smr=rd8(SMR);s->brr=rd8(BRR);s->ptr=rd8(PTR);
    s->mode=rd32(PCTR)&CS_MODE;s->data=rd16(PDTR)&CS;s->captured=1u;
    /* Ownership is recorded before the first possible hardware mutation. */
    s->acquired=1u;
    s->stage=KUI_REPORT_SCI_PIO_STAGE_SETUP;
    wr8(SCR,0);wr16(PDTR,rd16(PDTR)|CS);
    wr32(PCTR,(rd32(PCTR)&~CS_MODE)|CS_OUTPUT);
    wr8(PTR,0);wr8(SMR,0x80u);wr8(BRR,31u);report_pio_delay(1024u);
    s->slow=1u;s->selected=0u;
    if(rd8(STB)&1u) return report_pio_fault(s,KUI_REPORT_SCI_PIO_RESTORE,0);
    if(rd8(SCR) || rd8(PTR) || rd8(SMR)!=0x80u || rd8(BRR)!=31u ||
       (rd32(PCTR)&CS_MODE)!=CS_OUTPUT || !(rd16(PDTR)&CS))
        return report_pio_sci_fault(s,KUI_REPORT_SCI_PIO_RESTORE);
    return KUI_REPORT_SCI_PIO_OK;
}
int kui_report_sci_pio_release(struct kui_report_sci_pio *s) {
    if(!report_pio_valid(s)) return KUI_REPORT_SCI_PIO_STATE;
    s->stage=KUI_REPORT_SCI_PIO_STAGE_OWNER;
    if(!s->acquired) return s->fault?(int)s->fault:report_pio_fault(s,KUI_REPORT_SCI_PIO_STATE,0);
    s->stage=KUI_REPORT_SCI_PIO_STAGE_RELEASE_CONTEXT;
    if(!report_pio_context(report_pio_sr_get())) return report_pio_fault(s,KUI_REPORT_SCI_PIO_CONTEXT,0);
    if(s->captured && (rd8(STB)&1u)) {
        /* A stopped module cannot be sampled or restored. Preserve ownership
         * until restoration is verified; the caller must quarantine the loan.
         * In particular, SETUP may already have recorded this same fault. */
        s->stage=KUI_REPORT_SCI_PIO_STAGE_STANDBY;
        return report_pio_fault(s,KUI_REPORT_SCI_PIO_RESTORE,0);
    }
    if(!s->captured) {
        /* A failed wake or idle check changed only module standby. */
        s->stage=KUI_REPORT_SCI_PIO_STAGE_STANDBY;
        wr8(STB,(rd8(STB)&~1u)|s->standby);
        if((rd8(STB)&1u)!=s->standby) (void)report_pio_fault(s,KUI_REPORT_SCI_PIO_RESTORE,0);
        s->acquired=0u;return (int)s->fault;
    }
    s->stage=KUI_REPORT_SCI_PIO_STAGE_RELEASE_TEND;
    if(!s->fault) (void)report_pio_wait_flag(s,TEND);
    s->stage=KUI_REPORT_SCI_PIO_STAGE_RESTORE;
    wr8(SCR,0);wr16(PDTR,rd16(PDTR)|CS);
    uint8_t status=rd8(SSR);
    if(status&RDRF) (void)rd8(RDR);
    if(status&(RDRF|ERRORS)) wr8(SSR,status&~(RDRF|ERRORS));
    wr8(SMR,s->smr);wr8(BRR,s->brr);wr8(PTR,s->ptr);
    wr32(PCTR,(rd32(PCTR)&~CS_MODE)|s->mode);
    wr16(PDTR,(rd16(PDTR)&(uint16_t)~CS)|s->data);wr8(SCR,s->scr);
    if(rd8(SMR)!=s->smr || rd8(BRR)!=s->brr || rd8(PTR)!=s->ptr || rd8(SCR)!=s->scr ||
       (rd32(PCTR)&CS_MODE)!=s->mode || (rd16(PDTR)&CS)!=s->data)
        (void)report_pio_sci_fault(s,KUI_REPORT_SCI_PIO_RESTORE);
    s->stage=KUI_REPORT_SCI_PIO_STAGE_STANDBY;
    wr8(STB,(rd8(STB)&~1u)|s->standby);
    if((rd8(STB)&1u)!=s->standby) (void)report_pio_fault(s,KUI_REPORT_SCI_PIO_RESTORE,0);
    s->acquired=0u;s->selected=0u;return (int)s->fault;
}
static bool report_pio_owner(struct kui_report_sci_pio *s) {
    if(!report_pio_valid(s)) return false;
    s->stage=KUI_REPORT_SCI_PIO_STAGE_OWNER;
    if(!s->acquired || !s->captured) { (void)report_pio_fault(s,KUI_REPORT_SCI_PIO_STATE,0);return false; }
    s->stage=KUI_REPORT_SCI_PIO_STAGE_TRANSFER_CONTEXT;
    if(!report_pio_context(report_pio_sr_get())) { (void)report_pio_fault(s,KUI_REPORT_SCI_PIO_CONTEXT,0);return false; }
    return true;
}
static void report_pio_select_card(void *ctx,bool selected) {
    struct kui_report_sci_pio *s=ctx;
    if(!report_pio_owner(s) || (s->fault && selected)) return;
    s->stage=KUI_REPORT_SCI_PIO_STAGE_SELECT;
    if(!s->fault && !report_pio_wait_flag(s,TEND)) return;
    /* All report I/O stays masked; retain exact SR across GPIO RMW too. */
    uint32_t before=report_pio_sr_get();report_pio_sr_set(before|0xf0u);
    uint16_t data=rd16(PDTR);
    wr16(PDTR,selected?(data&(uint16_t)~CS):(data|CS));
    bool okay=((rd16(PDTR)&CS)==(selected?0u:CS));
    report_pio_sr_set(before);
    if(!okay) (void)report_pio_sci_fault(s,KUI_REPORT_SCI_PIO_RESTORE);
    else s->selected=selected?1u:0u;
}
static uint8_t report_pio_transfer(void *ctx,uint8_t data,bool slow) {
    struct kui_report_sci_pio *s=ctx;
    if(!report_pio_valid(s)) return 0xffu;
    /* Protocol token deadlines must progress even after a latched failure
     * makes every later byte a no-I/O 0xff result. */
    s->work+=slow?256u:8u;
    if(!report_pio_owner(s) || s->fault) return 0xffu;
    if(s->slow!=(slow?1u:0u)) {
        s->stage=KUI_REPORT_SCI_PIO_STAGE_SPEED;
        if(!report_pio_wait_flag(s,TEND)) return 0xffu;
        wr8(SCR,0);wr8(BRR,slow?31u:0u);report_pio_delay(1024u);
        if(rd8(BRR)!=(slow?31u:0u)) { (void)report_pio_sci_fault(s,KUI_REPORT_SCI_PIO_RESTORE);return 0xffu; }
        s->slow=slow?1u:0u;
    }
    s->stage=KUI_REPORT_SCI_PIO_STAGE_SCR;
    wr8(SCR,0x30u);
    if(rd8(SCR)!=0x30u) { (void)report_pio_sci_fault(s,KUI_REPORT_SCI_PIO_RESTORE);return 0xffu; }
    s->stage=KUI_REPORT_SCI_PIO_STAGE_TDRE;
    if(!report_pio_wait_flag(s,TDRE)) return 0xffu;
    wr8(TDR,report_pio_reverse(data));wr8(SSR,0x7cu);
    s->stage=KUI_REPORT_SCI_PIO_STAGE_RDRF;
    if(!report_pio_wait_flag(s,RDRF)) return 0xffu;
    uint8_t received=rd8(RDR);wr8(SSR,0xbcu);
    s->stage=KUI_REPORT_SCI_PIO_STAGE_CLEAR;
    uint8_t status=rd8(SSR);
    if(status&(RDRF|ERRORS)) {
        (void)report_pio_fault(s,KUI_REPORT_SCI_PIO_RESTORE,(uint32_t)rd8(SCR)<<8|status);wr8(SCR,0);return 0xffu;
    }
    s->stage=KUI_REPORT_SCI_PIO_STAGE_TEND;
    if(!report_pio_wait_flag(s,TEND)) return 0xffu;
    return report_pio_reverse(received);
}
static uint32_t report_pio_ticks(void *ctx) {
    const struct kui_report_sci_pio *s=ctx;return report_pio_valid(s)?s->work:0u;
}
int kui_report_sci_pio_bus(struct kui_report_sci_pio *s,struct kui_loader_sd_bus *out) {
    if(!report_pio_valid(s) || !out) return KUI_REPORT_SCI_PIO_ARGUMENT;
    uintptr_t a=(uintptr_t)s,b=(uintptr_t)out;
    if(a<=b?b-a<sizeof(*s):a-b<sizeof(*out)) return KUI_REPORT_SCI_PIO_ARGUMENT;
    *out=(struct kui_loader_sd_bus){s,NULL,NULL,report_pio_select_card,report_pio_transfer,report_pio_ticks,NULL};
    return KUI_REPORT_SCI_PIO_OK;
}

static uint8_t report_identity_crc7(const uint8_t packet[5]) {
    uint8_t crc=0u;
    for(unsigned i=0;i<5u;i++) {
        uint8_t value=packet[i];
        for(unsigned bit=0;bit<8u;bit++) {
            crc=(uint8_t)(crc<<1);
            if((crc^value)&0x80u)crc^=9u;
            value=(uint8_t)(value<<1);
        }
    }
    return (uint8_t)((crc<<1)|1u);
}
static uint16_t report_identity_crc16(uint16_t crc,uint8_t value) {
    uint32_t x=(crc>>8)^value;x^=x>>4;
    return (uint16_t)((crc<<8)^(x<<12)^(x<<5)^x);
}
static bool report_identity_wait(struct kui_report_sci_pio *s,struct kui_loader_sd *card,
    bool ready,uint8_t *value) {
    uint32_t start=s->work;
    /* This is a nominal 500ms wire-work ceiling plus a finite byte bound,
     * not elapsed time. Faulted byte operations still advance work. */
    for(unsigned i=0;i<1000000u;i++) {
        *value=report_pio_transfer(s,0xffu,false);
        if(!kui_report_sci_pio_healthy(s))return false;
        if(ready?*value==0xffu:*value!=0xffu)return true;
        if((uint32_t)(s->work-start)>=500u*12500u)break;
    }
    card->ready=false;return false;
}
static bool report_identity_register(struct kui_report_sci_pio *s,struct kui_loader_sd *card,
    unsigned command,uint8_t out[16]) {
    uint8_t packet[6]={(uint8_t)(0x40u|command),0,0,0,0,0},value=0xffu;
    packet[5]=report_identity_crc7(packet);bool okay=false;
    card->last_command=(uint8_t)command;card->last_response=0xffu;
    report_pio_select_card(s,true);
    if(!kui_report_sci_pio_healthy(s) || !report_identity_wait(s,card,true,&value))goto done;
    for(unsigned i=0;i<6u;i++)(void)report_pio_transfer(s,packet[i],false);
    for(unsigned i=0;i<20u;i++) {
        value=report_pio_transfer(s,0xffu,false);
        if(!(value&0x80u))break;
    }
    card->last_response=value;
    if(value || !report_identity_wait(s,card,false,&value) || value!=0xfeu)goto done;
    uint16_t crc=0u;
    for(unsigned i=0;i<16u;i++) {
        out[i]=report_pio_transfer(s,0xffu,false);crc=report_identity_crc16(crc,out[i]);
    }
    uint16_t expected=(uint16_t)report_pio_transfer(s,0xffu,false)<<8;
    expected|=report_pio_transfer(s,0xffu,false);
    okay=crc==expected && kui_report_sci_pio_healthy(s);
done:
    report_pio_select_card(s,false);(void)report_pio_transfer(s,0xffu,false);
    return okay && kui_report_sci_pio_healthy(s);
}
int kui_report_sci_pio_identity(struct kui_report_sci_pio *s,struct kui_loader_sd *card,
    uint8_t cid[16],uint64_t *sectors,uint32_t *high_capacity) {
    if(!report_pio_valid(s) || !card || !cid || !sectors || !high_capacity)
        return KUI_REPORT_SCI_PIO_ARGUMENT;
    if(!report_pio_owner(s) || s->fault)return s->fault?(int)s->fault:KUI_REPORT_SCI_PIO_STATE;
    if(!card->ready || card->slow || !card->blocks || card->blocks>(UINT64_C(1)<<32) ||
       (!card->high_capacity && card->blocks>(UINT64_C(1)<<23)) ||
       card->bus.ctx!=s || card->bus.select!=report_pio_select_card ||
       card->bus.transfer!=report_pio_transfer || card->bus.ticks!=report_pio_ticks ||
       card->bus.begin || card->bus.end || card->bus.transfer_block)
        return KUI_REPORT_SCI_PIO_ARGUMENT;
    uint8_t fresh[16],csd[16];
    if(!report_identity_register(s,card,10u,fresh) || !report_identity_register(s,card,9u,csd))
        goto failed;
    uint64_t capacity=0u;unsigned version=csd[0]>>6;
    if(version==1u && card->high_capacity) {
        uint32_t size=(uint32_t)(csd[7]&63u)<<16|(uint32_t)csd[8]<<8|csd[9];
        capacity=((uint64_t)size+1u)*1024u;
    } else if(!version && !card->high_capacity) {
        unsigned bits=csd[5]&15u,mult=((csd[9]&3u)<<1)|(csd[10]>>7);
        uint32_t size=(uint32_t)(csd[6]&3u)<<10|(uint32_t)csd[7]<<2|(csd[8]>>6);
        if(bits<9u || bits>11u)goto failed;
        capacity=(size+1u)<<(mult+2u+bits-9u);
    } else goto failed;
    if(capacity!=card->blocks)goto failed;
    unsigned zero=0u,ones=0u;
    for(unsigned i=0;i<16u;i++){zero+=fresh[i]==0u;ones+=fresh[i]==0xffu;}
    if(zero==16u || ones==16u)goto failed;
    memcpy(cid,fresh,16u);*sectors=capacity;*high_capacity=card->high_capacity?1u:0u;
    return KUI_REPORT_SCI_PIO_OK;
failed:
    card->ready=false;
    return report_pio_fault(s,KUI_REPORT_SCI_PIO_IDENTITY,0u);
}
