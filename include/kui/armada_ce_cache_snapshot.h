/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_ARMADA_CE_CACHE_SNAPSHOT_H
#define KUI_ARMADA_CE_CACHE_SNAPSHOT_H
#include "kui/armada_ce_ram_bind.h"

/* One serialized, caller-private P2 job. The native leaf admits only this
 * exact linked object and never writes guest RAM. No result is published to
 * the caller until both tag and repeated-data witnesses have succeeded. */
struct kui_armada_ce_cache_snapshot_job {
    uint32_t va,p1,page,mask,word[8],detail[3];
};
_Static_assert(sizeof(struct kui_armada_ce_cache_snapshot_job)==60u,"snapshot job ABI");
_Static_assert(offsetof(struct kui_armada_ce_cache_snapshot_job,word)==16u,"snapshot output ABI");
_Static_assert(offsetof(struct kui_armada_ce_cache_snapshot_job,detail)==48u,"snapshot detail ABI");
extern struct kui_armada_ce_cache_snapshot_job kui_armada_ce_cache_snapshot_job;
extern unsigned kui_armada_ce_cache_snapshot(struct kui_armada_ce_cache_snapshot_job *);
enum kui_armada_ce_ram_bind_status kui_armada_ce_cache_line_native(
    uint32_t va,uint32_t p1,uint32_t page,uint32_t mask,
    kui_armada_ce_ram_read32 read32,void *context,uint32_t out[8]);
#endif
