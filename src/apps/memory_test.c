/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/apps.h"
#include "kui/memory_test.h"
#include <kos/thread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct memory_context {
    struct kui_app_status *out;
    kui_cancel_fn cancel;
    kui_app_progress_fn progress;
};
static bool should_stop(void *ctx) {
    const struct memory_context *c=ctx;return c->cancel && c->cancel();
}
static void give_time(void *ctx) {(void)ctx;thd_pass();}
static void update(void *ctx,const struct kui_memory_test_result *r) {
    struct memory_context *c=ctx;struct kui_app_status *out=c->out;
    out->done=r->work_done;out->total=r->work_total;out->errors=r->errors;
    out->stopped=r->stopped;out->complete=r->complete;out->passed=r->complete && !r->errors;
    snprintf(out->message,sizeof(out->message),"%s",r->stopped?"Memory test stopped":
        r->errors?"Memory mismatch detected":r->complete?"Allocated region passed":"Testing allocated RAM");
    out->line_count=6;
    snprintf(out->lines[0],KUI_APP_LINE_CAP,"Owned test region: %lu KiB",(unsigned long)(r->buffer_bytes/1024));
    snprintf(out->lines[1],KUI_APP_LINE_CAP,"Read coverage: %lu bytes; passes %u/%u",
        (unsigned long)r->tested_bytes,r->passes,r->total_passes);
    snprintf(out->lines[2],KUI_APP_LINE_CAP,"Errors: %u; first mismatch stops the test",r->errors);
    snprintf(out->lines[3],KUI_APP_LINE_CAP,"6 full pattern/address passes");
    snprintf(out->lines[4],KUI_APP_LINE_CAP,"64 walking-bit passes at 4 KiB block edges");
    snprintf(out->lines[5],KUI_APP_LINE_CAP,"CPU accesses; other RAM and VRAM are not tested");
    if(r->errors) {
        out->line_count=8;
        snprintf(out->lines[6],KUI_APP_LINE_CAP,"First failure: byte offset %lu",(unsigned long)r->first_failure_offset);
        snprintf(out->lines[7],KUI_APP_LINE_CAP,"Expected %08lx; read %08lx",(unsigned long)r->expected,(unsigned long)r->actual);
    }
    if(c->progress) c->progress(out);
}
void kui_memory_app_run(struct kui_app_status *out,kui_log_fn log,kui_cancel_fn cancel,
    kui_app_progress_fn progress) {
    if(!out) return;
    memset(out,0,sizeof(*out));
    if(cancel && cancel()) {
        out->stopped=true;snprintf(out->message,sizeof(out->message),"Memory test stopped before allocation");
        if(progress) progress(out);
        return;
    }
    size_t bytes=KUI_MEMORY_TEST_MAX;uint32_t *memory=NULL;
    while(bytes>=64u*1024u && !(memory=malloc(bytes))) bytes/=2;
    if(!memory) {
        out->errors=1;snprintf(out->message,sizeof(out->message),"Not enough free RAM for a test region");
        if(log) log("Memory test: allocation failed; no memory tested");
        if(progress) progress(out);
        return;
    }
    if(log) log("Memory test: %lu allocated bytes only; no physical RAM sweep",(unsigned long)bytes);
    struct memory_context ctx={out,cancel,progress};
    struct kui_memory_test_ops ops={&ctx,should_stop,update,give_time};
    struct kui_memory_test_result result;
    bool passed=kui_memory_test(memory,bytes,&ops,&result);
    free(memory);
    if(log) {
        log("Memory test %s: region=%lu read_coverage=%lu passes=%u/%u errors=%u",
            passed?"PASSED":result.stopped?"STOPPED":"FAILED",(unsigned long)result.buffer_bytes,
            (unsigned long)result.tested_bytes,result.passes,result.total_passes,result.errors);
        if(result.errors) log("Memory first mismatch: offset=%lu expected=%08lx actual=%08lx",
            (unsigned long)result.first_failure_offset,(unsigned long)result.expected,(unsigned long)result.actual);
        log("Memory coverage excludes other main RAM, VRAM and audio RAM");
    }
}
