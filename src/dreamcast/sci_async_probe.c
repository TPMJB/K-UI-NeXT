/* SPDX-License-Identifier: GPL-3.0-only
 * Original isolated receive-only SCI experiment. Hardware register meanings:
 * Renesas SH7750 Hardware Manual Rev7.02, sections 14, 15 and 17; SH7091
 * SCSPTR mapping also corroborated by Linux v2.6.32 drivers/serial/sh-sci.h.
 * No external driver implementation is incorporated here.
 */
#include "kui/sci_async_probe.h"
#include "../loader/sd_reader.h"
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
struct probe {
    struct receive_area rx;
    struct kui_sci_async_probe_result *out;
    struct kui_sci_async_stage *stage;
    irq_cb_t previous[3];
    uint32_t sar,dar,tcr,chcr;
    uint8_t smr,brr,scr,scmr,sptr;
    unsigned sci_priority;
    bool leased;
    uint32_t expected_sar,expected_dar,expected_tcr,expected_chcr,start_address;
    volatile bool armed,done,foreign_dma,quarantined;
    volatile uint32_t end_chcr,end_count,end_ssr,event;
    uint64_t start_us;
};
static bool occupied;
static bool poisoned;
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
    wr(SCR,0,1);
    if(!active_dma_owned(p)) {p->foreign_dma=true;return;}
    uint32_t control=rd(CHCR,4);
    wr(CHCR,control&~UINT32_C(5),4);
    (void)rd(CHCR,4);
    settle(64);
    p->end_chcr=rd(CHCR,4);
    p->end_count=rd(TCR,4);
    p->end_ssr=rd(SSR,1);
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
    if(control&7u) status=KUI_SCI_ASYNC_BUSY;
    else if((mask&UINT32_C(0x100000f0)) || irq_inside_int() || !priority ||
            (dma&7u)!=1u || (scr&0xc4u) || (ssr&(RDRF|FLAGS)) ||
            (ssr&0x84u)!=0x84u || !(rd(PDTR,2)&0x80u) ||
            rd(SMR,1)!=0x80u || rd(SCMR,1)!=0 || rd(BRR,1)!=0)
        status=KUI_SCI_ASYNC_UNSUPPORTED;
    if(status!=KUI_SCI_ASYNC_OK) {unmask(p,mask,start);return status;}
    p->sar=rd(SAR,4);p->dar=rd(DAR,4);p->tcr=rd(TCR,4);p->chcr=control;
    p->expected_sar=p->sar;p->expected_dar=p->dar;p->expected_tcr=p->tcr;p->expected_chcr=p->chcr;
    p->smr=(uint8_t)rd(SMR,1);p->brr=(uint8_t)rd(BRR,1);p->scr=scr;
    p->scmr=(uint8_t)rd(SCMR,1);p->sptr=(uint8_t)rd(SPTR,1);
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
static unsigned port_mask(unsigned v) {
    /* Input-data bits reflect pin levels; restoring their sampled value is
     * neither possible nor necessary. Compare direction/EIO and output latches. */
    return 0x8au|((v&2u)?1u:0u)|((v&8u)?4u:0u);
}
static void release(struct probe *p) {
    if(!p->leased) return;
    uint64_t start=timer_us_gettime64(); irq_mask_t mask=irq_disable();
    if(p->armed) {p->armed=false;freeze(p);}
    if(!p->quarantined && !dma_unchanged(p)) p->foreign_dma=true;
    /* No source is enabled when the prior callback becomes visible again. */
    wr(SCR,0,1);
    if(!p->foreign_dma && !p->quarantined) {wr(CHCR,0,4);(void)rd(CHCR,4);settle(64);}
    uint8_t status=(uint8_t)rd(SSR,1);
    if(status&RDRF) (void)rd(RDR,1);
    wr(SSR,status&~(RDRF|FLAGS),1);
    if(!p->foreign_dma && !p->quarantined) {
        wr(SAR,p->sar,4);wr(DAR,p->dar,4);wr(TCR,p->tcr,4);wr(CHCR,p->chcr,4);
    }
    wr(SMR,p->smr,1);wr(BRR,p->brr,1);wr(SCMR,p->scmr,1);wr(SPTR,p->sptr,1);
    settle(1024);
    bool handlers=true;
    for(unsigned i=0;i<3;++i) {
        irq_cb_t current=irq_get_handler(events[i]);
        if(current.hdl!=interrupt || current.data!=p) {handlers=false;continue;}
        if(irq_set_handler(events[i],p->previous[i].hdl,p->previous[i].data)) handlers=false;
        if(!same_callback(irq_get_handler(events[i]),p->previous[i])) handlers=false;
    }
    irq_set_priority(IRQ_SRC_SCI1,p->sci_priority);
    handlers=handlers && irq_get_priority(IRQ_SRC_SCI1)==p->sci_priority;
    wr(SCR,p->scr,1);
    bool registers=!p->foreign_dma && !p->quarantined && rd(SAR,4)==p->sar && rd(DAR,4)==p->dar && rd(TCR,4)==p->tcr &&
        rd(CHCR,4)==p->chcr && rd(SMR,1)==p->smr && rd(BRR,1)==p->brr &&
        rd(SCR,1)==p->scr && rd(SCMR,1)==p->scmr &&
        (rd(SPTR,1)&port_mask(p->sptr))==(p->sptr&port_mask(p->sptr));
    p->out->handlers_restored=handlers;p->out->registers_restored=registers;
    p->out->dma_quarantined=p->quarantined;p->out->foreign_dma=p->foreign_dma;
    p->out->safe_restored=handlers && registers && !(rd(CHCR,4)&7u) &&
        !(rd(SSR,1)&(RDRF|FLAGS)) && (rd(DMAOR,4)&7u)==1u;
    poisoned=!p->out->safe_restored;p->leased=false;
    unmask(p,mask,start);
}

