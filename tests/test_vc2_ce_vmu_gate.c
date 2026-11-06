/* SPDX-License-Identifier: GPL-3.0-only */
/* Exercise the actual trusted low gate with authored reader/MMIO state.
 * No original game data, instructions, or hardware timing is modeled here. */
#define KUI_RETAIL_ASYNC_TEST 1
#define KUI_NATIVE_OCRAM_BOOTSTRAP 1
#define KUI_NATIVE_GD_LEGACY_STREAM 1
#include "../src/loader/retail_async.h"
#include "kui/native_ocram_diagnostic.h"
#include "kui/vc2_ce_vmu_handoff.h"
#if KUI_VC2_VMU_LATE
#include "kui/vc2_ce_vmu_late.h"
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct kui_retail_async_region kui_retail_async_region;
static union {
    struct kui_retail_manifest map;
    struct {uint8_t prefix[68];volatile struct kui_native_ocram_diagnostic trace;} report;
} low_state;
_Static_assert(offsetof(struct kui_retail_manifest,title)==68u,"Host trace/map source relation");
#define kui_native_ocram_trace low_state.report.trace
#define reader (kui_retail_async_region.engine)
#define service (kui_retail_async_region.shared.service)
#define RETAIL_MANIFEST (&low_state.map)

struct hardware_word {uint32_t address,value;};
static struct hardware_word hardware[32];
static unsigned hardware_count,hardware_reads,purges,crcs,inits;
static uint32_t initializer_result,expected_blob_crc;
static bool dirty_alias,dirty_tail;
static _Alignas(32) uint32_t prepared_storage[KUI_VC2_VMU_RESERVE_BYTES/4u];
#define prepared ((uint8_t *)(void *)prepared_storage)
#define KUI_VC2_VMU_BLOB_BYTES 0x5ae4u
_Static_assert(KUI_VC2_VMU_BLOB_BYTES==23268u &&
    KUI_VC2_VMU_BLOB_BYTES<KUI_VC2_VMU_RESERVE_BYTES &&
    !(KUI_VC2_VMU_BLOB_BYTES&3u),"Immutable fixture length");
#if KUI_VC2_VMU_LATE
static uint8_t prepared_reference[KUI_VC2_VMU_RESERVE_BYTES];
static uint32_t expected_prepared_crc,loaded_blocks,load_fail_index;
static unsigned loads,raw_purges,raw_crcs;
static _Alignas(32) uint32_t raw_storage[512u/4u];
#define raw_island ((uint8_t *)(void *)raw_storage)
static bool dirty_raw_first,dirty_raw_last;
#define LATE_READ_ERROR UINT32_C(0xdeaf1234)
#define LATE_READ_WHY 0x20fu
#endif

