/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/cold_image.h"
#include "kui/hash.h"
#include "kui/retail_image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,owners;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"line %u: %s\n",__LINE__,#x);exit(1);}}while(0)
static uint8_t image[1024],out[1024],saved[1024],expected[32];
static struct kui_cold_image_seed blank(void) {
    return (struct kui_cold_image_seed){KUI_COLD_IMAGE_MAGIC0,KUI_COLD_IMAGE_MAGIC1,1u,32u,0u,0u,0u,0u};
}
static bool owner(uint32_t bytes,const uint8_t sha[32]) {
    ++owners;return bytes==sizeof(image) && !memcmp(sha,expected,32u);
}
static bool restore(const struct kui_cold_image_seed *s) {
    return kui_cold_image_restore(s,out,sizeof(out),image,sizeof(image),UINT32_C(0xf4ac7d4d),owner);
}
int main(void) {
    uint8_t prefix[512]={0};
    CHECK(!kui_cold_image_capable(prefix,sizeof(prefix),sizeof(image)));
    const uint32_t cap[6]={0x4349554bu,0x31305041u,1u,24u,sizeof(image),~(uint32_t)sizeof(image)};
    memcpy(prefix+KUI_COLD_IMAGE_CAP_OFFSET,cap,sizeof(cap));
    CHECK(kui_cold_image_capable(prefix,sizeof(prefix),sizeof(image)));
    CHECK(!kui_cold_image_capable(prefix,sizeof(prefix),sizeof(image)+4u));
    CHECK(!kui_cold_image_capable(prefix,KUI_COLD_IMAGE_CAP_OFFSET+sizeof(cap)-1u,sizeof(image)));
    for(unsigned bit=0u;bit<sizeof(cap)*8u;bit++) {
        prefix[KUI_COLD_IMAGE_CAP_OFFSET+bit/8u]^=(uint8_t)(1u<<(bit%8u));
        CHECK(!kui_cold_image_capable(prefix,sizeof(prefix),sizeof(image)));
        prefix[KUI_COLD_IMAGE_CAP_OFFSET+bit/8u]^=(uint8_t)(1u<<(bit%8u));
    }
    for(unsigned i=0u;i<sizeof(image);i++)image[i]=(uint8_t)(i*17u+5u);
    struct kui_sha256 hash;kui_sha256_init(&hash);kui_sha256_update(&hash,image,sizeof(image));kui_sha256_digest(&hash,expected);
    CHECK(kui_retail_crc32(0,image,sizeof(image))==UINT32_C(0xf4ac7d4d));
    struct kui_cold_image_seed s=blank(),original=s;
    CHECK(kui_cold_image_seed_blank(&s));CHECK(!kui_cold_image_seed_valid(&s));
    CHECK(kui_cold_image_seed_configure(&s,KUI_COLD_IMAGE_SCRATCH,sizeof(image),UINT32_C(0xf4ac7d4d)));
    CHECK(kui_cold_image_seed_valid(&s));CHECK(!kui_cold_image_seed_blank(&s));
    CHECK(restore(&s));CHECK(owners==1u);CHECK(!memcmp(out,image,sizeof(out)));
    memset(out,0x5a,sizeof(out));memcpy(saved,out,sizeof(saved));
    for(unsigned bit=0;bit<sizeof(s)*8u;bit++) {
        struct kui_cold_image_seed changed=s;((uint8_t *)&changed)[bit/8u]^=(uint8_t)(1u<<(bit%8u));
        CHECK(!restore(&changed));CHECK(!memcmp(out,saved,sizeof(out)));
    }
    const uint32_t rejected[]={0u,0x8c3fffe0u,0x8cc00000u,0x8c400001u,0x0c400000u,0xacbfffe0u};
    for(unsigned i=0;i<sizeof(rejected)/sizeof(rejected[0]);i++) {
        struct kui_cold_image_seed candidate=blank();
        CHECK(!kui_cold_image_seed_configure(&candidate,rejected[i],sizeof(image),1u));
        CHECK(!memcmp(&candidate,&original,sizeof(candidate)));
    }
    CHECK(!kui_cold_image_restore(&s,out,sizeof(out)-1u,image,sizeof(image),s.image_crc32,owner));
    CHECK(!kui_cold_image_restore(&s,out,sizeof(out),image,sizeof(image)-1u,s.image_crc32,owner));
    CHECK(!kui_cold_image_restore(&s,image,sizeof(image),image,sizeof(image),s.image_crc32,owner));
    CHECK(!kui_cold_image_restore(&s,out,sizeof(out),image,sizeof(image),s.image_crc32,NULL));
    CHECK(!memcmp(out,saved,sizeof(out)));
    image[100]^=1u;unsigned before=owners;
    CHECK(!restore(&s));CHECK(owners==before);CHECK(!memcmp(out,saved,sizeof(out)));
    /* Independent Python/zlib-authored CRC collision: full SHA must still
     * reject it. A CRC-only preserved-image shortcut would accept this. */
    const uint8_t patch[4]={130u,38u,85u,117u};memcpy(image+1020u,patch,4u);
    CHECK(kui_retail_crc32(0,image,sizeof(image))==s.image_crc32);
    CHECK(!restore(&s));CHECK(owners==before+1u);CHECK(!memcmp(out,saved,sizeof(out)));
    uint8_t payload[160];memset(payload,0xcc,sizeof(payload));
    memcpy(payload+32u,&original,sizeof(original));
    CHECK(kui_cold_image_seed_find(payload,sizeof(payload))==32u);
    memcpy(payload+96u,&original,sizeof(original));CHECK(kui_cold_image_seed_find(payload,sizeof(payload))==SIZE_MAX);
    memset(payload+96u,0xcc,sizeof(original));memcpy(payload+32u,&s,sizeof(s));
    CHECK(kui_cold_image_seed_find(payload,sizeof(payload))==SIZE_MAX);
    memcpy(payload+32u,&original,sizeof(original));payload[48u]=1u;
    CHECK(kui_cold_image_seed_find(payload,sizeof(payload))==SIZE_MAX);
    CHECK(kui_cold_image_seed_find(NULL,99u)==SIZE_MAX);
    printf("Cold image replay: %u checks passed; typed capability, CRC collision refused by fresh complete SHA owner proof\n",checks);
    return 0;
}
