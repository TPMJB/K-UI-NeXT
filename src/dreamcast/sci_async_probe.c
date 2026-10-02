/* SPDX-License-Identifier: GPL-3.0-only
 * Original runtime receive-only SCI reader and diagnostic client. Register meanings:
 * Renesas SH7750 Hardware Manual Rev7.02, sections 14, 15 and 17; SH7091
 * SCSPTR mapping also corroborated by Linux v2.6.32 drivers/serial/sh-sci.h.
 * No external driver implementation is incorporated here.
 */
#include "kui/sci_async_probe.h"
#include "../loader/sd_reader.h"
#include "../loader/sci_sd_bus.h"
#include <stddef.h>
#include <string.h>

#ifdef KUI_SCI_ASYNC_PROBE_TEST
#include "../../tests/sci_async_probe_test_support.h"
#else
#include <kos/irq.h>
#include <kos/timer.h>
#endif

#define SMR UINT32_C(0xffe00000)
#define BRR UINT32_C(0xffe00004)
#define SCR UINT32_C(0xffe00008)
#define SSR UINT32_C(0xffe00010)
#define RDR UINT32_C(0xffe00014)
#define SCMR UINT32_C(0xffe00018)
#define SPTR UINT32_C(0xffe0001c)
#define STBCR UINT32_C(0xffc00004)
#define PDTR UINT32_C(0xff800030)
#define SAR UINT32_C(0xffa00010)
#define DAR UINT32_C(0xffa00014)
#define TCR UINT32_C(0xffa00018)
#define CHCR UINT32_C(0xffa0001c)
#define DMAOR UINT32_C(0xffa00040)
#define RX_DMA UINT32_C(0x4915)
#define FLAGS 0x38u
#define ORER 0x20u
#define RDRF 0x40u
#define SENTINEL 0xa7u
#define TRIAL_TIMEOUT_US UINT64_C(50000)
#define FRAME_TIMEOUT_US UINT64_C(100000)
#define MAX_POLLS 200000u
#define HANDOFF_POLLS 8u
#define FRAMING_QUANTUM 8u

#ifdef KUI_SCI_ASYNC_PROBE_TEST
#define rd(a,w) kui_sci_async_test_read(a,w)
#define wr(a,v,w) kui_sci_async_test_write(a,v,w)
#else
static uint32_t rd(uint32_t a, unsigned w) {
    if(w==1) return *(volatile uint8_t *)(uintptr_t)a;
    if(w==2) return *(volatile uint16_t *)(uintptr_t)a;
    return *(volatile uint32_t *)(uintptr_t)a;
}
static void wr(uint32_t a, uint32_t v, unsigned w) {
    if(w==1) *(volatile uint8_t *)(uintptr_t)a=(uint8_t)v;
    else if(w==2) *(volatile uint16_t *)(uintptr_t)a=(uint16_t)v;
    else *(volatile uint32_t *)(uintptr_t)a=v;
}
#endif
static void settle(unsigned n) {
#ifdef KUI_SCI_ASYNC_PROBE_TEST
    kui_sci_async_test_delay(n);
#else
    __asm__ __volatile__("1: dt %0\n\tbf 1b" : "+r"(n) : : "t", "memory");
#endif
}
static void cache(void *p, size_t n, bool invalidate) {
#ifdef KUI_SCI_ASYNC_PROBE_TEST
    if(invalidate) kui_sci_async_test_cache_invalidate(p,n);
    else kui_sci_async_test_cache_purge(p,n);
#else
    uintptr_t first=((uintptr_t)p&UINT32_C(0x1fffffff))|UINT32_C(0x80000000);
    for(uintptr_t a=first;a<first+n;a+=32) {
        if(invalidate) __asm__ __volatile__("ocbi @%0" : : "r"(a) : "memory");
        else __asm__ __volatile__("ocbp @%0" : : "r"(a) : "memory");
    }
#endif
}
static uint32_t physical(void *p, size_t n) {
#ifdef KUI_SCI_ASYNC_PROBE_TEST
    return kui_sci_async_test_dma_address(p,n);
#else
    uintptr_t a=(uintptr_t)p;
    if((a&31u) || ((a>>24)!=0x8cu && (a>>24)!=0xacu)) return 0;
    a&=UINT32_C(0x1fffffff);
    return a<=UINT32_C(0x0d000000)-n?(uint32_t)a:0;
#endif
}
static uint8_t reverse_byte(uint8_t x) {
    x=(uint8_t)(((x>>1)&0x55u)|((x&0x55u)<<1));
    x=(uint8_t)(((x>>2)&0x33u)|((x&0x33u)<<2));
    return (uint8_t)((x>>4)|(x<<4));
}
static uint16_t crc_byte(uint16_t c, uint8_t b) {
    uint32_t x=(c>>8)^b; x^=x>>4;
    return (uint16_t)((c<<8)^(x<<12)^(x<<5)^x);
}
static uint8_t command_crc(const uint8_t *p, unsigned n) {
    unsigned c=0;
    for(unsigned i=0;i<n;++i) for(unsigned j=0;j<8;++j) {
        c<<=1; if(((p[i]>>(7-j))^(c>>7))&1u) c^=9u;
    }
    return (uint8_t)((c<<1)|1u);
}

static const irq_t events[3]={EXC_DMAC_DMTE1,EXC_SCI_ERI,EXC_SCI_RXI};
struct receive_area {
    uint8_t before[32], bytes[544], after[32];
} __attribute__((aligned(32)));
enum reader_phase { READER_IDLE, READER_FRAMING, READER_DMA, READER_READY, READER_FAILED };
struct probe {
    struct receive_area rx;
    struct kui_sci_async_probe_result *out;
    struct kui_sci_async_stage *stage;
    irq_cb_t previous[3];
    uint32_t sar,dar,tcr,chcr;
    uint8_t smr,brr,scr,scmr,sptr;
    unsigned sci_priority;
    bool leased, module_unavailable;
    struct kui_sci_async_reader *owner;
    const struct kui_loader_sd *card;
    uint32_t generation, lba, polls, frame_count, buffer_address;
    uint8_t command[6];
    enum reader_phase phase;
    enum kui_sci_async_status request_status;
    bool slow, cancel_requested, payload_validated, completion_recorded;
    uint64_t opened_us, request_us, frame_us;
    uint32_t expected_sar,expected_dar,expected_tcr,expected_chcr,start_address;
    volatile bool armed,done,foreign_dma,quarantined;
    volatile uint32_t end_chcr,end_count,end_ssr,event;
    uint64_t start_us;
};
static bool occupied;
static bool poisoned;
static uint32_t next_generation;
static struct probe state;
static void interrupt(irq_t code, irq_context_t *context, void *data);

