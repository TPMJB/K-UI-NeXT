/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/native_sdk_sci_route.h"
#include "kui/hash.h"
#include <string.h>

static const uint8_t owner_digest[32]={
    0x52,0xe0,0x0d,0x2b,0x3e,0x9d,0xb0,0x00,0x16,0xb1,0xf6,0xd7,0x82,0xcf,0x8a,0x21,
    0x41,0x61,0x7f,0x3d,0x00,0xbd,0x62,0x82,0x00,0xde,0x75,0x7f,0xd2,0xe1,0x16,0x05};
static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static bool overlap(const void *a,size_t an,const void *b,size_t bn) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return an>UINTPTR_MAX-x || bn>UINTPTR_MAX-y || (x<y+bn && y<x+an);
}
static bool fingerprint(const uint8_t *p,size_t n,const uint8_t expected[32]) {
    struct kui_sha256 hash;uint8_t digest[32];
    kui_sha256_init(&hash);kui_sha256_update(&hash,p,n);kui_sha256_digest(&hash,digest);
    return !memcmp(digest,expected,sizeof(digest));
}
static bool common(uint32_t address) {
    return address==UINT32_C(0x8c1ae030) || address==UINT32_C(0x8c1adec8);
}
static bool vector_fingerprint(const uint8_t *p) {
    static const uint8_t expected[32]={
        0xdc,0xda,0x3d,0xaf,0x9a,0x29,0x5c,0x62,0x4f,0x6f,0x18,0xe7,0x1c,0x49,0x60,0x96,
        0x2c,0xbf,0x7d,0x79,0xac,0x8c,0xfa,0xb4,0x9f,0x1f,0xca,0xd5,0x3a,0x04,0x6c,0x58};
    /* This one original initializer literal is mutable in the loaded image;
     * normalize it to its original value without copying a vector to stack. */
    static const uint8_t original[4]={0xc8,0xde,0x1a,0x8c};
    struct kui_sha256 hash;uint8_t digest[32];
    if(!common(u32(p+0x58u)))return false;
    kui_sha256_init(&hash);kui_sha256_update(&hash,p,0x58u);
    kui_sha256_update(&hash,original,4u);kui_sha256_update(&hash,p+0x5cu,4u);
    kui_sha256_digest(&hash,digest);return !memcmp(digest,expected,sizeof(digest));
}
static bool live_words(kui_native_sdk_sci_route_read read,void *context,
    uint32_t address,const uint8_t *expected,size_t bytes) {
    for(size_t at=0u;at<bytes;at+=4u) {
        uint32_t word;if(!read(context,address+(uint32_t)at,&word) || word!=u32(expected+at))return false;
    }
    return true;
}
struct window {uint32_t address,bytes;uint8_t sha256[32];};
/* Only compact fingerprints of independently supplied owner code are stored. */
static const struct window windows[]={
    {UINT32_C(0x8c1ae030),0x19cu,{
        0xa6,0x5e,0x4e,0xd8,0xf8,0xfd,0xae,0x94,0xa9,0xaf,0xe3,0x0b,0x38,0x75,0x9b,0x51,
        0xf3,0x29,0x2b,0x0a,0x3b,0xef,0x12,0xa9,0x51,0x2a,0x2a,0x32,0x47,0xdf,0xe9,0x8a}},
    {UINT32_C(0x8c1adec8),0x168u,{
        0xcc,0x73,0x77,0x5f,0x1b,0xf2,0x92,0xbd,0x1d,0x71,0x0d,0xb9,0x62,0x50,0x84,0x2a,
        0x6b,0x5a,0x6d,0xe6,0x8e,0xb0,0x68,0xb5,0xaa,0x52,0x3c,0x21,0x6e,0x67,0xe4,0x35}},
    {KUI_NATIVE_SDK_SCI_ROUTE_DEFAULT,0xf8u,{
        0x21,0x9a,0xc5,0x36,0xdf,0x2b,0xd5,0x09,0x03,0x32,0x28,0x75,0x18,0x0c,0x7b,0x04,
        0x95,0xd0,0xc8,0xbb,0x39,0xb7,0x5f,0x05,0x3e,0xbf,0x7d,0x85,0xf5,0x4a,0x01,0xec}},
    {UINT32_C(0x8c17e7c2),0x2eu,{
        0x96,0x8d,0x01,0x09,0x27,0xaf,0x0d,0x49,0x04,0x1c,0x5a,0xe7,0x31,0x7d,0xc7,0x7e,
        0x60,0x42,0xfa,0x62,0x0f,0x28,0xef,0xbc,0x99,0x18,0x4f,0x61,0x25,0xec,0x3f,0x92}}
};

