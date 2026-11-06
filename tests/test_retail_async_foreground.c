/* SPDX-License-Identifier: GPL-3.0-only */
/* Explicit foreground control against the production GD/cursor/SCI reader.
 * The card, peripheral registers and bus acquire/release calls are modeled;
 * this does not execute SH exceptions or establish hardware timing. */
#include <assert.h>
#include <stdint.h>
#include "kui/native_standalone_reader.h"
#include "kui/native_sdk_sci_runtime.h"

#if !KUI_NATIVE_STANDALONE_FOREGROUND || !KUI_NATIVE_STANDALONE_READER || \
    !KUI_NATIVE_STANDALONE_STRUCTURAL || !KUI_NATIVE_SDK_SCI_ROUTE
#error "The test requires the explicit structural foreground control"
#endif

static unsigned publications,callback_reads,bus_releases,verified,cancels;
static unsigned legacy_streams,legacy_pieces,legacy_cancels,legacy_errors;
static unsigned legacy_busy_polls,legacy_idle_polls;
static unsigned dma_streams,dma_pieces,dma_partials,dma_errors,dma_cancels;
static unsigned dma_events21,dma_events20;
static unsigned pio_streams,pio_pieces,pio_partials,pio_errors,pio_cancels;
static unsigned pio_events20,pio_registered,pio_cleared,pio_virtual_reads;
static unsigned vram_cases,vram_bytes,vram_odd_fragments,vram_crc_cases,vram_aborts,vram_bus_refusals;
static unsigned vram_same_ram_writes;
static unsigned vram_virtual_reads,vram_refusals;
#if KUI_VC2_VMU_LATE
static unsigned late_card_reads,late_card_refusals,late_card_errors,late_card_pio_reads;
#endif
static uint32_t foreground_sr=UINT32_C(0x600000f0);
volatile uint32_t kui_retail_hook_active,kui_retail_hook_fault;
uint32_t kui_native_standalone_test_sr(void) {return foreground_sr;}
void kui_native_standalone_test_publish(uint32_t address) {
    (void)address;++publications;assert(!"Foreground must not publish SDK callbacks");
}
void kui_retail_sdk_sci_entry(void) {}
uint32_t kui_retail_async_test_read32(uint32_t address) {
    (void)address;++callback_reads;assert(!"Foreground must not inspect callback slots");return 0;
}
#define kui_sci_sd_acquire fixture_sci_sd_acquire
#define main unused_original_async_foreground_fixture
#include "test_retail_async.c"
#undef main
#undef kui_sci_sd_acquire

/* The real bus helper borrows the current owner's SCI configuration. Model
 * that boundary explicitly, including a different owner between requests. */
