/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/armada_ce_maple_rx_slice.h"
#include "kui/armada_ce_maple_fence.h"
#include "kui/armada_ce_vmu_a2_publish.h"
#include "kui/armada_ce_vmu_runtime.h"
#include "kui/armada_ce_maple_continuation.h"
#include "kui/armada_ce_maple_auto.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

#define RAW UINT32_C(0x30a60)
#define THREAD UINT32_C(0x8c080000)
#define PROCESS UINT32_C(0x8c14e6e0)
#define TTB UINT32_C(0x8c090000)
#define SR UINT32_C(0x40008000)
static uint32_t platform[8];
static uint32_t test_sr;
static unsigned register_index(uint32_t at) {
    switch(at) {case 0x8c145894u:return 0;case 0x8c145890u:return 1;
    case 0xff000008u:return 2;case 0xff00001cu:return 3;
    case 0xac0000e0u:return 4;case 0xacfcff80u:return 5;
    case 0xacfcff78u:return 6;case 0xacfcff7cu:return 7;default:assert(0);return 0;}
}
#define REG(at) platform[register_index(at)]
static uint32_t test_register(uint32_t at) {return REG(at);}
static unsigned checks;
static jmp_buf auto_transfer;
static bool auto_transfer_ready;
#define CHECK(c) do {++checks;assert(c);} while(0)

struct kui_armada_ce_maple_traffic_snapshot kui_armada_ce_maple_hook_log;
struct kui_armada_ce_maple_fence_snapshot kui_armada_ce_maple_fence_log;
struct kui_armada_ce_vmu_a2_publish_snapshot kui_armada_ce_vmu_a2_log;
volatile uint32_t kui_armada_ce_maple_traffic_freeze;
volatile uint32_t kui_armada_ce_maple_proof_initial,kui_armada_ce_maple_proof_final;
const volatile uint32_t kui_armada_ce_maple_diagnostic_mode=0;
static struct kui_armada_ce_maple_traffic_scratch traffic_scratch;
static struct kui_armada_ce_maple_fence_state fence_state;
static struct kui_armada_ce_vmu_a2_publish_scratch a2_staging;
static struct kui_armada_ce_vmu_a2_input a2_input;
static struct kui_armada_ce_maple_rx_slice_scratch a2_query;
static struct kui_armada_ce_maple_rx_slice a2_slice[2];
uint32_t kui_armada_ce_maple_bridge_return;
typedef uint32_t a2_copy_word __attribute__((__may_alias__));
static struct {
    uint32_t ttb,process,ccr,records,index[2],needed[2],parent,target,target_bytes;
} a2_context;

static struct {
    struct kui_armada_ce_maple_traffic_scratch capture;
    struct kui_armada_ce_maple_rx_slice slice[2];
    struct kui_armada_ce_maple_mapping owner_map;
    uint8_t payload[8];
    uint32_t header,shutdown,handle;
    unsigned visits,queries,pre,post,bind,backend,stores,admissions;
    unsigned capture_result,query_result,pre_result,post_result,post_stuck;
    unsigned map_bad,cache_bad,thread_change,job_done,error,bind_error,admit_error,waits,wait_result,auto_stops;
    uint32_t stored_header,parent_function;
    uint32_t mmio[4],mmio_read_fail;
    unsigned mmio_reads,enable_writes,disable_writes,dma_runs,irq_status,irq_enabled,event,consumers;
    uint32_t controller_payload[3],a1_payload[32];
} f;

static unsigned mmio_index(uint32_t at) {
    switch(at) {
    case KUI_ARMADA_CE_MAPLE_FENCE_MDEN:return 0;
    case KUI_ARMADA_CE_MAPLE_FENCE_MDST:return 1;
    case KUI_ARMADA_CE_MAPLE_FENCE_MDSTAR:return 2;
    case KUI_ARMADA_CE_MAPLE_FENCE_MDTSEL:return 3;
    default:CHECK(0);return 0;
    }
}