static uint8_t byte(const struct kui_loader_sd *c, uint8_t v, bool slow) {
    return c->bus.transfer(c->bus.ctx,v,slow);
}
static enum kui_sci_async_status token(const struct kui_loader_sd *c,uint32_t lba,bool slow) {
    uint32_t address=c->high_capacity?lba:lba*512u;
    uint8_t cmd[6]={0x51,(uint8_t)(address>>24),(uint8_t)(address>>16),
        (uint8_t)(address>>8),(uint8_t)address,0};
    cmd[5]=command_crc(cmd,5);
    c->bus.select(c->bus.ctx,false);(void)byte(c,0xff,slow);
    c->bus.select(c->bus.ctx,true);
    uint64_t start=timer_us_gettime64(); bool ready=false;
    for(unsigned i=0;i<4096;++i) {
        if(byte(c,0xff,slow)==0xff) {ready=true;break;}
        if(timer_us_gettime64()-start>=FRAME_TIMEOUT_US) break;
    }
    if(!ready) return KUI_SCI_ASYNC_TIMEOUT;
    for(unsigned i=0;i<6;++i) (void)byte(c,cmd[i],slow);
    uint8_t response=0xff;
    for(unsigned i=0;i<16 && (response&0x80u);++i) response=byte(c,0xff,slow);
    if(response) return KUI_SCI_ASYNC_COMMAND;
    start=timer_us_gettime64();
    for(unsigned i=0;i<8192;++i) {
        uint8_t v=byte(c,0xff,slow);
        if(v==0xfe) return KUI_SCI_ASYNC_OK;
        if(v!=0xff) return KUI_SCI_ASYNC_TOKEN;
        if(timer_us_gettime64()-start>=FRAME_TIMEOUT_US) break;
    }
    return KUI_SCI_ASYNC_TIMEOUT;
}
static bool guards(const struct receive_area *r) {
    for(unsigned i=0;i<32;++i) if(r->before[i]!=SENTINEL || r->after[i]!=SENTINEL) return false;
    for(unsigned i=514;i<544;++i) if(r->bytes[i]!=SENTINEL) return false;
    return true;
}

