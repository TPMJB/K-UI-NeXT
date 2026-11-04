/* SPDX-License-Identifier: GPL-3.0-only */
/* Separate candidate translation unit: ordinary stage behavior is unchanged. */
#if !KUI_RETAIL_TOY_PILOT
#error This stage is only for the separate Toy pilot
#endif
#if !KUI_RETAIL_LOW_RESIDENT || defined(KUI_RETAIL_CE) || defined(KUI_RETAIL_ASYNC)
#error The Toy pilot requires the synchronous low native resident
#endif
static void toy_launch_options_check(void);
#define kui_retail_stage_relay kui_toy_pilot_base_relay
#include "retail_stage.c"
#undef kui_retail_stage_relay
#include "kui/hash.h"
#include "kui/native_cdda_compat.h"
#include "kui/retail_observe.h"
#include "kui/toy_pilot.h"
#include "kui/toy_pilot_boot.h"
#include "toy_pilot_admission.h"
#include "toy_pilot_cooked_admission.h"
#include "toy_pilot_resident_symbols.h"
#include "kui/toy_report_export.h"
#if KUI_TOY_TEXT_REPORT
#include "kui/core.h"
#include "kui/vmu_backing.h"
#include "kui/report_stage_cache.h"
static struct kui_report_file_descriptor toy_report_file;
static struct kui_vmu_fs_workspace toy_report_work;
static struct kui_vmu_fs_range toy_report_forbidden[KUI_RETAIL_MANIFEST_SLOTS];
static struct kui_report_stage_cache toy_report_cache __attribute__((aligned(32)));
static int toy_report_read_batch(void *context,uint32_t lba,uint32_t count,uint8_t *out) {
    struct kui_retail_storage *storage=context;
    return kui_loader_sd_read_multi(&storage->device.sd,lba,count,out)==KUI_LOADER_SD_OK &&
        kui_sci_sd_healthy()?0:-1;
}
/* Read-only ownership proof while the temporary stage still owns its card.
 * Nothing from the walker, its callbacks or filesystem scratch survives. */
static void toy_report_prepare(void) {
    if(card.transport!=KUI_STORAGE_SCI || card.stream.active ||
       !card.device.sd.ready || card.device.sd.slow)
        stopped("TOY TXT CARD NOT IDLE",0u);
    retail_display_line("VERIFYING TXT REPORT SLOT - KEEP POWER ON");
    enum kui_loader_sd_result acquired=kui_retail_storage_acquire(&card);
    if(acquired!=KUI_LOADER_SD_OK)stopped("TOY TXT CARD CLAIM REFUSED",acquired);
    struct kui_vmu_sd_identity identity;
    uint8_t sector0[512];struct kui_volume volume;
    int result=kui_vmu_sd_identity_read(&card.device.sd,&identity);
    size_t ranges=manifest.extent_count;
    if(manifest.track_count>KUI_RETAIL_MANIFEST_SLOTS ||
       ranges>KUI_RETAIL_MANIFEST_SLOTS-manifest.track_count)result=KUI_VMU_FS_ARGUMENT;
    for(size_t e=0u;!result && e<ranges;++e) {
        const struct kui_retail_extent *extent=&manifest.slots[manifest.track_count+e].extent;
        toy_report_forbidden[e]=(struct kui_vmu_fs_range){extent->card_lba,extent->blocks};
    }
    if(!result && kui_report_stage_cache_init(&toy_report_cache,identity.sectors,
       manifest.partition_start,manifest.partition_end,toy_report_forbidden,ranges,
       toy_report_read_batch,&card))result=KUI_VMU_FS_ARGUMENT;
    if(!result && (kui_report_stage_cache_read(&toy_report_cache,0u,sector0) ||
       !kui_select_volume(sector0,identity.sectors,&volume) ||
       volume.start!=manifest.partition_start ||
       (uint64_t)volume.start+volume.count!=manifest.partition_end))result=KUI_VMU_FS_VOLUME;
    if(!result) {
        const struct kui_report_file_input input={.read_sector=kui_report_stage_cache_read,.context=&toy_report_cache,
            .card_sectors=identity.sectors,.partition_first=volume.start,
            .partition_end=(uint64_t)volume.start+volume.count,.high_capacity=identity.high_capacity,
#if KUI_TOY_DECISION_PROFILE == 1
            .file_name="TOYCLEAN.TXT",
#else
            .file_name="TOYOBS.TXT",
#endif
            .profile=KUI_TOY_DECISION_PROFILE,.forbidden=toy_report_forbidden,.forbidden_count=ranges};
        struct kui_report_file_input actual=input;memcpy(actual.cid,identity.cid,16u);
        result=kui_report_file_admit(&actual,&toy_report_file,&toy_report_work);
    }
    bool healthy=kui_sci_sd_healthy() && card.device.sd.ready;
    kui_retail_storage_release(&card);
    /* The stage's plain SCI release may newly fail its bounded TEND wait.
     * Its existing first-fault record survives release (healthy no longer
     * applies once unacquired). Never publish a descriptor after that fault. */
    struct kui_sci_sd_fault released_fault;kui_sci_sd_fault_get(&released_fault);
    uint32_t counts[4]={toy_report_cache.requests,toy_report_cache.wire_calls,
        toy_report_cache.physical_sectors,toy_report_cache.hits};
    retail_display_values("TXT READS BATCHES SECTORS HITS",counts,4u);
    if(result || !healthy || !card.device.sd.ready || released_fault.valid) {
        retail_display_hex("TXT ADMISSION DETAIL",toy_report_work.stats.detail);
        stopped("TOY TXT FILE OWNERSHIP REFUSED",result?(uint32_t)result:UINT32_MAX);
    }
    retail_display_line("TOY TXT FILE OWNERSHIP VERIFIED");
}
#endif
#if KUI_NATIVE_CDDA_COMPAT
const volatile struct kui_native_cdda_descriptor kui_native_cdda_adapter_descriptor=
    KUI_NATIVE_CDDA_TOY_DESCRIPTOR(KUI_TOY_PILOT_COOKED_DATA_PROFILE);
