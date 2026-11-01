/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_NATIVE_PATCH_H
#define KUI_VMU_NATIVE_PATCH_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Conservative immutable-image guard. Excludes encoded direct branches and
 * aligned main-RAM literals into a displaced instruction window's interior.
 * It does not establish arbitrary indirect control flow. */
bool kui_vmu_native_patch_no_interior(const void *,size_t,uint32_t load,
    uint32_t begin,uint32_t bytes);
struct kui_vmu_native_patch_window {uint32_t begin,bytes;};
/* Exact twelve-byte SH jump veneer. Its MOV.L literal is four-byte aligned
 * for either possible halfword-aligned entry; aligned entries retain the
 * original encoding. target must be a P1/P2 executable RAM address. This
 * constructs bytes only and grants no authority to install a patch. */
bool kui_vmu_native_patch_encode(uint32_t address,uint32_t target,uint16_t out[6]);
/* Eight-byte head for an entry immediately followed by another entry's
 * twelve-byte veneer: it jumps through that veneer's literal, so the entry
 * at address+8 must carry a veneer to the intended target. The veneer keeps
 * its literal four-byte aligned, at +6 of a halfword-aligned entry and +8
 * otherwise, so from either alignment the head is the same four halfwords:
 * MOV.L @(3,PC),R0; JMP @R0; NOP; NOP. encode_head admits the address of
 * the whole twenty-byte joint and constructs bytes only. */
#define KUI_VMU_NATIVE_PATCH_HEAD {UINT16_C(0xd003),UINT16_C(0x402b),9u,9u}
bool kui_vmu_native_patch_encode_head(uint32_t address,uint16_t out[4]);
/* One immutable-image pass for several disjoint entry windows. */
bool kui_vmu_native_patch_no_interiors(const void *,size_t,uint32_t load,
    const struct kui_vmu_native_patch_window *,size_t count);
#endif
