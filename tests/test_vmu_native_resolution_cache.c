/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_native_resolution_cache.h"
#include "kui/vmu_native_program.h"
#include "kui/hash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)) {fprintf(stderr,"Resolution cache failed at line %u\n",__LINE__);exit(1);}} while(0)
static const uint32_t load=UINT32_C(0x8c010000);
static uint32_t get32(const uint8_t *p) {return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void digest(const void *p,size_t n,uint8_t out[32]) {
    struct kui_sha256 h;kui_sha256_init(&h);kui_sha256_update(&h,p,n);kui_sha256_digest(&h,out);
}
static void seal(uint8_t wire[1024]) {digest(wire,992u,wire+992u);}
static bool replay(const void *image,size_t n,uint32_t address,const void *wire,size_t bytes,
    const uint8_t expected[32],struct kui_vmu_native_resolution *out) {
    memset(out,0xa5,sizeof(*out));struct kui_vmu_native_resolution before=*out;
    bool result=kui_vmu_native_resolution_cache_replay(image,n,address,wire,bytes,expected,out);
    if(!result) CHECK(!memcmp(out,&before,sizeof(before)));
    return result;
}
static bool strict(const void *image,size_t n,uint32_t address,const void *wire,size_t bytes,
    struct kui_vmu_native_resolution *out) {
    memset(out,0xa5,sizeof(*out));struct kui_vmu_native_resolution before=*out;
    bool result=kui_vmu_native_resolution_cache_validate_untrusted(image,n,address,wire,bytes,out);
    if(!result) CHECK(!memcmp(out,&before,sizeof(before)));
    return result;
}
static void mock(const uint8_t *image,size_t n,struct kui_vmu_native_resolution *r) {
    /* Test-only stand-in for the independently trusted complete proof. Its
     * nonsensical addresses deliberately cannot pass strict discovery: this
     * exercises the fast API's explicit trust boundary, not a detector. */
    for(size_t i=0;i<sizeof(*r);i++) ((uint8_t *)r)[i]=(uint8_t)(i*29u+7u);
    r->driver.version=KUI_VMU_NATIVE_DISCOVERY_VERSION;r->driver.bytes=sizeof(r->driver);
    r->driver.family=KUI_VMU_NATIVE_FAMILY_UNIFORM_V1;r->driver.image_bytes=(uint32_t)n;
    r->driver.load_address=load;r->driver.requirements=KUI_VMU_NATIVE_REQUIREMENTS;r->driver.gate_register=12u;
    digest(image,n,r->driver.image_sha256);
    r->layout.version=KUI_VMU_NATIVE_LAYOUT_VERSION;r->layout.bytes=sizeof(r->layout);
    r->layout.rx_layout=KUI_VMU_NATIVE_RX_EXPLICIT48;r->layout.inputs=12u;
    r->heap.version=KUI_VMU_NATIVE_HEAP_VERSION;r->heap.bytes=sizeof(r->heap);
    r->heap.family=KUI_VMU_NATIVE_HEAP_TAIL32;r->heap.layout=KUI_VMU_NATIVE_HEAP_DISPATCH2;
    r->heap.load=load;r->heap.image_bytes=(uint32_t)n;
    r->exits.version=KUI_VMU_NATIVE_EXIT_VERSION;r->exits.bytes=sizeof(r->exits);
    r->exits.family=KUI_VMU_NATIVE_EXIT_FAMILY;r->exits.load=load;
    r->exits.image_bytes=(uint32_t)n;r->exits.count=KUI_VMU_NATIVE_EXIT_ENTRIES;
}
static void codec_and_trust_tests(void) {
    uint8_t image[1024],wire[1024],original[1024],pin[32],changed[32];
    for(unsigned i=0;i<sizeof(image);i++) image[i]=(uint8_t)(i*17u+1u);
    struct kui_vmu_native_resolution source,out;mock(image,sizeof(image),&source);
    CHECK(sizeof(source.driver)==104u && sizeof(source.layout)==440u && sizeof(source.heap)==72u &&
        sizeof(source.call)==20u && sizeof(source.exits)==48u && sizeof(source)==684u);
    CHECK(kui_vmu_native_resolution_cache_encode(&source,wire,sizeof(wire)));
    memcpy(original,wire,sizeof(wire));CHECK(!memcmp(wire,"KUINVR01",8u));
    CHECK(get32(wire+8u)==1u && get32(wire+12u)==1024u && get32(wire+16u)==KUI_VMU_NATIVE_RESOLUTION_DETECTOR_EPOCH && get32(wire+20u)==1u);
    CHECK(get32(wire+24u)==sizeof(image) && get32(wire+28u)==load && !memcmp(wire+32u,source.driver.image_sha256,32u));
    CHECK(get32(wire+64u)==source.driver.version && get32(wire+136u)==get32(source.driver.image_sha256));
    CHECK(get32(wire+168u)==source.layout.version && get32(wire+604u)==source.layout.input_values[23]);
    CHECK(get32(wire+608u)==source.heap.version && get32(wire+676u)==source.heap.begin_copy_cell);
    CHECK(get32(wire+680u)==source.call.load && get32(wire+696u)==source.call.entry_bytes);
    CHECK(get32(wire+700u)==source.exits.version && get32(wire+744u)==source.exits.cleanup);
    for(unsigned i=748u;i<992u;i++) CHECK(!wire[i]);
    CHECK(kui_vmu_native_resolution_cache_record_digest(wire,sizeof(wire),pin));CHECK(!memcmp(pin,wire+992u,32u));
    CHECK(replay(image,sizeof(image),load,wire,sizeof(wire),pin,&out));CHECK(!memcmp(&source,&out,sizeof(out)));
    CHECK(!memcmp(wire,original,sizeof(wire)));CHECK(!strict(image,sizeof(image),load,wire,sizeof(wire),&out));
    /* A locally resealed old detector record still lacks the new family /
     * uniqueness proof. Neither its old pin nor its recomputed local digest
     * can turn it into an epoch3 launch authority. No output is published. */
    for(unsigned epoch=1u;epoch<KUI_VMU_NATIVE_RESOLUTION_DETECTOR_EPOCH;epoch++) {
        memcpy(wire,original,sizeof(wire));wire[16]=(uint8_t)epoch;
        wire[17]=wire[18]=wire[19]=0u;seal(wire);memcpy(changed,wire+992u,32u);
        uint8_t untouched[32];memset(untouched,0xa5,sizeof(untouched));
        CHECK(!kui_vmu_native_resolution_cache_record_digest(wire,sizeof(wire),untouched));
        for(unsigned i=0;i<sizeof(untouched);i++)CHECK(untouched[i]==0xa5u);
        CHECK(!replay(image,sizeof(image),load,wire,sizeof(wire),pin,&out));
        CHECK(!replay(image,sizeof(image),load,wire,sizeof(wire),changed,&out));
        CHECK(!strict(image,sizeof(image),load,wire,sizeof(wire),&out));
    }
    memcpy(wire,original,sizeof(wire));
    /* Every single-byte corruption must miss before exposing any output. */
    for(unsigned i=0;i<sizeof(wire);i++) {
        wire[i]^=1u;memset(changed,0xa5,sizeof(changed));
        CHECK(!kui_vmu_native_resolution_cache_record_digest(wire,sizeof(wire),changed));
        for(unsigned j=0;j<32u;j++) CHECK(changed[j]==0xa5u);
        CHECK(!replay(image,sizeof(image),load,wire,sizeof(wire),pin,&out));wire[i]^=1u;
    }
    for(unsigned i=0;i<sizeof(image);i++) {
        image[i]^=1u;CHECK(!replay(image,sizeof(image),load,wire,sizeof(wire),pin,&out));image[i]^=1u;
    }
    for(unsigned i=0;i<32u;i++) {
        memcpy(changed,pin,sizeof(changed));changed[i]^=1u;
        CHECK(!replay(image,sizeof(image),load,wire,sizeof(wire),changed,&out));
    }
    /* Recomputed local integrity is not the independent trusted pin. */
    wire[88u]^=4u;seal(wire);CHECK(kui_vmu_native_resolution_cache_record_digest(wire,sizeof(wire),changed));
    CHECK(!replay(image,sizeof(image),load,wire,sizeof(wire),pin,&out));
    CHECK(!strict(image,sizeof(image),load,wire,sizeof(wire),&out));memcpy(wire,original,sizeof(wire));
    const unsigned schema_offsets[]={0u,8u,12u,16u,20u,24u,28u,32u,64u,68u,72u,76u,80u,84u,132u,
        168u,172u,192u,404u,608u,612u,616u,620u,624u,628u,700u,704u,708u,712u,716u,720u,748u,991u};
    for(unsigned i=0;i<sizeof(schema_offsets)/sizeof(schema_offsets[0]);i++) {
        memcpy(wire,original,sizeof(wire));wire[schema_offsets[i]]^=0x80u;seal(wire);
        CHECK(!kui_vmu_native_resolution_cache_record_digest(wire,sizeof(wire),changed));
        CHECK(!replay(image,sizeof(image),load,wire,sizeof(wire),pin,&out));
    }
    memcpy(wire,original,sizeof(wire));
    CHECK(!replay(image,sizeof(image)-1u,load,wire,sizeof(wire),pin,&out));
    CHECK(!replay(image,sizeof(image),load+4u,wire,sizeof(wire),pin,&out));
    CHECK(!replay(image,sizeof(image),load+UINT32_C(0x20000000),wire,sizeof(wire),pin,&out));
    CHECK(!replay(image,sizeof(image),load+2u,wire,sizeof(wire),pin,&out));
    CHECK(!replay(image,sizeof(image),UINT32_C(0xcc010000),wire,sizeof(wire),pin,&out));
    CHECK(!replay(image,sizeof(image),load,wire,sizeof(wire)-1u,pin,&out));
    CHECK(!replay(NULL,sizeof(image),load,wire,sizeof(wire),pin,&out));
    CHECK(!replay(image,sizeof(image),load,NULL,sizeof(wire),pin,&out));
    CHECK(!replay(image,sizeof(image),load,wire,sizeof(wire),NULL,&out));
    CHECK(!replay(image,sizeof(image),load,wire,sizeof(wire),wire+992u,&out));
    CHECK(!replay(image,sizeof(image),load,wire,sizeof(wire),image,&out));
    memset(&out,0xa5,sizeof(out));struct kui_vmu_native_resolution before=out;
    CHECK(!kui_vmu_native_resolution_cache_replay(image,sizeof(image),load,wire,sizeof(wire),(const uint8_t *)&out,&out));
    CHECK(!memcmp(&out,&before,sizeof(out)));
    CHECK(!kui_vmu_native_resolution_cache_replay(image,sizeof(image),load,wire,sizeof(wire),pin,NULL));
    CHECK(!kui_vmu_native_resolution_cache_replay(image,sizeof(image),load,wire,sizeof(wire),pin,(void *)image));
    CHECK(!memcmp(wire,original,sizeof(wire)));
    CHECK(!kui_vmu_native_resolution_cache_replay(image,sizeof(image),load,wire,sizeof(wire),pin,(void *)wire));
    CHECK(!memcmp(wire,original,sizeof(wire)));
    CHECK(!kui_vmu_native_resolution_cache_record_digest(wire,sizeof(wire),wire+992u));
    CHECK(!kui_vmu_native_resolution_cache_replay((void *)(uintptr_t)UINT32_C(0x8c100000),1024u,load,wire,sizeof(wire),pin,
        (void *)(uintptr_t)UINT32_C(0xac100040)));
    CHECK(!kui_vmu_native_resolution_cache_replay(image,sizeof(image),load,(void *)(uintptr_t)UINT32_C(0x8c100000),1024u,pin,
        (void *)(uintptr_t)UINT32_C(0xac100040)));
    CHECK(!kui_vmu_native_resolution_cache_replay(image,sizeof(image),load,wire,sizeof(wire),(void *)(UINTPTR_MAX-15u),&out));
    CHECK(!kui_vmu_native_resolution_cache_build((void *)(UINTPTR_MAX-15u),1024u,load,wire,sizeof(wire),&out));
    CHECK(!kui_vmu_native_resolution_cache_build(image,sizeof(image),load,wire,sizeof(wire),&out));
    CHECK(!memcmp(wire,original,sizeof(wire)));
    CHECK(!kui_vmu_native_resolution_cache_build(image,sizeof(image),load,image,sizeof(image),&out));
    CHECK(!kui_vmu_native_resolution_cache_build(image,sizeof(image),load,wire,sizeof(wire),(void *)wire));
    CHECK(!kui_vmu_native_resolution_cache_build((void *)(uintptr_t)UINT32_C(0x8c100000),1024u,load,
        (void *)(uintptr_t)UINT32_C(0xac100000),1024u,&out));
    CHECK(!kui_vmu_native_resolution_cache_encode(&source,wire,1023u));
    CHECK(!kui_vmu_native_resolution_cache_encode(NULL,wire,sizeof(wire)));
    CHECK(!kui_vmu_native_resolution_cache_encode(&source,&source,1024u));
    source.layout.inputs=24u;CHECK(!kui_vmu_native_resolution_cache_encode(&source,wire,sizeof(wire)));
    CHECK(!memcmp(wire,original,sizeof(wire)));
    for(unsigned family=KUI_VMU_NATIVE_EXIT_FAMILY;family<=KUI_VMU_NATIVE_EXIT_PAIR_ONLY;family++) {
        mock(image,sizeof(image),&source);source.exits.family=family;
        if(family==KUI_VMU_NATIVE_EXIT_PAIR_ONLY) {
            source.exits.count=2u;source.exits.entries[2]=0u;source.exits.exec_thunk=0u;
        }
        source.layout.rx_layout=KUI_VMU_NATIVE_RX_UNIFORM48;source.layout.inputs=1u;
        CHECK(kui_vmu_native_resolution_cache_encode(&source,wire,sizeof(wire)));
        CHECK(kui_vmu_native_resolution_cache_record_digest(wire,sizeof(wire),pin));
        CHECK(replay(image,sizeof(image),load,wire,sizeof(wire),pin,&out));CHECK(!memcmp(&source,&out,sizeof(out)));
        CHECK(!strict(image,sizeof(image),load,wire,sizeof(wire),&out));
    }
    source.exits.count=3u;CHECK(!kui_vmu_native_resolution_cache_encode(&source,wire,sizeof(wire)));
    source.exits.count=2u;source.exits.entries[2]=load;
    CHECK(!kui_vmu_native_resolution_cache_encode(&source,wire,sizeof(wire)));
    source.exits.entries[2]=0u;source.exits.exec_thunk=load;
    CHECK(!kui_vmu_native_resolution_cache_encode(&source,wire,sizeof(wire)));
}
static void actual_tests(const char *path) {
    FILE *f=fopen(path,"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));long length=ftell(f);CHECK(length>0 && length<=0xc00000);rewind(f);
    size_t n=(size_t)length;uint8_t *image=malloc(n),*original=malloc(n);CHECK(image && original);
    CHECK(fread(image,1,n,f)==n);CHECK(!fclose(f));memcpy(original,image,n);
    struct kui_vmu_native_resolution fresh,out;uint8_t wire[1024],second[1024],pin[32];
    CHECK(kui_vmu_native_resolution_cache_build(image,n,load,wire,sizeof(wire),&fresh));
    CHECK(kui_vmu_native_resolution_cache_record_digest(wire,sizeof(wire),pin));
    CHECK(kui_vmu_native_resolution_cache_build(image,n,load,second,sizeof(second),NULL));CHECK(!memcmp(wire,second,sizeof(wire)));
    CHECK(replay(image,n,load,wire,sizeof(wire),pin,&out));CHECK(!memcmp(&fresh,&out,sizeof(out)));
    CHECK(strict(image,n,load,wire,sizeof(wire),&out));CHECK(!memcmp(&fresh,&out,sizeof(out)));
    CHECK(fresh.driver.submit==UINT32_C(0x8c2d9b68) && fresh.layout.tx_base==UINT32_C(0xac315180));
    CHECK(fresh.heap.initializer==UINT32_C(0x8c2b15c8) && fresh.call.entry_begin==UINT32_C(0x0c33a880));
    CHECK(fresh.exits.entries[0]==UINT32_C(0x8c2b3a98));
    /* Repaired checksums cannot redirect any stage component under the old
     * trusted pin; the untrusted path must independently reject every forgery. */
    for(unsigned which=0;which<5u;which++) {
        struct kui_vmu_native_resolution forged=fresh;
        if(which==0u) forged.driver.submit+=4u;
        else if(which==1u) forged.layout.tx_base+=32u;
        else if(which==2u) forged.heap.initializer+=4u;
        else if(which==3u) forged.call.entry_begin+=32u;
        else forged.exits.entries[0]+=4u;
        CHECK(kui_vmu_native_resolution_cache_encode(&forged,second,sizeof(second)));
        CHECK(!replay(image,n,load,second,sizeof(second),pin,&out));CHECK(!strict(image,n,load,second,sizeof(second),&out));
    }
    const size_t mutations[]={0x2c9748u+34u,0x2c9748u+116u,0x2c9748u+196u,
        0x2ac4d6u+38u,0x2ac4d6u+54u,0x2ac564u,0x2a0fdcu+84u,0x2a1114u,
        0x1d6c8u-10u,0x1d6c8u-3u,0x1d784u,0x1d788u,0x198ebcu,
        0x2ac1e0u+24u,0x2ac1e0u+36u,0x2ac2f4u,0x2abf84u+32u,0x2ac078u,0x2ac07cu,0x2ac084u,0x2ac088u,
        0x2a15c8u,0x2a15c8u+0x1b2u,0x2a15c8u+0x1bau,0x2a3a98u,0x2a3ac8u,0x29eaa0u,0x29eb0eu};
    for(unsigned i=0;i<sizeof(mutations)/sizeof(mutations[0]);i++) {
        CHECK(mutations[i]<n);memcpy(image,original,n);image[mutations[i]]^=1u;
        CHECK(!replay(image,n,load,wire,sizeof(wire),pin,&out));
        memset(second,0xa5,sizeof(second));
        CHECK(!kui_vmu_native_resolution_cache_build(image,n,load,second,sizeof(second),&out));
        for(unsigned j=0;j<sizeof(second);j++) CHECK(second[j]==0xa5u);
    }
    /* A copied but unregistered timer body has no authority over VBR. The
     * unique immutable registration keeps the original callbacks selected;
     * the changed source must still miss the previous independently pinned
     * cache. Ambiguous registrations are covered by the timer-family tests. */
    memcpy(image,original,n);memcpy(image+0x200u,image+0x2abf84u,0x100u);
    CHECK(!replay(image,n,load,wire,sizeof(wire),pin,&out));
    CHECK(kui_vmu_native_resolution_cache_build(image,n,load,second,sizeof(second),&out));
    CHECK(out.layout.timer1_callback==fresh.layout.timer1_callback &&
        out.layout.timer2_callback==fresh.layout.timer2_callback);
    CHECK(out.driver.submit==fresh.driver.submit && out.heap.initializer==fresh.heap.initializer);
    CHECK(memcmp(out.driver.image_sha256,fresh.driver.image_sha256,32u));
    /* Give the clone its own exact VBR self-registration: unlike an inert
     * body copy, this creates a second supported active timer binding. */
    memcpy(image,original,n);memcpy(image+0x100u,image+0x2abe84u,0x200u);
    uint16_t registration=(uint16_t)(image[0x156u]|(uint16_t)image[0x157u]<<8);
    CHECK((registration&0xf000u)==0xd000u);
    size_t registration_slot=((0x156u+4u)&~3u)+4u*(registration&255u);
    CHECK(registration_slot+4u<=0x300u);
    for(unsigned i=0;i<4u;i++) image[registration_slot+i]=(uint8_t)((load+0x200u)>>(8u*i));
    CHECK(!kui_vmu_native_resolution_cache_build(image,n,load,second,sizeof(second),&out));
    memcpy(image,original,n);CHECK(replay(image,n,load,wire,sizeof(wire),pin,&out));
    CHECK(!memcmp(image,original,n));free(original);free(image);
}
static void generic_actual_tests(const char *path) {
    FILE *f=fopen(path,"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));long length=ftell(f);CHECK(length>0 && length<=0xc00000);rewind(f);
    size_t n=(size_t)length;uint8_t *image=malloc(n);CHECK(image);CHECK(fread(image,1,n,f)==n);CHECK(!fclose(f));
    uint8_t wire[1024],again[1024],pin[32],source_hash[32],program_hash[32],after_hash[32];
    struct kui_vmu_native_resolution fresh,out;size_t program_bytes=0u;
    digest(image,n,source_hash);
    CHECK(kui_vmu_native_program_extent(image,n,load,&program_bytes));
    CHECK(program_bytes && program_bytes<=n && (program_bytes==n || n-program_bytes==0x8000u));
    digest(image,program_bytes,program_hash);
    CHECK(kui_vmu_native_resolution_cache_build(image,program_bytes,load,wire,sizeof(wire),&fresh));
    CHECK(fresh.driver.image_bytes==program_bytes && !memcmp(fresh.driver.image_sha256,program_hash,32u));
    CHECK(get32(wire+24u)==program_bytes && !memcmp(wire+32u,program_hash,32u));
    CHECK(kui_vmu_native_resolution_cache_record_digest(wire,sizeof(wire),pin));
    CHECK(replay(image,program_bytes,load,wire,sizeof(wire),pin,&out));CHECK(!memcmp(&fresh,&out,sizeof(fresh)));
    CHECK(strict(image,program_bytes,load,wire,sizeof(wire),&out));CHECK(!memcmp(&fresh,&out,sizeof(fresh)));
    CHECK(kui_vmu_native_resolution_cache_build(image,program_bytes,load,again,sizeof(again),NULL));CHECK(!memcmp(wire,again,sizeof(wire)));
    if(program_bytes!=n) {
        /* Live-image APIs retain their explicit extent contract: only the
         * source-boundary helper has authority to discard the proven IP. */
        CHECK(!replay(image,n,load,wire,sizeof(wire),pin,&out));
        CHECK(!kui_vmu_native_resolution_cache_build(image,n,load,again,sizeof(again),NULL));
    }
    CHECK((fresh.layout.rx_layout==KUI_VMU_NATIVE_RX_UNIFORM48 && fresh.layout.inputs==1u) ||
        (fresh.layout.rx_layout==KUI_VMU_NATIVE_RX_EXPLICIT48 && fresh.layout.inputs==12u));
    CHECK(fresh.exits.family>=KUI_VMU_NATIVE_EXIT_FAMILY && fresh.exits.family<=KUI_VMU_NATIVE_EXIT_PAIR_ONLY);
    CHECK(fresh.exits.count==(fresh.exits.family==KUI_VMU_NATIVE_EXIT_PAIR_ONLY?2u:3u));
    digest(image,n,after_hash);CHECK(!memcmp(source_hash,after_hash,32u));
    image[program_bytes-1u]^=1u;CHECK(!replay(image,program_bytes,load,wire,sizeof(wire),pin,&out));
    char source_hex[65],program_hex[65];kui_hex(source_hash,32u,source_hex);kui_hex(program_hash,32u,program_hex);
    printf("  Complete cache corpus: %s layout=%u inputs=%u exit-family=%u source-bytes=%zu program-bytes=%zu "
        "discarded-suffix-bytes=%zu source-sha256=%s image-sha256=%s\n",path,
        fresh.layout.rx_layout,fresh.layout.inputs,fresh.exits.family,n,program_bytes,n-program_bytes,source_hex,program_hex);
    free(image);
}
int main(int argc,char **argv) {
    CHECK(argc>=1);codec_and_trust_tests();if(argc>=2) actual_tests(argv[1]);
    for(int i=2;i<argc;i++) generic_actual_tests(argv[i]);
    printf("Native complete resolution cache: %u checks passed%s\n",checks,argc>=2?" including actual images, stale/forged records and first-build mutations":"");
    return 0;
}
