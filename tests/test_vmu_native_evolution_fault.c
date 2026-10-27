/* SPDX-License-Identifier: GPL-3.0-only */
/* External original executable proof; no retail file is modified or embedded.
 * cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -Iinclude \
 *   tests/test_vmu_native_evolution_fault.c src/core/vmu_native_discovery.c \
 *   src/core/vmu_native_patch.c src/core/hash.c src/core/data.c -o build/test-evolution-fault
 * ASAN_OPTIONS=detect_leaks=0 build/test-evolution-fault /path/to/1ST_READ.BIN
 */
#include "kui/vmu_native_discovery.h"
#include "kui/hash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/loader/vmu_native_evolution_fault_stage.inc"

static unsigned checks,published;
static bool corrupt_publication;
static uint8_t *live;
static size_t image_bytes;
#define CHECK(x) do {++checks;if(!(x)) {fprintf(stderr,"Evolution fault proof failed at %u\n",__LINE__);exit(1);}} while(0)
#define OFFSET (KUI_NATIVE_EVOLUTION_FAULT_ENTRY-KUI_NATIVE_EVOLUTION_FAULT_LOAD)
#define TARGET UINT32_C(0x8cfd0100)

static void publish(uint32_t begin,uint32_t end) {
    CHECK(begin==KUI_NATIVE_EVOLUTION_FAULT_ENTRY && end==begin+12u);
    ++published;
    if(corrupt_publication) live[OFFSET]^=1u;
}
static int install(const struct kui_vmu_native_discovery *driver) {
    return native_evolution_fault_install(driver,live,image_bytes,
        KUI_NATIVE_EVOLUTION_FAULT_LOAD,TARGET,publish);
}
static void untouched(const uint8_t *before,unsigned calls) {
    CHECK(published==calls);CHECK(!memcmp(live,before,image_bytes));
}
static void word(uint8_t *at,uint32_t value) {
    for(unsigned i=0;i<4u;i++) at[i]=(uint8_t)(value>>(8u*i));
}