static bool active_dma_owned(const struct probe *p) {
    irq_cb_t cb=irq_get_handler(EXC_DMAC_DMTE1);
    uint32_t address=rd(DAR,4),control=rd(CHCR,4);
    return cb.hdl==interrupt && cb.data==p && rd(SAR,4)==(RDR&UINT32_C(0x1fffffff)) &&
        address>=p->start_address && address<=p->start_address+514u && rd(TCR,4)<=514u &&
        (control&~UINT32_C(7))==(RX_DMA&~UINT32_C(7));
}

static void max_time(uint64_t *max, uint64_t start) {
    uint64_t n=timer_us_gettime64()-start; if(n>*max) *max=n;
}
static void unmask(struct probe *p, irq_mask_t mask, uint64_t start) {
    max_time(&p->out->max_irq_masked_us,start); irq_restore(mask);
}

/* Stop the source before the channel. DE/IE are cleared while preserving an
 * already-set TE, then TE is acknowledged. The readback/settle is NOT treated
 * as an abort-drain acknowledgement: only TE+count0 proves completion. Without
 * that evidence, quarantine the persistent buffer and channel until restart.
 */
static void freeze(struct probe *p) {
    p->stage->snapshot_ssr=rd(SSR,1);
    p->stage->snapshot_sptr=rd(SPTR,1);
    wr(SCR,0,1);
    if(!active_dma_owned(p)) {p->foreign_dma=true;return;}
    uint32_t control=rd(CHCR,4);
    wr(CHCR,control&~UINT32_C(5),4);
    (void)rd(CHCR,4);
    settle(64);
    p->end_chcr=rd(CHCR,4);
    p->end_count=rd(TCR,4);
    p->end_ssr=rd(SSR,1);
    p->stage->snapshot_ssr=p->end_ssr;
    p->stage->snapshot_sptr=rd(SPTR,1);
    if(p->end_count || !(p->end_chcr&2u)) p->quarantined=true;
    if(!p->quarantined) {
        wr(CHCR,0,4);
        p->expected_sar=rd(SAR,4);p->expected_dar=rd(DAR,4);
        p->expected_tcr=rd(TCR,4);p->expected_chcr=0;
    }
    if(p->end_ssr&RDRF) (void)rd(RDR,1);
    wr(SSR,p->end_ssr&~(RDRF|FLAGS),1);
    (void)rd(SSR,1);
}
static void interrupt(irq_t code, irq_context_t *context, void *data) {
    (void)context;
    struct probe *p=data;
    /* A stale invocation must not touch a restored/foreign channel. */
    if(!p->armed) return;
    uint64_t start=timer_us_gettime64();
    p->armed=false;
    freeze(p);
    p->event=(uint32_t)code;
    if(code==EXC_DMAC_DMTE1) ++p->stage->dma_irqs;
    else if(code==EXC_SCI_ERI) ++p->stage->sci_error_irqs;
    else ++p->stage->unexpected_rx_irqs;
    p->done=true;
    max_time(&p->out->max_irq_handler_us,start);
}

static enum kui_sci_async_status lease(struct probe *p) {
    uint64_t start=timer_us_gettime64(); irq_mask_t mask=irq_disable();
    enum kui_sci_async_status status=KUI_SCI_ASYNC_OK;
    unsigned priority=irq_get_priority(IRQ_SRC_DMAC);
    uint32_t dma=rd(DMAOR,4), control=rd(CHCR,4), ssr=rd(SSR,1);
    uint8_t scr=(uint8_t)rd(SCR,1);
    uint8_t sptr=(uint8_t)rd(SPTR,1);
    p->stage->last_phase=KUI_SCI_ASYNC_PHASE_LEASE;
    p->stage->snapshot_ssr=ssr;p->stage->snapshot_sptr=sptr;
    if(control&7u) status=KUI_SCI_ASYNC_BUSY;
    else if((mask&UINT32_C(0x100000f0)) || irq_inside_int() || !priority ||
            (dma&7u)!=1u || (scr&0xc4u) || (ssr&(RDRF|FLAGS)) ||
            (ssr&0x84u)!=0x84u || !(rd(PDTR,2)&0x80u) ||
            /* SPTR pin reads cannot recover another owner's output latches. */
            rd(SMR,1)!=0x80u || rd(SCMR,1)!=0 || rd(BRR,1)!=0 || (sptr&0x0au))
        status=KUI_SCI_ASYNC_UNSUPPORTED;
    if(status!=KUI_SCI_ASYNC_OK) {unmask(p,mask,start);return status;}
    p->sar=rd(SAR,4);p->dar=rd(DAR,4);p->tcr=rd(TCR,4);p->chcr=control;
    p->expected_sar=p->sar;p->expected_dar=p->dar;p->expected_tcr=p->tcr;p->expected_chcr=p->chcr;
    p->smr=(uint8_t)rd(SMR,1);p->brr=(uint8_t)rd(BRR,1);p->scr=scr;
    p->scmr=(uint8_t)rd(SCMR,1);p->sptr=sptr;
    p->sci_priority=irq_get_priority(IRQ_SRC_SCI1);
    for(unsigned i=0;i<3;++i) p->previous[i]=irq_get_handler(events[i]);
    unsigned installed=0;
    for(;installed<3;++installed) if(irq_set_handler(events[installed],interrupt,p)) break;
    if(installed!=3) {
        bool restored=true;
        while(installed) {
            --installed;
            if(irq_set_handler(events[installed],p->previous[installed].hdl,p->previous[installed].data)) restored=false;
            irq_cb_t current=irq_get_handler(events[installed]);
            if(current.hdl!=p->previous[installed].hdl || current.data!=p->previous[installed].data) restored=false;
        }
        p->out->handlers_restored=p->out->safe_restored=restored;
        p->out->registers_restored=true;
        if(!restored) poisoned=true;
        unmask(p,mask,start);return restored?KUI_SCI_ASYNC_UNSUPPORTED:KUI_SCI_ASYNC_RESTORE;
    }
    irq_set_priority(IRQ_SRC_SCI1,priority);
    p->leased=true;
    unmask(p,mask,start);return KUI_SCI_ASYNC_OK;
}

