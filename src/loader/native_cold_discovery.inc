/* SPDX-License-Identifier: GPL-3.0-only */
/* The original cache-miss recognition chain, with refusals returned instead
 * of rendering/halting inside an operand-cache scope. It reads only the
 * complete immutable executable and writes private cold-stage resolution
 * objects. Live SDK/heap calls, device I/O, coherence admission, original
 * instruction capture and every patch/publication remain in the caller. */
bool __attribute__((noipa)) kui_native_stage_discover_image(const void *image,size_t bytes,
    uint32_t load,uint32_t *detail) {
    if(!detail)return false;
    *detail=0u;
    if(image!=(const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS || bytes!=exec_bytes ||
       load!=KUI_RETAIL_EXEC_ADDRESS) {*detail=13u;return false;}
    if(!kui_vmu_native_discover_layout(image,bytes,load,&native_driver,&passive_layout)) {
        *detail=10u;return false;
    }
    int heap=kui_vmu_native_heap_discover(image,bytes,load,&native_heap);
#if KUI_NATIVE_DUAL_READERS
    if(heap==KUI_VMU_NATIVE_HEAP_NONE && native_arena_plan_bind(false)) {
        /* The reviewed no-allocator arena is already bound by that proof. */
    } else if(heap!=KUI_VMU_NATIVE_HEAP_FOUND) {
        *detail=(uint32_t)heap;return false;
    }
    if(!(native_arena || (native_heap.initializer_slot?
       kui_vmu_native_heap_call_discover(image,bytes,&native_heap,&native_call):native_fixed_plan_bind(false))) ||
       !allocation_safe(native_call.entry_begin,native_call.entry_bytes)) {
        *detail=native_heap.initializer_slot;return false;
    }
#else
    if(heap!=KUI_VMU_NATIVE_HEAP_FOUND || !native_heap.initializer_slot) {
        *detail=heap==KUI_VMU_NATIVE_HEAP_FOUND?0u:(uint32_t)heap;return false;
    }
    if(!kui_vmu_native_heap_call_discover(image,bytes,&native_heap,&native_call) ||
       !allocation_safe(native_call.entry_begin,native_call.entry_bytes)) {
        *detail=native_heap.initializer_slot;return false;
    }
#endif
    *detail=native_passive_prepare_image();
    return *detail==0u;
}
