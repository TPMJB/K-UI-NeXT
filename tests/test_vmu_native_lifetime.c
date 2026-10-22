/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_native_lifetime.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
struct cell {uint32_t address,value;};
static struct cell memory[4096];
static unsigned used,reads,fail_read,checks;
static struct kui_vmu_native_heap_lease lease;
static uint32_t candidate;
#define CHECK(expression) do {++checks;assert(expression);} while(0)
static uint32_t physical(uint32_t a) {return a&UINT32_C(0x1fffffff);}
static void put(uint32_t address,uint32_t value) {
    address=physical(address);
    for(unsigned i=0;i<used;i++) if(memory[i].address==address) {memory[i].value=value;return;}
    assert(used<sizeof(memory)/sizeof(memory[0]));memory[used++]=(struct cell){address,value};
}
static bool read_word(void *context,uint32_t address,uint32_t *value) {
    (void)context;++reads;
    if(fail_read && reads==fail_read) return false;
    assert(!(address&3u));address=physical(address);
    assert(address>=UINT32_C(0x0c000000) && address<=UINT32_C(0x0cfffffc));
    for(unsigned i=0;i<used;i++) if(memory[i].address==address) {*value=memory[i].value;return true;}
    return false;
}
static void fixture(uint32_t alias) {
    memset(memory,0,sizeof(memory));memset(&lease,0,sizeof(lease));used=reads=fail_read=0u;
    struct kui_vmu_native_heap *h=&lease.owner;
    *h=(struct kui_vmu_native_heap){.version=1u,.bytes=sizeof(*h),.family=1u,.layout=1u,
        .image_bytes=UINT32_C(0x200000),.load=UINT32_C(0x8c010000),
        .initializer=UINT32_C(0x8c012000),.allocate=UINT32_C(0x8c0120e2),.release=UINT32_C(0x8c0121b0),
        .dispatch_cell=UINT32_C(0x0c4ffffc),.dispatch_table=UINT32_C(0x0c011100),
        .sentinel=UINT32_C(0x0c500000),.cursor_cell=UINT32_C(0x0c500020),
        .begin_cell=UINT32_C(0x0c500024),.bytes_cell=UINT32_C(0x0c500028),.begin_copy_cell=UINT32_C(0x0c50002c)};
    lease.version=1u;lease.bytes=sizeof(lease);lease.phase=2u;lease.initializations=1u;
    lease.begin=alias|UINT32_C(0x0c600000);lease.heap_bytes=UINT32_C(0x100000);
    lease.lease_bytes=UINT32_C(0x30000);lease.payload=alias|UINT32_C(0x0c6d0000);
    lease.header=lease.payload-32u;lease.units=UINT32_C(0x1801);lease.remaining=UINT32_C(0x67ff);
    candidate=alias|UINT32_C(0x0c680020);
    put(h->begin_cell,lease.begin);put(h->begin_copy_cell,lease.begin);put(h->bytes_cell,lease.heap_bytes);
    put(h->dispatch_cell,h->dispatch_table);put(h->dispatch_table,h->allocate);put(h->dispatch_table+4u,h->release);
    put(h->sentinel,lease.begin);put(h->sentinel+4u,0u);put(h->cursor_cell,h->sentinel);
    put(lease.begin,alias|UINT32_C(0x0c650000));put(lease.begin+4u,UINT32_C(0x1000));
    put(alias|UINT32_C(0x0c650000),h->sentinel);put(alias|UINT32_C(0x0c650004),UINT32_C(0x800));
    put(lease.header+4u,lease.units);put(candidate-28u,UINT32_C(0x20));
}
static bool allowed(uint32_t payload) {
    return kui_vmu_native_lifetime_release_safe(&lease,read_word,NULL,1u,payload);
}
int main(void) {
    const uint32_t aliases[]={0u,UINT32_C(0x80000000),UINT32_C(0xa0000000)};
    for(unsigned a=0;a<3u;a++) {
        fixture(aliases[a]);CHECK(allowed(candidate));CHECK(allowed(0u));
        struct cell before[4096];memcpy(before,memory,sizeof(before));
        CHECK(allowed(candidate));CHECK(!memcmp(before,memory,sizeof(before)));
        for(unsigned b=0;b<3u;b++) {
            CHECK(allowed(physical(candidate)|aliases[b])==(a==b));
            CHECK(!allowed(physical(lease.payload)|aliases[b]));
            CHECK(!allowed(physical(lease.header)|aliases[b]));
        }
        for(unsigned offset=1u;offset<32u;offset++) CHECK(!allowed(candidate+offset));
        CHECK(!allowed(lease.begin));CHECK(!allowed(lease.begin+32u));
        CHECK(!allowed(lease.payload+lease.lease_bytes));
        CHECK(!allowed(UINT32_C(0x4c680020)));CHECK(!allowed(UINT32_MAX));
        for(unsigned offset=0;offset<lease.lease_bytes;offset+=32u)
            CHECK(!allowed(lease.payload+offset));
        /* An invented interior header in a coalesced free block must fail. */
        put(lease.begin+UINT32_C(0x100)+4u,1u);
        CHECK(!allowed(lease.begin+UINT32_C(0x120)));
        /* Exact disjoint endpoints are permitted; crossing the protected
         * allocator header by one 32-byte unit is refused. */
        uint32_t adjacent=lease.header-32u;
        put(adjacent-28u,2u);CHECK(allowed(adjacent));
        put(adjacent-28u,3u);CHECK(!allowed(adjacent));
        put(candidate-28u,0u);CHECK(!allowed(candidate));
        put(candidate-28u,UINT32_MAX);CHECK(!allowed(candidate));
        put(candidate-28u,UINT32_C(0x10000));CHECK(!allowed(candidate));
        put(candidate-28u,1u);CHECK(allowed(candidate));
        /* Preserve the original raw-pointer ordering ABI. */
        put(lease.begin,UINT32_C(0x0c650000)|(aliases[(a+1u)%3u]));
        CHECK(!allowed(candidate));
    }
    fixture(0u);CHECK(!kui_vmu_native_lifetime_release_safe(NULL,read_word,NULL,1u,candidate));
    CHECK(!kui_vmu_native_lifetime_release_safe(&lease,NULL,NULL,1u,candidate));
    CHECK(!kui_vmu_native_lifetime_release_safe(&lease,read_word,NULL,0u,candidate));
    CHECK(!kui_vmu_native_lifetime_release_safe(&lease,read_word,NULL,2u,candidate));
    fixture(0u);put(lease.header+4u,lease.units+1u);CHECK(!allowed(candidate));CHECK(!allowed(0u));
    fixture(0u);put(lease.owner.begin_cell,lease.begin+32u);CHECK(!allowed(candidate));
    fixture(0u);put(lease.owner.dispatch_table+4u,lease.owner.release+2u);CHECK(!allowed(candidate));
    fixture(0u);put(lease.owner.cursor_cell,candidate);CHECK(!allowed(candidate));
    fixture(0u);reads=0u;CHECK(allowed(candidate));unsigned total=reads;
    for(unsigned i=1u;i<=total;i++) {
        fixture(0u);fail_read=i;CHECK(!allowed(candidate));
    }
    fixture(0u);reads=0u;CHECK(allowed(0u));total=reads;
    for(unsigned i=1u;i<=total;i++) {
        fixture(0u);fail_read=i;CHECK(!allowed(0u));
    }
    fixture(0u);
    struct kui_vmu_native_lifetime_range ranges[KUI_VMU_NATIVE_LIFETIME_RANGES+1u];
    for(unsigned i=0;i<KUI_VMU_NATIVE_LIFETIME_RANGES+1u;i++)
        ranges[i]=(struct kui_vmu_native_lifetime_range){UINT32_C(0x8c100000)+i*32u,32u};
#define ALLOWED_RANGES(p,n) kui_vmu_native_lifetime_release_safe_ranges(&lease,read_word,NULL,1u,(p),ranges,(n))
    CHECK(ALLOWED_RANGES(candidate,0u));CHECK(ALLOWED_RANGES(candidate,KUI_VMU_NATIVE_LIFETIME_RANGES));
    CHECK(!ALLOWED_RANGES(candidate,KUI_VMU_NATIVE_LIFETIME_RANGES+1u));
    CHECK(!kui_vmu_native_lifetime_release_safe_ranges(&lease,read_word,NULL,1u,candidate,NULL,1u));
    CHECK(kui_vmu_native_lifetime_release_safe_ranges(&lease,read_word,NULL,1u,candidate,NULL,0u));
    for(unsigned a=0;a<3u;a++) {
        ranges[0]=(struct kui_vmu_native_lifetime_range){(candidate-32u)|aliases[a],1u};
        CHECK(!ALLOWED_RANGES(candidate,1u));CHECK(ALLOWED_RANGES(0u,1u));
        ranges[0]=(struct kui_vmu_native_lifetime_range){(candidate+UINT32_C(0x3df))|aliases[a],1u};
        CHECK(!ALLOWED_RANGES(candidate,1u));
        ranges[0]=(struct kui_vmu_native_lifetime_range){(candidate+UINT32_C(0x3e0))|aliases[a],1u};
        CHECK(ALLOWED_RANGES(candidate,1u));
        ranges[0]=(struct kui_vmu_native_lifetime_range){(candidate-33u)|aliases[a],1u};
        CHECK(ALLOWED_RANGES(candidate,1u));ranges[0].bytes=2u;
        CHECK(!ALLOWED_RANGES(candidate,1u));
    }
    const struct kui_vmu_native_lifetime_range invalid[]={
        {0u,1u},{UINT32_C(0x8c100000),0u},{UINT32_C(0x8c100000),UINT32_MAX},
        {UINT32_C(0xcc100000),32u},{UINT32_C(0x8d000000),1u},
        {UINT32_C(0x8cffffff),2u},{UINT32_MAX,32u}};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        ranges[0]=invalid[i];reads=0u;
        CHECK(!ALLOWED_RANGES(candidate,1u));CHECK(!ALLOWED_RANGES(0u,1u));CHECK(reads==0u);
    }
    ranges[0]=(struct kui_vmu_native_lifetime_range){UINT32_C(0x8c100000),32u};
    CHECK(ALLOWED_RANGES(candidate,1u));
    CHECK(!kui_vmu_native_lifetime_release_safe_ranges(&lease,read_word,NULL,2u,candidate,ranges,1u));
    fixture(0u);put(lease.header+4u,lease.units+1u);CHECK(!ALLOWED_RANGES(0u,1u));
#undef ALLOWED_RANGES
    printf("Native release admission: %u checks passed\n",checks);return 0;
}
