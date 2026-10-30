/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SAVED_READ_PROBE_H
#define KUI_SAVED_READ_PROBE_H
#include "kui/core.h"
#include "kui/destination.h"

#define KUI_SAVED_READ_PROFILES 4u
#define KUI_SAVED_READ_RECORD_BYTES 5120u
#define KUI_SAVED_READ_BUDGET_MS 300000u

struct kui_saved_read_probe_status {
    char message[128],job[KUI_DEST_JOB_CAP];
    unsigned profile;
    uint32_t current_slot,records;
    uint64_t done,total,elapsed_ms;
    bool complete,failed,stopped,budget_expired,inconsistent;
    bool first_bad_seen;
    unsigned first_bad_profile;
    uint32_t first_bad_slot;
    char first_problem[48];
    bool tried[KUI_SAVED_READ_PROFILES],full[KUI_SAVED_READ_PROFILES];
    uint32_t invalids[KUI_SAVED_READ_PROFILES],processed[KUI_SAVED_READ_PROFILES];
    uint64_t read_ms[KUI_SAVED_READ_PROFILES];
    uint8_t digest[KUI_SAVED_READ_PROFILES][32];
};
struct kui_saved_read_probe_ops {
    void *ctx;
    bool (*cancelled)(void *);
    uint64_t (*now_ms)(void *);
    void (*progress)(void *,const struct kui_saved_read_probe_status *);
    kui_log_fn log;
    /* Optional bounded evidence sink. Called once for the first invalid
     * record in each profile. Bytes are borrowed only during this callback;
     * retaining them requires a copy. No sink may alter the selected job. */
    void (*observe)(void *,unsigned profile,uint32_t slot,const char *problem,
                    const uint8_t record[KUI_SAVED_READ_RECORD_BYTES]);
};
const char *kui_saved_read_probe_profile_name(unsigned profile);
/* Execution order is unaligned bulk/512-byte, then aligned bulk/512-byte:
 * profile IDs {2,3,0,1}. This starts the SCI PIO comparison before a later
 * aligned transfer failure can terminate the probe. Invalid step returns
 * KUI_SAVED_READ_PROFILES. IDs still describe the same request shapes. */
unsigned kui_saved_read_probe_profile_at_step(unsigned step);
/* Explicit read-only diagnosis of packed salvage candidates. Mounts/unmounts
 * the connected device and opens only header.bin/patches.bin with FA_READ.
 * Four fresh-handle profiles compare 5120/512-byte requests using aligned
 * and deliberately unaligned buffers, in profile_at_step() order. Labels
 * describe request shape, not a guarantee of DMA use on every transport or
 * metadata read.
 *
 * Header and candidate self-checks plus matching full-file SHA-256 observations
 * do NOT establish journal publication, complete track consistency, original
 * audio correctness, optical-cache independence, or repair authority. This
 * function never clears quarantine, changes saved files, or reads the disc.
 * A bad candidate ends that profile; later profiles may diagnose the same
 * immutable file, but inconsistency remains sticky. I/O/short/close failures,
 * Stop and the five-minute soft budget terminate the entire probe. Budget
 * checks finish the active bounded read first. Return true only when all four
 * profiles fully read, validate and agree. complete also distinguishes a
 * finished comparison with detected inconsistency from an interrupted test. */
bool kui_saved_read_probe_run(const char *job,const struct kui_saved_read_probe_ops *ops,
    struct kui_saved_read_probe_status *out);
#endif
