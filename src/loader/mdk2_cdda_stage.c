/* SPDX-License-Identifier: GPL-3.0-only */
/* Isolated MDK2 CDDA candidate. The ordinary and Toy targets are unchanged. */
#if !KUI_RETAIL_TOY_PILOT || !KUI_RETAIL_MDK2_CDDA
#error This stage is only for the separate MDK2 CDDA profile
#endif
#if !KUI_RETAIL_LOW_RESIDENT || defined(KUI_RETAIL_CE) || defined(KUI_RETAIL_ASYNC)
#error The MDK2 CDDA requires the synchronous low native resident
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
#include "kui/mdk2_cdda_map.h"
#include "mdk2_cdda_admission.h"
#include "toy_pilot_resident_symbols.h"
#if KUI_NATIVE_CDDA_COMPAT
const volatile struct kui_native_cdda_descriptor kui_native_cdda_adapter_descriptor=
    KUI_NATIVE_CDDA_MDK2_DESCRIPTOR;
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

#define MDK2_HEAP_HEADER UINT32_C(0x8cfcffe0)
#define MDK2_LEASE_UNITS UINT32_C(0x1801)
#define MDK2_INITIAL_FREE_UNITS UINT32_C(0x580c0)
#define MDK2_FREE_UNITS UINT32_C(0x568bf)
static const uint8_t mdk2_boot_sha[32]={
    0xac,0x58,0x26,0xc9,0xa3,0x71,0xde,0x04,0x20,0x77,0x48,0xe6,0x0e,0x49,0xe0,0xa3,
    0xc1,0x3a,0x39,0x46,0x89,0x02,0xf8,0x9d,0x8c,0x1c,0x82,0xe1,0xf5,0x44,0xbb,0x48};
static void toy_launch_options_check(void) {
#if KUI_NATIVE_CDDA_COMPAT
    if(kui_native_cdda_adapter_descriptor.binding!=KUI_NATIVE_CDDA_MDK2 ||
       kui_native_cdda_adapter_descriptor.cooked ||
       !kui_native_cdda_manifest_compatible(&manifest,KUI_NATIVE_CDDA_MDK2,false))
        stopped("EXPERIMENTAL NATIVE CDDA MAP REFUSED",manifest.boot_crc32);
    retail_display_line("EXPERIMENTAL NATIVE CDDA LAYOUT");
#else
    if(manifest.reader!=KUI_RETAIL_READER_STANDARD)
        stopped("MDK2 CDDA NEEDS STANDARD READER",manifest.reader);
    for(unsigned i=0;i<manifest.track_count;i++)
        if(kui_retail_track_sector_bytes(&manifest.slots[i].track)!=2352u)
            stopped("MDK2 CDDA NEEDS ORIGINAL RAW IMAGE",i+1u);
#endif
}

struct toy_patch {uint32_t address,original;uint16_t bytes,kind;};
enum toy_patch_kind {MDK2_HEAP,MDK2_PICKER,MDK2_SERVICE,MDK2_AM_INIT,
    MDK2_DRIVER_LOAD,MDK2_SHUTDOWN,MDK2_SHUTDOWN2,MDK2_HEAP_RESET,
    MDK2_ALLSTOP,MDK2_CHECK,MDK2_HEAP_INIT};
/* Numerical preconditions are checked together before any hook is installed.
 * Reserve channels61/62 only on the round-robin branch: streamingchannel63
 * keeps its original literal and path. No proprietary implementation is
 * bundled with this profile. */
