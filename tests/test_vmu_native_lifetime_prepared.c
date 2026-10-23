/* SPDX-License-Identifier: GPL-3.0-only */
/* Differential the complete prepared admission against the unchanged generic
 * production proof. Its sparse reader asserts every requested bus address. */
#define main lifetime_original_main
#include "test_vmu_native_lifetime.c"
#undef main

static struct kui_vmu_native_lifetime_plan plan;
static uint32_t trace[4096];
static unsigned trace_count;
static struct cell bus[8192];
static unsigned bucket(uint32_t at) {return ((at>>2)*UINT32_C(2654435761))&8191u;}
static void snapshot_bus(void) {
    memset(bus,0,sizeof(bus));
    for(unsigned i=0;i<used;i++) {
        unsigned at=bucket(memory[i].address);
        while(bus[at].address) at=(at+1u)&8191u;
        bus[at]=memory[i];
    }
}
static bool traced(void *context,uint32_t address,uint32_t *value) {
    CHECK(!(address&3u));
    CHECK((address&UINT32_C(0xe0000000))==0u ||
          (address&UINT32_C(0xe0000000))==UINT32_C(0x80000000) ||
          (address&UINT32_C(0xe0000000))==UINT32_C(0xa0000000));
    CHECK(physical(address)>=UINT32_C(0x0c000000) && physical(address)<=UINT32_C(0x0cfffffc));
    CHECK(trace_count<sizeof(trace)/sizeof(trace[0]));trace[trace_count++]=address;
    (void)context;++reads;if(fail_read && reads==fail_read) return false;
    address=physical(address);unsigned at=bucket(address);
    while(bus[at].address) {
        if(bus[at].address==address) {*value=bus[at].value;return true;}
        at=(at+1u)&8191u;
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
static void prepare(const struct kui_vmu_native_lifetime_range *ranges,size_t count) {
    CHECK(kui_vmu_native_lifetime_prepare(&plan,&lease,read_word,NULL,1u,ranges,count));
    CHECK(plan.self==(uintptr_t)&plan && plan.ready==KUI_VMU_NATIVE_LIFETIME_READY);
    snapshot_bus();
}
static bool prepared(uint32_t payload) {
    return kui_vmu_native_lifetime_release_prepared(&plan,traced,NULL,1u,payload);
}
static void compare(uint32_t payload,const struct kui_vmu_native_lifetime_range *ranges,
    size_t count,bool failures) {
    snapshot_bus();reads=fail_read=trace_count=0u;
    bool original=kui_vmu_native_lifetime_release_safe_ranges(&lease,traced,NULL,1u,payload,ranges,count);
    unsigned original_reads=reads;uint32_t original_trace[4096];
    memcpy(original_trace,trace,sizeof(original_trace));
    reads=trace_count=0u;bool fresh=prepared(payload);
    CHECK(fresh==original);
    if(original) {
        CHECK(reads==original_reads && trace_count==original_reads);
        CHECK(!memcmp(original_trace,trace,trace_count*sizeof(trace[0])));
        if(failures) for(unsigned fault=1u;fault<=original_reads;fault++) {
            reads=trace_count=0u;fail_read=fault;
            CHECK(!kui_vmu_native_lifetime_release_safe_ranges(&lease,traced,NULL,1u,payload,ranges,count));
            reads=trace_count=0u;CHECK(!prepared(payload));CHECK(reads==fault);
        }
    }
    fail_read=0u;
}
static void valid_lists(void) {
    static const uint32_t aliases[]={0u,UINT32_C(0x80000000),UINT32_C(0xa0000000)};
    static const unsigned nodes[]={0u,1u,8u,64u,256u,1023u};
    for(unsigned a=0;a<3u;a++) for(unsigned n=0;n<sizeof(nodes)/sizeof(nodes[0]);n++) {
        fragmented(nodes[n],aliases[a]);prepare(NULL,0u);
        compare(candidate,NULL,0u,true);compare(0u,NULL,0u,true);
        reads=trace_count=0u;CHECK(prepared(candidate));CHECK(reads==11u+2u*nodes[n]);
        if(nodes[n]) {
            put(lease.owner.cursor_cell,lease.begin+(nodes[n]/2u)*64u);
            compare(candidate,NULL,0u,true);
        }
    }
    /* The original NULL proof accepts physical aliases of in-arena free
     * nodes, while release ordering still requires the exact arena alias. */
    for(unsigned a=0;a<3u;a++) {
        fragmented(8u,aliases[a]);prepare(NULL,0u);
        put(lease.begin,physical(lease.begin+64u)|aliases[(a+1u)%3u]);
        compare(candidate,NULL,0u,false);compare(0u,NULL,0u,true);
    }
}
static void malformed_lists(void) {
    static const uint32_t aliases[]={0u,UINT32_C(0x80000000),UINT32_C(0xa0000000)};
    for(unsigned a=0;a<3u;a++) for(unsigned corruption=0u;corruption<24u;corruption++) {
        fragmented(8u,aliases[a]);prepare(NULL,0u);uint32_t at=lease.begin;
        switch(corruption) {
        case 0u:put(at+4u,0u);break;
        case 1u:put(at+4u,UINT32_MAX);break;
        case 2u:put(at,at);break;
        case 3u:put(at+64u,at);break;
        case 4u:put(at+4u,3u);break;
        case 5u:put(at,at+1u);break;
        case 6u:put(at,lease.payload);break;
        case 7u:put(at,aliases[a]|UINT32_C(0x0c5fffe0));break;
        case 8u:put(lease.owner.cursor_cell,at+32u);break;
        case 9u:put(lease.owner.sentinel+4u,1u);break;
        case 10u:put(lease.owner.begin_cell,at+32u);break;
        case 11u:put(lease.owner.begin_copy_cell,at+32u);break;
        case 12u:put(lease.owner.bytes_cell,lease.heap_bytes+32u);break;
        case 13u:put(lease.owner.dispatch_cell,lease.owner.dispatch_table+4u);break;
        case 14u:put(lease.owner.dispatch_table,lease.owner.allocate+2u);break;
        case 15u:put(lease.owner.dispatch_table+4u,lease.owner.release+2u);break;
        case 16u:put(lease.header+4u,lease.units+1u);break;
        case 17u:put(at+7u*64u,lease.owner.sentinel+UINT32_C(0x80000000));break;
        case 18u:put(at,(physical(at)+64u)|UINT32_C(0xe0000000));break;
        case 19u:put(at,(physical(at)+64u)|UINT32_C(0x20000000));break;
        case 20u:put(at,aliases[a]|UINT32_C(0x0d000000));break;
        case 21u:put(at,at+2u);break;
        case 22u:put(at,at+4u);break;
        case 23u:put(at,at+16u);break;
        default:CHECK(false);
        }
        compare(candidate,NULL,0u,false);compare(0u,NULL,0u,false);
    }
    fragmented(1024u,0u);
    CHECK(!kui_vmu_native_lifetime_prepare(&plan,&lease,read_word,NULL,1u,NULL,0u));
    for(unsigned a=0;a<3u;a++) {
        fixture(aliases[a]);prepare(NULL,0u);
        for(unsigned bit=0;bit<32u;bit++) {
            put(candidate-28u,UINT32_C(0x20)^(UINT32_C(1)<<bit));
            compare(candidate,NULL,0u,false);
        }
        put(candidate-28u,UINT32_C(0x20));
        const uint32_t payloads[]={0u,lease.begin,lease.begin+32u,lease.begin-32u,
            lease.header,lease.payload,lease.payload+32u,lease.begin+lease.heap_bytes,
            lease.begin+lease.heap_bytes+32u,candidate+1u,candidate+4u,candidate-32u};
        for(unsigned i=0;i<sizeof(payloads)/sizeof(payloads[0]);i++) compare(payloads[i],NULL,0u,true);
    }
}
static void intervals(void) {
    fixture(0u);
    struct kui_vmu_native_lifetime_range ranges[64];
    /* Sorting, exact clipping and adjacent/overlapping coalescing retain the
     * original interval union; spans outside the arena cannot affect a free. */
    for(unsigned i=0;i<64u;i++) ranges[i]=(struct kui_vmu_native_lifetime_range){
        UINT32_C(0x8c100000)+i*1024u,1024u};
    prepare(ranges,64u);CHECK(plan.count==0u);compare(candidate,ranges,64u,true);
    ranges[0]=(struct kui_vmu_native_lifetime_range){lease.begin-32u,64u};
    ranges[1]=(struct kui_vmu_native_lifetime_range){lease.begin+32u,32u};
    ranges[2]=(struct kui_vmu_native_lifetime_range){lease.begin+16u,32u};
    ranges[3]=(struct kui_vmu_native_lifetime_range){lease.begin+lease.heap_bytes-32u,64u};
    prepare(ranges,64u);CHECK(plan.count==2u);
    CHECK(plan.ranges[0].begin==physical(lease.begin) && plan.ranges[0].end==physical(lease.begin)+64u);
    CHECK(plan.ranges[1].end==physical(lease.begin)+lease.heap_bytes);
    compare(candidate,ranges,64u,true);compare(0u,ranges,64u,true);
    for(unsigned i=0;i<64u;i++) {
        ranges[i]=(struct kui_vmu_native_lifetime_range){UINT32_C(0xac680000)+i*32u,32u};
        prepare(ranges,64u);compare(candidate,ranges,64u,false);
    }
    fixture(0u);
    uint32_t random=UINT32_C(0x913ca753);
    for(unsigned iteration=0;iteration<512u;iteration++) {
        unsigned count=iteration%65u;
        for(unsigned i=0;i<count;i++) {
            random=random*UINT32_C(1664525)+UINT32_C(1013904223);
            uint32_t begin=UINT32_C(0x0c000000)+(random&UINT32_C(0x00ffffc0));
            uint32_t bytes=1u+((random>>20)&255u);
            if(bytes>UINT32_C(0x0d000000)-begin) bytes=UINT32_C(0x0d000000)-begin;
            ranges[i]=(struct kui_vmu_native_lifetime_range){begin|((iteration%3u)==1u?UINT32_C(0x80000000):
                (iteration%3u)==2u?UINT32_C(0xa0000000):0u),bytes};
        }
        prepare(ranges,count);compare(candidate,ranges,count,false);compare(0u,ranges,count,false);
    }
}
static void preparation_errors(void) {
    fixture(0u);memset(&plan,0x5a,sizeof(plan));
    struct kui_vmu_native_lifetime_plan saved=plan;
    struct kui_vmu_native_lifetime_range range={UINT32_C(0x0c100000),32u};
    CHECK(!kui_vmu_native_lifetime_prepare(NULL,&lease,read_word,NULL,1u,&range,1u));
    CHECK(!kui_vmu_native_lifetime_prepare(&plan,NULL,read_word,NULL,1u,&range,1u));
    CHECK(!kui_vmu_native_lifetime_prepare(&plan,&lease,NULL,NULL,1u,&range,1u));
    CHECK(!kui_vmu_native_lifetime_prepare(&plan,&lease,read_word,NULL,0u,&range,1u));
    CHECK(!kui_vmu_native_lifetime_prepare(&plan,&lease,read_word,NULL,2u,&range,1u));
    CHECK(!kui_vmu_native_lifetime_prepare(&plan,&lease,read_word,NULL,1u,NULL,1u));
    CHECK(!kui_vmu_native_lifetime_prepare(&plan,&lease,read_word,NULL,1u,&range,65u));
    CHECK(!kui_vmu_native_lifetime_prepare(&plan,&lease,read_word,NULL,1u,
        (const void *)(uintptr_t)(UINTPTR_MAX-7u),64u));
    CHECK(!kui_vmu_native_lifetime_prepare((void *)&lease,&lease,read_word,NULL,1u,&range,1u));
    CHECK(!kui_vmu_native_lifetime_prepare(&plan,&lease,read_word,NULL,1u,(const void *)&plan,1u));
    CHECK(!memcmp(&plan,&saved,sizeof(plan)));
    const struct kui_vmu_native_lifetime_range invalid[]={{0u,1u},{UINT32_C(0x0c100000),0u},
        {UINT32_C(0x2c100000),1u},{UINT32_C(0x0d000000),1u},{UINT32_C(0x0cffffff),2u}};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        CHECK(!kui_vmu_native_lifetime_prepare(&plan,&lease,read_word,NULL,1u,invalid+i,1u));
        CHECK(!memcmp(&plan,&saved,sizeof(plan)));
    }
    prepare(&range,1u);struct kui_vmu_native_lifetime_plan copied=plan;
    CHECK(!kui_vmu_native_lifetime_release_prepared(&copied,read_word,NULL,1u,candidate));
    CHECK(!kui_vmu_native_lifetime_release_prepared(NULL,read_word,NULL,1u,candidate));
    CHECK(!kui_vmu_native_lifetime_release_prepared(&plan,NULL,NULL,1u,candidate));
    CHECK(!kui_vmu_native_lifetime_release_prepared(&plan,read_word,NULL,0u,candidate));
    CHECK(!kui_vmu_native_lifetime_release_prepared(&plan,read_word,NULL,2u,candidate));
    plan.ready^=1u;CHECK(!prepared(candidate));plan.ready^=1u;
    plan.count=65u;CHECK(!prepared(candidate));
}
static void identity_cases(void) {
    static const uint32_t aliases[]={0u,UINT32_C(0x80000000),UINT32_C(0xa0000000)};
    for(unsigned a=0;a<3u;a++) {
        fixture(aliases[a]);prepare(NULL,0u);
        reads=trace_count=0u;
        CHECK(kui_vmu_native_lifetime_identity_prepared_inline(&plan,traced,NULL,1u));CHECK(reads==8u);
        for(unsigned i=0;i<8u;i++) {
            uint32_t original=plan.identity[i].value;
            for(unsigned bit=0;bit<32u;bit++) {
                put(plan.identity[i].address,original^(UINT32_C(1)<<bit));snapshot_bus();
                reads=trace_count=fail_read=0u;
                CHECK(!kui_vmu_native_lifetime_identity_prepared_inline(&plan,traced,NULL,1u));
                CHECK(!kui_vmu_native_heap_lease_identity_valid(&lease,read_word,NULL,1u));
            }
            put(plan.identity[i].address,original);snapshot_bus();
        }
        for(unsigned fault=1u;fault<=8u;fault++) {
            reads=trace_count=0u;fail_read=fault;
            CHECK(!kui_vmu_native_lifetime_identity_prepared_inline(&plan,traced,NULL,1u));CHECK(reads==fault);
        }
        fail_read=0u;
        CHECK(!kui_vmu_native_lifetime_identity_prepared_inline(&plan,traced,NULL,0u));
        CHECK(!kui_vmu_native_lifetime_identity_prepared_inline(&plan,traced,NULL,2u));
        CHECK(!kui_vmu_native_lifetime_identity_prepared_inline(&plan,NULL,NULL,1u));
        /* Identity retains its deliberately narrower scope. */
        put(lease.begin,lease.begin);snapshot_bus();reads=trace_count=0u;
        CHECK(kui_vmu_native_lifetime_identity_prepared_inline(&plan,traced,NULL,1u));
        reads=trace_count=0u;CHECK(!prepared(candidate));
    }
}
static void evolution(uint32_t alias) {
    struct interval {uint32_t at,units;} free_blocks[65],allocated[64]={{0}};
    fragmented(1u,alias);free_blocks[0]=(struct interval){lease.begin,lease.remaining};
    put(lease.begin+4u,lease.remaining);prepare(NULL,0u);unsigned count=1u;
    uint32_t random=UINT32_C(0x913ca753);
    for(unsigned step=0;step<1024u;step++) {
        random=random*UINT32_C(1664525)+UINT32_C(1013904223);unsigned slot=(random>>16)&63u;
        if(allocated[slot].units) {
            reads=trace_count=0u;CHECK(prepared(allocated[slot].at+32u));
            CHECK(count<65u);free_blocks[count++]=allocated[slot];allocated[slot].units=0u;
            for(unsigned i=1u;i<count;i++) {
                struct interval item=free_blocks[i];unsigned j=i;
                while(j && free_blocks[j-1u].at>item.at) {free_blocks[j]=free_blocks[j-1u];--j;}
                free_blocks[j]=item;
            }
            for(unsigned i=0u;i+1u<count;) {
                CHECK(free_blocks[i].at+free_blocks[i].units*32u<=free_blocks[i+1u].at);
                if(free_blocks[i].at+free_blocks[i].units*32u==free_blocks[i+1u].at) {
                    free_blocks[i].units+=free_blocks[i+1u].units;
                    memmove(free_blocks+i+1u,free_blocks+i+2u,(--count-i-1u)*sizeof(*free_blocks));
                } else ++i;
            }
        } else {
            uint32_t units=2u+((random>>8)&63u);unsigned chosen=count;
            for(unsigned i=0;i<count;i++) if(free_blocks[i].units>=units) {chosen=i;break;}
            CHECK(chosen<count);free_blocks[chosen].units-=units;
            uint32_t at=free_blocks[chosen].at+free_blocks[chosen].units*32u;
            allocated[slot]=(struct interval){at,units};put(at+4u,units);
            if(!free_blocks[chosen].units)
                memmove(free_blocks+chosen,free_blocks+chosen+1u,(--count-chosen)*sizeof(*free_blocks));
        }
        put(lease.owner.sentinel,count?free_blocks[0].at:lease.owner.sentinel);
        for(unsigned i=0;i<count;i++) {
            put(free_blocks[i].at,i+1u<count?free_blocks[i+1u].at:lease.owner.sentinel);
            put(free_blocks[i].at+4u,free_blocks[i].units);
        }
        put(lease.owner.cursor_cell,(step&1u) && count?free_blocks[(random>>24)%count].at:lease.owner.sentinel);
        compare(0u,NULL,0u,false);
        for(unsigned i=0;i<64u;i++) if(allocated[i].units) compare(allocated[i].at+32u,NULL,0u,false);
    }
}
int main(void) {
    valid_lists();malformed_lists();intervals();preparation_errors();identity_cases();
    evolution(0u);evolution(UINT32_C(0x80000000));evolution(UINT32_C(0xa0000000));
    printf("Native prepared lifetime: %u checks; exact full-proof differential, all read faults, private plans, clipped unions and allocator evolution\n",checks);
    return 0;
}
