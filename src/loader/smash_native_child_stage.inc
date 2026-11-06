/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_native_child.h"
#if KUI_SMASH_NATIVE_MENU
#include "kui/smash_native_menu.h"
/* The GUI may use a saved mapping without a duplicate executable read
 * only when this immutable capability is present. The relay independently
 * hashes its own complete MDE and enforces this owner before native hooks. */
__asm__(".section .rodata.smash_native_menu,\"a\"\n.balign 4\n"
    ".global __native_source_owner\n__native_source_owner:\n"
    ".ascii \"KUINOWN1\"\n.long 1,64,1848288,0x8c010000\n"
    ".byte 4,244,157,88,59,57,189,199,16,206,198,208,164,52,181,215\n"
    ".byte 16,185,188,206,221,83,155,237,218,41,117,81,53,240,254,97\n"
    ".long 1,0x8c5ed646\n.previous\n");
__asm__(".section .rodata.smash_native_menu,\"a\"\n.balign 4\n"
    ".global __smash_native_menu_seed\n__smash_native_menu_seed:\n"
    ".long 0x5349554b,0x31554e4d,1,304,0,0,0,0\n.zero 272\n"
    ".global __smash_menu_return_seed\n__smash_menu_return_seed:\n"
    ".long 0x5349554b,0x3154524d,1,368\n.zero 352\n"
#if KUI_SMASH_PHASE_DIAGNOSTIC
    ".global __smash_phase_trace_seed\n__smash_phase_trace_seed:\n"
    ".long 0x5349554b,0x314d4954,1,992\n.zero 976\n"
    ".global __smash_phase_video_seed\n__smash_phase_video_seed:\n"
    ".long 0x5349554b,0x31444956,1,80\n.zero 64\n"
#endif
    ".previous\n");
extern const struct kui_smash_native_menu_seed smash_menu_seed __asm__("__smash_native_menu_seed");
#else
__asm__(".section .rodata.smash_native_child,\"a\"\n.balign 4\n"
    ".global __smash_native_child_seed\n__smash_native_child_seed:\n"
    ".long 0x5349554b,0x31444843,1,32,0,0,0,0\n.previous\n");
extern const struct kui_smash_native_child_seed smash_child_seed __asm__("__smash_native_child_seed");
#endif
#if !KUI_SMASH_NATIVE_GIN_ADOPT
static unsigned smash_native_stage_child(void) {
#if KUI_SMASH_NATIVE_MENU
    struct kui_smash_native_menu_seed seed;
    memcpy(&seed,&smash_menu_seed,sizeof(seed));
    if(!kui_smash_native_menu_seed_valid(&seed) || manifest.boot_bytes!=KUI_SMASH_NATIVE_ENGINE_BYTES ||
#else
    struct kui_smash_native_child_seed seed;
    memcpy(&seed,&smash_child_seed,sizeof(seed));
    if(!kui_smash_native_child_seed_valid(&seed) || manifest.boot_bytes!=KUI_SMASH_NATIVE_ENGINE_BYTES ||
#endif
       manifest.boot_crc32!=KUI_SMASH_NATIVE_ENGINE_CRC32 || manifest.ip_crc32!=KUI_SMASH_NATIVE_IP_CRC32 ||
       manifest.track_count+manifest.extent_count>48u || strcmp(manifest.bootfile,"MDE_US.BIN") || strcmp(manifest.product,"MK-51146"))
        relay_stopped("SMASH NATIVE CHILD OWNER REFUSED",
#if KUI_SMASH_NATIVE_MENU
            seed.selection);
    return seed.selection;
#else
            seed.child);
    return seed.child;
#endif
}
static void smash_native_stage_frame(const uint32_t *frame) {
    (void)smash_native_stage_child();
    if(!kui_smash_native_child_frame((uint32_t *)(uintptr_t)frame,(uint32_t)(uintptr_t)frame))
        relay_stopped("SMASH NATIVE CHILD CPU STATE REFUSED",(uint32_t)(uintptr_t)frame);
}
static void smash_native_stage_arguments(void) {
    unsigned child=smash_native_stage_child();
    const uint32_t begin=KUI_RETAIL_EXEC_ADDRESS+KUI_SMASH_NATIVE_CONTEXT_OFFSET;
    /* Publish and invalidate P1 lines before modifying their P2 aliases. This
     * prevents an earlier SDK-discovery read from retaining original argv. */
    kui_native_passive_publish(begin,begin+KUI_SMASH_NATIVE_CONTEXT_BYTES);
    kui_native_passive_publish(UINT32_C(0x8c0080e0),UINT32_C(0x8c008100));
    uint8_t *context=(uint8_t *)(uintptr_t)(begin|UINT32_C(0x20000000));
    uint8_t *ip=(uint8_t *)(uintptr_t)UINT32_C(0xac0080f0);
#if KUI_SMASH_NATIVE_MENU
    if(!kui_smash_native_menu_arguments(context,KUI_SMASH_NATIVE_CONTEXT_BYTES,ip,&smash_menu_seed))
        relay_stopped("SMASH MENU NATIVE ARGUMENTS REFUSED",child);
    retail_display_line(kui_smash_native_menu_name(child));
    retail_display_line("ORIGINAL MENU / SHARED SMASH PACK VMU A1");
    kui_smash_native_return_stage_publish();
#else
    if(!kui_smash_native_child_arguments(context,KUI_SMASH_NATIVE_CONTEXT_BYTES,ip,child))
        relay_stopped("SMASH NATIVE CHILD ARGUMENTS REFUSED",child);
    retail_display_line(child==KUI_SMASH_NATIVE_CHILD_SHINING_FORCE?
        "SHINING FORCE / SHARED SMASH PACK VMU A1":"PHANTASY STAR II / SHARED SMASH PACK VMU A1");
#endif
}
#endif