static void put32(uint8_t *p,uint32_t value) {
    for(unsigned i=0;i<4u;i++) p[i]=(uint8_t)(value>>(8u*i));
}
static bool traffic_read_p1(void *ctx,uint32_t at,uint32_t *value) {
    (void)ctx;(void)at;*value=0;return true;
}
static bool traffic_read_cache(void *ctx,uint32_t at,uint32_t *value) {
    return traffic_read_p1(ctx,at,value);
}
static bool traffic_read_p2(void *ctx,uint32_t at,uint32_t *value) {
    (void)ctx;if(at!=((f.capture.word[6]+4u)|0xa0000000u)) return false;
    *value=f.parent_function;return true;
}
static bool fence_read_mmio(void *ctx,uint32_t at,uint32_t *value) {
    CHECK(!ctx);++f.mmio_reads;
    if(f.mmio_read_fail==at) return false;
    *value=f.mmio[mmio_index(at)];return true;
}
static bool fence_write_mmio(void *ctx,uint32_t at,uint32_t value) {
    CHECK(!ctx && at==KUI_ARMADA_CE_MAPLE_FENCE_MDEN && value<=1u);
    f.mmio[0]=value;
    if(value) ++f.enable_writes;else ++f.disable_writes;
    return true;
}
/* Hardware has one coalesced normal-status latch, not an event FIFO. The
 * source IRQ handler masks SYSINTR18 delivery, while InterruptDone enables
 * it after the consumer returns. A fresh VBlank can still run the same DMA
 * descriptor while MDEN1 and set a new status bit during that masked gap. */
