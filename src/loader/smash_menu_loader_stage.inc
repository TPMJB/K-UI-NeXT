/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_menu_loader.h"
#include "kui/smash_menu_loader_runtime.h"
#include "kui/smash_menu_loader_capability.h"
#include "kui/smash_native_child.h"
#include "kui/native_standalone_foreground.h"
#include "kui/hash.h"
#include "smash_menu_loader_symbols.h"
#if KUI_SMASH_PHASE_DIAGNOSTIC
#include "kui/smash_phase_video.h"
__asm__(".section .rodata.smash_phase_outer,\"a\"\n.balign 4\n"
    ".global __smash_phase_outer_profile\n__smash_phase_outer_profile:\n"
    ".long 0x5349554b,0x3154504f,1,16\n.previous\n");
extern const volatile uint32_t smash_phase_outer_profile[4] __asm__("__smash_phase_outer_profile");
static struct kui_smash_phase_video_seed smash_menu_prepared_video;
static void smash_menu_loader_prepare(void) __attribute__((noipa));
static void smash_menu_loader_publish(void) __attribute__((noipa));
#endif
#if !KUI_NATIVE_STANDALONE_FOREGROUND || !KUI_NATIVE_OCRAM_BOOTSTRAP || \
    KUI_VC2_CE_VMU || defined(KUI_RETAIL_CE) || KUI_NATIVE_VMU_ONLY || \
    KUI_VMU_MAPLE_OBSERVE || KUI_NATIVE_READAHEAD || KUI_RETAIL_ZSO
#error "Smash menu handoff requires its separate native foreground cold module"
#endif
__asm__(".global __kui_smash_menu_loader_stage\n.set __kui_smash_menu_loader_stage,1\n"
    ".section .rodata.smash_menu_collection,\"a\"\n.balign 4\n"
    ".global __smash_menu_collection\n__smash_menu_collection:\n"
    ".long 0x5349554b,0x31434d53,1,224,3,7\n.zero 200\n.previous\n"
    ".section .rodata.smash_menu_return_seed,\"a\"\n.balign 4\n"
    ".global __smash_menu_return_seed\n__smash_menu_return_seed:\n"
    ".long 0x5349554b,0x3154524d,1,368\n.zero 352\n"
#if KUI_SMASH_PHASE_DIAGNOSTIC
    ".global __smash_menu_return_video_seed\n__smash_menu_return_video_seed:\n"
    ".long 0x5349554b,0x31444956,1,80\n.zero 64\n"
#endif
    ".previous\n");
extern const struct kui_smash_menu_collection smash_menu_collection __asm__("__smash_menu_collection");
extern const struct kui_smash_menu_return_seed smash_menu_return_seed __asm__("__smash_menu_return_seed");
#if KUI_SMASH_PHASE_DIAGNOSTIC
extern const struct kui_smash_phase_video_seed smash_menu_return_video_seed __asm__("__smash_menu_return_video_seed");
#endif
const volatile struct kui_smash_menu_loader_capability kui_smash_menu_loader_capability
    __attribute__((used,section(".rodata.smash_menu_loader_capability"),aligned(4)))={
    KUI_SMASH_MENU_LOADER_CAP_WORDS};
extern const uint8_t __smash_menu_raw_start[] __asm__("__smash_menu_raw_start");
extern const uint8_t __smash_menu_raw_end[] __asm__("__smash_menu_raw_end");
extern const uint8_t __smash_menu_handoff_start[] __asm__("__smash_menu_handoff_start");
extern const uint8_t __smash_menu_handoff_end[] __asm__("__smash_menu_handoff_end");
static struct kui_smash_menu_loader_runtime smash_menu_prepared;
static const uint32_t smash_menu_firmware_address[9]={
    0x8c0000bcu,0x8c0000c0u,0x8c0000e0u,0x8c001000u,0x8c001004u,
    0x8c001008u,0x8c0010f0u,0x8c0010f4u,0x8c0010f8u};
