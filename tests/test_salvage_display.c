/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/salvage_display.h"
#include "kui/shell_font.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct kui_app_status view;
static void render(const struct kui_salvage_status *status) {
    kui_salvage_display_status(&view,status);
    assert(view.line_count==7 && view.done==status->done && view.total==status->total);
    for(unsigned i=0;i<view.line_count;++i) {
        assert(strlen(view.lines[i])<KUI_APP_LINE_CAP);
        assert(kui_shell_font_width(view.lines[i],false)<=568u);
    }
}
int main(void) {
    struct kui_salvage_status status={.track=3,.tracks=3,.fad=173622,
        .attempts=291,.targets=31,.remaining=31,.done=1,.total=4,
        .read_fad=173622,.read_count=1,.read_attempt=2,.read_limit=2,
        .read_state=KUI_SALVAGE_READ_READING,.last_problem_fad=173599,
        .last_problem_count=1,.last_problem_attempt=2,.last_problem_limit=2,
        .last_problem_state=KUI_SALVAGE_READ_DEFERRED};
    strcpy(status.job,"/KUI/salvage/job-0002");render(&status);
    assert(strstr(view.lines[0],"FAD 173622"));
    assert(!strcmp(view.lines[1],"Original queue 31  repaired 0  left 31"));
    assert(!strcmp(view.lines[2],"Sector attempt 2/2; reading"));
    assert(!strcmp(view.lines[3],"Last issue FAD 173599: 2/2; deferred"));
    for(unsigned i=0;i<view.line_count;++i)assert(!strstr(view.lines[i],"291"));
    status.attempts=292;status.read_fad=173623;status.read_count=32;
    status.read_attempt=status.read_limit=1;render(&status);
    assert(strstr(view.lines[0],"173623-173654"));
    assert(!strcmp(view.lines[2],"Range attempt 1/1; reading"));
    assert(strstr(view.lines[3],"173599: 2/2; deferred"));
    status.read_state=KUI_SALVAGE_READ_SPLIT;render(&status);
    assert(strstr(view.lines[2],"1/1; splitting"));
    status.read_count=1;status.read_confirm=1;status.read_confirm_limit=2;
    status.read_state=KUI_SALVAGE_READ_READING;render(&status);
    assert(strstr(view.lines[2],"attempt 1/1; audio read 1/2"));
    status.read_confirm=2;render(&status);
    assert(strstr(view.lines[2],"attempt 1/1; audio read 2/2"));
    status.read_state=KUI_SALVAGE_READ_ACCEPTED;render(&status);
    assert(strstr(view.lines[2],"checks passed"));
    status.last_problem_attempt=2;status.last_problem_state=KUI_SALVAGE_READ_ACCEPTED;render(&status);
    assert(strstr(view.lines[3],"2/2; checks passed"));
    status.first_pass_complete=true;status.pass=2;render(&status);
    assert(strstr(view.lines[4],"pass 2; until stopped"));
    status.pass_limit=50;render(&status);assert(strstr(view.lines[4],"pass 2/50"));
    status.result=KUI_SALVAGE_STOPPED;render(&status);assert(view.stopped);
    status.complete=true;render(&status);
    assert(view.complete && !strcmp(view.lines[2],"Optical acquisition complete."));
    status=(struct kui_salvage_status){0};render(&status);
    assert(strstr(view.lines[0],"Checking disc identity"));
    assert(strstr(view.lines[2],"Waiting for optical"));
    assert(strstr(view.lines[3],"none this run"));
    status=(struct kui_salvage_status){.track=99,.tracks=99,.read_fad=719968,.read_count=32,
        .read_attempt=51,.read_limit=51,.read_confirm=2,.read_confirm_limit=2,
        .read_state=KUI_SALVAGE_READ_FATAL,.last_problem_fad=719999,.last_problem_count=1,
        .last_problem_attempt=51,.last_problem_limit=51,.last_problem_confirm=2,
        .last_problem_confirm_limit=2,.last_problem_state=KUI_SALVAGE_READ_FATAL};
    render(&status);assert(strstr(view.lines[2],"drive stopped"));
    status.last_problem_count=32;status.last_problem_fad=719968;render(&status);
    assert(strstr(view.lines[3],"719968-719999"));
    status=(struct kui_salvage_status){.done=1048576,.total=4194304,
        .collection_active=true,.collected_this_run=1048576,.collection_elapsed_ms=4000};
    render(&status);
    assert(!strcmp(view.lines[4],"Collection 25.0%  avg 256.0 KiB/s"));
    assert(!strcmp(view.lines[1],"Need reread 0; repair follows scan"));
    status.targets=status.remaining=719999;render(&status);
    assert(!strcmp(view.lines[1],"Need reread 719999; repair follows scan"));
    status.targets=status.remaining=0;render(&status);
    assert(!strcmp(view.lines[6],"Est. scan time left: 12s; recovery follows."));
    /* Resume's old prefix belongs to percent, never to throughput or ETA. */
    status.done=3145728;render(&status);
    assert(strstr(view.lines[4],"75.0%  avg 256.0"));
    assert(strstr(view.lines[6],"4s; recovery follows"));
    /* No new commits during a slow read: the average drops as the clock grows. */
    status.collection_elapsed_ms=8000;render(&status);
    assert(strstr(view.lines[4],"avg 128.0"));assert(strstr(view.lines[6],"8s;"));
    status.collection_elapsed_ms=1999;render(&status);
    assert(strstr(view.lines[4],"avg --"));assert(strstr(view.lines[6],"estimating"));
    status.collection_elapsed_ms=0;render(&status);
    assert(strstr(view.lines[4],"avg --"));
    status.collection_elapsed_ms=4000;status.collected_this_run=0;render(&status);
    assert(strstr(view.lines[6],"estimating"));
    status.collected_this_run=1048576;status.done=4194303;render(&status);
    assert(strstr(view.lines[4],"99.9%"));assert(strstr(view.lines[6],"1s;"));
    status.done=status.total;render(&status);
    assert(strstr(view.lines[4],"100.0%"));assert(strstr(view.lines[6],"0s;"));
    status.done=UINT64_MAX-1;status.total=UINT64_MAX;render(&status);
    assert(strstr(view.lines[4],"99.9%"));
    status.done=UINT64_MAX;status.total=1;render(&status);
    assert(strstr(view.lines[4],"100.0%"));
    status.total=0;render(&status);assert(strstr(view.lines[4],"Collection --%"));
    status=(struct kui_salvage_status){.done=1,.total=4,.collection_active=true,
        .collected_this_run=1,.collection_elapsed_ms=2001};render(&status);
    assert(strstr(view.lines[4],"avg 0.0"));assert(strstr(view.lines[6],"7s;"));
    status.collection_elapsed_ms=1200000;render(&status);
    assert(strstr(view.lines[6],"1h 00m;"));
    status.collection_elapsed_ms=2399667;render(&status);
    assert(strstr(view.lines[6],"2h 00m;"));
    status.collection_elapsed_ms=UINT64_MAX;render(&status);
    assert(strstr(view.lines[6],"estimating"));
    status.collection_elapsed_ms=1200000000;render(&status);
    assert(strstr(view.lines[6],">999h;"));
    status=(struct kui_salvage_status){.done=1048576,.total=4194304,
        .collection_active=true,.collected_this_run=1048576,.collection_elapsed_ms=4000,
        .targets=1,.remaining=1,.read_state=KUI_SALVAGE_READ_READING};
    render(&status);
    assert(!strcmp(view.lines[4],"Collection 25.0%  avg 256.0 KiB/s"));
    assert(!strcmp(view.lines[6],"Scan ETA uncertain; deferred ranges repair later."));
    status.targets=status.remaining=0;
    const enum kui_salvage_read_state failed_states[]={KUI_SALVAGE_READ_RETRY,KUI_SALVAGE_READ_DEFERRED,
        KUI_SALVAGE_READ_SPLIT,KUI_SALVAGE_READ_FATAL};
    for(unsigned i=0;i<sizeof(failed_states)/sizeof(failed_states[0]);++i) {
        status.read_state=failed_states[i];render(&status);
        assert(!strcmp(view.lines[4],"Collection 25.0%  avg 256.0 KiB/s"));
        assert(!strcmp(view.lines[6],"Scan ETA uncertain; deferred ranges repair later."));
    }
    /* A historical issue alone does not suppress an otherwise clean scan. */
    status.read_state=KUI_SALVAGE_READ_ACCEPTED;status.last_problem_state=KUI_SALVAGE_READ_DEFERRED;
    status.last_problem_fad=150;status.last_problem_count=1;
    status.last_problem_attempt=status.last_problem_limit=1;render(&status);
    assert(!strcmp(view.lines[6],"Est. scan time left: 12s; recovery follows."));
    /* The reported resumed run keeps its measured low rate without a 325h ETA. */
    status=(struct kui_salvage_status){.track=3,.tracks=3,.read_fad=599000,.read_count=32,
        .read_attempt=1,.read_limit=1,.read_state=KUI_SALVAGE_READ_READING,
        .targets=185,.remaining=185,.done=1182585600,.total=1411200000,
        .collection_active=true,.collected_this_run=75264,.collection_elapsed_ms=400000};
    render(&status);
    assert(!strcmp(view.lines[2],"Range attempt 1/1; reading"));
    assert(!strcmp(view.lines[4],"Collection 83.8%  avg 0.1 KiB/s"));
    assert(!strcmp(view.lines[6],"Scan ETA uncertain; deferred ranges repair later."));
    status.result=KUI_SALVAGE_STOPPED;render(&status);
    assert(!strcmp(view.lines[6],"Scan stopped; Resume continues collection."));
    status.read_state=KUI_SALVAGE_READ_FATAL;
    status.collection_active=false;status.result=KUI_SALVAGE_FAILED;render(&status);
    assert(!strcmp(view.lines[6],"Scan estimate unavailable."));
    status.result=KUI_SALVAGE_STOPPED;render(&status);
    assert(strstr(view.lines[4],"avg --"));assert(strstr(view.lines[6],"Scan stopped"));
    /* Individual repair keeps its existing pass/counter UI, with no rate or ETA. */
    status.first_pass_complete=true;status.pass=3;render(&status);
    assert(!strcmp(view.lines[4],"Recovery pass 3; until stopped; B keeps progress"));
    assert(!strcmp(view.lines[6],"Unresolved sectors keep this job incomplete."));
    status=(struct kui_salvage_status){.read_fad=469694,.read_count=32,
        .read_state=KUI_SALVAGE_READ_DEFERRED,.last_problem_fad=469694,.last_problem_count=32,
        .last_problem_state=KUI_SALVAGE_READ_DEFERRED,.collection_active=true};
    render(&status);
    assert(!strcmp(view.lines[2],"Range deferred by request; no optical read."));
    assert(!strcmp(view.lines[3],"Last issue FAD 469694-469725: deferred by request"));
    assert(!strstr(view.lines[2],"0/0") && !strstr(view.lines[3],"0/0"));
    assert(strstr(view.lines[6],"Scan ETA uncertain"));
    status.read_fad=469726;status.read_attempt=status.read_limit=1;status.read_state=KUI_SALVAGE_READ_READING;
    render(&status);
    assert(!strcmp(view.lines[2],"Range attempt 1/1; reading"));
    assert(strstr(view.lines[3],"469694-469725: deferred by request"));
    status.last_problem_count=1;render(&status);
    assert(!strcmp(view.lines[3],"Last issue FAD 469694: deferred by request"));
    puts("PASS salvage display: attempts, collection percent/rate, uncertain ETA, explicit no-read deferral, resume and text width");
}
