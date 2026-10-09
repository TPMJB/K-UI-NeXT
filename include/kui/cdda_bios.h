/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_CDDA_BIOS_H
#define KUI_CDDA_BIOS_H
#include <stdbool.h>
#include <stdint.h>
#include "kui/cdda_control.h"
#include "kui/gd_service.h"

/* A separate controlled-client queue. No stable retail reader changes and no
 * title hook. IDs/PLAY fields follow pinned KOS syscalls.h; PLAY2 sector-end
 * fidelity, finite repeat counts and other commands are deliberately refused. */
#define KUI_CDDA_BIOS_PLAY 20u
#define KUI_CDDA_BIOS_PAUSE 22u
#define KUI_CDDA_BIOS_RELEASE 23u
#define KUI_CDDA_BIOS_AUDIO_FIRST_FAD 150u
#define KUI_CDDA_BIOS_AUDIO_END_FAD 225u
#define KUI_CDDA_BIOS_AUDIO_FIRST_FRAME 268128u /* Fixture sector456. */
#define KUI_CDDA_BIOS_AUDIO_END_FRAME 312228u
#define KUI_CDDA_BIOS_DATA_FIRST_FAD 45150u
#define KUI_CDDA_BIOS_DATA_END_FAD 49246u
#define KUI_CDDA_BIOS_DATA_BYTES (8u*1024u*1024u)
#define KUI_CDDA_BIOS_MAP_VALIDATE 2

enum kui_cdda_bios_state {KUI_CDDA_BIOS_EMPTY,KUI_CDDA_BIOS_QUEUED,
    KUI_CDDA_BIOS_RUNNING,KUI_CDDA_BIOS_TERMINAL};
enum kui_cdda_bios_kind {KUI_CDDA_BIOS_AUDIO,KUI_CDDA_BIOS_DATA,KUI_CDDA_BIOS_NOOP};
enum kui_cdda_bios_result {KUI_CDDA_BIOS_OK,KUI_CDDA_BIOS_INVALID,
    KUI_CDDA_BIOS_NOTHING,KUI_CDDA_BIOS_BUSY,KUI_CDDA_BIOS_STALE,KUI_CDDA_BIOS_OVERFLOW};
struct kui_cdda_bios_ops {
    void *context;
    /* Numeric main-RAM aliases are validated then normalized to cached P1.
     * writing=0 reads copied params;1 writes status/data;2 checks ownership
     * only, with no memory/cache/storage/hardware effects or dereference.
     * Platforms using caller aliases must establish cache coherence separately;
     * the controlled native client exercises cached P1 buffers only. */
    uint8_t *(*map)(void *,uint32_t address,uint32_t bytes,int writing);
};
struct kui_cdda_bios_work {
    uint32_t handle,epoch,command;
    enum kui_cdda_bios_kind kind;
    uint32_t track,offset,destination,bytes;
    struct kui_cdda_control_request audio;
};
struct kui_cdda_bios {
    struct kui_cdda_bios_ops ops;
    uint32_t guest_first,guest_end,handle_generation,epoch_generation;
    uint32_t error,completed_bytes,drive_status;
    enum kui_cdda_bios_state state;
    int32_t status;
    struct kui_cdda_bios_work work;
    bool initialized;
};

/* Immutable synthetic map: track1 audio FAD[150,225) references fixture
 * frames[268128,312228); track2 data FAD[45150,49246) references stress.bin.
 * source_frames must cover the audio range; data_bytes must be exactly8MiB.
 * Guest bounds are P1-only, excluding firmware and all engine-owned ranges. */
enum kui_cdda_bios_result kui_cdda_bios_init(struct kui_cdda_bios *,
    const struct kui_cdda_bios_ops *,uint32_t source_frames,uint32_t data_bytes,
    uint32_t guest_first,uint32_t guest_end);
/* Actual GD registers: r6=0,r7=function; REQUEST r4=command,r5=params.
 * PLAY20 params12B {first_track=1,last_track=1,repeat=0 or15}; PAUSE22,
 * RELEASE23,STOP33,NOP29 have no params. READ16 params16B
 * {FAD,count=1,destination,test=0}; whole2048B output checked on submission.
 * REQUEST copies params and returns a positive nonwrapping signed handle or0.
 * EXEC returns0; the native adapter then calls take/validates/executes/complete.
 * CHECK r4=handle,r5=16B output {err1,err2,bytes,ATA}: PROCESSING1,busy4;
 * terminal COMPLETE2/FAILED-1,ATA0 acknowledged exactly once; unknown0.
 * DRIVE r4=8B output {busy0/paused1/standby2/playing3/error9,GD-ROM0x80}.
 * INIT/RESET invalidate this queue only; ongoing audio is unchanged. ABORT
 * cancels only QUEUED and retains FAILED/CANCELLED until terminal CHECK.
 * Submit/poll/reset/abort perform no SD/AICA work. RUNNING reentry refuses
 * REQUEST0,CHECK4,others-1 before mapping any guest pointer. */
int32_t kui_cdda_bios_dispatch(struct kui_cdda_bios *,uint32_t r4,uint32_t r5,uint32_t r6,uint32_t r7);
/* Native EXEC owner claims one copied authoritative work item. Its BIOS handle,
 * queue epoch, audio-control epoch and data-job token are different lifetimes.
 * No I/O here. Output must not overlap queue owner state. */
enum kui_cdda_bios_result kui_cdda_bios_take(struct kui_cdda_bios *,struct kui_cdda_bios_work *);
/* Pure exact-field preflight, required before platform operations and again on
 * complete. Stale/forged/canceled copies cannot affect hardware or newer work. */
enum kui_cdda_bios_result kui_cdda_bios_validate_work(const struct kui_cdda_bios *,
    const struct kui_cdda_bios_work *);
/* Success reports exactly2048 bytes for READ and0 for control/NOP. Failure
 * reports0 accepted bytes: discard any partially touched read destination.
 * The native owner performs bounded stop/fault cleanup on physical failure. */
enum kui_cdda_bios_result kui_cdda_bios_complete(struct kui_cdda_bios *,
    const struct kui_cdda_bios_work *,bool success,uint32_t bytes);
/* Update the pure DRIVE snapshot from the engine's checked actual state, never
 * by querying AICA during REQUEST/CHECK. EOF reports paused;FAULT reports9. */
enum kui_cdda_bios_result kui_cdda_bios_observe_audio(struct kui_cdda_bios *,enum kui_cdda_control_state);
#endif
