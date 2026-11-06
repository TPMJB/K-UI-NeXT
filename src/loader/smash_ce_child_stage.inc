/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_ce_child.h"
#include "kui/smash_native_child.h"
#include "kui/smash_menu_return.h"
#include "kui/hash.h"
#if KUI_SMASH_CE_RETURN
#include "kui/smash_ce_return_loader.h"
#include "kui/smash_ce_transport.h"
#include "kui/vc2_ce_vmu_persist.h"
#include "smash_ce_return_view_symbols.h"
#include "vc2_ce_vmu_symbols.h"
#include "vc2_late_symbols.h"
__asm__(".global __kui_smash_ce_return_transport\n.set __kui_smash_ce_return_transport,1\n");
#endif
#if KUI_SMASH_CE_RETURN_PROBE
#include "kui/smash_ce_return_probe.h"
#include "smash_ce_return_probe_symbols.h"
#include "vc2_ce_vmu_symbols.h"
#endif
#if !KUI_VC2_CE_VMU || !KUI_VC2_VMU_PERSIST || !KUI_VC2_VMU_LATE || \
    !KUI_NATIVE_STANDALONE_FOREGROUND || !KUI_NATIVE_OCRAM_BOOTSTRAP || \
    defined(KUI_RETAIL_CE) || KUI_SMASH_MENU_LOADER || KUI_NATIVE_READAHEAD
#error "Smash CE child requires its separate persistent foreground backend"
#endif
#if KUI_SMASH_SWIRL_VMU
#define SMASH_CE_CHILD_PROFILE 2
__asm__(".global __kui_smash_ce_child\n.set __kui_smash_ce_child,2\n");
#define SMASH_CE_CHILD_PROFILE_ASM "2"
#else
#define SMASH_CE_CHILD_PROFILE 1
__asm__(".global __kui_smash_ce_child\n.set __kui_smash_ce_child,1\n");
#define SMASH_CE_CHILD_PROFILE_ASM "1"
#endif
/* The launch-local IP is the complete original menu folder output. SAMPLE
 * contains no GINS context, and both original SAMPLE files are identical. */
__asm__(".section .rodata.smash_ce_child,\"a\"\n.balign 4\n"
    ".global __smash_ce_child_seed\n__smash_ce_child_seed:\n"
    ".long 0x5349554b,0x31454353,1,64," SMASH_CE_CHILD_PROFILE_ASM ",0,0,0\n.zero 32\n"
    ".global __smash_menu_return_seed\n__smash_menu_return_seed:\n"
    ".long 0x5349554b,0x3154524d,1,368\n.zero 352\n"
#if KUI_SMASH_CE_RETURN
    ".global __smash_ce_return_video_seed\n__smash_ce_return_video_seed:\n"
    ".long 0x5349554b,0x31444956,1,80\n.zero 64\n"
#endif
    ".previous\n");
