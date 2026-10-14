/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_ARMADA_CE_EXCERPT_H
#define KUI_ARMADA_CE_EXCERPT_H
#include "kui/armada_ce_excerpt_pool.h"
#include <stdint.h>

/* The native stereo looping Play contract requires a 64-byte boundary.
 * Verify the whole 150-sector source, but omit its last eight stereo frames
 * from playback so its native loop length is valid (32 bytes / 0.1814 ms). */
#define KUI_ARMADA_CE_EXCERPT_PLAY_BYTES (KUI_ARMADA_CE_EXCERPT_PCM_BYTES&~63u)
_Static_assert(KUI_ARMADA_CE_EXCERPT_PLAY_BYTES>0u &&
    KUI_ARMADA_CE_EXCERPT_PLAY_BYTES%64u==0u &&
    KUI_ARMADA_CE_EXCERPT_PCM_BYTES-KUI_ARMADA_CE_EXCERPT_PLAY_BYTES==32u,
    "native stereo loop alignment");

/* Exact Armada-only, caller-owned DirectSound excerpt attempt. This is not a
 * full CDDA streamer. All addresses in this record must come from the live
 * identity binder; neither nominal ROM addresses nor physical aliases are
 * callable substitutions. revalidate is an authored integer-only callback
 * that repeats the binding/identity checks in the deferred caller context. */
struct kui_armada_ce_excerpt_binding {
    uint32_t owner_process,owner_slot,device,device_vtable;
    uint32_t add_ref,device_release,create_buffer,buffer_vtable;
    uint32_t buffer_release,get_caps,lock,unlock;
    uint32_t play,stop,get_status,get_position;
    uint32_t enter_cs,leave_cs,revalidate;
};
_Static_assert(sizeof(struct kui_armada_ce_excerpt_binding)==19u*4u,"excerpt binding ABI");
typedef int (*kui_armada_ce_excerpt_revalidate_fn)(
    const struct kui_armada_ce_excerpt_binding *);

enum kui_armada_ce_excerpt_status {
    KUI_ARMADA_CE_EXCERPT_IDLE, KUI_ARMADA_CE_EXCERPT_QUEUED,
    KUI_ARMADA_CE_EXCERPT_PLAYING, KUI_ARMADA_CE_EXCERPT_PAUSED,
    KUI_ARMADA_CE_EXCERPT_STOPPED, KUI_ARMADA_CE_EXCERPT_RELEASED,
    KUI_ARMADA_CE_EXCERPT_ARGUMENT=16, KUI_ARMADA_CE_EXCERPT_OWNER,
    KUI_ARMADA_CE_EXCERPT_CONTEXT, KUI_ARMADA_CE_EXCERPT_IDENTITY,
    KUI_ARMADA_CE_EXCERPT_POOL, KUI_ARMADA_CE_EXCERPT_REFERENCE,
    KUI_ARMADA_CE_EXCERPT_BUFFER, KUI_ARMADA_CE_EXCERPT_CAPS,
    KUI_ARMADA_CE_EXCERPT_LOCK, KUI_ARMADA_CE_EXCERPT_COPY,
    KUI_ARMADA_CE_EXCERPT_UNLOCK, KUI_ARMADA_CE_EXCERPT_VOICES,
    KUI_ARMADA_CE_EXCERPT_PLAY, KUI_ARMADA_CE_EXCERPT_NATIVE_STOP,
    KUI_ARMADA_CE_EXCERPT_CALLBACK
};
struct kui_armada_ce_excerpt_snapshot {
    uint32_t status,requests,attempts,play_ok;
    uint32_t refused,caller_sr,caller_fpscr,current_process;
    uint32_t owner_process,device,buffer,caps_flags;
    uint32_t caps_bytes,lock_bytes,free_voices,last_hresult;
    uint32_t pcm_crc32,busy_skips,stop_ok,release_ok;
    uint32_t play_cursor,write_cursor,position_ok,command;
    uint32_t first,last,repeat,device_reference;
};
_Static_assert(sizeof(struct kui_armada_ce_excerpt_snapshot)==28u*4u,"excerpt snapshot ABI");
/* Called only with the GD entry lock held and interrupts masked, after an
 * accepted audio request. Returns1 when this pending command needs the
 * separately owned deferred callback. Never overwrites a running attempt;
 * the integration must give an existing PIO deferred callback priority. */
int kui_armada_ce_excerpt_queue(const struct kui_armada_ce_excerpt_binding *,
    uint32_t command,uint32_t first,uint32_t last,uint32_t repeat);
void kui_armada_ce_excerpt_snapshot(struct kui_armada_ce_excerpt_snapshot *);

/* KUI's lock is already released and WSEGACD's SetKMode(true) is active.
 * The integer outer bridge uses the exact kernel's native owner callback.
 * The inner bridge lets CE's lazy FPU handler acquire this thread's state,
 * then preserves both banks, FPUL and FPSCR. Non-scalar FPSCR is refused.
 * Native callback return restores the driver's original process and keys.
 * No adapter KData/TTB writes, SetKMode, ARM reset or G2 operation occurs. */
void kui_armada_ce_excerpt_deferred(uint32_t unused);
void kui_armada_ce_excerpt_dispatch(uint32_t sr,uint32_t stack);
uint32_t kui_armada_ce_excerpt_owner_callback(uint32_t token);
void kui_armada_ce_excerpt_owner_deferred(uint32_t unused);
void kui_armada_ce_excerpt_attempt(uint32_t sr,uint32_t fpscr);

/* Authored GCC-to-Microsoft-SH ABI bridge: exactly8 argument words, four
 * register arguments plus four above the16-byte register home area. */
uint32_t kui_armada_ce_excerpt_ce_call(uint32_t target,const uint32_t args[8]);
#if defined(KUI_ARMADA_CE_STREAM) && KUI_ARMADA_CE_STREAM == 1
struct kui_armada_ce_live_stream_api;
/* Integer GD-mailbox query: no binding, allocation, storage or native call.
 * A busy PIO deferred slot may defer bootstrap, without losing commands. */
int kui_armada_ce_stream_needs_deferred(void);
/* Cached diagnostics only: fetched source bytes modulo2^32, recovery count,
 * native worker lease status. No guest/native/storage read is performed. */
void kui_armada_ce_stream_progress(uint32_t out[3]);
/* Cached first failure only: reason, address, observed, expected, detail. */
void kui_armada_ce_stream_fault(uint32_t out[5]);
int kui_armada_ce_stream_bind_deferred(struct kui_armada_ce_excerpt_binding *);
int kui_armada_ce_stream_bind_native(struct kui_armada_ce_live_stream_api *);
/* Native ThreadBase enters this integer C function only through the owned
 * CE P0 veneer. Every player step uses the existing full-FPU owner bridge. */
uint32_t kui_armada_ce_stream_worker(uint32_t argument);
#endif
#endif
