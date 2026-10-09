/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual shared arbiter + SCI receiver + RAW cursor, against the existing
 * CMD18/CMD12/CRC card model. MMIO reads observe hardware without advancing
 * it. Only the independently elapsed host clock moves DMA, one wire byte at
 * a time, between engine entries. No real sound hardware is modelled here. */
#include "toy_pilot_sci.h"
#include "kui/retail_cursor.h"
#include "kui/retail_image.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Keep the existing register/card model intact. Its original receiver is
 * disabled at read seams; the timed receiver below reuses its actual card
 * framing, byte reversal, physical areas and deferred DMA-write fence. */
#define kui_sci_stream_test_read fixture_read
#define kui_sci_stream_test_write fixture_write
#define kui_sci_stream_test_physical fixture_physical
#define bus_transfer fixture_bus_transfer
#define bus_select fixture_bus_select
#include "sci_stream_model.h"
#undef kui_sci_stream_test_read
#undef kui_sci_stream_test_write
#undef kui_sci_stream_test_physical
#undef bus_transfer
#undef bus_select

#define BEGIN UINT32_C(0x8c010000)
#define END (BEGIN+UINT32_C(0x40000))
#define PARAM (BEGIN+32u)
#define STATUS (BEGIN+64u)
#define OUT (BEGIN+4096u)
#define GAME_VBR UINT32_C(0x8c0f0000)
#define IPRB UINT32_C(0xffd00008)
#define CARD_BLOCKS 8192u
#define MIXED_SECONDS 20u
#define MIXED_AUDIO_END (4u+MIXED_SECONDS*75u+8u)
#define WIRE_BYTE_TICKS 4u
/* Cadence trials use one 1.28-us clock tick per wire byte (6.25 MHz),
 * still slower than SCI's 12.5-MHz raw bus. MMIO never clocks payload. */
static uint32_t wire_byte_ticks=WIRE_BYTE_TICKS,fixture_audio_end=12u;
#define CHECK(test) do { ++checks; assert(test); } while(0)

static unsigned checks,waits,active_stops;
static uint32_t data_completed_tick;
static uint8_t image_card[CARD_BLOCKS*512u],ram[END-BEGIN];
static uint8_t expected[32u*2352u];
static uint8_t raw_output[2352u] __attribute__((aligned(32)));
static uint8_t old_output[2352u] __attribute__((aligned(32)));
static struct kui_loader_sd sd;
static struct kui_retail_manifest manifest;
static struct kui_retail_image reference;
static struct kui_retail_gd service;
static struct {
    uint32_t ticks,next_wire,vbr;
    uint16_t iprb;
    unsigned acquires,releases,reads,programmed_bytes,writes;
    bool leased,in_entry,frozen,in_fence,cancel_allowed;
} hw;

uint32_t kui_toy_pilot_sci_test_vbr(void) { return hw.vbr; }
void kui_toy_pilot_sci_test_set_vbr(uint32_t v) { hw.vbr=v; }
uint16_t kui_toy_pilot_sci_test_read16(uint32_t address) {
    CHECK(address==IPRB);return hw.iprb;
}
void kui_toy_pilot_sci_test_write16(uint32_t address,uint16_t value) {
    CHECK(address==IPRB && (value&0xff0fu)==(hw.iprb&0xff0fu));hw.iprb=value;
}
uint32_t kui_toy_pilot_sci_test_ticks(void) { return hw.ticks++; }
const uint32_t kui_toy_pilot_sci_forward[3]={0x11111111u,0x22222222u,0u};
const uint32_t kui_toy_pilot_sci_interrupt[11]={1u,2u,3u,4u,5u,6u,7u,8u,0xff000028u,10u,11u};
void kui_toy_pilot_sci_release_100(void) {}
void kui_toy_pilot_sci_release_400(void) {}
void kui_toy_pilot_sci_release_600(void) {}
void kui_toy_pilot_sci_rehook(void) {}

static void dma_started(void) {
    CHECK(hw.leased && hw.in_entry && m.rx && (m.chcr&1u));
    hw.next_wire=hw.ticks+wire_byte_ticks;
}
static void elapse(uint32_t amount);
uint32_t kui_sci_stream_test_read(uint32_t address,unsigned width) {
    CHECK(hw.leased);++hw.reads;
    if(hw.in_fence)elapse(1u);
    bool receiving=m.rx;m.rx=false;
    uint32_t value=fixture_read(address,width);
    m.rx=receiving;return value;
}
void kui_sci_stream_test_write(uint32_t address,uint32_t value,unsigned width) {
    CHECK(hw.leased);fixture_write(address,value,width);
}
uint32_t kui_sci_stream_test_physical(const void *area) {
    CHECK(hw.leased && area && !((uintptr_t)area&31u));
    for(unsigned i=0;i<2u;i++) if(!m.areas[i] || m.areas[i]==area) {
        m.areas[i]=(uint8_t *)(uintptr_t)area;return fixture_physical(area);
    }
    CHECK(!"A third receive area was used");return 0u;
}
static uint8_t bus_transfer(void *ctx,uint8_t value,bool slow) {
    CHECK(hw.leased);++hw.programmed_bytes;++hw.ticks;
    return fixture_bus_transfer(ctx,value,slow);
}
static void bus_select(void *ctx,bool selected) {
    CHECK(hw.leased);fixture_bus_select(ctx,selected);
}

