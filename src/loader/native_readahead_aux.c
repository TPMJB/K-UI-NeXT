/* SPDX-License-Identifier: GPL-3.0-only */
/* Optional physical-card look-ahead. The normal reader remains the sole
 * owner of demand completion, guest destinations, retries and card adoption.
 * This module runs only through its separately guarded private-stack entry. */
#include "native_readahead_aux_internal.h"
#include "kui/vmu_native_background.h"
#include "kui/toy_pilot_clock.h"
#include <string.h>

#if !KUI_NATIVE_READAHEAD || defined(KUI_RETAIL_CE) || KUI_SCI_STREAM_TOKEN_SLICED
#error "Read-ahead auxiliary code requires its separate native unsliced SCI profile"
#endif

/* These are the existing unsliced sci_stream.c states. No new state or field
 * is inserted in the tightly bounded low reader region. */
enum { RA_CLOSED=0u,RA_PAUSED=1u,RA_DMA=2u,RA_LOST=3u,RA_RESUME=4u };
#define SPEC_TOKEN_LIMIT 256u
__asm__(".global __kui_native_readahead_diagnostic_auxiliary\n"
        ".set __kui_native_readahead_diagnostic_auxiliary,1\n"
        ".global __kui_native_readahead_repair_diagnostic_auxiliary\n"
        ".set __kui_native_readahead_repair_diagnostic_auxiliary,1\n"
        ".global __kui_native_readahead_pio_auxiliary\n"
        ".set __kui_native_readahead_pio_auxiliary,1\n"
        ".global __kui_native_readahead_pio_pair_auxiliary\n"
        ".set __kui_native_readahead_pio_pair_auxiliary,1\n");
#if KUI_NATIVE_GENERAL_LEAN_LAYOUT
__asm__(".global __kui_native_general_lean_auxiliary\n"
        ".set __kui_native_general_lean_auxiliary,1\n");
#endif
#define C kui_native_readahead_aux_context
struct kui_native_readahead_aux_context kui_native_readahead_aux_context
    __attribute__((section(".data.native_ra_context"),aligned(32)))={0};

