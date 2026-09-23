/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SHELL_H
#define KUI_SHELL_H
#include "kui/settings.h"
#include "kui/destination.h"
#include "kui/known_dumps.h"
#include "kui/system_settings.h"
#include "kui/apps.h"
#include "kui/music_player.h"
#include <stdbool.h>
#include <stdint.h>

#define KUI_SHELL_WIDTH 640u
#define KUI_SHELL_HEIGHT 480u
#define KUI_SHELL_LOG_ROWS 10u
enum kui_shell_button {
    KUI_SHELL_UP = 1u << 0, KUI_SHELL_DOWN = 1u << 1,
    KUI_SHELL_LEFT = 1u << 2, KUI_SHELL_RIGHT = 1u << 3,
    KUI_SHELL_A = 1u << 4, KUI_SHELL_B = 1u << 5,
    KUI_SHELL_X = 1u << 6, KUI_SHELL_Y = 1u << 7,
    KUI_SHELL_START = 1u << 8, KUI_SHELL_L = 1u << 9,
    KUI_SHELL_R = 1u << 10
};
enum kui_shell_page { KUI_SHELL_HOME, KUI_SHELL_RIPPER,
    KUI_SHELL_SETTINGS, KUI_SHELL_DIAGNOSTICS,
    KUI_SHELL_DESTINATION, KUI_SHELL_KEYBOARD, KUI_SHELL_ADVANCED,
    KUI_SHELL_RIPPER_SETTINGS, KUI_SHELL_VMU, KUI_SHELL_MEMORY, KUI_SHELL_NETWORK,
    KUI_SHELL_GD_PLAY, KUI_SHELL_MUSIC };
enum kui_shell_action {
    KUI_SHELL_NONE, KUI_SHELL_STOP, KUI_SHELL_MSTATS,
    KUI_SHELL_DISC_PROBE, KUI_SHELL_STORAGE_PROBE, KUI_SHELL_SAVE_LOG,
    KUI_SHELL_NEW_DUMP, KUI_SHELL_RESUME, KUI_SHELL_VERIFY,
    KUI_SHELL_BENCH, KUI_SHELL_LOAD_SETTINGS, KUI_SHELL_SAVE_SETTINGS,
    KUI_SHELL_DISCARD_SETTINGS, KUI_SHELL_DEST_LIST, KUI_SHELL_DEST_SAVE,
    KUI_SHELL_MEMORY_TEST, KUI_SHELL_NETWORK_TEST, KUI_SHELL_VMU_LIST,
    KUI_SHELL_VMU_BACKUP, KUI_SHELL_VMU_BACKUP_ALL, KUI_SHELL_LOAD_SYSTEM,
    KUI_SHELL_SAVE_SYSTEM, KUI_SHELL_DISCARD_SYSTEM, KUI_SHELL_PREVIEW_VIDEO,
    KUI_SHELL_CONFIRM_VIDEO, KUI_SHELL_CANCEL_VIDEO, KUI_SHELL_MUSIC_NEXT,
    KUI_SHELL_MUSIC_CYCLE, KUI_SHELL_RESUME_QUICK, KUI_SHELL_GD_BOOT,
    KUI_SHELL_MUSIC_LIST, KUI_SHELL_MUSIC_PLAY
};
enum kui_shell_outcome { KUI_SHELL_OUTCOME_NONE, KUI_SHELL_OUTCOME_COMPLETE,
    KUI_SHELL_OUTCOME_STOPPED, KUI_SHELL_OUTCOME_FAILED };
struct kui_shell {
    enum kui_shell_page page;
    unsigned home_selected, setting_selected, scroll;
    bool confirm_new, confirm_quick_resume, confirm_gd_boot;
    struct kui_settings saved, draft;
    struct kui_system_settings system_saved, system_draft;
    unsigned system_selected;
    bool video_trial;
    unsigned vmu_slot, vmu_page, vmu_selected;
    struct kui_vmu_view vmu;
    char music_path[KUI_DEST_ROOT_CAP], music_selected_path[KUI_DEST_ROOT_CAP];
    unsigned music_page, music_selected;
    struct kui_music_player_page music_listing;
    /* Destination is committed only by a successful worker load/save. Browsing
     * and typing are drafts; neither changes where a new capture is written. */
    char destination[KUI_DEST_ROOT_CAP], browse_path[KUI_DEST_ROOT_CAP];
    char keyboard[KUI_DEST_ROOT_CAP], keyboard_original[KUI_DEST_ROOT_CAP];
    char destination_notice[128];
    struct kui_destination_page listing;
    unsigned browser_selected, browser_page, keyboard_selected;
    unsigned advanced_selected;
    enum kui_shell_page settings_return;
    bool keyboard_upper;
};
void kui_shell_init(struct kui_shell *shell, const struct kui_settings *settings);
/* Main owns the reducer. Pass new button edges; a held B must also be included
 * when another edge arrives, so Stop/cancel retains priority over a new action.
 * Busy includes queued work and settings I/O. It locks launch/navigation but
 * permits Stop, memory snapshots and scrolling the diagnostics already open. */