/* Each elapsed clock byte takes the exact same wire/DMA route as the shared
 * register model. A partial flight remains visible in TCR and DAR; neither
 * engine polling nor token search can manufacture the missing payload. */
static void wire_byte(void) {
    CHECK((!hw.in_entry || hw.in_fence) && hw.leased && m.rx);
    if(m.stall)return;
    if(m.foreign_during_rx) {m.sar=0x0c200000u;m.foreign_during_rx=false;}
    uint8_t value=rev8(card_clock(0xffu));++m.received;
    unsigned at=m.overrun_every?m.overrun_every:m.overrun_after;
    bool dma=(m.chcr&1u) && m.tcr && m.sar==(RDR&0x1fffffffu) &&
        (m.dmaor&7u)==1u && !(at && 513u-m.tcr==at);
    if(dma) {
        *memory(m.dar++)=value;if(!--m.tcr)m.chcr|=2u;
    } else if(!(m.ssr&RDRF)) {m.rdr=value;m.ssr|=RDRF;}
    else {
        m.ssr|=ORER;m.rx=false;
        if(m.overrun_after) {m.overrun_after=m.overrun_again;m.overrun_again=0u;}
        if(m.late_take && (m.chcr&1u) && m.tcr && m.sar==(RDR&0x1fffffffu)) {
            if(m.in_flight) {m.flight=m.rdr;m.flying=true;m.in_flight=false;}
            else {*memory(m.dar++)=m.rdr;if(!--m.tcr)m.chcr|=2u;}
            m.ssr&=(uint8_t)~RDRF;
        }
    }
}
static void elapse(uint32_t amount) {
    CHECK(!hw.in_entry || hw.in_fence);uint32_t target=hw.ticks+amount;
    while(m.rx && (int32_t)(target-hw.next_wire)>=0) {
        hw.ticks=hw.next_wire;wire_byte();hw.next_wire+=wire_byte_ticks;
    }
    hw.ticks=target;
}

/* Include the unmodified receiver, renaming only counters' interception
 * points and private names that collide with the card fixture above. */
#undef SMR
#undef BRR
#undef SCR
#undef SSR
#undef RDR
#undef SCMR
#undef SPTR
#undef STBCR
#undef PDTR
#undef SAR
#undef DAR
#undef TCR
#undef CHCR
#undef DMAOR
#define command integration_stream_command
#define receive integration_stream_receive
#define module_reset integration_stream_module_reset
#define crc16 integration_stream_crc16
#define kui_sci_stream_wait integration_stream_wait_body
#define kui_sci_stream_stop integration_stream_stop_body
#include "../src/loader/sci_stream.c"
#undef command
#undef receive
#undef module_reset
#undef crc16
#undef kui_sci_stream_wait
#undef kui_sci_stream_stop
enum kui_sci_stream_result kui_sci_stream_wait(void) {
    ++waits;CHECK(hw.cancel_allowed);hw.in_fence=true;
    enum kui_sci_stream_result result=integration_stream_wait_body();hw.in_fence=false;return result;
}
enum kui_sci_stream_result kui_sci_stream_stop(void) {
    bool flight=kui_sci_stream_busy();
    if(flight) {CHECK(hw.cancel_allowed);++active_stops;hw.in_fence=true;}
    enum kui_sci_stream_result result=integration_stream_stop_body();hw.in_fence=false;return result;
}

