/* SPDX-License-Identifier: GPL-3.0-only */
#include "platform.h"
#include "kui/ui_rate.h"
#include "kui/report.h"
#ifdef KUI_SD_RUNTIME
#include "kui/shell.h"
#include "kui/settings.h"
#include "kui/system_settings.h"
#include "kui/disc_identity.h"
#include "kui/music.h"
#include "kui/apps.h"
#include "kui/music_player.h"
#include "kui/splash.h"
#include "kui/gd_play.h"
#endif
#include <kos.h>
#include <dc/minifont.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* No KOS CD/VFS polling and no serial console competing with the SD adapter. */
#ifdef KUI_SD_RUNTIME
KOS_INIT_FLAGS(INIT_IRQ | INIT_CONTROLLER | INIT_VMU | INIT_NO_DCLOAD | INIT_QUIET);
#else
KOS_INIT_FLAGS(INIT_IRQ | INIT_CONTROLLER | INIT_NO_DCLOAD | INIT_QUIET);
#endif
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
static char destination_current[KUI_DEST_ROOT_CAP], destination_pending[KUI_DEST_ROOT_CAP];
static char capture_destination[KUI_DEST_ROOT_CAP], destination_note[128];
static bool destination_ready;
static unsigned destination_generation, destination_result, destination_offset;
static struct kui_destination_page destination_listing;
static struct kui_capture_stats capture_summary;
static enum kui_shell_outcome capture_outcome;
static char capture_message[128];
static bool drive_reset_required;
static struct kui_system_settings system_current, system_pending;
static unsigned system_generation;
static bool system_ok, system_loaded;
static char system_note[128];
static bool video_preview, video_awaiting_save, safe_video_boot, video_prior_safe;
static unsigned video_prior_mode;
static uint64_t video_deadline;
static unsigned applied_video = KUI_VIDEO_AUTO;
static struct kui_disc_identity disc_identity, disc_snapshot;
static struct kui_music_status music_snapshot;
static struct kui_app_status memory_test_status, network_test_status;
static struct kui_vmu_view vmu_snapshot;
static unsigned vmu_generation, vmu_slot_pending, vmu_page_pending, vmu_selected_pending;
static unsigned active_app;
static bool splash_active,boot_ready,player_active;
static struct kui_music_player_page music_listing;
static struct kui_app_status player_status;
static unsigned music_listing_generation,music_offset_pending;
static char music_path_pending[256];
static bool is_capture_action(unsigned action) {
    return (action>=4 && action<=6) || action==22;
}

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
static uint64_t phase_started_ms, progress_updated_ms;
void kui_capture_status(void *ctx,const struct kui_capture_progress *p) {
    (void)ctx;mutex_lock(&lock);
    if(capture_status.phase!=p->phase || p->elapsed_ms<rate_at || p->done<rate_bytes) {
        rate_at=p->elapsed_ms;rate_bytes=p->done;rate_kib=0;
        phase_started_ms=timer_ms_gettime64();
    } else if(p->elapsed_ms-rate_at>=1000) {
        rate_kib=(unsigned)((p->done-rate_bytes)*1000/(p->elapsed_ms-rate_at)/1024);
        rate_at=p->elapsed_ms;rate_bytes=p->done;
    }
    capture_status=*p;progress_updated_ms=timer_ms_gettime64();mutex_unlock(&lock);
}
#endif

