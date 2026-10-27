/* SPDX-License-Identifier: GPL-3.0-only */
/* Passive one-pair proof, or a separately enabled bounded read-only bridge. */
#include "kui/vmu_native_passive.h"
#include "kui/vmu_native_background.h"
#include "retail_display.h"
#include <string.h>
#if KUI_VMU_NATIVE_READONLY_PROBE || KUI_VMU_NATIVE_PERSISTENT_PROBE
#include "kui/vmu_readonly.h"
#include "kui/vmu_readonly_overlay.h"
#include "kui/vmu_native_lifetime.h"
#define READONLY_PAIR_LIMIT 3600u
extern uint32_t kui_vmu_native_readonly_release_resume;
#endif
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
#include "kui/vmu_durable.h"
#include "kui/vmu_native_live.h"
#include "kui/vmu_native_terminal_dispatch.h"
#include "kui/vmu_native_patch.h"
#include "kui/vmu_native_profile.h"
#include "kui/vmu_volatile_chain.h"
#include "kui/vmu_native_trace.h"
#include "vmu_native_fault_capture.h"
#include "vmu_native_sdk_coherence.h"
#include "sd_reader.h"
__asm__(".global __kui_native_controller_failure_witness_worker\n"
        ".set __kui_native_controller_failure_witness_worker,1\n");
__asm__(".global __kui_native_terminal_dispatch_worker\n"
        ".set __kui_native_terminal_dispatch_worker,1\n");
__asm__(".global __kui_native_owned_timer_callbacks_worker\n"
        ".set __kui_native_owned_timer_callbacks_worker,1\n");
#if KUI_NATIVE_CURRENT_BOTTOM_HEAP
__asm__(".global __kui_native_bottom_heap_worker\n"
        ".set __kui_native_bottom_heap_worker,1\n");
#endif
#if KUI_NATIVE_STANDARD_VMU_ISOLATION
__asm__(".global __kui_native_standard_vmu_isolation_worker\n"
        ".set __kui_native_standard_vmu_isolation_worker,1\n");
#endif
#if KUI_NATIVE_STANDARD_VMU_ISOLATION || KUI_NATIVE_READER_ONLY_ISOLATION || KUI_NATIVE_METADATA_CACHE_ISOLATION || KUI_NATIVE_SPLIT_CONTROLLER_ISOLATION
_Static_assert(KUI_VMU_NATIVE_BACKGROUND_LEASE_BYTES==UINT32_C(0x60000),
    "Native isolation variants retain the complete current lease");
__asm__(".global __kui_native_isolation_lease_bytes\n"
        ".set __kui_native_isolation_lease_bytes,0x60000\n");
#endif
#if KUI_NATIVE_READER_ONLY_ISOLATION
__asm__(".global __kui_native_reader_only_isolation_worker\n"
        ".set __kui_native_reader_only_isolation_worker,1\n");
#endif
#if KUI_NATIVE_RA_TICK_OFF_ISOLATION
__asm__(".global __kui_native_ra_tick_off_isolation_worker\n"
        ".set __kui_native_ra_tick_off_isolation_worker,1\n");
#endif
#if KUI_NATIVE_E1_WAIT_CAPTURE
__asm__(".global __kui_native_e1_wait_capture_worker\n"
        ".set __kui_native_e1_wait_capture_worker,1\n");
#endif
#if KUI_NATIVE_MAPLE_BYPASS_ISOLATION
__asm__(".global __kui_native_maple_bypass_isolation_worker\n"
        ".set __kui_native_maple_bypass_isolation_worker,1\n");
#endif
#if KUI_NATIVE_MAPLE_DELAY_ISOLATION
__asm__(".global __kui_native_maple_delay_isolation_worker\n"
        ".set __kui_native_maple_delay_isolation_worker,1\n");
#endif
#if KUI_NATIVE_MAPLE_DELAY_COMPLETE_ONLY_ISOLATION
__asm__(".global __kui_native_maple_delay_complete_only_isolation_worker\n"
        ".set __kui_native_maple_delay_complete_only_isolation_worker,1\n");
#endif
#if KUI_NATIVE_MAPLE_DELAY_SUBMIT_ONLY_ISOLATION
__asm__(".global __kui_native_maple_delay_submit_only_isolation_worker\n"
        ".set __kui_native_maple_delay_submit_only_isolation_worker,1\n");
#endif
#if KUI_NATIVE_MAPLE_DELAY_POST_START_ISOLATION
__asm__(".global __kui_native_maple_delay_post_start_isolation_worker\n"
        ".set __kui_native_maple_delay_post_start_isolation_worker,1\n");
#endif
#if KUI_NATIVE_SPLIT_CONTROLLER_ISOLATION
__asm__(".global __kui_native_split_controller_isolation_worker\n"
        ".set __kui_native_split_controller_isolation_worker,1\n");
#endif
#if KUI_NATIVE_METADATA_CACHE_ISOLATION
__asm__(".global __kui_native_metadata_cache_isolation_worker\n"
        ".set __kui_native_metadata_cache_isolation_worker,1\n");
#define NATIVE_METADATA_OBJECT(name) __attribute__((section(".bss.native_metadata." #name),aligned(32)))
#else
#define NATIVE_METADATA_OBJECT(name)
#endif
#define WORKER_SPAN UINT32_C(0x20000)
#define PAIR_LIMIT 180000u
#else
#define WORKER_SPAN UINT32_C(0x10000)
#define PAIR_LIMIT READONLY_PAIR_LIMIT
#endif
#ifndef NATIVE_METADATA_OBJECT
#define NATIVE_METADATA_OBJECT(name)
#endif
#if KUI_NATIVE_METADATA_CACHE_ISOLATION
#define NATIVE_HOT_CONFIG (*cfg)
#define NATIVE_HOT_LIVE (*metadata)
#else
#define NATIVE_HOT_CONFIG config
#define NATIVE_HOT_LIVE live_plan
#endif
#define GUARD UINT32_C(0x4b535431)
#define TX_WORDS (KUI_VMU_NATIVE_TX_BANK_BYTES/4u)
extern const struct kui_vmu_native_passive_exports kui_vmu_native_worker_exports;
extern uint32_t kui_vmu_native_passive_resume,kui_vmu_native_passive_consumer;
extern uint32_t stack_bottom[] __asm__("__vmu_native_worker_stack_bottom");
extern uint32_t stack_top[] __asm__("__vmu_native_worker_stack_top");
static struct kui_vmu_native_passive_config config NATIVE_METADATA_OBJECT(config);
static struct {
    uint32_t armed,pending,submitted,completed,error,source,bytes,descriptors,nops;
    uint32_t submit_sr,complete_sr,rx_slots,hash,first_rx,first_response,last_response;
    uint32_t fault_at,fault_value,fault_expect,runtime_result;
#if KUI_VMU_NATIVE_READONLY_PROBE || KUI_VMU_NATIVE_PERSISTENT_PROBE
    uint32_t proof,overlay_result;
#endif
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
    uint32_t frame_address,frame_pr,frame_sr;
#endif
} state;
static uint32_t captured[TX_WORDS];
static uint32_t receivers[48];
#if KUI_VMU_NATIVE_READONLY_PROBE || KUI_VMU_NATIVE_PERSISTENT_PROBE
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
static struct kui_vmu_durable durable_card;
static _Alignas(32) uint8_t issued_chain[KUI_VMU_VOLATILE_CHAIN_PASSTHROUGH_BYTES];
static struct kui_vmu_volatile_chain_frame chain_frames[KUI_VMU_VOLATILE_CHAIN_FRAMES];
static uint32_t write_payloads[KUI_VMU_VOLATILE_CHAIN_FRAMES][32];
static struct kui_vmu_volatile_chain_plan issued_plan;
static struct kui_vmu_native_passive_frame completion_frame;
static struct kui_vmu_native_live live_plan NATIVE_METADATA_OBJECT(live);
/* A controller/main-unit-query pair keeps the original SDK DMA chain. This private
 * snapshot grants only one parent-presence byte, never a virtual reply or
 * storage operation. A bounded miss uses the unchanged complete VMU path. */
#define CONTROLLER_WORDS 32u
#define CONTROLLER_FRAMES 4u
static struct {
    uint32_t pending,bank,source,words,count,nops,parent;
    uint32_t snapshot[CONTROLLER_WORDS];
    struct {uint32_t rx,header;} frames[CONTROLLER_FRAMES];
} controller_pair;
#if KUI_NATIVE_SPLIT_CONTROLLER_ISOLATION
enum native_controller_pending {
    NATIVE_CONTROLLER_IDLE=0u,NATIVE_CONTROLLER_PREPARED=1u,NATIVE_CONTROLLER_CAPTURED=2u
};
#endif
static struct kui_vmu_native_lifetime_range protected_ranges[KUI_VMU_NATIVE_LIFETIME_RANGES];
static unsigned protected_count;
static struct kui_vmu_native_lifetime_plan lifetime_plan NATIVE_METADATA_OBJECT(lifetime);
#define HOOK_WITNESSES 32u
#if KUI_NATIVE_METADATA_CACHE_ISOLATION
struct native_hook_witness {uint32_t address,value,mask;};
static struct native_hook_witness hook_witness[HOOK_WITNESSES] NATIVE_METADATA_OBJECT(hooks);
#else
static struct {uint32_t address,value,mask;} hook_witness[HOOK_WITNESSES];
#endif
static unsigned hook_witness_count;
static uint32_t hook_witness_ready;
#include "vmu_native_metadata_cache.inc"
static struct {
    struct kui_vmu_native_profile timing;
    uint32_t clock_started,clock_first,clock_last,clock_samples,clock_invalid;
    uint32_t ccr_first,ccr_last,ice_off,gesture_count,active,capture_poll_start;
    uint32_t fast_submit,full_submit,fast_complete,full_complete;
} diagnostic;
#include "vmu_native_phase.inc"
static struct native_phase_state diagnostic_phase;
#if KUI_NATIVE_READER_DIAGNOSTIC
#include "vmu_native_reader_capture.inc"
#endif
#if KUI_NATIVE_LOADER_TIMELINE
#include "vmu_native_loader_timeline.inc"
#endif
static bool persistent_storage_guard(void);
static bool persistent_context_guard(void);
static bool installed_hooks_valid(void);
#if KUI_NATIVE_SPLIT_CONTROLLER_ISOLATION
static bool split_controller_geometry(void);
#endif
#ifdef KUI_VMU_NATIVE_PASSIVE_HOST_TEST
static struct {unsigned heap,layout,runtime,chain,hooks,storage;} measured_work;
#define RECORD_WORK(field) (++measured_work.field)
#else
#define RECORD_WORK(field) ((void)0)
#endif
static int persistent_handler(void *context,const uint8_t *request,size_t request_bytes,
    uint8_t *response,size_t capacity,size_t *response_bytes) {
    return kui_vmu_durable_handle(context,request,request_bytes,response,capacity,response_bytes);
}
static int persistent_admit(void *context,uint32_t header,uint32_t payload0,uint32_t payload1) {
    return kui_vmu_durable_admit(context,header,payload0,payload1);
}
#else
static struct kui_vmu_readonly card;
static int readonly_handler(void *context,const uint8_t *request,size_t request_bytes,
    uint8_t *response,size_t capacity,size_t *response_bytes) {
    return kui_vmu_readonly_handle(context,request,request_bytes,response,capacity,response_bytes);
}
#endif
static struct kui_vmu_readonly_overlay overlay;
static struct kui_vmu_readonly_overlay_frame overlay_frames[KUI_VMU_READONLY_OVERLAY_FRAMES];
#endif
#ifndef RECORD_WORK
#define RECORD_WORK(field) ((void)0)
#endif
#ifdef KUI_VMU_NATIVE_PASSIVE_HOST_TEST
extern bool kui_vmu_native_passive_test_read(uint32_t,unsigned,uint32_t *);
extern uint32_t kui_vmu_native_passive_test_sr(void);
extern uint32_t kui_vmu_native_passive_test_sp(void);
extern uint32_t kui_vmu_native_passive_test_guard(uint32_t,unsigned);
extern void kui_vmu_native_passive_test_terminal(uint32_t,uint32_t) __attribute__((noreturn));
#ifndef KUI_VMU_NATIVE_TEST_SELF_ADDRESS
#define KUI_VMU_NATIVE_TEST_SELF_ADDRESS UINT32_C(0xac700000)
#endif
#define SELF_ADDRESS KUI_VMU_NATIVE_TEST_SELF_ADDRESS
#define STACK_LOW (SELF_ADDRESS+WORKER_SPAN-UINT32_C(0x1000))
#define STACK_HIGH (SELF_ADDRESS+WORKER_SPAN)
static uint32_t current_sr(void) {return kui_vmu_native_passive_test_sr();}
static uint32_t current_sp(void) {return kui_vmu_native_passive_test_sp();}
#if KUI_VMU_NATIVE_READONLY_PROBE || KUI_VMU_NATIVE_PERSISTENT_PROBE
extern uint8_t *kui_vmu_native_passive_test_rx(uint32_t,unsigned);
#endif
#else
#define SELF_ADDRESS ((uint32_t)(uintptr_t)&kui_vmu_native_worker_exports)
#define STACK_LOW ((uint32_t)(uintptr_t)stack_bottom)
#define STACK_HIGH ((uint32_t)(uintptr_t)stack_top)
static uint32_t current_sr(void) {uint32_t r;__asm__ volatile("stc sr,%0":"=r"(r));return r;}
static uint32_t current_sp(void) {uint32_t r;__asm__ volatile("mov r15,%0":"=r"(r));return r;}
#endif
/* Cold arm runs on the stage-admitted P2 stack. After arm, every runtime
 * gateway uses its P1 alias; private C frames are dead before game code can
 * change cache mode. Persistent worker state and DMA buffers stay in P2. */
static uint32_t active_stack_low(void) {
#if KUI_VMU_NATIVE_CACHED_CODE
    return STACK_LOW-(state.armed?UINT32_C(0x20000000):0u);
#else
    return STACK_LOW;
#endif
}
/* Call only after admitting SP against its exact active-alias bounds. Canary
 * loads follow that same alias, so a dirty cached overflow cannot be hidden
 * by sampling the old P2 backing bytes. The bottom256B remain a guard border. */
