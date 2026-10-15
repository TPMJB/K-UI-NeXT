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
#include "kui/toy_pilot_scratch.h"
#if KUI_RETAIL_MDK2_CDDA
#include "kui/mdk2_cdda_scratch.h"
#endif
#if KUI_RETAIL_MDK2_CDDA
#include "kui/mdk2_cdda_diagnostics.h"
#endif

/* Export initialized pointer words so the stage symbol generator need not
 * depend on compiler names for local LTO objects. */
#if KUI_RETAIL_MDK2_CDDA
/* This exports the protected pointer word, never the map object itself. */
const void * const kui_toy_pilot_manifest_pointer=&mdk2_manifest;
#else
const void * const kui_toy_pilot_manifest_pointer=&manifest;
#endif
#if KUI_TOY_PILOT_SHARED_SCI || KUI_TOY_PILOT_DATA_SCHEDULE_MODE == 2
const void * const kui_toy_pilot_card_pointer=&card.device.sd;
#endif
/* The worker treats only command16/17 as data priority. An audio mailbox
 * handoff must not block its own prefill. */
const void * const kui_toy_pilot_pending_pointer=&service.command;
const volatile void * const kui_toy_pilot_active_pointer=&kui_retail_hook_active;
/* Claimed only by this serialized dispatch, never by a guest address. */
#if KUI_RETAIL_MDK2_CDDA
static struct kui_mdk2_cdda_scratch mdk2_scratch;
#else
static uint32_t toy_scratch;
#endif

static uint8_t *toy_map(void *context,uint32_t address,uint32_t bytes,int writing) {
#if KUI_RETAIL_MDK2_CDDA
    if(kui_mdk2_cdda_map_overlaps((uint32_t)(uintptr_t)mdk2_manifest,address,bytes))
        return NULL;
#endif
    /* The allocator's header and worker are owned. FE..RAM-end padding remains
     * available to this title's established direct SDK scratch accesses. */
    if(bytes && address<0x8cfe0000u && address+bytes>0x8cfcffe0u &&
#if KUI_RETAIL_MDK2_CDDA
       !kui_mdk2_cdda_scratch_maps(&mdk2_scratch,address,bytes,writing,
           service.token,service.command,service.executing))
#else
       !kui_toy_pilot_scratch_maps(toy_scratch,address,bytes,writing))
#endif
        return NULL;
    return map_guest(context,address,bytes,writing);
}

int kui_toy_pilot_read_raw(uint32_t lba,uint32_t count,void *output) {
    /* Output is the worker's fixed private raw buffer, never a GD-supplied
     * destination. The linked worker validates its lease before this call;
     * the existing image reader validates all source map extents again. */
    if(count!=1u) return -1;
    /* Sound SDK work runs outside this mask. Only the shared SCI card visit
     * acquires the existing resident lock, with exact caller SR restored. */
    uint32_t saved,masked;
    __asm__ __volatile__("stc sr,%0":"=r"(saved));
    masked=saved|0x100000f0u;
    __asm__ __volatile__("ldc %0,sr"::"r"(masked):"memory","t");
    int result=-1;
    if(!kui_retail_hook_active
#if !KUI_TOY_PILOT_SHARED_SCI
       && service.command!=KUI_GD_PIOREAD && service.command!=KUI_GD_DMAREAD
#endif
       ) {
        kui_retail_hook_active=1;
        result=read_sectors(NULL,lba,count,2352u,output);
        kui_retail_hook_active=0;
    }
    __asm__ __volatile__("ldc %0,sr"::"r"(saved):"memory","t");
    return result;
}

int kui_retail_resident_init(const struct kui_retail_manifest *prepared,
    const struct kui_retail_storage *prepared_card,uint32_t original_gd_vector,
    const struct retail_display_state *saved_display) {
    int result=kui_toy_pilot_base_init(prepared,prepared_card,original_gd_vector,saved_display);
    service.ops.map=toy_map; /* Failed init never enters the game. */
    /* P2 callback state goes straight to RAM after the stage's initial purge.
     * The retained P1 profile must publish the changed callback. */
#if !KUI_TOY_PILOT_PRIVATE_P2
    purge((uint32_t)(uintptr_t)&service,sizeof(service));
#endif
    return result;
}

