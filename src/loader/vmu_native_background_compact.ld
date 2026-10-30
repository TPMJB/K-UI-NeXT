/* SPDX-License-Identifier: GPL-3.0-only */
/* Bounded VMU worker in the distinct compact Background profile. The complete
 * reader/map/auxiliary/cache lease is validated outside this worker image. */
__kui_native_general_lean_worker_layout = 1;
__kui_native_background_compact_worker_layout = 1;
INCLUDE src/loader/vmu_native_performance.ld
