/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_UPDATE_BOOT_H
#define KUI_UPDATE_BOOT_H
#include "kui/runtime.h"
#include "kui/hash.h"

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
 * PINNED binds later module loads to this bank. ERROR blocks module loads.
 * A bank is never silently combined with root modules. */
enum kui_update_boot_result kui_update_boot_read(const char *compiled_build,
    unsigned transport,struct kui_runtime_image *out,struct kui_update_boot_view *view,
    kui_log_fn log,kui_cancel_fn cancel);
/* Safe manual bypass: a bank GUI chains to the root GUI rather than merely
 * clearing its bank pin and continuing with root modules. */
enum kui_update_boot_result kui_update_boot_baseline(const char *compiled_build,
    unsigned transport,struct kui_runtime_image *out,struct kui_update_boot_view *view,
    kui_log_fn log,kui_cancel_fn cancel);
void kui_update_boot_reset(void);
/* Call on startup mount/release/staging failures before the helper can run,
 * or if its checked chain image cannot be handed off. */
void kui_update_boot_block(void);

/* Optional runtime_file.c hook. Legacy bootstrap/fixture links omit this
 * implementation and retain their original root-path behavior. Verification
 * callbacks keep SHA dependencies in the updater implementation; runtime_file
 * feeds the same open stream's header and exact payload through them. */
struct kui_update_runtime_stream {
    bool pinned;
    uint64_t bytes;
    char build[13];
    uint8_t expected[32];
    struct kui_sha256 hash;
    void (*update)(struct kui_update_runtime_stream *,const void *,size_t);
    bool (*finish)(const struct kui_update_runtime_stream *);
};
bool kui_update_runtime_resolve(const char *logical,char *resolved,size_t capacity,
    struct kui_update_runtime_stream *stream);
#endif
