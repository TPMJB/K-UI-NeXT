/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_CDDA_BIOS_BATCH_H
#define KUI_CDDA_BIOS_BATCH_H
#include "kui/cdda_bios.h"

/* A new controlled queue; the accepted profile08 core remains unchanged. */
#define KUI_CDDA_BIOS_BATCH_MAX_SECTORS 16u
#define KUI_CDDA_BIOS_BATCH_CHUNK_BYTES 2048u
#define KUI_CDDA_BIOS_BATCH_MAX_BYTES 32768u
#define KUI_CDDA_BIOS_BATCH_PLAY KUI_CDDA_BIOS_PLAY
#define KUI_CDDA_BIOS_BATCH_PAUSE KUI_CDDA_BIOS_PAUSE
#define KUI_CDDA_BIOS_BATCH_RELEASE KUI_CDDA_BIOS_RELEASE
#define KUI_CDDA_BIOS_BATCH_AUDIO_FIRST_FAD KUI_CDDA_BIOS_AUDIO_FIRST_FAD
#define KUI_CDDA_BIOS_BATCH_AUDIO_END_FAD KUI_CDDA_BIOS_AUDIO_END_FAD
#define KUI_CDDA_BIOS_BATCH_AUDIO_FIRST_FRAME KUI_CDDA_BIOS_AUDIO_FIRST_FRAME
#define KUI_CDDA_BIOS_BATCH_AUDIO_END_FRAME KUI_CDDA_BIOS_AUDIO_END_FRAME
#define KUI_CDDA_BIOS_BATCH_DATA_FIRST_FAD KUI_CDDA_BIOS_DATA_FIRST_FAD
#define KUI_CDDA_BIOS_BATCH_DATA_END_FAD KUI_CDDA_BIOS_DATA_END_FAD
#define KUI_CDDA_BIOS_BATCH_DATA_BYTES KUI_CDDA_BIOS_DATA_BYTES
#define KUI_CDDA_BIOS_BATCH_MAP_VALIDATE KUI_CDDA_BIOS_MAP_VALIDATE

enum kui_cdda_bios_batch_state {KUI_CDDA_BIOS_BATCH_EMPTY,KUI_CDDA_BIOS_BATCH_QUEUED,
    KUI_CDDA_BIOS_BATCH_RUNNING,KUI_CDDA_BIOS_BATCH_TERMINAL};
enum kui_cdda_bios_batch_kind {KUI_CDDA_BIOS_BATCH_AUDIO,KUI_CDDA_BIOS_BATCH_DATA,KUI_CDDA_BIOS_BATCH_NOOP};
enum kui_cdda_bios_batch_result {KUI_CDDA_BIOS_BATCH_OK,KUI_CDDA_BIOS_BATCH_INVALID,
    KUI_CDDA_BIOS_BATCH_NOTHING,KUI_CDDA_BIOS_BATCH_BUSY,KUI_CDDA_BIOS_BATCH_STALE,KUI_CDDA_BIOS_BATCH_OVERFLOW};
struct kui_cdda_bios_batch_ops {
    void *context;
    /* Numeric guest aliases normalize to P1 after full bounds validation.
     * writing0 copies params,1 writes outputs,2 validates ownership only.
     * Ownership validation never touches guest memory, caches or hardware. */
    uint8_t *(*map)(void *,uint32_t address,uint32_t bytes,int writing);
};
struct kui_cdda_bios_batch_work {
    uint32_t handle,epoch,command;
    enum kui_cdda_bios_batch_kind kind;
    uint32_t track,offset,destination,bytes;
    struct kui_cdda_control_request audio;
    uint32_t total_bytes,committed,chunk;
};
struct kui_cdda_bios_batch {
    struct kui_cdda_bios_batch_ops ops;
    uint32_t guest_first,guest_end,handle_generation,epoch_generation,chunk_generation;
    uint32_t error,completed_bytes,drive_status;
    enum kui_cdda_bios_batch_state state;
    int32_t status;
    /* work is the immutable FULL submitted request. pending is the exact
     * active chunk claim, and exists only while RUNNING. */
    struct kui_cdda_bios_batch_work work,pending;
    bool initialized;
};
enum kui_cdda_bios_batch_result kui_cdda_bios_batch_init(struct kui_cdda_bios_batch *,
    const struct kui_cdda_bios_batch_ops *,uint32_t source_frames,uint32_t data_bytes,
    uint32_t guest_first,uint32_t guest_end);
