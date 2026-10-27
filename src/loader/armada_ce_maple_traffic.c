/* SPDX-License-Identifier: GPL-3.0-only */
/* The exact source-worker bridge serializes this sampler with interrupts
 * masked. All C frames and scratch live in the pre-NK private reservation. */
#include "kui/armada_ce_maple_traffic.h"
#include "kui/armada_ce_maple_continuation.h"
#if KUI_ARMADA_CE_VMU_WRITE_RUNTIME
#include "kui/armada_ce_vmu_runtime.h"
#endif
extern struct kui_armada_ce_maple_traffic_snapshot kui_armada_ce_maple_hook_log;
extern volatile uint32_t kui_armada_ce_maple_traffic_freeze;
static struct kui_armada_ce_maple_traffic_scratch traffic_scratch;
#if KUI_ARMADA_CE_MAPLE_FENCE
#include "kui/armada_ce_maple_fence.h"
extern struct kui_armada_ce_maple_fence_snapshot kui_armada_ce_maple_fence_log;
static struct kui_armada_ce_maple_fence_state fence_state;
static bool __attribute__((noinline,noclone)) fence_read_mmio(void *context,uint32_t at,uint32_t *value) {
    (void)context;
    if(!value || (at!=KUI_ARMADA_CE_MAPLE_FENCE_MDSTAR && at!=KUI_ARMADA_CE_MAPLE_FENCE_MDTSEL &&
       at!=KUI_ARMADA_CE_MAPLE_FENCE_MDEN && at!=KUI_ARMADA_CE_MAPLE_FENCE_MDST)) return false;
    *value=*(volatile const uint32_t *)(uintptr_t)at;return true;
}
static bool __attribute__((noinline,noclone)) fence_write_mmio(void *context,uint32_t at,uint32_t value) {
    (void)context;
    if(at!=KUI_ARMADA_CE_MAPLE_FENCE_MDEN || value>1u) return false;
    *(volatile uint32_t *)(uintptr_t)at=value;return true;
}
#endif
static bool __attribute__((noinline,noclone)) traffic_read_p1(
    void *context,uint32_t at,uint32_t *value) {
    uint32_t p1;(void)context;
    if(!value || ((at!=0xff000008u && at!=0xff00001cu) &&
       (!kui_armada_ce_ram_table_p1(at,&p1) || kui_armada_ce_maple_traffic_owned(p1,4u)))) return false;
    *value=*(volatile const uint32_t *)(uintptr_t)at;return true;
}
extern uint32_t kui_armada_ce_excerpt_array_read(uint32_t address);
static bool __attribute__((noinline,noclone)) traffic_read_cache(
    void *context,uint32_t at,uint32_t *value) {
    uint32_t p1;(void)context;
    if(!value || (at&3u)) return false;
    if((at&0xffffc01fu)==0xf4000000u || (at&0xffffc003u)==0xf5000000u) {
        /* Linked into this P2-only helper, with the complete 32-byte cache
         * array leaf pinned independently by the native package checker. */
        *value=kui_armada_ce_excerpt_array_read(at);return true;
    }
    if((at&0xe0000000u)!=0xa0000000u || !kui_armada_ce_ram_p1(at,&p1) ||
       kui_armada_ce_maple_traffic_owned(p1,4u)) return false;
    *value=*(volatile const uint32_t *)(uintptr_t)at;return true;
}
static bool __attribute__((noinline,noclone)) traffic_read_p2(
    void *context,uint32_t at,uint32_t *value) {
    uint32_t p1;(void)context;
    if(!value || (at&0xe0000000u)!=0xa0000000u ||
       !kui_armada_ce_ram_p1(at,&p1) || kui_armada_ce_maple_traffic_owned(p1,4u)) return false;
    *value=*(volatile const uint32_t *)(uintptr_t)at;return true;
}
#if KUI_ARMADA_CE_MAPLE_A2
#include "kui/armada_ce_maple_rx_slice.h"
#if !KUI_ARMADA_CE_VMU_WRITE_RUNTIME
#include "kui/armada_ce_vmu_readonly.h"
#endif
#include "kui/armada_ce_vmu_a2_publish.h"
#include "kui/armada_ce_maple_auto.h"
#if KUI_ARMADA_CE_CACHE_SNAPSHOT_NATIVE
struct kui_armada_ce_cache_snapshot_job kui_armada_ce_cache_snapshot_job;
enum kui_armada_ce_ram_bind_status kui_armada_ce_cache_line_native(
    uint32_t va,uint32_t p1,uint32_t page,uint32_t mask,
    kui_armada_ce_ram_read32 read32,void *context,uint32_t out[8]) {
    /* Charge the complete bounded snapshot before entering the callback-free
     * leaf. This is conservative on refusal and exact on successful reads. */
    uint32_t cost=8u;
    for(unsigned i=0;i<8u;i++) if(mask&(1u<<i)) cost+=2u;
    if(read32==kui_armada_ce_maple_traffic_cache) {
        struct kui_armada_ce_maple_traffic_io *io=context;
        if(io->w->reads>KUI_ARMADA_CE_MAPLE_TRAFFIC_READ_LIMIT ||
           cost>KUI_ARMADA_CE_MAPLE_TRAFFIC_READ_LIMIT-io->w->reads) {
            (void)kui_armada_ce_maple_traffic_fail(io->w,KUI_ARMADA_CE_MAPLE_TRAFFIC_BUDGET,
                va,io->w->reads,KUI_ARMADA_CE_MAPLE_TRAFFIC_READ_LIMIT,cost);
            return KUI_ARMADA_CE_RAM_BIND_READ;
        }
        io->w->reads+=cost;
    } else if(read32==kui_armada_ce_maple_rx_slice_cache) {
        struct kui_armada_ce_maple_rx_slice_io *io=context;
        if(io->w->reads>KUI_ARMADA_CE_MAPLE_RX_SLICE_READ_LIMIT ||
           cost>KUI_ARMADA_CE_MAPLE_RX_SLICE_READ_LIMIT-io->w->reads) {
            (void)kui_armada_ce_maple_rx_slice_fail(io,KUI_ARMADA_CE_MAPLE_RX_SLICE_BUDGET,
                va,io->w->reads,KUI_ARMADA_CE_MAPLE_RX_SLICE_READ_LIMIT,cost);
            return KUI_ARMADA_CE_RAM_BIND_READ;
        }
        io->w->reads+=cost;
    } else return KUI_ARMADA_CE_RAM_BIND_ARGUMENT;
    struct kui_armada_ce_cache_snapshot_job *job=&kui_armada_ce_cache_snapshot_job;
    job->va=va;job->p1=p1;job->page=page;job->mask=mask;
    for(unsigned i=0;i<3u;i++) job->detail[i]=0;
    unsigned status=kui_armada_ce_cache_snapshot(job);
    if(status) {
        if(read32==kui_armada_ce_maple_traffic_cache) {
            struct kui_armada_ce_maple_traffic_io *io=context;
            (void)kui_armada_ce_maple_traffic_fail(io->w,
                status==2u?KUI_ARMADA_CE_MAPLE_TRAFFIC_CHANGED:KUI_ARMADA_CE_MAPLE_TRAFFIC_CACHE,
                job->detail[0],job->detail[1],job->detail[2],mask);
        } else {
            struct kui_armada_ce_maple_rx_slice_io *io=context;
            (void)kui_armada_ce_maple_rx_slice_fail(io,
                status==2u?KUI_ARMADA_CE_MAPLE_RX_SLICE_CHANGED:KUI_ARMADA_CE_MAPLE_RX_SLICE_CACHE,
                job->detail[0],job->detail[1],job->detail[2],mask);
        }
        return status==1u?KUI_ARMADA_CE_RAM_BIND_CACHE:
            status==2u?KUI_ARMADA_CE_RAM_BIND_CHANGED:KUI_ARMADA_CE_RAM_BIND_ARGUMENT;
    }
    for(unsigned i=0;i<8u;i++) if(mask&(1u<<i)) out[i]=job->word[i];
    return KUI_ARMADA_CE_RAM_BIND_OK;
}
#endif
extern struct kui_armada_ce_vmu_a2_publish_snapshot kui_armada_ce_vmu_a2_log;
#if !KUI_ARMADA_CE_VMU_WRITE_RUNTIME
static struct kui_armada_ce_vmu_readonly a2_backend;
#endif
static struct kui_armada_ce_vmu_a2_publish_scratch a2_staging;
static struct kui_armada_ce_vmu_a2_input a2_input;
static struct kui_armada_ce_maple_rx_slice_scratch a2_query;
static struct kui_armada_ce_maple_rx_slice a2_slice[2],a2_again;
static struct {
    uint32_t ttb,process,ccr,records,index[2],needed[2],parent,target,target_bytes;
} a2_context;
#if !KUI_ARMADA_CE_VMU_WRITE_RUNTIME
static uint32_t a2_ready;
#endif
#if KUI_ARMADA_CE_VMU_WRITE_RUNTIME
/* All spans are private P2 BSS. Nothing retains a guest buffer for I/O. A
 * fresh post-service query must describe this exact completion before the
 * backend can consume a durable result or publish any RX byte. */
