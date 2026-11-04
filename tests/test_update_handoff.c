/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/update_handoff.h"
#include "kui/runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"Update handoff failed %u: %s\n",__LINE__,#x);exit(1);}}while(0)
static const uint8_t empty[KUI_UPDATE_HANDOFF_BYTES]=KUI_UPDATE_HANDOFF_INITIALIZER;
static void put32(uint8_t *p,uint32_t n) {for(unsigned i=0;i<4u;i++)p[i]=(uint8_t)(n>>(8u*i));}
static void put64(uint8_t *p,uint64_t n) {for(unsigned i=0;i<8u;i++)p[i]=(uint8_t)(n>>(8u*i));}
static void seal(uint8_t *wire) {put32(wire+188u,kui_crc32(0,wire,188u));}
static bool all_zero(const void *data,size_t bytes) {
    const uint8_t *p=data;for(size_t i=0;i<bytes;i++)if(p[i])return false;return true;
}
static struct kui_update_handoff sample(void) {
    struct kui_update_handoff h={.build="012345abcdef",.transport=1u,.payload_offset=73u,
        .payload_crc32=UINT32_C(0x89abcdef),.memory_bytes=16384u,
        .sequence=UINT64_C(0x123456789abcdef0),.file_bytes=8192u};
    for(unsigned i=0;i<32u;i++) {h.manifest_sha256[i]=(uint8_t)(i*7u);h.runtime_sha256[i]=(uint8_t)(255u-i*3u);}
    for(unsigned i=0;i<16u;i++)h.nonce[i]=(uint8_t)(17u+i*13u);
    return h;
}
static void equal(const struct kui_update_handoff *a,const struct kui_update_handoff *b) {
    CHECK(!memcmp(a->build,b->build,sizeof(a->build)) && a->transport==b->transport &&
        a->payload_offset==b->payload_offset && a->payload_crc32==b->payload_crc32 &&
        a->memory_bytes==b->memory_bytes && a->sequence==b->sequence && a->file_bytes==b->file_bytes &&
        !memcmp(a->manifest_sha256,b->manifest_sha256,32u) &&
        !memcmp(a->runtime_sha256,b->runtime_sha256,32u) && !memcmp(a->nonce,b->nonce,16u));
}
static void refused(const uint8_t *wire) {
    struct kui_update_handoff h;memset(&h,0xa5,sizeof(h));
    CHECK(!kui_update_handoff_decode(wire,&h));CHECK(all_zero(&h,sizeof(h)));
}
static void codec_tests(void) {
    struct kui_update_handoff original=sample(),decoded;uint8_t wire[KUI_UPDATE_HANDOFF_BYTES],mutated[KUI_UPDATE_HANDOFF_BYTES];
    CHECK(kui_update_handoff_encode(wire,&original));CHECK(kui_update_handoff_decode(wire,&decoded));equal(&original,&decoded);
    CHECK(!memcmp(wire,empty,16u));CHECK(wire[16u]==1u && wire[20u]==1u && wire[24u]==254u);
    CHECK(wire[48u]==0xf0u && wire[55u]==0x12u && wire[56u]==0u && wire[57u]==32u && wire[128u]==73u);
    CHECK(!kui_update_handoff_encode(NULL,&original));CHECK(!kui_update_handoff_encode(wire,NULL));
    CHECK(!kui_update_handoff_decode(wire,NULL));refused(NULL);refused(empty);
    for(unsigned i=0;i<KUI_UPDATE_HANDOFF_BYTES;i++)for(unsigned bit=0;bit<8u;bit++) {
        memcpy(mutated,wire,sizeof(wire));mutated[i]^=(uint8_t)(1u<<bit);refused(mutated);
    }
    /* CRC repair must not admit malformed structure or reserved bytes. */
    static const unsigned structural[]={0u,1u,2u,3u,4u,5u,6u,7u,8u,9u,10u,11u,12u,13u,14u,15u,
        16u,17u,18u,19u,24u,25u,26u,27u,28u,29u,30u,31u,44u,45u,46u,47u,
        148u,149u,150u,151u,152u,153u,154u,155u,156u,157u,158u,159u,160u,161u,162u,163u,
        172u,173u,174u,175u,176u,177u,178u,179u,
        180u,181u,182u,183u,184u,185u,186u,187u};
    for(unsigned i=0;i<sizeof(structural)/sizeof(structural[0]);i++) {
        memcpy(mutated,wire,sizeof(wire));mutated[structural[i]]^=1u;seal(mutated);refused(mutated);
    }
    for(unsigned i=32u;i<44u;i++) {
        memcpy(mutated,wire,sizeof(wire));mutated[i]='G';seal(mutated);refused(mutated);
    }
    for(unsigned transport=0;transport<5u;transport++) {
        memcpy(mutated,wire,sizeof(wire));put32(mutated+20u,transport);put32(mutated+24u,~transport);seal(mutated);
        if(transport<3u) {CHECK(kui_update_handoff_decode(mutated,&decoded));CHECK(decoded.transport==transport);}
        else refused(mutated);
    }
    memcpy(mutated,wire,sizeof(wire));put64(mutated+48u,0u);seal(mutated);refused(mutated);
    static const uint64_t bad_sizes[]={0u,255u,257u,UINT64_C(64)+KUI_RUNTIME_MAX_BYTES+1u,UINT64_MAX};
    for(unsigned i=0;i<sizeof(bad_sizes)/sizeof(bad_sizes[0]);i++) {
        memcpy(mutated,wire,sizeof(wire));put64(mutated+56u,bad_sizes[i]);seal(mutated);refused(mutated);
    }
    memcpy(mutated,wire,sizeof(wire));put32(mutated+128u,8192u-64u-KUI_UPDATE_HANDOFF_BYTES+1u);seal(mutated);refused(mutated);
    memcpy(mutated,wire,sizeof(wire));put32(mutated+128u,UINT32_MAX);seal(mutated);refused(mutated);
    memcpy(mutated,wire,sizeof(wire));memset(mutated+132u,0,16u);memset(mutated+148u,255,16u);seal(mutated);refused(mutated);
    static const uint32_t bad_memory[]={0u,8124u,16385u,KUI_RUNTIME_MAX_MEMORY+4u,UINT32_MAX};
    for(unsigned i=0;i<sizeof(bad_memory)/sizeof(bad_memory[0]);i++) {
        memcpy(mutated,wire,sizeof(wire));put32(mutated+168u,bad_memory[i]);seal(mutated);refused(mutated);
    }
    /* Hashes, all nonzero sequences, and boundary-contained slots are values,
     * whose bank/receipt identity is checked by the boot integration. */
    original=sample();original.sequence=UINT64_MAX;original.file_bytes=64u+KUI_UPDATE_HANDOFF_BYTES;original.payload_offset=0u;
    original.memory_bytes=KUI_UPDATE_HANDOFF_BYTES;original.payload_crc32=0u;
    memset(original.manifest_sha256,0,32u);memset(original.runtime_sha256,0,32u);
    CHECK(kui_update_handoff_encode(wire,&original));CHECK(kui_update_handoff_decode(wire,&decoded));equal(&original,&decoded);
    original.file_bytes=64u+KUI_RUNTIME_MAX_BYTES;original.payload_offset=KUI_RUNTIME_MAX_BYTES-KUI_UPDATE_HANDOFF_BYTES;
    original.memory_bytes=KUI_RUNTIME_MAX_MEMORY;original.payload_crc32=UINT32_MAX;
    CHECK(kui_update_handoff_encode(wire,&original));CHECK(kui_update_handoff_decode(wire,&decoded));equal(&original,&decoded);
    for(unsigned kind=0;kind<12u;kind++) {
        original=sample();
        if(kind==0u)original.build[12]='0';
        if(kind==1u)original.build[0]='A';
        if(kind==2u)original.transport=3u;
        if(kind==3u)original.sequence=0u;
        if(kind==4u)original.file_bytes=255u;
        if(kind==5u)original.file_bytes=65u+KUI_RUNTIME_MAX_BYTES;
        if(kind==6u)original.payload_offset=UINT32_MAX;
        if(kind==7u)memset(original.nonce,0,16u);
        if(kind==8u)original.file_bytes=8193u;
        if(kind==9u)original.memory_bytes=8124u;
        if(kind==10u)original.memory_bytes=16385u;
        if(kind==11u)original.memory_bytes=KUI_RUNTIME_MAX_MEMORY+4u;
        CHECK(!kui_update_handoff_encode(wire,&original));
    }
}
static bool scan_split(const uint8_t *data,size_t bytes,size_t split) {
    struct kui_update_handoff_scan s={0};
    kui_update_handoff_scan_update(&s,data,split);kui_update_handoff_scan_update(&s,data+split,bytes-split);
    return kui_update_handoff_scan_valid(&s);
}
static bool scan_width(const uint8_t *data,size_t bytes,size_t width) {
    struct kui_update_handoff_scan s={0};
    for(size_t i=0;i<bytes;i+=width)kui_update_handoff_scan_update(&s,data+i,bytes-i<width?bytes-i:width);
    return kui_update_handoff_scan_valid(&s);
}
static void all_splits(const uint8_t *data,size_t bytes,bool valid) {
    for(size_t split=0;split<=bytes;split++)CHECK(scan_split(data,bytes,split)==valid);
    for(size_t width=1; width<=bytes+1u;width++)CHECK(scan_width(data,bytes,width)==valid);
}
static void empty_and_scan_tests(void) {
    uint8_t data[3u*KUI_UPDATE_HANDOFF_BYTES+227u];uint32_t offset;
    struct kui_update_handoff h=sample();uint8_t filled[KUI_UPDATE_HANDOFF_BYTES];CHECK(kui_update_handoff_encode(filled,&h));
    CHECK(!kui_update_handoff_empty(NULL,0,NULL));CHECK(!kui_update_handoff_scan_valid(NULL));
    struct kui_update_handoff_scan s={0};kui_update_handoff_scan_update(NULL,empty,sizeof(empty));
    kui_update_handoff_scan_update(&s,NULL,1u);kui_update_handoff_scan_update(&s,empty,0u);CHECK(!kui_update_handoff_scan_valid(&s));
    CHECK(kui_update_handoff_empty(empty,sizeof(empty),&offset) && offset==0u);
    CHECK(kui_update_handoff_empty(empty,sizeof(empty),NULL));all_splits(empty,sizeof(empty),true);
    CHECK(!kui_update_handoff_empty(filled,sizeof(filled),NULL));all_splits(filled,sizeof(filled),false);
    for(size_t bytes=0;bytes<KUI_UPDATE_HANDOFF_BYTES;bytes++) {
        CHECK(!kui_update_handoff_empty(empty,bytes,NULL));all_splits(empty,bytes,false);
    }
    /* Every EMPTY byte participates in admission; a populated on-disk slot
     * cannot masquerade as the one initialized slot permitted in a package. */
    for(unsigned i=0;i<KUI_UPDATE_HANDOFF_BYTES;i++) {
        memcpy(data,empty,sizeof(empty));data[i]^=1u;offset=UINT32_MAX;
        CHECK(!kui_update_handoff_empty(data,sizeof(empty),&offset) && offset==UINT32_MAX);
        all_splits(data,sizeof(empty),false);
    }
    /* Exercise every placement against every split and uniform chunk width,
     * including complete slots ending exactly at the last byte. */
    for(unsigned at=0;at<=227u;at++) {
        memset(data,0x5a,sizeof(data));memcpy(data+at,empty,sizeof(empty));
        CHECK(kui_update_handoff_empty(data,at+sizeof(empty),&offset) && offset==at);
        all_splits(data,at+sizeof(empty),true);
    }
    memset(data,0x5a,sizeof(data));all_splits(data,sizeof(data),false);
    for(unsigned gap=0;gap<=35u;gap++) {
        memset(data,0x5a,sizeof(data));memcpy(data+7u,empty,sizeof(empty));
        memcpy(data+7u+sizeof(empty)+gap,empty,sizeof(empty));
        size_t bytes=7u+2u*sizeof(empty)+gap;
        CHECK(!kui_update_handoff_empty(data,bytes,NULL));all_splits(data,bytes,false);
    }
    /* A second full header followed by a truncated body is a duplicate even
     * when no chunk ever contains the whole second slot. Partial headers
     * shorter than 16 bytes are ordinary trailing payload bytes. */
    for(unsigned truncation=0;truncation<KUI_UPDATE_HANDOFF_BYTES;truncation++) {
        memset(data,0x5a,sizeof(data));memcpy(data+9u,empty,sizeof(empty));
        memcpy(data+9u+sizeof(empty)+13u,empty,truncation);
        size_t bytes=9u+sizeof(empty)+13u+truncation;bool valid=truncation<16u;
        CHECK(kui_update_handoff_empty(data,bytes,NULL)==valid);all_splits(data,bytes,valid);
    }
    /* Empty plus populated, and populated plus empty, always fail uniqueness. */
    for(unsigned order=0;order<2u;order++) {
        memcpy(data,order?empty:filled,sizeof(empty));memcpy(data+sizeof(empty),order?filled:empty,sizeof(empty));
        CHECK(!kui_update_handoff_empty(data,2u*sizeof(empty),NULL));all_splits(data,2u*sizeof(empty),false);
    }
#if SIZE_MAX>UINT32_MAX
    CHECK(!kui_update_handoff_empty(empty,(size_t)UINT32_MAX+1u,NULL));
#endif
}
int main(void) {
    codec_tests();empty_and_scan_tests();
    printf("PASS update handoff: %u checks; codec corruption/structure, empty-slot admission and streaming boundary equivalence\n",checks);
    return 0;
}
