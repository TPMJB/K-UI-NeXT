/* SPDX-License-Identifier: GPL-3.0-only */
/* The production native lease controller with authored CE-call and RAM
 * boundaries. No executable bytes or private source image are fixtures. */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kui/armada_ce_stream_native.h"

#define PROCESS_BASE KUI_ARMADA_CE_LIVE_PROCESS_BASE
#define OWNER (PROCESS_BASE + 5u * UINT32_C(0x9c))
#define OWNER_SLOT UINT32_C(0x0c000000)
#define TTB UINT32_C(0x8c1458a0)
#define SLOT_TABLE UINT32_C(0x8c146000)
#define BLOCK_TABLE UINT32_C(0x8c147000)
#define ALLOCATION UINT32_C(0x0c100000)
#define BACKING UINT32_C(0x8c400000)
#define WORKER UINT32_C(0x8cfa8000)
#define ARGUMENT UINT32_C(0x8cfb0000)
#define HANDLE UINT32_C(0x12345671)
#define DEVICE UINT32_C(0x0c250000)

static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)) { \
    fprintf(stderr,"line %d: %s\n",__LINE__,#x); abort(); \
} } while(0)

enum native_target {
    CREATE_THREAD=0x0de01000u, RESUME_THREAD=0x0de01004u,
    SLEEP=0x0de01008u, PRIORITY=0x0de0100cu, WAIT=0x0de01010u,
    CLOSE=0x0de01014u, ALLOC=0x0de01018u, FREE=0x0de0101cu,
    KMODE=0x0de01020u, IFLUSH=0x0de01024u, TICKS=0x0de01028u,
    SET_POSITION=0x0de0102cu, ENTER=0x0de01030u, LEAVE=0x0de01034u
};
struct fake_native {
    uint32_t memory[1024];
    uint32_t process,ttb,ccr,sr,allocation,wait_result,resume_result,owner_handle,owner_slot;
    uint32_t tick_value,page_word,slot_pointer,block_pointer;
    uint32_t fail_target,fail_read,fail_write,change_read;
    unsigned calls,reads,writes,publishes,depth,freed,closed;
    uint32_t target[64],args[64][8],written_address[32];
    bool publish_ok,created;
    bool change_after_alloc,change_after_publish;
};
static struct fake_native model;
static struct kui_armada_ce_stream_native state;
static struct kui_armada_ce_live_stream_api api;

static uint32_t context_ttb(void *ctx) {
    CHECK(ctx==&model);return model.ttb;
}
static uint32_t context_ccr(void *ctx) {
    CHECK(ctx==&model);return model.ccr;
}
static uint32_t context_sr(void *ctx) {
    CHECK(ctx==&model);return model.sr;
}
static bool read32(void *ctx,uint32_t address,uint32_t *out) {
    CHECK(ctx==&model && out);
    uint32_t p1;
    CHECK(kui_armada_ce_ram_table_p1(address,&p1) && p1==address);
    ++model.reads;
    if(model.fail_read==model.reads) return false;
    if(address==KUI_ARMADA_CE_LIVE_CURRENT_PROCESS) *out=model.process;
    else if(address==OWNER+8u) *out=model.owner_handle;
    else if(address==OWNER+12u) *out=model.owner_slot;
    else if(address==TTB+4u*(ALLOCATION>>25)) *out=model.slot_pointer;
    else if(address==SLOT_TABLE+4u*((ALLOCATION>>16)&0x1ffu)) *out=model.block_pointer;
    else if(address==BLOCK_TABLE+12u+4u*((ALLOCATION>>12)&15u)) *out=model.page_word;
    else if(address>=BACKING && address<BACKING+sizeof(model.memory))
        *out=model.memory[(address-BACKING)/4u];
    else if(address>=0x8cf50000u && address<0x8cfd0000u) *out=0u;
    else *out=0u; /* Unmapped authored fixture RAM, never MMIO/P2. */
    if(model.change_read==model.reads) *out^=4u;
    return true;
}
static bool write32(void *ctx,uint32_t address,uint32_t word) {
    CHECK(ctx==&model && !model.created);
    CHECK(address>=BACKING && address<BACKING+KUI_ARMADA_CE_STREAM_VENEER_BYTES && !(address&3u));
    CHECK(model.writes<32u);
    model.written_address[model.writes++]=address;
    if(model.fail_write==model.writes) return false;
    model.memory[(address-BACKING)/4u]=word;
    return true;
}
static bool publish_code(void *ctx,uint32_t address,uint32_t bytes) {
    CHECK(ctx==&model && !model.created);
    CHECK(address==BACKING && bytes==KUI_ARMADA_CE_STREAM_VENEER_PUBLISH_BYTES);
    CHECK(model.writes==KUI_ARMADA_CE_STREAM_VENEER_BYTES/4u);
    ++model.publishes;
    return model.publish_ok;
}

