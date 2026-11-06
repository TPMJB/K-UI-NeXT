/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SMASH_CE_RETURN_LOADER_H
#define KUI_SMASH_CE_RETURN_LOADER_H
#include "kui/smash_menu_return_loader.h"
#include "kui/smash_ce_return_view.h"
#include "kui/smash_phase_video.h"
#define KUI_SMASH_CE_RETURN_ENTRY UINT32_C(0xac008300)
#define KUI_SMASH_CE_RETURN_TRANSPORT_BYTES 7968u
#define KUI_SMASH_CE_RETURN_WAIT_MAX 65536u
/* This object is sealed by its native launcher, never reconstructed from
 * game-owned CE pointers. returning already contains the ROOT menu context. */
struct kui_smash_ce_return_runtime {
    struct kui_smash_menu_return_seed returning;
    uint32_t firmware[9];
    uint32_t code_crc32,raw_crc32,profile,helper_bytes,helper_crc32;
    struct kui_smash_ce_return_view view;
    struct kui_smash_card_receipt card_receipt;
    struct kui_smash_phase_video_seed phase_video;
};
_Static_assert(sizeof(struct kui_smash_ce_return_runtime)==632u,"CE terminal return runtime");
extern struct kui_smash_ce_return_runtime kui_smash_ce_return_runtime;
uint32_t kui_smash_ce_return_runtime_crc(uint32_t normalized_code_crc,
    const struct kui_smash_ce_return_runtime *);
int kui_smash_ce_return_main(const uint32_t frame[21]);
#endif
