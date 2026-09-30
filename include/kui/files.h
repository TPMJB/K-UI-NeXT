/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_FILES_H
#define KUI_FILES_H
#include "kui/apps.h"
#include "kui/destination.h"
#include <stdbool.h>
#include <stdint.h>

/* File Manager: every folder and file on the SD card, folders first and then
 * files, each in name order, eight rows per page. Paths are card-root paths
 * ("/", "/Games", "/Games/Crazy Taxi/disc.gdi") without a drive prefix. */
#define KUI_FILES_ROWS 8u
#define KUI_FILES_PATH_CAP KUI_DEST_PATH_CAP
#define KUI_FILES_NAME_CAP KUI_DEST_NAME_CAP
/* Any FatFs long name: 255 UTF-16 units written as UTF-8. Pages are found
 * from the name of a neighbouring row, so these keep whole names even when
 * the row itself shows [Name too long]. */
#define KUI_FILES_KEY_CAP 768u
/* Folder levels a copy, delete or details count may descend below the
 * folder it starts from. */
#define KUI_FILES_DEPTH 16u
/* Copies are written as KUI-copy-<n>.kui-part (n = 1..99) beside their
 * destination and take their real name only after being read back. */
#define KUI_FILES_PART_SUFFIX ".kui-part"
/* Picture view: PNG, JPEG or 16-bit .PVR fitted into a square. */
#define KUI_FILES_PICTURE_EDGE 272u
#define KUI_FILES_PICTURE_MAX_BYTES (3u * 1024u * 1024u)

enum kui_files_kind { KUI_FILES_KIND_FOLDER, KUI_FILES_KIND_GDI, KUI_FILES_KIND_AUDIO,
    KUI_FILES_KIND_PICTURE, KUI_FILES_KIND_OTHER };
/* FIRST: the folder's first page. NEXT/PREVIOUS: the rows after/before the
 * anchor. AT: rows from the anchor on, the anchor included if it exists. */
enum kui_files_seek { KUI_FILES_SEEK_FIRST, KUI_FILES_SEEK_NEXT, KUI_FILES_SEEK_PREVIOUS,
    KUI_FILES_SEEK_AT };
enum kui_files_op { KUI_FILES_OP_NONE, KUI_FILES_OP_DETAILS, KUI_FILES_OP_COPY,
    KUI_FILES_OP_MOVE, KUI_FILES_OP_DELETE, KUI_FILES_OP_RENAME, KUI_FILES_OP_MKDIR };

struct kui_files_entry {
    char name[KUI_FILES_NAME_CAP];
    uint64_t bytes;
    uint16_t date, time; /* Modified, in FAT format; zero when unknown. */
    uint8_t attributes;  /* FatFs AM_* bits. */
    /* disabled: the name or its path cannot be used by this menu (too long,
     * or not a name K-UI can write back); the row is shown but not opened. */
    bool directory, disabled;
};
struct kui_files_request {
    char path[KUI_FILES_PATH_CAP];
    char anchor[KUI_FILES_KEY_CAP];
    bool anchor_directory, folders_only;
    enum kui_files_seek seek;
};
struct kui_files_page {
    char path[KUI_FILES_PATH_CAP], message[128];
    struct kui_files_entry entries[KUI_FILES_ROWS];
    /* before: listable rows ahead of this page; total: in the whole folder. */
    unsigned count, before, total;
    char first[KUI_FILES_KEY_CAP], last[KUI_FILES_KEY_CAP];
    bool first_directory, last_directory, folders_only, ok;
};
/* source: the entry acted on (MKDIR: the folder to create in). target: the
 * destination folder of a copy or move. name: the new name of a rename or
 * folder; for a copy or move, the name the preview chose in the target. */
struct kui_files_job {
    enum kui_files_op op;
    char source[KUI_FILES_PATH_CAP], target[KUI_FILES_PATH_CAP];
    char name[KUI_FILES_NAME_CAP];
};
struct kui_files_preview {
    struct kui_files_job job;
    struct kui_app_status status;
    /* Everything below source, the source itself included. */
    uint64_t bytes, free_bytes;
    unsigned files, folders, read_only;
    uint16_t date, time;
    uint8_t attributes;
    /* ready: the operation may be confirmed. renamed: a copy or move takes a
     * numbered name because its own is taken in the target folder. */
    bool directory, ready, renamed, free_known;
};
struct kui_files_picture {
    char path[KUI_FILES_PATH_CAP], message[128];
    const char *format;
    unsigned width, height;
    uint64_t bytes;
    bool ok;
};

/* Pure path rules. A path is "/" or "/" followed by names joined by single
 * slashes, each a FAT-safe name (kui_destination_name_valid), shorter than
 * KUI_FILES_PATH_CAP in all. */
bool kui_files_path_valid(const char *path);
bool kui_files_join(char out[KUI_FILES_PATH_CAP], const char *folder, const char *name);
/* "/" is its own parent. */
bool kui_files_parent(char out[KUI_FILES_PATH_CAP], const char *path);
const char *kui_files_leaf(const char *path);
/* path is folder or lies below it, comparing ASCII letters as FAT does. */
bool kui_files_within(const char *path, const char *folder);
/* The files K-UI needs to start (the runtime and the Games loader) and the
 * folders holding them: never moved, renamed or deleted here. */
bool kui_files_protected(const char *path);
/* "Name (2).ext" for files and "Name (2)" for folders; false if the result
 * is not a valid name. */
bool kui_files_numbered(char out[KUI_FILES_NAME_CAP], const char *name, bool directory, unsigned number);
bool kui_files_part_name(const char *name);
enum kui_files_kind kui_files_kind(const char *name, bool directory);
/* Folders first, then ASCII letters without case, then the exact bytes. */
int kui_files_compare(bool a_directory, const char *a, bool b_directory, const char *b);
/* "1.5 KB", "650 MB", "1.1 GB" (1024-based); "No date" or "2026-09-26 14:03". */
void kui_files_size_text(char out[16], uint64_t bytes);
void kui_files_date_text(char out[20], uint16_t date, uint16_t time);

/* Worker operations: each connects and mounts the card, and leaves no file
 * or folder open. Listing reads folder entries only. */
bool kui_files_list(const struct kui_files_request *request, struct kui_files_page *out,
                    kui_log_fn log, kui_cancel_fn cancel);
/* Checks an operation before it is confirmed. DETAILS, COPY and DELETE count
 * everything below a folder; COPY checks free space and picks the name;
 * MOVE picks the name. Nothing is written. */
bool kui_files_preview(const struct kui_files_job *job, struct kui_files_preview *out,
                       kui_log_fn log, kui_cancel_fn cancel, kui_app_progress_fn progress);
/* Runs a confirmed operation after checking it again. Copies never replace
 * anything: each is written under a .kui-part name, read back and compared
 * with the source (size and CRC32), then renamed. Stop or a failure removes
 * the partial copy. Moves and renames only rename. Delete removes files one
 * by one; Stop keeps whatever is not yet deleted. totals: the preview's
 * counts for progress, or NULL. */
bool kui_files_commit(const struct kui_files_job *job, const struct kui_files_preview *totals,
                      struct kui_app_status *out, kui_log_fn log, kui_cancel_fn cancel,
                      kui_app_progress_fn progress);
/* Decodes a PNG, JPEG or .PVR picture into KUI_FILES_PICTURE_EDGE squared
 * RGB565 pixels, fitted and centred on the menu's navy. */
bool kui_files_picture(const char *path, uint16_t *pixels, struct kui_files_picture *out,
                       kui_log_fn log, kui_cancel_fn cancel);
#endif
