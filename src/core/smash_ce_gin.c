/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_ce_gin.h"
#include "kui/retail_image.h"
#include <string.h>

static bool header(const struct kui_smash_ce_gin_seed *s) {
    if(!s || s->magic0!=KUI_SMASH_CE_GIN_MAGIC0 ||
       s->magic1!=KUI_SMASH_CE_GIN_MAGIC1 || s->version!=1u ||
       s->bytes!=sizeof(*s) || (s->profile!=1u && s->profile!=2u))return false;
    for(unsigned i=0u;i<12u;i++)if(s->reserved[i])return false;
    return true;
}
static bool frame_valid(const uint32_t f[21]) {
    if(!f || f[0]!=0x28u || f[1] || f[2]!=0x8c000000u ||
       f[3]!=0x8c00f400u || f[4]!=0x700000f0u || f[5])return false;
    /* GIN clears the shared R14..R8. Its RTE selects RB1, whose actual
     * R7..R0 are captured verbatim rather than inferred from RB0 writes. */
    for(unsigned i=6u;i<13u;i++)if(f[i])return false;
    return true;
}
static uint32_t checksum(const struct kui_smash_ce_gin_seed *s) {
    const uint32_t zero=0u;
    uint32_t sum=kui_retail_crc32(0u,s,offsetof(struct kui_smash_ce_gin_seed,crc32));
    sum=kui_retail_crc32(sum,&zero,sizeof(zero));
    return kui_retail_crc32(sum,s->reserved,sizeof(s->reserved));
}
bool kui_smash_ce_gin_blank(const struct kui_smash_ce_gin_seed *s) {
    if(!header(s) || s->ccr || s->crc32)return false;
    for(unsigned i=0u;i<21u;i++)if(s->frame[i])return false;
    return true;
}
bool kui_smash_ce_gin_capable(const void *prefix,size_t bytes,unsigned profile) {
    if(!prefix || bytes<KUI_SMASH_CE_GIN_OFFSET+sizeof(struct kui_smash_ce_gin_seed))return false;
    struct kui_smash_ce_gin_seed seed;
    memcpy(&seed,(const uint8_t *)prefix+KUI_SMASH_CE_GIN_OFFSET,sizeof(seed));
    return seed.profile==profile && kui_smash_ce_gin_blank(&seed);
}
bool kui_smash_ce_gin_capture(struct kui_smash_ce_gin_seed *s,
    const uint32_t frame[21],uint32_t address,uint32_t ccr) {
    /* Match the existing handoff's physical address and cache-index guard.
     * CCR is an actual hardware observation, not a guessed cache profile. */
    if(!kui_smash_ce_gin_blank(s) ||
       (address&0xdfffffffu)!=KUI_SMASH_CE_GIN_FRAME_P1 ||
       (ccr&0x80u) || !frame_valid(frame))return false;
    struct kui_smash_ce_gin_seed result=*s;
    result.ccr=ccr;memcpy(result.frame,frame,sizeof(result.frame));
    result.crc32=checksum(&result);*s=result;
    return true;
}
bool kui_smash_ce_gin_valid(const struct kui_smash_ce_gin_seed *s) {
    return header(s) && !(s->ccr&0x80u) && frame_valid(s->frame) &&
        s->crc32==checksum(s);
}
