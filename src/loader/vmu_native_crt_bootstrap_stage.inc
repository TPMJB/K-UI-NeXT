/* SPDX-License-Identifier: GPL-3.0-only */
/* The extra CRT path changes FPSCR/SP and clears BSS before its BIOS call.
 * Its entry must be terminal before ANY original game code runs, rather than
 * waiting until a possibly nested allocator has returned. The retained low
 * terminal entry fails closed before worker readiness and dispatches to the
 * durable terminal path after arming. Later installation verifies this same
 * published veneer and the runtime witnesses retain it. */
static bool __attribute__((noipa)) native_crt_bootstrap_prearm(void) {
    if(native_exits.family!=KUI_VMU_NATIVE_EXIT_CRT_BOOTSTRAP) return true;
    if(!kui_vmu_native_exit_shape_valid(&native_exits) || native_exits.image_bytes<12u)
        return false;
    uint32_t entry=native_exits.entries[3];
    uint32_t at=entry&UINT32_C(0x1fffffff),begin=native_exits.load&UINT32_C(0x1fffffff);
    uint16_t patch[6];
    if(at<begin || at-begin>native_exits.image_bytes-12u ||
       !kui_vmu_native_patch_encode(entry,NATIVE_LOW_RETURN_HOOK,patch) ||
       memcmp((const void *)(uintptr_t)entry,persistent_exit_windows[3],sizeof(patch))) return false;
    readonly_patch_entry(entry,NATIVE_LOW_RETURN_HOOK);
    if(memcmp((const void *)(uintptr_t)entry,patch,sizeof(patch))) return false;
    memcpy(persistent_exit_windows[3],patch,sizeof(patch));
    return true;
}
