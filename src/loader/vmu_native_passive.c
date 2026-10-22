/* SPDX-License-Identifier: GPL-3.0-only */
/* Passive one-pair proof, or a separately enabled bounded read-only bridge. */
#include "kui/vmu_native_passive.h"
#include "retail_display.h"
#include <string.h>
#if KUI_VMU_NATIVE_READONLY_PROBE
#include "kui/vmu_readonly.h"
#include "kui/vmu_readonly_overlay.h"
#include "kui/vmu_native_lifetime.h"
#define READONLY_PAIR_LIMIT 3600u
extern uint32_t kui_vmu_native_readonly_release_resume;
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
#if KUI_VMU_NATIVE_READONLY_PROBE
    uint32_t proof,overlay_result;
#endif
} state;
static uint32_t captured[TX_WORDS];
static uint32_t receivers[48];
#if KUI_VMU_NATIVE_READONLY_PROBE
static struct kui_vmu_readonly card;
static struct kui_vmu_readonly_overlay overlay;
static struct kui_vmu_readonly_overlay_frame overlay_frames[KUI_VMU_READONLY_OVERLAY_FRAMES];
static int readonly_handler(void *context,const uint8_t *request,size_t request_bytes,
    uint8_t *response,size_t capacity,size_t *response_bytes) {
    return kui_vmu_readonly_handle(context,request,request_bytes,response,capacity,response_bytes);
}
#endif
#ifdef KUI_VMU_NATIVE_PASSIVE_HOST_TEST
extern bool kui_vmu_native_passive_test_read(uint32_t,unsigned,uint32_t *);
extern uint32_t kui_vmu_native_passive_test_sr(void);
extern uint32_t kui_vmu_native_passive_test_sp(void);
extern uint32_t kui_vmu_native_passive_test_guard(unsigned);
extern void kui_vmu_native_passive_test_terminal(uint32_t,uint32_t) __attribute__((noreturn));
#define SELF_ADDRESS UINT32_C(0xac700000)
#define STACK_LOW UINT32_C(0xac70f000)
#define STACK_HIGH UINT32_C(0xac710000)
static uint32_t current_sr(void) {return kui_vmu_native_passive_test_sr();}
static uint32_t current_sp(void) {return kui_vmu_native_passive_test_sp();}
static uint32_t guard(unsigned i) {return kui_vmu_native_passive_test_guard(i);}
#if KUI_VMU_NATIVE_READONLY_PROBE
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
    if(width!=1u && width!=4u) return false;
    if(!ram(a,width,width) && !(width==4u &&
       (a==UINT32_C(0xa05f6c04) || a==UINT32_C(0xa05f6c10) ||
        a==UINT32_C(0xa05f6c14) || a==UINT32_C(0xa05f6c18)))) return false;
#ifdef KUI_VMU_NATIVE_PASSIVE_HOST_TEST
    return kui_vmu_native_passive_test_read(a,width,v);
#else
    *v=width==1u?*(volatile const uint8_t *)(uintptr_t)a:*(volatile const uint32_t *)(uintptr_t)a;
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
    return kui_vmu_native_heap_lease_valid(&config.lease,read_heap,NULL,1u);
}
#if KUI_VMU_NATIVE_READONLY_PROBE
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
#if KUI_VMU_NATIVE_READONLY_PROBE
    kui_vmu_native_passive_test_terminal(state.proof?13u:14u,state.error);
#else
    kui_vmu_native_passive_test_terminal(state.completed?11u:12u,state.error);
#endif
#else
    struct retail_display_state display;memcpy(display.regs,config.display,sizeof(display.regs));
    retail_display_restore(&display);
#if KUI_VMU_NATIVE_READONLY_PROBE
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
    retail_display_line("STORAGE WAS READ ONLY");
    for(;;) __asm__ volatile("nop");
#endif
}
static void fail(uint32_t code) __attribute__((noreturn));
static void fail(uint32_t code) {state.error=code;
#if KUI_VMU_NATIVE_READONLY_PROBE
    state.proof=0u;
#endif
    report();}
void kui_vmu_native_passive_report(void) {if(!state.error) state.error=UINT32_C(0x100);
#if KUI_VMU_NATIVE_READONLY_PROBE
    state.proof=0u;
#else
    state.completed=0u;
#endif
    report();}
