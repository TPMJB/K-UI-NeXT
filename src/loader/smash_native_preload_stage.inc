/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/cold_image.h"
#include "kui/smash_native_child.h"
/* The live cold handoff alone may fill this optional blank record. The GUI
 * capsule stays canonical/blank on disk; no cross-launch image grant exists. */
__asm__(".section .rodata.smash_cold_image,\"a\"\n.balign 4\n"
    ".global __smash_cold_image_seed\n__smash_cold_image_seed:\n"
    ".ascii \"KUICIM01\"\n.long 1,32\n.zero 16\n.previous\n");
extern const struct kui_cold_image_seed smash_cold_image_seed __asm__("__smash_cold_image_seed");
static bool smash_native_preloaded_image(void *out) {
    struct kui_cold_image_seed seed;memcpy(&seed,&smash_cold_image_seed,sizeof(seed));
    if(seed.source!=KUI_COLD_IMAGE_SCRATCH || seed.image_bytes!=KUI_SMASH_NATIVE_ENGINE_BYTES ||
       manifest.boot_bytes!=KUI_SMASH_NATIVE_ENGINE_BYTES || manifest.boot_crc32!=KUI_SMASH_NATIVE_ENGINE_CRC32 ||
       manifest.ip_crc32!=KUI_SMASH_NATIVE_IP_CRC32 || strcmp(manifest.bootfile,"MDE_US.BIN") ||
       strcmp(manifest.product,"MK-51146") ||
       !kui_cold_image_restore(&seed,out,manifest.boot_bytes,
           (const void *)(uintptr_t)KUI_COLD_IMAGE_SCRATCH,KUI_COLD_IMAGE_SCRATCH_END-KUI_COLD_IMAGE_SCRATCH,
           KUI_SMASH_NATIVE_ENGINE_CRC32,kui_smash_native_child_engine_owner))return false;
    source_boot_crc=KUI_SMASH_NATIVE_ENGINE_CRC32;
    retail_display_line("FRESH ORIGINAL EXECUTABLE VERIFIED / REUSING MENU READ");
    return true;
}
