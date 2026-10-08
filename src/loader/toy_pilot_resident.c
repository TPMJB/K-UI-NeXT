/* SPDX-License-Identifier: GPL-3.0-only */
/* Separate opt-in translation unit; the ordinary reader is unchanged. */
#if !KUI_RETAIL_TOY_PILOT
#error Toy pilot resident requires its separate build
#endif
#define kui_retail_resident_init kui_toy_pilot_base_init
#define kui_retail_resident_dispatch kui_toy_pilot_base_dispatch
#define kui_retail_menu_return kui_toy_pilot_base_return
#include "retail_resident.c"
#undef kui_retail_resident_init
#undef kui_retail_resident_dispatch
#undef kui_retail_menu_return
#include "kui/toy_pilot.h"
#include "kui/toy_pilot_boot.h"
#include "kui/toy_pilot_gd.h"

/* Export initialized pointer words so the stage symbol generator need not
 * depend on compiler names for local LTO objects. */
const void * const kui_toy_pilot_manifest_pointer=&manifest;
/* The worker treats only command16/17 as data priority. An audio mailbox
 * handoff must not block its own prefill. */
const void * const kui_toy_pilot_pending_pointer=&service.command;
const volatile void * const kui_toy_pilot_active_pointer=&kui_retail_hook_active;

static uint8_t *toy_map(void *context,uint32_t address,uint32_t bytes,int writing) {
    /* The allocator's header and worker are owned. FE..RAM-end padding remains
     * available to this title's established direct SDK scratch accesses. */
    if(bytes && address<0x8cfe0000u && address+bytes>0x8cfcffe0u)
        return NULL;
    return map_guest(context,address,bytes,writing);
}

int kui_toy_pilot_read_raw(uint32_t lba,uint32_t count,void *output) {
    /* Output is the worker's fixed private raw buffer, never a GD-supplied
     * destination. The linked worker validates its lease before this call;
     * the existing image reader validates all source map extents again. */
    if(count-1u>1u) return -1;
    /* Sound SDK work runs outside this mask. Only the shared SCI card visit
     * acquires the existing resident lock, with exact caller SR restored. */
    uint32_t saved,masked;
    __asm__ __volatile__("stc sr,%0":"=r"(saved));
    masked=saved|0x100000f0u;
    __asm__ __volatile__("ldc %0,sr"::"r"(masked):"memory");
    int result=-1;
    if(!kui_retail_hook_active && service.command!=KUI_GD_PIOREAD && service.command!=KUI_GD_DMAREAD) {
        kui_retail_hook_active=1;
        result=read_sectors(NULL,lba,count,2352u,output);
        kui_retail_hook_active=0;
    }
    __asm__ __volatile__("ldc %0,sr"::"r"(saved):"memory");
    return result;
}

int kui_retail_resident_init(const struct kui_retail_manifest *prepared,
    const struct kui_retail_storage *prepared_card,uint32_t original_gd_vector,
    const struct retail_display_state *saved_display) {
    int result=kui_toy_pilot_base_init(prepared,prepared_card,original_gd_vector,saved_display);
    service.ops.map=toy_map; /* Failed init never enters the game. */
    /* The original init flushed BSS before returning. Flush the changed
     * callback too, before bootstrap may alter cache mode. */
    purge((uint32_t)(uintptr_t)&service,sizeof(service));
    return result;
}

static int toy_command(uint32_t command,uint32_t a,uint32_t b,uint32_t c) {
    uint32_t entry=kui_toy_pilot_boot_control.request;
    /* The stage publishes only validated linked entry addresses, after full
     * initialization. This low control is outside all guest mapped writes. */
    if(kui_toy_pilot_boot_control.status!=KUI_TOY_BOOT_INSTALLED) return 0;
    return ((int (*)(uint32_t,uint32_t,uint32_t,uint32_t))(uintptr_t)entry)(command,a,b,c);
}
static const struct kui_toy_pilot_snapshot *toy_snapshot(void) {
    uint32_t entry=kui_toy_pilot_boot_control.snapshot;
    if(kui_toy_pilot_boot_control.status!=KUI_TOY_BOOT_INSTALLED) return NULL;
    /* The exact linked function returns its fixed resident owner.stats.
     * It cannot select a guest-supplied pointer. */
    return ((const struct kui_toy_pilot_snapshot *(*)(void))(uintptr_t)entry)();
}

int32_t kui_retail_resident_dispatch(uint32_t r4,uint32_t r5,uint32_t r6,uint32_t r7) {
    /* This entry is masked and uses the existing small GD stack. Only integer
     * mailbox/snapshot work is allowed here; SDK and audio reads stay in the
     * game's normal service hook on the separately leased worker stack. */
    int real=kui_retail_hook_source==1u || r6!=UINT32_MAX;
    if(real && (r7==KUI_GD_INIT || r7==KUI_GD_RESET))
        (void)toy_command(KUI_TOY_PILOT_RESET,0,0,0);
    if(real && r7==KUI_GD_EXEC && kui_toy_pilot_gd_audio_pending(&service)) {
        kui_toy_pilot_gd_acknowledge(&service,toy_snapshot());
        return 0;
    }
    int32_t result=kui_toy_pilot_base_dispatch(r4,r5,r6,r7);
    if(real && r7==KUI_GD_REQUEST && result>0 &&
       (r4==KUI_RETAIL_GD_PLAY || r4==KUI_RETAIL_GD_PLAY2 ||
        r4==KUI_RETAIL_GD_PAUSE || r4==KUI_RETAIL_GD_RELEASE ||
        r4==KUI_GD_STOP || r4==KUI_GD_COMMAND_INIT)) {
        uint32_t command=r4==KUI_GD_COMMAND_INIT?KUI_TOY_PILOT_RESET:r4;
        service.count=(uint32_t)toy_command(command,service.outputs[0],service.outputs[1],service.outputs[2]);
        if(!service.count) {
            service.error=KUI_GD_ERROR_UNAVAILABLE;
            service.status=KUI_GD_FAILED;service.pending=0;
        }
    }
    return result;
}

void kui_retail_menu_return(uint32_t command,uint32_t caller,uint32_t stack) {
    (void)command;(void)caller;(void)stack;
    const struct kui_toy_pilot_snapshot *p=toy_snapshot();
    (void)toy_command(KUI_TOY_PILOT_RESET,0,0,0);
    /* The terminal path stops servicing. Finite hardware banks end on their
     * own. Preserve scalar telemetry before display/cache reuse. */
    struct kui_toy_pilot_snapshot *saved=(struct kui_toy_pilot_snapshot *)(void *)image.block;
    _Static_assert(sizeof(*saved)<=sizeof(image.block),"terminal pilot telemetry");
    if(p) memcpy(saved,p,sizeof(*saved));else memset(saved,0,sizeof(*saved));
    /* Compact numeric pages keep the low firmware reservation unchanged.
     * The package includes the exact sequential word legend for each page. */
    memset(image.block+sizeof(*saved),0,256u-sizeof(*saved));
    for(uint32_t page=0;;page=(page+1u)%4u) {
        retail_display_restore(&display);
        retail_display_values("PILOT PAGE",&page,1);
        const uint32_t *words=(const uint32_t *)(const void *)image.block;
        for(unsigned row=0;row<4u;row++)
            retail_display_values("WORDS",words+page*16u+row*4u,4);
        retail_display_pause(900u);
    }
}
