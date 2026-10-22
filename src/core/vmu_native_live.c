/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_native_live.h"
#include <string.h>
#define READY UINT32_C(0x4e4c5031)
static uint32_t physical(uint32_t a) {return a&UINT32_C(0x1fffffff);}
static bool alias(uint32_t a) {
    uint32_t area=a&UINT32_C(0xe0000000);
    return area==0u || area==UINT32_C(0x80000000) || area==UINT32_C(0xa0000000);
}
static bool separate(const void *a,size_t an,const void *b,size_t bn) {
    uintptr_t ap=(uintptr_t)a,bp=(uintptr_t)b;
    return an<=UINTPTR_MAX-ap && bn<=UINTPTR_MAX-bp && (ap+an<=bp || bp+bn<=ap);
}
enum kui_vmu_native_runtime_result kui_vmu_native_live_prepare(
    struct kui_vmu_native_live *out,const struct kui_vmu_native_layout *l,
    const struct kui_vmu_native_discovery *d,struct kui_vmu_native_runtime_config *c,
    const struct kui_vmu_native_runtime_boundary *b,kui_vmu_native_runtime_read read,
    void *context,struct kui_vmu_native_runtime_snapshot *snapshot,
    struct kui_vmu_native_layout_diagnostic *diagnostic) {
    if(!out || !l || !d || !c || !b || !read || !snapshot ||
       !separate(out,sizeof(*out),l,sizeof(*l)) || !separate(out,sizeof(*out),d,sizeof(*d)) ||
       !separate(out,sizeof(*out),c,sizeof(*c)) || !separate(out,sizeof(*out),b,sizeof(*b)) ||
       !separate(out,sizeof(*out),snapshot,sizeof(*snapshot)) ||
       (diagnostic && !separate(out,sizeof(*out),diagnostic,sizeof(*diagnostic))))
        return KUI_VMU_NATIVE_RUNTIME_INVALID;
    struct kui_vmu_native_runtime_config bound=*c;
    struct kui_vmu_native_runtime_snapshot sample;
    if(!kui_vmu_native_layout_bind_checked(l,d,read,context,&bound,diagnostic))
        return KUI_VMU_NATIVE_RUNTIME_INVALID;
    int r=kui_vmu_native_runtime_check(d,&bound,b,read,context,&sample);
    if(r!=KUI_VMU_NATIVE_RUNTIME_VALID) return r;
    if(sample.tx_base!=l->tx_base || sample.rx_base!=l->rx_base)
        return KUI_VMU_NATIVE_RUNTIME_STATE;
    struct kui_vmu_native_live p={0};
    p.self=(uintptr_t)out;p.ready=READY;p.manager_cell=d->manager_cell;
    p.manager=physical(d->manager_initial);p.lock_cell=d->lock_cell;p.lock=sample.lock;
    p.tx=sample.tx_base;p.rx=sample.rx_base;p.completion_return=physical(d->completion_call)+4u;
    p.lease_begin=physical(bound.lease_address);p.lease_bytes=bound.lease_bytes;
    p.guard_begin=sample.lease_guard_physical;p.guard_bytes=sample.lease_guard_bytes;
    p.gun=bound.gun_flag;p.timer1=bound.timer1_callback;p.timer2=bound.timer2_callback;
    if(l->rx_layout==KUI_VMU_NATIVE_RX_UNIFORM48) {
        p.input_count=2u;p.input_address[0]=l->input_array;p.input_value[0]=l->input_values[0];
        p.input_address[1]=l->index_array;p.input_value[1]=l->indices[0];
    } else {
        if(l->inputs>24u) return KUI_VMU_NATIVE_RUNTIME_INVALID;
        p.input_count=l->inputs;
        for(unsigned i=0;i<p.input_count;i++) {
            p.input_address[i]=l->input_array+4u*i;p.input_value[i]=l->input_values[i];
        }
    }
    memcpy(p.rx_slots,bound.rx_slots,sizeof(p.rx_slots));p.nonzero=sample.rx_slots_nonzero;
    *out=p;*c=bound;*snapshot=sample;return KUI_VMU_NATIVE_RUNTIME_VALID;
}
enum kui_vmu_native_runtime_result kui_vmu_native_live_check(
    const struct kui_vmu_native_live *p,const struct kui_vmu_native_runtime_boundary *b,
    kui_vmu_native_runtime_read read,void *context,struct kui_vmu_native_runtime_snapshot *snapshot) {
    if(!p || p->self!=(uintptr_t)p || p->ready!=READY || p->input_count>24u ||
       !b || !read || !snapshot || b->phase>KUI_VMU_NATIVE_BEFORE_CONSUMER)
        return KUI_VMU_NATIVE_RUNTIME_INVALID;
    unsigned mask=(b->inherited_sr>>4)&15u;
    if(((b->worker_sr>>4)&15u)!=15u || (mask!=6u && mask!=15u) ||
       (b->phase==KUI_VMU_NATIVE_BEFORE_CONSUMER &&
        (!alias(b->inherited_pr) || physical(b->inherited_pr)!=p->completion_return)))
        return KUI_VMU_NATIVE_RUNTIME_CONTEXT;
    unsigned bank;
    if(b->sdk_source_physical==physical(p->tx)) bank=0u;
    else if(b->sdk_source_physical==physical(p->tx)+KUI_VMU_NATIVE_TX_BANK_BYTES) bank=1u;
    else return KUI_VMU_NATIVE_RUNTIME_POINTER;
    uint32_t dma=b->dma_source_physical;
    if(dma!=physical(dma) || (dma&31u) || b->dma_bytes<12u ||
       b->dma_bytes>KUI_VMU_NATIVE_TX_BANK_BYTES || (b->dma_bytes&3u) ||
       (dma!=b->sdk_source_physical && (b->phase!=KUI_VMU_NATIVE_BEFORE_CONSUMER ||
        dma<p->lease_begin || dma-p->lease_begin>=p->lease_bytes ||
        b->dma_bytes>p->lease_bytes-(dma-p->lease_begin))))
        return KUI_VMU_NATIVE_RUNTIME_POINTER;
    uint32_t value,manager;
#define READ(address,width,target) do {if(!read(context,(address),(width),&(target))) \
    return KUI_VMU_NATIVE_RUNTIME_READ;} while(0)
