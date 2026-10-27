/* SPDX-License-Identifier: GPL-3.0-only */
/* Read-only views of the existing private objects, never copies of live SDK
 * words. The alternate linker script isolates complete D-cache lines from
 * every mutable neighbor. Cold arm and lazy preparation write only P2.
 * Config.runtime and live_plan are finalized together by live_prepare, so
 * publication belongs after its first successful commit, not at arm. */
#if KUI_NATIVE_METADATA_CACHE_ISOLATION
#define NATIVE_METADATA_READY UINT32_C(0x4e4d4331)
static uint32_t native_metadata_ready;
#ifndef KUI_VMU_NATIVE_PASSIVE_HOST_TEST
extern const uint8_t native_metadata_begin[] __asm__("__kui_native_metadata_begin");
extern const uint8_t native_metadata_end[] __asm__("__kui_native_metadata_end");
#endif
static inline __attribute__((always_inline)) const void *native_metadata_view(const void *raw) {
#ifdef KUI_VMU_NATIVE_PASSIVE_HOST_TEST
    return raw;
#else
    return native_metadata_ready==NATIVE_METADATA_READY?
        (const void *)(((uintptr_t)raw&UINT32_C(0x1fffffff))|UINT32_C(0x80000000)):raw;
#endif
}
static void __attribute__((noipa)) native_metadata_publish(void) {
#ifndef KUI_VMU_NATIVE_PASSIVE_HOST_TEST
    /* Discard any stale P1 metadata tags after all authoritative P2 writes.
     * Never use OCBP here: old dirty P1 bytes must not overwrite fresh P2.
     * The linker-proved lines contain no mutable worker state or DMA data. */
    __asm__ volatile("":::"memory");
    for(uintptr_t at=((uintptr_t)native_metadata_begin&UINT32_C(0x1fffffff))|UINT32_C(0x80000000),
        end=((uintptr_t)native_metadata_end&UINT32_C(0x1fffffff))|UINT32_C(0x80000000);
        at<end;at+=32u)
        __asm__ volatile("ocbi @%0"::"r"(at):"memory");
    __asm__ volatile("":::"memory");
#endif
    /* Accessors can select P1 only after every immutable line was retired. */
    native_metadata_ready=NATIVE_METADATA_READY;
}
static inline __attribute__((always_inline)) const struct kui_vmu_native_passive_config *native_cached_config(void) {
    return native_metadata_view(&config);
}
static inline __attribute__((always_inline)) const struct kui_vmu_native_live *native_cached_live(void) {
    return native_metadata_view(&live_plan);
}
static inline __attribute__((always_inline)) const struct kui_vmu_native_lifetime_plan *native_cached_lifetime(void) {
    return native_metadata_view(&lifetime_plan);
}
static inline __attribute__((always_inline)) const struct native_hook_witness *native_cached_hooks(void) {
    return native_metadata_view(hook_witness);
}
#else
#define native_cached_config() (&config)
#define native_cached_live() (&live_plan)
#define native_cached_lifetime() (&lifetime_plan)
#define native_cached_hooks() (hook_witness)
#define native_metadata_publish() ((void)0)
#endif
