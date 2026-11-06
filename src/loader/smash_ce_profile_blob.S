/* SPDX-License-Identifier: GPL-3.0-only */
    .section .resident_blob,"a"
    .balign 4
    .global _kui_smash_ce_profile_blob
_kui_smash_ce_profile_blob:
    .incbin KUI_SMASH_CE_PROFILE_BIN
