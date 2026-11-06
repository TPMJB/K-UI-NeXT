/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SMASH_LAUNCH_TIMING_H
#define KUI_SMASH_LAUNCH_TIMING_H
#include "kui/probe.h"
#include "kui/smash_phase_clock.h"
#ifndef KUI_SMASH_LAUNCH_TIMING
#define KUI_SMASH_LAUNCH_TIMING 0
#endif
#if KUI_SMASH_LAUNCH_TIMING != 0 && KUI_SMASH_LAUNCH_TIMING != 1
#error "KUI_SMASH_LAUNCH_TIMING must be zero or one"
#endif
#define KUI_SMASH_LAUNCH_TIMING_PATH "0:/KUI/apps/games/smash-launch-timing.txt"
#define KUI_NATIVE_LAUNCH_TIMING_PATH "0:/KUI/apps/games/launch-timing.txt"
enum kui_smash_ui_component {
    KUI_SMASH_UI_COMPONENT_MENU,KUI_SMASH_UI_COMPONENT_NATIVE,
    KUI_SMASH_UI_COMPONENT_VC2,KUI_SMASH_UI_COMPONENT_SWIRL,KUI_SMASH_UI_COMPONENTS
};
enum kui_smash_ui_phase {
    KUI_SMASH_UI_REQUEST_SETUP,KUI_SMASH_UI_METADATA,KUI_SMASH_UI_MENU_PROOF,
    KUI_SMASH_UI_TRACK_MAP,KUI_SMASH_UI_PREPARE_OTHER,KUI_SMASH_UI_CHILD_MODULE,
    KUI_SMASH_UI_MDE_READ_HASH,KUI_SMASH_UI_MDE_EXTENT,KUI_SMASH_UI_MDE_PROOF,
    KUI_SMASH_UI_CARD,KUI_SMASH_UI_CAPSULES,KUI_SMASH_UI_REPORT_CLEANUP,
    KUI_SMASH_UI_POST_PREPARE,KUI_SMASH_UI_ACTIVITY,KUI_SMASH_UI_AUDIO,
    KUI_SMASH_UI_MODULE_LOAD,KUI_SMASH_UI_MODULE_VERIFY,KUI_SMASH_UI_IP_CHECK,
    KUI_SMASH_UI_SESSION_PREPARE,KUI_SMASH_UI_FILE_CLEANUP,
    KUI_SMASH_UI_PHASES
};
enum kui_smash_ui_detail {
    KUI_SMASH_UI_HISTORY_TOTAL,KUI_SMASH_UI_HISTORY_RESUME,KUI_SMASH_UI_HISTORY_PRELAUNCH,
    KUI_SMASH_UI_HISTORY_RECEIPT,KUI_SMASH_UI_PROFILE_CACHE,KUI_SMASH_UI_RAW_FS,
    KUI_SMASH_UI_SNAPSHOT,KUI_SMASH_UI_PROFILE_SAVE,KUI_SMASH_UI_SDK_REPLAY,
    KUI_SMASH_UI_SDK_DISCOVERY,KUI_SMASH_UI_CAPSULE_INSPECT,KUI_SMASH_UI_CAPSULE_CHILD,
    KUI_SMASH_UI_CAPSULE_OUTER,KUI_SMASH_UI_EARLY_RESUME,
    KUI_SMASH_UI_EXEC_READ,KUI_SMASH_UI_EXEC_SHA,KUI_SMASH_UI_EXEC_CRC,
    KUI_SMASH_UI_PROGRAM_EXTENT,KUI_SMASH_UI_DETAILS
};
enum kui_smash_ui_metric {
    KUI_SMASH_UI_CACHE_HIT,KUI_SMASH_UI_MDE_BYTES,KUI_SMASH_UI_PROFILE_HIT,
    KUI_SMASH_UI_RAW_READS,KUI_SMASH_UI_FAT_STEPS,KUI_SMASH_UI_DIRECTORY_SECTORS,
    KUI_SMASH_UI_CHILD_CAPSULE_WRITTEN,KUI_SMASH_UI_OUTER_CAPSULE_WRITTEN,
    KUI_SMASH_UI_OUTER_MODULE_CRC32,KUI_SMASH_UI_CHILD_MODULE_CRC32,
    KUI_SMASH_UI_OUTER_PAYLOAD_BYTES,KUI_SMASH_UI_CHILD_PAYLOAD_BYTES,
    KUI_SMASH_UI_OUTER_CAPSULE_BYTES,KUI_SMASH_UI_CHILD_CAPSULE_BYTES,
    KUI_SMASH_UI_OUTER_CAPSULE_CRC32,KUI_SMASH_UI_CHILD_CAPSULE_CRC32,
    KUI_SMASH_UI_NATIVE_PROFILE_FLAGS,
    KUI_SMASH_UI_MODULE_CRC32,KUI_SMASH_UI_MODULE_BYTES,KUI_SMASH_UI_EXEC_SOURCE_BYTES,
    KUI_SMASH_UI_EXEC_SHA_BYTES,KUI_SMASH_UI_PROGRAM_BYTES,
    KUI_SMASH_UI_SOURCE_READ_CALLBACKS,KUI_SMASH_UI_SOURCE_READ_BYTES,KUI_SMASH_UI_SOURCE_SEEKS,
    KUI_SMASH_UI_HISTORY_READ_CALLS,KUI_SMASH_UI_HISTORY_READ_BYTES,KUI_SMASH_UI_VERIFY_BATCH_BYTES,
    KUI_SMASH_UI_METRICS
};
#if KUI_SMASH_LAUNCH_TIMING
struct kui_smash_launch_span {
    uint64_t ms;
    struct kui_smash_phase_stamp clock;
    unsigned id;
    bool enabled;
};
/* Main publishes the request while holding its existing worker lock. All
 * later recording and file I/O belongs to that one storage worker. */