static struct {
    uint32_t pending,rounds,raw,mode,sr,result,checked;
    uint32_t bound,thread,process,ttb,ccr,handle,wait;
    struct kui_armada_ce_maple_mapping extension;
    struct kui_armada_ce_maple_traffic_scratch capture;
    struct kui_armada_ce_vmu_a2_input input;
    struct kui_armada_ce_maple_rx_slice slice[2];
    uint32_t context[sizeof(a2_context)/4u];
} a2_continuation;
uint32_t kui_armada_ce_maple_bridge_return;
/* Private objects are word-aligned and whole-word sized. The input also
 * contains byte arrays, so inspect their representations without violating
 * GCC's effective-type rules (same integer-only alias used by native memcpy). */
typedef uint32_t a2_copy_word __attribute__((__may_alias__));
static void a2_copy_words(void *to,const void *from,size_t bytes) {
    a2_copy_word *a=to;const a2_copy_word *b=from;
    for(size_t i=0;i<bytes/4u;i++) a[i]=b[i];
}
static bool a2_same_words(const void *first,const void *second,size_t bytes) {
    const a2_copy_word *a=first,*b=second;
    for(size_t i=0;i<bytes/4u;i++) if(a[i]!=b[i]) return false;
    return true;
}
static bool a2_recheck(void *context);
static bool a2_extension_word(uint32_t offset,uint32_t *value);
#endif
extern const volatile uint32_t kui_armada_ce_maple_diagnostic_mode;
extern volatile uint32_t kui_armada_ce_maple_proof_initial;
extern volatile uint32_t kui_armada_ce_maple_proof_final;
#if KUI_ARMADA_CE_MAPLE_AUTO_STOP
#ifndef KUI_ARMADA_CE_MAPLE_AUTO_PHASE
#define KUI_ARMADA_CE_MAPLE_AUTO_PHASE KUI_ARMADA_CE_MAPLE_AUTO_BEFORE_ORIGINAL
#endif
#ifndef KUI_ARMADA_CE_MAPLE_AUTO_LIMIT
#define KUI_ARMADA_CE_MAPLE_AUTO_LIMIT 1200u
#endif
extern void kui_armada_ce_maple_auto_stop(void) __attribute__((noreturn));
static void a2_auto_capture(unsigned phase) {
    const struct kui_armada_ce_vmu_a2_publish_snapshot *s=&kui_armada_ce_vmu_a2_log;
    unsigned reason=kui_armada_ce_maple_auto_decide(KUI_ARMADA_CE_MAPLE_AUTO_STOP,
        s->counts[3],s->commands[0],s->commands[3],s->commands[4],s->fault[0]);
    /* Faults terminate before any further CE consumer call. A successful
     * checkpoint can stop on either side of that call. A completion-count
     * fallback also captures an event the driver never reaches. */
    if(reason!=KUI_ARMADA_CE_MAPLE_AUTO_FAULT && phase!=KUI_ARMADA_CE_MAPLE_AUTO_PHASE) return;
    if(!reason && kui_armada_ce_maple_hook_log.counts[1]>=KUI_ARMADA_CE_MAPLE_AUTO_LIMIT)
        reason=KUI_ARMADA_CE_MAPLE_AUTO_VISIT_LIMIT;
    if(!reason) return;
    uint32_t expected=*(volatile const uint32_t *)(uintptr_t)KUI_ARMADA_CE_MAPLE_AUTO_MENU_ADDRESS;
    uint32_t vector=*(volatile const uint32_t *)(uintptr_t)0xac0000e0u;
    /* This identity was published by our resident while installing this
     * exact vector. Never branch through a firmware/guest replacement. The
     * helper already runs in kernel mode with interrupts fully masked. */
    if(vector!=expected || (vector&1u) || vector<0x8c008300u || vector>=0x8c00d800u) return;
    *(volatile uint32_t *)(uintptr_t)KUI_ARMADA_CE_MAPLE_AUTO_PHASE_ADDRESS=phase;
    __asm__ volatile("" ::: "memory");
    *(volatile uint32_t *)(uintptr_t)KUI_ARMADA_CE_MAPLE_AUTO_REASON_ADDRESS=reason;
    __asm__ volatile("" ::: "memory");
    kui_armada_ce_maple_auto_stop();
}
#endif
static void a2_fault(unsigned reason,uint32_t at,uint32_t actual,uint32_t expected,uint32_t detail) {
    struct kui_armada_ce_vmu_a2_publish_snapshot *s=&kui_armada_ce_vmu_a2_log;
    if(!s->counts[0]) s->counts[0]=KUI_ARMADA_CE_VMU_A2_PUBLISH_MAGIC;
    s->status[0]=kui_armada_ce_maple_traffic_inc(s->status[0]);s->status[3]|=0x80000000u;
    if(!s->fault[0]) {s->fault[0]=reason;s->fault[1]=at;s->fault[2]=actual;
        s->fault[3]=expected;s->fault[4]=detail;}
}
/* This is the only guest-RAM writer. Its two admitted ranges are private
 * witnesses populated by the live unit/function/RX proof below. Every aligned
 * uncached store is confirmed before the publisher advances to its next word. */
