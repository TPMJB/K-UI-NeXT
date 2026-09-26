/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/files.h"
#include "platform.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* File Manager card operations, written for K-UI. Only the storage worker
 * calls these; each one connects, mounts, works and releases the card. */
#define CARD_CAP (KUI_FILES_PATH_CAP + 2u)
/* Below a copied, deleted or counted folder, paths may grow past what the
 * browser shows; FatFs itself accepts any length. */
#define WALK_CAP 1600u
#define CHUNK (32u * 1024u)
#define SCAN_LIMIT 32768u
static const char STOPPED[] = "Stopped";

static bool stopped(kui_cancel_fn cancel) { return cancel && cancel(); }
static bool card(char out[CARD_CAP], const char *path) {
    int n = snprintf(out, CARD_CAP, "0:%s", path);
    return n > 0 && n < (int)CARD_CAP;
}
static void say(struct kui_app_status *status, const char *format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(status->message, sizeof(status->message), format, args);
    va_end(args);
}
static void note(struct kui_app_status *status, const char *format, ...) {
    if(status->line_count >= KUI_APP_LINES) return;
    va_list args;
    va_start(args, format);
    vsnprintf(status->lines[status->line_count++], KUI_APP_LINE_CAP, format, args);
    va_end(args);
}
static void report(kui_app_progress_fn progress, const struct kui_app_status *status) {
    if(progress) progress(status);
}
static void finish_status(struct kui_app_status *status, bool ok, bool was_stopped) {
    status->complete = true;
    status->passed = ok;
    status->stopped = was_stopped;
    status->errors = ok || was_stopped ? 0u : 1u;
}

struct session { FATFS fs; bool connected; };
static bool begin(struct session *s, kui_log_fn log, const char **problem) {
    if(!kui_sd_connect()) { *problem = "SD card unavailable"; return false; }
    s->connected = true;
    if(!kui_mount(&s->fs, log)) { *problem = "Cannot mount SD card"; return false; }
    return true;
}
static bool end(struct session *s) {
    if(!s->connected) return true;
    bool ok = f_mount(NULL, "0:", 0) == FR_OK;
    kui_sd_disconnect();
    s->connected = false;
    return ok;
}
/* FatFs cannot stat the root folder; it always exists. */
static FRESULT stat_path(const char *path, FILINFO *info) {
    if(!path[1]) { memset(info, 0, sizeof(*info)); info->fattrib = AM_DIR; return FR_OK; }
    char c[CARD_CAP];
    if(!card(c, path)) return FR_INVALID_NAME;
    return f_stat(c, info);
}
static bool dot_entry(const char *name) { return !strcmp(name, ".") || !strcmp(name, ".."); }
static bool missing(FRESULT r) { return r == FR_NO_FILE || r == FR_NO_PATH; }