static uint32_t *hardware_value(uint32_t address) {
    for(unsigned i=0u;i<hardware_count;++i)
        if(hardware[i].address==address)return &hardware[i].value;
    assert(hardware_count<sizeof(hardware)/sizeof(hardware[0]));
    hardware[hardware_count]=(struct hardware_word){address,0u};
    return &hardware[hardware_count++].value;
}
static uint32_t read32(uint32_t address) {
    if(address==KUI_VC2_VMU_RESERVE_P2) {
        uint32_t word;memcpy(&word,prepared,sizeof(word));return word;
    }
    ++hardware_reads;return *hardware_value(address);
}
static void purge(uint32_t address) {
    assert(address==KUI_VC2_VMU_RESERVE_P1+32u*purges);
    assert(address<KUI_VC2_VMU_RESERVE_END);
    ++purges;
    /* An authored dirty native cache line becomes visible at writeback.
     * CRC must observe this corruption instead of executing stale P2 code. */
    if(dirty_alias && address==KUI_VC2_VMU_RESERVE_P1)prepared[0]^=1u;
    if(dirty_tail && address==KUI_VC2_VMU_RESERVE_END-32u)
        prepared[sizeof(prepared_storage)-1u]^=1u;
}
static uint32_t blob_crc(void) {
    assert(purges==KUI_VC2_VMU_RESERVE_BYTES/32u);
#if KUI_VC2_VMU_LATE
    assert(loads==1u && loaded_blocks==KUI_VC2_VMU_LATE_BLOCKS && raw_purges==1u && raw_crcs==1u);
    ++crcs;return kui_vc2_ce_vmu_crc32_words(prepared_storage,
        KUI_VC2_VMU_RESERVE_BYTES/sizeof(prepared_storage[0]));
#else
    ++crcs;return kui_vc2_ce_vmu_crc32_words(prepared_storage,
        KUI_VC2_VMU_BLOB_BYTES/sizeof(prepared_storage[0]));
#endif
}
#if KUI_VC2_VMU_LATE
static void raw_purge(void) {
    assert(!raw_purges && !raw_crcs && !loads && !crcs && !inits);
    ++raw_purges;
    if(dirty_raw_first)raw_island[0]^=1u;
    if(dirty_raw_last)raw_island[sizeof(raw_storage)-1u]^=1u;
}
static uint32_t raw_crc(void) {
    assert(raw_purges==1u && !raw_crcs && !loads && !crcs && !inits);
    ++raw_crcs;
    return kui_vc2_ce_vmu_crc32_words(raw_storage,sizeof(raw_storage)/4u);
}
static uint32_t raw_card(uint32_t lba,uint8_t out[512]) {
    assert(loads==1u && purges==KUI_VC2_VMU_RESERVE_BYTES/32u && !crcs && !inits);
    assert(raw_purges==1u && raw_crcs==1u);
    assert(loaded_blocks<KUI_VC2_VMU_LATE_BLOCKS);
    const uint32_t block=loaded_blocks++;
    const uint32_t split=low_state.map.gdi_crc32;
    assert(lba==(block<split?low_state.map.boot_lba+block:low_state.map.boot_bytes+block-split));
    assert(out==prepared+block*512u);
    if(block==load_fail_index) {memset(out,0xdd,512u);return LATE_READ_ERROR;}
    memcpy(out,prepared_reference+block*512u,512u);return 0u;
}
static uint32_t load_prepared(uint32_t *failed_lba) {
    assert(!loads && !crcs && !inits);
    assert(raw_purges==1u && raw_crcs==1u);
    ++loads;
    return kui_vc2_vmu_late_read(low_state.map.boot_lba,low_state.map.gdi_crc32,
        low_state.map.boot_bytes,prepared,raw_card,failed_lba);
}
#define KUI_VC2_VMU_PREPARED_CRC32 expected_prepared_crc
#define KUI_VC2_VMU_GATE_LOAD(failed_lba) load_prepared(failed_lba)
#define KUI_VC2_VMU_GATE_RAW_PURGE() raw_purge()
#define KUI_VC2_VMU_GATE_RAW_CRC() raw_crc()
#endif
static uint32_t initialize(void *region,uint32_t token,volatile void *trace) {
    assert(crcs==1u && purges==KUI_VC2_VMU_RESERVE_BYTES/32u);
    assert(region==&kui_retail_async_region);
    assert(trace==&kui_native_ocram_trace);
    ++inits;
    uint32_t result=kui_vc2_ce_vmu_service_validate(region,token);
    if(!result)result=initializer_result;
    return result?kui_vc2_ce_vmu_service_refuse(region,trace,result):0u;
}
#define KUI_VC2_VMU_BLOB_CRC32 expected_blob_crc
#define KUI_VC2_VMU_GATE_READ32(address) read32(address)
#define KUI_VC2_VMU_GATE_PURGE(address) purge(address)
#define KUI_VC2_VMU_GATE_BLOB_CRC() blob_crc()
#define KUI_VC2_VMU_GATE_PREPARED_CRC() blob_crc()
#define KUI_VC2_VMU_GATE_INITIALIZE(region,token,trace) initialize(region,token,trace)
#include "../src/loader/vc2_ce_vmu_gate.inc"