void kui_log(const char *format, ...) {
    char text[256];
    va_list args;
    va_start(args, format); vsnprintf(text, sizeof(text), format, args); va_end(args);
    mutex_lock(&lock);
#ifdef KUI_SD_RUNTIME
    /* Presentation latch only. The drive adapter owns the actual poisoned
     * state; no UI action clears it or retries an aborted command. */
    if((!strncmp(text,"CMD ",4) && strstr(text," ABORT FAILED: RESET REQUIRED")) ||
        strstr(text," GUARD CORRUPTION: RESET REQUIRED"))
    {
        drive_reset_required=true;
        disc_snapshot.title[0]=0;
        disc_snapshot.state=KUI_DISC_IDENTITY_RESET_REQUIRED;
    }
#endif
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
    bool ok = false, destination_ok = false;
    char root[KUI_DEST_ROOT_CAP];
    kui_destination_default(root);
    kui_sd_set_params(0, true);
    if(kui_sd_connect()) {
        FATFS fs;
        if(kui_mount(&fs, kui_log)) {
            ok = !kui_cancelled() && (save ? kui_settings_save(&value, kui_log)
                                        : kui_settings_load(&value, kui_log));
            if(!save) destination_ok = !kui_cancelled() && kui_destination_load(root, kui_log);
            f_mount(NULL, "0:", 0);
        }
        kui_sd_disconnect();
    }
    mutex_lock(&lock);
    if(ok) { settings_current = value; ++settings_generation; }
    if(!save) {
        destination_ready = destination_ok;
        if(destination_ok) strcpy(destination_current, root);
        destination_result = destination_ok ? 1 : 3;
        snprintf(destination_note, sizeof(destination_note), "%s",
            destination_ok ? "" : "Destination unavailable. Browse and save a folder to retry.");
        ++destination_generation;
    }
    snprintf(settings_note, sizeof(settings_note), "%s", ok ?
        (save ? "Preferences saved to SD." : "Preferences loaded; bench.cfg may override capture.") :
        (save ? "Save not confirmed; reopen Settings to check the card." :
                "Could not load preferences. See Diagnostics."));
    mutex_unlock(&lock);
    kui_log("Settings %s: %s", save ? "save" : "load", ok ? "complete" : "failed or stopped");
}
/* Independent system preferences: capture choices stay in the ripper record. */
static void publish_music(void) {
    mutex_lock(&lock); music_snapshot=*kui_music_status(); mutex_unlock(&lock);
}
static void configure_music(bool next) {
    struct kui_system_settings value;
    mutex_lock(&lock);value=system_current;mutex_unlock(&lock);
    kui_music_set_config(value.music_enabled,value.music_volume);
    if(value.music_enabled && (next || !kui_music_status()->loaded)) {
        unsigned track=next?(kui_music_status()->current_index+1)%KUI_MUSIC_TRACKS:
            (unsigned)(timer_ms_gettime64()%KUI_MUSIC_TRACKS);
        kui_music_load(track,kui_cancelled);
    }
    publish_music();
}
static void system_operation(bool save) {
    struct kui_system_settings value;
    bool legacy_memory;
    mutex_lock(&lock); value=system_pending; legacy_memory=settings_current.show_memory; mutex_unlock(&lock);
    if(!save) kui_system_settings_default(&value);
    bool ok=false;
    kui_sd_set_params(0,true);
    if(kui_sd_connect()) {
        FATFS fs;
        if(kui_mount(&fs,kui_log)) {
            ok=!kui_cancelled() && (save?kui_system_settings_save(&value,kui_log):
                kui_system_settings_load(&value,legacy_memory,kui_log));
            f_mount(NULL,"0:",0);
        }
        kui_sd_disconnect();
    }
    mutex_lock(&lock);
    if(ok) {system_current=value;system_loaded=true;}
    system_ok=ok;++system_generation;
    snprintf(system_note,sizeof(system_note),"%s",ok?
        (save?"System preferences saved.":"System preferences loaded."):
        "System settings not confirmed. See Diagnostics.");
    mutex_unlock(&lock);
    if(ok) configure_music(false);
    kui_log("System settings %s: %s",save?"save":"load",ok?"complete":"failed or stopped");
}
static void app_progress(const struct kui_app_status *status) {
    mutex_lock(&lock);
    if(active_app==19) memory_test_status=*status;
    else if(active_app==20) network_test_status=*status;
    else if(active_app==24) player_status=*status;
    else vmu_snapshot.status=*status;
    mutex_unlock(&lock);
}
/* Video is changed only by main, while no foreground I/O runs. The canvas
 * remains 640x480. VGA always receives its native 60 Hz progressive timing. */