static uint32_t guard(uint32_t sp,unsigned i) {
    uint32_t address=(STACK_LOW&UINT32_C(0x1fffffff))|(sp&UINT32_C(0xe0000000));
    address+=i*4u;
#ifdef KUI_VMU_NATIVE_PASSIVE_HOST_TEST
    return kui_vmu_native_passive_test_guard(address,i);
#else
    return *(const uint32_t *)(uintptr_t)address;
#endif
}
static uint32_t physical(uint32_t a) {return a&UINT32_C(0x1fffffff);}
static inline __attribute__((always_inline)) bool ram(uint32_t a,unsigned bytes,unsigned alignment) {
    uint32_t area=a&UINT32_C(0xe0000000),p=physical(a);
    return (area==0u || area==UINT32_C(0x80000000) || area==UINT32_C(0xa0000000)) &&
        !(p&(alignment-1u)) && bytes && p>=UINT32_C(0x0c000000) &&
        p<UINT32_C(0x0d000000) && bytes<=UINT32_C(0x0d000000)-p;
}
/* Keep the exact address/width admission at every call site. Inlining lets
 * SH GCC fold literal widths/MMIO addresses without rebuilding a private
 * stack frame for each individual SDK word. Indirect callbacks retain the
 * same independently callable checked reader. */
static inline __attribute__((always_inline)) bool read_value(void *unused,uint32_t a,unsigned width,uint32_t *v) {
    (void)unused;
    if(width!=1u && width!=2u && width!=4u) return false;
    if(!ram(a,width,width) && !(width==4u &&
       (a==UINT32_C(0xa05f6c04) || a==UINT32_C(0xa05f6c10) ||
        a==UINT32_C(0xa05f6c14) || a==UINT32_C(0xa05f6c18)))) return false;
#ifdef KUI_VMU_NATIVE_PASSIVE_HOST_TEST
    return kui_vmu_native_passive_test_read(a,width,v);
#else
    *v=width==1u?*(volatile const uint8_t *)(uintptr_t)a:
        width==2u?*(volatile const uint16_t *)(uintptr_t)a:*(volatile const uint32_t *)(uintptr_t)a;
    return true;
#endif
}
static bool read_heap_raw(void *unused,uint32_t a,uint32_t *v) {return read_value(unused,a,4u,v);}
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
/* Only heap-routing identity reads may recognize the typed terminal table.
 * Its four wrappers and installed fatal-handler patch are rechecked through
 * the raw reader, independently of the allocator/free-list proof. */
static void __attribute__((noipa)) normalize_heap_dispatch(uint32_t address,uint32_t *value) {
#if KUI_NATIVE_METADATA_CACHE_ISOLATION
    const struct kui_vmu_native_passive_config *const cfg=native_cached_config();
#endif
    (void)kui_vmu_native_terminal_dispatch_normalize(&NATIVE_HOT_CONFIG.exits,read_heap_raw,NULL,
        NATIVE_HOT_CONFIG.low_terminal,address,NATIVE_HOT_CONFIG.lease.owner.dispatch_cell,
        NATIVE_HOT_CONFIG.lease.owner.dispatch_table,value);
}
/* Only this exact routing word can change in the normalizer. Keep its full
 * live-table/terminal-hook proof on that path, but do not enter the outlined
 * proof for ordinary heap identities or installed-instruction witnesses.
 * One leaf gate keeps the rejection path free of a callee-save frame without
 * duplicating the gate at every inlined proved-reader callsite. */
__asm__(".global __kui_native_dispatch_read_gate_worker\n"
        ".set __kui_native_dispatch_read_gate_worker,1\n");
static void __attribute__((noipa)) normalize_heap_dispatch_if_routing(
    uint32_t address,uint32_t *value) {
#if KUI_NATIVE_METADATA_CACHE_ISOLATION
    const struct kui_vmu_native_passive_config *const cfg=native_cached_config();
#endif
    if(address==NATIVE_HOT_CONFIG.lease.owner.dispatch_cell &&
       NATIVE_HOT_CONFIG.exits.family==KUI_VMU_NATIVE_EXIT_TERMINAL_DISPATCH)
        normalize_heap_dispatch(address,value);
}
#endif
static bool read_heap(void *unused,uint32_t a,uint32_t *v) {
    if(!read_heap_raw(unused,a,v)) return false;
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
    normalize_heap_dispatch_if_routing(a,v);
#endif
    return true;
}
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
/* The prepared lifetime kernel proves every dynamic address before following
 * it; immutable hook addresses are admitted when their plan is constructed.
 * This reader is private to those paths. General runtime readers keep their
 * original independent span checks. */
static inline __attribute__((always_inline)) bool read_heap_proved(void *unused,uint32_t a,uint32_t *v) {
    (void)unused;
#ifdef KUI_VMU_NATIVE_PASSIVE_HOST_TEST
    if(!kui_vmu_native_passive_test_read(a,4u,v)) return false;
#else
    *v=*(volatile const uint32_t *)(uintptr_t)a;
#endif
    normalize_heap_dispatch_if_routing(a,v);return true;
}
#endif
static bool stack_valid(void) {
    uint32_t sp=current_sp(),low=active_stack_low();
    if(((current_sr()>>4)&15u)!=15u || sp<low+256u || sp>=low+0x1000u) return false;
    for(unsigned i=0;i<16u;i++) if(guard(sp,i)!=GUARD) return false;
    return true;
}
static bool lease_valid(void) {
    RECORD_WORK(heap);
    return kui_vmu_native_heap_lease_valid(&config.lease,read_heap,NULL,1u);
}
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
/* Arm established complete free-list exclusion. Reviewed allocate only splits
 * or removes free chunks; release and initializer entry remain intercepted.
 * Both polling callers verify those installed hooks before this bounded
 * identity witness. Full ownership is still renewed by every release check
 * and on both sides of every external storage loan. This is not a replacement
 * for a full check when admitting a new lease or an unobserved allocator. */
/* Shared failure-only row writer; code families 0x200xx, 0x4iirr and
 * 0x5iirr identify transfer, controller and lease polling respectively. */
static bool __attribute__((noinline)) failure_witness(uint32_t code,
    uint32_t address,uint32_t value,uint32_t expected) {
    state.fault_at=address;state.fault_value=value;state.fault_expect=expected;
    state.runtime_result=code;return false;
}
static bool poll_lease_valid(void) {
    RECORD_WORK(heap);
    /* Same admitted static plan, predicate and first-failure read order as
     * lifetime_identity_prepared_inline; retain the actual failed witness. */
    const struct kui_vmu_native_lifetime_plan *p=&lifetime_plan;
    if(p->self!=(uintptr_t)p || p->ready!=KUI_VMU_NATIVE_LIFETIME_READY ||
       p->count>KUI_VMU_NATIVE_LIFETIME_RANGES) {
        uint32_t bad=(p->self!=(uintptr_t)p) | ((p->ready!=KUI_VMU_NATIVE_LIFETIME_READY)<<1) |
            ((p->count>KUI_VMU_NATIVE_LIFETIME_RANGES)<<2);
        return failure_witness(UINT32_C(0x50001),(uint32_t)(uintptr_t)p,bad,0u);
    }
#if KUI_NATIVE_METADATA_CACHE_ISOLATION
    const struct kui_vmu_native_lifetime_plan *const metadata=native_cached_lifetime();
#define NATIVE_LIFETIME_VIEW metadata
#else
#define NATIVE_LIFETIME_VIEW p
#endif
    for(unsigned i=0;i<8u;i++) {
        uint32_t value,address=NATIVE_LIFETIME_VIEW->identity[i].address,expected=NATIVE_LIFETIME_VIEW->identity[i].value;
        if(!read_heap_proved(NULL,address,&value))
            return failure_witness(UINT32_C(0x50002)|(i<<8),address,UINT32_MAX,expected);
        if(value!=expected)
            return failure_witness(UINT32_C(0x50003)|(i<<8),address,value,expected);
    }
    return true;
#undef NATIVE_LIFETIME_VIEW
}
#else
#define poll_lease_valid lease_valid
#endif
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
/* Read-only sampling of the game's timer. The profiler admits the exact
 * existing free-running TMU0 profile; it never starts or reconfigures a clock.
 * This path is opt-in and normal installations take only the flag branches. */
static void diagnostic_sample(struct kui_vmu_native_profile_sample *sample) {
    uint32_t ccr;
#ifdef KUI_VMU_NATIVE_PASSIVE_HOST_TEST
    extern void kui_vmu_native_persistent_test_clock(struct kui_vmu_native_profile_sample *,uint32_t *);
    kui_vmu_native_persistent_test_clock(sample,&ccr);
#else
    sample->frqcr=*(volatile const uint16_t *)(uintptr_t)UINT32_C(0xffc00000);
    sample->tcor=*(volatile const uint32_t *)(uintptr_t)UINT32_C(0xffd80008);
    sample->tcr=*(volatile const uint16_t *)(uintptr_t)UINT32_C(0xffd80010);
    sample->tstr=*(volatile const uint8_t *)(uintptr_t)UINT32_C(0xffd80004);
    sample->counter=*(volatile const uint32_t *)(uintptr_t)UINT32_C(0xffd8000c);
    ccr=*(volatile const uint32_t *)(uintptr_t)UINT32_C(0xff00001c);
#endif
    if(!diagnostic.clock_samples) diagnostic.ccr_first=ccr;
    diagnostic.ccr_last=ccr;
    if(!(ccr&UINT32_C(0x100)) && diagnostic.ice_off!=UINT32_MAX) ++diagnostic.ice_off;
    if(diagnostic.clock_samples!=UINT32_MAX) ++diagnostic.clock_samples;
    if(kui_vmu_native_profile_sample_valid(sample)) {
        if(!diagnostic.clock_started) {
            diagnostic.clock_first=sample->counter;diagnostic.clock_started=1u;
        }
        diagnostic.clock_last=sample->counter;
    } else if(diagnostic.clock_invalid!=UINT32_MAX) ++diagnostic.clock_invalid;
}
static void diagnostic_begin(struct kui_vmu_native_profile_span *span) {
    struct kui_vmu_native_profile_sample sample;
    diagnostic_sample(&sample);kui_vmu_native_profile_begin(span,&sample);
}
/* These reads are diagnostic observations, never additional authority for a
 * transfer or storage loan. Stack reads keep the inherited cache alias. */