#endif

#ifndef KUI_TOY_PILOT_WORKER_BIN
#error The authored fixed-linked worker binary must be embedded explicitly
#endif
__asm__(".section .rodata.toy_pilot_worker,\"a\"\n"
        ".balign 32\n"
        ".global __toy_pilot_worker_blob_start\n"
        "__toy_pilot_worker_blob_start:\n"
        ".incbin \"" KUI_TOY_PILOT_WORKER_BIN "\"\n"
        ".global __toy_pilot_worker_blob_end\n"
        "__toy_pilot_worker_blob_end:\n"
        ".previous\n");
extern const uint8_t toy_worker_blob[] __asm__("__toy_pilot_worker_blob_start");
extern const uint8_t toy_worker_blob_end[] __asm__("__toy_pilot_worker_blob_end");
extern const uint8_t __retail_stage_bss_end[] __asm__("__retail_stage_bss_end");

#define TOY_IP_CRC UINT32_C(0x38ba2868)
#define TOY_HEAP_TABLE UINT32_C(0x8c0c5ac8)
#define TOY_HEAP_HEADER UINT32_C(0x8cfcffe0)
#define TOY_LEASE_UNITS UINT32_C(0x1801)
#define TOY_FREE_UNITS UINT32_C(0x755e5)

static const uint8_t toy_boot_sha[32]={
    0xee,0x68,0xa3,0x26,0xda,0x96,0x4f,0xec,0x58,0x92,0xb7,0x22,0x63,0x11,0xf2,0x29,
    0x3f,0xd3,0xa0,0xbb,0xf5,0xba,0xc4,0x96,0xec,0x82,0x99,0x19,0x64,0xd4,0x9b,0xbd};
#if !KUI_TOY_PILOT_COOKED_DATA_PROFILE && !KUI_NATIVE_CDDA_COMPAT
static const uint32_t toy_track_ends[15]={7107u,7753u,201750u,219712u,234091u,
    255599u,272017u,282799u,303092u,323352u,341314u,356690u,374201u,377272u,549150u};
#endif