static void apply_video(unsigned requested) {
    unsigned actual=safe_video_boot?KUI_VIDEO_AUTO:requested;
    int mode=vid_check_cable()==CT_VGA?DM_640x480_VGA:
        actual==KUI_VIDEO_PAL50?DM_640x480_PAL_IL:
        actual==KUI_VIDEO_NTSC60?DM_640x480_NTSC_IL:DM_640x480;
    vid_set_mode(mode|DM_MULTIBUFFER,PM_RGB565);
    applied_video=requested;
}
/* Folder browsing and preference writes share the single storage owner. A
 * typed missing directory is allowed; only New dump creates its parents. */
static void destination_operation(bool save) {
    char root[KUI_DEST_ROOT_CAP]; unsigned offset;
    mutex_lock(&lock);
    strcpy(root, destination_pending); offset = destination_offset;
    mutex_unlock(&lock);
    struct kui_destination_page listing = {0};
    bool ok = false;
    kui_sd_set_params(0, true);
    if(kui_sd_connect()) {
        FATFS fs;
        if(kui_mount(&fs, kui_log)) {
            if(!kui_cancelled()) {
                if(save) {
                    char path[KUI_DEST_PATH_CAP]; FILINFO info;
                    snprintf(path, sizeof(path), "0:%s", root);
                    FRESULT r = f_stat(path, &info);
                    bool usable = !strcmp(root, "/") || r == FR_NO_FILE || r == FR_NO_PATH ||
                        (r == FR_OK && (info.fattrib & AM_DIR));
                    if(usable) ok = kui_destination_save(root, kui_log);
                    else kui_log("Destination is not an accessible folder: FatFs=%u", (unsigned)r);
                } else ok = kui_destination_list(root, offset, &listing, kui_log);
            }
            f_mount(NULL, "0:", 0);
        }
        kui_sd_disconnect();
    }
    mutex_lock(&lock);
    if(ok && save) { strcpy(destination_current, root); destination_ready = true; }
    destination_listing = listing;
    destination_result = ok ? (save ? 1 : 2) : 3;
    snprintf(destination_note, sizeof(destination_note), "%s", ok ? "" :
        (save ? "Save not confirmed. Retry or reload Settings to check." :
                "Cannot list folder. B: parent; X: type; Y: use a new folder."));
    ++destination_generation;
    mutex_unlock(&lock);
}
#endif

