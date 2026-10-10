/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/toy_loader_payload_control.h"

/* Mutable control state is confined to the existing high resident's BSS.
 * The image's low block has its own complete 512-byte cache-line span.
 */
static struct kui_toy_loader_payload_control_counts payload_counts;
#if KUI_TOY_PILOT_DATA_PAYLOAD_MODE == 2
/* A complete 544-byte aligned object leaves the intentionally unaligned
 * [1,513) receive span private, within the high reservation, and in P2.
 */
static _Alignas(32) uint8_t payload_pio_scratch[544];
#endif

#ifdef KUI_TOY_LOADER_PAYLOAD_CONTROL_HOST_TEST
static uint8_t *payload_image_block;
#define PAYLOAD_IMAGE_BLOCK payload_image_block
#else
#if KUI_TOY_PILOT_DATA_PAYLOAD_MODE
#include "kui/retail_image.h"
#include "toy_pilot_resident_symbols.h"
_Static_assert(sizeof(struct kui_retail_image)==544u,"payload exact SH image ABI");
_Static_assert(_Alignof(struct kui_retail_image)==32u,"payload exact SH image alignment");
_Static_assert(offsetof(struct kui_retail_image,block)==32u,"payload exact SH block offset");
_Static_assert(sizeof(((struct kui_retail_image *)0)->block)==512u,"payload exact SH block span");
#define PAYLOAD_IMAGE_BLOCK ((uint8_t *)(uintptr_t)KUI_TOY_PILOT_LOW_PROBE_IMAGE_BLOCK)
#endif
#endif

#if KUI_TOY_PILOT_DATA_PAYLOAD_MODE
static inline __attribute__((always_inline)) void payload_count(uint32_t *value) {
    if(*value!=UINT32_MAX) ++*value;
}
static bool payload_exact(const uint8_t *tx,uint8_t *rx,size_t count,bool slow) {
    /* No broad address canonicalization: only the separately admitted low
     * image object's exact P2 block address may enter either experiment.
     */
    return !tx && rx && rx==PAYLOAD_IMAGE_BLOCK && count==512u && !slow;
}
#endif

#if KUI_TOY_PILOT_DATA_PAYLOAD_MODE == 1
static uint32_t payload_ccr(void) {
#ifdef KUI_TOY_LOADER_PAYLOAD_CONTROL_HOST_TEST
    return kui_toy_loader_payload_control_host_ccr();
#else
    return *(volatile const uint32_t *)(uintptr_t)UINT32_C(0xff00001c);
#endif
}
static uint8_t *payload_cached(uint8_t *rx) {
#ifdef KUI_TOY_LOADER_PAYLOAD_CONTROL_HOST_TEST
    return kui_toy_loader_payload_control_host_cached(rx);
#else
    return (uint8_t *)((uintptr_t)rx-UINT32_C(0x20000000));
#endif
}
static void payload_publish(uint8_t *cached) {
#ifdef KUI_TOY_LOADER_PAYLOAD_CONTROL_HOST_TEST
    kui_toy_loader_payload_control_host_purge(cached,512u);
#else
    uintptr_t end=(uintptr_t)cached+512u;
    for(uintptr_t line=(uintptr_t)cached;line<end;line+=32u)
        __asm__ __volatile__("ocbp @%0" : : "r"(line) : "memory");
#endif
}
#endif

bool kui_toy_loader_payload_call(kui_toy_loader_payload_fn original,void *context,
    const uint8_t *tx,uint8_t *rx,size_t count,bool slow,uint16_t *crc) {
#if KUI_TOY_PILOT_DATA_PAYLOAD_MODE
    if(!payload_exact(tx,rx,count,slow)
#if KUI_TOY_PILOT_DATA_PAYLOAD_MODE == 1
        || (payload_ccr()&UINT32_C(0x105))!=UINT32_C(0x101)
#endif
    ) {
        payload_count(&payload_counts.declines);
        return original(context,tx,rx,count,slow,crc);
    }
    payload_count(&payload_counts.attempts);
#if KUI_TOY_PILOT_DATA_PAYLOAD_MODE == 1
    uint8_t *cached=payload_cached(rx);
    /* The original DMA path also purges before starting. This outer pre-
     * purge admits the PIO fallback coherently, while the mandatory post-
     * purge publishes logical bytes before low image code resumes via P2.
     */
    payload_publish(cached);
    bool result=original(context,tx,cached,count,slow,crc);
    payload_publish(cached);
    payload_count(&payload_counts.publications);
#else
    /* Keep the ordinary pointer's P2 alias. An address with bit 0 set fails
     * the unchanged dma_address() admission before DMAC/IRQ ownership.
     */
    uint8_t *scratch=payload_pio_scratch+1u;
    bool result=original(context,tx,scratch,count,slow,crc);
    if(result) {
        /* This bounded copy avoids libc calls, an aligned cached source, or
         * converting any surrounding low resident state into a P1 alias.
         */
        for(size_t i=0;i<512u;++i) rx[i]=scratch[i];
        payload_count(&payload_counts.publications);
    }
#endif
    if(!result) payload_count(&payload_counts.failed);
    return result;
#else
    return original(context,tx,rx,count,slow,crc);
#endif
}

const struct kui_toy_loader_payload_control_counts *kui_toy_loader_payload_control_counts(void) {
    return &payload_counts;
}

#ifdef KUI_TOY_LOADER_PAYLOAD_CONTROL_HOST_TEST
void kui_toy_loader_payload_control_host_bind(uint8_t *image_block) {
    payload_image_block=image_block;
}
void kui_toy_loader_payload_control_host_reset(void) {
    payload_counts=(struct kui_toy_loader_payload_control_counts){0};
}
#endif
