/* SPDX-License-Identifier: GPL-3.0-only */
/* Structural standalone launch admission, its capability record and the SDK
 * family finder's input handling. No retail bytes are embedded: the finder's
 * positive cases run on originals in test_native_sdk_sci_route_discovery.c. */
#include "kui/native_standalone_admission.h"
#include "kui/native_standalone_capability.h"
#include "kui/native_sdk_sci_family.h"
#include "kui/native_sdk_sci_route.h"
#include "kui/retail_loader_layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"structural standalone check failed line%u\n",__LINE__);exit(1);}}while(0)
static struct kui_retail_manifest valid(void) {
    struct kui_retail_manifest m;memset(&m,0x5a,sizeof(m));
    m.storage_transport=KUI_STORAGE_SCI;m.reader=KUI_RETAIL_READER_ASYNC;
    m.boot_bytes=3097497u;m.track_count=3;m.extent_count=3;m.flags=0;
    return m;
}
static void refused(struct kui_retail_manifest m) {
    struct kui_retail_manifest before=m;
    CHECK(!kui_native_standalone_launch_prepare(&m));CHECK(!memcmp(&m,&before,sizeof(m)));
}
static void launch_checks(void) {
    CHECK(!kui_native_standalone_launch_prepare(NULL));
    for(unsigned tracks=1;tracks<64;tracks++)for(unsigned extents=1;extents<=64-tracks;extents++) {
        struct kui_retail_manifest m=valid(),before;m.track_count=tracks;m.extent_count=extents;
        m.flags=(tracks&1u)?KUI_RETAIL_IMAGE_BOOT_CRC:0;before=m;
        CHECK(kui_native_standalone_launch_prepare(&m));CHECK(!memcmp(&m,&before,sizeof(m)));
    }
    /* No title size: the stage's family proof decides on the loaded image. */
    const uint32_t sizes[]={128u,1815864u,2143652u,KUI_RETAIL_EXEC_MAX_BYTES};
    for(unsigned i=0;i<sizeof(sizes)/sizeof(sizes[0]);i++) {
        struct kui_retail_manifest m=valid();m.boot_bytes=sizes[i];
        CHECK(kui_native_standalone_launch_prepare(&m));
    }
    struct kui_retail_manifest m;
    for(unsigned id=0;id<8;id++)if(id!=KUI_STORAGE_SCI){m=valid();m.storage_transport=id;refused(m);}
    for(unsigned id=0;id<8;id++)if(id!=KUI_RETAIL_READER_ASYNC){m=valid();m.reader=id;refused(m);}
    for(unsigned bit=0;bit<32;bit++)if((1u<<bit)!=KUI_RETAIL_IMAGE_BOOT_CRC){m=valid();m.flags=1u<<bit;refused(m);}
    m=valid();m.track_count=0;refused(m);m=valid();m.extent_count=0;refused(m);
    m=valid();m.track_count=64;refused(m);m=valid();m.extent_count=62;refused(m);
}
static void put(uint8_t *p,uint32_t value) {for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(value>>(8u*i));}
static void capability(uint8_t *p) {
    const uint32_t words[8]={KUI_NATIVE_STANDALONE_CAP_MAGIC0,KUI_NATIVE_STANDALONE_CAP_MAGIC1,1u,
        KUI_NATIVE_STANDALONE_CAP_BYTES,KUI_RETAIL_READER_ASYNC,0u,0u,0u};
    for(unsigned i=0;i<8;i++)put(p+4u*i,words[i]);
}
static void capability_checks(void) {
    uint8_t package[256];size_t at=0;
    memset(package,0,sizeof(package));CHECK(kui_native_standalone_descriptor_find(package,sizeof(package),&at)==0);
    for(size_t offset=0;offset<=sizeof(package)-KUI_NATIVE_STANDALONE_CAP_BYTES;offset+=7) {
        memset(package,0xa5,sizeof(package));capability(package+offset);at=SIZE_MAX;
        CHECK(kui_native_standalone_descriptor_find(package,sizeof(package),&at)==1 && at==offset);
        CHECK(kui_native_standalone_descriptor_find(package,offset+KUI_NATIVE_STANDALONE_CAP_BYTES-1u,NULL)==0);
    }
    memset(package,0,sizeof(package));capability(package);capability(package+100);
    CHECK(kui_native_standalone_descriptor_find(package,sizeof(package),NULL)==-1);
    /* Every changed word, including a nonzero lease or virtual-card claim, refuses. */
    for(unsigned word=0;word<8;word++)for(unsigned bit=0;bit<32;bit+=5) {
        memset(package,0,sizeof(package));capability(package+16);
        package[16+4u*word+bit/8u]^=(uint8_t)(1u<<(bit%8u));
        CHECK(kui_native_standalone_descriptor_find(package,sizeof(package),NULL)==0);
    }
    CHECK(kui_native_standalone_descriptor_find(NULL,sizeof(package),NULL)==0);
    CHECK(!kui_native_standalone_descriptor_valid(package,KUI_NATIVE_STANDALONE_CAP_BYTES-1u));
}
static bool cleared(const struct kui_native_sdk_sci_family *f) {
    return !f->vector && !f->normal && !f->fpu && !f->original;
}
static void family_input_checks(void) {
    const size_t n=0x10000u;uint8_t *image=calloc(1,n);CHECK(image);
    struct kui_native_sdk_sci_family out;
    CHECK(kui_native_sdk_sci_family_find(image,n,KUI_NATIVE_SDK_SCI_ROUTE_LOAD,NULL)==-1);
    memset(&out,0xa5,sizeof(out));
    CHECK(kui_native_sdk_sci_family_find(NULL,n,KUI_NATIVE_SDK_SCI_ROUTE_LOAD,&out)==-1 && cleared(&out));
    memset(&out,0xa5,sizeof(out));
    CHECK(kui_native_sdk_sci_family_find(image,n,KUI_NATIVE_SDK_SCI_ROUTE_LOAD+4u,&out)==-1 && cleared(&out));
    for(size_t bytes=0;bytes<2;bytes++) {
        memset(&out,0xa5,sizeof(out));
        CHECK(kui_native_sdk_sci_family_find(image,bytes,KUI_NATIVE_SDK_SCI_ROUTE_LOAD,&out)==-1 && cleared(&out));
    }
    memset(&out,0xa5,sizeof(out));
    CHECK(kui_native_sdk_sci_family_find(image,KUI_NATIVE_SDK_SCI_ROUTE_MAX_IMAGE_BYTES+1u,
        KUI_NATIVE_SDK_SCI_ROUTE_LOAD,&out)==-1 && cleared(&out));
    memset(&out,0xa5,sizeof(out));
    CHECK(kui_native_sdk_sci_family_find(image,n,KUI_NATIVE_SDK_SCI_ROUTE_LOAD,&out)==0 && cleared(&out));
    /* Overlapping output is refused without writing the image. */
    memset(image,0x5a,n);
    CHECK(kui_native_sdk_sci_family_find(image,n,KUI_NATIVE_SDK_SCI_ROUTE_LOAD,
        (struct kui_native_sdk_sci_family *)(void *)(image+64))==-1);
    for(size_t i=0;i<n;i++)CHECK(image[i]==0x5a);
    /* NOP padding and the three cheap prefix words alone are not a family. */
    for(size_t at=0;at<n;at+=4)put(image+at,0x00090009u);
    CHECK(kui_native_sdk_sci_family_find(image,n,KUI_NATIVE_SDK_SCI_ROUTE_LOAD,&out)==0 && cleared(&out));
    for(size_t at=0x400;at<n-64;at+=0x400)put(image+at+8u,0x0009a00du);
    CHECK(kui_native_sdk_sci_family_find(image,n,KUI_NATIVE_SDK_SCI_ROUTE_LOAD,&out)==0 && cleared(&out));
    free(image);
}
int main(void) {
    launch_checks();capability_checks();family_input_checks();
    printf("Structural standalone launch/capability/family input: %u checks PASS\n",checks);
    return 0;
}