static uint32_t native_call(void *ctx,uint32_t target,const uint32_t args[8]) {
    CHECK(ctx==&model && args && model.calls<64u);
    unsigned at=model.calls++;
    model.target[at]=target;memcpy(model.args[at],args,sizeof(model.args[at]));
    if(target==ALLOC) {
        CHECK(!model.created && !model.depth);
        CHECK(!args[0] && args[1]==4096u && args[2]==0x3000u && args[3]==0x40u);
        if(model.change_after_alloc) model.process=PROCESS_BASE;
        return model.fail_target==ALLOC?0u:model.allocation;
    }
    if(target==FREE) {
        CHECK(args[0]==model.allocation && !model.depth);
        CHECK(!args[1] && args[2]==0x8000u);
        CHECK(!model.created || model.wait_result==0u);
        if(model.fail_target==FREE) return 0u;
        ++model.freed;return 1u;
    }
    if(target==IFLUSH) {
        CHECK(!model.created && model.publishes==1u && !model.depth);
        CHECK(args[0]==api.owner_handle && args[1]==ALLOCATION && args[2]==72u);
        if(model.change_after_publish) model.process=PROCESS_BASE;
        return model.fail_target==IFLUSH?0u:1u;
    }
    if(target==CREATE_THREAD) {
        CHECK(!model.created && model.publishes==1u && !model.depth);
        CHECK(args[2]==ALLOCATION && args[3]==ARGUMENT && args[4]==4u);
        if(model.fail_target==CREATE_THREAD) return 0u;
        model.created=true;return HANDLE;
    }
    if(target==PRIORITY) {
        CHECK(model.created && args[0]==HANDLE && args[1]==KUI_ARMADA_CE_STREAM_PRIORITY);
        return model.fail_target==PRIORITY?0u:1u;
    }
    if(target==RESUME_THREAD) {
        CHECK(model.created && args[0]==HANDLE);
        return model.fail_target==RESUME_THREAD?UINT32_MAX:model.resume_result;
    }
    if(target==WAIT) {
        CHECK(model.created && args[0]==HANDLE && !args[1] && !model.depth);
        return model.fail_target==WAIT?UINT32_MAX:model.wait_result;
    }
    if(target==CLOSE) {
        CHECK(model.created && args[0]==HANDLE && model.wait_result==0u && !model.depth);
        if(model.fail_target==CLOSE) return 0u;
        ++model.closed;return 1u;
    }
    if(target==TICKS) {CHECK(!model.depth);return model.tick_value;}
    if(target==SLEEP) {CHECK(!model.depth);return 0u;}
    if(target==ENTER) {CHECK(args[0]==DEVICE+108u && !model.depth);++model.depth;return 0u;}
    if(target==LEAVE) {CHECK(args[0]==DEVICE+108u && model.depth==1u);--model.depth;return 0u;}
    fprintf(stderr,"unexpected native target %08" PRIx32 "\n",target);abort();
}
static struct kui_armada_ce_stream_native_ops ops={
    &model,native_call,read32,write32,publish_code,context_ttb,context_ccr,context_sr
};
static void reset(void) {
    memset(&state,0,sizeof(state));memset(&model,0,sizeof(model));
    model.process=OWNER;model.ttb=TTB;model.ccr=0x105u;model.sr=0x40000000u;
    model.allocation=ALLOCATION;model.wait_result=258u;model.resume_result=1u;
    model.tick_value=0xfffffffeu;
    model.page_word=(BACKING&0x1ffff000u)|0x37du;
    model.owner_handle=0x87654321u;model.owner_slot=OWNER_SLOT;
    model.slot_pointer=SLOT_TABLE;model.block_pointer=BLOCK_TABLE;model.publish_ok=true;
    api=(struct kui_armada_ce_live_stream_api){
        .owner_process=OWNER,.owner_slot=OWNER_SLOT,.owner_handle=0x87654321u,
        .coredll_module=0x8c150400u,.create_thread=CREATE_THREAD,
        .resume_thread=RESUME_THREAD,.sleep=SLEEP,.set_thread_priority=PRIORITY,
        .wait_single=WAIT,.close_handle=CLOSE,.virtual_alloc=ALLOC,.virtual_free=FREE,
        .set_kmode=KMODE,.flush_instruction_cache=IFLUSH,.get_tick_count=TICKS,
        .buffer_set_position=SET_POSITION,.enter_cs=ENTER,.leave_cs=LEAVE
    };
}
static unsigned calls_to(uint32_t target) {
    unsigned n=0;for(unsigned i=0;i<model.calls;i++) if(model.target[i]==target) ++n;
    return n;
}
static bool start(void) {
    return kui_armada_ce_stream_native_start(&state,&api,WORKER,ARGUMENT,&ops);
}
static void no_thread_cleanup(void) {
    CHECK(!model.created && !state.thread && !state.allocation);
    CHECK(!calls_to(CREATE_THREAD) && !calls_to(PRIORITY) && !calls_to(RESUME_THREAD));
    CHECK(!model.closed);
}
static void retained(void) {
    CHECK(state.thread==HANDLE && state.allocation==ALLOCATION);
    CHECK(!model.closed && !model.freed);
}

