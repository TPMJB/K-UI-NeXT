/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_DISC_VMU_SUPPORT_H
#define KUI_DISC_VMU_SUPPORT_H

#include "sd_reader.h"
#include "kui/disc_vmu_layout.h"
#include <stdint.h>

#define KUI_DISC_VMU_SUPPORT_MAGIC UINT32_C(0x44565331)
#define KUI_DISC_VMU_SUPPORT_VERSION 1u
#define KUI_DISC_VMU_SUPPORT_LINK UINT32_C(0x8c112000)

/* Read through P2 at worker payload+72KiB after applying the independently
 * checked support relocation table. Code entries use P1. The complete support
 * span belongs to the same allocator lease as the worker, never BIOS RAM. */
struct kui_disc_vmu_support_exports {
    uint32_t magic,version,bytes,bind,sd_lease,terminal;
    uint32_t bss_begin,bss_end,memory_end,reserved[7];
};
_Static_assert(sizeof(struct kui_disc_vmu_support_exports)==64u,"Disc VMU support exports ABI");

/* Native stage-to-support call, not an on-card wire format. No pointer to this
 * object or prepared descriptor survives bind. worker_begin and code entries
 * are P1; worker_context and worker_stack_top are P2. lease_begin may use the
 * allocator's U0/P1/P2 alias and denotes its payload (excluding32B header).
 * The stage independently derives worker_context from the exact worker ELF;
 * the worker also checks that every transaction receives its durable object. */
struct kui_disc_vmu_support_request {
    uint32_t version,bytes,transport,worker_begin,lease_begin,lease_bytes;
    uint32_t worker_transaction,worker_context,worker_terminal,worker_stack_top;
    uint8_t cid[16];
    struct kui_loader_sd card;
};

/* Zero the entire relocated support BSS before its sole bind, on the stage's
 * private stack with IMASK15. Return0 succeeds, -1 refuses without wire traffic
 * or leaving any stage pointer resident. All workers and support bytes must
 * already be copied, relocated and published coherently. */
int kui_disc_vmu_support_bind(const struct kui_disc_vmu_support_request *request);
int kui_disc_vmu_support_sd_lease(int (*transaction)(void *,struct kui_loader_sd *),void *context);
void kui_disc_vmu_support_terminal(void) __attribute__((noreturn));
extern const struct kui_disc_vmu_support_exports kui_disc_vmu_support_exports;

#endif
