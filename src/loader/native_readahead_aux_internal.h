/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_NATIVE_READAHEAD_AUX_INTERNAL_H
#define KUI_NATIVE_READAHEAD_AUX_INTERNAL_H
#include "kui/native_readahead.h"
#include "kui/native_readahead_cache.h"
#include <stddef.h>

/* The SH entry owns busy. The public report prefix is deliberately small
 * and stable; all policy state is private to this separately owned module. */
struct kui_native_readahead_aux_context {
    uint32_t busy;
    struct kui_native_ra_stats stats;
    struct kui_native_ra_config config;
    struct kui_native_ra_cache cache;
    uint32_t initialized,quiesced,abandoned,have_seed;
    uint32_t completed_token,have_completed,hit_token,have_hit;
    uint32_t spec_owned,spec_lba,deferred;
};
_Static_assert(offsetof(struct kui_native_readahead_aux_context,busy)==0u,
    "auxiliary assembly busy word");
_Static_assert(offsetof(struct kui_native_readahead_aux_context,stats)==4u,
    "auxiliary public statistics prefix");
_Static_assert(offsetof(struct kui_native_readahead_aux_context,stats)+
    offsetof(struct kui_native_ra_stats,guard_faults)==64u,
    "auxiliary assembly guard fault counter");
_Static_assert(sizeof(struct kui_native_readahead_aux_context)<=4096u-64u,
    "auxiliary context fits its owned first page with export/guard margin");
extern struct kui_native_readahead_aux_context kui_native_readahead_aux_context;
int kui_native_readahead_aux_init(const struct kui_native_ra_config *);
uint32_t kui_native_readahead_aux_dispatch(uint32_t,uint32_t,
    kui_retail_async_region_type *,uint32_t);
#endif
