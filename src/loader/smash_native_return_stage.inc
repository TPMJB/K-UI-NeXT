/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_menu_return_loader.h"
#include "kui/smash_native_return.h"
#include "smash_menu_return_symbols.h"
extern const struct kui_smash_menu_return_seed smash_return_seed __asm__("__smash_menu_return_seed");
extern const uint8_t __smash_return_raw_start[] __asm__("__smash_return_raw_start");
extern const uint8_t __smash_return_raw_end[] __asm__("__smash_return_raw_end");
extern const uint8_t __smash_return_code_start[] __asm__("__smash_return_code_start");
extern const uint8_t __smash_return_code_end[] __asm__("__smash_return_code_end");
static struct kui_smash_menu_return_runtime smash_return_prepared;
static void smash_return_purge(uint32_t begin,uint32_t end) {
    for(uint32_t p=begin&~31u;p<end;p+=32u)__asm__ volatile("ocbp @%0"::"r"(p):"memory");
}
static bool smash_return_run(uint32_t lba,uint32_t count) {
    return !count?!lba:lba && (uint64_t)lba>=manifest.partition_start &&
        (uint64_t)lba+count<=manifest.partition_end && (uint64_t)lba+count<=manifest.card_sectors;
}
void kui_smash_native_return_stage_prepare(void) {
    struct kui_smash_menu_return_seed seed;memcpy(&seed,&smash_return_seed,sizeof(seed));
    const struct kui_smash_menu_loader_seed *p=&seed.outer;
    if(!kui_smash_menu_return_seed_valid(&seed) || seed.flags!=KUI_SMASH_MENU_RETURN_CONFIGURED ||
       manifest.boot_bytes!=KUI_SMASH_NATIVE_ENGINE_BYTES || boot_crc!=KUI_SMASH_NATIVE_ENGINE_CRC32 ||
       manifest.ip_crc32!=KUI_SMASH_NATIVE_IP_CRC32 ||
       !smash_return_run(p->first_lba,p->first_blocks) ||
       !smash_return_run(p->second_lba,p->block_count-p->first_blocks))
        stopped("SMASH RETURN PLAN REFUSED",seed.flags);
    if((size_t)(__smash_return_raw_end-__smash_return_raw_start)!=512u ||
       (size_t)(__smash_return_code_end-__smash_return_code_start)!=KUI_SMASH_RETURN_CODE_BYTES ||
       kui_retail_crc32(0,__smash_return_raw_start,512u)!=KUI_SMASH_RETURN_RAW_CRC32 ||
       kui_retail_crc32(0,__smash_return_code_start,KUI_SMASH_RETURN_CODE_BYTES)!=KUI_SMASH_RETURN_CODE_CRC32)
        stopped("SMASH RETURN LINKED CODE CHANGED",0u);
    smash_return_prepared.returning=seed;
#if KUI_SMASH_CARD_SESSION
    smash_return_prepared.card_receipt=smash_card_receipt;
#endif
#if KUI_SMASH_PHASE_DIAGNOSTIC
    /* The child received this original KOS capture from the authenticated
     * outer handoff. Retain it before the game reclaims the high stage. */
    memcpy(&smash_return_prepared.phase_video,&smash_phase_video_seed,
        sizeof(smash_return_prepared.phase_video));
    if(!kui_smash_phase_video_valid(&smash_return_prepared.phase_video))
        stopped("SMASH RETURN ORIGINAL VIDEO REFUSED",0u);
#endif
    smash_return_prepared.raw_crc32=KUI_SMASH_RETURN_RAW_CRC32;
    smash_return_prepared.stop_entry=KUI_SMASH_RETURN_STOP_P2;
    const uint32_t address[9]={0x8c0000bcu,0x8c0000c0u,0x8c0000e0u,
        0x8c001000u,0x8c001004u,0x8c001008u,0x8c0010f0u,0x8c0010f4u,0x8c0010f8u};
    for(unsigned i=0;i<9u;i++) {
        smash_return_purge(address[i],address[i]+4u);
        smash_return_prepared.firmware[i]=*(volatile const uint32_t *)(uintptr_t)(address[i]|UINT32_C(0x20000000));
    }
    smash_return_prepared.code_crc32=kui_smash_menu_return_runtime_crc(KUI_SMASH_RETURN_CODE_CRC32,&smash_return_prepared);
}
/* ABXY+Start must retain the original emulator options menu: its Save Game
 * action commits cartridge SRAM. Only the explicit Return to Menu path
 * reaches the checked application reset hook installed below. */
