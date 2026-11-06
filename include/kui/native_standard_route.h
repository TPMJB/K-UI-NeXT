/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_NATIVE_STANDARD_ROUTE_H
#define KUI_NATIVE_STANDARD_ROUTE_H
#include "kui/games_retail.h"
#include "kui/native_physical_capability.h"
#include "kui/native_standalone_foreground.h"
#include "kui/native_standalone_capability.h"
#include "kui/native_dual_reader.h"

enum kui_native_standard_module {
    KUI_NATIVE_STANDARD_REFUSED,
    KUI_NATIVE_STANDARD_PHYSICAL,
    KUI_NATIVE_STANDARD_FOREGROUND
};
/* Called only for the launcher's native Standard route. The optional control
 * lives at the same physical-only package path the current GUI selects. */
static inline const char *kui_native_standard_package(bool emulation_enabled) {
    return emulation_enabled?KUI_GAMES_RETAIL_PACKAGE:KUI_GAMES_RETAIL_PHYSICAL_PACKAGE;
}
/* Map against the selected resident's capacity so the existing audio-extent
 * fallback runs before a too-large map reaches the final admission check. */
static inline uint32_t kui_native_standard_map_capacity(bool foreground_control) {
    return foreground_control?KUI_RETAIL_ASYNC_SLOTS:KUI_RETAIL_IMAGE_SLOTS;
}
static inline enum kui_native_standard_module kui_native_standard_module_find(
    const void *data,size_t bytes) {
    int foreground=kui_native_foreground_descriptor_find(data,bytes,NULL);
    int physical=kui_native_physical_descriptor_find(data,bytes,NULL);
    if(!foreground)return physical==1?KUI_NATIVE_STANDARD_PHYSICAL:KUI_NATIVE_STANDARD_REFUSED;
    if(foreground!=1 || physical!=0 ||
       kui_native_standalone_descriptor_find(data,bytes,NULL)!=0 ||
       kui_native_dual_descriptor_find(data,bytes,NULL)!=0)
        return KUI_NATIVE_STANDARD_REFUSED;
    return KUI_NATIVE_STANDARD_FOREGROUND;
}
#endif