static bool same_callback(irq_cb_t a, irq_cb_t b) {return a.hdl==b.hdl && a.data==b.data;}
static bool dma_unchanged(const struct probe *p) {
    irq_cb_t current=irq_get_handler(EXC_DMAC_DMTE1);
    return current.hdl==interrupt && current.data==p &&
        rd(CHCR,4)==p->expected_chcr && rd(SAR,4)==p->expected_sar &&
        rd(DAR,4)==p->expected_dar && rd(TCR,4)==p->expected_tcr;
}
static void release(struct probe *p) {
    if(!p->leased) return;
    uint64_t start=timer_us_gettime64(); irq_mask_t mask=irq_disable();
    if(p->armed) {p->armed=false;freeze(p);}
    if(!p->quarantined && !dma_unchanged(p)) p->foreign_dma=true;
    /* No source is enabled when the prior callback becomes visible again. */
    if(!p->module_unavailable) wr(SCR,0,1);
    if(!p->foreign_dma && !p->quarantined) {wr(CHCR,0,4);(void)rd(CHCR,4);settle(64);}
    if(!p->module_unavailable) {
        uint8_t status=(uint8_t)rd(SSR,1);
        if(status&RDRF) (void)rd(RDR,1);
        wr(SSR,status&~(RDRF|FLAGS),1);
    }
    if(!p->foreign_dma && !p->quarantined) {
        wr(SAR,p->sar,4);wr(DAR,p->dar,4);wr(TCR,p->tcr,4);wr(CHCR,p->chcr,4);
    }
    if(!p->module_unavailable) {
        wr(SMR,p->smr,1);wr(BRR,p->brr,1);wr(SCMR,p->scmr,1);wr(SPTR,p->sptr,1);
        settle(1024);
    }
    bool handlers=true;
    for(unsigned i=0;i<3;++i) {
        irq_cb_t current=irq_get_handler(events[i]);
        if(current.hdl!=interrupt || current.data!=p) {handlers=false;continue;}
        if(irq_set_handler(events[i],p->previous[i].hdl,p->previous[i].data)) handlers=false;
        if(!same_callback(irq_get_handler(events[i]),p->previous[i])) handlers=false;
    }
    irq_set_priority(IRQ_SRC_SCI1,p->sci_priority);
    handlers=handlers && irq_get_priority(IRQ_SRC_SCI1)==p->sci_priority;
    if(!p->module_unavailable) wr(SCR,p->scr,1);
    bool registers=!p->module_unavailable && !p->foreign_dma && !p->quarantined && rd(SAR,4)==p->sar && rd(DAR,4)==p->dar && rd(TCR,4)==p->tcr &&
        rd(CHCR,4)==p->chcr && rd(SMR,1)==p->smr && rd(BRR,1)==p->brr &&
        rd(SCR,1)==p->scr && rd(SCMR,1)==p->scmr &&
        (rd(SPTR,1)&0x8au)==(p->sptr&0x8au);
    p->out->handlers_restored=handlers;p->out->registers_restored=registers;
    p->out->dma_quarantined=p->quarantined;p->out->foreign_dma=p->foreign_dma;
    /* The ordinary bus also caches BRR's speed. A slow-only session must
     * reconcile that software state after restoring the saved fast BRR.
     * An already faulted bus remains latched until its normal reacquire. */
    bool speed_synced=true;
    if(handlers && registers && kui_sci_sd_healthy()) speed_synced=kui_sci_sd_resync_speed();
    p->out->safe_restored=handlers && registers && speed_synced && !(rd(CHCR,4)&7u) &&
        !(rd(SSR,1)&(RDRF|FLAGS)) && (rd(DMAOR,4)&7u)==1u;
    poisoned=!p->out->safe_restored;p->leased=false;
    unmask(p,mask,start);
}

/* A completed, validated CMD17 with trailing overrun is the only reset
 * candidate. Renesas 9.2.1/9.6 and 15.1.4 document MSTP0 as SCI-only module
 * standby and initialization of SCI registers except SPTR. This experiment
 * tests whether that stronger reset restores the receiver after ORER; the
 * console has not established its internal cause. No DMA abort is inferred.
 */
static enum kui_sci_async_status module_reset(struct probe *p) {
    struct kui_sci_async_stage *s=p->stage;
    ++s->module_reset_attempts;
    s->module_reset_state=KUI_SCI_ASYNC_MODULE_RESET_PRECONDITION;
    uint64_t start=timer_us_gettime64();irq_mask_t mask=irq_disable();
    enum kui_sci_async_status result=KUI_SCI_ASYNC_HANDOFF;
    s->module_stb_before=rd(STBCR,1);
    if(s->module_stb_before&1u) {p->module_unavailable=true;goto failed;}
    if(p->armed || p->quarantined || p->foreign_dma || !dma_unchanged(p)) {
        p->foreign_dma=true;result=KUI_SCI_ASYNC_DMA_ERROR;goto failed;
    }
    if(p->end_count || !(p->end_chcr&2u) || !(p->end_ssr&ORER) ||
       !p->payload_validated || !p->out->guards_ok || !p->out->crc_ok ||
       !kui_sci_sd_healthy() || !(rd(PDTR,2)&0x80u) || rd(SCR,1)!=0 ||
       (rd(SSR,1)&(RDRF|FLAGS)) || (rd(SPTR,1)&0x8au)!=0x82u) goto failed;
    /* Only the SCI bit changes. The short mask protects these RMWs and the
     * ownership check; DMAC, SCIF, timers and the CPU keep their clocks. */
    p->module_unavailable=true;
    wr(STBCR,s->module_stb_before|1u,1);
    bool stopped=false;
    for(unsigned n=0;n<HANDOFF_POLLS;++n) {
        s->module_stb_stopped=rd(STBCR,1);
        if(s->module_stb_stopped&1u) {stopped=true;break;}
    }
    settle(64);
    /* Always attempt the bounded resume, including an unconfirmed assert.
     * There is no SCI MMIO between gating and confirming this clear. */
    wr(STBCR,rd(STBCR,1)&~1u,1);
    for(unsigned n=0;n<HANDOFF_POLLS;++n) {
        s->module_stb_after=rd(STBCR,1);
        if(!(s->module_stb_after&1u)) {p->module_unavailable=false;break;}
    }
    if(p->module_unavailable) {
        s->module_reset_state=KUI_SCI_ASYNC_MODULE_RESET_RESUME_FAILED;goto failed;
    }
    if(!stopped) {
        s->module_reset_state=KUI_SCI_ASYNC_MODULE_RESET_ASSERT_FAILED;goto failed;
    }
    settle(64);
    uint32_t bad=0;
    if(rd(SCR,1)!=0) bad|=KUI_SCI_ASYNC_MODULE_RESET_BAD_SCR;
    if(rd(SMR,1)!=0) bad|=KUI_SCI_ASYNC_MODULE_RESET_BAD_SMR;
    if(rd(BRR,1)!=0xffu) bad|=KUI_SCI_ASYNC_MODULE_RESET_BAD_BRR;
    if(rd(SCMR,1)!=0) bad|=KUI_SCI_ASYNC_MODULE_RESET_BAD_SCMR;
    if((rd(SSR,1)&0xfcu)!=0x84u) bad|=KUI_SCI_ASYNC_MODULE_RESET_BAD_SSR;
    if((rd(SPTR,1)&0x8au)!=0x82u) bad|=KUI_SCI_ASYNC_MODULE_RESET_BAD_SPTR;
    if(bad) {
        /* Resume is proven, so SCI writes are safe again. Do not expose an
         * unexpected enabled interrupt source before cleanup can run. */
        wr(SCR,0,1);
        s->module_reset_state=KUI_SCI_ASYNC_MODULE_RESET_SIGNATURE|bad;goto failed;
    }
    s->module_reset_state=KUI_SCI_ASYNC_MODULE_RESET_OK;
    ++s->module_resets;
    unmask(p,mask,start);return KUI_SCI_ASYNC_OK;
failed:
    ++s->module_reset_failures;
    unmask(p,mask,start);return result;
}

