/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VC2_CE_VMU_GD_WAIT_H
#define KUI_VC2_CE_VMU_GD_WAIT_H
#define KUI_VC2_CE_VMU_GD_WAIT_P2 0xacfce000
#define KUI_VC2_CE_VMU_GD_WAIT_LIMIT 1024
#define KUI_VC2_CE_VMU_GD_LOAN_FLAG 2
#ifndef __ASSEMBLER__
#include <stdint.h>
/* Exact original WSEGACD FN0 source sites only. No pointer is followed and
 * no API is called. A zero result refuses a native scheduling boundary. */
uint32_t kui_vc2_ce_vmu_gd_wait_admit(uint32_t caller_pr,uint32_t caller_sr,
    uint32_t command,uint32_t source);
void kui_vc2_ce_vmu_gd_wait_fault(uint32_t why,uint32_t value,
    uint32_t caller_pr,uint32_t caller_sr) __attribute__((noreturn));
/* Low .Lbusy contract: r0=active P1, r2=low retry-before-trace,
 * r3=legacy busy continuation, r8=existing 36-byte GD frame, r9=source,
 * r4..r7=raw arguments, r15=r8, interrupts masked. Non-FN0 calls branch
 * directly to r3. FN0 yields on this same CE stack, then retries without
 * pushing a second low frame or inventing a request token. */
#endif
#endif