#ifdef KUI_NATIVE_READAHEAD_AUX_HOST_TEST
extern uint32_t kui_native_readahead_aux_test_read(uint32_t,unsigned);
#define read_hw kui_native_readahead_aux_test_read
#else
static uint32_t read_hw(uint32_t address,unsigned width) {
    if(width==1u)return *(volatile const uint8_t *)(uintptr_t)address;
    if(width==2u)return *(volatile const uint16_t *)(uintptr_t)address;
    return *(volatile const uint32_t *)(uintptr_t)address;
}
#endif
struct ra_clock {uint32_t counter,valid;};
static struct ra_clock clock_read(void) {
    uint32_t frqcr=read_hw(UINT32_C(0xffc00000),2u);
    uint32_t tcor=read_hw(UINT32_C(0xffd80008),4u);
    uint32_t tcr=read_hw(UINT32_C(0xffd80010),2u);
    uint32_t tstr=read_hw(UINT32_C(0xffd80004),1u);
    struct ra_clock result={read_hw(UINT32_C(0xffd8000c),4u),
        kui_toy_pilot_clock_profile(frqcr,tcor,tcr,tstr)?1u:0u};
    return result;
}
static bool dma_idle(void) {
    return !(read_hw(UINT32_C(0xffa0001c),4u)&7u) &&
        (read_hw(UINT32_C(0xffa00040),4u)&7u)==1u;
}
static bool no_overlap(const void *a,size_t an,const void *b,size_t bn) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x<=UINTPTR_MAX-an && y<=UINTPTR_MAX-bn && (x+an<=y || y+bn<=x);
}
#ifndef KUI_NATIVE_READAHEAD_AUX_HOST_TEST
static bool low_code(uintptr_t address) {
    return !(address&1u) && address>=UINT32_C(0x8c004028) &&
        address<UINT32_C(0x8c007ba0);
}
#endif
static bool config_valid(const struct kui_native_ra_config *c) {
    if(!c || c->version!=KUI_NATIVE_RA_VERSION || c->bytes!=sizeof(*c) ||
       !c->region || !c->ops || !c->data || c->data_bytes!=KUI_NATIVE_RA_DATA_BYTES ||
       c->lease_bytes!=KUI_NATIVE_RA_LEASE_BYTES || ((uintptr_t)c->data&31u) ||
       !no_overlap(c,sizeof(*c),&C,sizeof(C)) ||
       !no_overlap(c->data,c->data_bytes,&C,sizeof(C)) ||
       !no_overlap(c->data,c->data_bytes,c->region,sizeof(*c->region)) ||
       !no_overlap(c->data,c->data_bytes,c->ops,sizeof(*c->ops)))return false;
#ifndef KUI_NATIVE_READAHEAD_AUX_HOST_TEST
    uint32_t area=c->lease&UINT32_C(0xe0000000),p=c->lease&UINT32_C(0x1fffffff);
    uintptr_t ops=(uintptr_t)c->ops;
    if((area && area!=UINT32_C(0x80000000) && area!=UINT32_C(0xa0000000)) ||
       (p&31u) || p<UINT32_C(0x0c010020) ||
       p>UINT32_C(0x0d000000)-KUI_NATIVE_RA_LEASE_BYTES ||
       (uintptr_t)&C<(p|UINT32_C(0xa0000000))+KUI_NATIVE_RA_AUX_OFFSET+64u ||
       (uintptr_t)&C>(p|UINT32_C(0xa0000000))+KUI_NATIVE_RA_AUX_OFFSET+4096u-sizeof(C) ||
       (uintptr_t)c->region!=(p|UINT32_C(0x80000000))+KUI_VMU_NATIVE_BACKGROUND_REGION_OFFSET ||
       (uintptr_t)c->data!=(p|UINT32_C(0xa0000000))+KUI_NATIVE_RA_DATA_OFFSET ||
       (ops&3u) || ops<UINT32_C(0x8c004028) ||
       ops>UINT32_C(0x8c007ba0)-sizeof(*c->ops))return false;
    uintptr_t cell=(uintptr_t)c->ops->region_storage;
    if(
       (cell&3u) || cell<UINT32_C(0x8c004000) || cell>UINT32_C(0x8c007b9c) ||
       !low_code((uintptr_t)c->ops->deliver) || !low_code((uintptr_t)c->ops->hook) ||
       !low_code((uintptr_t)c->ops->unhook) || !low_code((uintptr_t)c->ops->fetch) ||
       !low_code((uintptr_t)c->ops->poll) || !low_code((uintptr_t)c->ops->take) ||
       !low_code((uintptr_t)c->ops->stop) || !low_code((uintptr_t)c->ops->busy) ||
       !low_code((uintptr_t)c->ops->feed))return false;
#endif
    const struct kui_native_ra_low_ops *o=c->ops;
    return o->magic==KUI_NATIVE_RA_OPS_MAGIC && o->version==KUI_NATIVE_RA_VERSION &&
        o->bytes==sizeof(*o) && o->region_storage && *o->region_storage==c->region &&
        o->deliver && o->hook && o->unhook && o->fetch && o->poll && o->take &&
        o->stop && o->busy && o->feed;
}
int kui_native_readahead_aux_init(const struct kui_native_ra_config *c) {
    if(C.busy || C.initialized || !config_valid(c))return -1;
    struct kui_native_ra_cache cache;
    if(!kui_native_ra_cache_init(&cache,c->data,c->data_bytes))return -1;
    memset(&C,0,sizeof(C));
    C.config=*c;C.cache=cache;C.initialized=1u;
    return 0;
}
static void snapshot_hw(struct kui_native_ra_hw_snapshot *s) {
    /* Sequential, read-only observations: DMA can advance between reads.
     * Never read RDR, fence/purge memory, or change transport registers. */
    s->sar=read_hw(UINT32_C(0xffa00010),4u);
    s->dar=read_hw(UINT32_C(0xffa00014),4u);
    s->tcr=read_hw(UINT32_C(0xffa00018),4u);
    s->chcr=read_hw(UINT32_C(0xffa0001c),4u);
    s->dmaor=read_hw(UINT32_C(0xffa00040),4u);
    s->sci=read_hw(UINT32_C(0xffe00000),1u) |
        read_hw(UINT32_C(0xffe00004),1u)<<8 |
        read_hw(UINT32_C(0xffe00008),1u)<<16 |
        read_hw(UINT32_C(0xffe00010),1u)<<24;
    s->pins=read_hw(UINT32_C(0xffe0001c),1u) |
        read_hw(UINT32_C(0xffc00004),1u)<<8 |
        read_hw(UINT32_C(0xff800030),2u)<<16;
    s->tmu=read_hw(UINT32_C(0xffd8000c),4u);
}
static void snapshot_pre(enum kui_native_ra_failure_stage stage,
    const kui_retail_async_region_type *r,uint32_t operation_lba) {
    struct kui_native_ra_failure_snapshot *s=&C.observation;
    const struct kui_retail_async *e=&r->engine;
    const struct kui_sci_stream_state *stream=&r->shared.stream;
    s->stage=stage;s->event=C.current_event;
    s->flags=(e->active?KUI_NATIVE_RA_SNAP_ACTIVE:0u) |
        (r->shared.service.pending?KUI_NATIVE_RA_SNAP_PENDING:0u) |
        (r->shared.service.executing?KUI_NATIVE_RA_SNAP_EXECUTING:0u) |
        (e->failed?KUI_NATIVE_RA_SNAP_FAILED:0u) |
        (C.spec_owned?KUI_NATIVE_RA_SNAP_SPEC_OWNED:0u) |
        (C.deferred?KUI_NATIVE_RA_SNAP_DEFERRED:0u) |
        (C.quiesced?KUI_NATIVE_RA_SNAP_QUIESCED:0u) |
        (C.abandoned?KUI_NATIVE_RA_SNAP_ABANDONED:0u) |
        (C.have_seed?KUI_NATIVE_RA_SNAP_HAVE_SEED:0u) |
        (r->shared.port.acquired?KUI_NATIVE_RA_SNAP_ACQUIRED:0u) |
        (r->shared.port.fault?KUI_NATIVE_RA_SNAP_PORT_FAULT:0u) |
        (e->opened?KUI_NATIVE_RA_SNAP_OPENED:0u) |
        (e->in_irq?KUI_NATIVE_RA_SNAP_IRQ:0u);
    s->token=e->token;s->cursor_block=e->cursor.block;
    s->stream_state=stream->state;s->stream_position=stream->position;
    s->cache_base=C.cache.base;s->cache_count=C.cache.count;s->cache_end=C.cache.end;
    s->operation_lba=operation_lba;
    s->loan_state=(e->native_loan_state&UINT32_C(0xffff)) |
        (uint32_t)stream->unrepaired<<16;
    s->rx_flags=(uint32_t)stream->ready[0] | (uint32_t)stream->ready[1]<<8 |
        (uint32_t)stream->kept<<16 | stream->fill<<24;
    s->ready_lba[0]=stream->ready_lba[0];s->ready_lba[1]=stream->ready_lba[1];
    s->kept_lba=stream->kept_lba;s->arrived=stream->arrived;
    s->lost=(uint32_t)stream->lost[0] | (uint32_t)stream->lost[1]<<16;
    snapshot_hw(&s->pre);
}
static void snapshot_failure(const kui_retail_async_region_type *r,
    enum kui_sci_stream_result result) {
    struct kui_native_ra_failure_snapshot *s=&C.observation;
    s->result=result;
    s->post_stream_state=r->shared.stream.state;
    s->post_stream_position=r->shared.stream.position;
    snapshot_hw(&s->post);
    if(s->stage<KUI_NATIVE_RA_STAGE_COUNT && (uint32_t)result<KUI_NATIVE_RA_RESULT_COUNT)
        ++C.diagnostics.stage_result[s->stage][result];
    /* Word copies avoid an implicit large structure-copy runtime import. */
    const uint32_t *from=(const uint32_t *)(const void *)s;
    uint32_t *last=(uint32_t *)(void *)&C.diagnostics.last;
    uint32_t *first=(uint32_t *)(void *)&C.diagnostics.first;
    for(unsigned i=0u;i<sizeof(*s)/sizeof(uint32_t);++i) {
        last[i]=from[i];
        if(!C.diagnostics.failures)first[i]=from[i];
    }
    ++C.diagnostics.failures;
}
static bool skipped(enum kui_native_ra_skip reason) {
    ++C.diagnostics.skipped[reason];return true;
}
static void abandon(kui_retail_async_region_type *r,enum kui_sci_stream_result result) {
    snapshot_failure(r,result);
    if(result==KUI_SCI_STREAM_CRC)++C.stats.crc_errors;
    if(result==KUI_SCI_STREAM_TOKEN)++C.stats.token_errors;
    ++C.stats.abandoned;C.abandoned=1u;C.spec_owned=C.deferred=0u;
    /* poll/take/fetch have already stopped an owned reception. No optional
     * error may invoke CMD12's long busy wait from inside an interrupt. */
    if(!C.config.ops->busy())r->shared.stream.state=RA_LOST;
    C.config.ops->unhook();
}
/* Retire at most one speculative block. A mid-block repair remains stopped
 * in RESUME until a later foreground/heartbeat visit, never the same IRQ. */