static enum kui_sci_async_status trial(struct probe *p,const struct kui_loader_sd *c,
        uint32_t lba,const uint8_t *baseline,bool slow) {
    struct kui_sci_async_stage *s=p->stage;
    ++s->attempted;
    p->out->guards_ok=p->out->crc_ok=p->out->baseline_ok=false;
    memset(&p->rx,SENTINEL,sizeof(p->rx));
    uint32_t address=physical(p->rx.bytes,sizeof(p->rx.bytes));
    if(!address) return KUI_SCI_ASYNC_UNSUPPORTED;
    cache(&p->rx,sizeof(p->rx),false);
    enum kui_sci_async_status result=token(c,lba,slow);
    if(result!=KUI_SCI_ASYNC_OK) return result;
    /* transfer() returns at RDRF. Wait for the token's final wire edge before
     * replacing full-duplex clocking; don't turn its last bit into payload. */
    bool ended=false;
    for(unsigned i=0;i<10000;++i) {
        uint32_t ssr=rd(SSR,1);
        if(ssr&FLAGS) return KUI_SCI_ASYNC_RECEIVE_ERROR;
        if(ssr&4u) {ended=true;break;}
    }
    if(!ended) return KUI_SCI_ASYNC_TIMEOUT;
    settle(slow?1024u:64u);
    uint64_t masked_start=timer_us_gettime64();irq_mask_t mask=irq_disable();
    if(!dma_unchanged(p)) {
        p->foreign_dma=true;unmask(p,mask,masked_start);return KUI_SCI_ASYNC_BUSY;
    }
    /* Preload TxD's GPIO latch high before TE is cleared. SPB1IO remains zero
     * so SCK is owned by the receiver, not a GPIO output. */
    wr(SPTR,0x83u,1);
    if((rd(SPTR,1)&0x8bu)!=0x83u) {unmask(p,mask,masked_start);return KUI_SCI_ASYNC_UNSUPPORTED;}
    wr(SCR,0,1);
    wr(CHCR,0,4);wr(SAR,RDR&UINT32_C(0x1fffffff),4);wr(DAR,address,4);wr(TCR,514,4);
    p->start_address=address;
    p->done=false;p->event=0;p->end_count=514;p->end_chcr=0;p->end_ssr=0;
    p->start_us=timer_us_gettime64();p->armed=true;
    wr(CHCR,RX_DMA,4);
    wr(SCR,0x50u,1); /* RIE + RE, no transmitter/dummy-byte CPU loop. */
    unmask(p,mask,masked_start);
    uint32_t work=UINT32_C(0x6d2b79f5);
    bool timeout=false,dma_error=false;
    for(unsigned polls=0;!p->done && polls<MAX_POLLS;++polls) {
        uint32_t before=rd(TCR,4);
        for(unsigned i=0;i<16;++i) {work^=work<<13;work^=work>>17;work^=work<<5;}
#ifdef KUI_SCI_ASYNC_PROBE_TEST
        kui_sci_async_test_work_tick();
#endif
        __asm__ __volatile__("" : "+r"(work) : : "memory");
        uint32_t after=rd(TCR,4);
        if(before>after && after>0 && before<=514) {
            ++s->overlap_batches;s->overlap_iterations+=16;
        }
        if((rd(DMAOR,4)&7u)!=1u) {dma_error=true;break;}
        if(timer_us_gettime64()-p->start_us>=TRIAL_TIMEOUT_US) {timeout=true;break;}
    }
    masked_start=timer_us_gettime64();mask=irq_disable();
    bool delivered=p->done;
    if(p->armed) {p->armed=false;freeze(p);}
    uint64_t receive=timer_us_gettime64()-p->start_us;
    unmask(p,mask,masked_start);
    s->work_checksum=(s->work_checksum<<1)|(s->work_checksum>>31);
    s->work_checksum^=work^s->attempted;s->receive_us+=receive;
    if(receive>s->max_receive_us) s->max_receive_us=receive;
    s->last_remaining=p->end_count;s->last_chcr=p->end_chcr;s->last_ssr=p->end_ssr;
    /* A foreign owner may still be transferring; do not invalidate or inspect
     * the receive area when we could not establish that our transfer stopped. */
    if(p->foreign_dma) return KUI_SCI_ASYNC_DMA_ERROR;
    if(p->quarantined) {
        if(dma_error) return KUI_SCI_ASYNC_DMA_ERROR;
        if(timeout || !delivered) {++s->timeouts;return KUI_SCI_ASYNC_TIMEOUT;}
        ++s->premature_errors;return KUI_SCI_ASYNC_RECEIVE_ERROR;
    }
    cache(&p->rx,sizeof(p->rx),true);
    if(!guards(&p->rx)) {p->out->guards_ok=false;return KUI_SCI_ASYNC_GUARD;}
    p->out->guards_ok=true;
    if(dma_error) return KUI_SCI_ASYNC_DMA_ERROR;
    if(timeout || !delivered) {++s->timeouts;return KUI_SCI_ASYNC_TIMEOUT;}
    bool complete=p->end_count==0 && (p->end_chcr&2u);
    if(!complete || p->event==EXC_SCI_RXI || (p->end_ssr&(FLAGS&~ORER))) {
        ++s->premature_errors;return KUI_SCI_ASYNC_RECEIVE_ERROR;
    }
    uint16_t crc=0;bool equal=true;
    for(unsigned i=0;i<512;++i) {
        uint8_t v=reverse_byte(p->rx.bytes[i]);crc=crc_byte(crc,v);
        if(v!=baseline[i]) equal=false;
    }
    uint16_t expected=(uint16_t)((uint16_t)reverse_byte(p->rx.bytes[512])<<8)|reverse_byte(p->rx.bytes[513]);
    if(crc!=expected) {p->out->crc_ok=false;return KUI_SCI_ASYNC_CRC;}
    p->out->crc_ok=true;
    if(!equal) {p->out->baseline_ok=false;return KUI_SCI_ASYNC_MISMATCH;}
    p->out->baseline_ok=true;
    if(p->end_ssr&ORER) ++s->trailing_overruns;
    ++s->passed;return KUI_SCI_ASYNC_OK;
}

