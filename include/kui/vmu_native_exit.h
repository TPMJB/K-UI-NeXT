/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_NATIVE_EXIT_H
#define KUI_VMU_NATIVE_EXIT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define KUI_VMU_NATIVE_EXIT_VERSION 1u
#define KUI_VMU_NATIVE_EXIT_FAMILY 1u
#define KUI_VMU_NATIVE_EXIT_ENTRIES 3u
struct kui_vmu_native_exit {
    uint32_t version,bytes,family,load,image_bytes,count;
    /* Terminal entry patches: SDK exit, cleanup/exit, executable loader.
     * Each reviewed entry is 4-byte aligned with a 12-byte safe window. */
    uint32_t entries[KUI_VMU_NATIVE_EXIT_ENTRIES];
    uint32_t bios_vector_literal,exec_thunk,cleanup;
};
/* Fresh immutable-image discovery for one strict SDK exit/exec family. The
 * complete exit/cleanup/loader shapes and decoded platform literals must agree;
 * additional encoded PC-relative BIOS-vector/bootstrap consumers are refused.
 * All three entry windows also pass the conservative interior-target guard.
 * Does not install hooks or prove arbitrary synthesized/indirect control flow.
 * Caller must terminally intercept all returned entries before gameplay. Out
 * clears on failure except overlapping image/output, which is untouched. */
bool kui_vmu_native_exit_discover(const void *image,size_t bytes,uint32_t load,
    struct kui_vmu_native_exit *out);
#endif