static void dispatch_irq(void) {
    if(f.irq_enabled && f.irq_status) {f.event=1u;f.irq_enabled=0u;}
}
static void vblank(void) {
    if(!f.mmio[0]) return;
    CHECK(f.mmio[0]==1u && !f.mmio[1] && f.mmio[3]==1u);
    ++f.dma_runs;f.irq_status=1u;
    f.capture.row[0][4]=f.slice[0].rx_header=0x03310008u;
    f.capture.row[1][4]=f.slice[1].rx_header=UINT32_MAX;
    f.parent_function=0x01000000u;dispatch_irq();
}
static enum kui_armada_ce_maple_traffic_status fake_visit(uint32_t raw,uint32_t ttb,
    uint32_t process,uint32_t ccr,kui_armada_ce_ram_read32 p1,kui_armada_ce_ram_read32 cache,
    kui_armada_ce_ram_read32 p2,void *ctx,struct kui_armada_ce_maple_traffic_scratch *w,
    struct kui_armada_ce_maple_traffic_snapshot *log) {
    CHECK(raw==RAW && p1 && cache && p2 && !ctx && log==&kui_armada_ce_maple_hook_log);
    (void)ttb;(void)process;(void)ccr;++f.visits;*w=f.capture;
    return (enum kui_armada_ce_maple_traffic_status)f.capture_result;
}
static enum kui_armada_ce_ram_bind_status fake_resolve(uint32_t ttb,uint32_t va,
    kui_armada_ce_ram_read32 read,void *ctx,struct kui_armada_ce_maple_mapping *out) {
    CHECK(ttb==TTB && read && !ctx);
    if(va==0x02060c00u) {
        memset(out,0,sizeof(*out));out->page_word=0x005f6001u;
        return KUI_ARMADA_CE_RAM_BIND_DESTINATION;
    }
    CHECK(va==f.owner_map.virtual_address);*out=f.owner_map;
    if(f.map_bad) out->page_word^=0x1000u;
    return KUI_ARMADA_CE_RAM_BIND_OK;
}
static enum kui_armada_ce_ram_bind_status fake_cache_line(uint32_t va,uint32_t p1,
    uint32_t page,uint32_t ccr,uint32_t mask,kui_armada_ce_ram_read32 read,void *ctx,uint32_t out[8]) {
    unsigned offset=va==(f.owner_map.virtual_address&~31u)?0u:0x178u;
    CHECK(va==((f.owner_map.virtual_address+offset)&~31u) && p1==((f.owner_map.physical_address+offset)&~31u));
    CHECK(page==f.owner_map.page_word && ccr==1u && read==kui_armada_ce_maple_traffic_cache && ctx);
    CHECK(mask==(1u<<(((f.owner_map.virtual_address+offset)&31u)/4u)));
    for(unsigned i=0;i<8u;i++) out[i]=offset?f.handle:f.shutdown;
    if(f.thread_change) {REG(0x8c145894u)^=4u;f.thread_change=0;}
    return f.cache_bad?KUI_ARMADA_CE_RAM_BIND_CACHE:KUI_ARMADA_CE_RAM_BIND_OK;
}
static unsigned fake_wait(uint32_t handle,uint32_t milliseconds) {
    CHECK(handle==f.handle && milliseconds==1u && test_sr==SR);
    CHECK(fence_state.pending && fence_state.active && !f.stores);
    ++f.waits;vblank();return f.wait_result;
}
static unsigned (*fake_wait_boundary(uint32_t at))(uint32_t,uint32_t) {
    CHECK(at==kui_armada_ce_maple_bridge_return-0x4b2u);return fake_wait;
}
uint32_t kui_armada_ce_maple_native_wait(uint32_t entry,uint32_t handle,uint32_t milliseconds) {
    return fake_wait_boundary(entry)(handle,milliseconds);
}
static void kui_armada_ce_maple_auto_stop(void) __attribute__((noreturn));
static void kui_armada_ce_maple_auto_stop(void) {
    CHECK(auto_transfer_ready && !f.stores && !fence_state.active && !fence_state.pending);
    ++f.auto_stops;longjmp(auto_transfer,1);
}
static enum kui_armada_ce_maple_fence_result fake_pre(uint32_t raw,uint32_t tx,uint32_t mode,
    bool admitted,kui_armada_ce_maple_fence_read32 read,kui_armada_ce_maple_fence_write32 write,
    void *ctx,struct kui_armada_ce_maple_fence_state *state,struct kui_armada_ce_maple_fence_snapshot *log) {
    CHECK(raw==RAW && tx==f.capture.word[1] && mode==0 && read && write && !ctx);
    CHECK(log==&kui_armada_ce_maple_fence_log && !state->pending && !state->active);++f.pre;
    if(f.pre_result==KUI_ARMADA_CE_MAPLE_FENCE_SKIPPED) f.mmio[1]=1u;
    enum kui_armada_ce_maple_fence_result result=kui_armada_ce_maple_fence_pre(
        raw,tx,mode,admitted,read,write,ctx,state,log);
    return f.pre_result?(enum kui_armada_ce_maple_fence_result)f.pre_result:result;
}
static enum kui_armada_ce_maple_fence_result fake_post(uint32_t raw,uint32_t mode,
    kui_armada_ce_maple_fence_read32 read,kui_armada_ce_maple_fence_write32 write,void *ctx,
    struct kui_armada_ce_maple_fence_state *state,struct kui_armada_ce_maple_fence_snapshot *log) {
    CHECK(read && write && !ctx && log==&kui_armada_ce_maple_fence_log);
    CHECK(state->pending || state->active);++f.post;
    if(f.post_stuck) return KUI_ARMADA_CE_MAPLE_FENCE_RESTORE;
    enum kui_armada_ce_maple_fence_result result=kui_armada_ce_maple_fence_post(
        raw,mode,read,write,ctx,state,log);
    return f.post_result?(enum kui_armada_ce_maple_fence_result)f.post_result:result;
}
static enum kui_armada_ce_maple_rx_slice_status fake_query(
    const struct kui_armada_ce_maple_traffic_scratch *capture,unsigned records,unsigned index,
    uint32_t ttb,uint32_t process,uint32_t ccr,uint32_t min_bytes,
    kui_armada_ce_ram_read32 p1,kui_armada_ce_ram_read32 cache,kui_armada_ce_ram_read32 p2,void *ctx,
    struct kui_armada_ce_maple_rx_slice_scratch *scratch,struct kui_armada_ce_maple_rx_slice *out) {
    CHECK(capture==&traffic_scratch && records==2u && index<2u && ttb==TTB && process==PROCESS && ccr==1u);
    CHECK(min_bytes==(index?524u:16u) && p1 && cache && p2 && !ctx && scratch==&a2_query);
    CHECK(fence_state.pending && fence_state.active);++f.queries;*out=f.slice[index];
    return (enum kui_armada_ce_maple_rx_slice_status)f.query_result;
}
static bool a2_payload(unsigned index,unsigned words,uint8_t *out) {
    CHECK(index<2u && words==(index?2u:1u));
    if(index) memcpy(out,f.payload,8u);else put32(out,0x01000000u);
    return true;
}
static bool a2_recheck(void *ctx) {
    CHECK(!ctx && fence_state.pending && fence_state.active);++f.admissions;
    return !f.admit_error;
}
static bool a2_store32(void *ctx,uint32_t at,uint32_t value) {
    CHECK(!ctx && f.job_done && fence_state.pending && fence_state.active);
    CHECK(at==(f.slice[0].rx_bus|0xa0000000u) ||
          (at>=(f.slice[1].rx_bus|0xa0000000u) &&
           at-(f.slice[1].rx_bus|0xa0000000u)<f.slice[1].capacity));
    ++f.stores;
    if(at==(f.slice[1].rx_bus|0xa0000000u)) f.stored_header=value;
    return true;
}
int kui_armada_ce_vmu_runtime_bind(uint32_t owner) {
    CHECK(owner==RAW && fence_state.pending && fence_state.active);++f.bind;return f.bind_error?-1:0;
}
int kui_armada_ce_vmu_runtime_handle(void *ctx,const uint8_t *request,size_t n,
    uint8_t *reply,size_t capacity,size_t *produced) {
    CHECK(!ctx && request[0]==11u && n==12u && capacity==524u);
    CHECK(fence_state.pending && fence_state.active);++f.backend;
    if(!f.job_done) {*produced=0;return KUI_ARMADA_CE_VMU_A2_BACKEND_DEFERRED;}
    memset(reply,0,capacity);
    if(f.error) {put32(reply,0x010200fbu);put32(reply+4u,0x08000000u);*produced=8u;}
    else {put32(reply,0x82020008u);memcpy(reply+4u,request+4u,8u);*produced=524u;}
    return 1;
}

