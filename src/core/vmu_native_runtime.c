/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_native_runtime.h"

struct span {uint32_t begin,bytes;};
static uint32_t physical(uint32_t a) {return a&UINT32_C(0x1fffffff);}
static bool ram(uint32_t a,uint32_t bytes,unsigned alignment,struct span *s) {
    uint32_t area=a&UINT32_C(0xe0000000),p=physical(a);
    if((area!=0u && area!=UINT32_C(0x80000000) && area!=UINT32_C(0xa0000000)) ||
       !bytes || p<UINT32_C(0x0c000000) || p>=UINT32_C(0x0d000000) ||
       bytes>UINT32_C(0x0d000000)-p || (p&(alignment-1u))) return false;
    s->begin=p;s->bytes=bytes;return true;
}
static bool overlap(struct span a,struct span b) {
    return a.begin<b.begin+b.bytes && b.begin<a.begin+a.bytes;
}
static bool in(struct span outer,uint32_t address,uint32_t bytes) {
    return address>=outer.begin && address-outer.begin<outer.bytes &&
        bytes<=outer.bytes-(address-outer.begin);
}
static bool p2(uint32_t a) {return (a&UINT32_C(0xe0000000))==UINT32_C(0xa0000000);}
bool kui_vmu_native_timer_callback_owned(const struct kui_vmu_native_discovery *d,uint32_t callback) {
    if(!d) return false;
    if(!callback) return true;
    struct span code,target;
    return ram(d->load_address,d->image_bytes,2u,&code) &&
        ram(callback,2u,2u,&target) && in(code,target.begin,2u);
}

enum kui_vmu_native_runtime_result kui_vmu_native_runtime_check(
    const struct kui_vmu_native_discovery *d,const struct kui_vmu_native_runtime_config *c,
    const struct kui_vmu_native_runtime_boundary *b,kui_vmu_native_runtime_read read,
    void *context,struct kui_vmu_native_runtime_snapshot *snapshot) {
    struct span executable,lease,guard,manager_cell,lock_cell,initial,gun,timer1,timer2,callsite;
    if(!d || !c || !b || !read || !snapshot ||
       d->version!=KUI_VMU_NATIVE_DISCOVERY_VERSION || d->bytes!=sizeof(*d) ||
       !kui_vmu_native_family_valid(d->family) ||
       d->requirements!=KUI_VMU_NATIVE_REQUIREMENTS ||
       c->version!=KUI_VMU_NATIVE_RUNTIME_VERSION || c->bytes!=sizeof(*c) ||
       b->phase>KUI_VMU_NATIVE_BEFORE_CONSUMER || c->rx_layout>KUI_VMU_NATIVE_RX_EXPLICIT48 ||
       !ram(d->load_address,d->image_bytes,4u,&executable) ||
       !ram(c->lease_address,c->lease_bytes,32u,&lease) ||
       !c->required_lease_bytes || c->required_lease_bytes>c->lease_bytes ||
       c->lease_header_bytes>lease.begin-UINT32_C(0x0c000000) ||
       !ram(c->lease_address-c->lease_header_bytes,c->lease_bytes+c->lease_header_bytes,1u,&guard) ||
       !ram(d->manager_cell,4u,4u,&manager_cell) ||
       !ram(d->lock_cell,5u,4u,&lock_cell) ||
       !ram(d->manager_initial,0x1100u,4u,&initial) ||
       !in(executable,lock_cell.begin,lock_cell.bytes) ||
       !ram(c->gun_flag,4u,4u,&gun) || !ram(c->timer1_callback,4u,4u,&timer1) ||
       !ram(c->timer2_callback,4u,4u,&timer2) ||
       !ram(d->completion_call,6u,2u,&callsite) || !in(executable,callsite.begin,callsite.bytes) ||
       physical(d->manager_cell)!=initial.begin+0x10fcu ||
       overlap(gun,timer1) || overlap(gun,timer2) || overlap(timer1,timer2))
        return KUI_VMU_NATIVE_RUNTIME_INVALID;
    if(overlap(guard,executable) || overlap(guard,initial) ||
       overlap(guard,manager_cell) || overlap(guard,lock_cell) ||
       overlap(guard,gun) || overlap(guard,timer1) || overlap(guard,timer2))
        return KUI_VMU_NATIVE_RUNTIME_CONFLICT;
    /* An early server does not mask interrupts; its API wrappers may run it
     * at their own level. It holds the TAS lock instead, checked below. */
    unsigned mask=(b->inherited_sr>>4)&15u;
    bool masked=mask==6u || mask==15u || d->family==KUI_VMU_NATIVE_FAMILY_EARLY_V1;
    struct span returnsite;
    if(((b->worker_sr>>4)&15u)!=15u || !masked ||
       (b->phase==KUI_VMU_NATIVE_BEFORE_CONSUMER &&
        (!ram(b->inherited_pr,2u,2u,&returnsite) ||
         !in(executable,returnsite.begin,returnsite.bytes) ||
         returnsite.begin!=callsite.begin+4u)))
        return KUI_VMU_NATIVE_RUNTIME_CONTEXT;
    struct span source,dma;
    if(b->sdk_source_physical!=physical(b->sdk_source_physical) ||
       b->dma_source_physical!=physical(b->dma_source_physical) ||
       b->dma_bytes<12u || b->dma_bytes>KUI_VMU_NATIVE_TX_BANK_BYTES || (b->dma_bytes&3u) ||
       !ram(b->sdk_source_physical,KUI_VMU_NATIVE_TX_BANK_BYTES,32u,&source) ||
       !ram(b->dma_source_physical,b->dma_bytes,32u,&dma) ||
       (b->dma_source_physical!=b->sdk_source_physical &&
        (b->phase!=KUI_VMU_NATIVE_BEFORE_CONSUMER || !in(lease,dma.begin,dma.bytes))))
        return KUI_VMU_NATIVE_RUNTIME_POINTER;

    uint32_t value,manager,lock,tx,rx;
#define READ(address,width,target) do {if(!read(context,(address),(width),&(target))) \
    return KUI_VMU_NATIVE_RUNTIME_READ;} while(0)
