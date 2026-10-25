/* SPDX-License-Identifier: GPL-3.0-only */
/* Auxiliary policy integration with the real sector cursor and pure cache.
 * Low operations model one owned DMA/CRC completion at a time. Transport
 * register/byte behaviour is covered by the separate real-reader suites. */
#include "native_readahead_aux_internal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define kui_sci_stream_test_write model_stream_write
#include "sci_stream_model.h"
#undef kui_sci_stream_test_write
static unsigned dma_register_writes;
void kui_sci_stream_test_write(uint32_t address,uint32_t value,unsigned width) {
    if(address>=SAR && address<=CHCR)++dma_register_writes;
    model_stream_write(address,value,width);
}

#define C kui_native_readahead_aux_context
static kui_retail_async_region_type region,other;
kui_retail_async_region_type *kui_retail_async_region_storage=&region;
static _Alignas(32) uint8_t arena[KUI_NATIVE_RA_DATA_BYTES];
static uint8_t disk[640][512],output[128u*2048u];
static struct kui_retail_manifest manifest;
static unsigned checks,legacy_calls,fetches,stops,hooks,unhooks,feeds;
static uint32_t pending_lba,kept_lba,token,clock_counter;
static bool dma_available,receiving,kept_valid,raw,bad_clock;
static bool real_model,refuse_append;
static enum {POLL_GOOD,POLL_EARLY,POLL_REPAIR,POLL_CRC,POLL_MISSING} poll_case;
static enum kui_sci_stream_result fetch_result;
static enum kui_sci_stream_result stop_result;
#define CHECK(x) do {++checks;assert(x);}while(0)
enum {CLOSED,PAUSED,DMA,LOST,RESUME};
static const uint32_t CARD_BASE=1000u,DISC_BASE=45000u;

