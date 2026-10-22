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
/* TMU2: KallistiOS's uptime counter, counting down at Pck/4 (12,468,720 Hz,
 * 80.2 ns) and reloading from TCOR2 once a second. Raw reads time short
 * steps cheaply. */
#define TCOR2 UINT32_C(0xffd80020)
#define TCNT2 UINT32_C(0xffd80024)
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
#define MAX_QUANTUM 4096u
/* More than the 514-byte block an overrun can interrupt; the card idles at
 * 0xff after the block, so extra clocks are harmless for CMD17. */
#define DRAIN_BYTES 1024u
#define IDLE_CHECKS 4u

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
/* KOS's timer_us_gettime64() counts each TMU2 tick as 80 ns, so its reading
 * reaches only 997,498 us within a second and then jumps 2.5 ms when the
 * second ends. The probe scales the ticks by their real length instead. */
static uint64_t us_scale,ns_scale; /* us and ns per tick, times 2^32 */
static void clock_init(void) {
    const uint64_t period=(uint64_t)rd(TCOR2,4)+1u;
    us_scale=(UINT64_C(1000000)<<32)/period;
    ns_scale=(UINT64_C(1000000000)<<32)/period;
}
static uint64_t now_us(void) {
#ifdef KUI_SCI_ASYNC_PROBE_TEST
    timer_val_t t=kui_sci_async_test_ticks();
#else
    timer_val_t t=__dreamcast_get_ticks();
#endif
    return (uint64_t)t.secs*1000000u+(((uint64_t)t.ticks*us_scale)>>32);
}
static uint32_t ticks_ns(uint32_t ticks) {
    return (uint32_t)(((uint64_t)ticks*ns_scale)>>32);
}
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
/* Lookup tables for the per-block check, built once from the routines above:
 * bit order reversal of each received byte and CRC16 (0x1021) of each value. */
static uint8_t reverse_table[256];
static uint16_t crc_table[256];
static bool tables_ready;
static void build_tables(void) {
    if(tables_ready) return;
    for(unsigned i=0;i<256;++i) {
        reverse_table[i]=reverse_byte((uint8_t)i);
        crc_table[i]=crc_byte(0,(uint8_t)i);
    }
    tables_ready=true;
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
    unsigned retries, quantum;
    uint64_t opened_us, request_us, frame_us, attempt_us;
    uint32_t expected_sar,expected_dar,expected_tcr,expected_chcr,start_address;
    volatile bool armed,done,foreign_dma,quarantined,overrun_abort;
    /* A CMD18 receive (measurement or streamed block) stopped complete or
     * proven idle, so the module reset may follow. */
    bool capture_stop;
    /* CMD18 stream (begin_stream): a CMD18 outstanding at the card, the
     * hardware block being a continuation (reselect and token search) rather
     * than a fresh command, the DMA count per block (514, or 513 when
     * streaming) and the byte the trailing overrun left in RDR. */
    bool streaming, stream_issued, continuing, rdr_valid;
    uint8_t rdr_byte;
    uint32_t dma_bytes;
    /* The run: destination, first LBA and length; the block the hardware is
     * on and the number checked and copied. Blocks alternate between two
     * receive areas; a slot holds a received block until it is checked,
     * which happens while the next block is received. A block that fails
     * its check restarts the stream there once the hardware is free. */
    uint8_t *stream_dst;
    uint32_t stream_lba0, stream_count, stream_next, stream_checked;
    uint32_t retry_checked, restart_block;
    bool restart_pending;
    enum kui_sci_async_status restart_cause;
    struct receive_area srx[2];
    uint32_t srx_address[2];
    struct stream_slot {uint32_t block; uint8_t rdr; bool rdr_valid, full;} slot[2];
    /* Pause tracing: TMU2 period in ticks and the end of the last poll of
     * the current request (0: none yet). */
    uint32_t tick_period;
    uint64_t last_poll_end, last_call_end;
    volatile uint32_t end_chcr,end_count,end_ssr,event;
    uint64_t start_us;
    struct kui_sci_async_fault fault_candidate;
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
        address>=p->start_address && address<=p->start_address+p->dma_bytes &&
        rd(TCR,4)<=p->dma_bytes &&
        (control&~UINT32_C(7))==(RX_DMA&~UINT32_C(7));
}

static void max_time(uint64_t *max, uint64_t start) {
    uint64_t n=now_us()-start; if(n>*max) *max=n;
}
/* Where the longest interrupt-masked window was, and in which stage; and a
 * log of pauses (see kui_sci_async_pause). */
enum masked_site { MASKED_LEASE=1, MASKED_RELEASE, MASKED_MODULE_RESET, MASKED_ARM,
    MASKED_POLL, MASKED_OPEN, PAUSE_CALL, PAUSE_BETWEEN_POLLS };
#define PAUSE_MASKED_US 500u
#define PAUSE_CALL_US 1500u
static uint32_t stage_index(const struct probe *p) {
    const struct kui_sci_async_probe_result *o=p->out;
    return p->stage==&o->slow?0u:p->stage==&o->fast?1u:p->stage==&o->cmd18?2u:
        p->stage==&o->streaming?3u:4u;
}
static void pause_add(struct probe *p,unsigned site,uint64_t us,uint64_t at) {
    struct kui_sci_async_probe_result *o=p->out;
    if(o->pause_count<KUI_SCI_ASYNC_PAUSES)
        o->pauses[o->pause_count]=(struct kui_sci_async_pause){site,stage_index(p),
            (uint32_t)(us<UINT32_MAX?us:UINT32_MAX),at};
    if(o->pause_count<UINT32_MAX) ++o->pause_count;
}
static uint32_t ticks_between(uint32_t earlier,uint32_t later,uint32_t period) {
    /* TMU2 counts down and reloads once per period. */
    return earlier>=later?earlier-later:earlier+(period-later);
}
static void masked_record(struct probe *p, uint64_t start, unsigned site) {
    uint64_t n=now_us()-start;
    if(n>=PAUSE_MASKED_US) pause_add(p,site,n,start);
    if(n<=p->out->max_irq_masked_us) return;
    p->out->max_irq_masked_us=n;p->out->max_irq_masked_site=site;
    p->out->max_irq_masked_stage=stage_index(p);
}
static void unmask(struct probe *p, irq_mask_t mask, uint64_t start, unsigned site) {
    masked_record(p,start,site); irq_restore(mask);
}

/* Each idle check is preceded by an uncached read of the receive area, so a
 * transfer the channel had already accepted has had the external bus too. */
static void bus_fence(const struct probe *p) {
#ifdef KUI_SCI_ASYNC_PROBE_TEST
    (void)p;kui_sci_async_test_bus_fence();
#else
    (void)*(volatile uint32_t *)(uintptr_t)(p->start_address|UINT32_C(0xa0000000));
#endif
}
/* With SCR=0 the SCI raises no further receive request and with DE clear the
 * channel accepts none. Renesas 14.3.6 gives no acknowledgement for one it
 * had already accepted, so wait until the stopped count and address agree and
 * stay unchanged across consecutive checks. At most two late transfers are
 * tolerated; anything else is treated as not provably idle. A late byte can
 * only land inside this receive area: a retry rewrites every payload byte and
 * CRC/guards still gate publication. */
static bool stopped_channel_idle(const struct probe *p,uint32_t total) {
    uint32_t first=rd(TCR,4),count=first,address=rd(DAR,4);
    unsigned stable=0;
    if(!first || first>total) return false;
    for(unsigned n=0;n<4u*IDLE_CHECKS;++n) {
        bus_fence(p);settle(256);
        uint32_t next_count=rd(TCR,4),next_address=rd(DAR,4);
        bool same=next_count==count && next_address==address;
        count=next_count;address=next_address;
        if(!count || count>first || first-count>2u || (rd(CHCR,4)&1u)) return false;
        if(same && address==p->start_address+(total-count)) {
            if(++stable>=IDLE_CHECKS) return true;
        } else stable=0;
    }
    return false;
}

/* Stop the source before the channel. DE/IE are cleared while preserving an
 * already-set TE, then TE is acknowledged. The readback/settle is NOT treated
 * as an abort-drain acknowledgement: only TE+count0 proves completion. A lone
 * receive overrun whose stopped channel is proven idle becomes retryable;
 * every other incomplete stop quarantines the buffer and channel until restart.
 */