static bool __attribute__((noinline,noclone)) a2_store32(void *context,uint32_t at,uint32_t value) {
    (void)context;
    if((at&0xe0000003u)!=0xa0000000u || at<0xac010000u || at>0xacfffffcu ||
       (at<(KUI_ARMADA_CE_VMU_RESERVE_END+0x20000000u) && at>=KUI_ARMADA_CE_VMU_HOOK_P2)) return false;
    if(a2_context.target_bytes && (a2_context.target_bytes<4u || a2_context.target_bytes>1020u ||
       (a2_context.target_bytes&3u))) return false;
    if(at!=a2_context.parent && (!a2_context.target_bytes || at<a2_context.target ||
       at-a2_context.target>a2_context.target_bytes-4u)) return false;
    *(volatile uint32_t *)(uintptr_t)at=value;
    __asm__ volatile("" ::: "memory");
    return *(volatile const uint32_t *)(uintptr_t)at==value;
}
static int __attribute__((noinline,noclone)) a2_backend_call(void *context,
    const uint8_t *request,size_t request_bytes,uint8_t *response,size_t capacity,size_t *bytes) {
#if KUI_ARMADA_CE_VMU_WRITE_RUNTIME
    (void)context;
    if(!a2_continuation.bound) {
        if(!a2_recheck(NULL)) return -1;
        a2_continuation.thread=*(volatile const uint32_t *)(uintptr_t)0x8c145894u;
        a2_continuation.process=a2_context.process;a2_continuation.ttb=a2_context.ttb;
        a2_continuation.ccr=a2_context.ccr;
        a2_copy_words(&a2_continuation.extension,&traffic_scratch.map[0],sizeof(traffic_scratch.map[0]));
        uint32_t return_pc=kui_armada_ce_maple_bridge_return;
        if(!a2_continuation.thread || (return_pc&0x81ffffffu)!=0x01dd67c6u ||
           !a2_extension_word(0x178u,&a2_continuation.handle) || !a2_continuation.handle ||
           kui_armada_ce_vmu_runtime_bind(a2_continuation.raw)) return -1;
        a2_continuation.wait=return_pc-0x4b2u;
        a2_continuation.bound=1u;
    }
    return kui_armada_ce_vmu_runtime_handle(NULL,request,request_bytes,response,capacity,bytes);
#else
    (void)context;
    if(!a2_ready || a2_backend.self!=&a2_backend || a2_backend.backend.reader ||
       a2_backend.backend.reader_context) return -1;
    return kui_armada_ce_vmu_readonly_handle(&a2_backend,request,request_bytes,response,capacity,bytes);
#endif
}
static unsigned a2_minimum(uint32_t header) {
    unsigned command=header&255u;
    return command==1u?116u:command==2u?196u:command==10u?32u:command==11u?524u:4u;
}
static bool a2_payload(unsigned index,unsigned count,uint8_t *out) {
#if KUI_ARMADA_CE_MAPLE_WRITE_STAGING
    if(index>=KUI_ARMADA_CE_MAPLE_TRAFFIC_RECORD_LIMIT || count>34u) return false;
#endif
    const uint32_t *row=traffic_scratch.row[index];
#if KUI_ARMADA_CE_MAPLE_WRITE_STAGING
    if(count>(row[3]>>24) || row[0]>0x1000u-12u ||
       count>(0x1000u-12u-row[0])/4u) return false;
#endif
    for(unsigned i=0;i<count;i++) {uint32_t value;
        if(!traffic_read_p2(NULL,(traffic_scratch.word[1]+row[0]+12u+4u*i)|0xa0000000u,&value)) return false;
        for(unsigned j=0;j<4u;j++) out[4u*i+j]=(uint8_t)(value>>(8u*j));
    }
    return true;
}
#if KUI_ARMADA_CE_MAPLE_WRITE_STAGING
static bool a2_payload_matches(unsigned index,unsigned count,const uint8_t *expected) {
    if(index>=KUI_ARMADA_CE_MAPLE_TRAFFIC_RECORD_LIMIT || count>34u) return false;
    const uint32_t *row=traffic_scratch.row[index];
    if(count>(row[3]>>24) || row[0]>0x1000u-12u ||
       count>(0x1000u-12u-row[0])/4u) return false;
    for(unsigned i=0;i<count;i++) {uint32_t value;
        if(!traffic_read_p2(NULL,(traffic_scratch.word[1]+row[0]+12u+4u*i)|0xa0000000u,&value)) return false;
        for(unsigned j=0;j<4u;j++) if((uint8_t)(value>>(8u*j))!=expected[4u*i+j]) return false;
    }
    return true;
}
#endif
/* Final admission follows pure protocol staging and precedes every guest
 * store. No CE/API/blocking callback runs between this and publication. */
