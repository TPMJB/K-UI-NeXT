/* SPDX-License-Identifier: GPL-3.0-only */
/* Original owner bytes are supplied externally; none are embedded here. */
#include "kui/native_sdk_sci_route.h"
#include "kui/native_sdk_sci_family.h"
#include "kui/hash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOAD KUI_NATIVE_SDK_SCI_ROUTE_LOAD
#define CALLBACK UINT32_C(0x8c007300)
#define FIXTURE_BYTES 0x4000u
#define VECTOR_AT 0x400u
#define HELPER_AT 0x1000u
#define INIT_AT 0x1802u
#define CHECK(x) do {++checks;if(!(x)) {fprintf(stderr,"SDK SCI structural check failed at %u\n",__LINE__);exit(1);}} while(0)
static unsigned checks,reads,fail_read;
static uint8_t *image,*live,vector[96];
static size_t bytes,vector_at,default_at;
static uint32_t vbr,eri,rxi,original_default,owner_area;
static unsigned normal_bytes=0x168u,fpu_bytes=0x19cu,fpu_delta=0x1f4u,event_delta=0x398u,default_bytes=0xf8u;
static uint8_t init_template[0x2a];
static uint32_t word(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void put(uint8_t *p,uint32_t value) {
    for(unsigned i=0u;i<4u;i++)p[i]=(uint8_t)(value>>(8u*i));
}
static bool read_word(void *context,uint32_t address,uint32_t *out) {
    CHECK(context==live && out && !(address&3u));
    ++reads;if(fail_read && reads==fail_read)return false;
    if(address>=vbr+0x600u && address<vbr+0x660u) {
        *out=word(vector+address-(vbr+0x600u));return true;
    }
    if(address==vbr+0x25cu) {*out=eri;return true;}
    if(address==vbr+0x260u) {*out=rxi;return true;}
    if(address<LOAD || address-LOAD>bytes || bytes-(address-LOAD)<4u)return false;
    *out=word(live+address-LOAD);return true;
}
/* Every admission also runs the image-only family finder: it must agree with
 * each image refusal and name the same roles whenever the live route admits.
 * Live-only refusals legitimately leave the image family found. */
static int discover(struct kui_native_sdk_sci_route_plan *out) {
    int result=kui_native_sdk_sci_route_discover(image,bytes,LOAD,vbr,
        UINT32_C(0x600000f0),CALLBACK,read_word,live,out);
    if(!out || (const void *)out==(const void *)image)return result;
    struct kui_native_sdk_sci_family family;unsigned before=reads;
    int found=kui_native_sdk_sci_family_find(image,bytes,LOAD,&family);
    CHECK(reads==before);
    if(result==1) {
        uint32_t vector=LOAD+(uint32_t)vector_at;
        CHECK(found==1 && family.vector==vector && family.original==out->previous &&
            family.normal==(((vector+0x8cu)&UINT32_C(0x1fffffff))|owner_area) &&
            family.fpu==(((vector+fpu_delta)&UINT32_C(0x1fffffff))|owner_area) &&
            (out->common==family.normal || out->common==family.fpu));
    } else if(found!=1) {
        CHECK(found==result);
        const uint8_t *p=(const void *)&family;
        for(size_t i=0u;i<sizeof(family);i++)CHECK(!p[i]);
    }
    return result;
}
static void zero(const struct kui_native_sdk_sci_route_plan *out) {
    const uint8_t *p=(const void *)out;
    for(size_t i=0u;i<sizeof(*out);i++)CHECK(!p[i]);
}
static void seed(bool fpu) {
    memcpy(live,image,bytes);memcpy(vector,image+vector_at,sizeof(vector));
    put(vector+0x58u,((LOAD+(uint32_t)vector_at+(fpu?fpu_delta:0x8cu))&UINT32_C(0x1fffffff))|owner_area);
    put(vector+0x50u,UINT32_C(0xfedcba98));
    eri=rxi=original_default;reads=fail_read=0u;
}
static void good(bool fpu) {
    seed(fpu);struct kui_native_sdk_sci_route_plan out;
    CHECK(discover(&out)==1);
    CHECK(out.version==1u && out.bytes==sizeof(out) && out.vbr==vbr &&
        out.common==(((LOAD+vector_at+(fpu?fpu_delta:0x8cu))&UINT32_C(0x1fffffff))|owner_area) &&
        out.eri_slot==vbr+0x25cu && out.rxi_slot==vbr+0x260u &&
        out.callback==CALLBACK && out.previous==original_default);
    CHECK(!memcmp(image,live,bytes));
    CHECK(word(vector+0x50u)==UINT32_C(0xfedcba98));
}
static uint8_t *file_bytes(const char *path,size_t *length) {
    FILE *input=fopen(path,"rb");CHECK(input);
    CHECK(!fseek(input,0,SEEK_END));long size=ftell(input);CHECK(size>0);
    CHECK(!fseek(input,0,SEEK_SET));uint8_t *p=malloc((size_t)size);CHECK(p);
    CHECK(fread(p,1u,(size_t)size,input)==(size_t)size);CHECK(!fclose(input));
    *length=(size_t)size;return p;
}
static void fixture(const uint8_t *source,size_t n) {
    uint32_t original_vector,original_init,source_default;
    if(n==1578116u) {original_vector=0x8c15ab44u;original_init=0x8c135492u;source_default=0x8c1351e4u;
        normal_bytes=0x138u;fpu_bytes=0x16cu;fpu_delta=0x1c4u;event_delta=0x338u;default_bytes=0xfcu;}
    else if(n==2143652u) {original_vector=UINT32_C(0x8c1da080);original_init=UINT32_C(0x8c1b0e56);source_default=original_vector-0x130u;}
    else {CHECK(n==4057056u);original_vector=UINT32_C(0x8c1ade3c);original_init=UINT32_C(0x8c17e7c2);source_default=original_vector-0x130u;}
    uint8_t digest[32];struct kui_sha256 h;kui_sha256_init(&h);kui_sha256_update(&h,source,n);kui_sha256_digest(&h,digest);
    static const uint8_t expected[3][32]={
        {0xff,0xb5,0xa8,0xe5,0xff,0x55,0x47,0x02,0x6d,0x93,0x64,0x9e,0x1f,0x90,0x58,0xb7,
         0xee,0x5e,0x29,0xd8,0xd0,0xff,0x8a,0x89,0x33,0x57,0x01,0xf7,0xe9,0x64,0x7f,0x35},
        {0x16,0xb4,0x5f,0xcc,0x84,0x99,0x2a,0x60,0xd5,0xa6,0x4b,0xf6,0x4b,0xf2,0x2b,0x69,
         0x9b,0x27,0x30,0x6b,0x95,0xf1,0x77,0xba,0x3d,0x97,0x30,0xca,0x38,0x00,0x69,0xae},
        {0x69,0x19,0xed,0x8b,0xc9,0x67,0x82,0xd0,0x41,0xbe,0x41,0xba,0x86,0xf9,0x0b,0xf8,
         0x6d,0x99,0x63,0x2b,0x8b,0x77,0x87,0x39,0x19,0x69,0x72,0x1b,0xc0,0x99,0x17,0x36}};
    CHECK(!memcmp(digest,expected[n==2143652u?0u:n==1578116u?2u:1u],32u));
    bytes=FIXTURE_BYTES;vector_at=VECTOR_AT;vbr=UINT32_C(0x8c00e000);
    default_at=VECTOR_AT-0x130u;original_default=LOAD+(uint32_t)default_at;
    owner_area=UINT32_C(0x80000000);
    image=calloc(1u,bytes);live=malloc(bytes);CHECK(image && live);
    memcpy(image+VECTOR_AT,source+original_vector-LOAD,event_delta+8u);
    memcpy(image+VECTOR_AT-0x130u,source+source_default-LOAD,default_bytes);
    uint32_t helper=word(source+source_default-LOAD+0xf4u);
    memcpy(image+HELPER_AT,source+helper-LOAD,0x238u);
    memcpy(image+INIT_AT,source+original_init-LOAD,0x2eu);
    memcpy(init_template,source+original_init-LOAD,sizeof(init_template));
    uint32_t vector_address=LOAD+VECTOR_AT,event=vector_address+event_delta;
    put(image+VECTOR_AT+0x4cu,event);put(image+VECTOR_AT+0x58u,vector_address+0x8cu);
    put(image+VECTOR_AT+0x8cu+normal_bytes-4u,event);put(image+VECTOR_AT+fpu_delta+fpu_bytes-4u,event);
    put(image+VECTOR_AT-0x130u+0xe8u,event+4u);
    put(image+VECTOR_AT-0x130u+0xecu,event-8u);put(image+VECTOR_AT-0x130u+0xf0u,event-4u);
    put(image+VECTOR_AT-0x130u+0xf4u,LOAD+HELPER_AT);
    put(image+INIT_AT+0x2au,vector_address-0x130u);
    if(default_bytes==0xfcu)put(image+default_at+0xf8u,event);
}
static void alias(uint32_t area) {
    const size_t roles[]={VECTOR_AT+0x4cu,VECTOR_AT+0x58u,
        VECTOR_AT+0x8cu+normal_bytes-4u,VECTOR_AT+fpu_delta+fpu_bytes-4u,
        default_at+0xe8u,default_at+0xecu,default_at+0xf0u,default_at+0xf4u,INIT_AT+0x2au,default_at+0xf8u};
    for(unsigned i=0u;i<(default_bytes==0xfcu?10u:9u);i++)
        put(image+roles[i],(word(image+roles[i])&UINT32_C(0x1fffffff))|area);
    owner_area=area;original_default=(original_default&UINT32_C(0x1fffffff))|area;
}
static void alias_and_link_order_checks(void) {
    struct kui_native_sdk_sci_route_plan out;
    /* The initializer owns the default independently of source link order. */
    memcpy(image+0x2400u,image+default_at,default_bytes);size_t old_default=default_at;
    default_at=0x2400u;original_default=LOAD+(uint32_t)default_at;
    put(image+INIT_AT+0x2au,original_default);good(false);good(true);
    image[old_default+0x42u]^=1u;good(true);image[old_default+0x42u]^=1u;
    /* U0 source pointers remain raw in the SDK plan and callback slots;
     * the live reader receives only canonical P1 code addresses. */
    alias(0u);good(false);good(true);
    const size_t roles[]={VECTOR_AT+0x4cu,VECTOR_AT+0x58u,
        VECTOR_AT+0x8cu+normal_bytes-4u,VECTOR_AT+fpu_delta+fpu_bytes-4u,
        default_at+0xe8u,default_at+0xecu,default_at+0xf0u,default_at+0xf4u,INIT_AT+0x2au,default_at+0xf8u};
    for(unsigned i=0u;i<(default_bytes==0xfcu?10u:9u);i++) {
        uint32_t saved=word(image+roles[i]);put(image+roles[i],saved|UINT32_C(0x80000000));
        seed(true);CHECK(discover(&out)==-1);zero(&out);CHECK(!reads);
        put(image+roles[i],saved);
    }
    seed(true);put(vector+0x58u,LOAD+VECTOR_AT+fpu_delta);
    CHECK(discover(&out)==-1);zero(&out);
    seed(true);eri=rxi=original_default|UINT32_C(0x80000000);
    CHECK(discover(&out)==-1);zero(&out);
    alias(UINT32_C(0xa0000000));seed(true);CHECK(discover(&out)==-1);zero(&out);CHECK(!reads);
    alias(UINT32_C(0x40000000));seed(true);CHECK(discover(&out)==-1);zero(&out);CHECK(!reads);
    alias(0u);
    const uint32_t bad_default[]={UINT32_C(0x0c000ffc),UINT32_C(0x0c014000),
        UINT32_C(0x0c012401),UINT32_C(0xac012400),UINT32_C(0x8d000000),UINT32_MAX};
    for(unsigned i=0u;i<sizeof(bad_default)/sizeof(bad_default[0]);i++) {
        put(image+INIT_AT+0x2au,bad_default[i]);seed(true);
        CHECK(discover(&out)==-1);zero(&out);CHECK(!reads);
    }
    put(image+INIT_AT+0x2au,original_default);good(true);
    alias(UINT32_C(0x80000000));good(true);
}
static void nop_padding_checks(void) {
    uint8_t *small_image=image,*small_live=live;size_t small_bytes=bytes;
    bytes=KUI_NATIVE_SDK_SCI_ROUTE_MAX_IMAGE_BYTES;
    image=malloc(bytes);live=malloc(bytes);CHECK(image && live);
    for(size_t at=0u;at<bytes;at+=4u)put(image+at,UINT32_C(0x00090009));
    memcpy(image,small_image,small_bytes);good(false);good(true);
    /* A full copied prefix in the padding remains an ambiguous SDK owner;
     * the cheap third-word filter cannot select the first valid body. */
    memcpy(image+bytes-64u,image+VECTOR_AT,64u);seed(true);
    struct kui_native_sdk_sci_route_plan out;
    CHECK(discover(&out)==-1 && !reads);zero(&out);
    CHECK(kui_native_sdk_sci_route_discover(image,bytes+1u,LOAD,vbr,0x600000f0u,
        CALLBACK,read_word,live,&out)==-1);zero(&out);
    free(live);free(image);image=small_image;live=small_live;bytes=small_bytes;
}
static void cross_family_checks(const char *path) {
    size_t n;uint8_t *source=file_bytes(path,&n);
    bool other_compact=n==1578116u;
    if((default_bytes==0xfcu)==other_compact ||
       (n!=1578116u && n!=2143652u && n!=4057056u)) {free(source);return;}
    uint32_t other_vector=other_compact?0x8c15ab44u:n==2143652u?0x8c1da080u:0x8c1ade3cu;
    uint32_t other_default=other_compact?0x8c1351e4u:other_vector-0x130u;
    const unsigned lengths[]={other_compact?0x138u:0x168u,other_compact?0x16cu:0x19cu,
        other_compact?0xfcu:0xf8u};
    const size_t targets[]={VECTOR_AT+0x8cu,VECTOR_AT+fpu_delta,default_at};
    const uint32_t sources[]={other_vector+0x8cu,other_vector+(other_compact?0x1c4u:0x1f4u),other_default};
    for(unsigned kind=0u;kind<3u;kind++) {
        uint8_t saved[512];CHECK(sources[kind]>=LOAD && sources[kind]-LOAD+lengths[kind]<=n);
        memcpy(saved,image+targets[kind],sizeof(saved));
        memcpy(image+targets[kind],source+sources[kind]-LOAD,lengths[kind]);
        uint32_t event=LOAD+VECTOR_AT+event_delta;
        if(kind<2u)put(image+targets[kind]+lengths[kind]-4u,event);
        else {
            put(image+default_at+0xe8u,event+4u);put(image+default_at+0xecu,event-8u);
            put(image+default_at+0xf0u,event-4u);put(image+default_at+0xf4u,LOAD+HELPER_AT);
            if(other_compact)put(image+default_at+0xf8u,event);
        }
        seed(true);struct kui_native_sdk_sci_route_plan out;
        CHECK(discover(&out)==-1 && !reads);zero(&out);
        memcpy(image+targets[kind],saved,sizeof(saved));
    }
    free(source);good(false);good(true);
}
static void fixture_checks(void) {
    good(false);good(true);
    /* Dynamic owner VBR is a live readiness proof, not an address guess. */
    const uint32_t valid_vbr[]={UINT32_C(0x8c000000),UINT32_C(0x8c008000),
        UINT32_C(0x8c800000),UINT32_C(0x8cfff900)};
    for(unsigned i=0u;i<sizeof(valid_vbr)/sizeof(valid_vbr[0]);i++) {vbr=valid_vbr[i];good(true);}
    vbr=UINT32_C(0x8c00e000);
    put(image+VECTOR_AT+0x58u,LOAD+VECTOR_AT+fpu_delta);good(false);good(true);
    put(image+VECTOR_AT+0x58u,LOAD+VECTOR_AT+0x8cu);
    const size_t windows[][2]={{VECTOR_AT,96u},{VECTOR_AT+0x8cu,normal_bytes},
        {VECTOR_AT+fpu_delta,fpu_bytes},{VECTOR_AT-0x130u,default_bytes},{INIT_AT,0x2eu},
        {HELPER_AT,0x34u},{HELPER_AT+0x120u,12u},{HELPER_AT+0x224u,0x14u}};
    struct kui_native_sdk_sci_route_plan out;
    for(unsigned w=0u;w<sizeof(windows)/sizeof(windows[0]);w++)
        for(size_t at=0u;at<windows[w][1];at++)for(unsigned bit=0u;bit<8u;bit++) {
            size_t offset=windows[w][0]+at;image[offset]^=(uint8_t)(1u<<bit);
            seed(true);memset(&out,0xa5,sizeof(out));CHECK(discover(&out)<=0);zero(&out);
            image[offset]^=(uint8_t)(1u<<bit);
        }
    /* Each source instruction, literal role and callback initializer matters;
     * mutable state and unrelated debug output data do not define this ABI. */
    image[HELPER_AT+0x180u]^=1u;good(true);image[HELPER_AT+0x180u]^=1u;
    image[VECTOR_AT+event_delta]=0xa5u;good(true);image[VECTOR_AT+event_delta]=0u;
    for(unsigned mode=0u;mode<2u;mode++) {
        for(size_t at=0u;at<sizeof(vector);at++) {
            seed(mode!=0u);vector[at]^=1u;int result=discover(&out);
            if(at>=0x50u && at<0x54u)CHECK(result==1);
            else {CHECK(result==-1);zero(&out);}
        }
        for(unsigned w=1u;w<sizeof(windows)/sizeof(windows[0]);w++) {
            if(w==4u || (mode==0u && w==2u) || (mode!=0u && w==1u))continue;
            for(size_t at=0u;at<windows[w][1];at++) {
                seed(mode!=0u);live[windows[w][0]+at]^=1u;CHECK(discover(&out)==-1);zero(&out);
            }
        }
        seed(mode!=0u);CHECK(discover(&out)==1);unsigned total=reads;
        for(unsigned fail=1u;fail<=total;fail++) {
            seed(mode!=0u);fail_read=fail;CHECK(discover(&out)==-1);zero(&out);CHECK(reads==fail);
        }
    }
    for(unsigned slot=0u;slot<2u;slot++)for(unsigned bit=0u;bit<32u;bit++) {
        seed(true);if(slot)rxi^=UINT32_C(1)<<bit;else eri^=UINT32_C(1)<<bit;
        CHECK(discover(&out)==-1);zero(&out);
    }
    seed(true);eri=rxi=CALLBACK;CHECK(discover(&out)==-1);zero(&out);
    /* Duplicate partial vectors and complete initializers both refuse. */
    memcpy(image+0x3000u,image+VECTOR_AT,64u);seed(true);
    CHECK(discover(&out)==-1 && !reads);zero(&out);memset(image+0x3000u,0,64u);
    memcpy(image+0x1a02u,image+INIT_AT,0x2eu);seed(true);
    CHECK(discover(&out)==-1 && !reads);zero(&out);memset(image+0x1a02u,0,0x2eu);
    put(image+INIT_AT+0x2au,LOAD+VECTOR_AT-0x12cu);seed(true);
    CHECK(discover(&out)==-1 && !reads);zero(&out);put(image+INIT_AT+0x2au,LOAD+VECTOR_AT-0x130u);
    /* Correct initializer bytes at the wrong PC alignment are not the same
     * decoded default-literal relationship. */
    memcpy(image+0x1a00u,image+INIT_AT,0x2eu);memset(image+INIT_AT,0,0x2eu);seed(true);
    CHECK(discover(&out)==-1 && !reads);zero(&out);
    memcpy(image+INIT_AT,image+0x1a00u,0x2eu);memset(image+0x1a00u,0,0x2eu);
    seed(true);
    const uint32_t invalid_vbr[]={0u,UINT32_C(0x0c00e000),UINT32_C(0xac00e000),
        UINT32_C(0x8c00e004),UINT32_C(0x8c00e002),UINT32_C(0x8cfffA00),UINT32_MAX};
    for(unsigned i=0u;i<sizeof(invalid_vbr)/sizeof(invalid_vbr[0]);i++) {
        CHECK(kui_native_sdk_sci_route_discover(image,bytes,LOAD,invalid_vbr[i],0x600000f0u,
            CALLBACK,read_word,live,&out)==-1);zero(&out);
    }
    for(unsigned mask=0u;mask<15u;mask++) {
        CHECK(kui_native_sdk_sci_route_discover(image,bytes,LOAD,vbr,0x60000000u|(mask<<4),
            CALLBACK,read_word,live,&out)==-1);zero(&out);
    }
    const uint32_t bad_callback[]={0u,CALLBACK+1u,UINT32_C(0x0c007300),UINT32_C(0xac007300),
        UINT32_C(0x8c003ffe),UINT32_C(0x8c007ba0),UINT32_MAX};
    for(unsigned i=0u;i<sizeof(bad_callback)/sizeof(bad_callback[0]);i++) {
        CHECK(kui_native_sdk_sci_route_discover(image,bytes,LOAD,vbr,0x600000f0u,
            bad_callback[i],read_word,live,&out)==-1);zero(&out);
    }
    CHECK(kui_native_sdk_sci_route_discover(NULL,bytes,LOAD,vbr,0x600000f0u,CALLBACK,read_word,live,&out)==-1);zero(&out);
    CHECK(kui_native_sdk_sci_route_discover(image,bytes,LOAD,vbr,0x600000f0u,CALLBACK,NULL,live,&out)==-1);zero(&out);
    CHECK(kui_native_sdk_sci_route_discover(image,bytes,LOAD+4u,vbr,0x600000f0u,CALLBACK,read_word,live,&out)==-1);zero(&out);
    CHECK(kui_native_sdk_sci_route_discover(image,0u,LOAD,vbr,0x600000f0u,CALLBACK,read_word,live,&out)==-1);zero(&out);
    CHECK(kui_native_sdk_sci_route_discover(image,KUI_NATIVE_SDK_SCI_ROUTE_MAX_IMAGE_BYTES+1u,LOAD,vbr,0x600000f0u,CALLBACK,read_word,live,&out)==-1);zero(&out);
    CHECK(discover(NULL)==-1);
    uint8_t before[32];memcpy(before,image,32u);CHECK(discover((void *)image)==-1);CHECK(!memcmp(before,image,32u));
    good(true);
    uint8_t *saved=image;image=calloc(1u,bytes);CHECK(image);seed(true);
    CHECK(discover(&out)==0 && !reads);zero(&out);free(image);image=saved;
    alias_and_link_order_checks();
}
static void probe(const char *path) {
    size_t n;uint8_t *p=file_bytes(path,&n);image=p;bytes=n;live=malloc(n);CHECK(live);
    vector_at=0u;unsigned found=0u;
    static const uint8_t expected[32]={
        0x48,0x3c,0x13,0x82,0x8a,0x7a,0xc7,0x11,0xda,0xf2,0xd9,0x70,0xfc,0x60,0x92,0xda,
        0x1c,0x35,0x82,0x65,0x36,0x14,0x43,0x07,0x23,0xef,0x83,0xe6,0xa0,0x2c,0xd6,0x96};
    for(size_t at=0u;at<n && n-at>=64u;at+=4u) {
        if(word(p+at)!=0x00090009u || word(p+at+4u)!=0x00090009u)continue;
        uint8_t digest[32];struct kui_sha256 h;kui_sha256_init(&h);kui_sha256_update(&h,p+at,64u);kui_sha256_digest(&h,digest);
        if(!memcmp(digest,expected,32u)) {vector_at=at;found++;}
    }
    original_default=0u;owner_area=UINT32_C(0x80000000);
    for(size_t at=0u;at<n && n-at>=0x2eu;at+=2u)
        if(!memcmp(p+at,init_template,sizeof(init_template)))original_default=word(p+at+0x2au);
    owner_area=original_default&UINT32_C(0xe0000000);
    if(found==1u && n-vector_at>=96u)fpu_delta=((word(p+vector_at+0x4cu)&0x1fffffffu)==((LOAD+(uint32_t)vector_at)&0x1fffffffu)+0x338u)?0x1c4u:0x1f4u;
    vbr=UINT32_C(0x8c00f400);struct kui_native_sdk_sci_route_plan out;
    if(found==1u && n-vector_at>=0x3a0u)seed(true);
    else {memcpy(live,image,bytes);memset(vector,0,sizeof(vector));reads=fail_read=0u;}
    int result=discover(&out);
    if(result==1) {
        CHECK(found==1u);CHECK(out.previous==original_default);
        printf("structural source/live-model admission: %s vector=%08x default=%08x\n",path,LOAD+(unsigned)vector_at,out.previous);
    } else {zero(&out);printf("structural source family refused: %s result=%d\n",path,result);}
    free(live);free(image);image=live=NULL;
}
int main(int argc,char **argv) {
    CHECK(argc>=2);size_t n;uint8_t *source=file_bytes(argv[1],&n);fixture(source,n);free(source);
    bool quick=argc>=3 && !strcmp(argv[2],"--quick");
    if(quick) {good(false);good(true);alias_and_link_order_checks();}
    else fixture_checks();
    if(!quick && argc>=3)cross_family_checks(argv[2]);
    nop_padding_checks();free(live);free(image);
    for(int i=1;i<argc;i++)if(!(quick && i==2))probe(argv[i]);
    printf("native SDK SCI structural discovery: %u checks passed; relocated role graph, dynamic VBR, immutable/live windows, unique initializer, scratch, ownership and read faults\n",checks);
    return 0;
}