int main(int argc,char **argv) {
    CHECK(argc==2);
    FILE *input=fopen(argv[1],"rb");CHECK(input);
    CHECK(!fseek(input,0,SEEK_END));long size=ftell(input);
    CHECK(size==(long)KUI_NATIVE_EVOLUTION_FAULT_IMAGE_BYTES);rewind(input);
    image_bytes=(size_t)size;live=malloc(image_bytes);uint8_t *original=malloc(image_bytes);
    uint8_t *before=malloc(image_bytes);CHECK(live && original && before);
    CHECK(fread(original,1,image_bytes,input)==image_bytes);CHECK(!fclose(input));
    struct kui_vmu_native_discovery driver;
    CHECK(kui_vmu_native_discover(original,image_bytes,KUI_NATIVE_EVOLUTION_FAULT_LOAD,&driver)
        ==KUI_VMU_NATIVE_RESOLVED);
    memcpy(live,original,image_bytes);
    CHECK(install(&driver)==1);CHECK(published==1u);
    uint16_t patch[6];CHECK(kui_vmu_native_patch_encode(KUI_NATIVE_EVOLUTION_FAULT_ENTRY,TARGET,patch));
    CHECK(!memcmp(live+OFFSET,patch,sizeof(patch)));
    CHECK(!memcmp(live,original,OFFSET));
    CHECK(!memcmp(live+OFFSET+12u,original+OFFSET+12u,image_bytes-OFFSET-12u));
    /* The successful allocation path and original fatal self-loop remain
     * byte-identical; only the preceding null-allocation arm is instrumented. */
    CHECK(!memcmp(live+(UINT32_C(0x8c04317c)-KUI_NATIVE_EVOLUTION_FAULT_LOAD),
        original+(UINT32_C(0x8c04317c)-KUI_NATIVE_EVOLUTION_FAULT_LOAD),12u));
    CHECK(!memcmp(live+OFFSET+12u,original+OFFSET+12u,4u));

    /* Every bit of the exact twelve-byte window is independently required. */
    for(unsigned at=0;at<12u;at++) for(unsigned bit=0;bit<8u;bit++) {
        memcpy(live,original,image_bytes);live[OFFSET+at]^=(uint8_t)(1u<<bit);
        memcpy(before,live,image_bytes);unsigned calls=published;
        CHECK(install(&driver)==-1);untouched(before,calls);
    }
    /* A different immutable identity never arms this diagnostic. */
    for(unsigned at=0;at<32u;at++) for(unsigned bit=0;bit<8u;bit++) {
        memcpy(live,original,image_bytes);struct kui_vmu_native_discovery changed=driver;
        changed.image_sha256[at]^=(uint8_t)(1u<<bit);unsigned calls=published;
        CHECK(install(&changed)==0);untouched(original,calls);
    }
    /* Bind an actual changed executable independently rather than lending it
     * the accepted executable's digest. Normal discovery remains unchanged. */
    memcpy(live,original,image_bytes);live[0]^=1u;
    struct kui_vmu_native_discovery other;
    CHECK(kui_vmu_native_discover(live,image_bytes,KUI_NATIVE_EVOLUTION_FAULT_LOAD,&other)
        ==KUI_VMU_NATIVE_RESOLVED);
    memcpy(before,live,image_bytes);unsigned calls=published;
    CHECK(install(&other)==0);untouched(before,calls);

    /* Selected identity still needs all current admission metadata. */
    for(unsigned field=0;field<6u;field++) {
        memcpy(live,original,image_bytes);struct kui_vmu_native_discovery changed=driver;
        ((uint32_t *)(void *)&changed)[field]^=1u;calls=published;
        CHECK(install(&changed)==-1);untouched(original,calls);
    }
    const size_t wrong_bytes[]={0u,1u,KUI_NATIVE_EVOLUTION_FAULT_IMAGE_BYTES-1u,
        KUI_NATIVE_EVOLUTION_FAULT_IMAGE_BYTES+1u,SIZE_MAX};
    for(unsigned i=0;i<sizeof(wrong_bytes)/sizeof(wrong_bytes[0]);i++) {
        memcpy(live,original,image_bytes);calls=published;
        CHECK(native_evolution_fault_install(&driver,live,wrong_bytes[i],
            KUI_NATIVE_EVOLUTION_FAULT_LOAD,TARGET,publish)==-1);untouched(original,calls);
    }
    const uint32_t bad_targets[]={0u,UINT32_C(0x0cfd0100),UINT32_C(0xccfd0100),
        TARGET+1u,UINT32_C(0xad000000),UINT32_MAX};
    for(unsigned i=0;i<sizeof(bad_targets)/sizeof(bad_targets[0]);i++) {
        memcpy(live,original,image_bytes);calls=published;
        CHECK(native_evolution_fault_install(&driver,live,image_bytes,
            KUI_NATIVE_EVOLUTION_FAULT_LOAD,bad_targets[i],publish)==-1);untouched(original,calls);
    }
    memcpy(live,original,image_bytes);calls=published;
    CHECK(native_evolution_fault_install(NULL,live,image_bytes,KUI_NATIVE_EVOLUTION_FAULT_LOAD,TARGET,publish)==-1);
    CHECK(native_evolution_fault_install(&driver,NULL,image_bytes,KUI_NATIVE_EVOLUTION_FAULT_LOAD,TARGET,publish)==-1);
    CHECK(native_evolution_fault_install(&driver,live,image_bytes,KUI_NATIVE_EVOLUTION_FAULT_LOAD,TARGET,NULL)==-1);
    CHECK(native_evolution_fault_install(&driver,live,image_bytes,KUI_NATIVE_EVOLUTION_FAULT_LOAD+2u,TARGET,publish)==-1);
    untouched(original,calls);

    /* Generic control-flow admission remains in force for this extra window. */
    memcpy(live,original,image_bytes);
    const uint32_t branch_offset=OFFSET+0x100u;
    int displacement=((int)OFFSET+2-(int)branch_offset-4)/2;
    uint16_t branch=UINT16_C(0xa000)|((uint16_t)displacement&UINT16_C(0x0fff));
    live[branch_offset]=(uint8_t)branch;live[branch_offset+1u]=(uint8_t)(branch>>8);
    memcpy(before,live,image_bytes);calls=published;
    CHECK(install(&driver)==-1);untouched(before,calls);
    memcpy(live,original,image_bytes);word(live,KUI_NATIVE_EVOLUTION_FAULT_ENTRY+2u);
    memcpy(before,live,image_bytes);calls=published;
    CHECK(install(&driver)==-1);untouched(before,calls);

    /* Publication/readback failure is explicit; it cannot be called success. */
    memcpy(live,original,image_bytes);corrupt_publication=true;calls=published;
    CHECK(install(&driver)==-2);CHECK(published==calls+1u);corrupt_publication=false;
    memcpy(live,original,image_bytes);
    CHECK(native_evolution_fault_install(&driver,live,image_bytes,KUI_NATIVE_EVOLUTION_FAULT_LOAD,
        TARGET+UINT32_C(0x20000000),publish)==1);
    CHECK(kui_vmu_native_patch_encode(KUI_NATIVE_EVOLUTION_FAULT_ENTRY,TARGET+UINT32_C(0x20000000),patch));
    CHECK(!memcmp(live+OFFSET,patch,sizeof(patch)));
    printf("Evolution exact-revision failure-only instrumentation: %u checks passed\n",checks);
    free(before);free(original);free(live);return 0;
}
