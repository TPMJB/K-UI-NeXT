/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/salvage_display.h"
#include <inttypes.h>
#include <stdio.h>

static const char *read_result(enum kui_salvage_read_state state) {
    switch(state) {
    case KUI_SALVAGE_READ_ACCEPTED:return "checks passed";
    case KUI_SALVAGE_READ_RETRY:return "retrying";
    case KUI_SALVAGE_READ_SPLIT:return "splitting";
    case KUI_SALVAGE_READ_DEFERRED:return "deferred";
    case KUI_SALVAGE_READ_FATAL:return "drive stopped";
    default:return "reading";
    }
}
static const char *last_result(enum kui_salvage_read_state state) {
    if(state==KUI_SALVAGE_READ_SPLIT)return "range split";
    if(state==KUI_SALVAGE_READ_RETRY)return "retry needed";
    return read_result(state);
}
static unsigned collection_percent(uint64_t done,uint64_t total) {
    if(!total)return 0;
    if(done>=total)return 1000;
    if(done<=UINT64_MAX/1000u)return (unsigned)(done*1000u/total);
    /* Admitted disc sizes use the exact integer path. Keep malformed large
     * snapshots bounded and never round an unfinished scan up to 100%. */
    unsigned percent=(unsigned)((double)done/(double)total*1000.0);
    return percent>999u?999u:percent;
}
static void collection_progress(struct kui_app_status *out,
    const struct kui_salvage_status *status) {
    unsigned percent=collection_percent(status->done,status->total);
    bool timed=status->collection_active && status->collection_elapsed_ms>=2000u &&
        status->collected_this_run && status->collected_this_run<=status->done &&
        status->collected_this_run<=UINT64_MAX/10000u;
    if(!status->total)
        snprintf(out->lines[4],KUI_APP_LINE_CAP,"Collection --%%  avg -- KiB/s");
    else if(!timed)
        snprintf(out->lines[4],KUI_APP_LINE_CAP,"Collection %u.%u%%  avg -- KiB/s",percent/10u,percent%10u);
    else {
        uint64_t rate=status->collected_this_run*10000u/status->collection_elapsed_ms/1024u;
        snprintf(out->lines[4],KUI_APP_LINE_CAP,"Collection %u.%u%%  avg %" PRIu64 ".%" PRIu64 " KiB/s",
            percent/10u,percent%10u,rate/10u,rate%10u);
    }
    if(status->collection_active && (status->targets || status->read_state==KUI_SALVAGE_READ_RETRY ||
       status->read_state==KUI_SALVAGE_READ_DEFERRED || status->read_state==KUI_SALVAGE_READ_SPLIT ||
       status->read_state==KUI_SALVAGE_READ_FATAL)) {
        snprintf(out->lines[6],KUI_APP_LINE_CAP,"Scan ETA uncertain; deferred ranges repair later.");
        return;
    }
    uint64_t remaining=status->done<status->total?status->total-status->done:0;
    if(!timed || !status->total || (remaining && status->collection_elapsed_ms>UINT64_MAX/remaining)) {
        snprintf(out->lines[6],KUI_APP_LINE_CAP,"%s",status->collection_active?
            "Scan time left: estimating; recovery follows.":"Scan estimate unavailable.");
        return;
    }
    uint64_t scaled=remaining*status->collection_elapsed_ms;
    uint64_t ms=scaled/status->collected_this_run;
    uint64_t seconds=ms/1000u+(ms%1000u!=0 || scaled%status->collected_this_run!=0);
    if(seconds>=3600000u)
        snprintf(out->lines[6],KUI_APP_LINE_CAP,"Est. scan time left: >999h; recovery follows.");
    else if(seconds>=3600u) {
        uint64_t minutes=seconds/60u+(seconds%60u!=0);
        snprintf(out->lines[6],KUI_APP_LINE_CAP,"Est. scan time left: %" PRIu64 "h %02" PRIu64 "m; recovery follows.",
            minutes/60u,minutes%60u);
    }
    else if(seconds>=60u)
        snprintf(out->lines[6],KUI_APP_LINE_CAP,"Est. scan time left: %" PRIu64 "m %02" PRIu64 "s; recovery follows.",
            seconds/60u,seconds%60u);
    else snprintf(out->lines[6],KUI_APP_LINE_CAP,"Est. scan time left: %" PRIu64 "s; recovery follows.",seconds);
}
static void saved_progress(struct kui_app_status *out,
    const struct kui_salvage_status *status) {
    bool directory=status->sd_phase==KUI_SALVAGE_SD_DIRECTORY;
    uint64_t done=status->sd_done<status->sd_total?status->sd_done:status->sd_total;
    unsigned percent=directory?0:collection_percent(done,status->sd_total);
    out->done=directory?0:done;out->total=directory?0:status->sd_total;
    if(directory)
        snprintf(out->lines[0],KUI_APP_LINE_CAP,"Indexing saved repair files.");
    else if(status->sd_phase==KUI_SALVAGE_SD_JOURNAL)
        snprintf(out->lines[0],KUI_APP_LINE_CAP,"Rebuilding saved journal progress.");
    else if(status->track)
        snprintf(out->lines[0],KUI_APP_LINE_CAP,"Saved track %u/%u  FAD %" PRIu32,
            status->track,status->tracks,status->fad);
    else snprintf(out->lines[0],KUI_APP_LINE_CAP,"Checking saved target data.");
    snprintf(out->lines[1],KUI_APP_LINE_CAP,"Queue %" PRIu32 "  saved repairs %" PRIu32 "  left %" PRIu32,
        status->targets,status->recovered,status->remaining);
    const char *kind=status->sd_phase==KUI_SALVAGE_SD_JOURNAL?"records":"checks";
    if(directory)
        snprintf(out->lines[2],KUI_APP_LINE_CAP,"Directory entries checked %" PRIu64,status->sd_done);
    else if(status->sd_total)
        snprintf(out->lines[2],KUI_APP_LINE_CAP,"Saved %s %" PRIu64 "/%" PRIu64 " (%u.%u%%)",
            kind,done,status->sd_total,percent/10u,percent%10u);
    else snprintf(out->lines[2],KUI_APP_LINE_CAP,"Saved %s 0/0; nothing to check.",kind);
    snprintf(out->lines[3],KUI_APP_LINE_CAP,"%s",directory?
        "Checking saved repair filenames on SD.":"Checking saved data on SD.");
    snprintf(out->lines[4],KUI_APP_LINE_CAP,"%s",directory?
        "Saved target checks follow this directory scan.":"Disc repair follows these saved-data checks.");
    snprintf(out->lines[5],KUI_APP_LINE_CAP,"%.76s",status->job);
    snprintf(out->lines[6],KUI_APP_LINE_CAP,"B stops safely; existing progress is retained.");
    if(out->stopped) {
        snprintf(out->lines[3],KUI_APP_LINE_CAP,"%s",directory?
            "Directory scan stopped; saved progress retained.":"SD check stopped; saved progress retained.");
        snprintf(out->lines[4],KUI_APP_LINE_CAP,"%s",directory?
            "Resume rebuilds the saved repair index.":"Resume checks saved data before disc repair.");
    }
}
static void trial_progress(struct kui_app_status *out,const struct kui_salvage_status *status) {
    static const char *const names[]={"Original","Original / 1 retry","4x / 1 retry","2x / 1 retry"};
    static const char *const read_names[]={"Single","Lead-in 4","Centered 16","Reseek + 16"};
    bool read_trial=status->trial_read_strategy;
    out->done=status->trial_done;out->total=status->trial_total;
    if(status->trial_qualification_done)
        snprintf(out->lines[0],KUI_APP_LINE_CAP,"%s steps %u/%u; %u sampled sectors; FAD %" PRIu32,
            read_trial?"Read":"Trial",status->trial_done,status->trial_total,status->trial_sample_count,status->fad);
    else if(read_trial)snprintf(out->lines[0],KUI_APP_LINE_CAP,"Preparing the fixed unresolved-sector sample");
    else snprintf(out->lines[0],KUI_APP_LINE_CAP,"Qualifying profile %u/4 against saved disc anchors",
        status->trial_profile<4?status->trial_profile+1:4);
    snprintf(out->lines[1],KUI_APP_LINE_CAP,"Saved repairs %" PRIu32 "  remaining %" PRIu32 "; trial %s",
        status->recovered,status->remaining,status->trial_budget_expired?"budget reached":
        out->stopped?"stopped":status->trial_complete?"finished":"running");
    for(unsigned p=0;p<KUI_SALVAGE_TRIAL_PROFILES;++p) {
        const char *name=read_trial?read_names[p]:names[p];
        if(!status->trial_qualified[p]) {
            snprintf(out->lines[2+p],KUI_APP_LINE_CAP,"%s: %s",name,
                status->trial_qualification_done?"not qualified / skipped":"qualification pending");
        } else {
            uint64_t tenths=status->trial_profile_ms[p]/100u;
            snprintf(out->lines[2+p],KUI_APP_LINE_CAP,"%s: valid %" PRIu32 "/%" PRIu32 "  new %" PRIu32 "  %s %" PRIu64 ".%" PRIu64 "s",
                name,status->trial_profile_valid[p],status->trial_profile_reads[p],
                status->trial_profile_repairs[p],read_trial?"I/O":"read",tenths/10u,tenths%10u);
        }
    }
    snprintf(out->lines[6],KUI_APP_LINE_CAP,"%.76s",status->job);
}
void kui_salvage_display_status(struct kui_app_status *out,
    const struct kui_salvage_status *status) {
    *out=(struct kui_app_status){.done=status->done,.total=status->total,
        .complete=status->complete,.stopped=status->result==KUI_SALVAGE_STOPPED,
        .line_count=7};
    snprintf(out->message,sizeof(out->message),"%s",status->message);
    if(status->sd_phase!=KUI_SALVAGE_SD_NONE) {saved_progress(out,status);return;}
    if(status->trial_read_strategy || status->trial_active || status->trial_complete || status->trial_sample_count || status->trial_budget_expired) {
        trial_progress(out,status);return;
    }
    const char *where=status->track?"Track":"Identity";
    if(status->track && status->read_count>1)
        snprintf(out->lines[0],KUI_APP_LINE_CAP,"%s %u/%u  FAD %" PRIu32 "-%" PRIu32,
            where,status->track,status->tracks,status->read_fad,status->read_fad+status->read_count-1u);
    else if(status->track)
        snprintf(out->lines[0],KUI_APP_LINE_CAP,"%s %u/%u  FAD %" PRIu32,
            where,status->track,status->tracks,status->read_count?status->read_fad:status->fad);
    else if(status->read_count)snprintf(out->lines[0],KUI_APP_LINE_CAP,"Checking disc identity; FAD %" PRIu32,status->read_fad);
    else snprintf(out->lines[0],KUI_APP_LINE_CAP,"Checking disc identity.");
    if(status->collection_active)
        snprintf(out->lines[1],KUI_APP_LINE_CAP,"Need reread %" PRIu32 "; repair follows scan",status->remaining);
    else snprintf(out->lines[1],KUI_APP_LINE_CAP,"Original queue %" PRIu32 "  repaired %" PRIu32 "  left %" PRIu32,
        status->targets,status->recovered,status->remaining);
    if(status->complete)snprintf(out->lines[2],KUI_APP_LINE_CAP,"Optical acquisition complete.");
    else if(status->read_state==KUI_SALVAGE_READ_DEFERRED && status->read_count && !status->read_attempt && !status->read_limit)
        snprintf(out->lines[2],KUI_APP_LINE_CAP,"Range deferred by request; no optical read.");
    else if(status->read_state==KUI_SALVAGE_READ_NONE || !status->read_limit)
        snprintf(out->lines[2],KUI_APP_LINE_CAP,"Waiting for optical reads.");
    else if(status->read_confirm_limit>1)
        snprintf(out->lines[2],KUI_APP_LINE_CAP,"Sector attempt %u/%u; audio read %u/%u; %s",
            status->read_attempt,status->read_limit,status->read_confirm,status->read_confirm_limit,
            read_result(status->read_state));
    else snprintf(out->lines[2],KUI_APP_LINE_CAP,"%s attempt %u/%u; %s",
            status->read_count>1?"Range":"Sector",status->read_attempt,status->read_limit,
            read_result(status->read_state));
    if(status->last_problem_state==KUI_SALVAGE_READ_NONE || !status->last_problem_count)
        snprintf(out->lines[3],KUI_APP_LINE_CAP,"Last problem: none this run.");
    else if(status->last_problem_state==KUI_SALVAGE_READ_DEFERRED && !status->last_problem_attempt && !status->last_problem_limit) {
        if(status->last_problem_count>1)
            snprintf(out->lines[3],KUI_APP_LINE_CAP,"Last issue FAD %" PRIu32 "-%" PRIu32 ": deferred by request",
                status->last_problem_fad,status->last_problem_fad+status->last_problem_count-1u);
        else snprintf(out->lines[3],KUI_APP_LINE_CAP,"Last issue FAD %" PRIu32 ": deferred by request",status->last_problem_fad);
    }
    else if(status->last_problem_count>1)
        snprintf(out->lines[3],KUI_APP_LINE_CAP,"Last issue FAD %" PRIu32 "-%" PRIu32 ": %u/%u; %s",
            status->last_problem_fad,status->last_problem_fad+status->last_problem_count-1u,
            status->last_problem_attempt,status->last_problem_limit,last_result(status->last_problem_state));
    else if(status->last_problem_confirm_limit>1)
        snprintf(out->lines[3],KUI_APP_LINE_CAP,"Last issue FAD %" PRIu32 ": %u/%u, audio %u/%u; %s",
            status->last_problem_fad,status->last_problem_attempt,status->last_problem_limit,
            status->last_problem_confirm,status->last_problem_confirm_limit,last_result(status->last_problem_state));
    else snprintf(out->lines[3],KUI_APP_LINE_CAP,"Last issue FAD %" PRIu32 ": %u/%u; %s",
            status->last_problem_fad,status->last_problem_attempt,status->last_problem_limit,
            last_result(status->last_problem_state));
    if(status->first_pass_complete && !status->pass)
        snprintf(out->lines[4],KUI_APP_LINE_CAP,"Preparing disc repair; B keeps progress");
    else if(status->first_pass_complete && status->pass_limit)
        snprintf(out->lines[4],KUI_APP_LINE_CAP,"Recovery pass %u/%u; B keeps progress",status->pass,status->pass_limit);
    else if(status->first_pass_complete)
        snprintf(out->lines[4],KUI_APP_LINE_CAP,"Recovery pass %u; until stopped; B keeps progress",status->pass);
    snprintf(out->lines[5],KUI_APP_LINE_CAP,"%.76s",status->job);
    snprintf(out->lines[6],KUI_APP_LINE_CAP,"Unresolved sectors keep this job incomplete.");
    if(!status->first_pass_complete) {
        collection_progress(out,status);
        if(out->stopped)snprintf(out->lines[6],KUI_APP_LINE_CAP,"Scan stopped; Resume continues collection.");
    }
}