#define EXPECT(address,expected) do {READ((address),4u,value);if(value!=(expected)) \
    return KUI_VMU_NATIVE_RUNTIME_STATE;} while(0)
    for(unsigned i=0;i<p->input_count;i++) EXPECT(p->input_address[i],p->input_value[i]);
    EXPECT(UINT32_C(0xa05f6c18),0u);EXPECT(UINT32_C(0xa05f6c10),0u);
    EXPECT(UINT32_C(0xa05f6c14),1u);EXPECT(UINT32_C(0xa05f6c04),dma);
    READ(p->manager_cell,4u,manager);
    if(!alias(manager) || physical(manager)!=p->manager) return KUI_VMU_NATIVE_RUNTIME_POINTER;
    EXPECT(p->lock_cell,p->lock);EXPECT(manager+0xfc4u,p->tx);EXPECT(manager+0xfc8u,p->rx);
    EXPECT(manager+0x10a8u,p->tx);EXPECT(manager+0x10acu,p->tx+KUI_VMU_NATIVE_TX_BANK_BYTES);
    EXPECT(manager+0x10b0u,b->sdk_source_physical|UINT32_C(0xa0000000));
    EXPECT(manager+0x10b4u,bank^b->phase);EXPECT(manager+0x10b8u,bank^b->phase);
    EXPECT(manager+0x10c4u,0u);EXPECT(manager+0xfc0u,b->phase?0u:1u);
    EXPECT(manager+0x10f4u,UINT32_C(0xa05f6c00));
    READ(p->lock,1u,value);if(value!=0x80u) return KUI_VMU_NATIVE_RUNTIME_STATE;
    EXPECT(p->gun,0u);EXPECT(p->timer1,0u);EXPECT(p->timer2,0u);
    for(unsigned i=0;i<KUI_VMU_NATIVE_RX_SLOTS;i++) EXPECT(manager+0xfe8u+i*4u,p->rx_slots[i]);
    for(unsigned i=0;i<p->input_count;i++) EXPECT(p->input_address[i],p->input_value[i]);
    EXPECT(p->manager_cell,manager);EXPECT(p->lock_cell,p->lock);
    EXPECT(UINT32_C(0xa05f6c04),dma);EXPECT(UINT32_C(0xa05f6c10),0u);
    EXPECT(UINT32_C(0xa05f6c14),1u);EXPECT(UINT32_C(0xa05f6c18),0u);
#undef EXPECT
#undef READ
    *snapshot=(struct kui_vmu_native_runtime_snapshot){manager,p->lock,p->tx,p->rx,bank,
        b->sdk_source_physical,dma,p->guard_begin,p->guard_bytes,p->nonzero};
    return KUI_VMU_NATIVE_RUNTIME_VALID;
}
