/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_CDDA_DISC_CLIENT_H
#define KUI_CDDA_DISC_CLIENT_H
#include <stdbool.h>
#include <stdint.h>
#include "kui/cdda_bridge.h"

#define CDDA_DISC_CLIENT_MAGIC 0x43444342u
#define CDDA_DISC_CLIENT_REVISION 1u
enum cdda_disc_client_diagnostic_op {
    CDDA_DISC_CLIENT_MARK,CDDA_DISC_CLIENT_REPORT,
    CDDA_DISC_CLIENT_SNAPSHOT,CDDA_DISC_CLIENT_ARM_REENTRY,
    CDDA_DISC_CLIENT_TRACK_QUERY,CDDA_DISC_CLIENT_MAP_QUERY
};
/* Diagnostics expose counters, the last checked actual audio cursor and pure
 * immutable metadata only. All playback and data commands cross the owned GD
 * BIOS vector. TRACK_QUERY.value is an original track number; MAP_QUERY.value
 * is FAD and .aux is a sector count. query_result zero means a checked match;
 * a refused metadata query leaves the audio, queue, storage and clock alone.
 * Selected-source maps explicitly report map_complete=0 and refuse GETTOC2. */
struct cdda_disc_client_diagnostic {
    uint32_t op,value,aux,pc,sp,result,state,frame,prefetch,epoch,loops,track;
    uint32_t vector_calls,execs,checks,drive_checks,read_checked;
    uint32_t expected_refusals,stale_checks,polls,actions;
    uint32_t toc_checks,map_checks,eof_checks,track_switches;
    uint32_t query_result,map_count,map_complete,control,start_fad,end_fad;
    uint32_t source_frames,backing_offset,file_bytes,stride;
};
struct cdda_disc_client_exports {
    uint32_t magic,revision,bytes;
    void *context;
    uint32_t (*clock)(void *);
    uint32_t (*probe)(kui_cdda_bridge_service,void *);
    uint32_t (*diagnostic)(void *,struct cdda_disc_client_diagnostic *);
};
uint32_t cdda_disc_client_entry(const void *);

#ifdef CDDA_HARNESS_HOST_TEST
uint32_t cdda_disc_host_address(const void *,uint32_t bytes,bool writing);
int32_t cdda_disc_host_vector_call(uint32_t r4,uint32_t r5,uint32_t r6,uint32_t r7);
uint32_t cdda_disc_host_client_pc(void);
uint32_t cdda_disc_host_client_sp(void);
#endif
#endif
