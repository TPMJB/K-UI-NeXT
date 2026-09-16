/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/capture.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void digest(const void *p,size_t n,size_t chunk,const char *want) {
    struct kui_sha256 s;kui_sha256_init(&s);
    for(size_t pos=0;pos<n;) {
        size_t count=n-pos<chunk?n-pos:chunk;kui_sha256_update(&s,(const uint8_t *)p+pos,count);pos+=count;
    }
    uint8_t d[32];char h[65];kui_sha256_digest(&s,d);kui_hex(d,32,h);assert(!strcmp(h,want));
    kui_sha256_digest(&s,d);kui_hex(d,32,h);assert(!strcmp(h,want));
}
static uint32_t edc_reference(const uint8_t *p,size_t n) {
    uint32_t crc=0;
    for(size_t i=0;i<n;i++) {
        crc^=p[i];for(unsigned j=0;j<8;j++) crc=crc&1?(crc>>1)^0xd8018001u:crc>>1;
    }
    return crc;
}
static void put32(uint8_t *p,uint32_t n) {for(unsigned i=0;i<4;i++) p[i]=(uint8_t)(n>>(i*8));}
static void seal(uint8_t *record) {put32(record+4092,kui_crc32(0,record,4092));}
int main(void) {
    digest("",0,1,"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    digest("abc",3,1,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const char *long_text="abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    for(size_t c=1;c<=70;c++) digest(long_text,strlen(long_text),c,"248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    static char million[1000000];memset(million,'a',sizeof(million));
    digest(million,sizeof(million),1003,"cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    uint8_t raw[2352]={0};memset(raw+1,255,10);raw[15]=1;
    for(unsigned i=16;i<2064;i++) raw[i]=(uint8_t)i;
    put32(raw+2064,edc_reference(raw,2064));assert(kui_sector_edc_valid(raw));
    raw[31]^=1;assert(!kui_sector_edc_valid(raw));raw[31]^=1;
    memset(raw+16,0,8);raw[15]=2;put32(raw+2072,edc_reference(raw+16,2056));assert(kui_sector_edc_valid(raw));
    raw[18]=raw[22]=0x20;assert(!kui_sector_edc_valid(raw));
    assert(kui_cd_edc((const uint8_t *)million,sizeof(million))==edc_reference((const uint8_t *)million,sizeof(million)));
    struct kui_toc sessions[2]={ {.tracks={{1,4,150,650},{2,0,650,950}},.count=2},
        {.tracks={{3,4,45150,50000},{4,0,50000,50400},{5,0,50400,51000},{6,4,51000,51813}},.count=4} };
    struct kui_capture_plan plan;assert(kui_plan_tracks(sessions,&plan));
    assert(plan.count==6 && plan.tracks[0].end==500 && plan.tracks[1].end==950);
    assert(plan.tracks[2].end==49850 && plan.tracks[3].end==50400 && plan.tracks[4].end==50850 && plan.tracks[5].end==51813);
    sessions[1].tracks[1].number=9;assert(!kui_plan_tracks(sessions,&plan));sessions[1].tracks[1].number=4;
    sessions[0].tracks[0].end=200;assert(!kui_plan_tracks(sessions,&plan));sessions[0].tracks[0].end=650;
    assert(kui_plan_tracks(sessions,&plan));
    struct kui_checkpoint a={.sequence=12,.count=6,.build="0123456789ab"},b;
    uint8_t record[4096];a.identity[0]=19;
    kui_checkpoint_encode(&a,record);assert(kui_checkpoint_decode(record,&plan,a.identity,&b));
    a.track[0].sectors=350;a.track[0].crc32=123;memset(a.track[0].sha256,2,32);
    a.track[1].sectors=16;kui_checkpoint_encode(&a,record);
    assert(kui_checkpoint_decode(record,&plan,a.identity,&b) && b.track[1].sectors==16);
    record[120]^=1;assert(!kui_checkpoint_decode(record,&plan,a.identity,&b));
    kui_checkpoint_encode(&a,record);record[24]^=1;seal(record);assert(!kui_checkpoint_decode(record,&plan,a.identity,&b));
    kui_checkpoint_encode(&a,record);put32(record+96+2*40,1);seal(record);assert(!kui_checkpoint_decode(record,&plan,a.identity,&b));
    kui_checkpoint_encode(&a,record);put32(record+96,351);seal(record);assert(!kui_checkpoint_decode(record,&plan,a.identity,&b));
    kui_checkpoint_encode(&a,record);record[90]=1;seal(record);assert(!kui_checkpoint_decode(record,&plan,a.identity,&b));
    puts("PASS SHA-256 vectors/chunks, CD EDC, GDI gap/address plan, checkpoint identity/bounds/order");
    return 0;
}
