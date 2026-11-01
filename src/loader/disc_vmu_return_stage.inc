/* SPDX-License-Identifier: GPL-3.0-only */
/* The observed native CRT saves its inherited PR immediately, at entry+2.
 * Give that register a game-owned terminal address before the first
 * instruction, rather than attempting to find a saved stack slot later.
 *
 * The exact SDK exit was discovered in the immutable game and has an admitted
 * 12-byte patch window. It is terminal while this temporary installer is live;
 * the ordinary worker-install loop later replaces the same veneer with the
 * allocator-owned support terminal. All copies of this initial PR therefore
 * keep naming game code protected by the existing runtime lifetime plan. */
static uint32_t native_disc_entry_return_error;
static bool native_disc_return_disjoint(uint32_t a,uint32_t an,uint32_t b,uint32_t bn) {
    a&=UINT32_C(0x1fffffff);b&=UINT32_C(0x1fffffff);
    return an<=UINT32_MAX-a && bn<=UINT32_MAX-b && (a+an<=b || b+bn<=a);
}
static bool __attribute__((noipa)) native_disc_entry_return_bind(uint32_t *frame) {
    native_disc_entry_return_error=0x70u;
    uint32_t address=(uint32_t)(uintptr_t)frame;
    native_disc_entry_return_frame=address;
    native_disc_entry_return_entry=native_exits.entries[0];
    uint32_t area=address&UINT32_C(0xe0000000),physical=address&UINT32_C(0x1fffffff);
    if((uintptr_t)frame!=(uintptr_t)address ||
       (area!=0u && area!=UINT32_C(0x80000000) && area!=UINT32_C(0xa0000000)) ||
       (address&3u) || physical<UINT32_C(0x0c008100) ||
       physical>UINT32_C(0x0c00f400)-84u) return false;
    native_disc_entry_return_error=0x71u;
    if(!kui_vmu_native_exit_shape_valid(&native_exits) ||
       native_exits.load!=KUI_RETAIL_EXEC_ADDRESS || native_exits.image_bytes!=exec_bytes ||
       exec_bytes<sizeof(original_entry)+12u ||
       native_terminal!=(uint32_t)(uintptr_t)kui_vmu_observe_return_hook) return false;
    uint32_t entry=native_exits.entries[0],begin=native_exits.load&UINT32_C(0x1fffffff);
    uint32_t at=entry&UINT32_C(0x1fffffff);
    if((entry&UINT32_C(0xe0000000))!=UINT32_C(0x80000000) || (entry&1u) ||
       at<begin || at-begin>exec_bytes-12u ||
       !native_disc_return_disjoint(entry,12u,KUI_RETAIL_EXEC_ADDRESS,sizeof(original_entry)) ||
       !native_disc_return_disjoint(entry,12u,native_driver.submit,12u) ||
       !native_disc_return_disjoint(entry,12u,native_heap.initializer,0x1d0u) ||
       !native_disc_return_disjoint(entry,12u,native_heap.release,20u)) return false;
    for(unsigned i=1u;i<native_exits.count;i++)
        if(!native_disc_return_disjoint(entry,12u,native_exits.entries[i],12u)) return false;
    native_disc_entry_return_error=0x72u;
    uint16_t patch[6];
    if(!kui_vmu_native_patch_encode(entry,native_terminal,patch) ||
       memcmp((const void *)(uintptr_t)entry,persistent_exit_windows[0],sizeof(patch)) ||
       memcmp((const void *)(uintptr_t)(entry|UINT32_C(0x20000000)),persistent_exit_windows[0],sizeof(patch)))
        return false;
    /* The publisher is shared with allocator/exit installation: exact P1 span
     * publication, I-cache invalidation and P2 readback precede the PR write. */
    readonly_patch_entry(entry,native_terminal);
    native_disc_entry_return_error=0x73u;
    if(memcmp((const void *)(uintptr_t)(entry|UINT32_C(0x20000000)),patch,sizeof(patch))) return false;
    memcpy(persistent_exit_windows[0],patch,sizeof(patch));
    uint32_t slot=(physical|UINT32_C(0xa0000000))+5u*4u;
    *(volatile uint32_t *)(uintptr_t)slot=entry;
    __asm__ volatile("":::"memory");
    native_disc_entry_return_error=0x74u;
    if(native_word(slot)!=entry) return false;
    native_disc_entry_return_error=0u;return true;
}