static bool diagnostic_phase_read(void *unused,uint32_t address,unsigned width,uint32_t *value) {
    (void)unused;
    if(width!=4u || (!ram(address,4u,4u) && address!=UINT32_C(0xa05f80d8) &&
       address!=UINT32_C(0xa05f80cc) && address!=UINT32_C(0xa05f80d0) &&
       address!=UINT32_C(0xa05f810c) && address!=UINT32_C(0xff000028)
#if KUI_NATIVE_E1_WAIT_CAPTURE
       && address!=UINT32_C(0xffd8000c)
#endif
       )) return false;
#ifdef KUI_VMU_NATIVE_PASSIVE_HOST_TEST
#if KUI_NATIVE_E1_WAIT_CAPTURE
    extern bool kui_native_wait_capture_test_read(uint32_t,unsigned,uint32_t *);
    return kui_native_wait_capture_test_read(address,width,value);
#else
    return kui_vmu_native_passive_test_read(address,width,value);
#endif
#else
    *value=*(volatile const uint32_t *)(uintptr_t)address;return true;
#endif
}
static void diagnostic_pair_begin(struct kui_vmu_native_profile_span *span,
    const struct kui_vmu_native_passive_frame *frame,unsigned kind) {
    diagnostic_begin(span);
    native_phase_begin(&diagnostic_phase,kind,(uint32_t)(uintptr_t)frame,
        physical(config.lease.payload)-32u,config.lease.lease_bytes+32u,
        &span->begin,diagnostic_phase_read,NULL);
}
static void diagnostic_end(enum kui_vmu_native_profile_kind kind,
    const struct kui_vmu_native_profile_span *span) {
    struct kui_vmu_native_profile_sample sample;
    diagnostic_sample(&sample);kui_vmu_native_profile_end(&diagnostic.timing,kind,span,&sample);
#if KUI_NATIVE_READER_DIAGNOSTIC
    /* Completion reaches this only after the existing context/lease and
     * response/publication guards passed. Only that proved scope may reset
     * the two reader diagnostic counters in the owned lease tail. */
    if(diagnostic.active && kind==KUI_VMU_NATIVE_PROFILE_COMPLETE) {
        reader_capture_poll(&sample);
#if KUI_NATIVE_LOADER_TIMELINE
        native_loader_timeline_heartbeat(&sample);
#endif
    }
#endif
    if(kind==KUI_VMU_NATIVE_PROFILE_SUBMIT || kind==KUI_VMU_NATIVE_PROFILE_COMPLETE)
        native_phase_end(&diagnostic_phase,&sample,diagnostic_phase_read,NULL);
}
static void diagnostic_report(void) __attribute__((noreturn));
#ifndef KUI_VMU_NATIVE_PASSIVE_HOST_TEST
#if KUI_NATIVE_LOADER_TIMELINE
static void diagnostic_phase_row(const uint32_t values[5]) {
    native_loader_timeline_row(UINT32_MAX,0u,values,5u);
}
#else
static void diagnostic_phase_row(const uint32_t values[5]) {
    char line[46];
    for(unsigned i=0;i<5u;i++) {
        for(unsigned digit=0;digit<8u;digit++)
            line[i*9u+digit]="0123456789ABCDEF"[(values[i]>>(28u-digit*4u))&15u];
        line[i*9u+8u]=' ';
    }
    line[45]=0;retail_display_line(line);
}
#endif
#endif
static void diagnostic_report(void) {
    /* This is the intentional PROFILE terminal, before restoring any display
     * register. The current completion has passed its ordinary ownership and
     * response guards, and its inherited saved frame is still live. The exact
     * Evolution phase collector retains its existing history instead. */
    native_raw_owner_capture(&diagnostic_phase,
        (!KUI_NATIVE_E1_WAIT_CAPTURE && !KUI_NATIVE_METADATA_CACHE_ISOLATION && !KUI_NATIVE_SPLIT_CONTROLLER_ISOLATION && config.diagnostics==KUI_VMU_NATIVE_DIAGNOSTICS_PROFILE),
        state.frame_address,state.frame_pr,state.frame_sr,diagnostic.clock_last,
        physical(config.lease.payload)-32u,
        config.lease.lease_bytes<=UINT32_MAX-32u?config.lease.lease_bytes+32u:0u,
        diagnostic_phase_read,NULL);
#ifdef KUI_VMU_NATIVE_PASSIVE_HOST_TEST
    kui_vmu_native_passive_test_terminal(17u,0u);
#else
    struct retail_display_state display;memcpy(display.regs,config.display,sizeof(display.regs));
    for(;;) {
#if KUI_NATIVE_READER_DIAGNOSTIC
    reader_capture_report(&display);
#if KUI_NATIVE_READAHEAD
    reader_readahead_report(&display);
#endif
#if KUI_NATIVE_LOADER_TIMELINE
    native_loader_timeline_report(&display);
#endif
#endif
    retail_display_restore(&display);
    retail_display_line("VMU LATENCY");
#if KUI_NATIVE_COOP_PUMP
#else
    retail_display_line("STORE TIME IN COMPLETE");
#endif
    retail_display_line("N OK BAD SUM MAX");
    const char *const labels[4]={"SUB","END","FREE","STORE"};
    for(unsigned i=0;i<4u;i++) {
        const struct kui_vmu_native_profile_metric *m=&diagnostic.timing.metric[i];
        retail_display_values(labels[i],(uint32_t[5]){m->calls,m->valid,m->invalid,m->ticks_total,m->ticks_max},5u);
    }
    retail_display_values("WALL SAMPLES BAD FLAGS",(uint32_t[4]){
        diagnostic.clock_started?diagnostic.clock_first-diagnostic.clock_last:0u,
        diagnostic.clock_samples,diagnostic.clock_invalid,diagnostic.timing.flags},4u);
    retail_display_values("FAST/FULL SUB/END",(uint32_t[4]){
        diagnostic.fast_submit,diagnostic.full_submit,diagnostic.fast_complete,diagnostic.full_complete},4u);
    retail_display_line("PHOTO / POWER OFF");
    retail_display_pause(300u);
    retail_display_restore(&display);
    retail_display_line("CLOCK/VIDEO");
    retail_display_line("HEX / 781250HZ");
    retail_display_values("CCR0 CCR1 ICEOFF POLL COMMIT",(uint32_t[5]){
        diagnostic.ccr_first,diagnostic.ccr_last,diagnostic.ice_off,
        state.completed-diagnostic.capture_poll_start,durable_card.commits},5u);
    retail_display_values("MODE N VALID WAIT WRAP",(uint32_t[5]){
        diagnostic_phase.enabled,diagnostic_phase.sequence,diagnostic_phase.valid,
        diagnostic_phase.waits,diagnostic_phase.wraps},5u);
    retail_display_hex("FAULT VEC ARMED",kui_vmu_native_fault_capture_state[7]);
    retail_display_line("PHOTO / POWER OFF");
    retail_display_pause(300u);
    if(diagnostic_phase.enabled==NATIVE_RAW_OWNER_MODE) {
        const struct native_raw_owner *r=&diagnostic_phase.raw;
        retail_display_restore(&display);
        retail_display_line("RAW OWNER STACK");
        retail_display_line("FRAME N FLAGS TICK PR");
        diagnostic_phase_row(r->header);
        retail_display_line("SR LOAD VBL CTRL SCAN");
        diagnostic_phase_row(r->header+5u);
        retail_display_hex("INTEVT",r->event);
        retail_display_line("FLAGS: SPAN1 WORDS2 VIDEO4");
        retail_display_line("RAW / NOT FAULT PC");
        retail_display_pause(300u);
        for(unsigned first=0u;first<r->words;first+=48u) {
            retail_display_restore(&display);
            retail_display_line("RAW OWNER STACK");
            retail_display_line("ADDRESS / RAW WORDS");
            for(unsigned i=first;i<first+48u && i<r->words;i+=4u) {
                /* Four-word groups stay inside the192-word zeroed array;
                 * the recorded count distinguishes a partial final group. */
                uint32_t row[5];row[0]=r->frame+4u*i;
                memcpy(row+1u,r->data+i,4u*sizeof(uint32_t));
                diagnostic_phase_row(row);
            }
            retail_display_line("RAW / NOT FAULT PC");
            retail_display_pause(300u);
        }
    }
    for(unsigned first=0;first<diagnostic_phase.count;first+=4u) {
        retail_display_restore(&display);
        retail_display_line("PHASE HISTORY");
        retail_display_line("KIND+FLAGS IN OUT TICKS I");
        retail_display_line("PC PR SR EVT FR / LOAD VBL CTRL T0 T1");
        for(unsigned i=first;i<first+4u && i<diagnostic_phase.count;i++) {
            unsigned at=(diagnostic_phase.sequence-diagnostic_phase.count+i)&(NATIVE_PHASE_RECORDS-1u);
            const struct native_phase_record *r=&diagnostic_phase.ring[at];
            diagnostic_phase_row((uint32_t[5]){r->kind|(r->flags<<8),r->scan_begin,r->scan_end,
                r->tick_begin-r->tick_end,i});
            diagnostic_phase_row(r->words+8u);
            diagnostic_phase_row(r->words+3u);
        }
        retail_display_line("CLK1 RD2 VID4 FRM8 WAIT10 WRAP20");
        retail_display_pause(300u);
    }
    }
#endif
}
static bool diagnostic_gesture(uint32_t controls) {
    return !(controls&2u) && ((controls>>16)&255u)>=200u && (controls>>24)>=200u;
}
static bool diagnostic_fast_gesture(void) {
    for(unsigned i=0;i<controller_pair.count;i++) {
        uint32_t header=controller_pair.frames[i].header,controls;
        if(((header>>14)&3u)==0u && (header&255u)==9u &&
           read_value(NULL,(controller_pair.frames[i].rx+8u)|UINT32_C(0xa0000000),4u,&controls))
            return diagnostic_gesture(controls);
    }
    return false;
}
#if KUI_NATIVE_E1_WAIT_CAPTURE
#include "vmu_native_wait_report.inc"
#endif
static void diagnostic_complete(const struct kui_vmu_native_profile_span *span,bool gesture,bool fast) {
    diagnostic_end(KUI_VMU_NATIVE_PROFILE_COMPLETE,span);
    if(fast) ++diagnostic.fast_complete;else ++diagnostic.full_complete;
    if(!diagnostic.active) {
        diagnostic.gesture_count=gesture?diagnostic.gesture_count+1u:0u;
        if(diagnostic.gesture_count>=30u) {
            bool phase_enabled=diagnostic_phase.enabled!=0u;
            memset(&diagnostic,0,sizeof(diagnostic));diagnostic.active=1u;
            diagnostic.capture_poll_start=state.completed;
            native_phase_reset(&diagnostic_phase,phase_enabled);
#if KUI_NATIVE_READER_DIAGNOSTIC
            reader_capture_start();
#if KUI_NATIVE_LOADER_TIMELINE
            native_loader_timeline_start();
#endif
#endif
        }
    } else if((diagnostic.clock_started && !diagnostic.clock_invalid &&
               diagnostic.clock_first-diagnostic.clock_last>=20u*KUI_VMU_NATIVE_PROFILE_HZ) ||
              state.completed-diagnostic.capture_poll_start>=1200u) diagnostic_report();
}
static void write_mdstar(uint32_t source) {
#ifdef KUI_VMU_NATIVE_PASSIVE_HOST_TEST
    extern void kui_vmu_native_passive_test_write(uint32_t,unsigned,uint32_t);
    kui_vmu_native_passive_test_write(UINT32_C(0xa05f6c04),4u,source);
#else
    *(volatile uint32_t *)(uintptr_t)UINT32_C(0xa05f6c04)=source;
#endif
}
static uint32_t issued_physical(void) {
#ifdef KUI_VMU_NATIVE_PASSIVE_HOST_TEST
    extern uint32_t kui_vmu_native_persistent_test_chain(const void *,unsigned);
    uint32_t address=kui_vmu_native_persistent_test_chain(issued_chain,sizeof(issued_chain));
#else
    uint32_t pointer=(uint32_t)(uintptr_t)issued_chain;
    if((pointer&UINT32_C(0xe0000000))!=UINT32_C(0xa0000000)) return 0u;
    uint32_t address=physical(pointer);
#endif
    uint32_t begin=physical(config.lease.payload);
    return ram(address,sizeof(issued_chain),32u) && address>=begin &&
        address-begin<=WORKER_SPAN-sizeof(issued_chain)?address:0u;
}
int kui_vmu_native_persistent_sd_transaction(void *context,struct kui_loader_sd *loan) {
    if(context!=&durable_card || !loan || !persistent_context_guard()) {
        if(loan) loan->ready=false;
        return -1;
    }
    int result=kui_vmu_durable_transaction(context,loan);
    if(!persistent_context_guard()) {loan->ready=false;return -1;}
    return result;
}
static int persistent_lease(kui_vmu_sd_transaction transaction,void *context) {
    if(config.diagnostics==KUI_VMU_NATIVE_DIAGNOSTICS_RAW_CHAIN) return KUI_VMU_SD_LEASE_FAULT;
    struct kui_vmu_native_profile_span span;
    if((!KUI_NATIVE_E1_WAIT_CAPTURE && !KUI_NATIVE_METADATA_CACHE_ISOLATION && !KUI_NATIVE_SPLIT_CONTROLLER_ISOLATION && config.diagnostics==KUI_VMU_NATIVE_DIAGNOSTICS_PROFILE)) diagnostic_begin(&span);
    if(transaction!=kui_vmu_native_persistent_sd_transaction || context!=&durable_card ||
       !persistent_storage_guard()) {
        if((!KUI_NATIVE_E1_WAIT_CAPTURE && !KUI_NATIVE_METADATA_CACHE_ISOLATION && !KUI_NATIVE_SPLIT_CONTROLLER_ISOLATION && config.diagnostics==KUI_VMU_NATIVE_DIAGNOSTICS_PROFILE)) diagnostic_end(KUI_VMU_NATIVE_PROFILE_STORAGE,&span);
        return KUI_VMU_SD_LEASE_FAULT;
    }
#ifdef KUI_VMU_NATIVE_PASSIVE_HOST_TEST
    extern int kui_vmu_native_persistent_test_lease(kui_vmu_sd_transaction,void *);
    int result=kui_vmu_native_persistent_test_lease(transaction,context);
#else
    int (*lease)(kui_vmu_sd_transaction,void *)=(void *)(uintptr_t)config.sd_lease;
    int result=lease(transaction,context);
#endif
    if(!persistent_storage_guard()) result=KUI_VMU_SD_LEASE_FAULT;
    if((!KUI_NATIVE_E1_WAIT_CAPTURE && !KUI_NATIVE_METADATA_CACHE_ISOLATION && !KUI_NATIVE_SPLIT_CONTROLLER_ISOLATION && config.diagnostics==KUI_VMU_NATIVE_DIAGNOSTICS_PROFILE)) diagnostic_end(KUI_VMU_NATIVE_PROFILE_STORAGE,&span);
    return result;
}
static bool protect_range(uint32_t begin,uint32_t bytes) {
    if(protected_count==KUI_VMU_NATIVE_LIFETIME_RANGES || !ram(begin,bytes,1u)) return false;
    protected_ranges[protected_count++]=(struct kui_vmu_native_lifetime_range){begin,bytes};
    return true;
}
static bool protect_sdk(void) {
    if(config.layout.inputs>KUI_VMU_NATIVE_LAYOUT_INPUTS || !config.layout.inputs) return false;
    protected_count=0u;
    if(!protect_range(config.driver.load_address,config.driver.image_bytes) ||
       !protect_range(config.tx_base,2u*KUI_VMU_NATIVE_TX_BANK_BYTES) ||
       !protect_range(config.driver.manager_initial,0x1100u) ||
       !protect_range(config.driver.manager_cell,4u) || !protect_range(config.driver.lock_cell,5u) ||
       !protect_range(config.driver.completion_slot,4u) ||
       !protect_range(config.layout.rx_base,4u) ||
       !protect_range(config.layout.input_array,config.layout.inputs*4u) ||
       !protect_range(config.layout.index_array,config.layout.inputs*4u) ||
       !protect_range(config.layout.gun_flag,4u) ||
       !protect_range(config.layout.timer1_callback,4u) ||
       !protect_range(config.layout.timer2_callback,4u)) return false;
    for(unsigned i=0;i<KUI_VMU_NATIVE_RX_SLOTS;i++)
        if(config.layout.rx_slots[i] && !protect_range(config.layout.rx_slots[i],1024u)) return false;
    return kui_vmu_native_lifetime_prepare(&lifetime_plan,&config.lease,read_heap,NULL,1u,
        protected_ranges,protected_count);
}
static bool hook_word(uint32_t address,uint32_t value,uint32_t mask) {
    if(!mask || !ram(address,4u,4u) || hook_witness_count==HOOK_WITNESSES) return false;
    unsigned i=hook_witness_count++;
    hook_witness[i].address=physical(address)|UINT32_C(0xa0000000);
    hook_witness[i].value=value&mask;hook_witness[i].mask=mask;return true;
}
static bool prepare_hook_entry(uint32_t address,uint32_t target) {
    uint16_t patch[6];
    if(!ram(address,12u,2u) || !kui_vmu_native_patch_encode(address,target,patch)) return false;
    uint32_t word=physical(address)&~3u,value=0u,mask=0u;
    for(unsigned i=0;i<12u;i++) {
        uint32_t at=physical(address)+i;
        if((at&~3u)!=word) {
            if(!hook_word(word,value,mask)) return false;
            word=at&~3u;value=mask=0u;
        }
        unsigned shift=(at&3u)*8u;
        value|=(uint32_t)((patch[i/2u]>>((i&1u)*8u))&255u)<<shift;
        mask|=UINT32_C(255)<<shift;
    }
    return hook_word(word,value,mask);
}
#ifndef KUI_VMU_NATIVE_PASSIVE_HOST_TEST
static bool prepare_sdk_coherence_hook(void) {
    extern const uint32_t kui_vmu_native_trace_exports[10];
    const uint32_t *table=kui_vmu_native_trace_exports;
    unsigned kind=config.layout.rx_layout==KUI_VMU_NATIVE_RX_UNIFORM48?0u:1u;
    uint32_t pc=config.layout.initializer_wrapper+(kind?46u:42u),op,word;
    uint32_t begin=physical(config.driver.load_address),bytes=config.driver.image_bytes;
    if(table[0]!=UINT32_C(0x4b545231) || table[1]!=2u || table[2]!=40u ||
       !ram(pc,10u,2u) || physical(pc)<begin || bytes<10u ||
       physical(pc)-begin>bytes-10u || !read_value(NULL,pc,2u,&op) || (op&0xff00u)!=0xd300u)
        return false;
    const uint16_t uniform[4]={0x66f2u,0x6573u,0x430bu,0x54f1u};
    const uint16_t variable[4]={0x67f2u,0x56f1u,0x430bu,0x54f3u};
    for(unsigned i=0;i<4u;i++)
        if(!read_value(NULL,pc+2u+2u*i,2u,&word) || word!=(kind?variable[i]:uniform[i]))
            return false;
    uint32_t slot=((pc+4u)&~3u)+4u*(op&255u);
    if(!ram(slot,4u,4u) || physical(slot)<begin || physical(slot)-begin>bytes-4u ||
       !read_value(NULL,slot,4u,&word) || !ram(word,12u,2u) ||
       physical(word)!=physical(config.layout.initializer)) return false;
    return hook_word(slot,table[7u+kind],UINT32_MAX);
}
#endif
static bool prepare_installed_hooks(void) {
    hook_witness_ready=0u;hook_witness_count=0u;
    if((config.driver.submit|config.lease.owner.release|config.lease.owner.initializer)&3u ||
       !prepare_hook_entry(config.driver.submit,kui_vmu_native_worker_exports.submit_hook) ||
       !prepare_hook_entry(config.lease.owner.release,kui_vmu_native_worker_exports.release_hook) ||
       !prepare_hook_entry(config.lease.owner.initializer,config.low_terminal) ||
       !hook_word(config.lease.owner.initializer_slot,config.low_heap_hook,UINT32_MAX) ||
       !hook_word(config.driver.completion_slot,kui_vmu_native_worker_exports.complete_hook,UINT32_MAX)) return false;
    for(unsigned i=0;i<config.exits.count;i++)
        if(!prepare_hook_entry(config.exits.entries[i],config.low_terminal)) return false;
#ifndef KUI_VMU_NATIVE_PASSIVE_HOST_TEST
    /* Witness the original initializer call-target word, independently
     * decoded from its exact wrapper, alongside the ordinary DMA hooks. */
    if(!prepare_sdk_coherence_hook()) return false;
#endif
    hook_witness_ready=UINT32_C(0x4e485031);return true;
}
static bool installed_hooks_valid(void) {
#if KUI_NATIVE_METADATA_CACHE_ISOLATION
    RECORD_WORK(hooks);
    if(hook_witness_ready!=UINT32_C(0x4e485031)) return false;
    /* Count/ready remain authoritative P2 reads. No external/game caller or
     * metadata writer can intervene in this masked, immutable-plan scope. */
    const unsigned count=hook_witness_count;
    if(!count || count>HOOK_WITNESSES) return false;
    const struct native_hook_witness *const expected=native_cached_hooks();
    for(unsigned i=0;i<count;i++) {
        uint32_t value;
        if(!read_heap_proved(NULL,expected[i].address,&value) ||
           (value&expected[i].mask)!=expected[i].value) return false;
    }
    return true;
#else
    RECORD_WORK(hooks);
    if(hook_witness_ready!=UINT32_C(0x4e485031) || !hook_witness_count ||
       hook_witness_count>HOOK_WITNESSES) return false;
    for(unsigned i=0;i<hook_witness_count;i++) {
        uint32_t value;
        if(!read_heap_proved(NULL,hook_witness[i].address,&value) ||
           (value&hook_witness[i].mask)!=hook_witness[i].value) return false;
    }
    return true;
#endif
}
#endif
#if KUI_VMU_NATIVE_READONLY_PROBE || KUI_VMU_NATIVE_PERSISTENT_PROBE
static uint8_t *readonly_rx(uint32_t address,unsigned bytes) {
    if(!ram(address,bytes,32u)) return NULL;
#ifdef KUI_VMU_NATIVE_PASSIVE_HOST_TEST
    return kui_vmu_native_passive_test_rx(address,bytes);
#else
    return (uint8_t *)(uintptr_t)(address|UINT32_C(0xa0000000));
#endif
}
#endif
static void report(void) __attribute__((noreturn));
static void report(void) {
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
    if(kui_vmu_native_trace_enabled()) {
        const struct kui_vmu_native_trace_failure failure={
            state.runtime_result,state.fault_at,state.fault_value,state.fault_expect,
            state.source,state.bytes,issued_physical(),sizeof(issued_chain),
            state.descriptors,state.nops,chain_frames};
        kui_vmu_native_trace_failure(&failure);
        kui_vmu_native_trace_report(state.error,state.runtime_result,state.fault_at,state.fault_value);
    }
#endif
#ifdef KUI_VMU_NATIVE_PASSIVE_HOST_TEST
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
    kui_vmu_native_passive_test_terminal(state.proof?15u:16u,state.error);
#elif KUI_VMU_NATIVE_READONLY_PROBE
    kui_vmu_native_passive_test_terminal(state.proof?13u:14u,state.error);
#else
    kui_vmu_native_passive_test_terminal(state.completed?11u:12u,state.error);
#endif
#else
    struct retail_display_state display;memcpy(display.regs,config.display,sizeof(display.regs));
    retail_display_restore(&display);
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
#if KUI_NATIVE_MAPLE_DELAY_ISOLATION
    if(state.error==0x4du) retail_display_line("MAPLE DELAY CLOCK REFUSED");
#endif
    retail_display_line("VMU / A2");
    retail_display_line("SCI JOURNAL");
    retail_display_hex("PHASE",state.proof?15u:16u);
    retail_display_hex("DETAIL",state.error);
    retail_display_values("SUB END RD PHASE SAVE",(uint32_t[5]){
        state.submitted,state.completed,durable_card.reads,durable_card.write_phases,durable_card.commits},5u);
    retail_display_values("SD BUSY ERR LOAN BLOCK",(uint32_t[5]){
        durable_card.sd_reads,durable_card.sd_busy,durable_card.sd_faults,
        (uint32_t)durable_card.last_lease_result,durable_card.last_block},5u);
    retail_display_values("REQ REPLY RX OVL FLAGS",(uint32_t[5]){
        overlay.stats.last_request,overlay.stats.last_reply,overlay.stats.last_rx,state.overlay_result,overlay.stats.flags},5u);
    retail_display_values("AT VALUE EXPECT CHECK LEASE",(uint32_t[5]){
        state.fault_at,state.fault_value,state.fault_expect,state.runtime_result,config.lease.payload},5u);
    retail_display_values("FRAME PR SR COPY PR SR",(uint32_t[5]){
        state.frame_address,state.frame_pr,state.frame_sr,completion_frame.pr,completion_frame.sr},5u);
    retail_display_line("PHOTO / POWER OFF");
#elif KUI_VMU_NATIVE_READONLY_PROBE
    retail_display_line("AUTOMATIC NATIVE READ-ONLY VMU TEST");
    retail_display_line("VIRTUAL A2 / NO SAVE OR STORAGE WRITES");
    retail_display_hex("PHASE",state.proof?13u:14u);
    retail_display_hex("DETAIL",state.error);
    retail_display_values("SUBMITTED COMPLETED RX SLOTS ADVERTISE REPLIES",(uint32_t[5]){
        state.submitted,state.completed,state.rx_slots,overlay.stats.advertisements,overlay.stats.replies},5u);
    retail_display_values("INFO ALLINFO MEDIA READS REFUSED",(uint32_t[5]){
        overlay.stats.devinfo,overlay.stats.allinfo,overlay.stats.media,overlay.stats.reads,card.refused},5u);
    retail_display_values("REQ REPLY RX OVL FLAGS",(uint32_t[5]){
        overlay.stats.last_request,overlay.stats.last_reply,overlay.stats.last_rx,state.overlay_result,overlay.stats.flags},5u);
    retail_display_values("AT VALUE EXPECT CHECK LEASE",(uint32_t[5]){
        state.fault_at,state.fault_value,state.fault_expect,state.runtime_result,config.lease.payload},5u);
    retail_display_line(state.proof?"READ ONLY VMU READ VERIFIED - PHOTO THEN OFF":"READ ONLY VMU REFUSED - PHOTO THEN OFF");
#else
    retail_display_line("AUTOMATIC NATIVE PASSIVE MAPLE TEST");
    retail_display_line("ONE PHYSICAL TRANSFER / NO VIRTUAL REPLIES");
    retail_display_hex("PHASE",state.completed?11u:12u);
    retail_display_hex("DETAIL",state.error);
    retail_display_values("SUBMITTED COMPLETED RX SLOTS DESCRIPTORS NOPS",(uint32_t[5]){
        state.submitted,state.completed,state.rx_slots,state.descriptors,state.nops},5u);
    retail_display_values("SOURCE BYTES FINGERPRINT SUBMIT SR COMPLETE SR",(uint32_t[5]){
        state.source,state.bytes,state.hash,state.submit_sr,state.complete_sr},5u);
    retail_display_values("FIRST RX FIRST RESPONSE LAST RESPONSE",(uint32_t[3]){
        state.first_rx,state.first_response,state.last_response},3u);
    retail_display_values("AT VALUE EXPECT CHECK LEASE",(uint32_t[5]){
        state.fault_at,state.fault_value,state.fault_expect,state.runtime_result,config.lease.payload},5u);
    retail_display_line(state.completed?"PASSIVE PAIR VERIFIED - PHOTO THEN POWER OFF":"PASSIVE PROBE REFUSED - PHOTO THEN POWER OFF");
#endif
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
    retail_display_line("IMAGE/JOURNAL ONLY");
#else
    retail_display_line("STORAGE WAS READ ONLY");
#endif
    for(;;) __asm__ volatile("nop");
#endif
}
static void fail(uint32_t code) __attribute__((noreturn));
#if KUI_NATIVE_MAPLE_DELAY_ISOLATION
#include "vmu_native_maple_delay.inc"
#endif
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
/* These are terminal failure witnesses, never an alternate admission path.
 * The assembly gateway has already excluded IRQs and retired the owner SP.
 * Reporting does not borrow storage, publish RX data, or modify owner RAM. */