/* ---- Listing: one pass over the folder per page ---- */
struct slot { char name[KUI_FILES_KEY_CAP]; uint64_t bytes; uint16_t date, time; uint8_t attributes; bool directory; };
struct scan { struct slot slot[KUI_FILES_ROWS]; unsigned order[KUI_FILES_ROWS], count; };
/* Keeps the ROWS smallest rows seen (or the largest), in order. */
static void keep(struct scan *s, const FILINFO *info, bool directory, bool largest) {
    unsigned index;
    if(s->count < KUI_FILES_ROWS) index = s->count;
    else {
        unsigned out = largest ? 0 : s->count - 1u;
        const struct slot *edge = &s->slot[s->order[out]];
        int c = kui_files_compare(directory, info->fname, edge->directory, edge->name);
        if(largest ? c <= 0 : c >= 0) return;
        index = s->order[out];
        memmove(s->order + out, s->order + out + 1, (s->count - 1u - out) * sizeof(s->order[0]));
        --s->count;
    }
    struct slot *slot = &s->slot[index];
    memcpy(slot->name, info->fname, strlen(info->fname) + 1u);
    slot->directory = directory;
    slot->bytes = directory ? 0 : (uint64_t)info->fsize;
    slot->date = info->fdate; slot->time = info->ftime; slot->attributes = info->fattrib;
    unsigned at = 0;
    while(at < s->count && kui_files_compare(s->slot[s->order[at]].directory, s->slot[s->order[at]].name,
                                             directory, slot->name) < 0) ++at;
    memmove(s->order + at + 1, s->order + at, (s->count - at) * sizeof(s->order[0]));
    s->order[at] = index;
    ++s->count;
}
static bool request_valid(const struct kui_files_request *r) {
    return r && memchr(r->path, 0, sizeof(r->path)) && kui_files_path_valid(r->path) &&
        memchr(r->anchor, 0, sizeof(r->anchor)) && r->seek <= KUI_FILES_SEEK_AT &&
        (r->seek == KUI_FILES_SEEK_FIRST || r->anchor[0]);
}
bool kui_files_list(const struct kui_files_request *request, struct kui_files_page *out,
                    kui_log_fn log, kui_cancel_fn cancel) {
    if(!out) return false;
    memset(out, 0, sizeof(*out));
    /* Even a refused request names its folder, so the menu shows why. */
    if(request && memchr(request->path, 0, sizeof(request->path))) {
        strcpy(out->path, request->path);
        out->folders_only = request->folders_only;
    }
    if(!request_valid(request)) { snprintf(out->message, sizeof(out->message), "Invalid folder"); return false; }
    if(stopped(cancel)) { snprintf(out->message, sizeof(out->message), "Folder listing stopped"); return false; }
    struct session session;
    session.connected = false;
    struct scan *scan = malloc(sizeof(*scan));
    const char *problem = "Insufficient memory to list the folder";
    bool opened = false, ok = false;
    DIR dir;
    if(!scan) goto done;
    if(!begin(&session, log, &problem)) goto done;
    char path[CARD_CAP];
    card(path, request->path);
    FRESULT r = f_opendir(&dir, path);
    if(r != FR_OK) {
        problem = missing(r) ? "This folder is no longer on the card" : "Cannot open this folder";
        goto done;
    }
    opened = true;
    enum kui_files_seek seek = request->seek;
    unsigned total, passed;
    for(;;) {
        memset(scan, 0, sizeof(*scan));
        total = passed = 0;
        bool backward = seek == KUI_FILES_SEEK_PREVIOUS;
        for(unsigned scanned = 0;; ++scanned) {
            if(stopped(cancel)) { problem = "Folder listing stopped"; goto done; }
            if(scanned == SCAN_LIMIT) { problem = "Folder too large to list"; goto done; }
            FILINFO info;
            r = f_readdir(&dir, &info);
            if(r != FR_OK) { problem = "Cannot read this folder"; goto done; }
            if(!info.fname[0]) break;
            if(dot_entry(info.fname)) continue;
            bool directory = (info.fattrib & AM_DIR) != 0;
            if(request->folders_only && !directory) continue;
            ++total;
            if(seek != KUI_FILES_SEEK_FIRST) {
                int c = kui_files_compare(directory, info.fname, request->anchor_directory, request->anchor);
                bool candidate = seek == KUI_FILES_SEEK_NEXT ? c > 0 : seek == KUI_FILES_SEEK_AT ? c >= 0 : c < 0;
                /* passed counts rows before the anchor (PREVIOUS keeps
                 * the last of them) or rows the page starts after. */
                if(backward) { if(!candidate) continue; ++passed; }
                else if(!candidate) { ++passed; continue; }
            }
            keep(scan, &info, directory, backward);
        }
        /* Too few rows before the anchor to fill a page: show the first. */
        if(!backward || scan->count == KUI_FILES_ROWS) break;
        seek = KUI_FILES_SEEK_FIRST;
        r = f_readdir(&dir, NULL);
        if(r != FR_OK) { problem = "Cannot read this folder"; goto done; }
    }
    out->total = total;
    out->before = seek == KUI_FILES_SEEK_FIRST ? 0 : seek == KUI_FILES_SEEK_PREVIOUS ? passed - scan->count : passed;
    out->count = scan->count;
    for(unsigned i = 0; i < scan->count; ++i) {
        const struct slot *slot = &scan->slot[scan->order[i]];
        struct kui_files_entry *e = &out->entries[i];
        e->directory = slot->directory; e->bytes = slot->bytes;
        e->date = slot->date; e->time = slot->time; e->attributes = slot->attributes;
        char joined[KUI_FILES_PATH_CAP];
        if(strlen(slot->name) >= sizeof(e->name)) {
            snprintf(e->name, sizeof(e->name), "[Name too long]");
            e->disabled = true;
        } else {
            strcpy(e->name, slot->name);
            e->disabled = !kui_files_join(joined, out->path, e->name);
        }
    }
    if(scan->count) {
        const struct slot *first = &scan->slot[scan->order[0]], *last = &scan->slot[scan->order[scan->count - 1u]];
        strcpy(out->first, first->name); out->first_directory = first->directory;
        strcpy(out->last, last->name); out->last_directory = last->directory;
    }
    ok = true;
done:
    if(opened && f_closedir(&dir) != FR_OK && ok) { ok = false; problem = "Cannot close this folder"; }
    if(!end(&session) && ok) { ok = false; problem = "Cannot release SD filesystem"; }
    free(scan);
    if(ok) snprintf(out->message, sizeof(out->message), "%s", out->total ? "" :
        out->folders_only ? "No folders here." : "This folder is empty.");
    else {
        out->count = out->before = out->total = 0;
        out->first[0] = out->last[0] = 0;
        snprintf(out->message, sizeof(out->message), "%s", problem);
        if(log) log("Files list %s: %s", request->path, problem);
    }
    out->ok = ok;
    return ok;
}

