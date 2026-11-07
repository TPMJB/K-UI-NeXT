/* SPDX-License-Identifier: GPL-3.0-only */
/* Original shared miss/refusal control from source2aac9af, unchanged predicates. */
static void reference_passive_prepare(void) {
    if(native_driver.family==KUI_VMU_NATIVE_FAMILY_EARLY_P2_V1 &&
       !kui_vmu_native_early_p2_calls_valid((const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,
           exec_bytes,&native_driver,native_call.call,passive_layout.initializer_call)) native_report(2u,0x76u);
#if KUI_VMU_NATIVE_READONLY_PROBE
    const struct kui_vmu_native_patch_window windows[3]={{native_driver.submit,12u},
        {native_heap.initializer,12u},{native_heap.release,20u}};
    const size_t count=native_arena?1u:sizeof(windows)/sizeof(windows[0]);
    if(!native_arena && (((native_heap.initializer|native_heap.release)&3u) || !readonly_release_replay_valid()))
        native_report(2u,12u);
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
    if(!kui_vmu_native_exit_discover_guarded((const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,
       exec_bytes,KUI_RETAIL_EXEC_ADDRESS,windows,count,
       &native_exits) || !native_sdk_coherence_cached_admit()) native_report(2u,13u);
#else
    if(!kui_vmu_native_patch_no_interiors((const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,
       exec_bytes,KUI_RETAIL_EXEC_ADDRESS,windows,count)) native_report(2u,12u);
#endif
    native_passive_capture_windows();
#else
    if(!kui_vmu_native_patch_no_interior((const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,
       exec_bytes,KUI_RETAIL_EXEC_ADDRESS,native_driver.submit,12u)) native_report(2u,11u);
#endif
}
static void reference_miss(void) {
#if KUI_VMU_NATIVE_PASSIVE_PROBE
    retail_display_line("RESOLVING MAPLE DRIVER AND BUFFER LAYOUT");
    if(!kui_vmu_native_discover_layout((const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,
       exec_bytes,KUI_RETAIL_EXEC_ADDRESS,&native_driver,&passive_layout)) native_report(2u,10u);
    int driver=KUI_VMU_NATIVE_RESOLVED;
#else
#if KUI_VMU_NATIVE_WORKER_PROBE
    retail_display_line("SCANNING MAPLE DRIVER AND IMAGE HASH");
#endif
    int driver=kui_vmu_native_discover((const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,
        exec_bytes,KUI_RETAIL_EXEC_ADDRESS,&native_driver);
#endif
#if KUI_VMU_NATIVE_WORKER_PROBE || KUI_VMU_NATIVE_PASSIVE_PROBE
    retail_display_line("SCANNING ALLOCATOR FAMILY");
#endif
    int heap=kui_vmu_native_heap_discover((const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,
        exec_bytes,KUI_RETAIL_EXEC_ADDRESS,&native_heap);
#if KUI_NATIVE_DUAL_READERS
    /* No SDK allocator at all: only a reviewed arena plan for this exact
     * executable can provide the lease. */
    if(driver==KUI_VMU_NATIVE_RESOLVED && heap==KUI_VMU_NATIVE_HEAP_NONE && native_arena_plan_bind(false))
        retail_display_line("NO SDK ALLOCATOR - REVIEWED RAM ARENA");
    else if(driver!=KUI_VMU_NATIVE_RESOLVED || heap!=KUI_VMU_NATIVE_HEAP_FOUND)
        native_report(2u,heap==KUI_VMU_NATIVE_HEAP_FOUND?0u:(uint32_t)heap);
    if(!(native_arena || (native_heap.initializer_slot?kui_vmu_native_heap_call_discover((const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,
         exec_bytes,&native_heap,&native_call):native_fixed_plan_bind(false))) ||
       !allocation_safe(native_call.entry_begin,native_call.entry_bytes))
        native_report(2u,native_heap.initializer_slot);
#else
    if(driver!=KUI_VMU_NATIVE_RESOLVED || heap!=KUI_VMU_NATIVE_HEAP_FOUND ||
       !native_heap.initializer_slot) native_report(2u,heap==KUI_VMU_NATIVE_HEAP_FOUND?0u:(uint32_t)heap);
    if(!kui_vmu_native_heap_call_discover((const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,
       exec_bytes,&native_heap,&native_call) || !allocation_safe(native_call.entry_begin,native_call.entry_bytes))
        native_report(2u,native_heap.initializer_slot);
#endif
#if KUI_VMU_NATIVE_PASSIVE_PROBE
    /* Layout already shares the driver's one fresh immutable-image pass. */
    retail_display_line("CHECKING DISPLACED INSTRUCTION WINDOW");
    reference_passive_prepare();
#endif
}