void kui_vmu_native_runtime_fault(uint32_t kind,uint32_t value,uint32_t context1,uint32_t context2) {
    if(kui_vmu_native_trace_enabled()) kui_vmu_native_trace_report(kind,value,context1,context2);
#ifdef KUI_VMU_NATIVE_PASSIVE_HOST_TEST
    (void)value;(void)context1;(void)context2;
    kui_vmu_native_passive_test_terminal(18u,kind);
#else
    struct retail_display_state display;memcpy(display.regs,config.display,sizeof(display.regs));
    retail_display_restore(&display);
    retail_display_line("VMU FAILURE");
    bool exception=(kind&UINT32_C(0xffff0000))==UINT32_C(0x00010000);
    bool coherence=(kind&UINT32_C(0xffff0000))==UINT32_C(0x00020000);
    retail_display_line(exception?"EXCEPTION":
        coherence?"CACHE REFUSED":
        kind==1u?"ALLOCATION FAILED":"UNKNOWN");
    retail_display_hex("PHASE",18u);retail_display_hex("DETAIL",kind);
    retail_display_values(exception?"EXPEVT SPC SSR TEA":coherence?"CHECK AT VALUE EXPECT":
        "KIND VALUE CTX1 CTX2",(uint32_t[4]){
        (exception || coherence)?(kind&UINT32_C(0xffff)):kind,value,context1,context2},4u);
    retail_display_values("LEASE N HEAP N",(uint32_t[4]){
        config.lease.payload,config.lease.lease_bytes,config.lease.begin,config.lease.heap_bytes},4u);
    retail_display_line(exception?"NO SAVE":
        coherence?"NO TRANSFER":
        "REQUEST / SP");
    retail_display_line("PHOTO / POWER OFF");
    for(;;) __asm__ volatile("nop");
#endif
}
#endif
static void fail(uint32_t code) {state.error=code;
#if KUI_VMU_NATIVE_READONLY_PROBE || KUI_VMU_NATIVE_PERSISTENT_PROBE
    state.proof=0u;
#endif
    report();}
#if KUI_NATIVE_MAPLE_DELAY_POST_START_ISOLATION
void __attribute__((noreturn)) kui_vmu_native_maple_post_start_refused(
    const struct kui_vmu_native_passive_frame *f) {
    state.runtime_result=f->sr;fail(0x4eu);__builtin_unreachable();
}
#endif
void kui_vmu_native_passive_report(void) {if(!state.error) state.error=UINT32_C(0x100);
#if KUI_VMU_NATIVE_READONLY_PROBE || KUI_VMU_NATIVE_PERSISTENT_PROBE
    state.proof=0u;
#else
    state.completed=0u;
#endif
    report();}