static uint8_t byte(const struct kui_loader_sd *c, uint8_t v, bool slow) {
    return c->bus.transfer(c->bus.ctx,v,slow);
}
static bool bus_healthy(struct probe *p) {
    if(kui_sci_sd_healthy()) return true;
    ++p->stage->bus_faults;
    struct kui_sci_sd_fault fault={0};
    kui_sci_sd_fault_get(&fault);
    p->stage->bus_fault_valid=fault.valid;
    p->stage->bus_wait_flag=fault.wait_flag;
    p->stage->bus_fault_ssr=fault.ssr;p->stage->bus_fault_scr=fault.scr;
    p->stage->bus_fault_smr=fault.smr;p->stage->bus_fault_brr=fault.brr;
    p->stage->bus_fault_scmr=fault.scmr;p->stage->bus_fault_sptr=fault.sptr;
    p->stage->bus_fault_pdtr=fault.pdtr;p->stage->bus_fault_polls=fault.polls;
    p->stage->snapshot_ssr=rd(SSR,1);
    p->stage->snapshot_sptr=rd(SPTR,1);
    return false;
}
/* Each call spends at most FRAMING_QUANTUM byte operations. The ordinary
 * bus already bounds a byte's hardware waits; card-ready and token budgets
 * persist across calls, so a caller can yield between every quantum. */
static enum kui_sci_async_status frame_poll(struct probe *p) {
    const struct kui_loader_sd *c=p->card;
    struct kui_sci_async_stage *s=p->stage;
    if(!dma_unchanged(p)) {p->foreign_dma=true;return KUI_SCI_ASYNC_DMA_ERROR;}
    for(unsigned budget=0;budget<FRAMING_QUANTUM;++budget) {
        s->framing_index=p->frame_count;
        if(s->last_phase==KUI_SCI_ASYNC_PHASE_TOKEN_END) {
            uint32_t ssr=rd(SSR,1);
            if(ssr&FLAGS) return KUI_SCI_ASYNC_RECEIVE_ERROR;
            if(ssr&4u) return KUI_SCI_ASYNC_OK;
            if(++p->frame_count>=10000u || timer_us_gettime64()-p->frame_us>=FRAME_TIMEOUT_US)
                return KUI_SCI_ASYNC_TIMEOUT;
            continue;
        }
        uint8_t value=0xff;
        switch(s->framing_step) {
            case KUI_SCI_ASYNC_FRAMING_DESELECT:
                c->bus.select(c->bus.ctx,false);
                if(!bus_healthy(p)) return KUI_SCI_ASYNC_BUS_FAULT;
                s->framing_step=KUI_SCI_ASYNC_FRAMING_IDLE_CLOCK;break;
            case KUI_SCI_ASYNC_FRAMING_IDLE_CLOCK:
                (void)byte(c,0xff,p->slow);
                if(!bus_healthy(p)) return KUI_SCI_ASYNC_BUS_FAULT;
                s->framing_step=KUI_SCI_ASYNC_FRAMING_SELECT;break;
            case KUI_SCI_ASYNC_FRAMING_SELECT:
                c->bus.select(c->bus.ctx,true);
                if(!bus_healthy(p)) return KUI_SCI_ASYNC_BUS_FAULT;
                s->framing_step=KUI_SCI_ASYNC_FRAMING_READY;
                p->frame_count=0;p->frame_us=timer_us_gettime64();break;
            case KUI_SCI_ASYNC_FRAMING_READY:
                value=byte(c,0xff,p->slow);
                if(!bus_healthy(p)) return KUI_SCI_ASYNC_BUS_FAULT;
                if(value==0xff) {
                    s->last_phase=KUI_SCI_ASYNC_PHASE_COMMAND;
                    s->framing_step=KUI_SCI_ASYNC_FRAMING_COMMAND;p->frame_count=0;
                } else if(++p->frame_count>=4096u || timer_us_gettime64()-p->frame_us>=FRAME_TIMEOUT_US)
                    return KUI_SCI_ASYNC_TIMEOUT;
                break;
            case KUI_SCI_ASYNC_FRAMING_COMMAND:
                (void)byte(c,p->command[p->frame_count],p->slow);
                if(!bus_healthy(p)) return KUI_SCI_ASYNC_BUS_FAULT;
                if(++p->frame_count==6) {s->framing_step=KUI_SCI_ASYNC_FRAMING_RESPONSE;p->frame_count=0;}
                break;
            case KUI_SCI_ASYNC_FRAMING_RESPONSE:
                value=byte(c,0xff,p->slow);s->command_response=value;
                if(!bus_healthy(p)) return KUI_SCI_ASYNC_BUS_FAULT;
                if(!(value&0x80u)) {
                    if(value) return KUI_SCI_ASYNC_COMMAND;
                    s->last_phase=KUI_SCI_ASYNC_PHASE_TOKEN;
                    s->framing_step=KUI_SCI_ASYNC_FRAMING_TOKEN;
                    p->frame_count=0;p->frame_us=timer_us_gettime64();
                } else if(++p->frame_count>=16u) return KUI_SCI_ASYNC_COMMAND;
                break;
            case KUI_SCI_ASYNC_FRAMING_TOKEN:
                value=byte(c,0xff,p->slow);s->last_token=value;
                if(!bus_healthy(p)) return KUI_SCI_ASYNC_BUS_FAULT;
                if(value==0xfe) {
                    s->last_phase=KUI_SCI_ASYNC_PHASE_TOKEN_END;
                    p->frame_count=0;p->frame_us=timer_us_gettime64();
                } else if(value!=0xff) return KUI_SCI_ASYNC_TOKEN;
                else if(++p->frame_count>=8192u || timer_us_gettime64()-p->frame_us>=FRAME_TIMEOUT_US)
                    return KUI_SCI_ASYNC_TIMEOUT;
                break;
            default: return KUI_SCI_ASYNC_ARGUMENT;
        }
    }
    return KUI_SCI_ASYNC_PENDING;
}
/* The normal bus latches errors even in select(false)'s TEND wait. Establish
 * a clean, stopped SCI before invoking it: a completed RX DMA does not by
 * itself establish that its trailing RDRF/ORER was cleared. The manual's
 * synchronous error flow (15.3.4) checks ORER again after clearing it.
 * This bounded foreground step is not an incomplete-DMA drain or recovery.
 */
