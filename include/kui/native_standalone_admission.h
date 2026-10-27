/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_NATIVE_STANDALONE_ADMISSION_H
#define KUI_NATIVE_STANDALONE_ADMISSION_H
#include "kui/retail_image.h"
#include <stdbool.h>
#include <stddef.h>
#define KUI_NATIVE_STANDALONE_DOA2_BYTES UINT32_C(1815864)
/* This pilot uses the ordinary module route. A successful check changes
 * only the decoded manifest's reader; the wire and source image are read-only. */
bool kui_native_standalone_launch_prepare(struct kui_retail_manifest *);
bool kui_native_standalone_image_admit(const void *,size_t);
#endif
