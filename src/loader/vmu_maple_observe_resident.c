/* SPDX-License-Identifier: GPL-3.0-only */
/* Separate passive observer. The ordinary retail translation unit is intact. */
#if !KUI_VMU_MAPLE_OBSERVE || !KUI_RETAIL_LOW_RESIDENT || defined(KUI_RETAIL_CE) || defined(KUI_RETAIL_ASYNC)
#error VMU observation resident requires its isolated synchronous native build
#endif
#define kui_retail_resident_init kui_vmu_observe_base_init
#include "retail_resident.c"
#undef kui_retail_resident_init
#include "kui/vmu_maple_observe_boot.h"
#include "kui/vmu_toy_observe_admission.h"

static uint8_t *observe_map(void *context,uint32_t address,uint32_t bytes,int writing) {
    if(!kui_vmu_toy_observe_destination(address,bytes)) return NULL;
    return map_guest(context,address,bytes,writing);
}

int kui_retail_resident_init(const struct kui_retail_manifest *prepared,
    const struct kui_retail_storage *prepared_card,uint32_t original_gd_vector,
    const struct retail_display_state *saved_display) {
    int result=kui_vmu_observe_base_init(prepared,prepared_card,original_gd_vector,saved_display);
    service.ops.map=observe_map;
    purge((uint32_t)(uintptr_t)&service,sizeof(service));
    return result;
}

void kui_vmu_observe_low_failure(void) __attribute__((noreturn));
void kui_vmu_observe_low_failure(void) {
    const volatile struct kui_vmu_observe_boot_control *c=
        (const volatile struct kui_vmu_observe_boot_control *)
        ((uintptr_t)&kui_vmu_observe_boot_control+UINT32_C(0x20000000));
    /* A validated worker entry is published only after its initialization
     * succeeded. A late heap reset stops without destroying the live lease. */
    uint32_t terminal=c->terminal;
    if(c->status==KUI_VMU_OBS_BOOT_INSTALLED && !(terminal&1u) &&
       terminal>=KUI_VMU_TOY_LEASE_BEGIN && terminal<KUI_VMU_TOY_WORKER_END)
        ((void (*)(void))(uintptr_t)terminal)();
    retail_display_restore(&display);
    retail_display_line("TOY MAPLE OBSERVER STOPPED");
    retail_display_values("STATE ERROR LEASE HEAP",(uint32_t[4]){
        c->status,c->error,c->lease,c->heap_begin},4u);
    for(;;) __asm__ volatile("nop");
}
