/* SPDX-License-Identifier: GPL-3.0-only */
#include "platform.h"
#include "kui/ui_rate.h"
#include "kui/report.h"
#include "kui/clock_platform.h"
#ifdef KUI_SD_RUNTIME
#include "kui/shell.h"
#include "kui/settings.h"
#include "kui/system_settings.h"
#include "kui/disc_identity.h"
#include "kui/music.h"
#include "kui/apps.h"
#include "kui/music_player.h"
#include "kui/games.h"
#include "kui/games_probe.h"
#include "kui/games_image_probe.h"
#include "kui/image_loader_layout.h"
#include <arch/exec.h>
#include "kui/splash.h"
#include "kui/gd_play.h"
#include "kui/recovery_scan.h"
#include "kui/capture_display.h"
#include "kui/viewport.h"
#include "kui/network_probe.h"
#include "kui/salvage.h"
#include "kui/maintenance.h"
#include "kui/menu_sound.h"
#include "kui/cd_audio.h"
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
static struct kui_capture_display capture_display;
static bool observing_capture;
static enum kui_shell_outcome capture_outcome;
static char capture_message[128];
static bool drive_reset_required, dma_degraded;
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
static struct kui_cd_audio_status cd_audio_snapshot;
static unsigned cd_audio_generation,cd_track_pending;
static bool cd_drive_owned;
static struct kui_app_status memory_test_status, network_test_status,maintenance_status;
static struct kui_vmu_view vmu_snapshot;
static unsigned vmu_generation, vmu_slot_pending, vmu_page_pending, vmu_selected_pending;
static struct kui_vmu_backup_view vmu_backups;
static unsigned vmu_backups_generation,vmu_backup_page_pending,vmu_restore_generation;
static unsigned vmu_delete_generation,vmu_copy_generation,vmu_copy_slot_pending;
static char vmu_restore_path_pending[KUI_VMU_BACKUP_PATH_CAP];
static struct kui_datetime clock_pending,clock_snapshot;
static bool clock_valid;
static unsigned clock_generation;
static char clock_note[128];
static struct kui_app_status scan_status,salvage_status;
static char salvage_path_pending[KUI_DEST_JOB_CAP];
static unsigned salvage_passes_pending;
static bool salvage_zero_pending;
static char scan_path_pending[KUI_DEST_JOB_CAP];
static unsigned active_app;
static bool splash_active,boot_ready,startup_skip,startup_finished;
static bool probe_launch_ready,probe_launch_failed;
static struct kui_runtime_image probe_image;
static struct kui_app_status probe_status;
static uint64_t splash_deadline;
static int music_requested=-1;
static unsigned music_request_generation,music_cache_attempted;
static struct kui_music_player_page music_listing;
static struct kui_app_status player_status;
static unsigned music_listing_generation,music_offset_pending;
static char music_path_pending[256];
static struct kui_games_page games_listing;
static struct kui_games_detail games_detail;
static unsigned games_listing_generation,games_detail_generation;
static unsigned games_offset_pending,games_result_offset;
static char games_path_pending[KUI_GAMES_FILE_CAP];
static bool is_capture_action(unsigned action) {
    return (action>=4 && action<=6) || action==22;
}

#define KUI_ROLE "SD runtime"
#else
#define KUI_ROLE "CD bootstrap"
#endif

#define LOG_LINES 1500
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
    kui_capture_display_progress(&capture_display,p);
    capture_status=*p;progress_updated_ms=timer_ms_gettime64();mutex_unlock(&lock);
}
#endif