static void smash_menu_loader_purge(uint32_t begin,uint32_t end) {
    for(uintptr_t at=begin&~UINT32_C(31);at<end;at+=32u)
        __asm__ volatile("ocbp @%0"::"r"(at):"memory");
}
static bool smash_menu_loader_run(const struct kui_smash_menu_loader_seed *s,uint32_t lba,uint32_t n) {
    if(!n)return !lba;
    return lba && (uint64_t)lba>=manifest.partition_start &&
        (uint64_t)lba+n<=manifest.partition_end && (uint64_t)lba+n<=manifest.card_sectors &&
        n<=s->block_count;
}
static bool smash_menu_loader_plans_disjoint(const struct kui_smash_menu_loader_seed *a,
    const struct kui_smash_menu_loader_seed *b) {
    const uint32_t al[2]={a->first_lba,a->second_lba},bl[2]={b->first_lba,b->second_lba};
    const uint32_t an[2]={a->first_blocks,a->block_count-a->first_blocks};
    const uint32_t bn[2]={b->first_blocks,b->block_count-b->first_blocks};
    for(unsigned i=0;i<2u;i++)for(unsigned j=0;j<2u;j++)
        if(an[i] && bn[j] && (uint64_t)al[i]<(uint64_t)bl[j]+bn[j] &&
           (uint64_t)bl[j]<(uint64_t)al[i]+an[i])return false;
    return true;
}
static void smash_menu_loader_prepare(void) {
    struct kui_smash_menu_return_seed returning;memcpy(&returning,&smash_menu_return_seed,sizeof(returning));
#if KUI_SMASH_PHASE_DIAGNOSTIC
    struct kui_smash_phase_video_seed video;memcpy(&video,&smash_menu_return_video_seed,sizeof(video));
    if(smash_phase_outer_profile[2]!=1u || smash_phase_outer_profile[3]!=16u ||
       !kui_smash_menu_return_seed_valid(&returning))
        stopped("SMASH TIMING ORIGINAL VIDEO REFUSED",0u);
    if(returning.flags==KUI_SMASH_MENU_RETURN_CONFIGURED) {
        if(!kui_smash_phase_video_blank(&video) ||
           !kui_smash_phase_video_capture(&smash_menu_prepared_video,display.regs))
            stopped("SMASH TIMING ORIGINAL VIDEO REFUSED",1u);
    } else {
        if(returning.flags!=KUI_SMASH_MENU_RETURN_CONTEXT || !kui_smash_phase_video_valid(&video))
            stopped("SMASH TIMING RETURN VIDEO REFUSED",returning.flags);
        smash_menu_prepared_video=video;
    }
#endif
    struct kui_smash_menu_collection collection;memcpy(&collection,&smash_menu_collection,sizeof(collection));
    const volatile struct kui_smash_menu_loader_capability *cap=&kui_smash_menu_loader_capability;
    if(cap->version!=2u || cap->bytes!=32u || cap->flags!=7u ||
       cap->loader_seed_bytes!=224u || cap->native_seed_bytes!=304u || cap->reserved!=64u)
        stopped("SMASH MENU CAPABILITY CHANGED",cap->flags);
    uint8_t digest[32];struct kui_sha256 hash;
    kui_sha256_init(&hash);kui_sha256_update(&hash,(const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,exec_bytes);
    kui_sha256_digest(&hash,digest);
    if(!kui_smash_native_child_menu_owner(manifest.ip_crc32,exec_bytes,digest) ||
       boot_crc!=KUI_SMASH_NATIVE_MENU_CRC32 || memcmp(manifest.bootfile,"1ST_READ.BIN",13u) ||
       memcmp(manifest.product,"MK-51146",9u) || manifest.storage_transport!=KUI_STORAGE_SCI ||
       manifest.reader!=KUI_RETAIL_READER_STANDARD || (manifest.flags&~KUI_RETAIL_IMAGE_BOOT_CRC) ||
       manifest.track_count+manifest.extent_count>KUI_NATIVE_FOREGROUND_SMASH_SLOTS ||
       !kui_smash_menu_collection_valid(&collection))
        stopped("SMASH MENU OWNER OR CAPSULE REFUSED",collection.flags);
    if(!kui_smash_menu_return_seed_valid(&returning) ||
       !smash_menu_loader_run(&returning.outer,returning.outer.first_lba,returning.outer.first_blocks) ||
       !smash_menu_loader_run(&returning.outer,returning.outer.second_lba,
            returning.outer.block_count-returning.outer.first_blocks))
        stopped("SMASH MENU RETURN PLAN REFUSED",returning.flags);
    for(unsigned i=0;i<KUI_SMASH_MENU_CHILD_COUNT;i++) {
        struct kui_smash_menu_loader_seed *s=&collection.plans[i];
        if(s->handoff_crc32 || s->raw_crc32 ||
           !smash_menu_loader_run(s,s->first_lba,s->first_blocks) ||
           !smash_menu_loader_run(s,s->second_lba,s->block_count-s->first_blocks) ||
           !smash_menu_loader_plans_disjoint(s,&returning.outer))
            stopped("SMASH MENU CHILD PLAN REFUSED",i);
    }
    if((size_t)(__smash_menu_raw_end-__smash_menu_raw_start)!=512u ||
       (size_t)(__smash_menu_handoff_end-__smash_menu_handoff_start)!=KUI_SMASH_MENU_HANDOFF_BYTES ||
       kui_retail_crc32(0u,__smash_menu_raw_start,512u)!=KUI_SMASH_MENU_RAW_CRC32 ||
       kui_retail_crc32(0u,__smash_menu_handoff_start,KUI_SMASH_MENU_HANDOFF_BYTES)!=KUI_SMASH_MENU_HANDOFF_CRC32)
        stopped("SMASH MENU LINKED HANDOFF CHANGED",0u);
    for(unsigned i=0;i<KUI_SMASH_MENU_CHILD_COUNT;i++) {
        struct kui_smash_menu_loader_seed *s=&collection.plans[i];
        s->handoff_crc32=KUI_SMASH_MENU_HANDOFF_CRC32;s->raw_crc32=KUI_SMASH_MENU_RAW_CRC32;
        if(!kui_smash_menu_loader_seed_seal(s))stopped("SMASH MENU CAPSULE SEAL REFUSED",i);
    }
    if(!kui_smash_menu_collection_seal(&collection))stopped("SMASH COLLECTION SEAL REFUSED",0u);
    smash_menu_prepared.collection=collection;
    smash_menu_prepared.returning=returning;
#if KUI_SMASH_CARD_SESSION
    smash_menu_prepared.card_receipt=smash_card_receipt;
#endif
    smash_menu_prepared.stop_entry=KUI_SMASH_MENU_STOP_P2;
    for(unsigned i=0;i<9u;i++) {
        uint32_t at=smash_menu_firmware_address[i];smash_menu_loader_purge(at,at+4u);
        smash_menu_prepared.firmware[i]=*(volatile const uint32_t *)(uintptr_t)(at|UINT32_C(0x20000000));
    }
    retail_display_line("ORIGINAL SMASH PACK MENU / NATIVE AND CE VMU");
}
static void smash_menu_loader_return_arguments(void) {
    const struct kui_smash_menu_return_seed *s=&smash_menu_prepared.returning;
    if(!kui_smash_menu_return_seed_valid(s))stopped("SMASH MENU RETURN ARGUMENTS REFUSED",0u);
    if(s->flags==KUI_SMASH_MENU_RETURN_CONFIGURED)return;
    /* Apply only after the complete original image and its source-owned
     * reader have passed the normal bootstrap relay checks. */
    smash_menu_loader_purge(UINT32_C(0x8c036a44),UINT32_C(0x8c038a44));
    smash_menu_loader_purge(UINT32_C(0x8c0080e0),UINT32_C(0x8c008100));
    if(!kui_smash_menu_return_seed_apply((void *)(uintptr_t)UINT32_C(0xac036a44),8192u,
       (uint8_t *)(uintptr_t)UINT32_C(0xac0080f0),s))
        stopped("SMASH MENU RETURN CONTEXT CHANGED",s->flags);
}
#if KUI_SMASH_CARD_SESSION
static void __attribute__((noipa)) smash_menu_loader_root_capture(void) {
    /* The source-owned ROOT GINS context has an empty directory and mode4.
     * Before the first menu instruction, IP still contains BOOT2's flags.
     * Reproduce only the original pure folder("")/mode(4) setters in a local
     * copy: spaces in12 folder bytes, then1/4; preserve opaque tail bytes.
     * The original-source equivalence test executes both setters. */
    const uint8_t *context=(const void *)(uintptr_t)UINT32_C(0xac036a44);
    smash_menu_loader_purge(UINT32_C(0x8c036a40),UINT32_C(0x8c036da0));
    smash_menu_loader_purge(UINT32_C(0x8c0080e0),UINT32_C(0x8c008100));
    uint8_t root[16];memcpy(root,(const void *)(uintptr_t)UINT32_C(0xac0080f0),sizeof(root));
    if(((const uint32_t *)(const void *)context)[6]!=4u || context[UINT32_C(0x340)])
        stopped("SMASH MENU ROOT CONTEXT CAPTURE REFUSED",1u);
    memset(root,' ',12u);root[12]=1u;root[13]=4u;
    if(!kui_smash_menu_return_seed_capture(&smash_menu_prepared.returning,context+256u,root))
        stopped("SMASH MENU ROOT CONTEXT CAPTURE REFUSED",0u);
}
#endif
static void smash_menu_loader_publish(void) {
    if(!kui_smash_menu_collection_valid(&smash_menu_prepared.collection) ||
       smash_menu_prepared.collection.plans[0].handoff_crc32!=KUI_SMASH_MENU_HANDOFF_CRC32 ||
       smash_menu_prepared.collection.plans[0].raw_crc32!=KUI_SMASH_MENU_RAW_CRC32 ||
       !kui_smash_menu_return_seed_valid(&smash_menu_prepared.returning) ||
       memcmp(smash_menu_prepared.firmware,(const void *)(uintptr_t)KUI_SMASH_MENU_FIRMWARE_P2,36u))
        stopped("SMASH MENU FIRMWARE CAPTURE CHANGED",KUI_SMASH_MENU_FIRMWARE_P2);
#if KUI_SMASH_CARD_SESSION
    /* This is the complete source-verified outer image after BOOT2 and
     * after return_arguments has applied any prior ROOT context. Capture
     * before the original menu can select a child and replace IP's folder.
     * Native children still receive a freshly configured plan; only the
     * explicit CE-return capability receives this sealed ROOT context. */
    smash_menu_loader_root_capture();
#endif
    /* BOOT2 has finished. Its original entry and the complete lower reader
     * are verified. Only now reclaim the retired BOOT1 code/state/stack. */
    smash_menu_loader_purge(KUI_SMASH_MENU_LOADER_RAW_P1,KUI_SMASH_MENU_LOADER_STACK-0x20000000u);
    memset((void *)(uintptr_t)KUI_SMASH_MENU_LOADER_RAW_P2,0u,
        KUI_SMASH_MENU_LOADER_STACK-KUI_SMASH_MENU_LOADER_RAW_P2);
    memcpy((void *)(uintptr_t)KUI_SMASH_MENU_LOADER_RAW_P2,__smash_menu_raw_start,512u);
    memcpy((void *)(uintptr_t)KUI_SMASH_MENU_LOADER_P2,__smash_menu_handoff_start,KUI_SMASH_MENU_HANDOFF_BYTES);
    if(kui_retail_crc32(0u,(const void *)(uintptr_t)KUI_SMASH_MENU_LOADER_RAW_P2,512u)!=KUI_SMASH_MENU_RAW_CRC32 ||
       kui_retail_crc32(0u,(const void *)(uintptr_t)KUI_SMASH_MENU_LOADER_P2,KUI_SMASH_MENU_HANDOFF_BYTES)!=KUI_SMASH_MENU_HANDOFF_CRC32)
        stopped("SMASH MENU HANDOFF COPY CHANGED",0u);
    memcpy((void *)(uintptr_t)KUI_SMASH_MENU_RUNTIME_P2,&smash_menu_prepared,sizeof(smash_menu_prepared));
    if(memcmp((const void *)(uintptr_t)KUI_SMASH_MENU_RUNTIME_P2,&smash_menu_prepared,sizeof(smash_menu_prepared)))
        stopped("SMASH MENU RUNTIME COPY CHANGED",KUI_SMASH_MENU_RUNTIME_P2);
#if KUI_SMASH_PHASE_DIAGNOSTIC
    if(!kui_smash_phase_video_valid(&smash_menu_prepared_video))
        stopped("SMASH TIMING ORIGINAL VIDEO CHANGED",0u);
    memcpy((void *)(uintptr_t)KUI_SMASH_MENU_PHASE_VIDEO_P2,&smash_menu_prepared_video,sizeof(smash_menu_prepared_video));
    if(memcmp((const void *)(uintptr_t)KUI_SMASH_MENU_PHASE_VIDEO_P2,&smash_menu_prepared_video,sizeof(smash_menu_prepared_video)))
        stopped("SMASH TIMING VIDEO COPY CHANGED",KUI_SMASH_MENU_PHASE_VIDEO_P2);
#endif
}