void kui_smash_launch_timing_request(bool enabled,uint64_t action_ms);
void kui_smash_launch_timing_activate(const char *image,const char *build);
/* All native modes share the same measured request. Selection records only
 * descriptive launch context; it never changes the selected reader or VMU. */
void kui_smash_launch_timing_native(const char *image,const char *build,const char *product,
    const char *mode,bool virtual_vmu,uint32_t module_crc32,uint32_t module_bytes);
void kui_smash_launch_timing_report_allowed(bool allowed);
void kui_smash_launch_timing_phase(enum kui_smash_ui_phase);
void kui_smash_launch_timing_metric(enum kui_smash_ui_metric,uint64_t);
/* Record identity from the already verified, loaded module; no extra SD
 * read or SHA pass is introduced just to collect this diagnostic. */
void kui_smash_launch_timing_component(enum kui_smash_ui_component,const char *build,
    uint32_t payload_bytes,uint32_t payload_crc32,uint32_t helper_crc32);
void kui_smash_launch_timing_add(enum kui_smash_ui_metric,uint64_t);
struct kui_smash_launch_span kui_smash_launch_timing_span_begin(enum kui_smash_ui_detail);
void kui_smash_launch_timing_span_end(const struct kui_smash_launch_span *);
/* Freeze the measured action-to-ready interval before diagnostic report I/O.
 * Non-native routes produce neither a report nor a log summary. The mounted
 * writer performs no mount/unmount/connect calls and never affects launch. */
void kui_smash_launch_timing_freeze(bool prepared,kui_log_fn);
bool kui_smash_launch_timing_save_mounted(kui_log_fn,kui_cancel_fn);
void kui_smash_launch_timing_finish(bool prepared,kui_log_fn,kui_cancel_fn);
/* A deterministic host clock can exercise accounting without KOS. Console
 * callers leave this unset and use KOS monotonic milliseconds. */
void kui_smash_launch_timing_clock(uint64_t (*read)(void *),void *context);
size_t kui_smash_launch_timing_format(char *,size_t);
#define KUI_SMASH_TIME_SPAN(name,id) struct kui_smash_launch_span name=kui_smash_launch_timing_span_begin(id)
#define KUI_SMASH_TIME_END(name) kui_smash_launch_timing_span_end(&(name))
#else
#define kui_smash_launch_timing_request(enabled,ms) ((void)0)
#define kui_smash_launch_timing_activate(image,build) ((void)0)
#define kui_smash_launch_timing_native(image,build,product,mode,vmu,crc,bytes) ((void)0)
#define kui_smash_launch_timing_report_allowed(allowed) ((void)0)
#define kui_smash_launch_timing_phase(phase) ((void)0)
#define kui_smash_launch_timing_metric(metric,value) ((void)0)
#define kui_smash_launch_timing_component(id,build,bytes,crc,helper_crc) ((void)0)
#define kui_smash_launch_timing_add(metric,value) ((void)0)
#define kui_smash_launch_timing_freeze(prepared,log) ((void)0)
#define kui_smash_launch_timing_finish(prepared,log,cancel) ((void)0)
#define KUI_SMASH_TIME_SPAN(name,id) ((void)0)
#define KUI_SMASH_TIME_END(name) ((void)0)
#endif
#endif