static enum kui_sci_async_status handoff(struct probe *p,
        const struct kui_loader_sd *c, bool slow) {
    struct kui_sci_async_stage *s=p->stage;
    s->last_phase=KUI_SCI_ASYNC_PHASE_HANDOFF;
    ++s->handoff_checks;
    if(p->armed || p->quarantined || p->foreign_dma || !dma_unchanged(p)) {
        p->foreign_dma=true;return KUI_SCI_ASYNC_DMA_ERROR;
    }
    bool clean=false;
    for(unsigned n=0;n<HANDOFF_POLLS;++n) {
        s->handoff_scr=rd(SCR,1);s->handoff_ssr=rd(SSR,1);
        s->handoff_sptr=rd(SPTR,1);
        if(s->handoff_scr&0xf4u) break;
        if(!(s->handoff_ssr&(RDRF|FLAGS)) && (s->handoff_ssr&0x84u)==0x84u) {
            clean=true;break;
        }
        ++s->handoff_retries;
        if(s->handoff_ssr&RDRF) (void)rd(RDR,1);
        wr(SSR,s->handoff_ssr&~(RDRF|FLAGS),1);
        settle(64);
    }
    if(!clean) {++s->handoff_failures;return KUI_SCI_ASYNC_HANDOFF;}
    s->framing_step=KUI_SCI_ASYNC_FRAMING_HANDOFF;s->framing_index=0;
    c->bus.select(c->bus.ctx,false);
    if(!bus_healthy(p)) return KUI_SCI_ASYNC_BUS_FAULT;
    if(!(rd(PDTR,2)&0x80u)) {++s->handoff_failures;return KUI_SCI_ASYNC_HANDOFF;}
    if(p->end_ssr&ORER) {
        enum kui_sci_async_status reset=module_reset(p);
        if(reset!=KUI_SCI_ASYNC_OK) {++s->handoff_failures;return reset;}
    }
    /* Re-establish the documented synchronous initialization while CS is
     * high. Keep the current BRR so the normal bus's speed cache agrees.
     * TE and RE are enabled together; no transmit data is queued here.
     * Reinitializing at this boundary is experimental, not proof that the
     * previous console's unresponsive CMD17 was caused by the receiver. */
    wr(SCR,0,1);wr(SCMR,p->scmr,1);wr(SMR,p->smr,1);wr(BRR,slow?31u:0u,1);
    settle(1024);
    wr(SPTR,p->sptr,1);
    wr(SCR,0x30u,1);
    s->handoff_scr=rd(SCR,1);s->handoff_ssr=rd(SSR,1);
    s->handoff_sptr=rd(SPTR,1);
    if(s->handoff_scr!=0x30u || rd(SMR,1)!=p->smr || rd(BRR,1)!=(slow?31u:0u) ||
       (s->handoff_sptr&0x8au)!=(p->sptr&0x8au) ||
       (s->handoff_ssr&(RDRF|FLAGS)) || (s->handoff_ssr&0x84u)!=0x84u) {
        ++s->handoff_failures;return KUI_SCI_ASYNC_HANDOFF;
    }
    return KUI_SCI_ASYNC_OK;
}
static bool guards(const struct receive_area *r) {
    for(unsigned i=0;i<32;++i) if(r->before[i]!=SENTINEL || r->after[i]!=SENTINEL) return false;
    for(unsigned i=514;i<544;++i) if(r->bytes[i]!=SENTINEL) return false;
    return true;
}

