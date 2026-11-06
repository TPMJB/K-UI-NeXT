/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VC2_CE_VMU_GD_WAIT_H
#define KUI_VC2_CE_VMU_GD_WAIT_H
#define KUI_VC2_CE_VMU_GD_WAIT_P2 0xacfce000
#define KUI_VC2_CE_VMU_GD_WAIT_LIMIT 1024
#define KUI_VC2_CE_VMU_GD_LOAN_FLAG 2
#ifndef __ASSEMBLER__
#include <stdint.h>
/* Exact original WSEGACD call sites. No pointer is followed and no API is
 * called. VC2 packs source | (function<<8) into the fourth scalar; FN0's
 * original source3 is unchanged. Swirl retains its accepted FN0-only ABI.
 * A zero result refuses a native scheduling boundary. */
uint32_t kui_vc2_ce_vmu_gd_wait_admit(uint32_t caller_pr,uint32_t caller_sr,
    uint32_t command,uint32_t source);
void kui_vc2_ce_vmu_gd_wait_fault(uint32_t why,uint32_t value,
    uint32_t caller_pr,uint32_t caller_sr) __attribute__((noreturn));
/* Low .Lbusy contract: r0=active P1, r2=low retry-before-trace,
 * r3=legacy busy continuation, r8=existing 36-byte GD frame, r9=source,
 * r4..r7=raw arguments, r15=r8, interrupts masked. CHECK/EXEC branch
 * directly to r3; Swirl retains all non-FN0 calls.
 * Source-admitted VC2 calls yield on this same CE stack, then retry without
 * pushing a second low frame or fabricating a GD result. */
#endif
#endif
