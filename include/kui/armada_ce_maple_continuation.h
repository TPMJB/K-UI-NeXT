/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_ARMADA_CE_MAPLE_CONTINUATION_H
#define KUI_ARMADA_CE_MAPLE_CONTINUATION_H
/* Shared C/assembler ABI. This opt-in path holds the source worker's original
 * consumer inside PRE; no queued operation is represented by a native ACK. */
#ifndef KUI_ARMADA_CE_VMU_WRITE_RUNTIME
#define KUI_ARMADA_CE_VMU_WRITE_RUNTIME 0
#endif
#if KUI_ARMADA_CE_VMU_WRITE_RUNTIME != 0 && KUI_ARMADA_CE_VMU_WRITE_RUNTIME != 1
#error "CE VMU WRITE runtime must be explicitly disabled or enabled"
#endif
#if KUI_ARMADA_CE_VMU_WRITE_RUNTIME
#if !KUI_ARMADA_CE_MAPLE_WRITE_LAYOUT || !KUI_ARMADA_CE_MAPLE_WRITE_STAGING
#error "CE VMU WRITE runtime requires WRITE layout and staging"
#endif
#endif
#define KUI_ARMADA_CE_MAPLE_CONTINUE 0
#define KUI_ARMADA_CE_MAPLE_SERVICE 1
#define KUI_ARMADA_CE_MAPLE_TERMINAL 2
#define KUI_ARMADA_CE_MAPLE_RESUME_PHASE 2
/* Includes pre-acquisition provider BUSY and DMA-busy reacquisition. This is
 * an attempt bound, not a provider-I/O deadline or external-unload lease. */
#define KUI_ARMADA_CE_MAPLE_CONTINUATION_LIMIT 1024
#define KUI_ARMADA_CE_MAPLE_CONTINUATION_SR_FORBIDDEN 0x100000f0
#if KUI_ARMADA_CE_VMU_WRITE_RUNTIME && !defined(__ASSEMBLER__)
#include <stdint.h>
/* GCC caller to exact CE/SH callee. Native callees may spill r4..r7 into
 *16 bytes owned by their caller, even when the logical API has two args. */
uint32_t kui_armada_ce_maple_native_wait(uint32_t entry,uint32_t handle,uint32_t milliseconds);
#endif
#endif