static struct probe *reader_state(const struct kui_sci_async_reader *reader) {
    return reader && occupied && state.leased && state.owner==reader &&
        reader->generation && reader->generation==state.generation?&state:NULL;
}
static enum kui_sci_async_status measured(struct probe *p,uint64_t *max,
        uint64_t start,enum kui_sci_async_status status) {
    uint64_t elapsed=timer_us_gettime64()-start;
    if(elapsed>*max) *max=elapsed;
    if(elapsed>p->out->max_call_us) p->out->max_call_us=elapsed;
    return status;
}
static enum kui_sci_async_status failed(struct probe *p,enum kui_sci_async_status status) {
    if(!p->module_unavailable && p->stage->last_phase<KUI_SCI_ASYNC_PHASE_DMA &&
       p->stage->last_phase!=KUI_SCI_ASYNC_PHASE_GPIO) {
        p->stage->snapshot_ssr=rd(SSR,1);p->stage->snapshot_sptr=rd(SPTR,1);
    }
    p->phase=READER_FAILED;p->request_status=status;
    p->out->status=p->out->operation_status=status;
    return status;
}
static enum kui_sci_async_status arm(struct probe *p) {
    struct kui_sci_async_stage *s=p->stage;
    settle(p->slow?1024u:64u);
    uint64_t masked_start=timer_us_gettime64();irq_mask_t mask=irq_disable();
    s->last_phase=KUI_SCI_ASYNC_PHASE_OWNERSHIP;
    if(!dma_unchanged(p)) {
        p->foreign_dma=true;unmask(p,mask,masked_start);return KUI_SCI_ASYNC_BUSY;
    }
    /* Preload TxD's GPIO latch high before TE is cleared. SPTR reads return
     * RxD/SCK pin levels even when output is selected (manual 15.2.8), so only
     * EIO and direction controls can be verified. SPB1IO remains zero. */
    s->last_phase=KUI_SCI_ASYNC_PHASE_GPIO;
    wr(SPTR,0x83u,1);
    uint32_t port_value=rd(SPTR,1);
    s->snapshot_sptr=port_value;s->snapshot_ssr=rd(SSR,1);
    if((port_value&0x8au)!=0x82u) {unmask(p,mask,masked_start);return KUI_SCI_ASYNC_UNSUPPORTED;}
    wr(SCR,0,1);
    wr(CHCR,0,4);wr(SAR,RDR&UINT32_C(0x1fffffff),4);wr(DAR,p->buffer_address,4);wr(TCR,514,4);
    p->start_address=p->buffer_address;
    p->done=false;p->event=0;p->end_count=514;p->end_chcr=0;p->end_ssr=0;
    p->start_us=timer_us_gettime64();p->armed=true;
    s->last_phase=KUI_SCI_ASYNC_PHASE_DMA;++s->dma_started;
    wr(CHCR,RX_DMA,4);
    wr(SCR,0x50u,1); /* RIE + RE, no transmitter/dummy-byte CPU loop. */
    unmask(p,mask,masked_start);
    p->phase=READER_DMA;
    return KUI_SCI_ASYNC_PENDING;
}
static void completion(struct probe *p) {
    if(p->completion_recorded) return;
    p->completion_recorded=true;
    uint64_t receive=timer_us_gettime64()-p->start_us;
    p->stage->receive_us+=receive;
    if(receive>p->stage->max_receive_us) p->stage->max_receive_us=receive;
    p->stage->last_remaining=p->end_count;p->stage->last_chcr=p->end_chcr;
    p->stage->last_ssr=p->end_ssr;
}
static enum kui_sci_async_status finish_request(struct probe *p,uint8_t *dst,const uint8_t *expected) {
    struct kui_sci_async_stage *s=p->stage;
    if(!dma_unchanged(p)) {p->foreign_dma=true;return failed(p,KUI_SCI_ASYNC_DMA_ERROR);}
    s->last_phase=KUI_SCI_ASYNC_PHASE_VALIDATE;
    cache(&p->rx,sizeof(p->rx),true);
    if(!guards(&p->rx)) return failed(p,KUI_SCI_ASYNC_GUARD);
    p->out->guards_ok=true;
    uint16_t crc=0;bool equal=true;
    for(unsigned i=0;i<512;++i) {
        uint8_t value=reverse_byte(p->rx.bytes[i]);
        p->rx.bytes[i]=value;crc=crc_byte(crc,value);
        if(expected && value!=expected[i]) equal=false;
    }
    uint16_t wire_crc=(uint16_t)((uint16_t)reverse_byte(p->rx.bytes[512])<<8)|reverse_byte(p->rx.bytes[513]);
    if(crc!=wire_crc) return failed(p,KUI_SCI_ASYNC_CRC);
    p->out->crc_ok=true;
    p->out->baseline_checked=expected!=NULL;
    p->out->baseline_ok=expected && equal;
    if(!equal) return failed(p,KUI_SCI_ASYNC_MISMATCH);
    p->payload_validated=true;
    if(p->end_ssr&ORER) ++s->trailing_overruns;
    enum kui_sci_async_status result=handoff(p,p->card,p->slow);
    if(result!=KUI_SCI_ASYNC_OK) return failed(p,result);
    s->last_phase=KUI_SCI_ASYNC_PHASE_COMPLETE;
    s->elapsed_us+=timer_us_gettime64()-p->request_us;
    if(p->cancel_requested) return failed(p,KUI_SCI_ASYNC_CANCELLED);
    if(dst) memcpy(dst,p->rx.bytes,512);
    ++s->passed;p->phase=READER_IDLE;p->request_status=KUI_SCI_ASYNC_OK;
    return KUI_SCI_ASYNC_OK;
}