#define kui_armada_ce_maple_traffic_visit fake_visit
#define kui_armada_ce_maple_resolve fake_resolve
#define kui_armada_ce_cache_line fake_cache_line
#define kui_armada_ce_maple_fence_pre fake_pre
#define kui_armada_ce_maple_fence_post fake_post
#define kui_armada_ce_maple_rx_slice_query fake_query
#include "production.inc"

static void reset(void) {
    memset(&f,0,sizeof(f));memset(&a2_continuation,0,sizeof(a2_continuation));
    memset(&a2_input,0,sizeof(a2_input));memset(&a2_context,0,sizeof(a2_context));
    memset(&fence_state,0,sizeof(fence_state));memset(&a2_staging,0,sizeof(a2_staging));
    memset(&kui_armada_ce_vmu_a2_log,0,sizeof(kui_armada_ce_vmu_a2_log));
    memset(&kui_armada_ce_maple_fence_log,0,sizeof(kui_armada_ce_maple_fence_log));
    memset(&kui_armada_ce_maple_hook_log,0,sizeof(kui_armada_ce_maple_hook_log));
    memset(platform,0,sizeof(platform));auto_transfer_ready=false;
    kui_armada_ce_maple_traffic_freeze=kui_armada_ce_maple_proof_initial=kui_armada_ce_maple_proof_final=0;
    REG(0x8c145894u)=THREAD;REG(0x8c145890u)=PROCESS;REG(0xff000008u)=TTB;REG(0xff00001cu)=1u;
    test_sr=SR;f.handle=0x20u;f.wait_result=258u;kui_armada_ce_maple_bridge_return=0x01dd67c6u;
    f.mmio[0]=f.mmio[3]=1u;f.mmio[2]=0x0c020000u;
    f.parent_function=0x01000000u;
    for(unsigned i=0;i<3u;i++) f.controller_payload[i]=0xa1000000u+i;
    for(unsigned i=0;i<32u;i++) f.a1_payload[i]=0xa2000000u+i;
    f.capture.word[1]=0x0c020000u;f.capture.word[2]=0x02010000u;
    f.capture.word[3]=f.capture.word[2]+16u;f.capture.word[4]=f.capture.word[2]+36u;
    f.capture.word[5]=0x00060c00u;f.capture.word[6]=0x0c030000u;
    f.capture.row[0][3]=0x01002009u;f.capture.row[1][0]=16u;f.capture.row[1][3]=0x0200020bu;
    f.capture.map[0].virtual_address=0x02030a60u;f.capture.map[0].physical_address=0x8c040a60u;
    f.capture.map[0].page_word=0x0c040009u;f.owner_map=f.capture.map[0];
    for(unsigned i=0;i<2u;i++) {
        uint32_t *p=(void *)&f.slice[i];for(unsigned j=0;j<sizeof(f.slice[i])/4u;j++) p[j]=100u*i+j;
        f.slice[i].rx_bus=f.capture.word[6]+(i?0x420u:0u);
        f.slice[i].capacity=i?1020u:16u;f.slice[i].rx_header=i?UINT32_MAX:0x03310008u;
        f.capture.row[i][4]=f.slice[i].rx_header;
    }
    put32(f.payload,0x02000000u);put32(f.payload+4u,0x2a000000u);
}
static unsigned initial(void) {return kui_armada_ce_maple_traffic_run(RAW,0,0,SR);}
static unsigned resume(int status) {return kui_armada_ce_maple_traffic_run(RAW,2,0,(uint32_t)status);}
static void pending(void) {
    CHECK(initial()==KUI_ARMADA_CE_MAPLE_SERVICE);
    CHECK(a2_continuation.pending && fence_state.active && fence_state.pending);
    CHECK(!f.stores && f.pre==1u && !f.post && f.bind==1u && f.backend==1u);
    CHECK(f.disable_writes==1u && !f.enable_writes && !f.mmio[0] && !f.mmio[1]);
}
static void terminal(unsigned action) {
    CHECK(action==KUI_ARMADA_CE_MAPLE_TERMINAL && !f.stores);
    CHECK(kui_armada_ce_maple_traffic_freeze && kui_armada_ce_vmu_a2_log.fault[0]);
}
static void unchanged_physical_payloads(void) {
    for(unsigned i=0;i<3u;i++) CHECK(f.controller_payload[i]==0xa1000000u+i);
    for(unsigned i=0;i<32u;i++) CHECK(f.a1_payload[i]==0xa2000000u+i);
}
/* After consuming the current completion, the source calls InterruptDone,
 * then rebuilds/clears the same pooled RX before its subsequent wait. A raw
 * completion bit can become an event before that clear; it is not a FIFO. */
