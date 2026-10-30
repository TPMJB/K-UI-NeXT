/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/native_sdk_sci_route.h"
#include "kui/hash.h"
#include <string.h>

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
static bool live_words(kui_native_sdk_sci_route_read read,void *context,
    uint32_t address,const uint8_t *expected,size_t bytes) {
    for(size_t at=0u;at<bytes;at+=4u) {
        uint32_t word;if(!read(context,address+(uint32_t)at,&word) || word!=u32(expected+at))return false;
    }
    return true;
}
struct window {uint32_t address,bytes;uint8_t sha256[32];};
struct owner {
    uint32_t image_bytes,vector,vbr,eri_slot,rxi_slot;
    uint8_t digest[32],vector_sha256[32];
    struct window windows[4];
};
/* Compact fingerprints only; owner code is supplied independently. Window
 * order is FPU common, integer common, original default, table initializer. */
static const struct owner owners[]={
    /* Evolution2: the copied vector and callback ABI are separately replayed. */
    {UINT32_C(4024288),UINT32_C(0x8c1ade3c),
     KUI_NATIVE_SDK_SCI_ROUTE_VBR,KUI_NATIVE_SDK_SCI_ROUTE_ERI,KUI_NATIVE_SDK_SCI_ROUTE_RXI,
     {
            0x52,0xe0,0x0d,0x2b,0x3e,0x9d,0xb0,0x00,0x16,0xb1,0xf6,0xd7,0x82,0xcf,0x8a,0x21,
            0x41,0x61,0x7f,0x3d,0x00,0xbd,0x62,0x82,0x00,0xde,0x75,0x7f,0xd2,0xe1,0x16,0x05},
     {
            0xdc,0xda,0x3d,0xaf,0x9a,0x29,0x5c,0x62,0x4f,0x6f,0x18,0xe7,0x1c,0x49,0x60,0x96,
            0x2c,0xbf,0x7d,0x79,0xac,0x8c,0xfa,0xb4,0x9f,0x1f,0xca,0xd5,0x3a,0x04,0x6c,0x58}, {
        {UINT32_C(0x8c1ae030),UINT32_C(0x19c),{
            0xa6,0x5e,0x4e,0xd8,0xf8,0xfd,0xae,0x94,0xa9,0xaf,0xe3,0x0b,0x38,0x75,0x9b,0x51,
            0xf3,0x29,0x2b,0x0a,0x3b,0xef,0x12,0xa9,0x51,0x2a,0x2a,0x32,0x47,0xdf,0xe9,0x8a}},
        {UINT32_C(0x8c1adec8),UINT32_C(0x168),{
            0xcc,0x73,0x77,0x5f,0x1b,0xf2,0x92,0xbd,0x1d,0x71,0x0d,0xb9,0x62,0x50,0x84,0x2a,
            0x6b,0x5a,0x6d,0xe6,0x8e,0xb0,0x68,0xb5,0xaa,0x52,0x3c,0x21,0x6e,0x67,0xe4,0x35}},
        {UINT32_C(0x8c1add0c),UINT32_C(0xf8),{
            0x21,0x9a,0xc5,0x36,0xdf,0x2b,0xd5,0x09,0x03,0x32,0x28,0x75,0x18,0x0c,0x7b,0x04,
            0x95,0xd0,0xc8,0xbb,0x39,0xb7,0x5f,0x05,0x3e,0xbf,0x7d,0x85,0xf5,0x4a,0x01,0xec}},
        {UINT32_C(0x8c17e7c2),UINT32_C(0x2e),{
            0x96,0x8d,0x01,0x09,0x27,0xaf,0x0d,0x49,0x04,0x1c,0x5a,0xe7,0x31,0x7d,0xc7,0x7e,
            0x60,0x42,0xfa,0x62,0x0f,0x28,0xef,0xbc,0x99,0x18,0x4f,0x61,0x25,0xec,0x3f,0x92}},
    }},
    /* Evolution1: the copied vector and callback ABI are separately replayed. */
    {UINT32_C(2143652),UINT32_C(0x8c1da080),
     KUI_NATIVE_SDK_SCI_ROUTE_VBR,KUI_NATIVE_SDK_SCI_ROUTE_ERI,KUI_NATIVE_SDK_SCI_ROUTE_RXI,
     {
            0xff,0xb5,0xa8,0xe5,0xff,0x55,0x47,0x02,0x6d,0x93,0x64,0x9e,0x1f,0x90,0x58,0xb7,
            0xee,0x5e,0x29,0xd8,0xd0,0xff,0x8a,0x89,0x33,0x57,0x01,0xf7,0xe9,0x64,0x7f,0x35},
     {
            0xd9,0xb2,0x22,0x52,0xca,0x3d,0x8a,0xf7,0xb5,0x51,0x40,0x4c,0xd4,0x62,0x56,0x3b,
            0x81,0xc7,0xfe,0xd0,0x0a,0xa4,0xa4,0x2a,0x09,0xa1,0xd1,0xfb,0xda,0xcb,0x4c,0x62}, {
        {UINT32_C(0x8c1da274),UINT32_C(0x19c),{
            0xde,0x67,0x58,0x5e,0xa7,0x09,0x30,0x60,0x62,0x16,0xee,0xbb,0xd1,0x92,0xd5,0x62,
            0x25,0x60,0xb5,0x8f,0x13,0xe9,0xaa,0xe4,0x31,0xc0,0x2d,0xec,0xfc,0x18,0x3a,0x5d}},
        {UINT32_C(0x8c1da10c),UINT32_C(0x168),{
            0xcb,0x4f,0x52,0x11,0x7b,0xed,0xbf,0xf2,0xc7,0x50,0x34,0x02,0x0f,0xf3,0xb7,0xe8,
            0xb2,0xee,0x65,0x0c,0xb3,0xe8,0xfd,0x82,0xa3,0xe4,0x5c,0x14,0x2f,0x57,0x59,0xc3}},
        {UINT32_C(0x8c1d9f50),UINT32_C(0xf8),{
            0x04,0x0f,0xba,0xcb,0xd7,0x38,0x4f,0x73,0xae,0xeb,0x7b,0x43,0xcd,0x7b,0xf5,0x41,
            0x55,0x51,0xe3,0x0a,0x10,0x7e,0x4e,0x73,0x1e,0xdd,0xc8,0xeb,0x06,0x92,0x6b,0x93}},
        {UINT32_C(0x8c1b0e56),UINT32_C(0x2e),{
            0x9a,0x60,0x45,0x63,0xd2,0x6d,0x59,0xbd,0xab,0x49,0x38,0xf5,0x4b,0x69,0x0b,0x6b,
            0x96,0x56,0xa9,0xa5,0x3f,0x82,0x45,0x7c,0x51,0xc6,0xa1,0x57,0x90,0xbf,0xcf,0xf3}},
    }},
};
static bool common(const struct owner *o,uint32_t address) {
    return address==o->windows[0].address || address==o->windows[1].address;
}
static bool vector_fingerprint(const struct owner *o,const uint8_t *p) {
    struct kui_sha256 hash;uint8_t digest[32],original[4];
    uint32_t normal=o->windows[1].address;
    for(unsigned i=0u;i<4u;i++)original[i]=(uint8_t)(normal>>(8u*i));
    /* Normalize only the selected-common source literal, which the original
     * initializer can legitimately switch between its two reviewed modes. */
    if(!common(o,u32(p+0x58u)))return false;
    kui_sha256_init(&hash);kui_sha256_update(&hash,p,0x58u);
    kui_sha256_update(&hash,original,4u);kui_sha256_update(&hash,p+0x5cu,4u);
    kui_sha256_digest(&hash,digest);return !memcmp(digest,o->vector_sha256,sizeof(digest));
}