static void toy_launch_options_check(void) {
#if KUI_NATIVE_CDDA_COMPAT
    if(kui_native_cdda_adapter_descriptor.binding!=KUI_NATIVE_CDDA_TOY ||
       kui_native_cdda_adapter_descriptor.cooked!=KUI_TOY_PILOT_COOKED_DATA_PROFILE ||
       !kui_native_cdda_manifest_compatible(&manifest,KUI_NATIVE_CDDA_TOY,
            KUI_TOY_PILOT_COOKED_DATA_PROFILE!=0))
        stopped("EXPERIMENTAL NATIVE CDDA MAP REFUSED",manifest.boot_crc32);
    retail_display_line("EXPERIMENTAL NATIVE CDDA LAYOUT");
#else
#if KUI_TOY_PILOT_COOKED_DATA_PROFILE
    uint32_t reason=kui_toy_pilot_cooked_launch_refusal(&manifest);
#else
    uint32_t reason=kui_toy_pilot_launch_refusal(&manifest);
#endif
    if(!reason) return;
    retail_display_restore(&display);
    if(reason&KUI_TOY_PILOT_LAUNCH_READER) {
        retail_display_line("TOY PILOT NEEDS STANDARD READER");
        retail_display_line("USE A; X/Y BACKGROUND IS UNSUPPORTED");
    }
    if(reason&KUI_TOY_PILOT_LAUNCH_FORMAT) {
#if KUI_TOY_PILOT_COOKED_DATA_PROFILE
        retail_display_line("TOY PILOT NEEDS COOKED DATA IMAGE");
        retail_display_line("USE CANONICAL 2048 COPY / RAW AUDIO");
#else
        retail_display_line("TOY PILOT NEEDS ORIGINAL RAW IMAGE");
        retail_display_line("2048-BYTE COPY IS UNSUPPORTED");
#endif
    }
    stopped("TOY PILOT OPTIONS REFUSED",reason);
#endif
}

struct toy_patch {uint32_t address,original;uint16_t bytes,kind;};
enum toy_patch_kind {TOY_HEAP,TOY_PICKER,TOY_UPDATER_CAP,TOY_WRAP,
    TOY_SERVICE,TOY_AM_INIT,TOY_DRIVER_LOAD,TOY_SHUTDOWN,TOY_ALLSTOP,
    TOY_RESET_RETURN,TOY_CACHE_POLICY,TOY_PAUSE,TOY_CHECK};
/* Numerical contracts only; no game instructions or executable are bundled.
 * Every original value is checked as a transaction before the initial hook. */
static const struct toy_patch toy_patches[]={
    {0x8c04e9b4u,KUI_TOY_BOOT_HEAP_INIT,4,TOY_HEAP},
    {0x8c04e9acu,0x0d000000u,4,TOY_CHECK},
    {0x8c04e9b0u,0x80000000u,4,TOY_CHECK},
    {0x8c04e916u,0xd327u,2,TOY_CHECK},
    {0x8c04e920u,0x430bu,2,TOY_CHECK},
    {0x8c04e922u,0x7420u,2,TOY_CHECK},
    {0x8c04a8acu,0xed40u,2,TOY_PICKER},
    {0x8c04a29au,0xe240u,2,TOY_UPDATER_CAP},
    {0x8c04a5d2u,0x883fu,2,TOY_WRAP},
    {0x8c04ac38u,0x8c04a270u,4,TOY_SERVICE},
    {0x8c04a244u,0x8c06aa86u,4,TOY_AM_INIT},
    {0x8c06ab60u,0x8c06ac54u,4,TOY_DRIVER_LOAD},
    {0x8c04a050u,0x8c06aa0eu,4,TOY_SHUTDOWN},
    {0x8c04a04cu,0x8c068a96u,4,TOY_ALLSTOP},
    {0x8c04a258u,0x8c068a96u,4,TOY_ALLSTOP},
    {KUI_TOY_BOOT_RESET_CALLBACK_POOL,KUI_TOY_BOOT_RESET_CALLBACK_NATIVE,4,TOY_RESET_RETURN},
    {KUI_TOY_BOOT_RESET_REGISTER_SITE,KUI_TOY_BOOT_RESET_REGISTER_WORD,2,TOY_CHECK},
    {KUI_TOY_BOOT_RESET_INVOKE_SITE,KUI_TOY_BOOT_RESET_INVOKE_WORD,2,TOY_CHECK},
    {KUI_TOY_BOOT_CACHE_POLICY_POOL,KUI_TOY_BOOT_CACHE_POLICY_NATIVE,4,TOY_CACHE_POLICY},
    {KUI_TOY_BOOT_PAUSE_POOL,KUI_TOY_BOOT_PAUSE_NATIVE,4,TOY_PAUSE}
};

