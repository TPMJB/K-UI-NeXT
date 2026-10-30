/* SPDX-License-Identifier: GPL-3.0-only */
/* Authored allocator snapshots; original SH allocator execution is tested
 * separately against owner-supplied startup images. */
#include "kui/vmu_native_heap.h"
#include "kui/vmu_native_lifetime.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do {++checks;assert(x);} while(0)
#ifndef KUI_NATIVE_HEAP_TEST_LEASE_BYTES
#define KUI_NATIVE_HEAP_TEST_LEASE_BYTES 0x60000u
#endif
enum {LEASE=KUI_NATIVE_HEAP_TEST_LEASE_BYTES};
struct cell {uint32_t address,value;};
struct memory {struct cell cell[64];unsigned count,reads;uint32_t fail;};
static uint32_t phys(uint32_t a) {return a&UINT32_C(0x1fffffff);}
static void put(struct memory *m,uint32_t address,uint32_t value) {
    for(unsigned i=0;i<m->count;i++) if(m->cell[i].address==phys(address)) {
        m->cell[i].value=value;return;
    }
    CHECK(m->count<64u);m->cell[m->count++]=(struct cell){phys(address),value};
}
static bool read_word(void *context,uint32_t address,uint32_t *value) {
    struct memory *m=context;uint32_t area=address&UINT32_C(0xe0000000),at=phys(address);
    ++m->reads;
    CHECK((area==0u || area==0x80000000u || area==0xa0000000u) &&
        !(at&3u) && at>=0x0c000000u && at<=0x0cfffffcu);
    if(m->fail==at) return false;
    for(unsigned i=0;i<m->count;i++) if(m->cell[i].address==at) {
        *value=m->cell[i].value;return true;
    }
    return false;
}
static struct kui_vmu_native_heap owner(uint32_t alias) {
    return (struct kui_vmu_native_heap){
        .version=1u,.bytes=sizeof(struct kui_vmu_native_heap),.family=1u,.layout=1u,
        .image_bytes=0x3000u,.load=alias|0x0c010000u,
        .initializer=alias|0x0c010800u,.initializer_slot=alias|0x0c010100u,
        .initializer_target=alias|0x0c010800u,.allocate=alias|0x0c0108e2u,
        .release=alias|0x0c0109b0u,.dispatch_cell=alias|0x0c2ffffcu,
        .dispatch_table=alias|0x0c012100u,.sentinel=alias|0x0c300000u,
        .cursor_cell=alias|0x0c300020u,.begin_cell=alias|0x0c300024u,
        .bytes_cell=alias|0x0c300028u,.begin_copy_cell=alias|0x0c30002cu};
}
static void fresh_state(struct memory *m,const struct kui_vmu_native_heap *h,
    uint32_t begin,uint32_t bytes) {
    memset(m,0,sizeof(*m));
    put(m,h->begin_cell,begin);put(m,h->begin_copy_cell,begin);put(m,h->bytes_cell,bytes);
    put(m,h->dispatch_cell,h->dispatch_table);put(m,h->dispatch_table,h->allocate);
    put(m,h->dispatch_table+4u,h->release);put(m,h->sentinel,begin);put(m,h->sentinel+4u,0u);
    put(m,h->cursor_cell,h->sentinel);put(m,begin,h->sentinel);put(m,begin+4u,bytes/32u);
}
static uint32_t upper(const struct kui_vmu_native_heap_lease *l) {
    return l->begin+LEASE+32u;
}
static uint32_t placeholder(const struct kui_vmu_native_heap_lease *l) {
    return upper(l)+32u;
}
static void placeholder_state(struct memory *m,const struct kui_vmu_native_heap_lease *l) {
    put(m,l->begin+4u,LEASE/32u+1u);
    put(m,upper(l)+4u,l->heap_bytes/32u-(LEASE/32u+1u));
}
static void allocated_state(struct memory *m,const struct kui_vmu_native_heap_lease *l) {
    put(m,l->owner.sentinel,l->owner.sentinel);
}
static void ready_state(struct memory *m,const struct kui_vmu_native_heap_lease *l) {
    put(m,l->owner.sentinel,upper(l));put(m,upper(l),l->owner.sentinel);
}
static struct kui_vmu_native_heap_lease state(struct memory *m,uint32_t alias,
    uint32_t bytes,unsigned phase) {
    struct kui_vmu_native_heap h=owner(alias);struct kui_vmu_native_heap_lease l;
    uint32_t begin=alias|0x0c400000u;
    fresh_state(m,&h,begin,bytes);
    CHECK(kui_vmu_native_heap_bottom_fresh(&h,read_word,m,begin,bytes,1u,LEASE,&l));
    CHECK(l.phase==1u && l.header==begin && l.payload==begin+32u &&
        l.lease_bytes==LEASE && l.units==LEASE/32u+1u &&
        l.remaining==bytes/32u-l.units);
    if(phase>=1u) {
        placeholder_state(m,&l);
        CHECK(kui_vmu_native_heap_bottom_placeholder(&l,read_word,m,1u,placeholder(&l)));
        CHECK(l.phase==3u);
    }
    if(phase>=2u) {
        allocated_state(m,&l);
        CHECK(kui_vmu_native_heap_bottom_allocated(&l,read_word,m,1u,l.payload));
        CHECK(l.phase==2u);
    }
    if(phase>=3u) {
        ready_state(m,&l);CHECK(kui_vmu_native_heap_bottom_ready(&l,read_word,m,1u));
    }
    return l;
}
static void normal_sequence(uint32_t alias,uint32_t bytes) {
    struct memory m;struct kui_vmu_native_heap_lease l=state(&m,alias,bytes,0u);
    struct kui_vmu_native_heap_lease unchanged=l;
    CHECK(kui_vmu_native_heap_preallocate(&l,read_word,&m,1u));
    CHECK(!kui_vmu_native_heap_lease_valid(&l,read_word,&m,1u));
    CHECK(!kui_vmu_native_heap_bottom_ready(&l,read_word,&m,1u));
    CHECK(!memcmp(&l,&unchanged,sizeof(l)));
    placeholder_state(&m,&l);
    CHECK(kui_vmu_native_heap_bottom_placeholder(&l,read_word,&m,1u,placeholder(&l)));
    CHECK(!kui_vmu_native_heap_lease_identity_valid(&l,read_word,&m,1u));
    allocated_state(&m,&l);
    CHECK(kui_vmu_native_heap_bottom_allocated(&l,read_word,&m,1u,l.payload));
    CHECK(kui_vmu_native_heap_lease_valid(&l,read_word,&m,1u));
    uint32_t released=0;
    CHECK(kui_vmu_native_heap_lease_valid_with_release(&l,read_word,&m,1u,placeholder(&l),&released));
    CHECK(released==bytes-LEASE-32u);
    CHECK(!kui_vmu_native_heap_bottom_ready(&l,read_word,&m,1u));
    ready_state(&m,&l);
    CHECK(kui_vmu_native_heap_bottom_ready(&l,read_word,&m,1u));
    CHECK(kui_vmu_native_heap_lease_valid(&l,read_word,&m,1u));
    CHECK(!kui_vmu_native_lifetime_release_safe(&l,read_word,&m,1u,l.payload));
    CHECK(!kui_vmu_native_lifetime_release_safe(&l,read_word,&m,1u,placeholder(&l)));

    /* A normal positive or zero-sized allocation still comes from the
     * original upper boundary. Its release is admitted only while allocated. */
    const uint32_t requests[]={0u,32u,1024u,0x20000u};
    for(unsigned i=0;i<sizeof(requests)/sizeof(requests[0]);i++) {
        uint32_t units=requests[i]/32u+1u;
        if(units>l.remaining) continue;
        uint32_t header=l.begin+bytes-units*32u,payload=header+32u;
        put(&m,header+4u,units);
        if(units==l.remaining) put(&m,l.owner.sentinel,l.owner.sentinel);
        else put(&m,upper(&l)+4u,l.remaining-units);
        CHECK(payload==l.begin+bytes-requests[i]);
        CHECK(kui_vmu_native_heap_lease_valid(&l,read_word,&m,1u));
        CHECK(kui_vmu_native_lifetime_release_safe(&l,read_word,&m,1u,payload));
        struct kui_vmu_native_lifetime_plan p;
        CHECK(kui_vmu_native_lifetime_prepare(&p,&l,read_word,&m,1u,NULL,0u));
        CHECK(kui_vmu_native_lifetime_release_prepared(&p,read_word,&m,1u,payload));
        CHECK(!kui_vmu_native_lifetime_release_prepared(&p,read_word,&m,1u,l.payload));
        put(&m,l.owner.sentinel,upper(&l));put(&m,upper(&l)+4u,l.remaining);
        CHECK(kui_vmu_native_heap_bottom_ready(&l,read_word,&m,1u));
    }
}
static void transition_refusals(void) {
    struct memory m;struct kui_vmu_native_heap_lease l;
    for(unsigned step=0u;step<3u;step++) {
        l=state(&m,0x80000000u,0x100000u,step);
        uint32_t wrong[]={0u,l.payload-32u,l.payload+32u,
            (l.payload&0x1fffffffu)|0xa0000000u,placeholder(&l)+32u};
        for(unsigned i=0;i<sizeof(wrong)/sizeof(wrong[0]);i++) {
            struct kui_vmu_native_heap_lease trial=l;
            if(step==0u) CHECK(!kui_vmu_native_heap_bottom_placeholder(&trial,read_word,&m,1u,wrong[i]));
            else if(step==1u) CHECK(!kui_vmu_native_heap_bottom_allocated(&trial,read_word,&m,1u,wrong[i]));
            else CHECK(!kui_vmu_native_heap_postallocate(&trial,read_word,&m,1u,wrong[i]));
            CHECK(trial.phase==0u);
        }
    }
    /* Every transition has an exact predecessor. A retry or skipped
     * operation cannot manufacture a runtime lease from a stale snapshot. */
    for(unsigned phase=0u;phase<5u;phase++) {
        l=state(&m,0x80000000u,0x100000u,0u);l.phase=phase;
        CHECK(!kui_vmu_native_heap_bottom_allocated(&l,read_word,&m,1u,l.payload));
        CHECK(l.phase==0u);
        l=state(&m,0x80000000u,0x100000u,1u);l.phase=phase;
        if(phase!=1u) {
            CHECK(!kui_vmu_native_heap_bottom_placeholder(&l,read_word,&m,1u,placeholder(&l)));
            CHECK(l.phase==0u);
        }
    }
    /* Old tail APIs and runtime proofs retain their existing geometry. */
    struct kui_vmu_native_heap h=owner(0x80000000u);
    fresh_state(&m,&h,0x8c400000u,0x100000u);
    CHECK(kui_vmu_native_heap_fresh(&h,read_word,&m,0x8c400000u,0x100000u,1u,LEASE,&l));
    CHECK(l.payload==0x8c500000u-LEASE);
    struct kui_vmu_native_heap_lease trial=l;
    CHECK(!kui_vmu_native_heap_bottom_placeholder(&trial,read_word,&m,1u,placeholder(&trial)));
    put(&m,l.begin+4u,l.remaining);put(&m,l.header+4u,l.units);
    CHECK(kui_vmu_native_heap_postallocate(&l,read_word,&m,1u,l.payload));
    CHECK(kui_vmu_native_heap_lease_valid(&l,read_word,&m,1u));
    CHECK(!kui_vmu_native_heap_bottom_ready(&l,read_word,&m,1u));
}
static void witness_refusals(void) {
    for(unsigned step=0u;step<4u;step++) {
        struct memory baseline;struct kui_vmu_native_heap_lease initial=state(&baseline,0x80000000u,0x100000u,step);
        if(step==0u) placeholder_state(&baseline,&initial);
        if(step==1u) allocated_state(&baseline,&initial);
        if(step==2u) ready_state(&baseline,&initial);
        /* Discover exactly the required witness set independently through
         * refusal-on-read. Every required read must fail closed. */
        unsigned refusals=0u;
        for(unsigned i=0u;i<baseline.count;i++) {
            struct memory m=baseline;struct kui_vmu_native_heap_lease l=initial;
            m.fail=m.cell[i].address;bool ok;
            if(step==0u) ok=kui_vmu_native_heap_bottom_placeholder(&l,read_word,&m,1u,placeholder(&l));
            else if(step==1u) ok=kui_vmu_native_heap_bottom_allocated(&l,read_word,&m,1u,l.payload);
            else ok=kui_vmu_native_heap_bottom_ready(&l,read_word,&m,1u);
            if(ok) continue;
            ++refusals;
            if(step<2u) CHECK(l.phase==0u);
            m=baseline;l=initial;m.cell[i].value^=1u;
            if(step==0u) CHECK(!kui_vmu_native_heap_bottom_placeholder(&l,read_word,&m,1u,placeholder(&l)));
            else if(step==1u) CHECK(!kui_vmu_native_heap_bottom_allocated(&l,read_word,&m,1u,l.payload));
            else CHECK(!kui_vmu_native_heap_bottom_ready(&l,read_word,&m,1u));
        }
        CHECK(refusals>=11u);
    }
    struct memory m;struct kui_vmu_native_heap_lease l=state(&m,0x80000000u,0x100000u,3u);
    for(uint32_t offset=64u;offset<0x100000u-LEASE;offset+=32u) {
        struct kui_vmu_native_heap_lease changed=l;
        changed.header=l.begin+offset;changed.payload=changed.header+32u;
        CHECK(!kui_vmu_native_heap_lease_identity_valid(&changed,read_word,&m,1u));
    }
    for(unsigned n=0u;n<4u;n++) if(n!=1u) {
        CHECK(!kui_vmu_native_heap_lease_valid(&l,read_word,&m,n));
        CHECK(!kui_vmu_native_heap_bottom_ready(&l,read_word,&m,n));
    }
    /* A free block that includes even the retained header is never owned. */
    put(&m,l.owner.sentinel,l.begin);put(&m,l.begin,l.owner.sentinel);
    CHECK(!kui_vmu_native_heap_lease_valid(&l,read_word,&m,1u));
    CHECK(!kui_vmu_native_heap_bottom_ready(&l,read_word,&m,1u));
}
static void fragmented_runtime(void) {
    struct memory m;struct kui_vmu_native_heap_lease l=state(&m,0x80000000u,0x100000u,3u);
    uint32_t first=upper(&l),second=first+96u,payload=first+64u;
    put(&m,first,second);put(&m,first+4u,1u);put(&m,first+36u,2u);
    put(&m,second,l.owner.sentinel);put(&m,second+4u,l.remaining-3u);
    put(&m,l.owner.cursor_cell,first);
    CHECK(kui_vmu_native_heap_lease_valid(&l,read_word,&m,1u));
    CHECK(kui_vmu_native_lifetime_release_safe(&l,read_word,&m,1u,payload));
    CHECK(!kui_vmu_native_heap_bottom_ready(&l,read_word,&m,1u));
    struct kui_vmu_native_lifetime_plan p;
    CHECK(kui_vmu_native_lifetime_prepare(&p,&l,read_word,&m,1u,NULL,0u));
    CHECK(kui_vmu_native_lifetime_release_prepared(&p,read_word,&m,1u,payload));
    put(&m,first+36u,4u); /* Enlarged allocated chunk now overlaps free memory. */
    CHECK(!kui_vmu_native_lifetime_release_safe(&l,read_word,&m,1u,payload));
    CHECK(!kui_vmu_native_lifetime_release_prepared(&p,read_word,&m,1u,payload));
}
static void fresh_refusals(void) {
    struct memory m;struct kui_vmu_native_heap h=owner(0x80000000u);
    struct kui_vmu_native_heap_lease l,zero={0};
    const uint32_t sizes[]={0u,32u,LEASE,LEASE+32u,LEASE+63u,0xffffffe0u};
    for(unsigned i=0u;i<sizeof(sizes)/sizeof(sizes[0]);i++) {
        fresh_state(&m,&h,0x8c400000u,sizes[i]);memset(&l,0xff,sizeof(l));
        CHECK(!kui_vmu_native_heap_bottom_fresh(&h,read_word,&m,0x8c400000u,sizes[i],1u,LEASE,&l));
        CHECK(!memcmp(&l,&zero,sizeof(l)));
    }
    const uint32_t leases[]={0u,1u,31u,LEASE+1u,0x100000u,0xffffffe0u};
    for(unsigned i=0u;i<sizeof(leases)/sizeof(leases[0]);i++) {
        fresh_state(&m,&h,0x8c400000u,0x100000u);memset(&l,0xff,sizeof(l));
        CHECK(!kui_vmu_native_heap_bottom_fresh(&h,read_word,&m,0x8c400000u,0x100000u,1u,leases[i],&l));
        CHECK(!memcmp(&l,&zero,sizeof(l)));
    }
    const uint32_t begins[]={0x8c400001u,0x8c400004u,0x8cf80000u,0x8d000000u,
        0x4c400000u,0x8c010000u,0x8c300000u};
    for(unsigned i=0u;i<sizeof(begins)/sizeof(begins[0]);i++) {
        fresh_state(&m,&h,begins[i],0x100000u);memset(&l,0xff,sizeof(l));
        CHECK(!kui_vmu_native_heap_bottom_fresh(&h,read_word,&m,begins[i],0x100000u,1u,LEASE,&l));
        CHECK(!memcmp(&l,&zero,sizeof(l)));
    }
    for(unsigned n=0u;n<4u;n++) if(n!=1u) {
        fresh_state(&m,&h,0x8c400000u,0x100000u);
        CHECK(!kui_vmu_native_heap_bottom_fresh(&h,read_word,&m,0x8c400000u,0x100000u,n,LEASE,&l));
    }
    CHECK(!kui_vmu_native_heap_bottom_fresh(&h,read_word,&m,0x8c400000u,0x100000u,1u,LEASE,NULL));
    CHECK(!kui_vmu_native_heap_bottom_placeholder(NULL,read_word,&m,1u,0u));
    CHECK(!kui_vmu_native_heap_bottom_allocated(NULL,read_word,&m,1u,0u));
    CHECK(!kui_vmu_native_heap_bottom_ready(NULL,read_word,&m,1u));
}
int main(void) {
    _Static_assert(sizeof(struct kui_vmu_native_heap_lease)==116u,"Lease ABI remains unchanged");
    const uint32_t aliases[]={0u,0x80000000u,0xa0000000u};
    for(unsigned a=0u;a<3u;a++) {
        normal_sequence(aliases[a],LEASE+64u);
        normal_sequence(aliases[a],0x100000u);
        normal_sequence(aliases[a],0x800000u);
    }
    transition_refusals();witness_refusals();fragmented_runtime();fresh_refusals();
    puts("bottom heap reservation checks passed");printf("%u checks\n",checks);return 0;
}
