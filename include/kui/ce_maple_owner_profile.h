/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_CE_MAPLE_OWNER_PROFILE_H
#define KUI_CE_MAPLE_OWNER_PROFILE_H
#include <stdint.h>
#ifndef KUI_VC2_CE_VMU
#define KUI_VC2_CE_VMU 0
#endif
#ifndef KUI_VC2_VMU_WRITE_PROBE
#define KUI_VC2_VMU_WRITE_PROBE 0
#endif
#ifndef KUI_VC2_VMU_PERSIST
#define KUI_VC2_VMU_PERSIST 0
#endif
#if KUI_VC2_VMU_WRITE_PROBE != 0 && KUI_VC2_VMU_WRITE_PROBE != 1
#error "VC2 write staging test must be explicitly zero or one"
#endif
#if KUI_VC2_VMU_WRITE_PROBE && !KUI_VC2_CE_VMU
#error "VC2 write staging test requires its source-bound owner profile"
#endif
#if KUI_VC2_CE_VMU != 0 && KUI_VC2_CE_VMU != 1
#error "VC2 CE VMU profile must be explicitly disabled or enabled"
#endif
/* Only statically reviewed owner geometry varies here. All repeated live
 * mapping/cache/IRP/fence checks remain shared. Selecting this profile grants
 * no installation lifetime and enables no storage service. */
#if KUI_VC2_CE_VMU
#include "kui/vc2_ce_maple_profile.h"
#if (KUI_ARMADA_CE_VMU_WRITE_RUNTIME && !KUI_VC2_VMU_PERSIST) || \
    (KUI_ARMADA_CE_MAPLE_WRITE_STAGING && !KUI_VC2_VMU_WRITE_PROBE && !KUI_VC2_VMU_PERSIST)
#error "VC2 storage requires a separately admitted worker and foreground loan"
#endif
#if KUI_VC2_VMU_WRITE_PROBE && !KUI_ARMADA_CE_MAPLE_WRITE_STAGING
#error "VC2 write test requires bounded packet staging"
#endif
#define KUI_CE_MAPLE_CURRENT_PROCESS KUI_VC2_CE_CURRENT_PROCESS_P1
#define KUI_CE_MAPLE_CURRENT_THREAD KUI_VC2_CE_CURRENT_THREAD_P1
#define KUI_CE_MAPLE_PROCESS_BASE KUI_VC2_CE_PROCESS_TABLE_P1
#define KUI_CE_MAPLE_EXTENSION_BYTES KUI_VC2_CE_MAPLE_EXTENSION_BYTES
#define KUI_CE_MAPLE_FUNCTION_BYTES UINT32_C(0x150)
#define KUI_CE_MAPLE_WORKER_HANDLE_OFFSET KUI_VC2_CE_MAPLE_THREAD_HANDLE_OFFSET
#define KUI_CE_MAPLE_BRIDGE_PRE_RETURN (KUI_VC2_CE_MAPLE_NOMINAL_BASE+KUI_VC2_CE_MAPLE_BRIDGE_PRE_RETURN_RVA)
#define KUI_CE_MAPLE_WAIT_THUNK (KUI_VC2_CE_MAPLE_NOMINAL_BASE+KUI_VC2_CE_MAPLE_WAIT_RVA)
#else
#define KUI_CE_MAPLE_CURRENT_PROCESS UINT32_C(0x8c145890)
#define KUI_CE_MAPLE_CURRENT_THREAD UINT32_C(0x8c145894)
#define KUI_CE_MAPLE_PROCESS_BASE UINT32_C(0x8c14e6e0)
#define KUI_CE_MAPLE_EXTENSION_BYTES UINT32_C(0x198)
#define KUI_CE_MAPLE_FUNCTION_BYTES UINT32_C(0xf4)
#define KUI_CE_MAPLE_WORKER_HANDLE_OFFSET UINT32_C(0x178)
#define KUI_CE_MAPLE_BRIDGE_PRE_RETURN UINT32_C(0x01dd67c6)
#define KUI_CE_MAPLE_WAIT_THUNK UINT32_C(0x01dd6314)
#endif
#define KUI_CE_MAPLE_PROCESS_STRIDE UINT32_C(0x9c)
#define KUI_CE_MAPLE_PROCESS_COUNT 32u
#endif
