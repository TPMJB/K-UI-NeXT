/* SPDX-License-Identifier: GPL-3.0-only */
#include "platform.h"
#include "kui/version.h"
#include "kui/vmu_lcd.h"
#include "kui/ui_rate.h"
#include "kui/report.h"
#include "kui/clock_platform.h"
#ifndef KUI_SD_RUNTIME
#include "kui/boot_ui.h"
#endif
#ifdef KUI_SD_RUNTIME
#include "kui/shell.h"
#include "kui/recovery_dashboard.h"
#include "kui/salvage_session_policy.h"
#include "kui/storage_error.h"
#include "kui/vmu_backing.h"
#include "kui/recovery_guard.h"
#include "kui/settings.h"
#include "kui/system_settings.h"
#include "kui/disc_identity.h"
#include "kui/music.h"
#include "kui/apps.h"
#include "kui/vmu_import.h"
#include "kui/vmu_emulation_settings.h"
#include "kui/vmu_game_info.h"
#include "kui/music_player.h"
#include "kui/games.h"
#include "kui/game_launch_settings.h"
#include "kui/games_selection.h"
#include "kui/games_probe.h"
#include "kui/games_image_probe.h"
#include "kui/games_retail.h"
#include "kui/games_covers.h"
#include "kui/games_storage.h"
#include "kui/image_loader_layout.h"
#include <arch/exec.h>
#include "kui/splash.h"
#include "kui/gd_play.h"
#include "kui/recovery_scan.h"
#include "kui/repair_copy.h"
#include "kui/drive_speed_probe.h"
#include "kui/saved_read_probe.h"
#include "kui/salvage_revalidate.h"
#include "kui/g1_mode_probe.h"
#include "kui/capture_display.h"
#include "kui/salvage_display.h"
#include "kui/viewport.h"
#include "kui/network_probe.h"
#include "kui/ftp.h"
#include "kui/wifi_update.h"
#include "kui/salvage.h"
#include "kui/salvage_completion.h"
#include "kui/maintenance.h"
#include "kui/menu_sound.h"
#include "kui/cd_audio.h"
#include "kui/storage_test.h"
#include "sd.h"
#include "sci_sd_bus.h"
#include "kui/sci_video_quiet.h"
#include <dc/sq.h>
#endif
#include <kos.h>
#include <dc/minifont.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
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
#define KUI_BUTTON_BENCH (1u<<29)
#ifndef KUI_SD_RUNTIME
#define KUI_BUTTON_BOOT_LEFT (1u<<30)
#endif
#ifdef KUI_SD_RUNTIME
#define KUI_BUTTON_MSTATS (1u<<30)
static struct kui_memory_stats memory_status;
static bool memory_valid;
static struct kui_shell shell;
/* Draw antialiasing and panels against cached RAM, then stream the complete
 * RGB565 frame to the existing offscreen video buffer. Reading each font
 * background pixel directly from uncached VRAM stalls controller polling.
 * This optional cache is released before game/executable preparation so its
 * 600 KiB cannot reduce the loader's available heap or in-game reservation. */
static uint16_t *shell_frame;
static uint16_t *shell_frame_prepare(bool working) {
    if(!shell_frame && !working) shell_frame=aligned_alloc(32u,640u*480u*sizeof(*shell_frame));
    return shell_frame;
}
static void shell_frame_release_for_action(unsigned action) {
    if(action==19u || (action>=56u && action<=58u) || action==62u || action==79u) {
        free(shell_frame);shell_frame=NULL;
    }
}
static struct kui_settings settings_current, settings_pending;
static unsigned settings_generation;
static char settings_note[96];
static char destination_current[KUI_DEST_ROOT_CAP], destination_pending[KUI_DEST_ROOT_CAP];
static char capture_destination[KUI_DEST_ROOT_CAP], destination_note[128];
static char capture_job_pending[KUI_DEST_JOB_CAP],capture_label_pending[KUI_DEST_TITLE_CAP];
static bool capture_network_pending;
static bool capture_cd_toc_pending;
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
static struct kui_vmu_view vmu_image_snapshot;
static unsigned vmu_image_generation,vmu_image_page_pending,vmu_image_selected_pending;
static char vmu_image_path_pending[KUI_VMU_BACKUP_PATH_CAP];
static char maintenance_compare_path_pending[KUI_FILES_PATH_CAP];
static struct kui_vmu_view vmu_bulk_snapshot,vmu_import_snapshot;
static unsigned vmu_bulk_generation,vmu_bulk_source_result,vmu_bulk_destination_result;
static bool vmu_bulk_commit_result,vmu_import_commit_result;
static unsigned vmu_import_generation,vmu_import_request_pending,vmu_import_request_result;
static char vmu_import_path_pending[KUI_FILES_PATH_CAP],vmu_import_path_result[KUI_FILES_PATH_CAP];
static struct kui_vmu_backup_view vmu_collection_snapshot;
static unsigned vmu_collection_generation,vmu_collection_page_pending;
static unsigned vmu_collection_request_pending,vmu_collection_request_result;
static char vmu_name_path_pending[KUI_VMU_BACKUP_PATH_CAP],vmu_name_label_pending[49];
static uint8_t vmu_name_identity_pending[16];
static struct kui_vmu_game_info vmu_game_info_snapshot;
struct vmu_game_info_request {
    char product[12];
    unsigned generation;
    bool native_route;
};
static struct vmu_game_info_request vmu_game_info_requested,vmu_game_info_running;
static unsigned vmu_game_info_request_result,vmu_game_info_generation;
static bool vmu_game_info_request_valid;
static struct kui_vmu_view vmu_collection_copy_snapshot;
static unsigned vmu_collection_copy_generation,vmu_collection_copy_request_pending,vmu_collection_copy_request_result;
static unsigned vmu_collection_copy_source_result,vmu_collection_copy_page_result,vmu_collection_copy_selected_result;
static bool vmu_collection_copy_all_pending,vmu_collection_copy_all_result,vmu_collection_copy_commit_result;
static struct kui_vmu_wizard_request vmu_wizard_pending;
static struct kui_vmu_wizard_view vmu_wizard_snapshot;
static unsigned vmu_wizard_generation;
static unsigned vmu_emulation_request,vmu_emulation_result,vmu_emulation_generation;
static bool vmu_emulation_requested,vmu_emulation_enabled=true,vmu_emulation_ok,vmu_emulation_stopped;
static char vmu_emulation_note[128];
static struct kui_datetime clock_pending,clock_snapshot;
static bool clock_valid;
static unsigned clock_generation;
static char clock_note[128];
static struct kui_storage_test_request storage_test_pending;
static uint32_t storage_baseline_pending;
static struct kui_storage_test_result storage_test_result;
static struct kui_storage_test_history storage_test_history;
static struct kui_storage_test_progress storage_test_progress;
static unsigned storage_test_generation, storage_history_generation;
static bool storage_test_running;
static struct kui_app_status sci_async_status;
/* Only the UI thread draws. A fresh request is acknowledged by that thread
 * after its final frame and SQ drain, never merely by the worker setting a flag. */
static struct kui_sci_video_quiet sci_video_quiet;
static struct kui_app_status scan_status,salvage_status;
/* Worker-owned presentation snapshot. The UI copies this into shell under
 * lock before input; the renderer never observes worker writes to shell. */
static struct kui_salvage_dashboard recovery_dashboard;
/* Single-worker, RAM-only optimization. UI snapshots never authorize reuse. */
static struct kui_salvage_session_policy salvage_session={.epoch=1};
static void salvage_session_forget(void) {
    kui_salvage_session_policy_forget(&salvage_session);
    kui_salvage_session_invalidate();
}
static void salvage_session_observe_errors(void) {
    struct kui_storage_errors errors;kui_storage_errors_get(&errors);
    if(kui_salvage_session_policy_errors(&salvage_session,errors.total))
        salvage_session_forget();
}
static char salvage_path_pending[KUI_DEST_JOB_CAP];
static unsigned salvage_passes_pending;
static struct kui_drive_options salvage_drive_pending;
static bool salvage_trial_pending,salvage_speed_probe_pending;
static enum kui_salvage_diagnostic salvage_diagnostic_pending;
static bool salvage_verify_pending,salvage_defer_pending;
static char scan_path_pending[KUI_DEST_JOB_CAP];
static unsigned repair_attempts_pending=10u;
static unsigned repair_passes_pending;
static bool repair_in_place_pending,repair_new_copy_pending,recovery_cd_pending,repair_saved_identity_pending;
static bool repair_new_copy_created; /* Worker publishes under lock after creating a transaction. */
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
static struct kui_game_launch_settings games_detail_launch_settings,games_launch_settings_pending;
static struct kui_game_ram_patches games_patches_pending;
static bool games_detail_launch_loaded,games_detail_launch_found,games_launch_saved;
static char games_launch_path_pending[KUI_GAMES_FILE_CAP],games_launch_result_path[KUI_GAMES_FILE_CAP];
static unsigned games_launch_result_generation;
static unsigned games_offset_pending,games_result_offset;
static char games_seek_pending[KUI_DEST_NAME_CAP];
static bool games_seek_request;
static unsigned games_result_selected;
static bool games_result_restore;
static struct kui_games_selection games_selection_loaded,games_selection_requested;
static unsigned games_selection_generation,games_selection_request,games_selection_saved;
static uint64_t games_selection_deadline;
static unsigned games_epoch_pending,games_result_epoch,games_result_view;
static char games_path_pending[KUI_GAMES_FILE_CAP];
static unsigned games_reader_pending,games_codec_pending;
static bool games_codec_override_pending;
static unsigned games_view_pending;
static bool games_refresh_pending,games_cache_clear_pending;
static bool games_warm_done;
static unsigned games_storage_observed=KUI_STORAGE_AUTO;
static struct kui_shell_games_art_key games_art_requested,games_art_running,games_art_published,games_art_done;
static bool games_art_request_valid,games_art_result_ready,games_art_done_valid;
/* Background titles for the rows on screen, applied by the UI thread. */
static struct kui_games_page games_titles_page;
static unsigned games_titles_offset;
static bool games_titles_ready;
static struct kui_games_entry games_art_entry,games_art_running_entry;
static struct kui_games_cover_result games_art_result;
static struct kui_app_status games_scan_status;
/* The worker fills staging pixels; only main publishes them to the selected
 * cover. A ready result owns staging until main accepts/discards it, so neither
 * an older visible page nor drawing can race the next artwork read. */
static uint16_t games_cover_pixels[KUI_COVER_PIXELS],games_cover_staging[KUI_COVER_PIXELS];
static uint16_t games_detail_cover[KUI_COVER_PIXELS];
/* File Manager: requests copied from the shell when a job is queued, and
 * results the main loop installs by generation. files_status is the live
 * progress; files_result a finished run's (or File Manager song's) status. */
static struct kui_files_request files_request_pending;
static struct kui_files_job files_job_pending;
static struct kui_files_preview files_totals_pending;
static char files_picture_pending[KUI_FILES_PATH_CAP];
static struct kui_files_page files_listing_result;
static struct kui_files_preview files_preview_result;
static struct kui_app_status files_status,files_result;
static struct kui_files_picture files_picture_result;
static unsigned files_listing_generation,files_preview_generation,files_result_generation,files_picture_generation;
static bool files_music;
/* The picture view's pixels: only the worker writes them, and only while
 * the view shows none (its picture is still loading). */
static uint16_t files_picture_pixels[KUI_FILES_PICTURE_EDGE*KUI_FILES_PICTURE_EDGE];
/* FTP server: the status it publishes as it runs, drawn on its page. */
static struct kui_ftp_status ftp_status;
static bool ftp_seen;
/* Wi-Fi page: the job copied from the shell (its password wiped once the
 * job ends) and the view the job publishes as it goes, by generation. */
static struct kui_wifi_request wifi_request_pending;
static struct kui_wifi_view wifi_view;
static unsigned wifi_generation;
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
#ifndef KUI_SD_RUNTIME
static struct kui_boot_ui boot_ui;
static bool boot_worker_available,boot_attempt_cancelled;
static unsigned boot_attempt_previous,boot_held_navigation;
static uint64_t boot_last_draw,boot_repeat_at;
static uint64_t boot_draw_us;
static unsigned boot_draw_count;
static char boot_notice[128]="Ready to load K-UI. Choose a source or insert your card.";
#endif
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
            KUI_RELEASE_SHORT " " KUI_ROLE " %s\nLog truncated: %s\nReport trigger: %s\nOperation result: %s\n",
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
#ifdef KUI_SD_RUNTIME
    if(result!=KUI_REPORT_SAVED)salvage_session_forget();
    salvage_session_observe_errors();
#endif
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
    kui_sd_set_params(KUI_STORAGE_AUTO, true);
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
        (save ? "Preferences saved to storage." : "Preferences loaded; bench.cfg may override capture.") :
        (save ? "Save not confirmed; reopen Settings to check the card." :
                "Could not load preferences. See Diagnostics."));
    mutex_unlock(&lock);
    kui_log("Settings %s: %s", save ? "save" : "load", ok ? "complete" : "failed or stopped");
}
/* The codec is loaded before metadata inspection on the same storage worker.
 * Menu callbacks only publish or copy these values. */
static bool game_launch_settings_load(const char *path,
                                      struct kui_game_launch_settings *value,bool *found) {
    *found=false;
    kui_game_launch_settings_default(value);
    if(kui_cancelled()) return false;
    bool ok=false;
    kui_sd_set_params(KUI_STORAGE_AUTO,true);
    if(kui_games_storage_open(kui_log)==KUI_GAMES_STORAGE_OK) {
        if(!kui_cancelled()) ok=kui_game_launch_settings_lookup(path,value,found,kui_log);
        if(!kui_games_storage_done(ok || kui_cancelled())) ok=false;
    }
    if(!ok) {kui_game_launch_settings_default(value);*found=false;}
    return ok;
}
static bool game_launch_settings_write(const char *path,const struct kui_game_launch_settings *value) {
    bool ok=false;
    kui_sd_set_params(KUI_STORAGE_AUTO,true);
    if(!kui_cancelled() && kui_game_launch_settings_valid(value) &&
       kui_games_storage_open(kui_log)==KUI_GAMES_STORAGE_OK) {
        /* Once begun, the bounded save finishes sync, close and verify
         * before the worker admits another storage operation. */
        if(!kui_cancelled()) ok=kui_game_launch_settings_save(path,value,kui_log);
        if(!kui_games_storage_done(ok || kui_cancelled())) ok=false;
    }
    return ok;
}
static void game_launch_settings_publish_save(const char *path,bool ok) {
    mutex_lock(&lock);
    snprintf(games_launch_result_path,sizeof(games_launch_result_path),"%s",path);
    games_launch_saved=ok;++games_launch_result_generation;
    mutex_unlock(&lock);
}
static void game_launch_settings_save(void) {
    char path[KUI_GAMES_FILE_CAP];struct kui_game_launch_settings value;
    mutex_lock(&lock);
    memcpy(path,games_launch_path_pending,sizeof(path));value=games_launch_settings_pending;
    mutex_unlock(&lock);
    game_launch_settings_publish_save(path,game_launch_settings_write(path,&value));
}
/* Independent system preferences: capture choices stay in the ripper record. */
static void publish_music(void) {
    struct kui_music_status status;
    kui_music_status_copy(&status);
    mutex_lock(&lock); music_snapshot=status; mutex_unlock(&lock);
}
/* Routine UI publication must not wait for Ogg decoding or a G2 stream
 * transfer. Those run under audio_lock; controls still take fresh snapshots
 * through publish_music/status_copy when their result requires one. */
