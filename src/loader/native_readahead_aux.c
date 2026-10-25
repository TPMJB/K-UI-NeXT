/* SPDX-License-Identifier: GPL-3.0-only */
/* Optional physical-card look-ahead. The normal reader remains the sole
 * owner of demand completion, guest destinations, retries and card adoption.
 * This module runs only through its separately guarded private-stack entry. */
#include "native_readahead_aux_internal.h"
#include "kui/toy_pilot_clock.h"
#include <string.h>

#if !KUI_NATIVE_READAHEAD || defined(KUI_RETAIL_CE) || KUI_SCI_STREAM_TOKEN_SLICED
#error "Read-ahead auxiliary code requires its separate native unsliced SCI profile"
#endif

/* These are the existing unsliced sci_stream.c states. No new state or field
 * is inserted in the tightly bounded low reader region. */
enum { RA_CLOSED=0u,RA_PAUSED=1u,RA_DMA=2u,RA_LOST=3u,RA_RESUME=4u };
#define SPEC_TOKEN_LIMIT 256u
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
       (uintptr_t)c->region!=(p|UINT32_C(0x80000000))+UINT32_C(0x20400) ||
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
static void abandon(kui_retail_async_region_type *r,enum kui_sci_stream_result result) {
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
    enum kui_sci_stream_result result=o->busy()?o->poll(interrupt):KUI_SCI_STREAM_OK;
    if(result==KUI_SCI_STREAM_PENDING) {
        if(!o->busy() && r->shared.stream.state==RA_RESUME) {
            C.deferred=1u;o->unhook();
        }
        return;
    }
    if(result!=KUI_SCI_STREAM_OK) {abandon(r,result);return;}
    if(o->busy())return;
    const uint8_t *block=o->take(C.spec_lba,&result);
    if(!block || result!=KUI_SCI_STREAM_OK) {abandon(r,result);return;}
    if(!kui_native_ra_cache_append(&C.cache,C.spec_lba,block)) {
        abandon(r,KUI_SCI_STREAM_BUSY);return;
    }
    C.spec_owned=0u;++C.stats.producer_blocks;
}
static bool idle_request(const kui_retail_async_region_type *r) {
    return !r->engine.active && !r->shared.service.pending &&
        !r->shared.service.executing && !r->engine.failed;
}
static void producer(kui_retail_async_region_type *r,bool interrupt) {
    const struct kui_native_ra_low_ops *o=C.config.ops;
    if(C.quiesced || C.abandoned || !C.have_seed ||
       r->engine.native_loan_state!=KUI_VMU_NATIVE_ASYNC_LOAN_IDLE)return;
    ++C.stats.producer_visits;
    bool deferred_on_entry=C.deferred!=0u;
    retire_spec(r,interrupt);
    if(C.abandoned || !idle_request(r) || !r->engine.opened ||
       !r->shared.port.acquired || r->shared.port.fault || o->busy())return;
    if(C.deferred) {
        if(interrupt || !deferred_on_entry || !dma_idle() ||
           r->shared.stream.state!=RA_RESUME || r->shared.stream.position!=C.spec_lba)return;
        enum kui_sci_stream_result result=o->fetch(C.spec_lba,SPEC_TOKEN_LIMIT,false);
        if(result!=KUI_SCI_STREAM_OK) {abandon(r,result);return;}
        C.deferred=0u;
        if(o->busy())o->hook();
        return;
    }
    if(C.spec_owned)return;
    uint32_t next;
    if(!kui_native_ra_cache_next(&C.cache,&next)) {
        if(C.cache.count==KUI_NATIVE_RA_CACHE_BLOCKS)++C.stats.full;
        else ++C.stats.extent_ends;
        o->unhook();return;
    }
    /* Optional work never opens/restarts a card stream. In particular, a
     * busy guest DMA channel must not turn RESUME into CMD12/CMD18 or PIO. */
    if(r->shared.stream.state!=RA_PAUSED || r->shared.stream.position!=next || !dma_idle())return;
    enum kui_sci_stream_result result=o->fetch(next,SPEC_TOKEN_LIMIT,false);
    if(result!=KUI_SCI_STREAM_OK) {abandon(r,result);return;}
    C.spec_owned=1u;C.spec_lba=next;
    if(o->busy())o->hook();
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
        enum kui_sci_stream_result result=C.config.ops->stop();
        C.spec_owned=C.deferred=0u;C.config.ops->unhook();
        if(result!=KUI_SCI_STREAM_OK || C.config.ops->busy()) {
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
    if(r->engine.native_loan_state!=KUI_VMU_NATIVE_ASYNC_LOAN_IDLE)
        return event==KUI_NATIVE_RA_QUIESCE?0u:1u;
    if(event==KUI_NATIVE_RA_CAPTURE_START) {
        /* A gesture opens a statistics window, not a new cache lifetime. */
        memset(&C.stats,0,sizeof(C.stats));return 1u;
    }
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
