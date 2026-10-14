/* SPDX-License-Identifier: GPL-3.0-only */
/* Adversarial SCI ownership model around the actual Armada audio transport.
 * The reference reader independently checks fragmented RAW audio bytes,
 * packed file offsets and subchannel removal. No console executable bytes,
 * DirectSound calls, or physical register access are used here. */
#include "kui/armada_ce_stream_transport.h"
#include "kui/retail_image.h"
#include "sci_stream.h"
#include "retail_async.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CARD_BLOCKS 2048u
#define AUDIO_BYTES (8u * KUI_GAME_RAW_BYTES)
static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #x); abort(); \
} } while(0)
static uint8_t card_bytes[CARD_BLOCKS * 512u];
static struct kui_retail_manifest manifest;
static struct kui_retail_image reference;
static struct {
    uint8_t before[32], bytes[AUDIO_BYTES], after[32];
} output;
static uint8_t expected[AUDIO_BYTES];

static uint8_t source(unsigned track, uint32_t file_byte) {
    uint32_t stride = track == 3u ? 2448u : 2352u;
    uint32_t sector = file_byte / stride, inside = file_byte % stride;
    /* Audio deliberately contains no valid Mode1 sync/header, so a mistaken
     * cooked-data path cannot accidentally pass this fixture. */
    return (uint8_t)(track * 91u + sector * 53u + inside * 11u + (inside >> 8));
}
static int reference_block(void *context, uint32_t lba, uint8_t out[512]) {
    CHECK(context == card_bytes && lba < CARD_BLOCKS);
    memcpy(out, card_bytes + lba * 512u, 512u);
    return 0;
}
static void fixture(void) {
    memset(&manifest, 0, sizeof(manifest));
    memset(card_bytes, 0xf3, sizeof(card_bytes));
    memset(&output, 0xa5, sizeof(output));
    manifest.card_sectors = CARD_BLOCKS;
    manifest.partition_start = 50u;
    manifest.partition_end = 2000u;
    manifest.storage_transport = KUI_STORAGE_SCI;
    manifest.reader = KUI_RETAIL_READER_ASYNC;
    manifest.track_count = 4u;
    manifest.session_lba = 45000u;
    manifest.boot_lba = 45000u;
    manifest.boot_bytes = 2048u;
    strcpy(manifest.title, "Armada SCI ownership test");
    strcpy(manifest.bootfile, "0WINCEOS.BIN");
    static const uint32_t start[4] = {0u, 3u, 45000u, 45002u};
    static const uint32_t end[4] = {3u, 7u, 45002u, 45010u};
    static const uint32_t offset[4] = {0u, 509u, 0u, 257u};
    for(unsigned i = 0; i < 4u; ++i) {
        struct kui_retail_track *t = &manifest.slots[i].track;
        uint32_t stride = i == 3u ? 2448u : 2352u;
        uint32_t bytes = (end[i] - start[i]) * stride;
        uint32_t blocks = (offset[i] + bytes + 511u) / 512u;
        *t = (struct kui_retail_track){
            .start_lba = start[i], .end_lba = end[i],
            .first_extent = (uint16_t)((4u + manifest.extent_count) |
                                       ((offset[i] & 255u) << 8)),
            .control = (uint8_t)((i == 0u || i == 2u ? 4u : 0u) |
                                (offset[i] > 255u ? KUI_RETAIL_TRACK_OFFSET_HIGH : 0u) |
                                (i == 3u ? KUI_RETAIL_TRACK_2448 : 0u))
        };
        for(uint32_t n = 0; n < blocks;) {
            uint32_t take = blocks - n > 3u ? 3u : blocks - n;
            uint32_t index = manifest.extent_count;
            uint32_t physical = 100u + (index & 1u ? 900u : 0u) + (index >> 1) * 4u;
            CHECK(4u + index < KUI_RETAIL_MANIFEST_SLOTS && physical + take < 1990u);
            manifest.slots[4u + manifest.extent_count++].extent =
                (struct kui_retail_extent){n, physical, take};
            ++t->extent_count;
            for(uint32_t p = 0; p < take * 512u; ++p) {
                uint32_t at = n * 512u + p;
                if(at >= offset[i] && at - offset[i] < bytes)
                    card_bytes[physical * 512u + p] = source(i, at - offset[i]);
            }
            n += take;
        }
    }
    CHECK(kui_retail_manifest_validate(&manifest) == KUI_GAME_OK);
    CHECK(kui_retail_image_init(&reference, &manifest, reference_block,
                                card_bytes) == KUI_GAME_OK);
}
static void guard_bytes(void) {
    for(unsigned i = 0; i < sizeof(output.before); ++i) {
        CHECK(output.before[i] == 0xa5u);
        CHECK(output.after[i] == 0xa5u);
    }
}
static void reference_read(uint32_t lba, uint32_t count) {
    memset(expected, 0xa5, sizeof(expected));
    CHECK(kui_retail_image_read(&reference, lba, count,
        KUI_GAME_SECTOR_RAW, expected, sizeof(expected)) == KUI_GAME_OK);
}