static bool __attribute__((noinline,noclone)) a2_recheck(void *context) {
    (void)context;uint32_t value;
    if(!fence_state.active || !fence_state.pending ||
       !fence_read_mmio(NULL,KUI_ARMADA_CE_MAPLE_FENCE_MDEN,&value) || value ||
       !fence_read_mmio(NULL,KUI_ARMADA_CE_MAPLE_FENCE_MDST,&value) || value ||
       !fence_read_mmio(NULL,KUI_ARMADA_CE_MAPLE_FENCE_MDSTAR,&value) || value!=traffic_scratch.word[1]) return false;
    for(unsigned i=0;i<1u+a2_input.target_present;i++) {
        unsigned status=kui_armada_ce_maple_rx_slice_query(&traffic_scratch,a2_context.records,
            a2_context.index[i],a2_context.ttb,a2_context.process,a2_context.ccr,a2_context.needed[i],
            traffic_read_p1,traffic_read_cache,traffic_read_p2,NULL,&a2_query,&a2_again);
        if(status) return false;
        const uint32_t *a=(const void *)&a2_slice[i],*b=(const void *)&a2_again;
        for(unsigned j=0;j<sizeof(a2_again)/4u;j++) if(a[j]!=b[j]) return false;
    }
    if(!traffic_read_p2(NULL,(a2_input.parent.rx_bus+4u)|0xa0000000u,&value) ||
       value!=a2_input.parent.response_function) return false;
    uint8_t words[8];
    unsigned parent_words=a2_input.parent.request_header>>24;
    if(parent_words && (!a2_payload(a2_context.index[0],1u,words) ||
       ((uint32_t)words[0]|(uint32_t)words[1]<<8|(uint32_t)words[2]<<16|(uint32_t)words[3]<<24)!=a2_input.parent.request_function)) return false;
    if(a2_input.target_present) {
        unsigned n=a2_input.target.request_bytes/4u-1u;
#if KUI_ARMADA_CE_MAPLE_WRITE_STAGING
        if(n && !a2_payload_matches(a2_context.index[1],n,a2_input.target.request+4u)) return false;
#else
        if(n && !a2_payload(a2_context.index[1],n,words)) return false;
        for(unsigned j=0;j<4u*n;j++) if(words[j]!=a2_input.target.request[4u+j]) return false;
#endif
    }
    kui_armada_ce_maple_proof_final=kui_armada_ce_maple_traffic_inc(kui_armada_ce_maple_proof_final);
    return true;
}
static void a2_run(uint32_t ttb,uint32_t process,uint32_t ccr) {
#if KUI_ARMADA_CE_VMU_WRITE_RUNTIME
    a2_continuation.result=UINT32_MAX;a2_continuation.checked=0;
    if(a2_continuation.bound &&
       (ttb!=a2_continuation.ttb || process!=a2_continuation.process || ccr!=a2_continuation.ccr ||
        *(volatile const uint32_t *)(uintptr_t)0x8c145894u!=a2_continuation.thread)) {
        a2_fault(100u,0,process,a2_continuation.process,ttb);return;
    }
#endif
    unsigned diagnostic=kui_armada_ce_maple_diagnostic_mode;
    if(diagnostic>2u) {a2_fault(98u,0,diagnostic,2u,0);return;}
    if(diagnostic==1u) return; /* Same sampler and DMA fence; no A2 proof or reply. */
    if(kui_armada_ce_vmu_a2_log.fault[0]) return;
    unsigned records=0,parent=16u,target=16u;
    uint32_t last=traffic_scratch.word[3]-traffic_scratch.word[2];
    for(unsigned i=0;i<16u;i++) {
        unsigned address=(traffic_scratch.row[i][3]>>8)&255u;
        if(address==32u) {if(parent!=16u) return;parent=i;}
        if(address==2u) {if(target!=16u) return;target=i;}
        if(traffic_scratch.row[i][0]==last) {records=i+1u;break;}
    }
    if(!records || parent==16u) return;
    uint32_t header=traffic_scratch.row[parent][3];unsigned command=header&255u;
    if(command!=1u && command!=2u && command!=9u) return;
    a2_context.ttb=ttb;a2_context.process=process;a2_context.ccr=ccr;a2_context.records=records;
    a2_context.index[0]=parent;a2_context.index[1]=target;
    a2_context.needed[0]=command==9u?16u:a2_minimum(header);
    a2_context.needed[1]=target==16u?0u:a2_minimum(traffic_scratch.row[target][3]);
    for(unsigned i=0;i<1u+(target!=16u);i++) {
        unsigned status=kui_armada_ce_maple_rx_slice_query(&traffic_scratch,records,a2_context.index[i],
            ttb,process,ccr,a2_context.needed[i],traffic_read_p1,traffic_read_cache,traffic_read_p2,
            NULL,&a2_query,&a2_slice[i]);
        if(status) {
            /* The first controller DEVINFO has no unit/function yet. Wait
             * for the real driver's ordinary controller polling to begin. */
            if(kui_armada_ce_vmu_a2_log.counts[3] || target!=16u ||
               (diagnostic==2u && kui_armada_ce_maple_proof_initial))
                a2_fault(64u+status,a2_query.error[1],a2_query.error[2],a2_query.error[3],a2_query.error[4]);
            return;
        }
    }
    volatile uint32_t *zero=(void *)&a2_input;
#if KUI_ARMADA_CE_MAPLE_WRITE_STAGING
    for(unsigned i=0;i<(offsetof(struct kui_armada_ce_vmu_a2_input,target.request)+12u)/4u;i++) zero[i]=0;
#else
    for(unsigned i=0;i<sizeof(a2_input)/4u;i++) zero[i]=0;
#endif
    a2_input.pool_bus=traffic_scratch.word[6];a2_input.pool_bytes=0x2000u;
    a2_input.tx_bus=traffic_scratch.word[1];a2_input.tx_bytes=0x1000u;
    a2_input.protected_bus=KUI_ARMADA_CE_VMU_RESERVE_BEGIN&0x1fffffffu;
    a2_input.protected_bytes=KUI_ARMADA_CE_VMU_RESERVE_BYTES;
    a2_input.parent.request_header=header;a2_input.parent.response_header=a2_slice[0].rx_header;
    a2_input.parent.rx_bus=a2_slice[0].rx_bus;a2_input.parent.capacity=a2_slice[0].capacity;
    if(!traffic_read_p2(NULL,(a2_input.parent.rx_bus+4u)|0xa0000000u,&a2_input.parent.response_function)) return;
    if(command==9u) {
        uint8_t bytes[4];if(!a2_payload(parent,1u,bytes)) return;
        a2_input.parent.request_function=(uint32_t)bytes[0]|(uint32_t)bytes[1]<<8|
            (uint32_t)bytes[2]<<16|(uint32_t)bytes[3]<<24;
    }
    if(target!=16u) {
        a2_input.target_present=1u;uint32_t h=traffic_scratch.row[target][3];
        unsigned words=h>>24;
#if KUI_ARMADA_CE_MAPLE_WRITE_STAGING
        if((h&255u)==12u?words!=34u:words>2u) {
            a2_fault(96u,traffic_scratch.word[1]+traffic_scratch.row[target][0],h,34u,words);return;
        }
#else
        if(words>2u) {a2_fault(96u,traffic_scratch.word[1]+traffic_scratch.row[target][0],h,2u,words);return;}
#endif
        for(unsigned j=0;j<4u;j++) a2_input.target.request[j]=(uint8_t)(h>>(8u*j));
        if(words && !a2_payload(target,words,a2_input.target.request+4u)) return;
        a2_input.target.request_bytes=4u+4u*words;a2_input.target.response_header=a2_slice[1].rx_header;
        a2_input.target.rx_bus=a2_slice[1].rx_bus;a2_input.target.capacity=a2_slice[1].capacity;
    }
    a2_context.parent=a2_input.parent.rx_bus|0xa0000000u;
    a2_context.target=a2_input.target.rx_bus|0xa0000000u;
    a2_context.target_bytes=a2_input.target_present?a2_input.target.capacity:0u;
#if KUI_ARMADA_CE_VMU_WRITE_RUNTIME
    if(a2_continuation.pending &&
       (!a2_same_words(&a2_input,&a2_continuation.input,sizeof(a2_input)) ||
        !a2_same_words(a2_slice,a2_continuation.slice,sizeof(a2_slice)) ||
        !a2_same_words(&a2_context,a2_continuation.context,sizeof(a2_context)))) {
        a2_fault(101u,0,0,1u,0);return;
    }
    a2_continuation.checked=1u;
#endif
    kui_armada_ce_maple_proof_initial=kui_armada_ce_maple_traffic_inc(kui_armada_ce_maple_proof_initial);
    if(diagnostic==2u) {
        /* Exercise both real ownership witnesses without advertising A2,
         * invoking the VMU backend, or writing any guest RX word. */
        if(!a2_recheck(NULL)) a2_fault(99u,0,0,0,0);
        return;
    }
#if !KUI_ARMADA_CE_VMU_WRITE_RUNTIME
    if(!a2_ready) {
        if(kui_armada_ce_vmu_readonly_init(&a2_backend)) {a2_fault(97u,0,0,0,0);return;}
        a2_ready=1u;
    }
#endif
#if KUI_ARMADA_CE_VMU_WRITE_RUNTIME
    a2_continuation.result=
#else
    (void)
#endif
    kui_armada_ce_vmu_a2_publish(&a2_input,a2_backend_call,NULL,a2_recheck,NULL,
        a2_store32,NULL,&a2_staging,&kui_armada_ce_vmu_a2_log);
}
#if KUI_ARMADA_CE_VMU_WRITE_RUNTIME
static bool a2_extension_word(uint32_t offset,uint32_t *value) {
    const struct kui_armada_ce_maple_mapping *map=&a2_continuation.extension;
    uint32_t va=map->virtual_address+offset,p1=map->physical_address+offset;
    uint32_t words[8],word=(va&31u)/4u;
    traffic_scratch.reads=0;
    for(unsigned i=0;i<5u;i++) traffic_scratch.error[i]=0;
    struct kui_armada_ce_maple_traffic_io io={traffic_read_p1,traffic_read_cache,
        traffic_read_p2,NULL,&traffic_scratch};
    if(kui_armada_ce_cache_line(va&~31u,p1&~31u,map->page_word,a2_continuation.ccr,
       1u<<word,kui_armada_ce_maple_traffic_cache,&io,words)) return false;
    *value=words[word];return true;
}
/* The completion belongs to this worker until its original consumer runs.
 * Keep its admitted DMA-off window across unmasked storage service: enabling
 * early can replay the old descriptor and leave a stale completion event for
 * the next, freshly cleared RX buffer. This parks hardware, not interrupts. */