static void freeze(struct probe *p,irq_t event,const irq_context_t *context,
        uint64_t observed_us) {
    p->stage->snapshot_ssr=rd(SSR,1);
    p->stage->snapshot_sptr=rd(SPTR,1);
    /* A normal completion pays only two additional reads. ERI/RXI, forced
     * stops and unexpected completion state retain pre-stop evidence. A
     * trailing ERI can still be a valid completed read, so keep this private
     * until foreground classification actually rejects the request. */
    uint32_t control_before=0,count_before=0;
    bool exceptional=event!=EXC_DMAC_DMTE1;
    if(!exceptional) {
        control_before=rd(CHCR,4);count_before=rd(TCR,4);
        exceptional=count_before || !(control_before&2u) ||
            (p->stage->snapshot_ssr&(FLAGS&~ORER));
    }
    if(exceptional && !p->fault_candidate.valid) {
        struct kui_sci_async_fault *f=&p->fault_candidate;
        f->event=(uint32_t)event;f->ssr=p->stage->snapshot_ssr;
        f->scr=rd(SCR,1);f->dmaor=rd(DMAOR,4);f->sar=rd(SAR,4);f->dar=rd(DAR,4);
        f->tcr=event==EXC_DMAC_DMTE1?count_before:rd(TCR,4);
        f->chcr=event==EXC_DMAC_DMTE1?control_before:rd(CHCR,4);
        f->lba=p->lba;f->start_address=p->start_address;
        f->request_elapsed_us=observed_us-p->request_us;
        if(context) {f->context_valid=1;f->pc=context->pc;f->sr=context->sr;}
        f->valid=1;
    }
    /* A streamed block's count ends one byte early: let the receiver take
     * the second CRC byte into RDR and stop on the next byte's overrun before
     * SCR is cleared, so the card has seen whole bytes. */
    if(p->streaming && event==EXC_DMAC_DMTE1)
        for(unsigned n=0;n<512u && !(rd(SSR,1)&ORER);++n) {}
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
    if(p->end_count || !(p->end_chcr&2u)) {
        bool overrun=event==EXC_SCI_ERI && (p->end_ssr&ORER) && !(p->end_ssr&(FLAGS&~ORER));
        if(overrun && stopped_channel_idle(p,p->dma_bytes)) p->overrun_abort=true;
        else {
            if(overrun) ++p->stage->undrained_overruns;
            p->quarantined=true;
        }
    }
    if(!p->quarantined) {
        wr(CHCR,0,4);
        if(p->overrun_abort) wr(TCR,0,4);
        p->expected_sar=rd(SAR,4);p->expected_dar=rd(DAR,4);
        p->expected_tcr=rd(TCR,4);p->expected_chcr=0;
    }
    if(p->end_ssr&RDRF) {p->rdr_byte=(uint8_t)rd(RDR,1);p->rdr_valid=true;}
    wr(SSR,p->end_ssr&~(RDRF|FLAGS),1);
    (void)rd(SSR,1);
}
static void interrupt(irq_t code, irq_context_t *context, void *data) {
    struct probe *p=data;
    /* A stale invocation must not touch a restored/foreign channel. */
    if(!p->armed) return;
    uint64_t start=now_us();
    p->armed=false;
    freeze(p,code,context,start);
    p->event=(uint32_t)code;
    if(code==EXC_DMAC_DMTE1) ++p->stage->dma_irqs;
    else if(code==EXC_SCI_ERI) ++p->stage->sci_error_irqs;
    else ++p->stage->unexpected_rx_irqs;
    p->done=true;
    max_time(&p->out->max_irq_handler_us,start);
}

static enum kui_sci_async_status lease(struct probe *p) {
    irq_mask_t mask=irq_disable(); uint64_t start=now_us();
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
    if(status!=KUI_SCI_ASYNC_OK) {unmask(p,mask,start,MASKED_LEASE);return status;}
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
        unmask(p,mask,start,MASKED_LEASE);return restored?KUI_SCI_ASYNC_UNSUPPORTED:KUI_SCI_ASYNC_RESTORE;
    }
    irq_set_priority(IRQ_SRC_SCI1,priority);
    p->leased=true;
    unmask(p,mask,start,MASKED_LEASE);return KUI_SCI_ASYNC_OK;
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
    irq_mask_t mask=irq_disable(); uint64_t start=now_us();
    if(p->armed) {p->armed=false;freeze(p,0,NULL,start);}
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
    unmask(p,mask,start,MASKED_RELEASE);
}

/* A completed, validated CMD17 with trailing overrun, a payload overrun
 * whose stopped channel was proven idle, or a CMD18 measurement's receive that
 * stopped complete or proven idle, is a reset candidate. Renesas
 * 9.2.1/9.6 and 15.1.4 document MSTP0 as SCI-only module
 * standby and initialization of SCI registers except SPTR. This experiment
 * tests whether that stronger reset restores the receiver after ORER; the
 * console has not established its internal cause. No DMA abort is inferred.
 */
