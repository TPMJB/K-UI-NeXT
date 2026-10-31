/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_NATIVE_EXIT_H
#define KUI_VMU_NATIVE_EXIT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "kui/vmu_native_patch.h"
#define KUI_VMU_NATIVE_EXIT_VERSION 2u
#define KUI_VMU_NATIVE_EXIT_FAMILY 1u
#define KUI_VMU_NATIVE_EXIT_PVR_CLEAR 2u
#define KUI_VMU_NATIVE_EXIT_PVR_COMPACT 3u
#define KUI_VMU_NATIVE_EXIT_PAIR_ONLY 4u
#define KUI_VMU_NATIVE_EXIT_TERMINAL_DISPATCH 5u
#define KUI_VMU_NATIVE_EXIT_CRT_BOOTSTRAP 6u
/* pd 1.46 (syBt 1.04): two adjacent exits sharing one BIOS-vector literal.
 * The first is only eight bytes long, so it takes an eight-byte head that
 * jumps through the second exit's standard veneer literal. */
#define KUI_VMU_NATIVE_EXIT_EARLY_PAIR 7u
#define KUI_VMU_NATIVE_EXIT_ENTRIES 4u
struct kui_vmu_native_exit {
    uint32_t version,bytes,family,load,image_bytes,count;
    /* Terminal entry patches: SDK exit, cleanup/exit, executable loader,
     * and (CRT_BOOTSTRAP only) a separately proved CRT bootstrap entry.
     * Every entry has a 12-byte safe window. PVR variants may have a
     * halfword-aligned cleanup/exit entry; installers must encode their
     * PC-relative terminal literal for that actual alignment. */
    uint32_t entries[KUI_VMU_NATIVE_EXIT_ENTRIES];
    uint32_t bios_vector_literal;
    /* The family tag selects the meaning of this wire-compatible word.
     * TERMINAL_DISPATCH has the two ordinary SDK exits plus a separately
     * proved fatal allocator handler in entries[2]. It has no exec thunk. */
    union {
        uint32_t exec_thunk;
        struct {uint32_t table;} terminal_dispatch;
    };
    uint32_t cleanup;
};
/* One canonical family/count contract shared by cache and runtime admission.
 * Unused entries must be zero; the fourth entry is never an inferred hook. */
static inline bool kui_vmu_native_exit_shape_valid(const struct kui_vmu_native_exit *e) {
    if(!e || e->version!=KUI_VMU_NATIVE_EXIT_VERSION || e->bytes!=sizeof(*e)) return false;
    if(e->family==KUI_VMU_NATIVE_EXIT_PAIR_ONLY)
        return e->count==2u && !e->entries[2] && !e->entries[3] && !e->exec_thunk;
    if(e->family==KUI_VMU_NATIVE_EXIT_EARLY_PAIR)
        return e->count==2u && e->entries[0] && e->entries[1]==e->entries[0]+8u &&
            !e->entries[2] && !e->entries[3] && !e->exec_thunk;
    if(e->family==KUI_VMU_NATIVE_EXIT_TERMINAL_DISPATCH)
        return e->count==3u && e->entries[2] && !e->entries[3] && e->terminal_dispatch.table;
    if(e->family==KUI_VMU_NATIVE_EXIT_CRT_BOOTSTRAP)
        return e->count==4u && e->entries[3];
    return (e->family==KUI_VMU_NATIVE_EXIT_FAMILY ||
        e->family==KUI_VMU_NATIVE_EXIT_PVR_CLEAR || e->family==KUI_VMU_NATIVE_EXIT_PVR_COMPACT) &&
        e->count==3u && !e->entries[3];
}
/* Bytes a hook replaces at entry i: twelve, except the early pair's first
 * entry, whose eight-byte head ends where the second entry's veneer begins. */
static inline uint32_t kui_vmu_native_exit_window(const struct kui_vmu_native_exit *e,unsigned i) {
    return e->family==KUI_VMU_NATIVE_EXIT_EARLY_PAIR && !i?8u:12u;
}
/* Fresh immutable-image discovery for three strict SDK exit/exec shapes or
 * an exact legacy pair with no additional encoded firmware consumers and no
 * reviewed loader prologue. PAIR_ONLY returns count=2, entries[2]=0 and
 * exec_thunk=0. TERMINAL_DISPATCH returns count=3 with a separately proved
 * fatal allocator handler and a tagged alternate dispatch table; it cannot
 * authorize that table before the fatal handler is terminally intercepted.
 * CRT_BOOTSTRAP returns count=4: the three ordinary SDK entries and the
 * complete extra bootstrap entry, before its stack/FPSCR/BSS changes.
 * Other families return count=3. The complete admitted
 * exit/cleanup/loader shapes and decoded platform literals must agree;
 * additional encoded PC-relative BIOS-vector/bootstrap consumers are refused.
 * Every returned entry window passes the conservative interior-target guard.
 * Does not install hooks or prove arbitrary synthesized/indirect control flow.
 * Caller must terminally intercept all returned entries before gameplay. Out
 * clears on failure except overlapping image/output, which is untouched. */
bool kui_vmu_native_exit_discover(const void *image,size_t bytes,uint32_t load,
    struct kui_vmu_native_exit *out);
/* The same complete discovery, with up to five additional disjoint patch
 * windows checked in the exit windows' immutable-image pass. No checks are
 * omitted: every returned exit and every caller window passes one combined
 * interior-target guard. Additional-window/output overlap is refused without
 * writes, like image/output overlap. */
bool kui_vmu_native_exit_discover_guarded(const void *image,size_t bytes,uint32_t load,
    const struct kui_vmu_native_patch_window *additional,size_t count,
    struct kui_vmu_native_exit *out);
#endif