static void retire_spec(kui_retail_async_region_type *r,bool interrupt) {
    const struct kui_native_ra_low_ops *o=C.config.ops;
    if(!C.spec_owned || C.deferred)return;
    enum kui_sci_stream_result result=KUI_SCI_STREAM_OK;
    if(o->busy()) {snapshot_pre(KUI_NATIVE_RA_STAGE_POLL,r,C.spec_lba);result=o->poll(interrupt);}
    if(result==KUI_SCI_STREAM_PENDING) {
        if(!o->busy() && r->shared.stream.state==RA_RESUME) {
            C.deferred=1u;o->unhook();
        }
        return;
    }
    if(result!=KUI_SCI_STREAM_OK) {abandon(r,result);return;}
    if(o->busy())return;
    snapshot_pre(KUI_NATIVE_RA_STAGE_TAKE,r,C.spec_lba);
    const uint8_t *block=o->take(C.spec_lba,&result);
    if(!block || result!=KUI_SCI_STREAM_OK) {abandon(r,result);return;}
    snapshot_pre(KUI_NATIVE_RA_STAGE_APPEND,r,C.spec_lba);
    if(!kui_native_ra_cache_append(&C.cache,C.spec_lba,block)) {
        abandon(r,KUI_SCI_STREAM_BUSY);return;
    }
    C.spec_owned=0u;++C.stats.producer_blocks;
}
static bool producer_ready(kui_retail_async_region_type *r,bool interrupt,
    uint32_t *next,bool first) {
    const struct kui_native_ra_low_ops *o=C.config.ops;
    /* Optional CPU-clocked work belongs only to a later controller
     * heartbeat, never to GD entry or the SCI handler. */
    if(C.current_event!=KUI_NATIVE_RA_TICK || interrupt || r->engine.in_irq)return false;
    if(C.quiesced) {skipped(KUI_NATIVE_RA_SKIP_QUIESCED);return false;}
    if(C.abandoned) {skipped(KUI_NATIVE_RA_SKIP_ABANDONED);return false;}
    if(!C.have_seed) {skipped(KUI_NATIVE_RA_SKIP_NO_SEED);return false;}
    if(r->engine.native_loan_state!=KUI_VMU_NATIVE_ASYNC_LOAN_IDLE) {
        skipped(KUI_NATIVE_RA_SKIP_LOAN);return false;
    }
    if(first)++C.stats.producer_visits;
    if(r->engine.active) {skipped(KUI_NATIVE_RA_SKIP_ACTIVE);return false;}
    if(r->shared.service.pending || r->shared.service.executing) {
        skipped(KUI_NATIVE_RA_SKIP_PENDING);return false;
    }
    if(r->engine.failed) {skipped(KUI_NATIVE_RA_SKIP_FAILED);return false;}
    if(!r->engine.opened || !r->shared.port.acquired) {
        skipped(KUI_NATIVE_RA_SKIP_NOT_OPENED);return false;
    }
    if(r->shared.port.fault) {skipped(KUI_NATIVE_RA_SKIP_PORT_FAULT);return false;}
    if(o->busy()) {skipped(KUI_NATIVE_RA_SKIP_BUSY);return false;}
    if(C.deferred) {
        /* fetch(polled=true) still uses DMA in RESUME. Only the ordinary
         * demand path may adopt a stopped partial reception. */
        skipped(KUI_NATIVE_RA_SKIP_DEFERRED);return false;
    }
    if(C.spec_owned) {skipped(KUI_NATIVE_RA_SKIP_SPEC_OWNED);return false;}
    if(!kui_native_ra_cache_next(&C.cache,next)) {
        if(C.cache.count==KUI_NATIVE_RA_CACHE_BLOCKS)++C.stats.full;
        else ++C.stats.extent_ends;
        o->unhook();return false;
    }
    /* Continue a fresh block in the already open stream. Do not enter the
     * RESUME branch, seek, issue a command, or borrow a guest DMA channel. */
    if(r->shared.stream.state!=RA_PAUSED || r->shared.stream.position!=*next) {
        skipped(KUI_NATIVE_RA_SKIP_STREAM_POSITION);return false;
    }
    if(!dma_idle()) {skipped(KUI_NATIVE_RA_SKIP_GUEST_DMA);return false;}
    return true;
}
static void producer(kui_retail_async_region_type *r,bool interrupt) {
    uint32_t next;
    if(!producer_ready(r,interrupt,&next,true))return;
    const struct kui_native_ra_low_ops *o=C.config.ops;
    uint32_t *metrics=C.diagnostics.stage_result[KUI_NATIVE_RA_STAGE_NONE];
    struct ra_clock before=clock_read(),block_before=before;
    bool valid=true;
    for(unsigned block=0u;block<KUI_NATIVE_RA_PIO_BLOCK_LIMIT;++block) {
        if(block) {
            /* Recheck every ordinary admission guard before deciding
             * whether another complete block fits the soft window. */
            if(!producer_ready(r,interrupt,&next,false))break;
            struct ra_clock admission=clock_read();
            uint32_t elapsed=before.counter-admission.counter;
            if(!valid || !before.valid || !admission.valid ||
               !elapsed || elapsed>=UINT32_C(0x80000000) || elapsed>KUI_NATIVE_RA_PIO_SECOND_LIMIT_TICKS) {
                ++metrics[KUI_NATIVE_RA_PIO_SECOND_TIME_SKIPS];
                valid=valid && admission.valid && elapsed && elapsed<UINT32_C(0x80000000);
                break;
            }
            block_before=admission;
            ++metrics[KUI_NATIVE_RA_PIO_SECOND_ATTEMPTS];
        }
        ++metrics[KUI_NATIVE_RA_PIO_ATTEMPTS];
        snapshot_pre(KUI_NATIVE_RA_STAGE_FETCH,r,next);
        enum kui_sci_stream_result result=o->fetch(next,SPEC_TOKEN_LIMIT,true);
        if(result!=KUI_SCI_STREAM_OK)abandon(r,result);
        else {
            C.spec_owned=1u;C.spec_lba=next;
            /* Finish this physical block, including CRC and publication,
             * before considering another; never stop a partial block. */
            retire_spec(r,false);
        }
        struct ra_clock after=clock_read();
        uint32_t elapsed=block_before.counter-after.counter;
        if(!block_before.valid || !after.valid || !elapsed || elapsed>=UINT32_C(0x80000000)) {
            ++metrics[KUI_NATIVE_RA_PIO_CLOCK_BAD];valid=false;
        } else if(elapsed>metrics[KUI_NATIVE_RA_PIO_MAX_SINGLE_TICKS])
            metrics[KUI_NATIVE_RA_PIO_MAX_SINGLE_TICKS]=elapsed;
        if(C.abandoned)break;
    }
    /* This is an observed complete-work duration, not a hard deadline:
     * an admitted block may take longer than its reserved allowance. */
    struct ra_clock after=clock_read();uint32_t elapsed=before.counter-after.counter;
    if(valid && before.valid && after.valid && elapsed && elapsed<UINT32_C(0x80000000)) {
        if(elapsed>metrics[KUI_NATIVE_RA_PIO_MAX_TICKS])metrics[KUI_NATIVE_RA_PIO_MAX_TICKS]=elapsed;
        if(elapsed>KUI_NATIVE_RA_PIO_WINDOW_TICKS)++metrics[KUI_NATIVE_RA_PIO_WINDOW_OVERRUNS];
    }
}
static void seed_completed(kui_retail_async_region_type *r) {
    struct kui_retail_async *e=&r->engine;
    if(e->active || e->failed || !e->cursor.count || e->cursor.done!=e->cursor.count ||
       (C.have_completed && C.completed_token==e->token))return;
    C.have_completed=1u;C.completed_token=e->token;
    uint32_t block=e->cursor.block,end=kui_native_ra_extent_end(e->manifest,block);
    if(!end) {C.have_seed=0u;return;}
    if(!kui_native_ra_cache_lookup(&C.cache,block)) {
        enum kui_sci_stream_result result=KUI_SCI_STREAM_PENDING;
        const uint8_t *checked=C.config.ops->take(block,&result);
        if(!checked || result!=KUI_SCI_STREAM_OK ||
           !kui_native_ra_cache_rebase(&C.cache,block,end) ||
           !kui_native_ra_cache_append(&C.cache,block,checked)) {
            C.have_seed=0u;return;
        }
    }
    C.have_seed=1u;C.abandoned=0u;
}
static void demand_complete(kui_retail_async_region_type *r) {
    struct kui_retail_async *e=&r->engine;
    if(e->cursor.done!=e->cursor.count)return;
    e->active=0u;
#if KUI_NATIVE_REQUEST_TIMING
    uint32_t elapsed=e->stats.hooks-read_hw(UINT32_C(0xffd8000c),4u);
    if(elapsed>e->stats.releases) {
        e->stats.releases=elapsed;e->native_max_lba=r->shared.service.lba;
        e->native_max_count=r->shared.service.count;
    }
#endif
}
static void deliver(uint32_t wait,kui_retail_async_region_type *r) {
    const struct kui_native_ra_low_ops *o=C.config.ops;
    struct kui_retail_async *e=&r->engine;
    if(e->in_irq)++C.stats.irq_visits;
    retire_spec(r,e->in_irq!=0u);
    if(!e->active) {
        if(!C.spec_owned)o->deliver(wait);
        seed_completed(r);producer(r,e->in_irq!=0u);return;
    }
    if(C.quiesced) {C.quiesced=0u;++C.stats.resumes;}
    uint32_t delivered=0u,limit=e->in_irq?KUI_RETAIL_NATIVE_IRQ_BATCH_LIMIT:e->target;
    if(limit>25u)limit=25u;
    while(delivered<limit && e->active && !e->failed) {
        const uint8_t *block=kui_native_ra_cache_lookup(&C.cache,e->cursor.block);
        if(!block)break;
        e->retries=0u;
        if(o->feed(&e->cursor,block)!=KUI_GAME_OK)e->failed=KUI_GD_ERROR_IO;
        ++delivered;++e->since;++C.stats.hits;
        if(e->in_irq)++e->stats.irq_blocks;else ++e->stats.call_blocks;
        if(!C.have_hit || C.hit_token!=e->token) {
            C.have_hit=1u;C.hit_token=e->token;++C.stats.hit_requests;
        }
        demand_complete(r);
    }
    /* Preserve the complete per-visit cap. After partial hits, a miss is
     * supplied by the next ordinary call rather than adding a fresh20/25. */
    if(!delivered && e->active && !e->failed) {
        ++C.stats.misses;
        if(C.spec_owned) {
            if(e->cursor.block==C.spec_lba) {
                /* Its received block is exactly the caller's next demand. */
                C.spec_owned=C.deferred=0u;
            } else {
                if(e->in_irq)return;
                ++C.stats.seeks;
                (void)o->stop();C.spec_owned=C.deferred=0u;o->unhook();
            }
        }
        o->deliver(wait);
    }
    seed_completed(r);producer(r,e->in_irq!=0u);
}
static uint32_t quiesce(kui_retail_async_region_type *r) {
    if(r->engine.active || r->engine.in_irq || r->shared.service.pending ||
       r->shared.service.executing)return 0u;
    C.quiesced=1u;++C.stats.quiesces;
    if(C.spec_owned) {
        snapshot_pre(KUI_NATIVE_RA_STAGE_QUIESCE,r,C.spec_lba);
        enum kui_sci_stream_result result=C.config.ops->stop();
        bool failed=result!=KUI_SCI_STREAM_OK || C.config.ops->busy();
        if(failed)snapshot_failure(r,result);
        C.spec_owned=C.deferred=0u;C.config.ops->unhook();
        if(failed) {
            C.abandoned=1u;++C.stats.abandoned;return 0u;
        }
    }
    return C.config.ops->busy()?0u:1u;
}
uint32_t kui_native_readahead_aux_dispatch(uint32_t event,uint32_t arg,
    kui_retail_async_region_type *r,uint32_t reserved) {
    if(!C.initialized || reserved || r!=C.config.region ||
       *C.config.ops->region_storage!=r) {
        ++C.stats.guard_faults;return event==KUI_NATIVE_RA_QUIESCE?0u:1u;
    }
    if(r->engine.native_loan_state!=KUI_VMU_NATIVE_ASYNC_LOAN_IDLE) {
        skipped(KUI_NATIVE_RA_SKIP_LOAN);
        return event==KUI_NATIVE_RA_QUIESCE?0u:1u;
    }
    if(event==KUI_NATIVE_RA_CAPTURE_START) {
        /* A gesture opens a statistics window, not a new cache lifetime. */
        memset(&C.stats,0,sizeof(C.stats));
        memset(&C.diagnostics,0,sizeof(C.diagnostics));return 1u;
    }
    C.current_event=event;
    struct ra_clock before=clock_read();uint32_t result=1u;
    switch(event) {
    case KUI_NATIVE_RA_DELIVER:deliver(arg,r);break;
    case KUI_NATIVE_RA_TICK:
        ++C.stats.heartbeat_visits;
        if(C.quiesced && r->engine.opened && r->shared.port.acquired) {
            C.quiesced=0u;++C.stats.resumes;
        }
        seed_completed(r);producer(r,false);break;
    case KUI_NATIVE_RA_QUIESCE:result=quiesce(r);break;
    case KUI_NATIVE_RA_RESUME:
        C.quiesced=0u;++C.stats.resumes;break;
    case KUI_NATIVE_RA_RESET:
        result=quiesce(r);
        if(result) {
            (void)kui_native_ra_cache_init(&C.cache,C.config.data,C.config.data_bytes);
            C.have_seed=C.have_hit=C.abandoned=0u;
            /* A later heartbeat must not republish the old completed
             * cursor after explicit validity reset. A new token may seed. */
            C.have_completed=1u;C.completed_token=r->engine.token;
        }
        break;
    default:++C.stats.guard_faults;result=0u;break;
    }
    struct ra_clock after=clock_read();uint32_t elapsed=before.counter-after.counter;
    if(!before.valid || !after.valid || elapsed>=UINT32_C(0x80000000))++C.stats.clock_bad;
    else if(elapsed>C.stats.max_visit_ticks)C.stats.max_visit_ticks=elapsed;
    return result;
}