/* The singleton has two checked receive areas and one current reception.
 * CRC failures are reported at take(), as in sci_stream.c. Poll faults never
 * expose a pointer. Kept areas remain readable until discard/reuse. */
struct checked_bank {
    uint32_t used,lba,kept;
    enum kui_sci_stream_result result;
    uint8_t bytes[512];
};
static struct kui_retail_async_region region;
static struct {
    struct checked_bank bank[2];
    uint32_t busy,lba,delay,latency,fetches,polls,waits,takes,discards;
    uint32_t hooks,unhooks,wakes,budgets,opens,polled,completions;
    uint32_t fetch_faults,crc_faults,poll_faults,open_fails;
    uint32_t inflight_dma,guest_dma,guest_tuple,guest_irq;
    enum kui_sci_stream_result fetch_error,poll_error;
} wire;
static struct kui_armada_ce_stream_bus bus;
static struct kui_armada_ce_stream_transport_snapshot snap(uint32_t epoch) {
    struct kui_armada_ce_stream_transport_snapshot s;
    CHECK(kui_armada_ce_stream_transport_snapshot(epoch,&s)!=KUI_ARMADA_CE_STREAM_TRANSPORT_STALE);
    return s;
}
static void checked(uint32_t lba,enum kui_sci_stream_result result) {
    CHECK(lba<CARD_BLOCKS);
    unsigned i=wire.bank[0].used && !wire.bank[1].used ? 1u : 0u;
    CHECK(!wire.bank[i].used || wire.bank[i].kept);
    wire.bank[i].used=1u;wire.bank[i].kept=0u;
    wire.bank[i].lba=lba;wire.bank[i].result=result;
    memcpy(wire.bank[i].bytes,card_bytes+lba*512u,512u);
    if(result!=KUI_SCI_STREAM_OK) memset(wire.bank[i].bytes,0x5a,512u);
}
static enum kui_sci_stream_result complete(void) {
    CHECK(wire.busy && wire.inflight_dma==1u);
    wire.busy=wire.inflight_dma=0u;++wire.completions;
    if(wire.poll_faults) {--wire.poll_faults;return wire.poll_error;}
    enum kui_sci_stream_result result=KUI_SCI_STREAM_OK;
    if(wire.crc_faults) {--wire.crc_faults;result=KUI_SCI_STREAM_CRC;}
    checked(wire.lba,result);return KUI_SCI_STREAM_OK;
}
static bool mock_open(void) {
    ++wire.opens;if(wire.open_fails) return false;
    region.engine.opened=1u;return true;
}
static void mock_hook(void) {++wire.hooks;}
static void mock_unhook(void) {++wire.unhooks;}
static void mock_wake(void) {++wire.wakes;}
static bool mock_busy(void) {return wire.busy!=0u;}
static bool mock_ready(uint32_t lba) {
    for(unsigned i=0;i<2u;i++) if(wire.bank[i].used && wire.bank[i].lba==lba) return true;
    return false;
}
static enum kui_sci_stream_result mock_poll(bool checked_result) {
    CHECK(checked_result);++wire.polls;
    if(!wire.busy) return KUI_SCI_STREAM_OK;
    if(wire.delay) {--wire.delay;return KUI_SCI_STREAM_PENDING;}
    return complete();
}
static enum kui_sci_stream_result mock_wait(void) {
    CHECK(wire.busy);++wire.waits;return complete();
}
static enum kui_sci_stream_result mock_fetch(uint32_t lba,uint32_t limit,bool polled) {
    CHECK(!wire.busy && !wire.inflight_dma && lba<CARD_BLOCKS && limit==65536u);
    ++wire.fetches;wire.polled+=polled;
    if(wire.fetch_faults) {--wire.fetch_faults;return wire.fetch_error;}
    wire.busy=wire.inflight_dma=1u;wire.lba=lba;wire.delay=wire.latency;
    return KUI_SCI_STREAM_OK;
}
static const uint8_t *mock_take(uint32_t lba,enum kui_sci_stream_result *result) {
    CHECK(result);++wire.takes;
    for(unsigned i=0;i<2u;i++) if(wire.bank[i].used && wire.bank[i].lba==lba) {
        *result=wire.bank[i].result;
        if(*result!=KUI_SCI_STREAM_OK) {wire.bank[i].used=0u;return NULL;}
        wire.bank[i].kept=1u;return wire.bank[i].bytes;
    }
    *result=KUI_SCI_STREAM_PENDING;return NULL;
}
static void mock_discard(void) {
    CHECK(!wire.busy && !wire.inflight_dma);++wire.discards;
    memset(wire.bank,0,sizeof(wire.bank));
}
static void mock_budget(bool bounded) {CHECK(bounded);++wire.budgets;}
static void attach(void) {
    memset(&region,0,sizeof(region));memset(&wire,0,sizeof(wire));
    /* A guest channel/interrupt tuple is deliberately live. The transport
     * descriptor cannot access or overwrite it; only the mock SCI owns DMA. */
    wire.guest_dma=1u;wire.guest_tuple=0x3456789au;wire.guest_irq=0x7au;
    bus=(struct kui_armada_ce_stream_bus){
        .version=KUI_ARMADA_CE_STREAM_BUS_VERSION,.bytes=sizeof(bus),.region=&region,
        .open=mock_open,.hook=mock_hook,.unhook=mock_unhook,.wake=mock_wake,
        .busy=mock_busy,.ready=mock_ready,.poll=mock_poll,.wait=mock_wait,
        .fetch=mock_fetch,.take=mock_take,.discard=mock_discard,.token_budget=mock_budget
    };
    kui_armada_ce_stream_transport_test_reset();
    CHECK(kui_armada_ce_stream_transport_init(&manifest,(uintptr_t)output.bytes,
                                            (uintptr_t)output.bytes+sizeof(output.bytes)));
    CHECK(kui_armada_ce_stream_transport_event(KUI_ARMADA_CE_STREAM_BUS_ATTACH,(uintptr_t)&bus)==1u);
}
static void setup(void) {fixture();attach();}
static void intact(void) {
    guard_bytes();CHECK(wire.inflight_dma<=1u);
    CHECK(wire.guest_dma==1u && wire.guest_tuple==0x3456789au && wire.guest_irq==0x7au);
}
static void submit(uint32_t epoch,uint32_t lba,uint32_t count) {
    CHECK(kui_armada_ce_stream_transport_submit(epoch,lba,count,(uintptr_t)output.bytes,
          sizeof(output.bytes))==KUI_ARMADA_CE_STREAM_TRANSPORT_PENDING);
}
static void pump_one(uint32_t epoch,bool irq) {
    struct kui_armada_ce_stream_transport_snapshot before=snap(epoch);
    uint32_t fetches=wire.fetches,waits=wire.waits;
    if(irq) (void)kui_armada_ce_stream_transport_event(KUI_ARMADA_CE_STREAM_BUS_IRQ,0u);
    else (void)kui_armada_ce_stream_transport_pump();
    struct kui_armada_ce_stream_transport_snapshot after=snap(epoch);
    CHECK(after.audio_blocks-before.audio_blocks<=1u);
    CHECK(wire.fetches-fetches<=1u && wire.waits==waits);intact();
}
static void finish_audio(uint32_t epoch) {
    for(unsigned n=0;n<2000u && snap(epoch).status==KUI_ARMADA_CE_STREAM_TRANSPORT_PENDING;n++)
        pump_one(epoch,n%3u==1u && snap(epoch).owner==2u);
    CHECK(snap(epoch).status==KUI_ARMADA_CE_STREAM_TRANSPORT_DONE);
    CHECK(!wire.busy && !wire.waits);
}
static void exact_audio(uint32_t lba,uint32_t count) {
    reference_read(lba,count);CHECK(!memcmp(output.bytes,expected,sizeof(expected)));
    unsigned tr=lba<45000u ? 1u : 3u;
    uint32_t first=manifest.slots[tr].track.start_lba,stride=tr==3u ? 2448u : 2352u;
    /* Independent per-sample oracle also establishes that 2448 subchannel
     * tails and packed offset padding are not delivered as CDDA samples. */
    for(uint32_t n=0;n<count*2352u;n++)
        CHECK(output.bytes[n]==source(tr,(lba-first+n/2352u)*stride+n%2352u));
    intact();
}
static void test_raw_mapping(void) {
    for(unsigned latency=0;latency<4u;latency++) {
        setup();wire.latency=latency;submit(1u,3u,4u);finish_audio(1u);exact_audio(3u,4u);
        CHECK(snap(1u).bytes==4u*2352u && snap(1u).audio_blocks>=19u);
        memset(&output,0xa5,sizeof(output));submit(2u,45002u,8u);finish_audio(2u);exact_audio(45002u,8u);
        CHECK(snap(2u).bytes==8u*2352u && wire.hooks && wire.unhooks);
    }
}
static void test_admission(void) {
    setup();CHECK(!kui_armada_ce_stream_transport_init(&manifest,(uintptr_t)output.bytes,
                                                        (uintptr_t)output.bytes+sizeof(output.bytes)));
    struct kui_armada_ce_stream_bus bad=bus;
    bad.version=2u;CHECK(!kui_armada_ce_stream_transport_event(KUI_ARMADA_CE_STREAM_BUS_ATTACH,(uintptr_t)&bad));
    bad=bus;bad.wait=NULL;CHECK(!kui_armada_ce_stream_transport_event(KUI_ARMADA_CE_STREAM_BUS_ATTACH,(uintptr_t)&bad));
    bad=bus;bad.bytes--;CHECK(!kui_armada_ce_stream_transport_event(KUI_ARMADA_CE_STREAM_BUS_ATTACH,(uintptr_t)&bad));
    bad=bus;bad.region=NULL;CHECK(!kui_armada_ce_stream_transport_event(KUI_ARMADA_CE_STREAM_BUS_ATTACH,(uintptr_t)&bad));
    CHECK(kui_armada_ce_stream_transport_event(KUI_ARMADA_CE_STREAM_BUS_ATTACH,(uintptr_t)&bus)==1u);
    static const uint32_t args[][3]={{0u,3u,1u},{1u,3u,0u},{1u,3u,65u},{1u,0u,1u},
        {1u,45000u,1u},{1u,7u,1u},{1u,6u,2u},{1u,45009u,2u},{1u,UINT32_MAX,2u}};
    for(unsigned i=0;i<sizeof(args)/sizeof(args[0]);i++)
        CHECK(kui_armada_ce_stream_transport_submit(args[i][0],args[i][1],args[i][2],
            (uintptr_t)output.bytes,sizeof(output.bytes))==KUI_ARMADA_CE_STREAM_TRANSPORT_ARGUMENT);
    CHECK(kui_armada_ce_stream_transport_submit(1u,3u,1u,(uintptr_t)output.bytes,2351u)==KUI_ARMADA_CE_STREAM_TRANSPORT_ARGUMENT);
    CHECK(kui_armada_ce_stream_transport_submit(1u,3u,1u,(uintptr_t)output.bytes+1u,sizeof(output.bytes))==KUI_ARMADA_CE_STREAM_TRANSPORT_ARGUMENT);
    CHECK(kui_armada_ce_stream_transport_submit(1u,3u,1u,(uintptr_t)output.before,sizeof(output))==KUI_ARMADA_CE_STREAM_TRANSPORT_ARGUMENT);
    CHECK(kui_armada_ce_stream_transport_submit(1u,3u,1u,(uintptr_t)output.bytes+sizeof(output.bytes)-4u,sizeof(output.bytes))==KUI_ARMADA_CE_STREAM_TRANSPORT_ARGUMENT);
    CHECK(!wire.fetches);for(unsigned i=0;i<sizeof(output.bytes);i++) CHECK(output.bytes[i]==0xa5u);
    submit(1u,3u,1u);
    CHECK(kui_armada_ce_stream_transport_submit(1u,3u,1u,(uintptr_t)output.bytes,sizeof(output.bytes))==KUI_ARMADA_CE_STREAM_TRANSPORT_STALE);
    CHECK(kui_armada_ce_stream_transport_submit(2u,3u,1u,(uintptr_t)output.bytes,sizeof(output.bytes))==KUI_ARMADA_CE_STREAM_TRANSPORT_BUSY);
    CHECK(kui_armada_ce_stream_transport_cancel(2u)==KUI_ARMADA_CE_STREAM_TRANSPORT_STALE);
    CHECK(kui_armada_ce_stream_transport_snapshot(0u,NULL)==KUI_ARMADA_CE_STREAM_TRANSPORT_ARGUMENT);
    intact();
    /* An audio track may be intentionally unbacked. Compact the extent table
     * into another fully valid immutable map rather than mutating after init. */
    fixture();uint32_t slot=kui_retail_track_first_extent(&manifest.slots[1].track);
    uint32_t removed=manifest.slots[1].track.extent_count;
    memmove(&manifest.slots[slot],&manifest.slots[slot+removed],
            (manifest.track_count+manifest.extent_count-slot-removed)*sizeof(manifest.slots[0]));
    manifest.extent_count-=removed;manifest.slots[1].track.extent_count=0u;
    for(unsigned i=2;i<4u;i++) manifest.slots[i].track.first_extent-=removed;
    CHECK(kui_retail_manifest_validate(&manifest)==KUI_GAME_OK);attach();
    CHECK(kui_armada_ce_stream_transport_submit(1u,3u,1u,(uintptr_t)output.bytes,sizeof(output.bytes))==KUI_ARMADA_CE_STREAM_TRANSPORT_ARGUMENT);
    CHECK(!wire.fetches);intact();
}
static void data_active(uint32_t token) {
    region.engine.active=1u;region.engine.token=token;
    region.shared.service.pending=1u;region.shared.service.token=token;
}
static void credits(unsigned count) {
    for(unsigned n=0;n<count;n++) (void)kui_armada_ce_stream_transport_event(KUI_ARMADA_CE_STREAM_BUS_DATA_BLOCK,0u);
}
static struct kui_armada_ce_stream_data_take held(uint32_t lba,bool found) {
    struct kui_armada_ce_stream_data_take request={.lba=lba,.result=99u,.copied=99u};
    CHECK(kui_armada_ce_stream_transport_event(KUI_ARMADA_CE_STREAM_BUS_DATA_TAKE,(uintptr_t)&request)==(uint32_t)found);
    CHECK(request.copied==(uint32_t)found);if(!found) CHECK(!request.block);return request;
}
static uint8_t data_output[3u*2352u];
static uint32_t data_written;
static void write_data(void *context,uint32_t offset,const uint8_t *bytes,uint32_t count) {
    CHECK(context==data_output && offset==data_written && count<=sizeof(data_output)-offset);
    for(uint32_t n=0;n<count;n++) CHECK(bytes[n]==source(0u,offset+n));
    memcpy(data_output+offset,bytes,count);data_written+=count;
}
static void test_parked_data(void) {
    setup();data_active(77u);memset(data_output,0xa5,sizeof(data_output));data_written=0u;
    CHECK(kui_retail_cursor_begin(&region.engine.cursor,&manifest,0u,3u,KUI_GAME_SECTOR_RAW,
                                  write_data,data_output)==KUI_GAME_OK);
    uint32_t first=region.engine.cursor.block;CHECK(region.engine.cursor.run>=2u);
    checked(first,KUI_SCI_STREAM_OK);checked(first+1u,KUI_SCI_STREAM_OK);
    submit(1u,45002u,8u);pump_one(1u,false);CHECK(!wire.fetches && snap(1u).owner!=2u);
    credits(15u);CHECK(kui_armada_ce_stream_transport_event(KUI_ARMADA_CE_STREAM_BUS_DATA_PREFETCH,0u));
    pump_one(1u,false);CHECK(!wire.fetches);
    credits(1u);CHECK(!kui_armada_ce_stream_transport_event(KUI_ARMADA_CE_STREAM_BUS_DATA_PREFETCH,0u));
    pump_one(1u,false);CHECK(snap(1u).holds==2u && wire.busy && snap(1u).owner==2u);
    /* Receive areas are reused by AUDIO; parked blocks still contain DATA. */
    struct kui_armada_ce_stream_data_take a=held(first,true);
    CHECK(a.result==KUI_SCI_STREAM_OK && a.block && !memcmp(a.block,card_bytes+first*512u,512u));
    CHECK(kui_retail_cursor_feed(&region.engine.cursor,a.block)==KUI_GAME_OK);held(first,false);
    a=held(first+1u,true);CHECK(a.result==KUI_SCI_STREAM_OK && a.block);
    CHECK(kui_retail_cursor_feed(&region.engine.cursor,a.block)==KUI_GAME_OK);held(first+1u,false);
    CHECK(data_written==1024u && !region.engine.failed && region.engine.token==77u);
    for(unsigned n=0;n<1024u;n++) CHECK(data_output[n]==source(0u,n));
    uint32_t audio_before=snap(1u).audio_blocks;
    for(unsigned n=0;n<40u && snap(1u).owner==2u;n++) pump_one(1u,true);
    CHECK(snap(1u).audio_blocks-audio_before==8u && snap(1u).owner==0u && wire.wakes);
    CHECK(snap(1u).status==KUI_ARMADA_CE_STREAM_TRANSPORT_PENDING);
    pump_one(1u,false);CHECK(snap(1u).owner==0u);
    /* Continuously active DATA cannot starve AUDIO: each sixteen checked
     * DATA deliveries permits another eight checked AUDIO deliveries. */
    region.engine.in_irq=1u;
    for(unsigned cycle=0;cycle<20u && snap(1u).status==KUI_ARMADA_CE_STREAM_TRANSPORT_PENDING;cycle++) {
        uint32_t previous=snap(1u).audio_blocks;
        credits(16u);CHECK(snap(1u).owner==2u);
        for(unsigned n=0;n<40u && snap(1u).owner==2u;n++) pump_one(1u,true);
        CHECK(snap(1u).audio_blocks>previous && snap(1u).audio_blocks-previous<=8u);
    }
    CHECK(snap(1u).status==KUI_ARMADA_CE_STREAM_TRANSPORT_DONE);exact_audio(45002u,8u);
    /* DATA then resumes at its exact next card block, without either held
     * block delivered twice. Its full raw output remains independently exact. */
    while(region.engine.cursor.done<region.engine.cursor.count) {
        uint32_t lba=region.engine.cursor.block;held(lba,false);
        CHECK(kui_retail_cursor_feed(&region.engine.cursor,card_bytes+lba*512u)==KUI_GAME_OK);
    }
    CHECK(data_written==sizeof(data_output));
    for(unsigned n=0;n<sizeof(data_output);n++) CHECK(data_output[n]==source(0u,n));
    CHECK(!wire.waits && region.engine.token==77u && !region.engine.failed);intact();
}
/* Keep delivering real DATA cursors while AUDIO is pending. This oracle
 * checks every DATA byte before dispatching the actual DATA_BLOCK callback;
 * credit calls here are consequences of checked deliveries, not clock ticks. */