enum kui_sci_async_status kui_sci_async_open(struct kui_sci_async_reader *reader,
        const struct kui_loader_sd *c,struct kui_sci_async_probe_result *out) {
    if(!reader || !out) return KUI_SCI_ASYNC_ARGUMENT;
    /* Never overwrite the result or handle of a currently live lease. */
    if(occupied && (reader==state.owner || out==state.out)) return KUI_SCI_ASYNC_BUSY;
    *out=(struct kui_sci_async_probe_result){.status=KUI_SCI_ASYNC_ARGUMENT,
        .operation_status=KUI_SCI_ASYNC_ARGUMENT};
    reader->generation=0;
    if(!c || !c->ready || c->slow || !c->bus.select || !c->bus.transfer || !c->blocks)
        return out->status;
    uint64_t start=timer_us_gettime64();
    uint64_t reservation_start=timer_us_gettime64();irq_mask_t mask=irq_disable();
    if(occupied || poisoned) {
        irq_restore(mask);out->status=out->operation_status=KUI_SCI_ASYNC_BUSY;return out->status;
    }
    occupied=true;memset(&state,0,sizeof(state));
    struct probe *p=&state;p->out=out;p->card=c;p->owner=reader;
    if(!++next_generation) ++next_generation;
    reader->generation=p->generation=next_generation;
    p->opened_us=start;p->phase=READER_IDLE;
    max_time(&out->max_irq_masked_us,reservation_start);irq_restore(mask);
    out->slow.clock_hz=390625;out->fast.clock_hz=12500000;
    out->slow.command_response=out->slow.last_token=0xffu;
    out->fast.command_response=out->fast.last_token=0xffu;
    p->stage=&out->slow;
    out->status=lease(p);out->operation_status=out->status;
    if(out->status!=KUI_SCI_ASYNC_OK) {
        if(out->status!=KUI_SCI_ASYNC_RESTORE)
            out->safe_restored=out->handlers_restored=out->registers_restored=true;
        mask=irq_disable();occupied=false;reader->generation=0;irq_restore(mask);
        out->elapsed_us=timer_us_gettime64()-start;
    } else out->started=true;
    return measured(p,&out->max_open_us,start,out->status);
}
enum kui_sci_async_status kui_sci_async_begin(struct kui_sci_async_reader *reader,uint32_t lba,bool slow) {
    struct probe *p=reader_state(reader);if(!p) return KUI_SCI_ASYNC_ARGUMENT;
    uint64_t start=timer_us_gettime64();
    enum kui_sci_async_status result=KUI_SCI_ASYNC_OK;
    if(p->phase!=READER_IDLE) result=KUI_SCI_ASYNC_BUSY;
    else if((uint64_t)lba>=p->card->blocks || (!p->card->high_capacity && lba>UINT32_MAX/512u))
        result=KUI_SCI_ASYNC_ARGUMENT;
    else if(!dma_unchanged(p)) {p->foreign_dma=true;result=failed(p,KUI_SCI_ASYNC_DMA_ERROR);}
    else {
        p->stage=slow?&p->out->slow:&p->out->fast;
        struct kui_sci_async_stage *s=p->stage;
        p->lba=lba;p->slow=slow;p->out->lba=lba;p->request_us=start;
        p->polls=p->frame_count=0;p->cancel_requested=false;
        p->payload_validated=p->completion_recorded=false;
        p->out->guards_ok=p->out->crc_ok=p->out->baseline_ok=p->out->baseline_checked=false;
        s->last_phase=KUI_SCI_ASYNC_PHASE_BUFFER;++s->attempted;
        s->command_response=s->last_token=0xff;
        memset(&p->rx,SENTINEL,sizeof(p->rx));
        p->buffer_address=physical(p->rx.bytes,sizeof(p->rx.bytes));
        if(!p->buffer_address) result=failed(p,KUI_SCI_ASYNC_UNSUPPORTED);
        else {
            cache(&p->rx,sizeof(p->rx),false);
            uint32_t address=p->card->high_capacity?lba:lba*512u;
            p->command[0]=0x51;p->command[1]=(uint8_t)(address>>24);
            p->command[2]=(uint8_t)(address>>16);p->command[3]=(uint8_t)(address>>8);
            p->command[4]=(uint8_t)address;p->command[5]=command_crc(p->command,5);
            s->last_phase=KUI_SCI_ASYNC_PHASE_READY;
            s->framing_step=KUI_SCI_ASYNC_FRAMING_DESELECT;s->framing_index=0;
            p->phase=READER_FRAMING;p->request_status=KUI_SCI_ASYNC_PENDING;
        }
    }
    return measured(p,&p->out->max_begin_us,start,result);
}
enum kui_sci_async_status kui_sci_async_poll(struct kui_sci_async_reader *reader) {
    struct probe *p=reader_state(reader);if(!p) return KUI_SCI_ASYNC_ARGUMENT;
    uint64_t start=timer_us_gettime64();
    enum kui_sci_async_status result=KUI_SCI_ASYNC_PENDING;
    if(p->phase==READER_FRAMING) {
        result=frame_poll(p);
        if(result==KUI_SCI_ASYNC_OK) result=arm(p);
        if(result!=KUI_SCI_ASYNC_PENDING) result=failed(p,result);
    } else if(p->phase==READER_DMA) {
        bool timeout=false,dma_error=false;
        uint64_t masked_start=timer_us_gettime64();irq_mask_t mask=irq_disable();
        dma_error=(rd(DMAOR,4)&7u)!=1u;
        if(!p->done) {
            timeout=++p->polls>=MAX_POLLS || start-p->start_us>=TRIAL_TIMEOUT_US;
            if(dma_error || timeout || !active_dma_owned(p)) {
                p->armed=false;freeze(p);p->done=true;
            }
        }
        unmask(p,mask,masked_start);
        if(p->done) {
            completion(p);
            if(p->foreign_dma) result=KUI_SCI_ASYNC_DMA_ERROR;
            else if(dma_error) result=KUI_SCI_ASYNC_DMA_ERROR;
            else if(timeout || !p->event) {++p->stage->timeouts;result=KUI_SCI_ASYNC_TIMEOUT;}
            else if(p->quarantined || p->end_count || !(p->end_chcr&2u) ||
                    p->event==EXC_SCI_RXI || (p->end_ssr&(FLAGS&~ORER))) {
                ++p->stage->premature_errors;result=KUI_SCI_ASYNC_RECEIVE_ERROR;
            } else {p->phase=READER_READY;result=KUI_SCI_ASYNC_OK;}
            if(result!=KUI_SCI_ASYNC_OK) result=failed(p,result);
        }
    } else if(p->phase==READER_READY) result=KUI_SCI_ASYNC_OK;
    else if(p->phase==READER_FAILED) result=p->request_status;
    else result=KUI_SCI_ASYNC_ARGUMENT;
    return measured(p,&p->out->max_poll_us,start,result);
}
enum kui_sci_async_status kui_sci_async_finish(struct kui_sci_async_reader *reader,
        uint8_t dst[512],const uint8_t expected[512]) {
    struct probe *p=reader_state(reader);if(!p) return KUI_SCI_ASYNC_ARGUMENT;
    uint64_t start=timer_us_gettime64();enum kui_sci_async_status result;
    if(!dst && !p->cancel_requested) result=KUI_SCI_ASYNC_ARGUMENT;
    else if(p->phase==READER_FRAMING || p->phase==READER_DMA) result=KUI_SCI_ASYNC_PENDING;
    else if(p->phase==READER_READY) result=finish_request(p,dst,expected);
    else result=p->phase==READER_FAILED?p->request_status:KUI_SCI_ASYNC_ARGUMENT;
    return measured(p,&p->out->max_finish_us,start,result);
}
enum kui_sci_async_status kui_sci_async_cancel(struct kui_sci_async_reader *reader) {
    struct probe *p=reader_state(reader);if(!p) return KUI_SCI_ASYNC_ARGUMENT;
    uint64_t start=timer_us_gettime64();enum kui_sci_async_status result;
    p->cancel_requested=true;
    if(p->phase==READER_DMA) result=KUI_SCI_ASYNC_PENDING;
    else if(p->phase==READER_READY) result=KUI_SCI_ASYNC_OK;
    else if(p->phase==READER_FAILED) result=p->request_status;
    else result=failed(p,KUI_SCI_ASYNC_CANCELLED);
    return measured(p,&p->out->max_cancel_us,start,result);
}
enum kui_sci_async_status kui_sci_async_close(struct kui_sci_async_reader *reader) {
    struct probe *p=reader_state(reader);if(!p) return KUI_SCI_ASYNC_ARGUMENT;
    uint64_t start=timer_us_gettime64();
    if(p->phase==READER_FRAMING || p->phase==READER_DMA)
        return measured(p,&p->out->max_close_us,start,KUI_SCI_ASYNC_PENDING);
    if(p->phase==READER_READY) (void)finish_request(p,NULL,NULL);
    if(!p->module_unavailable && !(rd(PDTR,2)&0x80u)) {
        p->card->bus.select(p->card->bus.ctx,false);
        if(!p->stage->bus_faults) (void)bus_healthy(p);
    }
    p->out->status=p->out->operation_status=p->request_status;
    release(p);
    if(!p->out->safe_restored) p->out->status=KUI_SCI_ASYNC_RESTORE;
    p->out->elapsed_us=timer_us_gettime64()-p->opened_us;
    irq_mask_t mask=irq_disable();occupied=false;reader->generation=0;irq_restore(mask);
    return measured(p,&p->out->max_close_us,start,p->out->status);
}
uint32_t kui_sci_async_work_sample(struct kui_sci_async_reader *reader) {
    struct probe *p=reader_state(reader);
    if(!p || !p->armed || p->phase!=READER_DMA) return 0;
    if(!active_dma_owned(p)) return 0;
    uint32_t chcr=rd(CHCR,4),remaining=rd(TCR,4);
    if((chcr&3u)!=1u || chcr!=rd(CHCR,4) || !remaining || remaining>=514u || !p->armed)
        return 0;
    return remaining;
}
void kui_sci_async_work_record(struct kui_sci_async_reader *reader,
        uint32_t before,uint32_t iterations,uint32_t checksum) {
    struct probe *p=reader_state(reader);if(!p) return;
    uint32_t after=kui_sci_async_work_sample(reader);
    if(before>after && after>0 && before<514u) {
        ++p->stage->overlap_batches;p->stage->overlap_iterations+=iterations;
        p->stage->work_checksum=(p->stage->work_checksum<<1)|(p->stage->work_checksum>>31);
        p->stage->work_checksum^=checksum;
    }
}

