/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_native_cache_seed.h"
#include "kui/vmu_native_resolution_cache.h"
#include "kui/hash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)) {fprintf(stderr,"Cache seed failed at line %u\n",__LINE__);exit(1);}} while(0)
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void hash(const void *p,size_t n,uint8_t out[32]) {
    struct kui_sha256 h;kui_sha256_init(&h);kui_sha256_update(&h,p,n);kui_sha256_digest(&h,out);
}
static void mock_wire(uint8_t wire[1024]) {
    /* Codec-only fixture: static metadata, not a discovery certificate. The
     * seed codec must validate canonical records but grants no hook authority. */
    struct kui_vmu_native_resolution r={0};r.driver.version=1u;r.driver.bytes=sizeof(r.driver);
    r.driver.family=1u;r.driver.image_bytes=4096u;r.driver.load_address=UINT32_C(0x8c010000);
    r.driver.requirements=7u;r.driver.gate_register=12u;
    hash("cache seed codec fixture",24u,r.driver.image_sha256);
    r.layout.version=1u;r.layout.bytes=sizeof(r.layout);r.layout.rx_layout=1u;r.layout.inputs=12u;
    r.heap.version=1u;r.heap.bytes=sizeof(r.heap);r.heap.family=1u;r.heap.layout=1u;
    r.heap.image_bytes=r.driver.image_bytes;r.heap.load=r.driver.load_address;
    r.heap.initializer_slot=UINT32_C(0x8c010100);  /* Kind 1: an initializer call record. */
    r.exits.version=KUI_VMU_NATIVE_EXIT_VERSION;r.exits.bytes=sizeof(r.exits);r.exits.family=1u;
    r.exits.image_bytes=r.driver.image_bytes;r.exits.load=r.driver.load_address;r.exits.count=3u;
    CHECK(kui_vmu_native_resolution_cache_encode(&r,wire,1024u));
}
static void read_refused(const void *slot,size_t bytes) {
    const uint8_t *wire=(const uint8_t *)(uintptr_t)UINT32_C(0x12345678);
    const uint8_t *digest=(const uint8_t *)(uintptr_t)UINT32_C(0x23456789);
    CHECK(!kui_vmu_native_cache_seed_read(slot,bytes,&wire,&digest));
    CHECK(wire==(const uint8_t *)(uintptr_t)UINT32_C(0x12345678));
    CHECK(digest==(const uint8_t *)(uintptr_t)UINT32_C(0x23456789));
}
static void fill_refused(uint8_t slot[1120],size_t bytes,const void *wire,size_t wire_bytes) {
    uint8_t before[1120];memcpy(before,slot,sizeof(before));
    CHECK(!kui_vmu_native_cache_seed_fill(slot,bytes,wire,wire_bytes));
    CHECK(!memcmp(slot,before,sizeof(before)));
}
static void check_filled(const uint8_t slot[1120],const uint8_t expected_wire[1024]) {
    const uint8_t *wire=NULL,*digest=NULL;uint8_t actual[32];
    CHECK(kui_vmu_native_cache_seed_read(slot,1120u,&wire,&digest));
    CHECK(wire==slot+KUI_VMU_NATIVE_CACHE_SEED_WIRE_OFFSET);
    CHECK(digest==slot+KUI_VMU_NATIVE_CACHE_SEED_DIGEST_OFFSET);
    CHECK(!memcmp(wire,expected_wire,1024u));
    CHECK(kui_vmu_native_resolution_cache_record_digest(wire,1024u,actual));
    CHECK(!memcmp(digest,actual,sizeof(actual)));
    CHECK(get32(slot+16u)==1u && get32(slot+20u)==1024u);
    for(unsigned i=56u;i<64u;i++) CHECK(!slot[i]);
    for(unsigned i=1088u;i<1120u;i++) CHECK(!slot[i]);
}
static void template_and_find(void) {
    uint8_t slot[1120],canonical[1120],module[2u*1120u+137u];
    memset(slot,0xa5,sizeof(slot));kui_vmu_native_cache_seed_template(slot);
    CHECK(!memcmp(slot,"KUINVS01",8u));CHECK(get32(slot+8u)==1u && get32(slot+12u)==1120u);
    CHECK(get32(slot+16u)==0u && get32(slot+20u)==1024u);
    for(unsigned i=24u;i<sizeof(slot);i++) CHECK(!slot[i]);
    memcpy(canonical,slot,sizeof(slot));kui_vmu_native_cache_seed_template(NULL);
    CHECK(kui_vmu_native_cache_seed_find(slot,sizeof(slot))==0u);read_refused(slot,sizeof(slot));
    CHECK(kui_vmu_native_cache_seed_find(NULL,0u)==SIZE_MAX);
    CHECK(kui_vmu_native_cache_seed_find(NULL,1120u)==SIZE_MAX);
    CHECK(kui_vmu_native_cache_seed_find(slot,1119u)==SIZE_MAX);
    CHECK(kui_vmu_native_cache_seed_find((const void *)(UINTPTR_MAX-100u),1120u)==SIZE_MAX);
    const unsigned offsets[]={0u,1u,2u,3u,7u,31u,32u,37u,1120u+137u};
    for(unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++) {
        memset(module,0xa5,sizeof(module));memcpy(module+offsets[i],canonical,sizeof(canonical));
        CHECK(kui_vmu_native_cache_seed_find(module,sizeof(module))==offsets[i]);
        CHECK(kui_vmu_native_cache_seed_find(module,offsets[i]+1120u)==offsets[i]);
        CHECK(kui_vmu_native_cache_seed_find(module,offsets[i]+1119u)==SIZE_MAX);
    }
    memset(module,0xa5,sizeof(module));memcpy(module,canonical,1120u);memcpy(module+1121u,canonical,1120u);
    CHECK(kui_vmu_native_cache_seed_find(module,sizeof(module))==SIZE_MAX);
    /* A malformed marker is not a valid second slot. */
    module[8u]^=1u;CHECK(kui_vmu_native_cache_seed_find(module,sizeof(module))==1121u);
    module[1121u+12u]^=1u;CHECK(kui_vmu_native_cache_seed_find(module,sizeof(module))==SIZE_MAX);
    CHECK(!memcmp(slot,canonical,sizeof(slot)));
}
static void output_alias_guards(const uint8_t wire[1024]) {
    _Alignas(const uint8_t *) uint8_t slot[1120],before[1120];
    kui_vmu_native_cache_seed_template(slot);CHECK(kui_vmu_native_cache_seed_fill(slot,sizeof(slot),wire,1024u));
    memcpy(before,slot,sizeof(before));const uint8_t *a=(const uint8_t *)(uintptr_t)1u,*b=(const uint8_t *)(uintptr_t)2u;
    CHECK(!kui_vmu_native_cache_seed_read(slot,sizeof(slot),&a,&a));CHECK(a==(const uint8_t *)(uintptr_t)1u);
    CHECK(!kui_vmu_native_cache_seed_read(slot,sizeof(slot),NULL,&b));CHECK(b==(const uint8_t *)(uintptr_t)2u);
    CHECK(!kui_vmu_native_cache_seed_read(slot,sizeof(slot),&a,NULL));CHECK(a==(const uint8_t *)(uintptr_t)1u);
    CHECK(!kui_vmu_native_cache_seed_read(slot,sizeof(slot),(const uint8_t **)(void *)(slot+64u),&b));
    CHECK(!kui_vmu_native_cache_seed_read(slot,sizeof(slot),&a,(const uint8_t **)(void *)(slot+24u)));
    CHECK(!memcmp(slot,before,sizeof(slot)));
    /* Physical aliases must fail before valid() reads the synthetic target
     * addresses. These calls intentionally supply no mapped host RAM there. */
    CHECK(!kui_vmu_native_cache_seed_read((const void *)(uintptr_t)UINT32_C(0x8c100000),1120u,
        (const uint8_t **)(uintptr_t)UINT32_C(0xac100040),&b));
    CHECK(!kui_vmu_native_cache_seed_read((const void *)(uintptr_t)UINT32_C(0xac100000),1120u,
        &a,(const uint8_t **)(uintptr_t)UINT32_C(0x0c100018)));
    CHECK(!kui_vmu_native_cache_seed_read(slot,sizeof(slot),
        (const uint8_t **)(uintptr_t)UINT32_C(0x8c200000),(const uint8_t **)(uintptr_t)UINT32_C(0xac200000)));
    CHECK(!kui_vmu_native_cache_seed_read(slot,sizeof(slot),(const uint8_t **)(UINTPTR_MAX-1u),&b));
    CHECK(a==(const uint8_t *)(uintptr_t)1u && b==(const uint8_t *)(uintptr_t)2u);
    CHECK(!memcmp(slot,before,sizeof(slot)));
    read_refused(NULL,sizeof(slot));read_refused(slot,0u);read_refused(slot,1119u);read_refused(slot,1121u);
    read_refused((const void *)(UINTPTR_MAX-100u),1120u);
    /* Output objects directly adjacent to the slot are disjoint and valid. */
    struct {const uint8_t *left;uint8_t bytes[1120];const uint8_t *right;} adjacent;
    memcpy(adjacent.bytes,slot,1120u);adjacent.left=adjacent.right=NULL;
    CHECK(kui_vmu_native_cache_seed_read(adjacent.bytes,1120u,&adjacent.left,&adjacent.right));
    CHECK(adjacent.left==adjacent.bytes+64u && adjacent.right==adjacent.bytes+24u);
    CHECK(!memcmp(adjacent.bytes,slot,1120u));
}
static void record_cases(const uint8_t wire[1024]) {
    uint8_t slot[1120],canonical_empty[1120],filled[1120],corrupt[1120],bad_wire[1024];
    kui_vmu_native_cache_seed_template(slot);memcpy(canonical_empty,slot,sizeof(slot));
    CHECK(kui_vmu_native_cache_seed_fill(slot,sizeof(slot),wire,1024u));check_filled(slot,wire);
    memcpy(filled,slot,sizeof(filled));
    CHECK(kui_vmu_native_cache_seed_find(slot,sizeof(slot))==0u);
    /* Filling from the existing interior record must snapshot before replacing
     * its enclosing seed, including replacing an already-filled container. */
    CHECK(kui_vmu_native_cache_seed_fill(slot,sizeof(slot),slot+64u,1024u));
    CHECK(!memcmp(slot,filled,sizeof(slot)));check_filled(slot,wire);
    fill_refused(slot,1119u,wire,1024u);fill_refused(slot,1121u,wire,1024u);
    fill_refused(slot,sizeof(slot),NULL,1024u);fill_refused(slot,sizeof(slot),wire,0u);
    fill_refused(slot,sizeof(slot),wire,1023u);fill_refused(slot,sizeof(slot),wire,1025u);
    fill_refused(slot,sizeof(slot),slot,1024u);fill_refused(slot,sizeof(slot),slot+24u,1024u);
    fill_refused(slot,sizeof(slot),(const void *)(UINTPTR_MAX-100u),1024u);
    CHECK(!kui_vmu_native_cache_seed_fill(NULL,1120u,wire,1024u));
    CHECK(!kui_vmu_native_cache_seed_fill((void *)(UINTPTR_MAX-100u),1120u,wire,1024u));
    memcpy(bad_wire,wire,sizeof(bad_wire));
    for(unsigned i=0;i<sizeof(bad_wire);i++) {
        bad_wire[i]^=1u;fill_refused(slot,sizeof(slot),bad_wire,sizeof(bad_wire));bad_wire[i]^=1u;
    }
    for(unsigned i=0;i<sizeof(slot);i++) {
        memcpy(corrupt,filled,sizeof(corrupt));corrupt[i]^=1u;
        read_refused(corrupt,sizeof(corrupt));CHECK(kui_vmu_native_cache_seed_find(corrupt,sizeof(corrupt))==SIZE_MAX);
        fill_refused(corrupt,sizeof(corrupt),wire,1024u);
        memcpy(corrupt,canonical_empty,sizeof(corrupt));corrupt[i]^=1u;
        read_refused(corrupt,sizeof(corrupt));CHECK(kui_vmu_native_cache_seed_find(corrupt,sizeof(corrupt))==SIZE_MAX);
        fill_refused(corrupt,sizeof(corrupt),wire,1024u);
    }
    /* Re-sealing a changed record without updating the independent seed pin
     * must fail. Nonzero reserved seed bytes fail even with valid wire/pin. */
    memcpy(corrupt,filled,sizeof(corrupt));corrupt[64u+88u]^=4u;
    hash(corrupt+64u,992u,corrupt+64u+992u);read_refused(corrupt,sizeof(corrupt));
    fill_refused(corrupt,sizeof(corrupt),wire,1024u);
    memcpy(corrupt,filled,sizeof(corrupt));corrupt[16u]=2u;read_refused(corrupt,sizeof(corrupt));
    memcpy(corrupt,filled,sizeof(corrupt));corrupt[56u]=1u;read_refused(corrupt,sizeof(corrupt));
    memcpy(corrupt,filled,sizeof(corrupt));corrupt[1119u]=1u;read_refused(corrupt,sizeof(corrupt));
    output_alias_guards(wire);
    kui_vmu_native_cache_seed_template(slot);CHECK(!memcmp(slot,canonical_empty,sizeof(slot)));read_refused(slot,sizeof(slot));
}
int main(int argc,char **argv) {
    template_and_find();uint8_t wire[1024],previous[1024];mock_wire(wire);record_cases(wire);
    memcpy(previous,wire,sizeof(previous));
    for(int arg=1;arg<argc;arg++) {
        FILE *f=fopen(argv[arg],"rb");CHECK(f);CHECK(fread(wire,1,sizeof(wire),f)==sizeof(wire));
        CHECK(fgetc(f)==EOF);CHECK(!fclose(f));uint8_t pin[32];
        CHECK(kui_vmu_native_resolution_cache_record_digest(wire,sizeof(wire),pin));record_cases(wire);
        /* A launcher may reuse an already-filled launch-module slot for a
         * different game. Replacing the pin and wire must be one full copy. */
        uint8_t slot[1120],module[2240];kui_vmu_native_cache_seed_template(slot);
        CHECK(kui_vmu_native_cache_seed_fill(slot,sizeof(slot),previous,sizeof(previous)));
        CHECK(kui_vmu_native_cache_seed_fill(slot,sizeof(slot),wire,sizeof(wire)));check_filled(slot,wire);
        memcpy(module,slot,1120u);memcpy(module+1120u,slot,1120u);
        CHECK(kui_vmu_native_cache_seed_find(module,sizeof(module))==SIZE_MAX);
        memcpy(previous,wire,sizeof(previous));
    }
    printf("Native cache seed C codec: %u checks passed%s\n",checks,argc>1?" including generated metadata records":"");
    return 0;
}
