/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_PERSIST_STAGE_H
#define KUI_VMU_PERSIST_STAGE_H
#include "kui/vmu_persist_session.h"
#include "kui/vmu_durable.h"
#include "kui/retail_image.h"

enum kui_vmu_persist_stage_result {
    KUI_VMU_PERSIST_STAGE_OK=0,KUI_VMU_PERSIST_STAGE_ARGUMENT=-1,
    KUI_VMU_PERSIST_STAGE_MAPPING=-2,KUI_VMU_PERSIST_STAGE_SESSION=-3,
    KUI_VMU_PERSIST_STAGE_MARKER=-4,KUI_VMU_PERSIST_STAGE_HANDOFF=-5
};
enum kui_vmu_persist_stage_phase {
    KUI_VMU_PERSIST_STAGE_PREFLIGHT=1,KUI_VMU_PERSIST_STAGE_RECOVER=2,
    KUI_VMU_PERSIST_STAGE_CHECKPOINT=3,KUI_VMU_PERSIST_STAGE_SNAPSHOT=4,
    KUI_VMU_PERSIST_STAGE_ZERO_MARKER=5,KUI_VMU_PERSIST_STAGE_SEAL=6
};
/* High-stage scratch only, never copied into the worker. Zero once, do not
 * move/reuse it after preparation starts. Caller owns the already initialized
 * and acquired SCI device exclusively across the complete call and its final
 * release; a release failure must discard the result. No callback/context is
 * retained in the handoff. No argument buffers may alias this workspace. */
struct kui_vmu_persist_stage_workspace {
    struct kui_vmu_persist_session session;
    struct kui_vmu_fs_workspace filesystem;
    struct kui_vmu_image_workspace image;
    struct kui_vmu_fs_range protected[KUI_RETAIL_MANIFEST_SLOTS];
    uint32_t started,phase;
    int result;
};
/* No I/O. Protects ALL manifest extents (including every mapped audio track)
 * from ALL target whole-cluster allocations, including journal/identity slack.
 * Exact-title admission remains independently mandatory in the caller. */
int kui_vmu_persist_stage_admit(const struct kui_vmu_persist_profile *,
    const struct kui_retail_manifest *);
/* Fresh semantic ownership -> real-map recovery -> general VMU validation /
 * current hashes -> explicit clean checkpoint -> fresh current hashes ->
 * final zero-marker/identity/health check -> sealed compact handoff. Embedded
 * preparation hashes are provenance only. Disjoint output is zero on failure.
 * No mount, allocation, card init, fallback or arbitrary raw write exists. */
int kui_vmu_persist_stage_prepare(const struct kui_vmu_persist_profile *,
    const struct kui_retail_manifest *,const struct kui_vmu_persist_session_ops *,
    struct kui_vmu_persist_stage_workspace *,struct kui_vmu_durable_handoff *);
/* Collection-only new-owner preparation. The lifecycle owner supplies the
 * trusted live-generation expected fields documented by session_attach.
 * Rechecks every game/audio extent against all target whole allocations,
 * freshly replays the complete metadata witness, recovers the real journal,
 * validates/hashes the current image, checkpoints and verifies zero marker /
 * final identity before sealing a NEW unchanged1272-byte worker handoff.
 * The existing FS workspace supplies aligned replay scratch; no retained
 * worker or stage-workspace ABI grows. Ordinary prepare/init are unchanged. */
int kui_vmu_persist_stage_attach(const struct kui_vmu_persist_profile *,
    const struct kui_retail_manifest *,const struct kui_vmu_persist_attach_source *,
    const struct kui_vmu_persist_attach_ops *,
    struct kui_vmu_persist_stage_workspace *,struct kui_vmu_durable_handoff *);
/* Collection lifecycle transfer of already established allocation authority.
 * Rechecks all child/game extents against complete allocations, consumes the
 * trusted live epoch, and performs fresh journal/image/checkpoint/final-CID
 * preparation. Only host metadata admission/replay is omitted. The result is
 * the same fresh1272-byte handoff; ordinary prepare and witness attach remain
 * independent entry points. */
int kui_vmu_persist_stage_rebind(const struct kui_vmu_persist_profile *,
    const struct kui_retail_manifest *,struct kui_vmu_persist_allocation_transfer *,
    const struct kui_vmu_persist_session_ops *,struct kui_vmu_persist_stage_workspace *,
    struct kui_vmu_durable_handoff *);
/* Physical-disc variant. All game content is on the GD-ROM, so admission has
 * no SD image map or protected game extents. The complete live filesystem,
 * card identity, selected pair, journal recovery and sealed handoff checks are
 * shared with the image path. Only SCI and SCIF are admitted. The caller owns
 * the already initialized/acquired selected port throughout preparation and
 * must discard the result if its final release does not restore that port. */
int kui_vmu_persist_disc_stage_admit(const struct kui_vmu_persist_profile *,
    uint32_t transport);
int kui_vmu_persist_disc_stage_prepare(const struct kui_vmu_persist_profile *,
    uint32_t transport,const struct kui_vmu_persist_session_ops *,
    struct kui_vmu_persist_stage_workspace *,struct kui_vmu_durable_handoff *);
/* Installer rechecks a mutable prepared result against the immutable embedded
 * P2 identity/maps, preserving its newly computed hashes after bootstrap. */
int kui_vmu_persist_stage_handoff_matches(const struct kui_vmu_persist_profile *,
    const struct kui_vmu_durable_handoff *);
#endif
