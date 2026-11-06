/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VC2_CE_VMU_DIAGNOSTIC_H
#define KUI_VC2_CE_VMU_DIAGNOSTIC_H
#include <stdint.h>
/* Private P2 words recorded by the actual callback shim before C runs:
 * raw extension, phase, old mode, entry SR, bridge SP, bridge PR, TTB, CCR. */
extern uint32_t kui_vc2_ce_vmu_call_context[8];
extern uint32_t kui_vc2_ce_vmu_owner_context[5];
#if KUI_VC2_VMU_WRITE_PROBE
void kui_vc2_ce_vmu_write_snapshot(uint32_t [20]);
#endif
void kui_vc2_ce_vmu_diagnostic_stop(uint32_t,uint32_t)
    __attribute__((noreturn));
#endif
