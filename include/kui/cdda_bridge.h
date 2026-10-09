/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_CDDA_BRIDGE_H
#define KUI_CDDA_BRIDGE_H
#include <stdint.h>

typedef uint32_t (*kui_cdda_client_entry)(const void *context);
typedef uint32_t (*kui_cdda_bridge_service)(void *context);

/* Controlled, ordinary SH integer C call on a separate client or worker stack.
 * Preconditions: entry is callable; stack_top is 16-byte aligned and is the
 * high end of a writable downward-growing stack disjoint from all current or
 * suspended call stacks. Both sides obey the integer SH ABI and restore SP
 * on return. The caller validates these ranges and controls worker reentry.
 * Saves caller r8..r14 and PR (32 bytes on the caller stack), reserves a
 * 16-byte destination-stack anchor, passes context in r4, and returns entry's
 * r0. Ordinary nested calls use that stack until another explicit bridge call
 * selects a different validated stack; all such frames must fit their budget.
 * No FPU/FPSCR preservation is claimed. This is not an interrupt trampoline,
 * and it does not write SR/VBR/GBR configuration, timers, or interrupt state. */
uint32_t kui_cdda_client_call(kui_cdda_client_entry entry, const void *context,
                              void *stack_top);

/* Called by the controlled client. Seeds/checks r8..r14 and checks the return
 * PR across one ordinary service(context) call. Returns zero on preservation;
 * bits 0..6 identify r8..r14 mismatches, bit 7 identifies a PR mismatch.
 * The service's r0 result is discarded. The probe restores its own caller's
 * original r8..r14/PR and consumes 32 stack bytes plus the service's frame.
 * As with any C call, a service that cannot return to the expected address or
 * restore SP cannot be recovered by this probe. Integer comparisons use the
 * ordinary caller-clobbered T flag. No FPU state is examined. */
uint32_t kui_cdda_bridge_probe(kui_cdda_bridge_service service, void *context);
#endif
