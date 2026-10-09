/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_CDDA_BATCH_CLIENT_H
#define KUI_CDDA_BATCH_CLIENT_H
#include <stdbool.h>
#include <stdint.h>
#include "kui/cdda_bridge.h"

#define CDDA_BATCH_CLIENT_MAGIC 0x43444339u
#define CDDA_BATCH_CLIENT_REVISION 1u
enum cdda_batch_client_diagnostic_op {
    CDDA_BATCH_CLIENT_MARK,CDDA_BATCH_CLIENT_REPORT,
    CDDA_BATCH_CLIENT_SNAPSHOT,CDDA_BATCH_CLIENT_ARM_REENTRY,
    CDDA_BATCH_CLIENT_ARM_GAP
};
/* Counters and checked owner snapshots only. All audio commands and bounded
 * data requests still cross the owned BIOS vector. Partial confirmed prefixes
 * count as checked bytes; full_pass_bytes is completed sequential-pass
 * coverage, not the engine's current candidate cursor after random reads. */
struct cdda_batch_client_diagnostic {
    uint32_t op,value,aux,pc,sp,result,state,frame,prefetch;
    uint32_t vector_calls,execs,checks,drive_checks,read_checked;
    uint32_t expected_refusals,stale_checks,cancels,resets;
    uint32_t full_passes,full_pass_bytes,partial_cancels,partial_resets,progress_polls;
    uint32_t boundary_reads,random_reads,class_counts[8];
};
struct cdda_batch_client_exports {
    uint32_t magic,revision,bytes;
    void *context;
    uint32_t (*clock)(void *);
    uint32_t (*probe)(kui_cdda_bridge_service,void *);
    uint32_t (*diagnostic)(void *,struct cdda_batch_client_diagnostic *);
};
uint32_t cdda_batch_client_entry(const void *);

#ifdef CDDA_HARNESS_HOST_TEST
uint32_t cdda_batch_host_address(const void *,uint32_t bytes,bool writing);
int32_t cdda_batch_host_vector_call(uint32_t r4,uint32_t r5,uint32_t r6,uint32_t r7);
uint32_t cdda_batch_host_client_pc(void);
uint32_t cdda_batch_host_client_sp(void);
#endif
#endif