static volatile struct kui_toy_pilot_boot_control *toy_control(void) {
    return (volatile struct kui_toy_pilot_boot_control *)(uintptr_t)KUI_TOY_PILOT_LOW_CONTROL;
}
static uint32_t toy_read(uint32_t address,unsigned bytes) {
    return bytes==2u?*(volatile const uint16_t *)(uintptr_t)address:
        *(volatile const uint32_t *)(uintptr_t)address;
}
static int toy_manifest_admit(void) {
#if KUI_NATIVE_CDDA_COMPAT
    return kui_native_cdda_manifest_compatible(&manifest,KUI_NATIVE_CDDA_TOY,
        KUI_TOY_PILOT_COOKED_DATA_PROFILE!=0);
#elif KUI_TOY_PILOT_COOKED_DATA_PROFILE
    return kui_toy_pilot_cooked_manifest_admit(&manifest);
#else
    if(!kui_retail_observe_admit(&manifest) || manifest.boot_lba!=548634u)
        return 0;
    for(unsigned i=0;i<15u;i++)
        if(manifest.slots[i].track.end_lba!=toy_track_ends[i]) return 0;
    return 1;
#endif
}
static void toy_write(uint32_t address,unsigned bytes,uint32_t value) {
    if(bytes==2u) *(volatile uint16_t *)(uintptr_t)address=(uint16_t)value;
    else *(volatile uint32_t *)(uintptr_t)address=value;
}

/* Both routines are authored. Publish through P2: write back copied lines,
 * request only instruction-cache invalidation, and preserve all CCR mode
 * bits. This does not use the ordinary stage's whole-RAM/cache reset helper. */
void kui_toy_pilot_stage_publish(uint32_t begin,uint32_t end);
__asm__(".section .text.toy_pilot_stage_bridge,\"ax\"\n"
        ".align 2\n"
        ".global _kui_toy_pilot_stage_publish\n"
        "_kui_toy_pilot_stage_publish:\n"
        "mov.l 1f,r0\n"
        "jmp @r0\n"
        "nop\n"
        "2:\n"
        "mov #-32,r0\n"
        "and r0,r4\n"
        "3:\n"
        "cmp/hs r5,r4\n"
        "bt 4f\n"
        "ocbp @r4\n"
        "add #32,r4\n"
        "bra 3b\n"
        "nop\n"
        "4:\n"
        "mov.l 5f,r0\n"
        "mov.l @r0,r1\n"
        "mov.l 6f,r2\n"
        "or r2,r1\n"
        "mov.l r1,@r0\n"
        "nop\n"
        "nop\n"
        "nop\n"
        "nop\n"
        "nop\n"
        "nop\n"
        "nop\n"
        "nop\n"
        "rts\n"
        "nop\n"
        ".align 2\n"
        "1: .long 2b+0x20000000\n"
        "5: .long 0xff00001c\n"
        "6: .long 0x00000800\n"
        ".previous\n");

static inline void toy_publish(uint32_t begin,uint32_t end) {
#if KUI_TOY_PILOT_PRIVATE_P2
    begin=kui_toy_pilot_cached_address(begin);
    end=kui_toy_pilot_cached_address(end);
#endif
    kui_toy_pilot_stage_publish(begin,end);
}

