/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_PERSIST_SESSION_H
#define KUI_VMU_PERSIST_SESSION_H
#include <stdbool.h>
#include "kui/vmu_fs_admission.h"
#include "kui/vmu_image.h"
#include "kui/vmu_store.h"
#include "kui/vmu_fs_witness.h"

struct kui_vmu_persist_session_identity {
    uint8_t cid[16];
    uint64_t card_sectors;
    uint32_t high_capacity;
};
/* The caller exclusively owns one initialized, connected device for the
 * entire session. Every callback uses that SAME device/context, is serialized,
 * retains no buffer, and returns zero on success (healthy returns true).
 * identity obtains actual CID/capacity/HC, never merely cached expected data.
 * sync has the durability/ordering contract of kui_vmu_store_io. This API has
 * no typed card, filesystem or lease pointer; the caller owns context and its
 * lifetime, including exclusive device access until the session is retired. */
struct kui_vmu_persist_session_ops {
    void *context;
    int (*identity)(void *,struct kui_vmu_persist_session_identity *);
    int (*read_sector)(void *,uint64_t,void *);
    int (*write_sector)(void *,uint64_t,const void *);
    int (*sync)(void *);
    bool (*healthy)(void *);
};
enum kui_vmu_persist_session_result {
    KUI_VMU_PERSIST_SESSION_OK=0, KUI_VMU_PERSIST_SESSION_ARGUMENT=-1,
    KUI_VMU_PERSIST_SESSION_BLOCKED=-2, KUI_VMU_PERSIST_SESSION_BUSY=-3,
    KUI_VMU_PERSIST_SESSION_IDENTITY=-4, KUI_VMU_PERSIST_SESSION_IO=-5,
    KUI_VMU_PERSIST_SESSION_ALLOCATION=-6, KUI_VMU_PERSIST_SESSION_STORE=-7,
    KUI_VMU_PERSIST_SESSION_IMAGE=-8, KUI_VMU_PERSIST_SESSION_VERIFY=-9,
    KUI_VMU_PERSIST_SESSION_BOUNDS=-10, KUI_VMU_PERSIST_SESSION_WITNESS=-11
};
enum kui_vmu_persist_session_phase {
    KUI_VMU_PERSIST_SESSION_IDLE=0, KUI_VMU_PERSIST_SESSION_ADMISSION=1,
    KUI_VMU_PERSIST_SESSION_RECOVERY=2, KUI_VMU_PERSIST_SESSION_VALIDATION=3,
    KUI_VMU_PERSIST_SESSION_READ=4, KUI_VMU_PERSIST_SESSION_COMMIT=5,
    KUI_VMU_PERSIST_SESSION_CHECKPOINT=6, KUI_VMU_PERSIST_SESSION_WITNESS_CHECK=7,
    KUI_VMU_PERSIST_SESSION_REBIND=8
};
/* Static allocation only. Zero-initialize once; never move/copy/edit/reuse a
 * started instance, even after failure. Public caller-supplied API buffers and
 * workspaces must not overlap this object; raw ops receive its private buffers
 * normally. Any operational fault latches permanently. There is no
 * format, retry, implicit checkpoint, allocation or initialization callback.
 * base_sha256 is current only when snapshot_valid=1. block_crc remains current
 * after each successful commit; a whole-image refresh is explicit. */
struct kui_vmu_persist_session {
    const struct kui_vmu_persist_session *self;
    struct kui_vmu_persist_session_ops ops;
    struct kui_vmu_persist_session_identity identity;
    struct kui_vmu_store store;
    struct kui_vmu_store_extent allowed[18];
    size_t image_count,journal_count;
    uint32_t block_crc[256];
    uint8_t base_sha256[32],scratch[512];
    struct kui_vmu_image_summary summary;
    int last_result,fs_result,store_result,image_result;
    uint32_t phase,started,admitted,ready,faulted,busy,callback_active,snapshot_valid;
};
/* Expected is a canonical decoded P2 preparation record. Its hashes describe
 * provenance and are NEVER required to match current saved contents. Fresh
 * filesystem input supplies current identity/partition/path/protected ranges;
 * its read_sector/context are deliberately replaced by a shim over ops, so
 * admission and the bound store cannot accidentally use different backends.
 * Fresh semantic ownership MUST pass before recovery can write. Recovery is
 * followed by read-only general VMU validation and fresh SHA256/block CRCs.
 * Workspaces and input pointers are borrowed only for this call. */
int kui_vmu_persist_session_init(struct kui_vmu_persist_session *,
    const struct kui_vmu_persist_profile *,const struct kui_vmu_fs_input *,
    const struct kui_vmu_persist_session_ops *,struct kui_vmu_fs_workspace *,
    struct kui_vmu_image_workspace *);
/* Collection attachment is a separate entry point, never an option on init.
 * The lifecycle owner supplies expected_* from its LIVE, terminally retired
 * generation receipt established by a complete ordinary ownership admission.
 * They must not be accepted from the persisted witness header itself. The
 * root receipt's binding SHA binds ownership_sha256 plus collection/build;
 * the manifest SHA pins every captured metadata observation. No old session,
 * callback, filesystem object or image-content snapshot is resumed/copied.
 * Header/records are borrowed untrusted bytes; expected_* are trusted inputs. */
