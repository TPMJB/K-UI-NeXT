/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SMASH_CE_RETURN_PROBE_H
#define KUI_SMASH_CE_RETURN_PROBE_H
#include "kui/smash_menu_return.h"
#include "kui/smash_ce_return.h"
#define KUI_SMASH_CE_RETURN_UNAVAILABLE UINT32_C(0x2000)
/* This normal child return remains terminal until complete CE retirement
 * fits its accepted helper. No successful return/cold-overwrite path exists. */
struct kui_smash_ce_return_probe_runtime {
    struct kui_smash_menu_return_seed returning;
    uint32_t code_crc32,raw_crc32,helper_bytes,helper_crc32;
    uint32_t report_entry,trace_address,profile;
};
_Static_assert(sizeof(struct kui_smash_ce_return_probe_runtime)==396u,"CE terminal return runtime ABI");
uint32_t kui_smash_ce_return_probe_crc(uint32_t normalized_code,
    const struct kui_smash_ce_return_probe_runtime *);
#endif
