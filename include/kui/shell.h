/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SHELL_H
#define KUI_SHELL_H
#include "kui/settings.h"
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
    KUI_SHELL_SETTINGS, KUI_SHELL_DIAGNOSTICS };
enum kui_shell_action {
    KUI_SHELL_NONE, KUI_SHELL_STOP, KUI_SHELL_MSTATS,
    KUI_SHELL_DISC_PROBE, KUI_SHELL_STORAGE_PROBE, KUI_SHELL_SAVE_LOG,
    KUI_SHELL_NEW_DUMP, KUI_SHELL_RESUME, KUI_SHELL_VERIFY,
    KUI_SHELL_BENCH, KUI_SHELL_LOAD_SETTINGS, KUI_SHELL_SAVE_SETTINGS,
    KUI_SHELL_DISCARD_SETTINGS
};
enum kui_shell_outcome { KUI_SHELL_OUTCOME_NONE, KUI_SHELL_OUTCOME_COMPLETE,
    KUI_SHELL_OUTCOME_STOPPED, KUI_SHELL_OUTCOME_FAILED };
struct kui_shell {
    enum kui_shell_page page;
    unsigned home_selected, setting_selected, scroll;
    bool confirm_new;
    struct kui_settings saved, draft;
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

/* Main copies shared worker state while locked, then draws outside the lock.
 * Pointer fields remain valid for this draw. log_lines contains up to LOG_ROWS
 * already-scrolled lines; total_log_lines is the count before windowing.
 * phase uses the existing kui_capture_phase numeric values (0..4); phase 4 is
 * completed, never an assertion that saved bytes were verified. */
struct kui_shell_view {
    const char *build, *job_dir, *message, *settings_notice;
    bool busy, saving, cancel_requested, saved_verified, memory_valid;
    bool log_truncated;
    enum kui_shell_outcome outcome;
    unsigned phase, track, tracks, rate_kib, retries;
    uint64_t done, total, committed, elapsed_ms;
    uint32_t memory_used, memory_physical, memory_peak;
    const char *const *log_lines;
    unsigned log_count, total_log_lines;
};
/* Text is clipped to RF-safe margins and uses the existing 8x16 console font.
 * Main's callback sets minifont's RGB color and draws into the same framebuffer.
 * The renderer only draws: it never flips, waits, reads devices or allocates. */
typedef void (*kui_shell_text_fn)(void *ctx, unsigned x, unsigned y,
    uint16_t rgb565, const char *text);
void kui_shell_draw(uint16_t *frame, const struct kui_shell *shell,
    const struct kui_shell_view *view, kui_shell_text_fn text, void *ctx);
#endif
