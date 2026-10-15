/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/game_launch_settings.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct kui_game_launch_table table, decoded, before;
static uint8_t record[KUI_GAME_LAUNCH_SETTINGS_RECORD_MAX + 1u], damaged[sizeof(record)];

static void same(struct kui_game_launch_settings a, struct kui_game_launch_settings b) {
    assert(a.reader == b.reader && a.cdda == b.cdda && a.encoding == b.encoding);
    for(unsigned i=0;i<2;i++) {
        assert(a.patches.entries[i].address==b.patches.entries[i].address);
        assert(a.patches.entries[i].value==b.patches.entries[i].value);
        assert(a.patches.entries[i].enabled==b.patches.entries[i].enabled);
    }
}
static void put32(uint8_t *p, uint32_t value) {
    for(unsigned i=0;i<4;i++) p[i]=(uint8_t)(value>>(8*i));
}
/* Independent bitwise IEEE CRC, checked against the standard check vector. */
static uint32_t checksum(const uint8_t *p, size_t size) {
    uint32_t value=UINT32_MAX;
    for(size_t i=0;i<size;i++) {
        value^=p[i];
        for(unsigned bit=0;bit<8;bit++) value=(value>>1)^((value&1)?UINT32_C(0xedb88320):0);
    }
    return ~value;
}
static void reseal(uint8_t *p,size_t size) {put32(p+size-4,checksum(p,size-4));}
static void rejects(const void *bytes,size_t size) {
    memset(&decoded,0xa5,sizeof(decoded));before=decoded;uint64_t sequence=91;
    assert(!kui_game_launch_settings_decode(&decoded,&sequence,bytes,size));
    assert(!memcmp(&decoded,&before,sizeof(decoded)) && sequence==91);
}
static void paths_and_capacity(void) {
    char key[KUI_GAME_LAUNCH_SETTINGS_PATH_CAP], original[sizeof(key)];
    memset(key,0xa5,sizeof(key));
    assert(kui_game_launch_settings_normalize(key,"0:\\Games\\Worms//Disc.GDI"));
    assert(!strcmp(key,"/games/worms/disc.gdi"));
    const char *invalid[]={"","/","0:/","1:/Games/a.gdi","Games/a.gdi","/Games/../a.gdi",
        "/Games/./a.gdi","/Games/a.gdi/","/Games/ a.gdi","/Games/a.gdi ","/Games/a.",
        "/Games/a?.gdi","/Games/a:gdi","/Games/\001a.gdi"};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        memset(key,0x5a,sizeof(key));memcpy(original,key,sizeof(key));
        assert(!kui_game_launch_settings_normalize(key,invalid[i]));
        assert(!memcmp(key,original,sizeof(key)));
    }
    char longest[KUI_GAME_LAUNCH_SETTINGS_PATH_CAP+2u];
    memset(longest,'a',sizeof(longest));longest[0]='/';longest[511]=0;
    assert(kui_game_launch_settings_normalize(key,longest));
    longest[511]='a';longest[512]=0;
    assert(!kui_game_launch_settings_normalize(key,longest));
    struct kui_game_launch_settings a={.reader=KUI_GAME_READER_BACKGROUND,.cdda=true,.encoding=KUI_GAME_ENCODING_PLAIN};
    struct kui_game_launch_settings b={.reader=KUI_GAME_READER_EAGER,.encoding=KUI_GAME_ENCODING_SCRAMBLED},out;
    a.patches.entries[0]=(struct kui_game_ram_patch){.address=UINT32_C(0x8c200000),.value=UINT32_C(0x12345678),.enabled=true};
    kui_game_launch_table_clear(&table);
    assert(kui_game_launch_table_upsert(&table,"/Games/Original/game.gdi",&a));
    assert(kui_game_launch_table_upsert(&table,"/Games/2048/game.gdi",&b));
    assert(kui_game_launch_table_find(&table,"0:\\GAMES\\ORIGINAL\\GAME.GDI",&out));same(out,a);
    assert(kui_game_launch_table_find(&table,"/games/2048/game.gdi",&out));same(out,b);
    assert(kui_game_launch_table_upsert(&table,"0:/GAMES/Original/GAME.GDI",&b));
    assert(table.count==2);
    for(unsigned i=2;i<KUI_GAME_LAUNCH_SETTINGS_ENTRIES;i++) {
        char path[64];snprintf(path,sizeof(path),"/games/title-%u/disc.gdi",i);
        assert(kui_game_launch_table_upsert(&table,path,&a));
    }
    before=table;
    assert(!kui_game_launch_table_upsert(&table,"/games/extra/disc.gdi",&a));
    assert(!memcmp(&table,&before,sizeof(table)));
    assert(kui_game_launch_table_upsert(&table,"/games/2048/game.gdi",&a));
    assert(table.count==KUI_GAME_LAUNCH_SETTINGS_ENTRIES);
    before=table;struct kui_game_launch_settings bad=a;bad.reader=KUI_GAME_READER_COUNT;
    assert(!kui_game_launch_table_upsert(&table,"/games/2048/game.gdi",&bad));
    assert(!memcmp(&table,&before,sizeof(table)));
    bad=a;bad.patches.entries[0].address++;
    assert(!kui_game_launch_table_upsert(&table,"/games/2048/game.gdi",&bad));
    assert(!memcmp(&table,&before,sizeof(table)));
    out=b;assert(!kui_game_launch_table_find(&table,"/missing.gdi",&out));same(out,b);
    puts("PASS launch keys: canonical aliases, unsafe paths, separate image variants and capacity without eviction");
}
static void wire_contract(void) {
    /* Generated independently with Python struct.pack and zlib.crc32. */
    static const uint8_t fixture_v1[]={
        0x4b,0x55,0x49,0x4c,0x43,0x48,0x30,0x31,0x01,0,0,0,0x38,0,0,0,
        0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x01,0,0,0,0,0,0,0,
        0x0c,0,0x02,0x01,0x02,0,0,0,0x2f,0x67,0x61,0x6d,0x65,0x73,0x2f,0x61,
        0x2e,0x67,0x64,0x69,0x84,0x69,0xc9,0xef};
    static const uint8_t fixture[]={
        0x4b,0x55,0x49,0x4c,0x43,0x48,0x30,0x31,0x02,0x00,0x00,0x00,0x48,0x00,0x00,0x00,
        0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x0c,0x00,0x02,0x01,0x02,0x01,0x00,0x00,0x50,0x34,0x12,0x8c,0xef,0xbe,0xad,0xde,
        0x01,0x00,0x00,0x00,0x21,0x43,0x65,0x87,0x2f,0x67,0x61,0x6d,0x65,0x73,0x2f,0x61,
        0x2e,0x67,0x64,0x69,0xb9,0xc1,0x53,0x96};
    assert(checksum((const uint8_t *)"123456789",9)==UINT32_C(0xcbf43926));
    struct kui_game_launch_settings choice={.reader=KUI_GAME_READER_EAGER,.cdda=true,.encoding=KUI_GAME_ENCODING_SCRAMBLED};
    uint64_t sequence=0;
    memset(&decoded,0xa5,sizeof(decoded));
    assert(kui_game_launch_settings_decode(&decoded,&sequence,fixture_v1,sizeof(fixture_v1)));
    assert(sequence==UINT64_C(0x0807060504030201) && decoded.count==1);
    same(decoded.entries[0].settings,choice);
    for(unsigned bit=0;bit<sizeof(fixture_v1)*8u;bit++) {
        memcpy(damaged,fixture_v1,sizeof(fixture_v1));damaged[bit/8]^=(uint8_t)(1u<<(bit%8));
        rejects(damaged,sizeof(fixture_v1));
    }
    for(size_t n=0;n<sizeof(fixture_v1);n++) rejects(fixture_v1,n);
    memcpy(damaged,fixture_v1,sizeof(fixture_v1));damaged[37]=1;
    reseal(damaged,sizeof(fixture_v1));rejects(damaged,sizeof(fixture_v1));
    choice.patches.entries[0]=(struct kui_game_ram_patch){.address=UINT32_C(0x8c123450),.value=UINT32_C(0xdeadbeef),.enabled=true};
    choice.patches.entries[1]=(struct kui_game_ram_patch){.address=1,.value=UINT32_C(0x87654321)};
    kui_game_launch_table_clear(&table);assert(kui_game_launch_table_upsert(&table,"/games/a.gdi",&choice));
    size_t size=0;
    assert(kui_game_launch_settings_encode(record,sizeof(record),&size,&table,UINT64_C(0x0807060504030201)));
    assert(size==sizeof(fixture) && !memcmp(record,fixture,size));
    assert(kui_game_launch_settings_decode(&decoded,&sequence,fixture,sizeof(fixture)));
    assert(sequence==UINT64_C(0x0807060504030201) && decoded.count==1);
    same(decoded.entries[0].settings,choice);
    for(unsigned bit=0;bit<sizeof(fixture)*8u;bit++) {
        memcpy(damaged,fixture,sizeof(fixture));damaged[bit/8]^=(uint8_t)(1u<<(bit%8));
        rejects(damaged,sizeof(fixture));
    }
    for(size_t n=0;n<sizeof(fixture);n++) rejects(fixture,n);
    memcpy(damaged,fixture,sizeof(fixture));damaged[sizeof(fixture)]=0;rejects(damaged,sizeof(fixture)+1);
    const unsigned offsets[]={0,8,12,16,24,28,32,34,35,36,37,38,39,40,56};
    const uint8_t values[]={'X',3,71,0,129,1,0,KUI_GAME_READER_COUNT,2,KUI_GAME_ENCODING_COUNT,4,1,1,0x51,'G'};
    for(unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++) {
        memcpy(damaged,fixture,sizeof(fixture));
        if(offsets[i]==16) memset(damaged+16,0,8);else damaged[offsets[i]]=values[i];
        reseal(damaged,sizeof(fixture));rejects(damaged,sizeof(fixture));
    }
    /* Both patch slots independently validate addresses, and aliases cannot
     * make two enabled entries target the same physical word. */
    memcpy(damaged,fixture,sizeof(fixture));put32(damaged+40,UINT32_C(0xa05f8000));
    reseal(damaged,sizeof(fixture));rejects(damaged,sizeof(fixture));
    memcpy(damaged,fixture,sizeof(fixture));damaged[37]=3;
    reseal(damaged,sizeof(fixture));rejects(damaged,sizeof(fixture));
    memcpy(damaged,fixture,sizeof(fixture));damaged[37]=3;put32(damaged+48,UINT32_C(0x0c123450));
    reseal(damaged,sizeof(fixture));rejects(damaged,sizeof(fixture));
    /* A CRC-valid duplicate key must not merge or silently override a choice. */
    memcpy(damaged,fixture,68);memcpy(damaged+68,fixture+32,36);
    put32(damaged+12,108);put32(damaged+24,2);reseal(damaged,108);rejects(damaged,108);
    memset(damaged,0x5a,sizeof(damaged));memcpy(record,damaged,sizeof(record));size=123;
    assert(!kui_game_launch_settings_encode(record,sizeof(fixture)-1u,&size,&table,1));
    assert(size==123 && !memcmp(record,damaged,sizeof(record)));
    assert(!kui_game_launch_settings_encode(record,sizeof(record),&size,&table,0));
    assert(size==123 && !memcmp(record,damaged,sizeof(record)));
    table.entries[0].path[1]='G';
    assert(!kui_game_launch_settings_encode(record,sizeof(record),&size,&table,1));
    assert(size==123 && !memcmp(record,damaged,sizeof(record)));
    rejects(NULL,sizeof(fixture));
    puts("PASS launch record: independent v1 migration/v2 endian fixtures, patch validation, corruption and output atomicity");
}
static void maximum_record(void) {
    kui_game_launch_table_clear(&table);
    struct kui_game_launch_settings choice={.patches={.entries={{.address=UINT32_C(0xac200000),.value=UINT32_MAX,.enabled=true},
        {.address=UINT32_C(0x0c200004),.value=UINT32_C(0x89abcdef),.enabled=true}}}};
    for(unsigned i=0;i<KUI_GAME_LAUNCH_SETTINGS_ENTRIES;i++) {
        char path[KUI_GAME_LAUNCH_SETTINGS_PATH_CAP];memset(path,'a',sizeof(path));path[0]='/';
        snprintf(path+sizeof(path)-5,5,"%04u",i);
        assert(kui_game_launch_table_upsert(&table,path,&choice));
    }
    size_t size=0;uint64_t sequence=0;
    assert(kui_game_launch_settings_encode(record,sizeof(record),&size,&table,15));
    assert(size==KUI_GAME_LAUNCH_SETTINGS_RECORD_MAX);
    assert(kui_game_launch_settings_decode(&decoded,&sequence,record,size));
    assert(sequence==15 && decoded.count==KUI_GAME_LAUNCH_SETTINGS_ENTRIES);
    for(unsigned i=0;i<decoded.count;i++) same(decoded.entries[i].settings,choice);
    memcpy(damaged,record,sizeof(record));size=123;
    assert(!kui_game_launch_settings_encode(record,KUI_GAME_LAUNCH_SETTINGS_RECORD_MAX-1u,&size,&table,15));
    assert(size==123 && !memcmp(record,damaged,sizeof(record)));
    puts("PASS launch record: 128 maximum-length image paths with two patches fit the bounded v2 record");
}
int main(void) {
    struct kui_game_launch_settings defaults;memset(&defaults,0xa5,sizeof(defaults));kui_game_launch_settings_default(&defaults);
    assert(defaults.reader==KUI_GAME_READER_STANDARD && !defaults.cdda && defaults.encoding==KUI_GAME_ENCODING_AUTO);
    for(unsigned i=0;i<2;i++) assert(!defaults.patches.entries[i].enabled && !defaults.patches.entries[i].address && !defaults.patches.entries[i].value);
    paths_and_capacity();wire_contract();maximum_record();return 0;
}
