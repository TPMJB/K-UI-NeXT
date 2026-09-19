/* SPDX-License-Identifier: GPL-3.0-only */
#include "platform.h"
#include "kui/report.h"
#include <kos.h>
#include <dc/minifont.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* No KOS CD/VFS polling and no serial console competing with the SD adapter. */
KOS_INIT_FLAGS(INIT_IRQ | INIT_CONTROLLER | INIT_NO_DCLOAD | INIT_QUIET);
#ifndef KUI_BUILD_ID
#define KUI_BUILD_ID "local-unversioned"
#endif
#ifdef KUI_SD_RUNTIME
#define KUI_BUTTON_MSTATS (1u<<30)
#define KUI_BUTTON_BENCH (1u<<29)
static struct kui_memory_stats memory_status;
static bool memory_valid;
#define KUI_ROLE "SD runtime"
#else
#define KUI_ROLE "CD bootstrap"
#endif

#define LOG_LINES 768
#define LINE_BYTES 77
#define VISIBLE_LINES 19
static mutex_t lock = MUTEX_INITIALIZER;
static char lines[LOG_LINES][LINE_BYTES];
static unsigned line_count;
static bool log_truncated, busy, cancel_requested, saving_report;
static unsigned pending;
static char report[LOG_LINES * LINE_BYTES + 256];
#ifdef KUI_SD_RUNTIME
static struct kui_capture_progress capture_status;
static uint64_t rate_at,rate_bytes;
static unsigned rate_kib;
void kui_capture_status(void *ctx,const struct kui_capture_progress *p) {
    (void)ctx;mutex_lock(&lock);
    if(capture_status.phase!=p->phase || p->elapsed_ms<rate_at || p->done<rate_bytes) {
        rate_at=p->elapsed_ms;rate_bytes=p->done;rate_kib=0;
    } else if(p->elapsed_ms-rate_at>=1000) {
        rate_kib=(unsigned)((p->done-rate_bytes)*1000/(p->elapsed_ms-rate_at)/1024);
        rate_at=p->elapsed_ms;rate_bytes=p->done;
    }
    capture_status=*p;mutex_unlock(&lock);
}
#endif

void kui_log(const char *format, ...) {
    char text[256];
    va_list args;
    va_start(args, format); vsnprintf(text, sizeof(text), format, args); va_end(args);
    mutex_lock(&lock);
    size_t size = strlen(text);
    for(size_t pos = 0; pos < size || pos == 0; pos += LINE_BYTES - 1) {
        if(line_count == LOG_LINES) {
            log_truncated = true;
            memmove(lines, lines + 1, (LOG_LINES - 1) * LINE_BYTES);
            --line_count;
        }
        snprintf(lines[line_count++], LINE_BYTES, "%.*s", LINE_BYTES - 1, text + pos);
    }
    mutex_unlock(&lock);
}
bool kui_cancelled(void) {
    mutex_lock(&lock);
    bool requested = cancel_requested;
    mutex_unlock(&lock);
    return requested;
}

