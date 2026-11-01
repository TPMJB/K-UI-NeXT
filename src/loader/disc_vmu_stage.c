/* SPDX-License-Identifier: GPL-3.0-only */
/* Temporary physical-disc installer. All retained objects move into one
 * original-game allocator lease; firmware GD code and low RAM stay intact. */
#include "kui/disc_launch.h"
#include "kui/disc_vmu_layout.h"
#include "kui/disc_vmu_probe.h"
#include "kui/disc_vmu_support.h"
#include "kui/hash.h"
#include "kui/storage.h"
#include "kui/vmu_native_discovery.h"
#include "kui/vmu_native_background.h"
#include "kui/vmu_native_heap.h"
#include "kui/vmu_native_mode.h"
#include "kui/vmu_native_passive.h"
#include "kui/vmu_native_program.h"
#include "kui/vmu_native_stage_placement.h"
#include "kui/vmu_native_startup_stack.h"
#include "kui/vmu_maple_observe_boot.h"
#include "kui/vmu_persist_stage.h"
#include "kui/vmu_backing.h"
#include "../dreamcast/sci_sd_storage.h"
#include "retail_display.h"
#include "sci_sd_bus.h"
#include <string.h>

extern uint8_t __disc_vmu_stage_binary_end[] __asm__("__disc_vmu_stage_binary_end");
extern uint8_t __disc_vmu_stage_bss_end[] __asm__("__disc_vmu_stage_bss_end");
extern const uint8_t __disc_vmu_trampoline_start[] __asm__("__disc_vmu_trampoline_start");
extern const uint8_t __disc_vmu_trampoline_end[] __asm__("__disc_vmu_trampoline_end");
extern uint32_t kui_vmu_native_heap_original;
void kui_disc_vmu_stage_enter(void) __attribute__((noreturn));
void kui_vmu_observe_low_failure(void) __attribute__((noreturn));

static struct retail_display_state display;
static struct kui_disc_launch_plan plan;
static uint32_t exec_bytes,original_gd;
static uint8_t original_entry[128];
static struct kui_vmu_native_discovery native_driver;
static struct kui_disc_vmu_probe native_probe;
static struct kui_vmu_native_heap native_heap;
/* This installer admits original allocator leases only. The shared worker
 * installer also serves the image launcher's separately reviewed arena path. */
#define native_arena 0u
static struct kui_vmu_native_heap_call native_call;
static uint8_t native_heap_code_sha[32];
static uint8_t native_installer_stack[0x10000] __attribute__((aligned(32)));
static struct kui_vmu_persist_profile native_persist_profile;
static struct kui_vmu_persist_stage_workspace native_persist_work;
static struct kui_vmu_durable_handoff native_persist_handoff;
static struct kui_loader_sd native_persist_card;
static bool native_persist_ready;
static uint32_t native_heap_hook, native_terminal;
static uint32_t native_disc_entry_return_frame,native_disc_entry_return_entry;
#define NATIVE_LOW_HEAP_HOOK native_heap_hook
#define NATIVE_LOW_RETURN_HOOK native_terminal
#define KUI_VMU_OBSERVE_LOW_HEAP_HOOK native_heap_hook
#define KUI_VMU_OBSERVE_LOW_RETURN_HOOK native_terminal
#define KUI_VMU_OBSERVE_LOW_SD_LEASE 0u

