/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_native_gin.h"
#include "kui/smash_native_child.h"
#include "kui/smash_native_menu.h"
#include "kui/hash.h"
#include "kui/retail_image.h"
#include "smash_native_gin_offsets.h"
#include <string.h>

/* This independently linked prefix executes before its original high MDE
 * source is reclaimed by the stage. The collection handoff authenticated the
 * complete capsule and captured the actual GIN CPU frame. No card or SDK
 * address is inferred here, and no live worker crosses an executable owner. */
uint32_t kui_smash_native_gin_verify(void) {
    const uint8_t *source=(const void *)(uintptr_t)KUI_SMASH_NATIVE_GIN_SOURCE_P1;
    uint8_t *loaded=(void *)(uintptr_t)UINT32_C(0x8c010000);
    const struct kui_smash_native_gin_seed *gin=(const void *)(uintptr_t)UINT32_C(0x8cc00200);
    const struct kui_smash_native_menu_seed *menu=(const void *)(uintptr_t)
        (UINT32_C(0x8cc00000)+KUI_SMASH_NATIVE_GIN_MENU_SEED_OFFSET);
    struct kui_smash_native_gin_transfer *saved=(void *)(uintptr_t)KUI_SMASH_NATIVE_GIN_TRANSFER_P1;
    static const uint8_t owner[32]={4,244,157,88,59,57,189,199,16,206,198,208,164,52,181,215,
        16,185,188,206,221,83,155,237,218,41,117,81,53,240,254,97};
    struct kui_sha256 hash;uint8_t digest[32];
    if(!kui_smash_native_gin_valid(gin))return 1u;
    uint32_t crc=kui_retail_crc32(0u,source,KUI_SMASH_NATIVE_ENGINE_BYTES);
    kui_sha256_init(&hash);kui_sha256_update(&hash,source,KUI_SMASH_NATIVE_ENGINE_BYTES);
    kui_sha256_digest(&hash,digest);
    if(crc!=KUI_SMASH_NATIVE_ENGINE_CRC32 || memcmp(owner,digest,sizeof(owner)))return 2u;
    const uint32_t at=KUI_SMASH_NATIVE_CONTEXT_OFFSET,n=KUI_SMASH_NATIVE_CONTEXT_BYTES;
    if(memcmp(source,loaded,at) || memcmp(source+at+n,loaded+at+n,KUI_SMASH_NATIVE_ENGINE_BYTES-at-n))return 3u;
    const uint8_t *live=loaded+at,*original=source+at;
    const uint32_t *tuple=(const void *)(live+0x10u);
    /* Exact stock-row admission follows in highstage begin before card I/O
     * or native hooks. Here bind the actual GIN copy and every immutable
     * context byte, accepting only the two original-menu writable spans. */
    if(tuple[0]!=KUI_SMASH_NATIVE_GIN_SOURCE_P1 || tuple[1]!=903u || tuple[2]!=4u || tuple[3]!=1u ||
       memcmp(live,(const void *)(uintptr_t)UINT32_C(0x8cfd0000),n) ||
       memcmp(live,original,0x10u) || memcmp(live+0x20u,original+0x20u,0xa0u) ||
       memcmp(live+0x200u,original+0x200u,n-0x200u) ||
       memcmp(live+0x100u,menu->argv,sizeof(menu->argv)))return 4u;
    saved->magic0=KUI_SMASH_NATIVE_GIN_TRANSFER_MAGIC0;
    saved->magic1=KUI_SMASH_NATIVE_GIN_TRANSFER_MAGIC1;
    saved->version=1u;saved->bytes=sizeof(*saved);
    memcpy(&saved->gin,gin,sizeof(*gin));
    memcpy(saved->context,loaded+at,n);
    memcpy(saved->ip,(const void *)(uintptr_t)UINT32_C(0x8c008000),sizeof(saved->ip));
    saved->crc32=kui_retail_crc32(0u,saved,offsetof(struct kui_smash_native_gin_transfer,crc32));
    if(!kui_smash_native_gin_normalize(source+at,loaded+at))return 5u;
    /* The following stage enables a fresh cache profile. Publish every
     * intentional write before its cache reset; scratch stack is retired. */
    for(uint32_t p=KUI_SMASH_NATIVE_GIN_TRANSFER_P1;p<KUI_SMASH_NATIVE_GIN_TRANSFER_P1+sizeof(*saved);p+=32u)
        __asm__ volatile("ocbwb @%0"::"r"(p):"memory");
    for(uint32_t p=(UINT32_C(0x8c010000)+at)&~31u;p<UINT32_C(0x8c010000)+at+n;p+=32u)
        __asm__ volatile("ocbwb @%0"::"r"(p):"memory");
    return 0u;
}