static enum kui_sci_async_status module_reset(struct probe *p) {
    struct kui_sci_async_stage *s=p->stage;
    ++s->module_reset_attempts;
    s->module_reset_state=KUI_SCI_ASYNC_MODULE_RESET_PRECONDITION;
    irq_mask_t mask=irq_disable();uint64_t start=now_us();
    uint32_t t[7];t[0]=rd(TCNT2,4);
    enum kui_sci_async_status result=KUI_SCI_ASYNC_HANDOFF;
    s->module_stb_before=rd(STBCR,1);
    if(s->module_stb_before&1u) {p->module_unavailable=true;goto failed;}
    if(p->armed || p->quarantined || p->foreign_dma || !dma_unchanged(p)) {
        p->foreign_dma=true;result=KUI_SCI_ASYNC_DMA_ERROR;goto failed;
    }
    bool completed=!p->end_count && (p->end_chcr&2u) && p->payload_validated &&
        p->out->guards_ok && p->out->crc_ok;
    if(!(completed || p->overrun_abort || p->capture_stop) || !(p->end_ssr&ORER) ||
       !kui_sci_sd_healthy() || !(rd(PDTR,2)&0x80u) || rd(SCR,1)!=0 ||
       (rd(SSR,1)&(RDRF|FLAGS)) || (rd(SPTR,1)&0x8au)!=0x82u) goto failed;
    /* Only the SCI bit changes. The short mask protects these RMWs and the
     * ownership check; DMAC, SCIF, timers and the CPU keep their clocks. */
    t[1]=rd(TCNT2,4);
    p->module_unavailable=true;
    wr(STBCR,s->module_stb_before|1u,1);
    bool stopped=false;
    for(unsigned n=0;n<HANDOFF_POLLS;++n) {
        s->module_stb_stopped=rd(STBCR,1);
        if(s->module_stb_stopped&1u) {stopped=true;break;}
    }
    t[2]=rd(TCNT2,4);
    settle(64);
    t[3]=rd(TCNT2,4);
    /* Always attempt the bounded resume, including an unconfirmed assert.
     * There is no SCI MMIO between gating and confirming this clear. */
    wr(STBCR,rd(STBCR,1)&~1u,1);
    for(unsigned n=0;n<HANDOFF_POLLS;++n) {
        s->module_stb_after=rd(STBCR,1);
        if(!(s->module_stb_after&1u)) {p->module_unavailable=false;break;}
    }
    t[4]=rd(TCNT2,4);
    if(p->module_unavailable) {
        s->module_reset_state=KUI_SCI_ASYNC_MODULE_RESET_RESUME_FAILED;goto failed;
    }
    if(!stopped) {
        s->module_reset_state=KUI_SCI_ASYNC_MODULE_RESET_ASSERT_FAILED;goto failed;
    }
    settle(64);
    t[5]=rd(TCNT2,4);
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
    t[6]=rd(TCNT2,4);
    struct kui_sci_async_reset_time *worst=&p->out->reset_worst;
    uint32_t total=ticks_ns(ticks_between(t[0],t[6],p->tick_period));
    if(total>worst->ns) {
        worst->stage=stage_index(p);worst->ns=total;
        for(unsigned i=0;i<6;++i)
            worst->steps_ns[i]=ticks_ns(ticks_between(t[i],t[i+1],p->tick_period));
    }
    unmask(p,mask,start,MASKED_MODULE_RESET);return KUI_SCI_ASYNC_OK;
failed:
    ++s->module_reset_failures;
    unmask(p,mask,start,MASKED_MODULE_RESET);return result;
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
    for(unsigned budget=0;budget<p->quantum;++budget) {
        s->framing_index=p->frame_count;
        if(s->last_phase==KUI_SCI_ASYNC_PHASE_TOKEN_END) {
            uint32_t ssr=rd(SSR,1);
            if(ssr&FLAGS) return KUI_SCI_ASYNC_RECEIVE_ERROR;
            if(ssr&4u) return KUI_SCI_ASYNC_OK;
            if(++p->frame_count>=10000u || (!(p->frame_count&31u) &&
               now_us()-p->frame_us>=FRAME_TIMEOUT_US))
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
                p->frame_count=0;p->frame_us=now_us();break;
            case KUI_SCI_ASYNC_FRAMING_READY:
                value=byte(c,0xff,p->slow);
                if(!bus_healthy(p)) return KUI_SCI_ASYNC_BUS_FAULT;
                if(value==0xff) {
                    s->last_phase=KUI_SCI_ASYNC_PHASE_COMMAND;
                    s->framing_step=KUI_SCI_ASYNC_FRAMING_COMMAND;p->frame_count=0;
                } else if(++p->frame_count>=4096u || (!(p->frame_count&31u) &&
                          now_us()-p->frame_us>=FRAME_TIMEOUT_US))
                    return KUI_SCI_ASYNC_TIMEOUT;
                break;
            case KUI_SCI_ASYNC_FRAMING_COMMAND:
                /* From the first CMD18 byte on, the card may be streaming. */
                if(p->streaming) p->stream_issued=true;
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
                    p->frame_count=0;p->frame_us=now_us();
                } else if(++p->frame_count>=16u) return KUI_SCI_ASYNC_COMMAND;
                break;
            case KUI_SCI_ASYNC_FRAMING_TOKEN:
                value=byte(c,0xff,p->slow);s->last_token=value;
                if(!bus_healthy(p)) return KUI_SCI_ASYNC_BUS_FAULT;
                if(value==0xfe) {
                    uint64_t now=now_us(),waited=now-p->frame_us;
                    s->token_bytes+=p->frame_count;
                    if(p->frame_count>s->max_token_bytes) s->max_token_bytes=p->frame_count;
                    s->token_us+=waited;
                    if(waited>s->max_token_us) s->max_token_us=waited;
                    s->last_phase=KUI_SCI_ASYNC_PHASE_TOKEN_END;
                    p->frame_count=0;p->frame_us=now;
                } else if(value!=0xff) return KUI_SCI_ASYNC_TOKEN;
                else if(++p->frame_count>=8192u || (!(p->frame_count&31u) &&
                        now_us()-p->frame_us>=FRAME_TIMEOUT_US))
                    return KUI_SCI_ASYNC_TIMEOUT;
                break;
            case KUI_SCI_ASYNC_FRAMING_DRAIN:
                /* Finish clocking a block cut short by an overrun, with CS
                 * low, before deselecting and re-issuing the command. */
                if(!p->frame_count) {
                    c->bus.select(c->bus.ctx,true);
                    if(!bus_healthy(p)) return KUI_SCI_ASYNC_BUS_FAULT;
                }
                (void)byte(c,0xff,p->slow);
                if(!bus_healthy(p)) return KUI_SCI_ASYNC_BUS_FAULT;
                if(++p->frame_count>=DRAIN_BYTES) {
                    s->framing_step=KUI_SCI_ASYNC_FRAMING_DESELECT;p->frame_count=0;
                }
                break;
            case KUI_SCI_ASYNC_FRAMING_RESELECT:
                /* Between streamed blocks: the card kept its place while
                 * deselected for the SCI reset; search for the next token. */
                c->bus.select(c->bus.ctx,true);
                if(!bus_healthy(p)) return KUI_SCI_ASYNC_BUS_FAULT;
                s->last_phase=KUI_SCI_ASYNC_PHASE_TOKEN;
                s->framing_step=KUI_SCI_ASYNC_FRAMING_TOKEN;
                p->frame_count=0;p->frame_us=now_us();break;
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
    /* At least one bit time at the new rate: about 2.6 us slow, 80 ns fast. */
    settle(slow?1024u:64u);
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
    p->last_call_end=now_us();
    uint64_t elapsed=p->last_call_end-start;
    if(elapsed>=PAUSE_CALL_US) pause_add(p,PAUSE_CALL,elapsed,start);
    if(elapsed>*max) *max=elapsed;
    if(elapsed>p->out->max_call_us) p->out->max_call_us=elapsed;
    return status;
}
static enum kui_sci_async_status sync_stop(struct probe *p,uint32_t *response,
        uint32_t *busy,uint64_t *us);
/* An outstanding stream gets CMD12 where the ordinary bus may run (framing,
 * or after a handoff); otherwise the caller's normal recovery reinitializes
 * the card. The failure's own phase evidence is kept. */
static void stream_abandon(struct probe *p) {
    p->stream_issued=false;
    if(p->quarantined || p->foreign_dma || p->module_unavailable || p->armed ||
       !kui_sci_sd_healthy() || rd(SCR,1)!=0x30u) return;
    struct kui_sci_async_stage *s=p->stage;
    enum kui_sci_async_phase phase=s->last_phase;
    enum kui_sci_async_framing_step step=s->framing_step;
    uint32_t index=s->framing_index,response=0xff,busy=0;uint64_t us=0;
    (void)sync_stop(p,&response,&busy,&us);
    s->last_phase=phase;s->framing_step=step;s->framing_index=index;
}
static enum kui_sci_async_status failed(struct probe *p,enum kui_sci_async_status status) {
    if(status!=KUI_SCI_ASYNC_CANCELLED && p->fault_candidate.valid && !p->out->fault.valid)
        p->out->fault=p->fault_candidate;
    if(!p->module_unavailable && p->stage->last_phase<KUI_SCI_ASYNC_PHASE_DMA &&
       p->stage->last_phase!=KUI_SCI_ASYNC_PHASE_GPIO) {
        p->stage->snapshot_ssr=rd(SSR,1);p->stage->snapshot_sptr=rd(SPTR,1);
    }
    if(p->stream_issued) stream_abandon(p);
    p->phase=READER_FAILED;p->request_status=status;
    p->out->status=p->out->operation_status=status;
    return status;
}
static enum kui_sci_async_status arm(struct probe *p) {
    struct kui_sci_async_stage *s=p->stage;
    settle(p->slow?1024u:64u);
    irq_mask_t mask=irq_disable();uint64_t masked_start=now_us();
    s->last_phase=KUI_SCI_ASYNC_PHASE_OWNERSHIP;
    if(!dma_unchanged(p)) {
        p->foreign_dma=true;unmask(p,mask,masked_start,MASKED_ARM);return KUI_SCI_ASYNC_BUSY;
    }
    /* Preload TxD's GPIO latch high before TE is cleared. SPTR reads return
     * RxD/SCK pin levels even when output is selected (manual 15.2.8), so only
     * EIO and direction controls can be verified. SPB1IO remains zero. */
    s->last_phase=KUI_SCI_ASYNC_PHASE_GPIO;
    wr(SPTR,0x83u,1);
    uint32_t port_value=rd(SPTR,1);
    s->snapshot_sptr=port_value;s->snapshot_ssr=rd(SSR,1);
    if((port_value&0x8au)!=0x82u) {unmask(p,mask,masked_start,MASKED_ARM);return KUI_SCI_ASYNC_UNSUPPORTED;}
    wr(SCR,0,1);
    wr(CHCR,0,4);wr(SAR,RDR&UINT32_C(0x1fffffff),4);wr(DAR,p->buffer_address,4);
    wr(TCR,p->dma_bytes,4);
    p->start_address=p->buffer_address;
    p->done=false;p->event=0;p->end_count=p->dma_bytes;p->end_chcr=0;p->end_ssr=0;
    p->rdr_valid=false;
    p->start_us=now_us();p->armed=true;
    s->framing_us+=p->start_us-p->attempt_us;
    s->last_phase=KUI_SCI_ASYNC_PHASE_DMA;++s->dma_started;
    wr(CHCR,RX_DMA,4);
    wr(SCR,0x50u,1); /* RIE + RE, no transmitter/dummy-byte CPU loop. */
    unmask(p,mask,masked_start,MASKED_ARM);
    p->phase=READER_DMA;
    return KUI_SCI_ASYNC_PENDING;
}
static void completion(struct probe *p) {
    if(p->completion_recorded) return;
    p->completion_recorded=true;
    uint64_t receive=now_us()-p->start_us;
    p->stage->receive_us+=receive;
    if(receive>p->stage->max_receive_us) p->stage->max_receive_us=receive;
    p->stage->last_remaining=p->end_count;p->stage->last_chcr=p->end_chcr;
    p->stage->last_ssr=p->end_ssr;
}
static void set_command(struct probe *p,uint8_t code,uint32_t lba) {
    uint32_t address=p->card->high_capacity?lba:lba*512u;
    p->command[0]=code;p->command[1]=(uint8_t)(address>>24);
    p->command[2]=(uint8_t)(address>>16);p->command[3]=(uint8_t)(address>>8);
    p->command[4]=(uint8_t)address;p->command[5]=command_crc(p->command,5);
}
/* ---- CMD18 streams: poll drives the whole run ---- */
/* Fresh sentinels and DMA address for the area the hardware block will use. */
static void stream_prepare(struct probe *p) {
    struct receive_area *r=&p->srx[p->stream_next&1u];
    memset(r,SENTINEL,sizeof(*r));
    cache(r,sizeof(*r),false);
    p->buffer_address=p->srx_address[p->stream_next&1u];
    p->lba=p->stream_lba0+p->stream_next;p->out->lba=p->lba;
    p->polls=p->frame_count=0;p->done=false;p->event=0;
    p->completion_recorded=p->overrun_abort=false;
    p->fault_candidate=(struct kui_sci_async_fault){0};
}
/* Restart the stream at `block`: CMD12 if a CMD18 is outstanding, then frame
 * a fresh CMD18 there. At most KUI_SCI_ASYNC_OVERRUN_RETRIES restarts until
 * another block has been checked; past that the cause is returned. The
 * ordinary bus must be usable (framing, after a reset, or after the final
 * stop). */
static enum kui_sci_async_status stream_reissue(struct probe *p,uint32_t block,
        enum kui_sci_async_status cause) {
    struct kui_sci_async_stage *s=p->stage;
    if(p->stream_checked!=p->retry_checked) {p->retry_checked=p->stream_checked;p->retries=0;}
    if(p->retries>=KUI_SCI_ASYNC_OVERRUN_RETRIES) return cause;
    ++p->retries;
    if(p->stream_issued) {
        uint32_t response=0xff,busy=0;uint64_t us=0;
        enum kui_sci_async_status stopped=sync_stop(p,&response,&busy,&us);
        p->stream_issued=false;
        if(stopped!=KUI_SCI_ASYNC_OK) return stopped;
    }
    ++s->stream_restarts;
    p->restart_pending=p->continuing=false;
    p->stream_next=block;
    stream_prepare(p);
    p->attempt_us=now_us();
    set_command(p,0x52,p->lba);
    s->last_phase=KUI_SCI_ASYNC_PHASE_READY;
    s->framing_step=KUI_SCI_ASYNC_FRAMING_DESELECT;s->framing_index=0;
    p->phase=READER_FRAMING;
    return KUI_SCI_ASYNC_PENDING;
}
/* Check the oldest received block (CRC16 completed by its RDR byte) into the
 * destination. Runs while the next block is received. A failed check
 * restarts the stream at that block, after the in-flight DMA if any. */
static enum kui_sci_async_status stream_check(struct probe *p) {
    struct kui_sci_async_stage *s=p->stage;
    struct stream_slot *slot=&p->slot[p->stream_checked&1u];
    if(!slot->full || slot->block!=p->stream_checked) return KUI_SCI_ASYNC_PENDING;
    uint64_t start=now_us();
    struct receive_area *r=&p->srx[slot->block&1u];
    enum kui_sci_async_status result=KUI_SCI_ASYNC_OK;
    slot->full=false;
    cache(r,sizeof(*r),true);
    if(!slot->rdr_valid) {++s->missing_tail_bytes;result=KUI_SCI_ASYNC_RECEIVE_ERROR;}
    else {
        r->bytes[513]=slot->rdr;
        if(!guards(r)) result=KUI_SCI_ASYNC_GUARD;
        else {
            uint16_t crc=0;
            const uint8_t *bytes=r->bytes;uint8_t *out=p->stream_dst+(size_t)slot->block*512u;
            for(unsigned i=0;i<512;++i) {
                uint8_t value=reverse_table[bytes[i]];
                out[i]=value;crc=(uint16_t)((crc<<8)^crc_table[(uint8_t)(crc>>8)^value]);
            }
            uint16_t wire=(uint16_t)((uint16_t)reverse_table[bytes[512]]<<8)|reverse_table[bytes[513]];
            if(crc!=wire) result=KUI_SCI_ASYNC_CRC;
        }
    }
    s->finish_us+=now_us()-start;
    if(result!=KUI_SCI_ASYNC_OK) {
        if(p->phase==READER_DMA) {
            p->restart_pending=true;p->restart_block=slot->block;p->restart_cause=result;
            return KUI_SCI_ASYNC_PENDING;
        }
        return stream_reissue(p,slot->block,result);
    }
    ++s->passed;++p->stream_checked;
    p->out->guards_ok=p->out->crc_ok=true;
    if(p->stream_checked<p->stream_count) return KUI_SCI_ASYNC_PENDING;
    s->last_phase=KUI_SCI_ASYNC_PHASE_COMPLETE;
    s->elapsed_us+=now_us()-p->request_us;
    p->streaming=false;p->phase=READER_IDLE;p->request_status=KUI_SCI_ASYNC_OK;
    return KUI_SCI_ASYNC_OK;
}
/* A streamed block's DMA completed normally: keep its RDR byte, reset the
 * SCI, start the next block (or stop the card after the last one), then check
 * this block while the next one is received. */
static enum kui_sci_async_status stream_block_done(struct probe *p) {
    struct kui_sci_async_stage *s=p->stage;
    uint32_t block=p->stream_next;
    uint64_t serial_start=now_us();
    if(p->end_ssr&ORER) ++s->trailing_overruns;
    p->capture_stop=true;
    enum kui_sci_async_status result=handoff(p,p->card,false);
    p->capture_stop=false;
    if(result!=KUI_SCI_ASYNC_OK) return result;
    if(p->cancel_requested) return KUI_SCI_ASYNC_CANCELLED;
    /* A block checked bad while this one was received: this one is dropped. */
    if(p->restart_pending) return stream_reissue(p,p->restart_block,p->restart_cause);
    struct stream_slot *slot=&p->slot[block&1u];
    slot->block=block;slot->rdr=p->rdr_byte;slot->rdr_valid=p->rdr_valid;slot->full=true;
    ++p->stream_next;
    if(p->stream_next<p->stream_count) {
        stream_prepare(p);
        p->attempt_us=serial_start; /* framing_us: reset, reselect, token, DMA start */
        p->continuing=true;++s->attempted;
        s->last_phase=KUI_SCI_ASYNC_PHASE_TOKEN;
        s->framing_step=KUI_SCI_ASYNC_FRAMING_RESELECT;s->framing_index=0;
        p->phase=READER_FRAMING;
        result=frame_poll(p);
        if(result==KUI_SCI_ASYNC_TOKEN || result==KUI_SCI_ASYNC_TIMEOUT)
            result=stream_reissue(p,p->stream_next,result);
        else if(result==KUI_SCI_ASYNC_OK) result=arm(p);
        if(result!=KUI_SCI_ASYNC_PENDING) return result;
    } else {
        uint32_t response=0xff,busy=0;uint64_t us=0;
        result=sync_stop(p,&response,&busy,&us);
        p->stream_issued=false;
        if(result!=KUI_SCI_ASYNC_OK) return result;
        p->phase=READER_IDLE; /* hardware idle; the run ends once checked */
    }
    return stream_check(p);
}
/* A proven-idle mid-block overrun in a stream: reset the SCI, then CMD12 and
 * CMD18 again at this block (or at a block that failed its check meanwhile). */
static enum kui_sci_async_status stream_overrun(struct probe *p) {
    struct kui_sci_async_stage *s=p->stage;
    ++s->payload_overruns;
    if(p->fault_candidate.valid && !p->out->first_overrun.valid)
        p->out->first_overrun=p->fault_candidate;
    enum kui_sci_async_status result=handoff(p,p->card,false);
    p->overrun_abort=false;
    if(result!=KUI_SCI_ASYNC_OK) return result;
    if(p->cancel_requested) return KUI_SCI_ASYNC_CANCELLED;
    result=stream_reissue(p,p->restart_pending?p->restart_block:p->stream_next,KUI_SCI_ASYNC_RECEIVE_ERROR);
    if(result==KUI_SCI_ASYNC_PENDING) ++s->overrun_retries;
    return result;
}
/* A proven-idle overrun leaves the card part way through its block. Restore
 * the receiver as after any trailing overrun (deselect, MSTP0 reset and
 * synchronous re-initialization), then clock out the rest of the block and
 * re-issue the same CMD17, at most KUI_SCI_ASYNC_OVERRUN_RETRIES times. */
static enum kui_sci_async_status overrun_retry(struct probe *p) {
    struct kui_sci_async_stage *s=p->stage;
    ++s->payload_overruns;
    if(p->fault_candidate.valid && !p->out->first_overrun.valid)
        p->out->first_overrun=p->fault_candidate;
    enum kui_sci_async_status result=handoff(p,p->card,p->slow);
    p->overrun_abort=false;
    if(result!=KUI_SCI_ASYNC_OK) return result;
    if(p->cancel_requested) return KUI_SCI_ASYNC_CANCELLED;
    if(p->retries>=KUI_SCI_ASYNC_OVERRUN_RETRIES) return KUI_SCI_ASYNC_RECEIVE_ERROR;
    ++p->retries;++s->overrun_retries;
    p->fault_candidate=(struct kui_sci_async_fault){0};
    p->polls=p->frame_count=0;p->done=false;p->event=0;
    p->completion_recorded=p->payload_validated=false;
    p->attempt_us=now_us();
    memset(&p->rx,SENTINEL,sizeof(p->rx));
    cache(&p->rx,sizeof(p->rx),false);
    s->last_phase=KUI_SCI_ASYNC_PHASE_READY;
    s->framing_step=KUI_SCI_ASYNC_FRAMING_DRAIN;s->framing_index=0;
    p->phase=READER_FRAMING;
    return KUI_SCI_ASYNC_PENDING;
}
static enum kui_sci_async_status finish_request(struct probe *p,uint8_t *dst,const uint8_t *expected) {
    struct kui_sci_async_stage *s=p->stage;
    if(!dma_unchanged(p)) {p->foreign_dma=true;return failed(p,KUI_SCI_ASYNC_DMA_ERROR);}
    s->last_phase=KUI_SCI_ASYNC_PHASE_VALIDATE;
    cache(&p->rx,sizeof(p->rx),true);
    if(!guards(&p->rx)) return failed(p,KUI_SCI_ASYNC_GUARD);
    p->out->guards_ok=true;
    uint16_t crc=0;bool equal=true;
    uint8_t *bytes=p->rx.bytes;
    if(expected) {
        for(unsigned i=0;i<512;++i) {
            uint8_t value=reverse_table[bytes[i]];
            bytes[i]=value;crc=(uint16_t)((crc<<8)^crc_table[(uint8_t)(crc>>8)^value]);
            if(value!=expected[i]) equal=false;
        }
    } else {
        for(unsigned i=0;i<512;++i) {
            uint8_t value=reverse_table[bytes[i]];
            bytes[i]=value;crc=(uint16_t)((crc<<8)^crc_table[(uint8_t)(crc>>8)^value]);
        }
    }
    uint16_t wire_crc=(uint16_t)((uint16_t)reverse_table[bytes[512]]<<8)|reverse_table[bytes[513]];
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
    s->elapsed_us+=now_us()-p->request_us;
    if(p->cancel_requested) return failed(p,KUI_SCI_ASYNC_CANCELLED);
    if(dst) memcpy(dst,p->rx.bytes,512);
    /* A valid trailing ERI belongs to this successful request, not a later
     * ownership failure discovered before the next request can begin. */
    p->fault_candidate=(struct kui_sci_async_fault){0};
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
    if(!us_scale) clock_init();
    build_tables(); /* once, before interrupts are masked */
    uint64_t start=now_us();
    irq_mask_t mask=irq_disable();uint64_t reservation_start=now_us();
    if(occupied || poisoned) {
        irq_restore(mask);out->status=out->operation_status=KUI_SCI_ASYNC_BUSY;return out->status;
    }
    occupied=true;memset(&state,0,sizeof(state));
    struct probe *p=&state;p->out=out;p->card=c;p->owner=reader;
    if(!++next_generation) ++next_generation;
    reader->generation=p->generation=next_generation;
    p->opened_us=start;p->phase=READER_IDLE;p->quantum=FRAMING_QUANTUM;
    p->tick_period=rd(TCOR2,4)+1u;
    masked_record(p,reservation_start,MASKED_OPEN);irq_restore(mask);
    out->slow.clock_hz=390625;
    out->fast.clock_hz=out->cmd18.clock_hz=out->streaming.clock_hz=12500000;
    out->slow.command_response=out->slow.last_token=0xffu;
    out->fast.command_response=out->fast.last_token=0xffu;
    out->cmd18.command_response=out->cmd18.last_token=0xffu;
    out->streaming.command_response=out->streaming.last_token=0xffu;
    p->stage=&out->slow;
    out->status=lease(p);out->operation_status=out->status;
    if(out->status!=KUI_SCI_ASYNC_OK) {
        if(out->status!=KUI_SCI_ASYNC_RESTORE)
            out->safe_restored=out->handlers_restored=out->registers_restored=true;
        mask=irq_disable();occupied=false;reader->generation=0;irq_restore(mask);
        out->elapsed_us=now_us()-start;
    } else out->started=true;
    return measured(p,&out->max_open_us,start,out->status);
}
static bool block_range(const struct probe *p,uint32_t lba,uint32_t blocks) {
    uint64_t end=(uint64_t)lba+blocks;
    return blocks && end<=p->card->blocks && (p->card->high_capacity || end<=UINT32_MAX/512u);
}
/* count==0: one CMD17 request; otherwise a CMD18 stream of count blocks
 * into dst. */
static enum kui_sci_async_status start_request(struct probe *p,uint32_t lba,bool slow,
        uint32_t count,uint8_t *dst,uint64_t start) {
    enum kui_sci_async_status result=KUI_SCI_ASYNC_OK;
    if(p->phase!=READER_IDLE) result=KUI_SCI_ASYNC_BUSY;
    else if(!block_range(p,lba,count?count:1u)) result=KUI_SCI_ASYNC_ARGUMENT;
    else if(!dma_unchanged(p)) {p->foreign_dma=true;result=failed(p,KUI_SCI_ASYNC_DMA_ERROR);}
    else {
        p->stage=count?&p->out->streaming:slow?&p->out->slow:&p->out->fast;
        struct kui_sci_async_stage *s=p->stage;
        p->lba=lba;p->slow=slow;p->out->lba=lba;p->request_us=start;
        p->polls=p->frame_count=0;p->cancel_requested=false;
        p->payload_validated=p->completion_recorded=false;
        p->retries=0;p->overrun_abort=p->capture_stop=false;p->attempt_us=start;
        p->streaming=count!=0;p->stream_issued=p->continuing=p->restart_pending=false;
        p->last_poll_end=0;
        p->dma_bytes=count?513u:514u;
        p->stream_dst=dst;p->stream_lba0=lba;p->stream_count=count;
        p->stream_next=p->stream_checked=p->retry_checked=0;
        p->slot[0].full=p->slot[1].full=false;
        p->fault_candidate=(struct kui_sci_async_fault){0};
        p->out->guards_ok=p->out->crc_ok=p->out->baseline_ok=p->out->baseline_checked=false;
        s->last_phase=KUI_SCI_ASYNC_PHASE_BUFFER;++s->attempted;
        s->command_response=s->last_token=0xff;
        if(count) {
            p->srx_address[0]=physical(p->srx[0].bytes,sizeof(p->srx[0].bytes));
            p->srx_address[1]=physical(p->srx[1].bytes,sizeof(p->srx[1].bytes));
            p->buffer_address=p->srx_address[0] && p->srx_address[1];
            if(p->buffer_address) stream_prepare(p);
        } else {
            memset(&p->rx,SENTINEL,sizeof(p->rx));
            p->buffer_address=physical(p->rx.bytes,sizeof(p->rx.bytes));
            if(p->buffer_address) cache(&p->rx,sizeof(p->rx),false);
        }
        if(!p->buffer_address) result=failed(p,KUI_SCI_ASYNC_UNSUPPORTED);
        else {
            set_command(p,count?0x52:0x51,lba);
            s->last_phase=KUI_SCI_ASYNC_PHASE_READY;
            s->framing_step=KUI_SCI_ASYNC_FRAMING_DESELECT;s->framing_index=0;
            p->phase=READER_FRAMING;p->request_status=KUI_SCI_ASYNC_PENDING;
        }
    }
    return result;
}
enum kui_sci_async_status kui_sci_async_begin(struct kui_sci_async_reader *reader,uint32_t lba,bool slow) {
    struct probe *p=reader_state(reader);if(!p) return KUI_SCI_ASYNC_ARGUMENT;
    uint64_t start=now_us();
    return measured(p,&p->out->max_begin_us,start,start_request(p,lba,slow,0,NULL,start));
}
enum kui_sci_async_status kui_sci_async_begin_stream(struct kui_sci_async_reader *reader,
        uint32_t lba,uint32_t count,uint8_t *dst) {
    struct probe *p=reader_state(reader);if(!p) return KUI_SCI_ASYNC_ARGUMENT;
    uint64_t start=now_us();
    enum kui_sci_async_status result=count && dst?start_request(p,lba,false,count,dst,start):
        KUI_SCI_ASYNC_ARGUMENT;
    return measured(p,&p->out->max_begin_us,start,result);
}
enum kui_sci_async_status kui_sci_async_poll(struct kui_sci_async_reader *reader) {
    struct probe *p=reader_state(reader);if(!p) return KUI_SCI_ASYNC_ARGUMENT;
    uint64_t start=now_us();
    /* Polls of one request follow each other directly in the diagnostics. */
    if(p->last_poll_end && start-p->last_poll_end>=PAUSE_CALL_US)
        pause_add(p,PAUSE_BETWEEN_POLLS,start-p->last_poll_end,p->last_poll_end);
    enum kui_sci_async_status result=KUI_SCI_ASYNC_PENDING;
    if(p->phase==READER_FRAMING) {
        result=frame_poll(p);
        if(p->streaming && p->continuing && (result==KUI_SCI_ASYNC_TOKEN || result==KUI_SCI_ASYNC_TIMEOUT))
            result=stream_reissue(p,p->stream_next,result);
        else if(result==KUI_SCI_ASYNC_OK) result=arm(p);
        if(result!=KUI_SCI_ASYNC_PENDING) result=failed(p,result);
    } else if(p->phase==READER_DMA) {
        bool timeout=false,dma_error=false;
        irq_mask_t mask=irq_disable();uint64_t masked_start=now_us();
        dma_error=(rd(DMAOR,4)&7u)!=1u;
        if(!p->done) {
            timeout=++p->polls>=MAX_POLLS || start-p->start_us>=TRIAL_TIMEOUT_US;
            if(dma_error || timeout || !active_dma_owned(p)) {
                p->armed=false;freeze(p,0,NULL,start);p->done=true;
            }
        }
        unmask(p,mask,masked_start,MASKED_POLL);
        if(p->done) {
            completion(p);
            if(p->foreign_dma) result=KUI_SCI_ASYNC_DMA_ERROR;
            else if(dma_error) result=KUI_SCI_ASYNC_DMA_ERROR;
            else if(timeout || !p->event) {++p->stage->timeouts;result=KUI_SCI_ASYNC_TIMEOUT;}
            else if(p->overrun_abort && !p->quarantined)
                result=p->streaming?stream_overrun(p):overrun_retry(p);
            else if(p->quarantined || p->end_count || !(p->end_chcr&2u) ||
                    p->event==EXC_SCI_RXI || (p->end_ssr&(FLAGS&~ORER))) {
                ++p->stage->premature_errors;result=KUI_SCI_ASYNC_RECEIVE_ERROR;
            } else if(p->streaming) result=stream_block_done(p);
            else {p->phase=READER_READY;result=KUI_SCI_ASYNC_OK;}
            if(result!=KUI_SCI_ASYNC_OK && result!=KUI_SCI_ASYNC_PENDING) result=failed(p,result);
        }
    } else if(p->phase==READER_READY) result=KUI_SCI_ASYNC_OK;
    else if(p->phase==READER_FAILED) result=p->request_status;
    else result=KUI_SCI_ASYNC_ARGUMENT;
    result=measured(p,&p->out->max_poll_us,start,result);
    p->last_poll_end=result==KUI_SCI_ASYNC_PENDING?p->last_call_end:0;
    return result;
}
enum kui_sci_async_status kui_sci_async_finish(struct kui_sci_async_reader *reader,
        uint8_t dst[512],const uint8_t expected[512]) {
    struct probe *p=reader_state(reader);if(!p) return KUI_SCI_ASYNC_ARGUMENT;
    uint64_t start=now_us();enum kui_sci_async_status result;
    if(!dst && !p->cancel_requested) result=KUI_SCI_ASYNC_ARGUMENT;
    else if(p->phase==READER_FRAMING || p->phase==READER_DMA) result=KUI_SCI_ASYNC_PENDING;
    else if(p->phase==READER_READY) {
        struct kui_sci_async_stage *s=p->stage;
        result=finish_request(p,dst,expected);
        s->finish_us+=now_us()-start;
    }
    else result=p->phase==READER_FAILED?p->request_status:KUI_SCI_ASYNC_ARGUMENT;
    return measured(p,&p->out->max_finish_us,start,result);
}
enum kui_sci_async_status kui_sci_async_cancel(struct kui_sci_async_reader *reader) {
    struct probe *p=reader_state(reader);if(!p) return KUI_SCI_ASYNC_ARGUMENT;
    uint64_t start=now_us();enum kui_sci_async_status result;
    p->cancel_requested=true;
    if(p->phase==READER_DMA) result=KUI_SCI_ASYNC_PENDING;
    else if(p->phase==READER_READY) result=KUI_SCI_ASYNC_OK;
    else if(p->phase==READER_FAILED) result=p->request_status;
    else result=failed(p,KUI_SCI_ASYNC_CANCELLED);
    return measured(p,&p->out->max_cancel_us,start,result);
}
enum kui_sci_async_status kui_sci_async_close(struct kui_sci_async_reader *reader) {
    struct probe *p=reader_state(reader);if(!p) return KUI_SCI_ASYNC_ARGUMENT;
    uint64_t start=now_us();
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
    p->out->elapsed_us=now_us()-p->opened_us;
    irq_mask_t mask=irq_disable();occupied=false;reader->generation=0;irq_restore(mask);
    return measured(p,&p->out->max_close_us,start,p->out->status);
}
void kui_sci_async_set_framing_quantum(struct kui_sci_async_reader *reader,unsigned bytes) {
    struct probe *p=reader_state(reader);if(!p) return;
    p->quantum=bytes<FRAMING_QUANTUM?FRAMING_QUANTUM:bytes>MAX_QUANTUM?MAX_QUANTUM:bytes;
}
/* ---- CMD18 measurements for the speed test ---- */

/* One receive-only DMA of `bytes` with CPU interrupts masked and no DMA
 * completion IRQ. SCR is cleared only after the trailing overrun has ended
 * the SCI's continuous clocking, so the card has seen whole bytes. OK: the
 * count completed. RECEIVE_ERROR: an early stop whose channel was proven idle
 * (*remaining is its final count). DMA_ERROR: quarantined, as for a request.
 * capture_stop marks a stop the module reset may follow. */
static enum kui_sci_async_status masked_receive(struct probe *p,uint32_t address,
        uint32_t bytes,uint32_t *remaining,uint64_t *receive_us,uint64_t *max_masked_us) {
    struct kui_sci_async_stage *s=p->stage;
    /* As in arm(): let the last polled byte's final edge pass first. */
    settle(64);
    irq_mask_t mask=irq_disable();uint64_t masked_start=now_us();
    s->last_phase=KUI_SCI_ASYNC_PHASE_OWNERSHIP;
    if(!dma_unchanged(p)) {
        p->foreign_dma=true;irq_restore(mask);return KUI_SCI_ASYNC_DMA_ERROR;
    }
    s->last_phase=KUI_SCI_ASYNC_PHASE_GPIO;
    wr(SPTR,0x83u,1);
    s->snapshot_sptr=rd(SPTR,1);s->snapshot_ssr=rd(SSR,1);
    if((s->snapshot_sptr&0x8au)!=0x82u) {irq_restore(mask);return KUI_SCI_ASYNC_UNSUPPORTED;}
    wr(SCR,0,1);
    wr(CHCR,0,4);wr(SAR,RDR&UINT32_C(0x1fffffff),4);wr(DAR,address,4);wr(TCR,bytes,4);
    p->start_address=address;p->end_count=bytes;p->end_chcr=0;p->end_ssr=0;
    p->rdr_valid=false;
    s->last_phase=KUI_SCI_ASYNC_PHASE_DMA;++s->dma_started;
    uint64_t start=now_us(),limit=(uint64_t)bytes*64u/100u+2000u;
    wr(CHCR,RX_DMA&~UINT32_C(4),4);
    wr(SCR,0x50u,1);
    bool complete=false;
    for(uint32_t polls=1;;++polls) {
        if(rd(CHCR,4)&2u) {complete=true;break;}
        if((rd(SSR,1)&FLAGS) || (rd(DMAOR,4)&7u)!=1u) break;
        if(!(polls&63u) && now_us()-start>=limit) {++s->timeouts;break;}
    }
    /* Continuous clocking ends with the overrun, as after a request's IRQ. */
    if(complete) for(unsigned n=0;n<512u && !(rd(SSR,1)&ORER);++n) {}
    wr(SCR,0,1);
    *receive_us=now_us()-start;
    uint32_t control=rd(CHCR,4);
    wr(CHCR,control&~UINT32_C(5),4);
    (void)rd(CHCR,4);
    settle(64);
    p->end_chcr=rd(CHCR,4);p->end_count=rd(TCR,4);p->end_ssr=rd(SSR,1);
    s->snapshot_ssr=p->end_ssr;s->snapshot_sptr=rd(SPTR,1);
    s->last_remaining=p->end_count;s->last_chcr=p->end_chcr;s->last_ssr=p->end_ssr;
    enum kui_sci_async_status result=KUI_SCI_ASYNC_OK;
    if(p->end_count || !(p->end_chcr&2u)) {
        if(stopped_channel_idle(p,bytes)) {result=KUI_SCI_ASYNC_RECEIVE_ERROR;++s->premature_errors;}
        else {p->quarantined=true;result=KUI_SCI_ASYNC_DMA_ERROR;}
    } else if(p->end_ssr&ORER) ++s->trailing_overruns;
    *remaining=rd(TCR,4);
    if(!p->quarantined) {
        wr(CHCR,0,4);
        if(*remaining) wr(TCR,0,4);
        p->expected_sar=rd(SAR,4);p->expected_dar=rd(DAR,4);
        p->expected_tcr=rd(TCR,4);p->expected_chcr=0;
        p->capture_stop=true;
    }
    if(p->end_ssr&RDRF) {p->rdr_byte=(uint8_t)rd(RDR,1);p->rdr_valid=true;}
    wr(SSR,p->end_ssr&~(RDRF|FLAGS),1);
    (void)rd(SSR,1);
    uint64_t masked=now_us()-masked_start;
    if(masked>*max_masked_us) *max_masked_us=masked;
    irq_restore(mask);
    return result;
}
/* Deselect, SCI module reset (after a trailing overrun) and re-initialization,
 * exactly as after a request; the card keeps its CMD18 state. */
static enum kui_sci_async_status stream_handoff(struct probe *p,uint32_t *reset_state) {
    enum kui_sci_async_status result=handoff(p,p->card,false);
    p->capture_stop=false;
    *reset_state=p->stage->module_reset_state;
    return result;
}
/* Polled ordinary-bus byte operations at the fast clock. */
static enum kui_sci_async_status sync_token(struct probe *p,uint32_t *count,
        uint64_t *us,uint32_t *token) {
    uint64_t start=now_us();
    p->stage->last_phase=KUI_SCI_ASYNC_PHASE_TOKEN;
    p->stage->framing_step=KUI_SCI_ASYNC_FRAMING_TOKEN;
    for(uint32_t n=0;;++n) {
        uint8_t value=byte(p->card,0xff,false);
        if(!bus_healthy(p)) return KUI_SCI_ASYNC_BUS_FAULT;
        if(value!=0xffu) {
            *count=n;*us=now_us()-start;*token=p->stage->last_token=value;
            return value==0xfeu?KUI_SCI_ASYNC_OK:KUI_SCI_ASYNC_TOKEN;
        }
        if(n>=65535u || (!(n&31u) && now_us()-start>=FRAME_TIMEOUT_US)) {
            *count=n;*us=now_us()-start;
            return KUI_SCI_ASYNC_TIMEOUT;
        }
    }
}
/* *issued is set once the first command byte has been clocked: from then on
 * the card may be streaming, so CMD12 must follow whatever happens. */
static enum kui_sci_async_status sync_cmd18(struct probe *p,uint32_t lba,
        uint32_t *response,bool *issued) {
    const struct kui_loader_sd *c=p->card;
    struct kui_sci_async_stage *s=p->stage;
    s->last_phase=KUI_SCI_ASYNC_PHASE_READY;
    s->framing_step=KUI_SCI_ASYNC_FRAMING_DESELECT;
    c->bus.select(c->bus.ctx,false);
    if(!bus_healthy(p)) return KUI_SCI_ASYNC_BUS_FAULT;
    s->framing_step=KUI_SCI_ASYNC_FRAMING_IDLE_CLOCK;
    (void)byte(c,0xff,false);
    if(!bus_healthy(p)) return KUI_SCI_ASYNC_BUS_FAULT;
    s->framing_step=KUI_SCI_ASYNC_FRAMING_SELECT;
    c->bus.select(c->bus.ctx,true);
    if(!bus_healthy(p)) return KUI_SCI_ASYNC_BUS_FAULT;
    s->framing_step=KUI_SCI_ASYNC_FRAMING_READY;
    uint64_t start=now_us();
    for(uint32_t n=1;byte(c,0xff,false)!=0xffu;++n) {
        if(!bus_healthy(p)) return KUI_SCI_ASYNC_BUS_FAULT;
        if(n>=65535u || (!(n&31u) && now_us()-start>=FRAME_TIMEOUT_US))
            return KUI_SCI_ASYNC_TIMEOUT;
    }
    if(!bus_healthy(p)) return KUI_SCI_ASYNC_BUS_FAULT;
    uint32_t address=c->high_capacity?lba:lba*512u;
    uint8_t command[6]={0x52,(uint8_t)(address>>24),(uint8_t)(address>>16),
        (uint8_t)(address>>8),(uint8_t)address,0};
    command[5]=command_crc(command,5);
    s->last_phase=KUI_SCI_ASYNC_PHASE_COMMAND;
    s->framing_step=KUI_SCI_ASYNC_FRAMING_COMMAND;
    *issued=true;
    for(unsigned i=0;i<6;++i) (void)byte(c,command[i],false);
    s->framing_step=KUI_SCI_ASYNC_FRAMING_RESPONSE;
    uint8_t value=0xff;
    for(unsigned i=0;i<16u && (value&0x80u);++i) value=byte(c,0xff,false);
    *response=s->command_response=value;
    if(!bus_healthy(p)) return KUI_SCI_ASYNC_BUS_FAULT;
    return value&0x80u?KUI_SCI_ASYNC_TIMEOUT:value?KUI_SCI_ASYNC_COMMAND:KUI_SCI_ASYNC_OK;
}
/* CMD12 straight into the stream (no ready wait), its stuff byte discarded,
 * R1, then the busy interval; deselect and one idle byte in every case. */
static enum kui_sci_async_status sync_stop(struct probe *p,uint32_t *response,
        uint32_t *busy,uint64_t *us) {
    static const uint8_t stop[6]={0x4c,0,0,0,0,0x61};
    const struct kui_loader_sd *c=p->card;
    struct kui_sci_async_stage *s=p->stage;
    uint64_t start=now_us();
    enum kui_sci_async_status result=KUI_SCI_ASYNC_OK;
    s->last_phase=KUI_SCI_ASYNC_PHASE_COMMAND;
    s->framing_step=KUI_SCI_ASYNC_FRAMING_COMMAND;
    if(!(rd(PDTR,2)&0x80u)) {
        /* Already selected (an early failure): send CMD12 as it is. */
    } else c->bus.select(c->bus.ctx,true);
    for(unsigned i=0;i<6;++i) (void)byte(c,stop[i],false);
    (void)byte(c,0xff,false);
    s->framing_step=KUI_SCI_ASYNC_FRAMING_RESPONSE;
    uint8_t value=0xff;
    for(unsigned i=0;i<20u && (value&0x80u);++i) value=byte(c,0xff,false);
    *response=value;
    if(value&0x80u) result=KUI_SCI_ASYNC_TIMEOUT;
    else {
        /* R1b: the card holds data low while busy. A latched bus fault
         * returns 0xff and ends this loop; it is reported below. */
        for(uint32_t n=0;byte(c,0xff,false)!=0xffu;) {
            *busy=++n;
            if(n>=1000000u || (!(n&31u) && now_us()-start>=FRAME_TIMEOUT_US)) {
                result=KUI_SCI_ASYNC_TIMEOUT;break;
            }
        }
        if(value) result=KUI_SCI_ASYNC_COMMAND;
    }
    s->framing_step=KUI_SCI_ASYNC_FRAMING_DESELECT;
    c->bus.select(c->bus.ctx,false);
    (void)byte(c,0xff,false);
    *us=now_us()-start;
    s->framing_step=KUI_SCI_ASYNC_FRAMING_NONE;
    return bus_healthy(p)?result:KUI_SCI_ASYNC_BUS_FAULT;
}
/* Measurement findings leave the reader usable once CMD12 has stopped the
 * card: what the card sent, not a failure of the reader's own hardware. */
static bool stream_finding(enum kui_sci_async_status status) {
    return status==KUI_SCI_ASYNC_CRC || status==KUI_SCI_ASYNC_TOKEN ||
        status==KUI_SCI_ASYNC_TIMEOUT || status==KUI_SCI_ASYNC_RECEIVE_ERROR;
}
/* Reverse every received byte, then walk block, gap, token, block... */
static void stream_parse(uint8_t *b,struct kui_sci_async_stream *out) {
    uint32_t received=out->received,pos=0;
    for(uint32_t i=0;i<received;++i) b[i]=reverse_table[b[i]];
    while(received-pos>=514u) {
        uint16_t crc=0;
        for(unsigned i=0;i<512;++i) crc=(uint16_t)((crc<<8)^crc_table[(uint8_t)(crc>>8)^b[pos+i]]);
        if(crc!=(uint16_t)(((uint16_t)b[pos+512]<<8)|b[pos+513])) {++out->crc_errors;break;}
        if(pos!=out->blocks*512u) memmove(b+out->blocks*512u,b+pos,512);
        ++out->blocks;pos+=514u;
        uint32_t gap=0;
        while(pos<received && b[pos]==0xffu) {++gap;++pos;}
        if(pos==received) break;
        out->last_token=b[pos++];
        if(out->last_token!=0xfeu) {++out->token_errors;break;}
        if(out->gaps<KUI_SCI_ASYNC_STREAM_GAPS) out->gap[out->gaps]=(uint16_t)(gap<65535u?gap:65535u);
        if(!out->gaps || gap<out->gap_min) out->gap_min=gap;
        if(gap>out->gap_max) out->gap_max=gap;
        out->gap_total+=gap;++out->gaps;
    }
}
/* Common end of both measurements: stop the card if a CMD18 was issued,
 * record the outcome and fail the reader unless the problem is a finding
 * about the data the card sent after accepting CMD18 (streaming). */
static enum kui_sci_async_status stream_end(struct probe *p,
        struct kui_sci_async_stage *saved,bool issued,bool streaming,
        enum kui_sci_async_status result,uint32_t *stop_response,uint32_t *stop_busy,
        uint64_t *stop_us,enum kui_sci_async_status *status) {
    /* status keeps the first problem; the reader fails for anything but a
     * finding, and always when the card could not be stopped. */
    enum kui_sci_async_status measured=result;
    bool usable=result==KUI_SCI_ASYNC_OK || (streaming && stream_finding(result));
    bool stopped=true;
    if(issued) {
        enum kui_sci_async_status stop=KUI_SCI_ASYNC_BUS_FAULT; /* The card may still stream. */
        if(!p->quarantined && !p->foreign_dma && !p->module_unavailable && kui_sci_sd_healthy())
            stop=sync_stop(p,stop_response,stop_busy,stop_us);
        if(stop!=KUI_SCI_ASYNC_OK) {
            stopped=false;
            if(usable) result=stop;
        }
    }
    *status=measured!=KUI_SCI_ASYNC_OK?measured:result;
    if(usable && stopped) {
        if(measured==KUI_SCI_ASYNC_OK) ++p->stage->passed;
        p->stage->last_phase=KUI_SCI_ASYNC_PHASE_COMPLETE;
        p->stage=saved;
        return KUI_SCI_ASYNC_OK;
    }
    enum kui_sci_async_status failure=failed(p,result);
    p->stage=saved;
    return failure;
}

enum kui_sci_async_status kui_sci_async_stream_capture(struct kui_sci_async_reader *reader,
        uint32_t lba,void *buffer,uint32_t bytes,struct kui_sci_async_stream *out) {
    struct probe *p=reader_state(reader);
    if(!p || !out) return KUI_SCI_ASYNC_ARGUMENT;
    *out=(struct kui_sci_async_stream){.status=KUI_SCI_ASYNC_ARGUMENT,.lba=lba,.bytes=bytes,
        .command_response=0xffu,.last_token=0xffu,.stop_response=0xffu};
    if(p->phase!=READER_IDLE) return out->status=KUI_SCI_ASYNC_BUSY;
    uint32_t address=0;
    if(buffer && !((uintptr_t)buffer&31u) && !(bytes&31u) && bytes>=1056u &&
       bytes<=32768u && block_range(p,lba,bytes/514u+1u)) address=physical(buffer,bytes);
    if(!address) return out->status;
    uint64_t start=now_us();
    struct kui_sci_async_stage *saved=p->stage;
    p->stage=&p->out->cmd18;++p->stage->attempted;
    p->capture_stop=false;out->status=KUI_SCI_ASYNC_OK;
    bool issued=false;
    enum kui_sci_async_status result=KUI_SCI_ASYNC_OK;
    if(!dma_unchanged(p)) {p->foreign_dma=true;result=KUI_SCI_ASYNC_DMA_ERROR;}
    if(result==KUI_SCI_ASYNC_OK) result=sync_cmd18(p,lba,&out->command_response,&issued);
    bool streaming=result==KUI_SCI_ASYNC_OK;
    if(result==KUI_SCI_ASYNC_OK)
        result=sync_token(p,&out->first_token_bytes,&out->first_token_us,&out->last_token);
    if(result==KUI_SCI_ASYNC_OK) {
        cache(buffer,bytes,false);
        uint32_t remaining=bytes;
        result=masked_receive(p,address,bytes,&remaining,&out->capture_us,&out->masked_us);
        out->end_ssr=p->end_ssr;out->end_count=p->end_count;
        if(result==KUI_SCI_ASYNC_OK || result==KUI_SCI_ASYNC_RECEIVE_ERROR) {
            out->received=bytes-remaining;out->complete=result==KUI_SCI_ASYNC_OK;
            /* The stopped channel was proven idle: drop stale lines now. */
            cache(buffer,bytes,true);
            enum kui_sci_async_status handed=stream_handoff(p,&out->reset_state);
            if(handed!=KUI_SCI_ASYNC_OK) result=handed;
        }
    }
    result=stream_end(p,saved,issued,streaming,result,&out->stop_response,&out->stop_busy_bytes,
        &out->stop_us,&out->status);
    if(out->received) {
        stream_parse(buffer,out);
        if(out->status==KUI_SCI_ASYNC_OK && (out->crc_errors || out->token_errors))
            out->status=out->crc_errors?KUI_SCI_ASYNC_CRC:KUI_SCI_ASYNC_TOKEN;
    }
    out->elapsed_us=now_us()-start;
    return result;
}

enum kui_sci_async_status kui_sci_async_stream_resume(struct kui_sci_async_reader *reader,
        uint32_t lba,uint32_t count,uint8_t *dst,struct kui_sci_async_resume *out) {
    struct probe *p=reader_state(reader);
    if(!p || !out) return KUI_SCI_ASYNC_ARGUMENT;
    *out=(struct kui_sci_async_resume){.status=KUI_SCI_ASYNC_ARGUMENT,.lba=lba,.requested=count,
        .command_response=0xffu,.last_token=0xffu,.stop_response=0xffu};
    if(p->phase!=READER_IDLE) return out->status=KUI_SCI_ASYNC_BUSY;
    if(!dst || !count || count>KUI_SCI_ASYNC_RESUME_BLOCKS || !block_range(p,lba,count))
        return out->status;
    p->buffer_address=physical(p->rx.bytes,sizeof(p->rx.bytes));
    if(!p->buffer_address) return out->status=KUI_SCI_ASYNC_UNSUPPORTED;
    uint64_t start=now_us();
    struct kui_sci_async_stage *saved=p->stage;
    p->stage=&p->out->cmd18;++p->stage->attempted;
    p->capture_stop=false;out->status=KUI_SCI_ASYNC_OK;
    bool issued=false;
    enum kui_sci_async_status result=KUI_SCI_ASYNC_OK;
    if(!dma_unchanged(p)) {p->foreign_dma=true;result=KUI_SCI_ASYNC_DMA_ERROR;}
    if(result==KUI_SCI_ASYNC_OK) result=sync_cmd18(p,lba,&out->command_response,&issued);
    bool streaming=result==KUI_SCI_ASYNC_OK;
    if(result==KUI_SCI_ASYNC_OK)
        result=sync_token(p,&out->first_token_bytes,&out->first_token_us,&out->last_token);
    for(uint32_t i=0;result==KUI_SCI_ASYNC_OK && i<count;++i) {
        memset(&p->rx,SENTINEL,sizeof(p->rx));
        cache(&p->rx,sizeof(p->rx),false);
        uint32_t remaining=513u;uint64_t received_us=0;
        result=masked_receive(p,p->buffer_address,513u,&remaining,&received_us,&out->max_masked_us);
        out->receive_us+=received_us;
        if(result!=KUI_SCI_ASYNC_OK && result!=KUI_SCI_ASYNC_RECEIVE_ERROR) break;
        cache(&p->rx,sizeof(p->rx),true);
        uint64_t reset_start=now_us();
        enum kui_sci_async_status handed=stream_handoff(p,&out->reset_state);
        uint64_t check_start=now_us();
        out->reset_us+=check_start-reset_start;
        if(handed!=KUI_SCI_ASYNC_OK) {result=handed;break;}
        if(result!=KUI_SCI_ASYNC_OK) break;
        /* The overrun that stopped reception left the second CRC byte in RDR. */
        if(!p->rdr_valid) {++out->missing_tail_bytes;result=KUI_SCI_ASYNC_RECEIVE_ERROR;break;}
        p->rx.bytes[513]=p->rdr_byte;
        if(!guards(&p->rx)) {++out->guard_errors;result=KUI_SCI_ASYNC_GUARD;break;}
        uint16_t crc=0;
        const uint8_t *bytes=p->rx.bytes;uint8_t *block=dst+(size_t)i*512u;
        for(unsigned j=0;j<512;++j) {
            uint8_t value=reverse_table[bytes[j]];
            block[j]=value;crc=(uint16_t)((crc<<8)^crc_table[(uint8_t)(crc>>8)^value]);
        }
        uint16_t wire=(uint16_t)((uint16_t)reverse_table[bytes[512]]<<8)|reverse_table[bytes[513]];
        out->check_us+=now_us()-check_start;
        if(crc!=wire) {++out->crc_errors;result=KUI_SCI_ASYNC_CRC;break;}
        ++out->blocks;
        if(i+1u<count) {
            p->card->bus.select(p->card->bus.ctx,true);
            uint32_t waited=0;uint64_t us=0;
            result=sync_token(p,&waited,&us,&out->last_token);
            if(result==KUI_SCI_ASYNC_TOKEN) ++out->token_errors;
            out->token_bytes+=waited;out->token_us+=us;
            if(waited>out->max_token_bytes) out->max_token_bytes=waited;
            if(us>out->max_token_us) out->max_token_us=us;
        }
    }
    result=stream_end(p,saved,issued,streaming,result,&out->stop_response,&out->stop_busy_bytes,
        &out->stop_us,&out->status);
    out->elapsed_us=now_us()-start;
    return result;
}
uint32_t kui_sci_async_work_sample(struct kui_sci_async_reader *reader) {
    struct probe *p=reader_state(reader);
    if(!p || !p->armed || p->phase!=READER_DMA) return 0;
    if(!active_dma_owned(p)) return 0;
    uint32_t chcr=rd(CHCR,4),remaining=rd(TCR,4);
    if((chcr&3u)!=1u || chcr!=rd(CHCR,4) || !remaining || remaining>=p->dma_bytes || !p->armed)
        return 0;
    return remaining;
}
void kui_sci_async_work_record(struct kui_sci_async_reader *reader,
        uint32_t before,uint32_t iterations,uint32_t checksum) {
    struct probe *p=reader_state(reader);if(!p) return;
    uint32_t after=kui_sci_async_work_sample(reader);
    if(before>after && after>0 && before<p->dma_bytes) {
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
        "cancelled","command rejected","data token error","timeout","receive error","DMA error",
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
        "ready poll","command","response","token","handoff deselect","drain","reselect"};
    return (unsigned)s<sizeof(names)/sizeof(names[0])?names[s]:"unknown";
}