static void toy_failure(uint32_t error,uint32_t detail) __attribute__((noreturn));
static void toy_failure(uint32_t error,uint32_t detail) {
    volatile struct kui_toy_pilot_boot_control *c=toy_control();
    c->status=KUI_TOY_BOOT_FAILED;c->error=error;
    c->installer=0;c->stage_stack=0;c->request=0;c->snapshot=0;c->gd_dispatch=0;
#if KUI_TOY_PILOT_LOADER_TRACE
    c->trace_terminal=0;
#endif
    toy_publish((uint32_t)(uintptr_t)c,
        (uint32_t)(uintptr_t)c+sizeof(*c));
    retail_display_restore(&display);
    retail_display_hex("TOY PILOT BOOT FAILURE",error);
    retail_display_hex("DETAIL",detail);
    retail_display_hex("MAIN LEASE",c->lease);
    stopped("TOY PILOT REFUSED: PHOTO SCREEN",error);
}
static int toy_entry(uint32_t address,uint32_t bytes) {
    return !(address&1u) && address>=KUI_TOY_PILOT_WORKER_BEGIN+sizeof(struct kui_toy_pilot_exports) &&
        address<KUI_TOY_PILOT_WORKER_BEGIN+bytes;
}
static void toy_exports_check(struct kui_toy_pilot_exports *e,uint32_t bytes) {
    if(bytes<sizeof(*e) || bytes>0x10000u) toy_failure(2,bytes);
    memcpy(e,toy_worker_blob,sizeof(*e));
    uint32_t bss_begin=kui_toy_pilot_cached_address(e->bss_begin);
    uint32_t bss_end=kui_toy_pilot_cached_address(e->bss_end);
    uint32_t stack_bottom=kui_toy_pilot_cached_address(e->stack_bottom);
    uint32_t stack_top=kui_toy_pilot_cached_address(e->stack_top);
    if(e->magic!=KUI_TOY_PILOT_MAGIC || e->version!=KUI_TOY_PILOT_API || e->bytes!=sizeof(*e) ||
       e->main_lease_bytes!=KUI_TOY_PILOT_MAIN_LEASE_BYTES ||
       !kui_toy_pilot_state_address(e->bss_begin,KUI_TOY_PILOT_WORKER_BEGIN,KUI_TOY_PILOT_WORKER_END) ||
       !kui_toy_pilot_state_address(e->stack_bottom,KUI_TOY_PILOT_WORKER_BEGIN,KUI_TOY_PILOT_WORKER_END) ||
       e->bss_end!=bss_end+KUI_TOY_PILOT_DATA_ALIAS ||
       e->stack_top!=stack_top+KUI_TOY_PILOT_DATA_ALIAS ||
       bss_begin<KUI_TOY_PILOT_WORKER_BEGIN+bytes || bss_begin>bss_end ||
       bss_end>stack_bottom || stack_bottom>stack_top ||
       e->stack_top-e->stack_bottom!=KUI_TOY_PILOT_STACK_BYTES ||
       stack_top!=e->worker_end || e->worker_end>KUI_TOY_PILOT_WORKER_END ||
       ((e->bss_begin|e->bss_end|e->stack_bottom|e->stack_top|e->worker_end)&31u) ||
       !toy_entry(e->initialize,bytes) || !toy_entry(e->request,bytes) ||
       !toy_entry(e->service_hook,bytes) || !toy_entry(e->am_init_hook,bytes) ||
       !toy_entry(e->shutdown_hook,bytes) || !toy_entry(e->snapshot,bytes) ||
       !toy_entry(e->allstop_hook,bytes) || !toy_entry(e->driver_load_hook,bytes) ||
       !toy_entry(e->gd_dispatch,bytes) || !toy_entry(e->pause_hook,bytes))
        toy_failure(3,e->worker_end);
#if KUI_TOY_PILOT_LOADER_TRACE
    if(!toy_entry(e->trace_terminal,bytes)) toy_failure(3,e->trace_terminal);
#endif
}
static void toy_original_patches_check(int installed_heap) {
    for(unsigned i=0;i<sizeof(toy_patches)/sizeof(toy_patches[0]);i++) {
        const struct toy_patch *p=&toy_patches[i];
        uint32_t expected=p->kind==TOY_HEAP && installed_heap?
            KUI_TOY_PILOT_LOW_HEAP_HOOK:p->original;
        if(p->kind==TOY_CACHE_POLICY && installed_heap)
            expected=KUI_TOY_PILOT_CACHE_POLICY_SELECTED;
        if(toy_read(p->address,p->bytes)!=expected) toy_failure(4,p->address);
    }
}

