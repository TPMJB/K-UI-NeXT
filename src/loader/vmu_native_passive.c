/* SPDX-License-Identifier: GPL-3.0-only */
/* Passive one-pair proof, or a separately enabled bounded read-only bridge. */
#include "kui/vmu_native_passive.h"
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
#include "kui/vmu_native_patch.h"
#include "kui/vmu_volatile_chain.h"
#include "sd_reader.h"
#define WORKER_SPAN UINT32_C(0x20000)
#define PAIR_LIMIT 180000u
#else
#define WORKER_SPAN UINT32_C(0x10000)
#define PAIR_LIMIT READONLY_PAIR_LIMIT
#endif
#define GUARD UINT32_C(0x4b535431)
#define TX_WORDS (KUI_VMU_NATIVE_TX_BANK_BYTES/4u)
extern const struct kui_vmu_native_passive_exports kui_vmu_native_worker_exports;
extern uint32_t kui_vmu_native_passive_resume,kui_vmu_native_passive_consumer;
extern uint32_t stack_bottom[] __asm__("__vmu_native_worker_stack_bottom");
extern uint32_t stack_top[] __asm__("__vmu_native_worker_stack_top");
static struct kui_vmu_native_passive_config config;
static struct {
    uint32_t armed,pending,submitted,completed,error,source,bytes,descriptors,nops;
    uint32_t submit_sr,complete_sr,rx_slots,hash,first_rx,first_response,last_response;
    uint32_t fault_at,fault_value,fault_expect,runtime_result;
#if KUI_VMU_NATIVE_READONLY_PROBE || KUI_VMU_NATIVE_PERSISTENT_PROBE
    uint32_t proof,overlay_result;
#endif
} state;
static uint32_t captured[TX_WORDS];
static uint32_t receivers[48];
#if KUI_VMU_NATIVE_READONLY_PROBE || KUI_VMU_NATIVE_PERSISTENT_PROBE
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
static struct kui_vmu_durable durable_card;
static _Alignas(32) uint8_t issued_chain[KUI_VMU_VOLATILE_CHAIN_BYTES];
static struct kui_vmu_volatile_chain_frame chain_frames[KUI_VMU_VOLATILE_CHAIN_FRAMES];
static uint32_t write_payloads[KUI_VMU_VOLATILE_CHAIN_FRAMES][32];
static struct kui_vmu_volatile_chain_plan issued_plan;
static struct kui_vmu_native_passive_frame completion_frame;
static struct kui_vmu_native_live live_plan;
static struct kui_vmu_native_lifetime_range protected_ranges[KUI_VMU_NATIVE_LIFETIME_RANGES];
static unsigned protected_count;
static bool persistent_storage_guard(void);
static bool persistent_context_guard(void);
static bool installed_hooks_valid(void);
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
extern uint32_t kui_vmu_native_passive_test_guard(unsigned);
extern void kui_vmu_native_passive_test_terminal(uint32_t,uint32_t) __attribute__((noreturn));
#ifndef KUI_VMU_NATIVE_TEST_SELF_ADDRESS
#define KUI_VMU_NATIVE_TEST_SELF_ADDRESS UINT32_C(0xac700000)
#endif
#define SELF_ADDRESS KUI_VMU_NATIVE_TEST_SELF_ADDRESS
#define STACK_LOW (SELF_ADDRESS+WORKER_SPAN-UINT32_C(0x1000))
#define STACK_HIGH (SELF_ADDRESS+WORKER_SPAN)
static uint32_t current_sr(void) {return kui_vmu_native_passive_test_sr();}
static uint32_t current_sp(void) {return kui_vmu_native_passive_test_sp();}
static uint32_t guard(unsigned i) {return kui_vmu_native_passive_test_guard(i);}
#if KUI_VMU_NATIVE_READONLY_PROBE || KUI_VMU_NATIVE_PERSISTENT_PROBE
extern uint8_t *kui_vmu_native_passive_test_rx(uint32_t,unsigned);
#endif
#else
#define SELF_ADDRESS ((uint32_t)(uintptr_t)&kui_vmu_native_worker_exports)
#define STACK_LOW ((uint32_t)(uintptr_t)stack_bottom)
#define STACK_HIGH ((uint32_t)(uintptr_t)stack_top)
static uint32_t current_sr(void) {uint32_t r;__asm__ volatile("stc sr,%0":"=r"(r));return r;}
static uint32_t current_sp(void) {uint32_t r;__asm__ volatile("mov r15,%0":"=r"(r));return r;}
static uint32_t guard(unsigned i) {return stack_bottom[i];}
#endif
static uint32_t physical(uint32_t a) {return a&UINT32_C(0x1fffffff);}
static bool ram(uint32_t a,unsigned bytes,unsigned alignment) {
    uint32_t area=a&UINT32_C(0xe0000000),p=physical(a);
    return (area==0u || area==UINT32_C(0x80000000) || area==UINT32_C(0xa0000000)) &&
        !(p&(alignment-1u)) && bytes && p>=UINT32_C(0x0c000000) &&
        p<UINT32_C(0x0d000000) && bytes<=UINT32_C(0x0d000000)-p;
}
static bool read_value(void *unused,uint32_t a,unsigned width,uint32_t *v) {
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
static bool read_heap(void *unused,uint32_t a,uint32_t *v) {return read_value(unused,a,4u,v);}
static bool stack_valid(void) {
    uint32_t sp=current_sp();
    if(((current_sr()>>4)&15u)!=15u || sp<STACK_LOW+256u || sp>=STACK_HIGH) return false;
    for(unsigned i=0;i<16u;i++) if(guard(i)!=GUARD) return false;
    return true;
}
static bool lease_valid(void) {
    RECORD_WORK(heap);
    return kui_vmu_native_heap_lease_valid(&config.lease,read_heap,NULL,1u);
}
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
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
    if(transaction!=kui_vmu_native_persistent_sd_transaction || context!=&durable_card ||
       !persistent_storage_guard()) return KUI_VMU_SD_LEASE_FAULT;
#ifdef KUI_VMU_NATIVE_PASSIVE_HOST_TEST
    extern int kui_vmu_native_persistent_test_lease(kui_vmu_sd_transaction,void *);
    int result=kui_vmu_native_persistent_test_lease(transaction,context);
#else
    int (*lease)(kui_vmu_sd_transaction,void *)=(void *)(uintptr_t)config.sd_lease;
    int result=lease(transaction,context);
#endif
    if(!persistent_storage_guard()) return KUI_VMU_SD_LEASE_FAULT;
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
    return kui_vmu_native_lifetime_release_safe_ranges(&config.lease,read_heap,NULL,1u,0u,
        protected_ranges,protected_count);
}
static bool patch_matches(uint32_t address,uint32_t target) {
    uint16_t patch[6];
    if(!ram(address,12u,2u) || !kui_vmu_native_patch_encode(address,target,patch)) return false;
    unsigned width=(address&3u)?2u:4u;
    for(unsigned i=0;i<12u/width;i++) {
        uint32_t value,expected=patch[i*(width/2u)];
        if(width==4u) expected|=(uint32_t)patch[i*2u+1u]<<16;
        if(!read_value(NULL,(physical(address)+i*width)|UINT32_C(0xa0000000),width,&value) ||
           value!=expected) return false;
    }
    return true;
}
static bool installed_hooks_valid(void) {
    RECORD_WORK(hooks);
    uint32_t value;
    if((config.driver.submit|config.lease.owner.release|config.lease.owner.initializer)&3u) return false;
    if(!patch_matches(config.driver.submit,kui_vmu_native_worker_exports.submit_hook) ||
       !patch_matches(config.lease.owner.release,kui_vmu_native_worker_exports.release_hook) ||
       !patch_matches(config.lease.owner.initializer,config.low_terminal) ||
       !read_value(NULL,physical(config.lease.owner.initializer_slot)|UINT32_C(0xa0000000),4u,&value) ||
       value!=config.low_heap_hook ||
       !read_value(NULL,physical(config.driver.completion_slot)|UINT32_C(0xa0000000),4u,&value) ||
       value!=kui_vmu_native_worker_exports.complete_hook) return false;
    for(unsigned i=0;i<config.exits.count;i++)
        if(!patch_matches(config.exits.entries[i],config.low_terminal)) return false;
    return true;
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
    retail_display_line("AUTOMATIC NATIVE PERSISTENT VMU TEST");
    retail_display_line("VIRTUAL A1 / SCI IMAGE AND JOURNAL");
    retail_display_hex("PHASE",state.proof?15u:16u);
    retail_display_hex("DETAIL",state.error);
    retail_display_values("SUBMITTED COMPLETED READS WRITE PHASES COMMITS",(uint32_t[5]){
        state.submitted,state.completed,durable_card.reads,durable_card.write_phases,durable_card.commits},5u);
    retail_display_values("SD READS BUSY FAULTS LEASE RESULT LAST BLOCK",(uint32_t[5]){
        durable_card.sd_reads,durable_card.sd_busy,durable_card.sd_faults,
        (uint32_t)durable_card.last_lease_result,durable_card.last_block},5u);
    retail_display_values("LAST REQUEST REPLY RX OVERLAY FLAGS",(uint32_t[5]){
        overlay.stats.last_request,overlay.stats.last_reply,overlay.stats.last_rx,state.overlay_result,overlay.stats.flags},5u);
    retail_display_values("AT VALUE EXPECT CHECK LEASE",(uint32_t[5]){
        state.fault_at,state.fault_value,state.fault_expect,state.runtime_result,config.lease.payload},5u);
    retail_display_line("PERSISTENT VMU STOPPED - PHOTO THEN POWER OFF");
#elif KUI_VMU_NATIVE_READONLY_PROBE
    retail_display_line("AUTOMATIC NATIVE READ-ONLY VMU TEST");
    retail_display_line("VIRTUAL A1 / NO SAVE OR STORAGE WRITES");
    retail_display_hex("PHASE",state.proof?13u:14u);
    retail_display_hex("DETAIL",state.error);
    retail_display_values("SUBMITTED COMPLETED RX SLOTS ADVERTISE REPLIES",(uint32_t[5]){
        state.submitted,state.completed,state.rx_slots,overlay.stats.advertisements,overlay.stats.replies},5u);
    retail_display_values("INFO ALLINFO MEDIA READS REFUSED",(uint32_t[5]){
        overlay.stats.devinfo,overlay.stats.allinfo,overlay.stats.media,overlay.stats.reads,card.refused},5u);
    retail_display_values("LAST REQUEST REPLY RX OVERLAY FLAGS",(uint32_t[5]){
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
    retail_display_line("PERSISTENT WRITES USE ADMITTED IMAGE / JOURNAL ONLY");
#else
    retail_display_line("STORAGE WAS READ ONLY");
#endif
    for(;;) __asm__ volatile("nop");
#endif
}
static void fail(uint32_t code) __attribute__((noreturn));
static void fail(uint32_t code) {state.error=code;
#if KUI_VMU_NATIVE_READONLY_PROBE || KUI_VMU_NATIVE_PERSISTENT_PROBE
    state.proof=0u;
#endif
    report();}
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
       c->layout.version!=1u || c->layout.bytes!=sizeof(c->layout) ||
       c->layout.tx_base!=c->tx_base || c->layout.rx_layout>KUI_VMU_NATIVE_RX_EXPLICIT48 ||
       c->lease.lease_bytes!=UINT32_C(0x30000) ||
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
    memcpy(&config,c,sizeof(config));
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
    memset(&live_plan,0,sizeof(live_plan));
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
          config.exits.family!=KUI_VMU_NATIVE_EXIT_PVR_COMPACT) ||
         config.exits.count!=KUI_VMU_NATIVE_EXIT_ENTRIES)) ||
       config.exits.load!=config.driver.load_address || config.exits.image_bytes!=config.driver.image_bytes ||
       config.exits.image_bytes<12u || !ram(config.exits.load,config.exits.image_bytes,4u) ||
       !protect_sdk() ||
       kui_vmu_durable_init(&durable_card,&config.durable,persistent_lease,kui_vmu_native_persistent_sd_transaction) ||
       kui_vmu_readonly_overlay_init_backend(&overlay,persistent_handler,persistent_admit,&durable_card)) {
        memset(&config,0,sizeof(config));return -3;
    }
    for(unsigned i=0;i<config.exits.count;i++) {
        uint32_t entry=config.exits.entries[i];
        if(!ram(entry,12u,2u) || physical(entry)<physical(config.exits.load) ||
           physical(entry)-physical(config.exits.load)>config.exits.image_bytes-12u) {
            memset(&config,0,sizeof(config));return -3;
        }
    }