static void save_report(const char *trigger,const char *outcome,bool automatic) {
    /* A new B press can cancel saving. The earlier capture Stop must not
     * suppress its own report. Keep busy set until all report I/O has ended. */
    mutex_lock(&lock);
    if(automatic) cancel_requested=false;
    saving_report=true;
    mutex_unlock(&lock);
#ifdef KUI_SD_RUNTIME
    kui_memory_log("save log");
#endif
    char path[96]={0};enum kui_report_result result=KUI_REPORT_FAILED;
    if(kui_sd_connect()) {
        mutex_lock(&lock);
        size_t used=(size_t)snprintf(report,sizeof(report),
            "K-UI " KUI_ROLE " %s\nLog truncated: %s\nReport trigger: %s\nCapture result: %s\n",
            KUI_BUILD_ID,log_truncated?"YES":"no",trigger,outcome);
        for(unsigned i=0;i<line_count;i++)
            used+=(size_t)snprintf(report+used,sizeof(report)-used,"%s\n",lines[i]);
        mutex_unlock(&lock);
        result=kui_report_save(report,used,path,kui_log,kui_cancelled);
        kui_sd_disconnect();
    }
    if(result==KUI_REPORT_SAVED) kui_log("Report saved: %s",path+2);
    else {
        kui_log("Report save %s; diagnostics Y can retry.",result==KUI_REPORT_STOPPED?"cancelled":"FAILED");
        if(automatic) kui_log("Capture result remains: %s",outcome);
    }
    mutex_lock(&lock);saving_report=false;mutex_unlock(&lock);
}
static void *worker(void *unused) {
    (void)unused;
    for(;;) {
        mutex_lock(&lock);
        unsigned action = pending;
        pending = 0;
        mutex_unlock(&lock);
        if(!action) { thd_sleep(16); continue; }
        if(kui_cancelled()) kui_log("Operation stopped before starting.");
        else {
            if(action == 1) kui_disc_probe();
            if(action == 2 && kui_sd_connect()) {
                kui_storage_probe(kui_log, kui_cancelled);
                kui_sd_disconnect();
            }
            if(action == 3) save_report("manual","see operation log",false);
#ifdef KUI_SD_RUNTIME
            if(action == 7) {
                kui_memory_log("bench start");
                enum kui_bench_result result=kui_bench_start();
                kui_memory_log("bench end");
                const char *outcome=result==KUI_BENCH_COMPLETE?"complete":result==KUI_BENCH_STOPPED?"stopped":"failed";
                kui_log("Bench result: %s",outcome);
                kui_log("Saving diagnostic report automatically; B cancels log save.");
                save_report("auto bench",outcome,true);
            }
            if(action >= 4 && action <= 6) {
                kui_memory_log("capture/verify start");
                enum kui_capture_result result=kui_capture_start((enum kui_capture_mode)(action-4),KUI_BUILD_ID);
                kui_memory_log("capture/verify end");
                const char *outcome=result==KUI_CAPTURE_COMPLETE?"complete":result==KUI_CAPTURE_STOPPED?"stopped":"failed";
                kui_log("Capture result: %s",outcome);
                kui_log("Saving diagnostic report automatically; B cancels log save.");
                save_report(action==4?"auto new capture":action==5?"auto resume":"auto verify",outcome,true);
            }
#endif
        }
#ifdef KUI_SD_RUNTIME
        kui_log("Operation ended. Diagnostics page: Y saves the log to SD.");
#else
        kui_log("Operation ended. Y saves the current log to SD.");
#endif
        mutex_lock(&lock);
        busy = false;
        mutex_unlock(&lock);
    }
    return NULL;
}

