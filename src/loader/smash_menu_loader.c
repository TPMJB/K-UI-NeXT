/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_menu_loader.h"
#include "kui/smash_menu_loader_runtime.h"
#include "kui/smash_native_menu.h"
#include "kui/smash_ce_child.h"
#include "kui/retail_image.h"
#include "kui/cold_load.h"
#include "kui/cold_image.h"
#include "kui/smash_native_child.h"
#include "kui/smash_ce_gin.h"
#include "kui/smash_native_gin.h"
#if KUI_SMASH_PHASE_DIAGNOSTIC
#include "kui/smash_phase_trace.h"
#include "kui/smash_phase_video.h"
#endif
#include <string.h>

/* The outer stage verifies the immutable blob, clears its entire BSS, then
 * publishes this record with the saved original firmware. The binary carries
 * 632 zero bytes here; its normalized CRC remains checkable. */
struct kui_smash_menu_loader_runtime kui_smash_menu_loader_runtime
    __attribute__((used,section(".data.smash_menu_runtime")))={0};
volatile uint32_t kui_smash_menu_loader_diagnostic[4];
#if KUI_SMASH_CARD_SESSION
__asm__(".global __kui_smash_card_session_flow\n.set __kui_smash_card_session_flow,1\n");
#endif
static union {
    struct kui_smash_native_menu_seed native;
    struct kui_smash_ce_child_seed ce;
} captured;
#if KUI_SMASH_PHASE_DIAGNOSTIC
/* This is private diagnostic state, outside both working runtime ABIs.
 * The enclosing stage zeros the handoff BSS each time it is installed. */
/* Only phases1..4 occur in the surviving outer owner. Keep their complete
 * stamps in this bounded private object; the unchanged992-byte TIM1 wire is
 * expanded in the authenticated child before phase5 is appended there. */
struct smash_outer_trace {uint32_t count;struct kui_smash_phase_entry entries[4];};
struct smash_outer_trace kui_smash_menu_loader_phase_trace;
struct kui_smash_phase_video_seed kui_smash_menu_loader_phase_video;
static bool smash_outer_trace_valid(void) {
    const struct smash_outer_trace *t=&kui_smash_menu_loader_phase_trace;
    if(t->count>4u)return false;
    uint32_t last=0u;
    for(unsigned i=0u;i<t->count;i++) {
        const struct kui_smash_phase_entry *e=&t->entries[i];
        if(!e->phase || e->phase>4u || e->phase<=last || e->reserved || e->stamp.reserved)return false;
        last=e->phase;
    }
    const uint8_t *p=(const void *)(t->entries+t->count);
    size_t bytes=(4u-t->count)*sizeof(t->entries[0]);
    while(bytes--)if(*p++)return false;
    return true;
}
static bool smash_outer_trace_mark(uint32_t phase) {
    struct smash_outer_trace *t=&kui_smash_menu_loader_phase_trace;
    if(!smash_outer_trace_valid() || !phase || phase>4u || t->count==4u ||
       (t->count && phase<=t->entries[t->count-1u].phase))return false;
    struct kui_smash_phase_entry *e=&t->entries[t->count];e->phase=phase;
    kui_smash_phase_clock_read(&e->stamp);++t->count;return true;
}
#endif
extern const uint8_t __smash_menu_loader_begin[] __asm__("__smash_menu_loader_begin");
extern const uint8_t __smash_menu_loader_binary_end[] __asm__("__smash_menu_loader_binary_end");