/* ---- Walking a folder tree, one open folder per level ---- */
enum step { STEP_FILE, STEP_ENTER, STEP_LEAVE, STEP_END, STEP_FAILED, STEP_DEEP, STEP_LONG };
struct walker {
    DIR dir[KUI_FILES_DEPTH + 1];
    size_t length[KUI_FILES_DEPTH + 1];
    uint16_t date[KUI_FILES_DEPTH + 1], time[KUI_FILES_DEPTH + 1];
    unsigned open, left; /* open levels; the level a LEAVE step closed */
    bool pop;
    FRESULT result;
    char path[WALK_CAP], item[WALK_CAP];
    FILINFO info;
};
static bool walk_start(struct walker *w, const char *root, uint16_t date, uint16_t time) {
    w->open = 0; w->pop = false;
    size_t n = strlen(root);
    if(n >= WALK_CAP) { w->result = FR_INVALID_NAME; return false; }
    memcpy(w->path, root, n + 1u);
    w->result = f_opendir(&w->dir[0], w->path);
    if(w->result != FR_OK) return false;
    w->length[0] = n; w->date[0] = date; w->time[0] = time; w->open = 1;
    return true;
}
/* FILE: item is a file in path. ENTER: path is a folder just opened. LEAVE:
 * path is a folder whose entries have all been visited; it is closed, so it
 * may be removed. END follows the root's LEAVE. */
static enum step walk_next(struct walker *w) {
    if(w->pop) {
        w->pop = false;
        if(!w->open) return STEP_END;
        w->path[w->length[w->open - 1u]] = 0;
    }
    for(;;) {
        w->result = f_readdir(&w->dir[w->open - 1u], &w->info);
        if(w->result != FR_OK) return STEP_FAILED;
        if(!w->info.fname[0]) {
            w->left = --w->open;
            w->result = f_closedir(&w->dir[w->open]);
            if(w->result != FR_OK) return STEP_FAILED;
            w->pop = true;
            return STEP_LEAVE;
        }
        if(dot_entry(w->info.fname)) continue;
        size_t base = strlen(w->path), n = strlen(w->info.fname);
        if(base + 1u + n >= WALK_CAP) return STEP_LONG;
        if(w->info.fattrib & AM_DIR) {
            if(w->open > KUI_FILES_DEPTH) return STEP_DEEP;
            w->path[base] = '/';
            memcpy(w->path + base + 1u, w->info.fname, n + 1u);
            w->result = f_opendir(&w->dir[w->open], w->path);
            if(w->result != FR_OK) { w->path[base] = 0; return STEP_FAILED; }
            w->length[w->open] = base + 1u + n;
            w->date[w->open] = w->info.fdate; w->time[w->open] = w->info.ftime;
            ++w->open;
            return STEP_ENTER;
        }
        memcpy(w->item, w->path, base);
        w->item[base] = '/';
        memcpy(w->item + base + 1u, w->info.fname, n + 1u);
        return STEP_FILE;
    }
}
static void walk_abort(struct walker *w) {
    while(w->open) f_closedir(&w->dir[--w->open]);
    w->pop = false;
}
static bool walk_broken(enum step step) { return step == STEP_FAILED || step == STEP_DEEP || step == STEP_LONG; }
static const char *walk_problem(enum step step, const char *failed) {
    return step == STEP_DEEP ? "Folders inside are nested more than 16 deep" :
        step == STEP_LONG ? "A path inside is too long" : failed;
}

/* ---- Counting ---- */
struct tally { uint64_t bytes, allocated; unsigned files, folders, read_only; size_t longest; };
static uint64_t rounded(uint64_t bytes, uint64_t cluster) {
    return cluster ? (bytes + cluster - 1u) / cluster * cluster : bytes;
}
static const char *count_tree(struct walker *w, const char *root, uint64_t cluster, struct tally *t,
                              struct kui_app_status *status, kui_app_progress_fn progress, kui_cancel_fn cancel) {
    if(!walk_start(w, root, 0, 0)) return "Cannot open this folder";
    size_t base = strlen(root);
    for(unsigned seen = 0;; ++seen) {
        if(stopped(cancel)) { walk_abort(w); return STOPPED; }
        enum step step = walk_next(w);
        if(step == STEP_END) return NULL;
        if(walk_broken(step)) { walk_abort(w); return walk_problem(step, "Cannot read a folder inside"); }
        if(step == STEP_FILE) {
            ++t->files;
            t->bytes += (uint64_t)w->info.fsize;
            t->allocated += rounded((uint64_t)w->info.fsize, cluster);
            if(w->info.fattrib & AM_RDO) ++t->read_only;
            size_t n = strlen(w->item) - base;
            if(n > t->longest) t->longest = n;
        } else if(step == STEP_ENTER) {
            ++t->folders;
            t->allocated += cluster;
            if(w->info.fattrib & AM_RDO) ++t->read_only;
            size_t n = strlen(w->path) - base;
            if(n > t->longest) t->longest = n;
        }
        if(!(seen % 64u)) {
            char size[16];
            kui_files_size_text(size, t->bytes);
            say(status, "Counting: %u files in %u folders, %s", t->files, t->folders, size);
            report(progress, status);
        }
    }
}

