/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_DISC_VMU_ENTRY_H
#define KUI_DISC_VMU_ENTRY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum kui_disc_vmu_entry_reason {
    KUI_DISC_VMU_ENTRY_ARGUMENT=1,
    KUI_DISC_VMU_ENTRY_BOUNDS,
    KUI_DISC_VMU_ENTRY_PR_READ,
    KUI_DISC_VMU_ENTRY_CONTROL,
    KUI_DISC_VMU_ENTRY_UNKNOWN,
    KUI_DISC_VMU_ENTRY_BACKWARD,
    KUI_DISC_VMU_ENTRY_LIMIT
};
struct kui_disc_vmu_entry_failure {uint32_t address,opcode,reason;};

/* Before the first original-game instruction executes, prove that every
 * bounded forward path from its immutable entry replaces the inherited PR
 * without reading/saving it. A JSR/BSR replaces PR before its delay slot;
 * explicit LDS/LDS.L PR also ends a path. Calls' targets remain game-owned:
 * this lifetime predicate does not authorize a call target or emulate RAM.
 *
 * Unknown instructions, dynamic jumps, returns, exception instructions,
 * backward edges and exhausted path bounds refuse. Delay slots follow the
 * same SH-4 restrictions as the linked instruction audit; all STS PR forms
 * are conservatively refused even in a call's slot. No recursion, allocation,
 * I/O or writes to image occur. Internal path storage is512 bytes.
 * failure is optional and zero on success; an output overlapping image
 * refuses without changing either input or output. */
bool kui_disc_vmu_entry_drops_inherited_pr(const void *image,size_t bytes,
    uint32_t load,struct kui_disc_vmu_entry_failure *failure);
#endif
