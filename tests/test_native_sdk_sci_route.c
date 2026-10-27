/* SPDX-License-Identifier: GPL-3.0-only */
/* Supply unchanged Evolution1 and/or Evolution2 startups; no owner code is embedded. */
#include "kui/native_sdk_sci_route.h"
#include "kui/hash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks,reads,fail_read;
static uint8_t *image,*live;
static uint8_t vector[96],digest[32];
static uint32_t eri,rxi,bytes,default_callback,source_vector,normal_common,fpu_common,table_init;
#define CALLBACK UINT32_C(0x8c007300)
#define LOAD KUI_NATIVE_SDK_SCI_ROUTE_LOAD
#define BYTES bytes
#define VBR KUI_NATIVE_SDK_SCI_ROUTE_VBR
#define DEFAULT default_callback
#define CHECK(x) do {++checks;if(!(x)) {fprintf(stderr,"SDK SCI route check failed at %u\n",__LINE__);exit(1);}} while(0)
static uint32_t word(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void put(uint8_t *p,uint32_t value) {
    for(unsigned i=0u;i<4u;i++)p[i]=(uint8_t)(value>>(8u*i));
}
static bool read_word(void *context,uint32_t address,uint32_t *out) {
    CHECK(context==live && out && !(address&3u));
    ++reads;if(fail_read && reads==fail_read)return false;
    if(address>=VBR+0x600u && address<VBR+0x660u) {
        *out=word(vector+address-(VBR+0x600u));return true;
    }
    if(address==KUI_NATIVE_SDK_SCI_ROUTE_ERI) {*out=eri;return true;}
    if(address==KUI_NATIVE_SDK_SCI_ROUTE_RXI) {*out=rxi;return true;}
    CHECK(address>=LOAD && address<=LOAD+BYTES-4u);
    *out=word(live+address-LOAD);return true;
}
static int admit(struct kui_native_sdk_sci_route_plan *out) {
    return kui_native_sdk_sci_route_admit(image,BYTES,LOAD,digest,VBR,
        UINT32_C(0x600000f0),CALLBACK,read_word,live,out);
}
static void zero(const struct kui_native_sdk_sci_route_plan *out) {
    const uint8_t *p=(const void *)out;
    for(size_t i=0;i<sizeof(*out);i++)CHECK(!p[i]);
}
static void seed(uint32_t common) {
    memcpy(vector,image+(source_vector-LOAD),sizeof(vector));
    put(vector+0x58u,common);put(vector+0x50u,UINT32_C(0xfedcba98));
    eri=rxi=DEFAULT;reads=fail_read=0u;
}
static void good(uint32_t common) {
    seed(common);struct kui_native_sdk_sci_route_plan out;
    CHECK(admit(&out)==1);
    CHECK(out.version==1u && out.bytes==sizeof(out) && out.vbr==VBR && out.common==common);
    CHECK(out.eri_slot==KUI_NATIVE_SDK_SCI_ROUTE_ERI && out.rxi_slot==KUI_NATIVE_SDK_SCI_ROUTE_RXI);
    CHECK(out.callback==CALLBACK && out.previous==DEFAULT);
    CHECK(eri==DEFAULT && rxi==DEFAULT && word(vector+0x50u)==UINT32_C(0xfedcba98));
}
static void run_owner(const char *path) {
    unsigned begin=checks;
    FILE *input=fopen(path,"rb");CHECK(input);
    CHECK(!fseek(input,0,SEEK_END));long raw_bytes=ftell(input);
    CHECK(raw_bytes==4057056 || raw_bytes==KUI_NATIVE_SDK_SCI_EVO1_IMAGE_BYTES);
    bool evolution1=raw_bytes==KUI_NATIVE_SDK_SCI_EVO1_IMAGE_BYTES;
    bytes=evolution1?KUI_NATIVE_SDK_SCI_EVO1_IMAGE_BYTES:KUI_NATIVE_SDK_SCI_ROUTE_IMAGE_BYTES;
    default_callback=evolution1?KUI_NATIVE_SDK_SCI_EVO1_DEFAULT:KUI_NATIVE_SDK_SCI_ROUTE_DEFAULT;
    source_vector=evolution1?KUI_NATIVE_SDK_SCI_EVO1_VECTOR:UINT32_C(0x8c1ade3c);
    normal_common=evolution1?KUI_NATIVE_SDK_SCI_EVO1_NORMAL:UINT32_C(0x8c1adec8);
    fpu_common=evolution1?KUI_NATIVE_SDK_SCI_EVO1_FPU:UINT32_C(0x8c1ae030);
    table_init=evolution1?KUI_NATIVE_SDK_SCI_EVO1_INIT:UINT32_C(0x8c17e7c2);
    rewind(input);image=malloc(BYTES);live=malloc(BYTES);CHECK(image && live);
    CHECK(fread(image,1,BYTES,input)==BYTES);CHECK(!fclose(input));memcpy(live,image,BYTES);
    struct kui_sha256 hash;kui_sha256_init(&hash);kui_sha256_update(&hash,image,BYTES);kui_sha256_digest(&hash,digest);
    good(fpu_common);good(normal_common);
    /* The original SDK can update its source-template selected common word. */
    size_t common_at=source_vector+0x58u-LOAD;
    uint32_t original_common=word(image+common_at);
    put(image+common_at,fpu_common);good(fpu_common);
    put(image+common_at,original_common);

    struct kui_native_sdk_sci_route_plan out;
    /* Every immutable source-vector and code fingerprint byte is required,
     * even with an otherwise accepted previously verified owner digest. */
    const uint32_t windows[][2]={{source_vector,96u},{fpu_common,0x19cu},
        {normal_common,0x168u},{DEFAULT,0xf8u},{table_init,0x2eu}};
    for(unsigned w=0u;w<sizeof(windows)/sizeof(windows[0]);w++)
        for(unsigned at=0u;at<windows[w][1];at++)for(unsigned bit=0u;bit<8u;bit++) {
            size_t offset=windows[w][0]-LOAD+at;image[offset]^=(uint8_t)(1u<<bit);
            seed(fpu_common);memset(&out,0xa5,sizeof(out));CHECK(admit(&out)==-1);zero(&out);
            image[offset]^=(uint8_t)(1u<<bit);
        }
    /* A copied vector has just one scratch word; every other bit binds its
     * original code or its exact selected common entry. */
    for(unsigned at=0u;at<sizeof(vector);at++)for(unsigned bit=0u;bit<8u;bit++) {
        seed(fpu_common);vector[at]^=(uint8_t)(1u<<bit);
        memset(&out,0xa5,sizeof(out));int result=admit(&out);
        if(at>=0x50u && at<0x54u)CHECK(result==1);
        else {CHECK(result==-1);zero(&out);}
    }
    /* Code sampled through the live RAM reader must match the immutable
     * input; changing only the separate live view cannot borrow its proof. */
    for(unsigned mode=0u;mode<2u;mode++) {
        uint32_t common=mode?normal_common:fpu_common;
        unsigned bytes=mode?0x168u:0x19cu;
        for(unsigned at=0u;at<bytes;at++) {
            seed(common);live[common-LOAD+at]^=1u;CHECK(admit(&out)==-1);zero(&out);
            live[common-LOAD+at]^=1u;
        }
        for(unsigned at=0u;at<0xf8u;at++) {
            seed(common);live[DEFAULT-LOAD+at]^=1u;CHECK(admit(&out)==-1);zero(&out);
            live[DEFAULT-LOAD+at]^=1u;
        }
        seed(common);CHECK(admit(&out)==1);unsigned total=reads;
        for(unsigned fail=1u;fail<=total;fail++) {
            seed(common);fail_read=fail;CHECK(admit(&out)==-1);zero(&out);CHECK(reads==fail);
        }
    }
    for(unsigned slot=0u;slot<2u;slot++)for(unsigned bit=0u;bit<32u;bit++) {
        seed(fpu_common);if(slot)rxi^=UINT32_C(1)<<bit;else eri^=UINT32_C(1)<<bit;
        CHECK(admit(&out)==-1);zero(&out);
    }
    seed(fpu_common);eri=rxi=CALLBACK;CHECK(admit(&out)==-1);zero(&out);
    /* An independently admitted owner's vector, common, callback default
     * or verified identity must not grant this owner's plan. */
    uint32_t foreign_fpu=evolution1?UINT32_C(0x8c1ae030):KUI_NATIVE_SDK_SCI_EVO1_FPU;
    uint32_t foreign_normal=evolution1?UINT32_C(0x8c1adec8):KUI_NATIVE_SDK_SCI_EVO1_NORMAL;
    uint32_t foreign_default=evolution1?KUI_NATIVE_SDK_SCI_ROUTE_DEFAULT:KUI_NATIVE_SDK_SCI_EVO1_DEFAULT;
    for(unsigned mode=0u;mode<2u;++mode) {
        uint32_t foreign_common=mode?foreign_normal:foreign_fpu;
        seed(fpu_common);put(vector+0x58u,foreign_common);
        CHECK(admit(&out)==-1);zero(&out);
        seed(fpu_common);put(image+common_at,foreign_common);
        CHECK(admit(&out)==-1);zero(&out);put(image+common_at,original_common);
    }
    seed(fpu_common);eri=rxi=foreign_default;CHECK(admit(&out)==-1);zero(&out);
    static const uint8_t identity[2][32]={
        {0x52,0xe0,0x0d,0x2b,0x3e,0x9d,0xb0,0x00,0x16,0xb1,0xf6,0xd7,0x82,0xcf,0x8a,0x21,
         0x41,0x61,0x7f,0x3d,0x00,0xbd,0x62,0x82,0x00,0xde,0x75,0x7f,0xd2,0xe1,0x16,0x05},
        {0xff,0xb5,0xa8,0xe5,0xff,0x55,0x47,0x02,0x6d,0x93,0x64,0x9e,0x1f,0x90,0x58,0xb7,
         0xee,0x5e,0x29,0xd8,0xd0,0xff,0x8a,0x89,0x33,0x57,0x01,0xf7,0xe9,0x64,0x7f,0x35}
    };
    seed(fpu_common);uint8_t original_digest[32];memcpy(original_digest,digest,32u);
    memcpy(digest,identity[evolution1?0u:1u],32u);
    CHECK(admit(&out)==-1);zero(&out);CHECK(!reads);
    memcpy(digest,original_digest,32u);
    for(unsigned at=0u;at<32u;at++)for(unsigned bit=0u;bit<8u;bit++) {
        seed(fpu_common);digest[at]^=(uint8_t)(1u<<bit);CHECK(admit(&out)==0);zero(&out);
        CHECK(!reads);digest[at]^=(uint8_t)(1u<<bit);
    }
    seed(fpu_common);
    for(unsigned mask=0u;mask<15u;mask++) {
        CHECK(kui_native_sdk_sci_route_admit(image,BYTES,LOAD,digest,VBR,
            UINT32_C(0x60000000)|(mask<<4),CALLBACK,read_word,live,&out)==-1);zero(&out);
    }
    const uint32_t bad[]={0u,CALLBACK+1u,UINT32_C(0x0c007300),UINT32_C(0xac007300),
        UINT32_C(0x8c003ffe),UINT32_C(0x8c007ba0),UINT32_C(0x8cfd0100),UINT32_MAX};
    for(unsigned i=0u;i<sizeof(bad)/sizeof(bad[0]);i++) {
        CHECK(kui_native_sdk_sci_route_admit(image,BYTES,LOAD,digest,VBR,
            UINT32_C(0x600000f0),bad[i],read_word,live,&out)==-1);zero(&out);
    }
    CHECK(kui_native_sdk_sci_route_admit(image,BYTES,LOAD,digest,VBR,0xf0u,CALLBACK,read_word,live,&out)==-1);
    CHECK(kui_native_sdk_sci_route_admit(image,BYTES,LOAD,digest,VBR+4u,0x600000f0u,CALLBACK,read_word,live,&out)==-1);
    CHECK(kui_native_sdk_sci_route_admit(image,BYTES,LOAD+2u,digest,VBR,0x600000f0u,CALLBACK,read_word,live,&out)==-1);
    CHECK(kui_native_sdk_sci_route_admit(image,BYTES-1u,LOAD,digest,VBR,0x600000f0u,CALLBACK,read_word,live,&out)==-1);
    CHECK(kui_native_sdk_sci_route_admit(NULL,BYTES,LOAD,digest,VBR,0x600000f0u,CALLBACK,read_word,live,&out)==-1);
    CHECK(kui_native_sdk_sci_route_admit(image,BYTES,LOAD,NULL,VBR,0x600000f0u,CALLBACK,read_word,live,&out)==-1);
    CHECK(kui_native_sdk_sci_route_admit(image,BYTES,LOAD,digest,VBR,0x600000f0u,CALLBACK,NULL,live,&out)==-1);
    CHECK(admit(NULL)==-1);
    uint8_t before[32];memcpy(before,image,sizeof(before));
    CHECK(admit((void *)image)==-1);CHECK(!memcmp(before,image,sizeof(before)));
    memcpy(before,digest,sizeof(before));CHECK(admit((void *)digest)==-1);CHECK(!memcmp(before,digest,sizeof(before)));
    CHECK(!memcmp(live,image,BYTES));good(fpu_common);
    printf("native SDK SCI route admission Evolution%u: %u checks; original identity, immutable/live windows, both common modes, scratch, ownership and read faults passed\n",evolution1?1u:2u,checks-begin);
    free(live);free(image);
}
int main(int argc,char **argv) {
    CHECK(argc==2 || argc==3);
    for(int i=1;i<argc;++i)run_owner(argv[i]);
    printf("native SDK SCI route total: %u checks passed\n",checks);
    return 0;
}