/* ---- Names in a destination ---- */
static bool free_name(const char *folder, const char *name, bool directory, char out[KUI_FILES_NAME_CAP],
                      bool *renamed, const char **problem) {
    for(unsigned n = 1; n < 100; ++n) {
        char candidate[KUI_FILES_NAME_CAP], joined[KUI_FILES_PATH_CAP];
        if(n == 1) memcpy(candidate, name, strlen(name) + 1u);
        else if(!kui_files_numbered(candidate, name, directory, n)) { *problem = "No free name for this item there"; return false; }
        if(!kui_files_join(joined, folder, candidate)) { *problem = "The destination path would be too long"; return false; }
        FILINFO info;
        FRESULT r = stat_path(joined, &info);
        if(r == FR_NO_FILE) { memcpy(out, candidate, strlen(candidate) + 1u); *renamed = n > 1; return true; }
        if(r != FR_OK) { *problem = "Cannot check the destination folder"; return false; }
    }
    *problem = "No free name: 99 numbered copies exist there";
    return false;
}
/* A KUI-copy-<n>.kui-part name nobody uses yet in folder, as a card path. */
static bool part_path(const char *folder, char out[CARD_CAP]) {
    for(unsigned n = 1; n < 100; ++n) {
        char name[32], joined[KUI_FILES_PATH_CAP];
        snprintf(name, sizeof(name), "KUI-copy-%u" KUI_FILES_PART_SUFFIX, n);
        if(!kui_files_join(joined, folder, name)) return false;
        FILINFO info;
        FRESULT r = stat_path(joined, &info);
        if(r == FR_NO_FILE) return card(out, joined);
        if(r != FR_OK) return false;
    }
    return false;
}

/* ---- Deleting ---- */
static FRESULT remove_one(const char *path) {
    FRESULT r = f_unlink(path);
    if(r == FR_DENIED) {
        /* Read-only items are cleared first; a folder that is not empty
         * stays denied. */
        FILINFO info;
        if(f_stat(path, &info) == FR_OK && (info.fattrib & AM_RDO) && f_chmod(path, 0, AM_RDO) == FR_OK)
            r = f_unlink(path);
    }
    return r;
}
struct deletion { unsigned files, folders; };
static const char *delete_tree(struct walker *w, const char *root, struct deletion *d, struct kui_app_status *status,
                               uint64_t total, kui_app_progress_fn progress, kui_cancel_fn cancel) {
    if(!walk_start(w, root, 0, 0)) return "Cannot open this folder";
    for(;;) {
        if(stopped(cancel)) { walk_abort(w); return STOPPED; }
        enum step step = walk_next(w);
        if(step == STEP_END) return NULL;
        if(walk_broken(step)) { walk_abort(w); return walk_problem(step, "Cannot read a folder inside"); }
        if(step == STEP_FILE || step == STEP_LEAVE) {
            FRESULT r = remove_one(step == STEP_FILE ? w->item : w->path);
            if(r != FR_OK) {
                walk_abort(w);
                return step == STEP_FILE ? "Cannot delete a file inside" : "Cannot delete a folder inside";
            }
            if(step == STEP_FILE) ++d->files; else ++d->folders;
            if(status) {
                status->done = d->files + d->folders;
                status->total = total;
                say(status, "Deleting: %u of %llu items", d->files + d->folders, (unsigned long long)total);
                report(progress, status);
            }
        }
    }
}

/* ---- Copying ---- */
struct copier {
    struct walker walk;
    char target[WALK_CAP];
    uint8_t *buffer;
    struct kui_app_status *status;
    kui_app_progress_fn progress;
    kui_cancel_fn cancel;
    uint64_t done, total;
    unsigned file, files;
};
static void copy_progress(struct copier *c, const char *verb, const char *name) {
    c->status->done = c->done;
    c->status->total = c->total;
    say(c->status, "%s %u of %u: %s", verb, c->file, c->files, name);
    report(c->progress, c->status);
}
/* Streams one file, then reads the written file back and compares its size
 * and CRC32 with what was read from the source. */
