/* SPDX-License-Identifier: GPL-3.0-only */
/* Early Shinobi SDK SCI interrupt family. The original executable is supplied
 * externally (argv[1]); none of its bytes are embedded here. Later arguments
 * are probed with a live model seeded from their own images. */
#include "kui/native_sdk_sci_route.h"
#include "kui/native_sdk_sci_family.h"
#include "kui/hash.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOAD KUI_NATIVE_SDK_SCI_ROUTE_LOAD
#define CALLBACK UINT32_C(0x8c007300)
#define FIXTURE_BYTES 0x4000u
#define E_AT 0x400u
#define I_AT (E_AT+0x34u)
#define C_AT (I_AT+0x34u)
#define EVENT_AT (C_AT+0x2d4u)
#define D_AT (EVENT_AT+0xcu)
#define INIT_AT 0x1802u
#define CHECK(x) do {++checks;if(!(x)) {fprintf(stderr,"early SDK SCI check failed at %u\n",__LINE__);exit(1);}} while(0)
static unsigned checks,reads,fail_read;
static uint8_t *image,*live,stub600[0x34];
static size_t bytes,stub_at,default_at;
static uint32_t vbr,eri,rxi,area;

static uint32_t word(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void put(uint8_t *p,uint32_t value) {for(unsigned i=0u;i<4u;i++)p[i]=(uint8_t)(value>>(8u*i));}
static uint32_t role(uint32_t address) {return (address&UINT32_C(0x1fffffff))|area;}
static bool read_word(void *context,uint32_t address,uint32_t *out) {
    CHECK(context==live && out && !(address&3u));
    ++reads;if(fail_read && reads==fail_read)return false;
    if(address>=vbr+0x600u && address<vbr+0x634u) {*out=word(stub600+address-(vbr+0x600u));return true;}
    if(address==vbr+0x25cu) {*out=eri;return true;}
    if(address==vbr+0x260u) {*out=rxi;return true;}
    if(address<LOAD || address-LOAD>bytes || bytes-(address-LOAD)<4u)return false;
    *out=word(live+address-LOAD);return true;
}
static void seed(void) {
    memcpy(live,image,bytes);memcpy(stub600,image+stub_at,sizeof(stub600));
    put(stub600+0x2cu,UINT32_C(0xfedcba98)); /* the stub's mutable R0 scratch */
    eri=rxi=role(LOAD+(uint32_t)default_at);reads=fail_read=0u;
}
static void zero(const void *p,size_t n) {
    const uint8_t *b=p;for(size_t i=0u;i<n;i++)CHECK(!b[i]);
}
/* Every live admission agrees with the image-only finder. */
static int discover(struct kui_native_sdk_sci_route_plan *out) {
    int result=kui_native_sdk_sci_route_discover(image,bytes,LOAD,vbr,UINT32_C(0x600000f0),
        CALLBACK,read_word,live,out);
    struct kui_native_sdk_sci_family family;unsigned before=reads;
    int found=kui_native_sdk_sci_family_find(image,bytes,LOAD,&family);
    CHECK(reads==before);
    if(result==1) {
        uint32_t common=role(LOAD+(uint32_t)stub_at+0x34u);
        CHECK(found==1 && family.vector==LOAD+(uint32_t)stub_at && family.normal==common &&
              family.fpu==common && family.original==out->previous && out->common==common);
    } else if(found!=1) {CHECK(found==result);zero(&family,sizeof(family));}
    if(result!=1)zero(out,sizeof(*out));
    return result;
}
static void good(void) {
    seed();struct kui_native_sdk_sci_route_plan out;
    CHECK(discover(&out)==1);
    CHECK(out.version==KUI_NATIVE_SDK_SCI_ROUTE_VERSION && out.bytes==sizeof(out) && out.vbr==vbr &&
          out.eri_slot==vbr+0x25cu && out.rxi_slot==vbr+0x260u && out.callback==CALLBACK &&
          out.previous==role(LOAD+(uint32_t)default_at));
    CHECK(!memcmp(image,live,bytes));
}
static uint8_t *file_bytes(const char *path,size_t *length) {
    FILE *input=fopen(path,"rb");CHECK(input);
    CHECK(!fseek(input,0,SEEK_END));long size=ftell(input);CHECK(size>0);
    CHECK(!fseek(input,0,SEEK_SET));uint8_t *p=malloc((size_t)size);CHECK(p);
    CHECK(fread(p,1u,(size_t)size,input)==(size_t)size);CHECK(!fclose(input));
    *length=(size_t)size;return p;
}
/* Locate an original's early roles by its INTEVT idiom, independently of the
 * production finder, then rebuild them in a compact fixture. */
static void fixture(const uint8_t *source,size_t n) {
    size_t idiom=0u;unsigned count=0u;
    for(size_t at=0u;at+8u<=n;at+=2u)
        if(source[at]==0xffu && source[at+1u]==0xe1u && word(source+at+2u)==UINT32_C(0x41184128) &&
           source[at+6u]==0x28u && source[at+7u]==0x71u) {idiom=at;++count;}
    CHECK(count==1u && idiom>=0x46u);
    size_t i=idiom-0x12u,e=i-0x34u,c=i+0x34u;
    uint32_t event=word(source+i+0x28u),source_area=event&UINT32_C(0xe0000000);
    size_t ev=((event&UINT32_C(0x1fffffff))|UINT32_C(0x80000000))-LOAD,d=ev+0xcu;
    size_t init=0u;count=0u;
    for(size_t at=0u;at+0x2eu<=n;at+=2u)
        if(source[at]==0x11u && source[at+1u]==0x95u && source[at+2u]==0x11u && source[at+3u]==0x94u &&
           ((word(source+at+0x2au)&UINT32_C(0x1fffffff))|UINT32_C(0x80000000))-LOAD==d) {init=at;++count;}
    CHECK(count==1u && ev==c+0x2d4u && d+0xc4u<=n);
    bytes=FIXTURE_BYTES;stub_at=I_AT;default_at=D_AT;vbr=UINT32_C(0x8c00e000);area=UINT32_C(0x80000000);
    image=calloc(1u,bytes);live=malloc(bytes);CHECK(image && live);
    memcpy(image+E_AT,source+e,0x34u+0x34u+0x14cu);
    memcpy(image+D_AT,source+d,0xc4u);memcpy(image+INIT_AT,source+init,0x2eu);
    uint32_t fe=LOAD+EVENT_AT,fc=LOAD+C_AT;
    for(size_t s=E_AT;s<=I_AT;s+=0x34u) {put(image+s+0x28u,fe);put(image+s+0x2cu,0u);put(image+s+0x30u,fc);}
    put(image+C_AT+0x148u,fe);
    put(image+D_AT+0xb8u,fe+4u);put(image+D_AT+0xbcu,fe-8u);put(image+D_AT+0xc0u,fe-4u);
    put(image+INIT_AT+0x2au,LOAD+D_AT);
    (void)source_area;
}
static const size_t roles[]={E_AT+0x28u,E_AT+0x30u,I_AT+0x28u,I_AT+0x30u,C_AT+0x148u,
    D_AT+0xb8u,D_AT+0xbcu,D_AT+0xc0u,INIT_AT+0x2au};
static void alias(uint32_t to) {
    for(size_t i=0u;i<sizeof(roles)/sizeof(roles[0]);i++)
        put(image+roles[i],(word(image+roles[i])&UINT32_C(0x1fffffff))|to);
    area=to;
}
static void fixture_checks(void) {
    struct kui_native_sdk_sci_route_plan out;
    good();
    /* Every fingerprinted image byte matters. */
    const size_t windows[][2]={{E_AT,0x34u},{I_AT,0x34u},{C_AT,0x14cu},{D_AT,0xc4u},{INIT_AT,0x2eu}};
    for(size_t w=0u;w<sizeof(windows)/sizeof(windows[0]);w++)
        for(size_t at=0u;at<windows[w][1];at++)for(unsigned bit=0u;bit<8u;bit++) {
            image[windows[w][0]+at]^=(uint8_t)(1u<<bit);
            seed();CHECK(discover(&out)<=0);
            image[windows[w][0]+at]^=(uint8_t)(1u<<bit);
        }
    good();
    /* Live copies: only the scratch word may differ. */
    for(size_t at=0u;at<sizeof(stub600);at++) {
        seed();stub600[at]^=1u;int result=discover(&out);
        CHECK(at>=0x2cu && at<0x30u?result==1:result==-1);
    }
    seed();put(stub600+0x30u,LOAD+C_AT+0x14cu);CHECK(discover(&out)==-1);
    for(size_t at=0u;at<0x14cu;at++) {seed();live[C_AT+at]^=1u;CHECK(discover(&out)==-1);}
    for(size_t at=0u;at<0xc4u;at++) {seed();live[D_AT+at]^=1u;CHECK(discover(&out)==-1);}
    for(unsigned bit=0u;bit<32u;bit++) {
        seed();eri^=UINT32_C(1)<<bit;CHECK(discover(&out)==-1);
        seed();rxi^=UINT32_C(1)<<bit;CHECK(discover(&out)==-1);
    }
    seed();eri=rxi=CALLBACK;CHECK(discover(&out)==-1);
    seed();CHECK(discover(&out)==1);unsigned total=reads;
    for(unsigned fail=1u;fail<=total;fail++) {
        seed();fail_read=fail;CHECK(discover(&out)==-1);CHECK(reads==fail);
    }
    /* Roles must share one U0 or P1 area. */
    alias(0u);good();
    for(size_t i=0u;i<sizeof(roles)/sizeof(roles[0]);i++) {
        uint32_t saved=word(image+roles[i]);put(image+roles[i],saved|UINT32_C(0x80000000));
        seed();CHECK(discover(&out)==-1);put(image+roles[i],saved);
    }
    alias(UINT32_C(0xa0000000));seed();CHECK(discover(&out)==-1);
    alias(UINT32_C(0x80000000));good();
    /* Uniqueness, absence and the moved default. */
    memcpy(image+0x3000u,image+I_AT,0x34u);seed();CHECK(discover(&out)==-1);memset(image+0x3000u,0,0x34u);
    memcpy(image+0x1a02u,image+INIT_AT,0x2eu);seed();CHECK(discover(&out)==-1);memset(image+0x1a02u,0,0x2eu);
    put(image+INIT_AT+0x2au,LOAD+D_AT+4u);seed();CHECK(discover(&out)==-1);put(image+INIT_AT+0x2au,LOAD+D_AT);
    memcpy(image+0x2400u,image+D_AT,0xc4u);default_at=0x2400u;put(image+INIT_AT+0x2au,LOAD+0x2400u);good();
    memset(image+0x2400u,0,0xc4u);default_at=D_AT;put(image+INIT_AT+0x2au,LOAD+D_AT);good();
    uint8_t *saved=image;image=calloc(1u,bytes);CHECK(image);seed();
    CHECK(discover(&out)==0 && !reads);free(image);image=saved;
    /* Owner context is checked exactly as for the full family. */
    CHECK(kui_native_sdk_sci_route_discover(image,bytes,LOAD,vbr,UINT32_C(0x60000070),CALLBACK,
        read_word,live,&out)==-1);
    CHECK(kui_native_sdk_sci_route_discover(image,bytes,LOAD,vbr,UINT32_C(0x600000f0),CALLBACK+1u,
        read_word,live,&out)==-1);
    good();
}
static void probe(const char *path) {
    size_t n;uint8_t *p=file_bytes(path,&n);
    uint8_t *saved_image=image,*saved_live=live;size_t saved_bytes=bytes,saved_stub=stub_at,saved_default=default_at;
    uint32_t saved_area=area;
    image=p;bytes=n;live=malloc(n);CHECK(live);
    struct kui_native_sdk_sci_family family;int found=kui_native_sdk_sci_family_find(p,n,LOAD,&family);
    int result=found;
    if(found==1 && family.vector-LOAD+0x34u<=n && family.normal==family.fpu) {
        stub_at=family.vector-LOAD;area=family.original&UINT32_C(0xe0000000);
        default_at=((family.original&UINT32_C(0x1fffffff))|UINT32_C(0x80000000))-LOAD;
        vbr=UINT32_C(0x8c00f400);seed();
        struct kui_native_sdk_sci_route_plan out;result=discover(&out);
    }
    printf("%s: early family %s (finder %d, live model %d)\n",path,
        found==1 && result==1 && family.normal==family.fpu?"admitted":"not admitted",found,result);
    free(live);free(p);image=saved_image;live=saved_live;bytes=saved_bytes;stub_at=saved_stub;
    default_at=saved_default;area=saved_area;vbr=UINT32_C(0x8c00e000);
}
int main(int argc,char **argv) {
    CHECK(argc>=2);size_t n;uint8_t *source=file_bytes(argv[1],&n);fixture(source,n);free(source);
    fixture_checks();
    for(int i=2;i<argc;i++)probe(argv[i]);
    free(live);free(image);
    printf("early SDK SCI family: %u checks passed; fingerprints, live stub/common/default, slots, aliases, read faults\n",checks);
    return 0;
}
