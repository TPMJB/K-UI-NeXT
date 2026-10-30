/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/native_vmu_standard.h"
#include "kui/vmu_persist_profile.h"
#include "kui/hash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"%s:%u: %s\n",__FILE__,__LINE__,#x);exit(1);}} while(0)
static const size_t labels[6]={148930,159190,176082,188306,200530,212754};
static bool recognized(struct kui_runtime_image *image) {
    uint32_t crc=kui_crc32(0,image->data,image->info.payload_bytes);size_t slot=SIZE_MAX;
    bool ok=kui_native_vmu_standard_recognize(image,&slot);
    CHECK(crc==kui_crc32(0,image->data,image->info.payload_bytes));
    if(ok) CHECK(slot==97600u);
    return ok;
}
static void test(const char *path) {
    FILE *file=fopen(path,"rb");CHECK(file && !fseek(file,0,SEEK_END));long size=ftell(file);
    CHECK(size>64 && !fseek(file,0,SEEK_SET));uint8_t *wire=malloc((size_t)size),*original=malloc((size_t)size);
    CHECK(wire && original && fread(wire,1,(size_t)size,file)==(size_t)size && !fclose(file));
    memcpy(original,wire,(size_t)size);struct kui_runtime_image image={.data=wire+64};
    CHECK(kui_runtime_header(wire,(uint64_t)size,&image.info)==KUI_RUNTIME_OK);
    CHECK(kui_crc32(0,image.data,image.info.payload_bytes)==image.info.crc32 && recognized(&image));
    uint8_t *data=image.data;
    for(size_t at=0;at<image.info.payload_bytes;at+=4096u) {
        if(at>=97600u && at<101696u)continue;
        data[at]^=1u;CHECK(!recognized(&image));data[at]^=1u;
    }
    const size_t edges[]={97599,101696,213287,148924,148942,159184,159202,176076,176094,
        188300,188318,200524,200542,212748,212766};
    for(unsigned i=0;i<sizeof(edges)/sizeof(edges[0]);i++) {
        data[edges[i]]^=1u;CHECK(!recognized(&image));data[edges[i]]^=1u;
    }
    for(unsigned i=0;i<6u;i++) {
        data[labels[i]]^=1u;CHECK(!recognized(&image));data[labels[i]]^=1u;
    }
    struct kui_vmu_persist_profile *profile=malloc(sizeof(*profile));CHECK(profile);
    CHECK(!kui_vmu_persist_profile_decode(profile,data+97600u,4096u));
    profile->cid[0]^=0x80u;profile->image_id[0]^=0x40u;
    struct kui_sha256 identity_hash;kui_sha256_init(&identity_hash);
    kui_sha256_update(&identity_hash,profile->image_id,16u);
    kui_sha256_digest(&identity_hash,profile->identity_sha256);
    CHECK(!kui_vmu_persist_profile_encode(profile,data+97600u) && recognized(&image));
    data[97608u]^=1u;CHECK(!recognized(&image));
    memcpy(data+97600u,original+64u+97600u,4096u);
    memcpy(data+4096u,data+97600u,4096u);CHECK(!recognized(&image));
    memcpy(data,original+64u,image.info.payload_bytes);
    /* Build labels are metadata; every exact occurrence must agree with the
     * validated envelope. Code and all other bytes remain fully pinned. */
    memcpy(image.info.build,"0123456789ab",13u);
    for(unsigned i=0;i<6u;i++)memcpy(data+labels[i],image.info.build,12u);
    CHECK(recognized(&image));
    memcpy(data+2048u,image.info.build,12u);CHECK(!recognized(&image));
    memcpy(data,original+64u,image.info.payload_bytes);
    CHECK(!recognized(&image));
    CHECK(kui_runtime_header(original,(uint64_t)size,&image.info)==KUI_RUNTIME_OK);
    image.info.build[12]='x';CHECK(!recognized(&image));image.info.build[12]=0;
    --image.info.memory_bytes;CHECK(!recognized(&image));++image.info.memory_bytes;
    --image.info.payload_bytes;CHECK(!recognized(&image));++image.info.payload_bytes;
    CHECK(recognized(&image));free(profile);free(original);free(wire);
}
int main(int argc,char **argv) {
    CHECK(argc==3);test(argv[1]);test(argv[2]);
    printf("Paced SCI72 Standard whole-payload recognition: %u checks passed, both accepted modules and all regions\n",checks);
    return 0;
}
