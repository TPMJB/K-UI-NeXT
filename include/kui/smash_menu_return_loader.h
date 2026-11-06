/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SMASH_MENU_RETURN_LOADER_H
#define KUI_SMASH_MENU_RETURN_LOADER_H
#include "kui/smash_menu_return.h"
#include "kui/smash_card_session.h"
#ifndef KUI_SMASH_PHASE_DIAGNOSTIC
#define KUI_SMASH_PHASE_DIAGNOSTIC 0
#endif
#if KUI_SMASH_PHASE_DIAGNOSTIC
#include "kui/smash_phase_video.h"
#endif
#define KUI_SMASH_NATIVE_RESET_ENTRY UINT32_C(0x8c0117f0)
#define KUI_SMASH_MENU_RETURN_WAIT_MAX 4096u
#define KUI_SMASH_MENU_RETURN_WAIT_SPINS 1024u
struct kui_smash_menu_return_runtime {
    struct kui_smash_menu_return_seed returning;
    uint32_t firmware[9],stop_entry,retire_entry,code_crc32,raw_crc32;
#if KUI_SMASH_CARD_SESSION
    struct kui_smash_card_receipt card_receipt;
#endif
#if KUI_SMASH_PHASE_DIAGNOSTIC
    /* The original KOS capture survives the game in this authenticated low
     * return runtime; current game PVR state cannot replace its provenance. */
    struct kui_smash_phase_video_seed phase_video;
#endif
};
_Static_assert(sizeof(struct kui_smash_menu_return_runtime)==420u+96u*KUI_SMASH_CARD_SESSION+
    80u*(KUI_SMASH_PHASE_DIAGNOSTIC!=0),"Smash return loader runtime ABI");
uint32_t kui_smash_menu_return_runtime_crc(uint32_t normalized_code_crc,
    const struct kui_smash_menu_return_runtime *);
void kui_smash_native_return_stage_prepare(void);
void kui_smash_native_return_stage_publish(void);
void kui_smash_native_return_stage_bind(uint32_t worker,uint32_t initialized_bytes,uint32_t span);
#endif
