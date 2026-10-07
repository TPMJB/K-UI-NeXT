/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_CDDA_VECTOR_H
#define KUI_CDDA_VECTOR_H
#include <stdint.h>

#define KUI_CDDA_BIOS_GD_VECTOR_ADDRESS 0x8c0000bcu
typedef int32_t (*kui_cdda_bios_vector_fn)(uint32_t, uint32_t, uint32_t, uint32_t);

/* Controlled native GD/MISC indirect-vector entry. The client loads the
 * pointer from the vector slot, passes r4/r5 arguments, r6 subsystem (0 for
 * GD), and r7 selector, and receives a signed 32-bit result in r0.
 * This wrapper saves r8..r14 and the incoming return PR (32 caller-stack
 * bytes), calls cdda_bios_native_dispatch with r4..r7 unchanged, then restores
 * the caller's integer frame/SP while retaining the dispatcher's r0 result.
 * Dispatch runs initially on the caller stack: it must validate and reject
 * worker reentry before explicitly selecting a separate worker stack.
 * The caller and dispatcher obey the ordinary integer SH C ABI. Volatile
 * registers and arithmetic flags are not preserved; no FPU preservation or
 * interrupt-entry behavior is claimed. The wrapper does not write SR/VBR/GBR
 * configuration, change timers, select a fixed stack, or alter IRQ state.
 * Installation, vector ownership and exact restoration belong to the owner. */
int32_t kui_cdda_bios_vector_entry(uint32_t r4, uint32_t r5,
                                  uint32_t r6, uint32_t r7);
#endif
