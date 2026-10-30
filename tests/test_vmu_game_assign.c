/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_game_assign.h"
#include "kui/retail_image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);exit(1);}}while(0)
static struct kui_vmu_game_assign_table table,decoded,before;
static struct kui_vmu_game_assignment batch[KUI_VMU_GAME_ASSIGN_MAX];
static uint8_t wire[KUI_VMU_GAME_ASSIGN_WIRE_BYTES],bad[KUI_VMU_GAME_ASSIGN_WIRE_BYTES+1u];
static void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4u;i++)p[i]=(uint8_t)(v>>(8u*i));}
static void recalc(uint8_t *p) {
    uint32_t crc=kui_retail_crc32(0,p,508u);
    crc=kui_retail_crc32(crc,p+512u,KUI_VMU_GAME_ASSIGN_WIRE_BYTES-512u);put32(p+508u,crc);
}
static bool zero(const void *p,size_t n){const uint8_t *b=p;for(size_t i=0;i<n;i++)if(b[i])return false;return true;}
static void refused(uint8_t *p,size_t bytes) {
    memset(&decoded,0x5a,sizeof(decoded));CHECK(!kui_vmu_game_assign_decode(p,bytes,&decoded));
    CHECK(zero(&decoded,sizeof(decoded)));
}
static void make_entry(struct kui_vmu_game_assignment *entry,unsigned number) {
    memset(entry,0,sizeof(*entry));char product[12];snprintf(product,sizeof(product),"T-%04u",number);
    CHECK(kui_vmu_game_card_key(product,entry->key));entry->pair=number%9999u+1u;entry->identity[0]=1u;
    entry->identity[1]=(uint8_t)number;entry->identity[2]=(uint8_t)(number>>8u);
}
static void roundtrip(void) {
    memset(&table,0,sizeof(table));make_entry(batch,1);make_entry(batch+1,2);
    batch[1].pair=batch[0].pair;memcpy(batch[1].identity,batch[0].identity,16u);
    CHECK(kui_vmu_game_assign_update(&table,batch,2u));CHECK(table.count==2u);
    CHECK(!kui_vmu_game_assign_encode(&table,wire));CHECK(zero(wire,sizeof(wire)));
    table.sequence=0x123456789abcdef0ULL;
    CHECK(kui_vmu_game_assign_encode(&table,wire));
    CHECK(!memcmp(wire,"KUIVMUA1",8u));CHECK(wire[16]==0xf0u && wire[23]==0x12u);
    CHECK(kui_vmu_game_assign_decode(wire,sizeof(wire),&decoded));
    CHECK(!memcmp(&decoded,&table,sizeof(table)));
    CHECK(kui_vmu_game_assign_find(&decoded,batch[0].key)==decoded.entries);
    CHECK(kui_vmu_game_assign_find(&decoded,batch[1].key)==decoded.entries+1u);
    char missing[12]="T-9999";CHECK(!kui_vmu_game_assign_find(&decoded,missing));
    table.sequence=UINT64_MAX;CHECK(kui_vmu_game_assign_encode(&table,wire));
    CHECK(kui_vmu_game_assign_decode(wire,sizeof(wire),&decoded) && decoded.sequence==UINT64_MAX);
}
static void corrupt(void) {
    CHECK(kui_vmu_game_assign_encode(&table,wire));
    /* A changed byte in the metadata or any live/unused record cannot pass. */
    for(size_t i=0;i<sizeof(wire);i++) {
        memcpy(bad,wire,sizeof(wire));bad[i]^=1u;refused(bad,sizeof(wire));
    }
    refused(wire,sizeof(wire)-1u);memcpy(bad,wire,sizeof(wire));bad[sizeof(wire)]=0;refused(bad,sizeof(bad));
    refused(NULL,sizeof(wire));CHECK(!kui_vmu_game_assign_decode(wire,sizeof(wire),NULL));
    static const unsigned fields[]={8u,12u,24u,28u,32u,36u,40u,507u,512u+10u,512u+32u*2u};
    for(size_t i=0;i<sizeof(fields)/sizeof(fields[0]);i++) {
        memcpy(bad,wire,sizeof(wire));bad[fields[i]]^=0x80u;recalc(bad);refused(bad,sizeof(wire));
    }
    memcpy(bad,wire,sizeof(wire));memset(bad+16u,0,8u);recalc(bad);refused(bad,sizeof(wire));
    memcpy(bad,wire,sizeof(wire));memcpy(bad+544u,bad+512u,12u);recalc(bad);refused(bad,sizeof(wire));
    memcpy(bad,wire,sizeof(wire));memset(bad+528u,0,16u);recalc(bad);refused(bad,sizeof(wire));
    memcpy(bad,wire,sizeof(wire));put32(bad+524u,10000u);recalc(bad);refused(bad,sizeof(wire));
    memcpy(bad,wire,sizeof(wire));put32(bad+524u,0u);recalc(bad);refused(bad,sizeof(wire));
}
static void updates(void) {
    before=table;
    batch[1]=batch[0];CHECK(!kui_vmu_game_assign_update(&table,batch,2u));CHECK(!memcmp(&table,&before,sizeof(table)));
    make_entry(batch+1,3u);memset(batch[1].identity,0,16u);
    CHECK(!kui_vmu_game_assign_update(&table,batch,2u));CHECK(!memcmp(&table,&before,sizeof(table)));
    make_entry(batch+1,3u);batch[1].key[0]='t';
    CHECK(!kui_vmu_game_assign_update(&table,batch,2u));CHECK(!memcmp(&table,&before,sizeof(table)));
    CHECK(!kui_vmu_game_assign_update(&table,NULL,1u));CHECK(!memcmp(&table,&before,sizeof(table)));
    CHECK(!kui_vmu_game_assign_update(&table,table.entries,1u));CHECK(!memcmp(&table,&before,sizeof(table)));
    CHECK(kui_vmu_game_assign_update(&table,NULL,0u));CHECK(!memcmp(&table,&before,sizeof(table)));
    make_entry(batch,1u);batch[0].pair=9999u;batch[0].identity[7]=0x5au;make_entry(batch+1,3u);
    CHECK(kui_vmu_game_assign_update(&table,batch,2u));CHECK(table.count==3u && table.sequence==before.sequence);
    CHECK(!memcmp(table.entries,batch,sizeof(batch[0])));CHECK(!memcmp(table.entries+2,batch+1,sizeof(batch[1])));
    memset(&table,0,sizeof(table));
    for(unsigned i=0;i<KUI_VMU_GAME_ASSIGN_MAX;i++)make_entry(batch+i,i+1u);
    CHECK(kui_vmu_game_assign_update(&table,batch,KUI_VMU_GAME_ASSIGN_MAX));CHECK(table.count==KUI_VMU_GAME_ASSIGN_MAX);
    table.sequence=1u;CHECK(kui_vmu_game_assign_encode(&table,wire));CHECK(kui_vmu_game_assign_decode(wire,sizeof(wire),&decoded));
    CHECK(!memcmp(&table,&decoded,sizeof(table)));before=table;
    make_entry(batch,257u);CHECK(!kui_vmu_game_assign_update(&table,batch,1u));CHECK(!memcmp(&table,&before,sizeof(table)));
    make_entry(batch,256u);batch[0].identity[15]=0x5au;
    CHECK(kui_vmu_game_assign_update(&table,batch,1u));CHECK(table.count==KUI_VMU_GAME_ASSIGN_MAX);
    CHECK(table.entries[255].identity[15]==0x5au);
    table.count=KUI_VMU_GAME_ASSIGN_MAX+1u;before=table;
    CHECK(!kui_vmu_game_assign_update(&table,NULL,0u));CHECK(!memcmp(&table,&before,sizeof(table)));
    CHECK(!kui_vmu_game_assign_encode(&table,wire));CHECK(zero(wire,sizeof(wire)));
    CHECK(!kui_vmu_game_assign_encode(NULL,wire));CHECK(!kui_vmu_game_assign_encode(&table,NULL));
    CHECK(!kui_vmu_game_assign_update(NULL,NULL,0u));
}
int main(void){roundtrip();corrupt();updates();printf("VMU game assignments: %u checks passed\n",checks);return 0;}