static void test_continuous_data(void) {
    setup();data_active(100u);region.engine.in_irq=1u;
    submit(1u,45002u,8u);wire.latency=2u;
    unsigned jobs=0u,delivered=0u;
    for(unsigned cycle=0;cycle<20u && snap(1u).status==KUI_ARMADA_CE_STREAM_TRANSPORT_PENDING;cycle++) {
        CHECK(kui_armada_ce_stream_transport_event(KUI_ARMADA_CE_STREAM_BUS_DATA_ENTER,0u));
        uint32_t before=snap(1u).audio_blocks;
        for(unsigned n=0;n<16u;n++) {
            if(!jobs || region.engine.cursor.done==region.engine.cursor.count) {
                if(jobs) CHECK(data_written==sizeof(data_output));
                ++jobs;data_active(100u+jobs);data_written=0u;
                CHECK(kui_retail_cursor_begin(&region.engine.cursor,&manifest,0u,3u,
                    KUI_GAME_SECTOR_RAW,write_data,data_output)==KUI_GAME_OK);
            }
            uint32_t lba=region.engine.cursor.block;
            checked(lba,KUI_SCI_STREAM_OK);
            enum kui_sci_stream_result result=KUI_SCI_STREAM_PENDING;
            const uint8_t *block=mock_take(lba,&result);
            CHECK(block && result==KUI_SCI_STREAM_OK);
            CHECK(kui_retail_cursor_feed(&region.engine.cursor,block)==KUI_GAME_OK);
            ++delivered;
            uint32_t granted=kui_armada_ce_stream_transport_event(KUI_ARMADA_CE_STREAM_BUS_DATA_BLOCK,0u);
            CHECK(granted==(uint32_t)(n==15u));
            if(n<15u) CHECK(snap(1u).owner==1u && !wire.busy);
        }
        CHECK(snap(1u).owner==2u && wire.busy && !wire.waits);
        for(unsigned n=0;n<80u && snap(1u).owner==2u;n++) pump_one(1u,true);
        CHECK(snap(1u).audio_blocks>before && snap(1u).audio_blocks-before<=8u);
        CHECK(!region.engine.failed && region.engine.active && region.shared.service.pending);
    }
    CHECK(snap(1u).status==KUI_ARMADA_CE_STREAM_TRANSPORT_DONE);
    CHECK(snap(1u).data_blocks==delivered && delivered>=64u && jobs>=4u);
    exact_audio(45002u,8u);CHECK(!wire.waits);intact();
}
static void test_fences(void) {
    setup();wire.latency=100u;submit(1u,45002u,8u);pump_one(1u,false);
    CHECK(wire.busy && snap(1u).bytes==0u);
    CHECK(!kui_armada_ce_stream_transport_event(KUI_ARMADA_CE_STREAM_BUS_DATA_ENTER,0u));
    CHECK(!wire.waits && wire.busy);
    CHECK(kui_armada_ce_stream_transport_event(KUI_ARMADA_CE_STREAM_BUS_DATA_ENTER,1u));
    CHECK(wire.waits==1u && wire.completions==1u && !wire.busy);
    CHECK(snap(1u).audio_blocks==1u && snap(1u).bytes<=512u);
    CHECK(snap(1u).status==KUI_ARMADA_CE_STREAM_TRANSPORT_PENDING && snap(1u).owner==1u);
    reference_read(45002u,8u);CHECK(!memcmp(output.bytes,expected,snap(1u).bytes));
    for(unsigned n=snap(1u).bytes;n<sizeof(output.bytes);n++) CHECK(output.bytes[n]==0xa5u);
    CHECK(snap(1u).preemptions==1u);intact();
    /* A full CE virtual-piece spill ring is backpressure, not a reason to
     * make DATA wait for a complete CDDA chunk. IDLE lends SCI to AUDIO. */
    setup();data_active(5u);region.engine.spill_bytes=KUI_RETAIL_ASYNC_SPILL_BYTES;
    submit(1u,3u,1u);CHECK(kui_armada_ce_stream_transport_event(KUI_ARMADA_CE_STREAM_BUS_IDLE,0u));
    CHECK(wire.busy && snap(1u).owner==2u && !wire.waits);
    region.engine.spill_bytes=0u;region.engine.active=0u;finish_audio(1u);exact_audio(3u,1u);
}
static void test_token_holds_and_errors(void) {
    setup();data_active(8u);region.engine.cursor.block=100u;region.engine.cursor.run=2u;
    checked(100u,KUI_SCI_STREAM_OK);checked(101u,KUI_SCI_STREAM_CRC);
    submit(1u,3u,1u);credits(16u);pump_one(1u,false);
    CHECK(snap(1u).holds==2u && !region.engine.failed);
    struct kui_armada_ce_stream_data_take a=held(101u,true);
    CHECK(a.result==KUI_SCI_STREAM_CRC && !a.block);held(101u,false);
    region.engine.token=9u;region.shared.service.token=9u;held(100u,false);
    CHECK(!region.engine.failed && snap(1u).retries==0u);
    region.engine.active=0u;finish_audio(1u);exact_audio(3u,1u);
    /* A failed DATA reception is parked for its DATA caller, rather than
     * becoming an AUDIO retry or an unchecked audio bank. */
    setup();data_active(8u);region.engine.cursor.block=100u;region.engine.cursor.run=1u;
    wire.lba=100u;wire.busy=wire.inflight_dma=1u;wire.poll_faults=1u;wire.poll_error=KUI_SCI_STREAM_OVERRUN;
    submit(1u,3u,1u);credits(16u);pump_one(1u,false);
    a=held(100u,true);CHECK(a.result==KUI_SCI_STREAM_OVERRUN && !a.block && snap(1u).retries==0u);
    CHECK(!region.engine.failed);region.engine.active=0u;finish_audio(1u);exact_audio(3u,1u);
}
static void test_audio_errors(void) {
    for(unsigned faults=1u;faults<=3u;faults++) {
        setup();wire.crc_faults=faults;submit(1u,45002u,8u);finish_audio(1u);
        exact_audio(45002u,8u);CHECK(!region.engine.failed && wire.fetches==snap(1u).audio_blocks+faults);
        if(faults>=2u) CHECK(wire.polled);
    }
    static const enum kui_sci_stream_result error[]={KUI_SCI_STREAM_TOKEN,KUI_SCI_STREAM_COMMAND,KUI_SCI_STREAM_OVERRUN};
    for(unsigned k=0;k<sizeof(error)/sizeof(error[0]);k++) {
        setup();wire.fetch_faults=3u;wire.fetch_error=error[k];submit(1u,3u,1u);
        finish_audio(1u);exact_audio(3u,1u);CHECK(wire.polled && !region.engine.failed);
    }
    setup();wire.crc_faults=100u;submit(1u,45002u,8u);
    for(unsigned n=0;n<100u && snap(1u).status==KUI_ARMADA_CE_STREAM_TRANSPORT_PENDING;n++) pump_one(1u,false);
    CHECK(snap(1u).status==KUI_ARMADA_CE_STREAM_TRANSPORT_IO && snap(1u).retries==9u && wire.fetches==9u);
    CHECK(!wire.busy && !region.engine.failed && !snap(1u).bytes);
    for(unsigned n=0;n<sizeof(output.bytes);n++) CHECK(output.bytes[n]==0xa5u);
    setup();wire.fetch_faults=100u;wire.fetch_error=KUI_SCI_STREAM_TOKEN;submit(1u,3u,1u);
    for(unsigned n=0;n<100u && snap(1u).status==KUI_ARMADA_CE_STREAM_TRANSPORT_PENDING;n++) pump_one(1u,false);
    CHECK(snap(1u).status==KUI_ARMADA_CE_STREAM_TRANSPORT_IO && snap(1u).retries==9u && wire.fetches==9u);
    CHECK(!wire.busy && !region.engine.failed && !snap(1u).bytes);
    setup();wire.poll_faults=1u;wire.poll_error=KUI_SCI_STREAM_RESET;submit(1u,3u,1u);
    pump_one(1u,false);pump_one(1u,false);
    CHECK(snap(1u).status==KUI_ARMADA_CE_STREAM_TRANSPORT_IO && wire.fetches==1u && !wire.busy);
    setup();wire.open_fails=1u;submit(1u,3u,1u);pump_one(1u,false);
    CHECK(snap(1u).status==KUI_ARMADA_CE_STREAM_TRANSPORT_IO && wire.opens==1u && !wire.fetches);intact();
}
static void test_cancel(void) {
    setup();wire.latency=4u;submit(11u,45002u,8u);pump_one(11u,false);
    CHECK(wire.busy);uint8_t untouched[AUDIO_BYTES];memcpy(untouched,output.bytes,sizeof(untouched));
    CHECK(kui_armada_ce_stream_transport_cancel(11u)==KUI_ARMADA_CE_STREAM_TRANSPORT_CANCELLED);
    CHECK(kui_armada_ce_stream_transport_submit(12u,3u,1u,(uintptr_t)output.bytes,sizeof(output.bytes))==KUI_ARMADA_CE_STREAM_TRANSPORT_BUSY);
    for(unsigned n=0;n<10u && wire.busy;n++) {
        pump_one(11u,false);CHECK(!memcmp(output.bytes,untouched,sizeof(untouched)));
    }
    CHECK(!wire.busy && !wire.waits && snap(11u).owner==0u && snap(11u).bytes==0u);
    CHECK(kui_armada_ce_stream_transport_submit(10u,3u,1u,(uintptr_t)output.bytes,sizeof(output.bytes))==KUI_ARMADA_CE_STREAM_TRANSPORT_STALE);
    CHECK(kui_armada_ce_stream_transport_cancel(10u)==KUI_ARMADA_CE_STREAM_TRANSPORT_STALE);
    wire.latency=0u;submit(12u,3u,1u);finish_audio(12u);exact_audio(3u,1u);
    CHECK(kui_armada_ce_stream_transport_snapshot(11u,&(struct kui_armada_ce_stream_transport_snapshot){0})==KUI_ARMADA_CE_STREAM_TRANSPORT_STALE);
    /* Cancellation after checked prefix delivery preserves that prefix and
     * never copies the next completed bank into the abandoned source ticket. */
    setup();submit(1u,45002u,8u);pump_one(1u,false);pump_one(1u,false);
    CHECK(snap(1u).bytes && wire.busy);uint32_t bytes=snap(1u).bytes;
    memcpy(untouched,output.bytes,sizeof(untouched));
    CHECK(kui_armada_ce_stream_transport_cancel(1u)==KUI_ARMADA_CE_STREAM_TRANSPORT_CANCELLED);
    pump_one(1u,false);CHECK(!memcmp(output.bytes,untouched,sizeof(untouched)) && snap(1u).bytes==bytes);
    CHECK(!wire.busy && !wire.waits);intact();
}
int main(void) {
    test_raw_mapping();test_admission();test_parked_data();test_continuous_data();test_fences();
    test_token_holds_and_errors();test_audio_errors();test_cancel();
    printf("Armada CE production SCI transport: %u checks passed\n",checks);
    return 0;
}