int kui_vmu_native_passive_arm(const struct kui_vmu_native_passive_config *c,void *unused) {
    (void)unused;
    if(!c || c->version!=1u || c->bytes!=sizeof(*c) || state.armed || state.pending ||
       c->driver.version!=1u || c->driver.bytes!=sizeof(c->driver) ||
       c->driver.family!=KUI_VMU_NATIVE_FAMILY_UNIFORM_V1 || c->driver.requirements!=KUI_VMU_NATIVE_REQUIREMENTS ||
       c->layout.version!=1u || c->layout.bytes!=sizeof(c->layout) ||
       c->layout.tx_base!=c->tx_base || c->layout.rx_layout!=KUI_VMU_NATIVE_RX_EXPLICIT48 ||
       c->lease.lease_bytes!=UINT32_C(0x30000) || c->lease.payload!=physical(c->lease.payload) ||
       !ram(c->lease.payload,c->lease.lease_bytes,32u) ||
       SELF_ADDRESS!=(c->lease.payload|UINT32_C(0xa0000000)) ||
       STACK_LOW!=SELF_ADDRESS+UINT32_C(0xf000) || STACK_HIGH!=SELF_ADDRESS+UINT32_C(0x10000) ||
       !stack_valid() || c->runtime.version!=1u || c->runtime.bytes!=sizeof(c->runtime) ||
       physical(c->runtime.lease_address)!=c->lease.payload || c->runtime.lease_bytes!=c->lease.lease_bytes ||
       c->runtime.lease_header_bytes!=32u || c->runtime.required_lease_bytes<UINT32_C(0x10000) ||
       c->runtime.required_lease_bytes>c->runtime.lease_bytes ||
       c->runtime.rx_layout!=KUI_VMU_NATIVE_RX_EXPLICIT48 ||
       !ram(c->tx_base,UINT32_C(0x6000),32u) || (c->tx_base&UINT32_C(0xe0000000))!=UINT32_C(0xa0000000) ||
       kui_vmu_native_passive_resume!=c->driver.resume || kui_vmu_native_passive_consumer!=c->driver.consumer)
        return -1;
    /* The stage-owned input object is copied, not retained. */
    memcpy(&config,c,sizeof(config));
    if(!lease_valid()) {memset(&config,0,sizeof(config));return -2;}
#if KUI_VMU_NATIVE_READONLY_PROBE
    if(kui_vmu_native_readonly_release_resume!=config.lease.owner.release+20u) {
        memset(&config,0,sizeof(config));return -3;
    }
    /* Initialize callbacks only after relocation, at their final P2 addresses. */
    if(kui_vmu_readonly_init(&card) || kui_vmu_readonly_overlay_init(&overlay,readonly_handler,&card)) {
        memset(&config,0,sizeof(config));return -3;
    }
#endif
    state.armed=1u;return 0;
}
#if KUI_VMU_NATIVE_READONLY_PROBE
void kui_vmu_native_readonly_release_check(const struct kui_vmu_native_passive_frame *f) {
    state.fault_at=f?f->r4:0u;state.fault_value=state.fault_at;
    state.fault_expect=config.lease.payload;
    /* Normal game frees can occur while Maple DMA is pending. They may
     * proceed only if their exact allocated chunk cannot touch this lease. */
    if(!state.armed || !f || !stack_valid() ||
       kui_vmu_native_readonly_release_resume!=config.lease.owner.release+20u ||
       !kui_vmu_native_lifetime_release_safe(&config.lease,read_heap,NULL,1u,f->r4)) fail(0x30u);
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
    struct kui_vmu_native_runtime_boundary b={phase,current_sr(),f->sr,f->pr,state.source,state.source,bytes};
    int r=kui_vmu_native_runtime_check(&config.driver,&config.runtime,&b,boundary_read,NULL,snapshot);
    state.runtime_result=(uint32_t)r;
    return r==KUI_VMU_NATIVE_RUNTIME_VALID && snapshot->tx_base==config.tx_base;
}
static bool bind_runtime(struct kui_vmu_native_runtime_config *runtime) {
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
    unsigned at=0u,count=0u,nops=0u;uint32_t hash=UINT32_C(2166136261);
    for(unsigned entry=0;entry<256u && at<TX_WORDS;entry++) {
        uint32_t control;
        if(!read_value(NULL,(state.source+at*4u)|UINT32_C(0xa0000000),4u,&control)) return false;
        unsigned words=control==UINT32_C(0x700)?1u:3u+(control&255u);
        state.fault_at=state.source+at*4u;state.fault_value=control;
        if(words>TX_WORDS-at || (control!=UINT32_C(0x700) && (control&~UINT32_C(0x800300ff)))) return false;
        for(unsigned i=0;i<words;i++) {
            uint32_t value;
            if(!read_value(NULL,(state.source+(at+i)*4u)|UINT32_C(0xa0000000),4u,&value)) return false;
            if(compare) {if(value!=captured[at+i]) return false;} else captured[at+i]=value;
            hash=(hash^value)*UINT32_C(16777619);
        }
        if(control==UINT32_C(0x700)) {++nops;at+=words;continue;}
#if KUI_VMU_NATIVE_READONLY_PROBE
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
        uint32_t begin=config.lease.payload-32u,end=config.lease.payload+config.lease.lease_bytes;
        if(rx<end && begin<rx+1024u) return false;
        for(unsigned i=0;i<count;i++) if(receivers[i]==rx) return false;
        if(compare) {if(receivers[count]!=rx) return false;} else receivers[count]=rx;
#if KUI_VMU_NATIVE_READONLY_PROBE
        if(!compare) {
            uint8_t *mapped=readonly_rx(rx,1024u);if(!mapped) return false;
            overlay_frames[count]=(struct kui_vmu_readonly_overlay_frame){
                .header=header,.payload0=words>3u?captured[at+3u]:0u,
                .payload1=words>4u?captured[at+4u]:0u,.rx_physical=rx,
                .rx_capacity=1024u,.rx_bytes=mapped,.write_payload=NULL};
        }
#endif
        ++count;at+=words;
        if(control&UINT32_C(0x80000000)) {
            if(compare) return at*4u==state.bytes && count==state.descriptors && nops==state.nops && hash==state.hash;
            state.bytes=at*4u;state.descriptors=count;state.nops=nops;state.hash=hash;
            state.first_rx=receivers[0];return true;
        }
    }
    return false;
}
void kui_vmu_native_passive_submit(const struct kui_vmu_native_passive_frame *f) {
#if KUI_VMU_NATIVE_READONLY_PROBE
    if(!state.armed || state.pending || state.submitted!=state.completed || !f || !stack_valid()) fail(1u);
    if(state.submitted>=READONLY_PAIR_LIMIT) fail(20u);
#else
    if(!state.armed || state.pending || state.submitted || !f || !stack_valid()) fail(1u);
#endif
    state.submit_sr=f->sr;
    if(!ram(f->r14,4u,4u) || physical(f->r14)!=physical(config.driver.manager_cell) || f->r12!=1u) fail(2u);
    if(!lease_valid() || kui_vmu_native_passive_resume!=config.driver.resume ||
       kui_vmu_native_passive_consumer!=config.driver.consumer) fail(3u);
    if(!bind_runtime(&config.runtime)) fail(14u);
    uint32_t source;
    if(!read_value(NULL,UINT32_C(0xa05f6c04),4u,&source) || source!=physical(source) ||
       !ram(source,KUI_VMU_NATIVE_TX_BANK_BYTES,32u)) fail(4u);
    state.source=source;
    struct kui_vmu_native_runtime_snapshot snapshot;
    if(!boundary(f,0u,12u,&snapshot)) fail(5u);
    if(!chain(snapshot.bank,false)) fail(6u);
#if KUI_VMU_NATIVE_READONLY_PROBE
    int prepared=kui_vmu_readonly_overlay_prepare(&overlay,overlay_frames,state.descriptors,
        state.source,state.bytes,config.lease.payload-32u,config.lease.lease_bytes+32u);
    state.overlay_result=(uint32_t)prepared;if(prepared) fail(21u);
#endif
    if(!boundary(f,0u,state.bytes,&snapshot) || !lease_valid() || !stack_valid()) fail(7u);
    state.rx_slots=snapshot.rx_slots_nonzero;state.pending=1u;
#if KUI_VMU_NATIVE_READONLY_PROBE
    ++state.submitted;
#else
    state.submitted=1u;
#endif
}
void kui_vmu_native_passive_complete(const struct kui_vmu_native_passive_frame *f) {
#if KUI_VMU_NATIVE_READONLY_PROBE
    if(!state.armed || !state.pending || state.submitted!=state.completed+1u || !f || !stack_valid()) fail(8u);
#else
    if(!state.armed || !state.pending || state.submitted!=1u || state.completed || !f || !stack_valid()) fail(8u);
#endif
    state.complete_sr=f->sr;
    if(!lease_valid()) fail(9u);
    struct kui_vmu_native_runtime_config rebound=config.runtime;
    if(!bind_runtime(&rebound) ||
       memcmp(&rebound,&config.runtime,sizeof(rebound))) fail(15u);
    struct kui_vmu_native_runtime_snapshot snapshot;
    if(!boundary(f,1u,state.bytes,&snapshot)) fail(10u);
    if(!chain(snapshot.bank,true) || !lease_valid() || !stack_valid()) fail(11u);
    for(unsigned i=0;i<state.descriptors;i++) {
        uint32_t response;
        if(!read_value(NULL,receivers[i]|UINT32_C(0xa0000000),4u,&response)) fail(12u);
        if(!i) state.first_response=response;
        state.last_response=response;
    }
    if(!boundary(f,1u,state.bytes,&snapshot)) fail(13u);
#if KUI_VMU_NATIVE_READONLY_PROBE
    /* No consumer can run until this function returns. Revalidate immediately
     * before the overlay's all-or-nothing publication to the proved P2 slots. */
    if(!lease_valid() || !stack_valid() ||
       kui_vmu_native_passive_consumer!=config.driver.consumer) fail(22u);
    int published=kui_vmu_readonly_overlay_complete(&overlay);
    state.overlay_result=(uint32_t)published;if(published) fail(23u);
    if(card.faulted || card.commit_attempts || !lease_valid() || !stack_valid() ||
       kui_vmu_native_passive_consumer!=config.driver.consumer ||
       !boundary(f,1u,state.bytes,&snapshot)) fail(24u);
    state.pending=0u;++state.completed;state.error=0u;
    if((overlay.stats.devinfo || overlay.stats.allinfo) && overlay.stats.media && overlay.stats.reads) {
        state.proof=1u;report();
    }
    return;
#else
    state.pending=0u;state.completed=1u;state.error=0u;report();
#endif
}