static void arguments_and_context(void) {
    reset();CHECK(!kui_armada_ce_stream_native_start(NULL,&api,WORKER,ARGUMENT,&ops));
    CHECK(!kui_armada_ce_stream_native_start(&state,NULL,WORKER,ARGUMENT,&ops));
    CHECK(!kui_armada_ce_stream_native_start(&state,&api,WORKER,ARGUMENT,NULL));
    const uint32_t bad_worker[]={0u,0x8c000000u,WORKER+1u,0xacfa8000u,0x8d000000u};
    for(unsigned i=0;i<sizeof(bad_worker)/sizeof(*bad_worker);i++) {
        reset();CHECK(!kui_armada_ce_stream_native_start(&state,&api,bad_worker[i],ARGUMENT,&ops));
        CHECK(!model.calls && !model.writes);
    }
    const uint32_t bad_argument[]={0u,ARGUMENT+2u,0xacfb0000u,0x0cfb0000u,0xa0800000u};
    for(unsigned i=0;i<sizeof(bad_argument)/sizeof(*bad_argument);i++) {
        reset();CHECK(!kui_armada_ce_stream_native_start(&state,&api,WORKER,bad_argument[i],&ops));
        CHECK(!model.calls && !model.writes);
    }
    for(unsigned i=0;i<sizeof(api)/sizeof(uint32_t);i++) {
        reset();((uint32_t *)&api)[i]=0u;CHECK(!start());CHECK(!model.calls);
    }
    for(unsigned i=4;i<sizeof(api)/sizeof(uint32_t);i++) {
        reset();((uint32_t *)&api)[i]|=1u;CHECK(!start());CHECK(!model.calls);
        reset();((uint32_t *)&api)[i]=0xa0800000u;CHECK(!start());CHECK(!model.calls);
    }
    for(unsigned i=0;i<7u;i++) {
        reset();struct kui_armada_ce_stream_native_ops missing=ops;
        switch(i) {
        case 0:missing.call=NULL;break;case 1:missing.read32=NULL;break;
        case 2:missing.write32=NULL;break;case 3:missing.publish_code=NULL;break;
        case 4:missing.ttb=NULL;break;case 5:missing.ccr=NULL;break;
        default:missing.sr=NULL;break;
        }
        CHECK(!kui_armada_ce_stream_native_start(&state,&api,WORKER,ARGUMENT,&missing));
        CHECK(!model.calls);
    }
    const uint32_t bad_sr[]={0u,0x50000000u,0x60000000u,0x40000010u,0x400000f0u};
    for(unsigned i=0;i<sizeof(bad_sr)/sizeof(*bad_sr);i++) {
        reset();model.sr=bad_sr[i];CHECK(!start());
        CHECK(state.status==KUI_ARMADA_CE_STREAM_NATIVE_CONTEXT && !model.calls);
    }
    reset();model.process=PROCESS_BASE;CHECK(!start());CHECK(!model.calls);
    reset();model.owner_handle^=4u;CHECK(!start());CHECK(!model.calls);
    reset();model.owner_slot^=0x02000000u;CHECK(!start());CHECK(!model.calls);
}