int kui_native_sdk_sci_route_admit(const void *image,size_t image_bytes,uint32_t load,
    const uint8_t verified_sha256[32],uint32_t current_vbr,uint32_t current_sr,
    uint32_t callback,kui_native_sdk_sci_route_read read,void *context,
    struct kui_native_sdk_sci_route_plan *out) {
    if(!out)return -1;
    if((image && overlap(image,image_bytes,out,sizeof(*out))) ||
       (verified_sha256 && overlap(verified_sha256,32u,out,sizeof(*out))))return -1;
    memset(out,0,sizeof(*out));
    if(!image || !verified_sha256 || !read)return -1;
    const struct owner *o=NULL;
    for(unsigned i=0u;i<sizeof(owners)/sizeof(owners[0]);i++)
        if(!memcmp(verified_sha256,owners[i].digest,32u)) {o=&owners[i];break;}
    if(!o)return 0;
    if(image_bytes!=o->image_bytes || load!=KUI_NATIVE_SDK_SCI_ROUTE_LOAD ||
       current_vbr!=o->vbr ||
       (current_sr&UINT32_C(0x400000f0))!=UINT32_C(0x400000f0) || (callback&1u) ||
       callback<UINT32_C(0x8c004000) || callback>=UINT32_C(0x8c007ba0))return -1;
    const uint8_t *p=image;
    const uint8_t *vector=p+(o->vector-load);
    if(!vector_fingerprint(o,vector))return -1;
    for(size_t i=0u;i<sizeof(o->windows)/sizeof(o->windows[0]);i++) {
        const struct window *w=&o->windows[i];
        if(!fingerprint(p+(w->address-load),w->bytes,w->sha256))return -1;
    }
    uint32_t selected=0u,eri,rxi;
    for(unsigned i=0u;i<24u;i++) {
        if(i==20u)continue; /* original copied vector's mutable R0 scratch */
        uint32_t word;
        if(!read(context,current_vbr+0x600u+4u*i,&word))return -1;
        if(i==22u) {if(!common(o,word))return -1;selected=word;}
        else if(word!=u32(vector+4u*i))return -1;
    }
    for(size_t i=0u;i<3u;i++) {
        const struct window *w=&o->windows[i];
        if((i==2u || w->address==selected) &&
           !live_words(read,context,w->address,p+(w->address-load),w->bytes))return -1;
    }
    if(!read(context,o->eri_slot,&eri) ||
       !read(context,o->rxi_slot,&rxi) ||
       eri!=o->windows[2].address || rxi!=o->windows[2].address)return -1;
    out->version=KUI_NATIVE_SDK_SCI_ROUTE_VERSION;out->bytes=sizeof(*out);
    out->vbr=current_vbr;out->common=selected;out->eri_slot=o->eri_slot;
    out->rxi_slot=o->rxi_slot;out->callback=callback;out->previous=eri;
    return 1;
}

