/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SMASH_NATIVE_COMBO_H
#define KUI_SMASH_NATIVE_COMBO_H
#include "kui/vmu_native_patch.h"
#define KUI_SMASH_NATIVE_COMBO_BRANCH UINT32_C(0x8c01048e)
#define KUI_SMASH_NATIVE_COMBO_ORIGINAL UINT16_C(0xa1f8)
#define KUI_SMASH_NATIVE_COMBO_DIRECT UINT16_C(0xa1f4)
#define KUI_SMASH_NATIVE_COMBO_CALL UINT32_C(0x8c01087a)
#define KUI_SMASH_NATIVE_COMBO_RESET UINT32_C(0x8c0117f0)
struct kui_smash_native_combo_plan {
    uint32_t address,call,reset;
    uint16_t original,replacement;
};
_Static_assert(sizeof(struct kui_smash_native_combo_plan)==16u,"Smash combo plan ABI");
/* The caller has JUST authenticated the whole immutable MDE and supplies its
 * fresh digest. This pure helper grants no authority from serialized metadata
 * or a recognizable branch. Prove the original conditional combo edge, its
 * untouched delay slot and existing near reset call. The two-byte patch must
 * be disjoint from every independently installed SDK/hook witness window.
 * No original instruction is displaced and all non-combo edges are retained.
 * False leaves out unchanged. */
bool kui_smash_native_combo_plan(const void *image,size_t bytes,uint32_t load,
    const uint8_t fresh_sha256[32],const struct kui_vmu_native_patch_window *protected,
    size_t count,struct kui_smash_native_combo_plan *out);
#endif
