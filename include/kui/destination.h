/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_DESTINATION_H
#define KUI_DESTINATION_H
#include "kui/probe.h"

#define KUI_DEST_ROOT_CAP 128u
#define KUI_DEST_TITLE_CAP 96u
#define KUI_DEST_NAME_CAP 128u
#define KUI_DEST_JOB_CAP 256u
#define KUI_DEST_PATH_CAP 384u
#define KUI_DEST_PAGE_SIZE 8u
#define KUI_DEST_INDEX_MAX 9999u
#define KUI_DEST_RECORD_SIZE (KUI_DEST_ROOT_CAP+28u)
#define KUI_DEST_PATH_A "0:/KUI/destination-a.bin"
#define KUI_DEST_PATH_B "0:/KUI/destination-b.bin"

/* Card-root paths use '/Games', never a drive prefix. Components must be FAT
 * safe and valid UTF-8. Repeated/trailing slashes are removed; '.' is skipped;
 * '..', controls, trailing dots/spaces and reserved DOS names are rejected. */
void kui_destination_default(char out[KUI_DEST_ROOT_CAP]);
bool kui_destination_normalize(char out[KUI_DEST_ROOT_CAP],const char *path);
bool kui_destination_parent(char out[KUI_DEST_ROOT_CAP],const char *path);
bool kui_destination_join(char out[KUI_DEST_ROOT_CAP],const char *parent,const char *name);
/* Validate one FAT-safe component independently of a directory-path limit.
 * Allows up to NAME_CAP-1 UTF-8 bytes; no slash, dot aliases or reserved name. */
bool kui_destination_name_valid(const char *name);
/* Titles replace unsafe bytes with '_', collapse whitespace, trim leading/trailing
 * dots/spaces, preserve whole UTF-8 characters and use DreamcastDisc if empty
 * or reserved. Folder helpers require an already sanitized title. Numbered
 * folders use 'Title (2)' through 'Title (9999)'. Parsing folds ASCII case to
 * match FAT naming; non-ASCII bytes compare exactly. No leading-zero suffixes. */
void kui_destination_title(char out[KUI_DEST_TITLE_CAP],const char *ip_title);
bool kui_destination_folder_name(char out[KUI_DEST_NAME_CAP],const char *title,unsigned index);
bool kui_destination_folder_index(const char *name,const char *title,unsigned *index);
bool kui_destination_job_path(char out[KUI_DEST_JOB_CAP],const char *root,const char *folder);

struct kui_destination_entry {char name[KUI_DEST_NAME_CAP];bool disabled;};
struct kui_destination_page {
    struct kui_destination_entry entries[KUI_DEST_PAGE_SIZE];
    unsigned count;
    bool has_more;
};
/* Mounted-volume APIs: only the worker may call these. No formatting or file
 * overwrite. mkdirs creates missing parents; existing directories are kept.
 * Listing counts directories only in lexicographic order. An unrepresentable
 * name is a disabled '[Name too long]' entry rather than a truncated path. */
bool kui_destination_mkdirs(const char *root,kui_log_fn log);
bool kui_destination_list(const char *root,unsigned offset,
                           struct kui_destination_page *page,kui_log_fn log);
/* Separate alternating CRC/sequence records; the settings record is untouched.
 * Missing/corrupt records load /Games successfully. I/O errors return false.
 * Save preserves the newest valid slot and verifies the replacement after
 * sync/close. Filesystem/card power-loss guarantees remain those of FatFs and
 * the device. Only normalized paths are stored; no directory is created here. */
bool kui_destination_load(char out[KUI_DEST_ROOT_CAP],kui_log_fn log);
bool kui_destination_save(const char *root,kui_log_fn log);
bool kui_destination_encode(uint8_t out[KUI_DEST_RECORD_SIZE],const char *root,uint64_t sequence);
bool kui_destination_decode(char out[KUI_DEST_ROOT_CAP],uint64_t *sequence,
                             const void *record,size_t size);
#endif
