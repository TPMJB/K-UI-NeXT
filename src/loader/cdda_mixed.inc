/* SPDX-License-Identifier: GPL-3.0-only */
/* Cooperative, serialized SCI jobs in the standalone owner only. Audio
 * commands and the independent data FIL have different lifetime epochs. */
#include "kui/cdda_job.h"
#include "kui/cdda_clock.h"

#define MIXED_CLASSES 8u
static const uint32_t mixed_sizes[MIXED_CLASSES]={1u,31u,511u,512u,513u,2048u,4096u,32768u};
static struct kui_cdda_job mixed_job;
static struct kui_cdda_stream_clock mixed_clock;
static _Alignas(32) uint8_t mixed_buffer[KUI_CDDA_JOB_MAX_CHUNK];
static uint32_t mixed_file_bytes,mixed_data_epoch,mixed_token,mixed_job_class,mixed_sequence;
static uint32_t mixed_class_counts[MIXED_CLASSES],mixed_full_passes,mixed_checked,mixed_errors;
static uint32_t mixed_jobs_done,mixed_chunks_done,mixed_reads,mixed_cancels,mixed_stale_refusals;
static uint32_t mixed_budget_ticks,mixed_worst_ticks,mixed_last_progress,mixed_max_gap_ticks,mixed_gap_limit;
static bool mixed_enabled,mixed_auto,mixed_dispatch_enabled,mixed_hold_commit,mixed_stop_after_chunk;
static bool mixed_full_job,mixed_faulted;

static bool mixed_live(void) {
    return mixed_job.state==KUI_CDDA_JOB_QUEUED || mixed_job.state==KUI_CDDA_JOB_READING ||
        mixed_job.state==KUI_CDDA_JOB_READY;
}
static uint32_t mixed_min_class(void) {
    uint32_t minimum=UINT32_MAX;
    for(unsigned i=0;i<MIXED_CLASSES;i++) if(mixed_class_counts[i]<minimum) minimum=mixed_class_counts[i];
    return minimum;
}
static uint8_t mixed_pattern(uint32_t at) {
    uint32_t x=at^0x9e3779b9u;
    x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;return (uint8_t)x;
}
static bool mixed_clock_update(void) {
    return kui_cdda_stream_clock_update(&mixed_clock,ticks());
}
static bool mixed_fatal(const char *reason) {
    if(!mixed_faulted) {mixed_faulted=true;mixed_errors++;cdda_display_line(reason);}
    /* This owner cleanup runs only after synchronous storage has returned. */
    if(mixed_live()) (void)kui_cdda_job_fail(&mixed_job,mixed_job.generation);
    if(command_control.initialized) {
        uint32_t epoch=command_control.current.pending?
            command_control.current.pending_epoch:command_control.current.epoch;
        (void)kui_cdda_control_fail(&command_control,epoch);
    }
    if(command_running) (void)session_close();
    else {(void)kui_cdda_aica_stop();cdda_storage_close();}
    command_running=false;mixed_enabled=false;return false;
}
static bool mixed_begin(uint32_t first,uint32_t bytes,uint32_t klass,bool full) {
    if(kui_cdda_job_begin(&mixed_job,mixed_file_bytes,first,bytes,mixed_data_epoch,&mixed_token)!=KUI_CDDA_JOB_OK)
        return mixed_fatal("Mixed data job could not begin.");
    mixed_job_class=klass;mixed_full_job=full;return true;
}
static bool mixed_begin_class(void) {
    uint32_t klass=mixed_sequence%MIXED_CLASSES,round=mixed_sequence/MIXED_CLASSES;
    uint32_t bytes=mixed_sizes[klass],first;
    switch(round%4u) {
    case 0:first=509u;break; /* Unaligned and often across a card boundary. */
    case 1:first=mixed_file_bytes-bytes-(round*149u)%511u;break;
    case 2:first=(round*257u+klass*65537u)%(mixed_file_bytes-bytes+1u);break;
    default:first=mixed_file_bytes-bytes;break; /* Exact EOF, never beyond it. */
    }
    mixed_sequence++;
    return mixed_begin(first,bytes,klass,false);
}
/* Full pure preflight before any observe/phase query, card I/O or audio effect.
 * Even a copied callback from an older chunk of the same job is refused. */
