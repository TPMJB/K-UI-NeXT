/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_native_gin.h"
#include "kui/smash_native_child.h"
#include "kui/smash_native_menu.h"
__asm__(".global __kui_smash_native_gin_adoption\n.set __kui_smash_native_gin_adoption,1\n");
static struct kui_smash_native_gin_transfer smash_native_gin_saved;
extern const struct kui_smash_native_menu_seed smash_native_gin_menu_seed
    __asm__("__smash_native_menu_seed");
extern void kui_native_passive_publish(uint32_t,uint32_t);
extern void kui_smash_native_gin_arm_scope(const uint32_t *,uint32_t);
static void relay_stopped(const char *,uint32_t) __attribute__((noreturn));

/* Import every continuation byte before storage setup or any guest code can
 * reclaim the temporary capsule. Only the verifier's canonicalized MDE is
 * inspected by the existing immutable SDK/allocator proof below. */
static void __attribute__((noipa)) smash_native_gin_begin(void) {
    const struct kui_smash_native_gin_transfer *transfer=(const void *)(uintptr_t)
        KUI_SMASH_NATIVE_GIN_TRANSFER_P1;
    if(transfer->magic0!=KUI_SMASH_NATIVE_GIN_TRANSFER_MAGIC0 ||
       transfer->magic1!=KUI_SMASH_NATIVE_GIN_TRANSFER_MAGIC1 || transfer->version!=1u ||
       transfer->bytes!=sizeof(*transfer) || transfer->crc32!=kui_retail_crc32(0u,transfer,
           offsetof(struct kui_smash_native_gin_transfer,crc32)))
        stopped("SMASH NATIVE GIN TRANSFER REFUSED",transfer->bytes);
    memcpy(&smash_native_gin_saved,transfer,sizeof(smash_native_gin_saved));
    uint32_t sr;__asm__ volatile("stc sr,%0":"=r"(sr)::"memory");
    if(!kui_smash_native_gin_valid(&smash_native_gin_saved.gin) ||
       (sr&UINT32_C(0x700080f0))!=(smash_native_gin_saved.gin.frame[4]&UINT32_C(0x700080f0)) ||
       !kui_smash_native_gin_context_matches((const void *)(uintptr_t)
           (KUI_RETAIL_EXEC_ADDRESS+KUI_SMASH_NATIVE_CONTEXT_OFFSET),
           smash_native_gin_saved.context,smash_native_gin_saved.context,&smash_native_gin_menu_seed) ||
       memcmp(smash_native_gin_saved.ip+UINT32_C(0xf0),smash_native_gin_menu_seed.ip_tail,16u))
        stopped("SMASH NATIVE NEEDS ORIGINAL GIN CONTINUATION",sr);
    retail_display_adoption_quiet(1u);
}

static void __attribute__((noipa)) smash_native_gin_loaded(void) {
    if(manifest.boot_bytes!=KUI_SMASH_NATIVE_ENGINE_BYTES ||
       manifest.boot_crc32!=KUI_SMASH_NATIVE_ENGINE_CRC32 || manifest.ip_crc32!=KUI_SMASH_NATIVE_IP_CRC32 ||
       (manifest.flags&(KUI_RETAIL_IMAGE_CD|KUI_RETAIL_IMAGE_SCRAMBLED)) ||
       strcmp(manifest.bootfile,"MDE_US.BIN") || strcmp(manifest.product,"MK-51146"))
        stopped("SMASH NATIVE GIN OWNER MAP REFUSED",manifest.boot_bytes);
    exec_bytes=KUI_SMASH_NATIVE_ENGINE_BYTES;
    source_boot_crc=KUI_SMASH_NATIVE_ENGINE_CRC32;
    memcpy(original_entry,(const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,ENTRY_PATCH_BYTES);
}

static void __attribute__((noipa)) smash_native_gin_restore_arguments(void) {
    const uint32_t begin=KUI_RETAIL_EXEC_ADDRESS+KUI_SMASH_NATIVE_CONTEXT_OFFSET;
    /* Discovery has freshly hashed the complete canonical original MDE.
     * Restore the exact actual menu/GIN context, including the original
     * 64-byte folder-table copy, instead of rebuilding selected arguments. */
    kui_native_passive_publish(begin,begin+KUI_SMASH_NATIVE_CONTEXT_BYTES);
    memcpy((void *)(uintptr_t)(begin|UINT32_C(0x20000000)),
        smash_native_gin_saved.context,sizeof(smash_native_gin_saved.context));
    if(memcmp((const void *)(uintptr_t)(begin|UINT32_C(0x20000000)),
        smash_native_gin_saved.context,sizeof(smash_native_gin_saved.context)) ||
       memcmp((const void *)(uintptr_t)UINT32_C(0x8c008000),
        smash_native_gin_saved.ip,sizeof(smash_native_gin_saved.ip)))
        relay_stopped("ORIGINAL NATIVE GIN CONTEXT CHANGED",0u);
}

static void __attribute__((noipa,noreturn)) smash_native_gin_adopt(void) {
    if(!kui_smash_native_gin_valid(&smash_native_gin_saved.gin))
        stopped("ORIGINAL NATIVE GIN FRAME CHANGED",0u);
    /* Fresh storage/card attachment already completed in stage_main. The
     * same 72KiB worker is still leased only by the original MDE allocator,
     * after CRT, via its existing independently verified initializer hook. */
    install_resident();
    uint32_t *frame=(void *)(uintptr_t)KUI_SMASH_NATIVE_GIN_FRAME_P1;
    memcpy(frame,smash_native_gin_saved.gin.frame,sizeof(smash_native_gin_saved.gin.frame));
    kui_smash_native_gin_arm_scope(frame,smash_native_gin_saved.gin.ccr);
    void (*resume)(const uint32_t *,uint32_t)=
        (void (*)(const uint32_t *,uint32_t))(uintptr_t)
        ((uintptr_t)kui_smash_native_gin_resume|UINT32_C(0x20000000));
    resume(frame,smash_native_gin_saved.gin.ccr);
    __builtin_unreachable();
}