/* Called exactly once after the real SDK heap init, before its caller starts
 * filesystem initialization. The game owns this lease; temporary stage code
 * is discarded immediately after this installation returns. */
void kui_toy_pilot_stage_install(void) {
    volatile struct kui_toy_pilot_boot_control *c=toy_control();
    c->status=KUI_TOY_BOOT_INITIALIZING;
    if((toy_read(0xff00001cu,4)&0x105u)!=KUI_TOY_PILOT_CACHE_POLICY_SELECTED ||
       c->heap_begin!=KUI_TOY_BOOT_HEAP_BEGIN_VALUE ||
       c->heap_bytes!=KUI_TOY_BOOT_HEAP_BYTES_VALUE ||
       c->entry_sp<0x8c00c100u || c->entry_sp>0x8c00f400u || (c->entry_sp&3u) ||
       toy_read(0x8c117dbcu,4)!=TOY_HEAP_TABLE ||
       toy_read(TOY_HEAP_TABLE,4)!=0x8c0b3ef2u ||
       toy_read(TOY_HEAP_TABLE+4u,4)!=0x8c0b3fc0u ||
       toy_read(0x8c117de4u,4)!=KUI_TOY_BOOT_HEAP_BEGIN_VALUE ||
       toy_read(0x8c117decu,4)!=KUI_TOY_BOOT_HEAP_BEGIN_VALUE ||
       toy_read(0x8c117de8u,4)!=KUI_TOY_BOOT_HEAP_BYTES_VALUE ||
       toy_read(0x8c117de0u,4)!=0x8c117dc0u ||
       toy_read(0x8c117dc0u,4)!=KUI_TOY_BOOT_HEAP_BEGIN_VALUE ||
       toy_read(0x8c117dc4u,4)!=0u ||
       toy_read(KUI_TOY_BOOT_HEAP_BEGIN_VALUE,4)!=0x8c117dc0u ||
       toy_read(KUI_TOY_BOOT_HEAP_BEGIN_VALUE+4u,4)!=0x76de6u ||
       (uint32_t)(uintptr_t)__retail_stage_bss_end>TOY_HEAP_HEADER)
        toy_failure(5,c->entry_sp);
    toy_original_patches_check(1);
    uint32_t bytes=(uint32_t)(toy_worker_blob_end-toy_worker_blob);
    struct kui_toy_pilot_exports e;
    toy_exports_check(&e,bytes);
    void *(*allocate)(uint32_t)=(void *(*)(uint32_t))(uintptr_t)0x8c0b3edeu;
    c->lease=(uint32_t)(uintptr_t)allocate(KUI_TOY_BOOT_LEASE_BYTES);
    if(c->lease!=KUI_TOY_BOOT_LEASE_BEGIN ||
       toy_read(TOY_HEAP_HEADER+4u,4)!=TOY_LEASE_UNITS ||
       toy_read(KUI_TOY_BOOT_HEAP_BEGIN_VALUE+4u,4)!=TOY_FREE_UNITS ||
       toy_read(0x8c117de0u,4)!=0x8c117dc0u ||
       toy_read(0x8c117dc0u,4)!=KUI_TOY_BOOT_HEAP_BEGIN_VALUE ||
       toy_read(KUI_TOY_BOOT_HEAP_BEGIN_VALUE,4)!=0x8c117dc0u)
        toy_failure(6,c->lease);
    c->status=KUI_TOY_BOOT_LEASED;
    memcpy((void *)(uintptr_t)c->lease,toy_worker_blob,bytes);
#if KUI_TOY_PILOT_PRIVATE_P2
    /* Remove every old cached alias before the P2 BSS/stack owner starts.
     * The publication endpoint is physical/P1, never a P2 sweep endpoint. */
    toy_publish(c->lease,e.worker_end);
    memset((void *)(uintptr_t)e.bss_begin,0,e.bss_end-e.bss_begin);
#else
    memset((void *)(uintptr_t)e.bss_begin,0,e.bss_end-e.bss_begin);
    toy_publish(c->lease,e.bss_end);
#endif
    /* Verification must not refill a P1 alias of initialized private data. */
    if(memcmp((const void *)(uintptr_t)(c->lease+KUI_TOY_PILOT_DATA_ALIAS),
              toy_worker_blob,bytes))
        toy_failure(12,c->lease);
#if KUI_TOY_TEXT_REPORT
    size_t report_offset;
    if(!kui_report_file_descriptor_valid(&toy_report_file) ||
       !kui_toy_report_binding_offset(toy_worker_blob,bytes,KUI_TOY_DECISION_PROFILE,&report_offset))
        toy_failure(13,bytes);
    volatile struct kui_toy_report_binding *binding=(volatile struct kui_toy_report_binding *)(uintptr_t)
        (c->lease+KUI_TOY_PILOT_DATA_ALIAS+(uint32_t)report_offset);
    memcpy((void *)&binding->file,&toy_report_file,sizeof(toy_report_file));
    if(memcmp((const void *)&binding->file,&toy_report_file,sizeof(toy_report_file)))
        toy_failure(13,(uint32_t)report_offset);
#endif
    const struct kui_toy_pilot_config config={
        KUI_TOY_PILOT_MAGIC,KUI_TOY_PILOT_API,sizeof(config),
        KUI_TOY_PILOT_LOW_MANIFEST,KUI_TOY_PILOT_LOW_READ_RAW,
        KUI_TOY_PILOT_LOW_ACTIVE,KUI_TOY_PILOT_LOW_DATA_PENDING,
        c->lease,c->lease+KUI_TOY_BOOT_LEASE_BYTES,
        c->lease,e.worker_end,bytes,KUI_TOY_PILOT_LOW_RETURN_HOOK
#if KUI_TOY_PILOT_SHARED_SCI
        ,KUI_TOY_PILOT_LOW_SCI_CARD,KUI_TOY_PILOT_LOW_SCI_ACQUIRE,
        KUI_TOY_PILOT_LOW_SCI_RELEASE,KUI_TOY_PILOT_LOW_SCI_HEALTHY
#endif
        };
    /* INITIALIZE fills its stack guard and performs its own stack bridge. */
    int (*initialize)(const struct kui_toy_pilot_config *)=
        (int (*)(const struct kui_toy_pilot_config *))(uintptr_t)e.initialize;
    if(!initialize(&config))
        toy_failure(7,e.initialize);
    /* Recheck every original word before the multiword sound transaction. */
    toy_original_patches_check(1);
    uint32_t reset_target=kui_toy_pilot_reset_target(
        toy_read(KUI_TOY_BOOT_RESET_CALLBACK_POOL,4),
        toy_read(KUI_TOY_BOOT_RESET_REGISTER_SITE,2),
        toy_read(KUI_TOY_BOOT_RESET_INVOKE_SITE,2),KUI_TOY_PILOT_LOW_RETURN_HOOK);
    if(!reset_target) toy_failure(4,KUI_TOY_BOOT_RESET_CALLBACK_POOL);
    for(unsigned i=0;i<sizeof(toy_patches)/sizeof(toy_patches[0]);i++) {
        const struct toy_patch *p=&toy_patches[i];
        uint32_t value=p->original;
        switch(p->kind) {
            case TOY_PICKER:value=0xed3eu;break;
            case TOY_UPDATER_CAP:value=0xe23eu;break;
            case TOY_WRAP:value=0x883du;break;
            case TOY_SERVICE:value=e.service_hook;break;
            case TOY_AM_INIT:value=e.am_init_hook;break;
            case TOY_DRIVER_LOAD:value=e.driver_load_hook;break;
            case TOY_SHUTDOWN:value=e.shutdown_hook;break;
            case TOY_ALLSTOP:value=e.allstop_hook;break;
            /* Preserve the title's reset decision; publish its terminal
             * callback before normal startup registers it. That callback
             * must not enter the original unbounded global-stop retry. */
            case TOY_RESET_RETURN:value=reset_target;break;
            /* Startup consumes this policy before reaching the heap hook;
             * it was admitted and installed at the original entry relay. */
            case TOY_CACHE_POLICY:continue;
            case TOY_PAUSE:value=e.pause_hook;break;
            /* Keep the low persistent guard: a repeat call through this
             * exact slot must stop before reinitializing the leased heap. */
            case TOY_HEAP:continue;
            default:continue;
        }
        toy_write(p->address,p->bytes,value);
        toy_publish(p->address,p->address+p->bytes);
    }
    c->worker_end=e.worker_end;c->request=e.request;c->snapshot=e.snapshot;c->gd_dispatch=e.gd_dispatch;
#if KUI_TOY_PILOT_LOADER_TRACE
    c->trace_terminal=e.trace_terminal;
#endif
    c->installer=0;c->stage_stack=0;c->status=KUI_TOY_BOOT_INSTALLED;
    toy_publish((uint32_t)(uintptr_t)c,
        (uint32_t)(uintptr_t)c+sizeof(*c));
}