static const char *copy_file(struct copier *c, const char *from, const char *to, const FILINFO *source) {
    const char *name = kui_files_leaf(from);
    FIL in, out;
    FRESULT r = f_open(&in, from, FA_READ);
    if(r != FR_OK) return "Cannot read a file to copy";
    r = f_open(&out, to, FA_WRITE | FA_CREATE_NEW);
    if(r != FR_OK) { f_close(&in); return r == FR_EXIST ? "The copy's name is already taken" : "Cannot create the copy"; }
    const char *problem = NULL;
    uint32_t crc = 0;
    uint64_t copied = 0;
    ++c->file;
    copy_progress(c, "Copying", name);
    for(;;) {
        if(stopped(c->cancel)) { problem = STOPPED; break; }
        UINT got = 0, put = 0;
        r = f_read(&in, c->buffer, CHUNK, &got);
        if(r != FR_OK) { problem = "Cannot read a file to copy"; break; }
        if(!got) break;
        crc = kui_crc32(crc, c->buffer, got);
        r = f_write(&out, c->buffer, got, &put);
        if(r != FR_OK || put != got) { problem = r == FR_OK ? "The card is full" : "Cannot write the copy"; break; }
        copied += got;
        c->done += got;
        copy_progress(c, "Copying", name);
    }
    FRESULT closed_in = f_close(&in), closed_out = f_close(&out);
    if(!problem && (closed_in != FR_OK || closed_out != FR_OK)) problem = "Cannot finish the copy";
    if(!problem && copied != (uint64_t)source->fsize) problem = "The source changed while copying";
    if(problem) return problem;
    r = f_open(&in, to, FA_READ);
    if(r != FR_OK) return "Cannot read the copy back";
    uint32_t check = 0;
    uint64_t checked = 0;
    for(;;) {
        if(stopped(c->cancel)) { problem = STOPPED; break; }
        UINT got = 0;
        r = f_read(&in, c->buffer, CHUNK, &got);
        if(r != FR_OK) { problem = "Cannot read the copy back"; break; }
        if(!got) break;
        check = kui_crc32(check, c->buffer, got);
        checked += got;
        c->done += got;
        copy_progress(c, "Checking", name);
    }
    if(f_close(&in) != FR_OK && !problem) problem = "Cannot read the copy back";
    if(!problem && (checked != copied || check != crc)) problem = "The copy does not match the source";
    if(!problem && source->fdate) f_utime(to, source);
    return problem;
}
static bool copy_path(const char *source_root, const char *copy_root, const char *path, char out[WALK_CAP]) {
    int n = snprintf(out, WALK_CAP, "%s%s", copy_root, path + strlen(source_root));
    return n > 0 && n < (int)WALK_CAP;
}
static const char *copy_tree(struct copier *c, const char *from, const char *to, const FILINFO *root) {
    FRESULT r = f_mkdir(to);
    if(r != FR_OK) return r == FR_EXIST ? "The copy's name is already taken" : "Cannot create the copy folder";
    struct walker *w = &c->walk;
    if(!walk_start(w, from, root->fdate, root->ftime)) return "Cannot open the folder to copy";
    for(;;) {
        if(stopped(c->cancel)) { walk_abort(w); return STOPPED; }
        enum step step = walk_next(w);
        if(step == STEP_END) return NULL;
        if(walk_broken(step)) { walk_abort(w); return walk_problem(step, "Cannot read a folder to copy"); }
        const char *problem = NULL;
        if(step == STEP_ENTER) {
            if(!copy_path(from, to, w->path, c->target)) problem = "A path in the copy is too long";
            else if(f_mkdir(c->target) != FR_OK) problem = "Cannot create a folder in the copy";
        } else if(step == STEP_FILE) {
            if(!copy_path(from, to, w->item, c->target)) problem = "A path in the copy is too long";
            else problem = copy_file(c, w->item, c->target, &w->info);
        } else if(step == STEP_LEAVE && w->date[w->left]) {
            /* Folders keep their dates once their contents are in place. */
            FILINFO stamp;
            memset(&stamp, 0, sizeof(stamp));
            stamp.fdate = w->date[w->left];
            stamp.ftime = w->time[w->left];
            if(copy_path(from, to, w->path, c->target)) f_utime(c->target, &stamp);
        }
        if(problem) { walk_abort(w); return problem; }
    }
}

/* ---- Checks shared by preview and commit ---- */
static bool job_valid(const struct kui_files_job *job) {
    if(!job || !memchr(job->source, 0, sizeof(job->source)) || !memchr(job->target, 0, sizeof(job->target)) ||
       !memchr(job->name, 0, sizeof(job->name)) || !kui_files_path_valid(job->source)) return false;
    switch(job->op) {
    case KUI_FILES_OP_DETAILS: case KUI_FILES_OP_DELETE: return job->source[1] != 0;
    case KUI_FILES_OP_COPY: case KUI_FILES_OP_MOVE: return job->source[1] && kui_files_path_valid(job->target);
    case KUI_FILES_OP_RENAME: return job->source[1] && kui_destination_name_valid(job->name);
    case KUI_FILES_OP_MKDIR: return kui_destination_name_valid(job->name);
    default: return false;
    }
}
static const char *op_verb(enum kui_files_op op) {
    return op == KUI_FILES_OP_COPY ? "copy" : op == KUI_FILES_OP_MOVE ? "move" : op == KUI_FILES_OP_DELETE ? "delete" :
        op == KUI_FILES_OP_RENAME ? "rename" : op == KUI_FILES_OP_MKDIR ? "new folder" : "details";
}
static const char *refused(const struct kui_files_job *job) {
    if((job->op == KUI_FILES_OP_MOVE || job->op == KUI_FILES_OP_DELETE || job->op == KUI_FILES_OP_RENAME) &&
       kui_files_protected(job->source))
        return job->op == KUI_FILES_OP_MOVE ? "K-UI needs this to start; it cannot be moved." :
            job->op == KUI_FILES_OP_DELETE ? "K-UI needs this to start; it cannot be deleted." :
            "K-UI needs this to start; it cannot be renamed.";
    return NULL;
}
/* The destination of a copy or move: an existing folder, not inside the
 * source when the source is a folder, and (for a move) somewhere new. */
