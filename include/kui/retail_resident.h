/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_RETAIL_RESIDENT_H
#define KUI_RETAIL_RESIDENT_H
#include <stdint.h>
#include "kui/retail_loader_layout.h"

struct retail_display_state;
struct kui_retail_manifest;
struct kui_loader_sd;
/* Fixed first instruction at KUI_RETAIL_RESIDENT_ADDRESS. The stage must
 * install the resident bytes with coherent instruction/data caches and call
 * this on its temporary high stack with maskable interrupts blocked. The
 * entry clears resident BSS. The stage supplies its validated immutable map
 * and initialized idle card. Every input is copied before returning, and the
 * BIOS vector is changed only on complete success. There is no high-stage
 * pointer retained after success. */
typedef int (*kui_retail_resident_entry)(const struct kui_retail_manifest *,
    const struct kui_loader_sd *, uint32_t original_gd_vector,
    const struct retail_display_state *display);
enum kui_retail_resident_result {
    KUI_RETAIL_RESIDENT_OK = 0, KUI_RETAIL_RESIDENT_ARGUMENT = 1,
    KUI_RETAIL_RESIDENT_MAP = 2, KUI_RETAIL_RESIDENT_SD = 3,
    KUI_RETAIL_RESIDENT_SERVICE = 4
};
int kui_retail_resident_init(const struct kui_retail_manifest *,
    const struct kui_loader_sd *, uint32_t original_gd_vector,
    const struct retail_display_state *display);
int32_t kui_retail_resident_hook(uint32_t, uint32_t, uint32_t, uint32_t);
int32_t kui_retail_resident_dispatch(uint32_t, uint32_t, uint32_t, uint32_t);
#endif