int kui_native_sdk_sci_route_admit(const void *image,size_t image_bytes,uint32_t load,
    const uint8_t verified_sha256[32],uint32_t current_vbr,uint32_t current_sr,
    uint32_t callback,kui_native_sdk_sci_route_read read,void *context,
    struct kui_native_sdk_sci_route_plan *out) {
    if(!out)return -1;
    if((image && overlap(image,image_bytes,out,sizeof(*out))) ||
       (verified_sha256 && overlap(verified_sha256,32u,out,sizeof(*out))))return -1;
    memset(out,0,sizeof(*out));
    if(!image || !verified_sha256 || !read)return -1;
    if(memcmp(verified_sha256,owner_digest,sizeof(owner_digest)))return 0;
    if(image_bytes!=KUI_NATIVE_SDK_SCI_ROUTE_IMAGE_BYTES || load!=KUI_NATIVE_SDK_SCI_ROUTE_LOAD ||
       current_vbr!=KUI_NATIVE_SDK_SCI_ROUTE_VBR ||
       (current_sr&UINT32_C(0x400000f0))!=UINT32_C(0x400000f0) || (callback&1u) ||
       callback<UINT32_C(0x8c004000) || callback>=UINT32_C(0x8c007ba0))return -1;
    const uint8_t *p=image;
    const uint8_t *vector=p+(UINT32_C(0x8c1ade3c)-KUI_NATIVE_SDK_SCI_ROUTE_LOAD);
    if(!vector_fingerprint(vector))return -1;
    for(size_t i=0u;i<sizeof(windows)/sizeof(windows[0]);i++) {
        const struct window *w=&windows[i];
        if(!fingerprint(p+(w->address-load),w->bytes,w->sha256))return -1;
    }
    uint32_t selected=0u,eri,rxi;
    for(unsigned i=0u;i<24u;i++) {
        if(i==20u)continue; /* original copied vector's mutable R0 scratch */
        uint32_t word;
        if(!read(context,current_vbr+0x600u+4u*i,&word))return -1;
        if(i==22u) {if(!common(word))return -1;selected=word;}
        else if(word!=u32(vector+4u*i))return -1;
    }
    for(size_t i=0u;i<3u;i++) {
        const struct window *w=&windows[i];
        if((i==2u || w->address==selected) &&
           !live_words(read,context,w->address,p+(w->address-load),w->bytes))return -1;
    }
    if(!read(context,KUI_NATIVE_SDK_SCI_ROUTE_ERI,&eri) ||
       !read(context,KUI_NATIVE_SDK_SCI_ROUTE_RXI,&rxi) ||
       eri!=KUI_NATIVE_SDK_SCI_ROUTE_DEFAULT || rxi!=KUI_NATIVE_SDK_SCI_ROUTE_DEFAULT)return -1;
    out->version=KUI_NATIVE_SDK_SCI_ROUTE_VERSION;out->bytes=sizeof(*out);
    out->vbr=current_vbr;out->common=selected;out->eri_slot=KUI_NATIVE_SDK_SCI_ROUTE_ERI;
    out->rxi_slot=KUI_NATIVE_SDK_SCI_ROUTE_RXI;out->callback=callback;out->previous=eri;
    return 1;
}
