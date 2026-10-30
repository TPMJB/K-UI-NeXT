/* SPDX-License-Identifier: GPL-3.0-only */
/* The General lean worker profile and ABI in the bounded VMU-lite span; the
 * lite marker selects the span in vmu_native_performance.ld. */
__kui_native_general_lean_worker_layout = 1;
__kui_native_vmu_lite_worker_layout = 1;
INCLUDE src/loader/vmu_native_performance.ld