void kui_log(const char *format, ...) {
    char text[256];
    va_list args;
    va_start(args, format); vsnprintf(text, sizeof(text), format, args); va_end(args);
    mutex_lock(&lock);
#ifdef KUI_SD_RUNTIME
    if(observing_capture) kui_capture_display_log(&capture_display,text);
    if(strstr(text,"DMA stays off until reboot")) dma_degraded=true;
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
    kui_music_log_stats("save log");
#endif
    char path[96]={0};enum kui_report_result result=KUI_REPORT_FAILED;
    if(kui_sd_connect()) {
        mutex_lock(&lock);
        size_t used=(size_t)snprintf(report,sizeof(report),
            "K-UI " KUI_ROLE " %s\nLog truncated: %s\nReport trigger: %s\nOperation result: %s\n",
            KUI_BUILD_ID,log_truncated?"YES":"no",trigger,outcome);
#ifdef KUI_SD_RUNTIME
        used+=(size_t)snprintf(report+used,sizeof(report)-used,
            "DMA failure latch: %s\n",dma_degraded?
            "SET; PIO remains usable; reboot to restore DMA":"not observed this boot");
#endif
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
    struct kui_music_status status;
    kui_music_status_copy(&status);
    mutex_lock(&lock); music_snapshot=status; mutex_unlock(&lock);
}
static void configure_music(void) {
    struct kui_system_settings value;
    mutex_lock(&lock);value=system_current;mutex_unlock(&lock);
    struct kui_music_status status;
    kui_music_status_copy(&status);
    kui_music_set_config(value.music_enabled,value.music_volume);
    if(value.music_enabled && !status.loaded && !kui_cd_audio_owns_drive()) {
        mutex_lock(&lock);
        if(music_requested<0) {
            music_requested=(int)(timer_ms_gettime64()%KUI_MUSIC_TRACKS);
            ++music_request_generation;
        }
        mutex_unlock(&lock);
    }
    /* Loading an unchanged Settings page must respect Music's explicit Stop.
     * A real off-to-on change resumes the cached selection without card I/O. */
    if(value.music_enabled && !status.enabled && status.loaded && !kui_cd_audio_owns_drive()) kui_music_resume();
    kui_cd_audio_set_volume(value.music_enabled?value.music_volume:0);
    publish_music();
}
/* Background cache reads still belong to this I/O worker. Yield the card as
 * soon as a foreground action arrives; audio itself only reads cached RAM. */
static bool music_preload_cancelled(void) {
    mutex_lock(&lock);
    bool stop=busy || pending || video_preview || cancel_requested;
    mutex_unlock(&lock);
    return stop;
}
static void music_idle_work(void) {
    if(kui_cd_audio_owns_drive() || music_preload_cancelled()) return;
    struct kui_music_status status;
    kui_music_status_copy(&status);
    mutex_lock(&lock);
    int wanted=music_requested;
    unsigned generation=music_request_generation;
    mutex_unlock(&lock);
    if(wanted>=0) {
        bool ok=kui_music_cache_menu((unsigned)wanted,music_preload_cancelled);
        if(!music_preload_cancelled()) {
            mutex_lock(&lock);
            bool current=generation==music_request_generation;
            /* The audio lock never logs or takes this publication lock.
             * Keep selection atomic with its generation check so an older
             * preload cannot overwrite a newer trigger selection. */
            if(current) {
                if(ok) kui_music_select_cached((unsigned)wanted);
                music_requested=-1;
            }
            mutex_unlock(&lock);
        }
    } else if(status.enabled) {
        for(unsigned index=0;index<KUI_MUSIC_TRACKS;index++) {
            unsigned bit=1u<<index;
            if((status.cached_mask|music_cache_attempted)&bit) continue;
            bool ok=kui_music_cache_menu(index,music_preload_cancelled);
            if(ok || !music_preload_cancelled()) music_cache_attempted|=bit;
            break;
        }
    }
    publish_music();
}
/* Trigger controls never touch the card. An uncached selection waits for idle
 * storage ownership; newer trigger presses replace that pending request. */
static void music_step(int direction) {
    mutex_lock(&lock);
    unsigned index=music_requested>=0?(unsigned)music_requested:music_snapshot.current_index;
    unsigned wanted=kui_music_next_index(index,direction);
    music_requested=(int)wanted;
    unsigned generation=++music_request_generation;
    mutex_unlock(&lock);
    if(kui_music_select_cached(wanted)) {
        mutex_lock(&lock);
        if(generation==music_request_generation) music_requested=-1;
        mutex_unlock(&lock);
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
    if(ok) configure_music();
    kui_log("System settings %s: %s",save?"save":"load",ok?"complete":"failed or stopped");
}
static void app_progress(const struct kui_app_status *status) {
    mutex_lock(&lock);
    if(active_app==19) memory_test_status=*status;
    else if(active_app==20 || active_app==45) network_test_status=*status;
    else if(active_app>=41 && active_app<=43) maintenance_status=*status;
    else if(active_app==24) player_status=*status;
    else if(active_app==31) vmu_backups.status=*status;
    else vmu_snapshot.status=*status;
    mutex_unlock(&lock);
}
static bool startup_sound_cancelled(void) {
    mutex_lock(&lock);
    bool stop=startup_skip || cancel_requested || timer_ms_gettime64()>=splash_deadline;
    mutex_unlock(&lock);
    return stop;
}
static void clock_operation(bool write) {
    bool written=!write || kui_clock_set_local(&clock_pending);
    struct kui_datetime now={0};
    bool valid=kui_clock_now(&now);
    mutex_lock(&lock);
    clock_valid=valid;clock_snapshot=now;++clock_generation;
    snprintf(clock_note,sizeof(clock_note),"%s",!written?
        "Clock write not confirmed. Check the displayed time before retrying.":
        !valid?"Clock unavailable. Set a valid local date and time.":
        write?"Console clock updated and read back.":"Local console time; no timezone conversion.");
    mutex_unlock(&lock);
    if(write) kui_log("Clock edit: %s",written?"RTC and system time confirmed":"not confirmed");
    if(valid) kui_log("Clock now: %04u-%02u-%02u %02u:%02u:%02u local",
        (unsigned)now.year,(unsigned)now.month,(unsigned)now.day,
        (unsigned)now.hour,(unsigned)now.minute,(unsigned)now.second);
}
static bool scan_cancel(void *ctx) { (void)ctx;return kui_cancelled(); }
static uint64_t scan_now(void *ctx) { (void)ctx;return timer_ms_gettime64(); }
static void scan_progress(void *ctx,const struct kui_scan_status *status) {
    (void)ctx;
    struct kui_app_status view={.done=status->done,.total=status->total,
        .errors=status->bad_sectors+status->unsupported_sectors+status->crc_mismatches+status->sha_mismatches,
        .complete=status->complete,.passed=status->result==KUI_SCAN_CLEAN && status->complete,
        .stopped=status->result==KUI_SCAN_STOPPED,.line_count=5};
    snprintf(view.message,sizeof(view.message),"%s",status->message);
    snprintf(view.lines[0],KUI_APP_LINE_CAP,"Track %u / %u",status->track,status->tracks);
    snprintf(view.lines[1],KUI_APP_LINE_CAP,"Data sectors %lu  Audio sectors %lu",
        (unsigned long)status->data_sectors,(unsigned long)status->audio_sectors);
    snprintf(view.lines[2],KUI_APP_LINE_CAP,"Bad sectors %lu  Unsupported %lu",
        (unsigned long)status->bad_sectors,(unsigned long)status->unsupported_sectors);
    snprintf(view.lines[3],KUI_APP_LINE_CAP,"CRC mismatches %lu  SHA mismatches %lu",
        (unsigned long)status->crc_mismatches,(unsigned long)status->sha_mismatches);
    snprintf(view.lines[4],KUI_APP_LINE_CAP,"%s",status->reference_hashes?
        "Recorded hashes checked; track files are read-only.":
        "Structural scan only; no expected hashes/audio verification.");
    if(status->catalogue_checked) {
        view.line_count=7;
        snprintf(view.lines[4],KUI_APP_LINE_CAP,"%.16s: %.59s",
            status->catalogue.catalog,kui_known_text(status->catalogue.result));
        snprintf(view.lines[5],KUI_APP_LINE_CAP,"%.79s",status->catalogue.name[0]?
            status->catalogue.name:"No matching reference title identified.");
        snprintf(view.lines[6],KUI_APP_LINE_CAP,"%s",status->reference_hashes?
            "Manifest hashes checked too; original files are read-only.":
            "No manifest; only FULL TRACK MATCH checks all audio too.");
    }
    mutex_lock(&lock);scan_status=view;mutex_unlock(&lock);
}
static void scan_operation(void) {
    struct kui_scan_status result={.result=KUI_SCAN_FAILED};
    snprintf(result.message,sizeof(result.message),"Could not connect SD for Advanced CRC scan.");
    const struct kui_scan_ops ops={.cancelled=scan_cancel,.now_ms=scan_now,
        .progress=scan_progress,.log=kui_log};
    kui_sd_set_params(0,true);
    if(kui_sd_connect()) {
        kui_recovery_scan(scan_path_pending,&ops,&result);
        kui_sd_disconnect();
    }
    scan_progress(NULL,&result);
    mutex_lock(&lock);
    scan_status.complete=true;
    if(result.result==KUI_SCAN_FAILED && !scan_status.errors) scan_status.errors=1;
    mutex_unlock(&lock);
    kui_ui_set_hz(KUI_OPT_UI_FULL);
    const char *outcome=result.result==KUI_SCAN_CLEAN?"clean":
        result.result==KUI_SCAN_ISSUES?"issues found":result.result==KUI_SCAN_STRUCTURAL?
        "structural checks complete; expected hashes unavailable":result.result==KUI_SCAN_STOPPED?"stopped":"failed";
    kui_log("Advanced CRC scan: %s",outcome);
    save_report("auto Advanced CRC",outcome,true);
}
static void salvage_progress(void *ctx,const struct kui_salvage_status *status) {
    (void)ctx;
    struct kui_app_status view={.done=status->done,.total=status->total,
        .complete=status->complete,.passed=status->result==KUI_SALVAGE_RESOLVED && status->complete,
        .stopped=status->result==KUI_SALVAGE_STOPPED,.line_count=6};
    snprintf(view.message,sizeof(view.message),"%s",status->message);
    snprintf(view.lines[0],KUI_APP_LINE_CAP,"Track %u/%u  FAD %lu  attempts %lu",status->track,status->tracks,
        (unsigned long)status->fad,(unsigned long)status->attempts);
    snprintf(view.lines[1],KUI_APP_LINE_CAP,"Targets %lu  recovered %lu  remaining %lu",
        (unsigned long)status->targets,(unsigned long)status->recovered,(unsigned long)status->remaining);
    snprintf(view.lines[2],KUI_APP_LINE_CAP,"Recovery pass %u/%u  First pass: %s",status->pass,status->pass_limit,
        status->first_pass_complete?"complete":"incomplete");
    snprintf(view.lines[3],KUI_APP_LINE_CAP,"%.76s",status->job);
    snprintf(view.lines[4],KUI_APP_LINE_CAP,"Unresolved zeros are NOT a verified game dump.");
    snprintf(view.lines[5],KUI_APP_LINE_CAP,"Separate recovery job; normal captures unchanged.");
    mutex_lock(&lock);salvage_status=view;mutex_unlock(&lock);
}
static enum kui_read_result salvage_read(void *ctx,uint32_t fad,unsigned sectors,uint8_t *out) {
    enum kui_read_result result=kui_disc_read_raw(ctx,fad,sectors,out);
    mutex_lock(&lock);bool reset=drive_reset_required;mutex_unlock(&lock);
    /* The existing reader uses FATAL for a clean user cancellation too. The
     * salvage engine sees B before another read/write; genuine poisoned drive
     * failures retain priority and never become placeholders. */
    if(result==KUI_READ_FATAL && kui_cancelled() && !reset) return KUI_READ_RETRY;
    return result;
}
static void salvage_operation(unsigned action) {
    struct kui_salvage_status result={.result=KUI_SALVAGE_FAILED};
    snprintf(result.message,sizeof(result.message),"Could not prepare disc and SD for salvage.");
    struct kui_toc sessions[2];struct kui_capture_plan plan;
    const struct kui_salvage_options options={.zero_fill=salvage_zero_pending,
        .passes=salvage_passes_pending,.job=salvage_path_pending,.progress=salvage_progress};
    const struct kui_capture_ops ops={.read=salvage_read,.cancelled=scan_cancel,
        .now_ms=scan_now,.log=kui_log,.build=KUI_BUILD_ID};
    kui_sd_set_params(0,true);
    if(kui_disc_prepare(sessions) && kui_plan_tracks(sessions,&plan) && !kui_cancelled() && kui_sd_connect()) {
        kui_disc_timing_phase(NULL,true);
        if(action==46 || kui_salvage_latest(&plan,&ops,salvage_path_pending))
            kui_salvage_run(&plan,&ops,&options,(enum kui_salvage_action)(action-46),&result);
        else snprintf(result.message,sizeof(result.message),"No matching salvage job found; see Diagnostics.");
        kui_disc_timing_phase(NULL,false);
        kui_sd_disconnect();
    }
    salvage_progress(NULL,&result);
    mutex_lock(&lock);
    salvage_status.complete=true;
    if(result.result==KUI_SALVAGE_FAILED) salvage_status.errors=1;
    mutex_unlock(&lock);
    kui_ui_set_hz(KUI_OPT_UI_FULL);
    const char *outcome=result.result==KUI_SALVAGE_RESOLVED?"all targets resolved; no catalogue claim":
        result.result==KUI_SALVAGE_UNRESOLVED?"unresolved holes remain":
        result.result==KUI_SALVAGE_STOPPED?"stopped":"failed";
    save_report("auto salvage",outcome,true);
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

#ifdef KUI_SD_RUNTIME
static void publish_cd_audio(void) {
    struct kui_cd_audio_status status;kui_cd_audio_status_copy(&status);
    bool owns=kui_cd_audio_owns_drive();
    mutex_lock(&lock);cd_audio_snapshot=status;cd_drive_owned=owns;++cd_audio_generation;mutex_unlock(&lock);
}
static bool needs_cd_handoff(unsigned action) {
    return action==1 || (action>=4 && action<=7) || action==12 || action==22 ||
        action==24 || action==25 || action==56 || action==57 || (action>=46 && action<=48);
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
            if(idle && kui_cd_audio_owns_drive()) {
                static uint64_t next_cd_poll;
                uint64_t now=timer_ms_gettime64();
                if(now>=next_cd_poll) {
                    kui_cd_audio_poll(NULL,kui_log);publish_cd_audio();next_cd_poll=now+1000;
                }
            }
            if(idle && !reset && !kui_cd_audio_owns_drive()) {
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
                if(idle) music_idle_work();
                thd_sleep(16);continue;
            }
#else
            thd_sleep(16);continue;
#endif
        }
#ifdef KUI_SD_RUNTIME
        if(action && needs_cd_handoff(action) && !kui_cd_audio_stop_for_io(kui_log)) {
            publish_cd_audio();
            mutex_lock(&lock);
            snprintf(capture_message,sizeof(capture_message),"Audio CD stop failed; no new optical operation started.");
            if(is_capture_action(action)) {capture_outcome=KUI_SHELL_OUTCOME_FAILED;observing_capture=false;}
            if(action==24) {player_status.errors=1;snprintf(player_status.message,sizeof(player_status.message),"Audio CD stop failed; SD playback refused.");}
            if(action>=46 && action<=48) {salvage_status.errors=1;snprintf(salvage_status.message,sizeof(salvage_status.message),"Audio CD stop failed; salvage refused.");}
            if(action==56 || action==57) probe_launch_failed=true;
            mutex_unlock(&lock);action=0;
        } else if(action && needs_cd_handoff(action)) publish_cd_audio();
#endif
        if(kui_cancelled()
#ifdef KUI_SD_RUNTIME
           && action!=27 && action!=53 && action!=44
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
            if(is_capture_action(action)) {capture_outcome = KUI_SHELL_OUTCOME_STOPPED;observing_capture=false;}
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
            if(action>=30 && action<=33) {
                struct kui_app_status stopped={.stopped=true};
                snprintf(stopped.message,sizeof(stopped.message),"Operation stopped before starting.");
                if(action==30) scan_status=stopped;
                else if(action==31) {vmu_backups.status=stopped;++vmu_backups_generation;}
                else {vmu_snapshot.status=stopped;vmu_snapshot.restore_ready=false;++vmu_restore_generation;}
            }
            if(action>=46 && action<=48) {
                salvage_status=(struct kui_app_status){.stopped=true};
                snprintf(salvage_status.message,sizeof(salvage_status.message),"Salvage stopped before starting.");
            }
            if(action>=49 && action<=52) {
                snprintf(cd_audio_snapshot.message,sizeof(cd_audio_snapshot.message),"Audio CD operation stopped before starting.");
                ++cd_audio_generation;
            }
            if(action==54) {
                memset(&games_listing,0,sizeof(games_listing));
                snprintf(games_listing.root,sizeof(games_listing.root),"%.*s",
                    (int)sizeof(games_listing.root)-1,games_path_pending);
                snprintf(games_listing.message,sizeof(games_listing.message),"Games browse stopped before starting");
                games_result_offset=games_offset_pending;++games_listing_generation;
            }
            if(action==55) {
                memset(&games_detail,0,sizeof(games_detail));games_detail.stopped=true;
                strcpy(games_detail.path,games_path_pending);
                snprintf(games_detail.message,sizeof(games_detail.message),"Games inspection stopped before starting");
                ++games_detail_generation;
            }
            if(action==56 || action==57) {
                probe_status=(struct kui_app_status){.stopped=true};
                snprintf(probe_status.message,sizeof(probe_status.message),"Probe stopped before starting.");
                probe_launch_failed=true;
            }
            if(action>=41 && action<=43) {
                maintenance_status=(struct kui_app_status){.stopped=true};
                snprintf(maintenance_status.message,sizeof(maintenance_status.message),"System operation stopped before starting.");
            }
            if(action==45) {
                network_test_status=(struct kui_app_status){.stopped=true};
                snprintf(network_test_status.message,sizeof(network_test_status.message),"Connection test stopped before starting.");
            }
            if(action>=37 && action<=40) {
                vmu_snapshot.status=(struct kui_app_status){.stopped=true};
                snprintf(vmu_snapshot.status.message,sizeof(vmu_snapshot.status.message),"VMU operation stopped before starting.");
                vmu_snapshot.delete_ready=vmu_snapshot.copy_ready=false;
                if(action<=38) ++vmu_delete_generation;else ++vmu_copy_generation;
            }
            if(action==34 || action==35) {
                clock_valid=false;++clock_generation;
                snprintf(clock_note,sizeof(clock_note),"Clock operation stopped before starting.");
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
                /* A volume/off command remains effective even if SD is full
                 * or unavailable. Persistence is a separate best-effort step. */
                system_current=next;system_pending=next;
                system_ok=true;++system_generation;
                mutex_unlock(&lock);
                kui_music_set_config(next.music_enabled,next.music_volume);
                kui_cd_audio_set_volume(next.music_enabled?next.music_volume:0);
                if(next.music_enabled && !kui_cd_audio_owns_drive()) kui_music_resume();
                publish_music();
                system_operation(true);
                mutex_lock(&lock);
                if(!system_ok) {
                    system_ok=true;++system_generation;
                    snprintf(system_note,sizeof(system_note),
                        "Music volume applied; preferences not saved to SD.");
                }
                mutex_unlock(&lock);
            }
            if(action==23) {
                kui_sd_set_params(0,true);
                struct kui_music_player_page page;
                bool ok=kui_music_player_list(music_path_pending,music_offset_pending,&page,kui_log,kui_cancelled);
                mutex_lock(&lock);
                music_listing=page;++music_listing_generation;
                snprintf(player_status.message,sizeof(player_status.message),"%s",page.message);
                player_status.complete=true;player_status.passed=ok;player_status.errors=ok?0u:1u;
                mutex_unlock(&lock);
            }
            if(action==24) {
                kui_sd_set_params(0,true);
                struct kui_app_status result;
                active_app=24;
                kui_music_player_run(music_path_pending,system_current.music_volume,&result,
                    kui_log,kui_cancelled,app_progress);
                mutex_lock(&lock);
                player_status=result;
                if(result.passed) {
                    system_current.music_enabled=true;system_pending=system_current;
                    system_ok=true;++system_generation;
                    music_requested=-1;++music_request_generation;
                }
                mutex_unlock(&lock);
                publish_music();
            }
            if(action==54) {
                kui_sd_set_params(0,true);
                struct kui_games_page page;
                kui_games_list(games_path_pending,games_offset_pending,&page,kui_log,kui_cancelled);
                mutex_lock(&lock);games_listing=page;games_result_offset=games_offset_pending;
                ++games_listing_generation;mutex_unlock(&lock);
            }
            if(action==55) {
                kui_sd_set_params(0,true);
                struct kui_games_detail detail;
                kui_games_inspect(games_path_pending,&detail,kui_log,kui_cancelled);
                mutex_lock(&lock);games_detail=detail;++games_detail_generation;mutex_unlock(&lock);
            }
            if(action==56 || action==57) {
                kui_sd_set_params(0,true);
                bool prepared=action==57?
                    kui_games_image_probe_prepare(games_path_pending,&probe_image,kui_log,kui_cancelled):
                    kui_games_probe_prepare(&probe_image,kui_log,kui_cancelled);
                if(prepared && kui_cancelled()) {kui_runtime_free(&probe_image);prepared=false;}
                if(prepared) {
                    /* No more filesystem work may run once main owns this
                     * image. arch_exec performs the final KOS teardown. */
                    mutex_lock(&lock);kui_menu_sound_shutdown();mutex_unlock(&lock);
                    kui_music_shutdown();publish_music();
                    mutex_lock(&lock);probe_launch_ready=true;mutex_unlock(&lock);
                    for(;;) {
                        thd_sleep(16);
                        mutex_lock(&lock);bool waiting=probe_launch_ready;mutex_unlock(&lock);
                        if(!waiting) break; /* main rejected the staging range */
                    }
                    /* A last-moment B or staging guard can return here. The
                     * shutdown joined the audio thread, so recreate services
                     * before making the menu usable again. */
                    kui_music_init(kui_log);
                    kui_music_set_config(system_current.music_enabled,system_current.music_volume);
                    mutex_lock(&lock);
                    music_cache_attempted=0;
                    (void)kui_menu_sound_init();
                    kui_menu_sound_config(system_current.menu_sounds,system_current.music_volume);
                    mutex_unlock(&lock);
                    publish_music();
                } else {
                    mutex_lock(&lock);probe_launch_failed=true;mutex_unlock(&lock);
                }
            }
            if(action==25 || action==44) {
                /* Restart must remain usable after an optical recovery failure. */
                if(action==44) {
                    kui_cd_audio_set_volume(0);
                    (void)kui_cd_audio_stop_for_io(kui_log);
                }
                /* All app I/O is finished; keep the worker parked until main's
                 * normal KOS shutdown tears down the remaining services. */
                mutex_lock(&lock);kui_menu_sound_shutdown();mutex_unlock(&lock);
                kui_music_shutdown();publish_music();
                mutex_lock(&lock);boot_ready=true;mutex_unlock(&lock);
                for(;;) thd_sleep(1000);
            }
            if(action==27) {
                settings_operation(false);
                system_operation(false);
                if(system_current.startup_chime && !startup_sound_cancelled())
                    kui_music_play_boot_chime(startup_sound_cancelled);
                mutex_lock(&lock);
                bool menu_sound_ok=kui_menu_sound_init();
                kui_menu_sound_config(system_current.menu_sounds,system_current.music_volume);
                splash_active=false;startup_finished=true;
                mutex_unlock(&lock);
                if(!menu_sound_ok) kui_log("Menu sounds unavailable; other audio remains usable.");
            }
            if(action>=49 && action<=53) {
                if(action==50 || action==52) {
                    mutex_lock(&lock);music_requested=-1;++music_request_generation;mutex_unlock(&lock);
                    kui_music_pause();publish_music();
                }
                kui_cd_audio_set_volume(action==50?system_current.music_volume:
                    system_current.music_enabled?system_current.music_volume:0);
                kui_cd_audio_run((enum kui_cd_audio_action)(action-49),cd_track_pending,NULL,kui_log,kui_cancelled);
                struct kui_cd_audio_status result;kui_cd_audio_status_copy(&result);
                if(action==50 && result.playing) {
                    mutex_lock(&lock);system_current.music_enabled=true;system_pending=system_current;
                    system_ok=true;++system_generation;mutex_unlock(&lock);
                    /* Keep the cached PCM stream paused while enabling volume controls. */
                    kui_music_set_config(true,system_current.music_volume);publish_music();
                }
                publish_cd_audio();
            }
            if(action==30) scan_operation();
            if(action==31) {
                struct kui_vmu_backup_view result;
                active_app=31;kui_sd_set_params(0,true);
                kui_vmu_backups_run(vmu_backup_page_pending,&result,kui_log,kui_cancelled,app_progress);
                mutex_lock(&lock);vmu_backups=result;++vmu_backups_generation;mutex_unlock(&lock);
            }
            if(action==32 || action==33) {
                struct kui_vmu_view result;
                active_app=action;kui_sd_set_params(0,true);
                kui_vmu_restore_run(vmu_restore_path_pending,vmu_slot_pending,action==33,
                    &result,kui_log,kui_cancelled,app_progress);
                mutex_lock(&lock);vmu_snapshot=result;++vmu_restore_generation;mutex_unlock(&lock);
                if(action==33) {
                    kui_log("VMU restore result: %s",result.status.message);
                    kui_ui_set_hz(KUI_OPT_UI_FULL);
                    save_report("auto VMU restore",result.status.passed?"verified":
                        result.status.stopped?"stopped":"not verified",true);
                }
            }
            if(action==34 || action==35) clock_operation(action==35);
            if(action==36) {
                mutex_lock(&lock);music_requested=-1;++music_request_generation;mutex_unlock(&lock);
                kui_music_clear_cache();publish_music();
                mutex_lock(&lock);music_cache_attempted=(1u<<KUI_MUSIC_TRACKS)-1u;mutex_unlock(&lock);
                kui_music_log_stats("clear cache");
            }
            if(action>=37 && action<=40) {
                struct kui_vmu_view result;
                active_app=action;kui_sd_set_params(0,true);
                if(action<=38) kui_vmu_delete_run(vmu_slot_pending,vmu_page_pending,vmu_selected_pending,
                    action==38,&result,kui_log,kui_cancelled,app_progress);
                else kui_vmu_copy_run(vmu_slot_pending,vmu_page_pending,vmu_selected_pending,
                    vmu_copy_slot_pending,action==40,&result,kui_log,kui_cancelled,app_progress);
                if((action==38 || action==40) && result.status.passed) {
                    struct kui_app_status completed=result.status;
                    struct kui_vmu_view refreshed;
                    kui_vmu_app_run(0,vmu_slot_pending,vmu_page_pending,0,&refreshed,
                        kui_log,NULL,NULL);
                    if(refreshed.status.passed) {result=refreshed;result.status=completed;}
                    else result.count=0;
                }
                mutex_lock(&lock);vmu_snapshot=result;
                if(action==38 || action==40) {
                    if(!result.status.passed) vmu_snapshot.count=0;
                    if(!vmu_snapshot.count) {vmu_snapshot.slot=vmu_slot_pending;vmu_snapshot.page=vmu_page_pending;}
                    ++vmu_generation;
                } else if(action==37) ++vmu_delete_generation;else ++vmu_copy_generation;
                mutex_unlock(&lock);
                if(action==38 || action==40) {
                    kui_ui_set_hz(KUI_OPT_UI_FULL);
                    save_report(action==38?"auto VMU delete":"auto VMU copy",result.status.passed?"verified":
                        result.status.stopped?"stopped":"not verified",true);
                }
            }
            if(action>=41 && action<=43) {
                struct kui_app_status result;
                active_app=action;kui_sd_set_params(0,true);
                kui_maintenance_run(action-41,&result,kui_log,kui_cancelled,app_progress);
                mutex_lock(&lock);maintenance_status=result;mutex_unlock(&lock);
                if(action!=41) {
                    kui_ui_set_hz(KUI_OPT_UI_FULL);
                    save_report(action==42?"auto settings flash backup":"auto visible BIOS backup",
                        result.passed?"backup verified":result.stopped?"stopped":"not verified",true);
                }
            }
            if(action>=46 && action<=48) salvage_operation(action);
            if(action==45) {
                struct kui_app_status result;
                active_app=action;
                kui_network_connect_run(NULL,&result,kui_log,kui_cancelled,app_progress);
                mutex_lock(&lock);network_test_status=result;mutex_unlock(&lock);
                kui_ui_set_hz(KUI_OPT_UI_FULL);
                save_report("auto network connection",result.passed?"local network passed":
                    result.stopped?"stopped":"not confirmed",true);
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
                observing_capture=false;
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
        if(action==1 || (action>=4 && action<=7) || action==22 || (action>=46 && action<=48)) kui_disc_identity_invalidate(&disc_identity);
#else
        kui_log("Operation ended. Y saves the current log to SD.");
#endif
        mutex_lock(&lock);
        busy = false;
        cancel_requested = false;
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
    snprintf(title, sizeof(title), "%s", capture_summary.disc_title[0]?
        capture_summary.disc_title:capture_display.title);
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
    view.music_change_pending=music_requested>=0;
    view.music_cache_bytes=music_snapshot.cache_bytes;
    view.music_loading_bytes=music_snapshot.loading_bytes;
    view.music_peak_file_bytes=music_snapshot.peak_file_bytes;
    if(cd_drive_owned) {
        snprintf(music_title,sizeof(music_title),"Audio CD - Track %u",cd_audio_snapshot.current);
        snprintf(music_notice,sizeof(music_notice),"%s",cd_audio_snapshot.message);
        view.music_enabled=system_current.music_enabled;
        view.music_playing=cd_audio_snapshot.playing;view.music_paused=cd_audio_snapshot.paused;
        view.music_volume=system_current.music_enabled?system_current.music_volume:0;
        view.music_change_pending=false;
    }
    view.drive_reset_required=drive_reset_required;
    view.dma_degraded=dma_degraded;
    view.video_trial=video_preview;
    uint64_t now=timer_ms_gettime64();
    view.video_seconds=video_preview && video_deadline>now?(unsigned)((video_deadline-now+999)/1000):0;
    view.phase_elapsed_ms=now>=phase_started_ms?now-phase_started_ms:0;
    view.progress_age_ms=now>=progress_updated_ms?now-progress_updated_ms:0;
    app_status=shell.page==KUI_SHELL_MEMORY?memory_test_status:
        (shell.page==KUI_SHELL_GAMES_PROBE_CONFIRM || shell.page==KUI_SHELL_GAMES_IMAGE_PROBE_CONFIRM)?probe_status:
        shell.page==KUI_SHELL_NETWORK?network_test_status:
        shell.page==KUI_SHELL_MUSIC?player_status:
        shell.page==KUI_SHELL_CRC_SCAN?scan_status:
        shell.page==KUI_SHELL_SALVAGE?salvage_status:
        shell.page==KUI_SHELL_SYSTEM_TOOLS?maintenance_status:
        shell.page==KUI_SHELL_VMU_RESTORE && active_app==31?vmu_backups.status:vmu_snapshot.status;
    view.phase = capture_status.phase; view.track = capture_status.track; view.tracks = capture_status.tracks;
    view.rate_kib = rate_kib; view.retries = capture_display.retries;
    view.retry_attempt=capture_display.retry_attempt;view.retry_limit=capture_display.retry_limit;
    view.retry_fad=capture_display.retry_fad;
    view.done = capture_status.done; view.total = capture_status.total;
    view.committed = capture_status.committed; view.elapsed_ms = capture_status.elapsed_ms;
    unsigned screen_inset=system_current.screen_inset*16u;
    mutex_unlock(&lock);
    view.memory_valid = memory_valid; view.memory_used = memory_status.used;
    view.memory_physical = memory_status.physical; view.memory_peak = memory_status.sampled_peak;
    /* The pinned KOS RGB565 clear uses SH-4 store queues instead of a pixel
     * loop. DM_MULTIBUFFER/vid_flip leave vram_s on the next, offscreen buffer;
     * publish only after the complete frame has been drawn. RGB565 = 0x0864,
     * matching the portable renderer's background. */
    vid_clear(8, 15, 35);
    kui_shell_draw_content(vram_s, &shell, &view, NULL, NULL);
    kui_viewport_inset(vram_s,640,480,screen_inset,0x0864);
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
        case KUI_SHELL_ADVANCED_CRC: return 30;
        case KUI_SHELL_VMU_BACKUPS_LIST: return 31;
        case KUI_SHELL_VMU_RESTORE_PREVIEW: return 32;
        case KUI_SHELL_VMU_RESTORE_COMMIT: return 33;
        case KUI_SHELL_CLOCK_READ: return 34;
        case KUI_SHELL_CLOCK_WRITE: return 35;
        case KUI_SHELL_MUSIC_CLEAR_CACHE: return 36;
        case KUI_SHELL_VMU_DELETE_PREVIEW: return 37;
        case KUI_SHELL_VMU_DELETE_COMMIT: return 38;
        case KUI_SHELL_VMU_COPY_PREVIEW: return 39;
        case KUI_SHELL_VMU_COPY_COMMIT: return 40;
        case KUI_SHELL_SYSTEM_INSPECT: return 41;
        case KUI_SHELL_FLASH_BACKUP: return 42;
        case KUI_SHELL_BIOS_BACKUP: return 43;
        case KUI_SHELL_RESTART: return 44;
        case KUI_SHELL_NETWORK_CONNECT: return 45;
        case KUI_SHELL_SALVAGE_NEW: return 46;
        case KUI_SHELL_SALVAGE_RESUME: return 47;
        case KUI_SHELL_SALVAGE_RECOVER: return 48;
        case KUI_SHELL_CD_LIST: return 49;
        case KUI_SHELL_CD_PLAY: return 50;
        case KUI_SHELL_CD_PAUSE: return 51;
        case KUI_SHELL_CD_RESUME: return 52;
        case KUI_SHELL_CD_STOP: return 53;
        case KUI_SHELL_GAMES_LIST: return 54;
        case KUI_SHELL_GAMES_INSPECT: return 55;
        case KUI_SHELL_GAMES_PROBE: return 56;
        case KUI_SHELL_GAMES_IMAGE_PROBE: return 57;
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
    kui_clock_start(kui_log);
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
    splash_deadline=timer_ms_gettime64()+3000;
    kui_log("K-UI launcher: Disc Ripper, VMU, Memory, Network, Settings, Diagnostics, GD Play and Music.");
    kui_log("B skips the original startup splash/chime. Home Y cycles menu music volume/off.");
    kui_log("Ripper: A new dump (confirm), X resume latest matching disc, Y verify.");
    kui_log("B returns home while idle; during work it stops and checkpoints.");
    kui_log("New dumps use game-named folders in /Games; duplicates get a number.");
    kui_log("Home/Ripper L/R: previous/next song. Ripper Start: Advanced, destination and settings.");
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
    unsigned seen_clock_generation=0,seen_vmu_backups=0,seen_vmu_restore=0;
    unsigned seen_vmu_delete=0,seen_vmu_copy=0,seen_cd_audio=0;
    unsigned seen_games_listing=0,seen_games_detail=0;
    bool startup_routed=false;
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
        publish_music();
        mutex_lock(&lock);
        if(splash_active && input_at>=splash_deadline) {splash_active=false;last_draw=0;}
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
                kui_menu_sound_config(system_current.menu_sounds,system_current.music_volume);
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
        if(seen_games_listing!=games_listing_generation) {
            if(shell.games_page*KUI_GAMES_ROWS==games_result_offset)
                kui_shell_set_games_listing(&shell,&games_listing);
            seen_games_listing=games_listing_generation;
        }
        if(seen_games_detail!=games_detail_generation) {
            kui_shell_set_games_detail(&shell,&games_detail);
            seen_games_detail=games_detail_generation;
        }
        if(seen_clock_generation!=clock_generation) {
            kui_shell_set_clock(&shell,clock_valid?&clock_snapshot:NULL,clock_note);
            seen_clock_generation=clock_generation;
        }
        if(seen_vmu_backups!=vmu_backups_generation) {
            kui_shell_set_vmu_backups(&shell,&vmu_backups);
            seen_vmu_backups=vmu_backups_generation;
        }
        if(seen_vmu_restore!=vmu_restore_generation) {
            kui_shell_set_vmu_restore_preview(&shell,&vmu_snapshot);
            seen_vmu_restore=vmu_restore_generation;
        }
        if(seen_vmu_delete!=vmu_delete_generation) {
            kui_shell_set_vmu_delete_preview(&shell,&vmu_snapshot);seen_vmu_delete=vmu_delete_generation;
        }
        if(seen_vmu_copy!=vmu_copy_generation) {
            kui_shell_set_vmu_copy_preview(&shell,&vmu_snapshot);seen_vmu_copy=vmu_copy_generation;
        }
        if(seen_cd_audio!=cd_audio_generation) {
            kui_shell_set_cd_audio(&shell,&cd_audio_snapshot);seen_cd_audio=cd_audio_generation;
        }
        if(startup_finished && !busy && !startup_routed) {
            startup_routed=true;
            switch(system_current.startup_app) {
                case KUI_STARTUP_RIPPER: shell.page=KUI_SHELL_RIPPER;break;
                case KUI_STARTUP_VMU: shell.page=KUI_SHELL_VMU;break;
                case KUI_STARTUP_MUSIC: shell.page=KUI_SHELL_MUSIC;break;
                case KUI_STARTUP_DIAGNOSTICS: shell.page=KUI_SHELL_DIAGNOSTICS;break;
                default: shell.page=KUI_SHELL_HOME;break;
            }
            /* Populate read-only lists through the storage worker. A save,
             * rip or VMU write always needs an explicit controller action. */
            if(shell.page==KUI_SHELL_VMU) {
                vmu_slot_pending=shell.vmu_slot;vmu_page_pending=shell.vmu_page;
                vmu_selected_pending=shell.vmu_selected;pending=16;
            } else if(shell.page==KUI_SHELL_MUSIC) {
                snprintf(music_path_pending,sizeof(music_path_pending),"%s",shell.music_path);
                music_offset_pending=0;pending=23;
            }
            if(pending) {busy=true;cancel_requested=false;ui_hz_busy=2;}
            last_draw=0;
        }
        if(video_preview && input_at>=video_deadline) {
            video_preview=false;shell.video_trial=false;
            safe_video_boot=video_prior_safe;apply_video(video_prior_mode);
            shell.system_draft.video_mode=system_current.video_mode;
            snprintf(system_note,sizeof(system_note),"Video reverted after 10 seconds.");
        }
        enum kui_shell_action requested=KUI_SHELL_NONE;
        if(splash_active) {if(pressed&CONT_B) {startup_skip=true;splash_active=false;last_draw=0;}}
        else {
            requested=kui_shell_input(&shell,shell_buttons(pressed),busy);
            if(pressed && !busy && startup_finished)
                kui_menu_sound_play(pressed&CONT_A?KUI_MENU_SOUND_CONFIRM:KUI_MENU_SOUND_MOVE);
        }
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
        if(cd_drive_owned && !busy && (requested==KUI_SHELL_MUSIC_NEXT || requested==KUI_SHELL_MUSIC_PREVIOUS)) {
            unsigned count=cd_audio_snapshot.count,index=0;
            while(index<count && cd_audio_snapshot.tracks[index].number!=cd_audio_snapshot.current) ++index;
            if(count) {
                if(index==count) index=0;
                index=(index+count+(requested==KUI_SHELL_MUSIC_PREVIOUS?-1:1))%count;
                shell.cd_audio=cd_audio_snapshot;shell.cd_selected=index;
                requested=KUI_SHELL_CD_PLAY;
            }
        }
        if(cd_drive_owned && requested==KUI_SHELL_MUSIC_STOP) requested=KUI_SHELL_CD_STOP;
        unsigned action = worker_action(requested);
        if(drive_reset_required && (action==1 || (action>=4 && action<=7) || action==22 || (action>=46 && action<=53))) {
            snprintf(capture_message,sizeof(capture_message),
                "Drive reset required. Reboot, then Resume the partial dump.");
            if(action>=46 && action<=48) {
                salvage_status=(struct kui_app_status){.errors=1};
                snprintf(salvage_status.message,sizeof(salvage_status.message),"Drive reset required. Use System Tools > Restart.");
            }
            if(action>=49 && action<=53) {
                cd_audio_snapshot.poisoned=true;
                snprintf(cd_audio_snapshot.message,sizeof(cd_audio_snapshot.message),"Drive reset required. Use System Tools > Restart.");
                ++cd_audio_generation;
            }
            action=0;
        }
        if(is_capture_action(action) && !destination_ready) {
            capture_summary = (struct kui_capture_stats){0};
            capture_status = (struct kui_capture_progress){0};
            capture_outcome = KUI_SHELL_OUTCOME_FAILED;
            kui_shell_destination_error(&shell, "Destination unavailable. Start > Destination to choose a folder.");
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
            if(action==30) {
                snprintf(scan_path_pending,sizeof(scan_path_pending),"%s",shell.browse_path);
                scan_status=(struct kui_app_status){0};
            }
            if(action==31) {
                vmu_backup_page_pending=shell.backup_page;
                vmu_backups.status=(struct kui_app_status){0};
            }
            if(action==32 || action==33) {
                vmu_slot_pending=shell.vmu_slot;
                snprintf(vmu_restore_path_pending,sizeof(vmu_restore_path_pending),"%s",shell.restore_path);
                vmu_snapshot.status=(struct kui_app_status){0};
            }
            if(action>=37 && action<=40) {
                vmu_slot_pending=shell.vmu_slot;vmu_page_pending=shell.vmu_page;
                vmu_selected_pending=shell.vmu_selected;vmu_copy_slot_pending=shell.vmu_copy_slot;
                vmu_snapshot.status=(struct kui_app_status){0};
            }
            if(action>=46 && action<=48) {
                salvage_zero_pending=shell.salvage_zero_fill;salvage_passes_pending=shell.salvage_passes;
                salvage_path_pending[0]=0;
                salvage_status=(struct kui_app_status){0};
            }
            if(action>=49 && action<=53) cd_track_pending=shell.cd_selected<shell.cd_audio.count?
                shell.cd_audio.tracks[shell.cd_selected].number:0;
            if(action>=41 && action<=43) maintenance_status=(struct kui_app_status){0};
            if(action==35) clock_pending=shell.clock_draft;
            if(action==23 || action==24) {
                snprintf(music_path_pending,sizeof(music_path_pending),"%s",
                    action==23?shell.music_path:shell.music_selected_path);
                music_offset_pending=shell.music_page*KUI_MUSIC_PLAYER_ROWS;
                player_status=(struct kui_app_status){0};
            }
            if(action==54 || action==55 || action==57) {
                snprintf(games_path_pending,sizeof(games_path_pending),"%s",
                    action==54?shell.games_path:shell.games_selected_path);
                games_offset_pending=shell.games_page*KUI_GAMES_ROWS;
            }
            if(action==56 || action==57) {
                probe_status=(struct kui_app_status){0};
                snprintf(probe_status.message,sizeof(probe_status.message),"%s",action==57?
                    "Mapping selected image and reading reference samples...":
                    "Preparing resident probe and SD map...");
                probe_launch_failed=false;
            }
            if(action==19) memory_test_status=(struct kui_app_status){0};
            if(action==20 || action==45) network_test_status=(struct kui_app_status){0};
            if(action == 8 || action == 9)
                snprintf(settings_note, sizeof(settings_note), "%s", action == 8 ?
                    "Loading preferences from SD..." : "Saving preferences to SD...");
            pending = action; busy = true; cancel_requested = false; shell.scroll = 0;
            ui_hz_busy = 2;
            if(is_capture_action(action)) {
                kui_capture_display_start(&capture_display,
                    disc_snapshot.state==KUI_DISC_IDENTITY_READY?disc_snapshot.title:NULL);
                observing_capture=true;
                capture_status = (struct kui_capture_progress){0};
                capture_summary = (struct kui_capture_stats){0};
                capture_outcome = KUI_SHELL_OUTCOME_NONE;
                rate_at = rate_bytes = 0; rate_kib = 0;
                capture_message[0]=0;phase_started_ms=progress_updated_ms=input_at;
            }
        }
        bool is_busy = busy, cd_owned=cd_drive_owned;
        mutex_unlock(&lock);
        if(requested==KUI_SHELL_MUSIC_NEXT && !cd_owned) music_step(1);
        if(requested==KUI_SHELL_MUSIC_PREVIOUS && !cd_owned) music_step(-1);
        if(requested==KUI_SHELL_MUSIC_STOP) {
            mutex_lock(&lock);music_requested=-1;++music_request_generation;mutex_unlock(&lock);
            kui_music_pause();publish_music();
        }
        mutex_lock(&lock);bool exiting=boot_ready;mutex_unlock(&lock);
        if(exiting) kui_gd_play_boot();
        mutex_lock(&lock);
        bool launch_probe=probe_launch_ready,probe_failed=probe_launch_failed;
        probe_launch_failed=false;
        mutex_unlock(&lock);
        if(probe_failed) {shell.page=KUI_SHELL_DIAGNOSTICS;shell.scroll=0;}
        if(launch_probe) {
            uintptr_t stack;
            __asm__ __volatile__("mov r15,%0" : "=r"(stack));
            uintptr_t source=(uintptr_t)probe_image.data;
            size_t length=probe_image.info.payload_bytes;
            /* arch_exec copies forward to low RAM and executes its trampoline
             * on main's stack. High resident relocation occurs only afterwards. */
            bool safe=source>=KUI_RUNTIME_ADDRESS && source<0x8d000000u && !(source&3u) &&
                length && !(length&3u) &&
                length<=KUI_IMAGE_RESIDENT_BLOB_OFFSET+KUI_IMAGE_RESIDENT_MAX_BYTES &&
                length<=0x8d000000u-source && stack>0x8c200000u &&
                source+length<=stack-65536u;
            if(safe && !kui_cancelled()) arch_exec(probe_image.data,(uint32_t)length);
            kui_runtime_free(&probe_image);
            kui_log("Loader probe handoff refused: %s",safe?"cancelled":"unsafe staging range");
            mutex_lock(&lock);probe_launch_ready=false;mutex_unlock(&lock);
            shell.page=KUI_SHELL_DIAGNOSTICS;shell.scroll=0;
        }
        if(requested == KUI_SHELL_MSTATS) {
            kui_memory_log("L trigger");kui_music_log_stats("L trigger");shell.scroll=0;
        }
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
