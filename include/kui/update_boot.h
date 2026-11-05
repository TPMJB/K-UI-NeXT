/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_UPDATE_BOOT_H
#define KUI_UPDATE_BOOT_H
#include "kui/runtime.h"
#include "kui/hash.h"
#include "kui/update_handoff.h"

#define KUI_UPDATE_BOOT_PATH_BYTES 192u
enum kui_update_boot_result {
    KUI_UPDATE_BOOT_BASELINE, KUI_UPDATE_BOOT_PINNED,
    KUI_UPDATE_BOOT_CHAIN, KUI_UPDATE_BOOT_ERROR
};
struct kui_update_boot_view {
    char bank[13],build[13],notice[128];
    bool fallback;
};
/* Caller owns one mounted volume before any other app worker starts. No
 * device discovery, writes or execution occur here. CHAIN owns out->data and
 * has the already selected transport patched into the checked RAM image;
 * caller must release storage and apply its normal staging/stack guards.
 * PINNED binds replacement module loads to this bank. ERROR blocks loads.
 * Explicit policy2 entries retain shared root modules and normal root checks;
 * missing/corrupt replacement entries never fall back to root modules. */
enum kui_update_boot_result kui_update_boot_read(const char *compiled_build,
    unsigned transport,struct kui_runtime_image *out,struct kui_update_boot_view *view,
    kui_log_fn log,kui_cancel_fn cancel);
/* Optional RAM-only transfer context. A matching checked chain can adopt its
 * mounted receipt after a header + EMPTY-slot read; every mismatch requires
 * the full runtime SHA/CRC path. The context is never written to storage. */
enum kui_update_boot_result kui_update_boot_read_handoff(const char *compiled_build,
    unsigned transport,const struct kui_update_handoff_context *,
    struct kui_runtime_image *out,struct kui_update_boot_view *view,
    kui_log_fn log,kui_cancel_fn cancel);
/* Restart the GUI selected NOW, fully rereading it even when its build equals
 * the running GUI. Returns CHAIN or ERROR; never uses a RAM handoff shortcut.
 * A failed reload restores the running module pin. After CHAIN, caller must
 * release storage, quiesce services and apply the main-thread RAM/stack guard.
 * If execution is cancelled/refused/returns, free out and call reload_abort
 * before resuming the current GUI. No selection record is written. */
enum kui_update_boot_result kui_update_boot_reload(const char *compiled_build,
    unsigned transport,struct kui_runtime_image *out,struct kui_update_boot_view *view,
    kui_log_fn log,kui_cancel_fn cancel);
void kui_update_boot_reload_abort(void);
/* Root/direct-entry helper for callers without a captured RAM handoff.
 * Startup bypass with a handoff must use the context-aware variant below. */
enum kui_update_boot_result kui_update_boot_baseline(const char *compiled_build,
    unsigned transport,struct kui_runtime_image *out,struct kui_update_boot_view *view,
    kui_log_fn log,kui_cancel_fn cancel);
/* Startup bypass must preserve its RAM provenance. A filled or damaged bank
 * handoff forces root execution even if the GUI build strings are equal. */
enum kui_update_boot_result kui_update_boot_baseline_handoff(const char *compiled_build,
    unsigned transport,const struct kui_update_handoff_context *,
    struct kui_runtime_image *out,struct kui_update_boot_view *view,
    kui_log_fn log,kui_cancel_fn cancel);
void kui_update_boot_reset(void);
/* Production GUI calls this before startup selection. The required engine
 * level only increases for the lifetime of this GUI; legacy host fixtures
 * that do not opt in preserve their original protocol admission behavior. */
void kui_update_boot_require_engine(unsigned level);
/* Effective running selection for constructing a subsequent partial update.
 * Returns 0 before the production context opt-in, -1 when loads are blocked,
 * or +1 with the bank ID + receipt SHA (empty/zero means retained root). A
 * failed/newer saved activation must never override this checked fallback. */
int kui_update_effective_snapshot(char bank[13],uint8_t manifest_sha256[32]);
/* Call on startup mount/release/staging failures before the helper can run,
 * or if its checked chain image cannot be handed off. */
void kui_update_boot_block(void);

/* Optional runtime_file.c hook. Legacy bootstrap/fixture links omit this
 * implementation and retain their original root-path behavior. Verification
 * callbacks keep SHA dependencies in the updater implementation; runtime_file
 * feeds the same open stream's header and exact payload through them. */
struct kui_update_runtime_stream {
    bool pinned,raw;
    uint64_t bytes;
    char build[13];
    uint8_t expected[32];
    struct kui_sha256 hash;
    uint32_t (*crc32)(uint32_t,const void *,size_t);
    void (*update)(struct kui_update_runtime_stream *,const void *,size_t);
    bool (*finish)(const struct kui_update_runtime_stream *);
};
bool kui_update_runtime_resolve(const char *logical,char *resolved,size_t capacity,
    struct kui_update_runtime_stream *stream);
/* True for a checked bank's explicit tombstone or an omitted FULL software
 * path. Ordinary root absence, invalid paths and a blocked resolver are false. */
bool kui_update_runtime_absent(const char *logical);
#endif
