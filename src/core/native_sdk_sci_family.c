/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/native_sdk_sci_family.h"
#include "kui/native_sdk_sci_route.h"
#include "kui/hash.h"
#include <stdbool.h>
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
#include "native_sdk_sci_family.inc"

/* Keep these image checks in the same order as kui_native_sdk_sci_route_discover;
 * tests/test_native_sdk_sci_route_discovery.c compares both on originals. */
int kui_native_sdk_sci_family_find(const void *image,size_t image_bytes,uint32_t load,
    struct kui_native_sdk_sci_family *out) {
    if(!out)return -1;
    if((image && overlap(image,image_bytes,out,sizeof(*out))) ||
       sizeof(*out)>UINTPTR_MAX-(uintptr_t)out)return -1;
    memset(out,0,sizeof(*out));
    if(!image || image_bytes<2u ||
       image_bytes>KUI_NATIVE_SDK_SCI_ROUTE_MAX_IMAGE_BYTES ||
       image_bytes>UINTPTR_MAX-(uintptr_t)image || load!=KUI_NATIVE_SDK_SCI_ROUTE_LOAD)return -1;
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
    *out=(struct kui_native_sdk_sci_family){vector,raw_normal,raw_fpu,original};
    return 1;
}