void kui_retail_stage_relay(const uint32_t *frame,uint32_t ccr) {
    kui_toy_pilot_base_relay(frame,ccr);
    /* The baseline relay has restored the original entry and checked the
     * unmodified entire executable and resident before these RAM patches. */
    if(!kui_toy_pilot_boot_identity(&manifest,exec_bytes,boot_crc) ||
#if !KUI_NATIVE_CDDA_COMPAT
       manifest.ip_crc32!=TOY_IP_CRC ||
#endif
       !toy_manifest_admit() ||
       !kui_retail_observe_native_ip((const uint8_t *)(uintptr_t)KUI_RETAIL_IP_ADDRESS))
        toy_failure(8,exec_bytes);
    struct kui_sha256 hash;uint8_t digest[32];
    kui_sha256_init(&hash);
    kui_sha256_update(&hash,(const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,exec_bytes);
    kui_sha256_digest(&hash,digest);
    if(memcmp(digest,toy_boot_sha,sizeof(digest))) toy_failure(9,boot_crc);
#if KUI_TOY_TEXT_REPORT
    toy_report_prepare();
#endif
    struct kui_toy_pilot_exports e;
    toy_exports_check(&e,(uint32_t)(toy_worker_blob_end-toy_worker_blob));
    toy_original_patches_check(0);
    volatile struct kui_toy_pilot_boot_control *c=toy_control();
    for(unsigned i=0;i<sizeof(*c)/4u;i++)
        if(toy_read(KUI_TOY_PILOT_LOW_CONTROL+4u*i,4))
            toy_failure(10,KUI_TOY_PILOT_LOW_CONTROL+4u*i);
    c->installer=(uint32_t)(uintptr_t)kui_toy_pilot_stage_install;
    c->stage_stack=KUI_RETAIL_STAGE_STACK-32u;c->status=KUI_TOY_BOOT_ARMED;
    toy_publish((uint32_t)(uintptr_t)c,
        (uint32_t)(uintptr_t)c+sizeof(*c));
    /* This must precede the first native system initialization. Changing it
     * after heap initialization would leave the active CCR in copy-back. */
    toy_write(KUI_TOY_BOOT_CACHE_POLICY_POOL,4,
        kui_toy_pilot_cache_policy(toy_read(KUI_TOY_BOOT_CACHE_POLICY_POOL,4)));
    toy_publish(KUI_TOY_BOOT_CACHE_POLICY_POOL,KUI_TOY_BOOT_CACHE_POLICY_POOL+4u);
    toy_write(KUI_TOY_BOOT_HEAP_POOL,4,KUI_TOY_PILOT_LOW_HEAP_HOOK);
    toy_publish(KUI_TOY_BOOT_HEAP_POOL,KUI_TOY_BOOT_HEAP_POOL+4u);
    ((uint32_t *)(uintptr_t)frame)[5]=KUI_TOY_PILOT_LOW_RETURN_HOOK;
}
