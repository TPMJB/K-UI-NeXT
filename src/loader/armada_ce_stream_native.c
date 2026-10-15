/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/target.h"
#include "kui/armada_ce_stream_native.h"
#include "kui/armada_ce_excerpt.h"
#include <stddef.h>

#if !defined(KUI_ARMADA_CE_STREAM) || KUI_ARMADA_CE_STREAM != 1 || !defined(KUI_RETAIL_CE)
#error "The native worker lease belongs only to the isolated Armada CE stream"
#endif

#define SR_MASK UINT32_C(0x700000f0)
#define SR_OWNER UINT32_C(0x40000000)
#define WAIT_OBJECT_0 0u
#define RWX_COMMITTED UINT32_C(0x37d)

#if defined(KUI_ON_CONSOLE)
static uint32_t native_call(void *context,uint32_t target,const uint32_t args[8]) {
    (void)context;return kui_armada_ce_excerpt_ce_call(target,args);
}
static bool native_read(void *context,uint32_t address,uint32_t *out) {
    uint32_t p1;(void)context;
    if(!out || !kui_armada_ce_ram_table_p1(address,&p1)) return false;
    *out=*(volatile const uint32_t *)(uintptr_t)p1;return true;
}
static bool native_write(void *context,uint32_t address,uint32_t value) {
    uint32_t p1;(void)context;
    if(!kui_armada_ce_ram_table_p1(address,&p1)) return false;
    *(volatile uint32_t *)(uintptr_t)p1=value;return true;
}
static bool native_publish(void *context,uint32_t address,uint32_t bytes) {
    uint32_t p1;(void)context;
    if((address&31u) || (bytes&31u) || !bytes || bytes>4096u ||
       !kui_armada_ce_ram_table_p1(address,&p1) || p1>0x8d000000u-bytes) return false;
    /* Only newly allocated caller-owned lines. The kernel zeroed this page
     * through this same P1 alias; no P0 cached store is made by the adapter. */
    for(uint32_t at=0;at<bytes;at+=32u) {
        uint32_t line=p1+at;
        __asm__ __volatile__("ocbp @%0" : : "r"(line) : "memory");
    }
    return true;
}
static uint32_t native_ttb(void *context) {
    (void)context;return *(volatile const uint32_t *)(uintptr_t)UINT32_C(0xff000008);
}
static uint32_t native_ccr(void *context) {
    (void)context;return *(volatile const uint32_t *)(uintptr_t)UINT32_C(0xff00001c);
}
static uint32_t native_sr(void *context) {
    uint32_t value;(void)context;
    __asm__ __volatile__("stc sr,%0" : "=r"(value));return value;
}
static const struct kui_armada_ce_stream_native_ops production={NULL,native_call,
    native_read,native_write,native_publish,native_ttb,native_ccr,native_sr};
#endif
static const struct kui_armada_ce_stream_native_ops *operations(
    const struct kui_armada_ce_stream_native_ops *ops) {
#if defined(KUI_ON_CONSOLE)
    if(!ops) ops=&production;
#endif
    return ops && ops->call && ops->read32 && ops->write32 && ops->publish_code &&
        ops->ttb && ops->ccr && ops->sr?ops:NULL;
}
static bool reject(struct kui_armada_ce_stream_native *s,uint32_t status) {
    if(s) s->status=status;
    return false;
}
static bool api_valid(const struct kui_armada_ce_live_stream_api *a) {
    if(!a || a->owner_process<KUI_ARMADA_CE_LIVE_PROCESS_BASE ||
       a->owner_process>=KUI_ARMADA_CE_LIVE_PROCESS_BASE+32u*KUI_ARMADA_CE_LIVE_PROCESS_STRIDE ||
       (a->owner_process-KUI_ARMADA_CE_LIVE_PROCESS_BASE)%KUI_ARMADA_CE_LIVE_PROCESS_STRIDE ||
       a->owner_slot!=((1u+(a->owner_process-KUI_ARMADA_CE_LIVE_PROCESS_BASE)/
           KUI_ARMADA_CE_LIVE_PROCESS_STRIDE)<<25) || !a->owner_handle || a->owner_handle==UINT32_MAX)
        return false;
    uint32_t p1;
    if(!kui_armada_ce_ram_table_p1(a->coredll_module,&p1)) return false;
    const uint32_t *words=(const uint32_t *)a;
    for(unsigned i=4u;i<sizeof(*a)/sizeof(uint32_t);i++)
        if(words[i]<0x10000u || words[i]>=0x80000000u || (words[i]&1u)) return false;
    return true;
}
static bool context_valid(struct kui_armada_ce_stream_native *s,
    const struct kui_armada_ce_stream_native_ops *o) {
    uint32_t current,slot,handle;
    if(!s || !o || !api_valid(&s->api) || (o->sr(o->context)&SR_MASK)!=SR_OWNER ||
       !o->read32(o->context,KUI_ARMADA_CE_LIVE_CURRENT_PROCESS,&current) || current!=s->api.owner_process ||
       !o->read32(o->context,s->api.owner_process+12u,&slot) || slot!=s->api.owner_slot ||
       !o->read32(o->context,s->api.owner_process+8u,&handle) || handle!=s->api.owner_handle)
        return reject(s,KUI_ARMADA_CE_STREAM_NATIVE_CONTEXT);
    return true;
}
static uint32_t invoke(struct kui_armada_ce_stream_native *s,
    const struct kui_armada_ce_stream_native_ops *o,uint32_t target,
    uint32_t a0,uint32_t a1,uint32_t a2,uint32_t a3,uint32_t a4,uint32_t a5) {
    uint32_t args[8]={a0,a1,a2,a3,a4,a5,0,0};
    s->last_result=o->call(o->context,target,args);return s->last_result;
}
/* Independently authored SH veneer, not bytes taken from a CE module. It owns
 * the Microsoft16-byte home area, saves callee registers/PR, asks the native
 * kernel for KMode and tests actual SR.MD before a P1 jump. STC SR is itself
 * privileged: the exact admitted NK's unconditional TRUE transition is a
 * prerequisite, not something this query can safely test in a foreign kernel.
 * Native ThreadBase
 * still owns DLL/TLS attach and ExitThread. Restore the old mode while PC is
 * still in the CE-owned P0 page, then return to ThreadBase normally. */
