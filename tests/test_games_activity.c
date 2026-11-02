/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/games_activity.h"
#include "kui/hash.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct kui_games_activity table,snapshot,decoded;
static uint8_t wire[KUI_GAMES_ACTIVITY_WIRE_MAX],changed[KUI_GAMES_ACTIVITY_WIRE_MAX];
static const char group[]="0123456789abcdef0123456789abcdef";
static unsigned checks;
#define CHECK(expression) do {++checks;assert(expression);} while(0)
static void put32(uint8_t *p,uint32_t n) {for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i));}
static void checksums(uint8_t *p,size_t bytes) {
    put32(p+32,kui_crc32(0,p+40,bytes-40));put32(p+36,kui_crc32(0,p,36));
}
static void rejected(const uint8_t *record,size_t bytes) {
    uint64_t sequence=456;decoded=snapshot;
    CHECK(!kui_games_activity_decode(&decoded,&sequence,record,bytes));
    CHECK(sequence==456 && !memcmp(&decoded,&snapshot,sizeof(decoded)));
}
static void keys_and_updates(void) {
    struct kui_games_activity_entry key={0},old;
    CHECK(kui_games_activity_key(&key,"0:\\Games\\Title\\disc.GDI",""));
    CHECK(!strcmp(key.path,"/games/title/disc.gdi") && !key.group[0]);old=key;
    CHECK(!kui_games_activity_key(&key,"1:/Games/disc.gdi",""));
    CHECK(!memcmp(&key,&old,sizeof(key)));
    CHECK(!kui_games_activity_key(&key,"/Games/../disc.gdi",""));
    CHECK(!kui_games_activity_key(&key,"/Games/title./disc.gdi",""));
    CHECK(!kui_games_activity_key(&key,"/Games/disc.gdi","short"));
    CHECK(!kui_games_activity_key(&key,"/Games/disc.gdi","0123456789abcdef0123456789abcdeg"));
    CHECK(!kui_games_activity_key(&key,"/",""));
    CHECK(kui_games_activity_key(&key,"/","0123456789ABCDEF0123456789ABCDEF"));
    CHECK(!strcmp(key.group,group) && !strcmp(key.path,"/"));
    char long_path[500];memset(long_path,'a',sizeof(long_path));long_path[0]='/';long_path[sizeof(long_path)-1]=0;
    CHECK(!kui_games_activity_key(&key,long_path,""));

    memset(&table,0,sizeof(table));
    CHECK(kui_games_activity_favorite(&table,"/Games/Title/disc.gdi","",true));
    CHECK(table.count==1 && !table.clock);
    CHECK(kui_games_activity_played(&table,"0:/GAMES/TITLE/DISC.GDI",""));
    CHECK(table.count==1 && table.clock==1 && table.entries[0].favorite && table.entries[0].played==1);
    CHECK(kui_games_activity_favorite(&table,"/Games/Title/disc.gdi","",false));
    CHECK(table.count==1 && !table.entries[0].favorite && table.entries[0].played==1);
    CHECK(kui_games_activity_favorite(&table,"/Games/Unplayed/disc.gdi","",true));
    CHECK(table.count==2);
    CHECK(kui_games_activity_favorite(&table,"/Games/Unplayed/disc.gdi","",false));
    CHECK(table.count==1);
    CHECK(kui_games_activity_favorite(&table,"/Games/Missing/disc.gdi","",false));
    CHECK(table.count==1);
    CHECK(kui_games_activity_favorite(&table,"/Games",group,true));
    CHECK(kui_games_activity_played(&table,"/Games",group));
    CHECK(table.count==2 && table.clock==2);
    const struct kui_games_activity_entry *found=kui_games_activity_find(&table,"0:/GAMES",group);
    CHECK(found && found->favorite && found->played==2);
}
static void codec(void) {
    size_t bytes=0;uint64_t sequence=0;
    CHECK(kui_games_activity_encode(wire,sizeof(wire),&bytes,&table,12));
    CHECK(bytes>40 && bytes<=sizeof(wire));
    CHECK(kui_games_activity_decode(&decoded,&sequence,wire,bytes));
    CHECK(sequence==12 && decoded.count==2 && decoded.clock==2);
    CHECK(kui_games_activity_find(&decoded,"/games/title/disc.gdi","")->played==1);
    snapshot=decoded;
    for(size_t i=0;i<bytes;i++) {
        memcpy(changed,wire,bytes);changed[i]^=0x80;rejected(changed,bytes);
    }
    for(size_t size=0;size<bytes;size++)rejected(wire,size);
    memcpy(changed,wire,bytes);changed[bytes]=0;rejected(changed,bytes+1);
    memcpy(changed,wire,bytes);changed[43]=2;checksums(changed,bytes);rejected(changed,bytes);
    memcpy(changed,wire,bytes);changed[44]=3;checksums(changed,bytes);rejected(changed,bytes);
    memcpy(changed,wire,bytes);changed[53]='G';checksums(changed,bytes);rejected(changed,bytes);
    memcpy(changed,wire,bytes);changed[52]=0;checksums(changed,bytes);rejected(changed,bytes);
    memcpy(changed,wire,bytes);changed[42]=31;checksums(changed,bytes);rejected(changed,bytes);
    memcpy(changed,wire,bytes);put32(changed+12,KUI_GAMES_ACTIVITY_CAP+1u);checksums(changed,bytes);rejected(changed,bytes);
    memcpy(changed,wire,bytes);memset(changed+16,0,8);checksums(changed,bytes);rejected(changed,bytes);
    CHECK(!kui_games_activity_encode(changed,bytes-1,&bytes,&table,1));
    CHECK(!kui_games_activity_encode(changed,sizeof(changed),&bytes,&table,0));
    struct kui_games_activity_entry old=table.entries[1];table.entries[1]=table.entries[0];
    CHECK(!kui_games_activity_encode(changed,sizeof(changed),&bytes,&table,1));table.entries[1]=old;
}
static void bounded_history(void) {
    memset(&table,0,sizeof(table));char path[64];
    for(unsigned i=0;i<KUI_GAMES_ACTIVITY_CAP;i++) {
        snprintf(path,sizeof(path),"/Games/Game%03u/disc.gdi",i);
        CHECK(kui_games_activity_played(&table,path,""));
    }
    CHECK(kui_games_activity_favorite(&table,"/Games/Game000/disc.gdi","",true));
    CHECK(kui_games_activity_played(&table,"/Games/New/disc.gdi",""));
    CHECK(table.count==KUI_GAMES_ACTIVITY_CAP && table.clock==KUI_GAMES_ACTIVITY_CAP+1u);
    CHECK(kui_games_activity_find(&table,"/Games/Game000/disc.gdi","")!=NULL);
    CHECK(kui_games_activity_find(&table,"/Games/Game001/disc.gdi","")==NULL);
    CHECK(kui_games_activity_played(&table,"/Games/Game000/disc.gdi",""));
    CHECK(kui_games_activity_find(&table,"/Games/Game000/disc.gdi","")->played==table.clock);

    memset(&table,0,sizeof(table));
    for(unsigned i=0;i<KUI_GAMES_ACTIVITY_CAP;i++) {
        snprintf(path,sizeof(path),"/Games/Game%03u/disc.gdi",i);
        CHECK(kui_games_activity_favorite(&table,path,"",true));
    }
    snapshot=table;
    CHECK(!kui_games_activity_played(&table,"/Games/Extra/disc.gdi",""));
    CHECK(!memcmp(&table,&snapshot,sizeof(table)));
    CHECK(!kui_games_activity_favorite(&table,"/Games/Extra/disc.gdi","",true));
    CHECK(!memcmp(&table,&snapshot,sizeof(table)));
    CHECK(kui_games_activity_favorite(&table,"/Games/Game000/disc.gdi","",false));
    CHECK(table.count==KUI_GAMES_ACTIVITY_CAP-1u);
    CHECK(kui_games_activity_played(&table,"/Games/Extra/disc.gdi",""));
    CHECK(table.count==KUI_GAMES_ACTIVITY_CAP);
    table.clock=UINT64_MAX;snapshot=table;
    CHECK(!kui_games_activity_played(&table,"/Games/Extra/disc.gdi",""));
    CHECK(!memcmp(&table,&snapshot,sizeof(table)));
}
int main(void) {
    keys_and_updates();codec();bounded_history();
    printf("PASS Games activity: %u checks; canonical keys, corruption rejection, bounded recent history and favorite preservation\n",checks);
    return 0;
}
