/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SMASH_CARD_SESSION_H
#define KUI_SMASH_CARD_SESSION_H
#define KUI_SMASH_CARD_RECEIPT_OFFSET 0x2a0
#define KUI_SMASH_CARD_RECEIPT_BYTES 96
#define KUI_SMASH_CARD_RECEIPT_VERSION 2
#define KUI_SMASH_CARD_CONFIG_BYTES 4224
#ifndef KUI_SMASH_CARD_SESSION
#define KUI_SMASH_CARD_SESSION 0
#endif
#ifndef __ASSEMBLER__
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "kui/smash_menu_capsule.h"
#include "kui/vmu_fs_witness.h"
#include "kui/vmu_persist_profile.h"
#define KUI_SMASH_CARD_WITNESS_PATH "0:/KUI/apps/games/smash-card-session.bin"
#define KUI_SMASH_CARD_CONFIG_MAGIC0 UINT32_C(0x5349554b)
#define KUI_SMASH_CARD_CONFIG_MAGIC1 UINT32_C(0x31304643)
#define KUI_SMASH_CARD_RECEIPT_MAGIC0 UINT32_C(0x5349554b)
#define KUI_SMASH_CARD_RECEIPT_MAGIC1 UINT32_C(0x32305243)
#define KUI_SMASH_CARD_RECEIPT_VALID 1u
#define KUI_SMASH_CARD_RECEIPT_OWNER_SHIFT 8u
#define KUI_SMASH_CARD_ROLE_OUTER 1u
#define KUI_SMASH_CARD_ROLE_NATIVE 2u
#define KUI_SMASH_CARD_ROLE_VC2 3u
#define KUI_SMASH_CARD_ROLE_SWIRL 4u
/* Fixed little-endian SH/host wire. Only outer carries profile_wire; child
 * records leave it zero so existing unique P2 profile discovery is unchanged.
 * GUI patches the immutable config before it seals each module capsule. */
struct kui_smash_card_config {
    uint32_t magic0,magic1,version,bytes,role,generation,witness_bytes,crc32;
    struct kui_smash_menu_capsule_runs data,allocated;
    uint8_t ownership_sha256[32];
    char build[12];uint32_t reserved0;
    uint8_t profile_wire[4096];
    uint32_t reserved[4];
};
/* Live RAM authority only. Persisted bytes never grant it. Version2 carries
 * the complete admission digest and the current owner in flags bits8..15.
 * Mint only follows outer admission; transfer follows owner quiescence. */
struct kui_smash_card_receipt {
    uint32_t magic0,magic1,version,bytes,flags,generation;
    uint8_t binding_sha256[32],admission_sha256[32];
    uint32_t sequence,crc32;
};
_Static_assert(sizeof(struct kui_smash_card_config)==4224u,"Smash card config ABI");
_Static_assert(offsetof(struct kui_smash_card_config,profile_wire)==112u,"Smash outer profile ABI");
_Static_assert(sizeof(struct kui_smash_card_receipt)==96u,"Smash live card receipt ABI");
size_t kui_smash_card_config_find(const void *,size_t);
bool kui_smash_card_config_blank(const struct kui_smash_card_config *);
bool kui_smash_card_config_valid(const struct kui_smash_card_config *);
bool kui_smash_card_configure(struct kui_smash_card_config *,unsigned role,uint32_t generation,
    const char build[13],const struct kui_smash_menu_capsule_runs *data,
    const struct kui_smash_menu_capsule_runs *allocated,const struct kui_vmu_persist_profile *);
bool kui_smash_card_binding(const struct kui_smash_card_config *,
    const struct kui_vmu_persist_profile *,struct kui_vmu_fs_witness_binding *);
bool kui_smash_card_receipt_blank(const struct kui_smash_card_receipt *);
bool kui_smash_card_receipt_valid(const struct kui_smash_card_receipt *);
unsigned kui_smash_card_receipt_owner(const struct kui_smash_card_receipt *);
bool kui_smash_card_receipt_transition_valid(const struct kui_smash_card_receipt *,unsigned next_role);
bool kui_smash_card_receipt_mint(struct kui_smash_card_receipt *,
    const struct kui_vmu_fs_witness_binding *,const uint8_t admission_sha256[32]);
bool kui_smash_card_receipt_transfer(struct kui_smash_card_receipt *,unsigned next_role);
/* Separate terminal CE-return edge. The caller must validate the captured
 * ROOT return context before storage and the current CID/binding before
 * advancing. Only the checked controller may publish this live receipt:
 * its helper/GD idle fence and permanent no-resume retire the old CE owner.
 * Generic child transfer intentionally continues to reject CE -> outer. */
bool kui_smash_card_receipt_ce_return_valid(const struct kui_smash_card_receipt *);
bool kui_smash_card_receipt_ce_return(struct kui_smash_card_receipt *);
bool kui_smash_card_receipt_matches(const struct kui_smash_card_receipt *,
    const struct kui_vmu_fs_witness_binding *);
#endif
#endif