static void stopped(const char *message,uint32_t detail) __attribute__((noreturn));
static void stopped(const char *message,uint32_t detail) {
    retail_display_restore(&display);
    retail_display_line("PHYSICAL DISC / VIRTUAL VMU TEST");
    retail_display_line(message);retail_display_hex("DETAIL",detail);
    retail_display_line("PHOTO THEN POWER OFF");
    for(;;) __asm__ volatile("nop");
}
static volatile struct kui_vmu_observe_boot_control *control(void) {
    return (void *)((uintptr_t)&kui_vmu_observe_boot_control+UINT32_C(0x20000000));
}
static uint32_t native_word(uint32_t address) {return *(volatile const uint32_t *)(uintptr_t)address;}
static void native_publish(uint32_t address) {
    __asm__ volatile("ocbp @%0"::"r"(address&~31u):"memory");
}
static bool native_read(void *context,uint32_t address,uint32_t *out) {
    (void)context;uint32_t area=address&UINT32_C(0xe0000000),p=address&UINT32_C(0x1fffffff);
    if((area!=0u && area!=UINT32_C(0x80000000) && area!=UINT32_C(0xa0000000)) ||
       (address&3u) || p<UINT32_C(0x0c000000) || p>UINT32_C(0x0cfffffc)) return false;
    *out=native_word(address);return true;
}
static void code_sha(uint8_t out[32]) {
    struct kui_sha256 h;kui_sha256_init(&h);
    kui_sha256_update(&h,(const void *)(uintptr_t)native_heap.initializer,0x1d0u);
    kui_sha256_digest(&h,out);
}
static bool allocation_safe(uint32_t begin,uint32_t bytes) {
    return kui_vmu_native_stage_allocation_safe(&native_heap,exec_bytes,
        KUI_DISC_LAUNCH_STAGE_ADDRESS,(uint32_t)(uintptr_t)__disc_vmu_stage_bss_end,begin,bytes);
}
static uint32_t native_runtime_mode(void) {return KUI_VMU_NATIVE_MODE_NORMAL;}
static void native_report(uint32_t phase,uint32_t detail) __attribute__((noreturn));
static void native_report(uint32_t phase,uint32_t detail) {
    retail_display_restore(&display);retail_display_line("PHYSICAL DISC VMU INSTALL REFUSED");
    retail_display_hex("PHASE",phase);retail_display_hex("DETAIL",detail);
    if(phase==2u && detail==10u) {
        /* The combined finder deliberately clears both authoritative outputs
         * on failure. Preserve a separately proved diagnostic candidate only
         * to identify the unsupported substage, never to authorize a hook. */
        retail_display_line(kui_disc_vmu_probe_stage_text(native_probe.stage));
        retail_display_hex("DRIVER RESULT",(uint32_t)native_probe.driver_result);
        retail_display_hex("PROBE SUBMIT",native_probe.driver.submit);
        retail_display_hex("PROBE INITIALIZER",native_probe.driver.initializer);
        retail_display_hex("BOOT BYTES",native_probe.file_bytes);
        retail_display_hex("PROGRAM BYTES",native_probe.program_bytes);
        retail_display_hex("FILE CRC",native_probe.file_crc32);
        retail_display_line("PHOTO THEN POWER OFF");
        for(;;) __asm__ volatile("nop");
    }
    if(phase==2u && detail>=0x70u && detail<=0x74u) {
        retail_display_hex("RETURN FRAME",native_disc_entry_return_frame);
        retail_display_hex("RETURN ENTRY",native_disc_entry_return_entry);
        retail_display_hex("RETURN TARGET",native_terminal);
    }
    retail_display_hex("SDK SUBMIT",native_driver.submit);
    retail_display_hex("HEAP INITIALIZER",native_heap.initializer);
    retail_display_hex("HEAP POINTER",native_heap.initializer_slot);
    retail_display_hex("ALLOCATED PAYLOAD",control()->lease);
    retail_display_line("PHOTO THEN POWER OFF");
    for(;;) __asm__ volatile("nop");
}

/* The independent support binary has no callbacks into this installer. */
__asm__(".section .rodata.disc_support,\"a\"\n.balign 32\n"
    ".global __disc_support_blob_begin\n__disc_support_blob_begin:\n.incbin \"" KUI_DISC_SUPPORT_BIN "\"\n"
    ".global __disc_support_blob_end\n__disc_support_blob_end:\n.balign 4\n"
    ".global __disc_support_reloc_begin\n__disc_support_reloc_begin:\n.incbin \"" KUI_DISC_SUPPORT_RELOC "\"\n"
    ".global __disc_support_reloc_end\n__disc_support_reloc_end:\n.previous\n");