struct kui_vmu_persist_attach_source {
    const uint8_t *header;
    const struct kui_vmu_fs_witness_record *records;
    size_t records_bytes;
    struct kui_vmu_fs_witness_binding expected_binding;
    uint8_t expected_manifest_sha256[32],ownership_sha256[32];
};
/* Every callback, including read_run, accesses the SAME initialized device and
 * io.context under one exclusive acquisition across attach and final release.
 * read_run checks each block's ordinary wire CRC and healthy stream stop,
 * retains no buffer, and writes only count*512 bytes to its supplied output. */
struct kui_vmu_persist_attach_ops {
    struct kui_vmu_persist_session_ops io;
    kui_vmu_fs_witness_read_run read_run;
};
/* Internally verifies canonical profile binding, actual identity before/after
 * complete fresh witness replay, then admits the exact maps before any store
 * recovery write. Recovery, full VMU validation and current CRC/SHA collection
 * are identical to init. Scratch is private, disjoint,32B aligned and >=16KiB.
 * A fault latches this new session; neither witness nor caller bool bypasses
 * verification. No local attestation is exposed or reusable after this call. */
int kui_vmu_persist_session_attach(struct kui_vmu_persist_session *,
    const struct kui_vmu_persist_profile *,const struct kui_vmu_persist_attach_source *,
    const struct kui_vmu_persist_attach_ops *,void *scratch,size_t scratch_bytes,
    struct kui_vmu_image_workspace *);
/* A LIVE allocation epoch is distinct from a device/bus acquisition. Its
 * lifecycle owner has proved that no allocation-capable writer ran after
 * admission, and transfers it only after healthy terminal owner retirement.
 * Protocol reinitialization may occur between owners; faults/GUI/reset or an
 * unknown writer end the epoch BEFORE reinitialization can clear bus flags.
 * This local descriptor is never an on-card record or an import interface.
 * domain_binding_sha256 is authenticated by that lifecycle owner against its
 * collection/build domain. A caller must not manufacture it from disk bytes.
 * Rooted lifecycle generation/sequence validation prevents copies/replays;
 * this mutable invocation guard additionally prevents reuse/reentry locally. */
#define KUI_VMU_ALLOCATION_TRANSFER_MAGIC UINT32_C(0x31544156)
#define KUI_VMU_ALLOCATION_TRANSFER_VERSION 1u
#define KUI_VMU_ALLOCATION_TRANSFER_READY 1u
#define KUI_VMU_ALLOCATION_TRANSFER_BUSY 2u
#define KUI_VMU_ALLOCATION_TRANSFER_CONSUMED 3u
#define KUI_VMU_ALLOCATION_TRANSFER_POISONED 4u
struct kui_vmu_persist_allocation_transfer {
    uint32_t magic,version,bytes,state;
    uint64_t card_sectors,partition_first,partition_end;
    uint32_t high_capacity,generation,sequence,previous_owner,next_owner;
    uint8_t cid[16],image_id[16],ownership_sha256[32],domain_binding_sha256[32];
    uint32_t reserved;
};
_Static_assert(sizeof(struct kui_vmu_persist_allocation_transfer)==160u,"Local allocation transfer bound");
/* Fresh independent session: consumes the trusted live transfer, checks the
 * exact ownership/actual CID, then recovers and validates CURRENT card data.
 * No host filesystem metadata is read. All image/block CRC, journal and
 * durability behavior remains the ordinary session behavior. The transfer is
 * BUSY before any callback and terminally consumed/poisoned afterward. */
int kui_vmu_persist_session_rebind(struct kui_vmu_persist_session *,
    const struct kui_vmu_persist_profile *,struct kui_vmu_persist_allocation_transfer *,
    const struct kui_vmu_persist_session_ops *,struct kui_vmu_image_workspace *);
/* Read preserves the output on failure. Commit succeeds only after the
 * journal/base durability barriers, exact base readback and final identity/
 * health check. An error may still leave a committed redo record: do not ACK,
 * resume or rebind; use a NEW session with fresh admission to recover it. */
int kui_vmu_persist_session_read(struct kui_vmu_persist_session *,unsigned,void *);
int kui_vmu_persist_session_commit(struct kui_vmu_persist_session *,unsigned,const void *);
/* Explicit clean handoff only: sync base, clear+sync marker, verify zero
 * marker, check identity/health. Caller must then relinquish exclusive use. */
int kui_vmu_persist_session_checkpoint(struct kui_vmu_persist_session *);
/* Only after a coherent complete save: validates VMU filesystem and refreshes
 * full SHA256/CRCs. It never clears/rewrites the journal or source data. */
int kui_vmu_persist_session_refresh(struct kui_vmu_persist_session *,
    struct kui_vmu_image_workspace *);
/* Owner can latch a later release/bus failure; no I/O and no reset API. */
void kui_vmu_persist_session_poison(struct kui_vmu_persist_session *);
#endif