/* Same controlled GD function/command subset as08: r6=0,r7=function,
 * REQUEST(command,params) copies and validates before returning handle>0/0.
 * PLAY20 {1,1,repeat0/15};PAUSE22/RELEASE23/STOP33/NOP29 have no params.
 * READ16 {FAD,count1..16,destination,test0} validates the complete FAD span
 * and complete count*2048-byte output before admission. The complete request
 * must fit the remaining nonwrapping chunk identities. EXEC returns0 and
 * native owner claims at most one2048-byte chunk for that call.
 * Between committed chunks state is QUEUED and CHECK returns PROCESSING1
 * with {err1=0,err2=0,committed_prefix_bytes,ATA4}. Terminal COMPLETE2 or
 * FAILED-1 has ATA0 and is acknowledged exactly once; unknown handles0.
 * ABORT only between physical calls (QUEUED): FAILED/CANCELLED retains the
 * committed prefix, including zero before dispatch. RUNNING reentry refuses
 * REQUEST0/CHECK4/others-1 before mapping any pointer. INIT/RESET invalidate
 * only this queue and never reset audio/control/data-token namespaces.
 * REQUEST/CHECK/DRIVE/ABORT/INIT/RESET perform no SD or AICA operations. */
int32_t kui_cdda_bios_batch_dispatch(struct kui_cdda_bios_batch *,
    uint32_t r4,uint32_t r5,uint32_t r6,uint32_t r7);
/* Claim retains handle/epoch/full request total, gives a fresh nonwrapping
 * chunk ID, committed prefix, current physical offset/destination/bytes.
 * work.bytes remains total in owner.work, but is<=2048 in the returned claim.
 * No I/O. Output cannot overlap owner state; refusal leaves it unchanged. */
enum kui_cdda_bios_batch_result kui_cdda_bios_batch_take(struct kui_cdda_bios_batch *,
    struct kui_cdda_bios_batch_work *);
/* Pure exact-field check against pending, including chunk and prefix. It is
 * mandatory before guest mapping/I/O and again after the full service lease.
 * Old chunks of the SAME request cannot commit or trigger new physical work. */
enum kui_cdda_bios_batch_result kui_cdda_bios_batch_validate_work(const struct kui_cdda_bios_batch *,
    const struct kui_cdda_bios_batch_work *);
/* After matching full-lease success, report exactly claim.bytes (2048 for a
 * data chunk,0 for audio/NOP). Only this call publishes prefix progress. OK
 * returns QUEUED for another chunk or TERMINAL when the full request is done.
 * Failure reports0 accepted bytes for the current chunk, preserves earlier
 * committed bytes and produces terminal IO; discard bytes after that prefix.
 * RUNNING fatal cleanup uses owner.pending, never immutable owner.work. */
enum kui_cdda_bios_batch_result kui_cdda_bios_batch_complete(struct kui_cdda_bios_batch *,
    const struct kui_cdda_bios_batch_work *,bool success,uint32_t bytes);
/* Pure DRIVE snapshot from actual checked audio state. EOF→paused1,FAULT→9;
 * completion/reset of data requests does not change this audio snapshot. */
enum kui_cdda_bios_batch_result kui_cdda_bios_batch_observe_audio(struct kui_cdda_bios_batch *,
    enum kui_cdda_control_state);
#endif