static void draw(unsigned scroll,unsigned page) {
    char visible[VISIBLE_LINES][LINE_BYTES] = {{0}};
    char status[LINE_BYTES];
    mutex_lock(&lock);
    unsigned end = line_count > scroll ? line_count - scroll : 0;
    unsigned first = end > VISIBLE_LINES ? end - VISIBLE_LINES : 0;
    unsigned actual=end-first;
    for(unsigned i = first; i < end; ++i) strcpy(visible[i - first], lines[i]);
    snprintf(status, sizeof(status), "%s  |  %u log lines%s",
        busy ? (saving_report ? (cancel_requested?"STOPPING LOG SAVE":"SAVING LOG") :
            (cancel_requested ? "STOP REQUESTED" : "WORKING")) : "READY",
        line_count, log_truncated ? " (earlier lines truncated)" : "");
    mutex_unlock(&lock);
    /* Multibuffer mode keeps this drawing area separate from the displayed
     * frame. Clearing the displayed frame exposes blank/partial redraws. */
    vid_clear(8, 16, 24);
    minifont_set_color(100, 220, 220);
    minifont_draw_str(vram_s + 20 * 640 + 16, 640, "K-UI NeXT | " KUI_ROLE);
    minifont_set_color(220, 230, 235);
    minifont_draw_str(vram_s + 44 * 640 + 16, 640, "Build " KUI_BUILD_ID);
#ifdef KUI_SD_RUNTIME
    minifont_draw_str(vram_s + 44*640+360,640,"L: mstats   R: bench");
#endif
    minifont_draw_str(vram_s + 68 * 640 + 16, 640,page?
        "A New dump   X Resume latest   Y Verify latest":
        "A Disc probe   X Write/read SD test   Y Save log");
    minifont_draw_str(vram_s + 88 * 640 + 16, 640,
        "B Stop   Left/Right page   Up/Down scroll   Start latest");
    minifont_draw_str(vram_s + 116 * 640 + 16, 640, status);
    unsigned top=144,shown=VISIBLE_LINES;
#ifdef KUI_SD_RUNTIME
    if(page) {
        static const char *phases[]={"Identifying disc","Checking saved prefix","Capturing","Verifying saved files","Verified"};
        struct kui_capture_progress p;unsigned rate;
        mutex_lock(&lock);p=capture_status;rate=rate_kib;mutex_unlock(&lock);
        char line[77];
        snprintf(line,sizeof(line),"%s  Track %u/%u  Retry %lu",phases[p.phase],p.track,p.tracks,(unsigned long)p.retries);
        minifont_draw_str(vram_s + 140*640+16,640,line);
        snprintf(line,sizeof(line),"%lu / %lu MiB  %u KiB/s  Saved %lu MiB",
            (unsigned long)(p.done/(1024*1024)),(unsigned long)(p.total/(1024*1024)),rate,
            (unsigned long)(p.committed/(1024*1024)));
        minifont_draw_str(vram_s + 160*640+16,640,line);
        if(memory_valid) snprintf(line,sizeof(line),"RAM ~%lu/%lu KiB  sampled peak ~%lu KiB",
            (unsigned long)(memory_status.used/1024),(unsigned long)(memory_status.physical/1024),
            (unsigned long)(memory_status.sampled_peak/1024));
        else snprintf(line,sizeof(line),"RAM snapshot unavailable; L trigger retries");
        minifont_draw_str(vram_s+180*640+16,640,line);
        top=208;shown=15;
    }
#endif
    unsigned first_visible=actual>shown?actual-shown:0;
    for(unsigned i = 0; i < shown && i+first_visible<actual; ++i)
        minifont_draw_str(vram_s + (top + i * 16) * 640 + 16, 640, visible[i+first_visible]);
    /* Publish the completed frame, then let KOS select the next drawing area. */
    vid_waitvbl();
    vid_flip(-1);
}

static unsigned controller_buttons(void) {
    maple_device_t *controller = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
    cont_state_t *state = controller ? maple_dev_status(controller) : NULL;
    if(!state) return 0;
    unsigned buttons=state->buttons;
    if(state->joyx<-48) buttons|=CONT_DPAD_LEFT;
    if(state->joyx>48) buttons|=CONT_DPAD_RIGHT;
    if(state->joyy<-48) buttons|=CONT_DPAD_UP;
    if(state->joyy>48) buttons|=CONT_DPAD_DOWN;
#ifdef KUI_SD_RUNTIME
    if(state->ltrig>128) buttons|=KUI_BUTTON_MSTATS;
    if(state->rtrig>128) buttons|=KUI_BUTTON_BENCH;
#endif
    return buttons;
}

#ifndef KUI_SD_RUNTIME
static bool boot_cancelled(void) {
    draw(0,0);
    return (controller_buttons() & CONT_B) != 0;
}
#endif