static bool a2_held(void) {
    if(fence_state.pending!=1u || fence_state.active!=1u ||
       fence_state.extension!=a2_continuation.raw ||
       fence_state.source!=traffic_scratch.word[1] ||
       fence_state.previous_mode!=a2_continuation.mode ||
       fence_state.saved_enable!=1u || fence_state.trigger!=1u) return false;
    static const uint32_t at[4]={KUI_ARMADA_CE_MAPLE_FENCE_MDEN,KUI_ARMADA_CE_MAPLE_FENCE_MDST,
        KUI_ARMADA_CE_MAPLE_FENCE_MDSTAR,KUI_ARMADA_CE_MAPLE_FENCE_MDTSEL};
    const uint32_t wanted[4]={0,0,fence_state.source,1u};
    for(unsigned i=0;i<4u;i++) {uint32_t value;
        if(!fence_read_mmio(NULL,at[i],&value) || value!=wanted[i]) return false;
    }
    return true;
}
/* Called only by the fixed runtime worker, under its short saved-SR guard.
 * KData+94 is the exact NK SetKMode current-PTHREAD source (P1 coherent alias),
 * not the extension+178 CreateThread HANDLE. No guest virtual load occurs. */
bool kui_armada_ce_maple_runtime_owner(uint32_t owner) {
    if(!a2_continuation.bound || !a2_continuation.pending || owner!=a2_continuation.raw ||
       !a2_held() ||
       *(volatile const uint32_t *)(uintptr_t)0x8c145894u!=a2_continuation.thread ||
       *(volatile const uint32_t *)(uintptr_t)0x8c145890u!=a2_continuation.process ||
       *(volatile const uint32_t *)(uintptr_t)0xff000008u!=a2_continuation.ttb ||
       *(volatile const uint32_t *)(uintptr_t)0xff00001cu!=a2_continuation.ccr) return false;
    struct kui_armada_ce_maple_mapping now;
    const struct kui_armada_ce_maple_mapping *before=&a2_continuation.extension;
    if(kui_armada_ce_maple_resolve(a2_continuation.ttb,before->virtual_address,
       traffic_read_p1,NULL,&now) || !a2_same_words(before,&now,sizeof(now))) return false;
    uint32_t shutdown,handle;
    if(!a2_extension_word(0,&shutdown) || shutdown || !a2_extension_word(0x178u,&handle) ||
       handle!=a2_continuation.handle) return false;
    return *(volatile const uint32_t *)(uintptr_t)0x8c145894u==a2_continuation.thread &&
        *(volatile const uint32_t *)(uintptr_t)0x8c145890u==a2_continuation.process &&
        *(volatile const uint32_t *)(uintptr_t)0xff000008u==a2_continuation.ttb &&
        *(volatile const uint32_t *)(uintptr_t)0xff00001cu==a2_continuation.ccr;
}
int kui_armada_ce_maple_runtime_wait(uint32_t owner) {
    uint32_t saved;__asm__ volatile("stc sr,%0":"=r"(saved));
    if((saved&0x500000f0u)!=0x40000000u) return -1;
    uint32_t masked=saved|0xf0u;
    __asm__ volatile("ldc %0,sr"::"r"(masked):"t","memory");
    bool ready=kui_armada_ce_maple_runtime_owner(owner);
    __asm__ volatile("ldc %0,sr"::"r"(saved):"t","memory");
    if(!ready) return -1;
    /* The exact source main-worker HANDLE (extension+178) cannot signal while
     * this worker keeps executing. Unlike the DMA event, its1ms wait consumes
     * no completion. The callee comes from this PRE's authored bridge PR. */
    unsigned result=kui_armada_ce_maple_native_wait(a2_continuation.wait,a2_continuation.handle,1u);
    return result==258u?0:-1;
}
static unsigned a2_terminal(unsigned reason,uint32_t detail) {
    a2_fault(reason,0,a2_continuation.rounds,KUI_ARMADA_CE_MAPLE_CONTINUATION_LIMIT,detail);
    kui_armada_ce_maple_traffic_freeze=1u;
    /* active can survive a failed restore: retry once here, then stop rather
     * than entering the consumer with an uncertain gate or owner lifetime. */
    if(fence_state.pending || fence_state.active)
        (void)kui_armada_ce_maple_fence_post(fence_state.extension,fence_state.previous_mode,
            fence_read_mmio,fence_write_mmio,NULL,&fence_state,&kui_armada_ce_maple_fence_log);
#if KUI_ARMADA_CE_MAPLE_AUTO_STOP
    a2_auto_capture(KUI_ARMADA_CE_MAPLE_AUTO_BEFORE_ORIGINAL);
#endif
    return KUI_ARMADA_CE_MAPLE_TERMINAL;
}
static unsigned a2_service(void) {
    if(a2_continuation.rounds>=KUI_ARMADA_CE_MAPLE_CONTINUATION_LIMIT)
        return a2_terminal(102u,0);
    ++a2_continuation.rounds;
    if(!a2_held()) return a2_terminal(103u,0);
    return KUI_ARMADA_CE_MAPLE_SERVICE;
}
static unsigned a2_pending(void) {
    if(!a2_continuation.pending) {
        a2_copy_words(&a2_continuation.capture,&traffic_scratch,sizeof(traffic_scratch));
        a2_copy_words(&a2_continuation.input,&a2_input,sizeof(a2_input));
        a2_copy_words(a2_continuation.slice,a2_slice,sizeof(a2_slice));
        a2_copy_words(a2_continuation.context,&a2_context,sizeof(a2_context));
        a2_continuation.rounds=0;a2_continuation.pending=1u;
    }
    return a2_service();
}
#endif
#endif
#if KUI_ARMADA_CE_VMU_WRITE_RUNTIME
#define TRAFFIC_DONE() return KUI_ARMADA_CE_MAPLE_CONTINUE
unsigned
#else
#define TRAFFIC_DONE() return
void
#endif
kui_armada_ce_maple_traffic_run(uint32_t raw,uint32_t original_sp,
    uint32_t original_pr,uint32_t previous_mode) {
#if KUI_ARMADA_CE_VMU_WRITE_RUNTIME
    bool resuming=original_sp==KUI_ARMADA_CE_MAPLE_RESUME_PHASE;
    if(resuming) {
        if(!a2_continuation.pending || raw!=a2_continuation.raw || original_pr!=a2_continuation.mode ||
           (previous_mode!=0u && previous_mode!=1u && previous_mode!=UINT32_MAX))
            return a2_terminal(104u,previous_mode);
        if(!a2_held()) return a2_terminal(108u,0);
        original_sp=0;
    } else if(original_sp==0u) {
        if(a2_continuation.pending ||
           (a2_continuation.bound && raw!=a2_continuation.raw) ||
           (previous_mode&KUI_ARMADA_CE_MAPLE_CONTINUATION_SR_FORBIDDEN) ||
           !(previous_mode&0x40000000u)) return a2_terminal(105u,previous_mode);
        a2_continuation.raw=raw;a2_continuation.mode=original_pr;a2_continuation.sr=previous_mode;
    }
#else
    (void)previous_mode;
#endif
#if KUI_ARMADA_CE_MAPLE_FENCE
    /* The call-site bridge passes phase in r5, observed old mode in r6. A
     * pending POST must recover its held window even after capture freezes. */
    if(original_sp==1u) {
        if(!kui_armada_ce_maple_traffic_freeze || fence_state.pending || fence_state.active)
            (void)kui_armada_ce_maple_fence_post(raw,original_pr,fence_read_mmio,fence_write_mmio,NULL,
                &fence_state,&kui_armada_ce_maple_fence_log);
#if KUI_ARMADA_CE_MAPLE_A2 && KUI_ARMADA_CE_MAPLE_AUTO_STOP
        a2_auto_capture(KUI_ARMADA_CE_MAPLE_AUTO_AFTER_ORIGINAL);
#endif
        TRAFFIC_DONE();
    }
    if(original_sp!=0u) {TRAFFIC_DONE();}
#else
    (void)original_sp;(void)original_pr;
#endif
    if(kui_armada_ce_maple_traffic_freeze) {
#if KUI_ARMADA_CE_VMU_WRITE_RUNTIME
        if(resuming) return a2_terminal(106u,0);
#endif
        TRAFFIC_DONE();
    }
    const uint32_t ttb=*(volatile const uint32_t *)(uintptr_t)0xff000008u;
    const uint32_t process=*(volatile const uint32_t *)(uintptr_t)0x8c145890u;
    const uint32_t ccr=*(volatile const uint32_t *)(uintptr_t)0xff00001cu;
    unsigned result=kui_armada_ce_maple_traffic_visit(raw,ttb,process,ccr,
        traffic_read_p1,traffic_read_cache,traffic_read_p2,NULL,
        &traffic_scratch,&kui_armada_ce_maple_hook_log);
#if KUI_ARMADA_CE_VMU_WRITE_RUNTIME
    /* The same DMA-off window survives unmasked service. Re-walk the complete
     * capture anyway: no owner, mapping, request or header may change while
     * the original source consumer is withheld. */
    if(resuming && (result || !a2_same_words(&traffic_scratch,&a2_continuation.capture,
       offsetof(struct kui_armada_ce_maple_traffic_scratch,reads))))
        return a2_terminal(107u,result);
#endif
#if KUI_ARMADA_CE_MAPLE_FENCE
    bool admitted=result==KUI_ARMADA_CE_MAPLE_TRAFFIC_OK && !traffic_scratch.word[0];
    uint32_t mmio_va=0,mmio_page=0,mmio_status=UINT32_MAX;
    if(admitted) {
        uint32_t slot=(((process-0x8c14e6e0u)/0x9cu)+1u)<<25,va=0;
        struct kui_armada_ce_maple_mapping mapping;
        if(!kui_armada_ce_maple_owner_pointer(traffic_scratch.word[5],slot,0x100u,&va)) admitted=false;
        else {
            /* The restricted RAM resolver retains all repeated PTE witnesses
             * when refusing this deliberately non-RAM MMIO destination. */
            unsigned mapped=kui_armada_ce_maple_resolve(ttb,va,traffic_read_p1,NULL,&mapping);
            mmio_va=va;mmio_page=mapping.page_word;mmio_status=mapped;
            admitted=mapped==KUI_ARMADA_CE_RAM_BIND_DESTINATION &&
                (mapping.page_word&9u)==1u &&
                ((mapping.page_word&0x1ffff000u)|(va&0xfffu))==0x005f6c00u;
        }
    }
    unsigned fence_result=
#if KUI_ARMADA_CE_VMU_WRITE_RUNTIME
        resuming?KUI_ARMADA_CE_MAPLE_FENCE_OK:
#endif
        kui_armada_ce_maple_fence_pre(raw,traffic_scratch.word[1],original_pr,admitted,
        fence_read_mmio,fence_write_mmio,NULL,&fence_state,&kui_armada_ce_maple_fence_log);
#if KUI_ARMADA_CE_VMU_WRITE_RUNTIME
    if(resuming && (!admitted || fence_result || !fence_state.active || !fence_state.pending))
        return a2_terminal(108u,fence_result);
#endif
#if KUI_ARMADA_CE_MAPLE_A2
    if(!fence_result && fence_state.active && fence_state.pending) a2_run(ttb,process,ccr);
#if KUI_ARMADA_CE_VMU_WRITE_RUNTIME
    if(kui_armada_ce_vmu_a2_log.fault[0]) return a2_terminal(109u,kui_armada_ce_vmu_a2_log.fault[0]);
    if(!fence_result && fence_state.active && fence_state.pending &&
       a2_continuation.result==KUI_ARMADA_CE_VMU_A2_PUBLISH_PENDING && a2_continuation.checked)
        return a2_pending();
    if(resuming) {
        if(!a2_continuation.checked || a2_continuation.result!=KUI_ARMADA_CE_VMU_A2_PUBLISH_OK)
            return a2_terminal(110u,a2_continuation.result);
        a2_continuation.pending=0;
    }
#endif
#if KUI_ARMADA_CE_MAPLE_AUTO_STOP
    a2_auto_capture(KUI_ARMADA_CE_MAPLE_AUTO_BEFORE_ORIGINAL);
#endif
#else
    (void)fence_result;
#endif
    if(!admitted && result==KUI_ARMADA_CE_MAPLE_TRAFFIC_OK &&
       kui_armada_ce_maple_fence_log.fault[0]==KUI_ARMADA_CE_MAPLE_FENCE_ADMISSION) {
        kui_armada_ce_maple_fence_log.fault[1]=mmio_va;
        kui_armada_ce_maple_fence_log.fault[2]=mmio_page;
        kui_armada_ce_maple_fence_log.fault[3]=0x005f6c00u;
        kui_armada_ce_maple_fence_log.fault[4]=mmio_status;
    }
#else
    (void)result;
#endif
    TRAFFIC_DONE();
}
#undef TRAFFIC_DONE
