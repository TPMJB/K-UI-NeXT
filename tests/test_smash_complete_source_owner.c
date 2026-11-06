/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual stage dispatch/proof with a fixed-address SHA adapter for host RAM.
 * The SHA implementation is real; only its console source pointer is mapped
 * to the fixture, with an exact complete-byte assertion. */
#include "kui/hash.h"
#include "kui/retail_loader_layout.h"
#include "kui/smash_native_child.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static struct {uint32_t boot_bytes;} manifest;
static struct {uint32_t load_address,image_bytes;uint8_t image_sha256[32];} native_driver;
static uint32_t exec_bytes;
static uint8_t image[KUI_SMASH_NATIVE_ENGINE_BYTES],expected[32];
static unsigned checks,scopes,hashes,owners;static size_t hashed_bytes;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"line %u: %s\n",__LINE__,#x);exit(1);}}while(0)
void __real_kui_sha256_update(struct kui_sha256 *,const void *,size_t);
void __wrap_kui_sha256_update(struct kui_sha256 *hash,const void *data,size_t bytes) {
    if(data==(const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS) {
        CHECK(bytes==sizeof(image));++hashes;hashed_bytes+=bytes;data=image;
    }
    __real_kui_sha256_update(hash,data,bytes);
}
static bool test_owner(uint32_t bytes,const uint8_t sha[32]) {
    ++owners;return bytes==sizeof(image) && !memcmp(sha,expected,sizeof(expected));
}
#define kui_smash_native_child_engine_owner test_owner
#include "../src/loader/smash_source_owner_stage.inc"
#undef kui_smash_native_child_engine_owner
bool kui_smash_native_menu_cached_full_source_owner(void) {
    ++scopes;return kui_smash_native_menu_full_source_owner();
}
static void digest(size_t bytes,uint8_t out[32]) {
    struct kui_sha256 hash;kui_sha256_init(&hash);
    __real_kui_sha256_update(&hash,image,bytes);kui_sha256_digest(&hash,out);
}
static void setup(void) {
    manifest.boot_bytes=sizeof(image);exec_bytes=sizeof(image);
    native_driver.image_bytes=sizeof(image);native_driver.load_address=KUI_RETAIL_EXEC_ADDRESS;
    memcpy(native_driver.image_sha256,expected,32u);scopes=hashes=owners=0u;hashed_bytes=0u;
}
int main(void) {
    for(unsigned i=0u;i<sizeof(image);i++)image[i]=(uint8_t)(i*17u+11u);
    digest(sizeof(image),expected);setup();
    CHECK(smash_native_complete_source_owner());CHECK(!scopes && !hashes && owners==1u);
    native_driver.image_sha256[0]^=1u;CHECK(!smash_native_complete_source_owner());CHECK(!scopes && !hashes);
    setup();exec_bytes-=4096u;native_driver.image_bytes=exec_bytes;digest(exec_bytes,native_driver.image_sha256);
    CHECK(smash_native_complete_source_owner());CHECK(scopes==1u && hashes==1u && hashed_bytes==sizeof(image));
    /* A changed reclaimed suffix must fail even though its prefix proof is
     * unchanged and valid. This was the dangerous full-vs-prefix ambiguity. */
    image[sizeof(image)-1u]^=1u;
    CHECK(!smash_native_complete_source_owner());CHECK(scopes==2u && hashes==2u && hashed_bytes==2u*sizeof(image));
    image[sizeof(image)-1u]^=1u;
    for(unsigned bad=0u;bad<5u;bad++) {
        setup();
        if(bad==0u)--manifest.boot_bytes;
        if(bad==1u)native_driver.load_address+=4u;
        if(bad==2u)--native_driver.image_bytes;
        if(bad==3u)exec_bytes=0u;
        if(bad==4u)exec_bytes=native_driver.image_bytes=sizeof(image)+4u;
        CHECK(!smash_native_complete_source_owner());CHECK(!scopes && !hashes && !owners);
    }
    printf("Smash complete-source owner: %u checks passed; full-proof reuse, cached full-prefix fallback and corrupt suffix refusal\n",checks);
    return 0;
}
