/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_ce_helper.h"
#include "kui/smash_ce_transport.h"
#include "kui/smash_ce_profile.h"
#include "kui/runtime.h"
#include "kui/retail_image.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void put(uint8_t *p,uint32_t n) {for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8u*i));}
static void header(uint8_t *p,unsigned kind,unsigned raw) {
    const uint32_t words[]={KUI_SMASH_CE_HELPER_MAGIC0,KUI_SMASH_CE_HELPER_MAGIC1,1,32,kind,raw,49152,0};
    for(unsigned i=0;i<8;i++)put(p+4u*i,words[i]);
}
static void core(void) {
    _Alignas(4) uint8_t data[512]={0};struct kui_smash_ce_helper out;
    assert(kui_smash_ce_helper_find(data,sizeof(data),1,&out)==0 && !out.raw);
    put(data, KUI_SMASH_CE_HELPER_MAGIC0);put(data+4,KUI_SMASH_CE_HELPER_MAGIC1);
    assert(kui_smash_ce_helper_find(data,sizeof(data),1,&out)==0); /* literal pair */
    for(unsigned kind=1;kind<=2;kind++) {
        memset(data+16,0,sizeof(data)-16);header(data+64,kind,32);memset(data+96,0x53,32);
        assert(kui_smash_ce_helper_find(data,sizeof(data),kind,&out)==1 && out.raw==data+96 &&
            out.raw_bytes==32 && out.stored_bytes==32 && out.padded_bytes==49152);
        assert(kui_smash_ce_helper_find(data,sizeof(data),3-kind,&out)==-1 && !out.raw);
        const unsigned fields[]={16,20,24,28};
        for(unsigned i=0;i<4;i++) {
            uint8_t old=data[64+fields[i]];data[64+fields[i]]^=1;
            assert(kui_smash_ce_helper_find(data,sizeof(data),kind,&out)==-1 && !out.raw);
            data[64+fields[i]]=old;
        }
        assert(kui_smash_ce_helper_find(data,64+31,kind,&out)==-1);
        assert(kui_smash_ce_helper_find(data,64+32+31,kind,&out)==-1);
        header(data+192,kind,32);
        assert(kui_smash_ce_helper_find(data,sizeof(data),kind,&out)==-1);
        memset(data+192,0,64);memmove(data+65,data+64,64);data[64]=0;
        assert(kui_smash_ce_helper_find(data,sizeof(data),kind,&out)==-1);
    }
    assert(kui_smash_ce_helper_find(NULL,0,1,&out)==-1 && !out.raw);
    assert(kui_smash_ce_helper_find(data,sizeof(data),0,&out)==-1);
    assert(kui_smash_ce_helper_find(data,sizeof(data),1,(void *)data)==-1);
}
static void transport(void) {
    enum {AT=64,STORED=49152,TOTAL=AT+32+STORED};
    uint8_t *data=calloc(1,TOTAL);assert(data);struct kui_smash_ce_helper out;
    for(unsigned kind=1u;kind<=2u;kind++) {
        memset(data,0,TOTAL);header(data+AT,kind,41152u);put(data+AT+8u,2u);put(data+AT+28u,STORED);
        uint8_t *image=data+AT+32u;memset(image,0x53,41152u);
        struct kui_smash_ce_transport tail={KUI_SMASH_CE_TRANSPORT_MAGIC0,KUI_SMASH_CE_TRANSPORT_MAGIC1,
            1u,32u,41152u,640u,0u,632u};
        memcpy(image+41152u,&tail,sizeof(tail));
        assert(kui_smash_ce_helper_find(data,TOTAL,kind,&out)==1 && out.raw==image &&
            out.raw_bytes==41152u && out.stored_bytes==49152u && out.padded_bytes==49152u);
        assert(kui_smash_ce_transport_static(out.raw,out.stored_bytes,NULL));
        assert(kui_smash_ce_helper_find(data,TOTAL-1u,kind,&out)==-1);
        put(data+AT+20u,49152u);assert(kui_smash_ce_helper_find(data,TOTAL,kind,&out)==-1);
        put(data+AT+20u,41152u);
        put(data+AT+8u,1u);assert(kui_smash_ce_helper_find(data,TOTAL,kind,&out)==-1);put(data+AT+8u,2u);
        put(data+AT+28u,0u);assert(kui_smash_ce_helper_find(data,TOTAL,kind,&out)==-1);put(data+AT+28u,STORED);
        for(unsigned failure=0u;failure<7u;failure++) {
            struct kui_smash_ce_transport bad=tail;
            if(failure==0u)bad.magic1^=1u;
            if(failure==1u)bad.code_bytes=7972u;
            if(failure==2u)bad.runtime_bytes=628u;
            if(failure==3u)bad.runtime_offset=1u;
            if(failure==4u)bad.runtime_offset=12u;
            memcpy(image+41152u,&bad,sizeof(bad));
            if(failure==5u)image[41184u]=1u;
            if(failure==6u)image[41184u+640u]=1u;
            assert(kui_smash_ce_helper_find(data,TOTAL,kind,&out)==-1 && !out.raw);
            image[41184u]=image[41184u+640u]=0u;
        }
    }
    free(data);
}
static void actual(const char *path,unsigned kind) {
    FILE *f=fopen(path,"rb");assert(f && !fseek(f,0,SEEK_END));long bytes=ftell(f);assert(bytes>64);
    rewind(f);uint8_t *file=malloc((size_t)bytes);assert(file && fread(file,1,(size_t)bytes,f)==(size_t)bytes && !fclose(f));
    struct kui_runtime_info info;assert(kui_runtime_header(file,(uint64_t)bytes,&info)==KUI_RUNTIME_OK);
    uint8_t *body=file+64;assert(kui_retail_crc32(0,body,info.payload_bytes)==info.crc32);
    struct kui_smash_ce_profile owner;struct kui_smash_ce_helper helper;
    assert(kui_smash_ce_profile_find(body,info.payload_bytes,kind,&owner)==1 &&
        owner.version==KUI_SMASH_CE_PROFILE_EMBEDDED_VERSION);
    assert(kui_smash_ce_helper_find(body,info.payload_bytes,kind,&helper)==1);
    assert(helper.stored_bytes>=helper.raw_bytes && helper.stored_bytes<=helper.padded_bytes);
    if(helper.stored_bytes!=helper.raw_bytes) {
        assert(helper.raw_bytes==41152u && helper.stored_bytes==49152u &&
            kui_smash_ce_transport_static(helper.raw,helper.stored_bytes,NULL));
        const uint32_t cap[]={UINT32_C(0x5349554b),UINT32_C(0x31524543),1u,16u};
        assert(info.payload_bytes>=0x1f0u && !memcmp(body+0x1e0u,cap,sizeof(cap)));
        unsigned count=0u;
        for(size_t i=0u;i+sizeof(cap)<=info.payload_bytes;i++)
            if(!memcmp(body+i,cap,sizeof(cap))) {assert(!(i&3u));++count;}
        assert(count==1u);
    }
    uint8_t *prepared=calloc(1,helper.padded_bytes);assert(prepared);memcpy(prepared,helper.raw,helper.stored_bytes);
    assert(kui_retail_crc32(0,prepared,helper.padded_bytes)==owner.helper_crc32);
    size_t at=(size_t)(helper.raw-body)-32;body[at+20]^=1;
    assert(kui_smash_ce_helper_find(body,info.payload_bytes,kind,&helper)==-1);
    free(prepared);free(file);
}
int main(int argc,char **argv) {
    assert(argc==1 || argc==3);core();transport();
    if(argc==3) {actual(argv[1],1);actual(argv[2],2);}
    puts("PASS embedded CE helper: strict legacy records, typed bounded static CE transport, malformed/duplicate/refusal cases and actual padded owner CRC");return 0;
}
