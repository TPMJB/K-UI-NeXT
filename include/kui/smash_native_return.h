/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SMASH_NATIVE_RETURN_H
#define KUI_SMASH_NATIVE_RETURN_H
#include <stdint.h>
#define KUI_SMASH_NATIVE_RETURN_MAGIC UINT32_C(0x4b535254)
#define KUI_SMASH_NATIVE_RETURN_VERSION 1u
#define KUI_SMASH_NATIVE_RETURN_BYTES 16u
struct kui_smash_native_return_exports {
    uint32_t magic,version,bytes,entry;
};
_Static_assert(sizeof(struct kui_smash_native_return_exports)==16u,"Smash return export ABI");
extern const struct kui_smash_native_return_exports kui_smash_native_return_exports;
/* The descriptor names a C-ABI assembly gateway, which uses the existing
 * worker stack and preserves the caller's stack and callee-saved registers.
 * IMASK15 and BL must already be set; the gateway changes no SR bank/mode.
 * 1 retires exactly once, 0 leaves a busy worker live, -1 refuses bad ownership.
 * After 1 the caller MUST cold-restart without resuming any old SDK callback.
 * This never flushes an unfinished VMU command or resets/reuses its backend. */
int kui_smash_native_return_entry(void);
/* Pure phase zero runs on the caller's stack. It grants no ownership and
 * touches no worker stack, SDK pointer, hardware or SD state. */
int kui_smash_native_return_preflight(void);
/* Internal C body. Entry through the switched-stack gateway is required. */
int kui_smash_native_return_retire(void);
#endif