extern const uint8_t __disc_support_blob_begin[] __asm__("__disc_support_blob_begin");
extern const uint8_t __disc_support_blob_end[] __asm__("__disc_support_blob_end");
extern const uint8_t __disc_support_reloc_begin[] __asm__("__disc_support_reloc_begin");
extern const uint8_t __disc_support_reloc_end[] __asm__("__disc_support_reloc_end");
static void native_disc_support_bind(const struct kui_vmu_native_passive_exports *,
    uint32_t,uint32_t,const struct kui_vmu_native_heap_lease *,struct kui_vmu_native_passive_config *);
#include "vmu_native_passive_stage.inc"
#include "disc_vmu_return_stage.inc"

static int persist_identity(void *context,struct kui_vmu_persist_session_identity *out) {
    struct kui_vmu_sd_identity id;
    if(kui_vmu_sd_identity_read(context,&id)!=KUI_VMU_BACKING_OK) return -1;
    memcpy(out->cid,id.cid,16u);out->card_sectors=id.sectors;out->high_capacity=id.high_capacity;return 0;
}
static bool persist_healthy(void *context) {
    struct kui_loader_sd *card=context;
    return card->ready && kui_sci_sd_healthy() && !kui_sci_sd_faulted();
}
static int persist_read(void *context,uint64_t lba,void *out) {
    return lba>UINT32_MAX || kui_loader_sd_read(context,(uint32_t)lba,1u,out)!=KUI_LOADER_SD_OK?-1:0;
}
static int persist_write(void *context,uint64_t lba,const void *in) {
    struct kui_loader_sd *card=context;
    if(lba>UINT32_MAX) return -1;
    bool (*block)(void *,const uint8_t *,uint8_t *,size_t,bool,uint16_t *)=card->bus.transfer_block;
    card->bus.transfer_block=NULL;
    enum kui_loader_sd_result result=kui_sci_sd_write(card,(uint32_t)lba,1u,in);
    card->bus.transfer_block=block;return result==KUI_LOADER_SD_OK?0:-1;
}
static int persist_sync(void *context) {return kui_sci_sd_sync(context)==KUI_LOADER_SD_OK?0:-1;}
static void native_persist_prepare(const uint8_t *wire) {
    if(plan.vmu_transport!=KUI_STORAGE_SCI || plan.vmu_profile_bytes!=KUI_VMU_PERSIST_PROFILE_WIRE_BYTES ||
       kui_vmu_persist_profile_decode(&native_persist_profile,wire,plan.vmu_profile_bytes) ||
       kui_vmu_persist_disc_stage_admit(&native_persist_profile,plan.vmu_transport))
        stopped("SELECTED VMU PROFILE REFUSED",1u);
    enum kui_loader_sd_result result=kui_sci_sd_acquire();
    if(result!=KUI_LOADER_SD_OK) stopped("SCI SAVE PORT UNAVAILABLE",(uint32_t)result);
    result=kui_loader_sd_init_bus(&native_persist_card,kui_sci_sd_bus());
    bool healthy=result==KUI_LOADER_SD_OK && persist_healthy(&native_persist_card);
    kui_sci_sd_release();
    if(!healthy || kui_sci_sd_faulted()) stopped("SCI CARD INITIALIZATION REFUSED",(uint32_t)result);
    result=kui_sci_sd_acquire();
    if(result!=KUI_LOADER_SD_OK) stopped("SCI SAVE PREPARATION PORT REFUSED",(uint32_t)result);
    retail_display_line("CHECKING SELECTED VMU OWNERSHIP / JOURNAL");
    const struct kui_vmu_persist_session_ops ops={&native_persist_card,persist_identity,
        persist_read,persist_write,persist_sync,persist_healthy};
    int prepared=kui_vmu_persist_disc_stage_prepare(&native_persist_profile,plan.vmu_transport,
        &ops,&native_persist_work,&native_persist_handoff);
    healthy=persist_healthy(&native_persist_card);kui_sci_sd_release();
    if(prepared || !healthy || kui_sci_sd_faulted()) {
        retail_display_restore(&display);
        retail_display_line("PHYSICAL DISC VMU PREPARATION REFUSED");
        retail_display_hex("PREPARE",(uint32_t)prepared);
        retail_display_hex("PHASE",native_persist_work.phase);
        retail_display_hex("VMU SESSION",(uint32_t)native_persist_work.session.last_result);
        retail_display_hex("VMU FILESYSTEM",(uint32_t)native_persist_work.filesystem.stats.result);
        retail_display_hex("FILESYSTEM DETAIL",native_persist_work.filesystem.stats.detail);
        retail_display_hex("PORT HEALTHY",healthy && !kui_sci_sd_faulted());
        retail_display_line("PHOTO THEN POWER OFF / JOURNAL RECOVERY MAY BE NEEDED");
        for(;;) __asm__ volatile("nop");
    }
    native_persist_ready=true;
}

