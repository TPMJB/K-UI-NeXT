/* SPDX-License-Identifier: GPL-3.0-only */
/* The production shared SCI arbiter, cursor and GD service with a separately
 * scheduled card receiver. A DMA arrives only when independent simulated
 * time advances or the test explicitly schedules its completion. Payloads are
 * compared with the ordinary image reader, including fragmented raw tracks. */
#include "toy_pilot_sci.h"
#include "kui/retail_gd.h"
#include "kui/retail_image.h"
#include "sci_stream.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define BEGIN UINT32_C(0x8c010000)
#define END (BEGIN + UINT32_C(0x40000))
#define PARAM (BEGIN + 32u)
#define STATUS (BEGIN + 64u)
#define OUT (BEGIN + 4096u)
#define OUT2 (BEGIN + 131072u)
#define GAME_VBR UINT32_C(0x8c0f0000)
#define IPRB UINT32_C(0xffd00008)
#define BLOCKS 2048u
#define MAX_SECTORS 32u
#define CHECK(test) do { ++checks; assert(test); } while(0)

static unsigned checks;
extern volatile uint32_t kui_toy_pilot_sci_irq_fault;
static uint8_t ram[END-BEGIN], card_bytes[BLOCKS*512u];
static uint8_t expected[MAX_SECTORS*2352u];
static struct kui_retail_manifest manifest;
static struct kui_retail_image reference;
static struct kui_loader_sd card;
static struct kui_retail_gd service;
static struct {
    uint32_t vbr, ticks, deny, tick_step, revoke_tick;
    uint16_t iprb;
    unsigned acquires, releases, maps, bytes, callbacks;
    unsigned blocked_acquires, clock_reads;
    bool leased, frozen;
} hw;
static struct {
    uint8_t *area[2];
    uint32_t lba[2], in_lba, ready_tick, receiver_delay;
    unsigned fill, in_area, opens, fetches, polls, takes, stops, waits;
    unsigned token_budgets, token_remaining, token_bytes;
    unsigned foreground_polls, background_polls, partial_bytes, poll_tick_cost;
    bool valid[2], flight, arrived, token_pending, deferred_resume;
    enum kui_sci_stream_result open_error, fetch_error, poll_error, take_error, stop_error;
    struct kui_sci_stream_stats stats;
} wire;

/* Platform seams: the engine may only change SCI's IPRB nibble, and every
 * low bus acquisition must be paired with release after the receiver stops. */
uint32_t kui_toy_pilot_sci_test_vbr(void) { return hw.vbr; }
void kui_toy_pilot_sci_test_set_vbr(uint32_t value) { hw.vbr=value; }
uint16_t kui_toy_pilot_sci_test_read16(uint32_t address) {
    CHECK(address==IPRB); return hw.iprb;
}
void kui_toy_pilot_sci_test_write16(uint32_t address,uint16_t value) {
    CHECK(address==IPRB && (value & 0xff0fu)==(hw.iprb & 0xff0fu)); hw.iprb=value;
}
static void advance_clock(uint32_t amount) {
    hw.ticks+=amount;
    /* This receiver runs while the game masks IRQ delivery. Poll merely
     * observes completion; it never itself advances the transfer. */
    if(wire.flight && wire.receiver_delay &&
       (int32_t)(hw.ticks-wire.ready_tick)>=0) wire.arrived=true;
    /* Independently arriving DMA may already have overwritten its private
     * receiver while no complete, CRC-verified card block is available. */
    if(wire.flight && wire.receiver_delay && !wire.arrived && !wire.partial_bytes &&
       (int32_t)(hw.ticks-(wire.ready_tick-wire.receiver_delay/2u))>=0) {
        memcpy(wire.area[wire.in_area],card_bytes+wire.in_lba*512u,64u);
        wire.partial_bytes=64u;
    }
    if(hw.revoke_tick && (int32_t)(hw.ticks-hw.revoke_tick)>=0) {
        hw.revoke_tick=0u; service.destination=OUT2; hw.frozen=true;
    }
}
uint32_t kui_toy_pilot_sci_test_ticks(void) {
    uint32_t now=hw.ticks;
    advance_clock(hw.tick_step); ++hw.clock_reads;
    return now;
}
const uint32_t kui_toy_pilot_sci_forward[3]={0x11111111u,0x22222222u,0u};
const uint32_t kui_toy_pilot_sci_interrupt[11]={1u,2u,3u,4u,5u,6u,7u,8u,0xff000028u,10u,11u};
void kui_toy_pilot_sci_release_100(void) {}
void kui_toy_pilot_sci_release_400(void) {}
void kui_toy_pilot_sci_release_600(void) {}
void kui_toy_pilot_sci_rehook(void) {}
static enum kui_loader_sd_result acquire(void) {
    ++hw.acquires;
    CHECK(!hw.leased);
    if(hw.blocked_acquires) { --hw.blocked_acquires; return KUI_LOADER_SD_UNSUPPORTED; }
    hw.leased=true; return KUI_LOADER_SD_OK;
}
static void release(void) {
    CHECK(hw.leased && !wire.flight); hw.leased=false; ++hw.releases;
}

