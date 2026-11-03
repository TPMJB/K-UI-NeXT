/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SALVAGE_H
#define KUI_SALVAGE_H
#include "kui/capture.h"

/* Explicit damaged-disc workflow. Normal capture/checkpoint formats are never
 * read or written. The single I/O worker prepares the disc and connects SD,
 * then this function owns FatFs mount/unmount and the bounded read callback. */
/* Targets are indexed on disk, bounded by the admitted disc layout rather
 * than a fixed in-memory damaged-sector count. */
enum kui_salvage_action { KUI_SALVAGE_NEW, KUI_SALVAGE_RESUME, KUI_SALVAGE_RECOVER };
enum kui_salvage_result { KUI_SALVAGE_FAILED, KUI_SALVAGE_STOPPED,
    KUI_SALVAGE_UNRESOLVED, KUI_SALVAGE_RESOLVED };
/* Transient read display only; no values are persisted in salvage jobs.
 * ACCEPTED means the read's required checks passed (audio collection needs only
 * transport success; audio candidates need agreement), not saved-file verification,
 * a matching saved identity digest, or a catalogue/recovery success claim. */
enum kui_salvage_read_state { KUI_SALVAGE_READ_NONE, KUI_SALVAGE_READ_READING,
    KUI_SALVAGE_READ_ACCEPTED, KUI_SALVAGE_READ_RETRY, KUI_SALVAGE_READ_SPLIT,
    KUI_SALVAGE_READ_DEFERRED, KUI_SALVAGE_READ_FATAL };
/* Transient saved-data work, never persisted in a job. Counts describe
 * processed journal records, checked targets or visited directory entries,
 * not optical repair attempts. DIRECTORY has no known total (sd_total=0). */
enum kui_salvage_sd_phase { KUI_SALVAGE_SD_NONE, KUI_SALVAGE_SD_JOURNAL,
    KUI_SALVAGE_SD_TARGETS, KUI_SALVAGE_SD_DIRECTORY };
#define KUI_SALVAGE_TRIAL_PROFILES 4u
#define KUI_SALVAGE_TRIAL_TARGETS 8u
#define KUI_SALVAGE_READ_TRIAL_TARGETS 16u
struct kui_salvage_status {
    enum kui_salvage_result result;
    bool first_pass_complete, complete, collection_active;
    bool session_reused, session_retained; /* Same-boot lease; never persistent authority. */
    enum kui_salvage_sd_phase sd_phase;
    uint64_t sd_done, sd_total;
    unsigned track, tracks, pass, pass_limit;
    uint32_t fad, targets, recovered, remaining, attempts;
    /* attempts remains the cumulative number of optical callback invocations.
     * The current range has its own logical attempt budget. Audio agreement
     * reads share one attempt and advance confirm from 1/2 to 2/2 instead. */
    uint32_t read_fad, read_count;
    unsigned read_attempt, read_limit, read_confirm, read_confirm_limit;
    enum kui_salvage_read_state read_state;
    /* Most recent problem range, including its final successful checks if that
     * exact range subsequently passes; retained across other healthy reads.
     * SPLIT means subdivision, DEFERRED means this local budget was exhausted
     * (zero attempt/limit denotes an explicitly unread range deferred by request);
     * neither means a sector was permanently abandoned by later passes. */
    uint32_t last_problem_fad, last_problem_count;
    unsigned last_problem_attempt, last_problem_limit;
    unsigned last_problem_confirm, last_problem_confirm_limit;
    enum kui_salvage_read_state last_problem_state;
    uint64_t done, total, elapsed_ms;
    /* Collection output committed during this invocation only, including
     * explicit unresolved placeholders. Clock excludes preparation/replay and
     * stops when collection returns; elapsed 0 means no usable rate yet.
     * Existing done includes resumed bytes and is not a rate numerator. */
    uint64_t collected_this_run, collection_elapsed_ms;
    /* Repair profiling for this invocation only. Calls exclude identity/recovery
     * anchor reads. singles counts actual callbacks (two for matching audio);
     * accepted counts validated sector candidates, not durable repairs. */
    uint32_t repair_batches, repair_singles, repair_batch_accepted, repair_single_accepted;
    uint64_t repair_optical_ms;
    /* Transient bounded data-sector comparison, never repair authority or a
     * physical spindle-speed claim. Reads/valid/ms count comparison targets;
     * qualifying identity reads are logged separately. */
    bool trial_active, trial_complete, trial_budget_expired, trial_qualification_done;
    unsigned trial_done, trial_total, trial_profile, trial_sample_count;
    uint32_t trial_profile_reads[KUI_SALVAGE_TRIAL_PROFILES];
    uint32_t trial_profile_valid[KUI_SALVAGE_TRIAL_PROFILES];
    uint32_t trial_profile_repairs[KUI_SALVAGE_TRIAL_PROFILES];
    uint64_t trial_profile_ms[KUI_SALVAGE_TRIAL_PROFILES];
    uint64_t trial_switch_ms[KUI_SALVAGE_TRIAL_PROFILES];
    bool trial_qualified[KUI_SALVAGE_TRIAL_PROFILES];
    uint32_t final_crc[99]; /* Available only for a resolved complete job. */
    uint8_t identity[32]; /* SHA-256 of the exact immutable header, same receipt. */
    char job[KUI_DEST_JOB_CAP], message[128];
    /* trial_* counters describe read approaches rather than drive settings. */
    bool trial_read_strategy;
};
struct kui_salvage_options {
    bool zero_fill; /* Deprecated/ignored: salvage always records unresolved holes. */
    unsigned passes; /* All actions: 0 = until stopped, or 1, 5, 10, 20, 50 sweeps. */
    const char *job; /* RESUME/RECOVER: exact /KUI/salvage/job-NNNN folder. */
    void (*progress)(void *ctx,const struct kui_salvage_status *status);
    /* Explicit RESUME-only request after restarting a blocked drive: defer the
     * next uncommitted range (<=32 sectors) without an optical attempt, then
     * continue collection. These bytes remain unresolved, never recovered.
     * False preserves normal Resume. Ready/no-remaining jobs are refused. */
    bool defer_next_range;
    /* Explicit RECOVER-only trial on a collected job. Up to eight unresolved
     * data targets are frozen and compared once under four fixed profiles,
     * with rotating order and a five-minute soft budget after admission.
     * Callback: 1 verified, 0 unsupported with previous policy restored,
     * -1 fatal/unavailable. Every profile derives from the entry snapshot:
     * original, original-speed/retry1, requested4x/retry1, requested2x/retry1.
     * Invalid/fatal reads never authorize bytes. A good repair is committed
     * immediately and comparison reads never replace it. */
    bool control_trial;
    int (*trial_select)(void *ctx,unsigned profile);
    /* Optional exclusive-worker session lease. Zero/NULL preserves the full
     * historical replay/reconciliation path. The caller supplies a nonzero
     * ownership epoch and exact 32-byte immutable physical-card fingerprint,
     * invalidating the lease after other job mutations, untracked storage activity, media change,
     * storage errors, or uncertain drive cleanup. Never derive this from a
     * displayed title or from persistent cache metadata. */
    uint64_t session_epoch;
    const uint8_t *session_card;
    /* Explicit RECOVER-only comparison on a collected job, exclusive with
     * control_trial/defer_next_range. Freezes up to 16 unresolved DATA targets,
     * rotates single/leading4/centered16/verified-anchor+leading16 reads, and
     * uses the caller's unchanged drive policy throughout. Ten-minute soft
     * budget starts after ordinary saved-data reconciliation. Every accepted
     * target uses the normal durable repair protocol; later profiles must
     * byte-match an already repaired target. Requires now_ms, no trial_select.
     * No session receipt is reused or retained. trial_profile_reads counts
     * admitted method attempts; ms includes approach and target reads. */
    bool read_trial;
};
/* Single-worker/idle use only. Erases the private RAM receipt; no files or
 * persistent settings are changed. Reboot also starts without a receipt. */