static struct {uint8_t smr,brr,scr,sptr;} saved_owner;
static bool bus_owned;
static bool inject_stop_failure;
static unsigned stop_failures;
static uint32_t vectors100[8],vectors400[8],vectors600[16];
enum kui_sci_stream_result __real_kui_sci_stream_stop(void);
enum kui_sci_stream_result __wrap_kui_sci_stream_stop(void) {
    if(inject_stop_failure && !R.shared.service.pending && R.engine.opened) {
        inject_stop_failure=false;++stop_failures;
        return KUI_SCI_STREAM_COMMAND;
    }
    return __real_kui_sci_stream_stop();
}
enum kui_loader_sd_result kui_sci_sd_acquire(void) {
    CHECK(!bus_owned && !R.shared.port.acquired);
    enum kui_loader_sd_result r=fixture_sci_sd_acquire();
    if(r==KUI_LOADER_SD_OK) {
        saved_owner.smr=m.smr;saved_owner.brr=m.brr;
        saved_owner.scr=m.scr;saved_owner.sptr=m.sptr;
        m.smr=0x80u;m.brr=0u;m.scr=0x30u;m.sptr=0u;
        bus_owned=true;R.shared.port.acquired=true;
    }
    return r;
}
void kui_sci_sd_release(void) {
    CHECK(bus_owned && R.shared.port.acquired);
    CHECK(!kui_sci_stream_busy() && !R.shared.stream.state && m.cs_high);
    CHECK(!R.shared.stream.ready[0] && !R.shared.stream.ready[1] &&
        !R.shared.stream.kept && !R.shared.stream.arrived);
    CHECK(!card.streaming && !card.start_after_queue);
    CHECK(!R.engine.hooked && !R.engine.release.armed);
    m.smr=saved_owner.smr;m.brr=saved_owner.brr;
    m.scr=saved_owner.scr;m.sptr=saved_owner.sptr;
    R.shared.port.acquired=false;bus_owned=false;++bus_releases;
}
static void vectors_unchanged(void) {
#if KUI_NATIVE_GD_MMU_BUFFERS
    const struct kui_retail_async_stats zero_stats={0};
    assert(!memcmp(&R.engine.stats,&zero_stats,sizeof(zero_stats)));
#endif
    CHECK(!memcmp(vectors100,R.vector100,sizeof(vectors100)));
    CHECK(!memcmp(vectors400,R.vector400,sizeof(vectors400)));
    CHECK(!memcmp(vectors600,R.vector600,sizeof(vectors600)));
    CHECK(!publications && !callback_reads && !hw.vbr_sets);
}
static void quiescent(uint32_t owner,uint16_t iprb) {
    CHECK(!R.engine.active && !R.engine.opened && !R.engine.hooked);
    CHECK(!R.engine.release.armed && !R.engine.release.depth);
    CHECK(!R.shared.port.acquired && !bus_owned);
    CHECK(!kui_sci_stream_busy() && !R.shared.stream.state && m.cs_high);
    CHECK(!R.shared.stream.ready[0] && !R.shared.stream.ready[1] &&
        !R.shared.stream.kept && !R.shared.stream.arrived);
    CHECK(!card.streaming && !card.start_after_queue);
    CHECK(hw.vbr==owner && hw.iprb==iprb);
    CHECK(m.smr==saved_owner.smr && m.brr==saved_owner.brr &&
        m.scr==saved_owner.scr && m.sptr==saved_owner.sptr);
    vectors_unchanged();
}
static void setup_foreground(bool cooked,bool scattered) {
    CHECK(!bus_owned);
    cooked_fixture=cooked;setup(GAME_VBR,X,48u,scattered);
    manifest.reader=KUI_RETAIL_READER_STANDARD;kui_retail_async_init(&manifest);
    kui_retail_hook_active=1u;kui_retail_hook_fault=0u;
    publications=callback_reads=bus_releases=0u;
    /* A stale but superficially ready callback record is deliberately
     * present. Foreground must ignore it, without dereferencing its owner. */
    for(unsigned i=0u;i<8u;i++)R.vector100[i]=UINT32_C(0x71300000)+i;
    for(unsigned i=0u;i<8u;i++)R.vector400[i]=UINT32_C(0x72400000)+i;
    for(unsigned i=0u;i<16u;i++)R.vector600[i]=UINT32_C(0x73600000)+i;
    const struct kui_native_standalone_source source={
        UINT32_C(0x8c0a0040),UINT32_C(0x0c0a00cc),
        UINT32_C(0x0c0a0204),UINT32_C(0x0c09ff10),
        KUI_NATIVE_STANDALONE_SOURCE_MAGIC};
    memcpy(R.vector100,&source,sizeof(source));
    R.vector600[KUI_NATIVE_SDK_SCI_COOKIE_INDEX]=0u;
    R.vector600[KUI_NATIVE_SDK_SCI_VBR_INDEX]=0u;
    R.vector600[KUI_NATIVE_SDK_SCI_DEFAULT_INDEX]=UINT32_C(0x8c012346);
    memcpy(vectors100,R.vector100,sizeof(vectors100));
    memcpy(vectors400,R.vector400,sizeof(vectors400));
    memcpy(vectors600,R.vector600,sizeof(vectors600));
    /* The public binder must also be inert. No fixed P1 host mappings are
     * provided: inspecting a former executable's vector is a failure. */
    kui_native_standalone_bind();vectors_unchanged();
    R.vector600[KUI_NATIVE_SDK_SCI_COOKIE_INDEX]=KUI_NATIVE_SDK_SCI_COOKIE;
    R.vector600[KUI_NATIVE_SDK_SCI_VBR_INDEX]=GAME_VBR;
    R.vector600[KUI_NATIVE_SDK_SCI_DEFAULT_INDEX]=UINT32_C(0x8c012346);
    memcpy(vectors100,R.vector100,sizeof(vectors100));
    memcpy(vectors400,R.vector400,sizeof(vectors400));
    memcpy(vectors600,R.vector600,sizeof(vectors600));
    mode(2048u);
}
static void owner_phase(unsigned phase,uint32_t owner,uint16_t iprb) {
    CHECK(!R.engine.opened && !bus_owned && !R.shared.port.acquired);
    hw.vbr=owner;hw.iprb=iprb;
    m.smr=(uint8_t)(0x40u+phase);m.brr=(uint8_t)(7u+phase);
    m.scr=(uint8_t)(0x10u+phase);m.sptr=(uint8_t)(0x20u+phase);
    hw.frozen=false;
}
static int32_t finish_foreground(int32_t token) {
    for(unsigned round=0u;round<3000u;round++) {
        elapse();
        uint32_t done=R.engine.cursor.done;
        kui_retail_hook_active=0u;
        CHECK(kui_retail_sdk_sci_dispatch(round&1u?0x4e0u:0x500u)==0u);
        kui_retail_hook_active=1u;
        CHECK(R.engine.cursor.done==done && !R.engine.release.armed);
        CHECK(!R.engine.stats.irq_blocks);
        int32_t status=gd(round&1u?KUI_GD_EXEC:KUI_GD_CHECK,
            round&1u?0u:(uint32_t)token,round&1u?0u:STATUS);
        if(round&1u)status=gd(KUI_GD_CHECK,(uint32_t)token,STATUS);
        vectors_unchanged();
        if(status!=KUI_GD_PROCESSING)return status;
    }
    CHECK(!"foreground request never completed");return -1;
}
static void read_foreground(uint32_t lba,unsigned count,uint32_t owner,uint16_t iprb) {
    memset(ram+OUTPUT-BEGIN,0xa5,sizeof(expected)+16u);
    unsigned acquired=hw.acquires,released=bus_releases;
    int32_t token=request(lba,count,OUTPUT);
    CHECK(hw.acquires==acquired+1u && R.engine.opened && bus_owned);
    CHECK(!R.engine.release.armed && hw.iprb==(uint16_t)(R.engine.hooked?
        iprb&~0xf0u:iprb));
    CHECK(finish_foreground(token)==KUI_GD_COMPLETED);
    CHECK(get(STATUS+8u)==count*R.shared.service.sector_bytes && !get(STATUS+4u));
    compare(lba,count,OUTPUT);++verified;
    CHECK(bus_releases==released+1u);quiescent(owner,iprb);
}
static void repeated_phases(void) {
    static const uint32_t owners[]={UINT32_C(0x8c00f400),UINT32_C(0x8c200000),UINT32_C(0x8c300000)};
    for(unsigned cooked=0u;cooked<2u;cooked++)for(unsigned scatter=0u;scatter<2u;scatter++) {
        setup_foreground(cooked!=0u,scatter!=0u);
        for(unsigned phase=0u;phase<3u;phase++)for(unsigned field=0u;field<16u;field++) {
            uint16_t iprb=(uint16_t)(0x5a0fu+field*16u);
            owner_phase(phase,owners[phase],iprb);mode(field&1u?2352u:2048u);
            /* A cooked 2048-byte track cannot supply a 2352-byte sector;
             * its adjacent final track remains genuinely raw. */
            uint32_t lba=field&1u?(cooked?45300u:45298u):45001u;
            unsigned count=field&1u?(cooked?2u:4u):64u;
            read_foreground(lba,count,owners[phase],iprb);
            CHECK(R.shared.service.pending==0u);
        }
    }
}
static void foreign_dma(void) {
    setup_foreground(true,true);owner_phase(3u,GAME_VBR,0x5a9fu);
    m.sar=UINT32_C(0x0c123450);m.dar=UINT32_C(0x0c156780);m.tcr=0x345u;m.chcr=1u;
    read_foreground(45001u,64u,GAME_VBR,0x5a9fu);
    CHECK(m.sar==UINT32_C(0x0c123450) && m.dar==UINT32_C(0x0c156780) && m.tcr==0x345u && m.chcr==1u);
}
static void corruptions_observed(unsigned fatal) {
#if KUI_NATIVE_GD_DMA_STREAM
    /* This compact profile omits nonfunctional SCI statistic counters.
     * Count corrupted blocks at the independent card-arrival boundary. */
    CHECK(card.corrupt_count==(fatal?991u:0u));
#else
    CHECK(kui_sci_stream_stats()->crc_errors==(fatal?9u:2u));
#endif
}
static void crc_failures(void) {
    for(unsigned fatal=0u;fatal<2u;fatal++) {
        setup_foreground(true,true);owner_phase(2u,GAME_VBR,0x5a6fu);
        card.corrupt_lba=manifest.slots[manifest.slots[2].track.first_extent].extent.card_lba+5u;
        card.corrupt_count=fatal?1000u:2u;
        int32_t token=request(45000u,40u,OUTPUT);
        CHECK(finish_foreground(token)==(fatal?KUI_GD_FAILED:KUI_GD_COMPLETED));
        corruptions_observed(fatal);
        if(!fatal) {compare(45000u,40u,OUTPUT);++verified;}
        else CHECK(get(STATUS+4u)==KUI_GD_ERROR_IO);
        quiescent(GAME_VBR,0x5a6fu);
        card.corrupt_count=0u;owner_phase(5u,GAME_VBR+0x100u,0x5a2fu);
        read_foreground(45001u,8u,GAME_VBR+0x100u,0x5a2fu);
    }
}
static void bus_refusal_recovery(void) {
    setup_foreground(true,false);owner_phase(1u,GAME_VBR,0x5a9fu);
    read_foreground(45001u,8u,GAME_VBR,0x5a9fu);
    const uint32_t owner=GAME_VBR+0x100u;const uint16_t iprb=0x5a7fu;
    owner_phase(3u,owner,iprb);hw.busy_bus=true;
    unsigned acquired=hw.acquires,released=bus_releases,writes=hw.writes;
    int32_t token=request(45010u,8u,OUTPUT);
    CHECK(gd(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_FAILED);
    CHECK(get(STATUS+4u)==KUI_GD_ERROR_IO && !get(STATUS+8u));
    CHECK(hw.acquires==acquired+1u && bus_releases==released && hw.writes==writes);
    CHECK(!R.engine.opened && !R.engine.active && !R.engine.hooked && !R.engine.release.armed);
    CHECK(!R.shared.port.acquired && !bus_owned && !kui_sci_stream_busy() && !R.shared.stream.state);
    CHECK(hw.vbr==owner && hw.iprb==iprb && m.smr==0x43u && m.brr==10u &&
        m.scr==0x13u && m.sptr==0x23u);
    vectors_unchanged();
    hw.busy_bus=false;
    read_foreground(45010u,8u,owner,iprb);
}
static void cancellation(void) {
    for(unsigned how=0u;how<3u;how++) {
        setup_foreground(true,true);owner_phase(how,GAME_VBR,0x5abfu);
        int32_t token=request(45001u,64u,OUTPUT);
        CHECK(R.shared.service.pending && R.engine.cursor.done<64u);
        CHECK(!gd(how==0u?KUI_GD_ABORT:how==1u?KUI_GD_INIT:KUI_GD_RESET,
            how==0u?(uint32_t)token:0u,0u));
        hw.frozen=true;unsigned writes=hw.writes;
        quiescent(GAME_VBR,0x5abfu);
        for(unsigned i=0u;i<8u;i++) {elapse();(void)gd(KUI_GD_EXEC,0u,0u);}
        CHECK(hw.writes==writes);++cancels;
        if(how==0u) {
            /* ABORT preserves the failed GD handle until the caller
             * acknowledges it; INIT/RESET clear the handle themselves. */
            CHECK(gd(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_FAILED);
            CHECK(get(STATUS+4u)==KUI_GD_ERROR_CANCELLED);
        }
        owner_phase(how+4u,GAME_VBR+0x200u,0x5a3fu);
        read_foreground(45010u,8u,GAME_VBR+0x200u,0x5a3fu);
    }
}
#if KUI_NATIVE_GD_LEGACY_STREAM
#define LEGACY_LEFT (STATUS+32u)
static int32_t legacy_request(uint32_t lba,uint32_t sectors) {
    unsigned acquired=hw.acquires,started=card.cmd18,released=bus_releases,writes=hw.writes;
    put(PARAM,lba+150u);put(PARAM+4u,sectors);
    int32_t token=gd(KUI_GD_REQUEST,KUI_RETAIL_GD_DMAREAD_LEGACY_STREAM,PARAM);
    CHECK(token>0 && !R.shared.service.pending && !R.engine.active && !R.engine.opened);
    CHECK(!bus_owned && !R.shared.port.acquired && !kui_sci_stream_busy());
    CHECK(hw.acquires==acquired && card.cmd18==started && bus_releases==released && hw.writes==writes);
    CHECK(gd(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_RETAIL_GD_LEGACY_STREAMING);
    CHECK(!get(STATUS+8u) && !get(STATUS+4u) &&
        R.shared.service.command==KUI_RETAIL_GD_DMAREAD_LEGACY_STREAM);
    put(PARAM,OUTPUT);put(PARAM+4u,R.shared.service.sector_bytes);
    CHECK(gd(KUI_GD_DMA_TRANSFER,(uint32_t)token+1u,PARAM)==-1);
    put(LEGACY_LEFT,UINT32_MAX);
    CHECK(gd(KUI_GD_DMA_CHECK,(uint32_t)token+1u,LEGACY_LEFT)==-1 && get(LEGACY_LEFT)==UINT32_MAX);
    put(LEGACY_LEFT,UINT32_MAX);
    CHECK(gd(KUI_GD_DMA_CHECK,(uint32_t)token,LEGACY_LEFT)==0 && !get(LEGACY_LEFT));
    CHECK(!gd(KUI_GD_DMA_CHECK,(uint32_t)token,0u));
    CHECK(hw.acquires==acquired && card.cmd18==started && bus_releases==released && hw.writes==writes);
    ++legacy_idle_polls;++legacy_streams;vectors_unchanged();return token;
}
static void legacy_transfer(int32_t token,uint32_t destination,unsigned sectors) {
    unsigned acquired=hw.acquires,released=bus_releases;
    CHECK(!R.shared.service.pending && !R.engine.opened && !bus_owned);
    put(PARAM,destination);put(PARAM+4u,sectors*R.shared.service.sector_bytes);
    CHECK(!gd(KUI_GD_DMA_TRANSFER,(uint32_t)token,PARAM));
    CHECK(hw.acquires==acquired+1u && bus_releases==released && R.engine.opened && bus_owned);
    CHECK(R.shared.service.token==(uint32_t)token &&
        R.shared.service.command==KUI_RETAIL_GD_DMAREAD_LEGACY_STREAM);
}
/* CHECK remains STREAMING between pieces. FN7 and the payload engine, rather
 * than a terminal CHECK, determine when the current piece has finished. */
static int32_t finish_legacy_piece(int32_t token,unsigned sectors) {
    uint32_t previous=sectors*R.shared.service.sector_bytes;
    for(unsigned round=0u;round<3000u;round++) {
        put(LEGACY_LEFT,UINT32_MAX);
        int32_t busy=gd(KUI_GD_DMA_CHECK,(uint32_t)token,LEGACY_LEFT);
        if(R.shared.service.error) {
            CHECK(busy==-1 && get(LEGACY_LEFT)==UINT32_MAX);
            return KUI_GD_FAILED; /* Preserve the failed handle for ABORT/CHECK tests. */
        }
        CHECK(busy==0 || busy==1);
        CHECK(get(LEGACY_LEFT)<=previous);previous=get(LEGACY_LEFT);
        CHECK(R.shared.service.pending==(uint32_t)(busy==1));
        if(!busy) {
            CHECK(!previous && !R.engine.active && !R.engine.opened && !bus_owned);
            ++legacy_idle_polls;return KUI_RETAIL_GD_LEGACY_STREAMING;
        }
        ++legacy_busy_polls;
        CHECK(gd(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_RETAIL_GD_LEGACY_STREAMING);
        elapse();CHECK(!gd(KUI_GD_EXEC,0u,0u));vectors_unchanged();
    }
    CHECK(!"legacy stream piece never completed");return -1;
}
static void legacy_piece_bytes(int32_t token,uint32_t lba,unsigned sectors,
    uint32_t destination,uint32_t aggregate,uint32_t owner,uint16_t iprb) {
    memset(ram+destination-BEGIN,0xa5,(size_t)sectors*R.shared.service.sector_bytes+16u);
    unsigned released=bus_releases;
    legacy_transfer(token,destination,sectors);
    CHECK(finish_legacy_piece(token,sectors)==KUI_RETAIL_GD_LEGACY_STREAMING);
    CHECK(bus_releases==released+1u);quiescent(owner,iprb);
    CHECK(gd(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_RETAIL_GD_LEGACY_STREAMING);
    CHECK(!get(STATUS+4u) && get(STATUS+8u)==aggregate && !get(STATUS+12u));
    CHECK(R.shared.service.lba==lba+sectors && !R.shared.service.count);
    compare(lba,sectors,destination);++verified;++legacy_pieces;
}
static void legacy_piece_sequences(void) {
    /* The final two-sector piece crosses into a different raw track. The
     * 2352-byte case also leaves a16-byte alignment gap after the first piece. */
    for(unsigned cooked=0u;cooked<2u;cooked++)for(unsigned scatter=0u;scatter<2u;scatter++)
    for(unsigned raw=0u;raw<(cooked?1u:2u);raw++) {
        setup_foreground(cooked!=0u,scatter!=0u);owner_phase(6u,GAME_VBR,0x5a4fu);
        mode(raw?2352u:2048u);
        uint32_t bytes=R.shared.service.sector_bytes,lba=45235u,destination=OUTPUT,total=0u;
        uint32_t destinations[3],lbas[3];
        int32_t token=legacy_request(lba,67u);
        const unsigned pieces[]={1u,64u,2u};
        for(unsigned i=0u;i<3u;i++) {
            destinations[i]=destination;lbas[i]=lba;
            uint32_t delivered=pieces[i]*bytes;total+=delivered;
            legacy_piece_bytes(token,lba,pieces[i],destination,total,GAME_VBR,0x5a4fu);
            lba+=pieces[i];destination+=(delivered+31u)&~31u;
        }
        /* A later piece must not overwrite an earlier destination or its raw
         * sector alignment gap. Recheck all retained bytes after the stream. */
        for(unsigned i=0u;i<3u;i++) {
            uint32_t delivered=pieces[i]*bytes;
            CHECK(kui_retail_image_read(&reference,lbas[i],pieces[i],
                raw?KUI_GAME_SECTOR_RAW:KUI_GAME_SECTOR_MODE1,expected,sizeof(expected))==KUI_GAME_OK);
            CHECK(!memcmp(ram+destinations[i]-BEGIN,expected,delivered));
            for(uint32_t gap=delivered;gap<((delivered+31u)&~31u);gap++)
                CHECK(ram[destinations[i]-BEGIN+gap]==0xa5u);
        }
        CHECK(R.shared.service.token==(uint32_t)token && total==67u*bytes);
        unsigned acquired=hw.acquires,released=bus_releases;
        put(PARAM,destination);put(PARAM+4u,bytes);
        CHECK(gd(KUI_GD_DMA_TRANSFER,(uint32_t)token,PARAM)==-1);
        CHECK(hw.acquires==acquired && bus_releases==released && !R.engine.opened && !bus_owned);
        CHECK(!gd(KUI_GD_ABORT,(uint32_t)token,0u));
        CHECK(gd(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_COMPLETED);
        CHECK(get(STATUS+8u)==total && !R.shared.service.command);
        CHECK(gd(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_NOT_FOUND);
        quiescent(GAME_VBR,0x5a4fu);
    }
}
static void legacy_cancellation(void) {
    for(unsigned active=0u;active<2u;active++)for(unsigned how=0u;how<3u;how++) {
        setup_foreground(true,true);owner_phase(7u,GAME_VBR,0x5a8fu);
        int32_t token=legacy_request(45000u,65u);
        legacy_piece_bytes(token,45000u,1u,OUTPUT,2048u,GAME_VBR,0x5a8fu);
        if(active)legacy_transfer(token,OUTPUT+4096u,64u);
        CHECK(!gd(how==0u?KUI_GD_ABORT:how==1u?KUI_GD_INIT:KUI_GD_RESET,
            how==0u?(uint32_t)token:0u,0u));
        unsigned writes=hw.writes;hw.frozen=true;quiescent(GAME_VBR,0x5a8fu);
        for(unsigned i=0u;i<8u;i++) {elapse();CHECK(!gd(KUI_GD_EXEC,0u,0u));}
        CHECK(hw.writes==writes);++legacy_cancels;
        if(!how)CHECK(gd(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_COMPLETED);
        else CHECK(gd(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_NOT_FOUND);
        owner_phase(8u,GAME_VBR+0x300u,0x5a1fu);
        read_foreground(45010u,8u,GAME_VBR+0x300u,0x5a1fu);
    }
}
static void legacy_crc_failures(void) {
    for(unsigned fatal=0u;fatal<2u;fatal++) {
        setup_foreground(true,true);owner_phase(9u,GAME_VBR,0x5a6fu);
        int32_t token=legacy_request(45000u,65u);
        legacy_piece_bytes(token,45000u,1u,OUTPUT,2048u,GAME_VBR,0x5a6fu);
        card.corrupt_lba=manifest.slots[manifest.slots[2].track.first_extent].extent.card_lba+5u;
        card.corrupt_count=fatal?1000u:2u;
        legacy_transfer(token,OUTPUT+4096u,64u);
        CHECK(finish_legacy_piece(token,64u)==(fatal?KUI_GD_FAILED:KUI_RETAIL_GD_LEGACY_STREAMING));
        corruptions_observed(fatal);
        if(fatal) {
            CHECK(R.shared.service.error==KUI_GD_ERROR_IO);
            CHECK(gd(KUI_GD_ABORT,(uint32_t)token,0u)==-1);
            CHECK(R.shared.service.error==KUI_GD_ERROR_IO && R.shared.service.status==KUI_GD_FAILED);
            CHECK(gd(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_FAILED);
            CHECK(get(STATUS+4u)==KUI_GD_ERROR_IO);
            ++legacy_errors;
        } else {
            compare(45001u,64u,OUTPUT+4096u);++verified;++legacy_pieces;
            CHECK(!gd(KUI_GD_ABORT,(uint32_t)token,0u));
            CHECK(gd(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_COMPLETED);
            CHECK(get(STATUS+8u)==65u*2048u);
        }
        quiescent(GAME_VBR,0x5a6fu);
        card.corrupt_count=0u;owner_phase(10u,GAME_VBR+0x400u,0x5a2fu);
        read_foreground(45020u,8u,GAME_VBR+0x400u,0x5a2fu);
    }
}
#endif
#if KUI_NATIVE_GD_DMA_STREAM
/* The stream oracle is the independent ordinary image reader. Scatter DMA
 * must concatenate to that byte stream, including partial disc sectors. */
static int32_t dma_request(uint32_t lba,unsigned sectors) {
    unsigned acquired=hw.acquires,started=card.cmd18,released=bus_releases;
    put(PARAM,lba+150u);put(PARAM+4u,sectors);put(PARAM+8u,45123u);
    int32_t token=gd(KUI_GD_REQUEST,KUI_RETAIL_GD_DMAREAD_STREAM,PARAM);
    CHECK(token>0 && R.shared.service.pending && !R.engine.active && !R.engine.opened);
    CHECK(hw.acquires==acquired && card.cmd18==started && bus_releases==released && !bus_owned);
    CHECK(gd(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_RETAIL_GD_STREAMING);
    CHECK(!get(STATUS+8u) && !get(STATUS+4u));
    CHECK(!kui_retail_gd_native_dma_events(&R.shared.service));
    ++dma_streams;return token;
}
static uint32_t dma_events(void) {
    uint32_t events=kui_retail_gd_native_dma_events(&R.shared.service);
    CHECK(!(events&~(KUI_RETAIL_GD_IRQ_DMA_END|KUI_RETAIL_GD_IRQ_DRIVE)));
    if(events&KUI_RETAIL_GD_IRQ_DMA_END)++dma_events21;
    if(events&KUI_RETAIL_GD_IRQ_DRIVE)++dma_events20;
    CHECK(!kui_retail_gd_native_dma_events(&R.shared.service));
    return events;
}
static void dma_guards(uint32_t destination,uint32_t bytes) {
    for(unsigned i=0u;i<32u;i++) {
        CHECK(ram[destination-BEGIN-32u+i]==0xa5u);
        CHECK(ram[destination-BEGIN+bytes+i]==0xa5u);
    }
}
static void dma_piece(int32_t token,uint32_t destination,uint32_t bytes,
    uint32_t offset,uint32_t total,unsigned alias) {
    memset(ram+destination-BEGIN-32u,0xa5u,bytes+64u);
    uint32_t physical=alias==0u?destination:alias==1u?
        destination&0x1fffffffu:(destination&0x1fffffffu)|0xa0000000u;
    put(PARAM,physical);put(PARAM+4u,bytes);
    CHECK(!gd(KUI_GD_DMA_TRANSFER,(uint32_t)token,PARAM));
    /* The foreground bridge finishes this piece before returning FN6.
     * No synthetic EXEC, CHECK or SCI interrupt is used to finish it. */
    CHECK(!R.engine.active && R.shared.service.completed_bytes==offset+bytes);
    CHECK(!memcmp(ram+destination-BEGIN,expected+offset,bytes));
    dma_guards(destination,bytes);vectors_unchanged();
    CHECK(dma_events()==(KUI_RETAIL_GD_IRQ_DMA_END|
        (offset+bytes==total?KUI_RETAIL_GD_IRQ_DRIVE:0u)));
    put(LEGACY_LEFT,UINT32_MAX);
    CHECK(!gd(KUI_GD_DMA_CHECK,(uint32_t)token,LEGACY_LEFT) && !get(LEGACY_LEFT));
    CHECK(gd(KUI_GD_CHECK,(uint32_t)token,STATUS)==
        (offset+bytes==total?KUI_GD_COMPLETED:KUI_RETAIL_GD_STREAMING));
    CHECK(get(STATUS+8u)==offset+bytes && !get(STATUS+4u));
    if(offset+bytes==total) {
        CHECK(!R.shared.service.command);
    } else CHECK(R.shared.service.pending);
    /* The request can remain pending, but each synchronous piece gives
     * the caller its SCI controls back before the DMA event is consumed. */
    quiescent(GAME_VBR,0x5a4fu);
    ++dma_pieces;if(bytes%R.shared.service.sector_bytes)++dma_partials;
}
static void dma_scatter_sequences(void) {
    static const uint32_t offsets[]={0x1000u,0x18000u,0x8000u,0x24000u};
    for(unsigned cooked=0u;cooked<2u;cooked++)for(unsigned scatter=0u;scatter<2u;scatter++)
    for(unsigned raw=0u;raw<2u;raw++)for(unsigned split=0u;split<2u;split++) {
        setup_foreground(cooked!=0u,scatter!=0u);owner_phase(6u,GAME_VBR,0x5a4fu);
        mode(raw?2352u:2048u);
        uint32_t lba=raw&&cooked?45300u:45298u,count=raw&&cooked?2u:4u;
        uint32_t total=count*R.shared.service.sector_bytes;
        CHECK(kui_retail_image_read(&reference,lba,count,
            raw?KUI_GAME_SECTOR_RAW:KUI_GAME_SECTOR_MODE1,expected,sizeof(expected))==KUI_GAME_OK);
        uint32_t parts[4]={32u,4096u,32u,total-4160u};
        unsigned pieces=split?4u:1u;if(!split)parts[0]=total;
        int32_t token=dma_request(lba,count);uint32_t offset=0u;
        unsigned acquired=hw.acquires,released=bus_releases;
        for(unsigned i=0u;i<pieces;i++) {
            dma_piece(token,OUTPUT+offsets[i],parts[i],offset,total,i%3u);
            offset+=parts[i];
        }
        CHECK(offset==total && hw.acquires==acquired+pieces && bus_releases==released+pieces);
        offset=0u;
        for(unsigned i=0u;i<pieces;i++) {
            CHECK(!memcmp(ram+OUTPUT+offsets[i]-BEGIN,expected+offset,parts[i]));
            dma_guards(OUTPUT+offsets[i],parts[i]);offset+=parts[i];
        }
        CHECK(gd(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_NOT_FOUND);
        ++verified;
        /* New profile must still support ordinary reads immediately after
         * the terminal stream handle has been acknowledged. */
        owner_phase(7u,GAME_VBR+0x400u,0x5a2fu);
        read_foreground(raw&&cooked?45300u:45010u,2u,GAME_VBR+0x400u,0x5a2fu);
    }
}
static void dma_refusal_and_abort(void) {
    setup_foreground(true,true);owner_phase(6u,GAME_VBR,0x5a4fu);
    int32_t token=dma_request(45000u,4u);
    static const uint32_t bad_dest[]={OUTPUT+1u,OUTPUT,OUTPUT,OUTPUT};
    static const uint32_t bad_bytes[]={32u,0u,31u,4u*2048u+32u};
    unsigned acquired=hw.acquires,writes=hw.writes;
    for(unsigned i=0u;i<4u;i++) {
        put(PARAM,bad_dest[i]);put(PARAM+4u,bad_bytes[i]);
        CHECK(gd(KUI_GD_DMA_TRANSFER,(uint32_t)token,PARAM)==-1);
        CHECK(R.shared.service.pending && !R.shared.service.completed_bytes &&
            !R.shared.service.outputs[0] && hw.acquires==acquired && hw.writes==writes);
        CHECK(!dma_events());
    }
    put(PARAM,OUTPUT);put(PARAM+4u,32u);
    CHECK(gd(KUI_GD_DMA_TRANSFER,(uint32_t)token+1u,PARAM)==-1);
    CHECK(hw.acquires==acquired && hw.writes==writes && !dma_events());
    CHECK(kui_retail_image_read(&reference,45000u,4u,KUI_GAME_SECTOR_MODE1,
        expected,sizeof(expected))==KUI_GAME_OK);
    dma_piece(token,OUTPUT+0x8000u,32u,0u,4u*2048u,0u);
    CHECK(!gd(KUI_GD_ABORT,(uint32_t)token,0u));
    CHECK(dma_events()==(KUI_RETAIL_GD_IRQ_DMA_END|KUI_RETAIL_GD_IRQ_DRIVE));
    CHECK(gd(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_FAILED);
    CHECK(get(STATUS+4u)==KUI_GD_ERROR_CANCELLED && get(STATUS+8u)==32u);
    quiescent(GAME_VBR,0x5a4fu);++dma_cancels;
}
static void dma_crc_failures(void) {
    for(unsigned fatal=0u;fatal<2u;fatal++) {
        setup_foreground(true,true);owner_phase(6u,GAME_VBR,0x5a4fu);
        CHECK(kui_retail_image_read(&reference,45000u,4u,KUI_GAME_SECTOR_MODE1,
            expected,sizeof(expected))==KUI_GAME_OK);
        int32_t token=dma_request(45000u,4u);
        dma_piece(token,OUTPUT+0x8000u,32u,0u,4u*2048u,0u);
        card.corrupt_lba=manifest.slots[manifest.slots[2].track.first_extent].extent.card_lba+5u;
        card.corrupt_count=fatal?1000u:2u;
        uint32_t destination=OUTPUT+0x18000u,bytes=4u*2048u-32u;
        memset(ram+destination-BEGIN-32u,0xa5u,bytes+64u);
        put(PARAM,destination);put(PARAM+4u,bytes);
        CHECK(!gd(KUI_GD_DMA_TRANSFER,(uint32_t)token,PARAM));
        CHECK(!R.engine.active);
        CHECK(dma_events()==(KUI_RETAIL_GD_IRQ_DMA_END|KUI_RETAIL_GD_IRQ_DRIVE));
        corruptions_observed(fatal);
        CHECK(gd(KUI_GD_CHECK,(uint32_t)token,STATUS)==(fatal?KUI_GD_FAILED:KUI_GD_COMPLETED));
        uint32_t delivered=get(STATUS+8u)-32u;
        CHECK(delivered<=bytes && !memcmp(ram+destination-BEGIN,expected+32u,delivered));
        dma_guards(destination,bytes);
        if(fatal) {CHECK(get(STATUS+4u)==KUI_GD_ERROR_IO);++dma_errors;}
        else {CHECK(delivered==bytes);++verified;++dma_pieces;++dma_partials;}
        quiescent(GAME_VBR,0x5a4fu);
        card.corrupt_count=0u;owner_phase(7u,GAME_VBR+0x400u,0x5a2fu);
        read_foreground(45010u,8u,GAME_VBR+0x400u,0x5a2fu);
    }
}
static void dma_bus_refusal(void) {
    setup_foreground(true,true);owner_phase(6u,GAME_VBR,0x5a4fu);
    int32_t token=dma_request(45000u,4u);
    unsigned acquired=hw.acquires,released=bus_releases,writes=hw.writes,started=card.cmd18;
    uint8_t smr=m.smr,brr=m.brr,scr=m.scr,sptr=m.sptr;
    hw.busy_bus=true;put(PARAM,OUTPUT);put(PARAM+4u,32u);
    CHECK(!gd(KUI_GD_DMA_TRANSFER,(uint32_t)token,PARAM));
    CHECK(!R.engine.active && !R.engine.opened && !R.shared.service.pending);
    CHECK(!R.shared.port.acquired && !bus_owned && !R.shared.port.fault);
    CHECK(hw.acquires==acquired+1u && bus_releases==released && hw.writes==writes && card.cmd18==started);
    CHECK(m.smr==smr && m.brr==brr && m.scr==scr && m.sptr==sptr);
    CHECK(dma_events()==(KUI_RETAIL_GD_IRQ_DMA_END|KUI_RETAIL_GD_IRQ_DRIVE));
    CHECK(gd(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_FAILED);
    CHECK(get(STATUS+4u)==KUI_GD_ERROR_IO && !get(STATUS+8u));
    vectors_unchanged();++dma_errors;
    hw.busy_bus=false;read_foreground(45010u,8u,GAME_VBR,0x5a4fu);
}
#endif
#if KUI_NATIVE_GD_VRAM_DMA
/* Authored aperture banks are deliberately separate from main RAM and from
 * each other. The target linked model separately verifies real bus widths
 * and the shared/interleaved physical VRAM backing. */
#define VRAM_SIZE 0x800000u
#define VRAM_ADDRESS 0x04575600u
#define VRAM_OFFSET 0x575600u
#define VRAM_TOTAL 0x80800u
static uint8_t vram_memory[2][VRAM_SIZE];
static uint8_t vram_expected[VRAM_TOTAL];
static uint8_t vram_same_ram[0x80040u];
static uint8_t vram_cpu_virtual[0x80040u];
static unsigned vram_validations,vram_writes;
static uint8_t *vram_map(void *context,uint32_t address,uint32_t bytes,int writing) {
    for(unsigned view=0u;view<2u;view++) {
        const uint32_t base=view?0xa5000000u:0xa4000000u;
        if(address>=base && address-base<VRAM_SIZE && bytes<=VRAM_SIZE-(address-base)) {
            if(writing==KUI_RETAIL_MAP_DMA_VALIDATE) {++vram_validations;return vram_memory[view]+VRAM_SIZE;}
            if(writing!=KUI_RETAIL_MAP_DMA_WRITE)return NULL;
            CHECK(bytes && !hw.frozen);++hw.writes;++vram_writes;
            if((address|bytes)&1u)++vram_odd_fragments;
            return vram_memory[view]+address-base;
        }
    }
    /* The SAME numeric04 address is a CPU MMU address for PIO/parameters.
     * DMA's dedicated access must instead arrive through P2 A4/A5 above. */
    if(address>=VRAM_ADDRESS-32u && address-(VRAM_ADDRESS-32u)<sizeof(vram_cpu_virtual) &&
       bytes<=sizeof(vram_cpu_virtual)-(address-(VRAM_ADDRESS-32u)) && writing<=2) {
        if(writing==KUI_RETAIL_MAP_VALIDATE)return vram_cpu_virtual+sizeof(vram_cpu_virtual);
        if(writing==1) {CHECK(!hw.frozen);++hw.writes;}
        return vram_cpu_virtual+address-(VRAM_ADDRESS-32u);
    }
    /* If the old blanket main-RAM normalization survives in start(), its
     * writes land here rather than accidentally satisfying the VRAM oracle. */
    if(address>=0x8c5755e0u && address-0x8c5755e0u<sizeof(vram_same_ram) &&
       bytes<=sizeof(vram_same_ram)-(address-0x8c5755e0u)) {
        if(writing==KUI_RETAIL_MAP_VALIDATE)return vram_same_ram+sizeof(vram_same_ram);
        if(writing==1) {++vram_same_ram_writes;++hw.writes;}
        return vram_same_ram+address-0x8c5755e0u;
    }
    if(address>=BEGIN && address<END && bytes<=END-address)
        return map(context,address,bytes,writing);
    return NULL;
}
static void setup_vram(unsigned phase,bool cooked) {
    setup_foreground(cooked,false);owner_phase(6u,GAME_VBR,0x5a4fu);
    /* A complete one-track map allows a sector stream larger than the old
     * 64-sector expected buffer and explicit odd container offsets. */
    memset(manifest.slots,0,sizeof(manifest.slots));
    manifest.track_count=1u;manifest.extent_count=1u;
    manifest.slots[0].track=(struct kui_retail_track){.start_lba=45000u,.end_lba=45300u,
        .first_extent=(uint16_t)(1u|((phase&255u)<<8)),.extent_count=1u,
        .control=(uint8_t)(4u|(cooked?KUI_RETAIL_TRACK_COOKED:0u)|
            (phase&256u?KUI_RETAIL_TRACK_OFFSET_HIGH:0u))};
    const uint32_t stride=cooked?2048u:2352u,bytes=300u*stride;
    manifest.slots[1].extent=(struct kui_retail_extent){0u,100u,(phase+bytes+511u)/512u};
    memset(image_card,0xf3,sizeof(image_card));
    for(uint32_t i=0u;i<bytes;i++)image_card[100u*512u+phase+i]=source(2u,i);
    CHECK(kui_retail_manifest_validate(&manifest)==KUI_GAME_OK);
    CHECK(kui_retail_image_init(&reference,&manifest,read_block,NULL)==KUI_GAME_OK);
    const struct kui_gd_ops ops={NULL,vram_map,check,NULL};
    kui_retail_gd_init_validated(&R.shared.service,manifest.slots,1u,&ops,BEGIN,END);
    R.shared.service.cpu_mmu=1u;mode(2048u);
    memset(vram_memory,0xa5,sizeof(vram_memory));
    memset(vram_same_ram,0xa5,sizeof(vram_same_ram));
    memset(vram_cpu_virtual,0xa5,sizeof(vram_cpu_virtual));
    vram_validations=vram_writes=0u;
}
static void vram_canaries(unsigned view,uint32_t offset,uint32_t bytes) {
    for(unsigned i=0u;i<32u;i++) {
        CHECK(vram_memory[view][offset-32u+i]==0xa5u);
        CHECK(vram_memory[view][offset+bytes+i]==0xa5u);
    }
    for(uint32_t i=0u;i<bytes;i++)CHECK(vram_memory[view^1u][offset+i]==0xa5u);
    for(unsigned i=0u;i<sizeof(vram_same_ram);i++)assert(vram_same_ram[i]==0xa5u);
    for(unsigned i=0u;i<sizeof(vram_cpu_virtual);i++)assert(vram_cpu_virtual[i]==0xa5u);
    CHECK(!vram_same_ram_writes);
}
static void vram_reference(uint32_t lba,unsigned sectors) {
    /* The independent image API bounds each call to64 sectors; concatenate
     * its ordinary reads rather than widening its validation for this test. */
    unsigned done=0u;
    while(done<sectors) {
        unsigned count=sectors-done;if(count>64u)count=64u;
        CHECK(kui_retail_image_read(&reference,lba+done,count,KUI_GAME_SECTOR_MODE1,
            vram_expected+done*2048u,sizeof(vram_expected)-done*2048u)==KUI_GAME_OK);
        done+=count;
    }
}
static void vram_piece(int32_t token,uint32_t destination,uint32_t bytes,
    uint32_t offset,uint32_t total) {
    put(PARAM,destination);put(PARAM+4u,bytes);
    CHECK(!gd(KUI_GD_DMA_TRANSFER,(uint32_t)token,PARAM));
    CHECK(!R.engine.active && !R.shared.service.error &&
        R.shared.service.completed_bytes==offset+bytes && !R.shared.service.outputs[0]);
    CHECK(dma_events()==(KUI_RETAIL_GD_IRQ_DMA_END|
        (offset+bytes==total?KUI_RETAIL_GD_IRQ_DRIVE:0u)));
    CHECK(gd(KUI_GD_CHECK,(uint32_t)token,STATUS)==
        (offset+bytes==total?KUI_GD_COMPLETED:KUI_RETAIL_GD_STREAMING));
    CHECK(get(STATUS+8u)==offset+bytes && !get(STATUS+4u));
    quiescent(GAME_VBR,0x5a4fu);
}
static void vram_observed_sequence(void) {
    for(unsigned view=0u;view<2u;view++) {
        setup_vram(0u,true);
        vram_reference(45020u,257u);
        int32_t token=dma_request(45020u,257u);
        vram_piece(token,OUTPUT+0x8000u,32u,0u,VRAM_TOTAL);
        CHECK(!memcmp(ram+OUTPUT+0x8000u-BEGIN,vram_expected,32u));
        const uint32_t destination=VRAM_ADDRESS+view*0x1000000u;
        vram_piece(token,destination,0x80000u,32u,VRAM_TOTAL);
        CHECK(vram_validations==1u && vram_writes>0u);
        CHECK(!memcmp(vram_memory[view]+VRAM_OFFSET,vram_expected+32u,0x80000u));
        vram_canaries(view,VRAM_OFFSET,0x80000u);
        vram_piece(token,OUTPUT+0x18000u,2016u,0x80020u,VRAM_TOTAL);
        CHECK(!memcmp(ram+OUTPUT+0x18000u-BEGIN,vram_expected+0x80020u,2016u));
        CHECK(!memcmp(vram_memory[view]+VRAM_OFFSET,vram_expected+32u,0x80000u));
        CHECK(gd(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_NOT_FOUND);
        ++vram_cases;vram_bytes+=0x80000u;
    }
}
static void vram_fragments(void) {
    static const unsigned phases[]={1u,2u,3u,255u,511u};
    for(unsigned cooked=0u;cooked<2u;cooked++)for(unsigned i=0u;i<sizeof(phases)/sizeof(*phases);i++) {
        const unsigned view=i&1u;
        setup_vram(phases[i],cooked!=0u);
        vram_reference(45020u,4u);
        int32_t token=dma_request(45020u,4u);
        const uint32_t destination=(VRAM_ADDRESS+view*0x1000000u)|
            (i%3u==0u?0u:i%3u==1u?0x80000000u:0xa0000000u);
        vram_piece(token,destination,8192u,0u,8192u);
        CHECK(vram_validations==1u && vram_writes>0u);
        CHECK(!memcmp(vram_memory[view]+VRAM_OFFSET,vram_expected,8192u));
        vram_canaries(view,VRAM_OFFSET,8192u);++vram_cases;vram_bytes+=8192u;
    }
}
static void vram_crc_failures(void) {
    for(unsigned fatal=0u;fatal<2u;fatal++) {
        setup_vram(1u,true);
        vram_reference(45000u,4u);
        int32_t token=dma_request(45000u,4u);
        card.corrupt_lba=105u;card.corrupt_count=fatal?1000u:2u;
        put(PARAM,VRAM_ADDRESS);put(PARAM+4u,8192u);
        CHECK(!gd(KUI_GD_DMA_TRANSFER,(uint32_t)token,PARAM));
        CHECK(!R.engine.active && !R.shared.service.outputs[0]);
        CHECK(dma_events()==(KUI_RETAIL_GD_IRQ_DMA_END|KUI_RETAIL_GD_IRQ_DRIVE));
        CHECK(gd(KUI_GD_CHECK,(uint32_t)token,STATUS)==(fatal?KUI_GD_FAILED:KUI_GD_COMPLETED));
        const uint32_t done=get(STATUS+8u);CHECK(done<=8192u);
        CHECK(!memcmp(vram_memory[0]+VRAM_OFFSET,vram_expected,done));
        for(uint32_t i=done;i<8192u;i++)CHECK(vram_memory[0][VRAM_OFFSET+i]==0xa5u);
        if(fatal)CHECK(get(STATUS+4u)==KUI_GD_ERROR_IO && done<8192u);
        else CHECK(!get(STATUS+4u) && done==8192u);
        vram_canaries(0u,VRAM_OFFSET,8192u);quiescent(GAME_VBR,0x5a4fu);
        card.corrupt_count=0u;++vram_crc_cases;
    }
}
static void vram_abort_and_bus_refusal(void) {
    setup_vram(3u,true);
    int32_t token=dma_request(45000u,4u);
    vram_piece(token,OUTPUT+0x8000u,32u,0u,8192u);
    CHECK(!gd(KUI_GD_ABORT,(uint32_t)token,0u));
    CHECK(dma_events()==(KUI_RETAIL_GD_IRQ_DMA_END|KUI_RETAIL_GD_IRQ_DRIVE));
    CHECK(gd(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_FAILED);
    CHECK(get(STATUS+8u)==32u && get(STATUS+4u)==KUI_GD_ERROR_CANCELLED);
    unsigned writes=vram_writes;hw.frozen=true;
    for(unsigned i=0u;i<8u;i++)CHECK(!gd(KUI_GD_EXEC,0u,0u));
    CHECK(vram_writes==writes && !writes);quiescent(GAME_VBR,0x5a4fu);++vram_aborts;

    setup_vram(1u,true);token=dma_request(45000u,4u);
    hw.busy_bus=true;put(PARAM,VRAM_ADDRESS);put(PARAM+4u,8192u);
    CHECK(!gd(KUI_GD_DMA_TRANSFER,(uint32_t)token,PARAM));
    CHECK(!R.engine.active && !R.engine.opened && !bus_owned && !R.shared.port.acquired);
    CHECK(!vram_writes && !R.shared.service.completed_bytes);
    CHECK(dma_events()==(KUI_RETAIL_GD_IRQ_DMA_END|KUI_RETAIL_GD_IRQ_DRIVE));
    CHECK(gd(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_FAILED);
    CHECK(!get(STATUS+8u) && get(STATUS+4u)==KUI_GD_ERROR_IO);
    vram_canaries(0u,VRAM_OFFSET,8192u);vectors_unchanged();
    hw.busy_bus=false;++vram_bus_refusals;
}
static void vram_context_and_ordinary_reads(void) {
    int32_t token;
    for(unsigned view=0u;view<2u;view++) {
        setup_vram(3u,true);vram_reference(45020u,4u);
        int32_t token=request(45020u,4u,VRAM_ADDRESS+view*0x1000000u);
        CHECK(finish_foreground(token)==KUI_GD_COMPLETED);
        CHECK(get(STATUS+8u)==8192u && !get(STATUS+4u));
        CHECK(!memcmp(vram_memory[view]+VRAM_OFFSET,vram_expected,8192u));
        vram_canaries(view,VRAM_OFFSET,8192u);quiescent(GAME_VBR,0x5a4fu);
        ++vram_cases;vram_bytes+=8192u;
    }
#if KUI_NATIVE_GD_PIO_STREAM
    setup_vram(1u,true);vram_reference(45020u,4u);
    put(PARAM,45170u);put(PARAM+4u,4u);put(PARAM+8u,VRAM_ADDRESS);put(PARAM+12u,0u);
    token=gd(KUI_GD_REQUEST,KUI_GD_PIOREAD,PARAM);CHECK(token>0);
    CHECK(finish_foreground(token)==KUI_GD_COMPLETED);
    CHECK(!memcmp(vram_cpu_virtual+32u,vram_expected,8192u));
    CHECK(!vram_validations && !vram_writes && !vram_same_ram_writes);
    for(unsigned view=0u;view<2u;view++)for(unsigned i=0u;i<8192u;i++)
        CHECK(vram_memory[view][VRAM_OFFSET+i]==0xa5u);
    quiescent(GAME_VBR,0x5a4fu);++vram_virtual_reads;
#endif

    setup_vram(1u,true);token=dma_request(45000u,4u);
    vram_piece(token,OUTPUT+0x8000u,32u,0u,8192u);
    unsigned acquired=hw.acquires,writes=hw.writes;
    /* CPU result/parameter buffers never inherit physical-VRAM access,
     * even while a DMA stream owns the command. */
    CHECK(gd(KUI_GD_CHECK,(uint32_t)token,0xa4575600u)==-1);++vram_refusals;
    CHECK(gd(KUI_GD_DMA_TRANSFER,(uint32_t)token,0xa4575600u)==-1);++vram_refusals;
    static const uint32_t bad[]={0x04800000u,0x05800000u,0x047ffff0u,0x10000000u,0x00800000u};
    for(unsigned i=0u;i<sizeof(bad)/sizeof(*bad);i++) {
        put(PARAM,bad[i]);put(PARAM+4u,32u);
        CHECK(gd(KUI_GD_DMA_TRANSFER,(uint32_t)token,PARAM)==-1);++vram_refusals;
    }
    CHECK(hw.acquires==acquired && hw.writes==writes && !vram_validations && !vram_writes);
    CHECK(R.shared.service.completed_bytes==32u && R.shared.service.pending &&
        !R.shared.service.outputs[0] && !R.shared.service.error && !dma_events());
    CHECK(!gd(KUI_GD_ABORT,(uint32_t)token,0u));
    CHECK(dma_events()==(KUI_RETAIL_GD_IRQ_DMA_END|KUI_RETAIL_GD_IRQ_DRIVE));
    CHECK(gd(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_FAILED);
    CHECK(get(STATUS+8u)==32u && get(STATUS+4u)==KUI_GD_ERROR_CANCELLED);
    quiescent(GAME_VBR,0x5a4fu);
}
#endif
#if KUI_NATIVE_GD_PIO_STREAM
/* Separate authored CPU address spaces: a numeric0c process-slot buffer
 * deliberately has different bytes from its physical main-RAM alias. */
#define PIO_BANK_BYTES 0x30000u
static const uint32_t pio_bases[]={0x0e900000u,0xc0900000u,0x0c010000u};
static uint8_t pio_memory[3][PIO_BANK_BYTES];
#define PIO_PARAM (0x0e900000u+0x100u)
#define PIO_STATUS (0xc0900000u+0x200u)
#define PIO_LEFT (0x0c010000u+0x300u)
#define PIO_CALLBACK 0x01cf5724u
#define PIO_ARGUMENT (0x0e900000u+0x400u)
static uint8_t *pio_pointer(uint32_t address,uint32_t bytes) {
    for(unsigned i=0u;i<3u;i++)
        if(address>=pio_bases[i] && address-pio_bases[i]<PIO_BANK_BYTES &&
           bytes<=PIO_BANK_BYTES-(address-pio_bases[i]))
            return pio_memory[i]+address-pio_bases[i];
    return NULL;
}
static uint8_t *pio_map(void *context,uint32_t address,uint32_t bytes,int writing) {
    /* Admission of an authored executable callback page only. This harness
     * never executes the registered callback; the real SH return veneer is
     * checked separately. No original retail instructions are embedded. */
    static uint8_t callback_page[2];
    if(address==PIO_CALLBACK && bytes==2u && writing==0)return callback_page;
    if(address>=BEGIN && address<END && bytes<=END-address)
        return map(context,address,bytes,writing);
    uint8_t *p=pio_pointer(address,bytes);
    if(!p)return NULL;
    if(writing==KUI_RETAIL_MAP_VALIDATE)return pio_memory[0]+PIO_BANK_BYTES;
    if(writing==1) {CHECK(!hw.frozen);++hw.writes;}
    return p;
}
static void pio_put(uint32_t address,uint32_t value) {
    uint8_t *p=pio_pointer(address,4u);CHECK(p!=NULL);
    for(unsigned i=0u;i<4u;i++)p[i]=(uint8_t)(value>>(8u*i));
}
static uint32_t pio_get(uint32_t address) {
    const uint8_t *p=pio_pointer(address,4u);uint32_t value=0u;CHECK(p!=NULL);
    for(unsigned i=0u;i<4u;i++)value|=(uint32_t)p[i]<<(8u*i);
    return value;
}
static void setup_pio(bool cooked,bool scattered) {
    setup_foreground(cooked,scattered);owner_phase(6u,GAME_VBR,0x5a4fu);
    memset(pio_memory,0xa5,sizeof(pio_memory));
    R.shared.service.ops.map=pio_map;R.shared.service.cpu_mmu=1u;
}
static uint32_t pio_events(void) {
    uint32_t events=kui_retail_gd_native_dma_events(&R.shared.service);
    CHECK(!(events&~KUI_RETAIL_GD_IRQ_DRIVE));
    if(events)++pio_events20;
    CHECK(!kui_retail_gd_native_dma_events(&R.shared.service));
    return events;
}
static int32_t pio_request(uint32_t lba,unsigned sectors) {
    unsigned acquired=hw.acquires,released=bus_releases,started=card.cmd18;
    pio_put(PIO_PARAM,lba+150u);pio_put(PIO_PARAM+4u,sectors);
    pio_put(PIO_PARAM+8u,0x12345u);
    int32_t token=gd(KUI_GD_REQUEST,KUI_RETAIL_GD_PIOREAD_STREAM,PIO_PARAM);
    CHECK(token>0 && R.shared.service.pending && !R.engine.active && !R.engine.opened);
    CHECK(hw.acquires==acquired && bus_releases==released && card.cmd18==started && !bus_owned);
    CHECK(gd(KUI_GD_CHECK,(uint32_t)token,PIO_STATUS)==KUI_RETAIL_GD_STREAMING);
    CHECK(!pio_get(PIO_STATUS+8u) && !pio_get(PIO_STATUS+4u) && !pio_events());
    CHECK(!gd(KUI_GD_PIO_CALLBACK,PIO_CALLBACK,PIO_ARGUMENT));
    CHECK(R.shared.service.pio_callback==PIO_CALLBACK &&
        R.shared.service.pio_argument==PIO_ARGUMENT && !R.shared.service.callback_due);
    ++pio_registered;++pio_streams;return token;
}
static void pio_clear(void) {
    CHECK(!gd(KUI_GD_PIO_CALLBACK,0u,0u));
    CHECK(!R.shared.service.pio_callback && !R.shared.service.pio_argument &&
        !R.shared.service.callback_due);++pio_cleared;
}
static uint32_t pio_offer(int32_t token) {
    pio_put(PIO_LEFT,UINT32_MAX);
    CHECK(!gd(KUI_GD_PIO_CHECK,(uint32_t)token,PIO_LEFT));
    return pio_get(PIO_LEFT);
}
static void pio_guards(uint32_t destination,uint32_t bytes) {
    const uint8_t *p=pio_pointer(destination-32u,bytes+64u);CHECK(p!=NULL);
    for(unsigned i=0u;i<32u;i++) {
        CHECK(p[i]==0xa5u && p[32u+bytes+i]==0xa5u);
    }
    if(destination>=pio_bases[2] && destination-pio_bases[2]<PIO_BANK_BYTES)
        for(unsigned i=0u;i<bytes+64u;i++)
            CHECK(ram[destination-pio_bases[2]-32u+i]==0xa5u);
}
static void pio_piece(int32_t token,uint32_t destination,uint32_t bytes,
    uint32_t offset,uint32_t total) {
    uint32_t offered=pio_offer(token);
    uint32_t left=total-offset,ceiling=2u*R.shared.service.sector_bytes;
    CHECK(offered==(left<ceiling?left:ceiling) && offered>=bytes);
    uint8_t *p=pio_pointer(destination-32u,bytes+64u);CHECK(p!=NULL);
    memset(p,0xa5u,bytes+64u);
    pio_put(PIO_PARAM,destination);pio_put(PIO_PARAM+4u,bytes);
    CHECK(!gd(KUI_GD_PIO_TRANSFER,(uint32_t)token,PIO_PARAM));
    CHECK(!R.engine.active && R.shared.service.completed_bytes==offset+bytes);
    CHECK(!memcmp(pio_pointer(destination,bytes),expected+offset,bytes));
    pio_guards(destination,bytes);vectors_unchanged();
    CHECK(R.shared.service.callback_due==1u && R.shared.service.pio_callback==PIO_CALLBACK);
    CHECK(pio_events()==KUI_RETAIL_GD_IRQ_DRIVE);
    /* The C model cannot execute the resident's outside-lock return veneer.
     * Leave due set through EXEC, then model its consumer only after the
     * original caller could have advanced its scatter entry after FN12. */
    CHECK(!gd(KUI_GD_EXEC,0u,0u) && R.shared.service.callback_due==1u);
    R.shared.service.callback_due=0u;
    CHECK(gd(KUI_GD_CHECK,(uint32_t)token,PIO_STATUS)==
        (offset+bytes==total?KUI_GD_COMPLETED:KUI_RETAIL_GD_STREAMING));
    CHECK(pio_get(PIO_STATUS+8u)==offset+bytes && !pio_get(PIO_STATUS+4u));
    quiescent(GAME_VBR,0x5a4fu);
    ++pio_pieces;if(bytes%R.shared.service.sector_bytes)++pio_partials;
}
static void pio_scatter_sequences(void) {
    for(unsigned cooked=0u;cooked<2u;cooked++)for(unsigned scattered=0u;scattered<2u;scattered++)
    for(unsigned raw=0u;raw<2u;raw++) {
        setup_pio(cooked!=0u,scattered!=0u);mode(raw?2352u:2048u);
        uint32_t lba=raw&&cooked?45300u:45298u,count=raw&&cooked?2u:4u;
        uint32_t total=count*R.shared.service.sector_bytes;
        CHECK(kui_retail_image_read(&reference,lba,count,
            raw?KUI_GAME_SECTOR_RAW:KUI_GAME_SECTOR_MODE1,expected,sizeof(expected))==KUI_GAME_OK);
        uint32_t parts[6]={2u,2050u,30u,4096u,2u,total-6180u};
        unsigned pieces=6u;
        if(total<6182u) {pieces=4u;parts[3]=total-2082u;}
        int32_t token=pio_request(lba,count);uint32_t offset=0u;
        unsigned acquired=hw.acquires,released=bus_releases;
        for(unsigned i=0u;i<pieces;i++) {
            uint32_t destination=pio_bases[i%3u]+0x1000u+i*0x5000u+2u;
            pio_piece(token,destination,parts[i],offset,total);offset+=parts[i];
        }
        CHECK(offset==total && hw.acquires==acquired+pieces && bus_releases==released+pieces);
        CHECK(!pio_offer(token));pio_clear();
        CHECK(gd(KUI_GD_CHECK,(uint32_t)token,PIO_STATUS)==KUI_GD_NOT_FOUND);
        ++verified;
    }
}
static void pio_virtual_ordinary(void) {
    for(unsigned bank=0u;bank<3u;bank++)for(unsigned cooked=0u;cooked<2u;cooked++) {
        setup_pio(cooked!=0u,true);
        uint32_t destination=pio_bases[bank]+0x10002u;
        CHECK(kui_retail_image_read(&reference,45298u,4u,KUI_GAME_SECTOR_MODE1,
            expected,sizeof(expected))==KUI_GAME_OK);
        pio_put(PIO_PARAM,45298u+150u);pio_put(PIO_PARAM+4u,4u);
        /* The actual driver passes the queued-request FAD in word3 even
         * for ordinary reads. It is an opaque hint in this AT1 profile. */
        pio_put(PIO_PARAM+8u,destination);pio_put(PIO_PARAM+12u,0x45123u+bank);
        int32_t token=gd(KUI_GD_REQUEST,KUI_GD_PIOREAD,PIO_PARAM);CHECK(token>0);
        CHECK(finish_foreground(token)==KUI_GD_COMPLETED);
        CHECK(!memcmp(pio_pointer(destination,8192u),expected,8192u));
        pio_guards(destination,8192u);quiescent(GAME_VBR,0x5a4fu);
        ++verified;++pio_virtual_reads;
    }
}
static void ordinary_dma_hint_and_at0_refusal(void) {
    setup_pio(true,true);
    const uint32_t lba=45298u,count=4u,destination=OUTPUT+0x8000u;
    CHECK(kui_retail_image_read(&reference,lba,count,KUI_GAME_SECTOR_MODE1,
        expected,sizeof(expected))==KUI_GAME_OK);
    pio_put(PIO_PARAM,lba+150u);pio_put(PIO_PARAM+4u,count);
    pio_put(PIO_PARAM+8u,destination&0x1fffffffu);pio_put(PIO_PARAM+12u,0x45234u);
    int32_t token=gd(KUI_GD_REQUEST,KUI_GD_DMAREAD,PIO_PARAM);CHECK(token>0);
    CHECK(finish_foreground(token)==KUI_GD_COMPLETED);
    CHECK(!memcmp(ram+destination-BEGIN,expected,8192u));
    dma_guards(destination,8192u);
    /* A CPU virtual process-slot mapping with the same numeric0c address
     * must retain its distinct bytes while physical DMA writes main RAM. */
    const uint8_t *virtual_alias=pio_pointer(destination&0x1fffffffu,8192u);
    CHECK(virtual_alias!=NULL);
    for(unsigned i=0u;i<8192u;i++)CHECK(virtual_alias[i]==0xa5u);
    quiescent(GAME_VBR,0x5a4fu);++verified;

    /* With AT clear, isolate the word3 contract using valid P1 packets and
     * destinations: rejection must precede storage acquisition or output. */
    setup_pio(true,true);R.shared.service.cpu_mmu=0u;
    put(PARAM,lba+150u);put(PARAM+4u,count);
    put(PARAM+8u,destination);put(PARAM+12u,0x45234u);
    unsigned acquired=hw.acquires,released=bus_releases,writes=hw.writes;
    CHECK(!gd(KUI_GD_REQUEST,KUI_GD_PIOREAD,PARAM));
    CHECK(!gd(KUI_GD_REQUEST,KUI_GD_DMAREAD,PARAM));
    CHECK(!R.shared.service.command && !R.shared.service.pending && !R.engine.active);
    CHECK(hw.acquires==acquired && bus_releases==released && hw.writes==writes);
    for(unsigned i=0u;i<8192u;i++)CHECK(ram[destination-BEGIN+i]==0xa5u);
    quiescent(GAME_VBR,0x5a4fu);
}
static void pio_refusal_and_abort(void) {
    setup_pio(true,true);int32_t token=pio_request(45000u,4u);
    unsigned acquired=hw.acquires,writes=hw.writes;
    static const uint32_t lengths[]={0u,4098u,8194u,2u};
    for(unsigned i=0u;i<4u;i++) {
        pio_put(PIO_PARAM,pio_bases[0]+0x10002u+(i==3u));
        pio_put(PIO_PARAM+4u,lengths[i]);
        CHECK(gd(KUI_GD_PIO_TRANSFER,(uint32_t)token,PIO_PARAM)==-1);
        CHECK(hw.acquires==acquired && hw.writes==writes &&
            !R.shared.service.completed_bytes && !R.shared.service.callback_due && !pio_events());
    }
    pio_put(PIO_PARAM,pio_bases[0]+0x10002u);pio_put(PIO_PARAM+4u,2u);
    CHECK(gd(KUI_GD_PIO_TRANSFER,(uint32_t)token+1u,PIO_PARAM)==-1);
    CHECK(hw.acquires==acquired && hw.writes==writes);
    CHECK(!pio_offer(token+1) && hw.acquires==acquired);
    CHECK(kui_retail_image_read(&reference,45000u,4u,KUI_GAME_SECTOR_MODE1,
        expected,sizeof(expected))==KUI_GAME_OK);
    pio_piece(token,pio_bases[0]+0x10002u,2u,0u,8192u);
    CHECK(!gd(KUI_GD_ABORT,(uint32_t)token,0u));
    CHECK(pio_events()==KUI_RETAIL_GD_IRQ_DRIVE);
    CHECK(gd(KUI_GD_CHECK,(uint32_t)token,PIO_STATUS)==KUI_GD_FAILED);
    CHECK(pio_get(PIO_STATUS+4u)==KUI_GD_ERROR_CANCELLED && pio_get(PIO_STATUS+8u)==2u);
    pio_clear();quiescent(GAME_VBR,0x5a4fu);++pio_cancels;
}
static void pio_crc_failures(void) {
    for(unsigned fatal=0u;fatal<2u;fatal++) {
        setup_pio(true,true);
        CHECK(kui_retail_image_read(&reference,45000u,2u,KUI_GAME_SECTOR_MODE1,
            expected,sizeof(expected))==KUI_GAME_OK);
        int32_t token=pio_request(45000u,2u);
        pio_piece(token,pio_bases[0]+0x10002u,2u,0u,4096u);
        card.corrupt_lba=manifest.slots[manifest.slots[2].track.first_extent].extent.card_lba+5u;
        card.corrupt_count=fatal?1000u:2u;
        uint32_t destination=pio_bases[1]+0x10002u,bytes=4094u;
        pio_put(PIO_PARAM,destination);pio_put(PIO_PARAM+4u,bytes);
        CHECK(!gd(KUI_GD_PIO_TRANSFER,(uint32_t)token,PIO_PARAM));
        CHECK(!R.engine.active && R.shared.service.callback_due==1u);
        CHECK(pio_events()==KUI_RETAIL_GD_IRQ_DRIVE);corruptions_observed(fatal);
        CHECK(gd(KUI_GD_CHECK,(uint32_t)token,PIO_STATUS)==(fatal?KUI_GD_FAILED:KUI_GD_COMPLETED));
        uint32_t delivered=pio_get(PIO_STATUS+8u)-2u;
        CHECK(delivered<=bytes && !memcmp(pio_pointer(destination,bytes),expected+2u,delivered));
        pio_guards(destination,bytes);
        if(fatal) {CHECK(pio_get(PIO_STATUS+4u)==KUI_GD_ERROR_IO);++pio_errors;}
        else {CHECK(delivered==bytes);++verified;++pio_pieces;++pio_partials;}
        pio_clear();quiescent(GAME_VBR,0x5a4fu);
    }
}
static void pio_bus_refusal(void) {
    setup_pio(true,true);int32_t token=pio_request(45000u,2u);
    unsigned acquired=hw.acquires,released=bus_releases,writes=hw.writes;
    hw.busy_bus=true;pio_put(PIO_PARAM,pio_bases[0]+0x10002u);pio_put(PIO_PARAM+4u,2u);
    CHECK(!gd(KUI_GD_PIO_TRANSFER,(uint32_t)token,PIO_PARAM));
    CHECK(!R.engine.active && !R.engine.opened && !R.shared.service.pending);
    CHECK(hw.acquires==acquired+1u && bus_releases==released && hw.writes==writes);
    CHECK(!R.shared.port.fault && !R.shared.port.acquired && !bus_owned);
    CHECK(pio_events()==KUI_RETAIL_GD_IRQ_DRIVE);
    CHECK(gd(KUI_GD_CHECK,(uint32_t)token,PIO_STATUS)==KUI_GD_FAILED);
    CHECK(pio_get(PIO_STATUS+4u)==KUI_GD_ERROR_IO && !pio_get(PIO_STATUS+8u));
    pio_clear();vectors_unchanged();++pio_errors;
    hw.busy_bus=false;
}
#endif
static void failed_stop_quarantines(void) {
    setup_foreground(true,true);owner_phase(4u,GAME_VBR,0x5a9fu);
#if KUI_NATIVE_GD_PIO_STREAM
    R.shared.service.ops.map=pio_map;R.shared.service.cpu_mmu=1u;
    memset(pio_memory,0xa5,sizeof(pio_memory));
    int32_t token=pio_request(45001u,2u);
    pio_put(PIO_PARAM,pio_bases[0]+0x10002u);pio_put(PIO_PARAM+4u,4096u);inject_stop_failure=true;
    CHECK(!gd(KUI_GD_PIO_TRANSFER,(uint32_t)token,PIO_PARAM));
#elif KUI_NATIVE_GD_DMA_STREAM
    int32_t token=dma_request(45001u,64u);
    put(PARAM,OUTPUT);put(PARAM+4u,64u*2048u);inject_stop_failure=true;
    CHECK(!gd(KUI_GD_DMA_TRANSFER,(uint32_t)token,PARAM));
#elif KUI_NATIVE_GD_LEGACY_STREAM
    int32_t token=legacy_request(45001u,64u);
    legacy_transfer(token,OUTPUT,64u);inject_stop_failure=true;
#else
    int32_t token=request(45001u,64u,OUTPUT);inject_stop_failure=true;
#endif
    for(unsigned round=0u;R.shared.service.pending && round<3000u;round++) {
        elapse();CHECK(!gd(KUI_GD_EXEC,0u,0u));
    }
    CHECK(!R.shared.service.pending && stop_failures==1u);
    CHECK(R.shared.port.fault && R.shared.port.acquired && bus_owned && R.engine.opened);
    CHECK(!R.engine.active && !R.engine.hooked && !R.engine.release.armed);
#if KUI_NATIVE_GD_LEGACY_STREAM
    CHECK(gd(KUI_GD_ABORT,(uint32_t)token,0u)==-1);
    CHECK(R.shared.port.fault && R.shared.service.error==KUI_GD_ERROR_IO);
#if KUI_NATIVE_GD_PIO_STREAM
    CHECK(pio_events()==KUI_RETAIL_GD_IRQ_DRIVE);++pio_errors;
#elif KUI_NATIVE_GD_DMA_STREAM
    CHECK(dma_events()==(KUI_RETAIL_GD_IRQ_DMA_END|KUI_RETAIL_GD_IRQ_DRIVE));++dma_errors;
#else
    ++legacy_errors;
#endif
#endif
    CHECK(gd(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_FAILED);
    CHECK(get(STATUS+4u)==KUI_GD_ERROR_IO && !bus_releases);
    vectors_unchanged();

    /* A failed CMD12 leaves the owned stream uncertain. Do not release it
     * or allow an acquire to clear the fault and resume against new data. */
    unsigned acquired=hw.acquires,released=bus_releases,writes=hw.writes;
    uint8_t smr=m.smr,brr=m.brr,scr=m.scr,sptr=m.sptr;
    token=request(45010u,8u,OUTPUT);
    CHECK(gd(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_FAILED);
    CHECK(get(STATUS+4u)==KUI_GD_ERROR_IO && !get(STATUS+8u));
    CHECK(R.shared.port.fault && hw.acquires==acquired && bus_releases==released && hw.writes==writes);
    CHECK(m.smr==smr && m.brr==brr && m.scr==scr && m.sptr==sptr);
    CHECK(hw.vbr==GAME_VBR && hw.iprb==0x5a9fu);
    vectors_unchanged();
}
#if KUI_VC2_VMU_LATE
struct late_card_output {
    uint8_t before[16],bytes[512],after[16];
};
static void late_card_guards(const struct late_card_output *out,bool untouched) {
    for(unsigned i=0u;i<sizeof(out->before);i++)CHECK(out->before[i]==0xa5u);
    for(unsigned i=0u;i<sizeof(out->after);i++)CHECK(out->after[i]==0xa5u);
    if(untouched)for(unsigned i=0u;i<sizeof(out->bytes);i++)CHECK(out->bytes[i]==0xa5u);
}
/* Establish the same legal handoff as the late caller: a real completed GD
 * command, acknowledged by CHECK, with its stream stopped and bus released.
 * The raw operation must retain every GD field and the completed cursor. */
static void late_card_setup(void) {
    setup_foreground(true,true);owner_phase(6u,GAME_VBR,0x5a9fu);
    read_foreground(45001u,1u,GAME_VBR,0x5a9fu);
    CHECK(!R.shared.service.command && !R.shared.service.pending &&
        !R.shared.service.executing && R.shared.service.token &&
        R.engine.cursor.done==R.engine.cursor.count);
}
static void late_card_reads_exactly(void) {
    late_card_setup();
    static const uint32_t lbas[]={50u,100u,1999u,100u};
    for(unsigned i=0u;i<sizeof(lbas)/sizeof(lbas[0]);i++) {
        owner_phase(7u+i,GAME_VBR,0x5a9fu);
        const struct kui_retail_gd service=R.shared.service;
        const struct kui_retail_async engine=R.engine;
        struct late_card_output out;memset(&out,0xa5,sizeof(out));
        unsigned acquired=hw.acquires,released=bus_releases;
        unsigned started=card.cmd18,stopped=card.cmd12,writes=hw.writes;
#if !KUI_SMASH_MENU_LOADER
        unsigned dma=m.dma_starts,polled=R.shared.stream.stats.polled;
#endif
        CHECK(kui_vc2_ce_vmu_read_card(lbas[i],out.bytes)==KUI_SCI_STREAM_OK);
#if !KUI_SMASH_MENU_LOADER
        CHECK(m.dma_starts==dma && R.shared.stream.stats.polled==polled+1u);++late_card_pio_reads;
#endif
        CHECK(!memcmp(out.bytes,image_card+lbas[i]*512u,512u));
        late_card_guards(&out,false);
        CHECK(!memcmp(&service,&R.shared.service,sizeof(service)));
        CHECK(!memcmp(&engine,&R.engine,sizeof(engine)));
        CHECK(hw.acquires==acquired+1u && bus_releases==released+1u &&
            card.cmd18==started+1u && card.cmd12==stopped+1u && hw.writes==writes);
        quiescent(GAME_VBR,0x5a9fu);++late_card_reads;
    }
}
static void late_card_refuses_before_io(void) {
    late_card_setup();
    const kui_retail_async_region_type baseline=R;
    for(unsigned how=0u;how<19u;how++) {
        R=baseline;
        uint32_t lba=100u;
        struct late_card_output out;memset(&out,0xa5,sizeof(out));
        uint8_t *destination=out.bytes;
        switch(how) {
        case 0u: destination=NULL;break;
        case 1u: R.engine.manifest=NULL;break;
        case 2u: lba=manifest.partition_start-1u;break;
        case 3u: lba=manifest.partition_end;break;
        case 4u: lba=UINT32_MAX;break;
        case 5u: manifest.card_sectors=100u;break;
        case 6u: R.shared.service.initialized=0u;break;
        case 7u: R.shared.service.pending=1u;break;
        case 8u: R.shared.service.executing=1u;break;
        case 9u: R.engine.active=1u;break;
        case 10u: R.engine.in_irq=1u;break;
        case 11u: R.engine.hooked=1u;break;
        case 12u: R.engine.opened=1u;break;
        case 13u: R.engine.release.armed=1u;break;
        case 14u: R.engine.release.depth=1u;break;
        case 15u: R.shared.port.acquired=true;break;
        case 16u: R.shared.stream.state=1u;break;
        case 17u: R.shared.port.fault=true;break;
        case 18u: R.shared.port.fault=true;destination=NULL;lba=UINT32_MAX;break;
        }
        const kui_retail_async_region_type before=R;
        unsigned acquired=hw.acquires,released=bus_releases,clocks=card.clocks;
        unsigned started=card.cmd18,stopped=card.cmd12,writes=hw.writes;
        CHECK(kui_vc2_ce_vmu_read_card(lba,destination)==
            (how>=17u?KUI_SCI_STREAM_RESET:KUI_SCI_STREAM_BUSY));
        CHECK(!memcmp(&before,&R,sizeof(before)));
        CHECK(hw.acquires==acquired && bus_releases==released && card.clocks==clocks &&
            card.cmd18==started && card.cmd12==stopped && hw.writes==writes);
        late_card_guards(&out,true);vectors_unchanged();++late_card_refusals;
        manifest.card_sectors=2048u;
    }
    R=baseline;quiescent(GAME_VBR,0x5a9fu);
}
static void late_card_live_stream_refuses(void) {
    setup_foreground(true,true);owner_phase(6u,GAME_VBR,0x5a9fu);
    int32_t token=request(45001u,64u,OUTPUT);
    CHECK(R.shared.service.pending && R.engine.active && R.engine.opened &&
        R.shared.port.acquired && bus_owned);
    const kui_retail_async_region_type before=R;
    struct late_card_output out;memset(&out,0xa5,sizeof(out));
    unsigned acquired=hw.acquires,released=bus_releases,clocks=card.clocks;
    unsigned started=card.cmd18,stopped=card.cmd12,writes=hw.writes;
    CHECK(kui_vc2_ce_vmu_read_card(100u,out.bytes)==KUI_SCI_STREAM_BUSY);
    CHECK(!memcmp(&before,&R,sizeof(before)));
    CHECK(hw.acquires==acquired && bus_releases==released && card.clocks==clocks &&
        card.cmd18==started && card.cmd12==stopped && hw.writes==writes);
    late_card_guards(&out,true);++late_card_refusals;
    CHECK(finish_foreground(token)==KUI_GD_COMPLETED);
    compare(45001u,64u,OUTPUT);quiescent(GAME_VBR,0x5a9fu);
}
static void late_card_error_teardown(void) {
    for(unsigned how=0u;how<3u;how++) {
        late_card_setup();
        const struct kui_retail_gd service=R.shared.service;
        const struct kui_retail_async engine=R.engine;
        struct late_card_output out;memset(&out,0xa5,sizeof(out));
        const uint32_t lba=100u;
        if(how==0u) {card.corrupt_lba=lba;card.corrupt_count=1u;}
        else if(how==1u) {card.bad_token_lba=lba;card.bad_token_count=1u;}
        else hw.busy_bus=true;
        unsigned acquired=hw.acquires,released=bus_releases,clocks=card.clocks;
        unsigned started=card.cmd18,stopped=card.cmd12,writes=hw.writes;
#if !KUI_SMASH_MENU_LOADER
        unsigned dma=m.dma_starts;
#endif
        CHECK(kui_vc2_ce_vmu_read_card(lba,out.bytes)==
            (how==0u?KUI_SCI_STREAM_CRC:how==1u?KUI_SCI_STREAM_TOKEN:KUI_SCI_STREAM_RESET));
#if !KUI_SMASH_MENU_LOADER
        CHECK(m.dma_starts==dma);
#endif
        late_card_guards(&out,true);
        CHECK(!memcmp(&service,&R.shared.service,sizeof(service)));
        CHECK(!memcmp(&engine,&R.engine,sizeof(engine)));
        CHECK(hw.acquires==acquired+1u && hw.writes==writes && !R.shared.port.fault);
        CHECK(bus_releases==released+(how<2u) && card.cmd18==started+(how<2u) &&
            card.cmd12==stopped+(how<2u));
        if(how==2u)CHECK(card.clocks==clocks);
        quiescent(GAME_VBR,0x5a9fu);++late_card_errors;
        hw.busy_bus=false;
        CHECK(kui_vc2_ce_vmu_read_card(lba,out.bytes)==KUI_SCI_STREAM_OK);
#if !KUI_SMASH_MENU_LOADER
        CHECK(m.dma_starts==dma);++late_card_pio_reads;
#endif
        CHECK(!memcmp(out.bytes,image_card+lba*512u,512u));
        late_card_guards(&out,false);quiescent(GAME_VBR,0x5a9fu);++late_card_reads;
    }
}
/* A distinct process invocation ends at quarantine, so the fixture never
 * invents recovery or clears a production ownership fault for later tests. */
static void late_card_failed_stop(void) {
    late_card_setup();
    const struct kui_retail_gd service=R.shared.service;
    const struct kui_retail_async engine=R.engine;
    struct late_card_output out;memset(&out,0xa5,sizeof(out));
    unsigned acquired=hw.acquires,released=bus_releases,started=card.cmd18;
    inject_stop_failure=true;
    CHECK(kui_vc2_ce_vmu_read_card(100u,out.bytes)==KUI_SCI_STREAM_RESET);
    CHECK(stop_failures==1u && R.shared.port.fault && R.shared.port.acquired &&
        bus_owned && R.engine.opened && !R.engine.active);
    CHECK(hw.acquires==acquired+1u && bus_releases==released && card.cmd18==started+1u);
    CHECK(!memcmp(out.bytes,image_card+100u*512u,512u));late_card_guards(&out,false);
    struct kui_retail_gd failed_service=service;
    failed_service.error=KUI_GD_ERROR_IO;failed_service.diag.last_error=KUI_GD_ERROR_IO;
    failed_service.status=KUI_GD_FAILED;failed_service.pending=0u;
    CHECK(!memcmp(&failed_service,&R.shared.service,sizeof(failed_service)));
    struct kui_retail_async failed_engine=engine;
    failed_engine.opened=1u;failed_engine.failed=KUI_GD_ERROR_IO;failed_engine.active=0u;
    CHECK(!memcmp(&failed_engine,&R.engine,sizeof(failed_engine)));
    const kui_retail_async_region_type quarantined=R;
    acquired=hw.acquires;
    unsigned clocks=card.clocks,writes=hw.writes,stopped=card.cmd12;
    memset(&out,0xa5,sizeof(out));
    CHECK(kui_vc2_ce_vmu_read_card(101u,out.bytes)==KUI_SCI_STREAM_RESET);
    CHECK(!memcmp(&quarantined,&R,sizeof(quarantined)));
    CHECK(hw.acquires==acquired && bus_releases==released && card.clocks==clocks &&
        card.cmd12==stopped && hw.writes==writes);
    late_card_guards(&out,true);vectors_unchanged();
    printf("VC2_LATE_CARD_JSON {\"checks\":%u,\"raw_stop_failure_cases\":1,"
        "\"sticky_quarantine_cases\":1,\"hardware_executed\":false}\n",checks);
}
#endif
int main(int argc,char **argv) {
#if KUI_VC2_VMU_LATE
    if(argc==2 && !strcmp(argv[1],"--vc2-late-stop")) {late_card_failed_stop();return 0;}
    late_card_reads_exactly();late_card_refuses_before_io();late_card_live_stream_refuses();
    late_card_error_teardown();
#else
    (void)argc;(void)argv;
#endif
    repeated_phases();foreign_dma();crc_failures();bus_refusal_recovery();cancellation();
#if KUI_NATIVE_GD_LEGACY_STREAM
    legacy_piece_sequences();legacy_cancellation();legacy_crc_failures();
#endif
#if KUI_NATIVE_GD_DMA_STREAM
    dma_scatter_sequences();dma_refusal_and_abort();dma_crc_failures();dma_bus_refusal();
#endif
#if KUI_NATIVE_GD_VRAM_DMA
    vram_observed_sequence();vram_fragments();vram_crc_failures();vram_abort_and_bus_refusal();
    vram_context_and_ordinary_reads();
#endif
#if KUI_NATIVE_GD_PIO_STREAM
    pio_scatter_sequences();pio_virtual_ordinary();ordinary_dma_hint_and_at0_refusal();
    pio_refusal_and_abort();
    pio_crc_failures();pio_bus_refusal();
#endif
    failed_stop_quarantines();
#if KUI_VC2_VMU_LATE
    printf("VC2_LATE_CARD_JSON {\"checks\":%u,\"raw_card_reads\":%u,"
        "\"preflight_refusals\":%u,\"read_error_cases\":%u,\"forced_PIO_reads\":%u,\"hardware_executed\":false}\n",
        checks,late_card_reads,late_card_refusals,late_card_errors,late_card_pio_reads);
#endif
    printf("FOREGROUND_READER_JSON {\"checks\":%u,\"byte_exact_requests\":%u,"
        "\"cancel_cases\":%u,\"bus_refusal_cases\":1,\"stop_failure_cases\":%u,"
        "\"legacy_stream_cases\":%u,\"legacy_piece_transfers\":%u,\"legacy_cancel_cases\":%u,"
        "\"legacy_error_cases\":%u,\"legacy_busy_polls\":%u,\"legacy_idle_polls\":%u,"
        "\"dma_stream_cases\":%u,\"dma_piece_transfers\":%u,\"dma_partial_pieces\":%u,"
        "\"dma_error_cases\":%u,\"dma_cancel_cases\":%u,\"dma_end_events\":%u,\"dma_drive_events\":%u,"
        "\"pio_stream_cases\":%u,\"pio_piece_transfers\":%u,\"pio_partial_pieces\":%u,"
        "\"pio_error_cases\":%u,\"pio_cancel_cases\":%u,\"pio_drive_events\":%u,"
        "\"pio_callbacks_registered\":%u,\"pio_callbacks_cleared\":%u,\"pio_virtual_direct_reads\":%u,"
        "\"vram_dma_cases\":%u,\"vram_dma_bytes\":%u,\"vram_odd_fragments\":%u,"
        "\"vram_crc_cases\":%u,\"vram_abort_cases\":%u,\"vram_bus_refusal_cases\":%u,"
        "\"vram_same_offset_ram_writes\":%u,"
        "\"vram_cpu_virtual_reads\":%u,\"vram_refusal_cases\":%u,"
        "\"callback_reads\":%u,\"publications\":%u,"
        "\"vbr_writes\":%u,\"hardware_executed\":false}\n",
        checks,verified,cancels,stop_failures,legacy_streams,legacy_pieces,legacy_cancels,
        legacy_errors,legacy_busy_polls,legacy_idle_polls,
        dma_streams,dma_pieces,dma_partials,dma_errors,dma_cancels,dma_events21,dma_events20,
        pio_streams,pio_pieces,pio_partials,pio_errors,pio_cancels,pio_events20,
        pio_registered,pio_cleared,pio_virtual_reads,
        vram_cases,vram_bytes,vram_odd_fragments,vram_crc_cases,vram_aborts,vram_bus_refusals,
        vram_same_ram_writes,
        vram_virtual_reads,vram_refusals,
        callback_reads,publications,hw.vbr_sets);
    return 0;
}