static void publish_music_if_ready(void) {
    struct kui_music_status status;
    if(!kui_music_status_try_copy(&status)) return;
    mutex_lock(&lock);music_snapshot=status;mutex_unlock(&lock);
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
/* A requested bundled song this card lacks must not leave startup silent.
 * While nothing is loaded, try the next song not yet known to be missing;
 * each miss is recorded, so this stops within one pass of the playlist. */
static int next_after_missing(unsigned wanted) {
    struct kui_music_status status;
    kui_music_status_copy(&status);
    if(status.loaded || !(status.missing_mask>>wanted&1u)) return -1;
    unsigned next=kui_music_next_index(wanted,1);
    return next<KUI_MUSIC_TRACKS && !(status.missing_mask>>next&1u)?(int)next:-1;
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
        (void)kui_games_storage_release();
        bool ok=kui_music_cache_menu((unsigned)wanted,music_preload_cancelled);
        if(!music_preload_cancelled()) {
            mutex_lock(&lock);
            bool current=generation==music_request_generation;
            /* The audio lock never logs or takes this publication lock.
             * Keep selection atomic with its generation check so an older
             * preload cannot overwrite a newer trigger selection. */
            if(current) {
                if(ok) kui_music_select_cached((unsigned)wanted);
                music_requested=ok?-1:next_after_missing((unsigned)wanted);
            }
            mutex_unlock(&lock);
        }
    } else if(status.enabled) {
        for(unsigned index=0;index<KUI_MUSIC_TRACKS;index++) {
            unsigned bit=1u<<index;
            if((status.cached_mask|music_cache_attempted)&bit) continue;
            (void)kui_games_storage_release();
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
    kui_sd_set_params(KUI_STORAGE_AUTO,true);
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
    else if((active_app>=41 && active_app<=43) || active_app==87 || active_app==88) maintenance_status=*status;
    else if(active_app>=84 && active_app<=86) vmu_image_snapshot.status=*status;
    else if(active_app==89 || active_app==90) vmu_bulk_snapshot.status=*status;
    else if(active_app==91 || active_app==92) vmu_import_snapshot.status=*status;
    else if(active_app==93 || active_app==101) vmu_collection_snapshot.status=*status;
    else if(active_app==94 || active_app==95) vmu_collection_copy_snapshot.status=*status;
    else if(active_app==98) vmu_wizard_snapshot.status=*status;
    else if(active_app==24) player_status=*status;
    else if(active_app==31) vmu_backups.status=*status;
    else vmu_snapshot.status=*status;
    mutex_unlock(&lock);
}
/* Global launch preference only. A write finishes its small durable record
 * even if Stop arrives during publication, then the view reports what the
 * card actually confirms. No running game or VMU image is changed here. */
static void vmu_emulation_operation(bool save) {
    bool enabled=true,ok=false,stopped=false,write_unconfirmed=false;
    char note[128]={0};
    snprintf(note,sizeof(note),"Cannot read the VMU emulation setting; refresh to retry.");
    if(kui_games_storage_open(kui_log)==KUI_GAMES_STORAGE_OK) {
        ok=kui_vmu_emulation_settings_load(&enabled,kui_log);
        if(ok && save) {
            if(kui_cancelled()) {
                stopped=true;snprintf(note,sizeof(note),"Setting change stopped; saved choice retained.");
            } else {
                ok=kui_vmu_emulation_settings_save(vmu_emulation_requested,kui_log);
                if(ok) enabled=vmu_emulation_requested;
                else {
                    write_unconfirmed=true;
                    ok=kui_vmu_emulation_settings_load(&enabled,kui_log);
                    snprintf(note,sizeof(note),ok?"Write was not confirmed; current saved setting reloaded.":
                        "Setting change was not confirmed; refresh before trying again.");
                }
            }
        }
        if(!kui_games_storage_release()) {
            ok=false;snprintf(note,sizeof(note),"Could not release storage; refresh to confirm the setting.");
        } else if(ok && !stopped && !write_unconfirmed && (!save || enabled==vmu_emulation_requested))
            snprintf(note,sizeof(note),"Emulated VMU %s. Applies on the next supported native launch.",enabled?"On":"Off");
    }
    mutex_lock(&lock);
    vmu_emulation_enabled=enabled;vmu_emulation_ok=ok;vmu_emulation_stopped=stopped;
    memcpy(vmu_emulation_note,note,sizeof(vmu_emulation_note));
    vmu_emulation_result=vmu_emulation_request;++vmu_emulation_generation;
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
        "Clock update not confirmed. Set the time in the Dreamcast BIOS.":
        !valid?"Clock unavailable. Set a valid local date and time.":
        write?"Console clock and BIOS timestamp updated.":"Local console time; no timezone conversion.");
    mutex_unlock(&lock);
    if(write) kui_log("Clock edit: %s",written?"RTC, system time and BIOS timestamp confirmed":
        "not confirmed; use the Dreamcast BIOS clock editor");
    if(valid) kui_log("Clock now: %04u-%02u-%02u %02u:%02u:%02u local",
        (unsigned)now.year,(unsigned)now.month,(unsigned)now.day,
        (unsigned)now.hour,(unsigned)now.minute,(unsigned)now.second);
}
static uint64_t storage_test_now(void *ctx) {(void)ctx;return timer_us_gettime64();}
static bool storage_test_cancelled(void *ctx) {(void)ctx;return kui_cancelled();}
static void storage_test_errors(void *ctx,struct kui_storage_errors *out) {
    (void)ctx;kui_storage_errors_get(out);
}
static void storage_test_publish(void *ctx,const struct kui_storage_test_progress *progress) {
    (void)ctx;
    mutex_lock(&lock);storage_test_progress=*progress;mutex_unlock(&lock);
}
/* Only the existing filesystem worker executes tests or accesses result files.
 * Keep the larger snapshots out of the worker stack. None of these operations
 * consult bench.cfg or redirect I/O to a device other than the selected one. */
static void storage_test_operation(unsigned action) {
    static struct kui_storage_test_result result;
    static struct kui_storage_test_history history;
    struct kui_music_status music={0};
    bool resume_music=false;
    result=(struct kui_storage_test_result){0};
    history=(struct kui_storage_test_history){0};
    /* Profiling is scoped to the engine call below, never connection,
     * History/report writes, normal runtime I/O or the separate game reader. */
    kui_sci_sd_profile_timer(NULL,NULL);
    if(action==65) {
        result.request=storage_test_pending;
        snprintf(result.metadata.build,sizeof(result.metadata.build),"%.15s",KUI_BUILD_ID);
        result.metadata.transport=kui_storage_selected();
        result.metadata.ui_hz=2;
        struct kui_datetime clock;
        if(kui_clock_now(&clock)) (void)kui_clock_to_seconds(&clock,&result.metadata.local_seconds);
        kui_music_status_copy(&music);resume_music=music.playing;
        if(resume_music) kui_music_pause();
        kui_storage_errors_reset();
    }
    kui_sd_set_params(KUI_STORAGE_AUTO,true);
    bool connected=kui_sd_connect();
    if(connected) {
        if(action==65) {
            struct kui_storage_test_metadata metadata=result.metadata;
            metadata.transport=kui_storage_active();
            const struct kui_storage_test_ops ops={NULL,storage_test_now,storage_test_cancelled,
                storage_test_publish,storage_test_errors,kui_log};
            struct kui_sci_sd_stats dma_before, dma_after;
            kui_sci_sd_stats_get(&dma_before);
            if(metadata.transport==KUI_STORAGE_SCI)
                kui_sci_sd_profile_timer(storage_test_now,NULL);
            kui_storage_test_run(&storage_test_pending,&metadata,&ops,&result);
            /* Engine returns here for Pass, Stop and every failure. Disable
             * before the final snapshot and all persistence/history I/O. */
            kui_sci_sd_profile_timer(NULL,NULL);
            if(metadata.transport==KUI_STORAGE_SCI) {
                kui_sci_sd_stats_get(&dma_after);
                uint32_t reads=dma_after.rx_blocks-dma_before.rx_blocks;
                uint32_t writes=dma_after.tx_blocks-dma_before.tx_blocks;
                uint32_t polled=dma_after.polled_blocks-dma_before.polled_blocks;
                uint32_t failures=dma_after.failures-dma_before.failures;
                result.sci_profile=(struct kui_storage_test_sci_profile){
                    .present=true,.rx_dma_blocks=reads,.tx_dma_blocks=writes,
                    .polled_blocks=polled,.dma_failures=failures,
                    .profiled_rx_blocks=dma_after.profiled_rx_blocks-dma_before.profiled_rx_blocks,
                    .profiled_tx_blocks=dma_after.profiled_tx_blocks-dma_before.profiled_tx_blocks,
                    .rx_setup_us=dma_after.rx_setup_us-dma_before.rx_setup_us,
                    .rx_transfer_us=dma_after.rx_transfer_us-dma_before.rx_transfer_us,
                    .rx_check_us=dma_after.rx_check_us-dma_before.rx_check_us,
                    .tx_setup_us=dma_after.tx_setup_us-dma_before.tx_setup_us,
                    .tx_transfer_us=dma_after.tx_transfer_us-dma_before.tx_transfer_us
                };
                kui_log("SCI test DMA: read=%lu write=%lu polled=%lu failures=%lu",
                    (unsigned long)reads,(unsigned long)writes,
                    (unsigned long)polled,(unsigned long)failures);
                const struct kui_storage_test_sci_profile *profile=&result.sci_profile;
                kui_log("SCI profile RX %lu blocks: setup=%llu transfer=%llu check=%llu us",
                    (unsigned long)profile->profiled_rx_blocks,
                    (unsigned long long)profile->rx_setup_us,
                    (unsigned long long)profile->rx_transfer_us,
                    (unsigned long long)profile->rx_check_us);
                kui_log("SCI profile TX %lu blocks: setup=%llu transfer=%llu us",
                    (unsigned long)profile->profiled_tx_blocks,
                    (unsigned long long)profile->tx_setup_us,
                    (unsigned long long)profile->tx_transfer_us);
                /* Reuse the versioned result's existing message field so
                 * JSON and History retain proof of DMA use without changing
                 * the binary format or hiding a failure's original cause. */
                if(result.outcome==KUI_STORAGE_TEST_PASSED)
                    snprintf(result.message,sizeof(result.message),
                        "Verified after remount; DMA R/W %lu/%lu; PIO %lu; faults %lu",
                        (unsigned long)reads,(unsigned long)writes,
                        (unsigned long)polled,(unsigned long)failures);
            }
            /* Persist a small final result even after Stop. The sample loop
             * has already finished; do not lose its outcome to a latched B. */
            if(result.id && !kui_storage_test_save(&result,kui_log))
                kui_log("Storage test result could not be saved; keep the results screen.");
            kui_log("Storage test %u: %s; result %s",(unsigned)result.id,
                kui_storage_test_outcome_name(result.outcome),result.saved?"saved":"not saved");
        }
        bool baseline_ok=true;
        if(action==67) baseline_ok=kui_storage_test_set_baseline(storage_baseline_pending,kui_log);
        (void)kui_storage_test_load_history(&history,kui_log);
        if(action==67) snprintf(history.message,sizeof(history.message),"%s",baseline_ok?
            "Baseline saved. Matching tests show comparisons.":"Baseline save could not be confirmed. See Diagnostics log.");
        kui_sd_disconnect();
    } else {
        snprintf(history.message,sizeof(history.message),"Storage unavailable; no device switch was attempted.");
        if(action==65) {
            result.outcome=KUI_STORAGE_TEST_FAILED;
            snprintf(result.failure_phase,sizeof(result.failure_phase),"connect");
            snprintf(result.message,sizeof(result.message),"Selected storage unavailable. Check Diagnostics log.");
            kui_storage_errors_get(&result.errors);
        }
    }
    if(resume_music) kui_music_resume();
    mutex_lock(&lock);
    if(action==65) {storage_test_result=result;++storage_test_generation;}
    storage_test_history=history;++storage_history_generation;
    mutex_unlock(&lock);
}
/* Independent experiment report: never added to the soak history/baseline. */
static bool sci_video_quiet_begin(uint32_t *generation,const char *notice) {
    mutex_lock(&lock);
    *generation=kui_sci_video_quiet_request(&sci_video_quiet);
    snprintf(sci_async_status.message,sizeof(sci_async_status.message),"%s",notice);
    mutex_unlock(&lock);
    uint64_t started=timer_ms_gettime64();
    for(unsigned polls=0;polls<200;polls++) {
        mutex_lock(&lock);
        bool cancelled=cancel_requested;
        bool acknowledged=sci_video_quiet.requested && sci_video_quiet.generation==*generation &&
            sci_video_quiet.acknowledged;
        mutex_unlock(&lock);
        if(cancelled) break;
        if(acknowledged) return true;
        if(timer_ms_gettime64()-started>=2000u) break;
        thd_sleep(10);
    }
    mutex_lock(&lock);
    kui_sci_video_quiet_withdraw(&sci_video_quiet,*generation);
    mutex_unlock(&lock);
    return false;
}
static void sci_video_quiet_end(uint32_t generation,struct kui_sd_async_result *result) {
    mutex_lock(&lock);
    result->video_quiet_requested=true;
    if(sci_video_quiet.generation==generation) {
        result->video_quiet_acknowledged=sci_video_quiet.acknowledged;
        result->video_sq_drained=sci_video_quiet.acknowledged;
        result->video_frames_during=sci_video_quiet.acknowledged?
            sci_video_quiet.total_frames-sci_video_quiet.first_frame:0;
        result->video_redraws_skipped=sci_video_quiet.skipped;
        kui_sci_video_quiet_withdraw(&sci_video_quiet,generation);
        snprintf(sci_async_status.message,sizeof(sci_async_status.message),"Reads ended; preparing the result...");
    }
    mutex_unlock(&lock);
}
static bool sci_async_integrity(const struct kui_sd_async_result *result) {
    const struct kui_sci_async_probe_result *r=&result->probe;
    /* Speed: the async pass must reproduce the ordinary pass's CRC32. */
    if(result->speed)
        return r->crc_ok && r->guards_ok && result->speed_match && result->speed_blocks &&
            r->fast.passed==result->speed_blocks && r->fast.attempted==r->fast.passed;
    if(!r->crc_ok || !r->baseline_ok || !r->guards_ok) return false;
    if(result->sustained) {
        if(!result->baseline_verified || !r->baseline_checked || result->baseline_sectors<2 ||
           result->baseline_sectors>KUI_SD_ASYNC_STRESS_SECTORS ||
           result->distinct_lbas_verified!=result->baseline_sectors) return false;
        uint64_t reads=0;
        for(unsigned i=0;i<result->baseline_sectors;i++) {
            if(!result->baseline_reads[i]) return false;
            reads+=result->baseline_reads[i];
        }
        return result->read_cycles && reads==result->read_cycles && result->read_cycles==r->fast.passed &&
            r->fast.attempted==r->fast.passed;
    }
    return
        r->slow.passed==KUI_SCI_ASYNC_SLOW_TRIALS && r->slow.attempted==r->slow.passed &&
        r->fast.passed==KUI_SCI_ASYNC_FAST_TRIALS && r->fast.attempted==r->fast.passed;
}
static bool sci_async_completion(const struct kui_sd_async_result *result) {
    const struct kui_sci_async_probe_result *r=&result->probe;
    return sci_async_integrity(result) && r->slow.dma_irqs==r->slow.passed && r->fast.dma_irqs==r->fast.passed;
}
static bool sci_async_passed(const struct kui_sd_async_result *result) {
    const struct kui_sci_async_probe_result *r=&result->probe;
    bool common=r->status==KUI_SCI_ASYNC_OK && result->baseline_verified && result->recovery_verified &&
        r->safe_restored && r->handlers_restored && r->registers_restored &&
        sci_async_completion(result);
    /* Speed polls with no other work, so it has no CPU-overlap evidence. The
     * CMD18 measurements must run and every block they validated must equal
     * the async pass's copy; what they find about the card is reported, not
     * a failure. */
    if(result->speed) return common && result->video_quiet_acknowledged && result->video_sq_drained &&
        !result->video_frames_during && result->stream_ran && result->resume_ran &&
        result->stream_match && result->resume_match && result->speed_stream_match;
    /* With the screen left running, retried overruns are expected evidence,
     * not a failure: every read must still verify. */
    return common && r->fast.overlap_batches &&
        (!result->sustained || (result->duration_complete && !result->iteration_limit &&
            (result->screen_active || (result->video_quiet_requested && result->video_quiet_acknowledged &&
            result->video_sq_drained && !result->video_frames_during)) &&
            result->stress_elapsed_us>=result->target_us && result->target_us>=KUI_SD_ASYNC_STRESS_US &&
            result->heartbeat.installed && result->heartbeat.restored && !result->heartbeat.ownership_lost &&
            result->heartbeat.dma_ticks));
}
static uint32_t sci_async_kib_s(uint64_t blocks,uint64_t us) {
    return us?(uint32_t)(blocks*UINT64_C(500000)/us):0;
}
static bool sci_async_fault_json(char *out,size_t size,const struct kui_sci_async_fault *f) {
    int n=snprintf(out,size,
        "{\"valid\":%lu,\"event\":%lu,\"ssr\":%lu,\"scr\":%lu,\"dmaor\":%lu,"
        "\"sar\":%lu,\"dar\":%lu,\"tcr\":%lu,\"chcr\":%lu,\"lba\":%lu,"
        "\"start_address\":%lu,\"context_valid\":%lu,\"pc\":%lu,\"sr\":%lu,\"request_elapsed_us\":%llu}",
        (unsigned long)f->valid,(unsigned long)f->event,(unsigned long)f->ssr,(unsigned long)f->scr,
        (unsigned long)f->dmaor,(unsigned long)f->sar,(unsigned long)f->dar,(unsigned long)f->tcr,
        (unsigned long)f->chcr,(unsigned long)f->lba,(unsigned long)f->start_address,
        (unsigned long)f->context_valid,(unsigned long)f->pc,(unsigned long)f->sr,
        (unsigned long long)f->request_elapsed_us);
    return n>=0 && (size_t)n<size;
}
/* Per-block cycle of the resume measurement: receive, reset, check and the
 * token search after reselection. */
static uint64_t sci_async_resume_cycle_us(const struct kui_sci_async_resume *m) {
    return m->receive_us+m->reset_us+m->check_us+m->token_us;
}
static bool sci_async_stream_json(char *out,size_t size,const struct kui_sd_async_result *result) {
    const struct kui_sci_async_stream *c=&result->stream;
    char gaps[KUI_SCI_ASYNC_STREAM_GAPS*7u+4u];
    size_t used=0;gaps[used++]='[';
    unsigned listed=c->gaps<KUI_SCI_ASYNC_STREAM_GAPS?c->gaps:KUI_SCI_ASYNC_STREAM_GAPS;
    for(unsigned i=0;i<listed;i++) {
        int n=snprintf(gaps+used,sizeof(gaps)-used,"%s%u",i?",":"",(unsigned)c->gap[i]);
        if(n<0 || (size_t)n>=sizeof(gaps)-used) return false;
        used+=(size_t)n;
    }
    if(used+2>sizeof(gaps)) return false;
    gaps[used++]=']';gaps[used]=0;
    int n=snprintf(out,size,
        "{\"ran\":%s,\"status\":%u,\"status_name\":\"%s\",\"lba\":%lu,\"bytes\":%lu,\"received\":%lu,"
        "\"complete\":%lu,\"command_response\":%lu,\"first_token_bytes\":%lu,\"first_token_us\":%llu,"
        "\"blocks\":%lu,\"crc_errors\":%lu,\"token_errors\":%lu,\"last_token\":%lu,"
        "\"gaps\":%lu,\"gap_min\":%lu,\"gap_max\":%lu,\"gap_total\":%lu,\"gap_list\":%s,"
        "\"end_ssr\":%lu,\"end_count\":%lu,\"reset_state\":%lu,\"stop_response\":%lu,\"stop_busy_bytes\":%lu,"
        "\"capture_us\":%llu,\"masked_us\":%llu,\"stop_us\":%llu,\"elapsed_us\":%llu,\"match\":%s}",
        result->stream_ran?"true":"false",(unsigned)c->status,kui_sci_async_status_name(c->status),
        (unsigned long)c->lba,(unsigned long)c->bytes,(unsigned long)c->received,(unsigned long)c->complete,
        (unsigned long)c->command_response,(unsigned long)c->first_token_bytes,(unsigned long long)c->first_token_us,
        (unsigned long)c->blocks,(unsigned long)c->crc_errors,(unsigned long)c->token_errors,(unsigned long)c->last_token,
        (unsigned long)c->gaps,(unsigned long)c->gap_min,(unsigned long)c->gap_max,(unsigned long)c->gap_total,gaps,
        (unsigned long)c->end_ssr,(unsigned long)c->end_count,(unsigned long)c->reset_state,
        (unsigned long)c->stop_response,(unsigned long)c->stop_busy_bytes,
        (unsigned long long)c->capture_us,(unsigned long long)c->masked_us,(unsigned long long)c->stop_us,
        (unsigned long long)c->elapsed_us,result->stream_match?"true":"false");
    return n>=0 && (size_t)n<size;
}
static bool sci_async_resume_json(char *out,size_t size,const struct kui_sd_async_result *result) {
    const struct kui_sci_async_resume *m=&result->resume;
    int n=snprintf(out,size,
        "{\"ran\":%s,\"status\":%u,\"status_name\":\"%s\",\"lba\":%lu,\"requested\":%lu,\"blocks\":%lu,"
        "\"crc_errors\":%lu,\"token_errors\":%lu,\"guard_errors\":%lu,\"missing_tail_bytes\":%lu,"
        "\"command_response\":%lu,"
        "\"first_token_bytes\":%lu,\"first_token_us\":%llu,\"last_token\":%lu,"
        "\"token_bytes\":%lu,\"max_token_bytes\":%lu,\"token_us\":%llu,\"max_token_us\":%llu,"
        "\"receive_us\":%llu,\"reset_us\":%llu,\"check_us\":%llu,\"max_masked_us\":%llu,"
        "\"reset_state\":%lu,\"stop_response\":%lu,\"stop_busy_bytes\":%lu,\"stop_us\":%llu,"
        "\"elapsed_us\":%llu,\"cycle_kib_s\":%lu,\"match\":%s}",
        result->resume_ran?"true":"false",(unsigned)m->status,kui_sci_async_status_name(m->status),
        (unsigned long)m->lba,(unsigned long)m->requested,(unsigned long)m->blocks,
        (unsigned long)m->crc_errors,(unsigned long)m->token_errors,(unsigned long)m->guard_errors,
        (unsigned long)m->missing_tail_bytes,(unsigned long)m->command_response,(unsigned long)m->first_token_bytes,(unsigned long long)m->first_token_us,
        (unsigned long)m->last_token,(unsigned long)m->token_bytes,(unsigned long)m->max_token_bytes,
        (unsigned long long)m->token_us,(unsigned long long)m->max_token_us,
        (unsigned long long)m->receive_us,(unsigned long long)m->reset_us,(unsigned long long)m->check_us,
        (unsigned long long)m->max_masked_us,(unsigned long)m->reset_state,(unsigned long)m->stop_response,
        (unsigned long)m->stop_busy_bytes,(unsigned long long)m->stop_us,(unsigned long long)m->elapsed_us,
        (unsigned long)sci_async_kib_s(m->blocks,sci_async_resume_cycle_us(m)),
        result->resume_match?"true":"false");
    return n>=0 && (size_t)n<size;
}
static const char *sci_async_pause_site(uint32_t site) {
    static const char *const names[]={"none","lease","release","module reset","DMA start",
        "DMA poll","open","long call","between polls"};
    return site<sizeof(names)/sizeof(names[0])?names[site]:"unknown";
}
static const char *sci_async_pause_stage(uint32_t stage) {
    static const char *const names[]={"slow","CMD17","CMD18 test","stream","none"};
    return stage<sizeof(names)/sizeof(names[0])?names[stage]:"unknown";
}
static bool __attribute__((format(printf,4,5))) sci_async_append(char *out,size_t size,
        size_t *used,const char *format,...) {
    va_list args;va_start(args,format);
    int n=vsnprintf(out+*used,size-*used,format,args);
    va_end(args);
    if(n<0 || (size_t)n>=size-*used) return false;
    *used+=(size_t)n;return true;
}
static bool sci_async_reset_time_json(char *out,size_t size,size_t *used,
        const struct kui_sci_async_reset_time *t) {
    return sci_async_append(out,size,used,
        "{\"stage\":%lu,\"ns\":%lu,\"steps_ns\":[%lu,%lu,%lu,%lu,%lu,%lu]}",
        (unsigned long)t->stage,(unsigned long)t->ns,
        (unsigned long)t->steps_ns[0],(unsigned long)t->steps_ns[1],(unsigned long)t->steps_ns[2],
        (unsigned long)t->steps_ns[3],(unsigned long)t->steps_ns[4],(unsigned long)t->steps_ns[5]);
}
/* Pause log and the slowest module reset. */
static bool sci_async_pause_json(char *out,size_t size,const struct kui_sd_async_result *result) {
    const struct kui_sci_async_probe_result *r=&result->probe;
    size_t used=0;
    if(!sci_async_append(out,size,&used,"\"pause_count\":%lu,\n  \"pauses\":[",
        (unsigned long)r->pause_count)) return false;
    for(uint32_t i=0;i<r->pause_count && i<KUI_SCI_ASYNC_PAUSES;i++) {
        const struct kui_sci_async_pause *e=&r->pauses[i];
        if(!sci_async_append(out,size,&used,"%s{\"site\":%lu,\"site_name\":\"%s\",\"stage\":%lu,"
            "\"us\":%lu,\"at_us\":%llu}",i?",":"",(unsigned long)e->site,sci_async_pause_site(e->site),
            (unsigned long)e->stage,(unsigned long)e->us,(unsigned long long)e->at_us)) return false;
    }
    return sci_async_append(out,size,&used,"],\n  \"reset_worst\":") &&
        sci_async_reset_time_json(out,size,&used,&r->reset_worst);
}
static bool sci_async_save(const struct kui_sd_async_result *result,char path[96]) {
    static char stage[4][2048],json[16384],baseline[3][192],fault[640],first_overrun[640],speed[768];
    static char stream[1536],resume[1024],pauses[1024];
    const struct kui_sci_async_probe_result *r=&result->probe;
    const struct kui_sci_async_stage *stages[]={&r->slow,&r->fast,&r->cmd18,&r->streaming};
    path[0]=0;
    for(unsigned i=0;i<4;i++) {
        const struct kui_sci_async_stage *s=stages[i];
        int n=snprintf(stage[i],sizeof(stage[i]),
            "{\"clock_hz\":%lu,\"attempted\":%lu,\"passed\":%lu,"
            "\"dma_irqs\":%lu,\"sci_error_irqs\":%lu,\"unexpected_rx_irqs\":%lu,"
            "\"trailing_overruns\":%lu,\"premature_errors\":%lu,\"timeouts\":%lu,"
            "\"overlap_batches\":%lu,\"overlap_iterations\":%lu,\"work_checksum\":%lu,"
            "\"last_remaining\":%lu,\"last_chcr\":%lu,\"last_ssr\":%lu,"
            "\"last_phase\":%u,\"last_phase_name\":\"%s\",\"dma_started\":%lu,"
            "\"command_response\":%lu,\"last_token\":%lu,\"snapshot_ssr\":%lu,\"snapshot_sptr\":%lu,"
            "\"handoff_checks\":%lu,\"handoff_retries\":%lu,\"handoff_failures\":%lu,"
            "\"handoff_ssr\":%lu,\"handoff_scr\":%lu,\"handoff_sptr\":%lu,\"bus_faults\":%lu,"
            "\"module_reset_attempts\":%lu,\"module_resets\":%lu,\"module_reset_failures\":%lu,"
            "\"module_reset_state\":%lu,\"module_stb_before\":%lu,\"module_stb_stopped\":%lu,\"module_stb_after\":%lu,"
            "\"framing_step\":%u,\"framing_name\":\"%s\",\"framing_index\":%lu,"
            "\"bus_fault_valid\":%lu,\"bus_wait_flag\":%lu,\"bus_fault_ssr\":%lu,\"bus_fault_scr\":%lu,"
            "\"bus_fault_smr\":%lu,\"bus_fault_brr\":%lu,\"bus_fault_scmr\":%lu,\"bus_fault_sptr\":%lu,"
            "\"bus_fault_pdtr\":%lu,\"bus_fault_polls\":%lu,"
            "\"elapsed_us\":%llu,\"receive_us\":%llu,\"max_receive_us\":%llu,"
            "\"payload_overruns\":%lu,\"overrun_retries\":%lu,\"undrained_overruns\":%lu,"
            "\"token_bytes\":%lu,\"max_token_bytes\":%lu,\"framing_us\":%llu,\"finish_us\":%llu,"
            "\"token_us\":%llu,\"max_token_us\":%llu,\"stream_restarts\":%lu,\"missing_tail_bytes\":%lu}",
            (unsigned long)s->clock_hz,(unsigned long)s->attempted,(unsigned long)s->passed,
            (unsigned long)s->dma_irqs,(unsigned long)s->sci_error_irqs,(unsigned long)s->unexpected_rx_irqs,
            (unsigned long)s->trailing_overruns,(unsigned long)s->premature_errors,(unsigned long)s->timeouts,
            (unsigned long)s->overlap_batches,(unsigned long)s->overlap_iterations,(unsigned long)s->work_checksum,
            (unsigned long)s->last_remaining,(unsigned long)s->last_chcr,(unsigned long)s->last_ssr,
            (unsigned)s->last_phase,kui_sci_async_phase_name(s->last_phase),(unsigned long)s->dma_started,
            (unsigned long)s->command_response,(unsigned long)s->last_token,
            (unsigned long)s->snapshot_ssr,(unsigned long)s->snapshot_sptr,
            (unsigned long)s->handoff_checks,(unsigned long)s->handoff_retries,(unsigned long)s->handoff_failures,
            (unsigned long)s->handoff_ssr,(unsigned long)s->handoff_scr,(unsigned long)s->handoff_sptr,
            (unsigned long)s->bus_faults,
            (unsigned long)s->module_reset_attempts,(unsigned long)s->module_resets,
            (unsigned long)s->module_reset_failures,(unsigned long)s->module_reset_state,
            (unsigned long)s->module_stb_before,(unsigned long)s->module_stb_stopped,(unsigned long)s->module_stb_after,
            (unsigned)s->framing_step,kui_sci_async_framing_name(s->framing_step),(unsigned long)s->framing_index,
            (unsigned long)s->bus_fault_valid,(unsigned long)s->bus_wait_flag,
            (unsigned long)s->bus_fault_ssr,(unsigned long)s->bus_fault_scr,
            (unsigned long)s->bus_fault_smr,(unsigned long)s->bus_fault_brr,
            (unsigned long)s->bus_fault_scmr,(unsigned long)s->bus_fault_sptr,
            (unsigned long)s->bus_fault_pdtr,(unsigned long)s->bus_fault_polls,
            (unsigned long long)s->elapsed_us,(unsigned long long)s->receive_us,(unsigned long long)s->max_receive_us,
            (unsigned long)s->payload_overruns,(unsigned long)s->overrun_retries,(unsigned long)s->undrained_overruns,
            (unsigned long)s->token_bytes,(unsigned long)s->max_token_bytes,
            (unsigned long long)s->framing_us,(unsigned long long)s->finish_us,
            (unsigned long long)s->token_us,(unsigned long long)s->max_token_us,
            (unsigned long)s->stream_restarts,(unsigned long)s->missing_tail_bytes);
        if(n<0 || (size_t)n>=sizeof(stage[i])) return false;
    }
    const uint32_t *baseline_values[]={result->baseline_lbas,result->baseline_crcs,result->baseline_reads};
    unsigned baseline_count=result->baseline_sectors;
    if(baseline_count>KUI_SD_ASYNC_STRESS_SECTORS) return false;
    for(unsigned a=0;a<3;a++) {
        size_t used=0;
        baseline[a][used++]='[';
        for(unsigned i=0;i<baseline_count;i++) {
            int written=snprintf(baseline[a]+used,sizeof(baseline[a])-used,"%s%lu",i?",":"",
                (unsigned long)baseline_values[a][i]);
            if(written<0 || (size_t)written>=sizeof(baseline[a])-used) return false;
            used+=(size_t)written;
        }
        if(used+2>sizeof(baseline[a])) return false;
        baseline[a][used++]=']';baseline[a][used]=0;
    }
    if(!sci_async_fault_json(fault,sizeof(fault),&r->fault) ||
       !sci_async_fault_json(first_overrun,sizeof(first_overrun),&r->first_overrun)) return false;
    int speed_size=snprintf(speed,sizeof(speed),
        "{\"file_found\":%s,\"lba\":%lu,\"blocks\":%lu,\"async_blocks\":%lu,"
        "\"normal_us\":%llu,\"async_us\":%llu,\"normal_kib_s\":%lu,\"async_kib_s\":%lu,"
        "\"normal_crc32\":%lu,\"async_crc32\":%lu,\"match\":%s,"
        "\"stream_blocks\":%lu,\"stream_us\":%llu,\"stream_kib_s\":%lu,\"stream_crc32\":%lu,"
        "\"stream_match\":%s}",
        result->speed_file_found?"true":"false",(unsigned long)result->speed_lba,
        (unsigned long)result->speed_blocks,(unsigned long)result->speed_async_blocks,
        (unsigned long long)result->speed_normal_us,(unsigned long long)result->speed_async_us,
        (unsigned long)sci_async_kib_s(result->speed_blocks,result->speed_normal_us),
        (unsigned long)sci_async_kib_s(result->speed_async_blocks,result->speed_async_us),
        (unsigned long)result->speed_normal_crc,(unsigned long)result->speed_async_crc,
        result->speed_match?"true":"false",(unsigned long)result->speed_stream_blocks,
        (unsigned long long)result->speed_stream_us,
        (unsigned long)sci_async_kib_s(result->speed_stream_blocks,result->speed_stream_us),
        (unsigned long)result->speed_stream_crc,result->speed_stream_match?"true":"false");
    if(speed_size<0 || (size_t)speed_size>=sizeof(speed)) return false;
    if(!sci_async_stream_json(stream,sizeof(stream),result) ||
       !sci_async_resume_json(resume,sizeof(resume),result) ||
       !sci_async_pause_json(pauses,sizeof(pauses),result)) return false;
    struct kui_datetime clock;int64_t local_seconds=0;
    if(kui_clock_now(&clock)) (void)kui_clock_to_seconds(&clock,&local_seconds);
    int n=snprintf(json,sizeof(json),
        "{\n  \"schema\":2,\n  \"kind\":\"SCI async probe\",\n  \"build\":\"%.15s\",\n"
        "  \"mode\":\"%s\",\n  \"target_us\":%llu,\n  \"stress_elapsed_us\":%llu,\n"
        "  \"duration_complete\":%s,\n  \"iteration_limit\":%s,\n"
        "  \"video_quiet_requested\":%s,\n  \"video_quiet_acknowledged\":%s,\n  \"video_sq_drained\":%s,\n"
        "  \"video_frames_during\":%lu,\n  \"video_redraws_skipped\":%lu,\n"
        "  \"baseline_sectors\":%lu,\n  \"distinct_lbas_verified\":%lu,\n  \"distinct_payloads\":%lu,\n"
        "  \"baseline_lbas\":%s,\n  \"baseline_crcs\":%s,\n  \"baseline_reads\":%s,\n"
        "  \"read_cycles\":%lu,\n  \"poll_calls\":%lu,\n  \"worker_yields\":%lu,\n"
        "  \"read_elapsed_us\":%llu,\n  \"max_read_us\":%llu,\n"
        "  \"heartbeat_installed\":%s,\n  \"heartbeat_restored\":%s,\n  \"heartbeat_ownership_lost\":%s,\n"
        "  \"heartbeat_ticks\":%lu,\n  \"heartbeat_dma_ticks\":%lu,\n  \"heartbeat_max_gap_us\":%llu,\n"
        "  \"max_open_us\":%llu,\n  \"max_begin_us\":%llu,\n  \"max_poll_us\":%llu,\n"
        "  \"max_finish_us\":%llu,\n  \"max_cancel_us\":%llu,\n  \"max_close_us\":%llu,\n  \"max_call_us\":%llu,\n"
        "  \"local_seconds\":%lld,\n  \"transport\":\"SCI\",\n  \"lba\":%lu,\n"
        "  \"status\":%u,\n  \"status_name\":\"%s\",\n  \"started\":%s,\n"
        "  \"operation_status\":%u,\n  \"operation_status_name\":\"%s\",\n"
        "  \"dma_quarantined\":%s,\n  \"foreign_dma\":%s,\n"
        "  \"baseline_verified\":%s,\n  \"baseline_crc32\":%lu,\n"
        "  \"safe_restored\":%s,\n  \"guards_ok\":%s,\n  \"crc_ok\":%s,\n  \"baseline_ok\":%s,\n"
        "  \"baseline_checked\":%s,\n"
        "  \"handlers_restored\":%s,\n  \"registers_restored\":%s,\n"
        "  \"recovery_verified\":%s,\n  \"recovery_reinitialized\":%s,\n  \"elapsed_us\":%llu,\n"
        "  \"recovery_phase\":%u,\n  \"recovery_phase_name\":\"%s\",\n"
        "  \"recovery_result\":%u,\n  \"recovery_result_name\":\"%s\",\n"
        "  \"recovery_command_valid\":%s,\n  \"recovery_command\":%u,\n  \"recovery_response\":%u,\n"
        "  \"recovery_bus_healthy\":%s,\n  \"recovery_data_match\":%s,\n"
        "  \"timer_irq_instrumented\":%s,\n"
        "  \"max_irq_masked_us\":%llu,\n  \"max_irq_masked_site\":%lu,\n  \"max_irq_masked_stage\":%lu,\n"
        "  \"max_irq_handler_us\":%llu,\n  %s,\n"
        "  \"probe_passed\":%s,\n  \"read_integrity_verified\":%s,\n  \"completion_irq_verified\":%s,\n"
        "  \"cpu_overlap_observed\":%s,\n  \"fault\":%s,\n  \"first_overrun\":%s,\n"
        "  \"screen_redraws\":%s,\n  \"speed\":%s,\n  \"stream\":%s,\n  \"resume\":%s,\n"
        "  \"slow\":%s,\n  \"fast\":%s,\n  \"cmd18\":%s,\n  \"streaming\":%s\n}\n",
        KUI_BUILD_ID,result->speed?"speed":result->sustained?(result->screen_active?"sustained-screen":"sustained"):"quick",
        (unsigned long long)result->target_us,(unsigned long long)result->stress_elapsed_us,
        result->duration_complete?"true":"false",result->iteration_limit?"true":"false",
        result->video_quiet_requested?"true":"false",result->video_quiet_acknowledged?"true":"false",
        result->video_sq_drained?"true":"false",(unsigned long)result->video_frames_during,
        (unsigned long)result->video_redraws_skipped,
        (unsigned long)result->baseline_sectors,(unsigned long)result->distinct_lbas_verified,(unsigned long)result->distinct_payloads,
        baseline[0],baseline[1],baseline[2],
        (unsigned long)result->read_cycles,(unsigned long)result->poll_calls,(unsigned long)result->worker_yields,
        (unsigned long long)result->read_elapsed_us,(unsigned long long)result->max_read_us,
        result->heartbeat.installed?"true":"false",result->heartbeat.restored?"true":"false",
        result->heartbeat.ownership_lost?"true":"false",(unsigned long)result->heartbeat.total_ticks,
        (unsigned long)result->heartbeat.dma_ticks,(unsigned long long)result->heartbeat.max_gap_us,
        (unsigned long long)r->max_open_us,(unsigned long long)r->max_begin_us,(unsigned long long)r->max_poll_us,
        (unsigned long long)r->max_finish_us,(unsigned long long)r->max_cancel_us,
        (unsigned long long)r->max_close_us,(unsigned long long)r->max_call_us,
        (long long)local_seconds,(unsigned long)r->lba,(unsigned)r->status,
        kui_sci_async_status_name(r->status),r->started?"true":"false",
        (unsigned)r->operation_status,kui_sci_async_status_name(r->operation_status),
        r->dma_quarantined?"true":"false",r->foreign_dma?"true":"false",
        result->baseline_verified?"true":"false",(unsigned long)result->baseline_crc32,
        r->safe_restored?"true":"false",r->guards_ok?"true":"false",r->crc_ok?"true":"false",r->baseline_ok?"true":"false",
        r->baseline_checked?"true":"false",
        r->handlers_restored?"true":"false",r->registers_restored?"true":"false",
        result->recovery_verified?"true":"false",result->recovery_reinitialized?"true":"false",
        (unsigned long long)r->elapsed_us,
        (unsigned)result->recovery_phase,kui_sd_async_recovery_name(result->recovery_phase),
        (unsigned)result->recovery_result,kui_loader_sd_result_name(result->recovery_result),
        result->recovery_command_valid?"true":"false",(unsigned)result->recovery_command,(unsigned)result->recovery_response,
        result->recovery_bus_healthy?"true":"false",result->recovery_data_match?"true":"false",
        r->timer_irq_instrumented?"true":"false",
        (unsigned long long)r->max_irq_masked_us,(unsigned long)r->max_irq_masked_site,
        (unsigned long)r->max_irq_masked_stage,(unsigned long long)r->max_irq_handler_us,pauses,
        sci_async_passed(result)?"true":"false",sci_async_integrity(result)?"true":"false",sci_async_completion(result)?"true":"false",
        r->fast.overlap_batches?"true":"false",fault,first_overrun,
        result->screen_active?"true":"false",speed,stream,resume,stage[0],stage[1],stage[2],stage[3]);
    if(n<0 || (size_t)n>=sizeof(json)) return false;
    FATFS fs;bool saved=false;
    if(!kui_mount(&fs,kui_log)) return false;
    const char *parents[]={"0:/KUI","0:/KUI/tests"};
    for(unsigned i=0;i<2;i++) {
        FRESULT f=f_mkdir(parents[i]);
        if(f!=FR_OK && f!=FR_EXIST) goto done;
    }
    char dir[64],temp[96];bool created=false;
    for(unsigned id=1;id<=9999;id++) {
        snprintf(dir,sizeof(dir),"0:/KUI/tests/sci-async-%04u",id);
        FRESULT f=f_mkdir(dir);
        if(f==FR_OK) {created=true;break;}
        if(f!=FR_EXIST) goto done;
    }
    if(!created) goto done;
    snprintf(temp,sizeof(temp),"%s/report.tmp",dir);
    snprintf(path,96,"%s/sci-async-probe.json",dir);
    saved=kui_write_new_file(temp,json,(size_t)n,kui_log) && f_rename(temp,path)==FR_OK;
done:
    if(f_mount(NULL,"0:",0)!=FR_OK) saved=false;
    if(!saved) path[0]=0;
    return saved;
}
enum {SCI_ASYNC_QUICK, SCI_ASYNC_STRESS, SCI_ASYNC_SCREEN, SCI_ASYNC_SPEED};
static void sci_async_operation(unsigned mode) {
    static struct kui_sd_async_result result;
    struct kui_app_status status={.complete=true};
    struct kui_music_status music={0};char path[96]={0};
    kui_music_status_copy(&music);
    if(music.playing) kui_music_pause();
    result=(struct kui_sd_async_result){0};
    bool connected=false,saved=false;
    if(kui_storage_selected()!=KUI_STORAGE_SCI) {
        snprintf(status.message,sizeof(status.message),"Requires SCI storage. Current device was not changed.");
        status.errors=1;
    } else {
        kui_sd_set_params(KUI_STORAGE_SCI,true);
        connected=kui_sd_connect();
        if(connected) {
            /* Stress X and Speed pause shell redraws; stress Y leaves them
             * running so receive overruns under screen traffic are counted. */
            bool quiet=mode==SCI_ASYNC_STRESS || mode==SCI_ASYNC_SPEED;
            if(quiet) {
                uint32_t quiet_generation;
                if(sci_video_quiet_begin(&quiet_generation,mode==SCI_ASYNC_SPEED?
                       "Speed comparison: display updates paused.":"60-second stress: display updates paused.")) {
                    if(mode==SCI_ASYNC_SPEED) kui_sd_async_speed(&result,storage_test_cancelled,NULL);
                    else kui_sd_async_stress(&result,storage_test_cancelled,NULL);
                } else {
                    result.sustained=mode==SCI_ASYNC_STRESS;result.speed=mode==SCI_ASYNC_SPEED;
                    result.probe.status=kui_cancelled()?KUI_SCI_ASYNC_CANCELLED:KUI_SCI_ASYNC_BUSY;
                    snprintf(result.message,sizeof(result.message),"%s",result.probe.status==KUI_SCI_ASYNC_CANCELLED?
                        "Stopped before reads started.":"Display pause not acknowledged; test did not start.");
                }
                /* The wrapper zeroes result and returns only after closing the
                 * reader and attempting safe recovery. Preserve the handshake
                 * separately until then, including all early failure paths. */
                sci_video_quiet_end(quiet_generation,&result);
            }
            else if(mode==SCI_ASYNC_SCREEN) {
                kui_sd_async_stress(&result,storage_test_cancelled,NULL);
                result.screen_active=true;
            }
            else kui_sd_async_probe(&result,storage_test_cancelled,NULL);
            if(result.recovery_verified) saved=sci_async_save(&result,path);
            const struct kui_sci_async_probe_result *r=&result.probe;
            bool heartbeat_fault=result.sustained && (result.heartbeat.ownership_lost ||
                (result.heartbeat.installed && !result.heartbeat.restored));
            bool video_fault=quiet && r->status==KUI_SCI_ASYNC_OK &&
                (!result.video_quiet_acknowledged || !result.video_sq_drained || result.video_frames_during);
            status.stopped=r->status==KUI_SCI_ASYNC_CANCELLED;
            status.passed=sci_async_passed(&result);
            status.errors=result.restart_required || heartbeat_fault || video_fault || (r->status!=KUI_SCI_ASYNC_OK && !status.stopped);
            snprintf(status.message,sizeof(status.message),"%s",result.message);
            if(video_fault)
                snprintf(status.message,sizeof(status.message),"Display pause not verified; stress proof is incomplete.");
            else if(r->status==KUI_SCI_ASYNC_OK && result.recovery_verified && !heartbeat_fault)
                snprintf(status.message,sizeof(status.message),"%s",status.passed?
                    result.speed?"Speed comparison finished; both readers returned the same data.":
                    result.screen_active?"60 seconds with screen updates: every read verified.":
                    result.sustained?"60-second stress passed with timer IRQs during DMA.":
                    "Verified reads with CPU work during DMA.":
                    result.speed?"Speed comparison finished; data match not confirmed.":
                    "Read test finished; async proof is incomplete.");
            status.line_count=8;
            snprintf(status.lines[0],KUI_APP_LINE_CAP,"Slow: %lu/%lu reads verified; %lu DMA interrupts",
                (unsigned long)r->slow.passed,(unsigned long)r->slow.attempted,(unsigned long)r->slow.dma_irqs);
            snprintf(status.lines[1],KUI_APP_LINE_CAP,"Fast: %lu/%lu reads verified; %lu DMA interrupts",
                (unsigned long)r->fast.passed,(unsigned long)r->fast.attempted,(unsigned long)r->fast.dma_irqs);
            snprintf(status.lines[2],KUI_APP_LINE_CAP,"CPU overlap batches: slow %lu / fast %lu",
                (unsigned long)r->slow.overlap_batches,(unsigned long)r->fast.overlap_batches);
            snprintf(status.lines[3],KUI_APP_LINE_CAP,"CRC %s  Data %s  Buffer guards %s",
                r->crc_ok?"OK":"unconfirmed",r->baseline_ok?"OK":"unconfirmed",r->guards_ok?"OK":"unconfirmed");
            snprintf(status.lines[4],KUI_APP_LINE_CAP,"Normal read recovery: %s",result.recovery_verified?"verified":
                result.recovery_phase==KUI_SD_ASYNC_RECOVERY_NONE?
                    (result.restart_required?"not attempted; restart required":"not attempted"):
                    (result.restart_required?"FAILED - restart required":"not verified"));
            snprintf(status.lines[5],KUI_APP_LINE_CAP,"%s",saved?"Saved independent report:":"Report not saved; photograph this result.");
            snprintf(status.lines[6],KUI_APP_LINE_CAP,"%.79s",path[0]?path+2:"No saved path");
            snprintf(status.lines[7],KUI_APP_LINE_CAP,"Normal game reads are unchanged by this experiment.");
            if(result.sustained) {
                char api_line[160];
                snprintf(status.lines[0],KUI_APP_LINE_CAP,"60s stress: %lu reads; %lu/%lu sectors verified",
                    (unsigned long)result.read_cycles,(unsigned long)result.distinct_lbas_verified,
                    (unsigned long)result.baseline_sectors);
                snprintf(status.lines[1],KUI_APP_LINE_CAP,"CRC/data/guards %s; DMA interrupts %lu",
                    sci_async_integrity(&result)?"OK":"unconfirmed",(unsigned long)r->fast.dma_irqs);
                snprintf(status.lines[2],KUI_APP_LINE_CAP,"CPU batches %lu; timer during DMA %lu",
                    (unsigned long)r->fast.overlap_batches,(unsigned long)result.heartbeat.dma_ticks);
                snprintf(api_line,sizeof(api_line),"Overruns %lu, retried %lu, not idle %lu; max read %llu us",
                    (unsigned long)r->fast.payload_overruns,(unsigned long)r->fast.overrun_retries,
                    (unsigned long)r->fast.undrained_overruns,(unsigned long long)result.max_read_us);
                snprintf(status.lines[3],KUI_APP_LINE_CAP,"%.79s",api_line);
                if(result.screen_active)
                    snprintf(status.lines[7],KUI_APP_LINE_CAP,"Screen kept updating; max poll %llu us",
                        (unsigned long long)r->max_poll_us);
                else snprintf(status.lines[7],KUI_APP_LINE_CAP,"Display quiet %s; frames %lu; skipped %lu",
                    result.video_quiet_acknowledged && result.video_sq_drained?"verified":"unconfirmed",
                    (unsigned long)result.video_frames_during,(unsigned long)result.video_redraws_skipped);
            }
            if(result.speed) {
                char line[160],stream_rate[24];
                snprintf(status.lines[0],KUI_APP_LINE_CAP,"Speed: %lu blocks from LBA %lu (%s)",
                    (unsigned long)result.speed_blocks,(unsigned long)result.speed_lba,
                    result.speed_file_found?"runtime.kui":"data area");
                snprintf(status.lines[1],KUI_APP_LINE_CAP,"Ordinary reader (CMD18 runs): %lu KiB/s",
                    (unsigned long)sci_async_kib_s(result.speed_blocks,result.speed_normal_us));
                if(result.speed_stream_blocks)
                    snprintf(stream_rate,sizeof(stream_rate),"%lu KiB/s",
                        (unsigned long)sci_async_kib_s(result.speed_stream_blocks,result.speed_stream_us));
                else snprintf(stream_rate,sizeof(stream_rate),"not run");
                snprintf(status.lines[2],KUI_APP_LINE_CAP,"Async: CMD17 per block %lu KiB/s, CMD18 stream %s",
                    (unsigned long)sci_async_kib_s(result.speed_async_blocks,result.speed_async_us),stream_rate);
                const struct kui_sci_async_stage *t=&r->streaming;
                uint64_t k=t->passed?t->passed:1u;
                snprintf(line,sizeof(line),"Stream us/block: receive %lu, gap %lu, check %lu; restarts %lu",
                    (unsigned long)(t->receive_us/k),(unsigned long)(t->framing_us/k),
                    (unsigned long)(t->finish_us/k),(unsigned long)t->stream_restarts);
                snprintf(status.lines[3],KUI_APP_LINE_CAP,"%.79s",line);
                snprintf(line,sizeof(line),"Longest interrupt-masked window %llu us (%s%s%s)",
                    (unsigned long long)r->max_irq_masked_us,sci_async_pause_site(r->max_irq_masked_site),
                    r->max_irq_masked_stage<4u?", ":"",
                    r->max_irq_masked_stage<4u?sci_async_pause_stage(r->max_irq_masked_stage):"");
                snprintf(status.lines[4],KUI_APP_LINE_CAP,"%.79s",line);
                const struct kui_sci_async_pause *worst=NULL;
                for(uint32_t i=0;i<r->pause_count && i<KUI_SCI_ASYNC_PAUSES;i++)
                    if(!worst || r->pauses[i].us>worst->us) worst=&r->pauses[i];
                if(worst) snprintf(line,sizeof(line),"Pauses %lu, longest %lu us (%s, %s)",
                    (unsigned long)r->pause_count,(unsigned long)worst->us,
                    sci_async_pause_site(worst->site),sci_async_pause_stage(worst->stage));
                else snprintf(line,sizeof(line),"No pauses: no masked window over 0.5 ms, no call over 1.5 ms");
                snprintf(status.lines[5],KUI_APP_LINE_CAP,"%.79s",line);
                if(!saved) snprintf(status.lines[6],KUI_APP_LINE_CAP,"Report not saved; photograph this result.");
                snprintf(status.lines[7],KUI_APP_LINE_CAP,"Data match %s/%s/%s/%s; recovery %s",
                    result.speed_match?"yes":"NO",
                    !result.speed_stream_blocks?"-":result.speed_stream_match?"yes":"NO",
                    !result.stream_ran?"-":result.stream_match?"yes":"NO",
                    !result.resume_ran?"-":result.resume_match?"yes":"NO",
                    result.recovery_verified?"verified":"not verified");
            }
            if(!status.passed && r->status!=KUI_SCI_ASYNC_OK) {
                /* A failed stream or CMD18 measurement keeps its own counters. */
                const struct kui_sci_async_stage *stage=r->streaming.attempted>r->streaming.passed?&r->streaming:
                    r->cmd18.attempted>r->cmd18.passed?&r->cmd18:r->fast.attempted?&r->fast:&r->slow;
                if(stage->handoff_checks || stage->bus_faults || stage->bus_fault_valid) {
                    snprintf(status.lines[0],KUI_APP_LINE_CAP,"Slow %lu/%lu IRQ%lu  Fast %lu/%lu IRQ%lu",
                        (unsigned long)r->slow.passed,(unsigned long)r->slow.attempted,(unsigned long)r->slow.dma_irqs,
                        (unsigned long)r->fast.passed,(unsigned long)r->fast.attempted,(unsigned long)r->fast.dma_irqs);
                    snprintf(status.lines[1],KUI_APP_LINE_CAP,"Handoff checks %lu retries %lu failures %lu faults %lu",
                        (unsigned long)stage->handoff_checks,(unsigned long)stage->handoff_retries,
                        (unsigned long)stage->handoff_failures,(unsigned long)stage->bus_faults);
                    snprintf(status.lines[2],KUI_APP_LINE_CAP,"Handoff SSR%02lX SCR%02lX SPTR%02lX; CPU batches %lu",
                        (unsigned long)(stage->handoff_ssr&255u),(unsigned long)(stage->handoff_scr&255u),
                        (unsigned long)(stage->handoff_sptr&255u),(unsigned long)stage->overlap_batches);
                }
                if(stage->bus_faults || stage->bus_fault_valid) {
                    snprintf(status.lines[1],KUI_APP_LINE_CAP,"Framing %.18s index %lu; faults %lu",
                        kui_sci_async_framing_name(stage->framing_step),(unsigned long)stage->framing_index,
                        (unsigned long)stage->bus_faults);
                    if(stage->bus_fault_valid)
                        snprintf(status.lines[2],KUI_APP_LINE_CAP,"Before stop: wait%02lX SSR%02lX SCR%02lX SPTR%02lX",
                            (unsigned long)(stage->bus_wait_flag&255u),(unsigned long)(stage->bus_fault_ssr&255u),
                            (unsigned long)(stage->bus_fault_scr&255u),(unsigned long)(stage->bus_fault_sptr&255u));
                    else snprintf(status.lines[2],KUI_APP_LINE_CAP,"First bus fault snapshot unavailable.");
                }
                if(stage->module_reset_attempts || stage->module_reset_failures) {
                    char reset_line[160];
                    if(result.sustained)
                        snprintf(reset_line,sizeof(reset_line),"Reads%lu IRQ%lu Reset%lu/%lu F%lu S%03lX",
                            (unsigned long)result.read_cycles,(unsigned long)stage->dma_irqs,
                            (unsigned long)stage->module_resets,(unsigned long)stage->module_reset_attempts,
                            (unsigned long)stage->module_reset_failures,(unsigned long)stage->module_reset_state);
                    else snprintf(reset_line,sizeof(reset_line),"Slow%lu/%lu IRQ%lu Fast%lu/%lu IRQ%lu Reset%lu/%lu fail%lu state%03lX",
                            (unsigned long)r->slow.passed,(unsigned long)r->slow.attempted,(unsigned long)r->slow.dma_irqs,
                            (unsigned long)r->fast.passed,(unsigned long)r->fast.attempted,(unsigned long)r->fast.dma_irqs,
                            (unsigned long)stage->module_resets,(unsigned long)stage->module_reset_attempts,
                            (unsigned long)stage->module_reset_failures,(unsigned long)stage->module_reset_state);
                    snprintf(status.lines[0],KUI_APP_LINE_CAP,"%.79s",reset_line);
                    if(stage->bus_fault_valid)
                        snprintf(status.lines[2],KUI_APP_LINE_CAP,"Before stop: wait%02lX SSR%02lX SCR%02lX SPTR%02lX STB%02lX/%02lX/%02lX",
                            (unsigned long)(stage->bus_wait_flag&255u),(unsigned long)(stage->bus_fault_ssr&255u),
                            (unsigned long)(stage->bus_fault_scr&255u),(unsigned long)(stage->bus_fault_sptr&255u),
                            (unsigned long)(stage->module_stb_before&255u),(unsigned long)(stage->module_stb_stopped&255u),
                            (unsigned long)(stage->module_stb_after&255u));
                    else if(stage->bus_faults)
                        snprintf(status.lines[2],KUI_APP_LINE_CAP,"No bus snapshot; STB%02lX/%02lX/%02lX",
                            (unsigned long)(stage->module_stb_before&255u),(unsigned long)(stage->module_stb_stopped&255u),
                            (unsigned long)(stage->module_stb_after&255u));
                    else snprintf(status.lines[2],KUI_APP_LINE_CAP,"Handoff SSR%02lX SCR%02lX SPTR%02lX STB%02lX/%02lX/%02lX",
                        (unsigned long)(stage->handoff_ssr&255u),(unsigned long)(stage->handoff_scr&255u),
                        (unsigned long)(stage->handoff_sptr&255u),(unsigned long)(stage->module_stb_before&255u),
                        (unsigned long)(stage->module_stb_stopped&255u),(unsigned long)(stage->module_stb_after&255u));
                }
                if(stage->dma_started)
                    snprintf(status.lines[3],KUI_APP_LINE_CAP,"DMA left %lu CHCR%08lX ERI%lu RXI%lu",
                        (unsigned long)stage->last_remaining,(unsigned long)stage->last_chcr,
                        (unsigned long)stage->sci_error_irqs,(unsigned long)stage->unexpected_rx_irqs);
                snprintf(status.lines[7],KUI_APP_LINE_CAP,"%.9s: DMA%lu SSR%02lX SPTR%02lX R1%02lX TK%02lX",
                    kui_sci_async_phase_name(stage->last_phase),(unsigned long)stage->dma_started,
                    (unsigned long)(stage->snapshot_ssr&255u),(unsigned long)(stage->snapshot_sptr&255u),
                    (unsigned long)(stage->command_response&255u),(unsigned long)(stage->last_token&255u));
                if(!saved && result.recovery_phase!=KUI_SD_ASYNC_RECOVERY_NONE) {
                    const char *detail=result.recovery_result!=KUI_LOADER_SD_OK?
                        kui_loader_sd_result_name(result.recovery_result):!result.recovery_bus_healthy?"bus fault":
                        result.recovery_phase==KUI_SD_ASYNC_RECOVERY_READ && !result.recovery_data_match?"data mismatch":"OK";
                    if(result.recovery_command_valid)
                        snprintf(status.lines[6],KUI_APP_LINE_CAP,"Recover %s: %.24s; CMD%u R1%02X",
                            kui_sd_async_recovery_name(result.recovery_phase),detail,
                            (unsigned)result.recovery_command,(unsigned)result.recovery_response);
                    else snprintf(status.lines[6],KUI_APP_LINE_CAP,"Recover %s: %.24s; no command",
                            kui_sd_async_recovery_name(result.recovery_phase),detail);
                }
                if(result.sustained) {
                    char run_line[160];
                    uint32_t lba=r->fault.valid?r->fault.lba:r->lba;
                    snprintf(run_line,sizeof(run_line),"Elapsed %llu.%03llu s; reads %lu; LBA %lu",
                        (unsigned long long)(result.stress_elapsed_us/1000000u),
                        (unsigned long long)((result.stress_elapsed_us/1000u)%1000u),
                        (unsigned long)result.read_cycles,(unsigned long)lba);
                    snprintf(status.lines[0],KUI_APP_LINE_CAP,"%.79s",run_line);
                    /* Keep framing/reset failures intact. A pure receive/DMA
                     * failure instead needs the active-window and pre-stop
                     * evidence on screen, since quarantine prevents a file. */
                    if(r->fault.valid && !stage->bus_faults && !stage->handoff_failures && !stage->module_reset_failures) {
                        const struct kui_sci_async_fault *f=&r->fault;
                        snprintf(status.lines[1],KUI_APP_LINE_CAP,"Timer%lu DMA%lu Quiet%s SQ%s frames%lu",
                            (unsigned long)result.heartbeat.total_ticks,(unsigned long)result.heartbeat.dma_ticks,
                            result.video_quiet_acknowledged?"ACK":"NO",result.video_sq_drained?"OK":"NO",
                            (unsigned long)result.video_frames_during);
                        snprintf(status.lines[2],KUI_APP_LINE_CAP,"Pre SSR%02lX SCR%02lX OR%08lX event%03lX",
                            (unsigned long)(f->ssr&255u),(unsigned long)(f->scr&255u),
                            (unsigned long)f->dmaor,(unsigned long)f->event);
                        snprintf(status.lines[3],KUI_APP_LINE_CAP,"DMA left %lu CHCR%08lX ERI%lu RXI%lu",
                            (unsigned long)f->tcr,(unsigned long)f->chcr,
                            (unsigned long)stage->sci_error_irqs,(unsigned long)stage->unexpected_rx_irqs);
                        if(!saved && result.recovery_phase==KUI_SD_ASYNC_RECOVERY_NONE) {
                            if(f->context_valid)
                                snprintf(status.lines[6],KUI_APP_LINE_CAP,"PC%08lX SR%08lX request %llu us",
                                    (unsigned long)f->pc,(unsigned long)f->sr,(unsigned long long)f->request_elapsed_us);
                            else snprintf(status.lines[6],KUI_APP_LINE_CAP,"No IRQ context; request %llu us",
                                (unsigned long long)f->request_elapsed_us);
                        }
                    }
                }
            }
            kui_log("SCI async probe: %s; report %s",result.message,saved?path:"not saved");
            kui_sd_disconnect();
        } else {
            snprintf(status.message,sizeof(status.message),"SCI unavailable; probe did not start. See Diagnostics.");
            status.errors=1;
        }
    }
    kui_sd_set_params(KUI_STORAGE_AUTO,true);
    if(music.playing && !result.restart_required) kui_music_resume();
    mutex_lock(&lock);sci_async_status=status;mutex_unlock(&lock);
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
    if(status->storage_unstable || status->sampled || status->guard_cleared) {
        unsigned line=view.line_count<KUI_APP_LINES?view.line_count++:KUI_APP_LINES-1;
        snprintf(view.lines[line],KUI_APP_LINE_CAP,"%s Repeats %lu; changed chunks %lu",
            status->storage_unstable?"Repair blocked.":status->guard_cleared?"Repair allowed.":"Samples agree.",
            (unsigned long)status->sampled,(unsigned long)status->unstable_chunks);
    }
    mutex_lock(&lock);scan_status=view;mutex_unlock(&lock);
}
static void scan_operation(void) {
    struct kui_scan_status result={.result=KUI_SCAN_FAILED};
    snprintf(result.message,sizeof(result.message),"Could not connect SD for Advanced CRC scan.");
    const struct kui_scan_ops ops={.cancelled=scan_cancel,.now_ms=scan_now,
        .progress=scan_progress,.log=kui_log};
    kui_sd_set_params(KUI_STORAGE_AUTO,true);
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
static void repair_progress(void *ctx,const struct kui_repair_status *status) {
    (void)ctx;
    struct kui_app_status view={.complete=status->complete,.stopped=status->result==KUI_REPAIR_STOPPED,
        .errors=status->result==KUI_REPAIR_FAILED?1u:0u,.done=status->done,.total=status->total,.line_count=4};
    snprintf(view.message,sizeof(view.message),"%.127s",status->message);
    snprintf(view.lines[0],KUI_APP_LINE_CAP,"%s track %u/%u; FAD %lu",status->resumed?"Resuming":"Repairing",
        status->track,status->tracks,(unsigned long)status->fad);
    snprintf(view.lines[1],KUI_APP_LINE_CAP,"Suspects %lu; repaired %lu; unresolved %lu",
        (unsigned long)status->suspects,(unsigned long)status->repaired,(unsigned long)status->unresolved);
    snprintf(view.lines[2],KUI_APP_LINE_CAP,"%s",status->storage_unstable?
        "Storage readbacks differ; optical repair blocked.":status->audio_unverified?
        "Audio cannot be validated from sector parity.":status->in_place?
        "Repairing original tracks; durable backups retained.":"Original saved dump is read-only.");
    snprintf(view.lines[3],KUI_APP_LINE_CAP,"%.79s",status->job);
    if(status->pass) {
        view.line_count=5;
        if(repair_passes_pending)snprintf(view.lines[4],KUI_APP_LINE_CAP,"Recovery pass %u/%u; B keeps progress",(unsigned)status->pass,repair_passes_pending);
        else snprintf(view.lines[4],KUI_APP_LINE_CAP,"Recovery pass %u; until stopped; B keeps progress",(unsigned)status->pass);
    }
    if(status->alternate_identity) {
        view.line_count=5;
        snprintf(view.lines[4],KUI_APP_LINE_CAP,"Identity: verified saved prefixes and fixed recovery anchors");
    }
    if(status->catalogue_checked) {
        view.line_count=6;
        snprintf(view.lines[4],KUI_APP_LINE_CAP,"%.12s: %.53s",status->catalogue.catalog,kui_known_text(status->catalogue.result));
        snprintf(view.lines[5],KUI_APP_LINE_CAP,"%.68s",status->catalogue.name);
        view.passed=status->complete && status->result==KUI_REPAIR_COMPLETE && status->catalogue.result==KUI_KNOWN_FULL_MATCH;
    }
    mutex_lock(&lock);scan_status=view;mutex_unlock(&lock);
}
static void forensic_progress(void *ctx,const struct kui_forensic_status *status) {
    (void)ctx;
    struct kui_app_status view={.complete=status->complete,.stopped=status->result==KUI_FORENSIC_STOPPED,
        .errors=status->result==KUI_FORENSIC_FAILED?1u:0u,.done=status->done,.total=status->total,.line_count=4};
    snprintf(view.message,sizeof(view.message),"%.127s",status->message);
    snprintf(view.lines[0],KUI_APP_LINE_CAP,"Track %u/%u; suspects %lu",status->track,status->tracks,(unsigned long)status->suspects);
    snprintf(view.lines[1],KUI_APP_LINE_CAP,"Reopened %lu; differing %lu; unsupported %lu",
        (unsigned long)status->sampled,(unsigned long)status->unstable,(unsigned long)status->unsupported);
    snprintf(view.lines[2],KUI_APP_LINE_CAP,"%s",status->limit_reached?
        "Sample limit reached; remaining sectors not sampled.":"Stable samples do not establish a correct dump.");
    snprintf(view.lines[3],KUI_APP_LINE_CAP,"%.79s",status->report);
    mutex_lock(&lock);scan_status=view;mutex_unlock(&lock);
}
static enum kui_read_result recovery_read(void *ctx,uint32_t fad,unsigned sectors,uint8_t *out) {
    enum kui_read_result result=kui_disc_read_raw(ctx,fad,sectors,out);
    mutex_lock(&lock);bool reset=drive_reset_required;mutex_unlock(&lock);
    /* The existing reader uses FATAL for a clean user cancellation too. The
     * recovery engine sees B before another read/write; genuine poisoned drive
     * failures retain priority and never become placeholders. */
    if(result==KUI_READ_FATAL && kui_cancelled() && !reset) return KUI_READ_RETRY;
    return result;
}
static enum kui_read_result salvage_read(void *ctx,uint32_t fad,unsigned sectors,uint8_t *out) {
    enum kui_read_result result=kui_disc_read_salvage(ctx,fad,sectors,out);
    mutex_lock(&lock);bool reset=drive_reset_required;mutex_unlock(&lock);
    /* Salvage permits the drive more time for damaged media, while retaining
     * the same clean-Stop translation and genuine fatal-state priority. */
    if(result==KUI_READ_FATAL && kui_cancelled() && !reset)return KUI_READ_RETRY;
    return result;
}
static bool prepare_recovery_disc(struct kui_capture_plan *plan) {
    struct kui_toc sessions[2];bool is_cd=false;
    if(!(recovery_cd_pending?kui_disc_prepare_capture(sessions,&is_cd):kui_disc_prepare(sessions)))return false;
    if(kui_cancelled())return false;
    if(is_cd)kui_log("Recovery CD scope: user-declared single session; TOC INDEX01 intervals");
    return is_cd?kui_plan_cd_tracks(&sessions[0],plan):kui_plan_tracks(sessions,plan);
}
static void recovery_dashboard_admitted(const char *job) {
    mutex_lock(&lock);
    /* The title is an insertion label only. The caller has already admitted
     * this job through saved anchors; no title comparison selects a job. */
    kui_recovery_dashboard_admit(&recovery_dashboard,job,
        disc_snapshot.state==KUI_DISC_IDENTITY_READY?disc_snapshot.title:NULL);
    mutex_unlock(&lock);
}
static void recovery_dashboard_guard_refresh(const char *job) {
    if(!kui_recovery_dashboard_job(job))return;
    /* This small read-only check runs on the existing worker after admission,
     * while storage is connected. Browsing and drawing perform no card I/O. */
    FATFS fs;bool allowed=false;
    if(kui_mount(&fs,kui_log)) {
        allowed=kui_recovery_guard_check(job,NULL);
        if(f_mount(NULL,"0:",0)!=FR_OK)allowed=false;
    }
    mutex_lock(&lock);
    kui_recovery_dashboard_guard(&recovery_dashboard,job,!allowed);
    mutex_unlock(&lock);
    if(!allowed)salvage_session_forget();
}
static bool salvage_session_card(uint8_t digest[32]) {
    struct kui_vmu_sd_identity card;
    if(!kui_sd_card_identity(&card))return false;
    /* Avoid structure padding and host byte order in the card binding. */
    uint8_t geometry[9];
    for(unsigned i=0;i<8;++i)geometry[i]=(uint8_t)(card.sectors>>(i*8));
    geometry[8]=card.high_capacity?1u:0u;
    struct kui_sha256 hash;kui_sha256_init(&hash);
    kui_sha256_update(&hash,"KUI-SALVAGE-CARD1",17);
    kui_sha256_update(&hash,card.cid,sizeof(card.cid));
    kui_sha256_update(&hash,geometry,sizeof(geometry));
    kui_sha256_digest(&hash,digest);return true;
}
static void repair_operation(bool forensic) {
    kui_sd_set_params(KUI_STORAGE_AUTO,true);
    if(forensic) {
        struct kui_forensic_status result={.result=KUI_FORENSIC_FAILED,.complete=true};
        snprintf(result.message,sizeof(result.message),"Could not connect storage for reread evidence.");
        const struct kui_forensic_ops ops={.cancelled=scan_cancel,.now_ms=scan_now,
            .progress=forensic_progress,.log=kui_log};
        if(kui_sd_connect()) {kui_recovery_forensic(scan_path_pending,&ops,&result);kui_sd_disconnect();}
        result.complete=true;forensic_progress(NULL,&result);
        save_report("auto suspect reread",result.result==KUI_FORENSIC_STABLE?"sampled reads stable":
            result.result==KUI_FORENSIC_UNSTABLE?"sampled reads differ":result.result==KUI_FORENSIC_STOPPED?"stopped":"failed",true);
    } else {
        struct kui_repair_status result={.result=KUI_REPAIR_FAILED,.complete=true};
        snprintf(result.message,sizeof(result.message),"Could not prepare matching disc and storage.");
        struct kui_capture_plan plan;
        const struct kui_repair_options options={.attempts=repair_attempts_pending,.progress=repair_progress,
            .continuous=true,.passes=repair_passes_pending,.in_place=repair_in_place_pending,
            .new_copy=repair_new_copy_pending,
            .allow_saved_identity=repair_saved_identity_pending && !repair_in_place_pending};
        const struct kui_capture_ops ops={.read=recovery_read,.cancelled=scan_cancel,
            .now_ms=scan_now,.log=kui_log,.build=KUI_BUILD_ID,.recover=kui_disc_recover};
        if(prepare_recovery_disc(&plan) && !kui_cancelled() && kui_sd_connect()) {
            kui_repair_copy(scan_path_pending,&plan,&ops,&options,&result);kui_sd_disconnect();
        }
        if(repair_new_copy_pending && result.resume_ready) {
            mutex_lock(&lock);repair_new_copy_created=true;mutex_unlock(&lock);
        }
        result.complete=true;repair_progress(NULL,&result);
        save_report(repair_in_place_pending?"auto in-place repair":"auto repair copy",result.result==KUI_REPAIR_COMPLETE?"repair completed":
            result.result==KUI_REPAIR_UNRESOLVED?"unresolved copy":result.result==KUI_REPAIR_STOPPED?"stopped":"failed",true);
    }
}
static void salvage_progress(void *ctx,const struct kui_salvage_status *status) {
    (void)ctx;
    struct kui_app_status view;
    kui_salvage_display_status(&view,status);
    mutex_lock(&lock);
    if(kui_recovery_dashboard_job(status->job)) {
        /* NEW publishes its admitted folder through progress. Existing jobs
         * have already passed latest() before this callback is installed. */
        kui_recovery_dashboard_admit(&recovery_dashboard,status->job,
            disc_snapshot.state==KUI_DISC_IDENTITY_READY?disc_snapshot.title:NULL);
        /* A partial first pass cannot know how many unread sectors remain;
         * a partially replayed journal must not look like a complete count. */
        if(status->first_pass_complete && status->sd_phase!=KUI_SALVAGE_SD_JOURNAL)
            kui_recovery_dashboard_counts(&recovery_dashboard,status->job,
                status->targets,status->recovered,status->remaining);
    }
    salvage_status=view;mutex_unlock(&lock);
}
static void salvage_check_progress(void *ctx,const struct kui_salvage_check_status *status) {
    const struct kui_salvage_status *receipt=ctx;
    salvage_progress(NULL,receipt);
    mutex_lock(&lock);struct kui_app_status view=salvage_status;mutex_unlock(&lock);
    view.complete=status->complete;
    view.stopped=status->result==KUI_SALVAGE_CHECK_STOPPED;
    view.passed=status->complete && status->catalogue_checked && status->catalogue.result==KUI_KNOWN_FULL_MATCH;
    if(status->total) {view.done=status->done;view.total=status->total;}
    view.line_count=7;
    snprintf(view.message,sizeof(view.message),"%.127s",status->message);
    if(status->verifying_files)
        snprintf(view.lines[0],KUI_APP_LINE_CAP,"Readback track %u/%u; recovered files retained",status->track,status->tracks);
    else
        snprintf(view.lines[0],KUI_APP_LINE_CAP,"Recovered %u tracks; all recorded holes resolved",receipt->tracks);
    snprintf(view.lines[3],KUI_APP_LINE_CAP,"%s",status->saved_verified?
        "Saved bytes: every track byte read back and verified.":status->verifying_files?
        "Saved bytes: full track readback in progress.":status->readback_requested?
        "Saved bytes: full readback did not complete.":"Saved bytes: not read back; recovery CRCs used.");
    if(status->catalogue_checked) {
        snprintf(view.lines[4],KUI_APP_LINE_CAP,"%.12s: %.53s",status->catalogue.catalog,kui_known_text(status->catalogue.result));
        snprintf(view.lines[5],KUI_APP_LINE_CAP,"%.68s",status->catalogue.name[0]?
            status->catalogue.name:"No matching reference title identified.");
    } else {
        snprintf(view.lines[4],KUI_APP_LINE_CAP,"Catalogue: comparison not complete.");
        view.lines[5][0]=0;
    }
    snprintf(view.lines[6],KUI_APP_LINE_CAP,"%.68s",status->report[0]?status->report:receipt->job);
    mutex_lock(&lock);salvage_status=view;mutex_unlock(&lock);
}
static void speed_probe_progress(void *ctx,const struct kui_drive_speed_probe_status *status) {
    (void)ctx;
    static const char *const names[]={"Original","Maximum","1x requested","2x requested","4x requested",
        "6x requested","8x requested","10x requested","12x requested"};
    struct kui_app_status view={.done=status->done,.total=status->total,.line_count=9,
        .complete=status->complete,.stopped=status->stopped,
        .passed=status->complete && !status->failed && !status->budget_expired,
        .errors=status->failed?1u:0u};
    snprintf(view.message,sizeof(view.message),"%s",status->message);
    for(unsigned p=0;p<9;++p) {
        if(!status->tried[p])snprintf(view.lines[p],KUI_APP_LINE_CAP,"%s: pending",names[p]);
        else if(!status->verified[p])snprintf(view.lines[p],KUI_APP_LINE_CAP,"%s: settings not verified; skipped",names[p]);
        else snprintf(view.lines[p],KUI_APP_LINE_CAP,"%s: matched %u/%u regions; read %" PRIu64 " ms",
            names[p],status->matched[p],status->attempted[p],status->read_ms[p]);
    }
    mutex_lock(&lock);salvage_status=view;mutex_unlock(&lock);
}
static void speed_probe_operation(void) {
    struct kui_drive_speed_probe_status result={0};struct kui_capture_plan plan;
    const struct kui_capture_ops ops={.read=salvage_read,.cancelled=scan_cancel,
        .now_ms=scan_now,.log=kui_log,.build=KUI_BUILD_ID};
    kui_disc_timing_reset();
    snprintf(result.message,sizeof(result.message),"Preparing read-only drive speed probe");
    speed_probe_progress(NULL,&result);
    bool ran=false;
    if(prepare_recovery_disc(&plan) && !kui_cancelled() && kui_disc_controls_trial_begin()) {
        kui_disc_timing_phase(NULL,true);
        ran=kui_drive_speed_probe_run(&plan,&ops,kui_disc_controls_speed_select,speed_probe_progress,&result);
        kui_disc_timing_phase(NULL,false);kui_disc_timing_report();
    } else {
        result.stopped=kui_cancelled();result.failed=!result.stopped;
        snprintf(result.message,sizeof(result.message),"%s",result.stopped?"Speed probe stopped":
            "Could not prepare drive controls; see Diagnostics");
    }
    bool restored=kui_disc_controls_end();
    if(!restored) {
        result.failed=true;
        snprintf(result.message,sizeof(result.message),"Drive unavailable. Restart console; recovery jobs unchanged.");
        mutex_lock(&lock);drive_reset_required=true;mutex_unlock(&lock);
    }
    speed_probe_progress(NULL,&result);
    mutex_lock(&lock);salvage_status.complete=true;mutex_unlock(&lock);
    kui_ui_set_hz(KUI_OPT_UI_FULL);
    kui_log("SPEED probe end restored=%u complete=%u stopped=%u failed=%u budget=%u; no recovery jobs opened or changed",
        restored?1u:0u,result.complete?1u:0u,result.stopped?1u:0u,result.failed?1u:0u,result.budget_expired?1u:0u);
    save_report("auto drive speed support probe",!restored?"restart required":result.stopped?"stopped":
        result.budget_expired?"soft budget reached":ran?"speed probe finished":"speed probe failed",true);
}
struct saved_read_evidence {
    unsigned captured,profile;
    bool profile_active;
    struct kui_sci_sd_stats before;
};
static void saved_read_bus_summary(struct saved_read_evidence *e) {
    if(!e->profile_active)return;
    struct kui_sci_sd_stats after;kui_sci_sd_stats_get(&after);
    kui_log("SAVED READ SCI profile=%u DMA_rx_blocks=%" PRIu32 " PIO_block_attempts=%" PRIu32 " bus_failures=%" PRIu32
        "; includes metadata and evidence I/O",e->profile,after.rx_blocks-e->before.rx_blocks,
        after.polled_blocks-e->before.polled_blocks,after.failures-e->before.failures);
    e->profile_active=false;
}
static void saved_read_observe(void *ctx,unsigned profile,uint32_t slot,const char *problem,
    const uint8_t record[KUI_SAVED_READ_RECORD_BYTES]) {
    struct saved_read_evidence *e=ctx;
    if(profile>=KUI_SAVED_READ_PROFILES || (e->captured&(1u<<profile)))return;
    e->captured|=1u<<profile;
    FRESULT r=f_mkdir("0:/KUI/probes");
    if(r!=FR_OK && r!=FR_EXIST) {kui_log("Saved-read evidence directory unavailable");return;}
    char path[128];FIL file;bool opened=false;
    for(unsigned n=0;n<10000;++n) {
        snprintf(path,sizeof(path),"0:/KUI/probes/saved-read-%04u-p%u-s%" PRIu32 ".bin",n,profile,slot);
        r=f_open(&file,path,FA_WRITE|FA_CREATE_NEW);
        if(r==FR_OK) {opened=true;break;}
        if(r!=FR_EXIST)break;
    }
    if(!opened) {kui_log("Saved-read evidence file unavailable; no source file was opened for writing");return;}
    UINT put=0;
    bool ok=f_write(&file,record,KUI_SAVED_READ_RECORD_BYTES,&put)==FR_OK &&
        put==KUI_SAVED_READ_RECORD_BYTES && f_sync(&file)==FR_OK;
    if(f_close(&file)!=FR_OK)ok=false;
    kui_log("Saved-read observation %s: %s; profile=%u slot=%" PRIu32 " problem=%s",
        ok?"saved":"save incomplete",path+2,profile,slot,problem);
}
static void saved_read_progress(void *ctx,const struct kui_saved_read_probe_status *status) {
    struct saved_read_evidence *e=ctx;
    if(e && status->profile<KUI_SAVED_READ_PROFILES && status->tried[status->profile] &&
       kui_storage_active()==KUI_STORAGE_SCI && (!e->profile_active || e->profile!=status->profile)) {
        saved_read_bus_summary(e);
        e->profile=status->profile;kui_sci_sd_stats_get(&e->before);e->profile_active=true;
    }
    struct kui_app_status view={.complete=status->complete,.stopped=status->stopped,
        .passed=status->complete && !status->failed && !status->inconsistent,
        .errors=(status->failed || status->inconsistent)?1u:0u,
        .done=status->done,.total=status->total,.line_count=8};
    snprintf(view.message,sizeof(view.message),"%.127s",status->message);
    for(unsigned step=0;step<KUI_SAVED_READ_PROFILES;++step) {
        unsigned p=kui_saved_read_probe_profile_at_step(step);
        const char *name=kui_saved_read_probe_profile_name(p);
        if(!status->tried[p])snprintf(view.lines[step],KUI_APP_LINE_CAP,"%s: pending",name);
        else snprintf(view.lines[step],KUI_APP_LINE_CAP,"%s: %lu/%lu; bad %lu; %" PRIu64 " ms",
            name,(unsigned long)status->processed[p],(unsigned long)status->records,
            (unsigned long)status->invalids[p],status->read_ms[p]);
    }
    snprintf(view.lines[4],KUI_APP_LINE_CAP,"%.79s",status->job);
    if(status->first_bad_seen)snprintf(view.lines[5],KUI_APP_LINE_CAP,"First bad: profile %u slot %lu; %.38s",
        status->first_bad_profile,(unsigned long)status->first_bad_slot,status->first_problem);
    else snprintf(view.lines[5],KUI_APP_LINE_CAP,"%s",status->budget_expired?
        "Five-minute budget reached; comparison incomplete.":"Comparing saved candidate bytes; no sector repairs.");
    snprintf(view.lines[6],KUI_APP_LINE_CAP,"Bad observations, if any, saved under /KUI/probes/.");
    snprintf(view.lines[7],KUI_APP_LINE_CAP,"Quarantine remains; this test does not authorize repair.");
    mutex_lock(&lock);salvage_status=view;mutex_unlock(&lock);
}
static void saved_read_probe_operation(void) {
    struct kui_saved_read_probe_status result={0};struct saved_read_evidence evidence={0};
    struct kui_capture_plan plan;
    const struct kui_capture_ops discovery={.read=salvage_read,.cancelled=scan_cancel,
        .now_ms=scan_now,.log=kui_log,.build=KUI_BUILD_ID};
    const struct kui_saved_read_probe_ops ops={.ctx=&evidence,.cancelled=scan_cancel,
        .now_ms=scan_now,.progress=saved_read_progress,.log=kui_log,.observe=saved_read_observe};
    snprintf(result.message,sizeof(result.message),"Identifying the matching saved salvage job");
    saved_read_progress(NULL,&result);
    bool ran=false;
    kui_sd_set_params(KUI_STORAGE_AUTO,true);
    if(prepare_recovery_disc(&plan) && !kui_cancelled() && kui_sd_connect()) {
        if(kui_salvage_latest(&plan,&discovery,salvage_path_pending)) {
            recovery_dashboard_admitted(salvage_path_pending);
            ran=kui_saved_read_probe_run(salvage_path_pending,&ops,&result);
            recovery_dashboard_guard_refresh(salvage_path_pending);
        } else {
            result.stopped=kui_cancelled();result.failed=!result.stopped;
            snprintf(result.message,sizeof(result.message),"No matching saved job; see Diagnostics");
        }
        saved_read_bus_summary(&evidence);
        kui_sd_disconnect();
    } else {
        result.stopped=kui_cancelled();result.failed=!result.stopped;
        snprintf(result.message,sizeof(result.message),"Could not prepare the saved-read comparison");
    }
    if(!kui_disc_controls_end()) {
        result.failed=true;
        mutex_lock(&lock);drive_reset_required=true;mutex_unlock(&lock);
    }
    saved_read_progress(NULL,&result);
    mutex_lock(&lock);salvage_status.complete=true;mutex_unlock(&lock);
    kui_ui_set_hz(KUI_OPT_UI_FULL);
    kui_log("Saved-read probe end complete=%u inconsistent=%u failed=%u stopped=%u budget=%u; quarantine unchanged",
        result.complete?1u:0u,result.inconsistent?1u:0u,result.failed?1u:0u,result.stopped?1u:0u,result.budget_expired?1u:0u);
    save_report("auto saved repair read comparison",result.stopped?"stopped":
        result.budget_expired?"comparison budget reached":ran?"candidate observations agree; repair remains blocked":
        result.inconsistent?"saved candidate observations differ; repair remains blocked":"saved-read comparison failed",true);
}
static void salvage_revalidate_progress(void *ctx,const struct kui_salvage_revalidate_status *status) {
    (void)ctx;
    static const char *const phases[]={"Preparing","Header","Journal","Repair records","Track data","Final checks"};
    const char *phase=(unsigned)status->phase<sizeof(phases)/sizeof(phases[0])?phases[status->phase]:"Unknown";
    bool ended=status->complete || status->failed || status->stopped;
    struct kui_app_status view={.complete=ended,.stopped=status->stopped,
        .passed=status->complete && !status->failed && !status->stopped && status->guard_cleared,
        .errors=status->failed?1u:0u,.done=status->bytes_done,.total=status->bytes_total,.line_count=8};
    snprintf(view.message,sizeof(view.message),"%.127s",status->message);
    snprintf(view.lines[0],KUI_APP_LINE_CAP,"Phase: %s; track %u/%u",phase,status->track,status->tracks);
    snprintf(view.lines[1],KUI_APP_LINE_CAP,"Read %" PRIu64 " / %" PRIu64 " MiB; repeated reads included",
        status->bytes_done>>20,status->bytes_total>>20);
    snprintf(view.lines[2],KUI_APP_LINE_CAP,"Targets %lu; saved repairs %lu; candidate records %lu",
        (unsigned long)status->targets,(unsigned long)status->recovered,(unsigned long)status->candidates);
    snprintf(view.lines[3],KUI_APP_LINE_CAP,"%.79s",status->job);
    snprintf(view.lines[4],KUI_APP_LINE_CAP,"Checks journal, repair records and every saved track byte.");
    snprintf(view.lines[5],KUI_APP_LINE_CAP,"%s",status->guard_cleared?
        "Quarantine cleared after full saved-data validation.":
        ended?"Validation incomplete; quarantine was not cleared.":
        "A full job scan can take tens of minutes; B stops safely.");
    snprintf(view.lines[6],KUI_APP_LINE_CAP,"Saved source files are not repaired or rewritten.");
    snprintf(view.lines[7],KUI_APP_LINE_CAP,"%s",status->guard_cleared?
        "Choose Continue recovery when ready.":"No optical recovery starts during this check.");
    mutex_lock(&lock);
    if(status->phase>=KUI_SALVAGE_REVALIDATE_CANDIDATES)
        kui_recovery_dashboard_counts(&recovery_dashboard,status->job,
            status->targets,status->recovered,status->targets-status->recovered);
    if(status->complete && status->guard_cleared && !status->failed && !status->stopped)
        kui_recovery_dashboard_guard(&recovery_dashboard,status->job,false);
    salvage_status=view;mutex_unlock(&lock);
}
static void salvage_revalidate_operation(void) {
    struct kui_salvage_revalidate_status result={0};struct kui_capture_plan plan;
    const struct kui_capture_ops ops={.read=salvage_read,.cancelled=scan_cancel,
        .now_ms=scan_now,.log=kui_log,.build=KUI_BUILD_ID};
    snprintf(result.message,sizeof(result.message),"Identifying the matching saved salvage job");
    salvage_revalidate_progress(NULL,&result);
    bool ran=false;
    kui_sd_set_params(KUI_STORAGE_AUTO,true);
    if(prepare_recovery_disc(&plan) && !kui_cancelled() && kui_sd_connect()) {
        if(kui_salvage_latest(&plan,&ops,salvage_path_pending)) {
            recovery_dashboard_admitted(salvage_path_pending);
            ran=kui_salvage_revalidate(&plan,&ops,salvage_path_pending,salvage_revalidate_progress,&result);
            if(!ran)recovery_dashboard_guard_refresh(salvage_path_pending);
        } else {
            result.stopped=kui_cancelled();result.failed=!result.stopped;
            snprintf(result.message,sizeof(result.message),"No matching saved job; quarantine was not cleared");
        }
        kui_sd_disconnect();
    } else {
        result.stopped=kui_cancelled();result.failed=!result.stopped;
        snprintf(result.message,sizeof(result.message),"Could not prepare full saved-data revalidation");
    }
    bool restored=kui_disc_controls_end();
    if(!restored) {
        result.failed=true;
        snprintf(result.message,sizeof(result.message),"Drive needs restart; see saved-data result below");
        mutex_lock(&lock);drive_reset_required=true;mutex_unlock(&lock);
    }
    salvage_revalidate_progress(NULL,&result);
    kui_ui_set_hz(KUI_OPT_UI_FULL);
    kui_log("Salvage revalidation end complete=%u failed=%u stopped=%u guard_cleared=%u bytes=%" PRIu64 "/%" PRIu64
        "; no optical recovery started",result.complete?1u:0u,result.failed?1u:0u,result.stopped?1u:0u,
        result.guard_cleared?1u:0u,result.bytes_done,result.bytes_total);
    save_report("auto saved salvage revalidation",!restored?"drive restart required; see saved-data result":
        result.stopped?"stopped; quarantine was not cleared":ran?"full saved-data checks passed; recovery idle":
        "saved-data revalidation failed; quarantine was not cleared",true);
}
static void g1_mode_progress(void *ctx,const struct kui_g1_mode_probe_status *status) {
    (void)ctx;
    static const char *const names[]={"Maximum","1x","2x","4x","6x","8x","10x","12x"};
    struct kui_app_status view={.complete=status->complete,.stopped=status->stopped,
        .errors=(status->failed || status->restart_required)?1u:0u,.done=status->done,.total=status->total,.line_count=9};
    snprintf(view.message,sizeof(view.message),"%.127s",status->message);
    for(unsigned p=0;p<KUI_G1_MODE_CHOICES;++p) {
        if(!status->tried[p])snprintf(view.lines[p],KUI_APP_LINE_CAP,"%s: pending",names[p]);
        else if(status->observed_valid[p])snprintf(view.lines[p],KUI_APP_LINE_CAP,"%s: returned code %u; %s; diff %03x",
            names[p],status->observed[p][2],status->verified[p]?"mode verified":"mode differs",status->difference[p]);
        else snprintf(view.lines[p],KUI_APP_LINE_CAP,"%s: %.60s",names[p],kui_g1_probe_result_name(status->attempt[p]));
    }
    snprintf(view.lines[8],KUI_APP_LINE_CAP,"%s",status->restart_required?"Restart console before further drive use.":
        status->restored?"Original settings verified; no physical-speed measurement.":"Mode requests only; no sector reads or saved-job changes.");
    mutex_lock(&lock);salvage_status=view;mutex_unlock(&lock);
}
static void g1_mode_probe_operation(void) {
    struct kui_g1_mode_probe_status result={0};struct kui_capture_plan plan;
    snprintf(result.message,sizeof(result.message),"Preparing the direct G1 mode diagnostic");
    g1_mode_progress(NULL,&result);
    bool ran=false,began=false;
    if(prepare_recovery_disc(&plan) && !kui_cancelled() && (began=kui_disc_g1_probe_begin()))
        ran=kui_g1_mode_probe_native_run(g1_mode_progress,NULL,&result);
    else {
        result.stopped=kui_cancelled();result.failed=!result.stopped;
        snprintf(result.message,sizeof(result.message),"Could not obtain safe direct G1 access; see Diagnostics");
    }
    bool usable=began?kui_disc_g1_probe_end(result.restored && !result.restart_required):kui_disc_controls_end();
    if(!usable) {
        result.failed=result.restart_required=true;
        snprintf(result.message,sizeof(result.message),"G1 mode/ownership uncertain; restart console");
        mutex_lock(&lock);drive_reset_required=true;mutex_unlock(&lock);
    }
    g1_mode_progress(NULL,&result);
    mutex_lock(&lock);salvage_status.complete=true;mutex_unlock(&lock);
    kui_ui_set_hz(KUI_OPT_UI_FULL);
    kui_log("Direct G1 probe end complete=%u failed=%u stopped=%u raw_restored=%u BIOS_verified=%u retained_lease=%u",
        result.complete?1u:0u,result.failed?1u:0u,result.stopped?1u:0u,result.restored?1u:0u,
        usable?1u:0u,result.lease_retained?1u:0u);
    save_report("auto direct G1 settings probe",!usable?"restart required":result.stopped?"stopped":
        ran?"direct mode comparison complete; physical speed unmeasured":"direct mode comparison failed",true);
}
static void salvage_operation(unsigned action) {
    if(salvage_diagnostic_pending==KUI_SALVAGE_DIAGNOSTIC_SAVED_READS) {saved_read_probe_operation();return;}
    if(salvage_diagnostic_pending==KUI_SALVAGE_DIAGNOSTIC_G1_MODE) {g1_mode_probe_operation();return;}
    if(salvage_diagnostic_pending==KUI_SALVAGE_DIAGNOSTIC_REVALIDATE) {salvage_revalidate_operation();return;}
    if(salvage_diagnostic_pending!=KUI_SALVAGE_DIAGNOSTIC_NONE) {
        mutex_lock(&lock);
        salvage_status=(struct kui_app_status){.complete=true,.errors=1};
        snprintf(salvage_status.message,sizeof(salvage_status.message),"Invalid diagnostic request; no salvage started");
        mutex_unlock(&lock);
        kui_log("Unknown salvage diagnostic request; repair was not started");
        save_report("auto salvage diagnostic","invalid diagnostic request",true);return;
    }
    if(salvage_speed_probe_pending) {speed_probe_operation();return;}
    uint64_t operation_started=scan_now(NULL);
    kui_disc_timing_reset();
    struct kui_salvage_status result={.result=KUI_SALVAGE_FAILED};
    struct kui_salvage_check_status checked={0};bool checks_started=false;
    snprintf(result.message,sizeof(result.message),"Could not prepare disc and SD for salvage.");
    struct kui_capture_plan plan;
    bool defer_next_range=salvage_defer_pending;salvage_defer_pending=false;
    uint8_t session_card[32];
    struct kui_salvage_options options={.passes=salvage_passes_pending,
        .job=salvage_path_pending,.progress=salvage_progress,.defer_next_range=defer_next_range,
        .control_trial=salvage_trial_pending,.trial_select=kui_disc_controls_trial_select};
    const struct kui_capture_ops ops={.read=salvage_read,.cancelled=scan_cancel,
        .now_ms=scan_now,.log=kui_log,.build=KUI_BUILD_ID,.recover=kui_disc_recover};
    kui_sd_set_params(KUI_STORAGE_AUTO,true);
    bool controls_ok=false;
    if(prepare_recovery_disc(&plan) && !kui_cancelled()) {
        controls_ok=salvage_trial_pending?kui_disc_controls_trial_begin():
            kui_disc_controls_begin(&salvage_drive_pending);
        if(!controls_ok)snprintf(result.message,sizeof(result.message),
            "Drive controls could not be verified; salvage not started. See Diagnostics.");
    }
    if(controls_ok && !kui_cancelled() && kui_sd_connect()) {
        kui_disc_timing_phase(NULL,true);
        if(action==46 || kui_salvage_latest(&plan,&ops,salvage_path_pending)) {
            if(action!=46)recovery_dashboard_admitted(salvage_path_pending);
            bool ordinary=action==47 && !defer_next_range && !salvage_trial_pending;
            bool card_ok=true;
            if(ordinary && kui_storage_active()==KUI_STORAGE_SCI) {
                card_ok=salvage_session_card(session_card);
                salvage_session_observe_errors();
                if(card_ok) {options.session_epoch=salvage_session.epoch;options.session_card=session_card;}
                else {
                    salvage_session_forget();
                    snprintf(result.message,sizeof(result.message),"Card identity check failed; recovery did not start");
                }
            } else salvage_session_forget();
            if(card_ok) {
                kui_salvage_run(&plan,&ops,&options,(enum kui_salvage_action)(action-46),&result);
                salvage_session.parked=result.session_retained;
            }
        } else snprintf(result.message,sizeof(result.message),"No matching salvage job found; see Diagnostics.");
        kui_disc_timing_phase(NULL,false);
        kui_log("Salvage acquisition/repair wall_ms=%" PRIu64,scan_now(NULL)-operation_started);
        kui_disc_timing_report();
        if(result.result==KUI_SALVAGE_RESOLVED && result.complete) {
            const struct kui_salvage_check_ops check_ops={.ctx=&result,.cancelled=scan_cancel,
                .progress=salvage_check_progress,.log=kui_log};
            checks_started=true;
            kui_salvage_check_complete(&plan,&result,salvage_verify_pending,&check_ops,&checked);
        }
        if(result.job[0])recovery_dashboard_guard_refresh(result.job);
        kui_sd_disconnect();
    }
    /* Restore even after Stop, SD failure, failed mode application or completion
     * checks. The adapter refuses cleanup commands after uncertain ownership. */
    bool controls_restored=kui_disc_controls_end();
    if(!controls_restored) {
        salvage_session_forget();
        snprintf(result.message,sizeof(result.message),
            "Drive unavailable. Restart console; saved progress retained.");
        mutex_lock(&lock);drive_reset_required=true;mutex_unlock(&lock);
    }
    mutex_lock(&lock);bool reset=drive_reset_required;mutex_unlock(&lock);
    if(reset || result.result==KUI_SALVAGE_FAILED || !controls_ok)salvage_session_forget();
    if(reset && result.result==KUI_SALVAGE_FAILED) {
        snprintf(result.message,sizeof(result.message),"%s",result.job[0]?
            "Drive reset required. Restart, then Continue recovery.":
            "Drive reset required. Restart, then retry salvage.");
        kui_log("Salvage: reset required; committed progress retained");
    }
    if(checks_started)salvage_check_progress(&result,&checked);
    else salvage_progress(NULL,&result);
    mutex_lock(&lock);
    salvage_status.complete=true;
    if(result.result==KUI_SALVAGE_FAILED || (checks_started && checked.result==KUI_SALVAGE_CHECK_FAILED)) salvage_status.errors=1;
    if(!controls_restored) {
        salvage_status.errors=1;
        salvage_status.passed=false;
        snprintf(salvage_status.message,sizeof(salvage_status.message),
            "Drive unavailable. Restart console; saved progress retained.");
    }
    mutex_unlock(&lock);
    kui_ui_set_hz(KUI_OPT_UI_FULL);
    const char *outcome=!controls_restored?"drive unavailable; restart required":
        checks_started && checked.result==KUI_SALVAGE_CHECK_STOPPED?"resolved; recovery checks stopped":
        checks_started && checked.result==KUI_SALVAGE_CHECK_FAILED?"resolved; recovery checks failed":
        checks_started && checked.catalogue.result==KUI_KNOWN_FULL_MATCH?
            (checked.saved_verified?"resolved; saved bytes verified; full catalogue match":"resolved; recovery CRC full catalogue match"):
        result.result==KUI_SALVAGE_RESOLVED?
            (checked.saved_verified?"resolved; saved bytes verified; no full catalogue match":"resolved; no full catalogue match"):
        result.result==KUI_SALVAGE_UNRESOLVED && salvage_trial_pending?
            (result.trial_budget_expired?"bounded control trial budget reached; saved repairs retained":
                "bounded control trial finished; unresolved sectors remain"):
        result.result==KUI_SALVAGE_UNRESOLVED?"unresolved holes remain":
        result.result==KUI_SALVAGE_STOPPED?"stopped":"failed";
    save_report(salvage_trial_pending?"auto salvage control trial":"auto salvage",outcome,true);
    salvage_session_observe_errors();
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
    kui_sd_set_params(KUI_STORAGE_AUTO, true);
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
static void games_scan_progress(const struct kui_app_status *status) {
    mutex_lock(&lock);games_scan_status=*status;mutex_unlock(&lock);
}
static void ftp_publish(const struct kui_ftp_status *status) {
    mutex_lock(&lock);ftp_status=*status;ftp_seen=true;mutex_unlock(&lock);
}
static void wifi_publish(const struct kui_wifi_view *view) {
    mutex_lock(&lock);wifi_view=*view;++wifi_generation;mutex_unlock(&lock);
}
static void files_progress(const struct kui_app_status *status) {
    mutex_lock(&lock);files_status=*status;mutex_unlock(&lock);
}
/* A song chosen in the File Manager reports there: its name when it plays. */
static void files_song_result(const struct kui_app_status *result) {
    files_result=*result;
    if(result->passed) {
        const char *slash=strrchr(music_path_pending,'/');
        snprintf(files_result.message,sizeof(files_result.message),"Playing %.100s",slash?slash+1:music_path_pending);
    }
    files_music=false;++files_result_generation;
}
static bool needs_cd_handoff(unsigned action) {
    return action==1 || (action>=4 && action<=7) || action==12 || action==22 ||
        action==24 || action==25 || action==56 || action==57 || action==58 || action==65 || action==79 || action==81 || action==96 || action==97 || (action>=68 && action<=71) || (action>=46 && action<=48);
}
static bool games_page(enum kui_shell_page page) {
    return page==KUI_SHELL_GAMES || page==KUI_SHELL_GAMES_ADVANCED ||
        page==KUI_SHELL_GAMES_DETAIL || page==KUI_SHELL_GAMES_VARIANTS ||
        page==KUI_SHELL_GAMES_PROBE_CONFIRM || page==KUI_SHELL_GAMES_IMAGE_PROBE_CONFIRM ||
        page==KUI_SHELL_GAMES_RETAIL_CONFIRM || page==KUI_SHELL_GAMES_LAUNCH_OPTIONS ||
        page==KUI_SHELL_GAMES_EXTRA_OPTIONS || page==KUI_SHELL_GAMES_TESTS || page==KUI_SHELL_GAMES_INFO ||
        page==KUI_SHELL_GAMES_PATCHES || page==KUI_SHELL_GAMES_CODEC || page==KUI_SHELL_GAMES_CHECK;
}
/* Background preparation of the Games page on screen: its rows' titles, then
 * their box art at the view's size, then the folders and titles of the pages
 * either side, nearest first. One step per idle pass; input, an art request
 * or an action stops it, and it resumes at the same step. */
#define GAMES_PREFETCH_DONE 6u
static struct {char root[KUI_DEST_ROOT_CAP];unsigned page,view,step;bool valid;} games_prefetch;
static uint16_t games_prefetch_pixels[KUI_COVER_PIXELS];
static void games_prefetch_reset(void) {games_prefetch.valid=false;}
static void games_cache_invalidate(void) {
    (void)kui_games_storage_release();
    kui_games_cache_clear();kui_games_covers_cache_clear();games_warm_done=false;
    games_prefetch_reset();
    mutex_lock(&lock);
    games_art_done_valid=false;games_art_result_ready=false;games_titles_ready=false;
    mutex_unlock(&lock);
}
static void games_storage_observe(void) {
    /* This is the selected boot device, read without probing the card. A
     * physical card replacement requires explicit Refresh; no CID is cached
     * by the runtime reader. The worker alone owns both cache lifetimes. */
    unsigned selected=kui_storage_selected();
    if(selected==games_storage_observed) return;
    if(games_storage_observed!=KUI_STORAGE_AUTO) games_cache_invalidate();
    games_storage_observed=selected;
}
static bool games_action_invalidates(unsigned action) {
    /* Read-only identity, music, directory and metadata jobs retain discovery.
     * Writers, transport probes and memory handoffs explicitly invalidate it. */
    return action==2 || action==3 || (action>=4 && action<=9) || action==11 ||
        action==14 || action==17 || action==18 || action==19 || action==21 || action==22 ||
        action==25 || action==30 || action==33 || action==38 || action==40 ||
        (action>=42 && action<=48) || (action>=56 && action<=59) || action==62 ||
        action==64 || action==65 || action==67 || action==79 || action==81 || action==96 || action==97 || action==82 ||
        action==83 || action==85 || action==86 || action==90 || action==92 || action==95 || action==98 || action==100 || (action>=68 && action<=71);
}
static bool games_idle_cancelled(void) {
    mutex_lock(&lock);
    bool stop=busy || pending || video_preview || cancel_requested || games_cache_clear_pending ||
        sci_video_quiet.requested || !startup_finished || splash_active || boot_ready || probe_launch_ready;
    mutex_unlock(&lock);
    return stop;
}
static void games_selection_startup(void) {
    struct kui_games_selection last;kui_games_selection_default(&last);
    kui_sd_set_params(KUI_STORAGE_AUTO,true);
    if(kui_games_storage_open(kui_log)==KUI_GAMES_STORAGE_OK)
        (void)kui_games_storage_done(kui_games_selection_load(&last,kui_log));
    mutex_lock(&lock);
    games_selection_loaded=games_selection_requested=last;++games_selection_generation;
    games_selection_saved=games_selection_request;
    mutex_unlock(&lock);
}
static bool games_selection_store(bool force) {
    if(!force && (kui_cd_audio_owns_drive() || games_idle_cancelled()))return false;
    mutex_lock(&lock);
    unsigned generation=games_selection_request;
    bool wanted=generation!=games_selection_saved && (force || timer_ms_gettime64()>=games_selection_deadline);
    struct kui_games_selection last=games_selection_requested;
    mutex_unlock(&lock);
    if(!wanted)return false;
    salvage_session_forget();
    kui_sd_set_params(KUI_STORAGE_AUTO,true);
    if(kui_games_storage_open(kui_log)==KUI_GAMES_STORAGE_OK)
        (void)kui_games_storage_done(kui_games_selection_save(&last,kui_log));
    /* One failed background save must not retry every frame on read-only media. */
    mutex_lock(&lock);games_selection_saved=generation;mutex_unlock(&lock);
    return true;
}
static bool games_art_cancelled(void) {
    if(games_idle_cancelled()) return true;
    mutex_lock(&lock);
    struct kui_shell_games_art_key current;
    bool stop=!games_art_request_valid || !kui_shell_games_art_key(&shell,&current) ||
        !kui_shell_games_art_same(&games_art_running,&current);
    mutex_unlock(&lock);
    return stop;
}
static bool games_art_idle_work(void) {
    if(kui_cd_audio_owns_drive() || games_idle_cancelled()) return false;
    mutex_lock(&lock);
    bool wanted=games_art_request_valid && !games_art_result_ready &&
        (!games_art_done_valid || !kui_shell_games_art_same(&games_art_requested,&games_art_done));
    if(wanted) {games_art_running=games_art_requested;games_art_running_entry=games_art_entry;}
    mutex_unlock(&lock);
    if(!wanted || games_art_cancelled()) return false;
    kui_sd_set_params(KUI_STORAGE_AUTO,true);
    struct kui_games_cover_result result={0};
    bool ok=kui_games_selected_cover(&games_art_running_entry,kui_games_view_size(games_art_running.view),
        games_cover_staging,&result,kui_log,games_art_cancelled);
    bool stopped=games_art_cancelled();
    if(!ok && !stopped && !result.stopped) games_cache_invalidate();
    mutex_lock(&lock);
    if(!stopped && games_art_request_valid && !busy && !pending &&
       kui_shell_games_art_same(&games_art_requested,&games_art_running)) {
        games_art_result=result;games_art_published=games_art_running;games_art_result_ready=true;
        games_art_done=games_art_running;games_art_done_valid=true;
    }
    mutex_unlock(&lock);
    return true;
}
/* Requires lock; submitting a metadata hint never reserves the foreground
 * action slot or sets busy. A later hint replaces the queued request. */
static void vmu_game_info_request_start(void) {
    if(!shell.games_vmu_loading) return;
    memcpy(vmu_game_info_requested.product,shell.games_vmu_product,sizeof(vmu_game_info_requested.product));
    vmu_game_info_requested.native_route=(shell.games_detail.native_gd || shell.games_detail.native_cd) &&
        !shell.games_detail.windows_ce;
    vmu_game_info_requested.generation=shell.games_vmu_generation;
    vmu_game_info_request_valid=true;
}
/* Requires lock. The running request is a private worker snapshot: opening
 * another game's options cannot change the product being read mid-operation. */
static bool vmu_game_info_current(void) {
    return vmu_game_info_request_valid && shell.games_vmu_loading && games_page(shell.page) &&
        vmu_game_info_running.generation==vmu_game_info_requested.generation &&
        vmu_game_info_running.generation==shell.games_vmu_generation &&
        vmu_game_info_running.native_route==vmu_game_info_requested.native_route &&
        !memcmp(vmu_game_info_running.product,vmu_game_info_requested.product,12u) &&
        !memcmp(vmu_game_info_running.product,shell.games_vmu_product,12u);
}
static bool vmu_game_info_cancelled(void) {
    if(games_idle_cancelled()) return true;
    mutex_lock(&lock);bool stop=!vmu_game_info_current();mutex_unlock(&lock);
    return stop;
}
static bool vmu_game_info_idle_work(void) {
    if(kui_cd_audio_owns_drive() || games_idle_cancelled()) return false;
    mutex_lock(&lock);
    bool wanted=vmu_game_info_request_valid && shell.games_vmu_loading && games_page(shell.page);
    if(wanted) vmu_game_info_running=vmu_game_info_requested;
    mutex_unlock(&lock);
    if(!wanted || vmu_game_info_cancelled()) return false;
    struct kui_vmu_game_info result;
    kui_sd_set_params(KUI_STORAGE_AUTO,true);
    kui_vmu_game_info_run(vmu_game_info_running.product,vmu_game_info_running.native_route,
        &result,kui_log,vmu_game_info_cancelled);
    bool stopped=vmu_game_info_cancelled();
    mutex_lock(&lock);
    if(!stopped && !busy && !pending && vmu_game_info_current()) {
        vmu_game_info_snapshot=result;
        vmu_game_info_request_result=vmu_game_info_running.generation;++vmu_game_info_generation;
        vmu_game_info_request_valid=false;
    }
    mutex_unlock(&lock);
    return true;
}
static bool games_warm_cancelled(void) {
    if(games_idle_cancelled()) return true;
    mutex_lock(&lock);
    bool stop=games_art_request_valid &&
        (!games_art_done_valid || !kui_shell_games_art_same(&games_art_requested,&games_art_done));
    mutex_unlock(&lock);
    return stop;
}
/* Only Games browsing (listing, inspection and its launch settings) shares
 * the held storage session. Every other job connects and mounts for itself,
 * so it starts with the card released. */
static bool games_session_action(unsigned action) {
    return action==54 || action==55 || action==76;
}
/* Release the held session when the owner leaves Games or browsing has done
 * no storage work for a while; the next browsing job connects again. */
#define GAMES_STORAGE_IDLE_MS 15000u
static void games_storage_idle(void) {
    static unsigned seen;static uint64_t last;
    uint64_t now=timer_ms_gettime64();
    unsigned jobs=kui_games_storage_jobs();
    if(jobs!=seen || !last) {seen=jobs;last=now;}
    if(!kui_games_storage_active()) return;
    mutex_lock(&lock);bool browsing=games_page(shell.page);mutex_unlock(&lock);
    if(!browsing || now-last>=GAMES_STORAGE_IDLE_MS) (void)kui_games_storage_release();
}
/* Requires the lock. True while the page the work began on is still the one
 * shown, settled, with its selected art done. */
static bool games_prefetch_current(void) {
    return shell.page==KUI_SHELL_GAMES && !shell.games_loading && !shell.games_scanning &&
        games_prefetch.valid && shell.games_page==games_prefetch.page &&
        kui_shell_games_view(&shell)==games_prefetch.view && !strcmp(shell.games_path,games_prefetch.root) &&
        !strcmp(shell.games_path,shell.games_listing.root) &&
        (!games_art_request_valid ||
         (games_art_done_valid && kui_shell_games_art_same(&games_art_requested,&games_art_done)));
}
static bool games_prefetch_cancelled(void) {
    if(games_idle_cancelled()) return true;
    mutex_lock(&lock);bool stop=!games_prefetch_current();mutex_unlock(&lock);
    return stop;
}
/* List one page for background work. A stop resumes the step later; a
 * storage failure ends the work for this page, so a failing card is not
 * reread on every idle pass. */
static bool games_prefetch_rows(unsigned offset,struct kui_games_page *rows) {
    if(kui_games_list_rows(games_prefetch.root,offset,games_prefetch.view,rows,kui_log,
       games_prefetch_cancelled)) return true;
    if(!games_prefetch_cancelled()) games_prefetch.step=GAMES_PREFETCH_DONE;
    return false;
}
static bool games_prefetch_idle_work(void) {
    if(kui_cd_audio_owns_drive() || games_idle_cancelled()) return false;
    mutex_lock(&lock);
    bool showing=shell.page==KUI_SHELL_GAMES && !shell.games_loading && !shell.games_scanning &&
        !strcmp(shell.games_path,shell.games_listing.root);
    if(showing && (!games_prefetch.valid || shell.games_page!=games_prefetch.page ||
       kui_shell_games_view(&shell)!=games_prefetch.view || strcmp(shell.games_path,games_prefetch.root))) {
        snprintf(games_prefetch.root,sizeof(games_prefetch.root),"%s",shell.games_path);
        games_prefetch.page=shell.games_page;games_prefetch.view=kui_shell_games_view(&shell);
        games_prefetch.step=0;games_prefetch.valid=true;
    }
    bool wanted=showing && games_prefetch_current() && games_prefetch.step<GAMES_PREFETCH_DONE;
    unsigned total=shell.games_listing.total;bool more=shell.games_listing.has_more;
    mutex_unlock(&lock);
    if(!wanted) return false;
    kui_sd_set_params(KUI_STORAGE_AUTO,true);
    const unsigned page=games_prefetch.page,view=games_prefetch.view,offset=page*KUI_GAMES_ROWS;
    struct kui_games_page rows;
    if(games_prefetch.step==0) {
        /* Titles of the rows on screen, from record headers alone. */
        if(!games_prefetch_rows(offset,&rows)) return true;
        (void)kui_games_prefetch_titles(&rows,kui_log,games_prefetch_cancelled);
        if(games_prefetch_cancelled() || !games_prefetch_rows(offset,&rows)) return true;
        mutex_lock(&lock);
        games_titles_page=rows;games_titles_offset=offset;games_titles_ready=true;
        mutex_unlock(&lock);
        games_prefetch.step=1;return true;
    }
    if(games_prefetch.step==1) {
        /* Box art of every row on screen, so moving the selection shows it. */
        if(!games_prefetch_rows(offset,&rows)) return true;
        for(unsigned row=0;row<rows.count;row++) {
            struct kui_games_cover_result result;
            (void)kui_games_selected_cover(&rows.entries[row],kui_games_view_size(view),
                games_prefetch_pixels,&result,kui_log,games_prefetch_cancelled);
            if(games_prefetch_cancelled()) return true;
        }
        games_prefetch.step=2;return true;
    }
    /* Pages either side, nearest first: resolve their folders, read titles. */
    static const int near[4]={1,-1,2,-2};
    int target=(int)page+near[games_prefetch.step-2u];
    if(target<0 || (target>(int)page && !more) ||
       (total && (unsigned)target*KUI_GAMES_ROWS>=total)) {++games_prefetch.step;return true;}
    if(!games_prefetch_rows((unsigned)target*KUI_GAMES_ROWS,&rows)) return true;
    (void)kui_games_prefetch_titles(&rows,kui_log,games_prefetch_cancelled);
    if(!games_prefetch_cancelled()) ++games_prefetch.step;
    return true;
}
static void games_warm_idle_work(void) {
    if(games_warm_done || kui_cd_audio_owns_drive() || games_warm_cancelled()) return;
    struct kui_music_status music;kui_music_status_copy(&music);
    mutex_lock(&lock);bool song_pending=music_requested>=0;mutex_unlock(&lock);
    if(song_pending || (music.enabled && !music.loaded)) return;
    kui_sd_set_params(KUI_STORAGE_AUTO,true);
    struct kui_games_page page;
    bool ok=kui_games_list_rows(KUI_GAMES_SCAN_ROOT,0,KUI_GAMES_VIEW_SAVED,&page,kui_log,games_warm_cancelled);
    if(!games_warm_cancelled()) {
        if(!ok) games_cache_invalidate();
        games_warm_done=true;
    }
}
#endif
static void *worker(void *unused) {
    (void)unused;
#ifdef KUI_SD_RUNTIME
    kui_games_storage_hold(true);
#endif
    for(;;) {
        mutex_lock(&lock);
        unsigned action = pending;
        pending = 0;
#ifdef KUI_SD_RUNTIME
        bool clear_games_cache=games_cache_clear_pending || (action==54 && games_refresh_pending);
        games_cache_clear_pending=false;
#endif
        mutex_unlock(&lock);
#ifdef KUI_SD_RUNTIME
        salvage_session_observe_errors();
        bool ordinary_continue=action==47 && !salvage_defer_pending && !salvage_trial_pending &&
            !salvage_speed_probe_pending && salvage_diagnostic_pending==KUI_SALVAGE_DIAGNOSTIC_NONE;
        if(!kui_salvage_session_policy_action(action,ordinary_continue))salvage_session_forget();
        if(action && !games_session_action(action)) (void)kui_games_storage_release();
        /* The worker exclusively owns cached discovery and artwork. */
        games_storage_observe();
        if(clear_games_cache) games_cache_invalidate();
#endif
        if(!action) {
#ifdef KUI_SD_RUNTIME
            /* This read-only hint never makes the UI busy. A newly queued
             * foreground job cancels it, then runs on this same worker after
             * the lookup has released its filesystem; no storage concurrency. */
            if(!salvage_session.parked && vmu_game_info_idle_work()) {thd_sleep(1);continue;}
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
                    /* A completed track releases ownership but need not stop
                     * the spindle. Cleanup is exclusive on this same worker. */
                    if(!kui_cd_audio_owns_drive()) (void)kui_disc_stop();
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
                /* Observed removal invalidates the presentation cache even
                 * when the next disc has the same title. Routine post-I/O
                 * parking below is not a removal and retains earned counts. */
                if(disc_identity.state==KUI_DISC_IDENTITY_OPEN || disc_identity.state==KUI_DISC_IDENTITY_EMPTY) {
                    kui_recovery_dashboard_clear(&recovery_dashboard);
                    salvage_session_forget();
                }
                disc_snapshot=disc_identity;
                if(identify && !pending && !busy && !video_preview) {
                    busy=true;cancel_requested=false;ui_hz_busy=2;action=12;
                }
                mutex_unlock(&lock);
            }
            if(!action) {
                /* Keep the just-validated job exclusive while paused. Audio
                 * keeps playing from RAM; unrelated preloads/cache writers
                 * resume after another foreground action revokes the receipt. */
                if(!salvage_session.parked) {
                    games_storage_idle();
                    if(idle && !games_art_idle_work() && !games_selection_store(false)) {
                        music_idle_work();games_warm_idle_work();(void)games_prefetch_idle_work();
                    }
                }
                thd_sleep(16);continue;
            }
#else
            thd_sleep(16);continue;
#endif
        }
#ifdef KUI_SD_RUNTIME
        /* Other operations may alter card content, reconnect storage or hand
         * all memory to a game. Only browsing and inspection retain the cache. */
        if(games_action_invalidates(action)) games_cache_invalidate();
        if(action==1 || is_capture_action(action) || action==7 || action==49 ||
           action==81 || action==96 || action==97) {
            mutex_lock(&lock);kui_recovery_dashboard_clear(&recovery_dashboard);mutex_unlock(&lock);
        }
        if(action && needs_cd_handoff(action) && !kui_cd_audio_stop_for_io(kui_log)) {
            publish_cd_audio();
            mutex_lock(&lock);
            snprintf(capture_message,sizeof(capture_message),"Audio CD stop failed; no new optical operation started.");
            if(is_capture_action(action)) {capture_outcome=KUI_SHELL_OUTCOME_FAILED;observing_capture=false;}
            if(action==24) {player_status.errors=1;snprintf(player_status.message,sizeof(player_status.message),"Audio CD stop failed; SD playback refused.");}
            if(action>=46 && action<=48) {salvage_status.complete=true;salvage_status.errors=1;snprintf(salvage_status.message,sizeof(salvage_status.message),"Audio CD stop failed; salvage refused.");}
            if(action==81 || action==96 || action==97) {scan_status=(struct kui_app_status){.complete=true,.errors=1};
                snprintf(scan_status.message,sizeof(scan_status.message),"Audio CD stop failed; repair did not start.");}
            if(action==56 || action==57 || action==58) probe_launch_failed=true;
            if(action==79) {
                probe_status=(struct kui_app_status){.complete=true,.errors=1};
                snprintf(probe_status.message,sizeof(probe_status.message),"Audio CD stop failed; executable check did not start.");
            }
            if(action>=68 && action<=71) {
                sci_async_status=(struct kui_app_status){.complete=true,.errors=1};
                snprintf(sci_async_status.message,sizeof(sci_async_status.message),"Audio CD stop failed; probe did not start.");
            }
            if(action==65) {
                storage_test_result=(struct kui_storage_test_result){0};
                storage_test_result.request=storage_test_pending;
                storage_test_result.outcome=KUI_STORAGE_TEST_FAILED;
                snprintf(storage_test_result.message,sizeof(storage_test_result.message),
                    "Audio CD stop failed; storage test did not start.");
                ++storage_test_generation;
            }
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
            if(action==81 || action==82 || action==96 || action==97) {
                scan_status=(struct kui_app_status){.complete=true,.stopped=true};
                snprintf(scan_status.message,sizeof(scan_status.message),"Operation stopped before starting.");
            }
            if(action>=46 && action<=48) {
                salvage_session_forget();
                salvage_status=(struct kui_app_status){.complete=true,.stopped=true};
                snprintf(salvage_status.message,sizeof(salvage_status.message),"Salvage stopped before starting.");
            }
            if(action>=49 && action<=52) {
                snprintf(cd_audio_snapshot.message,sizeof(cd_audio_snapshot.message),"Audio CD operation stopped before starting.");
                ++cd_audio_generation;
            }
            if(action==54 || action==59) {
                memset(&games_listing,0,sizeof(games_listing));
                games_listing.view=KUI_GAMES_VIEW_SAVED;
                snprintf(games_listing.root,sizeof(games_listing.root),"%.*s",
                    (int)sizeof(games_listing.root)-1,action==59?KUI_GAMES_SCAN_ROOT:games_path_pending);
                snprintf(games_listing.message,sizeof(games_listing.message),"%s",action==59?
                    "Box art scan stopped before starting":"Games browse stopped before starting");
                games_result_offset=action==59?0:games_offset_pending;
                games_result_epoch=games_epoch_pending;games_result_view=games_view_pending;++games_listing_generation;
            }
            if(action==60 || action==62) {
                memset(&files_listing_result,0,sizeof(files_listing_result));
                memcpy(files_listing_result.path,files_request_pending.path,sizeof(files_listing_result.path));
                files_listing_result.folders_only=files_request_pending.folders_only;
                snprintf(files_listing_result.message,sizeof(files_listing_result.message),
                    "Stopped before listing; press R to list this folder again.");
                ++files_listing_generation;
            }
            if(action==61) {
                memset(&files_preview_result,0,sizeof(files_preview_result));
                files_preview_result.job=files_job_pending;
                files_preview_result.status=(struct kui_app_status){.complete=true,.stopped=true};
                snprintf(files_preview_result.status.message,sizeof(files_preview_result.status.message),"Check stopped before starting");
                ++files_preview_generation;
            }
            if(action==62) {
                files_result=(struct kui_app_status){.complete=true,.stopped=true};
                snprintf(files_result.message,sizeof(files_result.message),"Stopped before starting; nothing changed.");
                ++files_result_generation;
            }
            if(action==64) {
                ftp_status.state=KUI_FTP_STOPPED;
                snprintf(ftp_status.message,sizeof(ftp_status.message),"Stopped before starting");
            }
            if((action>=72 && action<=75) || action==77 || action==78) {
                memset(wifi_request_pending.password,0,sizeof(wifi_request_pending.password));
                wifi_view.working=false;
                wifi_view.firmware_updating=false;
                wifi_view.firmware_ready=false;
                wifi_view.firmware_committing=false;
                snprintf(wifi_view.message,sizeof(wifi_view.message),"Stopped before starting");
                ++wifi_generation;
            }
            if(action==63) {
                memset(&files_picture_result,0,sizeof(files_picture_result));
                memcpy(files_picture_result.path,files_picture_pending,sizeof(files_picture_result.path));
                files_picture_result.format="";
                snprintf(files_picture_result.message,sizeof(files_picture_result.message),"Picture stopped before loading");
                ++files_picture_generation;
            }
            if(action==24 && files_music) files_song_result(&player_status);
            if(action==55) {
                memset(&games_detail,0,sizeof(games_detail));games_detail.stopped=true;
                strcpy(games_detail.path,games_path_pending);
                snprintf(games_detail.message,sizeof(games_detail.message),"Games inspection stopped before starting");
                kui_game_launch_settings_default(&games_detail_launch_settings);
                games_detail_launch_loaded=games_detail_launch_found=false;
                ++games_detail_generation;
            }
            if(action==79) {
                probe_status=(struct kui_app_status){.complete=true,.stopped=true};
                snprintf(probe_status.message,sizeof(probe_status.message),"Executable check stopped before starting.");
            }
            if(action==76) {
                memcpy(games_launch_result_path,games_launch_path_pending,sizeof(games_launch_result_path));
                games_launch_saved=false;++games_launch_result_generation;
            }
            if(action==56 || action==57 || action==58) {
                probe_status=(struct kui_app_status){.stopped=true};
                snprintf(probe_status.message,sizeof(probe_status.message),"Games handoff stopped before starting.");
                probe_launch_failed=true;
            }
            if((action>=41 && action<=43) || action==87 || action==88) {
                maintenance_status=(struct kui_app_status){.complete=true,.stopped=true};
                snprintf(maintenance_status.message,sizeof(maintenance_status.message),"System operation stopped before starting.");
            }
            if(action==83) {
                vmu_snapshot.status=(struct kui_app_status){.complete=true,.stopped=true};
                snprintf(vmu_snapshot.status.message,sizeof(vmu_snapshot.status.message),"Card-image backup stopped before starting.");
                ++vmu_generation;
            }
            if(action>=84 && action<=86) {
                vmu_image_snapshot.status=(struct kui_app_status){.complete=true,.stopped=true};
                snprintf(vmu_image_snapshot.status.message,sizeof(vmu_image_snapshot.status.message),"Image operation stopped before starting.");
                ++vmu_image_generation;
            }
            if(action==89 || action==90) {
                vmu_bulk_snapshot.status=(struct kui_app_status){.complete=true,.stopped=true};
                snprintf(vmu_bulk_snapshot.status.message,sizeof(vmu_bulk_snapshot.status.message),"Copy-all stopped before starting; no VMU writes.");
                vmu_bulk_snapshot.copy_ready=false;
                vmu_bulk_source_result=vmu_slot_pending;vmu_bulk_destination_result=vmu_copy_slot_pending;
                vmu_bulk_commit_result=action==90;++vmu_bulk_generation;
            }
            if(action==91 || action==92) {
                vmu_import_snapshot.status=(struct kui_app_status){.complete=true,.stopped=true};
                snprintf(vmu_import_snapshot.status.message,sizeof(vmu_import_snapshot.status.message),"Save import stopped before starting.");
                vmu_import_snapshot.import_ready=false;
                memcpy(vmu_import_path_result,vmu_import_path_pending,sizeof(vmu_import_path_result));
                vmu_import_request_result=vmu_import_request_pending;vmu_import_commit_result=action==92;
                ++vmu_import_generation;
            }
            if(action==93 || action==101) {
                memset(&vmu_collection_snapshot,0,sizeof(vmu_collection_snapshot));
                vmu_collection_snapshot.page=vmu_collection_page_pending;
                vmu_collection_snapshot.status=(struct kui_app_status){.complete=true,.stopped=true};
                snprintf(vmu_collection_snapshot.status.message,sizeof(vmu_collection_snapshot.status.message),
                    "Virtual VMU operation stopped before starting.");
                vmu_collection_request_result=vmu_collection_request_pending;++vmu_collection_generation;
            }
            if(action==94 || action==95) {
                memset(&vmu_collection_copy_snapshot,0,sizeof(vmu_collection_copy_snapshot));
                vmu_collection_copy_snapshot.slot=vmu_slot_pending;vmu_collection_copy_snapshot.page=vmu_page_pending;
                vmu_collection_copy_snapshot.status=(struct kui_app_status){.complete=true,.stopped=true};
                snprintf(vmu_collection_copy_snapshot.status.message,sizeof(vmu_collection_copy_snapshot.status.message),
                    "Virtual card copy stopped before starting.");
                vmu_collection_copy_source_result=vmu_slot_pending;vmu_collection_copy_page_result=vmu_page_pending;
                vmu_collection_copy_selected_result=vmu_selected_pending;
                vmu_collection_copy_all_result=vmu_collection_copy_all_pending;
                vmu_collection_copy_request_result=vmu_collection_copy_request_pending;
                vmu_collection_copy_commit_result=action==95;++vmu_collection_copy_generation;
            }
            if(action==45) {
                network_test_status=(struct kui_app_status){.stopped=true};
                snprintf(network_test_status.message,sizeof(network_test_status.message),"Connection test stopped before starting.");
            }
            if(action==98) {
                vmu_wizard_snapshot.generation=vmu_wizard_pending.generation;
                if(vmu_wizard_pending.op==KUI_VMU_WIZARD_USE_CARD) {
                    vmu_wizard_snapshot.view_only=vmu_wizard_pending.view_only;
                    vmu_wizard_snapshot.existing_card=true;
                    vmu_wizard_snapshot.stage=KUI_VMU_WIZARD_GAMES_STAGE;
                    vmu_wizard_snapshot.count=vmu_wizard_snapshot.total=0;
                }
                vmu_wizard_snapshot.status=(struct kui_app_status){.complete=true,.stopped=true};
                snprintf(vmu_wizard_snapshot.status.message,sizeof(vmu_wizard_snapshot.status.message),
                    "Import wizard stopped before starting.");
                ++vmu_wizard_generation;
            }
            if(action==99 || action==100) {
                vmu_emulation_result=vmu_emulation_request;
                vmu_emulation_ok=false;vmu_emulation_stopped=true;
                snprintf(vmu_emulation_note,sizeof(vmu_emulation_note),"VMU setting operation stopped before starting.");
                ++vmu_emulation_generation;
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
            if(action>=68 && action<=71) {
                sci_async_status=(struct kui_app_status){.complete=true,.stopped=true};
                snprintf(sci_async_status.message,sizeof(sci_async_status.message),"Stopped before starting.");
            }
            if(action==65) {
                storage_test_result=(struct kui_storage_test_result){0};
                storage_test_result.request=storage_test_pending;
                storage_test_result.outcome=KUI_STORAGE_TEST_STOPPED;
                snprintf(storage_test_result.message,sizeof(storage_test_result.message),"Stopped before starting.");
                ++storage_test_generation;
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
            if(action == 7) {
#ifdef KUI_SD_RUNTIME
                kui_memory_log("bench start");
#endif
                enum kui_bench_result result=kui_bench_start();
                kui_ui_set_hz(KUI_OPT_UI_FULL);   /* the cap is for the measurement, not the report save */
#ifdef KUI_SD_RUNTIME
                kui_memory_log("bench end");
#endif
                const char *outcome=result==KUI_BENCH_COMPLETE?"complete":result==KUI_BENCH_STOPPED?"stopped":"failed";
                kui_log("Bench result: %s",outcome);
                kui_log("Saving diagnostic report automatically; B cancels log save.");
                save_report("auto bench",outcome,true);
            }
#ifdef KUI_SD_RUNTIME
            if(action>=65 && action<=67) storage_test_operation(action);
            if(action>=68 && action<=71) sci_async_operation(action-68u);
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
                kui_sd_set_params(KUI_STORAGE_AUTO,true);
                struct kui_music_player_page page;
                bool ok=kui_music_player_list(music_path_pending,music_offset_pending,&page,kui_log,kui_cancelled);
                mutex_lock(&lock);
                music_listing=page;++music_listing_generation;
                snprintf(player_status.message,sizeof(player_status.message),"%s",page.message);
                player_status.complete=true;player_status.passed=ok;player_status.errors=ok?0u:1u;
                mutex_unlock(&lock);
            }
            if(action==24) {
                kui_sd_set_params(KUI_STORAGE_AUTO,true);
                struct kui_app_status result;
                active_app=24;
                kui_music_player_run(music_path_pending,system_current.music_volume,&result,
                    kui_log,kui_cancelled,app_progress);
                mutex_lock(&lock);
                player_status=result;
                if(files_music) files_song_result(&result);
                if(result.passed) {
                    system_current.music_enabled=true;system_pending=system_current;
                    system_ok=true;++system_generation;
                    music_requested=-1;++music_request_generation;
                }
                mutex_unlock(&lock);
                publish_music();
            }
            if(action==54) {
                kui_sd_set_params(KUI_STORAGE_AUTO,true);
                unsigned offset=games_offset_pending,selected=0;
                bool restore=games_seek_request;
                if(restore && games_seek_pending[0]) {
                    unsigned found;
                    offset=0;
                    if(kui_games_find(games_path_pending,games_seek_pending,&found,kui_log,kui_cancelled)) {
                        offset=found/KUI_GAMES_ROWS*KUI_GAMES_ROWS;selected=found%KUI_GAMES_ROWS;
                    }
                }
                struct kui_games_page page;
                bool ok=kui_games_list_rows(games_path_pending,offset,games_view_pending,&page,kui_log,kui_cancelled);
                if(!ok && restore && !kui_cancelled() && strcmp(games_path_pending,"/Games")) {
                    offset=selected=0;
                    ok=kui_games_list_rows("/Games",0,games_view_pending,&page,kui_log,kui_cancelled);
                }
                if(!ok && !kui_cancelled()) games_cache_invalidate();
                mutex_lock(&lock);games_listing=page;games_result_offset=offset;
                games_result_restore=restore;games_result_selected=selected;
                games_result_epoch=games_epoch_pending;games_result_view=games_view_pending;
                ++games_listing_generation;mutex_unlock(&lock);
            }
            if(action==59) {
                kui_sd_set_params(KUI_STORAGE_AUTO,true);
                struct kui_app_status status;struct kui_games_scan_counts counts;
                kui_games_scan(&status,&counts,games_scan_progress,kui_log,kui_cancelled);
                /* Show the library either way: finished covers are kept, and
                 * a Stop request already ended the scan itself. */
                struct kui_games_page page;
                games_cache_invalidate();
                if(kui_games_list_rows(KUI_GAMES_SCAN_ROOT,0,games_view_pending,&page,kui_log,NULL))
                    snprintf(page.message,sizeof(page.message),"%s",status.message);
                else games_cache_invalidate();
                mutex_lock(&lock);games_listing=page;games_result_offset=0;
                games_result_restore=false;
                games_result_epoch=games_epoch_pending;games_result_view=games_view_pending;
                ++games_listing_generation;mutex_unlock(&lock);
            }
            if(action==60) {
                kui_sd_set_params(KUI_STORAGE_AUTO,true);
                struct kui_files_page page;
                kui_files_list(&files_request_pending,&page,kui_log,kui_cancelled);
                mutex_lock(&lock);files_listing_result=page;++files_listing_generation;mutex_unlock(&lock);
            }
            if(action==61) {
                kui_sd_set_params(KUI_STORAGE_AUTO,true);
                struct kui_files_preview preview;
                kui_files_preview(&files_job_pending,&preview,kui_log,kui_cancelled,files_progress);
                mutex_lock(&lock);files_preview_result=preview;++files_preview_generation;mutex_unlock(&lock);
            }
            if(action==62) {
                kui_sd_set_params(KUI_STORAGE_AUTO,true);
                struct kui_app_status status;
                kui_files_commit(&files_job_pending,&files_totals_pending,&status,kui_log,kui_cancelled,files_progress);
                /* The folder is listed again even after Stop, which ended the run itself. */
                struct kui_files_page page;
                kui_files_list(&files_request_pending,&page,kui_log,NULL);
                mutex_lock(&lock);
                files_result=status;++files_result_generation;
                files_listing_result=page;++files_listing_generation;
                mutex_unlock(&lock);
            }
            if(action==63) {
                kui_sd_set_params(KUI_STORAGE_AUTO,true);
                struct kui_files_picture picture;
                kui_files_picture(files_picture_pending,files_picture_pixels,&picture,kui_log,kui_cancelled);
                mutex_lock(&lock);files_picture_result=picture;++files_picture_generation;mutex_unlock(&lock);
            }
            if(action==64) {
                /* The card stays on SCIF; the network adapter (a W5500 or the
                 * Wi-Fi board) has the SCI port. */
                kui_sd_set_params(KUI_STORAGE_AUTO,true);
                struct kui_ftp_options options={0};
                options.seed=(uint32_t)timer_us_gettime64();
                const struct kui_net_ports ports={kui_w5500_console_port(),kui_wifi_console_port()};
                struct kui_ftp_status result;
                /* KOS's SD writes wait out the card's busy time after each
                 * write by polling it at the scheduler's ticks, 10 ms apart
                 * at KOS's 100 Hz; at 1000 Hz that wait is a millisecond at
                 * most. Only while the FTP server runs. */
                unsigned hz=thd_get_hz();
                thd_set_hz(1000);
                kui_ftp_run(&ports,&options,&result,kui_log,kui_cancelled,ftp_publish);
                thd_set_hz(hz);
                mutex_lock(&lock);ftp_status=result;ftp_seen=true;mutex_unlock(&lock);
            }
            if(action>=72 && action<=75) {
                /* The card is not used: the Wi-Fi board has the SCI port. */
                struct kui_wifi_view view;
                kui_wifi_run(kui_wifi_console_port(),&wifi_request_pending,&view,kui_log,kui_cancelled,wifi_publish);
                mutex_lock(&lock);
                memset(wifi_request_pending.password,0,sizeof(wifi_request_pending.password));
                mutex_unlock(&lock);
            }
            if(action==77 || action==78) {
                /* Use the boot-selected card, normally SCIF with this SCI
                 * adapter, and keep all storage/link I/O on this worker. */
                kui_sd_set_params(KUI_STORAGE_AUTO,true);
                struct kui_wifi_view view;
                kui_wifi_update_run(kui_wifi_console_port(),&wifi_request_pending,&view,
                    kui_log,kui_cancelled,wifi_publish);
                mutex_lock(&lock);
                memset(&wifi_request_pending.firmware,0,sizeof(wifi_request_pending.firmware));
                mutex_unlock(&lock);
            }
            if(action==55) {
                kui_sd_set_params(KUI_STORAGE_AUTO,true);
                struct kui_games_detail detail;
                struct kui_game_launch_settings value;
                bool found=false;
                bool loaded=game_launch_settings_load(games_path_pending,&value,&found);
                if(games_codec_override_pending) {
                    value.codec=games_codec_pending;
                    /* Preserve every other saved setting. A failed read cannot
                     * safely overwrite the collection; this choice remains temporary. */
                    bool saved=loaded && game_launch_settings_write(games_path_pending,&value);
                    game_launch_settings_publish_save(games_path_pending,saved);
                    loaded=true;found=true;
                }
                kui_games_inspect_cover_codec(games_path_pending,(enum kui_zso_codec)value.codec,
                    &detail,games_detail_cover,kui_log,kui_cancelled);
                mutex_lock(&lock);
                games_detail=detail;games_detail_launch_settings=value;
                games_detail_launch_loaded=loaded;games_detail_launch_found=found;
                ++games_detail_generation;
                mutex_unlock(&lock);
            }
            if(action==76) game_launch_settings_save();
            if(action==79) {
                kui_sd_set_params(KUI_STORAGE_AUTO,true);
                bool ok=kui_games_retail_check_launch(games_path_pending,games_reader_pending,
                    &games_patches_pending,kui_log,kui_cancelled);
                bool stopped=kui_cancelled();if(stopped)ok=false;
                struct kui_app_status checked={.complete=true,.passed=ok,.stopped=stopped};
                checked.errors=!ok && !checked.stopped?1u:0u;
                snprintf(checked.message,sizeof(checked.message),"%s",ok?
                    "Executable preparation passed. The game was not started.":checked.stopped?
                    "Executable check stopped. The game was not started.":
                    "Executable check failed. See Diagnostics for the exact reason.");
                mutex_lock(&lock);probe_status=checked;mutex_unlock(&lock);
            }
            if(action==56 || action==57 || action==58) {
                (void)games_selection_store(true);
                kui_sd_set_params(KUI_STORAGE_AUTO,true);
                bool prepared=action==58?
                    kui_games_retail_prepare_launch(games_path_pending,games_reader_pending,&games_patches_pending,&probe_image,
                        kui_log,kui_cancelled):action==57?
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
                games_selection_startup();
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
            if(action==81 || action==82 || action==96 || action==97)repair_operation(action==82);
            if(action==31) {
                struct kui_vmu_backup_view result;
                active_app=31;kui_sd_set_params(KUI_STORAGE_AUTO,true);
                kui_vmu_backups_run(vmu_backup_page_pending,&result,kui_log,kui_cancelled,app_progress);
                mutex_lock(&lock);vmu_backups=result;++vmu_backups_generation;mutex_unlock(&lock);
            }
            if(action==32 || action==33) {
                struct kui_vmu_view result;
                active_app=action;kui_sd_set_params(KUI_STORAGE_AUTO,true);
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
                active_app=action;kui_sd_set_params(KUI_STORAGE_AUTO,true);
                if(action<=38) kui_vmu_delete_run(vmu_slot_pending,vmu_page_pending,vmu_selected_pending,
                    action==38,&result,kui_log,kui_cancelled,app_progress);
                else if(vmu_copy_slot_pending<8)
                    kui_vmu_copy_run(vmu_slot_pending,vmu_page_pending,vmu_selected_pending,
                        vmu_copy_slot_pending,action==40,&result,kui_log,kui_cancelled,app_progress);
                else {
                    memset(&result,0,sizeof(result));result.slot=vmu_slot_pending;result.page=vmu_page_pending;
                    result.status.complete=true;result.status.errors=1;
                    snprintf(result.status.message,sizeof(result.status.message),"Choose a physical VMU or start a new virtual card check.");
                }
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
                active_app=action;kui_sd_set_params(KUI_STORAGE_AUTO,true);
                kui_maintenance_run(action-41,&result,kui_log,kui_cancelled,app_progress);
                mutex_lock(&lock);maintenance_status=result;mutex_unlock(&lock);
                if(action!=41) {
                    kui_ui_set_hz(KUI_OPT_UI_FULL);
                    save_report(action==42?"auto settings flash backup":"auto visible BIOS backup",
                        result.passed?"backup verified":result.stopped?"stopped":"not verified",true);
                }
            }
            if(action==87 || action==88) {
                struct kui_app_status result;
                active_app=action;kui_sd_set_params(KUI_STORAGE_AUTO,true);
                kui_maintenance_compare_run(maintenance_compare_path_pending,action==88,&result,
                    kui_log,kui_cancelled,app_progress);
                mutex_lock(&lock);maintenance_status=result;mutex_unlock(&lock);
            }
            if(action==83) {
                struct kui_vmu_view result;
                active_app=action;kui_sd_set_params(KUI_STORAGE_AUTO,true);
                kui_vmu_app_run(3,vmu_slot_pending,vmu_page_pending,vmu_selected_pending,
                    &result,kui_log,kui_cancelled,app_progress);
                mutex_lock(&lock);vmu_snapshot=result;++vmu_generation;mutex_unlock(&lock);
            }
            if(action>=84 && action<=86) {
                struct kui_vmu_view result;
                active_app=action;kui_sd_set_params(KUI_STORAGE_AUTO,true);
                kui_vmu_image_run(vmu_image_path_pending,vmu_image_page_pending,vmu_image_selected_pending,
                    action-84,&result,kui_log,kui_cancelled,app_progress);
                mutex_lock(&lock);vmu_image_snapshot=result;++vmu_image_generation;mutex_unlock(&lock);
            }
            if(action==89 || action==90) {
                struct kui_vmu_view result;
                active_app=action;kui_sd_set_params(KUI_STORAGE_AUTO,true);
                if(vmu_copy_slot_pending<8)
                    kui_vmu_copy_all_run(vmu_slot_pending,vmu_copy_slot_pending,action==90,
                        &result,kui_log,kui_cancelled,app_progress);
                else {
                    memset(&result,0,sizeof(result));result.slot=vmu_slot_pending;
                    result.status.complete=true;result.status.errors=1;
                    snprintf(result.status.message,sizeof(result.status.message),"Choose a physical VMU or start a new virtual card check.");
                }
                mutex_lock(&lock);vmu_bulk_snapshot=result;
                vmu_bulk_source_result=vmu_slot_pending;vmu_bulk_destination_result=vmu_copy_slot_pending;
                vmu_bulk_commit_result=action==90;++vmu_bulk_generation;mutex_unlock(&lock);
            }
            if(action==91 || action==92) {
                struct kui_vmu_view result;
                active_app=action;kui_sd_set_params(KUI_STORAGE_AUTO,true);
                kui_vmu_import_run(vmu_import_path_pending,action==92,&result,kui_log,kui_cancelled,app_progress);
                mutex_lock(&lock);vmu_import_snapshot=result;
                memcpy(vmu_import_path_result,vmu_import_path_pending,sizeof(vmu_import_path_result));
                vmu_import_request_result=vmu_import_request_pending;vmu_import_commit_result=action==92;
                ++vmu_import_generation;mutex_unlock(&lock);
            }
            if(action==93) {
                struct kui_vmu_backup_view result;
                active_app=action;kui_sd_set_params(KUI_STORAGE_AUTO,true);
                kui_vmu_collection_run(vmu_collection_page_pending,&result,kui_log,kui_cancelled,app_progress);
                mutex_lock(&lock);vmu_collection_snapshot=result;
                vmu_collection_request_result=vmu_collection_request_pending;++vmu_collection_generation;
                mutex_unlock(&lock);
            }
            if(action==94 || action==95) {
                struct kui_vmu_view result;
                active_app=action;kui_sd_set_params(KUI_STORAGE_AUTO,true);
                kui_vmu_collection_copy_run(vmu_slot_pending,vmu_page_pending,vmu_selected_pending,
                    vmu_collection_copy_all_pending,action==95,&result,kui_log,kui_cancelled,app_progress);
                mutex_lock(&lock);vmu_collection_copy_snapshot=result;
                vmu_collection_copy_source_result=vmu_slot_pending;vmu_collection_copy_page_result=vmu_page_pending;
                vmu_collection_copy_selected_result=vmu_selected_pending;
                vmu_collection_copy_all_result=vmu_collection_copy_all_pending;
                vmu_collection_copy_request_result=vmu_collection_copy_request_pending;
                vmu_collection_copy_commit_result=action==95;++vmu_collection_copy_generation;
                mutex_unlock(&lock);
            }
            if(action==98) {
                struct kui_vmu_wizard_view result;
                active_app=action;kui_sd_set_params(KUI_STORAGE_AUTO,true);
                kui_vmu_wizard_run(&vmu_wizard_pending,&result,kui_log,kui_cancelled,app_progress);
                mutex_lock(&lock);vmu_wizard_snapshot=result;++vmu_wizard_generation;mutex_unlock(&lock);
            }
            if(action==101) {
                struct kui_vmu_backup_view result;
                active_app=action;kui_sd_set_params(KUI_STORAGE_AUTO,true);
                kui_vmu_collection_rename_run(vmu_name_path_pending,vmu_name_label_pending,vmu_name_identity_pending,
                    vmu_collection_page_pending,&result,kui_log,kui_cancelled,app_progress);
                mutex_lock(&lock);vmu_collection_snapshot=result;
                vmu_collection_request_result=vmu_collection_request_pending;++vmu_collection_generation;
                mutex_unlock(&lock);
            }
            if(action==99 || action==100) {
                active_app=action;kui_sd_set_params(KUI_STORAGE_AUTO,true);
                vmu_emulation_operation(action==100);
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
            if(is_capture_action(action)) {
                kui_memory_log("capture/verify start");
                enum kui_capture_result result=kui_capture_start_target(
                    action==22?KUI_CAPTURE_RESUME:(enum kui_capture_mode)(action-4),KUI_BUILD_ID,
                    capture_destination,action==4?NULL:capture_job_pending,
                    action==4?capture_label_pending:NULL,action==22,capture_network_pending,capture_cd_toc_pending);
                mutex_lock(&lock);
                capture_summary = *kui_capture_last_stats();
                observing_capture=false;
                capture_outcome = result == KUI_CAPTURE_COMPLETE ? KUI_SHELL_OUTCOME_COMPLETE :
                    result == KUI_CAPTURE_STOPPED ? KUI_SHELL_OUTCOME_STOPPED : KUI_SHELL_OUTCOME_FAILED;
                const char *stage=action==6?"Verification":(action==5 || action==22)?"Resume":"Capture";
                snprintf(capture_message,sizeof(capture_message),"%s",
                    result==KUI_CAPTURE_FAILED?(drive_reset_required?
                        "Drive reset required. Reboot, then Resume the partial dump.":
                        capture_status.phase==KUI_EXPORTING?"Raw rip kept; export failed. See Diagnostics, then Resume.":
                        capture_status.phase==KUI_VERIFYING?"Saved-file verification failed. See Diagnostics.":
                        capture_status.phase==KUI_PREFIX_CHECK?"Saved-prefix check failed. See Diagnostics.":
                        "Disc operation failed before verification completed."):
                    result==KUI_CAPTURE_STOPPED?(capture_status.phase==KUI_EXPORTING?
                        "Raw rip kept; export stopped. Resume to recreate it.":capture_summary.job_dir[0]?
                        "Stopped safely; the committed partial dump is kept.":
                        "Stopped before a dump job was created."):"");
                mutex_unlock(&lock);
                kui_log("%s operation ended during %s",stage,
                    capture_status.phase==KUI_VERIFYING?"saved-file verification":
                    capture_status.phase==KUI_CAPTURING?"disc capture":
                    capture_status.phase==KUI_EXPORTING?"compressed export":
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
        if(action!=12 && action!=27 && action!=60) kui_log("Operation ended. Diagnostics page: Y saves the log to SD.");
        if(action==12 || action==1 || (action>=4 && action<=7) || action==22 ||
           action==49 || action==81 || action==96 || action==97 || (action>=46 && action<=48)) {
            if(!kui_cd_audio_owns_drive() && !kui_disc_stop())salvage_session_forget();
            if(action!=12) kui_disc_identity_invalidate(&disc_identity);
            mutex_lock(&lock);
            if(!drive_reset_required) disc_snapshot=disc_identity;
            mutex_unlock(&lock);
        }
        salvage_session_observe_errors();
#else
        if(action==1 || action==7) (void)kui_disc_stop();
        kui_log("Operation ended. View Log; saving a report is a separate write action.");
#endif
        mutex_lock(&lock);
        busy = false;
#ifdef KUI_SD_RUNTIME
        storage_test_running=false;
        salvage_defer_pending=false;
#endif
        cancel_requested = false;
        ui_hz_busy = KUI_OPT_UI_FULL;   /* no operation leaves its cap behind for the next */
        mutex_unlock(&lock);
    }
    return NULL;
}

#ifndef KUI_SD_RUNTIME
static void draw_boot(void) {
    /* Keep the displayed frame until something visible changes. In particular,
     * reading another 32 KiB must not copy/dim 600 KiB of VRAM and wait for
     * vertical blank again. Each changed frame fully repaints the back buffer. */
    static bool drawn;
    static struct kui_boot_ui last_ui;
    static struct kui_boot_view last_view;
    static char last_lines[KUI_BOOT_LOG_ROWS][LINE_BYTES],last_status[128];
    char visible[KUI_BOOT_LOG_ROWS][LINE_BYTES]={{0}},status[128];
    struct kui_boot_view view={.build=KUI_BUILD_ID,.worker_available=boot_worker_available,
        .from_card=kui_storage_boot_from_card()};
    mutex_lock(&lock);
    unsigned maximum=line_count>KUI_BOOT_LOG_ROWS?line_count-KUI_BOOT_LOG_ROWS:0;
    if(boot_ui.scroll>maximum) boot_ui.scroll=maximum;
    unsigned end=line_count-boot_ui.scroll;
    unsigned first=end>KUI_BOOT_LOG_ROWS?end-KUI_BOOT_LOG_ROWS:0;
    view.line_count=end-first;view.total_lines=line_count;
    for(unsigned i=0;i<view.line_count;i++) {
        strcpy(visible[i],lines[first+i]);view.lines[i]=visible[i];
    }
    view.busy=busy;view.cancelled=cancel_requested;
    if(busy) snprintf(status,sizeof(status),"%s",cancel_requested?"Stopping safely...":
        saving_report?"Saving diagnostic report...":line_count?lines[line_count-1]:"Working...");
    else snprintf(status,sizeof(status),"%s",boot_notice);
    mutex_unlock(&lock);
    uint64_t now=timer_ms_gettime64();
    if(boot_ui.autoboot_until>now)
        view.countdown=(unsigned)((boot_ui.autoboot_until-now+999u)/1000u);
    view.status=status;
    bool same=drawn && boot_ui.page==last_ui.page && boot_ui.selected==last_ui.selected &&
        boot_ui.transport==last_ui.transport && boot_ui.confirm==last_ui.confirm &&
        boot_ui.scroll==last_ui.scroll && boot_ui.log_column==last_ui.log_column &&
        view.busy==last_view.busy && view.cancelled==last_view.cancelled &&
        view.worker_available==last_view.worker_available && view.from_card==last_view.from_card &&
        view.countdown==last_view.countdown &&
        view.line_count==last_view.line_count && view.total_lines==last_view.total_lines &&
        !strcmp(status,last_status) && !memcmp(visible,last_lines,sizeof(visible));
    if(same) return;
    uint64_t started=timer_us_gettime64();
    kui_boot_ui_draw(vram_s,&boot_ui,&view);
    vid_waitvbl();vid_flip(-1);
    boot_draw_us+=timer_us_gettime64()-started;++boot_draw_count;
    boot_last_draw=timer_ms_gettime64();
    last_ui=boot_ui;
    /* Only retain scalar fields: the snapshot's text pointers are stack-local. */
    last_view=(struct kui_boot_view){.busy=view.busy,.cancelled=view.cancelled,
        .worker_available=view.worker_available,.from_card=view.from_card,.countdown=view.countdown,
        .line_count=view.line_count,.total_lines=view.total_lines};
    strcpy(last_status,status);memcpy(last_lines,visible,sizeof(visible));drawn=true;
}
#endif

#ifdef KUI_SD_RUNTIME
static void draw_shell(void) {
    mutex_lock(&lock);
    if(!kui_sci_video_quiet_draw_begin(&sci_video_quiet)) {mutex_unlock(&lock);return;}
    bool startup=splash_active;
    bool working=busy || boot_ready || probe_launch_ready;
    mutex_unlock(&lock);
    uint16_t *frame=shell_frame_prepare(working);
    if(startup) {
        kui_splash_draw(frame?frame:vram_s);
        if(frame) {sq_cpy(vram_s,frame,640u*480u*sizeof(*frame));sq_wait();}
        vid_waitvbl();vid_flip(-1);return;
    }
    char visible[KUI_SHELL_LOG_ROWS][LINE_BYTES] = {{0}};
    const char *log_rows[KUI_SHELL_LOG_ROWS];
    char path[KUI_DEST_JOB_CAP], notice[128], title[129], gdi[KUI_DEST_TITLE_CAP+5u];
    char inserted[129],music_title[40],music_notice[128],message[128];
    struct kui_app_status app_status;
    static struct kui_ftp_status ftp_view;
    struct kui_storage_test_progress test_progress;
    struct kui_shell_view view = {.build = KUI_BUILD_ID, .job_dir = path,
        .disc_title = title, .gdi_name = gdi, .settings_notice = notice, .message = message, .log_lines = log_rows,
        .inserted_title=inserted,.music_title=music_title,.music_notice=music_notice,.app_status=&app_status,
        .game_selected_cover=games_cover_pixels,.game_detail_cover=games_detail_cover,
        .files_picture=files_picture_pixels};
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
    test_progress=storage_test_progress;
    view.storage_test_progress=storage_test_running?&test_progress:NULL;
    view.storage_test_target=kui_storage_name(kui_storage_selected());
    view.busy = busy; view.saving = saving_report; view.cancel_requested = cancel_requested;
    view.salvage_control_trial=salvage_trial_pending;
    view.salvage_speed_probe=salvage_speed_probe_pending;
    view.salvage_diagnostic=salvage_diagnostic_pending;
    view.sci_video_quiet=sci_video_quiet.requested;
    view.outcome = capture_outcome; view.saved_verified = capture_summary.verified;
    snprintf(path, sizeof(path), "%s", capture_summary.job_dir);
    snprintf(title, sizeof(title), "%s", capture_summary.disc_title[0]?
        capture_summary.disc_title:capture_display.title);
    snprintf(gdi, sizeof(gdi), "%s", capture_summary.output_name[0]?
        capture_summary.output_name:capture_summary.gdi_name);
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
    bool files_page=shell.page==KUI_SHELL_FILES || shell.page==KUI_SHELL_FILES_ACTIONS ||
        shell.page==KUI_SHELL_FILES_PICK || shell.page==KUI_SHELL_FILES_CONFIRM ||
        shell.page==KUI_SHELL_FILES_INFO || shell.page==KUI_SHELL_FILES_VIEW;
    app_status=shell.page==KUI_SHELL_SCI_ASYNC_PROBE?sci_async_status:
        files_page?files_status:
        shell.page==KUI_SHELL_GAMES?games_scan_status:
        shell.page==KUI_SHELL_MEMORY?memory_test_status:
        (shell.page==KUI_SHELL_GAMES_PROBE_CONFIRM || shell.page==KUI_SHELL_GAMES_IMAGE_PROBE_CONFIRM ||
         shell.page==KUI_SHELL_GAMES_RETAIL_CONFIRM || shell.page==KUI_SHELL_GAMES_LAUNCH_OPTIONS ||
         shell.page==KUI_SHELL_GAMES_EXTRA_OPTIONS || shell.page==KUI_SHELL_GAMES_TESTS ||
         shell.page==KUI_SHELL_GAMES_INFO || shell.page==KUI_SHELL_GAMES_CHECK)?probe_status:
        shell.page==KUI_SHELL_NETWORK?network_test_status:
        shell.page==KUI_SHELL_MUSIC?player_status:
        shell.page==KUI_SHELL_CRC_SCAN?scan_status:
        shell.page==KUI_SHELL_SALVAGE?salvage_status:
        shell.page==KUI_SHELL_SYSTEM_TOOLS?maintenance_status:
        shell.page==KUI_SHELL_VMU_IMAGE?vmu_image_snapshot.status:
        shell.page==KUI_SHELL_VMU_IMPORT?vmu_import_snapshot.status:
        shell.page==KUI_SHELL_VMU_COLLECTION?vmu_collection_snapshot.status:
        shell.page==KUI_SHELL_VMU_WIZARD?vmu_wizard_snapshot.status:
        shell.page==KUI_SHELL_VMU_ACTIONS && shell.vmu_copy_slot==8 &&
            (shell.vmu_action_selected==1 || shell.vmu_action_selected==3)?vmu_collection_copy_snapshot.status:
        shell.page==KUI_SHELL_VMU_ACTIONS && shell.vmu_action_selected==3?vmu_bulk_snapshot.status:
        shell.page==KUI_SHELL_VMU_RESTORE && active_app!=32 && active_app!=33?vmu_backups.status:vmu_snapshot.status;
    view.phase = capture_status.phase; view.track = capture_status.track; view.tracks = capture_status.tracks;
    view.rate_kib = rate_kib; view.retries = capture_display.retries;
    view.retry_attempt=capture_display.retry_attempt;view.retry_limit=capture_display.retry_limit;
    view.retry_fad=capture_display.retry_fad;
    view.done = capture_status.done; view.total = capture_status.total;
    view.committed = capture_status.committed; view.elapsed_ms = capture_status.elapsed_ms;
    if(shell.page==KUI_SHELL_FTP && ftp_seen) {ftp_view=ftp_status;view.ftp=&ftp_view;}
    unsigned screen_inset=system_current.screen_inset*16u;
    mutex_unlock(&lock);
    view.memory_valid = memory_valid; view.memory_used = memory_status.used;
    view.memory_physical = memory_status.physical; view.memory_peak = memory_status.sampled_peak;
    /* DM_MULTIBUFFER/vid_flip leave vram_s on the next offscreen buffer.
     * Complete the cached frame before its one sequential store-queue upload;
     * sq_wait drains the upload before the buffer can become visible. */
    if(frame) {
        for(unsigned i=0;i<640u*480u;i++) frame[i]=UINT16_C(0x0864);
    } else vid_clear(8,15,35); /* Busy handoff or allocation failure: old path. */
    kui_shell_draw_content(frame?frame:vram_s, &shell, &view, NULL, NULL);
    kui_viewport_inset(frame?frame:vram_s,640,480,screen_inset,0x0864);
    if(frame) {sq_cpy(vram_s,frame,640u*480u*sizeof(*frame));sq_wait();}
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
        case KUI_SHELL_SALVAGE_RESUME:
        case KUI_SHELL_SALVAGE_RESUME_DEFER: return 47;
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
        case KUI_SHELL_GAMES_RETAIL: return 58;
        case KUI_SHELL_GAMES_SCAN: return 59;
        case KUI_SHELL_FILES_LIST: return 60;
        case KUI_SHELL_FILES_CHECK: return 61;
        case KUI_SHELL_FILES_RUN: return 62;
        case KUI_SHELL_FILES_PICTURE: return 63;
        case KUI_SHELL_FTP_START: return 64;
        case KUI_SHELL_TEST_RUN: return 65;
        case KUI_SHELL_TEST_HISTORY: return 66;
        case KUI_SHELL_TEST_BASELINE: return 67;
        case KUI_SHELL_SCI_ASYNC_RUN: return 68;
        case KUI_SHELL_SCI_ASYNC_STRESS: return 69;
        case KUI_SHELL_SCI_ASYNC_SCREEN: return 70;
        case KUI_SHELL_SCI_ASYNC_SPEED: return 71;
        case KUI_SHELL_WIFI_REFRESH: return 72;
        case KUI_SHELL_WIFI_JOIN: return 73;
        case KUI_SHELL_WIFI_FORGET: return 74;
        case KUI_SHELL_WIFI_BAND: return 75;
        case KUI_SHELL_SAVE_GAME_LAUNCH: return 76;
        case KUI_SHELL_GAMES_CHECK_EXECUTABLE: return 79;
        case KUI_SHELL_WIFI_UPDATE_CHECK: return 77;
        case KUI_SHELL_WIFI_UPDATE: return 78;
        case KUI_SHELL_REPAIR_COPY:return 81;
        case KUI_SHELL_REPAIR_IN_PLACE:return 96;
        case KUI_SHELL_REPAIR_NEW_COPY:return 97;
        case KUI_SHELL_FORENSIC:return 82;
        case KUI_SHELL_VMU_BACKUP_CARD:return 83;
        case KUI_SHELL_VMU_IMAGE_LIST:return 84;
        case KUI_SHELL_VMU_IMAGE_EXTRACT:return 85;
        case KUI_SHELL_VMU_IMAGE_EXTRACT_ALL:return 86;
        case KUI_SHELL_FLASH_COMPARE:return 87;
        case KUI_SHELL_BIOS_COMPARE:return 88;
        case KUI_SHELL_VMU_COPY_ALL_PREVIEW:return 89;
        case KUI_SHELL_VMU_COPY_ALL_COMMIT:return 90;
        case KUI_SHELL_VMU_IMPORT_PREVIEW:return 91;
        case KUI_SHELL_VMU_IMPORT_COMMIT:return 92;
        case KUI_SHELL_VMU_COLLECTION_LIST:return 93;
        case KUI_SHELL_VMU_COLLECTION_COPY_PREVIEW:return 94;
        case KUI_SHELL_VMU_COLLECTION_COPY_COMMIT:return 95;
        case KUI_SHELL_VMU_WIZARD_RUN:return 98;
        case KUI_SHELL_VMU_EMULATION_LOAD:return 99;
        case KUI_SHELL_VMU_EMULATION_SAVE:return 100;
        case KUI_SHELL_VMU_RENAME:return 101;
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
#else
    if(state->ltrig>128) buttons|=KUI_BUTTON_BOOT_LEFT;
#endif
    if(state->rtrig>128) buttons|=KUI_BUTTON_BENCH;
    return buttons;
}

#ifndef KUI_SD_RUNTIME
static unsigned boot_buttons(unsigned buttons) {
    unsigned out=0;
    if(buttons&CONT_DPAD_UP) out|=KUI_BOOT_UP;
    if(buttons&CONT_DPAD_DOWN) out|=KUI_BOOT_DOWN;
    if(buttons&(CONT_DPAD_LEFT|KUI_BUTTON_BOOT_LEFT)) out|=KUI_BOOT_LEFT;
    if(buttons&(CONT_DPAD_RIGHT|KUI_BUTTON_BENCH)) out|=KUI_BOOT_RIGHT;
    if(buttons&CONT_A) out|=KUI_BOOT_A;
    if(buttons&CONT_B) out|=KUI_BOOT_B;
    if(buttons&CONT_X) out|=KUI_BOOT_X;
    if(buttons&CONT_Y) out|=KUI_BOOT_Y;
    if(buttons&CONT_START) out|=KUI_BOOT_START;
    return out;
}
static unsigned boot_input_events(unsigned buttons,unsigned pressed,uint64_t now) {
    /* Repeat only vertical log navigation; boot and write actions remain
     * edge-triggered. This also runs while the synchronous loader owns main. */
    unsigned held=boot_ui.page==KUI_BOOT_LOG?
        buttons&(CONT_DPAD_UP|CONT_DPAD_DOWN):0;
    if(held!=boot_held_navigation) {
        boot_held_navigation=held;boot_repeat_at=now+400u;
    } else if(held && now>=boot_repeat_at) {
        pressed|=held;boot_repeat_at=now+80u;
    }
    if(pressed && (buttons&CONT_B)) pressed|=CONT_B;
    return boot_buttons(pressed);
}
static bool boot_cancelled(void) {
    unsigned buttons=controller_buttons();
    uint64_t now=timer_ms_gettime64();
    unsigned pressed=boot_input_events(buttons,buttons&~boot_attempt_previous,now);
    boot_attempt_previous=buttons;
    /* Busy input can only open/scroll the log or request Stop. It cannot
     * change transport, queue a worker, or start a second boot attempt. */
    (void)kui_boot_ui_input(&boot_ui,pressed,now,true,boot_worker_available);
    /* Sticky only for this attempt; retry resets it after B is released. */
    boot_attempt_cancelled=boot_attempt_cancelled || (buttons&CONT_B)!=0;
    mutex_lock(&lock);cancel_requested=boot_attempt_cancelled;mutex_unlock(&lock);
    if(now-boot_last_draw>=125u) draw_boot();
    return boot_attempt_cancelled;
}
#endif

int main(void) {
    kui_storage_boot_begin();
    ui_thread = thd_get_current();
    vid_set_mode(DM_640x480 | DM_MULTIBUFFER, PM_RGB565);
    kui_log("Running " KUI_RELEASE_SHORT " " KUI_ROLE " build " KUI_BUILD_ID);
    kui_clock_start(kui_log);
    kui_log("Video: %ux%u %s %s, buffered",
        (unsigned)vid_mode->width, (unsigned)vid_mode->height,
        vid_mode->cable_type == CT_VGA ? "VGA" :
            (vid_mode->flags & VID_PAL ? "PAL" : "NTSC"),
        vid_mode->flags & VID_INTERLACE ? "interlaced" : "progressive");
#ifndef KUI_SD_RUNTIME
    kui_boot_ui_init(&boot_ui,timer_ms_gettime64());
    kui_log("CD menu: B stays here; X opens recovery; startup boot begins after 3 seconds.");
    kui_log(kui_storage_boot_from_card()?"Card boot: automatic loader override bypassed.":
        "CD autoboot: optional /KUI/boot.kui first; Start K-UI bypasses it.");
    kui_log("Choose Auto, SCIF, SCI or IDE/CF. A retries after inserting an SD card.");
    kui_log("Boot images are read-only. Built-in diagnostics label and confirm writes.");
#endif
    kui_log("Diagnostic code and fonts are loaded entirely in RAM.");
    kui_log("Replace boot CD with a known-good retail GD-ROM; close lid.");
#ifdef KUI_SD_RUNTIME
    kui_log("Diagnostics page: A disc samples; X SD test; Y save log.");
#else
    kui_log("Open Diagnostics for disc samples, storage write/read checks, or log saving.");
#endif
    kui_log("Use a spare test card for write tests. No formatting; existing files preserved.");
#ifdef KUI_SD_RUNTIME
    kui_system_settings_default(&system_current);system_pending=system_current;
    kui_music_init(kui_log);kui_disc_identity_init(&disc_identity);
    kui_disc_identity_park(&disc_identity);disc_snapshot=disc_identity;
    safe_video_boot=(controller_buttons()&CONT_Y)!=0;
    kui_settings_default(&settings_current);
    settings_pending = settings_current;
    kui_shell_init(&shell, &settings_current);
    kui_shell_set_system_preferences(&shell,&system_current);
    kui_destination_default(destination_current);
    snprintf(settings_note,sizeof(settings_note),"Loading preferences from storage...");
    pending = 27; busy = true; splash_active=true;
    splash_deadline=timer_ms_gettime64()+3000;
    kui_log("K-UI launcher: Disc Ripper, VMU, Memory, Network, Settings, Diagnostics, GD Play and Music.");
    kui_log("B skips the original startup splash/chime. Home Y cycles menu music volume/off.");
    kui_log("Ripper: A new dump (confirm), X resume latest matching disc, Y verify.");
    kui_log("B returns home while idle; during work it stops and checkpoints.");
    kui_log("New dumps use game-named folders in /Games; duplicates get a number.");
    kui_log("Home/Ripper L/R: previous/next song. Ripper Start: Advanced, destination and settings.");
    kui_log("Newly inserted GD-ROM titles appear while idle, then the disc stops.");
    kui_log("The boot disc stays stopped; disc apps start the drive when requested.");
    kui_log("System Settings: video, memory display and menu music. Hold Y on runtime start for safe video.");
    kui_log("Reports say if a dump matches Redump/TOSEC (copy data/known-dumps/*.db to KUI/).");
    kui_log("Capture defaults: CRC32, no automatic readback. Y Verify rereads saved files.");
    kui_log("Saved preferences apply first; explicit /KUI/bench.cfg keys override them.");
    kui_log("Capture/Resume/Verify auto-save a report after ending. Wait for READY.");
    kui_log("Diagnostics R trigger: isolated benchmarks from /KUI/bench.cfg.");
    kui_log("The screen redraws 2x a second while working, which frees CPU (ui_hz=full: off).");
#else
    kui_log("Configured benchmarks use /KUI/bench.cfg; B stops safely.");
    kui_log("Bench SD sections write temporary test files; results auto-save to SD.");
    kui_log("Full capture is available in the updated SD runtime.");
#endif
    /* Code, fonts and assets are already in RAM. Show the boot screen before
     * the bounded STOP; no worker or app can own the drive at this point. Do
     * not read the boot disc merely to populate the optional idle title. */
#ifdef KUI_SD_RUNTIME
    draw_shell();
#else
    draw_boot();
#endif
    (void)kui_disc_stop();
    kui_vmu_lcd_startup();
    /* A slow STOP must not consume the interval in which input can cancel
     * automatic startup, nor expire the chime before its worker starts. */
#ifdef KUI_SD_RUNTIME
    splash_deadline=timer_ms_gettime64()+3000;
#else
    kui_boot_ui_init(&boot_ui,timer_ms_gettime64());
#endif
    kthread_attr_t attrs = {.stack_size = 64 * 1024, .label = "kui-io"};
    kthread_t *io_worker=thd_create_ex(&attrs,worker,NULL);
#ifndef KUI_SD_RUNTIME
    boot_worker_available=io_worker!=NULL;
#endif
    if(!io_worker) {
#ifdef KUI_SD_RUNTIME
        kui_log("Unable to start I/O worker; reset console");
        for(;;) {draw_shell();thd_sleep(100);}
#else
        kui_log("Diagnostic worker unavailable; boot, recovery, tools and log viewing remain available.");
        snprintf(boot_notice,sizeof(boot_notice),"Diagnostics unavailable; boot and recovery still work.");
#endif
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
    unsigned seen_storage_test=0,seen_storage_history=0;
    unsigned seen_vmu_delete=0,seen_vmu_copy=0,seen_cd_audio=0,seen_vmu_image=0;
    unsigned seen_games_listing=0,seen_games_detail=0,seen_games_launch_result=0;
    unsigned seen_games_selection=0;
    unsigned seen_files_listing=0,seen_files_preview=0,seen_files_result=0,seen_files_picture=0;
    unsigned seen_wifi_generation=0,seen_vmu_bulk=0,seen_vmu_import=0;
    unsigned seen_vmu_collection=0,seen_vmu_collection_copy=0,seen_vmu_wizard=0,seen_vmu_emulation=0;
    unsigned seen_vmu_game_info=0;
    bool startup_routed=false;
    unsigned held_navigation = 0;
    uint64_t repeat_at = 0;
#endif
    for(;;) {
        unsigned buttons = controller_buttons();
        unsigned pressed = buttons & ~previous;
        previous = buttons;
#ifdef KUI_SD_RUNTIME
        const unsigned navigation = CONT_DPAD_UP | CONT_DPAD_DOWN | CONT_DPAD_LEFT | CONT_DPAD_RIGHT;
        unsigned held = buttons & navigation;
        uint64_t input_at = timer_ms_gettime64();
        pressed=kui_ui_navigation_repeat(held,pressed,&held_navigation,&repeat_at,input_at);
        /* Preserve Stop/cancel priority even if B was held before an A edge. */
        if(pressed && (buttons & CONT_B)) pressed |= CONT_B;
        bool games_ui_changed=false;
        publish_music_if_ready();
        mutex_lock(&lock);
        if(repair_new_copy_created) {
            repair_new_copy_created=false;
            if(shell.recovery_operation==4u)shell.recovery_operation=1u;
        }
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
        if(seen_vmu_image!=vmu_image_generation) {
            kui_shell_set_vmu_image(&shell,vmu_image_path_pending,vmu_image_page_pending,&vmu_image_snapshot);
            seen_vmu_image=vmu_image_generation;
        }
        if(seen_vmu_bulk!=vmu_bulk_generation) {
            kui_shell_set_vmu_copy_all(&shell,vmu_bulk_source_result,vmu_bulk_destination_result,
                vmu_bulk_commit_result,&vmu_bulk_snapshot);seen_vmu_bulk=vmu_bulk_generation;
        }
        if(seen_vmu_import!=vmu_import_generation) {
            kui_shell_set_vmu_import(&shell,vmu_import_path_result,vmu_import_request_result,
                vmu_import_commit_result,&vmu_import_snapshot);seen_vmu_import=vmu_import_generation;
        }
        if(seen_music_listing!=music_listing_generation) {
            kui_shell_set_music_listing(&shell,&music_listing);
            seen_music_listing=music_listing_generation;
        }
        if(seen_games_listing!=games_listing_generation) {
            if(games_result_restore)(void)kui_shell_set_games_found_rows(&shell,&games_listing,games_result_epoch,
                games_result_offset,games_result_view,games_result_selected);
            else (void)kui_shell_set_games_rows(&shell,&games_listing,games_result_epoch,
                games_result_offset,games_result_view);
            seen_games_listing=games_listing_generation;
            games_ui_changed=true;
        }
        if(seen_games_selection!=games_selection_generation) {
            kui_shell_set_games_selection(&shell,&games_selection_loaded);
            seen_games_selection=games_selection_generation;
        }
        if(games_art_result_ready) {
            if(kui_shell_set_games_art(&shell,&games_art_published,&games_art_result)) {
                if(games_art_result.cover) memcpy(games_cover_pixels,games_cover_staging,sizeof(games_cover_pixels));
                games_ui_changed=true;
            }
            games_art_result_ready=false;
        }
        if(games_titles_ready) {
            if(kui_shell_set_games_titles(&shell,&games_titles_page,games_titles_offset)) games_ui_changed=true;
            games_titles_ready=false;
        }
        if(seen_files_result!=files_result_generation) {
            kui_shell_set_files_status(&shell,&files_result);seen_files_result=files_result_generation;
        }
        if(seen_files_listing!=files_listing_generation) {
            kui_shell_set_files_listing(&shell,&files_listing_result);seen_files_listing=files_listing_generation;
        }
        if(seen_files_preview!=files_preview_generation) {
            kui_shell_set_files_preview(&shell,&files_preview_result);seen_files_preview=files_preview_generation;
        }
        if(seen_files_picture!=files_picture_generation) {
            kui_shell_set_files_picture(&shell,&files_picture_result);seen_files_picture=files_picture_generation;
        }
        if(seen_wifi_generation!=wifi_generation) {
            kui_shell_set_wifi(&shell,&wifi_view);seen_wifi_generation=wifi_generation;
        }
        if(seen_games_detail!=games_detail_generation) {
            kui_shell_set_games_detail(&shell,&games_detail);
            kui_shell_set_game_launch_settings(&shell,games_detail.path,
                games_detail_launch_found?&games_detail_launch_settings:NULL,games_detail_launch_loaded);
            seen_games_detail=games_detail_generation;
            games_ui_changed=true;
        }
        if(seen_games_launch_result!=games_launch_result_generation) {
            kui_shell_game_launch_saved(&shell,games_launch_result_path,games_launch_saved);
            seen_games_launch_result=games_launch_result_generation;
            games_ui_changed=true;
        }
        if(seen_clock_generation!=clock_generation) {
            kui_shell_set_clock(&shell,clock_valid?&clock_snapshot:NULL,clock_note);
            seen_clock_generation=clock_generation;
        }
        if(seen_storage_test!=storage_test_generation) {
            kui_shell_set_storage_test_result(&shell,&storage_test_result);
            seen_storage_test=storage_test_generation;
        }
        if(seen_storage_history!=storage_history_generation) {
            kui_shell_set_storage_test_history(&shell,&storage_test_history);
            seen_storage_history=storage_history_generation;
        }
        if(seen_vmu_backups!=vmu_backups_generation) {
            kui_shell_set_vmu_backups(&shell,&vmu_backups);
            seen_vmu_backups=vmu_backups_generation;
        }
        if(seen_vmu_collection!=vmu_collection_generation) {
            kui_shell_set_vmu_collection(&shell,vmu_collection_request_result,&vmu_collection_snapshot);
            seen_vmu_collection=vmu_collection_generation;
        }
        if(seen_vmu_game_info!=vmu_game_info_generation) {
            kui_shell_set_game_vmu(&shell,vmu_game_info_request_result,vmu_game_info_snapshot.product,
                vmu_game_info_snapshot.label,vmu_game_info_snapshot.notice);
            seen_vmu_game_info=vmu_game_info_generation;
        }
        if(seen_vmu_collection_copy!=vmu_collection_copy_generation) {
            kui_shell_set_vmu_collection_copy(&shell,vmu_collection_copy_source_result,
                vmu_collection_copy_page_result,vmu_collection_copy_selected_result,vmu_collection_copy_all_result,
                vmu_collection_copy_request_result,vmu_collection_copy_commit_result,&vmu_collection_copy_snapshot);
            seen_vmu_collection_copy=vmu_collection_copy_generation;
        }
        if(seen_vmu_restore!=vmu_restore_generation) {
            kui_shell_set_vmu_restore_preview(&shell,&vmu_snapshot);
            seen_vmu_restore=vmu_restore_generation;
        }
        if(seen_vmu_wizard!=vmu_wizard_generation) {
            kui_shell_set_vmu_wizard(&shell,&vmu_wizard_snapshot);
            seen_vmu_wizard=vmu_wizard_generation;
        }
        if(seen_vmu_emulation!=vmu_emulation_generation) {
            kui_shell_set_vmu_emulation(&shell,vmu_emulation_result,vmu_emulation_enabled,
                vmu_emulation_ok,vmu_emulation_stopped,vmu_emulation_note);
            seen_vmu_emulation=vmu_emulation_generation;
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
            shell.page=kui_shell_startup_page(system_current.startup_app);
            /* Populate read-only lists through the storage worker. A save,
             * rip or VMU write always needs an explicit controller action. */
            if(shell.page==KUI_SHELL_VMU) {
                vmu_slot_pending=shell.vmu_slot;vmu_page_pending=shell.vmu_page;
                vmu_selected_pending=shell.vmu_selected;pending=16;
            } else if(shell.page==KUI_SHELL_VMU_TOOLS) {
                (void)kui_shell_input(&shell,KUI_SHELL_X,false);
                vmu_emulation_request=shell.vmu_emulation_generation;
                vmu_emulation_requested=shell.vmu_emulation_requested;pending=99;
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
        /* UI-thread ownership: worker snapshots are copied under lock before
         * controller dispatch, then stay immutable while this thread draws. */
        shell.salvage_dashboard=recovery_dashboard;
        enum kui_shell_action requested=KUI_SHELL_NONE;
        enum kui_shell_page page_before_input=shell.page;
        if(splash_active) {if(pressed&CONT_B) {startup_skip=true;splash_active=false;last_draw=0;}}
        else {
            requested=kui_shell_input(&shell,shell_buttons(pressed),busy);
            if(pressed && !busy && startup_finished)
                kui_menu_sound_play(pressed&CONT_A?KUI_MENU_SOUND_CONFIRM:KUI_MENU_SOUND_MOVE);
        }
        if(page_before_input!=shell.page) games_ui_changed=true;
        if(startup_finished) {
            struct kui_games_selection last;
            if(kui_shell_games_selection(&shell,&last) && !kui_games_selection_equal(&last,&games_selection_requested)) {
                games_selection_requested=last;++games_selection_request;games_selection_deadline=input_at+750u;
            }
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
        if(requested==KUI_SHELL_VMU_GAME_INFO && !busy) vmu_game_info_request_start();
        unsigned action = worker_action(requested);
        if(drive_reset_required && (action==1 || (action>=4 && action<=7) || action==22 || action==81 || action==96 || action==97 || (action>=46 && action<=53))) {
            snprintf(capture_message,sizeof(capture_message),
                "Drive reset required. Reboot, then Resume the partial dump.");
            if(action>=46 && action<=48) {
                salvage_defer_pending=false;
                salvage_trial_pending=false;
                salvage_speed_probe_pending=false;
                salvage_diagnostic_pending=KUI_SALVAGE_DIAGNOSTIC_NONE;
                shell.salvage_diagnostic_requested=KUI_SALVAGE_DIAGNOSTIC_NONE;
                shell.salvage_speed_probe_requested=false;
                shell.salvage_trial_requested=shell.salvage_trial_result=false;
                salvage_status=(struct kui_app_status){.complete=true,.errors=1};
                snprintf(salvage_status.message,sizeof(salvage_status.message),"Drive reset required. Use System Tools > Restart.");
            }
            if(action==81 || action==96 || action==97) {scan_status=(struct kui_app_status){.complete=true,.errors=1};
                snprintf(scan_status.message,sizeof(scan_status.message),"Drive reset required. Use System Tools > Restart.");}
            if(action>=49 && action<=53) {
                cd_audio_snapshot.poisoned=true;
                snprintf(cd_audio_snapshot.message,sizeof(cd_audio_snapshot.message),"Drive reset required. Use System Tools > Restart.");
                ++cd_audio_generation;
            }
            action=0;
        }
        if(is_capture_action(action) && !destination_ready && !shell.capture_network &&
           !(action!=4 && shell.capture_job[0])) {
            capture_summary = (struct kui_capture_stats){0};
            capture_status = (struct kui_capture_progress){0};
            capture_outcome = KUI_SHELL_OUTCOME_FAILED;
            kui_shell_destination_error(&shell, "Destination unavailable. Start > Destination to choose a folder.");
            action = 0;
        }
        if(action==76 && !busy) {
            if(kui_shell_game_launch_settings(&shell,&games_launch_settings_pending))
                snprintf(games_launch_path_pending,sizeof(games_launch_path_pending),"%s",shell.games_selected_path);
            else {
                kui_shell_game_launch_saved(&shell,shell.games_selected_path,false);
                action=0;
            }
        }
        if(action && !busy) {
            /* Main is the sole frame owner. Free before exposing pending to
             * the worker; drawing cannot reallocate it while this job is busy. */
            shell_frame_release_for_action(action);
            if(action>=68 && action<=71) {
                sci_async_status=(struct kui_app_status){0};
                snprintf(sci_async_status.message,sizeof(sci_async_status.message),"%s",
                    action==71?"Locating /KUI/runtime.kui, then reading 1 MiB with each reader...":
                    action==70?"Preparing 16 baselines, then 60 seconds of reads with the screen updating...":
                    action==69?"Preparing 16 baselines, then 60 seconds of varied reads...":
                    "Preparing baseline, then slow and fast read trials...");
            }
            if(action==65) {
                storage_test_running=true;
                storage_test_pending=shell.storage_test_request;
                storage_test_progress=(struct kui_storage_test_progress){.preset=storage_test_pending.preset};
                snprintf(storage_test_progress.phase,sizeof(storage_test_progress.phase),"Preparing");
            }
            if(action==67) storage_baseline_pending=shell.storage_test_baseline_id;
            if(action == 10 || action == 11) {
                strcpy(destination_pending, shell.browse_path);
                destination_offset = shell.browser_page * KUI_DEST_PAGE_SIZE;
            }
            if(is_capture_action(action)) {
                strcpy(capture_destination,shell.destination);
                strcpy(capture_job_pending,shell.capture_job);
                strcpy(capture_label_pending,shell.capture_folder_label);
                capture_network_pending=shell.capture_network;
                capture_cd_toc_pending=shell.capture_cd_toc;
            }
            if(action == 9) settings_pending = shell.draft;
            if(action==14) system_pending=shell.system_draft;
            if(action==13 || action==14) snprintf(system_note,sizeof(system_note),"%s",
                action==14?"Saving system settings...":"Loading system settings...");
            if((action>=16 && action<=18) || action==83) {
                vmu_slot_pending=shell.vmu_slot;vmu_page_pending=shell.vmu_page;
                vmu_selected_pending=shell.vmu_selected;
                vmu_snapshot.status=(struct kui_app_status){0};
            }
            if(action==30 || action==81 || action==82 || action==96 || action==97) {
                /* Consume New only after creation. Preparation failures retain
                 * it, while stopped created work uses Resume on this page. */
                shell.recovery_operation=action==97?4u:action==81?1u:action==82?2u:action==96?3u:0u;
                repair_in_place_pending=action==96;
                repair_new_copy_pending=action==97;
                repair_new_copy_created=false;
                repair_saved_identity_pending=shell.repair_saved_identity;
                repair_passes_pending=shell.salvage_passes;
                recovery_cd_pending=shell.capture_cd_toc;
                repair_attempts_pending=shell.saved.retry_limit?shell.saved.retry_limit:10u;
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
                recovery_cd_pending=shell.capture_cd_toc;
                salvage_passes_pending=shell.salvage_passes;
                salvage_drive_pending=shell.salvage_drive_options;
                salvage_trial_pending=shell.salvage_trial_requested;
                salvage_speed_probe_pending=shell.salvage_speed_probe_requested;
                salvage_diagnostic_pending=action==48?shell.salvage_diagnostic_requested:KUI_SALVAGE_DIAGNOSTIC_NONE;
                if(salvage_diagnostic_pending!=KUI_SALVAGE_DIAGNOSTIC_NONE)
                    salvage_trial_pending=salvage_speed_probe_pending=false;
                shell.salvage_diagnostic_requested=KUI_SALVAGE_DIAGNOSTIC_NONE;
                shell.salvage_speed_probe_requested=false;
                shell.salvage_trial_requested=false;
                salvage_verify_pending=shell.salvage_verify_files;
                salvage_defer_pending=requested==KUI_SHELL_SALVAGE_RESUME_DEFER;
                salvage_path_pending[0]=0;
                salvage_status=(struct kui_app_status){0};
            }
            if(action>=49 && action<=53) cd_track_pending=shell.cd_selected<shell.cd_audio.count?
                shell.cd_audio.tracks[shell.cd_selected].number:0;
            if((action>=41 && action<=43) || action==87 || action==88) maintenance_status=(struct kui_app_status){0};
            if(action==87 || action==88)
                memcpy(maintenance_compare_path_pending,shell.system_compare_path,sizeof(maintenance_compare_path_pending));
            if(action==89 || action==90) {
                vmu_slot_pending=shell.vmu_slot;vmu_copy_slot_pending=shell.vmu_copy_slot;
                vmu_bulk_snapshot=shell.vmu_copy_all;vmu_bulk_snapshot.slot=vmu_slot_pending;vmu_bulk_snapshot.copy_ready=false;
                vmu_bulk_snapshot.status=(struct kui_app_status){0};
            }
            if(action==91 || action==92) {
                memcpy(vmu_import_path_pending,shell.vmu_import_path,sizeof(vmu_import_path_pending));
                vmu_import_request_pending=shell.vmu_import_generation;
                vmu_import_snapshot=shell.vmu_import;vmu_import_snapshot.import_ready=false;
                vmu_import_snapshot.status=(struct kui_app_status){0};
            }
            if(action==93 || action==101) {
                vmu_collection_page_pending=shell.vmu_collection_page;
                vmu_collection_request_pending=shell.vmu_collection_generation;
                vmu_collection_snapshot=shell.vmu_collection;
                vmu_collection_snapshot.status=(struct kui_app_status){0};
            }
            if(action==101) {
                memcpy(vmu_name_path_pending,shell.vmu_name_path,sizeof(vmu_name_path_pending));
                memcpy(vmu_name_label_pending,shell.vmu_name_label,sizeof(vmu_name_label_pending));
                memcpy(vmu_name_identity_pending,shell.vmu_name_identity,sizeof(vmu_name_identity_pending));
            }
            if(action==98) {
                vmu_wizard_pending=shell.vmu_wizard_request;
                vmu_wizard_snapshot=shell.vmu_wizard;
                vmu_wizard_snapshot.status=(struct kui_app_status){0};
            }
            if(action==99 || action==100) {
                vmu_emulation_request=shell.vmu_emulation_generation;
                vmu_emulation_requested=shell.vmu_emulation_requested;
            }
            if(action==94 || action==95) {
                vmu_slot_pending=shell.vmu_slot;vmu_page_pending=shell.vmu_page;
                vmu_selected_pending=shell.vmu_selected;
                vmu_collection_copy_all_pending=shell.vmu_collection_copy_all;
                vmu_collection_copy_request_pending=shell.vmu_collection_copy_generation;
                vmu_collection_copy_snapshot=shell.vmu_collection_copy;
                vmu_collection_copy_snapshot.slot=vmu_slot_pending;vmu_collection_copy_snapshot.page=vmu_page_pending;
                vmu_collection_copy_snapshot.copy_ready=false;
                vmu_collection_copy_snapshot.status=(struct kui_app_status){0};
            }
            if(action>=84 && action<=86) {
                memcpy(vmu_image_path_pending,shell.vmu_image_path,sizeof(vmu_image_path_pending));
                vmu_image_page_pending=shell.vmu_image_page;vmu_image_selected_pending=shell.vmu_image_selected;
                vmu_image_snapshot=shell.vmu_image;vmu_image_snapshot.page=vmu_image_page_pending;
                vmu_image_snapshot.status=(struct kui_app_status){0};
            }
            if(action==35) clock_pending=shell.clock_draft;
            if(action==23 || action==24) {
                snprintf(music_path_pending,sizeof(music_path_pending),"%s",
                    action==23?shell.music_path:shell.music_selected_path);
                music_offset_pending=shell.music_page*KUI_MUSIC_PLAYER_ROWS;
                player_status=(struct kui_app_status){0};
            }
            if(action==54 || action==55 || action==57 || action==58 || action==79) {
                snprintf(games_path_pending,sizeof(games_path_pending),"%s",
                    action==54?shell.games_path:shell.games_selected_path);
                games_offset_pending=shell.games_page*KUI_GAMES_ROWS;
                games_reader_pending=shell.games_retail_reader;
                games_patches_pending=shell.games_launch_patches;
            }
            if(action==55) {games_codec_pending=shell.games_launch_codec;games_codec_override_pending=shell.games_codec_override;}
            if(action==54 || action==59) {games_view_pending=shell.games_view;games_epoch_pending=shell.games_generation;}
            if(action==54) games_refresh_pending=shell.games_refresh;
            if(action==54)snprintf(games_seek_pending,sizeof(games_seek_pending),"%s",shell.games_seek_name);
            if(action==54)games_seek_request=shell.games_seek;
            if(action>=60 && action<=63) {
                files_request_pending=shell.files_request;
                files_job_pending=shell.files_job;
                if(action==62) files_totals_pending=shell.files_preview;
                if(action==63) memcpy(files_picture_pending,shell.files_picture.path,sizeof(files_picture_pending));
                files_status=(struct kui_app_status){0};
            }
            if(action==24) files_music=shell.page==KUI_SHELL_FILES;
            if(action==64) {
                ftp_status=(struct kui_ftp_status){0};
                snprintf(ftp_status.message,sizeof(ftp_status.message),"Starting");
                ftp_seen=true;
            }
            if((action>=72 && action<=75) || action==77 || action==78) {
                wifi_request_pending=shell.wifi_request;
                wifi_request_pending.action=action==72?KUI_WIFI_REFRESH:action==73?KUI_WIFI_JOIN:
                    action==74?KUI_WIFI_FORGET:action==75?KUI_WIFI_BAND:
                    action==77?KUI_WIFI_UPDATE_CHECK:KUI_WIFI_UPDATE;
                /* The password lives only in the job now. */
                memset(shell.wifi_request.password,0,sizeof(shell.wifi_request.password));
                if(action==77 || action==78) {
                    wifi_view.working=true;
                    wifi_view.failed=false;
                    wifi_view.firmware_ready=false;
                    wifi_view.firmware_updating=action==78;
                    wifi_view.firmware_committing=false;
                    wifi_view.firmware_done=wifi_view.firmware_total=0;
                    snprintf(wifi_view.message,sizeof(wifi_view.message),"%s",action==77?
                        "Checking the Wi-Fi firmware image on the card":"Preparing Wi-Fi firmware update");
                    ++wifi_generation;
                }
            }
            if(action==59) {
                games_scan_status=(struct kui_app_status){0};
                snprintf(games_scan_status.message,sizeof(games_scan_status.message),"Finding games...");
            }
            if(action==79) {
                probe_status=(struct kui_app_status){0};
                snprintf(probe_status.message,sizeof(probe_status.message),"Reading executable, checking its package and preparing the launch map...");
            }
            if(action==56 || action==57 || action==58) {
                probe_status=(struct kui_app_status){0};
                snprintf(probe_status.message,sizeof(probe_status.message),"%s",action==58?
                    "Preparing selected game launch...":action==57?
                    "Mapping selected image and reading reference samples...":
                    "Preparing resident probe and SD map...");
                probe_launch_failed=false;
            }
            if(action==19) memory_test_status=(struct kui_app_status){0};
            if(action==20 || action==45) network_test_status=(struct kui_app_status){0};
            if(action == 8 || action == 9)
                snprintf(settings_note, sizeof(settings_note), "%s", action == 8 ?
                    "Loading preferences from storage..." : "Saving preferences to SD...");
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
        struct kui_shell_games_art_key art_key;
        bool art_wanted=!busy && kui_shell_games_art_key(&shell,&art_key);
        if(art_wanted) {
            games_art_requested=art_key;
            games_art_entry=shell.games_listing.entries[art_key.row];
            bool loading=!games_art_done_valid || !kui_shell_games_art_same(&games_art_done,&art_key);
            if(shell.games_art_loading!=loading) games_ui_changed=true;
            shell.games_art_loading=loading;
        } else shell.games_art_loading=false;
        games_art_request_valid=art_wanted;
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
            kui_log("Games handoff refused: %s",safe?"cancelled":"unsafe staging range");
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
        /* Only the main thread updates menu state. A CD worker has no idle
         * peripheral activity, and busy remains set until its cleanup ends. */
        uint64_t input_at=timer_ms_gettime64();
        unsigned menu_input=boot_input_events(buttons,pressed,input_at);
        mutex_lock(&lock);bool working=busy;mutex_unlock(&lock);
        if(was_busy && !working)
            snprintf(boot_notice,sizeof(boot_notice),"Operation finished. View Log for the result.");
        enum kui_boot_action requested=kui_boot_ui_input(&boot_ui,menu_input,
            input_at,working,boot_worker_available);
        if(requested==KUI_BOOT_STOP) {
            mutex_lock(&lock);if(busy) cancel_requested=true;mutex_unlock(&lock);
        } else if(requested==KUI_BOOT_RUNTIME || requested==KUI_BOOT_RECOVERY ||
                  requested==KUI_BOOT_TOOLS || requested==KUI_BOOT_MEASURE ||
                  requested==KUI_BOOT_AUTOBOOT) {
            mutex_lock(&lock);
            bool claimed=!busy && !pending && !(buttons&CONT_B);
            if(claimed) {busy=true;cancel_requested=false;ui_hz_busy=2;}
            mutex_unlock(&lock);
            if(claimed) {
                enum kui_boot_mode mode=requested==KUI_BOOT_RECOVERY?KUI_BOOT_MODE_RECOVERY:
                    requested==KUI_BOOT_TOOLS?KUI_BOOT_MODE_TOOLS:
                    requested==KUI_BOOT_AUTOBOOT?kui_boot_autostart_mode(kui_storage_boot_from_card()):
                    KUI_BOOT_MODE_NORMAL;
                boot_ui.autoboot_until=0;boot_attempt_cancelled=false;
                boot_attempt_previous=controller_buttons();boot_held_navigation=0;
                bool measure=requested==KUI_BOOT_MEASURE;
                boot_draw_us=0;boot_draw_count=0;
                if(measure) {
                    boot_ui.page=KUI_BOOT_LOG;boot_ui.return_page=KUI_BOOT_DIAGNOSTICS;
                    boot_ui.scroll=boot_ui.log_column=0;
                }
                snprintf(boot_notice,sizeof(boot_notice),"Reading selected boot image...");
                kui_log("%s boot: %s from %s",kui_storage_boot_from_card()?"Card":"CD",
                    requested==KUI_BOOT_RECOVERY?"recovery":requested==KUI_BOOT_TOOLS?"card tools":
                    measure?"measure runtime":mode==KUI_BOOT_MODE_AUTOBOOT?"optional loader/runtime":"runtime",
                    kui_storage_name(boot_ui.transport));
                draw_boot();
                enum kui_runtime_result result=measure?kui_bootstrap_measure(boot_ui.transport,boot_cancelled):
                    kui_bootstrap_start(boot_ui.transport,mode,boot_cancelled);
                /* Successful handoff never returns. Failed/cancelled attempts
                 * release storage before the menu permits another action. */
                mutex_lock(&lock);busy=false;cancel_requested=false;ui_hz_busy=KUI_OPT_UI_FULL;mutex_unlock(&lock);
                if(measure) {
                    kui_log("Boot screen: %u redraws, %" PRIu64 " ms drawing/waiting",boot_draw_count,boot_draw_us/1000u);
                    kui_log("Load measurement: %s; runtime was not started",kui_runtime_result_name(result));
                    snprintf(boot_notice,sizeof(boot_notice),"Measurement finished. B returns to Diagnostics.");
                    boot_ui.page=KUI_BOOT_LOG;boot_ui.return_page=KUI_BOOT_DIAGNOSTICS;
                    boot_ui.selected=4;boot_ui.scroll=boot_ui.log_column=0;
                } else {
                    snprintf(boot_notice,sizeof(boot_notice),"%s. Insert/check the card, then press A to retry.",
                        kui_runtime_result_name(result));
                    boot_ui.page=KUI_BOOT_HOME;boot_ui.selected=requested==KUI_BOOT_AUTOBOOT?0u:
                        (unsigned)requested-(unsigned)KUI_BOOT_RUNTIME;
                }
                previous=controller_buttons();boot_held_navigation=0;last_draw=0;was_busy=false;
            }
        } else {
            unsigned action=requested==KUI_BOOT_PROBE?1u:requested==KUI_BOOT_WRITE_TEST?2u:
                requested==KUI_BOOT_SAVE_LOG?3u:requested==KUI_BOOT_BENCH?7u:0u;
            if(action && boot_worker_available) {
                mutex_lock(&lock);
                if(!busy && !pending && !(buttons&CONT_B)) {
                    kui_sd_set_params(boot_ui.transport,true);
                    pending=action;busy=true;cancel_requested=false;ui_hz_busy=2;
                    boot_ui.scroll=boot_ui.log_column=0;
                    boot_ui.return_page=KUI_BOOT_DIAGNOSTICS;boot_ui.page=KUI_BOOT_LOG;
                }
                mutex_unlock(&lock);
            }
        }
        mutex_lock(&lock);bool is_busy=busy;mutex_unlock(&lock);
#endif
        /* Idle controls on every shell page run independently of the 33 ms
         * redraw cadence. Busy: at most ui_hz_busy redraws per
         * second, plus one at each start and end so the screen is never stale
         * about what state it is in. The controller is polled every loop either
         * way, so B still stops an operation whatever the cap. Keyed on when
         * the last draw happened, not on a precomputed deadline, so a cap that
         * changes mid-operation (the bench changes it between passes) applies at
         * once instead of waiting out the schedule the old value set. */
        unsigned hz = ui_hz_busy;
        uint64_t t = timer_ms_gettime64();
#ifdef KUI_SD_RUNTIME
        bool menu_drew=false;
        bool due = kui_ui_menu_redraw_due(is_busy,was_busy,hz,
            pressed || games_ui_changed,t,last_draw);
#else
        bool due = kui_ui_redraw_due(is_busy,was_busy,hz,t,last_draw);
#endif
        was_busy = is_busy;
#ifdef KUI_SD_RUNTIME
        mutex_lock(&lock);
        uint32_t quiet_generation;bool quiet_pending,quiet_skipped;
        due=kui_sci_video_quiet_draw_due(&sci_video_quiet,due,&quiet_generation,&quiet_pending,&quiet_skipped);
        if(quiet_skipped) last_draw=t;
        mutex_unlock(&lock);
#endif
        if(due) {
#ifdef KUI_SD_RUNTIME
            draw_shell();
            menu_drew=true;
            if(quiet_pending) {
                /* This is the sole drawing thread. Complete every store queue
                 * write from this and prior frames before releasing the worker.
                 * This handoff adds no IRQ mask around drawing or sq_wait. */
                sq_wait();
                mutex_lock(&lock);
                (void)kui_sci_video_quiet_ack(&sci_video_quiet,quiet_generation);
                mutex_unlock(&lock);
            }
#else
            draw_boot();
#endif
#ifdef KUI_SD_RUNTIME
            /* Count the idle interval from the completed frame. Timestamping
             * before an expensive paint/vblank can make its next background
             * repaint immediately due, spending each loop drawing again. */
            last_draw=is_busy?t:timer_ms_gettime64();
#else
            last_draw = t;
#endif
        }
#ifdef KUI_SD_RUNTIME
        thd_sleep(kui_ui_menu_wait_ms(is_busy,menu_drew));
#else
        thd_sleep(33);
#endif
    }
}
