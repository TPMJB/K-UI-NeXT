/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_CE_VMU_TARGET_H
#define KUI_CE_VMU_TARGET_H

/* Swirl's source-bound application opens only port A, first subunit. The
 * existing VC2/Armada profiles retain A2. This compile-time choice controls
 * descriptor ownership, physical-card conflict detection and wire addresses
 * together; it never widens either profile to a second virtual endpoint. */
#ifndef KUI_SMASH_SWIRL_VMU
#define KUI_SMASH_SWIRL_VMU 0
#endif
#if KUI_SMASH_SWIRL_VMU != 0 && KUI_SMASH_SWIRL_VMU != 1
#error "KUI_SMASH_SWIRL_VMU must be 0 or 1"
#endif
#if KUI_SMASH_SWIRL_VMU
#define KUI_CE_VMU_ADDRESS 1u
#define KUI_CE_VMU_UNIT_INDEX 1u
#else
#define KUI_CE_VMU_ADDRESS 2u
#define KUI_CE_VMU_UNIT_INDEX 2u
#endif
#define KUI_CE_VMU_ATTACHMENT_BIT (KUI_CE_VMU_ADDRESS << 16)

#endif