static enum kui_loader_sd_result acquire(void) {
    CHECK(!hw.leased && hw.in_entry);hw.leased=true;++hw.acquires;
    return KUI_LOADER_SD_OK;
}
static void release(void) {
    CHECK(hw.leased && hw.in_entry && !m.rx && !(m.chcr&1u) && !m.flying);
    CHECK(!card.streaming && !card.start_after_queue && m.cs_high);
    CHECK(m.sar==0x11111111u && m.dar==0x22222222u && m.tcr==0x33u && !m.chcr);
    hw.leased=false;++hw.releases;
}
static uint8_t content(uint32_t lba,unsigned offset) {
    CHECK(lba<CARD_BLOCKS && offset<512u);return image_card[lba*512u+offset];
}
static uint8_t source(unsigned track,uint32_t at) {
    uint32_t inside=at%2352u,sector=at/2352u;
    if(track!=1u && inside<16u) {
        if(!inside || inside==11u)return 0u;
        if(inside<11u)return 255u;
        return inside==15u?1u:0u;
    }
    return (uint8_t)(track*91u+sector*53u+inside*11u+(inside>>8));
}
static int read_block(void *ctx,uint32_t lba,uint8_t output[512]) {
    (void)ctx;CHECK(lba<CARD_BLOCKS);memcpy(output,image_card+lba*512u,512u);return 0;
}
static uint8_t *map(void *ctx,uint32_t address,uint32_t bytes,int writing) {
    (void)ctx;
    if(address<BEGIN || address>=END || bytes>END-address)return NULL;
    if(writing==KUI_RETAIL_MAP_VALIDATE)return ram+sizeof(ram);
    if(writing==1 && address>=OUT) {CHECK(!hw.frozen);hw.writes+=bytes;}
    return ram+address-BEGIN;
}
static int check(void *ctx,uint32_t lba,uint32_t count,uint32_t bytes) {
    (void)ctx;return kui_retail_image_check_validated(&manifest,lba,count,
        bytes==2352u?KUI_GAME_SECTOR_RAW:KUI_GAME_SECTOR_MODE1)==KUI_GAME_OK?0:-1;
}
static int forbidden_read(void *ctx,uint32_t lba,uint32_t count,uint32_t bytes,void *out) {
    (void)ctx;(void)lba;(void)count;(void)bytes;(void)out;
    CHECK(!"Synchronous GD read invoked");return -1;
}
static void fixture(bool scattered,uint32_t offset) {
    memset(&manifest,0,sizeof(manifest));memset(image_card,0xf3,sizeof(image_card));
    manifest.card_sectors=CARD_BLOCKS;manifest.partition_start=50u;
    manifest.partition_end=fixture_audio_end>164u?8000u:2000u;
    manifest.track_count=3u;manifest.session_lba=45000u;manifest.boot_lba=45001u;manifest.boot_bytes=4567u;
    strcpy(manifest.title,"Real shared RAW SCI test");strcpy(manifest.bootfile,"1ST_READ.BIN");
    const uint32_t starts[]={0u,4u,45000u},ends[]={4u,fixture_audio_end,45032u};
    uint32_t used=0u;
    for(unsigned track=0;track<3u;track++) {
        struct kui_retail_track *t=&manifest.slots[track].track;
        uint32_t first_offset=track==1u?offset:0u;
        *t=(struct kui_retail_track){.start_lba=starts[track],.end_lba=ends[track],
            .control=(uint8_t)((track==1u?0u:4u)|(first_offset&256u?KUI_RETAIL_TRACK_OFFSET_HIGH:0u)),
            .first_extent=(uint16_t)((3u+manifest.extent_count)|(first_offset&255u)<<8)};
        uint32_t bytes=(t->end_lba-t->start_lba)*2352u,blocks=(first_offset+bytes+511u)/512u;
        for(uint32_t first=0;first<blocks;) {
            uint32_t extent_size=fixture_audio_end>164u?256u:7u;
            uint32_t take=blocks-first>extent_size?extent_size:blocks-first,index=manifest.extent_count;
            uint32_t separation=fixture_audio_end>164u?4000u:900u;
            uint32_t distance=fixture_audio_end>164u?256u:8u;
            uint32_t physical=scattered?100u+(index&1u?separation:0u)+(index>>1)*distance:100u+used;
            CHECK(3u+index<KUI_RETAIL_IMAGE_SLOTS && physical+take<manifest.partition_end);
            manifest.slots[3u+manifest.extent_count++].extent=(struct kui_retail_extent){first,physical,take};
            ++t->extent_count;
            for(uint32_t p=0;p<take*512u;p++) {
                uint32_t at=first*512u+p;
                if(at>=first_offset && at-first_offset<bytes)image_card[physical*512u+p]=source(track,at-first_offset);
            }
            first+=take;used+=take;
        }
    }
    CHECK(kui_retail_manifest_validate(&manifest)==KUI_GAME_OK);
    CHECK(kui_retail_image_init(&reference,&manifest,read_block,NULL)==KUI_GAME_OK);
}
static void setup(bool scattered,uint32_t offset) {
    fixture_audio_end=12u;wire_byte_ticks=WIRE_BYTE_TICKS;
    fixture(scattered,offset);memset(&hw,0,sizeof(hw));memset(&card,0,sizeof(card));memset(&m,0,sizeof(m));
    /* Each fixture is a fresh resident boot; SCI BSS/statistics are normally
     * cleared by _start rather than by open(), which preserves a live run. */
    memset(&s,0,sizeof(s));
    hw.vbr=GAME_VBR;hw.iprb=0x5a0fu;waits=active_stops=0u;
    card.nac=1u;card.nac_first=30u;card.busy=4u;card.blocks=CARD_BLOCKS;card.high_capacity=true;
    card_content=content;
    m.smr=0x80u;m.scr=0x30u;m.ssr=0x84u;m.cs_high=true;m.healthy=true;m.late_take=true;
    m.sar=0x11111111u;m.dar=0x22222222u;m.tcr=0x33u;m.dmaor=0x8201u;m.on_dma_start=dma_started;
    memset(&sd,0,sizeof(sd));sd.ready=true;sd.high_capacity=true;sd.blocks=CARD_BLOCKS;
    sd.bus.select=bus_select;sd.bus.transfer=bus_transfer;
    const struct kui_gd_ops ops={NULL,map,check,forbidden_read};
    CHECK(!kui_retail_gd_init(&service,manifest.slots,manifest.track_count,&ops,BEGIN,END));
    memset(ram,0xa5,sizeof(ram));memset(raw_output,0xa5,sizeof(raw_output));memset(old_output,0xa5,sizeof(old_output));
    kui_toy_pilot_sci_init(&manifest,&sd,acquire,release);
}
static int audio(uint32_t lba,uint32_t generation,uint8_t *output) {
    CHECK(!hw.in_entry);hw.in_entry=true;
    int result=kui_toy_pilot_sci_audio_read(lba,generation,output);hw.in_entry=false;return result;
}
static int pump(bool foreground) {
    CHECK(!hw.in_entry);hw.in_entry=true;
    int result=foreground?kui_toy_pilot_sci_service(&service):kui_toy_pilot_sci_pump(&service);
    hw.in_entry=false;
    if(!service.pending && service.completed_bytes && !data_completed_tick)data_completed_tick=hw.ticks;
    return result;
}
static void irq(void) {
    CHECK(!hw.in_entry);hw.in_entry=true;(void)kui_toy_pilot_sci_irq();hw.in_entry=false;
    if(!service.pending && service.completed_bytes && !data_completed_tick)data_completed_tick=hw.ticks;
}
static void cancel(bool all) {
    CHECK(!hw.in_entry);hw.in_entry=true;hw.cancel_allowed=true;
    if(all)kui_toy_pilot_sci_cancel(NULL);else kui_toy_pilot_sci_audio_cancel();
    hw.in_entry=hw.cancel_allowed=false;
}
static int plan_audio(uint32_t lba,uint32_t end,uint32_t generation,uint32_t reserve) {
    CHECK(!hw.in_entry);hw.in_entry=true;
    int result=kui_toy_pilot_sci_audio_plan(lba,end,generation,reserve);
    hw.in_entry=false;return result;
}
static void pristine(const uint8_t *output) {
    for(unsigned i=0;i<2352u;i++)CHECK(output[i]==0xa5u);
}
static void expected_raw(uint32_t lba,uint8_t *output) {
    CHECK(kui_retail_image_read(&reference,lba,1u,KUI_GAME_SECTOR_RAW,output,2352u)==KUI_GAME_OK);
}
static uint32_t physical_block(uint32_t lba) {
    struct kui_retail_cursor cursor;
    CHECK(kui_retail_cursor_begin(&cursor,&manifest,lba,1u,KUI_GAME_SECTOR_RAW,NULL,NULL)==KUI_GAME_OK);
    return cursor.block;
}
static void cleanup(void) {
    cancel(true);CHECK(!hw.leased && !m.rx && hw.acquires==hw.releases);
    CHECK(hw.vbr==GAME_VBR && hw.iprb==0x5a0fu);
}
static void service_audio_only(void) {
    CHECK(!hw.in_entry);hw.in_entry=true;(void)kui_toy_pilot_sci_service(NULL);hw.in_entry=false;
}
static unsigned raw_finish(uint32_t lba,uint32_t generation,uint8_t *output) {
    unsigned steps=0u;
    int result=KUI_TOY_SCI_PENDING;
    while(result==KUI_TOY_SCI_PENDING && steps++<100u) {
        elapse(2200u);irq();pristine(output);
        result=audio(lba,generation,output);
        if(result==KUI_TOY_SCI_PENDING)pristine(output);
    }
    CHECK(result==KUI_TOY_SCI_OK && steps<=100u);return steps;
}
static void raw_geometry(bool scattered,uint32_t offset,uint32_t lba) {
    setup(scattered,offset);expected_raw(lba,expected);
    uint32_t file_offset=offset+(lba-4u)*2352u;
    unsigned physical_blocks=(file_offset%512u+2352u+511u)/512u;
    CHECK(physical_blocks==5u || physical_blocks==6u);
    CHECK(audio(lba,11u,raw_output)==KUI_TOY_SCI_PENDING && m.rx && hw.leased);
    pristine(raw_output);
    uint32_t before=m.tcr;
    elapse(40u);CHECK(m.rx && m.tcr<before && m.tcr>0u);before=m.tcr;
    CHECK(audio(lba,11u,raw_output)==KUI_TOY_SCI_PENDING && m.tcr==before);
    pristine(raw_output);service_audio_only();CHECK(m.tcr==before);pristine(raw_output);
    raw_finish(lba,11u,raw_output);
    CHECK(!memcmp(raw_output,expected,2352u));
    CHECK(m.dma_starts==physical_blocks && kui_sci_stream_stats()->blocks==physical_blocks);
    CHECK(m.areas[0] && m.areas[1] && m.areas[0]!=m.areas[1]);
    CHECK(!waits && !active_stops && !kui_sci_stream_stats()->polled && !kui_sci_stream_stats()->crc_errors);
    CHECK(!hw.leased && hw.acquires==1u && hw.releases==1u && card.cmd18);
    if(scattered && (file_offset/512u)/7u!=(file_offset/512u+physical_blocks-1u)/7u)
        CHECK(card.cmd18>=2u && card.cmd12>=2u);
    /* READY is delivered once to its stable output. A repeated completed
     * tuple neither rereads the card nor overwrites a reused worker buffer. */
    unsigned starts=m.dma_starts,clocks=card.clocks;
    memset(raw_output,0xcc,sizeof(raw_output));CHECK(audio(lba,11u,raw_output)==KUI_TOY_SCI_OK);
    CHECK(m.dma_starts==starts && card.clocks==clocks);
    for(unsigned i=0;i<2352u;i++)CHECK(raw_output[i]==0xccu);
    cleanup();
}
static void cancel_and_replace_partial_raw(void) {
    setup(true,511u);CHECK(audio(5u,21u,old_output)==KUI_TOY_SCI_PENDING);
    elapse(120u);CHECK(m.rx && m.tcr>0u && m.tcr<513u);pristine(old_output);
    cancel(false);CHECK(!hw.leased && !m.rx && active_stops==1u);pristine(old_output);
    for(unsigned n=0;n<3u;n++) {elapse(1000u);irq();service_audio_only();pristine(old_output);}
    expected_raw(6u,expected);CHECK(audio(6u,22u,raw_output)==KUI_TOY_SCI_PENDING);
    raw_finish(6u,22u,raw_output);CHECK(!memcmp(raw_output,expected,2352u));pristine(old_output);
    cleanup();

    setup(true,511u);CHECK(audio(5u,31u,old_output)==KUI_TOY_SCI_PENDING);elapse(120u);
    hw.cancel_allowed=true;CHECK(audio(6u,32u,raw_output)==KUI_TOY_SCI_PENDING);hw.cancel_allowed=false;
    CHECK(active_stops==1u);pristine(old_output);pristine(raw_output);expected_raw(6u,expected);
    raw_finish(6u,32u,raw_output);CHECK(!memcmp(raw_output,expected,2352u));pristine(old_output);
    cleanup();
}
static void crc_failure_is_private(void) {
    setup(true,511u);uint32_t lba=5u;
    card.corrupt_lba=physical_block(lba);card.corrupt_count=1u;
    CHECK(audio(lba,41u,raw_output)==KUI_TOY_SCI_PENDING);elapse(2200u);irq();
    CHECK(kui_sci_stream_stats()->crc_errors==1u);pristine(raw_output);
    expected_raw(lba,expected);raw_finish(lba,41u,raw_output);
    CHECK(!memcmp(raw_output,expected,2352u) && kui_toy_pilot_sci_snapshot()->retries>=1u);
    CHECK(!waits && !active_stops);cleanup();

    setup(true,511u);card.corrupt_lba=physical_block(lba);card.corrupt_count=100u;
    CHECK(audio(lba,51u,raw_output)==KUI_TOY_SCI_PENDING);
    int result=KUI_TOY_SCI_PENDING;
    for(unsigned n=0;n<40u && result==KUI_TOY_SCI_PENDING;n++) {
        elapse(2200u);irq();pristine(raw_output);result=audio(lba,51u,raw_output);pristine(raw_output);
    }
    CHECK(result==KUI_TOY_SCI_FAULT && !hw.leased && kui_sci_stream_stats()->crc_errors>1u);
    unsigned starts=m.dma_starts,clocks=card.clocks;
    CHECK(audio(lba,51u,raw_output)==KUI_TOY_SCI_FAULT && m.dma_starts==starts && card.clocks==clocks);
    pristine(raw_output);cleanup();
}
static void put(uint32_t address,uint32_t value) {
    for(unsigned i=0;i<4u;i++)ram[address-BEGIN+i]=(uint8_t)(value>>(8u*i));
}
static uint32_t data_request(uint32_t lba,uint32_t sectors) {
    data_completed_tick=0u;
    put(PARAM,lba+150u);put(PARAM+4u,sectors);put(PARAM+8u,OUT);put(PARAM+12u,0u);
    int32_t token=kui_retail_gd_dispatch(&service,KUI_GD_DMAREAD,PARAM,0u,KUI_GD_REQUEST);
    CHECK(token>0 && service.pending);return (uint32_t)token;
}
static void data_audio_contention(bool abort_data) {
    setup(true,511u);uint32_t token=data_request(45000u,4u);
    CHECK(pump(false)==KUI_TOY_SCI_PENDING && m.rx);elapse(40u);
    CHECK(audio(5u,61u,raw_output)==KUI_TOY_SCI_PENDING);pristine(raw_output);
    /* A checked data boundary arms RAW under one receiver at a time. */
    elapse(2200u);irq();CHECK(service.pending && m.rx && kui_toy_pilot_sci_snapshot()->card_owned==2u);
    if(abort_data) {
        uint32_t count=m.tcr;unsigned stops=active_stops;
        hw.in_entry=true;kui_toy_pilot_sci_cancel(&service);hw.in_entry=false;
        CHECK(m.rx && m.tcr==count && active_stops==stops && hw.leased);
        CHECK(!kui_retail_gd_dispatch(&service,token,0u,0u,KUI_GD_ABORT));
        hw.frozen=true;
    }
    expected_raw(5u,expected);raw_finish(5u,61u,raw_output);
    CHECK(!memcmp(raw_output,expected,2352u));
    CHECK(kui_toy_pilot_sci_snapshot()->audio_releases==1u && !waits && !active_stops);
    if(abort_data) {
        CHECK(kui_retail_gd_dispatch(&service,token,STATUS,0u,KUI_GD_CHECK)==KUI_GD_FAILED);
        CHECK(service.error==KUI_GD_ERROR_CANCELLED || !service.command);hw.frozen=false;
    } else {
        for(unsigned n=0;n<100u && service.pending;n++) {elapse(2200u);irq();(void)pump(false);}
        CHECK(!service.pending && !service.error && service.completed_bytes==4u*2048u);
        CHECK(kui_retail_image_read(&reference,45000u,4u,KUI_GAME_SECTOR_MODE1,expected,sizeof(expected))==KUI_GAME_OK);
        CHECK(!memcmp(ram+OUT-BEGIN,expected,4u*2048u));
        CHECK(kui_retail_gd_dispatch(&service,token,STATUS,0u,KUI_GD_CHECK)==KUI_GD_COMPLETED);
    }
    cleanup();
}
static void advance_transport_to(uint32_t deadline) {
    /* Hardware owns payload arrival; IRQ processing arms successors.
     * GD service also runs at bounded 1-ms intervals when a token search or
     * IRQ-deferred repair has no active receiver to generate an interrupt.
     * Neither producer path consumes a worker output sector. */
    while((int32_t)(deadline-hw.ticks)>0) {
        uint32_t remaining=deadline-hw.ticks;
        if(m.rx) {
            /* SCI's event is the tail overrun, after DMA's 513 bytes
             * plus the retained low CRC byte. DMA completion alone is not
             * this interrupt; invoking it early manufactures tail faults. */
            uint32_t step=(m.tcr+2u)*wire_byte_ticks;
            if(step>remaining)step=remaining;
            elapse(step);
            if(!m.rx)irq();
        } else {
            uint32_t step=remaining>781u?781u:remaining;
            elapse(step);
            CHECK(pump(true)!=KUI_TOY_SCI_FAULT);
        }
        pristine(raw_output);
    }
}

