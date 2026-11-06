/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_RUNTIME_MODULE_BUNDLE_H
#define KUI_RUNTIME_MODULE_BUNDLE_H
#include "kui/runtime.h"
/* Immutable GUI-owned compressed module assets. This is independent of the
 * updater's bank namespace. Explicit bank replacements always take priority. */
bool kui_runtime_module_bundle_contains(const char *path);
bool kui_runtime_module_bundle_claims_view(const void *bundle,size_t bytes,const char *path);
enum kui_runtime_result kui_runtime_module_bundle_read(const char *path,
    struct kui_runtime_image *out,kui_cancel_fn cancel,bool *handled);
enum kui_runtime_result kui_runtime_module_bundle_read_view(const void *bundle,size_t bytes,
    const char *path,struct kui_runtime_image *out,kui_cancel_fn cancel,bool *handled);
#endif