/* Typed support exports and the exact durable-context offset are generated
 * from the two linked ELFs, never inferred from a runtime caller. */
#include "disc_vmu_worker_symbols.h"
static void native_disc_support_bind(const struct kui_vmu_native_passive_exports *worker,
    uint32_t dest,uint32_t worker_bytes,const struct kui_vmu_native_heap_lease *lease,
    struct kui_vmu_native_passive_config *cfg) {
    uint32_t load=(lease->payload&UINT32_C(0x1fffffff))|UINT32_C(0x80000000);
    uint32_t support_load=load+KUI_DISC_VMU_SUPPORT_OFFSET,support_dest=support_load+UINT32_C(0x20000000);
    uint32_t bytes=(uint32_t)(__disc_support_blob_end-__disc_support_blob_begin);
    uint32_t reloc_bytes=(uint32_t)(__disc_support_reloc_end-__disc_support_reloc_begin);
    if(lease->lease_bytes!=KUI_DISC_VMU_LEASE_BYTES || bytes<64u || bytes>=KUI_DISC_VMU_SUPPORT_BYTES ||
       KUI_DISC_VMU_DURABLE_CONTEXT_OFFSET<worker_bytes ||
       KUI_DISC_VMU_DURABLE_CONTEXT_OFFSET>=KUI_DISC_VMU_WORKER_BYTES-0x1000u)
        native_report(8u,0x60u);
    for(uint32_t at=support_load;at<support_load+KUI_DISC_VMU_SUPPORT_BYTES;at+=32u)
        __asm__ volatile("ocbp @%0"::"r"(at):"memory");
    int relocated=kui_vmu_native_relocate(__disc_support_blob_begin,bytes,
        __disc_support_reloc_begin,reloc_bytes,(void *)(uintptr_t)support_dest,KUI_DISC_VMU_SUPPORT_BYTES,support_load);
    if(relocated) native_report(8u,0x61u);
    memset((void *)(uintptr_t)(support_dest+bytes),0,KUI_DISC_VMU_SUPPORT_BYTES-bytes);
    const struct kui_disc_vmu_support_exports *e=(const void *)(uintptr_t)support_dest;
    /* The wrapper's public ABI and all final export ranges are rechecked
     * before its first instruction can bind the card. */
    if(e->magic!=KUI_DISC_VMU_SUPPORT_MAGIC || e->version!=KUI_DISC_VMU_SUPPORT_VERSION ||
       e->bytes!=sizeof(*e) ||
       !kui_disc_vmu_support_entry_owned(lease->payload,e->bind) ||
       !kui_disc_vmu_support_entry_owned(lease->payload,e->sd_lease) ||
       !kui_disc_vmu_support_entry_owned(lease->payload,e->terminal) ||
       e->bind>=support_load+bytes || e->sd_lease>=support_load+bytes || e->terminal>=support_load+bytes ||
       e->bss_begin!=support_dest+bytes || e->bss_end<e->bss_begin ||
       e->bss_end>support_dest+KUI_DISC_VMU_SUPPORT_BYTES ||
       e->memory_end!=support_dest+KUI_DISC_VMU_SUPPORT_BYTES) native_report(8u,0x62u);
    for(unsigned i=0;i<7u;i++) if(e->reserved[i]) native_report(8u,0x62u);
    kui_native_passive_publish(support_load,support_load+bytes);
    struct kui_disc_vmu_support_request request={0};
    request.version=1u;request.bytes=sizeof(request);request.transport=plan.vmu_transport;
    request.worker_begin=load;request.lease_begin=lease->payload;request.lease_bytes=lease->lease_bytes;
    request.worker_transaction=worker->sd_transaction;
    request.worker_context=dest+KUI_DISC_VMU_DURABLE_CONTEXT_OFFSET;
    request.worker_terminal=worker->terminal;request.worker_stack_top=worker->stack_top;
    memcpy(request.cid,native_persist_handoff.cid,sizeof(request.cid));
    request.card=native_persist_card;
    int (*bind)(const struct kui_disc_vmu_support_request *)=(void *)(uintptr_t)e->bind;
    if(bind(&request)!=0) native_report(8u,0x63u);
    if(native_word(native_heap.initializer_slot)!=(uint32_t)(uintptr_t)kui_vmu_observe_heap_hook)
        native_report(8u,0x64u);
    native_heap_hook=e->terminal;native_terminal=e->terminal;
    *(volatile uint32_t *)(uintptr_t)native_heap.initializer_slot=native_heap_hook;
    native_publish(native_heap.initializer_slot);
    if(native_word(native_heap.initializer_slot|UINT32_C(0x20000000))!=native_heap_hook)
        native_report(8u,0x65u);
    cfg->sd_lease=e->sd_lease;cfg->low_terminal=e->terminal;cfg->low_heap_hook=e->terminal;
}