static const struct toy_patch toy_patches[]={
    {KUI_TOY_BOOT_HEAP_POOL,KUI_TOY_BOOT_HEAP_INIT,4,MDK2_HEAP},
    {0x8c0692d0u,0x0d000000u,4,MDK2_CHECK},
    {0x8c0692d4u,0x80000000u,4,MDK2_CHECK},
    {0x8c06920eu,0xd232u,2,MDK2_CHECK},
    {0x8c06921au,0x420bu,2,MDK2_CHECK},
    {0x8c06921cu,0x7420u,2,MDK2_CHECK},
    {KUI_MDK2_BOOT_CACHE_POLICY_POOL,KUI_MDK2_BOOT_CACHE_POLICY_NATIVE,4,MDK2_CHECK},
    {0x8c01818eu,0xa0a5u,2,MDK2_CHECK},
    {0x8c018190u,0x0009u,2,MDK2_PICKER},
    {0x8c017ba4u,0x8c075f76u,4,MDK2_HEAP_INIT},
    {0x8c0190d0u,0x8c017416u,4,MDK2_SERVICE},
    {0x8c019494u,0x8c074d34u,4,MDK2_AM_INIT},
    {0x8c01948cu,0x8c07545au,4,MDK2_DRIVER_LOAD},
    {0x8c0192a4u,0x8c075096u,4,MDK2_SHUTDOWN},
    {0x8c019324u,0x8c075096u,4,MDK2_SHUTDOWN2},
    {0x8c019260u,0x8c073e80u,4,MDK2_ALLSTOP},
    {0x8c0192a0u,0x8c07571cu,4,MDK2_HEAP_RESET}
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
    return kui_native_cdda_manifest_compatible(&manifest,KUI_NATIVE_CDDA_MDK2,false);
#else
    return kui_mdk2_cdda_manifest_admit(&manifest);
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
    c->bridge_active=0;
#if KUI_TOY_PILOT_LOADER_TRACE
    c->trace_terminal=0;
#endif
    toy_publish((uint32_t)(uintptr_t)c,
        (uint32_t)(uintptr_t)c+sizeof(*c));
    retail_display_restore(&display);
    retail_display_hex("MDK2 CDDA BOOT FAILURE",error);
    retail_display_hex("DETAIL",detail);
    retail_display_hex("MAIN LEASE",c->lease);
    stopped("MDK2 CDDA REFUSED: PHOTO SCREEN",error);
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
       stack_top!=e->worker_end || e->worker_end>KUI_MDK2_CDDA_MAP_ADDRESS ||
       ((e->bss_begin|e->bss_end|e->stack_bottom|e->stack_top|e->worker_end)&31u) ||
       !toy_entry(e->initialize,bytes) || !toy_entry(e->request,bytes) ||
       !toy_entry(e->service_hook,bytes) || !toy_entry(e->am_init_hook,bytes) ||
       !toy_entry(e->shutdown_hook,bytes) || !toy_entry(e->snapshot,bytes) ||
       !toy_entry(e->allstop_hook,bytes) || !toy_entry(e->driver_load_hook,bytes) ||
       !toy_entry(e->gd_dispatch,bytes) || e->pause_hook ||
       !toy_entry(e->mdk_shutdown2_hook,bytes) || !toy_entry(e->mdk_heap_reset_hook,bytes) ||
       !toy_entry(e->mdk_heap_init_hook,bytes) ||
       (e->bridge_active&3u) || e->bridge_active<e->bss_begin ||
       e->bridge_active>e->bss_end-4u)
        toy_failure(3,e->worker_end);
#if KUI_TOY_PILOT_LOADER_TRACE
    if(!toy_entry(e->trace_terminal,bytes)) toy_failure(3,e->trace_terminal);
#endif
}
static void toy_original_patches_check(int installed_heap) {
    for(unsigned i=0;i<sizeof(toy_patches)/sizeof(toy_patches[0]);i++) {
        const struct toy_patch *p=&toy_patches[i];
        uint32_t expected=p->kind==MDK2_HEAP && installed_heap?
            KUI_TOY_PILOT_LOW_HEAP_HOOK:p->original;
        if(toy_read(p->address,p->bytes)!=expected) toy_failure(4,p->address);
    }
}

/* Called exactly once after the real SDK heap init, before its caller starts
 * filesystem initialization. The game owns this lease; temporary stage code
 * is discarded immediately after this installation returns. */
