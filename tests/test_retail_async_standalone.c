/* SPDX-License-Identifier: GPL-3.0-only */
/* Production standalone binder, demand reader, cursor, GD service and SCI
 * stream against the existing byte/card/DMA model. This models transport;
 * it does not claim SD hardware, cache ordering or SH interrupt execution. */
#define _GNU_SOURCE
#include <sys/mman.h>
#include <stdint.h>
#include <assert.h>
#include "kui/native_standalone_reader.h"
#include "kui/native_sdk_sci_runtime.h"

#if KUI_NATIVE_STANDALONE_STRUCTURAL
/* Another owner than DOA2: its own VBR and source vector, compact FPU
 * geometry and U0 raw pointers, as the image-only family finder returns. */
#define TEST_VBR UINT32_C(0x8c0c4000)
#define TEST_VECTOR UINT32_C(0x8c0a0040)
#define TEST_NORMAL ((TEST_VECTOR+0x8cu)&UINT32_C(0x1fffffff))
#define TEST_FPU (TEST_NORMAL+0x138u)
#define TEST_DEFAULT UINT32_C(0x0c09ff10)
#else
#define TEST_VBR KUI_NATIVE_STANDALONE_SOURCE_VBR
#define TEST_VECTOR KUI_NATIVE_STANDALONE_SOURCE_VECTOR
#define TEST_NORMAL KUI_NATIVE_STANDALONE_SOURCE_NORMAL
#define TEST_FPU KUI_NATIVE_STANDALONE_SOURCE_FPU
#define TEST_DEFAULT KUI_NATIVE_STANDALONE_SOURCE_DEFAULT
#endif

static uint32_t host_sr=UINT32_C(0x600000f0);
static unsigned publication_visits;
#ifdef KUI_STANDALONE_DEMAND_ASAN
static uint32_t modeled_slots[2];
#endif
volatile uint32_t kui_retail_hook_active,kui_retail_hook_fault;
uint32_t kui_native_standalone_test_sr(void) {return host_sr;}
void kui_native_standalone_test_publish(uint32_t address) {
    (void)address;++publication_visits;
}
void kui_retail_sdk_sci_entry(void) {} /* Only its address is used by reader C. */
uint32_t kui_retail_async_test_read32(uint32_t address) {
    /* P2 readback refers to the same modeled RAM, with no cache simulator. */
    if((address&UINT32_C(0xe0000000))==UINT32_C(0xa0000000))
        address-=UINT32_C(0x20000000);
#ifdef KUI_STANDALONE_DEMAND_ASAN
    assert(address==TEST_VBR+KUI_NATIVE_SDK_SCI_ERI_SLOT_OFFSET ||
        address==TEST_VBR+KUI_NATIVE_SDK_SCI_RXI_SLOT_OFFSET);
    return modeled_slots[(address-(TEST_VBR+
        KUI_NATIVE_SDK_SCI_ERI_SLOT_OFFSET))/4u];
#else
    assert(address>=UINT32_C(0x8c000000) &&
        address<=UINT32_C(0x8c1ffffc) && !(address&3u));
    return *(volatile const uint32_t *)(uintptr_t)address;
#endif
}
#define kui_sci_sd_acquire fixture_sci_sd_acquire
#define main unused_original_async_suite_main
#include "test_retail_async.c"
#undef main
#undef kui_sci_sd_acquire

