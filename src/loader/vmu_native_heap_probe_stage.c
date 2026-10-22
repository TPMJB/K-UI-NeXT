/* SPDX-License-Identifier: GPL-3.0-only */
/* Isolated native allocator adapter and separately enabled worker probes. */
#if !KUI_VMU_NATIVE_HEAP_PROBE || KUI_VMU_READONLY_PROOF != 0
#error Native heap adapter requires its isolated native build
#endif
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
#include "retail_storage.h"
static enum kui_loader_sd_result native_persist_storage_init(struct kui_retail_storage *,uint32_t);
#define kui_retail_storage_init native_persist_storage_init
#endif
#define kui_retail_stage_relay kui_native_heap_base_relay
#include "retail_stage.c"
#undef kui_retail_stage_relay
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
#undef kui_retail_storage_init
#include "vmu_native_persist_stage.inc"
#endif
#include "kui/vmu_native_discovery.h"
#include "kui/vmu_native_heap.h"
#include "kui/vmu_maple_observe_boot.h"
#include "kui/retail_observe.h"
#include "kui/hash.h"
#include "vmu_maple_observe_symbols.h"
const uint32_t kui_vmu_maple_profile=1u;
extern uint32_t kui_vmu_native_heap_original;
static struct kui_vmu_native_discovery native_driver;
static struct kui_vmu_native_heap native_heap;
static struct kui_vmu_native_heap_call native_call;
static uint8_t native_heap_code_sha[32];
extern const uint8_t __retail_stage_bss_end[] __asm__("__retail_stage_bss_end");
static bool temporary_overlap(uint32_t address,uint32_t bytes) {
    uint32_t p=address&0x1fffffffu,area=address&0xe0000000u;
    if((area!=0u && area!=0x80000000u && area!=0xa0000000u) ||
       !bytes || p<0x0c010000u || p>=0x0d000000u || bytes>0x0d000000u-p) return true;
    uint32_t stage_end=(uint32_t)(uintptr_t)__retail_stage_bss_end&0x1fffffffu;
    return (p<stage_end && 0x0ce00000u<p+bytes) || p+bytes>0x0cfe0000u;
}
static bool allocation_safe(uint32_t begin,uint32_t bytes) {
    if(bytes<0x30040u || temporary_overlap(begin,64u) ||
       temporary_overlap(native_heap.sentinel-4u,0x34u) ||
       temporary_overlap(native_heap.dispatch_table,8u)) return false;
    uint32_t p=begin&0x1fffffffu,area=begin&0xe0000000u;
    if(bytes>0x0d000000u-p || (area!=0u && area!=0x80000000u && area!=0xa0000000u)) return false;
    uint32_t header=begin+bytes-0x30020u;
    return !temporary_overlap(header,0x30020u) &&
        !(p<0x0c010000u+exec_bytes && 0x0c010000u<p+bytes);
}
static volatile struct kui_vmu_observe_boot_control *control(void) {
    return (volatile struct kui_vmu_observe_boot_control *)(uintptr_t)
        (KUI_VMU_OBSERVE_LOW_CONTROL+UINT32_C(0x20000000));
}
static uint32_t native_word(uint32_t address) {return *(volatile const uint32_t *)(uintptr_t)address;}
static void native_publish(uint32_t address) {
    __asm__ volatile("ocbp @%0"::"r"(address&~31u):"memory");
}
static void code_sha(uint8_t out[32]) {
    struct kui_sha256 h;kui_sha256_init(&h);
    kui_sha256_update(&h,(const void *)(uintptr_t)native_heap.initializer,0x1d0u);
    kui_sha256_digest(&h,out);
}
static bool native_read(void *context,uint32_t address,uint32_t *out) {
    (void)context;uint32_t area=address&0xe0000000u,p=address&0x1fffffffu;
    if((area!=0u && area!=0x80000000u && area!=0xa0000000u) ||
       (address&3u) || p<0x0c000000u || p>0x0cfffffcu) return false;
    *out=native_word(address);return true;
}
static void native_report(uint32_t phase,uint32_t detail) __attribute__((noreturn));
static void native_report(uint32_t phase,uint32_t detail) {
    volatile struct kui_vmu_observe_boot_control *c=control();
    c->error=detail;
    retail_display_restore(&display);
    retail_display_line("AUTOMATIC NATIVE ALLOCATOR PROBE");
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
    retail_display_line("PERSISTENT VMU PREPARATION STOPPED");
#else
    retail_display_line("NO VIRTUAL VMU / NO SAVE WRITES");
#endif
    retail_display_hex("PHASE",phase);retail_display_hex("DETAIL",detail);
    retail_display_hex("SDK SUBMIT",native_driver.submit);
    retail_display_hex("INITIALIZER",native_heap.initializer);
    retail_display_hex("INITIALIZER SLOT",native_heap.initializer_slot);
    retail_display_hex("ALLOCATE",native_heap.allocate);
    retail_display_hex("ENTRY HEAP BEGIN",c->heap_begin);
    retail_display_hex("ENTRY HEAP BYTES",c->heap_bytes);
    retail_display_hex("ALLOCATED PAYLOAD",c->lease);
    retail_display_hex("ORIGINAL RETURN",c->original_pr);
    stopped(phase==7u?"ALLOCATION VERIFIED - PHOTO THEN POWER OFF":"PROBE REFUSED - PHOTO THEN POWER OFF",phase);
}
#if KUI_VMU_NATIVE_WORKER_PROBE
#include "vmu_native_worker_stage.inc"
#elif KUI_VMU_NATIVE_PASSIVE_PROBE
#include "vmu_native_passive_stage.inc"
#endif
void kui_vmu_observe_stage_install(void) {
    volatile struct kui_vmu_observe_boot_control *c=control();uint8_t digest[32];
    uint32_t sr;__asm__ volatile("stc sr,%0":"=r"(sr));
    if(((sr>>4)&15u)!=15u || c->status!=KUI_VMU_OBS_BOOT_INITIALIZING)
        native_report(3u,sr);
    code_sha(digest);
    if(memcmp(digest,native_heap_code_sha,sizeof(digest))) native_report(3u,native_heap.initializer);
    uint32_t begin=(c->heap_begin+31u)&~31u,bytes=c->heap_bytes&~31u;
    struct kui_vmu_native_heap_lease lease;
    if(c->heap_begin!=native_call.entry_begin || c->heap_bytes!=native_call.entry_bytes ||
       !allocation_safe(begin,bytes) ||
       !kui_vmu_native_heap_fresh(&native_heap,native_read,NULL,begin,bytes,1u,0x30000u,&lease))
        native_report(4u,begin);
    uint32_t pr=c->original_pr&0x1fffffffu,sp=c->entry_sp&0x1fffffffu;
    if((c->original_pr&1u) || pr!=(native_call.return_pr&0x1fffffffu) ||
       (sp&3u) || sp<0x0c00c100u || sp>0x0c00f400u ||
       !kui_vmu_native_heap_preallocate(&lease,native_read,NULL,1u))
        native_report(5u,c->entry_sp);
    void *(*allocate)(uint32_t)=(void *(*)(uint32_t))(uintptr_t)native_heap.allocate;
    c->lease=(uint32_t)(uintptr_t)allocate(lease.lease_bytes);
    __asm__ volatile("stc sr,%0":"=r"(sr));
    code_sha(digest);
    if(((sr>>4)&15u)!=15u || memcmp(digest,native_heap_code_sha,sizeof(digest)) ||
       !kui_vmu_native_heap_postallocate(&lease,native_read,NULL,1u,c->lease) ||
       !kui_vmu_native_heap_lease_valid(&lease,native_read,NULL,1u))
        native_report(6u,c->lease);
    c->status=KUI_VMU_OBS_BOOT_LEASED;
#if KUI_VMU_NATIVE_WORKER_PROBE
    native_worker_run(&lease);
#elif KUI_VMU_NATIVE_PASSIVE_PROBE
    native_passive_run(&lease);
#else
    native_report(7u,lease.lease_bytes);
#endif
}
void kui_retail_stage_relay(const uint32_t *frame,uint32_t ccr) {
#if KUI_VMU_NATIVE_WORKER_PROBE || KUI_VMU_NATIVE_PASSIVE_PROBE
    retail_display_restore(&display);
    retail_display_line("BOOTSTRAP 2 RETURNED");
    retail_display_line("VERIFYING EXECUTABLE AND RESIDENT");
#endif
    kui_native_heap_base_relay(frame,ccr);
    if(manifest.storage_transport!=KUI_STORAGE_SCI || manifest.reader!=KUI_RETAIL_READER_STANDARD ||
       (manifest.flags&(KUI_RETAIL_IMAGE_CD|KUI_RETAIL_IMAGE_SCRAMBLED)) ||
       !kui_retail_observe_native_ip((const uint8_t *)(uintptr_t)KUI_RETAIL_IP_ADDRESS))
        native_report(1u,manifest.storage_transport);
#if KUI_VMU_NATIVE_CACHE_PROBE
    bool mapping_cached=native_resolution_replay();
    if(mapping_cached) retail_display_line("USING VERIFIED NATIVE MAPPING CACHE");
    else {
        retail_display_line("NO MATCHING NATIVE CACHE - SCANNING");
#endif
#if KUI_VMU_NATIVE_PASSIVE_PROBE
    retail_display_line("RESOLVING MAPLE DRIVER AND BUFFER LAYOUT");
    if(!kui_vmu_native_discover_layout((const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,
       exec_bytes,KUI_RETAIL_EXEC_ADDRESS,&native_driver,&passive_layout)) native_report(2u,10u);
    int driver=KUI_VMU_NATIVE_RESOLVED;
#else
#if KUI_VMU_NATIVE_WORKER_PROBE
    retail_display_line("SCANNING MAPLE DRIVER AND IMAGE HASH");
#endif
    int driver=kui_vmu_native_discover((const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,
        exec_bytes,KUI_RETAIL_EXEC_ADDRESS,&native_driver);
#endif
#if KUI_VMU_NATIVE_WORKER_PROBE || KUI_VMU_NATIVE_PASSIVE_PROBE
    retail_display_line("SCANNING ALLOCATOR FAMILY");
#endif
    int heap=kui_vmu_native_heap_discover((const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,
        exec_bytes,KUI_RETAIL_EXEC_ADDRESS,&native_heap);
    if(driver!=KUI_VMU_NATIVE_RESOLVED || heap!=KUI_VMU_NATIVE_HEAP_FOUND ||
       !native_heap.initializer_slot) native_report(2u,heap==KUI_VMU_NATIVE_HEAP_FOUND?0u:(uint32_t)heap);
    if(!kui_vmu_native_heap_call_discover((const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,
       exec_bytes,&native_heap,&native_call) || !allocation_safe(native_call.entry_begin,native_call.entry_bytes))
        native_report(2u,native_heap.initializer_slot);
#if KUI_VMU_NATIVE_PASSIVE_PROBE
    /* Layout already shares the driver's one fresh immutable-image pass. */
    retail_display_line("CHECKING DISPLACED INSTRUCTION WINDOW");
    native_passive_prepare();
#endif
#if KUI_VMU_NATIVE_CACHE_PROBE
    }
#endif
    code_sha(native_heap_code_sha);
    volatile struct kui_vmu_observe_boot_control *c=control();
    for(unsigned i=0;i<sizeof(*c)/4u;i++)
        if(((volatile uint32_t *)c)[i]) native_report(2u,KUI_VMU_OBSERVE_LOW_CONTROL+4u*i);
    volatile uint32_t *original=(volatile uint32_t *)(uintptr_t)
        (KUI_VMU_NATIVE_HEAP_ORIGINAL+UINT32_C(0x20000000));
    for(unsigned i=0;i<8u;i++) if(original[i]) native_report(2u,KUI_VMU_NATIVE_HEAP_ORIGINAL+4u*i);
    if(native_word(native_heap.initializer_slot)!=native_heap.initializer_target) native_report(2u,native_heap.initializer_slot);
    *original=native_heap.initializer_target;
    original[1]=native_call.entry_begin;original[2]=native_call.entry_bytes;
    original[3]=native_call.return_pr&0x1fffffffu;
    c->installer=(uint32_t)(uintptr_t)kui_vmu_observe_stage_install;
    c->stage_stack=KUI_RETAIL_STAGE_STACK-32u;c->status=KUI_VMU_OBS_BOOT_ARMED;
    *(volatile uint32_t *)(uintptr_t)native_heap.initializer_slot=KUI_VMU_OBSERVE_LOW_HEAP_HOOK;
    native_publish(native_heap.initializer_slot);
    if(native_word(native_heap.initializer_slot|UINT32_C(0x20000000))!=KUI_VMU_OBSERVE_LOW_HEAP_HOOK)
        native_report(2u,native_heap.initializer_slot);
    ((uint32_t *)(uintptr_t)frame)[5]=KUI_VMU_OBSERVE_LOW_RETURN_HOOK;
}