enum kui_sci_stream_result kui_sci_stream_open(const struct kui_loader_sd *c,
    uint8_t *area0,uint8_t *area1,bool unknown) {
    (void)unknown;
    CHECK(c==&card && hw.leased && !wire.flight && area0 && area1 && area0!=area1);
    CHECK(!((uintptr_t)area0 & 31u) && !((uintptr_t)area1 & 31u));
    ++wire.opens; wire.area[0]=area0; wire.area[1]=area1;
    return wire.open_error;
}
enum kui_sci_stream_result kui_sci_stream_fetch(uint32_t lba,uint32_t limit,bool polled) {
    CHECK(hw.leased && !wire.flight && lba<BLOCKS && limit);
    if(polled) CHECK(wire.take_error || wire.poll_error || wire.fetch_error || wire.stats.crc_errors);
    ++wire.fetches; ++wire.stats.starts;
    if(wire.fetch_error) return wire.fetch_error;
    if(wire.token_pending) {
        wire.token_bytes+=wire.token_remaining; wire.token_remaining=0u;
        ++wire.stats.token_yields; return KUI_SCI_STREAM_PENDING;
    }
    wire.in_area=wire.fill; wire.fill^=1u; wire.valid[wire.in_area]=false;
    wire.in_lba=lba; wire.flight=true; wire.arrived=false;
    wire.partial_bytes=0u;
    if(polled) ++wire.stats.polled;
    wire.ready_tick=hw.ticks+wire.receiver_delay;
    return KUI_SCI_STREAM_OK;
}
bool kui_sci_stream_busy(void) { return wire.flight; }
bool kui_sci_stream_ready(uint32_t lba) {
    return (wire.valid[0] && wire.lba[0]==lba) || (wire.valid[1] && wire.lba[1]==lba);
}
void kui_sci_stream_token_budget(bool bounded) {
    CHECK(bounded); ++wire.token_budgets; wire.token_remaining=256u;
}
bool kui_sci_stream_token_pending(void) { return wire.token_pending; }
enum kui_sci_stream_result kui_sci_stream_poll(bool interrupt) {
    if(interrupt) ++wire.background_polls; else ++wire.foreground_polls;
    CHECK(hw.leased); ++wire.polls;
    if(wire.poll_tick_cost) advance_clock(wire.poll_tick_cost);
    if(wire.poll_error) { wire.flight=false; return wire.poll_error; }
    if(!wire.flight) return KUI_SCI_STREAM_OK;
    if(!wire.arrived) return KUI_SCI_STREAM_PENDING;
    if(wire.deferred_resume) {
        wire.deferred_resume=false; wire.flight=false;
        return KUI_SCI_STREAM_PENDING;
    }
    memcpy(wire.area[wire.in_area],card_bytes+wire.in_lba*512u,512u);
    wire.lba[wire.in_area]=wire.in_lba; wire.valid[wire.in_area]=true;
    wire.flight=false; ++wire.stats.blocks;
    return KUI_SCI_STREAM_OK;
}
enum kui_sci_stream_result kui_sci_stream_wait(void) {
    ++wire.waits; CHECK(!"The shared async arbiter waited for a payload");
    return KUI_SCI_STREAM_PENDING;
}
const uint8_t *kui_sci_stream_take(uint32_t lba,enum kui_sci_stream_result *result) {
    CHECK(hw.leased); ++wire.takes;
    if(wire.take_error) {
        if(wire.take_error==KUI_SCI_STREAM_CRC) ++wire.stats.crc_errors;
        *result=wire.take_error; return NULL;
    }
    for(unsigned i=0;i<2u;i++) if(wire.valid[i] && wire.lba[i]==lba) {
        *result=KUI_SCI_STREAM_OK; return wire.area[i];
    }
    *result=KUI_SCI_STREAM_PENDING; return NULL;
}
void kui_sci_stream_discard(void) {
    CHECK(!wire.flight); wire.valid[0]=wire.valid[1]=false;
}
enum kui_sci_stream_result kui_sci_stream_stop(void) {
    CHECK(hw.leased); ++wire.stops;
    /* Cancellation can stop a receiver. Audio handoff must arrive at a
     * verified boundary before reaching this operation. */
    wire.flight=false; wire.token_pending=false;
    return wire.stop_error;
}
const struct kui_sci_stream_stats *kui_sci_stream_stats(void) { return &wire.stats; }
static void arrive(void) { if(wire.flight) wire.arrived=true; }