enum kui_sci_async_status kui_sci_async_probe_run(const struct kui_loader_sd *c,
        uint32_t lba,const uint8_t baseline[512],bool (*cancelled)(void *),void *ctx,
        struct kui_sci_async_probe_result *out) {
    if(!out) return KUI_SCI_ASYNC_ARGUMENT;
    if(!baseline || !c || (uint64_t)lba>=c->blocks || (!c->high_capacity && lba>UINT32_MAX/512u)) {
        *out=(struct kui_sci_async_probe_result){.status=KUI_SCI_ASYNC_ARGUMENT,
            .operation_status=KUI_SCI_ASYNC_ARGUMENT,.lba=lba};
        return out->status;
    }
    struct kui_sci_async_reader reader={0};
    enum kui_sci_async_status result=kui_sci_async_open(&reader,c,out);
    out->lba=lba;
    if(result!=KUI_SCI_ASYNC_OK) return result;
    uint8_t payload[512];uint32_t work=UINT32_C(0x6d2b79f5);
    bool cancel_latched=false;
    for(unsigned speed=0;speed<2 && result==KUI_SCI_ASYNC_OK;++speed) {
        unsigned count=speed?KUI_SCI_ASYNC_FAST_TRIALS:KUI_SCI_ASYNC_SLOW_TRIALS;
        for(unsigned n=0;n<count && result==KUI_SCI_ASYNC_OK;++n) {
            if(cancelled && cancelled(ctx)) {result=kui_sci_async_cancel(&reader);break;}
            result=kui_sci_async_begin(&reader,lba,speed==0);
            if(result!=KUI_SCI_ASYNC_OK) break;
            do {
                if(!cancel_latched && cancelled && cancelled(ctx)) {
                    cancel_latched=true;
                    (void)kui_sci_async_cancel(&reader);
                }
                result=kui_sci_async_poll(&reader);
                if(result==KUI_SCI_ASYNC_PENDING) {
                    uint32_t before=kui_sci_async_work_sample(&reader);
                    for(unsigned i=0;i<16;++i) {work^=work<<13;work^=work>>17;work^=work<<5;}
#ifdef KUI_SCI_ASYNC_PROBE_TEST
                    kui_sci_async_test_work_tick();
#endif
                    __asm__ __volatile__("" : "+r"(work) : : "memory");
                    kui_sci_async_work_record(&reader,before,16,work);
                }
            } while(result==KUI_SCI_ASYNC_PENDING);
            if(result==KUI_SCI_ASYNC_OK) result=kui_sci_async_finish(&reader,payload,baseline);
        }
    }
    result=kui_sci_async_close(&reader);
    if(result==KUI_SCI_ASYNC_OK && (!out->slow.overlap_batches || !out->fast.overlap_batches))
        out->status=result=KUI_SCI_ASYNC_NO_OVERLAP;
    return result;
}

const char *kui_sci_async_status_name(enum kui_sci_async_status s) {
    static const char *const names[]={"pass","invalid argument","DMA busy","unsupported state",
        "cancelled","CMD17 rejected","data token error","timeout","receive error","DMA error",
        "buffer guard changed","CRC mismatch","baseline mismatch","restore failed","no CPU overlap measured",
        "handoff failed","framing bus fault","pending"};
    return (unsigned)s<sizeof(names)/sizeof(names[0])?names[s]:"unknown";
}

const char *kui_sci_async_phase_name(enum kui_sci_async_phase p) {
    static const char *const names[]={"none","lease","buffer","ready","command","token",
        "token end","ownership","GPIO","DMA","validate","complete","handoff"};
    return (unsigned)p<sizeof(names)/sizeof(names[0])?names[p]:"unknown";
}

const char *kui_sci_async_framing_name(enum kui_sci_async_framing_step s) {
    static const char *const names[]={"none","deselect","idle clock","select",
        "ready poll","command","response","token","handoff deselect"};
    return (unsigned)s<sizeof(names)/sizeof(names[0])?names[s]:"unknown";
}