static void original_consumer(void) {
    CHECK(!a2_continuation.pending && fence_state.active && fence_state.pending);
    ++f.consumers;
    CHECK(kui_armada_ce_maple_traffic_run(RAW,1,0,SR)==KUI_ARMADA_CE_MAPLE_CONTINUE);
    f.irq_enabled=1u;dispatch_irq();
    f.capture.row[0][4]=f.slice[0].rx_header=0;
    f.capture.row[1][4]=f.slice[1].rx_header=0;
    f.parent_function=0;
}
static void replay_regression(void) {
    /* Counterexample for the old release/reacquire policy. The first worker
     * already acknowledged its initial status before entering this hook.
     * A VBlank during storage sets another status latch while IRQ delivery
     * remains disabled; InterruptDone delivers it before the next rebuild
     * clears RX and waits, so the stale wake sees a zero response. */
    reset();pending();
    CHECK(fake_post(RAW,0,fence_read_mmio,fence_write_mmio,NULL,&fence_state,
                    &kui_armada_ce_maple_fence_log)==KUI_ARMADA_CE_MAPLE_FENCE_OK);
    vblank();CHECK(f.dma_runs==1u && f.irq_status && !f.event);
    CHECK(fake_pre(RAW,f.capture.word[1],0,true,fence_read_mmio,fence_write_mmio,
                   NULL,&fence_state,&kui_armada_ce_maple_fence_log)==KUI_ARMADA_CE_MAPLE_FENCE_OK);
    f.job_done=1u;CHECK(resume(1)==KUI_ARMADA_CE_MAPLE_CONTINUE);
    original_consumer();CHECK(f.event && !f.slice[0].rx_header && !f.parent_function);
    unsigned backend=f.backend;f.stores=0;f.event=0;f.irq_status=0;
    terminal(initial());
    CHECK(kui_armada_ce_vmu_a2_log.fault[0]==KUI_ARMADA_CE_VMU_A2_PUBLISH_PARENT);
    CHECK(f.backend==backend && !f.stores && f.consumers==1u);
    unchanged_physical_payloads();

    /* Production policy keeps MDEN0 through arbitrary VBlanks and real wait
     * opportunities. A new source completion is possible only after POST
     * restores DMA, so its RX header comes from a fresh physical response. */
    const unsigned delays[]={1u,2u,61u};
    for(unsigned d=0;d<sizeof(delays)/sizeof(delays[0]);d++) {
        reset();pending();
        for(unsigned i=0;i<delays[d];i++) {
            CHECK(!kui_armada_ce_maple_runtime_wait(RAW));
            CHECK(resume(0)==KUI_ARMADA_CE_MAPLE_SERVICE);
            CHECK(f.pre==1u && !f.post && !f.dma_runs && !f.irq_status && !f.event);
            CHECK(fence_state.active && fence_state.pending && !f.mmio[0]);
        }
        f.job_done=1u;CHECK(resume(1)==KUI_ARMADA_CE_MAPLE_CONTINUE);
        CHECK(f.stores==132u && f.pre==1u && !f.post);
        original_consumer();
        CHECK(f.consumers==1u && f.post==1u && f.enable_writes==1u && !f.event);
        f.stores=0;f.job_done=0;vblank();
        CHECK(f.event && f.dma_runs==1u && f.slice[0].rx_header==0x03310008u);
        f.event=0;f.irq_status=0;
        CHECK(initial()==KUI_ARMADA_CE_MAPLE_SERVICE);
        CHECK(!kui_armada_ce_vmu_a2_log.fault[0] && f.backend==delays[d]+3u && !f.stores);
        CHECK(f.pre==2u && f.post==1u && f.disable_writes==2u && f.enable_writes==1u);
        unchanged_physical_payloads();
    }
}
int main(void) {
    replay_regression();
    reset();pending();CHECK(resume(0)==KUI_ARMADA_CE_MAPLE_SERVICE && !f.stores && !f.post && f.pre==1u);
    f.job_done=1;CHECK(resume(1)==KUI_ARMADA_CE_MAPLE_CONTINUE);
    CHECK(!a2_continuation.pending && fence_state.active && fence_state.pending && f.stores==132u);
    CHECK(f.stored_header==0x82020008u && f.bind==1u && f.backend==3u);
    CHECK(kui_armada_ce_maple_traffic_run(RAW,1,0,SR)==0 && !fence_state.active && !fence_state.pending);
    reset();pending();f.job_done=f.error=1;CHECK(resume(-1)==0 && f.stores==3u && f.stored_header==0x010200fbu);
    for(int result=-5;result<6;result++) if(result<-1 || result>1) {
        reset();pending();terminal(resume(result));CHECK(f.backend==1u);
    }
    reset();f.pre_result=KUI_ARMADA_CE_MAPLE_FENCE_SKIPPED;
    CHECK(initial()==KUI_ARMADA_CE_MAPLE_CONTINUE && !f.backend && !f.stores && !f.post);
    CHECK(kui_armada_ce_maple_traffic_run(RAW,1,0,SR)==0 && f.post==1u && !fence_state.pending);
    reset();pending();f.post_result=KUI_ARMADA_CE_MAPLE_FENCE_RESTORE;f.post_stuck=1;
    f.capture.word[0]=1u;terminal(resume(1));CHECK(f.post==1u && fence_state.active);
    for(unsigned i=0;i<offsetof(struct kui_armada_ce_maple_traffic_scratch,reads)/4u;i++) {
        reset();pending();((uint32_t *)(void *)&f.capture)[i]^=0x100u;
        terminal(resume(1));CHECK(f.backend==1u);
    }
    for(unsigned i=0;i<sizeof(f.slice)/4u;i++) {
        reset();pending();((uint32_t *)(void *)f.slice)[i]^=1u;
        terminal(resume(1));CHECK(f.backend==1u);
    }
    for(unsigned i=0;i<8u;i++) {
        reset();pending();f.payload[i]^=1u;terminal(resume(1));CHECK(f.backend==1u);
    }
    for(unsigned i=0;i<3u;i++) {
        reset();pending();
        if(i==0) f.capture_result=KUI_ARMADA_CE_MAPLE_TRAFFIC_CHANGED;
        if(i==1) f.query_result=KUI_ARMADA_CE_MAPLE_RX_SLICE_CHANGED;
        if(i==2) f.mmio[2]^=0x1000u;
        terminal(resume(1));CHECK(f.backend==1u);
    }
    for(unsigned mask=1;mask<16u;mask++) {
        reset();terminal(kui_armada_ce_maple_traffic_run(RAW,0,0,SR|(mask<<4)));CHECK(!f.backend && !f.pre);
    }
    reset();terminal(kui_armada_ce_maple_traffic_run(RAW,0,0,SR|0x10000000u));
    reset();terminal(kui_armada_ce_maple_traffic_run(RAW,0,0,SR&~0x40000000u));
    for(unsigned bit=0;bit<32u;bit++) if(bit<25u || bit==31u) {
        reset();kui_armada_ce_maple_bridge_return^=1u<<bit;terminal(initial());CHECK(!f.bind && !f.backend);
    }
    for(unsigned slot=0;slot<64u;slot++) {
        reset();kui_armada_ce_maple_bridge_return=0x01dd67c6u|(slot<<25);pending();
        CHECK(a2_continuation.wait==kui_armada_ce_maple_bridge_return-0x4b2u);
        CHECK(!kui_armada_ce_maple_runtime_wait(RAW) && f.waits==1u);
    }
    reset();f.handle=0;terminal(initial());CHECK(!f.bind && !f.backend);
    /* A physical provider failure is already recorded by masked runtime_work.
     * Execute the actual traffic resume, terminal and automatic capture: the
     * canonical FILEERR path must not hide the original provider evidence or
     * let the original consumer run while the GD reader is quarantined. */
    reset();pending();
    const uint32_t provider_fault[5]={112u,6u,0x00020003u,UINT32_MAX,0x00053084u};
    memcpy(kui_armada_ce_vmu_a2_log.fault,provider_fault,sizeof(provider_fault));
    REG(0xac0000e0u)=REG(0xacfcff80u)=0x8c00c088u;test_sr=SR|0xf0u;
    auto_transfer_ready=true;
    if(!setjmp(auto_transfer)) { (void)resume(-1);CHECK(0); }
    auto_transfer_ready=false;
    CHECK(f.auto_stops==1u && !f.stores && f.backend==1u && kui_armada_ce_maple_traffic_freeze);
    CHECK(REG(0xacfcff78u)==KUI_ARMADA_CE_MAPLE_AUTO_FAULT);
    CHECK(REG(0xacfcff7cu)==KUI_ARMADA_CE_MAPLE_AUTO_BEFORE_ORIGINAL);
    CHECK(!memcmp(kui_armada_ce_vmu_a2_log.fault,provider_fault,sizeof(provider_fault)));
    reset();pending();for(unsigned i=1;i<KUI_ARMADA_CE_MAPLE_CONTINUATION_LIMIT;i++)
        CHECK(resume(0)==KUI_ARMADA_CE_MAPLE_SERVICE && !f.stores);
    terminal(resume(0));CHECK(a2_continuation.rounds==KUI_ARMADA_CE_MAPLE_CONTINUATION_LIMIT);
    reset();pending();CHECK(kui_armada_ce_maple_runtime_owner(RAW));
    CHECK(!kui_armada_ce_maple_runtime_wait(RAW) && f.waits==1u && test_sr==SR);
    f.wait_result=0;CHECK(kui_armada_ce_maple_runtime_wait(RAW)==-1 && f.waits==2u && test_sr==SR);
    f.wait_result=258u;
    for(unsigned mask=1;mask<16u;mask++) {test_sr=SR|(mask<<4);
        CHECK(kui_armada_ce_maple_runtime_wait(RAW)==-1 && f.waits==2u);}
    test_sr=SR|0x10000000u;CHECK(kui_armada_ce_maple_runtime_wait(RAW)==-1 && f.waits==2u);
    test_sr=SR&~0x40000000u;CHECK(kui_armada_ce_maple_runtime_wait(RAW)==-1 && f.waits==2u);
    test_sr=SR;f.handle^=4u;CHECK(!kui_armada_ce_maple_runtime_owner(RAW));
    CHECK(kui_armada_ce_maple_runtime_wait(RAW)==-1 && f.waits==2u && test_sr==SR);f.handle^=4u;
    CHECK(!kui_armada_ce_maple_runtime_owner(RAW+4u));
    /* All seven software lease witnesses and all four actual hardware words
     * must survive unmasked service. Test the public worker/wait admission
     * and the extracted resume; neither may reach backend/store on damage. */
    for(unsigned word=0;word<sizeof(fence_state)/4u;word++) for(unsigned bit=0;bit<32u;bit++) {
        reset();pending();((uint32_t *)(void *)&fence_state)[word]^=1u<<bit;
        CHECK(!kui_armada_ce_maple_runtime_owner(RAW));
        CHECK(kui_armada_ce_maple_runtime_wait(RAW)==-1 && !f.waits);
        terminal(resume(1));CHECK(f.backend==1u && !f.stores);
    }
    for(unsigned word=0;word<4u;word++) for(unsigned bit=0;bit<32u;bit++) {
        reset();pending();f.mmio[word]^=1u<<bit;
        CHECK(!kui_armada_ce_maple_runtime_owner(RAW));
        CHECK(kui_armada_ce_maple_runtime_wait(RAW)==-1 && !f.waits);
        terminal(resume(1));CHECK(f.backend==1u && !f.stores);
    }
    const uint32_t mmio_address[4]={KUI_ARMADA_CE_MAPLE_FENCE_MDEN,KUI_ARMADA_CE_MAPLE_FENCE_MDST,
        KUI_ARMADA_CE_MAPLE_FENCE_MDSTAR,KUI_ARMADA_CE_MAPLE_FENCE_MDTSEL};
    for(unsigned word=0;word<4u;word++) {
        reset();pending();f.mmio_read_fail=mmio_address[word];
        CHECK(!kui_armada_ce_maple_runtime_owner(RAW));
        CHECK(kui_armada_ce_maple_runtime_wait(RAW)==-1 && !f.waits);
        terminal(resume(1));CHECK(f.backend==1u && !f.stores);
    }
    /* Parking DMA fixes the completion lifecycle; it grants no permission
     * to synthesize an absent or malformed physical controller response. */
    for(unsigned bad=0;bad<4u;bad++) {
        reset();
        if(bad==0u) {f.slice[0].rx_header=0;f.parent_function=0;}
        if(bad==1u) f.slice[0].rx_header^=1u;
        if(bad==2u) f.slice[0].rx_header^=0x01000000u;
        if(bad==3u) f.parent_function=0x02000000u;
        f.capture.row[0][4]=f.slice[0].rx_header;
        terminal(initial());
        CHECK(kui_armada_ce_vmu_a2_log.fault[0]==KUI_ARMADA_CE_VMU_A2_PUBLISH_PARENT);
        CHECK(!f.backend && !f.stores);unchanged_physical_payloads();
    }
    reset();pending();
    const uint32_t reg[4]={0x8c145894u,0x8c145890u,0xff000008u,0xff00001cu};
    for(unsigned i=0;i<4u;i++) {uint32_t old=REG(reg[i]);REG(reg[i])^=4u;
        CHECK(!kui_armada_ce_maple_runtime_owner(RAW));REG(reg[i])=old;}
    f.shutdown=1;CHECK(!kui_armada_ce_maple_runtime_owner(RAW));f.shutdown=0;
    f.map_bad=1;CHECK(!kui_armada_ce_maple_runtime_owner(RAW));f.map_bad=0;
    f.cache_bad=1;CHECK(!kui_armada_ce_maple_runtime_owner(RAW));f.cache_bad=0;
    f.thread_change=1;CHECK(!kui_armada_ce_maple_runtime_owner(RAW));
    printf("Native continuation C glue: %u checks; actual production functions, real publisher, ASAN/UBSAN\n",checks);
    return 0;
}