static uint32_t read32(uint32_t address) {
    return *(volatile const uint32_t *)(uintptr_t)address;
}
static void purge(uint32_t begin,uint32_t end) {
    for(uint32_t p=begin&~31u;p<end;p+=32u)
        __asm__ volatile("ocbp @%0"::"r"(p):"memory");
}
static void __attribute__((noreturn)) stopped(uint32_t reason,uint32_t value) {
    kui_smash_menu_loader_diagnostic[0]=UINT32_C(0x4d4c4453);
    kui_smash_menu_loader_diagnostic[1]=reason;kui_smash_menu_loader_diagnostic[2]=value;
    uint32_t entry=kui_smash_menu_loader_runtime.stop_entry;
    if(!(entry&1u) && entry>=UINT32_C(0xac004000) && entry<UINT32_C(0xac007ea0))
        ((void (*)(uint32_t,uint32_t))(uintptr_t)entry)(reason,value);
    for(;;)__asm__ volatile("nop":::"memory");
}
static bool installed(void) {
    const struct kui_smash_menu_collection *c=&kui_smash_menu_loader_runtime.collection;
    return kui_smash_menu_collection_seal_valid(c) && c->plans[0].handoff_crc32 && c->plans[0].raw_crc32;
}
/* The callback is invoked only after the complete output bytes have reached
 * guest RAM. GIN's loader later replaces its8192-byte header, leaving this
 * separately authenticated control suffix intact. It reads this target as
 * data, so retiring the exact D-cache aliases is sufficient publication. */
bool kui_smash_menu_loader_redirect(uint32_t address,uint32_t bytes) {
    uint32_t physical=address&UINT32_C(0x1fffffff);
#if KUI_SMASH_PHASE_DIAGNOSTIC
    /* The existing callback observes every completed guest payload write.
     * This starts at the first arrived MDE chunk, after the user's selection;
     * subsequent writes must not restart that clock. */
    if(physical==UINT32_C(0x0ce00000) && bytes &&
       !kui_smash_menu_loader_phase_trace.count)
        smash_outer_trace_mark(KUI_SMASH_PHASE_MDE_FIRST_WRITE);
#endif
    if(physical>UINT32_C(0x0cfd2000) || bytes<UINT32_C(0x0cfd20f4)-physical)return true;
    purge(UINT32_C(0x8cfd2000),UINT32_C(0x8cfd2100));
    const void *code=(const void *)(uintptr_t)UINT32_C(0xacfd2000);
    if(kui_retail_crc32(0,code,244u)!=UINT32_C(0xa0fc2827))return true;
    if(!installed())return false;
    *(volatile uint32_t *)(uintptr_t)UINT32_C(0xacfd20e4)=KUI_SMASH_MENU_LOADER_P2;
    return read32(UINT32_C(0xacfd20e4))==KUI_SMASH_MENU_LOADER_P2;
}
static unsigned transfer_kind(void) {
    const uint32_t *g=(const void *)(uintptr_t)UINT32_C(0xacfd0000);
    if(g[4]==UINT32_C(0x8ce00000) && g[5]==903u)return KUI_SMASH_MENU_CHILD_NATIVE;
    if(g[4]==UINT32_C(0x8cf90000) && g[5]==100u)
        return kui_smash_ce_child_identify((const void *)(uintptr_t)UINT32_C(0xacfd0100),
            (const void *)(uintptr_t)UINT32_C(0xac0080f0));
    return KUI_SMASH_MENU_CHILD_COUNT;
}
static bool native_context(void) {
    const uint32_t *g=(const void *)(uintptr_t)UINT32_C(0xac19ee60);
    return g[0]==UINT32_C(0x47494e53) && g[1]==1u && g[2]==UINT32_C(0x3ac59e76) &&
        g[3]==8192u && g[4]==UINT32_C(0x8ce00000) && g[5]==903u && g[6]==4u && g[7]==1u;
}
/* The first512-byte probe chooses the native capsule's declared entry
 * policy before any MDE byte is retired. An occupied malformed capability
 * slot must stop, rather than dispatching a new capsule as an old one. */
static unsigned native_adoption(const uint8_t prefix[512]) {
    static const uint32_t capability[4]={UINT32_C(0x4e49554b),UINT32_C(0x31304441),1u,16u};
    const uint8_t *p=prefix+UINT32_C(0x1e0);
    bool occupied=false;for(unsigned i=0;i<16u;i++)if(p[i])occupied=true;
    if(!occupied)return 0u;
    if(memcmp(p,capability,sizeof(capability)))stopped(20u,0u);
    return 1u;
}
/* Current CE return capsules opt in explicitly. Occupied malformed metadata
 * cannot silently take the legacy terminal path. Full capsule CRC still
 * precedes all writes of ROOT context and original video. */
