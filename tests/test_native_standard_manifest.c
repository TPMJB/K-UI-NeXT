/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/native_vmu_standard.h"
#include "kui/vmu_persist_profile.h"
#include "kui/hash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef KUI_NATIVE_STANDARD_MANIFEST
#include KUI_NATIVE_STANDARD_MANIFEST
#endif
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"%s:%u: %s\n",__FILE__,__LINE__,#x);exit(1);}} while(0)
static bool recognize(struct kui_runtime_image *image,size_t expected) {
    uint32_t before=kui_crc32(0,image->data,image->info.payload_bytes);size_t slot=SIZE_MAX;
    bool result=kui_native_vmu_standard_recognize(image,&slot);
    CHECK(before==kui_crc32(0,image->data,image->info.payload_bytes));
    CHECK(slot==(result?expected:SIZE_MAX));return result;
}
int main(int argc,char **argv) {
    CHECK(argc==2);FILE *file=fopen(argv[1],"rb");CHECK(file && !fseek(file,0,SEEK_END));
    long size=ftell(file);CHECK(size>64 && !fseek(file,0,SEEK_SET));
    uint8_t *wire=malloc((size_t)size),*original=malloc((size_t)size);CHECK(wire && original);
    CHECK(fread(wire,1,(size_t)size,file)==(size_t)size && !fclose(file));
    memcpy(original,wire,(size_t)size);struct kui_runtime_image image={.data=wire+64};
    CHECK(kui_runtime_header(wire,(uint64_t)size,&image.info)==KUI_RUNTIME_OK);
#ifdef KUI_NATIVE_STANDARD_MANIFEST
    const size_t profile=KUI_NATIVE_STANDARD_MANIFEST_PROFILE_OFFSET;
    CHECK(sizeof(kui_native_standard_manifest_sha256)==32u);
    CHECK(image.info.payload_bytes==KUI_NATIVE_STANDARD_MANIFEST_PAYLOAD_BYTES);
    CHECK(recognize(&image,profile));
    uint8_t *data=image.data;
    /* Every code/data page remains fully pinned; only the canonical profile
     * changes during ordinary card selection. No build-label wildcard exists. */
    for(size_t at=0;at<image.info.payload_bytes;at+=512u) {
        if(at>=profile && at<profile+4096u)continue;
        data[at]^=1u;CHECK(!recognize(&image,profile));data[at]^=1u;
    }
    const size_t edges[]={profile-1u,profile+4096u,image.info.payload_bytes-1u};
    for(unsigned i=0;i<sizeof(edges)/sizeof(edges[0]);i++) {
        data[edges[i]]^=1u;CHECK(!recognize(&image,profile));data[edges[i]]^=1u;
    }
    struct kui_vmu_persist_profile *p=malloc(sizeof(*p));CHECK(p);
    CHECK(!kui_vmu_persist_profile_decode(p,data+profile,4096u));
    p->cid[0]^=0x80u;p->image_id[0]^=0x40u;
    struct kui_sha256 hash;kui_sha256_init(&hash);kui_sha256_update(&hash,p->image_id,16u);
    kui_sha256_digest(&hash,p->identity_sha256);
    CHECK(!kui_vmu_persist_profile_encode(p,data+profile) && recognize(&image,profile));
    data[profile+8u]^=1u;CHECK(!recognize(&image,profile));
    memcpy(data+profile,original+64u+profile,4096u);
    memcpy(data+4096u,data+profile,4096u);CHECK(!recognize(&image,profile));
    memcpy(data,original+64u,image.info.payload_bytes);
    image.info.build[0]=image.info.build[0]=='0'?'1':'0';CHECK(!recognize(&image,profile));
    CHECK(kui_runtime_header(original,(uint64_t)size,&image.info)==KUI_RUNTIME_OK);
    image.info.build[12]='x';CHECK(!recognize(&image,profile));image.info.build[12]=0;
    --image.info.memory_bytes;CHECK(!recognize(&image,profile));++image.info.memory_bytes;
    --image.info.payload_bytes;CHECK(!recognize(&image,profile));++image.info.payload_bytes;
    CHECK(recognize(&image,profile));free(p);
#else
    CHECK(!recognize(&image,SIZE_MAX));
#endif
    free(original);free(wire);
    printf("Standard build manifest: %u checks passed; current cohort only, code immutable, valid profile mutable\n",checks);
    return 0;
}
