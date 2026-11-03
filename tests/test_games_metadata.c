/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/games_metadata.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(condition) do {++checks;assert(condition);} while(0)
static void put32(uint8_t *p,uint32_t n) {for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i));}
static unsigned get16(const uint8_t *p) {return p[0]|(unsigned)p[1]<<8;}
static void seal(uint8_t *wire,size_t bytes) {put32(wire+32,kui_crc32(0,wire+40,bytes-40));put32(wire+36,kui_crc32(0,wire,36));}
static struct kui_games_metadata_entry entry(unsigned index) {
    struct kui_games_metadata_entry e={0};
    snprintf(e.path,sizeof(e.path),"/games/game-%03u/disc.gdi",index);strcpy(e.anchor,e.path);
    snprintf(e.edit.alias,sizeof(e.edit.alias),"Alias %03u",index);strcpy(e.edit.cover_path,"/kui/covers/assigned.png");
    strcpy(e.product,"KUI-TEST");strcpy(e.version,"V1.000");strcpy(e.region,"JUE");
    e.verified=true;e.identity[0]=(uint8_t)(index+1u);e.disc=1;e.representation=2;return e;
}
static void refuses(struct kui_games_metadata *out,const uint8_t *wire,size_t bytes) {
    struct kui_games_metadata *before=malloc(sizeof(*before));CHECK(before!=NULL);*before=*out;uint64_t sequence=777;
    CHECK(!kui_games_metadata_decode(out,&sequence,wire,bytes));
    CHECK(sequence==777 && !memcmp(out,before,sizeof(*out)));free(before);
}
int main(void) {
    struct kui_games_metadata *table=calloc(1,sizeof(*table)),*decoded=calloc(1,sizeof(*decoded)),*before=malloc(sizeof(*before));
    uint8_t *wire=malloc(KUI_GAMES_METADATA_WIRE_MAX),*changed=malloc(KUI_GAMES_METADATA_WIRE_MAX);
    CHECK(table && decoded && before && wire && changed);
    struct kui_games_library_edit edit={0};CHECK(kui_games_library_edit_valid(&edit));
    strcpy(edit.alias,"Keep folder names unchanged");strcpy(edit.cover_path,"/kui/covers/assigned.png");CHECK(kui_games_library_edit_valid(&edit));
    const char *bad_alias[]={" leading","trailing ","bad\nname","bad\x7f","\xc3\x89volution"};
    for(unsigned i=0;i<sizeof(bad_alias)/sizeof(bad_alias[0]);i++) {strcpy(edit.alias,bad_alias[i]);CHECK(!kui_games_library_edit_valid(&edit));}
    memset(edit.alias,'x',sizeof(edit.alias));CHECK(!kui_games_library_edit_valid(&edit));edit.alias[128]=0;CHECK(kui_games_library_edit_valid(&edit));
    const char *bad_cover[]={"0:/kui/covers/art.png","/KUI/covers/art.png","/kui/../art.png","/kui//art.png","/kui/covers/","relative.png","/kui/art?.png"};
    for(unsigned i=0;i<sizeof(bad_cover)/sizeof(bad_cover[0]);i++) {strcpy(edit.cover_path,bad_cover[i]);CHECK(!kui_games_library_edit_valid(&edit));}
    for(unsigned i=0;i<KUI_GAMES_METADATA_CAP;i++) {struct kui_games_metadata_entry e=entry(i);CHECK(kui_games_metadata_upsert(table,&e));}
    CHECK(table->count==KUI_GAMES_METADATA_CAP);
    struct kui_games_metadata_entry e=entry(KUI_GAMES_METADATA_CAP);*before=*table;CHECK(!kui_games_metadata_upsert(table,&e));CHECK(!memcmp(table,before,sizeof(*table)));
    e=entry(64);strcpy(e.edit.alias,"Updated without adding a slot");CHECK(kui_games_metadata_upsert(table,&e));CHECK(table->count==KUI_GAMES_METADATA_CAP);
    const struct kui_games_metadata_entry *found=kui_games_metadata_find(table,"0:/GAMES/GAME-064/DISC.GDI","");
    CHECK(found && !strcmp(found->edit.alias,e.edit.alias));
    size_t bytes=0;CHECK(kui_games_metadata_encode(wire,KUI_GAMES_METADATA_WIRE_MAX,&bytes,table,42));
    uint64_t sequence=0;CHECK(kui_games_metadata_decode(decoded,&sequence,wire,bytes));CHECK(sequence==42 && !memcmp(table,decoded,sizeof(*table)));
    size_t unchanged_size=123;memset(changed,0x5a,bytes);CHECK(!kui_games_metadata_encode(changed,bytes-1,&unchanged_size,table,42));CHECK(unchanged_size==123 && changed[0]==0x5a);
    for(size_t at=0;at<bytes;at++) {
        memcpy(changed,wire,bytes);changed[at]^=0x80;refuses(decoded,changed,bytes);
    }
    for(size_t length=0;length<bytes;length++)refuses(decoded,wire,length);
    static const unsigned reserved[]={24,25,31,59,60,61,62,63,96,97,98,99,100,101,102,103};
    for(unsigned i=0;i<sizeof(reserved)/sizeof(reserved[0]);i++) {memcpy(changed,wire,bytes);changed[reserved[i]]=1;seal(changed,bytes);refuses(decoded,changed,bytes);}
    memcpy(changed,wire,bytes);changed[40+16]=2;seal(changed,bytes);refuses(decoded,changed,bytes);
    memcpy(changed,wire,bytes);changed[40+17]=100;seal(changed,bytes);refuses(decoded,changed,bytes);
    memcpy(changed,wire,bytes);changed[40+18]=4;seal(changed,bytes);refuses(decoded,changed,bytes);
    memcpy(changed,wire,bytes);changed[40+18]=0;seal(changed,bytes);sequence=0;
    CHECK(kui_games_metadata_decode(decoded,&sequence,changed,bytes) && sequence==42 && decoded->entries[0].verified && !decoded->entries[0].representation);
    memcpy(changed,wire,bytes);memset(changed+40+24,0,32);seal(changed,bytes);refuses(decoded,changed,bytes);
    memcpy(changed,wire,bytes);changed[104+1]='G';seal(changed,bytes);refuses(decoded,changed,bytes);
    size_t next=40+64;for(unsigned i=0;i<8;i++)next+=get16(wire+40+2*i);
    memcpy(changed,wire,bytes);memcpy(changed+next+64,wire+104,get16(wire+40));seal(changed,bytes);refuses(decoded,changed,bytes);
    e=entry(0);strcpy(e.path,"/Games/game-000/disc.gdi");*before=*table;CHECK(!kui_games_metadata_upsert(table,&e));CHECK(!memcmp(table,before,sizeof(*table)));
    e=entry(0);memset(e.identity,0,sizeof(e.identity));CHECK(!kui_games_metadata_upsert(table,&e));
    e=entry(0);e.representation=4;CHECK(!kui_games_metadata_upsert(table,&e));
    e=entry(0);e.verified=false;CHECK(!kui_games_metadata_upsert(table,&e));
    memset(table,0,sizeof(*table));memset(&e,0,sizeof(e));strcpy(e.path,"/games");strcpy(e.anchor,"/games");memset(e.group,'a',32);
    strcpy(e.edit.alias,"Disc-set alias");CHECK(kui_games_metadata_upsert(table,&e));
    CHECK(kui_games_metadata_find(table,"/Games",e.group)!=NULL);
    e.verified=true;e.identity[0]=1;CHECK(!kui_games_metadata_upsert(table,&e));
    CHECK(!kui_games_metadata_encode(wire,KUI_GAMES_METADATA_WIRE_MAX,&bytes,table,0));
    CHECK(!kui_games_library_edit_valid(NULL));CHECK(!kui_games_metadata_upsert(NULL,&e));
    free(table);free(decoded);free(before);free(wire);free(changed);
    printf("PASS Games metadata: %u canonical path/edit/capacity/semantic corruption checks\n",checks);return 0;
}