extern const volatile struct kui_smash_ce_child_seed smash_ce_child_seed __asm__("__smash_ce_child_seed");
extern const volatile struct kui_smash_menu_return_seed smash_ce_return_seed __asm__("__smash_menu_return_seed");
static struct kui_smash_ce_child_seed smash_ce_prepared;
#if KUI_SMASH_CE_RETURN
extern const volatile struct kui_smash_phase_video_seed smash_ce_return_video_seed __asm__("__smash_ce_return_video_seed");
extern const uint8_t __vc2_vmu_blob_start[] __asm__("__vc2_vmu_blob_start");
extern const uint8_t __vc2_vmu_blob_end[] __asm__("__vc2_vmu_blob_end");
static struct kui_smash_ce_return_runtime smash_ce_return_prepared;
static uint32_t smash_ce_return_transport_crc;
#endif
#if KUI_SMASH_CE_RETURN_PROBE
static struct kui_smash_ce_return_probe_runtime smash_ce_return_prepared;
extern const uint8_t __smash_ce_return_probe_start[] __asm__("__smash_ce_return_probe_start");
extern const uint8_t __smash_ce_return_probe_end[] __asm__("__smash_ce_return_probe_end");
#endif
static void smash_ce_child_prepare(void) {
    memcpy(&smash_ce_prepared,(const void *)&smash_ce_child_seed,sizeof(smash_ce_prepared));
    struct kui_smash_menu_return_seed returning;
    memcpy(&returning,(const void *)&smash_ce_return_seed,sizeof(returning));
    uint8_t digest[32];struct kui_sha256 hash;
    kui_sha256_init(&hash);kui_sha256_update(&hash,(const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,exec_bytes);
    kui_sha256_digest(&hash,digest);
    if(!kui_smash_ce_child_valid(&smash_ce_prepared) ||
       smash_ce_prepared.profile!=SMASH_CE_CHILD_PROFILE ||
       !kui_smash_ce_child_owner(exec_bytes,digest) || boot_crc!=KUI_SMASH_CE_SAMPLE_CRC32 ||
       manifest.boot_bytes!=KUI_SMASH_CE_SAMPLE_BYTES || manifest.boot_crc32!=KUI_SMASH_CE_SAMPLE_CRC32 ||
       manifest.ip_crc32!=KUI_SMASH_NATIVE_IP_CRC32 || strcmp(manifest.product,"MK-51146") ||
       strcmp(manifest.bootfile,"SAMPLE.BIN") ||
       manifest.storage_transport!=KUI_STORAGE_SCI || manifest.reader!=KUI_RETAIL_READER_STANDARD ||
       (manifest.flags&~KUI_RETAIL_IMAGE_BOOT_CRC) || manifest.track_count+manifest.extent_count>48u ||
       !kui_smash_menu_return_seed_valid(&returning) || returning.flags!=
#if KUI_SMASH_CE_RETURN
       KUI_SMASH_MENU_RETURN_CONTEXT)
#else
       KUI_SMASH_MENU_RETURN_CONFIGURED)
#endif
        stopped("SMASH CE CHILD OWNER REFUSED",smash_ce_prepared.profile);
#if KUI_SMASH_CE_RETURN
    struct kui_smash_ce_transport transport;
    if((size_t)(__vc2_vmu_blob_end-__vc2_vmu_blob_start)!=KUI_SMASH_CE_TRANSPORT_FILE_BYTES ||
       !kui_smash_ce_transport_static(__vc2_vmu_blob_start,KUI_SMASH_CE_TRANSPORT_FILE_BYTES,&transport) ||
       transport.code_bytes!=KUI_SMASH_CE_RETURN_CODE_BYTES ||
       transport.runtime_offset!=KUI_SMASH_CE_RETURN_RUNTIME_OFFSET ||
       kui_retail_crc32(0u,__vc2_vmu_blob_start,KUI_SMASH_CE_TRANSPORT_FILE_BYTES)!=KUI_VC2_VMU_PREPARED_CRC32)
        stopped("SMASH CE RETURN TRANSPORT CHANGED",smash_ce_prepared.profile);
    smash_ce_return_prepared=(struct kui_smash_ce_return_runtime){
        .returning=returning,.raw_crc32=KUI_VC2_LATE_RAW_CRC32,.profile=SMASH_CE_CHILD_PROFILE,
        .helper_bytes=KUI_SMASH_CE_RETURN_HELPER_BYTES,.helper_crc32=KUI_SMASH_CE_RETURN_HELPER_CRC32,
        .view=KUI_SMASH_CE_RETURN_VIEW_INITIALIZER};
    memcpy(&smash_ce_return_prepared.phase_video,(const void *)&smash_ce_return_video_seed,
        sizeof(smash_ce_return_prepared.phase_video));
    if(!kui_smash_phase_video_valid(&smash_ce_return_prepared.phase_video))
        stopped("SMASH CE RETURN ORIGINAL VIDEO REFUSED",0u);
    const uint32_t address[9]={0x8c0000bcu,0x8c0000c0u,0x8c0000e0u,
        0x8c001000u,0x8c001004u,0x8c001008u,0x8c0010f0u,0x8c0010f4u,0x8c0010f8u};
    for(unsigned i=0u;i<9u;i++) {
        __asm__ volatile("ocbp @%0"::"r"(address[i]):"memory");
        smash_ce_return_prepared.firmware[i]=*(volatile const uint32_t *)(uintptr_t)(address[i]|UINT32_C(0x20000000));
    }
#endif
#if KUI_SMASH_CE_RETURN_PROBE
    if((size_t)(__smash_ce_return_probe_end-__smash_ce_return_probe_start)!=KUI_SMASH_CE_RETURN_CODE_BYTES ||
       kui_retail_crc32(0,__smash_ce_return_probe_start,KUI_SMASH_CE_RETURN_CODE_BYTES)!=KUI_SMASH_CE_RETURN_CODE_CRC32)
        stopped("SMASH CE RETURN OBSERVER CHANGED",smash_ce_prepared.profile);
    smash_ce_return_prepared=(struct kui_smash_ce_return_probe_runtime){
        .returning=returning,.raw_crc32=KUI_SMASH_CE_RETURN_RAW_CRC32,
        .helper_bytes=KUI_VC2_VMU_BLOB_BYTES,.helper_crc32=KUI_VC2_VMU_BLOB_CRC32,
        .report_entry=KUI_SMASH_CE_RETURN_REPORT_P2,.trace_address=KUI_SMASH_CE_RETURN_TRACE_P2,
        .profile=SMASH_CE_CHILD_PROFILE};
    smash_ce_return_prepared.code_crc32=kui_smash_ce_return_probe_crc(
        KUI_SMASH_CE_RETURN_CODE_CRC32,&smash_ce_return_prepared);
#endif
}
static void smash_ce_child_relay(const uint32_t frame[21]) {
    if(!kui_smash_ce_child_valid(&smash_ce_prepared) ||
       !kui_smash_native_child_frame((uint32_t *)(uintptr_t)frame,(uint32_t)(uintptr_t)frame))
        stopped("SMASH CE CHILD CPU STATE REFUSED",(uint32_t)(uintptr_t)frame);
    /* The complete original SAMPLE and low reader have passed BOOT2. Apply
     * only the captured original folder; the existing MAINANA path chooses
     * and validates its own kernel through the separate CE backend. */
    __asm__ volatile("ocbp @%0"::"r"(UINT32_C(0x8c0080e0)):"memory");
    memcpy((void *)(uintptr_t)UINT32_C(0xac0080f0),smash_ce_prepared.ip_tail,16u);
    if(memcmp((const void *)(uintptr_t)UINT32_C(0xac0080f0),smash_ce_prepared.ip_tail,16u))
        stopped("SMASH CE CHILD FOLDER PUBLICATION REFUSED",smash_ce_prepared.profile);
#if KUI_SMASH_CE_RETURN_PROBE
    for(uint32_t at=UINT32_C(0x8c008300);at<UINT32_C(0x8c00c000);at+=32u)
        __asm__ volatile("ocbp @%0"::"r"(at):"memory");
    memset((void *)(uintptr_t)UINT32_C(0xac008300),0,UINT32_C(0x3d00));
    memcpy((void *)(uintptr_t)UINT32_C(0xac008300),__smash_ce_return_probe_start,KUI_SMASH_CE_RETURN_CODE_BYTES);
    if(kui_retail_crc32(0,(const void *)(uintptr_t)UINT32_C(0xac008300),KUI_SMASH_CE_RETURN_CODE_BYTES)!=KUI_SMASH_CE_RETURN_CODE_CRC32)
        stopped("SMASH CE RETURN COPY CHANGED",smash_ce_prepared.profile);
    memcpy((void *)(uintptr_t)KUI_SMASH_CE_RETURN_RUNTIME_P2,&smash_ce_return_prepared,sizeof(smash_ce_return_prepared));
    if(memcmp((const void *)(uintptr_t)KUI_SMASH_CE_RETURN_RUNTIME_P2,&smash_ce_return_prepared,sizeof(smash_ce_return_prepared)))
        stopped("SMASH CE RETURN PLAN COPY CHANGED",smash_ce_prepared.profile);
#endif
}