static void fresh(void) {
    memset(&kui_retail_async_region,0,sizeof(kui_retail_async_region));
    memset(&low_state,0,sizeof(low_state));
    memset(hardware,0,sizeof(hardware));hardware_count=hardware_reads=0u;
    purges=crcs=inits=0u;initializer_result=0u;
    dirty_alias=dirty_tail=false;
    for(unsigned i=0u;i<KUI_VC2_VMU_BLOB_BYTES;++i)prepared[i]=(uint8_t)(i*13u+7u);
    memset(prepared+KUI_VC2_VMU_BLOB_BYTES,0,
        sizeof(prepared_storage)-KUI_VC2_VMU_BLOB_BYTES);
    expected_blob_crc=kui_vc2_ce_vmu_crc32(prepared,KUI_VC2_VMU_BLOB_BYTES);
#if KUI_VC2_VMU_LATE
    memcpy(prepared_reference,prepared,sizeof(prepared_reference));
    expected_prepared_crc=kui_vc2_ce_vmu_crc32(prepared_reference,sizeof(prepared_reference));
    loads=loaded_blocks=0u;load_fail_index=UINT32_MAX;
    raw_purges=raw_crcs=0u;dirty_raw_first=dirty_raw_last=false;
    for(unsigned i=0u;i<sizeof(raw_storage);++i)raw_island[i]=(uint8_t)(i*17u+(i>>8)*7u+3u);
    low_state.map.ip_crc32=kui_vc2_ce_vmu_crc32(raw_island,sizeof(raw_storage));
    low_state.map.boot_lba=100u;low_state.map.gdi_crc32=48u;low_state.map.boot_bytes=1000u;
#endif
    kui_native_ocram_trace.entry[5]=KUI_VC2_VMU_ABORT_PR;
    kui_native_ocram_trace.entry[7]=UINT32_C(0x700000f0);
    service.command=KUI_RETAIL_GD_DMAREAD_LEGACY_STREAM;
    service.token=0x1fu;service.area=KUI_VC2_VMU_KERNEL_FAD;
    service.initialized=1u;service.sector_bytes=2048u;service.destination=0x0c010000u;
    service.request_bytes=KUI_VC2_VMU_KERNEL_BYTES;
    service.completed_bytes=KUI_VC2_VMU_BODY_BYTES;
    service.outputs[0]=0x800u;
    service.status=KUI_RETAIL_GD_LEGACY_STREAMING;
    reader.manifest=&low_state.map;
    service.tracks=low_state.map.slots;
    *hardware_value(UINT32_C(0xff00001c))=0x121u;
}
static void expect_service(unsigned why) {
    struct kui_retail_async_region before=kui_retail_async_region;
    const struct retail_display_state *display=kui_vc2_ce_vmu_saved_display();
    uint32_t snapshot[16],after[16];
    kui_vc2_ce_vmu_service_snapshot(snapshot);
    assert(kui_vc2_ce_vmu_service_validate(&kui_retail_async_region,service.token)==why);
    assert(!memcmp(&before,&kui_retail_async_region,sizeof(before)));
    if(why) {
        assert(kui_vc2_ce_vmu_saved_display()==display);
        kui_vc2_ce_vmu_service_snapshot(after);
        assert(!memcmp(snapshot,after,sizeof(snapshot)));
    }
}
static void expect(unsigned why) {
    struct kui_retail_async_region before=kui_retail_async_region;
    uint8_t previous[sizeof(prepared_storage)];memcpy(previous,prepared,sizeof(previous));
    struct hardware_word old_hardware[32];memcpy(old_hardware,hardware,sizeof(hardware));
    unsigned old_count=hardware_count;
    uint32_t result=vc2_ce_vmu_gate(service.token,KUI_GD_ABORT);
    assert(result==why);
    assert(!memcmp(&before,&kui_retail_async_region,sizeof(before)));
    /* MMIO reads may add modeled zero registers, but never write one. */
    for(unsigned i=0u;i<old_count;++i)
        assert(*hardware_value(old_hardware[i].address)==old_hardware[i].value);
#if KUI_VC2_VMU_LATE
    if(!loads && !dirty_alias && !dirty_tail)assert(!memcmp(previous,prepared,sizeof(previous)));
#else
    if(!dirty_alias && !dirty_tail)assert(!memcmp(previous,prepared,sizeof(previous)));
#endif
    if(why)assert(kui_native_ocram_trace.entry[15]==why);
    if(why!=KUI_VC2_VMU_GATE_INITIALIZER)assert(inits==(why?0u:1u));
    if(why && why!=KUI_VC2_VMU_GATE_PREPARED_CRC && why!=KUI_VC2_VMU_GATE_INITIALIZER
#if KUI_VC2_VMU_LATE
       && why!=LATE_READ_WHY && why!=KUI_VC2_VMU_GATE_RAW_CODE
#endif
       )
        assert(!purges && !crcs && !inits);
}
static void expect_hardware(uint32_t address,uint32_t value) {
    fresh();*hardware_value(address)=value;
    expect(KUI_VC2_VMU_GATE_HARDWARE);
    assert(kui_native_ocram_trace.entry[3]==address);
    assert(kui_native_ocram_trace.entry[14]==value);
}
static void valid_and_unrelated_calls(void) {
    const uint32_t modes[]={0x105u,0x121u,0u};
    for(unsigned i=0u;i<sizeof(modes)/sizeof(modes[0]);++i) {
        fresh();*hardware_value(UINT32_C(0xff00001c))=modes[i];expect(0u);
        assert(hardware_reads && purges==1536u && crcs==1u && inits==1u);
        assert(!kui_native_ocram_trace.entry[14] && !kui_native_ocram_trace.entry[15]);
        assert(kui_vc2_ce_vmu_saved_display()==&kui_retail_async_region.shared.display);
        uint32_t snapshot[16];kui_vc2_ce_vmu_service_snapshot(snapshot);
        assert(snapshot[0]==service.token && snapshot[1]==service.command &&
            snapshot[2]==KUI_VC2_VMU_KERNEL_FAD && snapshot[3]==KUI_VC2_VMU_KERNEL_BYTES &&
            snapshot[4]==KUI_VC2_VMU_BODY_BYTES && snapshot[5]==0x800u &&
            !snapshot[6] && snapshot[7]==KUI_RETAIL_GD_LEGACY_STREAMING &&
            snapshot[14]==1u && snapshot[15]==(uint32_t)(uintptr_t)&kui_retail_async_region);
    }
    fresh();
    assert(!vc2_ce_vmu_gate(service.token,KUI_GD_CHECK));
    assert(!hardware_reads && !purges && !crcs && !inits);
    fresh();kui_native_ocram_trace.entry[5]^=2u;
    assert(!vc2_ce_vmu_gate(service.token,KUI_GD_ABORT));
    assert(!hardware_reads && !purges && !crcs && !inits);
}
static void caller_and_completed_stream(void) {
    for(unsigned mask=0u;mask<15u;++mask) {
        fresh();kui_native_ocram_trace.entry[7]=0x70000000u|(mask<<4);
        expect(KUI_VC2_VMU_GATE_CALLER_SR);
    }
    expect_hardware(UINT32_C(0xff000010),1u);
    expect_hardware(UINT32_C(0xff00001c),0x185u);
    expect_hardware(UINT32_C(0xff00001c),0x1a1u);
    fresh();
    assert(kui_vc2_ce_vmu_service_validate(NULL,service.token)==KUI_VC2_VMU_GATE_STREAM);
    assert(kui_vc2_ce_vmu_service_validate(&kui_retail_async_region,0u)==KUI_VC2_VMU_GATE_STREAM);
    assert(kui_vc2_ce_vmu_service_validate(&kui_retail_async_region,service.token+1u)==KUI_VC2_VMU_GATE_STREAM);
#define STREAM_CHANGE(change) do {fresh();change;expect_service(KUI_VC2_VMU_GATE_STREAM);} while(0)
    STREAM_CHANGE(service.initialized=0u);
    STREAM_CHANGE(service.ops.context=(void *)(uintptr_t)1u);
    STREAM_CHANGE(service.ops.context=(void *)(uintptr_t)0x1234u);
    STREAM_CHANGE(service.command=KUI_GD_DMAREAD);
    STREAM_CHANGE(service.error=KUI_GD_ERROR_IO);
    STREAM_CHANGE(++service.area);
    STREAM_CHANGE(service.sector_bytes=2352u);
    STREAM_CHANGE(--service.request_bytes);
    STREAM_CHANGE(--service.completed_bytes);
    STREAM_CHANGE(--service.outputs[0]);
    STREAM_CHANGE(service.completed_bytes=UINT32_MAX;service.outputs[0]=KUI_VC2_VMU_KERNEL_BYTES+1u);
    STREAM_CHANGE(service.destination=0x0c020000u);
    STREAM_CHANGE(service.count=1u);
    STREAM_CHANGE(service.pending=1u);
    STREAM_CHANGE(service.executing=1u);
    STREAM_CHANGE(service.status=KUI_GD_COMPLETED);
#undef STREAM_CHANGE
}
static void serial_and_hardware_producers(void) {
#define SCI_CHANGE(change) do {fresh();change;expect_service(KUI_VC2_VMU_GATE_SCI);} while(0)
    SCI_CHANGE(reader.active=1u);
    SCI_CHANGE(reader.failed=1u);
    SCI_CHANGE(reader.in_irq=1u);
    SCI_CHANGE(reader.hooked=1u);
    SCI_CHANGE(reader.opened=1u);
    SCI_CHANGE(reader.release.armed=1u);
    SCI_CHANGE(reader.release.depth=1u);
    SCI_CHANGE(kui_retail_async_region.shared.port.acquired=true);
    SCI_CHANGE(kui_retail_async_region.shared.port.fault=true);
    SCI_CHANGE(kui_retail_async_region.shared.stream.state=1u);
    SCI_CHANGE(kui_retail_async_region.shared.card.stream.active=true);
#undef SCI_CHANGE
    for(unsigned channel=0u;channel<4u;++channel) {
        expect_hardware(UINT32_C(0xffa0000c)+16u*channel,1u);
        /* DE+TE is completed, not a live producer. */
        fresh();*hardware_value(UINT32_C(0xffa0000c)+16u*channel)=3u;expect(0u);
    }
    expect_hardware(UINT32_C(0xa05f7418),1u);
    for(unsigned channel=0u;channel<4u;++channel)for(unsigned reg=20u;reg<=24u;reg+=4u) {
        expect_hardware(UINT32_C(0xa05f7800)+32u*channel+reg,1u);
    }
    for(unsigned reg=0u;reg<2u;++reg) {
        expect_hardware(UINT32_C(0xa05f6c14)+4u*reg,1u);
    }
    expect_hardware(UINT32_C(0xa05f6808),1u);
}
static void word_crc_matches_byte_crc(void) {
    const unsigned sizes[]={0u,1u,2u,3u,4u,8u,128u,1024u,
        KUI_VC2_VMU_BLOB_BYTES/4u,
        KUI_VC2_VMU_RESERVE_BYTES/4u};
    for(unsigned pattern=0u;pattern<4u;++pattern) {
        for(unsigned i=0u;i<sizeof(prepared_storage);++i)
            prepared[i]=(uint8_t)(pattern==0u?0u:pattern==1u?255u:
                pattern==2u?i:(i*37u)^(i>>7));
        for(unsigned at=0u;at<sizeof(sizes)/sizeof(sizes[0]);++at) {
            unsigned words=sizes[at];
            uint32_t before=kui_vc2_ce_vmu_crc32_words(prepared_storage,words);
            assert(before==kui_vc2_ce_vmu_crc32(prepared,4u*words));
            if(words) {
                prepared[4u*words-1u]^=0x80u;
                uint32_t changed=kui_vc2_ce_vmu_crc32_words(prepared_storage,words);
                assert(changed!=before && changed==kui_vc2_ce_vmu_crc32(prepared,4u*words));
                prepared[4u*words-1u]^=0x80u;
            }
        }
    }
}
static void owned_marker_requires_explicit_publication(void) {
    fresh();expect_service(0u);
    assert(!service.ops.context);
    struct kui_retail_async_region before=kui_retail_async_region;
    size_t context_offset=(const uint8_t *)(const void *)&service.ops.context-
        (const uint8_t *)(const void *)&kui_retail_async_region;
    void *owned=(void *)(uintptr_t)1u;
    assert(!kui_vc2_ce_vmu_service_own());
    memcpy((uint8_t *)(void *)&before+context_offset,&owned,sizeof(owned));
    assert(!memcmp(&before,&kui_retail_async_region,sizeof(before)));
    assert(!kui_vc2_ce_vmu_service_own());
    assert(!memcmp(&before,&kui_retail_async_region,sizeof(before)));
    expect_service(KUI_VC2_VMU_GATE_STREAM);
}
static void corruption_and_initializer_refusal(void) {
#if !KUI_VC2_VMU_LATE
    fresh();prepared[0]^=1u;expect(KUI_VC2_VMU_GATE_PREPARED_CRC);
    uint32_t word;memcpy(&word,prepared,sizeof(word));
    assert(kui_native_ocram_trace.entry[3]==word);
    assert(crcs==1u && !inits);
    fresh();prepared[KUI_VC2_VMU_BLOB_BYTES-1u]^=1u;expect(KUI_VC2_VMU_GATE_PREPARED_CRC);
    assert(crcs==1u && !inits);
    fresh();dirty_alias=true;expect(KUI_VC2_VMU_GATE_PREPARED_CRC);
    assert(crcs==1u && !inits);
#endif
    fresh();initializer_result=0x812u;expect(KUI_VC2_VMU_GATE_INITIALIZER);
    assert(inits==1u && kui_native_ocram_trace.entry[14]==0x812u);
    fresh();++service.area;expect(KUI_VC2_VMU_GATE_INITIALIZER);
    assert(inits==1u && kui_native_ocram_trace.entry[14]==KUI_VC2_VMU_GATE_STREAM);
    fresh();reader.failed=1u;expect(KUI_VC2_VMU_GATE_INITIALIZER);
    assert(inits==1u && kui_native_ocram_trace.entry[14]==KUI_VC2_VMU_GATE_SCI);
    assert(kui_vc2_ce_vmu_crc32("123456789",9u)==UINT32_C(0xcbf43926));
    assert(kui_vc2_ce_vmu_crc32("",0u)==0u);
}
static void native_zero_tail_is_not_immutable(void) {
    fresh();
    memset(prepared+KUI_VC2_VMU_BLOB_BYTES,0xa5,
        sizeof(prepared_storage)-KUI_VC2_VMU_BLOB_BYTES);
    expect(0u);
    assert(crcs==1u && inits==1u);
#if KUI_VC2_VMU_LATE
    assert(loads==1u && loaded_blocks==96u);
    assert(!memcmp(prepared,prepared_reference,sizeof(prepared_reference)));
    fresh();dirty_alias=dirty_tail=true;expect(0u);
    assert(loads==1u && loaded_blocks==96u && crcs==1u && inits==1u);
    assert(!memcmp(prepared,prepared_reference,sizeof(prepared_reference)));
#else
    /* The low gate only validates. The actual fixed-entry wrapper must
     * clear this entire unowned tail before using its private high stack. */
    for(unsigned i=KUI_VC2_VMU_BLOB_BYTES;i<sizeof(prepared_storage);++i)
        assert(prepared[i]==0xa5u);
    fresh();dirty_tail=true;expect(0u);
    assert(crcs==1u && inits==1u && prepared[sizeof(prepared_storage)-1u]==1u);
#endif
}
#if KUI_VC2_VMU_LATE
static void late_load_success_and_refusals(void) {
    fresh();memset(prepared,0,sizeof(prepared_storage));expect(0u);
    assert(loads==1u && loaded_blocks==96u && crcs==1u && inits==1u);
    assert(!memcmp(prepared,prepared_reference,sizeof(prepared_reference)));
    fresh();low_state.map.gdi_crc32=96u;low_state.map.boot_bytes=0u;
    memset(prepared,0xa5,sizeof(prepared_storage));expect(0u);
    assert(loads==1u && loaded_blocks==96u && crcs==1u && inits==1u);
    assert(!memcmp(prepared,prepared_reference,sizeof(prepared_reference)));
    const uint32_t fail_blocks[]={0u,47u,48u,49u,95u};
    for(unsigned i=0u;i<sizeof(fail_blocks)/sizeof(fail_blocks[0]);++i) {
        fresh();const uint32_t block=fail_blocks[i];load_fail_index=block;
        expect(LATE_READ_WHY);
        assert(loads==1u && loaded_blocks==block+1u && !crcs && !inits);
        assert(kui_native_ocram_trace.entry[3]==(block<48u?100u+block:1000u+block-48u));
        assert(kui_native_ocram_trace.entry[14]==LATE_READ_ERROR);
    }
    for(unsigned last=0u;last<2u;++last) {
        fresh();prepared_reference[last?sizeof(prepared_reference)-1u:0u]^=1u;
        expect(KUI_VC2_VMU_GATE_PREPARED_CRC);
        assert(loads==1u && loaded_blocks==96u && crcs==1u && !inits);
    }
}
static void late_raw_reader_code_refusals(void) {
    for(unsigned kind=0u;kind<5u;++kind) {
        fresh();
        if(kind==0u)memset(raw_island,0,sizeof(raw_storage));
        else if(kind==1u)raw_island[0]^=1u;
        else if(kind==2u)raw_island[sizeof(raw_storage)-1u]^=1u;
        else if(kind==3u)dirty_raw_first=true;
        else dirty_raw_last=true;
        expect(KUI_VC2_VMU_GATE_RAW_CODE);
        const uint32_t actual=kui_vc2_ce_vmu_crc32(raw_island,sizeof(raw_storage));
        assert(actual!=low_state.map.ip_crc32 && raw_purges==1u && raw_crcs==1u);
        assert(!loads && !loaded_blocks && !crcs && !inits);
        assert(kui_native_ocram_trace.entry[3]==UINT32_C(0xac008100));
        assert(kui_native_ocram_trace.entry[14]==actual);
    }
}
#endif
static void high_refusal_requires_source_bound_trace(void) {
    fresh();
    struct kui_native_ocram_diagnostic before;
    memcpy(&before,(const void *)&kui_native_ocram_trace,sizeof(before));
#define REJECT_TRACE(region,trace) do { \
    assert(kui_vc2_ce_vmu_service_refuse(region,trace,0x345u)==KUI_VC2_VMU_GATE_INITIALIZER); \
    assert(!memcmp(&before,(const void *)&kui_native_ocram_trace,sizeof(before))); \
} while(0)
    REJECT_TRACE(NULL,&kui_native_ocram_trace);
    REJECT_TRACE(&kui_retail_async_region,NULL);
    REJECT_TRACE(&kui_retail_async_region,&kui_native_ocram_trace.entry[1]);
    reader.manifest=NULL;
    REJECT_TRACE(&kui_retail_async_region,&kui_native_ocram_trace);
    reader.manifest=&low_state.map;service.tracks=NULL;
    REJECT_TRACE(&kui_retail_async_region,&kui_native_ocram_trace);