void kui_vmu_observe_stage_install(void) {
    volatile struct kui_vmu_observe_boot_control *c=control();uint8_t digest[32];
    uint32_t sr;__asm__ volatile("stc sr,%0":"=r"(sr));
    if((sr&UINT32_C(0xf0))!=UINT32_C(0xf0) || c->status!=KUI_VMU_OBS_BOOT_INITIALIZING)
        native_report(3u,sr);
    code_sha(digest);
    if(memcmp(digest,native_heap_code_sha,sizeof(digest))) native_report(3u,native_heap.initializer);
    uint32_t begin=(c->heap_begin+31u)&~31u,bytes=c->heap_bytes&~31u;
    struct kui_vmu_native_heap_lease lease;
    if(c->heap_begin!=native_call.entry_begin || c->heap_bytes!=native_call.entry_bytes ||
       !allocation_safe(begin,bytes) ||
       !kui_vmu_native_heap_fresh(&native_heap,native_read,NULL,begin,bytes,1u,KUI_DISC_VMU_LEASE_BYTES,&lease))
        native_report(4u,begin);
    uint32_t pr=c->original_pr&UINT32_C(0x1fffffff),sp=c->entry_sp&UINT32_C(0x1fffffff);
    if((c->original_pr&1u) || pr!=(native_call.return_pr&UINT32_C(0x1fffffff)) ||
       (sp&3u) || sp<KUI_VMU_NATIVE_OWNER_STACK_LOW || sp>KUI_VMU_NATIVE_OWNER_STACK_HIGH ||
       !kui_vmu_native_heap_preallocate(&lease,native_read,NULL,1u)) native_report(5u,c->entry_sp);
    void *(*allocate)(uint32_t)=(void *(*)(uint32_t))(uintptr_t)native_heap.allocate;
    c->lease=(uint32_t)(uintptr_t)allocate(lease.lease_bytes);
    __asm__ volatile("stc sr,%0":"=r"(sr));code_sha(digest);
    if((sr&UINT32_C(0xf0))!=UINT32_C(0xf0) || memcmp(digest,native_heap_code_sha,sizeof(digest)) ||
       !kui_vmu_native_heap_postallocate(&lease,native_read,NULL,1u,c->lease) ||
       !kui_vmu_native_heap_lease_valid(&lease,native_read,NULL,1u)) native_report(6u,c->lease);
    c->status=KUI_VMU_OBS_BOOT_LEASED;native_passive_run(&lease);
    if(native_word(UINT32_C(0xac0000bc))!=original_gd) native_report(9u,0x66u);
}

