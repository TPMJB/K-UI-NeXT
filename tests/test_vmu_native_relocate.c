/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_native_relocate.h"
#include "kui/hash.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do {++checks;assert(x);} while(0)
static uint8_t image[33],wire[104],output[128];
static const uint32_t preferred=0x8cfd0000u,load=0x8c38a880u;
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void put32(uint8_t *p,uint32_t v) {
    for(unsigned i=0;i<4u;i++) p[i]=(uint8_t)(v>>(8u*i));
}
static void checksum(void) {
    put32(wire+36u,0u);put32(wire+36u,kui_crc32(0,wire,sizeof(wire)));
}
static void fixture(void) {
    memset(image,0x5a,sizeof(image));memset(wire,0,sizeof(wire));memset(output,0xa5,sizeof(output));
    put32(image,0x8cfd0000u);put32(image+4u,0xacfd0020u);
    put32(image+8u,0x0cfd003eu);put32(image+12u,0xacfd0040u);
    put32(image+16u,0xa05f6c00u);
    memcpy(wire,"KUIVRL01",8u);put32(wire+8u,1u);put32(wire+12u,72u);
    put32(wire+16u,sizeof(image));put32(wire+20u,64u);put32(wire+24u,preferred);
    put32(wire+28u,4u);put32(wire+32u,sizeof(wire));
    struct kui_sha256 sha;kui_sha256_init(&sha);kui_sha256_update(&sha,image,sizeof(image));
    kui_sha256_digest(&sha,wire+40u);
    for(unsigned i=0;i<4u;i++) {put32(wire+72u+8u*i,4u*i);put32(wire+76u+8u*i,get32(image+4u*i));}
    checksum();
}
static int relocate(void) {
    return kui_vmu_native_relocate(image,sizeof(image),wire,sizeof(wire),output,sizeof(output),load);
}
static void unchanged(void) {
    for(size_t i=0;i<sizeof(output);i++) CHECK(output[i]==0xa5u);
}
static void refused(void) {CHECK(relocate()!=KUI_VMU_NATIVE_RELOCATED);unchanged();}
int main(void) {
    static uint8_t large_output[KUI_VMU_NATIVE_RELOCATE_MAX_SPAN];
    fixture();CHECK(relocate()==KUI_VMU_NATIVE_RELOCATED);
    CHECK(get32(output)==load);CHECK(get32(output+4u)==0xac38a8a0u);
    CHECK(get32(output+8u)==0x0c38a8beu);CHECK(get32(output+12u)==0xac38a8c0u);
    CHECK(get32(output+16u)==0xa05f6c00u);CHECK(!memcmp(output+16u,image+16u,sizeof(image)-16u));
    for(size_t i=sizeof(image);i<sizeof(output);i++) CHECK(output[i]==0xa5u);
    for(size_t i=0;i<sizeof(wire);i++) for(unsigned bit=0;bit<8u;bit++) {
        fixture();wire[i]^=(uint8_t)(1u<<bit);refused();
    }
    for(size_t i=0;i<sizeof(image);i++) {
        fixture();image[i]^=1u;CHECK(relocate()==KUI_VMU_NATIVE_RELOCATE_IMAGE);unchanged();
    }
    const uint32_t bad_offsets[]={1u,2u,0xfffffffcu,32u,0u};
    for(unsigned i=0;i<sizeof(bad_offsets)/sizeof(bad_offsets[0]);i++) {
        fixture();put32(wire+80u,bad_offsets[i]);checksum();refused();
    }
    fixture();put32(wire+76u,0xa05f6c00u);checksum();refused();
    fixture();put32(wire+76u,0x8cfcffffu);checksum();refused();
    fixture();put32(wire+76u,0x8cfd0041u);checksum();refused();
    fixture();put32(wire+76u,0x8cfd0004u);checksum();refused();
    fixture();put32(wire+20u,0x10020u);checksum();refused();
    fixture();put32(wire+28u,3u);put32(wire+32u,96u);put32(wire+36u,0u);
    put32(wire+36u,kui_crc32(0,wire,96u));
    CHECK(kui_vmu_native_relocate(image,sizeof(image),wire,96u,output,sizeof(output),load)==KUI_VMU_NATIVE_RELOCATE_TABLE);unchanged();
    fixture();CHECK(kui_vmu_native_relocate(image,sizeof(image),wire,sizeof(wire),output,63u,load)==KUI_VMU_NATIVE_RELOCATE_INVALID);unchanged();
    CHECK(kui_vmu_native_relocate(image,sizeof(image),wire,sizeof(wire),output,sizeof(output),load+4u)==KUI_VMU_NATIVE_RELOCATE_INVALID);unchanged();
    CHECK(kui_vmu_native_relocate(image,sizeof(image),wire,sizeof(wire),output,sizeof(output),0x8cffffe0u)==KUI_VMU_NATIVE_RELOCATE_INVALID);unchanged();
    CHECK(kui_vmu_native_relocate(image,sizeof(image),wire,sizeof(wire),output,sizeof(output),0xac38a880u)==KUI_VMU_NATIVE_RELOCATE_INVALID);unchanged();
    CHECK(kui_vmu_native_relocate(NULL,sizeof(image),wire,sizeof(wire),output,sizeof(output),load)==KUI_VMU_NATIVE_RELOCATE_INVALID);unchanged();
    CHECK(kui_vmu_native_relocate(image,sizeof(image),wire,sizeof(wire),image,sizeof(image),load)==KUI_VMU_NATIVE_RELOCATE_OVERLAP);
    uint8_t oldwire[sizeof(wire)];memcpy(oldwire,wire,sizeof(wire));
    CHECK(kui_vmu_native_relocate(image,sizeof(image),wire,sizeof(wire),wire,sizeof(wire),load)==KUI_VMU_NATIVE_RELOCATE_OVERLAP);
    CHECK(!memcmp(oldwire,wire,sizeof(wire)));
    const uintptr_t aliases[]={0u,0x80000000u,0xa0000000u};
    for(unsigned a=0;a<3u;a++) for(unsigned b=0;b<3u;b++) {
        CHECK(kui_vmu_native_relocate((const void *)(aliases[a]|0x0c010000u),33u,
            wire,sizeof(wire),(void *)(aliases[b]|0x0c010020u),64u,load)==KUI_VMU_NATIVE_RELOCATE_OVERLAP);
        CHECK(kui_vmu_native_relocate(image,sizeof(image),(const void *)(aliases[a]|0x0c010000u),
            sizeof(wire),(void *)(aliases[b]|0x0c00ffe0u),64u,load)==KUI_VMU_NATIVE_RELOCATE_OVERLAP);
    }
    /* The native persistent worker uses128KiB of its192KiB lease. The
     * relocator still requires the complete output allocation and RAM bound. */
    const uint32_t spans[]={0x10000u,0x20000u};
    for(unsigned i=0;i<2u;i++) {
        fixture();put32(wire+20u,spans[i]);checksum();memset(large_output,0xa5,sizeof(large_output));
        CHECK(kui_vmu_native_relocate(image,sizeof(image),wire,sizeof(wire),large_output,
            spans[i],load)==KUI_VMU_NATIVE_RELOCATED);
        CHECK(get32(large_output)==load && get32(large_output+4u)==0xac38a8a0u);
        for(size_t at=sizeof(image);at<sizeof(large_output);at++) CHECK(large_output[at]==0xa5u);
        memset(large_output,0xa5,sizeof(large_output));
        CHECK(kui_vmu_native_relocate(image,sizeof(image),wire,sizeof(wire),large_output,
            spans[i]-32u,load)==KUI_VMU_NATIVE_RELOCATE_INVALID);
        for(size_t at=0;at<sizeof(large_output);at++) CHECK(large_output[at]==0xa5u);
    }
    fixture();put32(wire+20u,0x20020u);checksum();
    CHECK(kui_vmu_native_relocate(image,sizeof(image),wire,sizeof(wire),large_output,
        sizeof(large_output),load)==KUI_VMU_NATIVE_RELOCATE_TABLE);
    for(size_t at=0;at<sizeof(large_output);at++) CHECK(large_output[at]==0xa5u);
    printf("Native worker relocation: %u checks passed; no hook authority\n",checks);
    return 0;
}
