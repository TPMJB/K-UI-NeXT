/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_CAPTURE_H
#define KUI_CAPTURE_H
#include "kui/hash.h"
#include "kui/probe.h"
#include "kui/timing.h"
#define KUI_CAPTURE_PROFILE "gdi-raw2352-typegap150-v1"
#define KUI_CAPTURE_CHUNK 32u
#define KUI_CAPTURE_RETRIES 10u
#define KUI_CHECKPOINT_SECTORS 4096u
#define KUI_CHECKPOINT_BYTES 4096u
struct kui_capture_track { uint32_t number, control, session, start, end, toc_end; };
struct kui_capture_plan { struct kui_capture_track tracks[99]; unsigned count; uint64_t bytes; };
bool kui_plan_tracks(const struct kui_toc sessions[2], struct kui_capture_plan *out);
/* Bench sanity: is `fad` inside a track of the type the bench is configured to capture?
 * Returns NULL when the configuration is consistent, else a sentence to log. A bench can
 * otherwise measure something other than what was asked for and say nothing: capture_fad
 * defaults to 45150, which is a DATA track on most discs, so `capture_type=audio` there
 * measures the audio code path on data media (correct timings, wrong question), and a
 * capture_fad from another disc's layout may not be on this disc at all. Pure; the console
 * passes the TOC it already read. */
const char *kui_bench_fad_note(const struct kui_toc sessions[2], uint32_t fad, bool audio);
enum kui_read_result { KUI_READ_OK, KUI_READ_RETRY, KUI_READ_FATAL };
enum kui_capture_mode { KUI_CAPTURE_NEW, KUI_CAPTURE_RESUME, KUI_CAPTURE_VERIFY };
enum kui_capture_result { KUI_CAPTURE_FAILED, KUI_CAPTURE_STOPPED, KUI_CAPTURE_COMPLETE };
enum kui_capture_phase { KUI_IDENTIFY, KUI_PREFIX_CHECK, KUI_CAPTURING, KUI_VERIFYING, KUI_FINISHED };
struct kui_capture_progress {
    enum kui_capture_phase phase;
    unsigned track, tracks;
    uint32_t fad, retries;
    uint64_t done, total, committed, elapsed_ms;
};
/* Runtime choices for the capture engine. All-zero, or no options at all, is the
 * engine exactly as it has always been: SHA-256 and CRC32 per track, every saved
 * byte re-read after capture, a full prefix check on resume, no sampling. They
 * exist so the alternatives can be measured (bench.cfg `capture_*`) before any
 * of them is chosen as a default. */
struct kui_capture_options {
    /* Record CRC32 only, no SHA-256. Applies to NEW jobs; a job keeps the mode it
     * started with (it is stored in the checkpoint), because SHA state cannot be
     * resumed from a digest. A CRC-only job writes a schema 2 manifest. */
    bool crc_only;
    /* Do not re-read every saved byte after capture. Honoured for CRC-only jobs
     * (a SHA-256 job's schema 1 manifest promises a read-back, so it always does
     * one). The Verify action always re-reads. The report then says CAPTURED,
     * never SAVED DATA VERIFIED. */
    bool skip_end_readback;
    /* On resume, check only file sizes instead of re-reading the committed bytes;
     * the running CRC32 continues from the checkpoint. CRC-only jobs only. */
    bool resume_size_only;
    /* While capturing, re-read and byte-compare 1 chunk in this many right after
     * it is written; a mismatch stops the capture. 0 = off. */
    unsigned sample_every;
    /* Read the disc with GD-ROM DMA and start the NEXT chunk's read while this one is written
     * and hashed. Needs ops->read_begin/read_end; ignored without them. Measured on hardware
     * (docs/evidence/t9-pipeline-overlap-2026-09-20.json): it hides 92% of the read time.
     * The bytes are the same either way, and any chunk it cannot overlap - an odd sector count,
     * or single-sector reads after a bad sector - falls back to the ordinary PIO read. */
    bool read_dma;
    /* Benchmark run: publish no metadata and skip the reference check. */
    bool bench;
};
/* What a run did, for the benchmark. Optional; filled when the run ends. */
struct kui_capture_stats {
    uint64_t phase_us[KUI_TIME_PHASES];             /* wall time per phase */
    uint64_t capture_bucket_us[KUI_TIME_BUCKETS];   /* the capture phase by stage */
    uint64_t bytes;         /* saved bytes when the run ended */
    uint32_t sampled;       /* chunks re-read and compared while capturing */
    bool verified;          /* every saved byte was re-read and matched this run */
    bool crc_only;          /* the job's hash mode */
    char job_dir[80];       /* where the job lives, so a benchmark can delete it */
};
struct kui_capture_ops {
    void *ctx;
    enum kui_read_result (*read)(void *, uint32_t fad, unsigned sectors, uint8_t *out);
    bool (*cancelled)(void *);
    uint64_t (*now_ms)(void *);
    void (*progress)(void *, const struct kui_capture_progress *);
    kui_log_fn log;
    const char *build;
    /* Optional observation only; no effect on deadlines or stored formats. */
    uint64_t (*now_us)(void *);
    /* Optional adapter profiling label; never changes the read policy. */
    void (*read_phase)(void *, bool capturing);
    /* Optional split GD-ROM DMA read (see read_dma). begin returns at once; end must always
     * follow a true begin, because until it does the firmware owns the buffer. */
    bool (*read_begin)(void *, uint32_t fad, unsigned sectors, uint8_t *out);
    enum kui_read_result (*read_end)(void *);
    const struct kui_capture_options *options;   /* NULL = defaults */
    struct kui_capture_stats *stats;             /* NULL = not wanted */
};
struct kui_checkpoint {
    uint64_t sequence;
    uint8_t identity[32];
    uint32_t count, retries;
    char build[13];
    bool crc_only;   /* SHA-256 not recorded; the per-track sha256 fields are zero */
    struct { uint32_t sectors, crc32; uint8_t sha256[32]; } track[99];
};
void kui_checkpoint_encode(const struct kui_checkpoint *state, uint8_t record[KUI_CHECKPOINT_BYTES]);
bool kui_checkpoint_decode(const uint8_t record[KUI_CHECKPOINT_BYTES], const struct kui_capture_plan *plan,
    const uint8_t identity[32], struct kui_checkpoint *out);
/* Sole worker owns FatFs and optical I/O. Caller connects/disconnects media.
 * Mount/unmount are handled here; all output is in an exclusively created job.
 */
enum kui_capture_result kui_capture(const struct kui_capture_plan *plan,
    const struct kui_capture_ops *ops, enum kui_capture_mode mode);
/* Benchmark: capture `sectors` sectors from `fad` as one track (data with EDC
 * checking, or audio) through the real engine, publishing nothing. NEW makes a
 * job (stats->job_dir); RESUME re-opens it, which times the resume check. */
enum kui_capture_result kui_capture_bench(const struct kui_capture_ops *ops,
    uint32_t fad, unsigned sectors, bool audio, enum kui_capture_mode mode);
#endif