int kui_vmu_native_passive_arm(const struct kui_vmu_native_passive_config *c,void *unused) {
    (void)unused;
    /* Heap identity and valid-free checks retain the allocator's raw arena
     * alias. Only worker location, DMA and protected bus spans use physical
     * addresses; a physically equal but mixed-alias lease still fails the
     * subsequent complete heap ownership proof. */
    if(!c || c->version!=1u || c->bytes!=sizeof(*c) || state.armed || state.pending ||
       c->driver.version!=1u || c->driver.bytes!=sizeof(c->driver) ||
       c->driver.family!=KUI_VMU_NATIVE_FAMILY_UNIFORM_V1 || c->driver.requirements!=KUI_VMU_NATIVE_REQUIREMENTS ||
       (c->layout.version!=KUI_VMU_NATIVE_LAYOUT_VERSION &&
        c->layout.version!=KUI_VMU_NATIVE_LAYOUT_INFILE_VERSION) || c->layout.bytes!=sizeof(c->layout) ||
       c->layout.tx_base!=c->tx_base || c->layout.rx_layout>KUI_VMU_NATIVE_RX_EXPLICIT48 ||
       c->lease.lease_bytes!=KUI_VMU_NATIVE_BACKGROUND_LEASE_BYTES ||
       !ram(c->lease.payload,c->lease.lease_bytes,32u) ||
       SELF_ADDRESS!=(physical(c->lease.payload)|UINT32_C(0xa0000000)) ||
       STACK_LOW!=SELF_ADDRESS+WORKER_SPAN-0x1000u || STACK_HIGH!=SELF_ADDRESS+WORKER_SPAN ||
       !stack_valid() || c->runtime.version!=1u || c->runtime.bytes!=sizeof(c->runtime) ||
       !ram(c->runtime.lease_address,c->runtime.lease_bytes,32u) ||
       physical(c->runtime.lease_address)!=physical(c->lease.payload) || c->runtime.lease_bytes!=c->lease.lease_bytes ||
       c->runtime.lease_header_bytes!=32u || c->runtime.required_lease_bytes<WORKER_SPAN ||
       c->runtime.required_lease_bytes>c->runtime.lease_bytes ||
       c->runtime.rx_layout>KUI_VMU_NATIVE_RX_EXPLICIT48 ||
       !ram(c->tx_base,UINT32_C(0x6000),32u) || (c->tx_base&UINT32_C(0xe0000000))!=UINT32_C(0xa0000000) ||
       kui_vmu_native_passive_resume!=c->driver.resume || kui_vmu_native_passive_consumer!=c->driver.consumer)
        return -1;
    /* The stage-owned input object is copied, not retained. */
#if KUI_NATIVE_METADATA_CACHE_ISOLATION
    native_metadata_ready=0u;
#endif
    memcpy(&config,c,sizeof(config));
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
    memset(&live_plan,0,sizeof(live_plan));
    memset(&controller_pair,0,sizeof(controller_pair));
    memset(&diagnostic,0,sizeof(diagnostic));
    memset(&lifetime_plan,0,sizeof(lifetime_plan));
    hook_witness_ready=0u;hook_witness_count=0u;
    if(config.diagnostics>KUI_VMU_NATIVE_DIAGNOSTICS_EVOLUTION_TRACE) {memset(&config,0,sizeof(config));return -2;}
#if KUI_NATIVE_E1_WAIT_CAPTURE || KUI_NATIVE_METADATA_CACHE_ISOLATION || KUI_NATIVE_MAPLE_DELAY_COMPLETE_ONLY_ISOLATION || KUI_NATIVE_MAPLE_DELAY_SUBMIT_ONLY_ISOLATION || KUI_NATIVE_SPLIT_CONTROLLER_ISOLATION
    if(config.diagnostics!=KUI_VMU_NATIVE_DIAGNOSTICS_NORMAL ||
       !native_phase_image(config.driver.image_bytes,config.driver.load_address,config.driver.image_sha256)) {
        memset(&config,0,sizeof(config));return -2;
    }
#endif
#endif
    if(!lease_valid()) {memset(&config,0,sizeof(config));return -2;}
#if KUI_VMU_NATIVE_READONLY_PROBE || KUI_VMU_NATIVE_PERSISTENT_PROBE
    if(kui_vmu_native_readonly_release_resume!=config.lease.owner.release+20u) {
        memset(&config,0,sizeof(config));return -3;
    }
    /* Initialize callbacks only after relocation, at their final P2 addresses. */
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
    if(((config.driver.submit|config.lease.owner.release|config.lease.owner.initializer)&3u) ||
       (config.sd_lease&1u) || config.sd_lease<UINT32_C(0x8c004000) ||
       config.sd_lease>=UINT32_C(0x8c007800) ||
       (config.low_terminal&1u) || config.low_terminal<UINT32_C(0x8c004000) ||
       config.low_terminal>=UINT32_C(0x8c007800) ||
       (config.low_heap_hook&1u) || config.low_heap_hook<UINT32_C(0x8c004000) ||
       config.low_heap_hook>=UINT32_C(0x8c007800) ||
       config.exits.version!=KUI_VMU_NATIVE_EXIT_VERSION || config.exits.bytes!=sizeof(config.exits) ||
       ((config.exits.family==KUI_VMU_NATIVE_EXIT_PAIR_ONLY)?
        (config.exits.count!=2u || config.exits.entries[2] || config.exits.exec_thunk):
        ((config.exits.family!=KUI_VMU_NATIVE_EXIT_FAMILY &&
          config.exits.family!=KUI_VMU_NATIVE_EXIT_PVR_CLEAR &&
          config.exits.family!=KUI_VMU_NATIVE_EXIT_PVR_COMPACT &&
          config.exits.family!=KUI_VMU_NATIVE_EXIT_TERMINAL_DISPATCH) ||
         config.exits.count!=KUI_VMU_NATIVE_EXIT_ENTRIES)) ||
       (config.exits.family==KUI_VMU_NATIVE_EXIT_TERMINAL_DISPATCH &&
        !kui_vmu_native_terminal_dispatch_tagged(&config.exits)) ||
       config.exits.load!=config.driver.load_address || config.exits.image_bytes!=config.driver.image_bytes ||
       config.exits.image_bytes<12u || !ram(config.exits.load,config.exits.image_bytes,4u) ||
       !protect_sdk() ||
       kui_vmu_durable_init_address(&durable_card,2u,&config.durable,persistent_lease,kui_vmu_native_persistent_sd_transaction) ||
       kui_vmu_readonly_overlay_init_backend_address(&overlay,2u,persistent_handler,persistent_admit,&durable_card)) {
        memset(&config,0,sizeof(config));return -3;
    }
    for(unsigned i=0;i<config.exits.count;i++) {
        uint32_t entry=config.exits.entries[i];
        if(!ram(entry,12u,2u) || physical(entry)<physical(config.exits.load) ||
           physical(entry)-physical(config.exits.load)>config.exits.image_bytes-12u) {
            memset(&config,0,sizeof(config));return -3;
        }
    }
#ifndef KUI_VMU_NATIVE_PASSIVE_HOST_TEST
    if(!kui_vmu_native_sdk_coherence_prepare(&config)) {memset(&config,0,sizeof(config));return -3;}
#endif
    if(!prepare_installed_hooks()) {memset(&config,0,sizeof(config));return -3;}
#if KUI_NATIVE_SPLIT_CONTROLLER_ISOLATION
    if(!split_controller_geometry()) {memset(&config,0,sizeof(config));return -3;}
#endif
#else
    if(kui_vmu_readonly_init_address(&card,2u) || kui_vmu_readonly_overlay_init_address(&overlay,2u,readonly_handler,&card)) {
        memset(&config,0,sizeof(config));return -3;
    }
#endif
#endif
#if KUI_VMU_NATIVE_PERSISTENT_PROBE && !defined(KUI_VMU_NATIVE_PASSIVE_HOST_TEST)
    /* Optional diagnosis only. NORMAL returns before any hardware access;
     * unsupported live exception ownership leaves the original vector alone.
     * All ordinary admission has completed; cold arm still owns its P2 stack. */
    (void)kui_vmu_native_fault_capture_install(
        (!KUI_NATIVE_E1_WAIT_CAPTURE && !KUI_NATIVE_METADATA_CACHE_ISOLATION && !KUI_NATIVE_SPLIT_CONTROLLER_ISOLATION && config.diagnostics==KUI_VMU_NATIVE_DIAGNOSTICS_PROFILE),&config.driver);
#endif
    state.armed=1u;
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
    native_phase_reset(&diagnostic_phase,(!KUI_NATIVE_E1_WAIT_CAPTURE && !KUI_NATIVE_METADATA_CACHE_ISOLATION && !KUI_NATIVE_SPLIT_CONTROLLER_ISOLATION && config.diagnostics==KUI_VMU_NATIVE_DIAGNOSTICS_PROFILE) &&
        native_phase_image(config.driver.image_bytes,config.driver.load_address,config.driver.image_sha256));
    kui_vmu_native_trace_init(&config);
#endif
    return 0;
}
#if KUI_VMU_NATIVE_READONLY_PROBE || KUI_VMU_NATIVE_PERSISTENT_PROBE
void kui_vmu_native_readonly_release_check(const struct kui_vmu_native_passive_frame *f) {
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
    kui_vmu_native_trace_point(0x30u,f,state.submitted,state.completed);
    struct kui_vmu_native_profile_span span;
    if((!KUI_NATIVE_E1_WAIT_CAPTURE && !KUI_NATIVE_METADATA_CACHE_ISOLATION && !KUI_NATIVE_SPLIT_CONTROLLER_ISOLATION && config.diagnostics==KUI_VMU_NATIVE_DIAGNOSTICS_PROFILE)) diagnostic_begin(&span);
#endif
    state.fault_at=f?f->r4:0u;state.fault_value=state.fault_at;
    state.fault_expect=config.lease.payload;
    /* Normal game frees can occur while Maple DMA is pending. They may
     * proceed only if their exact allocated chunk cannot touch this lease. */
    if(!state.armed || !f || !stack_valid() ||
       kui_vmu_native_readonly_release_resume!=config.lease.owner.release+20u ||
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
       !installed_hooks_valid() ||
       !kui_vmu_native_lifetime_release_prepared_inline(&lifetime_plan,read_heap_proved,NULL,1u,f->r4)) fail(0x30u);
#else
       !kui_vmu_native_lifetime_release_safe(&config.lease,read_heap,NULL,1u,f->r4)) fail(0x30u);
#endif
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
    if((!KUI_NATIVE_E1_WAIT_CAPTURE && !KUI_NATIVE_METADATA_CACHE_ISOLATION && !KUI_NATIVE_SPLIT_CONTROLLER_ISOLATION && config.diagnostics==KUI_VMU_NATIVE_DIAGNOSTICS_PROFILE)) diagnostic_end(KUI_VMU_NATIVE_PROFILE_FREE,&span);
    kui_vmu_native_trace_point(0x31u,f,state.submitted,state.completed);
#endif
}
#endif
static unsigned unit_index(uint32_t destination) {
    switch(destination&63u) {case 32:return 0u;case 1:return 1u;case 2:return 2u;
        case 4:return 3u;case 8:return 4u;case 16:return 5u;default:return 6u;}
}
static bool boundary_read(void *context,uint32_t address,unsigned width,uint32_t *value) {
    bool ok=read_value(context,address,width,value);
    /* A runtime state failure can precede chain parsing. Retain its last
     * sampled word rather than an all-zero diagnostic. This validator does
     * not expose the expected value: UINT32_MAX explicitly marks it unknown. */
    state.fault_at=address;state.fault_value=ok?*value:UINT32_MAX;
    state.fault_expect=UINT32_MAX;return ok;
}
static bool boundary(const struct kui_vmu_native_passive_frame *f,unsigned phase,unsigned bytes,
    struct kui_vmu_native_runtime_snapshot *snapshot) {
    RECORD_WORK(runtime);
    struct kui_vmu_native_runtime_boundary b={phase,current_sr(),f->sr,f->pr,state.source,state.source,bytes};
    int r=kui_vmu_native_runtime_check(&config.driver,&config.runtime,&b,boundary_read,NULL,snapshot);
    state.runtime_result=(uint32_t)r;
    return r==KUI_VMU_NATIVE_RUNTIME_VALID && snapshot->tx_base==config.tx_base;
}
static bool bind_runtime(struct kui_vmu_native_runtime_config *runtime) {
    RECORD_WORK(layout);
    struct kui_vmu_native_layout_diagnostic diagnostic;
    if(kui_vmu_native_layout_bind_checked(&config.layout,&config.driver,read_value,NULL,
       runtime,&diagnostic)) return true;
    state.fault_at=diagnostic.address;state.fault_value=diagnostic.value;
    state.fault_expect=diagnostic.expected;state.runtime_result=0x10000u|diagnostic.code;
    return false;
}
/* Parse the exact software-trigger chain, retaining all descriptor and payload
 * words. No RX address is followed until its exact slot has been established. */
