/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/native_standalone_admission.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"standalone admission check failed line%u\n",__LINE__);exit(1);}}while(0)
static struct kui_retail_manifest valid(void) {
    struct kui_retail_manifest m;memset(&m,0x5a,sizeof(m));
    m.storage_transport=KUI_STORAGE_SCI;m.reader=KUI_RETAIL_READER_STANDARD;
    m.boot_bytes=KUI_NATIVE_STANDALONE_DOA2_BYTES;m.track_count=3;m.extent_count=3;m.flags=0;
    return m;
}
static void refused(struct kui_retail_manifest m) {
    struct kui_retail_manifest before=m;
    CHECK(!kui_native_standalone_launch_prepare(&m));CHECK(!memcmp(&m,&before,sizeof(m)));
}
int main(int argc,char **argv) {
    CHECK(argc==2);FILE *f=fopen(argv[1],"rb");CHECK(f);
    uint8_t *image=malloc(KUI_NATIVE_STANDALONE_DOA2_BYTES);CHECK(image);
    CHECK(fread(image,1,KUI_NATIVE_STANDALONE_DOA2_BYTES,f)==KUI_NATIVE_STANDALONE_DOA2_BYTES);
    CHECK(fgetc(f)==EOF);CHECK(!fclose(f));
    CHECK(kui_native_standalone_image_admit(image,KUI_NATIVE_STANDALONE_DOA2_BYTES));
    CHECK(!kui_native_standalone_image_admit(NULL,KUI_NATIVE_STANDALONE_DOA2_BYTES));
    CHECK(!kui_native_standalone_image_admit(image,KUI_NATIVE_STANDALONE_DOA2_BYTES-1));
    CHECK(!kui_native_standalone_image_admit(image,KUI_NATIVE_STANDALONE_DOA2_BYTES+1));
    const size_t offsets[]={0,127,0x123f50,0x124018,0x1240a4,0x12420c,KUI_NATIVE_STANDALONE_DOA2_BYTES-1};
    for(unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++)for(unsigned bit=0;bit<8;bit++) {
        image[offsets[i]]^=(uint8_t)(1u<<bit);
        CHECK(!kui_native_standalone_image_admit(image,KUI_NATIVE_STANDALONE_DOA2_BYTES));
        image[offsets[i]]^=(uint8_t)(1u<<bit);
    }
    CHECK(kui_native_standalone_image_admit(image,KUI_NATIVE_STANDALONE_DOA2_BYTES));free(image);
    CHECK(!kui_native_standalone_launch_prepare(NULL));
    for(unsigned tracks=1;tracks<64;tracks++)for(unsigned extents=1;extents<=64-tracks;extents++) {
        struct kui_retail_manifest m=valid(),expected;m.track_count=tracks;m.extent_count=extents;
        m.flags=(tracks&1u)?KUI_RETAIL_IMAGE_BOOT_CRC:0;expected=m;expected.reader=KUI_RETAIL_READER_ASYNC;
        CHECK(kui_native_standalone_launch_prepare(&m));CHECK(!memcmp(&m,&expected,sizeof(m)));
    }
    struct kui_retail_manifest m;
    for(unsigned id=0;id<8;id++)if(id!=KUI_STORAGE_SCI){m=valid();m.storage_transport=id;refused(m);}
    for(unsigned id=1;id<8;id++){m=valid();m.reader=id;refused(m);}
    for(unsigned bit=0;bit<32;bit++)if((1u<<bit)!=KUI_RETAIL_IMAGE_BOOT_CRC){m=valid();m.flags=1u<<bit;refused(m);}
    m=valid();m.track_count=0;refused(m);m=valid();m.extent_count=0;refused(m);
    m=valid();m.track_count=64;refused(m);m=valid();m.extent_count=62;refused(m);
    m=valid();m.boot_bytes--;refused(m);m=valid();m.boot_bytes++;refused(m);
    printf("Standalone exact DOA2/SCI/admission: %u checks PASS; only decoded reader changes on admission\n",checks);
    return 0;
}
