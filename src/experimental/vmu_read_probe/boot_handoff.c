/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/storage_policy.h"

/* Card tools requires this initialized record on SCI/IDE before arch_exec.
 * The bootstrap patches it only in its checksum-validated RAM copy. This
 * read-only VMU app never consumes the transport or initializes storage.
 * The isolated Makefile requires and roots this symbol through --gc-sections; no SD
 * implementation or storage function is linked into the probe. */
volatile struct kui_storage_boot_marker kui_vmu_read_probe_boot_marker =
    KUI_STORAGE_BOOT_INITIALIZER;
_Static_assert(sizeof(kui_vmu_read_probe_boot_marker) == 20u, "storage handoff ABI");
