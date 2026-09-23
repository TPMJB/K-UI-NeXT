/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_RECOVERY_SCAN_H
#define KUI_RECOVERY_SCAN_H
#include "kui/capture.h"

/* Advanced CRC reads existing completed K-UI jobs. It never modifies track
 * data, checkpoints, GDI or manifest; the only output is a unique report under
 * /KUI/recovery. This is diagnosis, not optical recovery or ECC reconstruction. */
enum kui_scan_result { KUI_SCAN_FAILED, KUI_SCAN_STOPPED, KUI_SCAN_CLEAN, KUI_SCAN_ISSUES };
struct kui_scan_status {
    enum kui_scan_result result;
    bool complete;
    unsigned track, tracks;
    uint64_t done, total, elapsed_ms;
    uint32_t data_sectors, audio_sectors, bad_sectors, unsupported_sectors;
    uint32_t crc_mismatches, sha_mismatches;
    char message[128];
    char report[KUI_DEST_PATH_CAP];
};
struct kui_scan_ops {
    void *ctx;
    bool (*cancelled)(void *);
    uint64_t (*now_ms)(void *);
    void (*progress)(void *,const struct kui_scan_status *);
    kui_log_fn log;
};
/* Caller connects/disconnects media. Sole storage worker; mount/unmount here.
 * directory is a card-root path (/Games/Title) or qualified 0:/Games/Title.
 * B cancellation and any storage/report error preserve all original files.
 * Reports remain .part until final sync/close and rename succeeds. Only .txt
 * plus its COMPLETE footer is a published result; .part is always incomplete. */
enum kui_scan_result kui_recovery_scan(const char *directory,
    const struct kui_scan_ops *ops,struct kui_scan_status *out);

/* Strict, bounded parser for the completed schema 1/2 manifests emitted by
 * K-UI. Exposed for host checks, no allocation or storage/drive operations. */
#define KUI_SCAN_MANIFEST_LIMIT 32768u
struct kui_scan_manifest {
    struct kui_capture_plan plan;
    uint8_t identity[32];
    bool crc_only;
    char gdi[KUI_DEST_TITLE_CAP+5u];
    struct { uint32_t crc32; uint8_t sha256[32]; } track[99];
};
bool kui_recovery_manifest_parse(const void *data,size_t size,
    struct kui_scan_manifest *out);
#endif