static bool ce_return_capable(const uint8_t prefix[512]) {
    static const uint32_t capability[4]={UINT32_C(0x5349554b),UINT32_C(0x31524543),1u,16u};
    const uint8_t *p=prefix+UINT32_C(0x1e0);
    bool occupied=false;for(unsigned i=0u;i<16u;i++)if(p[i])occupied=true;
    if(!occupied)return false;
    if(memcmp(p,capability,sizeof(capability)))stopped(26u,0u);
    return true;
}
static uint32_t image_crc(void) {
    const uint8_t *runtime=(const uint8_t *)&kui_smash_menu_loader_runtime;
    const uint32_t zero=0;
    uint32_t crc=kui_retail_crc32(0,__smash_menu_loader_begin,
        (size_t)(runtime-__smash_menu_loader_begin));
    for(unsigned i=0;i<sizeof(kui_smash_menu_loader_runtime)/4u;i++)
        crc=kui_retail_crc32(crc,&zero,4u);
    runtime+=sizeof(kui_smash_menu_loader_runtime);
    return kui_retail_crc32(crc,runtime,(size_t)(__smash_menu_loader_binary_end-runtime));
}
static void hardware_quiescent(void) {
    const uint32_t mmu=read32(UINT32_C(0xff000010));
    if(mmu&1u)stopped(3u,mmu);
    const uint32_t ccr=read32(UINT32_C(0xff00001c));
    if(ccr&UINT32_C(0x80))stopped(4u,ccr);
    for(unsigned i=0;i<4u;i++) {
        uint32_t a=UINT32_C(0xffa0000c)+16u*i;
        if((read32(a)&3u)==1u)stopped(5u,a);
    }
    if(read32(UINT32_C(0xa05f7418))&1u)stopped(5u,UINT32_C(0xa05f7418));
    for(unsigned i=0;i<4u;i++) {
        uint32_t a=UINT32_C(0xa05f7800)+32u*i;
        if((read32(a+20u)|read32(a+24u))&1u)stopped(5u,a);
    }
    const uint32_t producers[]={UINT32_C(0xa05f6c14),UINT32_C(0xa05f6c18),UINT32_C(0xa05f6808)};
    for(unsigned i=0;i<3u;i++)if(read32(producers[i])&1u)stopped(5u,producers[i]);
}
static void firmware_valid(void) {
    for(unsigned i=0;i<3u;i++) {
        /* C0 is an opaque original firmware word, just like the six
         * overwritten instruction words. The cold stage never constrained
         * or called it; carry it exactly until the fresh resident replaces it. */
        if(i==1u)continue;
        uint32_t target=kui_smash_menu_loader_runtime.firmware[i];
        uint32_t p=target&UINT32_C(0x1fffffff);
        uint32_t area=target&UINT32_C(0xff000000);
        bool ram=(area==UINT32_C(0x0c000000) || area==UINT32_C(0x8c000000) || area==UINT32_C(0xac000000)) &&
            p>=UINT32_C(0x0c000100) && p<UINT32_C(0x0c004000);
        bool rom=i==2u && (area==0u || area==UINT32_C(0x80000000) || area==UINT32_C(0xa0000000)) &&
            p && p<UINT32_C(0x200000);
        if((target&1u) || (!ram && !rom))
            stopped(6u,target);
    }
}
static void firmware_restore(void) {
    const uint32_t *f=kui_smash_menu_loader_runtime.firmware;
    *(volatile uint32_t *)(uintptr_t)UINT32_C(0xac0000bc)=f[0];
    *(volatile uint32_t *)(uintptr_t)UINT32_C(0xac0000c0)=f[1];
    *(volatile uint32_t *)(uintptr_t)UINT32_C(0xac0000e0)=f[2];
    for(unsigned i=0;i<3u;i++) {
        *(volatile uint32_t *)(uintptr_t)(UINT32_C(0xac001000)+4u*i)=f[3u+i];
        *(volatile uint32_t *)(uintptr_t)(UINT32_C(0xac0010f0)+4u*i)=f[6u+i];
    }
}
/* A return means an unrelated original GIN target. That path makes
 * no cache or machine-state changes, letting assembly restore its real frame.
 * A selected native launch either completes a fresh cold entry or stops. */