static const char *check_target(const struct kui_files_job *job, bool directory) {
    FILINFO folder;
    FRESULT r = stat_path(job->target, &folder);
    if(r == FR_OK && !(folder.fattrib & AM_DIR)) return "The destination is not a folder";
    if(r != FR_OK) return missing(r) ? "The destination folder is no longer on the card" : "Cannot read the destination folder";
    if(directory && kui_files_within(job->target, job->source))
        return job->op == KUI_FILES_OP_COPY ? "A folder cannot be copied into itself" : "A folder cannot be moved into itself";
    char parent[KUI_FILES_PATH_CAP];
    if(job->op == KUI_FILES_OP_MOVE && kui_files_parent(parent, job->source) &&
       kui_files_within(parent, job->target) && kui_files_within(job->target, parent))
        return "It is already in this folder";
    return NULL;
}

/* ---- Preview ---- */
bool kui_files_preview(const struct kui_files_job *job, struct kui_files_preview *out,
                       kui_log_fn log, kui_cancel_fn cancel, kui_app_progress_fn progress) {
    if(!out) return false;
    memset(out, 0, sizeof(*out));
    struct kui_app_status *status = &out->status;
    /* The result names its job even when refused, so the menu shows why. */
    if(job) { out->job = *job; out->job.name[0] = 0; }
    if(!job_valid(job) || job->op == KUI_FILES_OP_RENAME || job->op == KUI_FILES_OP_MKDIR) {
        say(status, "Invalid item");
        finish_status(status, false, false);
        return false;
    }
    const char *problem = refused(job);
    if(problem) { say(status, "%s", problem); finish_status(status, false, false); return false; }
    if(stopped(cancel)) { say(status, "Stopped before starting"); finish_status(status, false, true); return false; }
    struct session session;
    session.connected = false;
    struct walker *walker = malloc(sizeof(*walker));
    char text[128];
    bool ok = false, was_stopped = false;
    problem = "Insufficient memory";
    if(!walker) goto done;
    if(!begin(&session, log, &problem)) goto done;
    FILINFO info;
    FRESULT r = stat_path(job->source, &info);
    if(r != FR_OK) { problem = missing(r) ? "This item is no longer on the card" : "Cannot read this item"; goto done; }
    out->directory = (info.fattrib & AM_DIR) != 0;
    out->date = info.fdate; out->time = info.ftime; out->attributes = info.fattrib;
    if(!out->directory) {
        out->bytes = (uint64_t)info.fsize;
        out->files = 1;
        out->read_only = (info.fattrib & AM_RDO) != 0;
    }
    bool movable = job->op == KUI_FILES_OP_COPY || job->op == KUI_FILES_OP_MOVE;
    if(movable && (problem = check_target(job, out->directory)) != NULL) goto done;
    uint64_t cluster = 0, allocated = 0;
    if(job->op == KUI_FILES_OP_COPY) {
        say(status, "Checking free space...");
        report(progress, status);
        DWORD clusters = 0;
        FATFS *fs = NULL;
        if(f_getfree("0:", &clusters, &fs) == FR_OK && fs) {
            cluster = (uint64_t)fs->csize * 512u;
            out->free_bytes = (uint64_t)clusters * cluster;
            out->free_known = true;
        }
    }
    size_t longest = 0;
    if(out->directory && job->op != KUI_FILES_OP_MOVE) {
        struct tally t = {0};
        char root[CARD_CAP];
        card(root, job->source);
        const char *failed = count_tree(walker, root, cluster, &t, status, progress, cancel);
        if(failed == STOPPED) { was_stopped = true; problem = "Stopped while counting"; goto done; }
        if(failed) { problem = failed; goto done; }
        out->bytes = t.bytes; out->files = t.files; out->folders = t.folders; out->read_only = t.read_only;
        allocated = t.allocated + cluster;
        longest = t.longest;
    } else allocated = rounded(out->bytes, cluster);
    if(movable) {
        if(!free_name(job->target, kui_files_leaf(job->source), out->directory, out->job.name, &out->renamed, &problem))
            goto done;
        /* Copies are first written below a KUI-copy-<n>.kui-part folder. */
        size_t top = strlen(out->job.name) > 24u ? strlen(out->job.name) : 24u;
        if(job->op == KUI_FILES_OP_COPY && 2u + strlen(job->target) + 1u + top + longest >= WALK_CAP) {
            problem = "Paths inside would be too long in the destination";
            goto done;
        }
    }
    if(job->op == KUI_FILES_OP_COPY && out->free_known && allocated + cluster > out->free_bytes) {
        char need[16], have[16];
        kui_files_size_text(need, allocated + cluster);
        kui_files_size_text(have, out->free_bytes);
        snprintf(text, sizeof(text), "Not enough free space: needs %s, %s free", need, have);
        problem = text;
        goto done;
    }
    out->ready = job->op != KUI_FILES_OP_DETAILS;
    ok = true;
done:
    if(!end(&session) && ok) { ok = false; problem = "Cannot release SD filesystem"; }
    free(walker);
    if(ok) {
        out->status.done = out->status.total = 0;
        say(status, "%s", job->op == KUI_FILES_OP_DETAILS ? "Details read" : out->renamed ?
            "Ready; the name is taken there, so a numbered name is used" : "Ready to confirm");
    } else {
        out->ready = false;
        say(status, "%s", problem);
    }
    finish_status(status, ok, was_stopped);
    if(log) log("Files %s check %s: %s", op_verb(job->op), job->source, status->message);
    return ok;
}