static bool chain(unsigned bank,bool compare) {
    RECORD_WORK(chain);
    unsigned at=0u,count=0u,nops=0u;uint32_t hash=UINT32_C(2166136261);
    for(unsigned entry=0;entry<256u && at<TX_WORDS;entry++) {
        uint32_t control;
        if(!read_value(NULL,(state.source+at*4u)|UINT32_C(0xa0000000),4u,&control)) return false;
        unsigned words=control==UINT32_C(0x700)?1u:3u+(control&255u);
        state.fault_at=state.source+at*4u;state.fault_value=control;
        if(words>TX_WORDS-at || (control!=UINT32_C(0x700) && (control&~UINT32_C(0x800300ff)))) return false;
        for(unsigned i=0;i<words;i++) {
            uint32_t value;
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
            /* IMASK15 and idle DMA cover this complete capture/compare.
             * The already sampled descriptor control is word zero. */
            if(!i) value=control;
            else
#endif
            if(!read_value(NULL,(state.source+(at+i)*4u)|UINT32_C(0xa0000000),4u,&value)) return false;
            if(compare) {if(value!=captured[at+i]) return false;} else captured[at+i]=value;
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
            if(!compare)
#endif
            hash=(hash^value)*UINT32_C(16777619);
        }
        if(control==UINT32_C(0x700)) {++nops;at+=words;continue;}
#if KUI_VMU_NATIVE_READONLY_PROBE || KUI_VMU_NATIVE_PERSISTENT_PROBE
        if(count==KUI_VMU_READONLY_OVERLAY_FRAMES) return false;
#else
        if(count==48u) return false;
#endif
        uint32_t rx=captured[at+1u],header=captured[at+2u];
        unsigned destination=(header>>8)&255u,sender=(header>>16)&255u;
        unsigned port=(control>>16)&3u,unit=unit_index(destination);
        uint32_t expected=unit<6u?config.runtime.rx_slots[bank*24u+port*6u+unit]:0u;
        if((header>>24)!=(control&255u) || (destination>>6)!=port || sender!=port*64u ||
           !expected || rx!=physical(expected) || !ram(rx,1024u,32u)) return false;
        uint32_t begin=physical(config.lease.payload)-32u,end=physical(config.lease.payload)+config.lease.lease_bytes;
        if(rx<end && begin<rx+1024u) return false;
        for(unsigned i=0;i<count;i++) if(receivers[i]==rx) return false;
        if(compare) {if(receivers[count]!=rx) return false;} else receivers[count]=rx;
#if KUI_VMU_NATIVE_READONLY_PROBE || KUI_VMU_NATIVE_PERSISTENT_PROBE
        if(!compare) {
            uint8_t *mapped=readonly_rx(rx,1024u);if(!mapped) return false;
            overlay_frames[count]=(struct kui_vmu_readonly_overlay_frame){
                .header=header,.payload0=words>3u?captured[at+3u]:0u,
                .payload1=words>4u?captured[at+4u]:0u,.rx_physical=rx,
                .rx_capacity=1024u,.rx_bytes=mapped,.write_payload=NULL};
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
            chain_frames[count]=(struct kui_vmu_volatile_chain_frame){.control=control,.rx=rx,.header=header,
                .payload0=words>3u?captured[at+3u]:0u,
                .payload=(const uint8_t *)(const void *)(captured+at+3u)};
            if(destination==2u && (header&255u)==12u && words==37u) {
                memcpy(write_payloads[count],captured+at+5u,128u);
                overlay_frames[count].write_payload=(const uint8_t *)(const void *)write_payloads[count];
            }
#endif
        }
#endif
        ++count;at+=words;
        if(control&UINT32_C(0x80000000)) {
            if(compare) return at*4u==state.bytes && count==state.descriptors && nops==state.nops
#if !KUI_VMU_NATIVE_PERSISTENT_PROBE
                && hash==state.hash
#endif
                ;
            state.bytes=at*4u;state.descriptors=count;state.nops=nops;state.hash=hash;
            state.first_rx=receivers[0];return true;
        }
    }
    return false;
}
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
static bool live_boundary(const struct kui_vmu_native_passive_frame *f,unsigned phase,
    uint32_t dma,unsigned bytes,struct kui_vmu_native_runtime_snapshot *snapshot) {
    RECORD_WORK(runtime);
    struct kui_vmu_native_runtime_boundary b={phase,current_sr(),f->sr,f->pr,
        state.source,dma,bytes};
    int r;
    if(!live_plan.ready) {
        struct kui_vmu_native_layout_diagnostic diagnostic={0};
        RECORD_WORK(layout);
        r=kui_vmu_native_live_prepare(&live_plan,&config.layout,&config.driver,&config.runtime,
            &b,boundary_read,NULL,snapshot,&diagnostic);
        /* live_prepare has committed both live_plan and config.runtime.
         * No P1 metadata view was used before this complete successful proof. */
#if KUI_NATIVE_METADATA_CACHE_ISOLATION
        if(r==KUI_VMU_NATIVE_RUNTIME_VALID) native_metadata_publish();
#endif
        if(r==KUI_VMU_NATIVE_RUNTIME_INVALID && diagnostic.code) {
            state.fault_at=diagnostic.address;state.fault_value=diagnostic.value;
            state.fault_expect=diagnostic.expected;state.runtime_result=0x10000u|diagnostic.code;
            return false;
        }
    } else r=kui_vmu_native_live_check(&live_plan,&config.driver,&b,boundary_read,NULL,snapshot);
    state.runtime_result=(uint32_t)r;
    return r==KUI_VMU_NATIVE_RUNTIME_VALID && snapshot->tx_base==config.tx_base;
}
static bool filtered_boundary(const struct kui_vmu_native_passive_frame *f,
    struct kui_vmu_native_runtime_snapshot *snapshot) {
    return live_boundary(f,1u,issued_plan.output_physical,issued_plan.output_bytes,snapshot);
}
static bool physical_response_valid(unsigned i,uint32_t response) {
    uint32_t header=chain_frames[i].header;
    unsigned destination=(header>>8)&255u,port=destination&192u,cmd=header&255u;
    if(destination==2u) return true; /* No virtual A2 packet reached hardware. */
    if(response==UINT32_MAX) return destination!=32u;
    if((destination&63u)!=32u) {
        unsigned code=response&255u,words=response>>24;
        if(((response>>8)&255u)!=port || ((response>>16)&255u)!=destination) return false;
        /* These exact query errors are legal physical responses, and are
         * passed through unchanged. They never authorize a virtual reply. */
        if(code==0xfcu || code==0xfdu || code==0xfeu) return !words;
        uint32_t function;
        if(cmd==1u || cmd==2u) {
            if(code!=(cmd==1u?5u:6u) || words!=(cmd==1u?28u:48u)) return false;
            return read_value(NULL,(receivers[i]+4u)|UINT32_C(0xa0000000),4u,&function) && function;
        }
        if(destination==1u && cmd==3u) return code==7u && !words;
        if(destination==1u && (cmd==10u || cmd==11u || cmd==12u || cmd==13u || cmd==14u)) {
            /* A1 remains a real VMU. Its exact wire reply stays owned by
             * hardware, including memory-card errors and LCD acknowledgments.
             * Only a typed response from that same endpoint can precede A2
             * publication; a malformed physical packet aborts the batch. */
            if(code==0xfbu) {
                return words==1u &&
                    read_value(NULL,(receivers[i]+4u)|UINT32_C(0xa0000000),4u,&function) &&
                    function && !(function&UINT32_C(0xc0ffffff));
            }
            if(cmd==12u || cmd==13u || cmd==14u) return code==7u && !words;
            if(code!=8u || words!=(cmd==10u?7u:130u) ||
               !read_value(NULL,(receivers[i]+4u)|UINT32_C(0xa0000000),4u,&function) ||
               function!=UINT32_C(0x02000000)) return false;
            return cmd==10u ||
                (read_value(NULL,(receivers[i]+8u)|UINT32_C(0xa0000000),4u,&function) &&
                 function==overlay_frames[i].payload1);
        }
        return cmd==9u && code==8u && words>=1u && words<=255u &&
            read_value(NULL,(receivers[i]+4u)|UINT32_C(0xa0000000),4u,&function) &&
            function==chain_frames[i].payload0;
    }
    unsigned expected=cmd==1u?5u:cmd==2u?6u:8u,words=cmd==1u?28u:cmd==2u?48u:3u;
    uint32_t function;
    return (response&255u)==expected && ((response>>8)&255u)==port &&
        ((response>>16)&224u)==(port|32u) && (response>>24)==words &&
        read_value(NULL,(receivers[i]+4u)|UINT32_C(0xa0000000),4u,&function) &&
        (cmd==9u?function==UINT32_C(0x01000000):!!(function&UINT32_C(0x01000000)));
}
static bool persistent_storage_guard(void) {
    RECORD_WORK(storage);
    struct kui_vmu_native_runtime_config rebound=config.runtime;
    struct kui_vmu_native_runtime_snapshot snapshot;
    return state.armed && state.pending && state.submitted==state.completed+1u &&
        lease_valid() && stack_valid() && installed_hooks_valid() &&
        kui_vmu_native_passive_consumer==config.driver.consumer &&
        bind_runtime(&rebound) && !memcmp(&rebound,&config.runtime,sizeof(rebound)) &&
        boundary(&completion_frame,1u,state.bytes,&snapshot) && chain(snapshot.bank,true) &&
        issued_plan.output_physical==issued_physical() &&
        !kui_vmu_volatile_chain_verify(&issued_plan,issued_chain,sizeof(issued_chain));
}
/* The full ownership proof is renewed at each scoped storage boundary. Between it
 * and MDST/RX publication only reviewed private RAM work runs under IMASK15.
 * Repeat the live MMIO/context witness here, not the complete geometry and
 * free-list walks that were just proved without an intervening game caller. */
static bool context_reject(unsigned code,uint32_t address,uint32_t value,uint32_t expected) {
    /* Failure-only witnesses: retain the existing refusal and the exact failed
     * comparison instead of leaving a stale successful chain read on screen. */
    return failure_witness(UINT32_C(0x20000)|code,address,value,expected);
}
static bool transfer_context_valid(const struct kui_vmu_native_passive_frame *f,
    unsigned bank,unsigned phase,uint32_t dma) {
#if KUI_NATIVE_METADATA_CACHE_ISOLATION
    const struct kui_vmu_native_passive_config *const cfg=native_cached_config();
#endif
    const uint32_t address[4]={UINT32_C(0xa05f6c04),UINT32_C(0xa05f6c10),
        UINT32_C(0xa05f6c14),UINT32_C(0xa05f6c18)};
    const uint32_t expected[4]={dma,0u,1u,0u};
    unsigned mask=(f->sr>>4)&15u;
    if(!stack_valid()) {
        uint32_t sr=current_sr(),sp=current_sp(),low=active_stack_low();
        if(((sr>>4)&15u)!=15u) return context_reject(1u,0u,sr,0xf0u);
        if(sp<low+256u) return context_reject(2u,sp,sp,low+256u);
        if(sp>=low+0x1000u) return context_reject(3u,sp,sp,low+0x1000u);
        for(unsigned i=0;i<16u;i++) {
            uint32_t value=guard(sp,i);
            if(value!=GUARD) return context_reject(4u,low+i*4u,value,GUARD);
        }
        return context_reject(5u,sp,sr,0xf0u);
    }
    if(bank>1u) return context_reject(6u,0u,bank,1u);
    if(phase>1u) return context_reject(7u,0u,phase,1u);
    if(mask!=6u && mask!=15u) return context_reject(8u,0u,f->sr,0xf0u);
    uint32_t source=physical(NATIVE_HOT_CONFIG.tx_base)+bank*KUI_VMU_NATIVE_TX_BANK_BYTES;
    if(state.source!=source) return context_reject(9u,0u,state.source,source);
    if(!phase) {
        if(f->r12!=1u) return context_reject(10u,0u,f->r12,1u);
        if(!ram(f->r14,4u,4u) || physical(f->r14)!=physical(NATIVE_HOT_CONFIG.driver.manager_cell))
            return context_reject(11u,0u,f->r14,NATIVE_HOT_CONFIG.driver.manager_cell);
    } else if(!ram(f->pr,2u,2u) || physical(f->pr)!=physical(NATIVE_HOT_CONFIG.driver.completion_call)+4u)
        return context_reject(12u,0u,f->pr,NATIVE_HOT_CONFIG.driver.completion_call+4u);
    if(kui_vmu_native_passive_resume!=NATIVE_HOT_CONFIG.driver.resume)
        return context_reject(13u,0u,kui_vmu_native_passive_resume,NATIVE_HOT_CONFIG.driver.resume);
    if(kui_vmu_native_passive_consumer!=NATIVE_HOT_CONFIG.driver.consumer)
        return context_reject(14u,0u,kui_vmu_native_passive_consumer,NATIVE_HOT_CONFIG.driver.consumer);
    for(unsigned i=0;i<4u;i++) {
        uint32_t value;
        if(!read_value(NULL,address[i],4u,&value))
            return context_reject(0xa0u+i,address[i],UINT32_MAX,expected[i]);
        if(value!=expected[i]) return context_reject(0x20u+i,address[i],value,expected[i]);
    }
    /* Recheck live selectors and the lock around an MDSTAR change or scoped
     * storage call. The full geometry proof belongs to this same IMASK15
     * scope; these operations cannot resume game code between witnesses. */
    const uint32_t offsets[6]={0xfc0u,0x10b0u,0x10b4u,0x10b8u,0x10c4u,0x10f4u};
    const uint32_t values[6]={phase?0u:1u,state.source|UINT32_C(0xa0000000),
        bank^phase,bank^phase,0u,UINT32_C(0xa05f6c00)};
    uint32_t manager,lock,value;
    if(!read_value(NULL,NATIVE_HOT_CONFIG.driver.manager_cell,4u,&manager))
        return context_reject(0xb0u,NATIVE_HOT_CONFIG.driver.manager_cell,UINT32_MAX,NATIVE_HOT_CONFIG.driver.manager_initial);
    if(physical(manager)!=physical(NATIVE_HOT_CONFIG.driver.manager_initial))
        return context_reject(0x30u,NATIVE_HOT_CONFIG.driver.manager_cell,manager,NATIVE_HOT_CONFIG.driver.manager_initial);
    uint32_t expected_lock=(physical(NATIVE_HOT_CONFIG.driver.lock_cell)+4u)|UINT32_C(0xa0000000);
    if(!read_value(NULL,NATIVE_HOT_CONFIG.driver.lock_cell,4u,&lock))
        return context_reject(0xb1u,NATIVE_HOT_CONFIG.driver.lock_cell,UINT32_MAX,expected_lock);
    if(lock!=expected_lock) return context_reject(0x31u,NATIVE_HOT_CONFIG.driver.lock_cell,lock,expected_lock);
    if(!read_value(NULL,lock,1u,&value)) return context_reject(0xb2u,lock,UINT32_MAX,0x80u);
    if(value!=0x80u) return context_reject(0x32u,lock,value,0x80u);
    for(unsigned i=0;i<6u;i++) {
        if(!read_value(NULL,manager+offsets[i],4u,&value))
            return context_reject(0xc0u+i,manager+offsets[i],UINT32_MAX,values[i]);
        if(value!=values[i]) return context_reject(0x40u+i,manager+offsets[i],value,values[i]);
    }
    return true;
}
static bool persistent_context_guard(void) {
    unsigned bank=state.source==physical(config.tx_base)?0u:1u;
    if(!state.armed) return context_reject(0x50u,0u,state.armed,1u);
    if(!state.pending) return context_reject(0x51u,0u,state.pending,1u);
    if(state.submitted!=state.completed+1u)
        return context_reject(0x52u,0u,state.submitted,state.completed+1u);
    return transfer_context_valid(&completion_frame,bank,1u,state.source);
}
/* The full launch-local layout proof has already established every span and
 * free-hook exclusion. This narrower authority uses the same live SDK/IRQ
 * context, all initializer-input witnesses and only the RX entries actually
 * named by this main-unit query batch. Unused slots cannot authorize its
 * single parent-header store. No external call or game code runs between this
 * witness and publication. Full VMU/storage paths retain all48-slot checks. */
/* Diagnostic-only witnesses reuse the existing terminal row. The low byte is
 * the reason, the next byte the descriptor/source-word/context-field index.
 * No success-path store or second read is needed to identify a refusal. */
#define controller_reject(reason,index,address,value,expected) \
    failure_witness(UINT32_C(0x40000)|((index)<<8)|(reason),(address),(value),(expected))