static bool mixed_span_current(const struct kui_cdda_job_span *span) {
    const struct kui_cdda_job_span *p=&mixed_job.pending;
    return span && mixed_job.state==KUI_CDDA_JOB_READY && span->job==mixed_job.generation &&
        span->session_epoch==mixed_data_epoch && span->session_epoch==mixed_job.session_epoch &&
        span->job==p->job && span->session_epoch==p->session_epoch && span->chunk==p->chunk &&
        span->offset==p->offset && span->bytes==p->bytes;
}
static enum kui_cdda_job_result mixed_commit(const struct kui_cdda_job_span *span) {
    if(!mixed_span_current(span)) return KUI_CDDA_JOB_STALE;
    uint32_t count=span->bytes;
    bool completes=count==mixed_job.length-mixed_job.done;
    if(count>UINT32_MAX-mixed_checked || mixed_chunks_done==UINT32_MAX ||
       (completes && (mixed_jobs_done==UINT32_MAX ||
        (mixed_full_job && mixed_full_passes==UINT32_MAX) ||
        (!mixed_full_job && mixed_job_class<MIXED_CLASSES && mixed_class_counts[mixed_job_class]==UINT32_MAX))))
        return KUI_CDDA_JOB_OVERFLOW;
    enum kui_cdda_job_result result=kui_cdda_job_commit(&mixed_job,span,mixed_data_epoch);
    if(result!=KUI_CDDA_JOB_OK && result!=KUI_CDDA_JOB_COMPLETE) return result;
    mixed_checked+=count;mixed_chunks_done++;
    uint32_t now=ticks(),gap=now-mixed_last_progress;
    if(gap>mixed_max_gap_ticks) mixed_max_gap_ticks=gap;
    mixed_last_progress=now;
    if(result==KUI_CDDA_JOB_COMPLETE) {
        mixed_jobs_done++;
        if(mixed_full_job) {
            if(mixed_job.first || mixed_job.length!=mixed_file_bytes || mixed_job.done!=mixed_file_bytes)
                return KUI_CDDA_JOB_INVALID;
            mixed_full_passes++;
        } else if(mixed_job_class<MIXED_CLASSES) mixed_class_counts[mixed_job_class]++;
    }
    if(mixed_stop_after_chunk) {mixed_stop_after_chunk=false;mixed_dispatch_enabled=false;}
    return result;
}
static enum kui_cdda_job_result mixed_dispatch(uint32_t token) {
    /* Do not inspect hardware while deciding whether a stale callback is live. */
    if(token!=mixed_job.generation || mixed_job.session_epoch!=mixed_data_epoch ||
       mixed_job.state!=KUI_CDDA_JOB_QUEUED) return KUI_CDDA_JOB_STALE;
    if(!command_running || !mixed_dispatch_enabled) return KUI_CDDA_JOB_BUSY;
    if(!observe() || !mixed_clock_update()) {mixed_fatal("Mixed pre-read observation failed.");return KUI_CDDA_JOB_IO;}
    if(!ring.ready[0] || !ring.ready[1]) return KUI_CDDA_JOB_BUSY;
    uint32_t margin;
    if(!kui_cdda_frames_to_ticks(KUI_CDDA_HALF_FRAMES-ring.last_position%KUI_CDDA_HALF_FRAMES,&margin))
        return KUI_CDDA_JOB_OVERFLOW;
    if(mixed_budget_ticks>(UINT32_MAX-KUI_CDDA_MARGIN_TICKS)/2u ||
       margin<=mixed_budget_ticks*2u+KUI_CDDA_MARGIN_TICKS) return KUI_CDDA_JOB_BUSY;
    uint32_t cap=KUI_CDDA_JOB_MAX_CHUNK;
    if(mixed_job_class<MIXED_CLASSES && mixed_job_class%3u==2u) cap=511u;
    struct kui_cdda_job_span span;
    enum kui_cdda_job_result result=kui_cdda_job_claim(&mixed_job,token,mixed_data_epoch,cap,&span);
    if(result!=KUI_CDDA_JOB_OK) return result;
    uint32_t start=ticks();mixed_reads++;
    int read_result=cdda_storage_data_read_at(span.offset,mixed_buffer,span.bytes);
    /* A blocking operation may consume the margin. Observe before marking its
     * checked bytes READY, committing any progress, or allowing another write. */
    if(!observe() || !mixed_clock_update()) {
        (void)kui_cdda_job_ready(&mixed_job,&span,mixed_data_epoch,false);
        mixed_fatal("Mixed read exceeded audio service deadline.");return KUI_CDDA_JOB_IO;
    }
    bool verified=!read_result;
    for(uint32_t i=0;verified && i<span.bytes;i++)
        if(mixed_buffer[i]!=mixed_pattern(span.offset+i)) verified=false;
    uint32_t elapsed=ticks()-start;
    if(elapsed>mixed_worst_ticks) mixed_worst_ticks=elapsed;
    if(elapsed>mixed_budget_ticks) mixed_budget_ticks=elapsed;
    result=kui_cdda_job_ready(&mixed_job,&span,mixed_data_epoch,verified);
    if(result!=KUI_CDDA_JOB_OK) {
        mixed_fatal(read_result?"Mixed data read failed.":"Mixed data pattern mismatch.");return KUI_CDDA_JOB_IO;
    }
    return mixed_hold_commit?KUI_CDDA_JOB_OK:mixed_commit(&span);
}
static bool mixed_job_step(void) {
    if(!mixed_enabled) return true;
    if(!mixed_clock_update()) return mixed_fatal("Mixed elapsed clock failed.");
    uint32_t gap=ticks()-mixed_last_progress;
    if(gap>mixed_max_gap_ticks) mixed_max_gap_ticks=gap;
    if(gap>=mixed_gap_limit) return mixed_fatal("Mixed workload starved: no checked chunk for5s.");
    if(mixed_job.state==KUI_CDDA_JOB_READY) {
        if(!mixed_hold_commit) {
            struct kui_cdda_job_span span=mixed_job.pending;
            enum kui_cdda_job_result result=mixed_commit(&span);
            if(result!=KUI_CDDA_JOB_OK && result!=KUI_CDDA_JOB_COMPLETE)
                return mixed_fatal("Mixed delayed commit failed.");
        }
        return true;
    }
    if(!mixed_live()) {
        if(!mixed_auto) return true;
        if(!mixed_full_passes) {
            if(!mixed_begin(0,mixed_file_bytes,UINT32_MAX,true)) return false;
        } else if(!mixed_begin_class()) return false;
    }
    enum kui_cdda_job_result result=mixed_dispatch(mixed_token);
    if(result==KUI_CDDA_JOB_OK || result==KUI_CDDA_JOB_COMPLETE || result==KUI_CDDA_JOB_BUSY) return true;
    return mixed_fatal("Mixed dispatch identity/state failed.");
}
static bool mixed_drain(void) {
    while(mixed_live()) if(!command_run(4410u)) return false;
    return true;
}
static bool mixed_cancel(void) {
    if(kui_cdda_job_cancel(&mixed_job,mixed_token)!=KUI_CDDA_JOB_OK || mixed_data_epoch==UINT32_MAX)
        return mixed_fatal("Mixed explicit cancellation failed.");
    mixed_cancels++;mixed_data_epoch++;return true;
}
static bool mixed_expect_stale(uint32_t token,struct kui_cdda_job_span old) {
    struct kui_cdda_control_status before,after;
    uint32_t reads=mixed_reads,checked=mixed_checked,commits=command_completions;
    uint32_t current=mixed_job.generation,done=mixed_job.done,chunk=mixed_job.chunk_generation;
    if(kui_cdda_control_status(&command_control,&before)!=KUI_CDDA_CONTROL_OK ||
       mixed_dispatch(token)!=KUI_CDDA_JOB_STALE || mixed_commit(&old)!=KUI_CDDA_JOB_STALE ||
       kui_cdda_control_status(&command_control,&after)!=KUI_CDDA_CONTROL_OK ||
       !command_same_status(&before,&after) || reads!=mixed_reads || checked!=mixed_checked ||
       commits!=command_completions || current!=mixed_job.generation || done!=mixed_job.done || chunk!=mixed_job.chunk_generation)
        return false;
    mixed_stale_refusals+=2u;return true;
}
static bool mixed_job_unchanged(uint32_t token,uint32_t done,enum kui_cdda_job_state state) {
    return mixed_job.generation==token && mixed_job.done==done && mixed_job.state==state &&
        mixed_job.session_epoch==mixed_data_epoch;
}
static bool mixed_cancel_cases(void) {
    mixed_auto=false;if(!mixed_drain()) return false;
    struct kui_cdda_control_status status,paused;
    cdda_display_line("3/7: queued data survives audio SEEK; explicit cancel");
    mixed_dispatch_enabled=false;
    if(!mixed_begin(509u,4096u,UINT32_MAX,false)) return false;
    uint32_t old=mixed_token;
    struct kui_cdda_job_span span={old,mixed_data_epoch,1u,509u,2048u};
    struct kui_cdda_control_request seek=command_request(KUI_CDDA_CONTROL_SEEK);seek.frame=TONE_FIRST+12345u;
    if(!command_accept(seek) || !mixed_job_unchanged(old,0,KUI_CDDA_JOB_QUEUED) || !mixed_cancel() ||
       !mixed_begin(513u,4096u,UINT32_MAX,false) || !mixed_expect_stale(old,span)) return false;
    mixed_dispatch_enabled=true;if(!mixed_drain()) return false;completed++;
    cdda_display_line("4/7: partial data survives PAUSE/STATUS/RESUME");
    if(!mixed_begin(1021u,32768u,UINT32_MAX,false)) return false;
    mixed_stop_after_chunk=true;
    while(!mixed_job.done) if(!command_run(4410u)) return false;
    old=mixed_token;span=(struct kui_cdda_job_span){old,mixed_data_epoch,1u,1021u,2048u};
    if(mixed_job.done!=2048u || !command_accept(command_request(KUI_CDDA_CONTROL_PAUSE)) ||
       !command_status(KUI_CDDA_CONTROL_PAUSED,&paused) ||
       !mixed_job_unchanged(old,2048u,KUI_CDDA_JOB_QUEUED) || !paused_silence() ||
       !command_status(KUI_CDDA_CONTROL_PAUSED,&status) || !command_same_status(&paused,&status)) return false;
    command_paused_frame=paused.frame;
    if(!command_accept(command_request(KUI_CDDA_CONTROL_RESUME)) ||
       session_first!=command_paused_frame || !mixed_job_unchanged(old,2048u,KUI_CDDA_JOB_QUEUED) ||
       !mixed_cancel() || !mixed_begin(1023u,4096u,UINT32_MAX,false) || !mixed_expect_stale(old,span)) return false;
    mixed_dispatch_enabled=true;if(!mixed_drain()) return false;completed++;
    cdda_display_line("5/7: READY data survives STOP/restart; no stale commit");
    if(!mixed_begin(mixed_file_bytes-4096u,4096u,UINT32_MAX,false)) return false;
    mixed_hold_commit=true;uint32_t checked=mixed_checked;
    while(mixed_job.state!=KUI_CDDA_JOB_READY) if(!command_run(4410u)) return false;
    old=mixed_token;span=mixed_job.pending;
    if(mixed_job.done || mixed_checked!=checked ||
       !command_accept(command_request(KUI_CDDA_CONTROL_STOP)) ||
       !command_status(KUI_CDDA_CONTROL_STOPPED,&status) ||
       !command_accept(command_request(KUI_CDDA_CONTROL_STOP)) ||
       !command_play(TONE_FIRST,TONE_FIRST+TONE_FRAMES,true) ||
       !mixed_job_unchanged(old,0,KUI_CDDA_JOB_READY) || !mixed_cancel() ||
       !mixed_begin(511u,4096u,UINT32_MAX,false) || !mixed_expect_stale(old,span)) return false;
    mixed_hold_commit=false;mixed_dispatch_enabled=true;
    if(!mixed_drain()) return false;
    completed++;
    mixed_auto=true;return true;
}
static bool mixed(void) {
    mixed_enabled=mixed_auto=mixed_dispatch_enabled=true;
    mixed_hold_commit=mixed_stop_after_chunk=mixed_full_job=mixed_faulted=false;
    mixed_data_epoch=1u;mixed_sequence=mixed_full_passes=mixed_checked=mixed_errors=0;
    mixed_jobs_done=mixed_chunks_done=mixed_reads=mixed_cancels=mixed_stale_refusals=0;
    mixed_worst_ticks=mixed_max_gap_ticks=0;
    command_accepted=command_completions=command_status_checks=command_paused_frame=command_max_loops=0;
    command_running=false;
    for(unsigned i=0;i<MIXED_CLASSES;i++) mixed_class_counts[i]=0;
    if(kui_cdda_job_init(&mixed_job)!=KUI_CDDA_JOB_OK ||
       !kui_cdda_stream_clock_init(&mixed_clock,ticks(),KUI_CDDA_TMU_HZ) ||
       !kui_cdda_ms_to_ticks(10u,&mixed_budget_ticks) || !kui_cdda_ms_to_ticks(5000u,&mixed_gap_limit)) goto failed;
    if(cdda_storage_data_open(&mixed_file_bytes) || mixed_file_bytes!=8u*1024u*1024u) goto failed;
    uint32_t bytes;
    if(cdda_storage_open("0:/KUI/tests/cdda/stereo.raw",&bytes)) goto failed;
    cdda_storage_close();
    if(!bytes || bytes%2352u || kui_cdda_control_init(&command_control,bytes/4u)!=KUI_CDDA_CONTROL_OK) goto failed;
    mixed_last_progress=ticks();
    cdda_display_line("1/7: contiguous8MiB checked pass during stereo loop");
    if(!command_play(TONE_FIRST,TONE_FIRST+TONE_FRAMES,true)) goto failed;
    /* Advertised workload time starts after source setup and the first key-on.
     * Initialization/prefill therefore cannot satisfy the180-second gate. */
    if(!kui_cdda_stream_clock_init(&mixed_clock,ticks(),KUI_CDDA_TMU_HZ)) goto failed;
    mixed_last_progress=ticks();mixed_max_gap_ticks=0;
    while(!mixed_full_passes) {
        if(!command_run(4410u) || mixed_clock.seconds>=120u) goto failed;
    }
    completed++;
    cdda_display_line("2/7: eight size classes, unaligned/backwards/EOF");
    while(mixed_min_class()<16u) {
        if(!command_run(4410u) || mixed_clock.seconds>=150u) goto failed;
    }
    completed++;
    if(!mixed_cancel_cases()) goto failed;
    cdda_display_line("6/7: mixed checked traffic until180TMU seconds");
    while(mixed_clock.seconds<180u) if(!command_run(4410u)) goto failed;
    completed++;
    cdda_display_line("7/7: drain current job; stop and verify final gates");
    mixed_auto=false;if(!mixed_drain()) goto failed;
    struct kui_cdda_control_status status;
    if(!command_accept(command_request(KUI_CDDA_CONTROL_STOP)) ||
       !command_status(KUI_CDDA_CONTROL_STOPPED,&status) || mixed_full_passes!=1u ||
       mixed_min_class()<16u || mixed_cancels!=3u || mixed_stale_refusals!=6u || mixed_errors ||
       mixed_live() || mixed_job.pending.bytes || command_control.current.pending ||
       mixed_clock.seconds<180u) goto failed;
    mixed_enabled=false;cdda_storage_data_close();completed++;
    return true;
failed:
    (void)mixed_fatal("Mixed job/control test failed; owned audio stopped.");
    cdda_storage_data_close();return false;
}
static void mixed_report(void) {
    uint32_t covered=0,worst_us=0,gap_us=0;
    for(unsigned i=0;i<MIXED_CLASSES;i++) if(mixed_class_counts[i]>=16u) covered++;
    (void)kui_cdda_ticks_to_us(mixed_worst_ticks,&worst_us);
    (void)kui_cdda_ticks_to_us(mixed_max_gap_ticks,&gap_us);
    display_pair("TMU seconds / full8MiB passes: ",mixed_clock.seconds,mixed_full_passes);
    display_pair("Committed checked bytes / errors: ",mixed_checked,mixed_errors);
    display_pair("Size classes / minimum completions: ",covered,mixed_min_class());
    display_pair("Completed logical jobs / chunks: ",mixed_jobs_done,mixed_chunks_done);
    display_pair("EXPECTED cancels / stale refusals: ",mixed_cancels,mixed_stale_refusals);
    display_pair("Audio command actions / STATUS checks: ",command_completions,command_status_checks);
    display_pair("Data job / no-progress gap (us): ",worst_us,gap_us);
}