/* ---- Commit ---- */
static const char *commit_copy(const struct kui_files_job *job, const struct kui_files_preview *totals,
                               const FILINFO *info, struct kui_app_status *status, kui_app_progress_fn progress,
                               kui_cancel_fn cancel, bool *was_stopped) {
    bool directory = (info->fattrib & AM_DIR) != 0;
    char destination[KUI_FILES_PATH_CAP], source[CARD_CAP], part[CARD_CAP], final[CARD_CAP];
    FILINFO seen;
    if(!kui_files_join(destination, job->target, job->name)) return "The destination path would be too long";
    FRESULT r = stat_path(destination, &seen);
    if(r == FR_OK) return "The chosen name is now taken there; review the copy again";
    if(r != FR_NO_FILE) return "Cannot check the destination folder";
    if(!part_path(job->target, part)) return "Cannot find a free name for the partial copy";
    card(source, job->source);
    card(final, destination);
    struct copier *c = calloc(1, sizeof(*c));
    uint8_t *buffer = malloc(CHUNK);
    const char *problem = NULL;
    if(!c || !buffer) { free(c); free(buffer); return "Insufficient memory to copy"; }
    c->buffer = buffer; c->status = status; c->progress = progress; c->cancel = cancel;
    uint64_t bytes = directory ? (totals ? totals->bytes : 0) : (uint64_t)info->fsize;
    c->total = 2u * bytes;
    c->files = directory ? (totals ? totals->files : 0) : 1u;
    problem = directory ? copy_tree(c, source, part, info) : copy_file(c, source, part, info);
    if(!problem) {
        r = f_rename(part, final);
        if(r != FR_OK) problem = r == FR_EXIST ? "The chosen name was taken during the copy" : "Cannot give the copy its name";
    }
    if(problem) {
        /* Remove the partial copy, whatever stopped it. */
        *was_stopped = problem == STOPPED;
        FILINFO left;
        r = f_stat(part, &left);
        bool cleaned = r == FR_NO_FILE;
        if(r == FR_OK) {
            struct deletion d = {0};
            cleaned = left.fattrib & AM_DIR ? !delete_tree(&c->walk, part, &d, NULL, 0, NULL, NULL) :
                remove_one(part) == FR_OK;
        }
        note(status, cleaned ? "The partial copy was removed." : "Could not remove the partial copy %s.",
             kui_files_leaf(part));
    } else {
        status->done = status->total = c->total;
        char size[16];
        kui_files_size_text(size, bytes);
        note(status, "%u file%s, %s, read back and checked.", c->file, c->file == 1 ? "" : "s", size);
    }
    free(buffer);
    free(c);
    return problem;
}
bool kui_files_commit(const struct kui_files_job *job, const struct kui_files_preview *totals,
                      struct kui_app_status *out, kui_log_fn log, kui_cancel_fn cancel,
                      kui_app_progress_fn progress) {
    if(!out) return false;
    memset(out, 0, sizeof(*out));
    if(!job_valid(job) || job->op == KUI_FILES_OP_DETAILS ||
       ((job->op == KUI_FILES_OP_COPY || job->op == KUI_FILES_OP_MOVE) && !kui_destination_name_valid(job->name))) {
        say(out, "Invalid operation");
        finish_status(out, false, false);
        return false;
    }
    const char *problem = refused(job);
    if(problem) { say(out, "%s", problem); finish_status(out, false, false); return false; }
    if(stopped(cancel)) { say(out, "Stopped before starting"); finish_status(out, false, true); return false; }
    struct session session;
    session.connected = false;
    struct walker *walker = NULL;
    bool ok = false, was_stopped = false;
    char done_text[128] = "", path[KUI_FILES_PATH_CAP], from[CARD_CAP], to[CARD_CAP];
    const char *name = kui_files_leaf(job->source);
    struct deletion deleted = {0};
    if(!begin(&session, log, &problem)) goto done;
    FILINFO info;
    FRESULT r;
    if(job->op == KUI_FILES_OP_MKDIR) {
        if(!kui_files_join(path, job->source, job->name)) { problem = "That name makes the path too long"; goto done; }
        card(to, path);
        r = f_mkdir(to);
        if(r != FR_OK) {
            problem = r == FR_EXIST ? "An item with that name is already here" :
                missing(r) ? "This folder is no longer on the card" : "Cannot create the folder";
            goto done;
        }
        snprintf(done_text, sizeof(done_text), "Created folder %.100s", job->name);
        ok = true;
        goto done;
    }
    r = stat_path(job->source, &info);
    if(r != FR_OK) { problem = missing(r) ? "This item is no longer on the card" : "Cannot read this item"; goto done; }
    bool directory = (info.fattrib & AM_DIR) != 0;
    card(from, job->source);
    switch(job->op) {
    case KUI_FILES_OP_RENAME: {
        char parent[KUI_FILES_PATH_CAP];
        if(!kui_files_parent(parent, job->source) || !kui_files_join(path, parent, job->name)) {
            problem = "That name makes the path too long";
            break;
        }
        if(!strcmp(path, job->source)) { snprintf(done_text, sizeof(done_text), "The name is unchanged"); ok = true; break; }
        card(to, path);
        r = f_rename(from, to);
        if(r != FR_OK) {
            problem = r == FR_EXIST ? "Another item here already has that name" :
                missing(r) ? "This item is no longer on the card" : "Cannot rename this item";
            break;
        }
        snprintf(done_text, sizeof(done_text), "Renamed to %.100s", job->name);
        ok = true;
        break;
    }
    case KUI_FILES_OP_MOVE: {
        if((problem = check_target(job, directory)) != NULL) break;
        if(!kui_files_join(path, job->target, job->name)) { problem = "The destination path would be too long"; break; }
        FILINFO seen;
        r = stat_path(path, &seen);
        if(r != FR_NO_FILE) { problem = r == FR_OK ? "The chosen name is now taken there; review the move again" :
            "Cannot check the destination folder"; break; }
        card(to, path);
        r = f_rename(from, to);
        if(r != FR_OK) { problem = missing(r) ? "This item is no longer on the card" : "Cannot move this item"; break; }
        snprintf(done_text, sizeof(done_text), "Moved to %.100s", job->target);
        if(strcmp(job->name, name)) note(out, "Its name there is %s.", job->name);
        ok = true;
        break;
    }
    case KUI_FILES_OP_DELETE:
        if(!directory) {
            if(remove_one(from) != FR_OK) { problem = "Cannot delete this file"; break; }
            deleted.files = 1;
        } else {
            walker = malloc(sizeof(*walker));
            if(!walker) { problem = "Insufficient memory"; break; }
            uint64_t total = totals ? (uint64_t)totals->files + totals->folders + 1u : 0;
            problem = delete_tree(walker, from, &deleted, out, total, progress, cancel);
            if(problem == STOPPED) {
                was_stopped = true;
                note(out, "%u files and %u folders were deleted; the rest remain.", deleted.files, deleted.folders);
                break;
            }
            if(problem) break;
        }
        snprintf(done_text, sizeof(done_text), "Deleted %.100s", name);
        ok = true;
        break;
    case KUI_FILES_OP_COPY:
        if((problem = check_target(job, directory)) != NULL) break;
        problem = commit_copy(job, totals, &info, out, progress, cancel, &was_stopped);
        if(problem) break;
        snprintf(done_text, sizeof(done_text), "Copied to %.100s", job->target);
        if(strcmp(job->name, name)) note(out, "Its name there is %s.", job->name);
        ok = true;
        break;
    default:
        problem = "Invalid operation";
        break;
    }
done:
    if(!end(&session) && ok) { ok = false; problem = "Cannot release SD filesystem"; }
    free(walker);
    if(ok) say(out, "%s", done_text);
    else if(was_stopped) say(out, "%s stopped", job->op == KUI_FILES_OP_COPY ? "Copy" : "Delete");
    else say(out, "%s", problem ? problem : "Operation failed");
    finish_status(out, ok, was_stopped);
    if(log) log("Files %s %s%s%s: %s", op_verb(job->op), job->source, job->target[0] ? " -> " : "",
                job->target, out->message);
    return ok;
}