static __attribute__((noinline)) uint32_t veneer_word(unsigned i,uint32_t mode,uint32_t worker) {
    static const uint16_t instruction[32]={
        0x2f86,0x2f96,0x4f22,0x7ff0,0x6843,0xe401,0xd00c,0x400b,
        0x0009,0x6903,0x0002,0x4000,0x4000,0x8b06,0x6483,0xd009,
        0x400b,0x0009,0x6803,0xa001,0x0009,0xe8ff,0x6493,0xd004,
        0x400b,0x0009,0x6083,0x7f10,0x4f26,0x69f6,0x000b,0x68f6
    };
    if(i==16u) return mode;
    if(i==17u) return worker;
    return (uint32_t)instruction[2u*i]|((uint32_t)instruction[2u*i+1u]<<16);
}
static bool release_allocation(struct kui_armada_ce_stream_native *s,
    const struct kui_armada_ce_stream_native_ops *o) {
    if(!s->allocation) return true;
    if(s->thread || s->consumer_cs) return reject(s,KUI_ARMADA_CE_STREAM_NATIVE_FREE);
    if(!s->allocation_owner || (s->allocation&0xfffu) || s->allocation>=0x80000000u ||
       s->allocation_owner!=(s->api.owner_slot|(s->allocation&0x01ffffffu)))
        return reject(s,KUI_ARMADA_CE_STREAM_NATIVE_FREE);
    if(!context_valid(s,o)) return false;
    if(!invoke(s,o,s->api.virtual_free,s->allocation,0,0x8000u,0,0,0))
        return reject(s,KUI_ARMADA_CE_STREAM_NATIVE_FREE);
    s->allocation=s->allocation_owner=s->allocation_p1=s->page_word=0;return true;
}
bool kui_armada_ce_stream_native_start(struct kui_armada_ce_stream_native *s,
    const struct kui_armada_ce_live_stream_api *api,uint32_t worker,uint32_t argument,
    const struct kui_armada_ce_stream_native_ops *ops) {
    const struct kui_armada_ce_stream_native_ops *o=operations(ops);
    uint32_t p1;
    if(!s || !o || !api_valid(api) || s->thread || s->allocation || s->consumer_cs ||
       worker<0x8c010000u || worker>=0x8d000000u || (worker&1u) ||
       !kui_armada_ce_ram_table_p1(argument,&p1))
        return reject(s,KUI_ARMADA_CE_STREAM_NATIVE_ARGUMENT);
    s->api=*api;s->worker=worker;s->argument=argument;s->resumed=s->thread_id=0;
    if(!context_valid(s,o)) return false;
    s->allocation=invoke(s,o,s->api.virtual_alloc,0,4096u,0x3000u,0x40u,0,0);
    if(!s->allocation) return reject(s,KUI_ARMADA_CE_STREAM_NATIVE_ALLOCATION);
    uint32_t raw_slot=s->allocation&0xfe000000u;
    uint32_t offset=s->allocation&0x01ffffffu;
    if((s->allocation&0xfffu) || s->allocation>=0x80000000u || offset<0x10000u ||
       offset>0x01fff000u || (raw_slot && raw_slot!=s->api.owner_slot)) {
        /* An alien/non-page-shaped return is not an admitted allocation;
         * never ask VirtualFree to interpret that unproved address. */
        return reject(s,KUI_ARMADA_CE_STREAM_NATIVE_MAPPING);
    }
    s->allocation_owner=s->api.owner_slot|offset;
    uint32_t ttb=o->ttb(o->context),ccr=o->ccr(o->context);
    struct kui_armada_ce_ram_binding mapping;
    enum kui_armada_ce_ram_bind_status mapped=kui_armada_ce_ram_bind(ttb,s->allocation_owner,ccr,
        o->read32,o->context,&mapping);
    if((ccr&0xa1u)!=1u || (mapped!=KUI_ARMADA_CE_RAM_BIND_OK && mapped!=KUI_ARMADA_CE_RAM_BIND_CACHE) ||
       (mapping.page_word&0xfffu)!=RWX_COMMITTED || (mapping.physical_address&0xfffu) ||
       mapping.physical_address<KUI_ARMADA_CE_EXCERPT_RAM_FREE ||
       mapping.physical_address>KUI_ARMADA_CE_EXCERPT_POOL_BEGIN-4096u ||
       !kui_armada_ce_excerpt_pool_destination_safe(mapping.physical_address,4096u)) {
        s->status=KUI_ARMADA_CE_STREAM_NATIVE_MAPPING;
        (void)release_allocation(s,o);return false;
    }
    s->allocation_p1=mapping.physical_address;s->page_word=mapping.page_word;
    /* Admission covers the full page, then repeats the same committed mapping
     * before any code store. No optimistic partially mapped publication. */
    struct kui_armada_ce_ram_binding tail;
    mapped=kui_armada_ce_ram_bind(ttb,s->allocation_owner+4092u,ccr,o->read32,o->context,&tail);
    if((mapped!=KUI_ARMADA_CE_RAM_BIND_OK && mapped!=KUI_ARMADA_CE_RAM_BIND_CACHE) ||
       tail.page_word!=mapping.page_word || tail.physical_address!=mapping.physical_address+4092u ||
       o->ttb(o->context)!=ttb || o->ccr(o->context)!=ccr || !context_valid(s,o)) {
        s->status=KUI_ARMADA_CE_STREAM_NATIVE_MAPPING;
        (void)release_allocation(s,o);return false;
    }
    for(unsigned i=0;i<KUI_ARMADA_CE_STREAM_VENEER_BYTES/4u;i++)
        if(!o->write32(o->context,s->allocation_p1+4u*i,veneer_word(i,s->api.set_kmode,worker))) {
            s->status=KUI_ARMADA_CE_STREAM_NATIVE_PUBLICATION;
            (void)release_allocation(s,o);return false;
        }
    if(!o->publish_code(o->context,s->allocation_p1,KUI_ARMADA_CE_STREAM_VENEER_PUBLISH_BYTES) ||
       !invoke(s,o,s->api.flush_instruction_cache,s->api.owner_handle,s->allocation,
               KUI_ARMADA_CE_STREAM_VENEER_BYTES,0,0,0)) {
        s->status=KUI_ARMADA_CE_STREAM_NATIVE_PUBLICATION;
        (void)release_allocation(s,o);return false;
    }
    if(!context_valid(s,o)) { (void)release_allocation(s,o);return false; }
    s->thread=invoke(s,o,s->api.create_thread,0,0,s->allocation,argument,4u,
        (uint32_t)(uintptr_t)&s->thread_id);
    if(!s->thread || s->thread==UINT32_MAX) {
        s->thread=0;s->status=KUI_ARMADA_CE_STREAM_NATIVE_CREATE;
        (void)release_allocation(s,o);return false;
    }
    if(!invoke(s,o,s->api.set_thread_priority,s->thread,KUI_ARMADA_CE_STREAM_PRIORITY,0,0,0,0))
        return reject(s,KUI_ARMADA_CE_STREAM_NATIVE_PRIORITY);
    uint32_t resume=invoke(s,o,s->api.resume_thread,s->thread,0,0,0,0,0);
    s->resumed=resume!=UINT32_MAX;
    if(resume!=1u) return reject(s,KUI_ARMADA_CE_STREAM_NATIVE_RESUME);
    s->status=KUI_ARMADA_CE_STREAM_NATIVE_RUNNING;return true;
}
bool kui_armada_ce_stream_native_reap(struct kui_armada_ce_stream_native *s,
    const struct kui_armada_ce_stream_native_ops *ops) {
    const struct kui_armada_ce_stream_native_ops *o=operations(ops);
    if(!s || !o) return reject(s,KUI_ARMADA_CE_STREAM_NATIVE_ARGUMENT);
    if(s->consumer_cs) return reject(s,KUI_ARMADA_CE_STREAM_NATIVE_LOCK);
    if(!s->thread && !s->allocation) return true;
    if(!context_valid(s,o)) return false;
    if(s->thread) {
        if(invoke(s,o,s->api.wait_single,s->thread,0,0,0,0,0)!=WAIT_OBJECT_0)
            return reject(s,KUI_ARMADA_CE_STREAM_NATIVE_WAIT);
        if(!invoke(s,o,s->api.close_handle,s->thread,0,0,0,0,0))
            return reject(s,KUI_ARMADA_CE_STREAM_NATIVE_CLOSE);
        s->thread=0;
    }
    if(!release_allocation(s,o)) return false;
    s->resumed=0;s->status=KUI_ARMADA_CE_STREAM_NATIVE_COMPLETE;return true;
}
bool kui_armada_ce_stream_native_ticks(struct kui_armada_ce_stream_native *s,
    const struct kui_armada_ce_stream_native_ops *ops,uint32_t *out) {
    const struct kui_armada_ce_stream_native_ops *o=operations(ops);
    if(!s || !o || !out) return reject(s,KUI_ARMADA_CE_STREAM_NATIVE_ARGUMENT);
    if(s->consumer_cs) return reject(s,KUI_ARMADA_CE_STREAM_NATIVE_LOCK);
    if(!context_valid(s,o)) return false;
    *out=invoke(s,o,s->api.get_tick_count,0,0,0,0,0,0);return true;
}
bool kui_armada_ce_stream_native_sleep(struct kui_armada_ce_stream_native *s,
    const struct kui_armada_ce_stream_native_ops *ops,uint32_t milliseconds) {
    const struct kui_armada_ce_stream_native_ops *o=operations(ops);
    if(!s || !o || milliseconds>1000u) return reject(s,KUI_ARMADA_CE_STREAM_NATIVE_ARGUMENT);
    if(s->consumer_cs) return reject(s,KUI_ARMADA_CE_STREAM_NATIVE_LOCK);
    if(!context_valid(s,o)) return false;
    (void)invoke(s,o,s->api.sleep,milliseconds,0,0,0,0,0);return true;
}
bool kui_armada_ce_stream_native_write_begin(struct kui_armada_ce_stream_native *s,
    const struct kui_armada_ce_stream_native_ops *ops,uint32_t device) {
    const struct kui_armada_ce_stream_native_ops *o=operations(ops);
    if(!s || !o || device<0x10000u || device>=0x80000000u || (device&3u) ||
       (device&0xfe000000u)!=s->api.owner_slot || (device&0x01ffffffu)>0x01ffff7fu)
        return reject(s,KUI_ARMADA_CE_STREAM_NATIVE_ARGUMENT);
    if(s->consumer_cs) return reject(s,KUI_ARMADA_CE_STREAM_NATIVE_LOCK);
    if(!context_valid(s,o)) return false;
#if KUI_WINCE_CDDA_WORMS
    /* The older runtime has voice masks at+108, not a consumer CS. This
     * logical lease only forbids waits/Stop; the player serializes each
     * bounded host write with its own IRQ-masked frontier transaction. */
    s->consumer_cs=UINT32_MAX;return true;
#else
    uint32_t cs=device+108u;
    (void)invoke(s,o,s->api.enter_cs,cs,0,0,0,0,0);
    s->consumer_cs=cs;return true;
#endif
}
bool kui_armada_ce_stream_native_write_end(struct kui_armada_ce_stream_native *s,
    const struct kui_armada_ce_stream_native_ops *ops) {
    const struct kui_armada_ce_stream_native_ops *o=operations(ops);
    if(!s || !o || !s->consumer_cs) return reject(s,KUI_ARMADA_CE_STREAM_NATIVE_LOCK);
    if(!context_valid(s,o)) return false;
#if KUI_WINCE_CDDA_WORMS
    if(s->consumer_cs!=UINT32_MAX) return reject(s,KUI_ARMADA_CE_STREAM_NATIVE_LOCK);
#else
    (void)invoke(s,o,s->api.leave_cs,s->consumer_cs,0,0,0,0,0);
#endif
    s->consumer_cs=0;return true;
}