static void admission_and_publication(void) {
    reset();model.fail_target=ALLOC;CHECK(!start());no_thread_cleanup();
    CHECK(state.status==KUI_ARMADA_CE_STREAM_NATIVE_ALLOCATION && !model.freed);
    const uint32_t invalid_alloc[]={ALLOCATION+4u,0x0e100000u,0x8c400000u,
        0xac400000u,OWNER_SLOT,0x0c002000u};
    for(unsigned i=0;i<sizeof(invalid_alloc)/sizeof(*invalid_alloc);i++) {
        reset();model.allocation=invalid_alloc[i];CHECK(!start());
        CHECK(state.allocation==invalid_alloc[i] && !state.allocation_owner);
        CHECK(!model.freed && !model.writes && !model.created);
        CHECK(!kui_armada_ce_stream_native_reap(&state,&ops));CHECK(!model.freed);
    }
    for(unsigned i=0;i<6u;i++) {
        reset();
        switch(i) {
        case 0:model.ttb|=0x20000000u;break;
        case 1:model.slot_pointer=0u;break;
        case 2:model.block_pointer=0u;break;
        case 3:model.page_word&=~1u;break;
        case 4:model.page_word^=0x10u;break;
        default:model.ccr|=0x80u;break;
        }
        CHECK(!start());no_thread_cleanup();CHECK(model.freed==1u && !model.writes);
    }
    reset();model.page_word=0x0cf50000u|0x37du;CHECK(!start());
    reset();model.page_word=0x0c149000u|0x37du;CHECK(!start());
    CHECK(!model.writes && !model.created);
    no_thread_cleanup();CHECK(!model.writes && model.freed==1u);
    reset();CHECK(start());unsigned reads=model.reads;
    for(unsigned i=1u;i<=reads;i++) {
        reset();model.fail_read=i;CHECK(!start());no_thread_cleanup();CHECK(!model.created);
    }
    const unsigned structural_races[]={4u,5u,6u,8u,9u,10u,11u,12u,13u,15u,16u,17u,18u,19u,20u,21u,22u,23u};
    for(unsigned i=0;i<sizeof(structural_races)/sizeof(*structural_races);i++) {
        reset();model.change_read=structural_races[i];CHECK(!start());no_thread_cleanup();
    }
    for(unsigned i=1u;i<=KUI_ARMADA_CE_STREAM_VENEER_BYTES/4u;i++) {
        reset();model.fail_write=i;CHECK(!start());no_thread_cleanup();
        CHECK(model.writes==i && !model.publishes && model.freed==1u);
    }
    reset();model.publish_ok=false;CHECK(!start());no_thread_cleanup();
    CHECK(model.publishes==1u && !calls_to(IFLUSH) && model.freed==1u);
    reset();model.fail_target=IFLUSH;CHECK(!start());no_thread_cleanup();
    CHECK(calls_to(IFLUSH)==1u && model.freed==1u);
    reset();model.fail_target=CREATE_THREAD;CHECK(!start());
    CHECK(!state.thread && !state.allocation && model.freed==1u && !model.created);
    reset();model.change_after_alloc=true;CHECK(!start());
    CHECK(state.allocation==ALLOCATION && !model.freed && !model.writes && !model.created);
    model.process=OWNER;CHECK(kui_armada_ce_stream_native_reap(&state,&ops));CHECK(model.freed==1u);
    reset();model.change_after_publish=true;CHECK(!start());
    CHECK(state.allocation==ALLOCATION && !model.freed && !model.created);
    model.process=OWNER;CHECK(kui_armada_ce_stream_native_reap(&state,&ops));CHECK(model.freed==1u);
}

