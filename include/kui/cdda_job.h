/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_CDDA_JOB_H
#define KUI_CDDA_JOB_H
#include <stdbool.h>
#include <stdint.h>

#define KUI_CDDA_JOB_MAX_CHUNK 2048u
enum kui_cdda_job_state {
    KUI_CDDA_JOB_EMPTY, KUI_CDDA_JOB_QUEUED, KUI_CDDA_JOB_READING,
    KUI_CDDA_JOB_READY, KUI_CDDA_JOB_DONE, KUI_CDDA_JOB_CANCELED, KUI_CDDA_JOB_FAULT
};
enum kui_cdda_job_result {
    KUI_CDDA_JOB_OK, KUI_CDDA_JOB_COMPLETE, KUI_CDDA_JOB_INVALID,
    KUI_CDDA_JOB_BUSY, KUI_CDDA_JOB_STALE, KUI_CDDA_JOB_IO, KUI_CDDA_JOB_OVERFLOW
};
/* A physical operation identity. A copied callback must match every field,
 * including chunk generation: the job epoch alone cannot reject a duplicate
 * chunk callback after another chunk of that same job has been dispatched. */
struct kui_cdda_job_span {uint32_t job,session_epoch,chunk,offset,bytes;};
struct kui_cdda_job {
    enum kui_cdda_job_state state;
    uint32_t file_bytes,first,length,done,generation,session_epoch,chunk_generation;
    struct kui_cdda_job_span pending;
    bool initialized;
};
/* Initialize once per quiescent owner lifetime. No token generation is reused
 * thereafter; discard all old callbacks before reinitializing an object. */
enum kui_cdda_job_result kui_cdda_job_init(struct kui_cdda_job *);
/* Begin only with no live job. Exact file bounds; zero-length jobs are invalid.
 * Completed/canceled/faulted jobs can be replaced using a fresh generation.
 * The token output must not alias owner state; such aliases are rejected. */
enum kui_cdda_job_result kui_cdda_job_begin(struct kui_cdda_job *, uint32_t file_bytes,
    uint32_t offset,uint32_t bytes,uint32_t session_epoch,uint32_t *token);
/* Claim one <=2048-byte synchronous operation. This is the mandatory token
 * check before storage dispatch. Call ready after the physical read returns. */
enum kui_cdda_job_result kui_cdda_job_claim(struct kui_cdda_job *, uint32_t token,
    uint32_t current_session_epoch,uint32_t max_bytes,struct kui_cdda_job_span *);
/* success means an exact read AND independently checked bytes. No progress is
 * exposed yet. Failed reads fault the job and clear its pending operation. */
enum kui_cdda_job_result kui_cdda_job_ready(struct kui_cdda_job *,
    const struct kui_cdda_job_span *,uint32_t current_session_epoch,bool success);
/* A second token/range check before publishing progress. COMPLETE means all
 * bytes of the original logical request were verified and committed. */
enum kui_cdda_job_result kui_cdda_job_commit(struct kui_cdda_job *,
    const struct kui_cdda_job_span *,uint32_t current_session_epoch);
/* Cancellation is allowed between synchronous physical operations, including
 * READY before commit. READING returns BUSY: this model does not abort SCI DMA.
 * Replayed/stale callbacks never mutate any state or output argument. */
enum kui_cdda_job_result kui_cdda_job_cancel(struct kui_cdda_job *,uint32_t token);
/* Owner-only fatal cleanup after a physical operation has returned/quiesced.
 * This is not an asynchronous abort or a per-chunk failure callback; use
 * ready(span,false) for checked failures of individual physical operations. */
enum kui_cdda_job_result kui_cdda_job_fail(struct kui_cdda_job *,uint32_t token);
#endif
