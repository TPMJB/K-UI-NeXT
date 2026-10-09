/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TOY_PILOT_BOOT_H
#define KUI_TOY_PILOT_BOOT_H
#include "kui/toy_pilot_cache.h"

/* This control belongs to the low candidate resident. The two temporary
 * stage addresses are erased before the first ordinary game allocation;
 * the low heap guard stays installed and refuses a repeat init through its
 * one verified slot before that call can invalidate the executing lease. */
#define KUI_TOY_BOOT_INSTALLER 0
#define KUI_TOY_BOOT_STAGE_STACK 4
#define KUI_TOY_BOOT_STATUS 8
#define KUI_TOY_BOOT_LEASE 12
#define KUI_TOY_BOOT_ERROR 16
#define KUI_TOY_BOOT_HEAP_BEGIN 20
#define KUI_TOY_BOOT_HEAP_BYTES 24
#define KUI_TOY_BOOT_ENTRY_SP 28
#define KUI_TOY_BOOT_REQUEST 32
#define KUI_TOY_BOOT_SNAPSHOT 36
#define KUI_TOY_BOOT_WORKER_END 40
#define KUI_TOY_BOOT_ORIGINAL_PR 44
#define KUI_TOY_BOOT_GD_DISPATCH 48
#if KUI_TOY_PILOT_LOADER_TRACE
#define KUI_TOY_BOOT_TRACE_TERMINAL 52
#define KUI_TOY_BOOT_CONTROL_BYTES 56
#else
#define KUI_TOY_BOOT_CONTROL_BYTES 52
#endif

#define KUI_TOY_BOOT_HEAP_INIT 0x8c0b3e10
#define KUI_TOY_BOOT_HEAP_POOL 0x8c04e9b4
#define KUI_TOY_BOOT_HEAP_BEGIN_VALUE 0x8c124340
#define KUI_TOY_BOOT_HEAP_BYTES_VALUE 0x00edbcc0
#define KUI_TOY_BOOT_LEASE_BEGIN 0x8cfd0000
#define KUI_TOY_BOOT_LEASE_BYTES 0x00030000
#define KUI_TOY_BOOT_WORKER_LIMIT 0x8cfe0000

/* Exact title reset callback registration. This callback runs after the
 * game's reset decision but before its sound/FS/cache teardown. The ordinary
 * reader and every other title keep their original reset route. */
#define KUI_TOY_BOOT_RESET_CALLBACK_POOL 0x8c027a24
#define KUI_TOY_BOOT_RESET_CALLBACK_NATIVE 0x8c027104
#define KUI_TOY_BOOT_RESET_REGISTER_SITE 0x8c02798e
#define KUI_TOY_BOOT_RESET_REGISTER_WORD 0x2322
#define KUI_TOY_BOOT_RESET_INVOKE_SITE 0x8c04f1ee
#define KUI_TOY_BOOT_RESET_INVOKE_WORD 0x430b
#define KUI_TOY_BOOT_CACHE_POLICY_POOL 0x8c0c5bc4
#define KUI_TOY_BOOT_CACHE_POLICY_NATIVE 0x00000105
#define KUI_TOY_BOOT_CACHE_POLICY_SAFE 0x00000101
#define KUI_TOY_BOOT_PAUSE_POOL 0x8c049d50
#define KUI_TOY_BOOT_PAUSE_NATIVE 0x8c0b3200

#ifndef __ASSEMBLER__
#include <stddef.h>
#include <stdint.h>
struct kui_toy_pilot_boot_control {
    uint32_t installer, stage_stack, status, lease, error;
    uint32_t heap_begin, heap_bytes, entry_sp;
    uint32_t request, snapshot, worker_end, original_pr;
    uint32_t gd_dispatch;
#if KUI_TOY_PILOT_LOADER_TRACE
    uint32_t trace_terminal;
#endif
};
_Static_assert(sizeof(struct kui_toy_pilot_boot_control)==KUI_TOY_BOOT_CONTROL_BYTES,
    "Toy low bootstrap control ABI");
_Static_assert(offsetof(struct kui_toy_pilot_boot_control,request)==KUI_TOY_BOOT_REQUEST,
    "Toy request publication ABI");
extern volatile struct kui_toy_pilot_boot_control kui_toy_pilot_boot_control;
void kui_toy_pilot_heap_hook(void);
void kui_toy_pilot_return_hook(void) __attribute__((noreturn));
enum kui_toy_pilot_boot_status {
    KUI_TOY_BOOT_EMPTY, KUI_TOY_BOOT_ARMED, KUI_TOY_BOOT_INITIALIZING,
    KUI_TOY_BOOT_LEASED, KUI_TOY_BOOT_INSTALLED, KUI_TOY_BOOT_FAILED
};
#endif
#endif