int main(void) {
    vid_set_mode(DM_640x480 | DM_MULTIBUFFER, PM_RGB565);
    kui_log("Running " KUI_ROLE " build " KUI_BUILD_ID);
    kui_log("Video: %ux%u %s %s, buffered",
        (unsigned)vid_mode->width, (unsigned)vid_mode->height,
        vid_mode->cable_type == CT_VGA ? "VGA" :
            (vid_mode->flags & VID_PAL ? "PAL" : "NTSC"),
        vid_mode->flags & VID_INTERLACE ? "interlaced" : "progressive");
#ifndef KUI_SD_RUNTIME
    kui_log("Hold B during startup for built-in diagnostics.");
    kui_log("Otherwise load /KUI/runtime.kui from SD.");
    uint64_t until = timer_ms_gettime64() + 1500;
    bool fallback = false;
    while(timer_ms_gettime64() < until) {
        if(boot_cancelled()) { fallback = true; break; }
        thd_sleep(16);
    }
    if(!fallback) kui_bootstrap_load(boot_cancelled);
    kui_log("Using built-in CD diagnostics; SD runtime is not running.");
#endif
    kui_log("Diagnostic code and fonts are loaded entirely in RAM.");
    kui_log("Replace boot CD with a known-good retail GD-ROM; close lid.");
    kui_log("Diagnostics page: A disc samples; X SD test; Y save log.");
    kui_log("Use a spare test card. No formatting; existing files preserved.");
#ifdef KUI_SD_RUNTIME
    kui_log("Capture page: A new dump; X resume latest matching disc; Y verify.");
    kui_log("Left/Right switches capture/diagnostics. B stops and checkpoints.");
    kui_log("New dumps use separate /KUI/dumps/ folders. Keep a known-good disc inserted.");
    kui_log("Capture rereads saved files; a reference match is a separate PC check.");
    kui_log("Capture uses single optical reads; final CRC32/SHA256 readback stays on.");
    kui_log("Capture/Resume/Verify auto-save a report after ending. Wait for READY.");
    kui_log("R trigger: isolated benchmarks from /KUI/bench.cfg (see docs/benchmarks.md).");
#else
    kui_log("Full capture is available in the updated SD runtime.");
#endif
    kthread_attr_t attrs = {.stack_size = 64 * 1024, .label = "kui-io"};
    if(!thd_create_ex(&attrs, worker, NULL)) {
        kui_log("Unable to start I/O worker; reset console");
        for(;;) { draw(0,0); thd_sleep(100); }
    }
#ifdef KUI_SD_RUNTIME
    kui_memory_log("runtime ready");
    memory_valid=kui_memory_snapshot(&memory_status);
    uint64_t next_memory_sample=timer_ms_gettime64()+1000;
#endif
    unsigned previous = 0, scroll = 0;
    unsigned page=0;
#ifdef KUI_SD_RUNTIME
    page=1;
#endif
    for(;;) {
        unsigned buttons = controller_buttons();
        unsigned pressed = buttons & ~previous;
        previous = buttons;
        mutex_lock(&lock);
        if(pressed & CONT_B) cancel_requested = true;
        if(!busy && !(buttons & CONT_B)) {
#ifdef KUI_SD_RUNTIME
            if(pressed & (CONT_DPAD_LEFT|CONT_DPAD_RIGHT)) {page^=1;scroll=0;}
#endif
            unsigned action = pressed & CONT_A ? 1 : pressed & CONT_X ? 2 : pressed & CONT_Y ? 3 : 0;
            if(action) action+=page?3:0;
#ifdef KUI_SD_RUNTIME
            if(pressed & KUI_BUTTON_BENCH) action=7;
#endif
            if(action) {
                pending = action; busy = true; cancel_requested = false; scroll = 0;
#ifdef KUI_SD_RUNTIME
                if(page) {capture_status=(struct kui_capture_progress){0};rate_at=rate_bytes=0;rate_kib=0;}
#endif
            }
        }
        if((pressed & CONT_DPAD_UP) && scroll + (page?15:VISIBLE_LINES) < line_count) ++scroll;
        if((pressed & CONT_DPAD_DOWN) && scroll) --scroll;
        if(pressed & CONT_START) scroll = 0;
        mutex_unlock(&lock);
#ifdef KUI_SD_RUNTIME
        if(pressed & KUI_BUTTON_MSTATS) {kui_memory_log("L trigger");scroll=0;}
        if(timer_ms_gettime64()>=next_memory_sample) {
            memory_valid=kui_memory_snapshot(&memory_status);next_memory_sample=timer_ms_gettime64()+1000;
        }
#endif
        draw(scroll,page);
        thd_sleep(33);
    }
}