void kui_salvage_session_invalidate(void);
/* NEW finds identity with one probe per prospective candidate (two matching
 * reads for audio), then collects readable sectors and records holes durably.
 * The stronger exact saved-anchor retry budget applies to Resume/latest.
 * It then automatically runs alternating targeted recovery sweeps. An invocation
 * that begins with unfinished collection can first rescue adjacent unresolved
 * data targets with one range read for up to 16 sectors, retaining only
 * independently checked slots. Each unverified slot gets at most one singleton
 * fallback that pass. Two consecutive batches with no validated sector disable
 * batching for the rest of that invocation; a positive batch resets the count.
 * RESUME/RECOVER of an already collected job use singleton targeted reads from
 * the start. Isolated targets use one singleton attempt; audio retains its two
 * successful matching reads without batching. Separate staging preserves
 * checked batch slots across fallback/recovery/publication. RESUME
 * continues collection and recovery, including jobs with the legacy OFF flag.
 * RECOVER runs additional sweeps after a completed first pass. passes=0 keeps
 * trying until all targets resolve, cancellation or an operational failure.
 * Fatal/reset read results always stop, never become zeros. Data candidates
 * require FAD/EDC/PQ (Mode1 for GD; Mode1/Mode2 Form1 for CD); audio repair
 * requires two byte-identical reads. Collection probes each <=32-sector range
 * once, retaining valid returned sectors and deferring unverified sectors to
 * later repair without subdivision or individual retries. A PARTIAL buffer
 * permits data sectors only after those same full checks; audio requires OK.
 * After eight wholly unverified collection ranges, one-shot exact saved-anchor
 * probes check drive health before more collection. At least one matching
 * anchor permits continued scanning; unreadable anchors request one optional
 * safe recovery and normal identity revalidation. Further health checks are
 * spaced by 64 wholly unverified ranges; any accepted sector restarts the
 * initial eight-range interval. Explicitly skipped ranges do not count.
 * Safe reinitialization also runs between recovery passes and after eight
 * consecutive RETRY results or invalid PARTIAL candidates outside collection,
 * then checks exact saved content anchors again. A valid PARTIAL candidate
 * resets that counter only after validation. FATAL never calls recovery.
 * No catalogue match or independent saved-file verification is implied.
 * Accepted normal/DMA capture code is not modified or used as a salvage loop. */
enum kui_salvage_result kui_salvage_run(const struct kui_capture_plan *plan,
    const struct kui_capture_ops *ops,const struct kui_salvage_options *options,
    enum kui_salvage_action action,struct kui_salvage_status *status);
/* Read-only discovery, mounts/unmounts. Chooses the greatest numbered valid
 * header matching the prepared profile/TOC and both optical identity anchors.
 * Exact identity reads have bounded retries; data must pass structural checks
 * and CD audio needs two matching reads. Returns
 * false on no match, cancellation or I/O/drive error. Does not repair/truncate
 * journals or imply that a selected job is complete. */
bool kui_salvage_latest(const struct kui_capture_plan *plan,
    const struct kui_capture_ops *ops,char job[KUI_DEST_JOB_CAP]);
#endif
