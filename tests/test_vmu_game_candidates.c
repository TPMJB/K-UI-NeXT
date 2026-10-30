/* SPDX-License-Identifier: GPL-3.0-only */
/* Exercise the actual private wire codec/bounds. Filesystem/public API tests
 * link this same production file separately against real FAT32/exFAT. */
#include "../src/core/vmu_game_candidates.c"
#include <stdio.h>
#include <stdlib.h>

static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);exit(1);}}while(0)
static struct candidate_table fixture,back,before,reverse;
static uint8_t wire[KUI_VMU_GAME_CANDIDATES_WIRE_BYTES],bad[KUI_VMU_GAME_CANDIDATES_WIRE_BYTES+1u];
static uint32_t independent_crc(uint32_t previous,const void *data,size_t bytes) {
    const uint8_t *p=data;uint32_t crc=~previous;
    for(size_t i=0;i<bytes;i++) {
        crc^=p[i];for(unsigned bit=0;bit<8u;bit++)crc=(crc>>1)^((crc&1u)?UINT32_C(0xedb88320):0u);
    }
    return ~crc;
}
static void repair_crc(uint8_t *p) {
    uint32_t crc=independent_crc(0,p,508u);
    crc=independent_crc(crc,p+512u,KUI_VMU_GAME_CANDIDATES_WIRE_BYTES-512u);put32(p+508u,crc);
}
static void initialize(struct candidate_table *t) {
    memset(t,0,sizeof(*t));t->sequence=UINT64_C(0x123456789abcdef0);
    for(unsigned i=0;i<16u;i++)t->storage.cid[i]=(uint8_t)(i+1u);
    t->storage.card_sectors=UINT64_C(0x100000000);t->storage.partition_first=2048u;
    t->storage.partition_end=UINT64_C(0xffffffff);t->storage.high_capacity=1u;
}
static struct kui_vmu_game_assignment entry(unsigned game,unsigned pair) {
    struct kui_vmu_game_assignment e={0};char product[12];
    snprintf(product,sizeof(product),"T-%04u",game);CHECK(kui_vmu_game_card_key(product,e.key));
    e.pair=pair;e.identity[0]=0x5au;e.identity[1]=(uint8_t)pair;e.identity[2]=(uint8_t)(pair>>8u);return e;
}
static void refused(size_t bytes) {
    memset(&back,0x5a,sizeof(back));CHECK(!decode(bad,bytes,&back));
}
static void roundtrip(void) {
    initialize(&fixture);struct kui_vmu_game_assignment e=entry(2u,5u);CHECK(!add(&fixture,&e));
    e=entry(1u,4u);CHECK(!add(&fixture,&e));e=entry(1u,2u);CHECK(!add(&fixture,&e));
    fixture.entries[1].flags=DEFAULT_FLAG;
    CHECK(valid_table(&fixture));CHECK(encode(&fixture,wire));
    CHECK(!memcmp(wire,"KUIVMUC2",8u) && get32(wire+8u)==2u);
    CHECK(wire[16u]==0xf0u && wire[23u]==0x12u && get32(wire+24u)==3u);
    CHECK(get32(wire+28u)==40u && get32(wire+32u)==512u && get32(wire+36u)==2048u);
    CHECK(!memcmp(wire+40u,fixture.storage.cid,16u) && get64(wire+56u)==UINT64_C(0x100000000));
    CHECK(get64(wire+64u)==2048u && get64(wire+72u)==UINT64_C(0xffffffff) && get32(wire+80u)==1u);
    CHECK(!memcmp(wire+512u,"T-0001",6u) && get32(wire+524u)==2u);
    CHECK(get32(wire+552u+32u)==1u && zero(wire+84u,424u));
    uint32_t crc=independent_crc(0,wire,508u);
    CHECK(independent_crc(crc,wire+512u,sizeof(wire)-512u)==get32(wire+508u));
    CHECK(decode(wire,sizeof(wire),&back) && !memcmp(&back,&fixture,sizeof(back)));
    initialize(&reverse);e=entry(1u,2u);CHECK(!add(&reverse,&e));e=entry(2u,5u);CHECK(!add(&reverse,&e));
    e=entry(1u,4u);CHECK(!add(&reverse,&e));reverse.entries[1].flags=DEFAULT_FLAG;
    CHECK(!memcmp(&fixture,&reverse,sizeof(fixture)));
    before=fixture;CHECK(!add(&fixture,&e) && !memcmp(&fixture,&before,sizeof(fixture)));
    e.identity[15]=1u;CHECK(add(&fixture,&e)==KUI_VMU_GAME_CANDIDATES_STALE && !memcmp(&fixture,&before,sizeof(fixture)));
}
static void corruption(void) {
    /* Every header/live-record byte and a byte in every payload sector is
     * covered; use independent CRC repair below to test semantic validation. */
    for(size_t at=0;at<512u+3u*40u;at++) {
        memcpy(bad,wire,sizeof(wire));bad[at]^=1u;refused(sizeof(wire));
    }
    for(size_t at=512u;at<sizeof(wire);at+=512u) {
        memcpy(bad,wire,sizeof(wire));bad[at+511u]^=0x80u;refused(sizeof(wire));
    }
    memcpy(bad,wire,sizeof(wire));refused(sizeof(wire)-1u);bad[sizeof(wire)]=0u;refused(sizeof(bad));
    static const size_t fields[]={8u,12u,28u,32u,36u,84u,507u,512u+10u,512u+36u,512u+3u*40u};
    for(size_t i=0;i<sizeof(fields)/sizeof(fields[0]);i++) {
        memcpy(bad,wire,sizeof(wire));bad[fields[i]]^=0x80u;repair_crc(bad);refused(sizeof(wire));
    }
    memcpy(bad,wire,sizeof(wire));put64(bad+16u,0u);repair_crc(bad);refused(sizeof(wire));
    memcpy(bad,wire,sizeof(wire));put32(bad+24u,2049u);repair_crc(bad);refused(sizeof(wire));
    memcpy(bad,wire,sizeof(wire));put32(bad+80u,2u);repair_crc(bad);refused(sizeof(wire));
    memcpy(bad,wire,sizeof(wire));memset(bad+40u,0,16u);repair_crc(bad);refused(sizeof(wire));
    memcpy(bad,wire,sizeof(wire));put64(bad+72u,2048u);repair_crc(bad);refused(sizeof(wire));
    memcpy(bad,wire,sizeof(wire));put64(bad+72u,UINT64_C(0x100000001));repair_crc(bad);refused(sizeof(wire));
    memcpy(bad,wire,sizeof(wire));put32(bad+524u,0u);repair_crc(bad);refused(sizeof(wire));
    memcpy(bad,wire,sizeof(wire));put32(bad+524u,10000u);repair_crc(bad);refused(sizeof(wire));
    memcpy(bad,wire,sizeof(wire));memset(bad+528u,0,16u);repair_crc(bad);refused(sizeof(wire));
    memcpy(bad,wire,sizeof(wire));put32(bad+544u,2u);repair_crc(bad);refused(sizeof(wire));
    memcpy(bad,wire,sizeof(wire));put32(bad+544u,1u);repair_crc(bad);refused(sizeof(wire));
    memcpy(bad,wire,sizeof(wire));put32(bad+564u,2u);repair_crc(bad);refused(sizeof(wire));
    memcpy(bad,wire,sizeof(wire));uint8_t saved[40];memcpy(saved,bad+512u,40u);
    memcpy(bad+512u,bad+552u,40u);memcpy(bad+552u,saved,40u);repair_crc(bad);refused(sizeof(wire));
    CHECK(!decode(NULL,sizeof(wire),&back));
}
static void limits(void) {
    initialize(&fixture);
    for(unsigned pair=1u;pair<=32u;pair++) {struct kui_vmu_game_assignment e=entry(1u,pair);CHECK(!add(&fixture,&e));}
    CHECK(valid_table(&fixture));before=fixture;struct kui_vmu_game_assignment e=entry(1u,33u);
    CHECK(add(&fixture,&e)==KUI_VMU_GAME_CANDIDATES_FULL && !memcmp(&fixture,&before,sizeof(fixture)));
    fixture.entries[0].flags=fixture.entries[1].flags=1u;CHECK(!valid_table(&fixture));
    initialize(&fixture);
    for(unsigned game=1u;game<=256u;game++) {e=entry(game,game);CHECK(!add(&fixture,&e));}
    CHECK(valid_table(&fixture));before=fixture;e=entry(257u,257u);
    CHECK(add(&fixture,&e)==KUI_VMU_GAME_CANDIDATES_FULL && !memcmp(&fixture,&before,sizeof(fixture)));
    initialize(&fixture);
    for(unsigned game=1u;game<=256u;game++)for(unsigned variant=1u;variant<=8u;variant++) {
        e=entry(game,(game-1u)*8u+variant);CHECK(!add(&fixture,&e));
    }
    CHECK(fixture.count==2048u && valid_table(&fixture));CHECK(encode(&fixture,wire));
    CHECK(decode(wire,sizeof(wire),&back) && !memcmp(&fixture,&back,sizeof(fixture)));
    before=fixture;e=entry(1u,9999u);
    CHECK(add(&fixture,&e)==KUI_VMU_GAME_CANDIDATES_FULL && !memcmp(&fixture,&before,sizeof(fixture)));
    fixture.sequence=0u;CHECK(!encode(&fixture,wire) && zero(wire,sizeof(wire)));
    fixture=before;fixture.sequence=UINT64_MAX;CHECK(encode(&fixture,wire));
    CHECK(decode(wire,sizeof(wire),&back) && back.sequence==UINT64_MAX);
    fixture=before;fixture.storage.high_capacity=2u;CHECK(!encode(&fixture,wire) && zero(wire,sizeof(wire)));
}
int main(void) {
    roundtrip();corruption();limits();
    printf("VMU game candidates codec/bounds: %u checks passed\n",checks);return 0;
}