#else
    if(kui_vmu_readonly_init(&card) || kui_vmu_readonly_overlay_init(&overlay,readonly_handler,&card)) {
        memset(&config,0,sizeof(config));return -3;
    }
#endif
#endif
    state.armed=1u;return 0;
}
#if KUI_VMU_NATIVE_READONLY_PROBE || KUI_VMU_NATIVE_PERSISTENT_PROBE
void kui_vmu_native_readonly_release_check(const struct kui_vmu_native_passive_frame *f) {
    state.fault_at=f?f->r4:0u;state.fault_value=state.fault_at;
    state.fault_expect=config.lease.payload;
    /* Normal game frees can occur while Maple DMA is pending. They may
     * proceed only if their exact allocated chunk cannot touch this lease. */
    if(!state.armed || !f || !stack_valid() ||
       kui_vmu_native_readonly_release_resume!=config.lease.owner.release+20u ||
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
       !installed_hooks_valid() ||
       !kui_vmu_native_lifetime_release_safe_ranges(&config.lease,read_heap,NULL,1u,f->r4,
           protected_ranges,protected_count)) fail(0x30u);
#else
       !kui_vmu_native_lifetime_release_safe(&config.lease,read_heap,NULL,1u,f->r4)) fail(0x30u);
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
            chain_frames[count]=(struct kui_vmu_volatile_chain_frame){control,rx,header,
                words>3u?captured[at+3u]:0u};
            if(destination==1u && (header&255u)==12u && words==37u) {
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
        if(r==KUI_VMU_NATIVE_RUNTIME_INVALID && diagnostic.code) {
            state.fault_at=diagnostic.address;state.fault_value=diagnostic.value;
            state.fault_expect=diagnostic.expected;state.runtime_result=0x10000u|diagnostic.code;
            return false;
        }
    } else r=kui_vmu_native_live_check(&live_plan,&b,boundary_read,NULL,snapshot);
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
    if(destination==1u) return true; /* No A1 packet reached hardware. */
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
/* The full ownership proof is renewed at each external boundary. Between it
 * and MDST/RX publication only reviewed private RAM work runs under IMASK15.
 * Repeat the live MMIO/context witness here, not the complete geometry and
 * free-list walks that were just proved without an intervening game caller. */
