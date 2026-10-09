/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_CDDA_BIOS_CLIENT_H
#define KUI_CDDA_BIOS_CLIENT_H
#include <stdbool.h>
#include <stdint.h>
#include "kui/cdda_bridge.h"

#define CDDA_BIOS_CLIENT_MAGIC 0x43444338u
#define CDDA_BIOS_CLIENT_REVISION 1u
enum cdda_bios_client_diagnostic_op {
    CDDA_BIOS_CLIENT_MARK,CDDA_BIOS_CLIENT_REPORT,
    CDDA_BIOS_CLIENT_SNAPSHOT,CDDA_BIOS_CLIENT_ARM_REENTRY
};
/* Owner diagnostics carry counters and checked snapshots only. Audio control
 * and data transfer are issued exclusively through the owned BIOS vector. */
struct cdda_bios_client_diagnostic {
    uint32_t op,value,aux,pc,sp,result,state,frame,prefetch;
    uint32_t vector_calls,execs,checks,drive_checks,read_checked;
    uint32_t expected_refusals,stale_checks,cancels,resets;
};
struct cdda_bios_client_exports {
    uint32_t magic,revision,bytes;
    void *context;
    uint32_t (*clock)(void *);
    uint32_t (*probe)(kui_cdda_bridge_service,void *);
    uint32_t (*diagnostic)(void *,struct cdda_bios_client_diagnostic *);
};
uint32_t cdda_bios_client_entry(const void *);

#ifdef CDDA_HARNESS_HOST_TEST
/* Host tests assign numeric guest addresses to client-owned local buffers,
 * preserving full host pointers in their map rather than truncating them. */
uint32_t cdda_bios_host_address(const void *,uint32_t bytes,bool writing);
int32_t cdda_bios_host_vector_call(uint32_t r4,uint32_t r5,uint32_t r6,uint32_t r7);
uint32_t cdda_bios_host_client_pc(void);
uint32_t cdda_bios_host_client_sp(void);
#endif
#endif
