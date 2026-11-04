/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/retail_demand_diagnostic.h"
#include "kui/armada_ce_excerpt.h"
#include "kui/armada_ce_stream_transport.h"
#include "retail_async.h"
#include <stddef.h>
#if KUI_RETAIL_DEMAND_DIAGNOSTIC == 3
#include "kui/retail_demand_window.h"
#endif
#if !KUI_ARMADA_CE_STREAM || !defined(KUI_RETAIL_CE) || !KUI_RETAIL_DEMAND_DIAGNOSTIC
#error "Demand collector belongs only to its isolated CE stream controller"
#endif
const uint32_t kui_armada_ce_demand_contract[4]={
    UINT32_C(0x444d4544),KUI_RETAIL_DEMAND_VERSION,KUI_RETAIL_DEMAND_DIAGNOSTIC,
    KUI_RETAIL_DEMAND_PROFILE_ID};

/* All new state lives in the already reserved high controller. The low
 * engine, vector gaps, receive areas and stack retain their exact geometry. */
struct demand_frame {
    struct kui_retail_demand_clock clock;
    uint32_t active,function,sr,command,lba,count,output,blocks,polled;
    uint32_t token_bytes,overruns,spill,flags,pio_bytes,token,irq_blocks,call_blocks,source;
};
static struct {
    struct demand_frame frame[2];
    uint32_t calls[15],gd,irq,unpaired,masked,blocked,last_sr;
    uint32_t gd_zero,irq_zero,waiting_dma,no_irq,pio_max,pio_min,dma_max;
    uint32_t physical,virtual_piece,mod_max[2],cycle_max[2];
    uint32_t cycle_sum[2][2],cycle_min[2],timed[2];
    uint32_t mod_invalid,cycle_invalid,mod_wrap,cycle_clip;
    uint32_t accepted,completed,revoked,request_token,request_bytes;
    struct kui_retail_demand_clock request_clock;
    uint32_t request_max,request_invalid,request_lba,request_count;
    uint32_t max_context[3][5],last[5],history[6][5],history_count;
    struct kui_retail_demand_clock last_clock;
    struct kui_retail_demand_page page;
    uint32_t terminal_guard;
#if KUI_RETAIL_DEMAND_DIAGNOSTIC == 3
    struct kui_retail_demand_window window;
    uint32_t sources[5],commands[5],notifications[2][2],irq_returns[3];
    struct kui_retail_async frozen_engine;
    struct kui_sci_stream_stats frozen_stream;
#endif
} demand;