#undef REJECT_TRACE
    service.tracks=low_state.map.slots;
    assert(kui_vc2_ce_vmu_service_refuse(&kui_retail_async_region,
        &kui_native_ocram_trace,0x345u)==KUI_VC2_VMU_GATE_INITIALIZER);
    before.entry[3]=KUI_VC2_VMU_INIT_P2;
    before.entry[14]=0x345u;before.entry[15]=KUI_VC2_VMU_GATE_INITIALIZER;
    assert(!memcmp(&before,(const void *)&kui_native_ocram_trace,sizeof(before)));
}
int main(void) {
    uint32_t snapshot[16];kui_vc2_ce_vmu_service_snapshot(snapshot);
    assert(!kui_vc2_ce_vmu_saved_display());
    for(unsigned i=0u;i<16u;++i)assert(!snapshot[i]);
    assert(kui_vc2_ce_vmu_service_own()==KUI_VC2_VMU_GATE_OWNERSHIP);
    assert(!service.ops.context && !kui_vc2_ce_vmu_saved_display());
    valid_and_unrelated_calls();caller_and_completed_stream();
    serial_and_hardware_producers();corruption_and_initializer_refusal();
    native_zero_tail_is_not_immutable();
#if KUI_VC2_VMU_LATE
    late_load_success_and_refusals();
    late_raw_reader_code_refusals();
#endif
    word_crc_matches_byte_crc();owned_marker_requires_explicit_publication();
    high_refusal_requires_source_bound_trace();
    puts("VC2 actual low gate: complete stream, caller/MMU/cache/SCI/DMA admission, CRC refusal, initializer errors and no reader/MMIO mutation passed");
    return 0;
}