void kui_toy_pilot_stage_install(void) {
    volatile struct kui_toy_pilot_boot_control *c=toy_control();
    c->status=KUI_TOY_BOOT_INITIALIZING;
    if((toy_read(0xff00001cu,4)&0x105u)!=KUI_MDK2_BOOT_CACHE_POLICY_NATIVE ||
       c->heap_begin!=KUI_TOY_BOOT_HEAP_BEGIN_VALUE ||
       c->heap_bytes!=KUI_TOY_BOOT_HEAP_BYTES_VALUE ||
       c->entry_sp<0x8c00c100u || c->entry_sp>0x8c00f400u || (c->entry_sp&3u) ||
       toy_read(KUI_MDK2_BOOT_HEAP_BEGIN,4)!=KUI_TOY_BOOT_HEAP_BEGIN_VALUE ||
       toy_read(KUI_MDK2_BOOT_HEAP_BASE_COPY,4)!=KUI_TOY_BOOT_HEAP_BEGIN_VALUE ||
       toy_read(KUI_MDK2_BOOT_HEAP_BYTES,4)!=KUI_TOY_BOOT_HEAP_BYTES_VALUE ||
       toy_read(KUI_MDK2_BOOT_HEAP_CURSOR,4)!=KUI_MDK2_BOOT_HEAP_SENTINEL ||
       toy_read(KUI_MDK2_BOOT_HEAP_SENTINEL,4)!=KUI_TOY_BOOT_HEAP_BEGIN_VALUE ||
       toy_read(KUI_MDK2_BOOT_HEAP_SENTINEL+4u,4)!=0u ||
       toy_read(KUI_TOY_BOOT_HEAP_BEGIN_VALUE,4)!=KUI_MDK2_BOOT_HEAP_SENTINEL ||
       toy_read(KUI_TOY_BOOT_HEAP_BEGIN_VALUE+4u,4)!=MDK2_INITIAL_FREE_UNITS ||
       (uint32_t)(uintptr_t)__retail_stage_bss_end>MDK2_HEAP_HEADER)
        toy_failure(5,c->entry_sp);
    toy_original_patches_check(1);
    uint32_t bytes=(uint32_t)(toy_worker_blob_end-toy_worker_blob);
    struct kui_toy_pilot_exports e;
    toy_exports_check(&e,bytes);
    void *(*allocate)(uint32_t)=(void *(*)(uint32_t))(uintptr_t)KUI_MDK2_BOOT_HEAP_ALLOCATE;
    c->lease=(uint32_t)(uintptr_t)allocate(KUI_TOY_BOOT_LEASE_BYTES);
    if(c->lease!=KUI_TOY_BOOT_LEASE_BEGIN ||
       toy_read(MDK2_HEAP_HEADER+4u,4)!=MDK2_LEASE_UNITS ||
       toy_read(KUI_TOY_BOOT_HEAP_BEGIN_VALUE+4u,4)!=MDK2_FREE_UNITS ||
       toy_read(KUI_MDK2_BOOT_HEAP_CURSOR,4)!=KUI_MDK2_BOOT_HEAP_SENTINEL ||
       toy_read(KUI_MDK2_BOOT_HEAP_SENTINEL,4)!=KUI_TOY_BOOT_HEAP_BEGIN_VALUE ||
       toy_read(KUI_TOY_BOOT_HEAP_BEGIN_VALUE,4)!=KUI_MDK2_BOOT_HEAP_SENTINEL)
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
    /* The stage map is validated and immutable, but its transient storage
     * cannot survive ordinary game allocations. This page belongs to the
     * real first lease, follows the worker's complete stack, and stays below
     * the SDK scratch padding. Publish old P1 aliases before writing via P2. */
    _Static_assert(sizeof(manifest)<=KUI_MDK2_CDDA_MAP_BYTES,"MDK2 held map page");
    toy_publish(KUI_MDK2_CDDA_MAP_ADDRESS,KUI_MDK2_CDDA_MAP_LIMIT);
    struct kui_retail_manifest *owned_map=
        (struct kui_retail_manifest *)(uintptr_t)KUI_MDK2_CDDA_MAP_ALIAS;
    memset(owned_map,0,KUI_MDK2_CDDA_MAP_BYTES);
    memcpy(owned_map,&manifest,sizeof(manifest));
    if(memcmp(owned_map,&manifest,sizeof(manifest)) ||
       toy_read(KUI_TOY_PILOT_LOW_MANIFEST_CONTROL,4)!=(uint32_t)(uintptr_t)&manifest)
        toy_failure(13,KUI_MDK2_CDDA_MAP_ALIAS);
    int (*bind)(const struct kui_retail_manifest *,const struct kui_retail_manifest *)=
        (int (*)(const struct kui_retail_manifest *,const struct kui_retail_manifest *))
            (uintptr_t)KUI_TOY_PILOT_LOW_MANIFEST_BIND;
    if(!bind(&manifest,owned_map) ||
       toy_read(KUI_TOY_PILOT_LOW_MANIFEST_CONTROL,4)!=KUI_MDK2_CDDA_MAP_ALIAS)
        toy_failure(14,KUI_TOY_PILOT_LOW_MANIFEST_CONTROL);
    const struct kui_toy_pilot_config config={
        KUI_TOY_PILOT_MAGIC,KUI_TOY_PILOT_API,sizeof(config),
        KUI_MDK2_CDDA_MAP_ALIAS,KUI_TOY_PILOT_LOW_READ_RAW,
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
    for(unsigned i=0;i<sizeof(toy_patches)/sizeof(toy_patches[0]);i++) {
        const struct toy_patch *p=&toy_patches[i];
        uint32_t value=p->original;
        switch(p->kind) {
            case MDK2_PICKER:value=0xe43du;break;
            case MDK2_SERVICE:value=e.service_hook;break;
            case MDK2_AM_INIT:value=e.am_init_hook;break;
            case MDK2_DRIVER_LOAD:value=e.driver_load_hook;break;
            case MDK2_SHUTDOWN:value=e.shutdown_hook;break;
            case MDK2_SHUTDOWN2:value=e.mdk_shutdown2_hook;break;
            case MDK2_HEAP_RESET:value=e.mdk_heap_reset_hook;break;
            case MDK2_HEAP_INIT:value=e.mdk_heap_init_hook;break;
            case MDK2_ALLSTOP:value=e.allstop_hook;break;
            /* The low heap guard remains installed for the entire run. */
            case MDK2_HEAP:continue;
            default:continue;
        }
        toy_write(p->address,p->bytes,value);
        toy_publish(p->address,p->address+p->bytes);
    }
    c->worker_end=e.worker_end;c->request=e.request;c->snapshot=e.snapshot;c->gd_dispatch=e.gd_dispatch;
    c->bridge_active=e.bridge_active;
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
    if(exec_bytes!=KUI_MDK2_BOOT_BYTES || boot_crc!=KUI_MDK2_BOOT_CRC ||
       !toy_manifest_admit() ||
       !kui_retail_observe_native_ip((const uint8_t *)(uintptr_t)KUI_RETAIL_IP_ADDRESS))
        toy_failure(8,exec_bytes);
    struct kui_sha256 hash;uint8_t digest[32];
    kui_sha256_init(&hash);
    kui_sha256_update(&hash,(const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,exec_bytes);
    kui_sha256_digest(&hash,digest);
    if(memcmp(digest,mdk2_boot_sha,sizeof(digest))) toy_failure(9,boot_crc);
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
    /* Preserve the admitted native cache policy. This first transaction
     * replaces only the unique early heap-init pointer. */
    toy_write(KUI_TOY_BOOT_HEAP_POOL,4,KUI_TOY_PILOT_LOW_HEAP_HOOK);
    toy_publish(KUI_TOY_BOOT_HEAP_POOL,KUI_TOY_BOOT_HEAP_POOL+4u);
    ((uint32_t *)(uintptr_t)frame)[5]=KUI_TOY_PILOT_LOW_RETURN_HOOK;
}
