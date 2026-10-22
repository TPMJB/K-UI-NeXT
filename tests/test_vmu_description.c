/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_description.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void put16(uint8_t *p,unsigned n) {p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);}
static void put32(uint8_t *p,uint32_t n) {for(unsigned i=0;i<4;i++) p[i]=(uint8_t)(n>>(i*8));}
static void header(uint8_t p[128]) {
    memset(p,0,128);memset(p,' ',48);memcpy(p,"Short save",10);
    memcpy(p+16,"Long description",16);memcpy(p+48,"GAME_APP",8);
    put16(p+64,1);put32(p+72,384);
}
static void safe(const char *text,size_t cap) {
    size_t n=0;while(n<cap && text[n]) {assert((unsigned char)text[n]>=32 && (unsigned char)text[n]<=126);++n;}
    assert(n<cap);
}
int main(void) {
    uint8_t p[128];struct kui_vmu_description out;header(p);
    assert(kui_vmu_description_parse(p,sizeof(p),1024,&out));
    assert(!strcmp(out.description,"Long description") && !strcmp(out.app_id,"GAME_APP"));
    /* The display header requires neither CRC16 nor a payload read. */
    assert(!p[70] && !p[71]);
    memset(p+16,' ',32);assert(kui_vmu_description_parse(p,128,1024,&out));
    assert(!strcmp(out.description,"Short save"));
    memset(p,' ',48);assert(kui_vmu_description_parse(p,128,1024,&out));
    assert(!out.description[0] && !strcmp(out.app_id,"GAME_APP"));
    /* Fixed-width fields can use all bytes without a terminator. */
    memset(p,'S',16);memset(p+16,'L',32);memset(p+48,'A',16);
    assert(kui_vmu_description_parse(p,128,1024,&out));
    assert(strlen(out.description)==32 && strlen(out.app_id)==16);
    assert(out.description[31]=='L' && out.app_id[15]=='A');
    header(p);memset(p+16,' ',32);memcpy(p+16,"  Save\n\t\x1b\x7f",10);p[26]=0x82;p[27]=0xa0;p[28]=' ';p[29]=0;
    memcpy(p+48," APP\n\x81\x40 ",8);
    assert(kui_vmu_description_parse(p,128,1024,&out));
    assert(!strcmp(out.description,"Save    ??") && !strcmp(out.app_id,"APP ?@"));
    safe(out.description,sizeof(out.description));safe(out.app_id,sizeof(out.app_id));
    /* A NUL terminates the bounded field; bytes after it cannot become UI text. */
    header(p);p[16]=0;p[17]=0x1b;
    assert(kui_vmu_description_parse(p,128,1024,&out) && !strcmp(out.description,"Short save"));
    header(p);put16(p+64,0);put32(p+72,384);
    assert(kui_vmu_description_parse(p,128,512,&out));
    for(unsigned icon=4;icon<=65535;icon+=65531) {
        header(p);put16(p+64,icon);assert(!kui_vmu_description_parse(p,128,131072,&out));assert(!out.description[0] && !out.app_id[0]);
    }
    header(p);put16(p+68,4);assert(!kui_vmu_description_parse(p,128,131072,&out));
    header(p);p[76]=1;assert(!kui_vmu_description_parse(p,128,1024,&out));
    header(p);assert(!kui_vmu_description_parse(p,127,1024,&out));
    assert(!kui_vmu_description_parse(p,128,127,&out));
    assert(!kui_vmu_description_parse(NULL,128,1024,&out));
    assert(!kui_vmu_description_parse(p,128,1024,NULL));
    header(p);assert(!kui_vmu_description_parse(p,128,639,&out));
    header(p);put32(p+72,385);assert(!kui_vmu_description_parse(p,128,1024,&out));
    header(p);put32(p+72,UINT32_MAX);assert(!kui_vmu_description_parse(p,128,1024,&out));
    static const unsigned eye_bytes[]={0,8064,4544,2048};
    for(unsigned eye=0;eye<4;eye++) {
        header(p);put16(p+68,eye);size_t bytes=128+512+384+eye_bytes[eye];
        assert(kui_vmu_description_parse(p,128,bytes,&out));
        assert(!kui_vmu_description_parse(p,128,bytes-1,&out));
    }
    memset(p,0xa5,sizeof(p));assert(!kui_vmu_description_parse(p,128,131072,&out));
    puts("PASS VMU display descriptions");return 0;
}