void kui_smash_menu_loader_main(const uint32_t frame[21],uint32_t ccr) {
    /* The quiescent raw shim verifies into its owned stream block before a
     * generic copy here, so this bounded private-stack probe needs no DMA
     * alignment. Its512 bytes fit the existing2048-byte handoff stack. */
    uint8_t probe[512];
    unsigned kind=transfer_kind();
    if(kind>=KUI_SMASH_MENU_CHILD_COUNT)return;
#if KUI_SMASH_PHASE_DIAGNOSTIC
    smash_outer_trace_mark(KUI_SMASH_PHASE_POST_GIN);
#endif
    if(!frame || ((uint32_t)(uintptr_t)frame&UINT32_C(0xdfffffff))!=UINT32_C(0x8c00f3ac) ||
       frame[0]!=0x28u || frame[1] || frame[2]!=UINT32_C(0x8c000000) ||
       frame[3]!=UINT32_C(0x8c00f400) || frame[4]!=UINT32_C(0x700000f0) || frame[5])stopped(1u,ccr);
    for(unsigned i=6u;i<13u;i++)if(frame[i])stopped(1u,i);
    if(!installed() || !kui_smash_menu_collection_valid(&kui_smash_menu_loader_runtime.collection) ||
       (kind==KUI_SMASH_MENU_CHILD_NATIVE && !native_context()) ||
       !kui_smash_menu_return_seed_valid(&kui_smash_menu_loader_runtime.returning))stopped(2u,0u);
    hardware_quiescent();
    purge(KUI_SMASH_MENU_LOADER_RAW_P1,KUI_SMASH_MENU_LOADER_LIMIT-UINT32_C(0x20000000));
    const struct kui_smash_menu_loader_seed *s=&kui_smash_menu_loader_runtime.collection.plans[kind];
    if(image_crc()!=s->handoff_crc32 ||
       kui_retail_crc32(0,(const void *)(uintptr_t)KUI_SMASH_MENU_LOADER_RAW_P2,512u)!=s->raw_crc32)
        stopped(7u,image_crc());
    firmware_valid();
    if(kind==KUI_SMASH_MENU_CHILD_NATIVE) {
        captured.native=(struct kui_smash_native_menu_seed){
            .magic0=KUI_SMASH_NATIVE_MENU_SEED_MAGIC0,.magic1=KUI_SMASH_NATIVE_MENU_SEED_MAGIC1,
            .version=1u,.bytes=sizeof(captured.native)};
        if(!kui_smash_native_menu_seed_capture(&captured.native,
           (const uint8_t *)(uintptr_t)UINT32_C(0xac19ef60),
           (const uint8_t *)(uintptr_t)UINT32_C(0xac0080f0)))stopped(8u,0u);
    } else {
        captured.ce=(struct kui_smash_ce_child_seed){.magic0=KUI_SMASH_CE_CHILD_MAGIC0,
            .magic1=KUI_SMASH_CE_CHILD_MAGIC1,.version=1u,.bytes=sizeof(captured.ce),.profile=kind};
        if(!kui_smash_ce_child_capture(&captured.ce,
           (const uint8_t *)(uintptr_t)UINT32_C(0xacfd0100),
           (const uint8_t *)(uintptr_t)UINT32_C(0xac0080f0)))stopped(8u,kind);
    }
    uint32_t (*read_card)(uint32_t,uint8_t *,uint32_t)=
        (uint32_t (*)(uint32_t,uint8_t *,uint32_t))(uintptr_t)KUI_SMASH_MENU_LOADER_RAW_P2;
    /* Check reader/SCI ownership through its exact quiescence-gated shim
     * before retiring the executable. A busy/refused probe changes no MDE. */
#if KUI_SMASH_PHASE_DIAGNOSTIC
    smash_outer_trace_mark(KUI_SMASH_PHASE_CAPSULE_BEGIN);
#endif
    uint32_t result=read_card(s->first_lba,probe,1u);
    if(result)stopped(9u,result);
    bool adopt_native=kind==KUI_SMASH_MENU_CHILD_NATIVE && native_adoption(probe);
    bool return_ce=kind!=KUI_SMASH_MENU_CHILD_NATIVE && ce_return_capable(probe);
    if(adopt_native && (s->padded_bytes>UINT32_C(0x100000) || s->payload_bytes<UINT32_C(0x2000)))
        stopped(20u,s->padded_bytes);
    /* Retire the owner's cache aliases before any preserved-image work.
     * This is the same full publication/disable boundary as the cold capsule
     * path, now also covering the original source and temporary scratch. */
    purge(UINT32_C(0x8c000000),UINT32_C(0x8d000000));
    *(volatile uint32_t *)(uintptr_t)UINT32_C(0xff00001c)=UINT32_C(0x808);
    __asm__ volatile("nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop":::"memory");
    /* GIN has already completed the original mapped read with every SD
     * block CRC. Preserve only this exact whole original source before the
     * child entry replaces its high RAM with the stage. Publish both source
     * and scratch aliases first; a later RAM purge must not overwrite the
     * fresh uncached copy with an old dirty line. No old owner/DMA resumes. */
    bool warm_native=false;
    if(kind==KUI_SMASH_MENU_CHILD_NATIVE && !adopt_native &&
       kui_cold_image_capable(probe,sizeof(probe),KUI_SMASH_NATIVE_ENGINE_BYTES)) {
        if(kui_cold_load_crc32(0,(const void *)(uintptr_t)UINT32_C(0xace00000),
            KUI_SMASH_NATIVE_ENGINE_BYTES)==KUI_SMASH_NATIVE_ENGINE_CRC32) {
            (void)kui_cold_load_call3((uintptr_t)memcpy,
                KUI_COLD_IMAGE_SCRATCH|UINT32_C(0x20000000),UINT32_C(0xace00000),KUI_SMASH_NATIVE_ENGINE_BYTES);
            warm_native=true;
        }
    }
    uint8_t *out=(void *)(uintptr_t)(adopt_native?UINT32_C(0xacc00000):UINT32_C(0xac010000));
    /* SAMPLE has no injectable GINS context. Bind its complete original
     * bytes before retiring it; only the exact captured folder selects CE. */
    if(kind!=KUI_SMASH_MENU_CHILD_NATIVE &&
       kui_cold_load_crc32(0,out,KUI_SMASH_CE_SAMPLE_BYTES)!=KUI_SMASH_CE_SAMPLE_CRC32)
        stopped(16u,kind);
    memcpy(out,probe,512u);
    /* Each authenticated physical extent keeps one bounded raw read open.
     * The stream still verifies every block's CRC16 before copying it, and
     * the exact padded payload CRC below remains mandatory. */
    for(uint32_t block=1u;block<s->block_count;) {
        uint32_t end=block<s->first_blocks?s->first_blocks:s->block_count;
        uint32_t lba=block<s->first_blocks?s->first_lba+block:s->second_lba+block-s->first_blocks;
        result=kui_cold_load_call3((uintptr_t)read_card,lba,
            (uintptr_t)(out+block*512u),end-block);
        if(result)stopped(10u,block);
        block=end;
    }
#if KUI_SMASH_PHASE_DIAGNOSTIC
    smash_outer_trace_mark(KUI_SMASH_PHASE_CAPSULE_LOADED);
#endif
    uint32_t crc=kui_cold_load_crc32(0,out,s->padded_bytes);
    if(crc!=s->payload_crc32)stopped(11u,crc);
    if(kind!=KUI_SMASH_MENU_CHILD_NATIVE) {
        if(!kui_smash_ce_gin_capable(out,s->payload_bytes,kind))stopped(18u,kind);
        struct kui_smash_ce_gin_seed *gin=(void *)(out+KUI_SMASH_CE_GIN_OFFSET);
        if(!kui_smash_ce_gin_capture(gin,frame,(uint32_t)(uintptr_t)frame,ccr))stopped(19u,kind);
    } else if(adopt_native) {
        if(!kui_smash_native_gin_capable(out,s->payload_bytes))stopped(20u,1u);
        struct kui_smash_native_gin_seed *gin=(void *)(out+KUI_SMASH_NATIVE_GIN_OFFSET);
        if(!kui_smash_native_gin_capture(gin,frame,(uint32_t)(uintptr_t)frame,ccr))stopped(20u,2u);
    } else {
        /* A stripped capability cannot turn this fixed temporary-RAM entry
         * into the legacy lower-MDE entry. Current cold native capsules own
         * this gap as zero; a claimed typed continuation requires adoption. */
        for(unsigned i=0u;i<KUI_SMASH_NATIVE_GIN_BYTES;i++)
            if(out[KUI_SMASH_NATIVE_GIN_OFFSET+i])stopped(20u,3u);
    }
    if(warm_native) {
        size_t at=kui_cold_image_seed_find(out,s->payload_bytes);
        /* Missing or ambiguous slots retain ordinary source loading. This
         * optional hint cannot bypass the new stage's fresh full SHA owner
         * proof; the disk capsule itself remains unchanged. */
        if(at!=SIZE_MAX) {
            struct kui_cold_image_seed *seed=(void *)(out+at);
            if(!kui_cold_image_seed_configure(seed,KUI_COLD_IMAGE_SCRATCH,
                KUI_SMASH_NATIVE_ENGINE_BYTES,KUI_SMASH_NATIVE_ENGINE_CRC32))stopped(17u,(uint32_t)at);
        }
    }
    uint32_t seed_bytes;
    if(kind==KUI_SMASH_MENU_CHILD_NATIVE) {
        struct kui_smash_native_menu_seed *destination=(void *)(out+s->menu_seed_offset);
        if(!kui_smash_native_menu_seed_blank(destination))stopped(12u,s->menu_seed_offset);
        memcpy(destination,&captured.native,sizeof(captured.native));
        if(!kui_smash_native_menu_seed_valid(destination))stopped(12u,0u);
        seed_bytes=sizeof(captured.native);
    } else {
        struct kui_smash_ce_child_seed *destination=(void *)(out+s->menu_seed_offset);
        if(!kui_smash_ce_child_blank(destination) || destination->profile!=kind)
            stopped(12u,s->menu_seed_offset);
        memcpy(destination,&captured.ce,sizeof(captured.ce));
        if(!kui_smash_ce_child_valid(destination))stopped(12u,kind);
        seed_bytes=sizeof(captured.ce);
    }
    /* The two source-owned child seeds are adjacent. Avoid rescanning an
     * uncached executable to rediscover the return field at every launch. */
    uint32_t return_offset=s->menu_seed_offset+seed_bytes;
    if(return_offset>s->payload_bytes || sizeof(struct kui_smash_menu_return_seed)>s->payload_bytes-return_offset)
        stopped(13u,return_offset);
    struct kui_smash_menu_return_seed *returning=(void *)(out+return_offset);
    if(!kui_smash_menu_return_seed_blank(returning) ||
       !kui_smash_menu_return_seed_configure(returning,&kui_smash_menu_loader_runtime.returning.outer))
        stopped(13u,return_offset);
    if(return_ce) {
#if KUI_SMASH_CARD_SESSION && KUI_SMASH_PHASE_DIAGNOSTIC
        if(kui_smash_menu_loader_runtime.returning.flags!=KUI_SMASH_MENU_RETURN_CONTEXT)
            stopped(26u,1u);
        memcpy(returning,&kui_smash_menu_loader_runtime.returning,sizeof(*returning));
        uint32_t video_offset=return_offset+sizeof(*returning);
        if(video_offset>s->payload_bytes || sizeof(struct kui_smash_phase_video_seed)>s->payload_bytes-video_offset)
            stopped(26u,video_offset);
        struct kui_smash_phase_video_seed *video=(void *)(out+video_offset);
        if(!kui_smash_phase_video_blank(video) ||
           !kui_smash_phase_video_valid(&kui_smash_menu_loader_phase_video))stopped(26u,2u);
        memcpy(video,&kui_smash_menu_loader_phase_video,sizeof(*video));
#else
        stopped(26u,3u);
#endif
    }
#if KUI_SMASH_PHASE_DIAGNOSTIC
    if(kind==KUI_SMASH_MENU_CHILD_NATIVE) {
    /* Diagnostic children add TIM1 immediately after the two existing seeds.
     * Authenticate the complete capsule before changing any trace byte. */
    uint32_t trace_offset=return_offset+sizeof(*returning);
    if(trace_offset>s->payload_bytes || sizeof(struct kui_smash_phase_trace)>s->payload_bytes-trace_offset)
        stopped(14u,trace_offset);
    struct kui_smash_phase_trace *trace=(void *)(out+trace_offset);
    if(!kui_smash_phase_trace_blank(trace) ||
       !smash_outer_trace_valid())stopped(14u,trace_offset);
    trace->count=kui_smash_menu_loader_phase_trace.count;
    memcpy(trace->entries,kui_smash_menu_loader_phase_trace.entries,
        trace->count*sizeof(trace->entries[0]));
    uint32_t video_offset=trace_offset+sizeof(*trace);
    if(video_offset>s->payload_bytes || sizeof(struct kui_smash_phase_video_seed)>s->payload_bytes-video_offset)
        stopped(15u,video_offset);
    struct kui_smash_phase_video_seed *video=(void *)(out+video_offset);
    if(!kui_smash_phase_video_blank(video) ||
       !kui_smash_phase_video_valid(&kui_smash_menu_loader_phase_video))stopped(15u,video_offset);
    memcpy(video,&kui_smash_menu_loader_phase_video,sizeof(*video));
    }
#endif
    hardware_quiescent();
#if KUI_SMASH_CARD_SESSION
    /* The immutable capsule was completely checked before this launch-local
     * slot changes. The high stage validates the live receipt before I/O. */
    if(s->payload_bytes<KUI_SMASH_CARD_RECEIPT_OFFSET+KUI_SMASH_CARD_RECEIPT_BYTES)
        stopped(25u,s->payload_bytes);
    memcpy(out+KUI_SMASH_CARD_RECEIPT_OFFSET,&kui_smash_menu_loader_runtime.card_receipt,
        KUI_SMASH_CARD_RECEIPT_BYTES);
#endif
    firmware_restore();
    kui_smash_menu_loader_diagnostic[3]=kind==KUI_SMASH_MENU_CHILD_NATIVE?captured.native.selection:0x100u+kind;
#if KUI_SMASH_PHASE_DIAGNOSTIC
    /* Write the final sample directly into the already authenticated child;
     * the high stage copies it before loading its original owner bootstrap. */
    if(kind==KUI_SMASH_MENU_CHILD_NATIVE) {
        uint32_t trace_offset=return_offset+sizeof(*returning);
        struct kui_smash_phase_trace *trace=(void *)(out+trace_offset);
        kui_smash_phase_trace_mark(trace,KUI_SMASH_PHASE_CHILD_JUMP);
    }
#endif
    uint32_t entry=adopt_native?UINT32_C(0xacc00000):UINT32_C(0xac010000);
    __asm__ volatile("jmp @%0\nnop"::"r"(entry):"memory");
    __builtin_unreachable();
}