static void queued_real_wire_range_and_cancel(void) {
    for(unsigned full=0;full<2u;++full) {
        setup(true,511u);
        CHECK(plan_audio(4u,12u,71u,0u)!=KUI_TOY_SCI_FAULT);
        if(full) {
            advance_transport_to(hw.ticks+100000u);
            CHECK(!hw.leased && !m.rx);
            unsigned reads=m.dma_starts;service_audio_only();
            CHECK(!hw.leased && !m.rx && m.dma_starts==reads);
        } else {elapse(120u);CHECK(m.rx && m.tcr>0u && m.tcr<513u);}
        cancel(false);pristine(raw_output);
        CHECK(!hw.leased && !m.rx);
        CHECK(plan_audio(8u,12u,72u,0u)!=KUI_TOY_SCI_FAULT);
        advance_transport_to(hw.ticks+100000u);
        if(hw.leased || m.rx)fprintf(stderr,"Queued wire range: full=%u leased=%u rx=%u tcr=%u ticks=%u starts=%u crc=%u ssr=%x busy=%u\n",
            full,hw.leased,m.rx,m.tcr,hw.ticks,m.dma_starts,kui_sci_stream_stats()->crc_errors,m.ssr,kui_sci_stream_busy());
        if(hw.leased)fprintf(stderr,"engine callblk=%u irqblk=%u retries=%u err=%u polled=%u claim=%u release=%u\n",
            kui_toy_pilot_sci_snapshot()->call_blocks,kui_toy_pilot_sci_snapshot()->irq_blocks,
            kui_toy_pilot_sci_snapshot()->retries,kui_toy_pilot_sci_snapshot()->errors,
            kui_toy_pilot_sci_snapshot()->polled_blocks,kui_toy_pilot_sci_snapshot()->audio_claims,
            kui_toy_pilot_sci_snapshot()->audio_releases);
        CHECK(!hw.leased && !m.rx);
        for(uint32_t lba=8u;lba<12u;++lba) {
            expected_raw(lba,expected);
            CHECK(audio(lba,72u,raw_output)==KUI_TOY_SCI_OK);
            CHECK(!memcmp(raw_output,expected,2352u));memset(raw_output,0xa5,sizeof(raw_output));
        }
        unsigned starts=m.dma_starts;advance_transport_to(hw.ticks+100000u);
        CHECK(m.dma_starts==starts && !hw.leased && !m.rx);
        CHECK(audio(12u,72u,raw_output)==KUI_TOY_SCI_FAULT);pristine(raw_output);cleanup();
    }
}
static void queued_real_wire_crc_authority(void) {
    setup(true,511u);card.corrupt_lba=physical_block(4u);card.corrupt_count=1u;
    CHECK(plan_audio(4u,8u,75u,0u)!=KUI_TOY_SCI_FAULT);
    advance_transport_to(hw.ticks+100000u);
    CHECK(!hw.leased && !m.rx && kui_sci_stream_stats()->crc_errors==1u);
    pristine(raw_output);
    for(uint32_t lba=4u;lba<8u;++lba) {
        expected_raw(lba,expected);CHECK(audio(lba,75u,raw_output)==KUI_TOY_SCI_OK);
        CHECK(!memcmp(raw_output,expected,2352u));memset(raw_output,0xa5,sizeof(raw_output));
    }
    CHECK(!waits && !active_stops);cleanup();

    setup(true,511u);card.corrupt_lba=physical_block(4u);card.corrupt_count=100u;
    CHECK(plan_audio(4u,8u,76u,0u)!=KUI_TOY_SCI_FAULT);
    for(unsigned n=0;n<100u && hw.leased;++n) {
        elapse(2200u);irq();service_audio_only();pristine(raw_output);
    }
    CHECK(!hw.leased && !m.rx && kui_sci_stream_stats()->crc_errors>1u);
    CHECK(audio(4u,76u,raw_output)==KUI_TOY_SCI_FAULT);pristine(raw_output);
    unsigned reads=m.dma_starts;service_audio_only();irq();
    CHECK(m.dma_starts==reads && !hw.leased);cleanup();
}
static void queued_mixed_real_wire_cadence(bool gap) {
    setup(true,511u);fixture_audio_end=MIXED_AUDIO_END;fixture(true,511u);
    kui_toy_pilot_sci_init(&manifest,&sd,acquire,release);wire_byte_ticks=1u;
    uint32_t lba=4u,generation=81u,origin=hw.ticks;
    const int32_t target_reserve=gap?32768:8192;
    int32_t reserve=target_reserve,min_reserve=reserve;
    int32_t first_second_min=reserve,last_second_min=reserve;uint32_t accounted=0u;
    unsigned deliveries=0u,data_runs=0u,max_burst=0u;
    uint32_t token=0u,data_started=0u,max_data_ticks=0u;bool catchup=false,gap_resumed=false;
    const uint32_t gap_start=29u*781250u/60u+78u,gap_end=gap_start+62500u;
    CHECK(plan_audio(lba,MIXED_AUDIO_END,generation,(uint32_t)reserve)!=KUI_TOY_SCI_FAULT);
    for(unsigned frame=0;frame<MIXED_SECONDS*60u;++frame) {
        uint32_t time=(uint32_t)((uint64_t)frame*781250u/60u);
        /* Keep transport/IRQ progress running during a single absent worker
         * interval; safe SOUND publication remains the worker's job. */
        if(gap && time>gap_start && time<gap_end) {
            advance_transport_to(origin+time);continue;
        }
        if(gap && time>=gap_end && !gap_resumed) {time=gap_end;gap_resumed=true;}
        for(unsigned cluster=0;cluster<2u;++cluster) {
            uint32_t deadline=origin+time+cluster*78u;
            advance_transport_to(deadline);
            uint32_t elapsed=hw.ticks-origin;
            uint32_t played=(uint32_t)((uint64_t)elapsed*44100u/781250u);
            reserve-=(int32_t)(played-accounted);accounted=played;
            if(reserve<min_reserve)min_reserve=reserve;
            CHECK(reserve>=target_reserve-(gap?10:6)*588);
            if(elapsed<781250u && reserve<first_second_min)first_second_min=reserve;
            if(elapsed>=(MIXED_SECONDS-1u)*781250u && reserve<last_second_min)last_second_min=reserve;
            CHECK(plan_audio(lba,MIXED_AUDIO_END,generation,(uint32_t)reserve)!=KUI_TOY_SCI_FAULT);
            unsigned burst=0u;
            while(burst<4u && reserve<=target_reserve-588) {
                int result=audio(lba,generation,raw_output);CHECK(result!=KUI_TOY_SCI_FAULT);
                if(result==KUI_TOY_SCI_PENDING) {pristine(raw_output);break;}
                expected_raw(lba,expected);CHECK(!memcmp(raw_output,expected,2352u));
                memset(raw_output,0xa5,sizeof(raw_output));++lba;++deliveries;++burst;reserve+=588;
            }
            if(burst>max_burst)max_burst=burst;
            if(gap && time>=gap_end && burst>1u)catchup=true;
            CHECK(plan_audio(lba,MIXED_AUDIO_END,generation,(uint32_t)reserve)!=KUI_TOY_SCI_FAULT);
            if(token && !service.pending) {
                CHECK(!service.error && service.completed_bytes==8u*2048u);
                CHECK(data_completed_tick);
                /* Measure transport completion, independent of the delayed
                 * worker's later CHECK acknowledgement after a gap. */
                uint32_t data_ticks=data_completed_tick-data_started;
                if(data_ticks>max_data_ticks)max_data_ticks=data_ticks;
                if(data_ticks>78125u)fprintf(stderr,"Mixed DATA latency: gap=%u frame=%u ticks=%u reserve=%d runs=%u delivered=%u crc=%u retry=%u\n",
                    gap,frame,data_ticks,reserve,data_runs,deliveries,kui_sci_stream_stats()->crc_errors,
                    kui_toy_pilot_sci_snapshot()->retries);
                CHECK(data_ticks<=78125u); /* Every 16-KiB request completes within 100 ms. */
                CHECK(kui_retail_image_read(&reference,45000u,8u,KUI_GAME_SECTOR_MODE1,
                    expected,sizeof(expected))==KUI_GAME_OK);
                CHECK(!memcmp(ram+OUT-BEGIN,expected,8u*2048u));
                CHECK(kui_retail_gd_dispatch(&service,token,STATUS,0u,KUI_GD_CHECK)==KUI_GD_COMPLETED);
                token=0u;++data_runs;
            }
            if(!token) {token=data_request(45000u,8u);data_started=hw.ticks;}
            unsigned blocks=kui_sci_stream_stats()->blocks;
            uint32_t began=hw.ticks;
            CHECK(pump(true)!=KUI_TOY_SCI_FAULT);
            CHECK(kui_sci_stream_stats()->blocks-blocks<=KUI_TOY_SCI_SERVICE_BLOCKS);
            /* Time checks happen between steps: a command/CRC repair step
             * can exceed allowance, but no payload is synchronously waited. */
            CHECK(hw.ticks-began<=KUI_TOY_SCI_SERVICE_TICKS+1024u);
            pristine(raw_output);
        }
    }
    advance_transport_to(origin+MIXED_SECONDS*781250u);
    reserve-=(int32_t)(MIXED_SECONDS*44100u-accounted);
    CHECK(plan_audio(lba,MIXED_AUDIO_END,generation,(uint32_t)reserve)!=KUI_TOY_SCI_FAULT);
    for(unsigned n=0;n<4u && reserve<=target_reserve-588;++n) {
        int result=audio(lba,generation,raw_output);CHECK(result!=KUI_TOY_SCI_FAULT);
        if(result==KUI_TOY_SCI_PENDING) {pristine(raw_output);break;}
        expected_raw(lba,expected);CHECK(!memcmp(raw_output,expected,2352u));
        memset(raw_output,0xa5,sizeof(raw_output));++lba;++deliveries;reserve+=588;
    }
    if(deliveries<MIXED_SECONDS*75u-2u || reserve<target_reserve-2*588)
        fprintf(stderr,"Long mixed endpoint: gap=%u delivered=%u reserve=%d min=%d target=%d runs=%u ticks=%u pending=%u\n",
            gap,deliveries,reserve,min_reserve,target_reserve,data_runs,hw.ticks,service.pending);
    CHECK(deliveries>=MIXED_SECONDS*75u-2u && reserve>=target_reserve-2*588 && min_reserve>0);
    /* Compare the same steady cadence bands: initial reserve cannot hide a
     * persistent producer deficit over twenty seconds of continuous DATA. */
    CHECK(last_second_min>=first_second_min-588);
    CHECK(data_runs>=8u && (!gap || catchup) && max_burst>=2u);
    /* The final token was admitted inside the scored window. Finish
     * and verify it after scoring; no cancelled tail is counted as DATA. */
    unsigned scored_runs=data_runs;
    for(unsigned n=0;n<100u && service.pending;++n) {advance_transport_to(hw.ticks+781u);(void)pump(true);}
    CHECK(token && !service.pending && !service.error && service.completed_bytes==8u*2048u);
    CHECK(data_completed_tick && data_completed_tick-data_started<=78125u);
    CHECK(kui_retail_image_read(&reference,45000u,8u,KUI_GAME_SECTOR_MODE1,
        expected,sizeof(expected))==KUI_GAME_OK);
    CHECK(!memcmp(ram+OUT-BEGIN,expected,8u*2048u));
    CHECK(kui_retail_gd_dispatch(&service,token,STATUS,0u,KUI_GD_CHECK)==KUI_GD_COMPLETED);
    CHECK(!waits && !active_stops && !kui_sci_stream_stats()->crc_errors);
    printf("Queued SCI mixed DATA/RAW: %u RAW sectors/%u s + %u KiB MODE1 data, min/final reserve %d/%d of %d frames, max worker burst %u, max DATA latency %u ticks, first/last-second minima %d/%d%s\n",
        deliveries,MIXED_SECONDS,scored_runs*16u,min_reserve,reserve,target_reserve,max_burst,max_data_ticks,
        first_second_min,last_second_min,
        gap?" with bounded 80-ms gap":"");
    cleanup();
}

int main(void) {
    raw_geometry(false,0u,4u);raw_geometry(false,0u,5u);
    raw_geometry(true,511u,4u);raw_geometry(true,511u,5u);
    cancel_and_replace_partial_raw();crc_failure_is_private();
    data_audio_contention(false);data_audio_contention(true);
    queued_real_wire_range_and_cancel();queued_real_wire_crc_authority();
    queued_mixed_real_wire_cadence(false);queued_mixed_real_wire_cadence(true);
    printf("Shared asynchronous CDDA real-wire integration: %u checks passed\n",checks);
    return 0;
}

