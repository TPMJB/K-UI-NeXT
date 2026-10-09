/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_CDDA_DISC_BIOS_H
#define KUI_CDDA_DISC_BIOS_H
#include "kui/cdda_bios.h"
#include "kui/cdda_disc.h"

/* A new disc-bound controlled queue; accepted profiles00..10 are unchanged. */
#define KUI_CDDA_DISC_BIOS_MAX_SECTORS 16u
#define KUI_CDDA_DISC_BIOS_CHUNK_BYTES 2048u
#define KUI_CDDA_DISC_BIOS_MAX_BYTES 32768u
#define KUI_CDDA_DISC_BIOS_PLAY KUI_CDDA_BIOS_PLAY
#define KUI_CDDA_DISC_BIOS_PAUSE KUI_CDDA_BIOS_PAUSE
#define KUI_CDDA_DISC_BIOS_RELEASE KUI_CDDA_BIOS_RELEASE
#define KUI_CDDA_DISC_BIOS_MAP_VALIDATE KUI_CDDA_BIOS_MAP_VALIDATE

enum kui_cdda_disc_bios_state {KUI_CDDA_DISC_BIOS_EMPTY,KUI_CDDA_DISC_BIOS_QUEUED,
    KUI_CDDA_DISC_BIOS_RUNNING,KUI_CDDA_DISC_BIOS_TERMINAL};
enum kui_cdda_disc_bios_kind {KUI_CDDA_DISC_BIOS_AUDIO,KUI_CDDA_DISC_BIOS_DATA,KUI_CDDA_DISC_BIOS_NOOP,KUI_CDDA_DISC_BIOS_TOC};
enum kui_cdda_disc_bios_result {KUI_CDDA_DISC_BIOS_OK,KUI_CDDA_DISC_BIOS_INVALID,
    KUI_CDDA_DISC_BIOS_NOTHING,KUI_CDDA_DISC_BIOS_BUSY,KUI_CDDA_DISC_BIOS_STALE,KUI_CDDA_DISC_BIOS_OVERFLOW};
struct kui_cdda_disc_bios_ops {
    void *context;
    /* Numeric guest aliases normalize to P1 after full bounds validation.
     * writing0 copies params,1 writes outputs,2 validates ownership only.
     * Ownership validation never touches guest memory, caches or hardware. */
    uint8_t *(*map)(void *,uint32_t address,uint32_t bytes,int writing);
};
struct kui_cdda_disc_bios_work {
    uint32_t handle,epoch,command;
    enum kui_cdda_disc_bios_kind kind;
    uint32_t track,offset,destination,bytes;
    struct kui_cdda_control_request audio;
    uint32_t total_bytes,committed,chunk;
    uint32_t fad,area;
};
struct kui_cdda_disc_bios {
    struct kui_cdda_disc_bios_ops ops;
    const struct kui_cdda_disc_map *disc;
    uint32_t guest_first,guest_end,handle_generation,epoch_generation,chunk_generation;
    uint32_t error,completed_bytes,drive_status;
    enum kui_cdda_disc_bios_state state;
    int32_t status;
    /* work is the immutable FULL submitted request. pending is the exact
     * active chunk claim, and exists only while RUNNING. */
    struct kui_cdda_disc_bios_work work,pending;
    bool initialized;
};
enum kui_cdda_disc_bios_result kui_cdda_disc_bios_init(struct kui_cdda_disc_bios *,
    const struct kui_cdda_disc_bios_ops *,const struct kui_cdda_disc_map *,
    uint32_t guest_first,uint32_t guest_end);
/* Same controlled GD function/command subset as08: r6=0,r7=function,
 * REQUEST(command,params) copies and validates before returning handle>0/0.
 * PLAY20 {same backed audio track number,number,repeat0/15};PAUSE22/RELEASE23/STOP33/NOP29 have no params.
 * PLAY21 is refused pending native endpoint discovery. GETTOC2(19)
 * {area0/1,destination} claims one complete408B derived TOC. READ16
 * {FAD,count1..16,destination,test0} validates the complete FAD span
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
int32_t kui_cdda_disc_bios_dispatch(struct kui_cdda_disc_bios *,
    uint32_t r4,uint32_t r5,uint32_t r6,uint32_t r7);
/* Claim retains handle/epoch/full request total, gives a fresh nonwrapping
 * chunk ID, committed prefix, current virtual payload offset/destination/bytes and current FAD.
 * work.bytes remains total in owner.work, but is2048 in a DATA claim or408 in a TOC claim.
 * No I/O. Output cannot overlap owner state; refusal leaves it unchanged. */
enum kui_cdda_disc_bios_result kui_cdda_disc_bios_take(struct kui_cdda_disc_bios *,
    struct kui_cdda_disc_bios_work *);
/* Pure exact-field check against pending, including chunk and prefix. It is
 * mandatory before guest mapping/I/O and again after the full service lease.
 * Old chunks of the SAME request cannot commit or trigger new physical work. */
enum kui_cdda_disc_bios_result kui_cdda_disc_bios_validate_work(const struct kui_cdda_disc_bios *,
    const struct kui_cdda_disc_bios_work *);
/* After matching full-lease success, report exactly claim.bytes (2048 for a
 * data chunk,408 for TOC,0 for audio/NOP). Only this call publishes prefix progress. OK
 * returns QUEUED for another chunk or TERMINAL when the full request is done.
 * Failure reports0 accepted bytes for the current chunk, preserves earlier
 * committed bytes and produces terminal IO; discard bytes after that prefix.
 * RUNNING fatal cleanup uses owner.pending, never immutable owner.work. */
enum kui_cdda_disc_bios_result kui_cdda_disc_bios_complete(struct kui_cdda_disc_bios *,
    const struct kui_cdda_disc_bios_work *,bool success,uint32_t bytes);
/* Pure DRIVE snapshot from actual checked audio state. EOF→paused1,FAULT→9;
 * completion/reset of data requests does not change this audio snapshot. */
enum kui_cdda_disc_bios_result kui_cdda_disc_bios_observe_audio(struct kui_cdda_disc_bios *,
    enum kui_cdda_control_state);
#endif
