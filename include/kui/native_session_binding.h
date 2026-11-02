/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_NATIVE_SESSION_BINDING_H
#define KUI_NATIVE_SESSION_BINDING_H
#include <stdint.h>
#define KUI_NATIVE_SESSION_BINDING_MAGIC UINT32_C(0x4b534231)
#define KUI_NATIVE_SESSION_BINDING_VERSION 1u
/* The immutable resident owns this descriptor. The stage compares it with
 * the loaded reader image before retaining any pointer; the worker repeats
 * the low-region and lease admission when it arms. Cells contain P1 pointers.
 * firmware_snapshot addresses nine original words: BC/C0/E0, then the three
 * words at 1000 and the three words at 10f0. No stage address is retained. */
struct kui_native_session_binding {
    uint32_t magic,version,bytes,region_cell,manifest_cell,firmware_snapshot;
};
_Static_assert(sizeof(struct kui_native_session_binding)==24u,
    "Native session low binding ABI");
#endif