#if !KUI_TOY_PILOT_SHARED_SCI && !KUI_TOY_PILOT_LOADER_TRACE
static int toy_command(uint32_t command,uint32_t a,uint32_t b,uint32_t c) {
    uint32_t entry=kui_toy_pilot_boot_control.request;
    /* The stage publishes only validated linked entry addresses, after full
     * initialization. This low control is outside all guest mapped writes. */
    if(kui_toy_pilot_boot_control.status!=KUI_TOY_BOOT_INSTALLED) return 0;
    return ((int (*)(uint32_t,uint32_t,uint32_t,uint32_t))(uintptr_t)entry)(command,a,b,c);
}
#endif
#if !KUI_TOY_PILOT_LOADER_TRACE
static const struct kui_toy_pilot_snapshot *toy_snapshot(void) {
    uint32_t entry=kui_toy_pilot_boot_control.snapshot;
    if(kui_toy_pilot_boot_control.status!=KUI_TOY_BOOT_INSTALLED) return NULL;
    /* The exact linked function returns its fixed resident owner.stats.
     * It cannot select a guest-supplied pointer. */
    return ((const struct kui_toy_pilot_snapshot *(*)(void))(uintptr_t)entry)();
}

#endif

int32_t kui_retail_resident_dispatch(uint32_t r4,uint32_t r5,uint32_t r6,uint32_t r7) {
    /* This entry is masked and uses the existing small GD stack. Only integer
     * mailbox/snapshot work is allowed here; SDK and audio reads stay in the
     * game's normal service hook on the separately leased worker stack. */
    int real=kui_retail_hook_source==1u || r6!=UINT32_MAX;
#if KUI_RETAIL_MDK2_CDDA
    kui_mdk2_cdda_scratch_clear(&mdk2_scratch);
#else
    toy_scratch=0u;
#endif
    if(real && kui_toy_pilot_boot_control.status==KUI_TOY_BOOT_INSTALLED) {
#if KUI_RETAIL_MDK2_CDDA
        uint32_t pointer=kui_toy_pilot_boot_control.bridge_active;
        uint32_t top=kui_toy_pilot_boot_control.worker_end,sr;
        __asm__ __volatile__("stc sr,%0":"=r"(sr));
        /* The installer validated this P2 BSS word. Recheck its domain
         * before dereferencing; the map and private stack never qualify. */
        uint32_t active=top>=0x8cfd2000u && top<=0x8cfd8000u &&
            !(pointer&3u) && pointer>=0xacfd0000u &&
            pointer<top+0x20000000u-KUI_TOY_PILOT_STACK_BYTES?
            *(volatile const uint32_t *)(uintptr_t)pointer:0u;
        const struct kui_mdk2_cdda_scratch_proof proof={
            kui_retail_native_caller[1],kui_retail_native_caller[0],top,
            active,kui_retail_hook_active,sr,service.token,service.command,service.executing};
        kui_mdk2_cdda_scratch_begin(&mdk2_scratch,&proof,r7,r4,r5);
#else
        if(r7==KUI_GD_CHECK || (r7==KUI_GD_REQUEST && r4==KUI_RETAIL_GD_REQ_STAT))
            toy_scratch=kui_toy_pilot_scratch_capability(r7,r4,r5,
                kui_retail_native_caller[0],kui_retail_native_caller[1],
                kui_toy_pilot_boot_control.worker_end);
#endif
        typedef int32_t (*adapter)(struct kui_retail_gd *,uint32_t,uint32_t,uint32_t,uintptr_t);
        int32_t result=((adapter)(uintptr_t)kui_toy_pilot_boot_control.gd_dispatch)(
            &service,r4,r5,r7,(uintptr_t)kui_toy_pilot_base_dispatch);
#if KUI_RETAIL_MDK2_CDDA
        kui_mdk2_cdda_scratch_clear(&mdk2_scratch);
#else
        toy_scratch=0u;
#endif
        return result;
    }
    return kui_toy_pilot_base_dispatch(r4,r5,r6,r7);
}

