/* SPDX-License-Identifier: GPL-3.0-only */
#include "platform.h"
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
static bool log_truncated, busy, cancel_requested;
static unsigned pending;
static char report[LOG_LINES * LINE_BYTES + 256];

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

static void save_report(void) {
    FATFS fs;
    if(!kui_sd_connect()) return;
    if(kui_mount(&fs, kui_log)) {
        char dir[64], path[96];
        if(kui_new_probe_dir(dir, kui_log)) {
            mutex_lock(&lock);
            size_t used = (size_t)snprintf(report, sizeof(report),
                "K-UI " KUI_ROLE " %s\nLog truncated: %s\n", KUI_BUILD_ID,
                log_truncated ? "YES" : "no");
            for(unsigned i = 0; i < line_count; ++i)
                used += (size_t)snprintf(report + used, sizeof(report) - used, "%s\n", lines[i]);
            mutex_unlock(&lock);
            snprintf(path, sizeof(path), "%s/diagnostics.txt", dir);
            if(kui_write_new_file(path, report, used, kui_log))
                kui_log("Report saved: %s", path + 2);
        }
    }
    f_mount(NULL, "0:", 0);
    kui_sd_disconnect();
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
            if(action == 3) save_report();
        }
        kui_log("Operation ended. Y saves the current log to SD.");
        mutex_lock(&lock);
        busy = false;
        mutex_unlock(&lock);
    }
    return NULL;
}

static void draw(unsigned scroll) {
    char visible[VISIBLE_LINES][LINE_BYTES] = {{0}};
    char status[LINE_BYTES];
    mutex_lock(&lock);
    unsigned end = line_count > scroll ? line_count - scroll : 0;
    unsigned first = end > VISIBLE_LINES ? end - VISIBLE_LINES : 0;
    for(unsigned i = first; i < end; ++i) strcpy(visible[i - first], lines[i]);
    snprintf(status, sizeof(status), "%s  |  %u log lines%s",
        busy ? (cancel_requested ? "STOP REQUESTED" : "WORKING") : "READY",
        line_count, log_truncated ? " (earlier lines truncated)" : "");
    mutex_unlock(&lock);
    /* Multibuffer mode keeps this drawing area separate from the displayed
     * frame. Clearing the displayed frame exposes blank/partial redraws. */
    vid_clear(8, 16, 24);
    minifont_set_color(100, 220, 220);
    minifont_draw_str(vram_s + 20 * 640 + 16, 640, "K-UI NeXT | " KUI_ROLE);
    minifont_set_color(220, 230, 235);
    minifont_draw_str(vram_s + 44 * 640 + 16, 640, "Build " KUI_BUILD_ID);
    minifont_draw_str(vram_s + 68 * 640 + 16, 640, "A Disc probe   X Write/read SD test   Y Save log");
    minifont_draw_str(vram_s + 88 * 640 + 16, 640, "B Stop   D-pad Up/Down scroll   Start latest log");
    minifont_draw_str(vram_s + 116 * 640 + 16, 640, status);
    for(unsigned i = 0; i < VISIBLE_LINES; ++i)
        minifont_draw_str(vram_s + (144 + i * 16) * 640 + 16, 640, visible[i]);
    /* Publish the completed frame, then let KOS select the next drawing area. */
    vid_waitvbl();
    vid_flip(-1);
}

static unsigned controller_buttons(void) {
    maple_device_t *controller = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
    cont_state_t *state = controller ? maple_dev_status(controller) : NULL;
    return state ? state->buttons : 0;
}

#ifndef KUI_SD_RUNTIME
static bool boot_cancelled(void) {
    draw(0);
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
    kui_log("A reads disc samples. X writes new test files to SD.");
    kui_log("Use a spare test card. No formatting; existing files preserved.");
    kui_log("This diagnostic does not dump games yet.");
    kthread_attr_t attrs = {.stack_size = 64 * 1024, .label = "kui-io"};
    if(!thd_create_ex(&attrs, worker, NULL)) {
        kui_log("Unable to start I/O worker; reset console");
        for(;;) { draw(0); thd_sleep(100); }
    }
    unsigned previous = 0, scroll = 0;
    for(;;) {
        unsigned buttons = controller_buttons();
        unsigned pressed = buttons & ~previous;
        previous = buttons;
        mutex_lock(&lock);
        if(pressed & CONT_B) cancel_requested = true;
        if(!busy && !(buttons & CONT_B)) {
            unsigned action = pressed & CONT_A ? 1 : pressed & CONT_X ? 2 : pressed & CONT_Y ? 3 : 0;
            if(action) { pending = action; busy = true; cancel_requested = false; scroll = 0; }
        }
        if((pressed & CONT_DPAD_UP) && scroll + VISIBLE_LINES < line_count) ++scroll;
        if((pressed & CONT_DPAD_DOWN) && scroll) --scroll;
        if(pressed & CONT_START) scroll = 0;
        mutex_unlock(&lock);
        draw(scroll);
        thd_sleep(33);
    }
}