static bool transfer_context_valid(const struct kui_vmu_native_passive_frame *f,
    unsigned bank,unsigned phase,uint32_t dma) {
    const uint32_t address[4]={UINT32_C(0xa05f6c04),UINT32_C(0xa05f6c10),
        UINT32_C(0xa05f6c14),UINT32_C(0xa05f6c18)};
    const uint32_t expected[4]={dma,0u,1u,0u};
    unsigned mask=(f->sr>>4)&15u;
    if(!stack_valid() || bank>1u || phase>1u || (mask!=6u && mask!=15u) ||
       state.source!=physical(config.tx_base)+bank*KUI_VMU_NATIVE_TX_BANK_BYTES ||
       (!phase && (f->r12!=1u || !ram(f->r14,4u,4u) ||
        physical(f->r14)!=physical(config.driver.manager_cell))) ||
       (phase && (!ram(f->pr,2u,2u) || physical(f->pr)!=physical(config.driver.completion_call)+4u)) ||
       kui_vmu_native_passive_resume!=config.driver.resume ||
       kui_vmu_native_passive_consumer!=config.driver.consumer) return false;
    for(unsigned i=0;i<4u;i++) {
        uint32_t value;
        if(!read_value(NULL,address[i],4u,&value) || value!=expected[i]) return false;
    }
    /* Recheck live selectors and the lock around an MDSTAR change or scoped
     * storage call. The full geometry proof belongs to this same IMASK15
     * scope; these operations cannot resume game code between witnesses. */
    const uint32_t offsets[6]={0xfc0u,0x10b0u,0x10b4u,0x10b8u,0x10c4u,0x10f4u};
    const uint32_t values[6]={phase?0u:1u,state.source|UINT32_C(0xa0000000),
        bank^phase,bank^phase,0u,UINT32_C(0xa05f6c00)};
    uint32_t manager,lock,value;
    if(!read_value(NULL,config.driver.manager_cell,4u,&manager) ||
       physical(manager)!=physical(config.driver.manager_initial) ||
       !read_value(NULL,config.driver.lock_cell,4u,&lock) ||
       lock!=((physical(config.driver.lock_cell)+4u)|UINT32_C(0xa0000000)) ||
       !read_value(NULL,lock,1u,&value) || value!=0x80u) return false;
    for(unsigned i=0;i<6u;i++)
        if(!read_value(NULL,manager+offsets[i],4u,&value) || value!=values[i]) return false;
    return true;
}
static bool persistent_context_guard(void) {
    unsigned bank=state.source==physical(config.tx_base)?0u:1u;
    return state.armed && state.pending && state.submitted==state.completed+1u &&
        transfer_context_valid(&completion_frame,bank,1u,state.source);
}
#endif
void kui_vmu_native_passive_submit(const struct kui_vmu_native_passive_frame *f) {
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
    if(!ram(f->r14,4u,4u) || physical(f->r14)!=physical(config.driver.manager_cell) || f->r12!=1u) fail(2u);
    if(!lease_valid() || kui_vmu_native_passive_resume!=config.driver.resume ||
       kui_vmu_native_passive_consumer!=config.driver.consumer) fail(3u);
#if !KUI_VMU_NATIVE_PERSISTENT_PROBE
    if(!bind_runtime(&config.runtime)) fail(14u);
#endif
    uint32_t source;
    if(!read_value(NULL,UINT32_C(0xa05f6c04),4u,&source) || source!=physical(source) ||
       !ram(source,KUI_VMU_NATIVE_TX_BANK_BYTES,32u)) fail(4u);
    state.source=source;
    struct kui_vmu_native_runtime_snapshot snapshot;
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
    if(!live_boundary(f,0u,state.source,12u,&snapshot)) fail(5u);
#else
    if(!boundary(f,0u,12u,&snapshot)) fail(5u);
#endif
    if(!chain(snapshot.bank,false)) fail(6u);
#if KUI_VMU_NATIVE_READONLY_PROBE || KUI_VMU_NATIVE_PERSISTENT_PROBE
    int prepared=kui_vmu_readonly_overlay_prepare(&overlay,overlay_frames,state.descriptors,
        state.source,state.bytes,physical(config.lease.payload)-32u,config.lease.lease_bytes+32u);
    state.overlay_result=(uint32_t)prepared;if(prepared) fail(21u);
#endif
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
    /* Both builders touch private RAM only; the admission callback is the
     * reviewed durable metadata whitelist. No game/SD call intervenes between
     * the full submission proof above and the final MDST-context witness. */
    uint32_t target=issued_physical();
    if(!target || kui_vmu_volatile_chain_build(&issued_plan,chain_frames,state.descriptors,
       state.source,state.bytes,issued_chain,sizeof(issued_chain),target) ||
       kui_vmu_volatile_chain_verify(&issued_plan,issued_chain,sizeof(issued_chain)) ||
       !transfer_context_valid(f,snapshot.bank,0u,state.source)) fail(0x40u);
    write_mdstar(target);
    if(!transfer_context_valid(f,snapshot.bank,0u,target)) fail(0x41u);
#else
    if(!boundary(f,0u,state.bytes,&snapshot) || !lease_valid() || !stack_valid()) fail(7u);
#endif
    state.rx_slots=snapshot.rx_slots_nonzero;state.pending=1u;
#if KUI_VMU_NATIVE_READONLY_PROBE || KUI_VMU_NATIVE_PERSISTENT_PROBE
    ++state.submitted;
#else
    state.submitted=1u;
#endif
}
void kui_vmu_native_passive_complete(const struct kui_vmu_native_passive_frame *f) {
#if KUI_VMU_NATIVE_READONLY_PROBE || KUI_VMU_NATIVE_PERSISTENT_PROBE
    if(!state.armed || !state.pending || state.submitted!=state.completed+1u || !f || !stack_valid()) fail(8u);
#else
    if(!state.armed || !state.pending || state.submitted!=1u || state.completed || !f || !stack_valid()) fail(8u);
#endif
    state.complete_sr=f->sr;
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
    if(!installed_hooks_valid()) fail(0x45u);
#endif
    if(!lease_valid()) fail(9u);
#if !KUI_VMU_NATIVE_PERSISTENT_PROBE
    struct kui_vmu_native_runtime_config rebound=config.runtime;
    if(!bind_runtime(&rebound) ||
       memcmp(&rebound,&config.runtime,sizeof(rebound))) fail(15u);
#endif
    struct kui_vmu_native_runtime_snapshot snapshot;
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
    if(issued_plan.source_physical!=state.source || issued_plan.source_bytes!=state.bytes ||
       issued_plan.frame_count!=state.descriptors || issued_plan.output_physical!=issued_physical() ||
       kui_vmu_volatile_chain_verify(&issued_plan,issued_chain,sizeof(issued_chain)) ||
       !filtered_boundary(f,&snapshot) || !chain(snapshot.bank,true) ||
       !stack_valid() ||
       kui_vmu_native_passive_consumer!=config.driver.consumer) fail(0x42u);
    /* Hardware is idle, software-triggered and owns exactly our immutable
     * physical-only chain. Restore MDSTAR before any SD callback or RX store. */
    write_mdstar(state.source);
    completion_frame=*f;
    if(!persistent_context_guard()) fail(0x43u);
#else
    if(!boundary(f,1u,state.bytes,&snapshot)) fail(10u);
    if(!chain(snapshot.bank,true) || !lease_valid() || !stack_valid()) fail(11u);
#endif
    for(unsigned i=0;i<state.descriptors;i++) {
        uint32_t response;
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
        if(((chain_frames[i].header>>8)&255u)==1u) continue;
#endif
        if(!read_value(NULL,receivers[i]|UINT32_C(0xa0000000),4u,&response)) fail(12u);
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
        if(!physical_response_valid(i,response)) fail(0x44u);
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
       kui_vmu_native_passive_consumer!=config.driver.consumer) fail(22u);
#endif
    int published=kui_vmu_readonly_overlay_complete(&overlay);
    state.overlay_result=(uint32_t)published;if(published) fail(23u);
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
    if(durable_card.faulted || !persistent_context_guard()) fail(24u);
#else
    if(card.faulted || card.commit_attempts || !lease_valid() || !stack_valid() ||
       kui_vmu_native_passive_consumer!=config.driver.consumer ||
       !boundary(f,1u,state.bytes,&snapshot)) fail(24u);
#endif
    state.pending=0u;++state.completed;state.error=0u;
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
    /* A durable commit remains visible to the game. Power-off/reboot readback
     * is a hardware test, never inferred from this in-session counter. */
    state.proof=durable_card.commits!=0u;
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
