/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SMASH_CE_CHILD_H
#define KUI_SMASH_CE_CHILD_H
#include "kui/smash_menu_collection.h"
#define KUI_SMASH_CE_CHILD_MAGIC0 UINT32_C(0x5349554b)
#define KUI_SMASH_CE_CHILD_MAGIC1 UINT32_C(0x31454353)
#define KUI_SMASH_CE_CHILD_BYTES 64u
#define KUI_SMASH_CE_CHILD_CAPTURED 1u
#define KUI_SMASH_CE_SAMPLE_BYTES 204576u
#define KUI_SMASH_CE_SAMPLE_CRC32 UINT32_C(0x655ed733)
struct kui_smash_ce_child_seed {
    uint32_t magic0,magic1,version,bytes,profile,flags,reserved,crc32;
    uint8_t ip_tail[16];
    uint32_t reserved_tail[4];
};
_Static_assert(sizeof(struct kui_smash_ce_child_seed)==64u,"Smash CE child ABI");
bool kui_smash_ce_child_blank(const struct kui_smash_ce_child_seed *);
size_t kui_smash_ce_child_find(const void *,size_t,unsigned profile);
bool kui_smash_ce_child_capture(struct kui_smash_ce_child_seed *,
    const uint8_t argv[256],const uint8_t ip_tail[16]);
bool kui_smash_ce_child_valid(const struct kui_smash_ce_child_seed *);
bool kui_smash_ce_child_owner(uint32_t bytes,const uint8_t sha256[32]);
const char *kui_smash_ce_child_folder(unsigned profile);
const char *kui_smash_ce_child_bootfile(unsigned profile);
/* Capture only one of the two exact original parser/IP outputs. */
unsigned kui_smash_ce_child_identify(const uint8_t argv[256],const uint8_t ip_tail[16]);
#endif