__asm__(".global __kui_smash_native_original_options\n.set __kui_smash_native_original_options,1\n");
void kui_smash_native_return_stage_publish(void) {
    if(!kui_smash_menu_return_seed_valid(&smash_return_prepared.returning) ||
       kui_retail_crc32(0,(const void *)(uintptr_t)KUI_SMASH_NATIVE_RESET_ENTRY,42u)!=UINT32_C(0xd71c4066))
        relay_stopped("SMASH RETURN SOURCE CHANGED",KUI_SMASH_NATIVE_RESET_ENTRY);
    smash_return_purge(UINT32_C(0x8c008100),UINT32_C(0x8c00c000));
    memset((void *)(uintptr_t)UINT32_C(0xac008100),0,UINT32_C(0x3f00));
    memcpy((void *)(uintptr_t)UINT32_C(0xac008100),__smash_return_raw_start,512u);
    memcpy((void *)(uintptr_t)UINT32_C(0xac008300),__smash_return_code_start,KUI_SMASH_RETURN_CODE_BYTES);
    if(kui_retail_crc32(0,(const void *)(uintptr_t)UINT32_C(0xac008100),512u)!=KUI_SMASH_RETURN_RAW_CRC32 ||
       kui_retail_crc32(0,(const void *)(uintptr_t)UINT32_C(0xac008300),KUI_SMASH_RETURN_CODE_BYTES)!=KUI_SMASH_RETURN_CODE_CRC32)
        relay_stopped("SMASH RETURN CODE COPY CHANGED",0u);
    memcpy((void *)(uintptr_t)KUI_SMASH_RETURN_RUNTIME_P2,&smash_return_prepared,sizeof(smash_return_prepared));
    if(memcmp((const void *)(uintptr_t)KUI_SMASH_RETURN_RUNTIME_P2,&smash_return_prepared,sizeof(smash_return_prepared)))
        relay_stopped("SMASH RETURN RUNTIME COPY CHANGED",0u);
    /* This application reset is earlier than its five cleanup helpers and
     * the later GINS executor. No SDK shutdown can orphan the old worker. */
    readonly_patch_entry(KUI_SMASH_NATIVE_RESET_ENTRY,UINT32_C(0xac008300));
}
void kui_smash_native_return_stage_bind(uint32_t worker,uint32_t bytes,uint32_t span) {
    struct kui_smash_native_return_exports found={0};unsigned count=0u;
    const uint8_t *p=(const void *)(uintptr_t)worker;
    for(uint32_t at=0;at+sizeof(found)<=bytes;at+=4u) {
        uint32_t magic;memcpy(&magic,p+at,4u);
        if(magic!=KUI_SMASH_NATIVE_RETURN_MAGIC)continue;
        struct kui_smash_native_return_exports e;memcpy(&e,p+at,sizeof(e));
        if(e.version!=1u || e.bytes!=sizeof(e) || !passive_entry(e.entry,worker,bytes))continue;
        found=e;++count;
    }
    volatile struct kui_smash_menu_return_runtime *runtime=(void *)(uintptr_t)KUI_SMASH_RETURN_RUNTIME_P2;
    if(count!=1u || span!=KUI_VMU_NATIVE_PASSIVE_SPAN || runtime->retire_entry ||
       memcmp((const void *)runtime,&smash_return_prepared,sizeof(smash_return_prepared)))
        native_report(9u,UINT32_C(0x534d5254));
    runtime->retire_entry=found.entry;
    uint32_t sealed=kui_smash_menu_return_runtime_crc(KUI_SMASH_RETURN_CODE_CRC32,(const void *)runtime);
    runtime->code_crc32=sealed;
    if(runtime->retire_entry!=found.entry || runtime->code_crc32!=sealed)native_report(9u,UINT32_C(0x534d5255));
}
