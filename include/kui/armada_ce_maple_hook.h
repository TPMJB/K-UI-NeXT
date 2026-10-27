/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_ARMADA_CE_MAPLE_HOOK_H
#define KUI_ARMADA_CE_MAPLE_HOOK_H
#include <stdint.h>
#define KUI_ARMADA_CE_MAPLE_HOOK_POOL_BEGIN UINT32_C(0x8cfcf000)
#define KUI_ARMADA_CE_MAPLE_HOOK_POOL_END UINT32_C(0x8cfd0000)
#define KUI_ARMADA_CE_MAPLE_HOOK_ENTRY UINT32_C(0xacfcf000)
#define KUI_ARMADA_CE_MAPLE_HOOK_LOG UINT32_C(0xacfcfe00)
#define KUI_ARMADA_CE_MAPLE_HOOK_LOG_BYTES 120u
#define KUI_ARMADA_CE_MAPLE_HOOK_MAGIC UINT32_C(0x314b484d) /* MHK1 */
#define KUI_ARMADA_CE_MAPLE_HOOK_SAMPLE_LIMIT 32u
#define KUI_ARMADA_CE_MAPLE_HOOK_BRIDGE_RVA UINT32_C(0x67a4)
#define KUI_ARMADA_CE_MAPLE_HOOK_BRIDGE_BYTES 92u
#define KUI_ARMADA_CE_MAPLE_HOOK_BRIDGE_ROM UINT32_C(0x8c0b47a4)
#define KUI_ARMADA_CE_MAPLE_HOOK_PATCH_ROM UINT32_C(0x8c0b3b0c)
#define KUI_ARMADA_CE_MAPLE_HOOK_PATCH_WORD UINT32_C(0x0009a64a)
#define KUI_ARMADA_CE_MAPLE_HOOK_PATCHED_CRC UINT32_C(0x576e75e7)
#define KUI_ARMADA_CE_MAPLE_HOOK_O32_VSIZE_ROM UINT32_C(0x8c099bd4)
#define KUI_ARMADA_CE_MAPLE_HOOK_ORIGINAL_VSIZE UINT32_C(0x57a2)
#define KUI_ARMADA_CE_MAPLE_HOOK_EXTENDED_VSIZE UINT32_C(0x5800)
/* This observer changes code only before NK executes. Its mapped bridge calls
 * the exact source-proven SetKMode thunk before entering P2, then restores the
 * previous mode. It neither follows r4 nor reads Maple MMIO, DMA, VMU payloads,
 * SD storage or any variable CE pointer. Caller-owned integer frame slots are
 * saved by the bridge and read only with fixed SP-relative instructions.
 * counts: MHK1, visits (saturates at UINT32_MAX), samples (caps at32), limit,
 *         previous KMode boolean for the most recently recorded sample.
 * first/last: entry r4, original SP, original PR, P2-entry SR, previous KMode,
 *             TTB, PTEH, CCR, current PROCESS, reserved zero.
 * These rows establish execution/context observations only, not DMA idleness,
 * allocation identity, request ownership, or virtual-card compatibility.
 * fault is reserved zero: this stackless leaf has no failure callback.
 * The native bridge API calls can alter T; its incoming value is dead because
 * the original function's first T consumer follows an unconditional TST.
 * MACH/MACL/GBR/PR and all volatile GPRs are explicitly saved by the bridge;
 * no floating-point instruction is emitted. */
struct kui_armada_ce_maple_hook_snapshot {
    uint32_t counts[5],first[10],last[10],fault[5];
};
_Static_assert(sizeof(struct kui_armada_ce_maple_hook_snapshot)==KUI_ARMADA_CE_MAPLE_HOOK_LOG_BYTES,
    "CE Maple hook report remains six five-word rows");
#endif
