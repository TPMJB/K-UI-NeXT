/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_NATIVE_RETURN_HARDWARE_H
#define KUI_NATIVE_RETURN_HARDWARE_H
#include <stdbool.h>
#include <stdint.h>
/* Terminal native-session menu only, with privileged IMASK15 and the SDK
 * stopped. One physical A-controller request, using worker-owned buffers.
 * Returns 1 for a complete typed response, 0 unavailable, -1 unsafe hardware.
 * Button bits use the controller's active-high low-16-bit layout (Start=8). */
int kui_native_return_controller(uint32_t *buttons);
/* Call after the low reader has relinquished streams and SD DMA. Retires
 * other game DMA/audio, then publishes the old stack and disables caches
 * BEFORE any P2 payload write, preventing stale game cache writebacks. */
bool kui_native_return_quiesce(void);
/* Payload must already be fully read, CRC checked, and transport-patched.
 * BIOS vectors must already have been restored from captured originals.
 * The entire live code/stack lease must exclude the incoming payload. */
void kui_native_return_enter(void) __attribute__((noreturn));
#endif
