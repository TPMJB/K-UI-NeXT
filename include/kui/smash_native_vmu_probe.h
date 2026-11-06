/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SMASH_NATIVE_VMU_PROBE_H
#define KUI_SMASH_NATIVE_VMU_PROBE_H
#include <stdbool.h>
#include <stdint.h>
/* Source-bound MDE addresses. These are read-only observations after the
 * existing native worker's ordinary transfer completion proof. */
#define KUI_SMASH_SRAM_BYTES UINT32_C(0x8c48f3e0)
#define KUI_SMASH_BUP_APP UINT32_C(0x8c576530)
#define KUI_SMASH_BUP_JOB UINT32_C(0x8c5fc994)
#define KUI_SMASH_BUP_CALLBACK UINT32_C(0x8c01bef2)
#define KUI_SMASH_BUP_MASK UINT32_C(0x8c5f9bec)
#define KUI_SMASH_SDK_STATUS UINT32_C(0x8c5fc448)
#define KUI_SMASH_SDK_ROOT_CELL UINT32_C(0x8c63e0a0)
#define KUI_SMASH_VMU_PROBE_DETAIL UINT32_C(0x534d0001)
struct kui_smash_native_vmu_probe {uint32_t completions,registered_at,ready;};
typedef bool (*kui_smash_native_vmu_probe_read)(void *,uint32_t,unsigned,uint32_t *);
/* No reads until each64th completion. Nonzero SRAM size is the source-bound
 * post-ROM-parse checkpoint immediately before the RPG's save check. A usable
 * card disables this startup probe. After512 further completions, report a
 * missing callback/mask as well as an unsuccessful card connection. */
bool kui_smash_native_vmu_probe_tick(struct kui_smash_native_vmu_probe *,
    kui_smash_native_vmu_probe_read,void *);
#endif
