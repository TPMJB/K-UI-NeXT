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
static bool identity_valid(void) {
    return kui_vmu_native_heap_lease_identity_valid(&lease,read_word,NULL,1u);
}
/* Independent interval model: allocation removes a suffix from a free
 * interval; release inserts the exact live chunk and merges neighbours.
 * Publish its sorted linked representation only after the model operation,
 * then ask both production validators about the resulting owner state. */
static void allocator_evolution(uint32_t alias) {
    struct interval {uint32_t at,units;};
    struct interval free_blocks[65],allocated[64]={{0}};
    fixture(alias);unsigned count=1u;
    free_blocks[0]=(struct interval){lease.begin,lease.remaining};
    put(lease.begin,lease.owner.sentinel);put(lease.begin+4u,lease.remaining);
    CHECK(identity_valid());CHECK(allowed(0u));
    uint32_t random=UINT32_C(0x913ca753);
    for(unsigned step=0;step<1024u;step++) {
        /* Before an original free runs, the complete release admission must
         * accept its tracked allocation, not merely the identity witness. */
        random=random*UINT32_C(1664525)+UINT32_C(1013904223);
        unsigned slot=(random>>16)&63u;
        if(allocated[slot].units) {
            CHECK(allowed(allocated[slot].at+32u));
            CHECK(count<sizeof(free_blocks)/sizeof(free_blocks[0]));
            free_blocks[count++]=allocated[slot];allocated[slot].units=0u;
            for(unsigned i=1u;i<count;i++) {
                struct interval item=free_blocks[i];unsigned j=i;
                while(j && free_blocks[j-1u].at>item.at) {free_blocks[j]=free_blocks[j-1u];--j;}
                free_blocks[j]=item;
            }
            for(unsigned i=0u;i+1u<count;) {
                CHECK(free_blocks[i].at+free_blocks[i].units*32u<=free_blocks[i+1u].at);
                if(free_blocks[i].at+free_blocks[i].units*32u==free_blocks[i+1u].at) {
                    free_blocks[i].units+=free_blocks[i+1u].units;
                    memmove(&free_blocks[i+1u],&free_blocks[i+2u],(--count-i-1u)*sizeof(free_blocks[0]));
                } else ++i;
            }
        } else {
            uint32_t units=2u+((random>>8)&63u);unsigned chosen=count;
            for(unsigned i=0;i<count;i++) if(free_blocks[i].units>=units) {chosen=i;break;}
            CHECK(chosen<count);
            free_blocks[chosen].units-=units;
            uint32_t at=free_blocks[chosen].at+free_blocks[chosen].units*32u;
            allocated[slot]=(struct interval){at,units};put(at+4u,units);
            if(!free_blocks[chosen].units) {
                memmove(&free_blocks[chosen],&free_blocks[chosen+1u],(--count-chosen)*sizeof(free_blocks[0]));
            }
        }
        put(lease.owner.sentinel,count?free_blocks[0].at:lease.owner.sentinel);
        for(unsigned i=0;i<count;i++) {
            put(free_blocks[i].at,i+1u<count?free_blocks[i+1u].at:lease.owner.sentinel);
            put(free_blocks[i].at+4u,free_blocks[i].units);
        }
        uint32_t cursor=step&1u && count?free_blocks[(random>>24)%count].at:lease.owner.sentinel;
        put(lease.owner.cursor_cell,cursor);
        reads=0u;CHECK(identity_valid());CHECK(reads==8u);CHECK(allowed(0u));
    }
}
static void identity_witness(void) {
    static const uint32_t aliases[]={0u,UINT32_C(0x80000000),UINT32_C(0xa0000000)};
    static const unsigned fragments[]={1u,8u,64u,256u,512u};
    for(unsigned a=0;a<3u;a++) {
        allocator_evolution(aliases[a]);
        for(unsigned n=0;n<sizeof(fragments)/sizeof(fragments[0]);n++) {
            fixture(aliases[a]);unsigned nodes=fragments[n];
            for(unsigned i=0;i<nodes;i++) {
                uint32_t at=lease.begin+i*64u;
                put(at,i+1u==nodes?lease.owner.sentinel:at+64u);put(at+4u,1u);
            }
            reads=0u;CHECK(identity_valid());CHECK(reads==8u);
            reads=0u;CHECK(allowed(0u));CHECK(reads==10u+2u*nodes);
        }
        fixture(aliases[a]);
        const uint32_t addresses[]={lease.owner.begin_cell,lease.owner.begin_copy_cell,
            lease.owner.bytes_cell,lease.owner.dispatch_cell,lease.owner.dispatch_table,
            lease.owner.dispatch_table+4u,lease.owner.sentinel+4u,lease.header+4u};
        for(unsigned cell=0;cell<sizeof(addresses)/sizeof(addresses[0]);cell++) {
            uint32_t original;CHECK(read_word(NULL,addresses[cell],&original));
            for(unsigned bit=0;bit<32u;bit++) {
                put(addresses[cell],original^(UINT32_C(1)<<bit));CHECK(!identity_valid());
                CHECK(!allowed(0u));
            }
            put(addresses[cell],original);CHECK(identity_valid());CHECK(allowed(0u));
        }
        for(unsigned i=1u;i<=8u;i++) {
            reads=0u;fail_read=i;CHECK(!identity_valid());
        }
        fail_read=0u;
        struct kui_vmu_native_heap_lease original=lease;
        static const size_t fields[]={offsetof(struct kui_vmu_native_heap_lease,version),
            offsetof(struct kui_vmu_native_heap_lease,bytes),offsetof(struct kui_vmu_native_heap_lease,phase),
            offsetof(struct kui_vmu_native_heap_lease,initializations),offsetof(struct kui_vmu_native_heap_lease,begin),
            offsetof(struct kui_vmu_native_heap_lease,heap_bytes),offsetof(struct kui_vmu_native_heap_lease,lease_bytes),
            offsetof(struct kui_vmu_native_heap_lease,header),offsetof(struct kui_vmu_native_heap_lease,payload),
            offsetof(struct kui_vmu_native_heap_lease,units),offsetof(struct kui_vmu_native_heap_lease,remaining)};
        for(unsigned field=0;field<sizeof(fields)/sizeof(fields[0]);field++) {
            uint32_t word;memcpy(&word,(const uint8_t *)&original+fields[field],sizeof(word));
            for(unsigned bit=0;bit<32u;bit++) {
                lease=original;uint32_t bad=word^(UINT32_C(1)<<bit);
                memcpy((uint8_t *)&lease+fields[field],&bad,sizeof(bad));CHECK(!identity_valid());
            }
        }
        lease=original;CHECK(identity_valid());
        CHECK(!kui_vmu_native_heap_lease_identity_valid(&lease,read_word,NULL,0u));
        CHECK(!kui_vmu_native_heap_lease_identity_valid(&lease,read_word,NULL,2u));
        CHECK(!kui_vmu_native_heap_lease_identity_valid(NULL,read_word,NULL,1u));
        CHECK(!kui_vmu_native_heap_lease_identity_valid(&lease,NULL,NULL,1u));
        /* The bounded witness deliberately grants no authority over a free
         * list modified outside the reviewed allocator/caller contract.
         * A damaged list is still rejected at release and storage boundaries. */
        put(lease.begin+4u,lease.heap_bytes/32u);CHECK(identity_valid());CHECK(!allowed(0u));
        fixture(aliases[a]);put(lease.begin,lease.begin);CHECK(identity_valid());CHECK(!allowed(0u));
    }
    printf("Native lease identity: 8 reads independent of 1..512 free blocks; full proofs remain 12..1034 reads\n");
}
int main(void) {
    identity_witness();
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