#include "native_sdk_sci_family.inc"
int kui_native_sdk_sci_route_discover(const void *image,size_t image_bytes,uint32_t load,
    uint32_t current_vbr,uint32_t current_sr,uint32_t callback,
    kui_native_sdk_sci_route_read read,void *context,
    struct kui_native_sdk_sci_route_plan *out) {
    if(!out)return -1;
    if((image && overlap(image,image_bytes,out,sizeof(*out))) ||
       sizeof(*out)>UINTPTR_MAX-(uintptr_t)out)return -1;
    memset(out,0,sizeof(*out));
    if(!image || !read || image_bytes<2u ||
       image_bytes>KUI_NATIVE_SDK_SCI_ROUTE_MAX_IMAGE_BYTES ||
       image_bytes>UINTPTR_MAX-(uintptr_t)image || load!=KUI_NATIVE_SDK_SCI_ROUTE_LOAD ||
       current_vbr<UINT32_C(0x8c000000) || current_vbr>UINT32_C(0x8cfff900) ||
       (current_vbr&255u) ||
       (current_sr&UINT32_C(0x400000f0))!=UINT32_C(0x400000f0) || (callback&1u) ||
       callback<UINT32_C(0x8c004000) || callback>=UINT32_C(0x8c007ba0))return -1;
    const uint8_t *p=image;size_t vector_at=0u;unsigned vectors=0u;
    for(size_t at=0u;at<image_bytes && image_bytes-at>=64u;at+=4u) {
        if(u32(p+at)!=UINT32_C(0x00090009) || u32(p+at+4u)!=UINT32_C(0x00090009) ||
           u32(p+at+8u)!=UINT32_C(0x0009a00d) ||
           !fingerprint(p+at,64u,family_vector_prefix))continue;
        vector_at=at;if(++vectors>1u)return -1;
    }
    if(!vectors)return 0;
    if(image_bytes-vector_at<96u)return -1;
    uint32_t vector=load+(uint32_t)vector_at;
    /* The event word selects the full, compact-FPU or round-toward-zero
     * layout; every role below is still decoded and fingerprinted. */
    uint32_t event_offset=(u32(p+vector_at+0x4cu)&UINT32_C(0x1fffffff))-
        (vector&UINT32_C(0x1fffffff));
    bool compact=event_offset==0x338u,zero=event_offset==0x3a0u;
    if(!compact && !zero && event_offset!=0x398u)return -1;
    if(image_bytes-vector_at<event_offset+8u)return -1;
    uint32_t normal_bytes=compact?0x138u:zero?0x16cu:0x168u;
    uint32_t fpu_bytes=compact?0x16cu:zero?0x1a0u:0x19cu;
    uint32_t normal=vector+0x8cu,fpu=normal+normal_bytes;
    uint32_t event=vector+event_offset,original=0u;size_t default_at=0u;
    const uint8_t *v=p+vector_at,*integer=v+0x8cu,*floating=v+0x8cu+normal_bytes;
    static const uint16_t vector_holes[]={0x4cu,0x58u};
    uint16_t normal_hole=(uint16_t)(normal_bytes-4u),fpu_hole=(uint16_t)(fpu_bytes-4u);
    static const uint16_t default_holes[]={0xe8u,0xecu,0xf0u,0xf4u,0xf8u};
    static const uint16_t init_holes[]={0x2au};
    /* Link order is not an ABI. The unique, exact SDK table initializer
     * supplies its original default; source adjacency never selects it. */
    unsigned initializers=0u;bool default_bound=false;
    for(size_t at=0u;at<image_bytes && image_bytes-at>=0x2eu;at+=2u) {
        if(u16(p+at)!=0x9511u || u16(p+at+2u)!=0x9411u ||
           !normalized(p+at,0x2eu,init_holes,1u,family_init))continue;
        if(++initializers>1u)return -1;
        default_bound=literal_at(p+at,load+(uint32_t)at,4u,7u,0x2au);
        original=u32(p+at+0x2au);
    }
    uint32_t area=original&UINT32_C(0xe0000000);
    if(initializers!=1u || !default_bound || (original&3u) ||
       (area!=0u && area!=UINT32_C(0x80000000)) ||
       !owner_span(original,compact?0xfcu:0xf8u,area,load,image_bytes,&default_at))return -1;
    const uint8_t *def=p+default_at;
    uint32_t raw_normal=(normal&UINT32_C(0x1fffffff))|area;
    uint32_t raw_fpu=(fpu&UINT32_C(0x1fffffff))|area;
    uint32_t raw_event=(event&UINT32_C(0x1fffffff))|area;
    uint32_t selected_source=u32(v+0x58u),helper=u32(def+0xf4u);size_t helper_at;
    if(u32(v+0x4cu)!=raw_event || (selected_source!=raw_normal && selected_source!=raw_fpu) ||
       u32(integer+normal_hole)!=raw_event || u32(floating+fpu_hole)!=raw_event ||
       u32(def+0xe8u)!=raw_event+4u || u32(def+0xecu)!=raw_event-8u || u32(def+0xf0u)!=raw_event-4u ||
       (compact && u32(def+0xf8u)!=raw_event) ||
       (helper&3u) || !owner_span(helper,0x238u,area,load,image_bytes,&helper_at) ||
       !literal_at(v,vector,0x38u,0u,0x4cu) || !literal_at(v,vector,0x3cu,1u,0x58u) ||
       !literal_at(integer,normal,compact?0x80u:zero?0x94u:0x92u,4u,normal_hole) ||
       !literal_at(integer,normal,compact?0x10cu:zero?0x140u:0x13cu,1u,normal_hole) ||
       !literal_at(floating,fpu,compact?0x13eu:zero?0x172u:0x16eu,1u,fpu_hole) ||
       !literal_at(def,load+(uint32_t)default_at,0x40u,0u,0xf4u) ||
       (compact && !literal_at(def,load+(uint32_t)default_at,0xbeu,1u,0xf8u)) ||
       !normalized(v,96u,vector_holes,2u,family_vector) ||
       !normalized(integer,normal_bytes,&normal_hole,1u,
           compact?compact_normal:zero?zero_normal:family_normal) ||
       !normalized(floating,fpu_bytes,&fpu_hole,1u,compact?compact_fpu:zero?zero_fpu:family_fpu) ||
       !normalized(def,compact?0xfcu:0xf8u,default_holes,compact?5u:4u,
           compact?compact_default:family_default))return -1;
    for(size_t i=0u;i<sizeof(helper_windows)/sizeof(helper_windows[0]);i++)
        if(!fingerprint(p+helper_at+helper_windows[i].offset,helper_windows[i].bytes,
            helper_windows[i].sha256))return -1;
    uint32_t selected=0u,eri,rxi;
    for(unsigned i=0u;i<24u;i++) {
        if(i==20u)continue;
        uint32_t word;
        if(!read(context,current_vbr+0x600u+4u*i,&word))return -1;
        if(i==22u) {
            if(word!=raw_normal && word!=raw_fpu)return -1;
            selected=word;
        } else if(word!=u32(v+4u*i))return -1;
    }
    uint32_t selected_code=(selected&UINT32_C(0x1fffffff))|UINT32_C(0x80000000);
    if(!live_words(read,context,selected_code,p+(selected_code-load),selected==raw_normal?normal_bytes:fpu_bytes) ||
       !live_words(read,context,load+(uint32_t)default_at,def,compact?0xfcu:0xf8u))return -1;
    for(size_t i=0u;i<sizeof(helper_windows)/sizeof(helper_windows[0]);i++)
        if(!live_words(read,context,load+(uint32_t)helper_at+helper_windows[i].offset,
            p+helper_at+helper_windows[i].offset,helper_windows[i].bytes))return -1;
    if(!read(context,current_vbr+0x25cu,&eri) || !read(context,current_vbr+0x260u,&rxi) ||
       eri!=original || rxi!=original)return -1;
    *out=(struct kui_native_sdk_sci_route_plan){KUI_NATIVE_SDK_SCI_ROUTE_VERSION,sizeof(*out),
        current_vbr,selected,current_vbr+0x25cu,current_vbr+0x260u,callback,eri};
    return 1;
}