static void demand_clock_sample(struct kui_retail_demand_clock *out) {
#ifdef KUI_RETAIL_DEMAND_DIAGNOSTIC_TEST
    extern void kui_retail_demand_test_clock(struct kui_retail_demand_clock *);
    kui_retail_demand_test_clock(out);
#else
    out->ticks=*(volatile const uint32_t *)(uintptr_t)0xffd8000cu;
    out->tcor=*(volatile const uint32_t *)(uintptr_t)0xffd80008u;
    out->tcr=*(volatile const uint16_t *)(uintptr_t)0xffd80010u;
    out->tstr=*(volatile const uint8_t *)(uintptr_t)0xffd80004u;
    out->frqcr=*(volatile const uint16_t *)(uintptr_t)0xffc00000u;
    out->pmcr=*(volatile const uint16_t *)(uintptr_t)0xff000084u;
    /* Stable48-bit read, bounded to two low-word reads. At200MHz the high
     * word changes only every21.47s; a second rollover cannot occur here. */
    uint32_t high=*(volatile const uint32_t *)(uintptr_t)0xff100004u&0xffffu;
    out->cycles_lo=*(volatile const uint32_t *)(uintptr_t)0xff100008u;
    out->cycles_hi=*(volatile const uint32_t *)(uintptr_t)0xff100004u&0xffffu;
    if(high!=out->cycles_hi)
        out->cycles_lo=*(volatile const uint32_t *)(uintptr_t)0xff100008u;
#endif
}
static bool demand_region_valid(uintptr_t address) {
#ifdef KUI_RETAIL_DEMAND_DIAGNOSTIC_TEST
    extern bool kui_retail_demand_test_region(uintptr_t);
    return kui_retail_demand_test_region(address);
#else
    return !(address&31u) && address>=0x8c008300u &&
        address<=0x8c00d800u-sizeof(struct kui_retail_async_region);
#endif
}
static uint32_t demand_flags(const struct kui_retail_async_region *r) {
    const struct kui_retail_async *e=&r->engine;
    /* These are observed software states, not an inference that the next
     * interrupt is guaranteed. bit7 distinguishes a ready predecessor. */
    return (e->active?1u:0u)|(e->piece_set?2u:0u)|(e->piece_physical?4u:0u)|
        (e->isr?8u:0u)|(r->shared.stream.state==2u?16u:0u)|
        (r->shared.stream.token_bounded && !r->shared.stream.token_budget?32u:0u)|
        (e->spill_bytes>KUI_RETAIL_ASYNC_SPILL_BYTES-512u?64u:0u)|
        (r->shared.stream.ready[0] || r->shared.stream.ready[1]?128u:0u);
}
static uint32_t demand_blocks(const struct kui_retail_async_region *r) {
    return r->engine.stats.irq_blocks+r->engine.stats.call_blocks;
}
static bool demand_reads(uint32_t command) {
    return command==KUI_GD_PIOREAD || command==KUI_GD_DMAREAD ||
        command==KUI_RETAIL_GD_DMAREAD_STREAM || command==KUI_RETAIL_GD_PIOREAD_STREAM;
}
static void demand_begin(unsigned kind,uint32_t function,uint32_t sr,
    const struct kui_retail_async_region *r) {
    struct demand_frame *f=&demand.frame[kind];
    if(f->active) {++demand.unpaired;return;}
    f->active=1u;f->function=function;f->sr=sr;
    f->command=r->shared.service.command;f->lba=r->shared.service.lba;
    f->count=r->shared.service.count;f->output=r->shared.service.completed_bytes;
    f->blocks=demand_blocks(r);f->polled=r->shared.stream.stats.polled;
    f->token_bytes=r->shared.stream.stats.token_bytes;
    f->overruns=r->shared.stream.stats.overruns;f->spill=r->engine.spill_bytes;
    f->pio_bytes=r->engine.pio_bytes;
    f->token=r->shared.service.token;
    f->irq_blocks=r->engine.stats.irq_blocks;f->call_blocks=r->engine.stats.call_blocks;
    f->flags=demand_flags(r);demand_clock_sample(&f->clock);
}
static void demand_finish(unsigned kind,uint32_t function,uint32_t result,uint32_t notifications,
    const struct kui_retail_async_region *r) {
    struct demand_frame *f=&demand.frame[kind];
    struct kui_retail_demand_clock now;demand_clock_sample(&now);demand.last_clock=now;
    if(!f->active || f->function!=function) {++demand.unpaired;return;}
    f->active=0u;
    struct kui_retail_demand_elapsed time=kui_retail_demand_elapsed(&f->clock,&now);
    bool new_mod=false;
    if(time.flags&KUI_RETAIL_DEMAND_TMU_VALID) {
        if(time.modulo>demand.mod_max[kind]) {demand.mod_max[kind]=time.modulo;new_mod=true;}
        if(time.flags&KUI_RETAIL_DEMAND_TMU_WRAP) ++demand.mod_wrap;
    } else ++demand.mod_invalid;
    if(time.flags&KUI_RETAIL_DEMAND_CYCLES_VALID) {
        if(time.flags&KUI_RETAIL_DEMAND_CYCLES_CLIPPED) ++demand.cycle_clip;
        else {
            uint32_t before=demand.cycle_sum[kind][0];
            demand.cycle_sum[kind][0]+=time.cycles;
            if(demand.cycle_sum[kind][0]<before) ++demand.cycle_sum[kind][1];
            if(!demand.timed[kind] || time.cycles<demand.cycle_min[kind])
                demand.cycle_min[kind]=time.cycles;
            ++demand.timed[kind];
        }
    } else ++demand.cycle_invalid;
    uint32_t checked=demand_blocks(r)-f->blocks;
    uint32_t output=r->shared.service.completed_bytes>=f->output?
        r->shared.service.completed_bytes-f->output:0u;
#if KUI_RETAIL_DEMAND_DIAGNOSTIC == 3
    if(r->shared.service.token!=f->token) output=r->shared.service.completed_bytes;
    if(notifications&KUI_RETAIL_GD_IRQ_DRIVE) ++demand.notifications[kind][0];
    if(notifications&KUI_RETAIL_GD_IRQ_DMA_END) ++demand.notifications[kind][1];
    if(kind) {
        if(result==0u) ++demand.irq_returns[0];
        else if(result==20u) ++demand.irq_returns[1];
        else if(result==21u) ++demand.irq_returns[2];
    } else if(f->source<5u) ++demand.sources[f->source];
    struct kui_retail_demand_window_visit visit={kind,
        r->shared.service.command?r->shared.service.command:f->command,
        r->engine.stats.irq_blocks-f->irq_blocks,r->engine.stats.call_blocks-f->call_blocks,
        output,f->sr,(f->flags&16u) && !checked,!checked && !output};
    kui_retail_demand_window_record(&demand.window,&f->clock,&now,&visit);
#else
    (void)notifications;
#endif
    uint32_t after=demand_flags(r);
    if(kind) {++demand.irq;if(!checked && !output) ++demand.irq_zero;}
    else {
        ++demand.gd;++demand.calls[function<14u?function:14u];
        demand.last_sr=f->sr;
        if(f->sr&0xf0u) ++demand.masked;
        if(f->sr&0x10000000u) ++demand.blocked;
        if(!checked && !output && r->shared.service.pending && demand_reads(r->shared.service.command))
            ++demand.gd_zero;
        if(function==KUI_GD_PIO_TRANSFER && r->engine.pio_calls && r->engine.pio_bytes!=f->pio_bytes) {
            uint32_t n=r->engine.pio_bytes-f->pio_bytes;
            if(n>demand.pio_max) demand.pio_max=n;
            if(!demand.pio_min || n<demand.pio_min) demand.pio_min=n;
        }
        if(function==KUI_GD_DMA_TRANSFER && !result &&
           r->engine.piece_bytes>demand.dma_max) demand.dma_max=r->engine.piece_bytes;
        if((function==KUI_GD_DMA_TRANSFER || function==KUI_GD_PIO_TRANSFER) && !result) {
            if(r->engine.piece_physical) ++demand.physical;
            else ++demand.virtual_piece;
        }
        if(function==KUI_GD_REQUEST && (int32_t)result>0 && demand_reads(r->shared.service.command)) {
            if(demand.request_token) ++demand.revoked;
            ++demand.accepted;demand.request_token=r->shared.service.token;
#if KUI_RETAIL_DEMAND_DIAGNOSTIC == 3
            uint32_t command=r->shared.service.command;
            unsigned index=command==16u?0u:command==17u?1u:command==38u?2u:command==39u?3u:4u;
            ++demand.commands[index];
#endif
            demand.request_bytes=r->shared.service.request_bytes;
            demand.request_clock=f->clock;
        }
        demand.last[0]=function;demand.last[1]=result;
        demand.last[2]=r->shared.service.command;demand.last[3]=r->shared.service.lba;
        demand.last[4]=r->shared.service.count;
        if(checked || output || function==KUI_GD_DMA_TRANSFER || function==KUI_GD_PIO_TRANSFER) {
            uint32_t *h=demand.history[demand.history_count++%6u];
            h[0]=function|(r->shared.service.command<<8);h[1]=r->shared.service.lba;
            h[2]=output;h[3]=(time.flags&KUI_RETAIL_DEMAND_CYCLES_VALID)?time.cycles:UINT32_MAX;
            h[4]=after;
        }
    }
    if((f->flags&16u) && !checked) ++demand.waiting_dma;
    if((after&1u) && !(after&16u)) ++demand.no_irq;
    bool new_cycles=(time.flags&KUI_RETAIL_DEMAND_CYCLES_VALID) && time.cycles>demand.cycle_max[kind];
    if(new_cycles) {
        demand.cycle_max[kind]=time.cycles;
    }
    /* If CE did not retain the elapsed counter, preserve the context of the
     * largest modulo lower bound, explicitly marked by TIMEFLAGS. It is not
     * evidence that the true longest call was shorter than a timer period. */
    if(!kind && (new_cycles || (!demand.cycle_max[0] && new_mod))) {
            uint32_t (*c)[5]=demand.max_context;
            c[0][0]=function;c[0][1]=r->shared.service.command?r->shared.service.command:f->command;
            c[0][2]=r->shared.service.lba;
            c[0][3]=r->shared.service.count;c[0][4]=f->sr;
            c[1][0]=output;c[1][1]=checked;c[1][2]=r->shared.stream.stats.polled-f->polled;
            c[1][3]=r->shared.stream.stats.token_bytes-f->token_bytes;
            c[1][4]=r->shared.stream.stats.overruns-f->overruns;
            c[2][0]=f->flags;c[2][1]=after;c[2][2]=f->spill;c[2][3]=r->engine.spill_bytes;
            c[2][4]=time.flags;
    }
    if(demand.request_token && (!r->shared.service.pending ||
       demand.request_token!=r->shared.service.token)) {
        if(demand.request_token==r->shared.service.token && !r->shared.service.error &&
           r->shared.service.completed_bytes==demand.request_bytes) {
            ++demand.completed;
            struct kui_retail_demand_elapsed age=kui_retail_demand_elapsed(&demand.request_clock,&now);
            if((age.flags&KUI_RETAIL_DEMAND_CYCLES_VALID) && !(age.flags&KUI_RETAIL_DEMAND_CYCLES_CLIPPED)) {
                if(age.cycles>demand.request_max) {
                    demand.request_max=age.cycles;demand.request_lba=r->shared.service.lba;
                    demand.request_count=r->shared.service.count;
                }
            } else ++demand.request_invalid;
        } else ++demand.revoked;
        demand.request_token=0u;
    }
}
static void demand_row(unsigned n,uint32_t a,uint32_t b,uint32_t c,uint32_t d,uint32_t e) {
    uint32_t *r=demand.page.rows[n];r[0]=a;r[1]=b;r[2]=c;r[3]=d;r[4]=e;
}
#if KUI_RETAIL_DEMAND_DIAGNOSTIC == 3
static const struct kui_retail_demand_page *demand_window_report(unsigned page) {
    static const char titles[4][48]={"LAST16S 1/4 - OLDEST EXIT BINS",
        "LAST16S 2/4 - EXIT BINS","LAST16S 3/4 - EXIT BINS","LAST16S 4/4 - NEWEST EXIT BINS"};
    static const char labels[7][48]={"FLAGS-A FLAGS-B EPOCH BINS STATE",
        "BIN MODE GD IRQ COVERED-CYC","IRQBLK CALLBLK OUTBYTES GD-CYC IRQ-CYC",
        "MAXBODY ENTRY-GAP MASKED ZERO DMA-ZERO",
        "BIN MODE GD IRQ COVERED-CYC","IRQBLK CALLBLK OUTBYTES GD-CYC IRQ-CYC",
        "MAXBODY ENTRY-GAP MASKED ZERO DMA-ZERO"};
    demand.page.title=(uint32_t)(uintptr_t)titles[page];
    for(unsigned row=0;row<7u;row++) demand.page.labels[row]=(uint32_t)(uintptr_t)labels[row];
    uint32_t flags[2];
    for(unsigned slot=0;slot<2u;slot++) {
        const struct kui_retail_demand_window_bin *b=kui_retail_demand_window_oldest(&demand.window,page*2u+slot);
        unsigned row=slot*3u+1u;
        flags[slot]=b?b->flags:UINT32_MAX;
        if(b) {
            if(b->gd_hi || b->irq_hi) flags[slot]|=KUI_RETAIL_DEMAND_WINDOW_BODY_CLIPPED;
            demand_row(row,b->ordinal,b->modes,b->gd,b->irq,b->span_cycles);
            demand_row(row+1u,b->irq_blocks,b->call_blocks,b->bytes,b->gd_hi?UINT32_MAX:b->gd_lo,
                b->irq_hi?UINT32_MAX:b->irq_lo);
            demand_row(row+2u,b->max_body,b->gap_max,b->masked,b->zero,b->dma_zero);
        } else {
            demand_row(row,UINT32_MAX,0u,0u,0u,UINT32_MAX);
            demand_row(row+1u,0u,0u,0u,UINT32_MAX,UINT32_MAX);
            demand_row(row+2u,UINT32_MAX,UINT32_MAX,0u,0u,0u);
        }
    }
    demand_row(0,flags[0],flags[1],demand.window.epoch,demand.window.count,
        demand.window.flags|(demand.window.frozen?0x80000000u:0u));
    return &demand.page;
}
#endif
static const struct kui_retail_demand_page *demand_report_page(unsigned page,const struct kui_retail_async_region *r) {
#if KUI_RETAIL_DEMAND_DIAGNOSTIC == 3
    if(page>=5u && page<9u) return demand_window_report(page-5u);
#endif
    static const char titles[5][48]={"CE DATA 1/5 - PROFILE AND ENGAGEMENT",
        "CE DATA 2/5 - EXISTING TRANSPORT COUNTERS",
        "CE DATA 3/5 - MAX OBSERVED BODY CONTEXT",
        "CE DATA 4/5 - LAST SIX DATA GD BODIES",
        "CE DATA 5/5 - CDDA ACTIVITY OR INACTIVITY"};
    static const char labels[5][7][48]={
        {"VERSION MODE PROFILE-ID GD IRQ","FN0 1 2 3 4","FN5 6 7 8 9",
         "FN10 11 12 13 OTHER","LAST-SR MASKED BL GDZERO IRQZERO",
         "GD-MOD IRQ-MOD BAD-MOD WRAP NO-DMA",
         "GD-CYC IRQ-CYC BAD-CYC CLIP DMA-ZERO"},
        {"IRQBLK CALLBLK WAIT PIOCALL PIOBYTE","PIOMAX PIOMIN DMAMAX PHYS VIRT",
         "QPEAK QBYTES QFULL QLEFT ACTIVE","DMA PIO CMD KEEP OVERRUN",
         "CRC TOKEN FOREIGN REPAIR AHEAD","DEFER LEFT0 LEFT128 LEFT384 LEFT513",
         "TOKBYTES TOKMAX STOPS TOKYIELD ISR"},
        {"READS FINISHED REVOKED REQ-CYC BAD-REQ",
         "MAX-FN CMD LBA COUNT CALLER-SR","OUTBYTES CARD-BLK PIOBLK TOKBYTES OVR",
         "BEFORE AFTER QBEFORE QAFTER TIMEFLAGS","LAST-FN RESULT CMD LBA COUNT",
         "FRQCR TCOR0 TCR0 TSTR PMCR1","REQ-LBA REQ-COUNT PEND TOKEN UNPAIRED"},
        {"RECORDS TIMED-GD TIMED-IRQ MIN-GD MIN-IRQ",
         "FN+CMD LBA OUTBYTES CYCLES FLAGS","FN+CMD LBA OUTBYTES CYCLES FLAGS",
         "FN+CMD LBA OUTBYTES CYCLES FLAGS","FN+CMD LBA OUTBYTES CYCLES FLAGS",
         "FN+CMD LBA OUTBYTES CYCLES FLAGS","FN+CMD LBA OUTBYTES CYCLES FLAGS"},
        {"SOURCE GAPS WORKER PLAY ATTEMPT","STATUS REQUESTS REFUSED HR COMMAND",
         "FIRST LAST REPEAT PLAYCUR WRITECUR","CURRENT OWNER DEVICE BUFFER VOICES",
         "FAULT ADDRESS OBSERVED EXPECTED DETAIL","CH2-LATE DMAOR-BAD GUARD RESERVED RESERVED",
         "GD-SUMLO HI IRQ-SUMLO HI TIMED"}};
    if(page>=KUI_RETAIL_DEMAND_PAGES) page=0u;
    demand.page.title=(uint32_t)(uintptr_t)titles[page];
    for(unsigned n=0;n<7u;n++) demand.page.labels[n]=(uint32_t)(uintptr_t)labels[page][n];
    const struct kui_retail_async *e=&r->engine;
    const struct kui_sci_stream_stats *s=&r->shared.stream.stats;
#if KUI_RETAIL_DEMAND_DIAGNOSTIC == 3
    if(demand.window.frozen) {e=&demand.frozen_engine;s=&demand.frozen_stream;}
#endif
    const volatile uint32_t *contract=kui_armada_ce_demand_contract;
    if(page==0u) {
        demand_row(0,contract[1],contract[2],contract[3],demand.gd,demand.irq);
        for(unsigned n=0;n<3u;n++) demand_row(n+1u,demand.calls[n*5u],demand.calls[n*5u+1u],
            demand.calls[n*5u+2u],demand.calls[n*5u+3u],demand.calls[n*5u+4u]);
        demand_row(4,demand.last_sr,demand.masked,demand.blocked,demand.gd_zero,demand.irq_zero);
        demand_row(5,demand.mod_max[0],demand.mod_max[1],demand.mod_invalid,demand.mod_wrap,demand.no_irq);
        demand_row(6,demand.cycle_max[0],demand.cycle_max[1],demand.cycle_invalid,demand.cycle_clip,demand.waiting_dma);
    } else if(page==1u) {
        demand_row(0,e->stats.irq_blocks,e->stats.call_blocks,e->stats.waits,e->pio_calls,e->pio_bytes);
        demand_row(1,demand.pio_max,demand.pio_min,demand.dma_max,demand.physical,demand.virtual_piece);
        demand_row(2,e->queue_highwater,e->prefetched_bytes,e->queue_blocked,e->spill_bytes,e->active);
        demand_row(3,s->blocks,s->polled,s->starts,s->kept,s->overruns);
        demand_row(4,s->crc_errors,s->token_errors,s->foreign,s->repaired,s->ahead);
        demand_row(5,s->deferred,s->incomplete[0],s->incomplete[1],s->incomplete[2],s->incomplete[3]);
        demand_row(6,s->token_bytes,s->token_max,s->stops,s->token_yields,e->isr);
    } else if(page==2u) {
#if KUI_RETAIL_DEMAND_DIAGNOSTIC == 3
        static const char title[]="CE OBSERVER - MODES AND CLOCK STATUS";
        static const char labels[7][48]={"GD-SOURCE0 1 2 3 4","REQUEST16 17 38 39 OTHER",
            "GD-NOTIFY20 21 SCI-NOTIFY20 21 UNPAIRED","SCI-RET0 20 21 EPOCH FLAGS",
            "FRQCR TCOR0 TCR0 TSTR PMCR1","UNTIMED GD IRQ MASKED ZERO BADCLK",
            "UNTIMED IRQBLK CALLBLK BYTES SOUND23 WHY"};
        demand.page.title=(uint32_t)(uintptr_t)title;
        for(unsigned i=0;i<7u;i++) demand.page.labels[i]=(uint32_t)(uintptr_t)labels[i];
        demand_row(0,demand.sources[0],demand.sources[1],demand.sources[2],demand.sources[3],demand.sources[4]);
        demand_row(1,demand.commands[0],demand.commands[1],demand.commands[2],demand.commands[3],demand.commands[4]);
        demand_row(2,demand.notifications[0][0],demand.notifications[0][1],demand.notifications[1][0],demand.notifications[1][1],demand.unpaired);
        demand_row(3,demand.irq_returns[0],demand.irq_returns[1],demand.irq_returns[2],demand.window.epoch,demand.window.flags);
        demand_row(4,demand.last_clock.frqcr,demand.last_clock.tcor,demand.last_clock.tcr,
            demand.last_clock.tstr,demand.last_clock.pmcr);
        demand_row(5,demand.window.untimed.gd,demand.window.untimed.irq,demand.window.untimed.masked,
            demand.window.untimed.zero,demand.window.bad_clock);
        /* Existing SCI adapter never receives native sound SYSINTR23.
         * FFFFFFFF + WHY1 means unavailable because no sound hook exists. */
        demand_row(6,demand.window.untimed.irq_blocks,demand.window.untimed.call_blocks,
            demand.window.untimed.bytes,UINT32_MAX,1u);
#else
        demand_row(0,demand.accepted,demand.completed,demand.revoked,demand.request_max,demand.request_invalid);
        for(unsigned n=0;n<3u;n++) for(unsigned i=0;i<5u;i++)
            demand.page.rows[n+1u][i]=demand.max_context[n][i];
        for(unsigned i=0;i<5u;i++) demand.page.rows[4][i]=demand.last[i];
        demand_row(5,demand.last_clock.frqcr,demand.last_clock.tcor,demand.last_clock.tcr,
            demand.last_clock.tstr,demand.last_clock.pmcr);
        demand_row(6,demand.request_lba,demand.request_count,r->shared.service.pending,
            r->shared.service.token,demand.unpaired);
#endif
    } else if(page==3u) {
        demand_row(0,demand.history_count,demand.timed[0],demand.timed[1],demand.cycle_min[0],demand.cycle_min[1]);
        unsigned count=demand.history_count<6u?demand.history_count:6u;
        unsigned first=demand.history_count-count;
        for(unsigned n=0;n<6u;n++) for(unsigned i=0;i<5u;i++)
            demand.page.rows[n+1u][i]=n<count?demand.history[(first+n)%6u][i]:0u;
    } else {
        struct kui_armada_ce_excerpt_snapshot audio;uint32_t progress[3],fault[5];
        kui_armada_ce_excerpt_snapshot(&audio);kui_armada_ce_stream_progress(progress);
        kui_armada_ce_stream_fault(fault);
        demand_row(0,progress[0],progress[1],progress[2],audio.play_ok,audio.attempts);
        demand_row(1,audio.status,audio.requests,audio.refused,audio.last_hresult,audio.command);
        demand_row(2,audio.first,audio.last,audio.repeat,audio.play_cursor,audio.write_cursor);
        demand_row(3,audio.current_process,audio.owner_process,audio.device,audio.buffer,audio.free_voices);
        for(unsigned i=0;i<5u;i++) demand.page.rows[4][i]=fault[i];
        demand_row(5,s->incomplete_ch2_active,s->incomplete_dmaor_bad,demand.terminal_guard,
            KUI_RETAIL_DEMAND_DIAGNOSTIC==3?UINT32_MAX:0u,KUI_RETAIL_DEMAND_DIAGNOSTIC==3?1u:0u);
        demand_row(6,demand.cycle_sum[0][0],demand.cycle_sum[0][1],
            demand.cycle_sum[1][0],demand.cycle_sum[1][1],demand.timed[0]+demand.timed[1]);
    }
    return &demand.page;
}
const struct kui_retail_demand_page *kui_armada_ce_demand_event(
    uint32_t event,uint32_t value,uintptr_t address) {
    /* The existing stage attaches only after its byte/zero-state relay
     * proof. No bootstrap call may sample clocks or mutate this controller
     * before that existing audited attachment succeeds. */
    if(!kui_armada_ce_stream_transport_attached()) return NULL;
    if(!demand_region_valid(address)) return NULL;
    const struct kui_retail_async_region *r=(const void *)address;
    unsigned kind=event&255u,function=(event>>8)&255u;
#if KUI_RETAIL_DEMAND_DIAGNOSTIC == 3
    if(kind==KUI_RETAIL_DEMAND_FREEZE) {
        if(!demand.window.frozen) {
            struct kui_retail_demand_clock now;demand_clock_sample(&now);
            demand.last_clock=now;kui_retail_demand_window_freeze(&demand.window,&now);
            demand.frozen_engine=r->engine;demand.frozen_stream=r->shared.stream.stats;
            if(demand.frame[0].active || demand.frame[1].active) ++demand.unpaired;
        }
        return NULL;
    }
    if(demand.window.frozen && kind!=KUI_RETAIL_DEMAND_REPORT) return NULL;
#endif
    if(kind==KUI_RETAIL_DEMAND_REPORT) {demand.terminal_guard=value;return demand_report_page(function,r);}
#if KUI_RETAIL_DEMAND_DIAGNOSTIC >= 2
    if(kind==KUI_RETAIL_DEMAND_GD_BEGIN) {
        demand_begin(0u,function,value,r);demand.frame[0].source=(event>>16)&255u;
    }
    else if(kind==KUI_RETAIL_DEMAND_GD_END) demand_finish(0u,function,value,event>>24,r);
    else if(kind==KUI_RETAIL_DEMAND_IRQ_BEGIN) demand_begin(1u,0u,value,r);
    else if(kind==KUI_RETAIL_DEMAND_IRQ_END) demand_finish(1u,0u,value,event>>24,r);
#else
    (void)value;
    /* Kept referenced for strict host/profile1 builds; no clock or observer
     * call is made by profile1's terminal-only path. */
    (void)demand_begin;(void)demand_finish;
#endif
    return NULL;
}