static void lifecycle_and_locks(void) {
    const uint32_t failures[]={PRIORITY,RESUME_THREAD};
    for(unsigned i=0;i<sizeof(failures)/sizeof(*failures);i++) {
        reset();model.fail_target=failures[i];CHECK(!start());retained();
        CHECK(state.status==(i?KUI_ARMADA_CE_STREAM_NATIVE_RESUME:KUI_ARMADA_CE_STREAM_NATIVE_PRIORITY));
        if(!i) CHECK(!calls_to(RESUME_THREAD));
        CHECK(!state.resumed);CHECK(!kui_armada_ce_stream_native_reap(&state,&ops));retained();
        model.fail_target=0;model.wait_result=0;CHECK(kui_armada_ce_stream_native_reap(&state,&ops));
        CHECK(model.closed==1u && model.freed==1u);
    }
    const uint32_t odd_resume[]={0u,2u,UINT32_MAX};
    for(unsigned i=0;i<sizeof(odd_resume)/sizeof(*odd_resume);i++) {
        reset();model.resume_result=odd_resume[i];CHECK(!start());retained();
        CHECK(state.resumed==(odd_resume[i]!=UINT32_MAX));
        CHECK(!kui_armada_ce_stream_native_reap(&state,&ops));retained();
    }
    reset();CHECK(start());model.wait_result=0;model.fail_target=CLOSE;
    CHECK(!kui_armada_ce_stream_native_reap(&state,&ops));retained();
    CHECK(state.status==KUI_ARMADA_CE_STREAM_NATIVE_CLOSE);
    model.fail_target=FREE;CHECK(!kui_armada_ce_stream_native_reap(&state,&ops));
    CHECK(!state.thread && state.allocation==ALLOCATION && model.closed==1u && !model.freed);
    CHECK(state.status==KUI_ARMADA_CE_STREAM_NATIVE_FREE);
    model.fail_target=0;CHECK(kui_armada_ce_stream_native_reap(&state,&ops));
    CHECK(model.closed==1u && model.freed==1u);
    reset();CHECK(start());unsigned before=model.calls;model.process=PROCESS_BASE;
    CHECK(!kui_armada_ce_stream_native_reap(&state,&ops));CHECK(model.calls==before);retained();
    model.process=OWNER;
    uint32_t ticks=0;CHECK(kui_armada_ce_stream_native_ticks(&state,&ops,&ticks));CHECK(ticks==model.tick_value);
    CHECK(!kui_armada_ce_stream_native_ticks(&state,&ops,NULL));
    CHECK(kui_armada_ce_stream_native_sleep(&state,&ops,0u));
    CHECK(kui_armada_ce_stream_native_sleep(&state,&ops,1000u));
    CHECK(!kui_armada_ce_stream_native_sleep(&state,&ops,1001u));
    const uint32_t bad_device[]={0u,DEVICE+2u,0x02010000u,0xa0800000u,OWNER_SLOT|0x01ffff80u};
    for(unsigned i=0;i<sizeof(bad_device)/sizeof(*bad_device);i++)
        CHECK(!kui_armada_ce_stream_native_write_begin(&state,&ops,bad_device[i]));
    CHECK(kui_armada_ce_stream_native_write_begin(&state,&ops,DEVICE));
    CHECK(state.consumer_cs==DEVICE+108u && model.depth==1u);
    before=model.calls;
    CHECK(!kui_armada_ce_stream_native_write_begin(&state,&ops,DEVICE));
    CHECK(!kui_armada_ce_stream_native_ticks(&state,&ops,&ticks));
    CHECK(!kui_armada_ce_stream_native_sleep(&state,&ops,1u));
    CHECK(!kui_armada_ce_stream_native_reap(&state,&ops));
    CHECK(!start());CHECK(model.calls==before && model.depth==1u);
    model.process=PROCESS_BASE;CHECK(!kui_armada_ce_stream_native_write_end(&state,&ops));
    CHECK(state.consumer_cs==DEVICE+108u && model.depth==1u && model.calls==before);
    model.process=OWNER;CHECK(kui_armada_ce_stream_native_write_end(&state,&ops));
    CHECK(!state.consumer_cs && !model.depth && calls_to(LEAVE)==1u);
    CHECK(!kui_armada_ce_stream_native_write_end(&state,&ops));
    model.wait_result=0;CHECK(kui_armada_ce_stream_native_reap(&state,&ops));
    before=model.calls;CHECK(kui_armada_ce_stream_native_reap(&state,&ops));CHECK(model.calls==before);
}

int main(void) {
    reset();CHECK(start());
    CHECK(state.status==KUI_ARMADA_CE_STREAM_NATIVE_RUNNING && state.resumed);
    CHECK(state.thread==HANDLE && state.allocation==ALLOCATION && state.allocation_p1==BACKING);
    CHECK(model.writes==KUI_ARMADA_CE_STREAM_VENEER_BYTES/4u && model.publishes==1u);
    CHECK(calls_to(ALLOC)==1u && calls_to(IFLUSH)==1u && calls_to(CREATE_THREAD)==1u);
    CHECK(calls_to(PRIORITY)==1u && calls_to(RESUME_THREAD)==1u);
    for(unsigned i=0;i<model.writes;i++) CHECK(model.written_address[i]==BACKING+4u*i);
    for(unsigned i=KUI_ARMADA_CE_STREAM_VENEER_BYTES/4u;i<1024u;i++) CHECK(!model.memory[i]);
    unsigned before=model.calls;CHECK(!start());CHECK(model.calls==before);retained();
    CHECK(!kui_armada_ce_stream_native_reap(&state,&ops));retained();
    model.wait_result=UINT32_MAX;CHECK(!kui_armada_ce_stream_native_reap(&state,&ops));retained();
    model.wait_result=0u;CHECK(kui_armada_ce_stream_native_reap(&state,&ops));
    CHECK(!state.thread && !state.allocation && model.closed==1u && model.freed==1u);
    CHECK(state.status==KUI_ARMADA_CE_STREAM_NATIVE_COMPLETE);
    arguments_and_context();admission_and_publication();lifecycle_and_locks();
    printf("armada native stream: %u checks passed\n",checks);
    return 0;
}

