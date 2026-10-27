/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual original three-load root callers, with independent initializer
 * execution, all live aliases, malformed-call and cache-policy mutations. */
#define main native_layout_reference_main
#include "test_vmu_native_layout.c"
#undef main
#include "kui/vmu_native_resolution_cache.h"
#include "kui/hash.h"

static uint16_t half(const uint8_t *p,size_t at) {return (uint16_t)(p[at]|(uint16_t)p[at+1u]<<8);}
static void set_half(uint8_t *p,size_t at,uint16_t value) {p[at]=(uint8_t)value;p[at+1u]=(uint8_t)(value>>8);}
static uint32_t word(const uint8_t *p,size_t at) {return (uint32_t)half(p,at)|(uint32_t)half(p,at+2u)<<16;}
static void set_word(uint8_t *p,size_t at,uint32_t value) {set_half(p,at,(uint16_t)value);set_half(p,at+2u,(uint16_t)(value>>16));}
static void hash(const uint8_t *p,size_t n,uint8_t digest[32]) {
    struct kui_sha256 h;kui_sha256_init(&h);kui_sha256_update(&h,p,n);kui_sha256_digest(&h,digest);
}
static void rejected(const uint8_t *p,size_t n,const uint8_t original_record[1024],
    const uint8_t pin[32],bool complete) {
    struct kui_vmu_native_discovery d,zero_d={0};struct kui_vmu_native_layout l,zero_l={0};
    memset(&d,0xa5,sizeof(d));memset(&l,0xa5,sizeof(l));
    CHECK(!kui_vmu_native_discover_layout(p,n,UINT32_C(0x8c010000),&d,&l));
    CHECK(!memcmp(&d,&zero_d,sizeof(d)) && !memcmp(&l,&zero_l,sizeof(l)));
    uint8_t out[1024],before[1024];memset(out,0xa5,sizeof(out));memcpy(before,out,sizeof(out));
    CHECK(!kui_vmu_native_resolution_cache_build(p,n,UINT32_C(0x8c010000),out,sizeof(out),NULL));
    CHECK(!memcmp(out,before,sizeof(out)));
    if(complete) {
        struct kui_vmu_native_resolution result,saved;memset(&result,0xa5,sizeof(result));saved=result;
        CHECK(!kui_vmu_native_resolution_cache_validate_untrusted(p,n,UINT32_C(0x8c010000),original_record,1024u,&result));
        CHECK(!memcmp(&result,&saved,sizeof(result)));
        CHECK(!kui_vmu_native_resolution_cache_replay(p,n,UINT32_C(0x8c010000),original_record,1024u,pin,&result));
        CHECK(!memcmp(&result,&saved,sizeof(result)));
    }
}
static void actual(const char *path,bool complete) {
    uniform_image_tests(path); /* Retain all existing witness/target-delay tests. */
    FILE *f=fopen(path,"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));long z=ftell(f);CHECK(z>0 && z<=0xc00000);
    rewind(f);size_t n=(size_t)z;uint8_t *p=malloc(n),*original=malloc(n);CHECK(p && original);
    CHECK(fread(p,1,n,f)==n && !fclose(f));memcpy(original,p,n);
    struct kui_vmu_native_discovery d;struct kui_vmu_native_layout l;
    CHECK(kui_vmu_native_discover_layout(p,n,UINT32_C(0x8c010000),&d,&l));
    size_t call=physical(l.initializer_call)-physical(d.load_address);
    unsigned target=(half(p,call)>>8)&15u;
    CHECK(call>=22u && half(p,call+2u)==0xe400u && (half(p,call)&0xf0ffu)==0x400bu);
    CHECK((half(p,call-6u)&0xff00u)==(0xd000u|(target<<8)));
    CHECK((half(p,call-4u)&0xff00u)==0xd500u && (half(p,call-2u)&0xff00u)==0xd600u);
    bool entry=half(p,call-8u)==0x4f22u;
    CHECK(entry || ((half(p,call-10u)&0xf0ffu)==0x400bu && (half(p,call-8u)&0xfff0u)==0x54f0u));
    transport_model(p,n,&d,&l);
    const uint32_t aliases[]={0u,UINT32_C(0x80000000),UINT32_C(0xa0000000)};
    uint32_t manager=sh_long(d.manager_cell),payload=UINT32_C(0x0cfa0000);
    for(unsigned lease_alias=0;lease_alias<3u;lease_alias++)for(unsigned owner_alias=0;owner_alias<3u;owner_alias++) {
        put(d.manager_cell,physical(manager)|aliases[owner_alias]);
        struct kui_vmu_native_runtime_config c={.version=1u,.bytes=sizeof(c),
            .lease_address=payload|aliases[lease_alias],.lease_bytes=0x60000u,
            .lease_header_bytes=32u,.required_lease_bytes=0x60000u};
        CHECK(kui_vmu_native_layout_bind(&l,&d,read_memory,NULL,&c));
        CHECK(c.lease_address==(payload|aliases[lease_alias]) && !memcmp(c.rx_slots,l.rx_slots,sizeof(c.rx_slots)));
        c.lease_address=physical(l.rx_base)|aliases[lease_alias];
        CHECK(!kui_vmu_native_layout_bind(&l,&d,read_memory,NULL,&c));
    }
    put(d.manager_cell,manager);
    uint8_t record[1024],pin[32];struct kui_vmu_native_resolution resolution;
    CHECK(kui_vmu_native_resolution_cache_build(p,n,d.load_address,record,sizeof(record),&resolution)==complete);
    if(complete) {
        CHECK(kui_vmu_native_resolution_cache_record_digest(record,sizeof(record),pin));
        CHECK(kui_vmu_native_resolution_cache_validate_untrusted(p,n,d.load_address,record,sizeof(record),&resolution));
        CHECK(kui_vmu_native_resolution_cache_replay(p,n,d.load_address,record,sizeof(record),pin,&resolution));
        uint8_t stale[1024],stale_pin[32];memcpy(stale,record,sizeof(stale));set_word(stale,16u,2u);hash(stale,992u,stale+992u);
        memcpy(stale_pin,stale+992u,sizeof(stale_pin));
        CHECK(!kui_vmu_native_resolution_cache_record_digest(stale,sizeof(stale),stale_pin));
        CHECK(!kui_vmu_native_resolution_cache_validate_untrusted(p,n,d.load_address,stale,sizeof(stale),&resolution));
        CHECK(!kui_vmu_native_resolution_cache_replay(p,n,d.load_address,stale,sizeof(stale),stale_pin,&resolution));
    } else {memset(record,0,sizeof(record));memset(pin,0,sizeof(pin));}
    /* Every newly admitted instruction is an actual bounded dependency.
     * Stack-derived buffers, wrong targets, wrong mode, late target loads
     * and an unproved preceding branch must still refuse before publication. */
    const size_t at[]={call-6u,call-4u,call-2u,call,call+2u,call+2u,call-8u,call-6u};
    const uint16_t replacement[]={0xdf00u,0x65f2u,0x66f2u,0x4f0bu,0x0009u,0xe401u,0x8bfcu,0x0009u};
    for(unsigned i=0;i<sizeof(at)/sizeof(at[0]);i++) {
        memcpy(p,original,n);set_half(p,at[i],replacement[i]);rejected(p,n,record,pin,complete);
    }
    if(entry)for(unsigned i=0;i<8u;i++) {
        memcpy(p,original,n);set_half(p,call-22u+2u*i,0xef00u);rejected(p,n,record,pin,complete);
    } else {
        memcpy(p,original,n);set_half(p,call-10u,0x0009u);rejected(p,n,record,pin,complete);
    }
    /* The new contract must not turn multiple complete roots into a choice.
     * Insert another exact prologue/three-load call with the same pointers;
     * even equivalent buffer geometry remains ambiguous and is refused. */
    memcpy(p,original,n);const size_t extra=0x100u,second=extra+22u,pool=extra+32u;
    const uint16_t prologue[]={0x2fe6u,0x2fd6u,0x2fc6u,0x2fb6u,0x2fa6u,0x2f96u,0x2f86u,0x4f22u};
    for(unsigned i=0;i<8u;i++)set_half(p,extra+2u*i,prologue[i]);
    size_t target_pool=((call-6u+4u)&~3u)+4u*(half(original,call-6u)&255u);
    uint32_t values[]={word(original,target_pool),l.input_values[0],l.indices[0]};
    const unsigned registers[]={3u,5u,6u};
    for(unsigned i=0;i<3u;i++) {
        size_t pc=second-6u+2u*i,slot=pool+4u*i,displacement=(slot-((pc+4u)&~3u))/4u;
        CHECK(slot>=((pc+4u)&~3u) && displacement<=255u);
        set_half(p,pc,(uint16_t)(0xd000u|(registers[i]<<8)|displacement));set_word(p,slot,values[i]);
    }
    set_half(p,second,0x430bu);set_half(p,second+2u,0xe400u);
    rejected(p,n,record,pin,complete);
    free(original);free(p);
}
int main(int argc,char **argv) {
    CHECK(argc==3);memory=calloc(1u,0x1000000u);CHECK(memory);
    binding_tests();uniform_binding_tests();actual(argv[1],true);actual(argv[2],true);
    free(memory);printf("Three-load original Maple roots: %u checks; Rayman/Lodoss complete with separate allocator proof\n",checks);
    return 0;
}
