/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_NATIVE_COLD_CACHE_POLICY_H
#define KUI_NATIVE_COLD_CACHE_POLICY_H
/* retail_stage.S publishes RAM and enters the native relay through P1 with
 * operands disabled. Pure immutable checks may use its guarded cache scopes.
 * Each scope still checks the actual SR/MMU/CCR/pointers/stack and otherwise
 * calls the original leaf without changing cache state. This is a cold-stage
 * operation policy, independent of reader mode, image encoding or title.
 * CE and live physical-disc stages retain their separate execution contract. */
#if !defined(KUI_RETAIL_CE) && !KUI_SMASH_CE_CHILD && !KUI_NATIVE_PHYSICAL_DISC
#define KUI_NATIVE_COLD_CRC_CACHE 1
#else
#define KUI_NATIVE_COLD_CRC_CACHE 0
#endif
#if KUI_NATIVE_COLD_CRC_CACHE && KUI_NATIVE_STANDALONE_STRUCTURAL && !KUI_NATIVE_STANDALONE_FOREGROUND
#define KUI_NATIVE_COLD_SDK_CACHE 1
#else
#define KUI_NATIVE_COLD_SDK_CACHE 0
#endif
#endif