enum kui_loader_sd_result kui_sci_sd_acquire(void) {
    enum kui_loader_sd_result result=fixture_sci_sd_acquire();
    if(result==KUI_LOADER_SD_OK)R.shared.port.acquired=true;
    return result;
}
static unsigned requests_verified,irq_entries,pio_bound_entries;
static unsigned loss_cases,cancel_cases,source_cases;
static volatile uint32_t *slots_at(uint32_t owner) {
#ifdef KUI_STANDALONE_DEMAND_ASAN
    (void)owner;return modeled_slots;
#else
    return (void *)(uintptr_t)(owner+
        KUI_NATIVE_SDK_SCI_ERI_SLOT_OFFSET);
#endif
}
static volatile uint32_t *slots(void) {return slots_at(TEST_VBR);}
static uint32_t callback(void) {return (uint32_t)(uintptr_t)kui_retail_sdk_sci_entry;}
static void source_context_at(uint32_t owner,bool initialized) {
    const struct kui_native_standalone_source descriptor={
        TEST_VECTOR,TEST_NORMAL,
        TEST_FPU,TEST_DEFAULT,
        KUI_NATIVE_STANDALONE_SOURCE_MAGIC};
    memcpy(R.vector100,&descriptor,sizeof(descriptor));
    R.vector600[KUI_NATIVE_SDK_SCI_DEFAULT_INDEX]=descriptor.original;
#ifdef KUI_STANDALONE_DEMAND_ASAN
    /* ASAN reserves the SH P1 addresses as host shadow. Its demand-only
     * run starts after the independently checked publication, so the real
     * binder takes its first-install refusal before dereferencing P1. */
    R.vector600[KUI_NATIVE_SDK_SCI_VBR_INDEX]=owner;
    R.vector600[KUI_NATIVE_SDK_SCI_COOKIE_INDEX]=initialized?KUI_NATIVE_SDK_SCI_COOKIE:0u;
    slots()[0]=slots()[1]=(uint32_t)(uintptr_t)kui_retail_sdk_sci_entry;
#else
    volatile uint32_t *original=(void *)(uintptr_t)descriptor.vector;
    volatile uint32_t *copy=(void *)(uintptr_t)(owner+0x600u);
    /* Synthetic vector words represent the prior stage admission. The
     * original DOA2 instructions are independently executed by SH proof. */
    for(unsigned i=0u;i<24u;++i)original[i]=UINT32_C(0x90000000)+i;
    original[22]=descriptor.normal;
    for(unsigned i=0u;i<24u;++i)copy[i]=initialized?original[i]:0u;
    copy[20]=UINT32_C(0x11223344); /* Original SDK's allowed R0 scratch. */
    if(initialized)copy[22]=descriptor.fpu; /* Independent reviewed common mode. */
    slots_at(owner)[0]=slots_at(owner)[1]=initialized?descriptor.original:0u;
#endif
}
static void source_context(bool initialized) {source_context_at(TEST_VBR,initialized);}
static void setup_standalone(bool cooked,bool ready,bool scattered) {
    cooked_fixture=cooked;
    setup(TEST_VBR,X,48u,scattered);
    publication_visits=0u;kui_retail_hook_active=1u;kui_retail_hook_fault=0u;
    host_sr=UINT32_C(0x600000f0);
    source_context(ready);mode(2048u);
    if(ready) {
        CHECK(R.vector600[KUI_NATIVE_SDK_SCI_COOKIE_INDEX]==KUI_NATIVE_SDK_SCI_COOKIE);
        CHECK(R.vector600[KUI_NATIVE_SDK_SCI_VBR_INDEX]==TEST_VBR);
        CHECK(slots()[0]==callback() && slots()[1]==callback());
        CHECK(publication_visits==
#ifdef KUI_STANDALONE_DEMAND_ASAN
            0u
#else
            4u
#endif
            && !hw.vbr_sets);
    } else CHECK(!R.vector600[KUI_NATIVE_SDK_SCI_COOKIE_INDEX] && !publication_visits);
}
static uint32_t sci_irq(uint32_t event) {
    uint32_t active=kui_retail_hook_active;kui_retail_hook_active=0u;
    uint32_t result=kui_retail_sdk_sci_dispatch(event);
    kui_retail_hook_active=active;++irq_entries;
    return result;
}
static void complete_standalone(int32_t token,uint32_t lba,unsigned count,bool irqs) {
    for(unsigned round=0u;round<2000u;++round) {
        if(irqs && kui_sci_stream_busy()) {
            elapse();
            unsigned before=R.engine.stats.irq_blocks;
            CHECK(sci_irq(round&1u?0x500u:0x4e0u)==0u);
            CHECK(R.engine.stats.irq_blocks-before<=KUI_RETAIL_NATIVE_IRQ_BATCH_LIMIT);
        } else elapse();
        int32_t status=gd(KUI_GD_CHECK,(uint32_t)token,STATUS);
        if(status==KUI_GD_COMPLETED) {
            CHECK(get(STATUS+8u)==count*R.shared.service.sector_bytes && !get(STATUS+4u));
            compare(lba,count,OUTPUT);++requests_verified;
            CHECK(!R.engine.active && !R.engine.hooked && !R.engine.release.armed);
            CHECK(!hw.vbr_sets && hw.vbr==TEST_VBR);
            return;
        }
        CHECK(status==KUI_GD_PROCESSING && round<1999u);
    }
}
static void bootstrap_then_late_bind(void) {
    setup_standalone(true,false,false);hw.iprb=0x5a9fu;
    int32_t token=request(45001u,64u,OUTPUT);
    CHECK(R.engine.active && !R.engine.release.armed && hw.iprb==0x5a0fu);
    complete_standalone(token,45001u,64u,false);
    CHECK(hw.iprb==0x5a9fu && !publication_visits);
    source_context(true);(void)gd(KUI_GD_DRIVE,STATUS,0u);
    CHECK(slots()[0]==callback() && slots()[1]==callback());
    CHECK(publication_visits==4u && !hw.vbr_sets);
    token=request(45001u,64u,OUTPUT);
    CHECK(hw.iprb==0x5a1fu && R.engine.release.armed==0x10u);
    complete_standalone(token,45001u,64u,true);CHECK(hw.iprb==0x5a9fu);
}
static void demand_variants(void) {
    for(unsigned cooked=0u;cooked<2u;++cooked)
        for(unsigned scatter=0u;scatter<2u;++scatter)
            for(unsigned irq=0u;irq<2u;++irq) {
                setup_standalone(cooked, true,scatter);
                int32_t token=request(45001u,64u,OUTPUT);
                unsigned before=R.engine.stats.call_blocks;
                CHECK(gd(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_PROCESSING);
                CHECK(R.engine.stats.call_blocks-before==20u);
                complete_standalone(token,45001u,64u,irq!=0u);
                CHECK(publication_visits==
#ifdef KUI_STANDALONE_DEMAND_ASAN
                    0u
#else
                    4u
#endif
                    );
            }
}
static void foreign_dma_pio(void) {
    setup_standalone(true,true,false);
    m.sar=UINT32_C(0x0c123450);m.dar=UINT32_C(0x0c156780);m.tcr=0x345u;m.chcr=1u;
    int32_t token=request(45001u,64u,OUTPUT);
    CHECK(kui_sci_stream_stats()->polled<=1u && R.engine.active);
    for(unsigned round=0u;round<1000u;++round) {
        unsigned before=kui_sci_stream_stats()->polled;
        int32_t status=gd(KUI_GD_CHECK,(uint32_t)token,STATUS);
        CHECK(kui_sci_stream_stats()->polled-before<=1u);++pio_bound_entries;
        CHECK(m.sar==UINT32_C(0x0c123450) && m.dar==UINT32_C(0x0c156780) &&
            m.tcr==0x345u && m.chcr==1u);
        if(status==KUI_GD_COMPLETED) {compare(45001u,64u,OUTPUT);++requests_verified;break;}
        CHECK(status==KUI_GD_PROCESSING && round<999u);
    }
    CHECK(!hw.vbr_sets && !R.engine.active);
}
static void crc_retry(void) {
    setup_standalone(true,true,true);
    card.corrupt_lba=manifest.slots[manifest.slots[2].track.first_extent].extent.card_lba+5u;
    card.corrupt_count=2u;
    int32_t token=request(45000u,40u,OUTPUT);
    complete_standalone(token,45000u,40u,true);
    CHECK(kui_sci_stream_stats()->crc_errors==2u && !R.shared.service.error);
}
static void route_loss(void) {
    for(unsigned cause=0u;cause<5u;++cause) {
        setup_standalone(true,true,false);hw.iprb=0x5a9fu;
        int32_t token=request(45001u,64u,OUTPUT);CHECK(hw.iprb==0x5a1fu);
        switch(cause) {
        case 0u:R.vector600[KUI_NATIVE_SDK_SCI_COOKIE_INDEX]^=1u;break;
        case 1u:R.vector600[KUI_NATIVE_SDK_SCI_VBR_INDEX]^=4u;break;
        case 2u:hw.vbr^=4u;break;
        case 3u:slots()[0]^=4u;break;
        default:slots()[1]^=4u;break;
        }
        uint32_t slot0=slots()[0],slot1=slots()[1];
        unsigned progress=R.engine.cursor.done;
        CHECK(sci_irq(0x4e0u)==TEST_DEFAULT);
        CHECK(!R.engine.release.armed && hw.iprb==0x5a0fu);
        CHECK(R.engine.cursor.done==progress);
        hw.vbr=TEST_VBR;
        complete_standalone(token,45001u,64u,false);
        CHECK(hw.iprb==0x5a9fu && slots()[0]==slot0 && slots()[1]==slot1);
        CHECK(publication_visits==
#ifdef KUI_STANDALONE_DEMAND_ASAN
            0u
#else
            4u
#endif
            );++loss_cases;
    }
}
static void cancellation(void) {
    for(unsigned how=0u;how<3u;++how) {
        setup_standalone(true,true,false);
        int32_t token=request(45001u,64u,OUTPUT);elapse();CHECK(!sci_irq(0x500u));
        CHECK(R.engine.cursor.done<64u);
        CHECK(!gd(how==0u?KUI_GD_ABORT:how==1u?KUI_GD_INIT:KUI_GD_RESET,
            how==0u?(uint32_t)token:0u,0u));
        hw.frozen=true;unsigned writes=hw.writes;
        for(unsigned i=0u;i<8u;++i) {
            elapse();(void)sci_irq(0x4e0u);(void)gd(KUI_GD_EXEC,0u,0u);
        }
        CHECK(hw.writes==writes && !R.engine.active && !kui_sci_stream_busy());
        CHECK(!hw.vbr_sets && publication_visits==
#ifdef KUI_STANDALONE_DEMAND_ASAN
            0u
#else
            4u
#endif
            );++cancel_cases;
    }
}
static void source_refusal(void) {
    for(unsigned field=0u;field<5u;++field) {
        setup_standalone(true,false,false);source_context(true);
        R.vector100[field]^=1u;
        (void)gd(KUI_GD_DRIVE,STATUS,0u);
        CHECK(!publication_visits && !R.vector600[KUI_NATIVE_SDK_SCI_COOKIE_INDEX]);
        CHECK(slots()[0]==TEST_DEFAULT &&
            slots()[1]==TEST_DEFAULT);++source_cases;
    }
}
#if KUI_NATIVE_STANDALONE_STRUCTURAL && !defined(KUI_STANDALONE_DEMAND_ASAN)
/* The structural binder takes the game's live VBR and published roles. An
 * owner below the IP image or off its 256-byte alignment, and a source vector
 * outside the loaded program, refuse before any publication even when a
 * matching copy and default slots exist there. Another owner binds alike. */
static void structural_owner_checks(void) {
    static const uint32_t owners[]={UINT32_C(0x8c007f00),UINT32_C(0x8c004000),
        TEST_VBR+4u,TEST_VBR+0x80u};
    for(unsigned i=0u;i<sizeof(owners)/sizeof(owners[0]);++i) {
        setup_standalone(true,false,false);source_context_at(owners[i],true);hw.vbr=owners[i];
        (void)gd(KUI_GD_DRIVE,STATUS,0u);
        CHECK(!publication_visits && !R.vector600[KUI_NATIVE_SDK_SCI_COOKIE_INDEX] &&
            !R.vector600[KUI_NATIVE_SDK_SCI_VBR_INDEX]);
        CHECK(slots_at(owners[i])[0]==TEST_DEFAULT && slots_at(owners[i])[1]==TEST_DEFAULT);
        ++source_cases;
    }
    static const uint32_t vectors[]={UINT32_C(0x8c00fffc),UINT32_C(0x8cc0ffa4),TEST_VECTOR+2u};
    for(unsigned i=0u;i<sizeof(vectors)/sizeof(vectors[0]);++i) {
        setup_standalone(true,false,false);source_context(true);R.vector100[0]=vectors[i];
        (void)gd(KUI_GD_DRIVE,STATUS,0u);
        CHECK(!publication_visits && !R.vector600[KUI_NATIVE_SDK_SCI_COOKIE_INDEX]);
        CHECK(slots()[0]==TEST_DEFAULT && slots()[1]==TEST_DEFAULT);++source_cases;
    }
    const uint32_t other=UINT32_C(0x8c180000);
    setup_standalone(true,false,false);source_context_at(other,true);hw.vbr=other;
    (void)gd(KUI_GD_DRIVE,STATUS,0u);
    CHECK(R.vector600[KUI_NATIVE_SDK_SCI_COOKIE_INDEX]==KUI_NATIVE_SDK_SCI_COOKIE &&
        R.vector600[KUI_NATIVE_SDK_SCI_VBR_INDEX]==other && publication_visits==4u);
    CHECK(slots_at(other)[0]==callback() && slots_at(other)[1]==callback());
    CHECK(!slots()[0] && !slots()[1] && !hw.vbr_sets); /* The unused owner is untouched. */
    hw.iprb=0x5a9fu;(void)request(45001u,64u,OUTPUT);
    CHECK(hw.iprb==0x5a1fu && R.engine.release.armed==0x10u);
}
#endif
#if KUI_NATIVE_STANDALONE_STRUCTURAL && !defined(KUI_STANDALONE_DEMAND_ASAN)
/* The early Shinobi family publishes one common handler as both roles. Its
 * live INTEVT copy is 13 words: only the R0 scratch (11) may differ, the
 * common (12) must be the reviewed one, and words past the stub are not its. */
static void early_context(volatile uint32_t *copy,uint32_t common) {
    const struct kui_native_standalone_source descriptor={
        TEST_VECTOR,common,common,TEST_DEFAULT,KUI_NATIVE_STANDALONE_SOURCE_MAGIC};
    memcpy(R.vector100,&descriptor,sizeof(descriptor));
    R.vector600[KUI_NATIVE_SDK_SCI_DEFAULT_INDEX]=descriptor.original;
    volatile uint32_t *original=(void *)(uintptr_t)TEST_VECTOR;
    for(unsigned i=0u;i<24u;++i)original[i]=UINT32_C(0x90000000)+i;
    original[11]=0u;original[12]=common;
    for(unsigned i=0u;i<24u;++i)copy[i]=original[i];
    copy[11]=UINT32_C(0x11223344);
    for(unsigned i=13u;i<24u;++i)copy[i]=UINT32_C(0xdead0000)+i;
    slots()[0]=slots()[1]=TEST_DEFAULT;
}
static void early_family_late_bind(void) {
    const uint32_t common=(TEST_VECTOR+0x34u)&UINT32_C(0x1fffffff);
    volatile uint32_t *copy=(void *)(uintptr_t)(TEST_VBR+0x600u);
    for(int changed=-1;changed<13;++changed)for(unsigned variant=0u;variant<2u;++variant) {
        setup_standalone(true,false,false);early_context(copy,common);
        if(changed>=0)copy[changed]^=variant?UINT32_C(0x80000000):1u;
        (void)gd(KUI_GD_DRIVE,STATUS,0u);
        bool bound=changed<0 || changed==11;
        CHECK(bound?R.vector600[KUI_NATIVE_SDK_SCI_COOKIE_INDEX]==KUI_NATIVE_SDK_SCI_COOKIE &&
            slots()[0]==callback() && slots()[1]==callback():
            !R.vector600[KUI_NATIVE_SDK_SCI_COOKIE_INDEX] && slots()[0]==TEST_DEFAULT &&
            slots()[1]==TEST_DEFAULT && !publication_visits);
        ++source_cases;
    }
    /* A live copy naming another handler than the reviewed common refuses. */
    setup_standalone(true,false,false);early_context(copy,common);copy[12]=common+0x14cu;
    (void)gd(KUI_GD_DRIVE,STATUS,0u);
    CHECK(!R.vector600[KUI_NATIVE_SDK_SCI_COOKIE_INDEX] && slots()[0]==TEST_DEFAULT);++source_cases;
}
#endif
int main(void) {
#if KUI_NATIVE_STANDALONE_STRUCTURAL && !defined(KUI_STANDALONE_DEMAND_ASAN)
    {
        void *early=mmap((void *)(uintptr_t)UINT32_C(0x8c000000),UINT32_C(0x200000),
            PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
        CHECK(early==(void *)(uintptr_t)UINT32_C(0x8c000000));
        early_family_late_bind();
        CHECK(!munmap(early,UINT32_C(0x200000)));
    }
    void *structural=mmap((void *)(uintptr_t)UINT32_C(0x8c000000),UINT32_C(0x200000),
        PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    CHECK(structural==(void *)(uintptr_t)UINT32_C(0x8c000000));
    structural_owner_checks();
    CHECK(!munmap(structural,UINT32_C(0x200000)));
#endif
#ifndef KUI_STANDALONE_DEMAND_ASAN
    void *mapped=mmap((void *)(uintptr_t)UINT32_C(0x8c000000),UINT32_C(0x200000),
        PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    CHECK(mapped==(void *)(uintptr_t)UINT32_C(0x8c000000));
    bootstrap_then_late_bind();source_refusal();
#else
    (void)bootstrap_then_late_bind;(void)source_refusal;
#endif
    demand_variants();foreign_dma_pio();crc_retry();route_loss();cancellation();
#ifndef KUI_STANDALONE_DEMAND_ASAN
    CHECK(!munmap(mapped,UINT32_C(0x200000)));
#endif
    printf("STANDALONE_DEMAND_JSON {\"checks\":%u,\"byte_exact_requests\":%u,"
        "\"modeled_sci_entries\":%u,\"pio_bounded_entries\":%u,\"route_loss_cases\":%u,"
        "\"cancel_cases\":%u,\"descriptor_fault_cases\":%u,\"vbr_writes\":0,"
        "\"hardware_executed\":false}\n",checks,requests_verified,irq_entries,
        pio_bound_entries,loss_cases,cancel_cases,source_cases);
    return 0;
}
