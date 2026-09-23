/* SPDX-License-Identifier: GPL-3.0-only */
#include "platform.h"
#include "kui/ui_rate.h"
#include "kui/report.h"
#ifdef KUI_SD_RUNTIME
#include "kui/shell.h"
#include "kui/settings.h"
#endif
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
static struct kui_shell shell;
static struct kui_settings settings_current, settings_pending;
static unsigned settings_generation;
static char settings_note[96];
static struct kui_capture_stats capture_summary;
static enum kui_shell_outcome capture_outcome;
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
/* The UI thread is main(). It shares the one CPU with the I/O worker, and a
 * full-screen software redraw plus vid_waitvbl (a busy-wait in this KOS) is a
 * lot of CPU. The cap below lets an operation trade screen updates for speed;
 * KUI_OPT_UI_FULL is the loop exactly as it always was. */
static kthread_t *ui_thread;
static volatile unsigned ui_hz_busy = KUI_OPT_UI_FULL;
void kui_ui_set_hz(unsigned hz) { ui_hz_busy = hz; }

/* Per-thread CPU time in ms from the scheduler. A running thread's current
 * slice is not in its total until the next switch, so add it here. */
static uint64_t cpu_ms(kthread_t *t, uint64_t now_ms) {
    uint64_t ms = thd_get_cpu_time(t);
    if(t == thd_get_current()) ms += now_ms - t->cpu_time.scheduled;
    return ms;
}
void kui_cpu_census_mark(struct kui_cpu_census *out) {
    irq_mask_t irq = irq_disable();   /* one consistent snapshot: no switch mid-sum */
    uint64_t ms = timer_ms_gettime64();
    kthread_t *self = thd_get_current();
    out->wall_us = timer_us_gettime64();
    out->worker_ms = cpu_ms(self, ms);
    out->ui_ms = ui_thread ? cpu_ms(ui_thread, ms) : 0;
    out->total_ms = thd_get_total_cpu_time() + (ms - self->cpu_time.scheduled);
    irq_restore(irq);
}
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
#ifdef KUI_SD_RUNTIME
/* Settings share the same single I/O owner as capture and reports. No menu
 * callback mounts the card or competes with an active rip. */
static void settings_operation(bool save) {
    struct kui_settings value;
    mutex_lock(&lock); value = settings_pending; mutex_unlock(&lock);
    if(!save) kui_settings_default(&value);
    bool ok = false;
    kui_sd_set_params(0, true);
    if(kui_sd_connect()) {
        FATFS fs;
        if(kui_mount(&fs, kui_log)) {
            ok = !kui_cancelled() && (save ? kui_settings_save(&value, kui_log)
                                        : kui_settings_load(&value, kui_log));
            f_mount(NULL, "0:", 0);
        }
        kui_sd_disconnect();
    }
    mutex_lock(&lock);
    if(ok) { settings_current = value; ++settings_generation; }
    snprintf(settings_note, sizeof(settings_note), "%s", ok ?
        (save ? "Preferences saved to SD." : "Preferences loaded; bench.cfg may override capture.") :
        (save ? "Save not confirmed; reopen Settings to check the card." :
                "Could not load preferences. See Diagnostics."));
    mutex_unlock(&lock);
    kui_log("Settings %s: %s", save ? "save" : "load", ok ? "complete" : "failed or stopped");
}
#endif