static void *worker(void *unused) {
    (void)unused;
    for(;;) {
        mutex_lock(&lock);
        unsigned action = pending;
        pending = 0;
        mutex_unlock(&lock);
        if(!action) {
#ifdef KUI_SD_RUNTIME
            /* Only this worker touches the drive. A queued foreground action
             * always wins over the optional insertion title read. */
            mutex_lock(&lock);
            bool idle=!busy && !video_preview;
            bool reset=drive_reset_required;
            mutex_unlock(&lock);
            if(idle && !reset) {
                const struct kui_disc_identity_ops *ops=kui_disc_identity_console_ops();
                enum kui_disc_identity_state before=disc_identity.state;
                int before_result=disc_identity.last_status_result;
                bool identify=kui_disc_identity_poll(&disc_identity,ops,timer_ms_gettime64(),true);
                if(before!=disc_identity.state || before_result!=disc_identity.last_status_result)
                    kui_log("Disc insertion: %s; BIOS result=%d status=%d type=%d",
                        kui_disc_identity_text(disc_identity.state),disc_identity.last_status_result,
                        disc_identity.last_status,disc_identity.last_disc_type);
                mutex_lock(&lock);
                disc_snapshot=disc_identity;
                if(identify && !pending && !busy && !video_preview) {
                    busy=true;cancel_requested=false;ui_hz_busy=2;action=12;
                }
                mutex_unlock(&lock);
            }
            if(!action) {
                if(idle) {kui_music_resume();kui_music_service();publish_music();}
                else {kui_music_pause();publish_music();}
                thd_sleep(16);continue;
            }
#else
            thd_sleep(16);continue;
#endif
        }
#ifdef KUI_SD_RUNTIME
        kui_music_pause();publish_music();
#endif
        if(kui_cancelled()
#ifdef KUI_SD_RUNTIME
           && action!=27
#endif
        ) {
            kui_log("Operation stopped before starting.");
#ifdef KUI_SD_RUNTIME
            if(action==12) {
                kui_disc_identity_read(&disc_identity,kui_disc_identity_console_ops());
                mutex_lock(&lock);
                if(!drive_reset_required) disc_snapshot=disc_identity;
                mutex_unlock(&lock);
            }
            mutex_lock(&lock);
            if(is_capture_action(action)) capture_outcome = KUI_SHELL_OUTCOME_STOPPED;
            if(action == 10 || action == 11) {
                destination_result = 3;
                snprintf(destination_note, sizeof(destination_note), "Folder operation stopped.");
                ++destination_generation;
            }
            if(action==13 || action==14) {
                system_ok=false;++system_generation;
                snprintf(system_note,sizeof(system_note),"System settings operation stopped.");
            }
            if((action>=16 && action<=20) || action==23 || action==24) {
                struct kui_app_status stopped={.stopped=true};
                snprintf(stopped.message,sizeof(stopped.message),"Operation stopped before starting.");
                if(action==19) memory_test_status=stopped;
                else if(action==20) network_test_status=stopped;
                else if(action==23 || action==24) player_status=stopped;
                else vmu_snapshot.status=stopped;
            }
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
            if(action == 8 || action == 9) {
                settings_operation(action == 9);
                if(!system_loaded) system_operation(false);
            }
            if(action==12) {
                kui_disc_identity_read(&disc_identity,kui_disc_identity_console_ops());
                mutex_lock(&lock);
                if(!drive_reset_required) disc_snapshot=disc_identity;
                mutex_unlock(&lock);
                if(disc_identity.state==KUI_DISC_IDENTITY_READY)
                    kui_log("Inserted disc: %s",disc_identity.title);
            }
            if(action==13 || action==14) system_operation(action==14);
            if(action==15) configure_music(true);
            if(action==21) {
                static const unsigned levels[]={15,30,50,75,100};
                mutex_lock(&lock);
                struct kui_system_settings next=system_current;
                unsigned level=next.music_enabled?next.music_volume:0;
                unsigned wanted=0;
                for(unsigned i=0;i<sizeof(levels)/sizeof(levels[0]);i++)
                    if(levels[i]>level) {wanted=levels[i];break;}
                next.music_enabled=wanted!=0;
                if(wanted) next.music_volume=wanted;
                system_pending=next;
                mutex_unlock(&lock);
                system_operation(true);
            }
            if(action==23) {
                kui_sd_set_params(0,true);
                struct kui_music_player_page page;
                bool ok=kui_music_player_list(music_path_pending,music_offset_pending,&page,kui_log,kui_cancelled);
                mutex_lock(&lock);
                music_listing=page;++music_listing_generation;
                snprintf(player_status.message,sizeof(player_status.message),"%s",page.message);
                player_status.complete=ok;player_status.passed=ok;
                mutex_unlock(&lock);
            }
            if(action==24) {
                kui_sd_set_params(0,true);
                struct kui_app_status result;
                active_app=24;
                kui_music_player_run(music_path_pending,system_current.music_volume,&result,
                    kui_log,kui_cancelled,app_progress);
                mutex_lock(&lock);player_status=result;mutex_unlock(&lock);
            }
            if(action==25) {
                /* All app I/O is finished; keep the worker parked until main's
                 * normal KOS shutdown tears down the remaining services. */
                kui_music_shutdown();publish_music();
                mutex_lock(&lock);boot_ready=true;mutex_unlock(&lock);
                for(;;) thd_sleep(1000);
            }
            if(action==27) {
                kui_music_play_boot_chime(kui_cancelled);
                mutex_lock(&lock);cancel_requested=false;mutex_unlock(&lock);
                settings_operation(false);
                system_operation(false);
                mutex_lock(&lock);splash_active=false;mutex_unlock(&lock);
            }
            if(action>=16 && action<=20) {
                active_app=action;
                if(action<=18) {
                    struct kui_vmu_view result;
                    kui_vmu_app_run(action-16,vmu_slot_pending,vmu_page_pending,vmu_selected_pending,
                        &result,kui_log,kui_cancelled,app_progress);
                    mutex_lock(&lock);vmu_snapshot=result;++vmu_generation;mutex_unlock(&lock);
                } else {
                    struct kui_app_status result;
                    if(action==19) kui_memory_app_run(&result,kui_log,kui_cancelled,app_progress);
                    else kui_network_app_run(&result,kui_log,kui_cancelled);
                    mutex_lock(&lock);
                    if(action==19) memory_test_status=result;else network_test_status=result;
                    mutex_unlock(&lock);
                }
            }
            if(action == 10 || action == 11) destination_operation(action == 11);
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
            if(is_capture_action(action)) {
                kui_memory_log("capture/verify start");
                enum kui_capture_result result=action==22?
                    kui_capture_resume_quick(KUI_BUILD_ID,capture_destination):
                    kui_capture_start((enum kui_capture_mode)(action-4),KUI_BUILD_ID,capture_destination);
                mutex_lock(&lock);
                capture_summary = *kui_capture_last_stats();
                capture_outcome = result == KUI_CAPTURE_COMPLETE ? KUI_SHELL_OUTCOME_COMPLETE :
                    result == KUI_CAPTURE_STOPPED ? KUI_SHELL_OUTCOME_STOPPED : KUI_SHELL_OUTCOME_FAILED;
                const char *stage=action==6?"Verification":(action==5 || action==22)?"Resume":"Capture";
                snprintf(capture_message,sizeof(capture_message),"%s",
                    result==KUI_CAPTURE_FAILED?(drive_reset_required?
                        "Drive reset required. Reboot, then Resume the partial dump.":
                        capture_status.phase==KUI_VERIFYING?"Saved-file verification failed. See Diagnostics.":
                        capture_status.phase==KUI_PREFIX_CHECK?"Saved-prefix check failed. See Diagnostics.":
                        "Disc operation failed before verification completed."):
                    result==KUI_CAPTURE_STOPPED?(capture_summary.job_dir[0]?
                        "Stopped safely; the committed partial dump is kept.":
                        "Stopped before a dump job was created."):"");
                mutex_unlock(&lock);
                kui_log("%s operation ended during %s",stage,
                    capture_status.phase==KUI_VERIFYING?"saved-file verification":
                    capture_status.phase==KUI_CAPTURING?"disc capture":
                    capture_status.phase==KUI_PREFIX_CHECK?"resume checks":
                    capture_status.phase==KUI_FINISHED?"completion":"disc identification");
                kui_ui_set_hz(KUI_OPT_UI_FULL);   /* the cap is for the capture, not the report save */
                kui_memory_log("capture/verify end");
                const char *outcome=result==KUI_CAPTURE_COMPLETE?"complete":result==KUI_CAPTURE_STOPPED?"stopped":"failed";
                kui_log("Capture result: %s",outcome);
                kui_log("Saving diagnostic report automatically; B cancels log save.");
                save_report(action==4?"auto new capture":action==5?"auto resume":action==22?"auto quick resume":"auto verify",outcome,true);
            }
#endif
        }
#ifdef KUI_SD_RUNTIME
        if(action!=12 && action!=27) kui_log("Operation ended. Diagnostics page: Y saves the log to SD.");
        if(action==1 || (action>=4 && action<=7) || action==22) kui_disc_identity_invalidate(&disc_identity);
#else
        kui_log("Operation ended. Y saves the current log to SD.");
#endif
        mutex_lock(&lock);
#ifdef KUI_SD_RUNTIME
        player_active=false;
#endif
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
static void draw_shell(void) {
    mutex_lock(&lock);bool startup=splash_active;mutex_unlock(&lock);
    if(startup) {
        kui_splash_draw(vram_s);vid_waitvbl();vid_flip(-1);return;
    }
    char visible[KUI_SHELL_LOG_ROWS][LINE_BYTES] = {{0}};
    const char *log_rows[KUI_SHELL_LOG_ROWS];
    char path[KUI_DEST_JOB_CAP], notice[128], title[129], gdi[KUI_DEST_TITLE_CAP+5u];
    char inserted[129],music_title[40],music_notice[128],message[128];
    struct kui_app_status app_status;
    struct kui_shell_view view = {.build = KUI_BUILD_ID, .job_dir = path,
        .disc_title = title, .gdi_name = gdi, .settings_notice = notice, .message = message, .log_lines = log_rows,
        .inserted_title=inserted,.music_title=music_title,.music_notice=music_notice,.app_status=&app_status};
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
    snprintf(title, sizeof(title), "%s", capture_summary.disc_title);
    snprintf(gdi, sizeof(gdi), "%s", capture_summary.gdi_name);
    view.reference_checked = capture_summary.reference_checked;
    view.reference = capture_summary.reference;
    snprintf(notice,sizeof(notice),"%s",shell.page==KUI_SHELL_SETTINGS?system_note:settings_note);
    snprintf(message,sizeof(message),"%s",capture_message[0]?capture_message:shell.destination_notice);
    snprintf(inserted,sizeof(inserted),"%s",disc_snapshot.state==KUI_DISC_IDENTITY_READY?
        disc_snapshot.title:kui_disc_identity_text(disc_snapshot.state));
    snprintf(music_title,sizeof(music_title),"%s",music_snapshot.title);
    snprintf(music_notice,sizeof(music_notice),"%s",music_snapshot.message);
    view.music_enabled=music_snapshot.enabled;
    view.music_playing=music_snapshot.playing;
    view.music_paused=music_snapshot.paused;
    view.music_volume=music_snapshot.volume;
    if(player_active) {
        const char *name=strrchr(music_path_pending,'/');
        snprintf(music_title,sizeof(music_title),"%s",name?name+1:music_path_pending);
        view.music_enabled=true;view.music_playing=true;view.music_paused=false;
        view.music_volume=system_current.music_volume;
    }
    view.drive_reset_required=drive_reset_required;
    view.video_trial=video_preview;
    uint64_t now=timer_ms_gettime64();
    view.video_seconds=video_preview && video_deadline>now?(unsigned)((video_deadline-now+999)/1000):0;
    view.phase_elapsed_ms=now>=phase_started_ms?now-phase_started_ms:0;
    view.progress_age_ms=now>=progress_updated_ms?now-progress_updated_ms:0;
    app_status=shell.page==KUI_SHELL_MEMORY?memory_test_status:
        shell.page==KUI_SHELL_NETWORK?network_test_status:
        shell.page==KUI_SHELL_MUSIC?player_status:vmu_snapshot.status;
    view.phase = capture_status.phase; view.track = capture_status.track; view.tracks = capture_status.tracks;
    view.rate_kib = rate_kib; view.retries = capture_status.retries;
    view.done = capture_status.done; view.total = capture_status.total;
    view.committed = capture_status.committed; view.elapsed_ms = capture_status.elapsed_ms;
    mutex_unlock(&lock);
    view.memory_valid = memory_valid; view.memory_used = memory_status.used;
    view.memory_physical = memory_status.physical; view.memory_peak = memory_status.sampled_peak;
    /* The pinned KOS RGB565 clear uses SH-4 store queues instead of a pixel
     * loop. DM_MULTIBUFFER/vid_flip leave vram_s on the next, offscreen buffer;
     * publish only after the complete frame has been drawn. RGB565 = 0x0864,
     * matching the portable renderer's background. */
    vid_clear(8, 15, 35);
    kui_shell_draw_content(vram_s, &shell, &view, NULL, NULL);
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
        case KUI_SHELL_DEST_LIST: return 10;
        case KUI_SHELL_DEST_SAVE: return 11;
        case KUI_SHELL_LOAD_SYSTEM: return 13;
        case KUI_SHELL_SAVE_SYSTEM: return 14;
        case KUI_SHELL_MUSIC_NEXT: return 15;
        case KUI_SHELL_VMU_LIST: return 16;
        case KUI_SHELL_VMU_BACKUP: return 17;
        case KUI_SHELL_VMU_BACKUP_ALL: return 18;
        case KUI_SHELL_MEMORY_TEST: return 19;
        case KUI_SHELL_NETWORK_TEST: return 20;
        case KUI_SHELL_MUSIC_CYCLE: return 21;
        case KUI_SHELL_RESUME_QUICK: return 22;
        case KUI_SHELL_MUSIC_LIST: return 23;
        case KUI_SHELL_MUSIC_PLAY: return 24;
        case KUI_SHELL_GD_BOOT: return 25;
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
    kui_system_settings_default(&system_current);system_pending=system_current;
    kui_music_init(kui_log);kui_disc_identity_init(&disc_identity);disc_snapshot=disc_identity;
    safe_video_boot=(controller_buttons()&CONT_Y)!=0;
    kui_settings_default(&settings_current);
    settings_pending = settings_current;
    kui_shell_init(&shell, &settings_current);
    kui_shell_set_system_preferences(&shell,&system_current);
    kui_destination_default(destination_current);
    snprintf(settings_note,sizeof(settings_note),"Loading preferences from SD...");
    pending = 27; busy = true; splash_active=true;
    kui_log("K-UI launcher: Disc Ripper, VMU, Memory, Network, Settings, Diagnostics, GD Play and Music.");
    kui_log("B skips the original startup splash/chime. Home Y cycles menu music volume/off.");
    kui_log("Ripper: A new dump (confirm), X resume latest matching disc, Y verify.");
    kui_log("B returns home while idle; during work it stops and checkpoints.");
    kui_log("New dumps use game-named folders in /Games; duplicates get a number.");
    kui_log("Ripper: R trigger browses destination; Start opens Advanced and ripper settings.");
    kui_log("Inserted GD-ROM titles appear while idle; no idle polling during operations.");
    kui_log("System Settings: video, memory display and menu music. Hold Y on runtime start for safe video.");
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
    unsigned seen_settings_generation = 0, seen_destination_generation = 0;
    unsigned seen_system_generation=0,seen_vmu_generation=0,seen_music_listing=0;
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
        if(seen_destination_generation != destination_generation) {
            if(destination_result == 1) kui_shell_set_destination(&shell, destination_current);
            else if(destination_result == 2) kui_shell_set_listing(&shell, &destination_listing);
            else kui_shell_destination_error(&shell, destination_note);
            seen_destination_generation = destination_generation;
        }
        if(seen_system_generation!=system_generation) {
            if(system_ok) {
                kui_shell_set_system_preferences(&shell,&system_current);
                if(applied_video!=system_current.video_mode) apply_video(system_current.video_mode);
            } else if(video_awaiting_save) {
                safe_video_boot=video_prior_safe;apply_video(video_prior_mode);
            }
            video_awaiting_save=false;
            seen_system_generation=system_generation;
        }
        if(seen_vmu_generation!=vmu_generation) {
            kui_shell_set_vmu(&shell,&vmu_snapshot);seen_vmu_generation=vmu_generation;
        }
        if(seen_music_listing!=music_listing_generation) {
            kui_shell_set_music_listing(&shell,&music_listing);
            seen_music_listing=music_listing_generation;
        }
        if(video_preview && input_at>=video_deadline) {
            video_preview=false;shell.video_trial=false;
            safe_video_boot=video_prior_safe;apply_video(video_prior_mode);
            shell.system_draft.video_mode=system_current.video_mode;
            snprintf(system_note,sizeof(system_note),"Video reverted after 10 seconds.");
        }
        enum kui_shell_action requested=KUI_SHELL_NONE;
        if(splash_active) {if(pressed&CONT_B) cancel_requested=true;}
        else requested=kui_shell_input(&shell,shell_buttons(pressed),busy);
        if(requested==KUI_SHELL_PREVIEW_VIDEO && !busy) {
            video_prior_safe=safe_video_boot;video_prior_mode=applied_video;
            safe_video_boot=false;
            apply_video(shell.system_draft.video_mode);
            video_preview=true;shell.video_trial=true;video_deadline=input_at+10000;
        }
        if(requested==KUI_SHELL_CANCEL_VIDEO) {
            video_preview=false;shell.video_trial=false;
            safe_video_boot=video_prior_safe;apply_video(video_prior_mode);
            shell.system_draft.video_mode=system_current.video_mode;
            snprintf(system_note,sizeof(system_note),"Video reverted; other edits are not saved.");
        }
        if(requested==KUI_SHELL_CONFIRM_VIDEO && video_preview) {
            video_preview=false;shell.video_trial=false;video_awaiting_save=true;
            requested=KUI_SHELL_SAVE_SYSTEM;
        }
        if(requested == KUI_SHELL_STOP) cancel_requested = true;
        unsigned action = worker_action(requested);
        if(drive_reset_required && (action==1 || (action>=4 && action<=7) || action==22)) {
            snprintf(capture_message,sizeof(capture_message),
                "Drive reset required. Reboot, then Resume the partial dump.");
            action=0;
        }
        if(is_capture_action(action) && !destination_ready) {
            capture_summary = (struct kui_capture_stats){0};
            capture_status = (struct kui_capture_progress){0};
            capture_outcome = KUI_SHELL_OUTCOME_FAILED;
            kui_shell_destination_error(&shell, "Destination unavailable. R: browse and save a folder.");
            action = 0;
        }
        if(action && !busy) {
            if(action == 10 || action == 11) {
                strcpy(destination_pending, shell.browse_path);
                destination_offset = shell.browser_page * KUI_DEST_PAGE_SIZE;
            }
            if(is_capture_action(action)) strcpy(capture_destination, shell.destination);
            if(action == 9) settings_pending = shell.draft;
            if(action==14) system_pending=shell.system_draft;
            if(action==13 || action==14) snprintf(system_note,sizeof(system_note),"%s",
                action==14?"Saving system settings...":"Loading system settings...");
            if(action>=16 && action<=18) {
                vmu_slot_pending=shell.vmu_slot;vmu_page_pending=shell.vmu_page;
                vmu_selected_pending=shell.vmu_selected;
                vmu_snapshot.status=(struct kui_app_status){0};
            }
            if(action==23 || action==24) {
                snprintf(music_path_pending,sizeof(music_path_pending),"%s",
                    action==23?shell.music_path:shell.music_selected_path);
                music_offset_pending=shell.music_page*KUI_MUSIC_PLAYER_ROWS;
                player_active=action==24;
                player_status=(struct kui_app_status){0};
            }
            if(action==19) memory_test_status=(struct kui_app_status){0};
            if(action==20) network_test_status=(struct kui_app_status){0};
            if(action == 8 || action == 9)
                snprintf(settings_note, sizeof(settings_note), "%s", action == 8 ?
                    "Loading preferences from SD..." : "Saving preferences to SD...");
            pending = action; busy = true; cancel_requested = false; shell.scroll = 0;
            ui_hz_busy = 2;
            if(is_capture_action(action)) {
                capture_status = (struct kui_capture_progress){0};
                capture_summary = (struct kui_capture_stats){0};
                capture_outcome = KUI_SHELL_OUTCOME_NONE;
                rate_at = rate_bytes = 0; rate_kib = 0;
                capture_message[0]=0;phase_started_ms=progress_updated_ms=input_at;
            }
        }
        bool is_busy = busy;
        mutex_unlock(&lock);
        mutex_lock(&lock);bool exiting=boot_ready;mutex_unlock(&lock);
        if(exiting) kui_gd_play_boot();
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