void kui_retail_menu_return(uint32_t command,uint32_t caller,uint32_t stack) {
    (void)command;(void)caller;(void)stack;
#if KUI_TOY_PILOT_LOADER_TRACE
    if(kui_toy_pilot_boot_control.status==KUI_TOY_BOOT_INSTALLED) {
        typedef void (*terminal)(const struct retail_display_state *,const void *,const void *,uint32_t);
        ((terminal)(uintptr_t)kui_toy_pilot_boot_control.trace_terminal)(
            &display,&toy_gd_timing,&service.diag,0u);
    }
    for(;;) {
        retail_display_restore(&display);
        retail_display_line("TRACE TERMINAL REFUSED");
        retail_display_pause(1600u);
    }
#else
    const struct kui_toy_pilot_snapshot *p=toy_snapshot();
#if KUI_RETAIL_MDK2_CDDA
    /* Capture before our RESET, and keep evidence from before native sound
     * teardown in its MDK-only extension. Four named pages replace the
     * empty pressure pages for this profile without increasing low BSS. */
    _Static_assert(sizeof(struct kui_mdk2_cdda_diagnostics)==256u,
        "MDK2 terminal extension size");
    memset(image.block,0,256u);
    if(p && p->version==KUI_TOY_PILOT_API && p->bytes==sizeof(*p)) {
        const struct kui_mdk2_cdda_extended_snapshot *extended=
            (const struct kui_mdk2_cdda_extended_snapshot *)(const void *)p;
        memcpy(image.block,&extended->diagnostics,128u);
        memcpy(image.block+128u,&extended->diagnostics.sound,64u);
        uint32_t *runtime=(uint32_t *)(void *)(image.block+192u);
        runtime[0]=p->state;runtime[1]=p->fault;
        runtime[2]=p->generation;runtime[3]=p->applied_generation;
        runtime[4]=p->sdk_init_result;runtime[5]=p->driver_generation;
        runtime[6]=p->driver_verified;runtime[7]=p->shutdowns;
        runtime[8]=p->raw_calls;runtime[9]=p->raw_errors;
        runtime[10]=p->filled_frames;runtime[11]=p->bank_starts;
        runtime[12]=p->service_calls;runtime[13]=p->service_skips;
        runtime[14]=p->sound_address;runtime[15]=p->stack_fault;
    }
    (void)toy_command(KUI_TOY_PILOT_RESET,0,0,0);
    static const char *const labels[4][4]={
        {"TAG VER PLAY OK","BAD WHY P0 P1","P2 FLG EPOCH MAIL","DRV INST AC AM"},
        {"TAG EVT FN ARG0","ARG1 R0 WHY CMD","ERR ST TOK LBA","CNT DST FLG PEND"},
        {"LBA TRK PEAK NZ","LPCM LRAM RPCM RRAM","LC RC LS RS","LT RT MV FLAGS"},
        {"ST FAULT GEN DONE","INIT DRV VERIFY SHUT","READ ERR FILL START","CALL SKIP SOUND STACK"}
    };
    static uint32_t mdk_page;
    for(mdk_page=0;;mdk_page=(mdk_page+1u)%4u) {
        retail_display_restore(&display);
        retail_display_values("MDK2 PAGE",&mdk_page,1);
        const uint32_t *words=(const uint32_t *)(const void *)image.block;
        for(unsigned row=0;row<4u;row++)
            retail_display_values(labels[mdk_page][row],words+mdk_page*16u+row*4u,4);
        retail_display_pause(900u);
    }
#else
    /* The terminal path stops servicing; an owned ring may repeat until
     * reset. Preserve scalar telemetry before display/cache reuse. */
    _Static_assert(KUI_TOY_PILOT_API==8u && sizeof(*p)==448u,"terminal pilot telemetry v8");
#if KUI_TOY_PILOT_GD_FIXED_STEP
    const unsigned extension_offset=384u;
    const unsigned pages=8u;
#else
    const unsigned extension_offset=320u;
    const unsigned pages=7u;
#endif
    _Static_assert(512u<=sizeof(image.block),"terminal pilot report workspace");
    if(p && p->version==KUI_TOY_PILOT_API && p->bytes==sizeof(*p)) {
        memcpy(image.block,p,320u);
        memcpy(image.block+extension_offset,(const uint8_t *)(const void *)p+320u,128u);
#if KUI_TOY_PILOT_SHARED_SCI
        /* Cache transport evidence before RESET fences and clears owners. */
        memcpy(image.block+320u,(const uint8_t *)(const void *)p+448u,64u);
#endif
    } else memset(image.block,0,extension_offset+128u);
#if KUI_TOY_PILOT_SHARED_SCI
    /* Fence transport on the separate high GD stack. The low terminal
     * stack retains only scalar snapshot/display work. RESET in this
     * adapter cancels DMA before changing the service's ownership. */
    (void)kui_retail_resident_dispatch(0,0,0,KUI_GD_RESET);
#else
    (void)toy_command(KUI_TOY_PILOT_RESET,0,0,0);
#endif
    /* Compact numeric pages keep the low firmware reservation unchanged.
     * The package includes the exact sequential word legend for each page. */
#if KUI_TOY_PILOT_GD_FIXED_STEP && !KUI_TOY_PILOT_SHARED_SCI
    uint32_t *gd=(uint32_t *)(void *)(image.block+320u);
    gd[0]=0x47444d31u;gd[1]=KUI_TOY_PILOT_GD_FIXED_STEP;
    memcpy(gd+2,&toy_gd_timing,sizeof(toy_gd_timing));
    gd[8]=service.diag.calls;gd[9]=service.diag.requests;
    gd[10]=service.diag.rejected;gd[11]=service.diag.last_error;
    gd[12]=gd[13]=gd[14]=gd[15]=0u; /* Reserved; no second timing model. */
#endif
    static uint32_t page;
    for(page=0;;page=(page+1u)%pages) {
        retail_display_restore(&display);
        retail_display_values("PILOT PAGE",&page,1);
        const uint32_t *words=(const uint32_t *)(const void *)image.block;
        for(unsigned row=0;row<4u;row++)
            retail_display_values("WORDS",words+page*16u+row*4u,4);
        retail_display_pause(900u);
    }
#endif
#endif
}
