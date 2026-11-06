/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SMASH_PHASE_TRACE_H
#define KUI_SMASH_PHASE_TRACE_H
#include <stdbool.h>
#include "kui/smash_phase_clock.h"
#define KUI_SMASH_PHASE_TRACE_MAGIC0 UINT32_C(0x5349554b)
#define KUI_SMASH_PHASE_TRACE_MAGIC1 UINT32_C(0x314d4954)
#define KUI_SMASH_PHASE_TRACE_BYTES 992u
#define KUI_SMASH_PHASE_TRACE_CAPACITY 24u
enum kui_smash_phase {
 KUI_SMASH_PHASE_MDE_FIRST_WRITE=1, KUI_SMASH_PHASE_POST_GIN=2,
 KUI_SMASH_PHASE_CAPSULE_BEGIN=3, KUI_SMASH_PHASE_CAPSULE_LOADED=4,
 KUI_SMASH_PHASE_CHILD_JUMP=5, KUI_SMASH_PHASE_STAGE_BEGIN=10,
 KUI_SMASH_PHASE_CARD_READY=11, KUI_SMASH_PHASE_FS_BEGIN=12,
 KUI_SMASH_PHASE_FS_DONE=13, KUI_SMASH_PHASE_SESSION_DONE=14,
 KUI_SMASH_PHASE_CHECKPOINT_DONE=15, KUI_SMASH_PHASE_REFRESH_DONE=16,
 KUI_SMASH_PHASE_STORAGE_DONE=17, KUI_SMASH_PHASE_IP_DONE=18,
 KUI_SMASH_PHASE_MDE_DONE=19, KUI_SMASH_PHASE_BOOT2_BEGIN=20,
 KUI_SMASH_PHASE_RELAY_BEGIN=21, KUI_SMASH_PHASE_RELAY_VERIFIED=22,
 KUI_SMASH_PHASE_RESOLVE_BEGIN=23, KUI_SMASH_PHASE_RESOLVE_DONE=24,
 KUI_SMASH_PHASE_NATIVE_READY=25,
 KUI_SMASH_PHASE_INSTALL_BEGIN=26, KUI_SMASH_PHASE_WORKER_RELOCATED=27,
 KUI_SMASH_PHASE_INSTALL_DONE=28
};
struct kui_smash_phase_entry {uint32_t phase,reserved;struct kui_smash_phase_stamp stamp;};
struct kui_smash_phase_trace {
 uint32_t magic0,magic1,version,bytes,count,overflow,reserved[2];
 struct kui_smash_phase_entry entries[KUI_SMASH_PHASE_TRACE_CAPACITY];
};
_Static_assert(sizeof(struct kui_smash_phase_trace)==KUI_SMASH_PHASE_TRACE_BYTES,"Phase trace ABI");
void kui_smash_phase_trace_init(struct kui_smash_phase_trace *);
bool kui_smash_phase_trace_valid(const struct kui_smash_phase_trace *);
bool kui_smash_phase_trace_blank(const struct kui_smash_phase_trace *);
/* Only an entirely zero BSS trace is initialized implicitly. Corrupt typed
 * records refuse; markers must be strictly increasing and bounded. */
bool kui_smash_phase_trace_mark(struct kui_smash_phase_trace *,uint32_t);
#endif
