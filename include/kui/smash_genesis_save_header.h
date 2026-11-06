/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SMASH_GENESIS_SAVE_HEADER_H
#define KUI_SMASH_GENESIS_SAVE_HEADER_H
#include <stddef.h>
#include <stdint.h>
#define KUI_SMASH_GENESIS_HEADER_BYTES 512u
/* Exact reviewed MDE parser, not a generic Genesis header interpretation.
 * A nonzero result establishes SRAM eligibility, not high-score persistence. */
uint32_t kui_smash_genesis_sram_bytes(const void *header,size_t bytes);
#endif