enum kui_shell_action kui_shell_input(struct kui_shell *shell,
    unsigned pressed, bool busy);
/* Install only a successfully loaded/saved settings snapshot. A failed save
 * leaves draft values available for retry and the prior saved values intact. */
void kui_shell_set_preferences(struct kui_shell *shell,
    const struct kui_settings *settings);
bool kui_shell_settings_dirty(const struct kui_shell *shell);
void kui_shell_set_system_preferences(struct kui_shell *shell,
    const struct kui_system_settings *settings);
bool kui_shell_system_dirty(const struct kui_shell *shell);
/* Main controls video_trial only while its reversible platform preview is
 * active. The reducer returns CONFIRM/CANCEL; it never commits a video mode. */
void kui_shell_set_vmu(struct kui_shell *shell, const struct kui_vmu_view *view);
/* MUSIC_LIST uses music_path and music_page * ROWS. MUSIC_PLAY uses the
 * validated music_selected_path. Worker completion never changes directory. */
void kui_shell_set_music_listing(struct kui_shell *shell,
    const struct kui_music_player_page *page);
/* DEST_LIST reads browse_path and browser_page (offset = page * PAGE_SIZE).
 * DEST_SAVE reads browse_path. Main owns the generation check before installing
 * worker results; these functions themselves perform no filesystem I/O. */
void kui_shell_set_destination(struct kui_shell *shell, const char *path);
void kui_shell_set_listing(struct kui_shell *shell,
    const struct kui_destination_page *page);
void kui_shell_destination_error(struct kui_shell *shell, const char *message);
/* Keyboard has four QWERTY/digit rows of ten keys, then SPACE/BACK/DONE.
 * Only directions may be repeated; A and the other action buttons are edges. */
#define KUI_SHELL_KEY_COUNT 43u
const char *kui_shell_key_label(unsigned key, bool uppercase);

/* Main copies shared worker state while locked, then draws outside the lock.
 * Pointer fields remain valid for this draw. log_lines contains up to LOG_ROWS
 * already-scrolled lines; total_log_lines is the count before windowing.
 * phase uses the existing kui_capture_phase numeric values (0..4); phase 4 is
 * completed, never an assertion that saved bytes were verified. */
struct kui_shell_view {
    const char *build, *job_dir, *message, *settings_notice;
    const char *disc_title, *inserted_title, *gdi_name;
    const char *music_title, *music_notice;
    const struct kui_app_status *app_status;
    bool busy, saving, cancel_requested, saved_verified, memory_valid;
    bool log_truncated, reference_checked, video_trial, drive_reset_required;
    bool music_enabled, music_playing, music_paused;
    unsigned music_volume;
    unsigned video_seconds;
    struct kui_known_summary reference;
    enum kui_shell_outcome outcome;
    unsigned phase, track, tracks, rate_kib, retries;
    uint64_t done, total, committed, elapsed_ms;
    uint64_t phase_elapsed_ms, progress_age_ms;
    uint32_t memory_used, memory_physical, memory_peak;
    const char *const *log_lines;
    unsigned log_count, total_log_lines;
};
/* Estimate only the current moving phase after a 2s warmup; do not imply the
 * later verification duration. A stalled (>3s old) rate is not an estimate. */
bool kui_shell_phase_eta(const struct kui_shell_view *view, uint64_t *seconds);
/* The renderer draws its embedded font and original artwork directly. Text is
 * clipped to safe margins. An optional observer receives rendered labels for
 * accessibility/host checks; it must not draw a second font over them. No flip,
 * wait, device access or allocation occurs here. */
typedef void (*kui_shell_text_fn)(void *ctx, unsigned x, unsigned y,
    uint16_t rgb565, const char *text, bool large);
void kui_shell_draw(uint16_t *frame, const struct kui_shell *shell,
    const struct kui_shell_view *view, kui_shell_text_fn text, void *ctx);
/* Console path: the caller first clears its offscreen framebuffer to RGB565
 * 0x0864 with KOS vid_clear(8,15,35). Keeping that optimized store-queue clear
 * outside the portable renderer avoids 307,200 ordinary CPU pixel stores. */
void kui_shell_draw_content(uint16_t *frame, const struct kui_shell *shell,
    const struct kui_shell_view *view, kui_shell_text_fn text, void *ctx);
#endif
