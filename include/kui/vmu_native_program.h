/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_NATIVE_PROGRAM_H
#define KUI_VMU_NATIVE_PROGRAM_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Select the immutable executable extent after the caller has verified the
 * entire source file. Ordinary images retain source_bytes. A trailing 32 KiB
 * IP bootstrap is excluded only when its canonical linked domain, absence of
 * prefix references into it, and the unique supported startup allocator prove
 * that the prefix boundary is reclaimed as heap before gameplay. This never
 * alters source bytes, loads another image, or grants live memory ownership.
 * Recognizable but malformed/truncated bootstrap tails and contradictory
 * allocator/caller/control-flow evidence refuse. Output is unchanged on any
 * failure, including overlap. A shortened result must remain bound to the
 * verified full source independently of any cache of the effective prefix. */
bool kui_vmu_native_program_extent(const void *image,size_t source_bytes,
    uint32_t load,size_t *program_bytes);
#endif
