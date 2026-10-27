/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_MAPLE_OBSERVE_BOOT_H
#define KUI_VMU_MAPLE_OBSERVE_BOOT_H

#define KUI_VMU_OBS_BOOT_INSTALLER 0
#define KUI_VMU_OBS_BOOT_STAGE_STACK 4
#define KUI_VMU_OBS_BOOT_STATUS 8
#define KUI_VMU_OBS_BOOT_LEASE 12
#define KUI_VMU_OBS_BOOT_ERROR 16
#define KUI_VMU_OBS_BOOT_HEAP_BEGIN 20
#define KUI_VMU_OBS_BOOT_HEAP_BYTES 24
#define KUI_VMU_OBS_BOOT_ENTRY_SP 28
#define KUI_VMU_OBS_BOOT_WORKER_END 32
#define KUI_VMU_OBS_BOOT_ORIGINAL_PR 36
#define KUI_VMU_OBS_BOOT_TERMINAL 40
#define KUI_VMU_OBS_BOOT_SAVED_R9 44
#define KUI_VMU_OBS_BOOT_SAVED_R10 48
#define KUI_VMU_OBS_BOOT_SD_TRANSACTION 52
#define KUI_VMU_OBS_BOOT_BYTES 56

#ifndef __ASSEMBLER__
#include <stdint.h>
#include <stddef.h>
struct kui_vmu_observe_boot_control {
    uint32_t installer,stage_stack,status,lease,error,heap_begin,heap_bytes;
    uint32_t entry_sp,worker_end,original_pr,terminal,saved_r9,saved_r10,sd_transaction;
};
_Static_assert(sizeof(struct kui_vmu_observe_boot_control)==KUI_VMU_OBS_BOOT_BYTES,
    "VMU observation low control ABI");
_Static_assert(offsetof(struct kui_vmu_observe_boot_control,terminal)==KUI_VMU_OBS_BOOT_TERMINAL,
    "VMU observation terminal ABI");
_Static_assert(offsetof(struct kui_vmu_observe_boot_control,sd_transaction)==KUI_VMU_OBS_BOOT_SD_TRANSACTION,
    "VMU SD transaction pin ABI");
enum kui_vmu_observe_boot_status {KUI_VMU_OBS_BOOT_EMPTY,KUI_VMU_OBS_BOOT_ARMED,
    KUI_VMU_OBS_BOOT_INITIALIZING,KUI_VMU_OBS_BOOT_LEASED,KUI_VMU_OBS_BOOT_INSTALLED,
    KUI_VMU_OBS_BOOT_FAILED};
extern struct kui_vmu_observe_boot_control kui_vmu_observe_boot_control;
void kui_vmu_observe_heap_hook(void);
void kui_vmu_observe_return_hook(void) __attribute__((noreturn));
#endif
#endif
