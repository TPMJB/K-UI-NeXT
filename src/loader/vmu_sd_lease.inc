/* SPDX-License-Identifier: GPL-3.0-only */
/* Included ONLY by the isolated observation resident after its private
 * P1 card/lock definitions. Tests compile this same body against a fake bus.
 * This entry is called from the worker stack, never from the low GD stack. */
#include "kui/vmu_sd_lease.h"
#include "sci_sd_bus.h"

#ifdef KUI_VMU_SD_LEASE_HOST_TEST
extern uint32_t kui_vmu_sd_lease_test_sr(void);
#define LEASE_SR() kui_vmu_sd_lease_test_sr()
#define LEASE_CONTROL (&kui_vmu_observe_boot_control)
#else
static inline uint32_t lease_sr(void) {
    uint32_t value;
    __asm__ volatile("stc sr,%0":"=r"(value)::"memory");
    return value;
}
#define LEASE_SR() lease_sr()
#define LEASE_CONTROL ((const volatile struct kui_vmu_observe_boot_control *) \
    ((uintptr_t)&kui_vmu_observe_boot_control+UINT32_C(0x20000000)))
#endif

#ifdef KUI_VMU_SD_LEASE_NATIVE_HOST_TEST
/* Host function pointers cannot carry the console's relocated P2 address.
 * The fixture supplies only that address; the real callback still executes. */
extern uintptr_t kui_vmu_sd_lease_test_target(kui_vmu_sd_transaction);
#define LEASE_TARGET(callback) kui_vmu_sd_lease_test_target(callback)
#else
#define LEASE_TARGET(callback) ((uintptr_t)(callback))
#endif

/* A separate linker output section owns the exact relaxed code/literal span.
 * SH --relax can leave ST_SIZE at its larger pre-relaxation value. */
int __attribute__((noipa,section(".vmu_sd_lease"))) kui_vmu_observe_sd_lease(
    kui_vmu_sd_transaction callback,void *context) {
    const volatile struct kui_vmu_observe_boot_control *control=LEASE_CONTROL;
    uintptr_t target=LEASE_TARGET(callback);
    if((LEASE_SR()&UINT32_C(0xf0))!=UINT32_C(0xf0)
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
       || !native_resident_transaction(control,target)
#else
       || !target || control->status!=KUI_VMU_OBS_BOOT_INSTALLED || target!=control->sd_transaction
#ifndef KUI_VMU_SD_LEASE_HOST_TEST
       || (target&1u) || target<KUI_VMU_MAPLE_OBSERVE_LEASE ||
       target>=KUI_VMU_MAPLE_OBSERVE_LIMIT
#endif
#endif
       ) return KUI_VMU_SD_LEASE_FAULT;
    if(kui_retail_hook_active) return KUI_VMU_SD_LEASE_BUSY;
    if(card.transport!=KUI_STORAGE_SCI) return KUI_VMU_SD_LEASE_FAULT;
    if(card.stream.active ||
       !card.device.sd.ready || card.device.sd.slow || !card.device.sd.blocks ||
       kui_sci_sd_faulted()) {
        card.device.sd.ready=false;
        return KUI_VMU_SD_LEASE_FAULT;
    }
    /* The caller already holds IMASK15: this P1 claim is atomic with the
     * existing native GD entry, and no live stream is stolen or stopped. */
    kui_retail_hook_active=1u;
    int result=kui_retail_storage_acquire(&card);
    if(result!=KUI_LOADER_SD_OK) {
        /* These two SCI refusal codes precede all register writes. Any
         * unexpected acquisition error is uncertain, never a retry. */
        if(result!=KUI_LOADER_SD_NOT_READY && result!=KUI_LOADER_SD_UNSUPPORTED)
            card.device.sd.ready=false;
        kui_retail_hook_active=0u;
        return card.device.sd.ready?KUI_VMU_SD_LEASE_BUSY:KUI_VMU_SD_LEASE_FAULT;
    }
    struct kui_loader_sd loan=card.device.sd;
    result=callback(context,&loan);
    if(!loan.ready || !kui_sci_sd_healthy()) result=KUI_VMU_SD_LEASE_FAULT;
    kui_retail_storage_release(&card);
    if(kui_sci_sd_faulted()) result=KUI_VMU_SD_LEASE_FAULT;
    if(result) card.device.sd.ready=false;
    kui_retail_hook_active=0u;
    return result?KUI_VMU_SD_LEASE_FAULT:KUI_VMU_SD_LEASE_OK;
}

#undef LEASE_CONTROL
#undef LEASE_SR

#undef LEASE_TARGET