uint32_t kui_native_readahead_aux_test_read(uint32_t at,unsigned width) {
    CHECK(at!=RDR); /* The observer must not consume a transport byte. */
    if(real_model && (at==SAR || at==DAR || at==TCR || at==CHCR || at==DMAOR ||
       at==SMR || at==BRR || at==SCR || at==SSR || at==SPTR || at==STBCR || at==PDTR))
        return kui_sci_stream_test_read(at,width);
    (void)width;
    switch(at) {
    case UINT32_C(0xffc00000):return bad_clock?0u:0xe0au;
    case UINT32_C(0xffd80008):return UINT32_MAX;
    case UINT32_C(0xffd80010):return 2u;
    case UINT32_C(0xffd80004):return 1u;
    case UINT32_C(0xffd8000c):clock_counter-=19u;return clock_counter;
    case UINT32_C(0xffa0001c):return dma_available?0u:1u;
    case UINT32_C(0xffa00040):return 1u;
    case SAR:return UINT32_C(0x11111111);
    case DAR:return UINT32_C(0x22222222);
    case TCR:return UINT32_C(0x33);
    case SMR:return 0x80u;
    case BRR:case STBCR:return 0u;
    case SCR:return 0x30u;
    case SSR:return 0x84u;
    case SPTR:return 0x82u;
    case PDTR:return 0x80u;
    default:assert(false);return 0u;
    }
}
static uint8_t content(uint32_t file) {
    uint32_t stride=raw?2352u:2048u,at=file%stride;
    if(raw && at<16u) {
        if(at && at<11u)return 255u;
        return at==15u?1u:0u;
    }
    return (uint8_t)((file*13u)^(file>>8)^0x5au);
}
static void write_out(void *unused,uint32_t offset,const uint8_t *bytes,uint32_t count) {
    (void)unused;
    CHECK(offset<=sizeof(output) && count<=sizeof(output)-offset);
    memcpy(output+offset,bytes,count);
}
static enum kui_game_result feed(struct kui_retail_cursor *cursor,const uint8_t block[512]) {
    ++feeds;return kui_retail_cursor_feed(cursor,block);
}
static void hook(void) {++hooks;region.engine.hooked=1u;region.engine.release.armed=0x10u;}
static void unhook(void) {++unhooks;region.engine.hooked=0u;region.engine.release.armed=0u;}
static bool busy(void) {return receiving;}
static enum kui_sci_stream_result fetch(uint32_t lba,uint32_t limit,bool polled) {
    CHECK(!receiving && limit==256u && polled && dma_available);
    CHECK(lba>=CARD_BASE && lba-CARD_BASE<640u);
    CHECK(region.shared.stream.state==PAUSED && region.shared.stream.position==lba);
    ++fetches;
    if(fetch_result!=KUI_SCI_STREAM_OK) {
        region.shared.stream.state=LOST;return fetch_result;
    }
    pending_lba=lba;kept_lba=lba;kept_valid=poll_case!=POLL_MISSING;
    region.shared.stream.state=PAUSED;region.shared.stream.position=lba+1u;
    return KUI_SCI_STREAM_OK;
}
static enum kui_sci_stream_result poll(bool interrupt) {
    CHECK(receiving);
    if(poll_case==POLL_EARLY) {poll_case=POLL_GOOD;return KUI_SCI_STREAM_PENDING;}
    receiving=false;
    if(poll_case==POLL_REPAIR) {
        CHECK(interrupt);poll_case=POLL_GOOD;region.shared.stream.state=RESUME;
        region.shared.stream.position=pending_lba;return KUI_SCI_STREAM_PENDING;
    }
    kept_valid=poll_case!=POLL_MISSING;kept_lba=pending_lba;
    region.shared.stream.state=PAUSED;region.shared.stream.position=pending_lba+1u;
    return KUI_SCI_STREAM_OK;
}
static const uint8_t *take(uint32_t lba,enum kui_sci_stream_result *result) {
    if(!kept_valid || kept_lba!=lba) {*result=KUI_SCI_STREAM_PENDING;return NULL;}
    if(poll_case==POLL_CRC) {
        poll_case=POLL_GOOD;kept_valid=false;*result=KUI_SCI_STREAM_CRC;return NULL;
    }
    if(refuse_append)C.cache.count=KUI_NATIVE_RA_CACHE_BLOCKS;
    *result=KUI_SCI_STREAM_OK;return disk[lba-CARD_BASE];
}
static enum kui_sci_stream_result stop(void) {
    ++stops;receiving=false;kept_valid=false;region.shared.stream.state=CLOSED;
    return stop_result;
}
static void legacy(uint32_t wait) {
    (void)wait;++legacy_calls;
    struct kui_retail_async *e=&region.engine;
    unsigned maximum=e->in_irq?KUI_RETAIL_NATIVE_IRQ_BATCH_LIMIT:e->target;
    for(unsigned n=0u;n<maximum && e->active && !e->failed;n++) {
        uint32_t block=e->cursor.block;CHECK(block>=CARD_BASE && block-CARD_BASE<640u);
        kept_valid=true;kept_lba=block;receiving=false;
        region.shared.stream.state=PAUSED;region.shared.stream.position=block+1u;
        CHECK(feed(&e->cursor,disk[block-CARD_BASE])==KUI_GAME_OK);
        ++e->since;
        if(e->in_irq)++e->stats.irq_blocks;else ++e->stats.call_blocks;
        if(e->cursor.done==e->cursor.count)e->active=0u;
    }
}
static const struct kui_native_ra_low_ops ops={
    KUI_NATIVE_RA_OPS_MAGIC,KUI_NATIVE_RA_VERSION,sizeof(ops),
    &kui_retail_async_region_storage,legacy,hook,unhook,fetch,poll,take,stop,busy,feed
};
static struct kui_native_ra_config configuration(void) {
    return (struct kui_native_ra_config){KUI_NATIVE_RA_VERSION,sizeof(struct kui_native_ra_config),
        &region,&ops,arena,sizeof(arena),UINT32_C(0x0c400000),KUI_NATIVE_RA_LEASE_BYTES};
}
static void setup(bool raw_sectors) {
    memset(&C,0,sizeof(C));memset(&region,0,sizeof(region));memset(&other,0,sizeof(other));
    memset(&manifest,0,sizeof(manifest));memset(output,0xa5,sizeof(output));
    legacy_calls=fetches=stops=hooks=unhooks=feeds=0u;
    token=0u;receiving=kept_valid=false;dma_available=true;raw=raw_sectors;bad_clock=false;
    poll_case=POLL_GOOD;fetch_result=stop_result=KUI_SCI_STREAM_OK;
    clock_counter=UINT32_C(0x70000000);
    real_model=refuse_append=false;dma_register_writes=0u;
    kui_retail_async_region_storage=&region;
    manifest.card_sectors=10000u;manifest.partition_start=100u;manifest.partition_end=9000u;
    manifest.track_count=manifest.extent_count=1u;
    manifest.slots[0].track=(struct kui_retail_track){DISC_BASE,DISC_BASE+128u,1u,1u,
        (uint8_t)(4u|(raw?0u:KUI_RETAIL_TRACK_COOKED))};
    manifest.slots[1].extent=(struct kui_retail_extent){0u,CARD_BASE,
        (128u*(raw?2352u:2048u)+511u)/512u};
    for(unsigned block=0;block<640u;block++)for(unsigned byte=0;byte<512u;byte++)
        disk[block][byte]=content(block*512u+byte);
    region.engine.manifest=&manifest;region.engine.target=20u;
    region.engine.opened=1u;region.shared.port.acquired=true;
    region.shared.service.initialized=1u;
    struct kui_native_ra_config cfg=configuration();CHECK(kui_native_readahead_aux_init(&cfg)==0);
}
static void begin(uint32_t sector,uint32_t count) {
    memset(output,0xa5,sizeof(output));
    struct kui_retail_async *e=&region.engine;
    e->token=++token;e->active=1u;e->failed=e->retries=e->since=0u;e->in_irq=0u;
    e->opened=1u;region.shared.port.acquired=true;
    struct kui_retail_gd *g=&region.shared.service;
    g->token=token;g->pending=1u;g->status=KUI_GD_PROCESSING;g->completed_bytes=0u;
    g->lba=sector;g->count=count;g->sector_bytes=2048u;
    CHECK(kui_retail_cursor_begin(&e->cursor,&manifest,sector,count,KUI_GAME_SECTOR_MODE1,
        write_out,NULL)==KUI_GAME_OK);
}
static uint32_t visit(uint32_t event) {
    uint32_t pending=region.shared.service.pending;
    int32_t status=region.shared.service.status;
    uint32_t completed=region.shared.service.completed_bytes,old_fetch=fetches;
    unsigned old_feeds=feeds;
    uint32_t result=kui_native_readahead_aux_dispatch(event,20u,&region,0u);
    CHECK(region.shared.service.pending==pending && region.shared.service.status==status &&
        region.shared.service.completed_bytes==completed);
    CHECK(fetches-old_fetch<=1u);
    if(event==KUI_NATIVE_RA_DELIVER)CHECK(feeds-old_feeds<=
        (region.engine.in_irq?KUI_RETAIL_NATIVE_IRQ_BATCH_LIMIT:region.engine.target));
    return result;
}
static void report(void) {
    struct kui_retail_gd *g=&region.shared.service;
    g->completed_bytes=region.engine.cursor.done*g->sector_bytes;
    if(!region.engine.active && !region.engine.failed) {g->pending=0u;g->status=KUI_GD_COMPLETED;}
}
static void compare(uint32_t sector,uint32_t count) {
    uint32_t stride=raw?2352u:2048u;
    uint32_t bytes=region.shared.service.sector_bytes;
    for(unsigned n=0;n<count;n++)for(unsigned b=0;b<bytes;b++)
        CHECK(output[n*bytes+b]==content((sector-DISC_BASE+n)*stride+
            (raw && bytes==2048u?16u:0u)+b));
    CHECK(output[count*bytes]==0xa5u);
}
static void complete(uint32_t sector,uint32_t count) {
    unsigned turns=0u;
    while(region.engine.active && turns++<128u) {CHECK(visit(KUI_NATIVE_RA_DELIVER)==1u);report();}
    CHECK(!region.engine.active && !region.engine.failed && !region.shared.service.pending);
    compare(sector,count);
}
static void warm(unsigned blocks) {
    uint32_t initial=C.stats.producer_blocks;
    for(unsigned n=0u;n<blocks+2u && C.stats.producer_blocks-initial<blocks;n++)
        CHECK(visit(KUI_NATIVE_RA_TICK)==1u);
    CHECK(C.stats.producer_blocks-initial==blocks);
}
static void init_guards(void) {
    setup(false);struct kui_native_ra_config cfg=configuration();
    CHECK(kui_native_readahead_aux_init(&cfg)==-1);
    memset(&C,0,sizeof(C));cfg.data_bytes--;
    CHECK(kui_native_readahead_aux_init(&cfg)==-1 && !C.initialized);
    cfg=configuration();kui_retail_async_region_storage=&other;
    CHECK(kui_native_readahead_aux_init(&cfg)==-1 && !C.initialized);
    kui_retail_async_region_storage=&region;cfg.version++;
    CHECK(kui_native_readahead_aux_init(&cfg)==-1 && !C.initialized);
    cfg=configuration();CHECK(kui_native_readahead_aux_init(&cfg)==0);
    unsigned old_fetch=fetches,old_legacy=legacy_calls;
    CHECK(kui_native_readahead_aux_dispatch(KUI_NATIVE_RA_DELIVER,20u,&other,0u)==1u);
    CHECK(kui_native_readahead_aux_dispatch(KUI_NATIVE_RA_QUIESCE,0u,&region,1u)==0u);
    CHECK(C.stats.guard_faults==2u && fetches==old_fetch && legacy_calls==old_legacy);
}
static void sequential(void) {
    setup(false);begin(DISC_BASE,16u);complete(DISC_BASE,16u);
    CHECK(C.cache.count==1u && C.cache.base==CARD_BASE+63u && !fetches);
    warm(96u);unsigned old_legacy=legacy_calls;
    begin(DISC_BASE+16u,16u);complete(DISC_BASE+16u,16u);
    CHECK(C.stats.hits==64u && C.stats.hit_requests==1u && legacy_calls==old_legacy);
    CHECK(C.cache.base==CARD_BASE+127u && C.stats.max_visit_ticks && !C.stats.clock_bad);
    bad_clock=true;CHECK(visit(KUI_NATIVE_RA_TICK)==1u && C.stats.clock_bad==1u);
}
static void shared_raw_boundary(void) {
    setup(true);begin(DISC_BASE,1u);complete(DISC_BASE,1u);
    CHECK(C.cache.base==CARD_BASE+4u && C.cache.count==1u);
    warm(12u);unsigned old_legacy=legacy_calls;
    begin(DISC_BASE+1u,1u);CHECK(region.engine.cursor.block==CARD_BASE+4u);
    complete(DISC_BASE+1u,1u);CHECK(legacy_calls==old_legacy && C.stats.hits==5u);
}
static void partial_hit_cap(void) {
    setup(false);begin(DISC_BASE,1u);complete(DISC_BASE,1u);warm(2u);
    begin(DISC_BASE+1u,1u);unsigned old_legacy=legacy_calls,old_feeds=feeds;
    poll_case=POLL_EARLY;
    CHECK(visit(KUI_NATIVE_RA_DELIVER)==1u);
    CHECK(feeds-old_feeds==2u && legacy_calls==old_legacy && region.engine.active);
    report();complete(DISC_BASE+1u,1u);CHECK(legacy_calls==old_legacy+1u);
    setup(false);begin(DISC_BASE,1u);complete(DISC_BASE,1u);warm(64u);
    begin(DISC_BASE+1u,8u);region.engine.in_irq=1u;old_feeds=feeds;
    CHECK(visit(KUI_NATIVE_RA_DELIVER)==1u && feeds-old_feeds==8u);
    region.engine.in_irq=0u;complete(DISC_BASE+1u,8u);
    setup(false);region.engine.target=25u;
    begin(DISC_BASE,1u);complete(DISC_BASE,1u);warm(64u);
    begin(DISC_BASE+1u,8u);old_feeds=feeds;old_legacy=legacy_calls;
    CHECK(visit(KUI_NATIVE_RA_DELIVER)==1u && feeds-old_feeds==25u &&
        legacy_calls==old_legacy && region.engine.active);
    report();complete(DISC_BASE+1u,8u);
}
static void seek_and_errors(void) {
    setup(false);begin(DISC_BASE,1u);complete(DISC_BASE,1u);warm(1u);
    begin(DISC_BASE+90u,1u);complete(DISC_BASE+90u,1u);
    CHECK(!stops && !C.stats.seeks); /* No optional receiver owns this seek. */
    setup(false);begin(DISC_BASE,1u);complete(DISC_BASE,1u);
    poll_case=POLL_CRC;unsigned old_stops=stops;
    CHECK(visit(KUI_NATIVE_RA_TICK)==1u);
    CHECK(C.stats.crc_errors==1u && C.abandoned && !receiving && stops==old_stops &&
        region.shared.stream.state==LOST && !region.engine.hooked && C.cache.count==1u);
    unsigned old_fetch=fetches;CHECK(visit(KUI_NATIVE_RA_TICK)==1u && fetches==old_fetch);
    begin(DISC_BASE+8u,1u);complete(DISC_BASE+8u,1u);CHECK(!C.abandoned);
    setup(false);begin(DISC_BASE,1u);complete(DISC_BASE,1u);fetch_result=KUI_SCI_STREAM_TOKEN;
    CHECK(visit(KUI_NATIVE_RA_TICK)==1u && C.stats.token_errors==1u && C.abandoned && !stops);
}
static void repair_and_dma_owner(void) {
    setup(false);begin(DISC_BASE,1u);complete(DISC_BASE,1u);
    dma_available=false;CHECK(visit(KUI_NATIVE_RA_TICK)==1u && !fetches);
    dma_available=true;CHECK(visit(KUI_NATIVE_RA_TICK)==1u && C.stats.producer_blocks==1u &&
        !receiving && !C.spec_owned && !C.deferred);
    unsigned old_fetch=fetches;
    region.shared.stream.state=RESUME;
    CHECK(visit(KUI_NATIVE_RA_TICK)==1u && fetches==old_fetch);
    C.deferred=1u;
    CHECK(visit(KUI_NATIVE_RA_TICK)==1u && fetches==old_fetch && C.deferred);
}
static void loan_and_pending(void) {
    setup(false);begin(DISC_BASE,1u);complete(DISC_BASE,1u);warm(1u);
    uint32_t valid=C.cache.count;CHECK(visit(KUI_NATIVE_RA_QUIESCE)==1u && !receiving && !stops);
    CHECK(C.quiesced && C.cache.count==valid && !C.spec_owned);
    unsigned old_fetch=fetches,old_legacy=legacy_calls;
    region.engine.native_loan_state=KUI_VMU_NATIVE_ASYNC_LOAN_WRITER;
    CHECK(visit(KUI_NATIVE_RA_TICK)==1u && visit(KUI_NATIVE_RA_DELIVER)==1u);
    CHECK(fetches==old_fetch && legacy_calls==old_legacy);
    region.engine.native_loan_state=KUI_VMU_NATIVE_ASYNC_LOAN_IDLE;
    region.engine.opened=0u;region.shared.port.acquired=false;
    CHECK(visit(KUI_NATIVE_RA_TICK)==1u && fetches==old_fetch);
    begin(DISC_BASE+20u,1u);CHECK(visit(KUI_NATIVE_RA_QUIESCE)==0u && !stops);
    complete(DISC_BASE+20u,1u);CHECK(!C.quiesced);
    CHECK(visit(KUI_NATIVE_RA_RESET)==1u && !C.cache.count && !C.have_seed);
    CHECK(visit(KUI_NATIVE_RA_TICK)==1u && !C.cache.count && !C.have_seed);
}
static void bounded_window(void) {
    setup(false);begin(DISC_BASE,1u);complete(DISC_BASE,1u);
    warm(KUI_NATIVE_RA_BLOCKS-1u);CHECK(C.cache.count==KUI_NATIVE_RA_BLOCKS);
    /* A final in-flight block is never started when the ring is full. */
    CHECK(!receiving);unsigned old_fetch=fetches;
    CHECK(visit(KUI_NATIVE_RA_TICK)==1u && fetches==old_fetch && C.stats.full);
    setup(false);begin(DISC_BASE+127u,1u);complete(DISC_BASE+127u,1u);
    CHECK(visit(KUI_NATIVE_RA_TICK)==1u && !fetches && C.stats.extent_ends);
}
static void capture_preserves_cache(void) {
    setup(false);begin(DISC_BASE,1u);complete(DISC_BASE,1u);warm(8u);
    struct kui_native_readahead_aux_context expected=C;
    memset(&expected.stats,0,sizeof(expected.stats));
    memset(&expected.diagnostics,0,sizeof(expected.diagnostics));
    uint8_t before[512];memcpy(before,arena,512u);
    CHECK(kui_native_readahead_aux_dispatch(KUI_NATIVE_RA_CAPTURE_START,0u,&region,0u)==1u);
    CHECK(!memcmp(&C,&expected,sizeof(C)) && !memcmp(before,arena,sizeof(before)) &&
        !receiving && !C.spec_owned && C.cache.count==9u);
}
static uint8_t real_content(uint32_t lba,unsigned byte) {
    CHECK(lba>=CARD_BASE && lba-CARD_BASE<640u && byte<512u);
    return disk[lba-CARD_BASE][byte];
}
static enum kui_sci_stream_result real_fetch(uint32_t lba,uint32_t limit,bool polled) {
    CHECK(limit==256u && polled);++fetches;
    return kui_sci_stream_fetch(lba,limit,polled);
}
static enum kui_sci_stream_result real_stop(void) {++stops;return kui_sci_stream_stop();}
static void real_legacy(uint32_t wait) {
    (void)wait;++legacy_calls;
    struct kui_retail_async *e=&region.engine;
    unsigned delivered=0u,limit=e->in_irq?KUI_RETAIL_NATIVE_IRQ_BATCH_LIMIT:e->target;
    while(e->active && !e->failed && delivered<limit) {
        if(kui_sci_stream_busy()) {
            enum kui_sci_stream_result result=kui_sci_stream_poll(e->in_irq!=0u);
            if(result==KUI_SCI_STREAM_PENDING) {
                if(e->in_irq)return;
                result=kui_sci_stream_wait();
            }
            CHECK(result==KUI_SCI_STREAM_OK);
        }
        uint32_t lba=e->cursor.block;
        if(e->cursor.run>1u && kui_sci_stream_ready(lba))
            CHECK(kui_sci_stream_fetch(lba+1u,65536u,false)==KUI_SCI_STREAM_OK);
        enum kui_sci_stream_result result;
        const uint8_t *block=kui_sci_stream_take(lba,&result);
        if(!block) {
            CHECK(result==KUI_SCI_STREAM_PENDING);
            enum kui_sci_stream_result fetched=kui_sci_stream_fetch(lba,65536u,false);
            CHECK(fetched==KUI_SCI_STREAM_OK || fetched==KUI_SCI_STREAM_BUSY);
            continue;
        }
        CHECK(result==KUI_SCI_STREAM_OK && feed(&e->cursor,block)==KUI_GAME_OK);
        ++delivered;++e->since;
        if(e->in_irq)++e->stats.irq_blocks;else ++e->stats.call_blocks;
        if(e->cursor.done==e->cursor.count)e->active=0u;
    }
}
static const struct kui_native_ra_low_ops real_ops={
    KUI_NATIVE_RA_OPS_MAGIC,KUI_NATIVE_RA_VERSION,sizeof(real_ops),
    &kui_retail_async_region_storage,real_legacy,hook,unhook,
    real_fetch,kui_sci_stream_poll,kui_sci_stream_take,real_stop,kui_sci_stream_busy,feed
};
static void setup_real(void) {
    setup(true);memset(&C,0,sizeof(C));real_model=true;
    memset(&card,0,sizeof(card));memset(&m,0,sizeof(m));
    card.nac=1u;card.nac_first=30u;card.busy=4u;card.blocks=10000u;card.high_capacity=true;
    card_content=real_content;
    m.smr=0x80u;m.scr=0x30u;m.ssr=0x84u;m.cs_high=m.healthy=m.late_take=true;
    m.sar=0x11111111u;m.dar=0x22222222u;m.tcr=0x33u;m.dmaor=0x8201u;m.delay=1000u;
    m.areas[0]=region.area0;m.areas[1]=region.area1;
    struct kui_loader_sd *sd=&region.shared.card.device.sd;
    sd->bus.select=bus_select;sd->bus.transfer=bus_transfer;
    sd->high_capacity=sd->ready=true;sd->blocks=10000u;
    CHECK(kui_sci_stream_open(sd,region.area0,region.area1,false)==KUI_SCI_STREAM_OK);
    struct kui_native_ra_config cfg=configuration();cfg.ops=&real_ops;
    CHECK(kui_native_readahead_aux_init(&cfg)==0);
}
static void real_stream_integration(void) {
    setup_real();begin(DISC_BASE,16u);complete(DISC_BASE,16u);
    CHECK(card.cmd18==1u && !card.cmd12 && C.cache.count==1u);
    uint32_t initial=C.stats.producer_blocks;
    for(unsigned n=0u;n<100u && C.stats.producer_blocks-initial<96u;n++) {
        if(m.rx)m.rx_delay=0u;
        CHECK(visit(KUI_NATIVE_RA_TICK)==1u);
    }
    CHECK(C.stats.producer_blocks-initial==96u && card.cmd18==1u && !card.cmd12);
    unsigned old_legacy=legacy_calls;
    begin(DISC_BASE+16u,16u);complete(DISC_BASE+16u,16u);
    CHECK(legacy_calls==old_legacy && C.stats.hits && !C.stats.crc_errors);
    /* A real transport CRC failure abandons only optional work, without a
     * CMD12 stop from this IRQ/heartbeat path or publishing corrupt bytes. */
    CHECK(!kui_sci_stream_busy());
    uint32_t bad=region.shared.stream.position;
    card.corrupt_lba=bad;card.corrupt_count=1u;m.rx_delay=0u;
    uint32_t valid=C.cache.count,old_stops=stops;
    CHECK(visit(KUI_NATIVE_RA_TICK)==1u && C.stats.crc_errors==1u && C.abandoned &&
        C.cache.count==valid && stops==old_stops);

    /* Optional PIO never enters a partial DMA RESUME, even when its
     * caller supplies polled=true to fetch. Ordinary demand remains the
     * only path authorized to adopt/recover such a partial block. */
    setup_real();begin(DISC_BASE,1u);complete(DISC_BASE,1u);
    unsigned before_fetch=fetches,before_dma=m.dma_starts;
    region.shared.stream.state=RESUME;
    CHECK(visit(KUI_NATIVE_RA_TICK)==1u && fetches==before_fetch &&
        m.dma_starts==before_dma && !C.spec_owned);

    /* RAW2352 demand copies the complete sector, while cache keys and
     * speculative production remain the same physical512-byte blocks. */
    setup_real();begin(DISC_BASE,1u);region.shared.service.sector_bytes=2352u;
    CHECK(kui_retail_cursor_begin(&region.engine.cursor,&manifest,DISC_BASE,1u,
        KUI_GAME_SECTOR_RAW,write_out,NULL)==KUI_GAME_OK);
    complete(DISC_BASE,1u);
    for(unsigned n=0u;n<10u;n++) {if(m.rx)m.rx_delay=0u;CHECK(visit(KUI_NATIVE_RA_TICK)==1u);}
    old_legacy=legacy_calls;begin(DISC_BASE+1u,1u);region.shared.service.sector_bytes=2352u;
    CHECK(kui_retail_cursor_begin(&region.engine.cursor,&manifest,DISC_BASE+1u,1u,
        KUI_GAME_SECTOR_RAW,write_out,NULL)==KUI_GAME_OK);
    complete(DISC_BASE+1u,1u);CHECK(legacy_calls==old_legacy && C.stats.hits && !stops);
}
static void real_full_refill(void) {
    setup_real();
    /* Keep a valid raw track beyond the123 requested sectors so all384
     * cache slots can fill.139*2352 rounds to639 of the640 fixture blocks. */
    manifest.slots[0].track.end_lba=DISC_BASE+139u;
    manifest.slots[1].extent.blocks=(139u*2352u+511u)/512u;
    CHECK(manifest.slots[1].extent.blocks==639u);
    begin(DISC_BASE,50u);complete(DISC_BASE,50u);
    CHECK(C.cache.base==CARD_BASE+229u && C.cache.count==1u &&
        card.cmd18==1u && !card.cmd12);
    for(unsigned n=0u;n<KUI_NATIVE_RA_BLOCKS+1u && C.cache.count<KUI_NATIVE_RA_BLOCKS;n++) {
        if(m.rx)m.rx_delay=0u;
        CHECK(visit(KUI_NATIVE_RA_TICK)==1u);
    }
    CHECK(C.cache.count==KUI_NATIVE_RA_BLOCKS && !kui_sci_stream_busy() &&
        C.stats.producer_blocks==KUI_NATIVE_RA_BLOCKS-1u);
    unsigned old_legacy=legacy_calls,old_cmd18=card.cmd18,old_cmd12=card.cmd12;
    uint32_t old_hits=C.stats.hits;
    begin(DISC_BASE+50u,73u);
    CHECK(region.engine.cursor.block==C.cache.base && region.engine.target==20u);
    complete(DISC_BASE+50u,73u);
    CHECK(C.stats.hits-old_hits==336u && legacy_calls==old_legacy &&
        card.cmd18==old_cmd18 && card.cmd12==old_cmd12 && !stops &&
        !C.stats.crc_errors && !C.stats.token_errors);
}
static void diagnostic_failures(void) {
    setup(false);begin(DISC_BASE,1u);complete(DISC_BASE,1u);
    fetch_result=KUI_SCI_STREAM_TOKEN;CHECK(visit(KUI_NATIVE_RA_TICK)==1u);
    CHECK(C.diagnostics.failures==1u && C.diagnostics.first.stage==KUI_NATIVE_RA_STAGE_FETCH &&
        C.diagnostics.first.result==KUI_SCI_STREAM_TOKEN &&
        C.diagnostics.stage_result[KUI_NATIVE_RA_STAGE_FETCH][KUI_SCI_STREAM_TOKEN]==1u &&
        C.diagnostics.first.stream_state==PAUSED && C.diagnostics.first.post_stream_state==LOST &&
        C.diagnostics.first.token==token && C.diagnostics.first.event==KUI_NATIVE_RA_TICK &&
        C.diagnostics.first.operation_lba==C.cache.base+C.cache.count);
    struct kui_native_ra_failure_snapshot first=C.diagnostics.first;
    unsigned old_fetch=fetches;uint32_t skipped=C.diagnostics.skipped[KUI_NATIVE_RA_SKIP_ABANDONED];
    CHECK(visit(KUI_NATIVE_RA_TICK)==1u && fetches==old_fetch &&
        C.diagnostics.skipped[KUI_NATIVE_RA_SKIP_ABANDONED]==skipped+1u);
    fetch_result=KUI_SCI_STREAM_OK;begin(DISC_BASE+8u,1u);complete(DISC_BASE+8u,1u);
    fetch_result=KUI_SCI_STREAM_RESET;CHECK(visit(KUI_NATIVE_RA_TICK)==1u);
    CHECK(C.diagnostics.failures==2u && !memcmp(&first,&C.diagnostics.first,sizeof(first)) &&
        C.diagnostics.last.stage==KUI_NATIVE_RA_STAGE_FETCH &&
        C.diagnostics.last.result==KUI_SCI_STREAM_RESET && C.stats.abandoned==2u &&
        C.diagnostics.stage_result[KUI_NATIVE_RA_STAGE_FETCH][KUI_SCI_STREAM_RESET]==1u);

    /* A receiver which reports completion without a matching checked block
     * must identify TAKE/PENDING, rather than masquerading as token/CRC. */
    setup(false);begin(DISC_BASE,1u);complete(DISC_BASE,1u);
    poll_case=POLL_MISSING;
    uint32_t valid=C.cache.count;
    CHECK(visit(KUI_NATIVE_RA_TICK)==1u && C.abandoned && C.cache.count==valid &&
        C.diagnostics.first.stage==KUI_NATIVE_RA_STAGE_TAKE &&
        C.diagnostics.first.result==KUI_SCI_STREAM_PENDING &&
        C.diagnostics.first.stream_state==PAUSED && C.diagnostics.first.operation_lba==pending_lba &&
        !C.stats.crc_errors && !C.stats.token_errors && !stops);

    /* Inject a stale speculative frontier. APPEND refusal must preserve the
     * existing ring and record its own stage before LOST overwrites state. */
    setup(false);begin(DISC_BASE,1u);complete(DISC_BASE,1u);
    refuse_append=true;
    CHECK(visit(KUI_NATIVE_RA_TICK)==1u && C.abandoned &&
        C.cache.count==KUI_NATIVE_RA_CACHE_BLOCKS && !C.stats.producer_blocks &&
        C.diagnostics.first.stage==KUI_NATIVE_RA_STAGE_APPEND &&
        C.diagnostics.first.result==KUI_SCI_STREAM_BUSY &&
        C.diagnostics.first.cache_count==KUI_NATIVE_RA_CACHE_BLOCKS &&
        C.diagnostics.first.post_stream_state==PAUSED && region.shared.stream.state==LOST);

    /* Keep the old cancellation failure diagnostic covered through an
     * explicitly injected owned reception, not a producer-created DMA. */
    setup(false);begin(DISC_BASE,1u);complete(DISC_BASE,1u);
    C.spec_owned=1u;C.spec_lba=C.cache.base+C.cache.count;
    receiving=true;region.shared.stream.state=DMA;stop_result=KUI_SCI_STREAM_RESET;
    CHECK(visit(KUI_NATIVE_RA_QUIESCE)==0u && C.abandoned &&
        C.diagnostics.first.stage==KUI_NATIVE_RA_STAGE_QUIESCE &&
        C.diagnostics.first.result==KUI_SCI_STREAM_RESET &&
        (C.diagnostics.first.flags&KUI_NATIVE_RA_SNAP_SPEC_OWNED));
}
static bool inject_handoff_reset;
static void diagnostic_bus_select(void *ctx,bool selected) {
    bus_select(ctx,selected);
    if(!selected && inject_handoff_reset) {
        inject_handoff_reset=false;m.sptr=0u;
    }
}
static void diagnostic_real_handoff(void) {
    setup_real();begin(DISC_BASE,1u);complete(DISC_BASE,1u);
    /* Inject an old-style reception solely to keep PRE/POST handoff
     * diagnostics exercised. New optional production never creates it. */
    C.spec_lba=C.cache.base+C.cache.count;C.spec_owned=1u;
    CHECK(kui_sci_stream_fetch(C.spec_lba,256u,false)==KUI_SCI_STREAM_OK);
    uint32_t valid=C.cache.count,old_stops=stops;
    region.shared.card.device.sd.bus.select=diagnostic_bus_select;
    inject_handoff_reset=true;m.rx_delay=0u;region.engine.in_irq=1u;
    CHECK(visit(KUI_NATIVE_RA_DELIVER)==1u);
    const struct kui_native_ra_failure_snapshot *s=&C.diagnostics.first;
    CHECK(C.abandoned && C.diagnostics.failures==1u && s->stage==KUI_NATIVE_RA_STAGE_POLL &&
        s->result==KUI_SCI_STREAM_RESET && s->event==KUI_NATIVE_RA_DELIVER &&
        (s->flags&KUI_NATIVE_RA_SNAP_IRQ) && s->stream_state==DMA &&
        s->pre.sar==(RDR&UINT32_C(0x1fffffff)) && (s->pre.chcr&1u) &&
        s->post.sar==UINT32_C(0x11111111) && s->post.dar==UINT32_C(0x22222222) &&
        s->post.tcr==UINT32_C(0x33) && !(s->post.chcr&7u) &&
        (s->pre.pins&255u)==0x83u && !(s->post.pins&255u) &&
        s->post_stream_state==LOST && C.cache.count==valid && stops==old_stops &&
        !C.stats.producer_blocks && !C.stats.crc_errors && !C.stats.token_errors &&
        !region.shared.stream.stats.foreign && !region.shared.stream.stats.overruns &&
        !kui_sci_stream_busy() && !region.engine.hooked);
    uint32_t skipped=C.diagnostics.skipped[KUI_NATIVE_RA_SKIP_ABANDONED];
    unsigned old_fetch=fetches;
    region.engine.in_irq=0u;
    CHECK(visit(KUI_NATIVE_RA_TICK)==1u && fetches==old_fetch &&
        C.diagnostics.skipped[KUI_NATIVE_RA_SKIP_ABANDONED]==skipped+1u);
}
static void diagnostic_skip_reasons(void) {
    setup(false);
    CHECK(visit(KUI_NATIVE_RA_TICK)==1u && !fetches &&
        C.diagnostics.skipped[KUI_NATIVE_RA_SKIP_NO_SEED]==1u);
    /* Priority reasons identify the first admission gate, rather than
     * charging every simultaneously true condition for the same visit. */
    C.quiesced=C.abandoned=1u;region.engine.opened=0u;
    CHECK(visit(KUI_NATIVE_RA_TICK)==1u && !fetches &&
        C.diagnostics.skipped[KUI_NATIVE_RA_SKIP_QUIESCED]==1u &&
        !C.diagnostics.skipped[KUI_NATIVE_RA_SKIP_ABANDONED]);
    region.engine.native_loan_state=KUI_VMU_NATIVE_ASYNC_LOAN_WRITER;
    uint32_t heartbeats=C.stats.heartbeat_visits;
    CHECK(visit(KUI_NATIVE_RA_TICK)==1u && !fetches &&
        C.diagnostics.skipped[KUI_NATIVE_RA_SKIP_LOAN]==1u &&
        C.stats.heartbeat_visits==heartbeats);

    setup(false);begin(DISC_BASE,1u);complete(DISC_BASE,1u);
    CHECK(visit(KUI_NATIVE_RA_CAPTURE_START)==1u);
    region.engine.active=1u;region.shared.service.pending=1u;
    CHECK(visit(KUI_NATIVE_RA_TICK)==1u && !fetches &&
        C.diagnostics.skipped[KUI_NATIVE_RA_SKIP_ACTIVE]==1u &&
        !C.diagnostics.skipped[KUI_NATIVE_RA_SKIP_PENDING]);
    region.engine.active=0u;
    CHECK(visit(KUI_NATIVE_RA_TICK)==1u && !fetches &&
        C.diagnostics.skipped[KUI_NATIVE_RA_SKIP_PENDING]==1u);
    region.shared.service.pending=0u;region.shared.service.executing=1u;
    CHECK(visit(KUI_NATIVE_RA_TICK)==1u && !fetches &&
        C.diagnostics.skipped[KUI_NATIVE_RA_SKIP_PENDING]==2u);
    region.shared.service.executing=0u;region.engine.failed=1u;
    CHECK(visit(KUI_NATIVE_RA_TICK)==1u && !fetches &&
        C.diagnostics.skipped[KUI_NATIVE_RA_SKIP_FAILED]==1u);
    region.engine.failed=0u;region.engine.opened=0u;
    CHECK(visit(KUI_NATIVE_RA_TICK)==1u && !fetches &&
        C.diagnostics.skipped[KUI_NATIVE_RA_SKIP_NOT_OPENED]==1u);
    region.engine.opened=1u;region.shared.port.acquired=false;
    CHECK(visit(KUI_NATIVE_RA_TICK)==1u && !fetches &&
        C.diagnostics.skipped[KUI_NATIVE_RA_SKIP_NOT_OPENED]==2u);
    region.shared.port.acquired=true;region.shared.port.fault=true;
    CHECK(visit(KUI_NATIVE_RA_TICK)==1u && !fetches &&
        C.diagnostics.skipped[KUI_NATIVE_RA_SKIP_PORT_FAULT]==1u);
    region.shared.port.fault=false;receiving=true;
    CHECK(visit(KUI_NATIVE_RA_TICK)==1u && !fetches &&
        C.diagnostics.skipped[KUI_NATIVE_RA_SKIP_BUSY]==1u);
    receiving=false;dma_available=false;
    CHECK(visit(KUI_NATIVE_RA_TICK)==1u && !fetches &&
        C.diagnostics.skipped[KUI_NATIVE_RA_SKIP_GUEST_DMA]==1u);
    dma_available=true;region.shared.stream.position++;
    CHECK(visit(KUI_NATIVE_RA_TICK)==1u && !fetches &&
        C.diagnostics.skipped[KUI_NATIVE_RA_SKIP_STREAM_POSITION]==1u);
    CHECK(!C.diagnostics.failures && !C.stats.abandoned && !stops);
}
static void diagnostic_unrepaired_snapshots(void) {
    static const uint8_t counts[]={0u,1u,2u,255u};
    setup(false);begin(DISC_BASE,1u);complete(DISC_BASE,1u);
    for(unsigned i=0u;i<sizeof(counts)/sizeof(counts[0]);++i) {
        if(i) {
            fetch_result=KUI_SCI_STREAM_OK;
            begin(DISC_BASE+8u*i,1u);complete(DISC_BASE+8u*i,1u);
        }
        region.shared.stream.unrepaired=counts[i];
        struct kui_native_ra_cache cache=C.cache;
        uint8_t cached[512];
        memcpy(cached,arena+C.cache.head*512u,sizeof(cached));
        unsigned before_fetch=fetches,before_stops=stops;
        fetch_result=KUI_SCI_STREAM_RESET;
        CHECK(visit(KUI_NATIVE_RA_TICK)==1u && C.abandoned && !C.spec_owned &&
            fetches==before_fetch+1u && stops==before_stops && !receiving &&
            !memcmp(&cache,&C.cache,sizeof(cache)) &&
            !memcmp(cached,arena+C.cache.head*512u,sizeof(cached)) &&
            region.shared.stream.unrepaired==counts[i] && !region.engine.failed);
        CHECK(C.diagnostics.failures==i+1u && C.stats.abandoned==i+1u &&
            C.diagnostics.stage_result[KUI_NATIVE_RA_STAGE_FETCH][KUI_SCI_STREAM_RESET]==i+1u &&
            C.diagnostics.first.loan_state==(uint32_t)KUI_VMU_NATIVE_ASYNC_LOAN_IDLE &&
            (C.diagnostics.last.loan_state&UINT32_C(0xffff))==KUI_VMU_NATIVE_ASYNC_LOAN_IDLE &&
            C.diagnostics.last.loan_state>>16==counts[i] &&
            C.diagnostics.last.stage==KUI_NATIVE_RA_STAGE_FETCH &&
            C.diagnostics.last.result==KUI_SCI_STREAM_RESET);
    }
    /* Starting another capture clears observations, not the existing
     * transport failure count, cached bytes, or abandoned producer latch. */
    struct kui_native_readahead_aux_context expected=C;
    memset(&expected.stats,0,sizeof(expected.stats));
    memset(&expected.diagnostics,0,sizeof(expected.diagnostics));
    struct kui_sci_stream_state stream=region.shared.stream;
    uint8_t cached[512];memcpy(cached,arena+C.cache.head*512u,sizeof(cached));
    unsigned before_fetch=fetches,before_stops=stops;
    CHECK(visit(KUI_NATIVE_RA_CAPTURE_START)==1u &&
        !memcmp(&C,&expected,sizeof(C)) &&
        !memcmp(&stream,&region.shared.stream,sizeof(stream)) &&
        !memcmp(cached,arena+C.cache.head*512u,sizeof(cached)) &&
        region.shared.stream.unrepaired==255u);
    CHECK(visit(KUI_NATIVE_RA_TICK)==1u && fetches==before_fetch && stops==before_stops &&
        C.abandoned && !C.diagnostics.failures &&
        C.diagnostics.skipped[KUI_NATIVE_RA_SKIP_ABANDONED]==1u &&
        region.shared.stream.unrepaired==255u);
}
static void real_no_optional_work(uint32_t event) {
    unsigned clocks=card.clocks,old_fetch=fetches,dma_writes=dma_register_writes;
    uint32_t attempts=C.diagnostics.stage_result[KUI_NATIVE_RA_STAGE_NONE][KUI_NATIVE_RA_PIO_ATTEMPTS];
    struct kui_native_ra_cache cache=C.cache;
    CHECK(visit(event)==1u && card.clocks==clocks && fetches==old_fetch &&
        dma_register_writes==dma_writes && !memcmp(&cache,&C.cache,sizeof(cache)) &&
        C.diagnostics.stage_result[KUI_NATIVE_RA_STAGE_NONE][KUI_NATIVE_RA_PIO_ATTEMPTS]==attempts);
}
static void real_pio_heartbeats(void) {
    setup_real();begin(DISC_BASE,4u);complete(DISC_BASE,4u);
    CHECK(!C.spec_owned && !C.deferred && !kui_sci_stream_busy());
    /* Neither repeated GD entry nor an IRQ-context TICK may initiate PIO. */
    for(unsigned n=0;n<4u;++n)real_no_optional_work(KUI_NATIVE_RA_DELIVER);
    region.engine.in_irq=1u;
    real_no_optional_work(KUI_NATIVE_RA_DELIVER);real_no_optional_work(KUI_NATIVE_RA_TICK);
    region.engine.in_irq=0u;
    region.shared.service.pending=1u;real_no_optional_work(KUI_NATIVE_RA_TICK);
    region.shared.service.pending=0u;region.shared.service.executing=1u;
    real_no_optional_work(KUI_NATIVE_RA_TICK);region.shared.service.executing=0u;
    region.engine.active=1u;real_no_optional_work(KUI_NATIVE_RA_TICK);region.engine.active=0u;
    region.engine.native_loan_state=KUI_VMU_NATIVE_ASYNC_LOAN_WRITER;
    real_no_optional_work(KUI_NATIVE_RA_TICK);
    region.engine.native_loan_state=KUI_VMU_NATIVE_ASYNC_LOAN_IDLE;
    uint32_t position=region.shared.stream.position;
    for(unsigned state=0u;state<=RESUME;++state)if(state!=PAUSED && state!=DMA) {
        region.shared.stream.state=state;real_no_optional_work(KUI_NATIVE_RA_TICK);
    }
    region.shared.stream.state=PAUSED;region.shared.stream.position++;
    real_no_optional_work(KUI_NATIVE_RA_TICK);region.shared.stream.position=position;
    m.chcr=1u;real_no_optional_work(KUI_NATIVE_RA_TICK);m.chcr=0u;

    /* Repeated DMA contention which defeated the old optional receiver is
     * left armed. CPU-clocked PIO never starts that receive-only DMA. */
    m.overrun_every=388u;m.overrun_again=458u;m.chcr2=1u;
    region.shared.stream.unrepaired=1u;
    unsigned writes=dma_register_writes,dma_starts=m.dma_starts,cmd18=card.cmd18,cmd12=card.cmd12;
    uint32_t sar=m.sar,dar=m.dar,tcr=m.tcr,chcr=m.chcr,dmaor=m.dmaor;
    uint32_t initial=C.stats.producer_blocks,polled=region.shared.stream.stats.polled;
    for(unsigned n=0;n<32u;++n) {
        uint32_t next;CHECK(kui_native_ra_cache_next(&C.cache,&next));
        uint32_t count=C.cache.count;unsigned old_fetch=fetches;
        CHECK(visit(KUI_NATIVE_RA_TICK)==1u && fetches==old_fetch+1u &&
            C.cache.count==count+1u && !C.spec_owned && !C.deferred && !kui_sci_stream_busy() &&
            dma_register_writes==writes && m.dma_starts==dma_starts &&
            m.sar==sar && m.dar==dar && m.tcr==tcr && m.chcr==chcr && m.dmaor==dmaor &&
            card.cmd18==cmd18 && card.cmd12==cmd12);
        /* Inspect without advancing the cache's consumer frontier. */
        uint32_t slot=C.cache.head+(next-C.cache.base);
        if(slot>=KUI_NATIVE_RA_BLOCKS)slot-=KUI_NATIVE_RA_BLOCKS;
        CHECK(!memcmp(C.cache.data+slot*512u,disk[next-CARD_BASE],512u));
    }
    uint32_t *metrics=C.diagnostics.stage_result[KUI_NATIVE_RA_STAGE_NONE];
    CHECK(C.stats.producer_blocks-initial==32u && region.shared.stream.stats.polled-polled==32u &&
        metrics[KUI_NATIVE_RA_PIO_ATTEMPTS]==32u && metrics[KUI_NATIVE_RA_PIO_MAX_TICKS] &&
        !metrics[KUI_NATIVE_RA_PIO_CLOCK_BAD] && !C.diagnostics.failures && !C.abandoned &&
        !region.shared.stream.stats.overruns && !region.shared.stream.stats.repaired &&
        !region.shared.stream.stats.crc_errors && !region.shared.stream.stats.token_errors &&
        region.shared.stream.unrepaired==1u && m.overrun_every==388u);
    uint32_t maximum=metrics[KUI_NATIVE_RA_PIO_MAX_TICKS];bad_clock=true;
    CHECK(visit(KUI_NATIVE_RA_TICK)==1u && metrics[KUI_NATIVE_RA_PIO_ATTEMPTS]==33u &&
        metrics[KUI_NATIVE_RA_PIO_CLOCK_BAD]==1u && metrics[KUI_NATIVE_RA_PIO_MAX_TICKS]==maximum &&
        C.stats.producer_blocks-initial==33u);
    bad_clock=false;
    CHECK(visit(KUI_NATIVE_RA_CAPTURE_START)==1u && !metrics[KUI_NATIVE_RA_PIO_ATTEMPTS] &&
        !metrics[KUI_NATIVE_RA_PIO_MAX_TICKS] && !metrics[KUI_NATIVE_RA_PIO_CLOCK_BAD] &&
        C.cache.count==34u && region.shared.stream.unrepaired==1u);
}
static void real_pio_byte_budget(void) {
    setup_real();begin(DISC_BASE,4u);complete(DISC_BASE,4u);
    card.frame=0u;card.gap=255u;
    unsigned before=bus_bytes,writes=dma_register_writes,cmd18=card.cmd18,cmd12=card.cmd12;
    CHECK(visit(KUI_NATIVE_RA_TICK)==1u && bus_bytes-before==256u+514u &&
        C.stats.producer_blocks==1u && dma_register_writes==writes &&
        card.cmd18==cmd18 && card.cmd12==cmd12 && !C.abandoned);
    /* A missing token consumes exactly the admitted256 search bytes,
     * publishes nothing, and leaves recovery to the ordinary demand path. */
    card.frame=0u;card.gap=256u;before=bus_bytes;
    uint32_t count=C.cache.count;
    CHECK(visit(KUI_NATIVE_RA_TICK)==1u && bus_bytes-before==256u &&
        C.cache.count==count && C.abandoned && C.stats.token_errors==1u &&
        dma_register_writes==writes && card.cmd18==cmd18 && card.cmd12==cmd12 &&
        !region.engine.failed && !C.spec_owned);
}
static unsigned pio_fault_at,pio_transfer_calls;
static uint8_t pio_fault_transfer(void *ctx,uint8_t value,bool slow) {
    if(++pio_transfer_calls==pio_fault_at)m.healthy=false;
    return bus_transfer(ctx,value,slow);
}
static void real_pio_bus_faults(void) {
    static const unsigned fault_at[]={1u,260u,514u};
    for(unsigned n=0;n<sizeof(fault_at)/sizeof(fault_at[0]);++n) {
        setup_real();begin(DISC_BASE,4u);complete(DISC_BASE,4u);
        pio_fault_at=fault_at[n];pio_transfer_calls=0u;
        region.shared.card.device.sd.bus.transfer=pio_fault_transfer;
        struct kui_native_ra_cache cache=C.cache;
        unsigned writes=dma_register_writes,cmd12=card.cmd12;
        CHECK(visit(KUI_NATIVE_RA_TICK)==1u && !m.healthy && C.abandoned &&
            !C.spec_owned && !C.stats.producer_blocks && !region.engine.failed &&
            !memcmp(&cache,&C.cache,sizeof(cache)) && dma_register_writes==writes &&
            card.cmd12==cmd12 && pio_transfer_calls<=770u &&
            C.diagnostics.first.stage==KUI_NATIVE_RA_STAGE_FETCH &&
            C.diagnostics.first.result==KUI_SCI_STREAM_RESET);
        unsigned calls=pio_transfer_calls;
        CHECK(visit(KUI_NATIVE_RA_TICK)==1u && pio_transfer_calls==calls);
    }
    /* This transport model proves bounded byte-call flow after a latched
     * fault; real SCI flag-wait timeout bounds belong to sci_sd_bus tests. */
}
int main(void) {
    init_guards();sequential();shared_raw_boundary();partial_hit_cap();seek_and_errors();
    repair_and_dma_owner();loan_and_pending();bounded_window();capture_preserves_cache();
    real_stream_integration();real_full_refill();diagnostic_failures();diagnostic_real_handoff();
    diagnostic_skip_reasons();diagnostic_unrepaired_snapshots();
    real_pio_heartbeats();real_pio_byte_budget();real_pio_bus_faults();
    printf("native read-ahead auxiliary: %u checks; real cursor bytes, strict visit caps, "
        "fresh-PAUSED heartbeat-only PIO, one checked block per visit, exact token/byte budgets, "
        "no optional DMA writes, full raw warm cache, demand caps, VMU loan and failure snapshots passed\n",checks);
    return 0;
}