static uint8_t source(unsigned track,uint32_t at) {
    uint32_t inside=at%2352u,sector=at/2352u;
    if(track!=1u && inside<16u) {
        if(!inside || inside==11u) return 0u;
        if(inside<11u) return 255u;
        return inside==15u?1u:0u;
    }
    return (uint8_t)(track*91u+sector*53u+inside*11u+(inside>>8));
}
static int read_block(void *context,uint32_t lba,uint8_t out[512]) {
    (void)context; CHECK(lba<BLOCKS); memcpy(out,card_bytes+lba*512u,512u); return 0;
}
static uint8_t *map(void *context,uint32_t address,uint32_t bytes,int writing) {
    (void)context; ++hw.maps;
    CHECK(writing>=0 && writing<=KUI_RETAIL_MAP_VALIDATE);
    if(address<BEGIN || address>=END || bytes>END-address || address==hw.deny) return NULL;
    if(writing==KUI_RETAIL_MAP_VALIDATE) return ram+sizeof(ram);
    if(writing==1 && address>=OUT) {
        CHECK(!hw.frozen); hw.bytes+=bytes; ++hw.callbacks;
    }
    return ram+address-BEGIN;
}
static int check(void *context,uint32_t lba,uint32_t count,uint32_t bytes) {
    (void)context;
    return kui_retail_image_check_validated(&manifest,lba,count,
        bytes==2352u?KUI_GAME_SECTOR_RAW:KUI_GAME_SECTOR_MODE1)==KUI_GAME_OK?0:-1;
}
static int forbidden_read(void *context,uint32_t lba,uint32_t count,uint32_t bytes,void *out) {
    (void)context; (void)lba; (void)count; (void)bytes; (void)out;
    CHECK(!"Async GD service invoked the synchronous reader"); return -1;
}
static void put(uint32_t address,uint32_t value) {
    for(unsigned i=0;i<4u;i++) ram[address-BEGIN+i]=(uint8_t)(value>>(8u*i));
}
static uint32_t get(uint32_t address) {
    const uint8_t *p=ram+address-BEGIN;
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static void fixture(bool cooked,bool scattered) {
    memset(&manifest,0,sizeof(manifest)); memset(card_bytes,0xf3,sizeof(card_bytes));
    manifest.card_sectors=BLOCKS; manifest.partition_start=50u; manifest.partition_end=2000u;
    manifest.track_count=3u; manifest.session_lba=45000u;
    manifest.boot_lba=45001u; manifest.boot_bytes=4567u;
    strcpy(manifest.title,"Shared SCI test"); strcpy(manifest.bootfile,"1ST_READ.BIN");
    static const uint32_t starts[]={0u,4u,45000u},ends[]={4u,12u,45032u};
    uint32_t used=0u;
    for(unsigned track=0;track<3u;track++) {
        struct kui_retail_track *t=&manifest.slots[track].track;
        *t=(struct kui_retail_track){.start_lba=starts[track],.end_lba=ends[track],
            .control=track==1u?0u:(uint8_t)(4u|(cooked?KUI_RETAIL_TRACK_COOKED:0u)),
            .first_extent=(uint16_t)(3u+manifest.extent_count)};
        uint32_t stride=kui_retail_track_sector_bytes(t),bytes=(t->end_lba-t->start_lba)*stride;
        uint32_t blocks=(bytes+511u)/512u;
        for(uint32_t first=0;first<blocks;) {
            uint32_t take=blocks-first>7u?7u:blocks-first,index=manifest.extent_count;
            uint32_t physical=scattered?100u+(index&1u?900u:0u)+(index>>1)*8u:100u+used;
            CHECK(3u+index<KUI_RETAIL_IMAGE_SLOTS && physical+take<2000u);
            manifest.slots[3u+manifest.extent_count++].extent=(struct kui_retail_extent){first,physical,take};
            ++t->extent_count;
            for(uint32_t p=0;p<take*512u && first*512u+p<bytes;p++) {
                uint32_t at=first*512u+p;
                uint32_t original=stride==2048u?at/2048u*2352u+16u+at%2048u:at;
                card_bytes[physical*512u+p]=source(track,original);
            }
            first+=take; used+=take;
        }
    }
    CHECK(kui_retail_manifest_validate(&manifest)==KUI_GAME_OK);
    CHECK(kui_retail_image_init(&reference,&manifest,read_block,NULL)==KUI_GAME_OK);
}
static void setup(bool cooked,bool scattered) {
    fixture(cooked,scattered); memset(&wire,0,sizeof(wire)); memset(&hw,0,sizeof(hw));
    hw.vbr=GAME_VBR; hw.iprb=0x5a0fu; hw.tick_step=1u;
    memset(&card,0,sizeof(card)); card.ready=true; card.high_capacity=true; card.blocks=BLOCKS;
    const struct kui_gd_ops ops={NULL,map,check,forbidden_read};
    CHECK(!kui_retail_gd_init(&service,manifest.slots,manifest.track_count,&ops,BEGIN,END));
    memset(ram,0xa5,sizeof(ram));
    kui_toy_pilot_sci_init(&manifest,&card,acquire,release);
}
static int32_t gd(uint32_t fn,uint32_t r4,uint32_t r5) {
    return kui_retail_gd_dispatch(&service,r4,r5,0u,fn);
}
static uint32_t request(uint32_t lba,uint32_t count,uint32_t destination,bool raw) {
    put(PARAM,0u); put(PARAM+4u,raw?0x1000u:0x2000u);
    put(PARAM+8u,raw?0u:1024u); put(PARAM+12u,raw?2352u:2048u);
    CHECK(gd(KUI_GD_DATATYPE,PARAM,0u)==0);
    put(PARAM,lba+150u); put(PARAM+4u,count); put(PARAM+8u,destination); put(PARAM+12u,0u);
    int32_t token=gd(KUI_GD_REQUEST,KUI_GD_DMAREAD,PARAM);
    CHECK(token>0 && service.pending && service.completed_bytes==0u);
    return (uint32_t)token;
}
static void visit(bool irq) {
    unsigned bytes=hw.bytes,takes=wire.takes;
    if(irq) CHECK(kui_toy_pilot_sci_irq()==0u);
    else (void)kui_toy_pilot_sci_pump(&service);
    CHECK(hw.bytes-bytes<=512u && wire.takes-takes<=1u && wire.waits==0u);
}
static int service_visit(void) {
    unsigned bytes=hw.bytes,budgets=wire.token_budgets;
    const struct kui_toy_pilot_sci_stats *stats=kui_toy_pilot_sci_snapshot();
    uint32_t blocks=stats->call_blocks+stats->irq_blocks;
    int result=kui_toy_pilot_sci_service(&service);
    CHECK(hw.bytes-bytes<=4u*512u &&
        stats->call_blocks+stats->irq_blocks-blocks<=4u);
    CHECK(wire.token_budgets==budgets+1u && !wire.waits);
    return result;
}
static void finish(void) {
    for(unsigned n=0;n<1000u && service.pending;n++) { arrive(); visit((n&1u)!=0u); }
    CHECK(!service.pending && service.status==KUI_GD_COMPLETED && !service.error);
}
static void finish_foreground(void) {
    for(unsigned n=0;n<1000u && service.pending;n++) (void)service_visit();
    CHECK(!service.pending && service.status==KUI_GD_COMPLETED && !service.error);
}
static void compare(uint32_t lba,uint32_t count,uint32_t destination,bool raw) {
    unsigned bytes=count*(raw?2352u:2048u);
    CHECK(bytes<=sizeof(expected));
    CHECK(kui_retail_image_read(&reference,lba,count,raw?KUI_GAME_SECTOR_RAW:
        KUI_GAME_SECTOR_MODE1,expected,sizeof(expected))==KUI_GAME_OK);
    CHECK(!memcmp(ram+destination-BEGIN,expected,bytes));
    CHECK(ram[destination-BEGIN+bytes]==0xa5u);
}
static void cleanup(void) {
    kui_toy_pilot_sci_cancel(&service);
    CHECK(!wire.flight && !hw.leased && hw.vbr==GAME_VBR && hw.iprb==0x5a0fu);
}

static void nonblocking_and_complete(void) {
    setup(false,true);
    uint32_t token=request(45000u,5u,OUT,false);
    CHECK(gd(KUI_GD_EXEC,0u,0u)==0 && !wire.fetches);
    visit(false);
    CHECK(wire.flight && hw.leased && hw.vbr!=GAME_VBR);
    unsigned calls=wire.fetches,bytes=hw.bytes;
    for(unsigned n=0;n<20u;n++) {
        visit((n&1u)!=0u);
        CHECK(service.pending && service.completed_bytes==0u && wire.flight);
    }
    CHECK(wire.fetches==calls && hw.bytes==bytes && ram[OUT-BEGIN]==0xa5u);
    /* Only complete logical sectors enter BIOS byte progress, while their
     * constituent verified card blocks can already have been copied. */
    for(unsigned n=0;n<20u && service.completed_bytes<2048u;n++) { arrive(); visit(false); }
    CHECK(service.pending && service.completed_bytes==2048u && hw.bytes>=2048u);
    CHECK(gd(KUI_GD_CHECK,token,STATUS)==KUI_GD_PROCESSING && get(STATUS+8u)==2048u);
    finish(); compare(45000u,5u,OUT,false);
    CHECK(service.completed_bytes==5u*2048u);
    CHECK(gd(KUI_GD_CHECK,token,STATUS)==KUI_GD_COMPLETED);
    CHECK(gd(KUI_GD_CHECK,token,STATUS)==KUI_GD_NOT_FOUND);
    cleanup();
}
static void payload_layouts(void) {
    for(unsigned cooked=0;cooked<2u;cooked++) for(unsigned scattered=0;scattered<2u;scattered++) {
        setup(cooked!=0u,scattered!=0u);
        uint32_t token=request(45001u,MAX_SECTORS-1u,OUT,false);
        finish(); compare(45001u,MAX_SECTORS-1u,OUT,false);
        CHECK(gd(KUI_GD_CHECK,token,STATUS)==KUI_GD_COMPLETED); cleanup();
        setup(cooked!=0u,scattered!=0u);
        token=request(5u,7u,OUT,true);
        finish(); compare(5u,7u,OUT,true);
        CHECK(gd(KUI_GD_CHECK,token,STATUS)==KUI_GD_COMPLETED); cleanup();
        if(!cooked) {
            setup(false,scattered!=0u);
            token=request(45000u,3u,OUT,true);
            finish(); compare(45000u,3u,OUT,true);
            CHECK(gd(KUI_GD_CHECK,token,STATUS)==KUI_GD_COMPLETED); cleanup();
        }
    }
}
static void token_search_yields(void) {
    setup(false,false); uint32_t token=request(45000u,2u,OUT,false);
    wire.token_pending=true;
    for(unsigned n=0;n<10u;n++) {
        visit(false);
        CHECK(service.pending && !service.error && !wire.flight && !hw.bytes);
    }
    CHECK(wire.token_budgets && kui_toy_pilot_sci_snapshot()->token_yields);
    wire.token_pending=false;
    finish(); compare(45000u,2u,OUT,false);
    CHECK(gd(KUI_GD_CHECK,token,STATUS)==KUI_GD_COMPLETED); cleanup();
}
static void audio_boundary_handoff(void) {
    setup(false,true); uint32_t token=request(45000u,8u,OUT,false);
    visit(false); CHECK(wire.flight && hw.leased);
    unsigned bytes=hw.bytes,stops=wire.stops,releases=hw.releases;
    CHECK(kui_toy_pilot_sci_audio_acquire()==KUI_TOY_SCI_PENDING);
    CHECK(kui_toy_pilot_sci_audio_acquire()==KUI_TOY_SCI_PENDING);
    CHECK(wire.flight && hw.leased && wire.stops==stops && hw.releases==releases && hw.bytes==bytes);
    CHECK(service.pending && !service.error && !service.completed_bytes);
    arrive(); CHECK(kui_toy_pilot_sci_audio_acquire()==KUI_TOY_SCI_OK);
    CHECK(!wire.flight && !hw.leased && wire.stops==stops+1u && hw.releases==releases+1u);
    CHECK(hw.vbr==GAME_VBR && hw.iprb==0x5a0fu);
    CHECK(hw.bytes-bytes<=512u && service.pending && !service.error);
    /* The existing synchronous audio callback owns precisely this separate
     * low lease. Data pumping cannot steal it even if its handle stays live. */
    CHECK(acquire()==KUI_LOADER_SD_OK); unsigned fetches=wire.fetches;
    visit(false); CHECK(wire.fetches==fetches && hw.leased);
    release(); kui_toy_pilot_sci_audio_release();
    CHECK(kui_toy_pilot_sci_snapshot()->audio_pending==2u);
    CHECK(kui_toy_pilot_sci_snapshot()->audio_claims==1u);
    CHECK(kui_toy_pilot_sci_snapshot()->audio_releases==1u);
    finish(); compare(45000u,8u,OUT,false);
    CHECK(kui_toy_pilot_sci_snapshot()->data_resumes>=1u);
    CHECK(gd(KUI_GD_CHECK,token,STATUS)==KUI_GD_COMPLETED); cleanup();

    setup(false,false); CHECK(kui_toy_pilot_sci_audio_acquire()==KUI_TOY_SCI_OK);
    CHECK(!hw.leased && !wire.opens && !wire.flight);
    CHECK(acquire()==KUI_LOADER_SD_OK); release(); kui_toy_pilot_sci_audio_release(); cleanup();

    /* The hardware may stop early in a block and defer repairing it to a
     * later fetch. Merely observing !busy does not establish a card boundary
     * that permits a raw command: that same block must finish and pass CRC. */
    setup(false,true); token=request(45000u,4u,OUT,false);
    visit(false); wire.deferred_resume=true; arrive();
    CHECK(kui_toy_pilot_sci_audio_acquire()==KUI_TOY_SCI_PENDING);
    CHECK(!wire.flight && hw.leased && !wire.stops && !hw.bytes);
    CHECK(kui_toy_pilot_sci_audio_acquire()==KUI_TOY_SCI_PENDING);
    CHECK(wire.flight && hw.leased && !wire.stops && !hw.bytes);
    arrive(); CHECK(kui_toy_pilot_sci_audio_acquire()==KUI_TOY_SCI_OK);
    CHECK(!wire.flight && !hw.leased && wire.stops==1u && hw.bytes>0u && hw.bytes<=512u);
    CHECK(acquire()==KUI_LOADER_SD_OK); release(); kui_toy_pilot_sci_audio_release();
    finish(); compare(45000u,4u,OUT,false);
    CHECK(gd(KUI_GD_CHECK,token,STATUS)==KUI_GD_COMPLETED); cleanup();
}
static void cancellation_and_epochs(void) {
    setup(false,true); uint32_t token=request(45000u,3u,OUT,false);
    visit(false); CHECK(wire.flight);
    kui_toy_pilot_sci_cancel(&service);
    CHECK(service.pending && !service.completed_bytes && !wire.flight && !hw.leased);
    CHECK(gd(KUI_GD_ABORT,token,0u)==0);
    hw.frozen=true;
    for(unsigned n=0;n<5u;n++) { arrive(); visit(false); }
    CHECK(!hw.bytes && service.error==KUI_GD_ERROR_CANCELLED);
    hw.frozen=false; CHECK(gd(KUI_GD_CHECK,token,STATUS)==KUI_GD_FAILED); cleanup();

    /* A repeated token value is insufficient authority for an old DMA's
     * destination. An IRQ must revalidate the whole captured request. */
    setup(false,true); token=request(45000u,3u,OUT,false);
    visit(false); CHECK(wire.flight);
    service.destination=OUT2; hw.frozen=true; arrive(); visit(true);
    CHECK(!hw.bytes && ram[OUT-BEGIN]==0xa5u && ram[OUT2-BEGIN]==0xa5u);
    hw.frozen=false; kui_toy_pilot_sci_cancel(&service);
    (void)gd(KUI_GD_ABORT,token,0u); (void)gd(KUI_GD_CHECK,token,STATUS); cleanup();

    setup(false,false); token=request(45000u,4u,OUT,false);
    visit(false); CHECK(wire.flight);
    CHECK(gd(KUI_GD_RESET,0u,0u)==0); hw.frozen=true; arrive(); visit(true);
    CHECK(!hw.bytes && !service.command && !service.pending);
    hw.frozen=false; cleanup();
}
static void revoked_audio_wait_does_not_stall_data(void) {
    setup(false,true); uint32_t token=request(45000u,5u,OUT,false);
    visit(false); CHECK(wire.flight && hw.leased);
    CHECK(kui_toy_pilot_sci_audio_acquire()==KUI_TOY_SCI_PENDING);
    kui_toy_pilot_sci_audio_cancel(); arrive(); visit(true);
    CHECK(service.pending && wire.flight && hw.leased && !wire.stops);
    CHECK(!kui_toy_pilot_sci_snapshot()->audio_claims);
    finish(); compare(45000u,5u,OUT,false);
    CHECK(gd(KUI_GD_CHECK,token,STATUS)==KUI_GD_COMPLETED); cleanup();

    setup(false,true); token=request(45000u,3u,OUT,false);
    visit(false); CHECK(kui_toy_pilot_sci_audio_acquire()==KUI_TOY_SCI_PENDING);
    kui_toy_pilot_sci_cancel(&service); CHECK(!wire.flight && !hw.leased);
    CHECK(gd(KUI_GD_RESET,0u,0u)==0);
    token=request(45003u,4u,OUT2,false);
    finish(); compare(45003u,4u,OUT2,false);
    CHECK(!kui_toy_pilot_sci_snapshot()->audio_claims);
    CHECK(gd(KUI_GD_CHECK,token,STATUS)==KUI_GD_COMPLETED); cleanup();
}
static void destination_failure(void) {
    setup(false,false); uint32_t token=request(45000u,2u,OUT,false);
    visit(false); hw.deny=OUT; arrive(); visit(false);
    CHECK(!service.pending && service.error==KUI_GD_ERROR_MEMORY && !hw.bytes);
    CHECK(gd(KUI_GD_CHECK,token,STATUS)==KUI_GD_FAILED); hw.deny=0u; cleanup();
}
static void transport_faults(void) {
    static const enum kui_sci_stream_result errors[]={
        KUI_SCI_STREAM_BUSY,KUI_SCI_STREAM_COMMAND,KUI_SCI_STREAM_TOKEN,KUI_SCI_STREAM_RESET
    };
    for(unsigned i=0;i<sizeof(errors)/sizeof(*errors);i++) {
        setup(false,false); uint32_t token=request(45000u,2u,OUT,false);
        wire.fetch_error=errors[i];
        for(unsigned n=0;n<20u && service.pending;n++) { arrive(); visit(false); }
        CHECK(!service.pending && service.error && !hw.bytes && !wire.flight && !hw.leased);
        CHECK(gd(KUI_GD_CHECK,token,STATUS)==KUI_GD_FAILED); cleanup();
    }
    setup(false,false); uint32_t token=request(45000u,2u,OUT,false);
    visit(false); wire.poll_error=KUI_SCI_STREAM_BUSY; arrive(); visit(true);
    CHECK(!service.pending && service.error && !hw.bytes && !wire.flight && !hw.leased);
    CHECK(gd(KUI_GD_CHECK,token,STATUS)==KUI_GD_FAILED); cleanup();

    setup(false,false); token=request(45000u,2u,OUT,false);
    visit(false); wire.take_error=KUI_SCI_STREAM_CRC; arrive(); visit(false);
    CHECK(!hw.bytes && ram[OUT-BEGIN]==0xa5u);
    wire.take_error=KUI_SCI_STREAM_OK;
    if(service.pending) { finish(); compare(45000u,2u,OUT,false); }
    else CHECK(service.error);
    (void)gd(KUI_GD_CHECK,token,STATUS); cleanup();

    setup(false,false); token=request(45000u,2u,OUT,false);
    wire.take_error=KUI_SCI_STREAM_CRC;
    for(unsigned n=0;n<20u && service.pending;n++) { arrive(); visit(false); }
    CHECK(!service.pending && service.error==KUI_GD_ERROR_IO && !hw.bytes && !hw.leased);
    CHECK(kui_toy_pilot_sci_snapshot()->retries>1u);
    CHECK(gd(KUI_GD_CHECK,token,STATUS)==KUI_GD_FAILED); cleanup();

    setup(false,false); token=request(45000u,2u,OUT,false);
    visit(false); wire.stop_error=KUI_SCI_STREAM_COMMAND; arrive();
    CHECK(kui_toy_pilot_sci_audio_acquire()==KUI_TOY_SCI_FAULT);
    CHECK(!service.pending && service.error && !wire.flight && !hw.leased);
    CHECK(kui_toy_pilot_sci_audio_acquire()==KUI_TOY_SCI_FAULT);
    CHECK(gd(KUI_GD_CHECK,token,STATUS)==KUI_GD_FAILED); cleanup();

    setup(false,false); token=request(45000u,2u,OUT,false);
    visit(false); kui_toy_pilot_sci_irq_fault=1u; arrive(); visit(true);
    CHECK(!service.pending && service.error==KUI_GD_ERROR_IO && !hw.bytes && !hw.leased);
    CHECK(gd(KUI_GD_CHECK,token,STATUS)==KUI_GD_FAILED); cleanup();
}

/* The scheduling model is deliberately separate from the transfer poll.
 * DMA progresses with the passage of time even when the game does not
 * dispatch SCI IRQs. EXEC and CHECK run the same high-core protocol, while
 * their physical work alternates between the baseline one-step pump and
 * the bounded foreground service. The adapter fixture separately verifies
 * that the real split adapter selects that service only for EXEC/CHECK. */
static unsigned scheduled_gd_loop(bool foreground) {
    setup(true,true); uint32_t token=request(45000u,8u,OUT,false);
    hw.tick_step=10u; wire.receiver_delay=80u;
    visit(false); /* Accepted REQUEST remains a nonblocking first arm. */
    unsigned external=0u;
    while(service.pending && external<100u) {
        advance_clock(100u); /* Game execution, with no delivered SCI IRQ. */
        if(foreground) (void)service_visit(); else visit(false);
        if(external&1u) {
            int32_t result=gd(KUI_GD_CHECK,token,STATUS);
            CHECK(result==(service.pending?KUI_GD_PROCESSING:KUI_GD_COMPLETED));
            CHECK(get(STATUS+8u)==service.completed_bytes);
        } else CHECK(gd(KUI_GD_EXEC,0u,0u)==0);
        ++external;
    }
    CHECK(!service.pending && !service.error && external<100u);
    CHECK(kui_toy_pilot_sci_snapshot()->irq_calls==0u);
    CHECK(foreground?wire.foreground_polls>0u && !wire.background_polls:
        wire.background_polls>0u && !wire.foreground_polls);
    compare(45000u,8u,OUT,false); cleanup();
    return external;
}
static void foreground_progress_and_limits(void) {
    unsigned baseline=scheduled_gd_loop(false),topup=scheduled_gd_loop(true);
    CHECK(baseline==32u && topup==8u && topup*4u==baseline);
    printf("Masked-IRQ time-driven DMA: %u baseline EXEC/CHECK entries, %u bounded top-up entries\n",
        baseline,topup);

    /* A progressing timer cuts an unready transfer off at admission, with
     * at most one finite step crossing its 1500-tick allowance. */
    setup(true,false); (void)request(45000u,8u,OUT,false); visit(false);
    hw.tick_step=10u; unsigned polls=wire.polls,reads=hw.clock_reads;
    uint32_t begin_ticks=hw.ticks;
    CHECK(service_visit()==KUI_TOY_SCI_PENDING);
    CHECK(wire.polls-polls<=150u && hw.clock_reads-reads<=152u);
    CHECK(hw.ticks-begin_ticks>=1500u && hw.ticks-begin_ticks<=1520u);
    CHECK(!hw.bytes && wire.flight && service.pending); cleanup();

    /* The timer can stop or wrap. With no receiver progress, no wait and
     * the independent 1024-step cap still guarantee a finite return. */
    setup(true,false); (void)request(45000u,8u,OUT,false); visit(false);
    hw.tick_step=0u; polls=wire.polls; reads=hw.clock_reads; begin_ticks=hw.ticks;
    CHECK(service_visit()==KUI_TOY_SCI_PENDING);
    CHECK(wire.polls-polls==1024u && hw.clock_reads-reads<=1026u);
    CHECK(hw.ticks==begin_ticks && !hw.bytes && wire.flight && service.pending); cleanup();
    setup(true,false); (void)request(45000u,8u,OUT,false); visit(false);
    hw.ticks=UINT32_MAX-100u; hw.tick_step=10u; polls=wire.polls;
    CHECK(service_visit()==KUI_TOY_SCI_PENDING);
    CHECK(wire.polls-polls<=150u && !hw.bytes && wire.flight); cleanup();

    /* A whole foreground entry shares one token allowance. A pending
     * search consumes its 256 bytes once and returns to the game. */
    setup(true,false); uint32_t token=request(45000u,3u,OUT,false);
    wire.token_pending=true; unsigned fetches=wire.fetches,budgets=wire.token_budgets;
    CHECK(service_visit()==KUI_TOY_SCI_PENDING);
    CHECK(wire.fetches==fetches+1u && wire.token_budgets==budgets+1u && wire.token_bytes==256u);
    CHECK(!wire.flight && !hw.bytes && service.pending && !service.error);
    CHECK(service_visit()==KUI_TOY_SCI_PENDING && wire.token_bytes==512u);
    wire.token_pending=false; hw.tick_step=10u; wire.receiver_delay=20u;
    finish_foreground();
    compare(45000u,3u,OUT,false);
    CHECK(gd(KUI_GD_CHECK,token,STATUS)==KUI_GD_COMPLETED); cleanup();
}
static void foreground_quota_credits_background(void) {
    static const unsigned preceding_irq[]={1u,3u,4u,6u};
    for(unsigned c=0;c<sizeof(preceding_irq)/sizeof(*preceding_irq);++c) {
        setup(true,false); (void)request(45000u,8u,OUT,false); visit(false);
        for(unsigned i=0;i<preceding_irq[c];++i) { arrive(); visit(true); }
        uint32_t previous=kui_toy_pilot_sci_snapshot()->call_blocks;
        unsigned polls=wire.foreground_polls;
        hw.tick_step=10u; wire.receiver_delay=20u; arrive();
        CHECK(service_visit()==KUI_TOY_SCI_PENDING);
        uint32_t allowed=preceding_irq[c]>=4u?0u:4u-preceding_irq[c];
        CHECK(kui_toy_pilot_sci_snapshot()->call_blocks-previous==allowed);
        if(!allowed) CHECK(wire.foreground_polls==polls);
        previous=kui_toy_pilot_sci_snapshot()->call_blocks;
        CHECK(service_visit()==KUI_TOY_SCI_PENDING);
        CHECK(kui_toy_pilot_sci_snapshot()->call_blocks-previous==4u);
        finish_foreground();
        compare(45000u,8u,OUT,false); cleanup();
    }
}
static void foreground_audio_and_cancellation(void) {
    /* Even an already exhausted data quota must permit ONE pending-audio
     * boundary check. Suppressed IRQ delivery must not strand the raw audio
     * claimant behind an unconsumed completed data DMA. */
    setup(true,true); uint32_t token=request(45000u,8u,OUT,false); visit(false);
    for(unsigned i=0;i<4u;++i) { arrive(); visit(true); }
    CHECK(kui_toy_pilot_sci_audio_acquire()==KUI_TOY_SCI_PENDING);
    unsigned blocks=kui_toy_pilot_sci_snapshot()->call_blocks,fetches=wire.fetches;
    unsigned foreground_polls=wire.foreground_polls;
    CHECK(service_visit()==KUI_TOY_SCI_PENDING && hw.leased && wire.flight);
    CHECK(kui_toy_pilot_sci_snapshot()->call_blocks==blocks && wire.fetches==fetches);
    arrive(); CHECK(service_visit()==KUI_TOY_SCI_PENDING);
    CHECK(kui_toy_pilot_sci_snapshot()->call_blocks==blocks+1u);
    CHECK(wire.foreground_polls==foreground_polls && wire.fetches==fetches);
    CHECK(!wire.flight && !hw.leased && service.pending);
    unsigned opens=wire.opens,bytes=hw.bytes;
    CHECK(service_visit()==KUI_TOY_SCI_PENDING);
    CHECK(wire.opens==opens && wire.fetches==fetches && hw.bytes==bytes);
    CHECK(kui_toy_pilot_sci_audio_acquire()==KUI_TOY_SCI_OK);
    CHECK(acquire()==KUI_LOADER_SD_OK);
    CHECK(service_visit()==KUI_TOY_SCI_PENDING);
    CHECK(hw.leased && wire.opens==opens && wire.fetches==fetches && hw.bytes==bytes);
    release(); kui_toy_pilot_sci_audio_release();
    hw.tick_step=10u; wire.receiver_delay=20u;
    finish_foreground();
    compare(45000u,8u,OUT,false);
    CHECK(gd(KUI_GD_CHECK,token,STATUS)==KUI_GD_COMPLETED); cleanup();

    /* A retry cannot publish unverified bytes; the next finite service
     * resumes the same checked cursor, with a fresh entry allowance. */
    setup(true,true); token=request(45000u,4u,OUT,false); visit(false);
    hw.tick_step=10u; wire.receiver_delay=20u; arrive(); wire.take_error=KUI_SCI_STREAM_CRC;
    (void)service_visit();
    CHECK(!hw.bytes && ram[OUT-BEGIN]==0xa5u);
    wire.take_error=KUI_SCI_STREAM_OK;
    if(service.pending) {
        finish_foreground();
        compare(45000u,4u,OUT,false);
    } else CHECK(service.error);
    (void)gd(KUI_GD_CHECK,token,STATUS); cleanup();

    /* Revoke the captured request between foreground steps while the
     * independently running DMA becomes ready. The old destination and
     * a substituted destination must both remain untouched. */
    setup(true,false); token=request(45000u,4u,OUT,false); visit(false);
    hw.tick_step=10u; wire.receiver_delay=20u;
    wire.ready_tick=hw.ticks+20u; hw.revoke_tick=hw.ticks+20u;
    CHECK(service_visit()==KUI_TOY_SCI_OK);
    CHECK(!hw.bytes && ram[OUT-BEGIN]==0xa5u && ram[OUT2-BEGIN]==0xa5u);
    CHECK(!hw.leased && !wire.flight);
    hw.frozen=false; (void)gd(KUI_GD_ABORT,token,0u);
    CHECK(gd(KUI_GD_CHECK,token,STATUS)==KUI_GD_FAILED); cleanup();

    setup(true,true); token=request(45000u,8u,OUT,false); visit(false);
    hw.tick_step=10u; wire.receiver_delay=20u;
    CHECK(service_visit()==KUI_TOY_SCI_PENDING && service.completed_bytes==2048u);
    kui_toy_pilot_sci_cancel(&service);
    CHECK(gd(KUI_GD_ABORT,token,0u)==0); hw.frozen=true;
    bytes=hw.bytes; advance_clock(1000u);
    CHECK(service_visit()==KUI_TOY_SCI_OK && hw.bytes==bytes && !wire.flight && !hw.leased);
    hw.frozen=false;
    CHECK(gd(KUI_GD_CHECK,token,STATUS)==KUI_GD_FAILED && get(STATUS+8u)==2048u);
    for(unsigned i=2048u;i<8u*2048u;++i) CHECK(ram[OUT-BEGIN+i]==0xa5u);
    cleanup();

    setup(true,false); token=request(45000u,4u,OUT,false); visit(false);
    wire.poll_error=KUI_SCI_STREAM_BUSY;
    CHECK(service_visit()==KUI_TOY_SCI_FAULT && !hw.bytes && !wire.flight && !hw.leased);
    CHECK(gd(KUI_GD_CHECK,token,STATUS)==KUI_GD_FAILED); cleanup();
}

int main(void) {
    nonblocking_and_complete(); payload_layouts(); token_search_yields(); audio_boundary_handoff();
    cancellation_and_epochs(); revoked_audio_wait_does_not_stall_data();
    destination_failure(); transport_faults();
    foreground_progress_and_limits(); foreground_quota_credits_background();
    foreground_audio_and_cancellation();
    printf("Shared asynchronous SCI: %u checks passed\n",checks);
    return 0;
}