#define EXPECT(address,expected) do {READ((address),4u,value);if(value!=(expected)) \
    return KUI_VMU_NATIVE_RUNTIME_STATE;} while(0)
    /* Observe idle/software-trigger state before following mutable pointers. */
    EXPECT(UINT32_C(0xa05f6c18),0u);EXPECT(UINT32_C(0xa05f6c10),0u);
    EXPECT(UINT32_C(0xa05f6c14),1u);EXPECT(UINT32_C(0xa05f6c04),dma.begin);
    READ(d->manager_cell,4u,manager);READ(d->lock_cell,4u,lock);
    struct span actual_manager,actual_lock;
    if(!ram(manager,0x1100u,4u,&actual_manager) || actual_manager.begin!=initial.begin ||
       lock!=kui_vmu_native_lock_pointer(d) || !ram(lock,1u,1u,&actual_lock))
        return KUI_VMU_NATIVE_RUNTIME_POINTER;
    READ(manager+0xfc4u,4u,tx);READ(manager+0xfc8u,4u,rx);
    struct span tx_span,rx_span;
    if(!p2(tx) || !p2(rx) ||
       !ram(tx,2u*KUI_VMU_NATIVE_TX_BANK_BYTES,32u,&tx_span) ||
       !ram(rx,c->rx_layout==KUI_VMU_NATIVE_RX_UNIFORM48?
           KUI_VMU_NATIVE_RX_SLOTS*KUI_VMU_NATIVE_RX_SLOT_BYTES:4u,32u,&rx_span) ||
       (c->rx_layout==KUI_VMU_NATIVE_RX_EXPLICIT48 && rx!=c->rx_base))
        return KUI_VMU_NATIVE_RUNTIME_POINTER;
    if(overlap(tx_span,rx_span) || overlap(tx_span,guard) || overlap(rx_span,guard) ||
       overlap(tx_span,actual_manager) || overlap(rx_span,actual_manager) ||
       overlap(tx_span,lock_cell) || overlap(rx_span,lock_cell) ||
       overlap(tx_span,gun) || overlap(tx_span,timer1) || overlap(tx_span,timer2) ||
       overlap(rx_span,gun) || overlap(rx_span,timer1) || overlap(rx_span,timer2))
        return KUI_VMU_NATIVE_RUNTIME_CONFLICT;
    unsigned bank;
    if(source.begin==tx_span.begin) bank=0u;
    else if(source.begin==tx_span.begin+KUI_VMU_NATIVE_TX_BANK_BYTES) bank=1u;
    else return KUI_VMU_NATIVE_RUNTIME_POINTER;
    EXPECT(manager+0x10a8u,tx);EXPECT(manager+0x10acu,tx+KUI_VMU_NATIVE_TX_BANK_BYTES);
    EXPECT(manager+0x10b0u,source.begin|UINT32_C(0xa0000000));
    EXPECT(manager+0x10b4u,bank^b->phase);EXPECT(manager+0x10b8u,bank^b->phase);
    EXPECT(manager+0x10c4u,0u);EXPECT(manager+0xfc0u,b->phase?0u:1u);
    EXPECT(manager+0x10f4u,UINT32_C(0xa05f6c00));
    READ(lock,1u,value);if(value!=0x80u) return KUI_VMU_NATIVE_RUNTIME_STATE;
    EXPECT(c->gun_flag,0u);
    READ(c->timer1_callback,4u,value);
    if(!kui_vmu_native_timer_callback_owned(d,value)) return KUI_VMU_NATIVE_RUNTIME_STATE;
    READ(c->timer2_callback,4u,value);
    if(!kui_vmu_native_timer_callback_owned(d,value)) return KUI_VMU_NATIVE_RUNTIME_STATE;
    unsigned nonzero=0u;
    for(unsigned i=0;i<KUI_VMU_NATIVE_RX_SLOTS;i++) {
        uint32_t expected=c->rx_layout==KUI_VMU_NATIVE_RX_UNIFORM48?
            rx+i*KUI_VMU_NATIVE_RX_SLOT_BYTES:c->rx_slots[i];
        EXPECT(manager+0xfe8u+i*4u,expected);
        if(!expected) continue;
        struct span slot;
        if(!p2(expected) || !ram(expected,KUI_VMU_NATIVE_RX_SLOT_BYTES,32u,&slot))
            return KUI_VMU_NATIVE_RUNTIME_POINTER;
        if(overlap(slot,guard) || overlap(slot,tx_span) || overlap(slot,actual_manager) ||
           overlap(slot,lock_cell) || overlap(slot,gun) || overlap(slot,timer1) || overlap(slot,timer2) ||
           (c->rx_layout==KUI_VMU_NATIVE_RX_EXPLICIT48 && overlap(slot,rx_span)))
            return KUI_VMU_NATIVE_RUNTIME_CONFLICT;
        if(c->rx_layout==KUI_VMU_NATIVE_RX_EXPLICIT48) for(unsigned j=0;j<i;j++) {
            if(!c->rx_slots[j]) continue;
            struct span previous={physical(c->rx_slots[j]),KUI_VMU_NATIVE_RX_SLOT_BYTES};
            if(overlap(slot,previous)) return KUI_VMU_NATIVE_RUNTIME_CONFLICT;
        }
        ++nonzero;
    }
    if(!nonzero) return KUI_VMU_NATIVE_RUNTIME_STATE;
    /* Catch a changed pointer/control word as well as the initial sample.
     * The caller's IMASK15 masks even a priority15 application timer while
     * these pointer/MMIO witnesses are inspected. Callbacks are never run by
     * this validator; the original SDK retains timer delivery between hooks. */
    EXPECT(d->manager_cell,manager);EXPECT(d->lock_cell,lock);
    EXPECT(UINT32_C(0xa05f6c04),dma.begin);EXPECT(UINT32_C(0xa05f6c10),0u);
    EXPECT(UINT32_C(0xa05f6c14),1u);EXPECT(UINT32_C(0xa05f6c18),0u);
#undef EXPECT
#undef READ
    *snapshot=(struct kui_vmu_native_runtime_snapshot){manager,lock,tx,rx,bank,
        source.begin,dma.begin,guard.begin,guard.bytes,nonzero};
    return KUI_VMU_NATIVE_RUNTIME_VALID;
}