#define CONTROLLER_FAIL(reason,index,address,value,expected) do { \
    (void)controller_reject((reason),(index),(address),(value),(expected));fail(0x47u); \
} while(0)
static bool controller_context_valid(const struct kui_vmu_native_passive_frame *f,unsigned phase) {
#if KUI_NATIVE_METADATA_CACHE_ISOLATION
    const struct kui_vmu_native_passive_config *const cfg=native_cached_config();
#endif
    if(live_plan.self!=(uintptr_t)&live_plan || live_plan.ready!=UINT32_C(0x4e4c5031) ||
       live_plan.input_count>24u || controller_pair.bank>1u || !controller_pair.count ||
       controller_pair.count>CONTROLLER_FRAMES || controller_pair.parent>=controller_pair.count) {
        uint32_t bad=(live_plan.self!=(uintptr_t)&live_plan) |
            ((live_plan.ready!=UINT32_C(0x4e4c5031))<<1) | ((live_plan.input_count>24u)<<2) |
            ((controller_pair.bank>1u)<<3) | ((!controller_pair.count)<<4) |
            ((controller_pair.count>CONTROLLER_FRAMES)<<5) |
            ((controller_pair.parent>=controller_pair.count)<<6);
        return controller_reject(0x20u,0u,(uint32_t)(uintptr_t)&live_plan,bad,0u);
    }
#if KUI_NATIVE_METADATA_CACHE_ISOLATION
    const struct kui_vmu_native_live *const metadata=native_cached_live();
#endif
    /* Preserve the existing, more specific 0x200xx transfer witness. */
    if(!transfer_context_valid(f,controller_pair.bank,phase,controller_pair.source)) return false;
    uint32_t value,manager;
    if(!read_value(NULL,NATIVE_HOT_CONFIG.driver.manager_cell,4u,&manager))
        return controller_reject(0x21u,0u,NATIVE_HOT_CONFIG.driver.manager_cell,UINT32_MAX,NATIVE_HOT_CONFIG.driver.manager_initial);
    if(!ram(manager,0x1100u,4u))
        return controller_reject(0x22u,0u,NATIVE_HOT_CONFIG.driver.manager_cell,manager,NATIVE_HOT_CONFIG.driver.manager_initial);
    if(physical(manager)!=physical(NATIVE_HOT_CONFIG.driver.manager_initial))
        return controller_reject(0x23u,0u,NATIVE_HOT_CONFIG.driver.manager_cell,manager,NATIVE_HOT_CONFIG.driver.manager_initial);
#define CONTROLLER_EXPECT(index,address,expected) do { \
    if(!read_value(NULL,(address),4u,&value)) \
        return controller_reject(0x24u,(index),(address),UINT32_MAX,(expected)); \
    if(value!=(expected)) return controller_reject(0x25u,(index),(address),value,(expected)); \
} while(0)
    CONTROLLER_EXPECT(0u,manager+0xfc4u,NATIVE_HOT_LIVE.tx);
    CONTROLLER_EXPECT(1u,manager+0xfc8u,NATIVE_HOT_LIVE.rx);
    CONTROLLER_EXPECT(2u,manager+0x10a8u,NATIVE_HOT_LIVE.tx);
    CONTROLLER_EXPECT(3u,manager+0x10acu,NATIVE_HOT_LIVE.tx+KUI_VMU_NATIVE_TX_BANK_BYTES);
    CONTROLLER_EXPECT(4u,NATIVE_HOT_LIVE.gun,0u);
    for(unsigned i=0;i<2u;i++) {
        uint32_t address=i?NATIVE_HOT_LIVE.timer2:NATIVE_HOT_LIVE.timer1;
        if(!read_value(NULL,address,4u,&value))
            return controller_reject(0x24u,5u+i,address,UINT32_MAX,NATIVE_HOT_CONFIG.driver.load_address);
        if(!kui_vmu_native_timer_callback_owned(&NATIVE_HOT_CONFIG.driver,value))
            return controller_reject(0x26u,5u+i,address,value,NATIVE_HOT_CONFIG.driver.load_address);
    }
    for(unsigned i=0;i<live_plan.input_count;i++)
        CONTROLLER_EXPECT(8u+i,NATIVE_HOT_LIVE.input_address[i],NATIVE_HOT_LIVE.input_value[i]);
    for(unsigned i=0;i<controller_pair.count;i++) {
        unsigned port=(controller_pair.frames[i].header>>14)&3u;
        unsigned slot=controller_pair.bank*24u+port*6u;
        CONTROLLER_EXPECT(32u+slot,manager+0xfe8u+slot*4u,NATIVE_HOT_CONFIG.runtime.rx_slots[slot]);
    }