static bool firmware_target(uint32_t pointer) {
    uint32_t area=pointer&UINT32_C(0xe0000000),physical=pointer&UINT32_C(0x1fffffff);
    return !(pointer&1u) && (area==0u || area==UINT32_C(0x80000000) || area==UINT32_C(0xa0000000)) &&
        ((physical>=UINT32_C(0x100) && physical<UINT32_C(0x200000)) ||
         (physical>=UINT32_C(0x0c000100) && physical<UINT32_C(0x0c008000)));
}
void kui_disc_vmu_stage_main(void) {
    retail_display_capture(&display);retail_display_restore(&display);
    retail_display_line("PHYSICAL DISC / VIRTUAL A2 / SCI");
    const uint8_t *payload=(const void *)(uintptr_t)KUI_DISC_LAUNCH_LOAD_ADDRESS;
    if(!kui_disc_launch_decode(payload+KUI_DISC_LAUNCH_CONFIG_OFFSET,&plan)) stopped("INVALID DISC VMU PACKET",1u);
    uint32_t actual=(uint32_t)(uintptr_t)__disc_vmu_stage_binary_end-KUI_DISC_LAUNCH_STAGE_ADDRESS;
    if(plan.flags!=KUI_DISC_LAUNCH_VMU_FLAGS || plan.module_bytes!=KUI_DISC_LAUNCH_STAGE_OFFSET+actual ||
       !kui_disc_launch_validate(payload,plan.payload_bytes,&plan)) stopped("DISC VMU HASH OR LAYOUT REFUSED",2u);
    original_gd=native_word(UINT32_C(0xac0000bc));
    if(!firmware_target(original_gd)) stopped("ORIGINAL GD VECTOR REFUSED",original_gd);
    native_persist_prepare(payload+plan.vmu_profile_offset);
    uint32_t low_crc=kui_crc32(0u,(const void *)(uintptr_t)UINT32_C(0x8c000000),0x8000u);
    memmove((void *)(uintptr_t)KUI_DISC_LAUNCH_IP_ADDRESS,payload+plan.ip_offset,KUI_DISC_LAUNCH_IP_BYTES);
    memmove((void *)(uintptr_t)KUI_DISC_LAUNCH_EXEC_ADDRESS,payload+plan.exec_offset,plan.exec_bytes);
    if(native_word(UINT32_C(0xac0000bc))!=original_gd ||
       kui_crc32(0u,(const void *)(uintptr_t)UINT32_C(0x8c000000),0x8000u)!=low_crc)
        stopped("LOW FIRMWARE RAM CHANGED",3u);
    exec_bytes=plan.exec_bytes;
    memcpy(original_entry,(const void *)(uintptr_t)KUI_DISC_LAUNCH_EXEC_ADDRESS,sizeof(original_entry));
    if(__disc_vmu_trampoline_end-__disc_vmu_trampoline_start!=(ptrdiff_t)sizeof(original_entry))
        stopped("DISC ENTRY TRAMPOLINE REFUSED",4u);
    memcpy((void *)(uintptr_t)KUI_DISC_LAUNCH_EXEC_ADDRESS,__disc_vmu_trampoline_start,sizeof(original_entry));
    ((uint8_t *)(uintptr_t)KUI_DISC_LAUNCH_IP_ADDRESS)[0xfcu]&=(uint8_t)~0x20u;
    retail_display_line("ORIGINAL GD FIRMWARE RETAINED");
    retail_display_line("ENTERING DISC BOOTSTRAP 2");
    kui_disc_vmu_stage_enter();
}
void kui_disc_vmu_stage_relay(const uint32_t *frame,uint32_t ccr) {
    (void)ccr;uint32_t sr;__asm__ volatile("stc sr,%0":"=r"(sr));
    uint32_t f=(uint32_t)(uintptr_t)frame,fp=f&UINT32_C(0x1fffffff);
    if((sr&UINT32_C(0xf0))!=UINT32_C(0xf0) || (f&3u) ||
       fp<UINT32_C(0x0c008100) || fp>UINT32_C(0x0c00f400)-84u ||
       native_word(UINT32_C(0xac0000bc))!=original_gd ||
       memcmp((const void *)(uintptr_t)KUI_DISC_LAUNCH_EXEC_ADDRESS,__disc_vmu_trampoline_start,sizeof(original_entry)))
        stopped("DISC BOOTSTRAP RELAY REFUSED",5u);
    memcpy((void *)(uintptr_t)KUI_DISC_LAUNCH_EXEC_ADDRESS,original_entry,sizeof(original_entry));
    if(kui_crc32(0u,(const void *)(uintptr_t)KUI_DISC_LAUNCH_EXEC_ADDRESS,exec_bytes)!=plan.exec_crc32)
        stopped("BOOTSTRAP CHANGED EXECUTABLE CRC",6u);
    uint8_t digest[32];struct kui_sha256 hash;kui_sha256_init(&hash);
    kui_sha256_update(&hash,(const void *)(uintptr_t)KUI_DISC_LAUNCH_EXEC_ADDRESS,exec_bytes);
    kui_sha256_digest(&hash,digest);
    if(memcmp(digest,plan.exec_sha256,sizeof(digest))) stopped("BOOTSTRAP CHANGED EXECUTABLE SHA",7u);
    size_t program_bytes;
    if(!kui_vmu_native_program_extent((const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,
       exec_bytes,KUI_RETAIL_EXEC_ADDRESS,&program_bytes)) native_report(2u,13u);
    exec_bytes=(uint32_t)program_bytes;
    retail_display_restore(&display);retail_display_line("RESOLVING DISC MAPLE DRIVER AND ALLOCATOR");
    if(!kui_vmu_native_discover_layout((const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,
       exec_bytes,KUI_RETAIL_EXEC_ADDRESS,&native_driver,&passive_layout)) {
        (void)kui_disc_vmu_probe_run((const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,
            plan.exec_bytes,KUI_RETAIL_EXEC_ADDRESS,&native_probe);
        native_report(2u,10u);
    }
    if(kui_vmu_native_heap_discover((const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,
       exec_bytes,KUI_RETAIL_EXEC_ADDRESS,&native_heap)!=KUI_VMU_NATIVE_HEAP_FOUND ||
       !native_heap.initializer_slot ||
       !kui_vmu_native_heap_call_discover((const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,
        exec_bytes,&native_heap,&native_call) || !allocation_safe(native_call.entry_begin,native_call.entry_bytes))
        native_report(2u,native_heap.initializer_slot);
    native_heap_hook=(uint32_t)(uintptr_t)kui_vmu_observe_heap_hook;
    native_terminal=(uint32_t)(uintptr_t)kui_vmu_observe_return_hook;
    native_passive_prepare();
    if(!native_crt_bootstrap_prearm()) native_report(2u,0x67u);
    if(!native_disc_entry_return_bind((uint32_t *)(uintptr_t)f)) {
        native_report(2u,native_disc_entry_return_error);
    }
    code_sha(native_heap_code_sha);
    volatile struct kui_vmu_observe_boot_control *c=control();
    for(unsigned i=0;i<sizeof(*c)/4u;i++) if(((volatile uint32_t *)c)[i]) native_report(2u,0x68u);
    volatile uint32_t *original=(void *)((uintptr_t)&kui_vmu_native_heap_original+UINT32_C(0x20000000));
    for(unsigned i=0;i<8u;i++) if(original[i]) native_report(2u,0x69u);
    if(native_word(native_heap.initializer_slot)!=native_heap.initializer_target) native_report(2u,0x6au);
    *original=native_heap.initializer_target;original[1]=native_call.entry_begin;
    original[2]=native_call.entry_bytes;original[3]=native_call.return_pr&UINT32_C(0x1fffffff);
    c->installer=(uint32_t)(uintptr_t)kui_vmu_observe_stage_install;
    c->stage_stack=(uint32_t)(uintptr_t)(native_installer_stack+sizeof(native_installer_stack)-32u);
    c->status=KUI_VMU_OBS_BOOT_ARMED;
    *(volatile uint32_t *)(uintptr_t)native_heap.initializer_slot=native_heap_hook;
    native_publish(native_heap.initializer_slot);
    if(native_word(native_heap.initializer_slot|UINT32_C(0x20000000))!=native_heap_hook) native_report(2u,0x6bu);
    /* PR now names the exact game-owned SDK exit veneer. The existing worker
     * installer replaces its temporary target with the owned support terminal
     * before the original allocator returns to gameplay. */
}
void kui_disc_vmu_stage_returned(void) {stopped("DISC BOOTSTRAP RETURNED",8u);}
void kui_vmu_observe_low_failure(void) {native_report(11u,control()->error);}
