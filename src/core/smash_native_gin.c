/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_native_gin.h"
#include "kui/smash_native_menu.h"
#include "kui/smash_native_child.h"
#include "kui/retail_image.h"
#include <string.h>

/* Exact 64-byte sources copied by the original menu at 8c015eac. Its
 * 1ST_READ.BIN SHA256 is 3a311b189a4870ccbefc4fcd306e2d38e5ae810ae5edd225af6b9aa6d5df5f10.
 * These are command-table bytes, including the next row's prefix, not ROMs.
 * Keep this table confined to context_matches(), independently collectible
 * from the small continuation/canonical-normalization verifier. */
/* The extra byte stores the C literal's terminator; compare exactly 64. */
static const uint8_t folder_rows[KUI_SMASH_NATIVE_MENU_SELECTIONS][65]={
    "ALTBEAST\0\\MDE_US.BIN ALTBEAST.SGA MODE2 SKIP0 SOUND0\0COLUMNS \0\\M",
    "COLUMNS \0\\MDE_US.BIN COLUMNS.SGA  MODE0 SKIP0 SOUND0\0GOLDAXE \0\\M",
    "GOLDAXE \0\\MDE_US.BIN GOLDAXE.SGA  MODE0 SKIP0 SOUND0\0PHSTAR2 \0\\M",
    "PHSTAR2 \0\\MDE_US.BIN PHSTAR2.SGA  MODE0 SKIP0 SOUND0\0REVSHIN \0\\M",
    "REVSHIN \0\\MDE_US.BIN REVSHIN.SGA  MODE0 SKIP1 SOUND0\0SHFORCE \0\\M",
    "SHFORCE \0\\MDE_US.BIN SHFORCE.SGA  MODE0 SKIP0 SOUND1\0SONIC1  \0\\M",
    "SONIC1  \0\\MDE_US.BIN SONIC1.SGA   MODE0 SKIP1 SOUND0\0STRAGE2 \0\\M",
    "STRAGE2 \0\\MDE_US.BIN STRAGE2.SGA  MODE0 SKIP1 SOUND0\0VECTORM \0\\M",
    "VECTORM \0\\MDE_US.BIN VECTORM.SGA  MODE4 SKIP2 SOUND0\0WRESTWAR\0\\M",
    "WRESTWAR\0\\MDE_US.BIN WRESTWAR.SGA MODE0 SKIP0 SOUND0\0VIRTCOP2\0\\V"
};
static bool span(const void *p,size_t bytes) {
    return p && (uintptr_t)p<=UINTPTR_MAX-bytes;
}
static bool disjoint(const void *a,size_t an,const void *b,size_t bn) {
    return span(a,an) && span(b,bn) &&
        ((uintptr_t)a+an<=(uintptr_t)b || (uintptr_t)b+bn<=(uintptr_t)a);
}
static bool canonical(const void *source) {
    return span(source,KUI_SMASH_NATIVE_CONTEXT_BYTES) &&
        kui_retail_crc32(0u,source,KUI_SMASH_NATIVE_CONTEXT_BYTES)==KUI_SMASH_NATIVE_CONTEXT_CRC32;
}
static bool header(const struct kui_smash_native_gin_seed *s) {
    if(!s || s->magic0!=KUI_SMASH_NATIVE_GIN_MAGIC0 ||
       s->magic1!=KUI_SMASH_NATIVE_GIN_MAGIC1 || s->version!=1u ||
       s->bytes!=sizeof(*s) || s->profile!=KUI_SMASH_NATIVE_GIN_PROFILE)return false;
    for(unsigned i=0u;i<12u;i++)if(s->reserved[i])return false;
    return true;
}
static bool frame_valid(const uint32_t f[21]) {
    if(!f || f[0]!=0x28u || f[1] || f[2]!=0x8c000000u ||
       f[3]!=0x8c00f400u || f[4]!=0x700000f0u || f[5])return false;
    /* RTE chooses RB1. Capture its actual R7..R0 rather than synthesizing
     * them from GIN's preceding RB0 writes. R14..R8 are shared and cleared. */
    for(unsigned i=6u;i<13u;i++)if(f[i])return false;
    return true;
}
static uint32_t checksum(const struct kui_smash_native_gin_seed *s) {
    const uint32_t zero=0u;
    uint32_t sum=kui_retail_crc32(0u,s,offsetof(struct kui_smash_native_gin_seed,crc32));
    sum=kui_retail_crc32(sum,&zero,sizeof(zero));
    return kui_retail_crc32(sum,s->reserved,sizeof(s->reserved));
}
bool kui_smash_native_gin_blank(const struct kui_smash_native_gin_seed *s) {
    if(!header(s) || s->ccr || s->crc32)return false;
    for(unsigned i=0u;i<21u;i++)if(s->frame[i])return false;
    return true;
}
bool kui_smash_native_gin_capable(const void *prefix,size_t bytes) {
    if(!span(prefix,bytes) || bytes<KUI_SMASH_NATIVE_GIN_OFFSET+sizeof(struct kui_smash_native_gin_seed))return false;
    struct kui_smash_native_gin_seed seed;
    memcpy(&seed,(const uint8_t *)prefix+KUI_SMASH_NATIVE_GIN_OFFSET,sizeof(seed));
    return kui_smash_native_gin_blank(&seed);
}
bool kui_smash_native_gin_capture(struct kui_smash_native_gin_seed *s,
    const uint32_t frame[21],uint32_t address,uint32_t ccr) {
    if(!kui_smash_native_gin_blank(s) ||
       (address&0xdfffffffu)!=KUI_SMASH_NATIVE_GIN_FRAME_P1 ||
       (ccr&0x80u) || !frame_valid(frame))return false;
    struct kui_smash_native_gin_seed result=*s;
    result.ccr=ccr;memcpy(result.frame,frame,sizeof(result.frame));
    result.crc32=checksum(&result);*s=result;return true;
}
bool kui_smash_native_gin_valid(const struct kui_smash_native_gin_seed *s) {
    return header(s) && !(s->ccr&0x80u) && frame_valid(s->frame) &&
        s->crc32==checksum(s);
}
bool kui_smash_native_gin_context_matches(const void *source,const void *loaded,
    const void *gin_context,const struct kui_smash_native_menu_seed *menu) {
    if(!canonical(source) || !span(loaded,KUI_SMASH_NATIVE_CONTEXT_BYTES) ||
       !span(gin_context,KUI_SMASH_NATIVE_CONTEXT_BYTES) ||
       !kui_smash_native_menu_seed_valid(menu))return false;
    const uint8_t *original=source,*live=loaded;
    uint32_t transfer[4];memcpy(transfer,live+0x10u,sizeof(transfer));
    if(transfer[0]!=KUI_SMASH_NATIVE_GIN_SOURCE_P1 || transfer[1]!=903u ||
       transfer[2]!=4u || transfer[3]!=1u ||
       memcmp(live,gin_context,KUI_SMASH_NATIVE_CONTEXT_BYTES) ||
       memcmp(live,original,0x10u) ||
       memcmp(live+0x20u,original+0x20u,0xa0u) ||
       memcmp(live+0x200u,original+0x200u,KUI_SMASH_NATIVE_CONTEXT_BYTES-0x200u) ||
       memcmp(live+0xc0u,folder_rows[menu->selection-1u],64u) ||
       memcmp(live+0x100u,menu->argv,sizeof(menu->argv)))return false;
    return true;
}
bool kui_smash_native_gin_normalize(const void *source,void *loaded) {
    if(!disjoint(source,KUI_SMASH_NATIVE_CONTEXT_BYTES,loaded,KUI_SMASH_NATIVE_CONTEXT_BYTES) ||
       !canonical(source))return false;
    const uint8_t *original=source;uint8_t *live=loaded;
    memcpy(live+0x10u,original+0x10u,0x10u);
    memcpy(live+0xc0u,original+0xc0u,0x140u);return true;
}