#undef CONTROLLER_EXPECT
    return true;
}
static bool controller_prepare(const struct kui_vmu_native_passive_frame *f) {
#if KUI_NATIVE_METADATA_CACHE_ISOLATION
    const struct kui_vmu_native_passive_config *const cfg=native_cached_config();
#endif
    if((NATIVE_HOT_CONFIG.diagnostics!=KUI_VMU_NATIVE_DIAGNOSTICS_NORMAL &&
        NATIVE_HOT_CONFIG.diagnostics!=KUI_VMU_NATIVE_DIAGNOSTICS_PROFILE) || !live_plan.ready ||
       overlay.pending || !overlay.admitted || controller_pair.pending) return false;
    controller_pair.count=controller_pair.nops=controller_pair.words=0u;
    controller_pair.source=state.source;controller_pair.parent=CONTROLLER_FRAMES;
    if(state.source==physical(NATIVE_HOT_CONFIG.tx_base)) controller_pair.bank=0u;
    else if(state.source==physical(NATIVE_HOT_CONFIG.tx_base)+KUI_VMU_NATIVE_TX_BANK_BYTES) controller_pair.bank=1u;
    else return false;
    unsigned at=0u,ports=0u;
    while(at<CONTROLLER_WORDS) {
        uint32_t control;
        if(!read_value(NULL,(state.source+at*4u)|UINT32_C(0xa0000000),4u,&control)) fail(0x46u);
        controller_pair.snapshot[at]=control;
        if(control==UINT32_C(0x700)) {++controller_pair.nops;++at;continue;}
        unsigned words=control&255u,width=3u+words;
        if((control&~UINT32_C(0x800300ff)) || words>1u ||
           width>CONTROLLER_WORDS-at || controller_pair.count==CONTROLLER_FRAMES) return false;
        for(unsigned j=1u;j<width;j++)
            if(!read_value(NULL,(state.source+(at+j)*4u)|UINT32_C(0xa0000000),4u,
                &controller_pair.snapshot[at+j])) fail(0x46u);
        uint32_t rx=controller_pair.snapshot[at+1u],header=controller_pair.snapshot[at+2u];
        unsigned port=(control>>16)&3u,command=header&255u;
        uint32_t endpoint=UINT32_C(0x00002000)|(port<<14)|(port<<22);
        bool condition=words==1u && header==(endpoint|UINT32_C(0x01000009)) &&
            controller_pair.snapshot[at+3u]==UINT32_C(0x01000000);
        /* Empty ports are repeatedly queried by the original SDK. Retain
         * their exact INFO/ALLINFO DMA and natural attachment responses;
         * only the A controller GETCOND can authorize virtual A2 presence. */
        bool query=port && !words && (command==1u || command==2u) && header==(endpoint|command);
        if((!condition && !query) || (ports&(1u<<port))) return false;
        unsigned slot=controller_pair.bank*24u+port*6u;
        uint32_t expected=NATIVE_HOT_CONFIG.runtime.rx_slots[slot];
        if(!expected || rx!=physical(expected) || !ram(rx,1024u,32u)) return false;
        controller_pair.frames[controller_pair.count].rx=rx;
        controller_pair.frames[controller_pair.count].header=header;
        if(!port) controller_pair.parent=controller_pair.count;
        ++controller_pair.count;ports|=1u<<port;at+=width;
        if(control&UINT32_C(0x80000000)) {
            if(!(ports&1u)) return false;
            controller_pair.words=at;
            if(!controller_context_valid(f,0u)) fail(0x46u);
            state.bytes=at*4u;state.descriptors=controller_pair.count;state.nops=controller_pair.nops;
            state.first_rx=controller_pair.frames[0].rx;state.rx_slots=live_plan.nonzero;
            controller_pair.pending=1u;state.pending=1u;++state.submitted;
            ++overlay.stats.prepared;return true;
        }
    }
    return false;
}
#include "vmu_native_split_controller.inc"
static void controller_complete(const struct kui_vmu_native_passive_frame *f) {
#if KUI_NATIVE_SPLIT_CONTROLLER_ISOLATION
    if(controller_pair.pending!=NATIVE_CONTROLLER_PREPARED)
        CONTROLLER_FAIL(1u,0u,(uint32_t)(uintptr_t)&controller_pair.pending,
            controller_pair.pending,NATIVE_CONTROLLER_PREPARED);
#endif
    if(!controller_pair.pending || controller_pair.source!=state.source ||
       !controller_pair.words || controller_pair.words>CONTROLLER_WORDS ||
       !controller_pair.count || controller_pair.count>CONTROLLER_FRAMES ||
       controller_pair.parent>=controller_pair.count ||
       controller_pair.words*4u!=state.bytes || controller_pair.count!=state.descriptors ||
       overlay.pending || !overlay.admitted || !durable_card.ready || durable_card.faulted) {
        uint32_t bad=(!controller_pair.pending) | ((controller_pair.source!=state.source)<<1) |
            ((!controller_pair.words)<<2) | ((controller_pair.words>CONTROLLER_WORDS)<<3) |
            ((!controller_pair.count)<<4) | ((controller_pair.count>CONTROLLER_FRAMES)<<5) |
            ((controller_pair.parent>=controller_pair.count)<<6) |
            ((controller_pair.words*4u!=state.bytes)<<7) | ((controller_pair.count!=state.descriptors)<<8) |
            ((!!overlay.pending)<<9) | ((!overlay.admitted)<<10) | ((!durable_card.ready)<<11) |
            ((!!durable_card.faulted)<<12);
        CONTROLLER_FAIL(1u,0u,(uint32_t)(uintptr_t)&controller_pair,bad,0u);
    }
    /* Validate the entire immutable source and every physical response before
     * touching even the parent header. A failed fast completion is terminal;
     * it cannot enter a partly prepared full-overlay transaction. */
    for(unsigned i=0;i<controller_pair.words;i++) {
        uint32_t value,address=(state.source+i*4u)|UINT32_C(0xa0000000);
        if(!read_value(NULL,address,4u,&value))
            CONTROLLER_FAIL(2u,i,address,UINT32_MAX,controller_pair.snapshot[i]);
        if(value!=controller_pair.snapshot[i])
            CONTROLLER_FAIL(3u,i,address,value,controller_pair.snapshot[i]);
    }
    uint32_t parent_response=0u;
    for(unsigned i=0;i<controller_pair.count;i++) {
        uint32_t response,function;uint32_t rx=controller_pair.frames[i].rx;
        unsigned port=(controller_pair.frames[i].header>>14)&3u;
        unsigned command=controller_pair.frames[i].header&255u;
        unsigned code=command==1u?5u:command==2u?6u:8u;
        unsigned words=command==1u?28u:command==2u?48u:3u;
        uint32_t address=rx|UINT32_C(0xa0000000);
        if(!read_value(NULL,address,4u,&response))
            CONTROLLER_FAIL(4u,i,address,UINT32_MAX,0u);
        if(response!=UINT32_MAX) {
            unsigned actual=response&255u;
            if(port && command!=9u && (actual==0xfcu || actual==0xfdu || actual==0xfeu)) {
                /* The same exact zero-word query errors accepted by the
                 * generic physical accessory validator carry no function.
                 * They remain untouched and cannot authorize A's overlay. */
                uint32_t expected=UINT32_C(0x00200000)|(port<<14)|(port<<22)|actual;
                if(response!=expected) CONTROLLER_FAIL(5u,i,address,response,expected);
            } else {
                uint32_t expected=(words<<24)|UINT32_C(0x00200000)|(port<<14)|(port<<22)|code;
                if((response&UINT32_C(0xffe0ffff))!=expected)
                    CONTROLLER_FAIL(6u,i,address,response,expected);
                if(!read_value(NULL,address+4u,4u,&function))
                    CONTROLLER_FAIL(7u,i,address+4u,UINT32_MAX,command==9u?UINT32_C(0x01000000):1u);
#if KUI_NATIVE_SPLIT_CONTROLLER_ISOLATION
                if(command==9u?function!=UINT32_C(0x01000000):
                   (!function || (!port && !(function&UINT32_C(0x01000000)))))
#else
                if(command==9u?function!=UINT32_C(0x01000000):!function)
#endif
                    CONTROLLER_FAIL(8u,i,address+4u,function,command==9u?UINT32_C(0x01000000):1u);
            }
        } else if(!port) CONTROLLER_FAIL(9u,i,address,response,0u);
        if(i==controller_pair.parent) {
            if(response&UINT32_C(0x00020000))
                CONTROLLER_FAIL(10u,i,address,response,response&~UINT32_C(0x00020000));
            parent_response=response;
        }
        if(!i) state.first_response=response;
        state.last_response=response;
    }
    if(!controller_context_valid(f,1u)) fail(0x47u);
    uint8_t *parent=readonly_rx(controller_pair.frames[controller_pair.parent].rx,1024u);
    if(!parent) CONTROLLER_FAIL(11u,controller_pair.parent,
        controller_pair.frames[controller_pair.parent].rx,0u,1024u);
#if !KUI_NATIVE_READER_ONLY_ISOLATION
    ((volatile uint8_t *)parent)[2]=(uint8_t)((parent_response>>16)|2u);
    ++overlay.stats.advertisements;
#else
    /* Keep the proved controller completion and its reader TICK, while the
     * physical response retains its original attachment byte. */
    (void)parent_response;
#endif
    ++overlay.stats.completed;
    controller_pair.pending=0u;state.pending=0u;++state.completed;state.error=0u;
    state.proof=durable_card.commits!=0u;
}
#undef CONTROLLER_FAIL
#undef controller_reject
#endif
void kui_vmu_native_passive_submit(const struct kui_vmu_native_passive_frame *f) {
#if KUI_NATIVE_METADATA_CACHE_ISOLATION
    const struct kui_vmu_native_passive_config *const cfg=native_cached_config();
#endif
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
    kui_vmu_native_trace_point(0x10u,f,state.submitted,state.completed);
    struct kui_vmu_native_profile_span span;
    if((!KUI_NATIVE_E1_WAIT_CAPTURE && !KUI_NATIVE_METADATA_CACHE_ISOLATION && !KUI_NATIVE_SPLIT_CONTROLLER_ISOLATION && NATIVE_HOT_CONFIG.diagnostics==KUI_VMU_NATIVE_DIAGNOSTICS_PROFILE))
        diagnostic_pair_begin(&span,f,KUI_VMU_NATIVE_PROFILE_SUBMIT);
#endif
#if KUI_VMU_NATIVE_READONLY_PROBE || KUI_VMU_NATIVE_PERSISTENT_PROBE
    if(!state.armed || state.pending || state.submitted!=state.completed || !f || !stack_valid()) fail(1u);
    if(state.submitted>=PAIR_LIMIT) fail(20u);
#else
    if(!state.armed || state.pending || state.submitted || !f || !stack_valid()) fail(1u);
#endif
    state.submit_sr=f->sr;
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
    if(!installed_hooks_valid()) fail(0x45u);
#endif
    if(!ram(f->r14,4u,4u) || physical(f->r14)!=physical(NATIVE_HOT_CONFIG.driver.manager_cell) || f->r12!=1u) fail(2u);
    if(!poll_lease_valid()) fail(3u);
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
    if(kui_vmu_native_passive_resume!=NATIVE_HOT_CONFIG.driver.resume) {
        (void)failure_witness(UINT32_C(0x50004),(uint32_t)(uintptr_t)&kui_vmu_native_passive_resume,
            kui_vmu_native_passive_resume,NATIVE_HOT_CONFIG.driver.resume);fail(3u);
    }
    if(kui_vmu_native_passive_consumer!=NATIVE_HOT_CONFIG.driver.consumer) {
        (void)failure_witness(UINT32_C(0x50005),(uint32_t)(uintptr_t)&kui_vmu_native_passive_consumer,
            kui_vmu_native_passive_consumer,NATIVE_HOT_CONFIG.driver.consumer);fail(3u);
    }
#else
    if(kui_vmu_native_passive_resume!=NATIVE_HOT_CONFIG.driver.resume ||
       kui_vmu_native_passive_consumer!=NATIVE_HOT_CONFIG.driver.consumer) fail(3u);
#endif
#if !KUI_VMU_NATIVE_PERSISTENT_PROBE
    if(!bind_runtime(&config.runtime)) fail(14u);
#endif
    uint32_t source;
    if(!read_value(NULL,UINT32_C(0xa05f6c04),4u,&source) || source!=physical(source) ||
       !ram(source,KUI_VMU_NATIVE_TX_BANK_BYTES,32u)) fail(4u);
    state.source=source;
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
    if(controller_prepare(f)) {
        if((!KUI_NATIVE_E1_WAIT_CAPTURE && !KUI_NATIVE_METADATA_CACHE_ISOLATION && !KUI_NATIVE_SPLIT_CONTROLLER_ISOLATION && NATIVE_HOT_CONFIG.diagnostics==KUI_VMU_NATIVE_DIAGNOSTICS_PROFILE)) {
            ++diagnostic.fast_submit;diagnostic_end(KUI_VMU_NATIVE_PROFILE_SUBMIT,&span);
        }
        kui_vmu_native_trace_point(0x1fu,f,state.submitted,state.completed);
        return;
    }
#endif
    struct kui_vmu_native_runtime_snapshot snapshot;
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
    if(!live_boundary(f,0u,state.source,12u,&snapshot)) fail(5u);
    kui_vmu_native_trace_point(0x11u,f,state.submitted,state.completed);
#else
    if(!boundary(f,0u,12u,&snapshot)) fail(5u);
#endif
    if(!chain(snapshot.bank,false)) fail(6u);
#if KUI_VMU_NATIVE_READONLY_PROBE || KUI_VMU_NATIVE_PERSISTENT_PROBE
    int prepared=kui_vmu_readonly_overlay_prepare(&overlay,overlay_frames,state.descriptors,
        state.source,state.bytes,physical(NATIVE_HOT_CONFIG.lease.payload)-32u,NATIVE_HOT_CONFIG.lease.lease_bytes+32u);
    state.overlay_result=(uint32_t)prepared;if(prepared) fail(21u);
#endif
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
    /* Both builders touch private RAM only; the admission callback is the
     * reviewed durable metadata whitelist. No game/SD call intervenes between
     * the full submission proof above and the final MDST-context witness. */
    uint32_t target=issued_physical();
    if(!target) {
        state.runtime_result=UINT32_C(0x30001);
        state.fault_at=(uint32_t)(uintptr_t)issued_chain;state.fault_value=0u;
        state.fault_expect=NATIVE_HOT_CONFIG.lease.payload;fail(0x40u);
    }
    int built=kui_vmu_volatile_chain_build_address(&issued_plan,chain_frames,state.descriptors,
        state.source,state.bytes,issued_chain,sizeof(issued_chain),target,2u);
    if(built) {
        state.runtime_result=UINT32_C(0x30002);state.fault_at=state.source;
        state.fault_value=(uint32_t)built;state.fault_expect=0u;fail(0x40u);
    }
    int verified=kui_vmu_volatile_chain_verify(&issued_plan,issued_chain,sizeof(issued_chain));
    if(verified) {
        state.runtime_result=UINT32_C(0x30003);state.fault_at=target;
        state.fault_value=(uint32_t)verified;state.fault_expect=0u;fail(0x40u);
    }
    if(!transfer_context_valid(f,snapshot.bank,0u,state.source)) fail(0x40u);
    kui_vmu_native_trace_point(0x12u,f,state.submitted,state.completed);
    if(NATIVE_HOT_CONFIG.diagnostics!=KUI_VMU_NATIVE_DIAGNOSTICS_RAW_CHAIN) write_mdstar(target);
    if(!transfer_context_valid(f,snapshot.bank,0u,
       NATIVE_HOT_CONFIG.diagnostics==KUI_VMU_NATIVE_DIAGNOSTICS_RAW_CHAIN?state.source:target)) fail(0x41u);
#else
    if(!boundary(f,0u,state.bytes,&snapshot) || !lease_valid() || !stack_valid()) fail(7u);
#endif
    state.rx_slots=snapshot.rx_slots_nonzero;state.pending=1u;
#if KUI_VMU_NATIVE_READONLY_PROBE || KUI_VMU_NATIVE_PERSISTENT_PROBE
    ++state.submitted;
#else
    state.submitted=1u;
#endif
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
    if((!KUI_NATIVE_E1_WAIT_CAPTURE && !KUI_NATIVE_METADATA_CACHE_ISOLATION && !KUI_NATIVE_SPLIT_CONTROLLER_ISOLATION && NATIVE_HOT_CONFIG.diagnostics==KUI_VMU_NATIVE_DIAGNOSTICS_PROFILE)) {
        ++diagnostic.full_submit;diagnostic_end(KUI_VMU_NATIVE_PROFILE_SUBMIT,&span);
    }
    kui_vmu_native_trace_point(0x1fu,f,state.submitted,state.completed);
#endif
}
void kui_vmu_native_passive_complete(const struct kui_vmu_native_passive_frame *f) {
#if KUI_NATIVE_METADATA_CACHE_ISOLATION
    const struct kui_vmu_native_passive_config *const cfg=native_cached_config();
#endif
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
    kui_vmu_native_trace_point(0x20u,f,state.submitted,state.completed);
    struct kui_vmu_native_profile_span span;
    if((!KUI_NATIVE_E1_WAIT_CAPTURE && !KUI_NATIVE_METADATA_CACHE_ISOLATION && !KUI_NATIVE_SPLIT_CONTROLLER_ISOLATION && NATIVE_HOT_CONFIG.diagnostics==KUI_VMU_NATIVE_DIAGNOSTICS_PROFILE))
        diagnostic_pair_begin(&span,f,KUI_VMU_NATIVE_PROFILE_COMPLETE);
#endif
#if KUI_VMU_NATIVE_READONLY_PROBE || KUI_VMU_NATIVE_PERSISTENT_PROBE
    if(!state.armed || !state.pending || state.submitted!=state.completed+1u || !f || !stack_valid()) fail(8u);
#else
    if(!state.armed || !state.pending || state.submitted!=1u || state.completed || !f || !stack_valid()) fail(8u);
#endif
    state.complete_sr=f->sr;
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
    state.frame_address=(uint32_t)(uintptr_t)f;state.frame_pr=f->pr;state.frame_sr=f->sr;
    if(!installed_hooks_valid()) fail(0x45u);
#endif
    if(!poll_lease_valid()) fail(9u);
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
#if KUI_NATIVE_SPLIT_CONTROLLER_ISOLATION
    if(controller_pair.pending==NATIVE_CONTROLLER_CAPTURED) split_controller_prepare_complete(f);
    else if(controller_pair.pending!=NATIVE_CONTROLLER_IDLE &&
            controller_pair.pending!=NATIVE_CONTROLLER_PREPARED) fail(0x47u);
#endif
    if(controller_pair.pending) {
        controller_complete(f);
#if KUI_NATIVE_COOP_PUMP
        reader_cooperative_service();
#endif
#if KUI_NATIVE_E1_WAIT_CAPTURE
        native_wait_capture_observe(f);
#endif
        if((!KUI_NATIVE_E1_WAIT_CAPTURE && !KUI_NATIVE_METADATA_CACHE_ISOLATION && !KUI_NATIVE_SPLIT_CONTROLLER_ISOLATION && NATIVE_HOT_CONFIG.diagnostics==KUI_VMU_NATIVE_DIAGNOSTICS_PROFILE))
            diagnostic_complete(&span,diagnostic_fast_gesture(),true);
        kui_vmu_native_trace_point(0x2fu,f,state.submitted,state.completed);
        return;
    }
#endif
#if !KUI_VMU_NATIVE_PERSISTENT_PROBE
    struct kui_vmu_native_runtime_config rebound=NATIVE_HOT_CONFIG.runtime;
    if(!bind_runtime(&rebound) ||
       memcmp(&rebound,&NATIVE_HOT_CONFIG.runtime,sizeof(rebound))) fail(15u);
#endif
    struct kui_vmu_native_runtime_snapshot snapshot;
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
    if(issued_plan.source_physical!=state.source || issued_plan.source_bytes!=state.bytes ||
       issued_plan.frame_count!=state.descriptors || issued_plan.output_physical!=issued_physical() ||
       kui_vmu_volatile_chain_verify(&issued_plan,issued_chain,sizeof(issued_chain)) ||
       !(NATIVE_HOT_CONFIG.diagnostics==KUI_VMU_NATIVE_DIAGNOSTICS_RAW_CHAIN?
          live_boundary(f,1u,state.source,state.bytes,&snapshot):filtered_boundary(f,&snapshot)) ||
       !chain(snapshot.bank,true) ||
       !stack_valid() ||
       kui_vmu_native_passive_consumer!=NATIVE_HOT_CONFIG.driver.consumer) fail(0x42u);
    /* Hardware is idle, software-triggered and owns exactly our immutable
     * physical-only chain. Restore MDSTAR before any SD callback or RX store. */
    kui_vmu_native_trace_point(0x21u,f,state.submitted,state.completed);
    if(NATIVE_HOT_CONFIG.diagnostics!=KUI_VMU_NATIVE_DIAGNOSTICS_RAW_CHAIN) write_mdstar(state.source);
    completion_frame=*f;
    if(!persistent_context_guard()) fail(0x43u);
    kui_vmu_native_trace_point(0x22u,f,state.submitted,state.completed);
#else
    if(!boundary(f,1u,state.bytes,&snapshot)) fail(10u);
    if(!chain(snapshot.bank,true) || !lease_valid() || !stack_valid()) fail(11u);
#endif
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
    bool capture_gesture=false;
#endif
    for(unsigned i=0;i<state.descriptors;i++) {
        uint32_t response;
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
        if(((chain_frames[i].header>>8)&255u)==2u) continue;
#endif
        if(!read_value(NULL,receivers[i]|UINT32_C(0xa0000000),4u,&response)) fail(12u);
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
        if(!physical_response_valid(i,response)) fail(0x44u);
        if((!KUI_NATIVE_E1_WAIT_CAPTURE && !KUI_NATIVE_METADATA_CACHE_ISOLATION && !KUI_NATIVE_SPLIT_CONTROLLER_ISOLATION && NATIVE_HOT_CONFIG.diagnostics==KUI_VMU_NATIVE_DIAGNOSTICS_PROFILE) && ((chain_frames[i].header>>8)&255u)==32u &&
           (chain_frames[i].header&255u)==9u) {
            uint32_t controls;
            if(!read_value(NULL,(receivers[i]+8u)|UINT32_C(0xa0000000),4u,&controls)) fail(12u);
            /* KOS Maple raw condition: active-low buttons then right/left
             * trigger bytes. B is bit1; no cooked host-stub masks. This
             * gesture avoids pausing the game or requesting its reset. */
            capture_gesture=diagnostic_gesture(controls);
        }
#endif
        if(!i) state.first_response=response;
        state.last_response=response;
    }
#if !KUI_VMU_NATIVE_PERSISTENT_PROBE
    if(!boundary(f,1u,state.bytes,&snapshot)) fail(13u);
#endif
#if KUI_VMU_NATIVE_READONLY_PROBE || KUI_VMU_NATIVE_PERSISTENT_PROBE
    /* No consumer can run until this function returns. Revalidate immediately
     * before the overlay's all-or-nothing publication to the proved P2 slots. */
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
    if(!persistent_context_guard()) fail(0x43u);
#else
    if(!lease_valid() || !stack_valid() ||
       kui_vmu_native_passive_consumer!=NATIVE_HOT_CONFIG.driver.consumer) fail(22u);
#endif
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
    kui_vmu_native_trace_point(0x23u,f,state.submitted,state.completed);
    int published=(KUI_NATIVE_READER_ONLY_ISOLATION ||
                  NATIVE_HOT_CONFIG.diagnostics==KUI_VMU_NATIVE_DIAGNOSTICS_HOOKS_ONLY ||
                  NATIVE_HOT_CONFIG.diagnostics==KUI_VMU_NATIVE_DIAGNOSTICS_RAW_CHAIN)?
        kui_vmu_readonly_overlay_complete_absent(&overlay):kui_vmu_readonly_overlay_complete(&overlay);
#else
    int published=kui_vmu_readonly_overlay_complete(&overlay);
#endif
    state.overlay_result=(uint32_t)published;if(published) fail(23u);
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
    if(durable_card.faulted || !persistent_context_guard()) fail(24u);
#else
    if(card.faulted || card.commit_attempts || !lease_valid() || !stack_valid() ||
       kui_vmu_native_passive_consumer!=NATIVE_HOT_CONFIG.driver.consumer ||
       !boundary(f,1u,state.bytes,&snapshot)) fail(24u);
#endif
    state.pending=0u;++state.completed;state.error=0u;
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
    /* A durable commit remains visible to the game. Power-off/reboot readback
     * is a hardware test, never inferred from this in-session counter. */
    state.proof=durable_card.commits!=0u;
    kui_vmu_native_trace_point(0x2fu,f,state.submitted,state.completed);
    if((!KUI_NATIVE_E1_WAIT_CAPTURE && !KUI_NATIVE_METADATA_CACHE_ISOLATION && !KUI_NATIVE_SPLIT_CONTROLLER_ISOLATION && NATIVE_HOT_CONFIG.diagnostics==KUI_VMU_NATIVE_DIAGNOSTICS_PROFILE))
        diagnostic_complete(&span,capture_gesture,false);
#else
    if((overlay.stats.devinfo || overlay.stats.allinfo) && overlay.stats.media && overlay.stats.reads) {
        state.proof=1u;report();
    }
#endif
    return;
#else
    state.pending=0u;state.completed=1u;state.error=0u;report();
#endif
}
