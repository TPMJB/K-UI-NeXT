/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_NATIVE_STANDALONE_READER_H
#define KUI_NATIVE_STANDALONE_READER_H
#include <stdint.h>

/* Exact DOA2 T3601N/V1.100 source roles, admitted by the temporary stage's
 * complete original-program SHA256 and SDK-family proof. No retail bytes
 * or temporary-stage pointer is retained. SDK routing leaves vector100
 * unused; these first five words are its cold, immutable source descriptor. */
#define KUI_NATIVE_STANDALONE_SOURCE_OFFSET 0u
#define KUI_NATIVE_STANDALONE_SOURCE_MAGIC UINT32_C(0x53524431)
#define KUI_NATIVE_STANDALONE_SOURCE_VECTOR UINT32_C(0x8c134018)
#define KUI_NATIVE_STANDALONE_SOURCE_NORMAL UINT32_C(0x8c1340a4)
#define KUI_NATIVE_STANDALONE_SOURCE_FPU UINT32_C(0x8c13420c)
#define KUI_NATIVE_STANDALONE_SOURCE_DEFAULT UINT32_C(0x8c133ee8)
#define KUI_NATIVE_STANDALONE_SOURCE_VBR UINT32_C(0x8c00f400)
struct kui_native_standalone_source {
    uint32_t vector,normal,fpu,original,magic;
};
_Static_assert(sizeof(struct kui_native_standalone_source)==20u,
    "Standalone reader retained source descriptor");
/* No result grants general SDK admission. The actual GD entry holds the
 * resident lock and IMASK15; the original tables must be fully initialized
 * and the reader idle before this first-install-only publication can run. */
void kui_native_standalone_bind(void);
#endif
