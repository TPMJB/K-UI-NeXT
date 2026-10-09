/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/cdda_job.h"
static bool live(enum kui_cdda_job_state s) {
    return s==KUI_CDDA_JOB_QUEUED || s==KUI_CDDA_JOB_READING || s==KUI_CDDA_JOB_READY;
}
static bool identity(const struct kui_cdda_job *j,const struct kui_cdda_job_span *s,uint32_t epoch) {
    return s->job==j->generation && s->session_epoch==j->session_epoch && epoch==j->session_epoch &&
        s->chunk==j->pending.chunk && s->offset==j->pending.offset && s->bytes==j->pending.bytes &&
        s->job==j->pending.job && s->session_epoch==j->pending.session_epoch;
}
static void clear_pending(struct kui_cdda_job *j) {j->pending=(struct kui_cdda_job_span){0};}
enum kui_cdda_job_result kui_cdda_job_init(struct kui_cdda_job *j) {
    if(!j) return KUI_CDDA_JOB_INVALID;
    *j=(struct kui_cdda_job){.initialized=true};return KUI_CDDA_JOB_OK;
}
enum kui_cdda_job_result kui_cdda_job_begin(struct kui_cdda_job *j,uint32_t file_bytes,
    uint32_t offset,uint32_t bytes,uint32_t epoch,uint32_t *token) {
    if(!j || !j->initialized || !token || !bytes || offset>file_bytes || bytes>file_bytes-offset)
        return KUI_CDDA_JOB_INVALID;
    uintptr_t output=(uintptr_t)token,owner=(uintptr_t)j;
    if(output>=owner && output-owner<sizeof(*j)) return KUI_CDDA_JOB_INVALID;
    if(live(j->state)) return KUI_CDDA_JOB_BUSY;
    if(j->generation==UINT32_MAX) return KUI_CDDA_JOB_OVERFLOW;
    j->generation++;j->state=KUI_CDDA_JOB_QUEUED;j->file_bytes=file_bytes;j->first=offset;
    j->length=bytes;j->done=0;j->session_epoch=epoch;j->chunk_generation=0;clear_pending(j);
    *token=j->generation;return KUI_CDDA_JOB_OK;
}
enum kui_cdda_job_result kui_cdda_job_claim(struct kui_cdda_job *j,uint32_t token,
    uint32_t epoch,uint32_t max_bytes,struct kui_cdda_job_span *out) {
    if(!j || !j->initialized || !out || !max_bytes || max_bytes>KUI_CDDA_JOB_MAX_CHUNK)
        return KUI_CDDA_JOB_INVALID;
    if(token!=j->generation || epoch!=j->session_epoch || !live(j->state)) return KUI_CDDA_JOB_STALE;
    if(j->state!=KUI_CDDA_JOB_QUEUED) return KUI_CDDA_JOB_BUSY;
    if(j->chunk_generation==UINT32_MAX) return KUI_CDDA_JOB_OVERFLOW;
    uint32_t count=j->length-j->done;if(count>max_bytes) count=max_bytes;
    struct kui_cdda_job_span span={token,epoch,j->chunk_generation+1u,j->first+j->done,count};
    j->chunk_generation++;j->pending=span;j->state=KUI_CDDA_JOB_READING;*out=span;
    return KUI_CDDA_JOB_OK;
}
enum kui_cdda_job_result kui_cdda_job_ready(struct kui_cdda_job *j,
    const struct kui_cdda_job_span *span,uint32_t epoch,bool success) {
    if(!j || !j->initialized || !span) return KUI_CDDA_JOB_INVALID;
    if(j->state!=KUI_CDDA_JOB_READING || !identity(j,span,epoch)) return KUI_CDDA_JOB_STALE;
    if(!success) {j->state=KUI_CDDA_JOB_FAULT;clear_pending(j);return KUI_CDDA_JOB_IO;}
    j->state=KUI_CDDA_JOB_READY;return KUI_CDDA_JOB_OK;
}
enum kui_cdda_job_result kui_cdda_job_commit(struct kui_cdda_job *j,
    const struct kui_cdda_job_span *span,uint32_t epoch) {
    if(!j || !j->initialized || !span) return KUI_CDDA_JOB_INVALID;
    if(j->state!=KUI_CDDA_JOB_READY || !identity(j,span,epoch)) return KUI_CDDA_JOB_STALE;
    j->done+=span->bytes;clear_pending(j);
    j->state=j->done==j->length?KUI_CDDA_JOB_DONE:KUI_CDDA_JOB_QUEUED;
    return j->state==KUI_CDDA_JOB_DONE?KUI_CDDA_JOB_COMPLETE:KUI_CDDA_JOB_OK;
}
enum kui_cdda_job_result kui_cdda_job_cancel(struct kui_cdda_job *j,uint32_t token) {
    if(!j || !j->initialized) return KUI_CDDA_JOB_INVALID;
    if(token!=j->generation || !live(j->state)) return KUI_CDDA_JOB_STALE;
    if(j->state==KUI_CDDA_JOB_READING) return KUI_CDDA_JOB_BUSY;
    j->state=KUI_CDDA_JOB_CANCELED;clear_pending(j);return KUI_CDDA_JOB_OK;
}
enum kui_cdda_job_result kui_cdda_job_fail(struct kui_cdda_job *j,uint32_t token) {
    if(!j || !j->initialized) return KUI_CDDA_JOB_INVALID;
    if(token!=j->generation || !live(j->state)) return KUI_CDDA_JOB_STALE;
    j->state=KUI_CDDA_JOB_FAULT;clear_pending(j);return KUI_CDDA_JOB_IO;
}