static void *worker(void *unused) {
    (void)unused;
    for(;;) {
        mutex_lock(&lock);
        unsigned action = pending;
        pending = 0;
        mutex_unlock(&lock);
        if(!action) { thd_sleep(16); continue; }
        if(kui_cancelled()) {
            kui_log("Operation stopped before starting.");
#ifdef KUI_SD_RUNTIME
            mutex_lock(&lock);
            if(action >= 4 && action <= 6) capture_outcome = KUI_SHELL_OUTCOME_STOPPED;
            if(action == 8 || action == 9)
                snprintf(settings_note, sizeof(settings_note), "Settings operation stopped before starting.");
            mutex_unlock(&lock);
#endif
        }
        else {
            if(action == 1) kui_disc_probe();
            if(action == 2 && kui_sd_connect()) {
                kui_storage_probe(kui_log, kui_cancelled);
                kui_sd_disconnect();
            }
            if(action == 3) save_report("manual","see operation log",false);
#ifdef KUI_SD_RUNTIME
            if(action == 8 || action == 9) settings_operation(action == 9);
            if(action == 7) {
                kui_memory_log("bench start");
                enum kui_bench_result result=kui_bench_start();
                kui_ui_set_hz(KUI_OPT_UI_FULL);   /* the cap is for the measurement, not the report save */
                kui_memory_log("bench end");
                const char *outcome=result==KUI_BENCH_COMPLETE?"complete":result==KUI_BENCH_STOPPED?"stopped":"failed";
                kui_log("Bench result: %s",outcome);
                kui_log("Saving diagnostic report automatically; B cancels log save.");
                save_report("auto bench",outcome,true);
            }
            if(action >= 4 && action <= 6) {
                kui_memory_log("capture/verify start");
                enum kui_capture_result result=kui_capture_start((enum kui_capture_mode)(action-4),KUI_BUILD_ID);
                mutex_lock(&lock);
                capture_summary = *kui_capture_last_stats();
                capture_outcome = result == KUI_CAPTURE_COMPLETE ? KUI_SHELL_OUTCOME_COMPLETE :
                    result == KUI_CAPTURE_STOPPED ? KUI_SHELL_OUTCOME_STOPPED : KUI_SHELL_OUTCOME_FAILED;
                mutex_unlock(&lock);
                kui_ui_set_hz(KUI_OPT_UI_FULL);   /* the cap is for the capture, not the report save */
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
        ui_hz_busy = KUI_OPT_UI_FULL;   /* no operation leaves its cap behind for the next */
        mutex_unlock(&lock);
    }
    return NULL;
}

#ifndef KUI_SD_RUNTIME
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
#endif

#ifdef KUI_SD_RUNTIME
static void shell_text(void *ctx, unsigned x, unsigned y, uint16_t color, const char *text) {
    uint16_t *frame = ctx;
    minifont_set_color(((color >> 11) & 31u) * 255u / 31u,
        ((color >> 5) & 63u) * 255u / 63u, (color & 31u) * 255u / 31u);
    minifont_draw_str(frame + y * 640 + x, 640, text);
}
static void draw_shell(void) {
    char visible[KUI_SHELL_LOG_ROWS][LINE_BYTES] = {{0}};
    const char *log_rows[KUI_SHELL_LOG_ROWS];
    char path[80], notice[96];
    struct kui_shell_view view = {.build = KUI_BUILD_ID, .job_dir = path,
        .settings_notice = notice, .message = "", .log_lines = log_rows};
    mutex_lock(&lock);
    unsigned max_scroll = line_count > KUI_SHELL_LOG_ROWS ? line_count - KUI_SHELL_LOG_ROWS : 0;
    if(shell.scroll > max_scroll) shell.scroll = max_scroll;
    unsigned end = line_count - shell.scroll;
    unsigned first = end > KUI_SHELL_LOG_ROWS ? end - KUI_SHELL_LOG_ROWS : 0;
    for(unsigned i = first; i < end; ++i) {
        strcpy(visible[i - first], lines[i]); log_rows[i - first] = visible[i - first];
    }
    view.log_count = end - first; view.total_log_lines = line_count;
    view.log_truncated = log_truncated;
    view.busy = busy; view.saving = saving_report; view.cancel_requested = cancel_requested;
    view.outcome = capture_outcome; view.saved_verified = capture_summary.verified;
    snprintf(path, sizeof(path), "%s", capture_summary.job_dir);
    snprintf(notice, sizeof(notice), "%s", settings_note);
    view.phase = capture_status.phase; view.track = capture_status.track; view.tracks = capture_status.tracks;
    view.rate_kib = rate_kib; view.retries = capture_status.retries;
    view.done = capture_status.done; view.total = capture_status.total;
    view.committed = capture_status.committed; view.elapsed_ms = capture_status.elapsed_ms;
    mutex_unlock(&lock);
    view.memory_valid = memory_valid; view.memory_used = memory_status.used;
    view.memory_physical = memory_status.physical; view.memory_peak = memory_status.sampled_peak;
    kui_shell_draw(vram_s, &shell, &view, shell_text, vram_s);
    vid_waitvbl(); vid_flip(-1);
}
static unsigned shell_buttons(unsigned buttons) {
    unsigned mapped = 0;
    if(buttons & CONT_DPAD_UP) mapped |= KUI_SHELL_UP;
    if(buttons & CONT_DPAD_DOWN) mapped |= KUI_SHELL_DOWN;
    if(buttons & CONT_DPAD_LEFT) mapped |= KUI_SHELL_LEFT;
    if(buttons & CONT_DPAD_RIGHT) mapped |= KUI_SHELL_RIGHT;
    if(buttons & CONT_A) mapped |= KUI_SHELL_A;
    if(buttons & CONT_B) mapped |= KUI_SHELL_B;
    if(buttons & CONT_X) mapped |= KUI_SHELL_X;
    if(buttons & CONT_Y) mapped |= KUI_SHELL_Y;
    if(buttons & CONT_START) mapped |= KUI_SHELL_START;
    if(buttons & KUI_BUTTON_MSTATS) mapped |= KUI_SHELL_L;
    if(buttons & KUI_BUTTON_BENCH) mapped |= KUI_SHELL_R;
    return mapped;
}
static unsigned worker_action(enum kui_shell_action action) {
    switch(action) {
        case KUI_SHELL_DISC_PROBE: return 1;
        case KUI_SHELL_STORAGE_PROBE: return 2;
        case KUI_SHELL_SAVE_LOG: return 3;
        case KUI_SHELL_NEW_DUMP: return 4;
        case KUI_SHELL_RESUME: return 5;
        case KUI_SHELL_VERIFY: return 6;
        case KUI_SHELL_BENCH: return 7;
        case KUI_SHELL_LOAD_SETTINGS: return 8;
        case KUI_SHELL_SAVE_SETTINGS: return 9;
        default: return 0;
    }
}
#endif

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
    ui_thread = thd_get_current();
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
    kui_settings_default(&settings_current);
    settings_pending = settings_current;
    kui_shell_init(&shell, &settings_current);
    snprintf(settings_note,sizeof(settings_note),"Loading preferences from SD...");
    pending = 8; busy = true;
    kui_log("K-UI launcher: choose Disc Ripper, Settings or Diagnostics with D-pad and A.");
    kui_log("Ripper: A new dump (confirm), X resume latest matching disc, Y verify.");
    kui_log("B returns home while idle; during work it stops and checkpoints.");
    kui_log("New dumps use separate /KUI/dumps/ folders. Keep a known-good disc inserted.");
    kui_log("Reports say if a dump matches Redump/TOSEC (copy data/known-dumps/*.db to KUI/).");
    kui_log("Capture defaults: CRC32, no automatic readback. Y Verify rereads saved files.");
    kui_log("Saved preferences apply first; explicit /KUI/bench.cfg keys override them.");
    kui_log("Capture/Resume/Verify auto-save a report after ending. Wait for READY.");
    kui_log("Diagnostics R trigger: isolated benchmarks from /KUI/bench.cfg.");
    kui_log("The screen redraws 2x a second while working, which frees CPU (ui_hz=full: off).");
#else
    kui_log("Full capture is available in the updated SD runtime.");
#endif
    kthread_attr_t attrs = {.stack_size = 64 * 1024, .label = "kui-io"};
    if(!thd_create_ex(&attrs, worker, NULL)) {
        kui_log("Unable to start I/O worker; reset console");
        for(;;) {
#ifdef KUI_SD_RUNTIME
            draw_shell();
#else
            draw(0,0);
#endif
            thd_sleep(100);
        }
    }
#ifdef KUI_SD_RUNTIME
    kui_memory_log("runtime ready");
    memory_valid=kui_memory_snapshot(&memory_status);
    uint64_t next_memory_sample=timer_ms_gettime64()+1000;
#endif
    unsigned previous = 0;
    uint64_t last_draw = 0;
    bool was_busy = false;
#ifdef KUI_SD_RUNTIME
    unsigned seen_settings_generation = 0;
    unsigned held_navigation = 0;
    uint64_t repeat_at = 0;
#else
    unsigned scroll = 0;
#endif
    for(;;) {
        unsigned buttons = controller_buttons();
        unsigned pressed = buttons & ~previous;
        previous = buttons;
#ifdef KUI_SD_RUNTIME
        const unsigned navigation = CONT_DPAD_UP | CONT_DPAD_DOWN | CONT_DPAD_LEFT | CONT_DPAD_RIGHT;
        unsigned held = buttons & navigation;
        uint64_t input_at = timer_ms_gettime64();
        if(held != held_navigation) { held_navigation = held; repeat_at = input_at + 400; }
        else if(held && input_at >= repeat_at) { pressed |= held; repeat_at = input_at + 140; }
        /* Preserve Stop/cancel priority even if B was held before an A edge. */
        if(pressed && (buttons & CONT_B)) pressed |= CONT_B;
        mutex_lock(&lock);
        if(seen_settings_generation != settings_generation) {
            kui_shell_set_preferences(&shell, &settings_current);
            seen_settings_generation = settings_generation;
        }
        enum kui_shell_action requested = kui_shell_input(&shell, shell_buttons(pressed), busy);
        if(requested == KUI_SHELL_STOP) cancel_requested = true;
        unsigned action = worker_action(requested);
        if(action && !busy) {
            if(action == 9) settings_pending = shell.draft;
            if(action == 8 || action == 9)
                snprintf(settings_note, sizeof(settings_note), "%s", action == 8 ?
                    "Loading preferences from SD..." : "Saving preferences to SD...");
            pending = action; busy = true; cancel_requested = false; shell.scroll = 0;
            ui_hz_busy = 2;
            if(action >= 4 && action <= 6) {
                capture_status = (struct kui_capture_progress){0};
                capture_summary = (struct kui_capture_stats){0};
                capture_outcome = KUI_SHELL_OUTCOME_NONE;
                rate_at = rate_bytes = 0; rate_kib = 0;
            }
        }
        bool is_busy = busy;
        mutex_unlock(&lock);
        if(requested == KUI_SHELL_MSTATS) { kui_memory_log("L trigger"); shell.scroll = 0; }
        if(timer_ms_gettime64() >= next_memory_sample) {
            memory_valid = kui_memory_snapshot(&memory_status); next_memory_sample = timer_ms_gettime64() + 1000;
        }
#else
        mutex_lock(&lock);
        if(pressed & CONT_B) cancel_requested = true;
        if(!busy && !(buttons & CONT_B)) {
            unsigned action = pressed & CONT_A ? 1 : pressed & CONT_X ? 2 : pressed & CONT_Y ? 3 : 0;
            if(action) {
                pending = action; busy = true; cancel_requested = false; scroll = 0;
            }
        }
        if((pressed & CONT_DPAD_UP) && scroll + VISIBLE_LINES < line_count) ++scroll;
        if((pressed & CONT_DPAD_DOWN) && scroll) --scroll;
        if(pressed & CONT_START) scroll = 0;
        bool is_busy = busy;
        mutex_unlock(&lock);
#endif
        /* Idle: always redraw, as before. Busy: at most ui_hz_busy redraws per
         * second, plus one at each start and end so the screen is never stale
         * about what state it is in. The controller is polled every loop either
         * way, so B still stops an operation whatever the cap. Keyed on when
         * the last draw happened, not on a precomputed deadline, so a cap that
         * changes mid-operation (the bench changes it between passes) applies at
         * once instead of waiting out the schedule the old value set. */
        unsigned hz = ui_hz_busy;
        uint64_t t = timer_ms_gettime64();
        bool due = kui_ui_redraw_due(is_busy, was_busy, hz, t, last_draw);
        was_busy = is_busy;
        if(due) {
#ifdef KUI_SD_RUNTIME
            draw_shell();
#else
            draw(scroll,0);
#endif
            last_draw = t;
        }
        thd_sleep(33);
    }
}
