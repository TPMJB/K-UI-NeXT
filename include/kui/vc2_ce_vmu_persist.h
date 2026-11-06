/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VC2_CE_VMU_PERSIST_H
#define KUI_VC2_CE_VMU_PERSIST_H
#include "kui/vmu_durable.h"
#include "kui/retail_image.h"
#include "kui/vc2_ce_vmu_late_seed.h"
#include "kui/vmu_game_card.h"
#define KUI_VC2_PERSIST_HELPER_BYTES 49152u
#define KUI_VC2_PERSIST_HELPER_BLOCKS 96u
#define KUI_VC2_PERSIST_CONFIG_BYTES 1536u
#define KUI_VC2_PERSIST_CONFIG_BLOCKS 3u
#define KUI_VC2_PERSIST_CAPSULE_BYTES 50688u
#define KUI_VC2_PERSIST_CAPSULE_BLOCKS 99u
#define KUI_VC2_PERSIST_CONFIG_MAGIC UINT32_C(0x31505643)
#define KUI_VC2_PERSIST_CONFIG_VERSION 1u
#define KUI_VC2_PERSIST_CAPSULE_PATH "0:/KUI/apps/games/vc2-vmu-session.bin"
#define KUI_VC2_PERSIST_PRODUCT KUI_VMU_SMASHPACK_KEY
struct kui_vc2_ce_vmu_persist_config {
    uint32_t magic,version,bytes,handoff_bytes;
    struct kui_vmu_durable_handoff handoff;
    uint8_t reserved[244];
    uint32_t crc32;
};
_Static_assert(sizeof(struct kui_vc2_ce_vmu_persist_config)==1536u,"VC2 late config ABI");
_Static_assert(offsetof(struct kui_vc2_ce_vmu_persist_config,crc32)==1532u,"VC2 config CRC ABI");
static inline uint32_t kui_vc2_ce_vmu_persist_config_crc(
    const struct kui_vc2_ce_vmu_persist_config *c) {
    const uint32_t zero=0u;
    return kui_retail_crc32(kui_retail_crc32(0u,c,1532u),&zero,4u);
}
static inline bool kui_vc2_ce_vmu_persist_config_valid(
    const struct kui_vc2_ce_vmu_persist_config *c) {
    if(!c || c->magic!=KUI_VC2_PERSIST_CONFIG_MAGIC ||
       c->version!=KUI_VC2_PERSIST_CONFIG_VERSION || c->bytes!=sizeof(*c) ||
       c->handoff_bytes!=sizeof(c->handoff) ||
       kui_vmu_durable_handoff_validate(&c->handoff) ||
       c->crc32!=kui_vc2_ce_vmu_persist_config_crc(c))return false;
    for(unsigned i=0u;i<sizeof(c->reserved);i++)if(c->reserved[i])return false;
    return true;
}
/* The source seed maps the entire99-block capsule. Both readers choose the
 * same one/two physical runs without changing the game's own map or TOC. */
static inline uint32_t kui_vc2_ce_vmu_capsule_lba(
    const struct kui_vc2_vmu_late_seed *s,uint32_t block) {
    return block<s->first_blocks?s->first_lba+block:
        s->second_lba+block-s->first_blocks;
}
#endif
