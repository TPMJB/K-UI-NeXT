static bool native_cache_hint_admitted_original(const struct kui_retail_manifest *map,bool dual,
    const struct kui_runtime_image *package,const struct kui_vmu_native_resolution *r) {
    const bool arena=r->heap.family==KUI_VMU_NATIVE_HEAP_ARENA;
    return r->driver.image_bytes==map->boot_bytes &&
        r->driver.load_address==KUI_RETAIL_EXEC_ADDRESS &&
        (dual || (!arena && r->heap.initializer_slot)) &&
        (!arena || native_cache_dual_lease(package)==r->call.entry_bytes);
}