enum kui_sci_async_status kui_sci_async_probe_run(const struct kui_loader_sd *c,
        uint32_t lba,const uint8_t baseline[512],bool (*cancelled)(void *),void *ctx,
        struct kui_sci_async_probe_result *out) {
    if(!out) return KUI_SCI_ASYNC_ARGUMENT;
    *out=(struct kui_sci_async_probe_result){.status=KUI_SCI_ASYNC_ARGUMENT,
        .operation_status=KUI_SCI_ASYNC_ARGUMENT,.lba=lba};
    if(!c || !baseline || !c->ready || c->slow || !c->bus.select || !c->bus.transfer ||
       (uint64_t)lba>=c->blocks || (!c->high_capacity && lba>UINT32_MAX/512u)) return out->status;
    uint64_t start=timer_us_gettime64();
    uint64_t reservation_start=timer_us_gettime64();irq_mask_t initial_mask=irq_disable();
    if(occupied || poisoned) {
        irq_restore(initial_mask);out->status=out->operation_status=KUI_SCI_ASYNC_BUSY;return out->status;
    }
    occupied=true;
    memset(&state,0,sizeof(state));state.out=out;
    struct probe *p=&state;
    max_time(&out->max_irq_masked_us,reservation_start);
    irq_restore(initial_mask);
    out->slow.clock_hz=390625;out->fast.clock_hz=12500000;
    out->status=lease(p);
    out->operation_status=out->status;
    if(out->status!=KUI_SCI_ASYNC_OK) {
        if(out->status!=KUI_SCI_ASYNC_RESTORE)
            out->safe_restored=out->handlers_restored=out->registers_restored=true;
        initial_mask=irq_disable();occupied=false;irq_restore(initial_mask);
        out->elapsed_us=timer_us_gettime64()-start;return out->status;
    }
    out->started=true;
    for(unsigned speed=0;speed<2 && out->status==KUI_SCI_ASYNC_OK;++speed) {
        p->stage=speed?&out->fast:&out->slow;
        uint64_t stage_start=timer_us_gettime64();
        unsigned count=speed?KUI_SCI_ASYNC_FAST_TRIALS:KUI_SCI_ASYNC_SLOW_TRIALS;
        for(unsigned n=0;n<count;++n) {
            if(cancelled && cancelled(ctx)) {out->status=KUI_SCI_ASYNC_CANCELLED;break;}
            out->status=trial(p,c,lba,baseline,speed==0);
            c->bus.select(c->bus.ctx,false);
            if(out->status!=KUI_SCI_ASYNC_OK) break;
            /* Return GPIO ownership before normal command framing. The next
             * transfer updates the existing bus's slow-clock state normally. */
            wr(SPTR,p->sptr,1);
        }
        p->stage->elapsed_us=timer_us_gettime64()-stage_start;
    }
    c->bus.select(c->bus.ctx,false);
    out->operation_status=out->status;
    release(p);
    if(!out->safe_restored) out->status=KUI_SCI_ASYNC_RESTORE;
    else if(out->status==KUI_SCI_ASYNC_OK &&
            (!out->slow.overlap_batches || !out->fast.overlap_batches))
        out->status=KUI_SCI_ASYNC_NO_OVERLAP;
    out->elapsed_us=timer_us_gettime64()-start;
    initial_mask=irq_disable();occupied=false;irq_restore(initial_mask);
    return out->status;
}

const char *kui_sci_async_status_name(enum kui_sci_async_status s) {
    static const char *const names[]={"pass","invalid argument","DMA busy","unsupported state",
        "cancelled","CMD17 rejected","data token error","timeout","receive error","DMA error",
        "buffer guard changed","CRC mismatch","baseline mismatch","restore failed","no CPU overlap measured"};
    return (unsigned)s<sizeof(names)/sizeof(names[0])?names[s]:"unknown";
}
