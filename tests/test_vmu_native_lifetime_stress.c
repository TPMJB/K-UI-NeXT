/* SPDX-License-Identifier: GPL-3.0-only */
/* Differential ownership admission against the previous independent two-walk
 * implementation. Both readers observe an immutable sparse guest arena. */
#define main lifetime_regression_main
#include "test_vmu_native_lifetime.c"
#undef main

static bool overlap(uint32_t a,uint32_t end,uint32_t b,uint32_t other_end) {
    return a<other_end && b<end;
}
static bool reference_release(uint32_t payload,
    const struct kui_vmu_native_lifetime_range *ranges,size_t count) {
    if(count>KUI_VMU_NATIVE_LIFETIME_RANGES || (count && !ranges) ||
       (count && (uintptr_t)ranges>UINTPTR_MAX-count*sizeof(*ranges))) return false;
    for(size_t i=0;i<count;i++) {
        uint32_t area=ranges[i].begin&UINT32_C(0xe0000000),at=physical(ranges[i].begin);
        if((area!=0u && area!=UINT32_C(0x80000000) && area!=UINT32_C(0xa0000000)) ||
           !ranges[i].bytes || at<UINT32_C(0x0c000000) || at>=UINT32_C(0x0d000000) ||
           ranges[i].bytes>UINT32_C(0x0d000000)-at) return false;
    }
    if(!kui_vmu_native_heap_lease_valid(&lease,read_word,NULL,1u)) return false;
    if(!payload) return true;
    uint32_t alias=lease.begin&UINT32_C(0xe0000000);
    uint32_t first=physical(lease.begin),end=first+lease.heap_bytes;
    uint32_t actual=physical(payload),units;
    if((payload&UINT32_C(0xe0000000))!=alias || (actual&31u) ||
       actual<first+32u || actual>end) return false;
    uint32_t header=actual-32u;
    uint32_t guard=physical(lease.header),guard_end=physical(lease.payload)+lease.lease_bytes;
    if((actual>=guard && actual<guard_end) ||
       !read_word(NULL,(header|alias)+4u,&units) || !units || units>(end-header)/32u)
        return false;
    uint32_t candidate_end=header+units*32u;
    if(overlap(header,candidate_end,guard,guard_end)) return false;
    for(size_t i=0;i<count;i++) {
        uint32_t at=physical(ranges[i].begin);
        if(overlap(header,candidate_end,at,at+ranges[i].bytes)) return false;
    }
    uint32_t node,previous_end=0u;
    if(!read_word(NULL,lease.owner.sentinel,&node)) return false;
    for(unsigned i=0;i<KUI_VMU_NATIVE_HEAP_FREE_LIMIT;i++) {
        if(node==lease.owner.sentinel) return true;
        uint32_t at=physical(node),next;
        if((node&UINT32_C(0xe0000000))!=alias || (at&31u) || at<first || at>=end ||
           (previous_end && at<previous_end) || !read_word(NULL,node+4u,&units) ||
           !units || units>(end-at)/32u || !read_word(NULL,node,&next)) return false;
        uint32_t free_end=at+units*32u;
        if(overlap(header,candidate_end,at,free_end)) return false;
        previous_end=free_end;node=next;
    }
    return false;
}
static void fragmented(unsigned nodes,uint32_t alias) {
    fixture(alias);
    if(!nodes) put(lease.owner.sentinel,lease.owner.sentinel);
    for(unsigned i=0;i<nodes;i++) {
        uint32_t at=lease.begin+i*64u;
        put(at,i+1u<nodes?at+64u:lease.owner.sentinel);put(at+4u,1u);
    }
}
static void compare(uint32_t payload,const struct kui_vmu_native_lifetime_range *ranges,
    size_t count,bool expected) {
    reads=fail_read=0u;bool before=reference_release(payload,ranges,count);
    unsigned reference_reads=reads;reads=0u;
    bool after=kui_vmu_native_lifetime_release_safe_ranges(&lease,read_word,NULL,1u,payload,ranges,count);
    CHECK(before==expected && after==before);
    if(after) {
        CHECK(reads<=reference_reads);
        if(payload) CHECK(reads<reference_reads);
    }
}
static void malformed(unsigned which,uint32_t alias) {
    fragmented(8u,alias);
    uint32_t begin=lease.begin;
    switch(which) {
    case 0u:put(begin+4u,0u);break;
    case 1u:put(begin+4u,UINT32_MAX);break;
    case 2u:put(begin,begin);break;
    case 3u:put(begin+64u,begin);break;
    case 4u:put(begin+4u,3u);break;
    case 5u:put(begin,begin+1u);break;
    case 6u:put(begin,lease.payload);break;
    case 7u:put(begin,alias|UINT32_C(0x0c5fffe0));break;
    case 8u:put(lease.owner.cursor_cell,begin+32u);break;
    case 9u:put(lease.owner.sentinel+4u,1u);break;
    case 10u:put(lease.owner.begin_cell,begin+32u);break;
    case 11u:put(lease.owner.begin_copy_cell,begin+32u);break;
    case 12u:put(lease.owner.bytes_cell,lease.heap_bytes+32u);break;
    case 13u:put(lease.owner.dispatch_cell,lease.owner.dispatch_table+4u);break;
    case 14u:put(lease.owner.dispatch_table,lease.owner.allocate+2u);break;
    case 15u:put(lease.owner.dispatch_table+4u,lease.owner.release+2u);break;
    case 16u:put(lease.header+4u,lease.units+1u);break;
    case 17u:put(begin+7u*64u,lease.owner.sentinel+UINT32_C(0x80000000));break;
    default:CHECK(false);
    }
}
int main(void) {
    static const uint32_t aliases[]={0u,UINT32_C(0x80000000),UINT32_C(0xa0000000)};
    static const unsigned nodes[]={0u,1u,8u,64u,256u,1023u};
    for(unsigned a=0;a<3u;a++) {
        for(unsigned n=0;n<sizeof(nodes)/sizeof(nodes[0]);n++) {
            fragmented(nodes[n],aliases[a]);
            compare(candidate,NULL,0u,true);compare(0u,NULL,0u,true);
            const unsigned positions[]={0u,nodes[n]/2u,nodes[n]?nodes[n]-1u:0u};
            for(unsigned i=0;nodes[n] && i<sizeof(positions)/sizeof(positions[0]);i++) {
                put(lease.owner.cursor_cell,lease.begin+positions[i]*64u);
                compare(candidate,NULL,0u,true);
            }
            reads=0u;CHECK(reference_release(candidate,NULL,0u));unsigned old=reads;
            reads=0u;CHECK(allowed(candidate));unsigned fresh=reads;
            CHECK(old==12u+4u*nodes[n] && fresh==11u+2u*nodes[n]);
            printf("release free_nodes=%u alias=%08x reads_before=%u reads_after=%u\n",nodes[n],aliases[a],old,fresh);
        }
        for(unsigned i=0;i<18u;i++) {
            malformed(i,aliases[a]);compare(candidate,NULL,0u,false);compare(0u,NULL,0u,false);
        }
        fragmented(8u,aliases[a]);
        /* A physically valid pointer in another alias is allowed by the lease
         * proof alone, but rejected for a nonnull raw-pointer-ordered release. */
        put(lease.begin,physical(lease.begin+64u)|aliases[(a+1u)%3u]);
        compare(candidate,NULL,0u,false);compare(0u,NULL,0u,true);
        fragmented(8u,aliases[a]);
        for(unsigned i=0;i<8u;i++) {
            put(lease.begin+i*64u+4u,2u);
            compare(lease.begin+i*64u+32u,NULL,0u,false);
            put(lease.begin+i*64u+4u,1u);
        }
        struct kui_vmu_native_lifetime_range ranges[KUI_VMU_NATIVE_LIFETIME_RANGES];
        for(unsigned r=0;r<KUI_VMU_NATIVE_LIFETIME_RANGES;r++)
            ranges[r]=(struct kui_vmu_native_lifetime_range){UINT32_C(0xac100000)+r*32u,32u};
        compare(candidate,ranges,KUI_VMU_NATIVE_LIFETIME_RANGES,true);
        for(unsigned r=0;r<KUI_VMU_NATIVE_LIFETIME_RANGES;r++) {
            struct kui_vmu_native_lifetime_range saved=ranges[r];
            ranges[r]=(struct kui_vmu_native_lifetime_range){candidate-32u,1u};
            compare(candidate,ranges,KUI_VMU_NATIVE_LIFETIME_RANGES,false);
            compare(0u,ranges,KUI_VMU_NATIVE_LIFETIME_RANGES,true);
            ranges[r]=saved;
        }
    }
    fragmented(1024u,0u);compare(candidate,NULL,0u,false);compare(0u,NULL,0u,false);
    fixture(0u);uint32_t bytes=UINT32_MAX;
    CHECK(kui_vmu_native_heap_lease_valid_with_release(&lease,read_word,NULL,1u,candidate,&bytes));
    CHECK(bytes==UINT32_C(0x400));
    CHECK(kui_vmu_native_heap_lease_valid_with_release(&lease,read_word,NULL,1u,0u,&bytes));
    CHECK(bytes==0u);bytes=UINT32_MAX;put(lease.header+4u,0u);
    CHECK(!kui_vmu_native_heap_lease_valid_with_release(&lease,read_word,NULL,1u,candidate,&bytes));
    CHECK(bytes==UINT32_MAX);
    printf("Native one-walk release differential: %u checks passed\n",checks);
    return 0;
}
