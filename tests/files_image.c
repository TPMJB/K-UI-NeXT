/* SPDX-License-Identifier: GPL-3.0-only */
#define _DEFAULT_SOURCE
#include "kui/files.h"
#include "kui/media.h"
#include "fixtures/cover_images.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Real FatFs on a card image made by tests/test_files_images.py. Each case
 * seeds its own files, runs File Manager operations and checks the card. */
static struct {
    FILE *image;
    uint64_t blocks;
    const char *fault;
    unsigned files, dirs, progress, cancel_after, writes, fail_after;
    bool active, connected, cancelled;
    FIL *corrupt;
} test;
static FATFS fs;

static void log_line(const char *format, ...) {
    va_list args; va_start(args, format); vprintf(format, args); va_end(args); puts("");
}
static uint64_t blocks(void *ctx) { (void)ctx; return test.blocks; }
static int read_image(void *ctx, uint32_t block, size_t count, uint8_t *data) {
    (void)ctx;
    return fseeko(test.image, (off_t)block * 512, SEEK_SET) || fread(data, 512, count, test.image) != count ? -1 : 0;
}
static int write_image(void *ctx, uint32_t block, size_t count, const uint8_t *data) {
    (void)ctx;
    return fseeko(test.image, (off_t)block * 512, SEEK_SET) || fwrite(data, 512, count, test.image) != count ? -1 : 0;
}
static int sync_image(void *ctx) { (void)ctx; return fflush(test.image) || fsync(fileno(test.image)) ? -1 : 0; }
static const struct kui_media_ops media = {NULL, blocks, read_image, write_image, sync_image};
bool kui_sd_connect(void) {
    assert(!test.connected);
    test.connected = test.active = true;
    kui_media_set(&media);
    return true;
}
void kui_sd_disconnect(void) {
    assert(test.connected);
    /* Every operation leaves no file or folder open. */
    assert(!test.files && !test.dirs);
    test.connected = test.active = false;
    kui_media_set(NULL);
}
static bool cancel(void) { return test.cancelled; }
static void progress(const struct kui_app_status *status) {
    assert(status->message[0]);
    if(++test.progress == test.cancel_after) test.cancelled = true;
}

FRESULT __real_f_open(FIL *file, const TCHAR *path, BYTE flags);
FRESULT __wrap_f_open(FIL *file, const TCHAR *path, BYTE flags) {
    FRESULT result = __real_f_open(file, path, flags);
    if(test.active && result == FR_OK) {
        ++test.files;
        /* verify-fail: the read-back of a copy sees one changed byte. */
        if(!strcmp(test.fault, "verify-fail") && flags == FA_READ && strstr(path, "KUI-copy-")) test.corrupt = file;
    }
    return result;
}
FRESULT __real_f_close(FIL *file);
FRESULT __wrap_f_close(FIL *file) {
    FRESULT result = __real_f_close(file);
    if(test.active) { assert(test.files); --test.files; }
    if(file == test.corrupt) test.corrupt = NULL;
    return result;
}
FRESULT __real_f_opendir(DIR *dir, const TCHAR *path);
FRESULT __wrap_f_opendir(DIR *dir, const TCHAR *path) {
    FRESULT result = __real_f_opendir(dir, path);
    if(test.active && result == FR_OK) ++test.dirs;
    return result;
}
FRESULT __real_f_closedir(DIR *dir);
FRESULT __wrap_f_closedir(DIR *dir) {
    FRESULT result = __real_f_closedir(dir);
    if(test.active) { assert(test.dirs); --test.dirs; }
    return result;
}
FRESULT __real_f_write(FIL *file, const void *data, UINT bytes, UINT *written);
FRESULT __wrap_f_write(FIL *file, const void *data, UINT bytes, UINT *written) {
    if(test.active && test.fail_after && ++test.writes > test.fail_after) { *written = 0; return FR_DISK_ERR; }
    return __real_f_write(file, data, bytes, written);
}
FRESULT __real_f_read(FIL *file, void *data, UINT bytes, UINT *got);
FRESULT __wrap_f_read(FIL *file, void *data, UINT bytes, UINT *got) {
    FRESULT result = __real_f_read(file, data, bytes, got);
    if(result == FR_OK && file == test.corrupt && *got) { ((uint8_t *)data)[*got / 2u] ^= 0x10u; test.corrupt = NULL; }
    return result;
}

/* ---- Card helpers, used with the card mounted outside any operation ---- */
static void mounted(bool on) {
    if(on) { kui_media_set(&media); assert(kui_mount(&fs, log_line)); }
    else { assert(f_mount(NULL, "0:", 0) == FR_OK); kui_media_set(NULL); }
}
static void card_path(char out[600], const char *path) { assert(snprintf(out, 600, "0:%s", path) < 600); }
static uint8_t pattern(uint32_t seed, uint64_t at) {
    uint32_t x = seed ^ (uint32_t)(at * 2654435761u) ^ (uint32_t)(at >> 32);
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return (uint8_t)x;
}
static void put(const char *path, uint64_t bytes, uint32_t seed) {
    char c[600]; card_path(c, path);
    FIL file; UINT written;
    assert(f_open(&file, c, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK);
    static uint8_t chunk[65536];
    for(uint64_t at = 0; at < bytes;) {
        UINT n = bytes - at < sizeof(chunk) ? (UINT)(bytes - at) : (UINT)sizeof(chunk);
        for(UINT i = 0; i < n; ++i) chunk[i] = pattern(seed, at + i);
        assert(f_write(&file, chunk, n, &written) == FR_OK && written == n);
        at += n;
    }
    assert(f_close(&file) == FR_OK);
}
static void put_bytes(const char *path, const void *data, size_t bytes) {
    char c[600]; card_path(c, path);
    FIL file; UINT written;
    assert(f_open(&file, c, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK);
    assert(f_write(&file, data, (UINT)bytes, &written) == FR_OK && written == bytes);
    assert(f_close(&file) == FR_OK);
}
static void folder(const char *path) { char c[600]; card_path(c, path); assert(f_mkdir(c) == FR_OK); }
static const uint16_t stamp_date = (uint16_t)((21u << 9) | (2u << 5) | 3u), stamp_time = (uint16_t)((4u << 11) | (5u << 5) | 3u);
static void stamp(const char *path) {
    char c[600]; card_path(c, path);
    FILINFO info; memset(&info, 0, sizeof(info));
    info.fdate = stamp_date; info.ftime = stamp_time;
    assert(f_utime(c, &info) == FR_OK);
}
static FRESULT look(const char *path, FILINFO *info) { char c[600]; card_path(c, path); return f_stat(c, info); }
static bool present(const char *path) { FILINFO info; return look(path, &info) == FR_OK; }
static bool same(const char *a, const char *b) {
    char ca[600], cb[600]; card_path(ca, a); card_path(cb, b);
    FIL x, y;
    assert(f_open(&x, ca, FA_READ) == FR_OK && f_open(&y, cb, FA_READ) == FR_OK);
    bool equal = f_size(&x) == f_size(&y);
    static uint8_t bx[8192], by[8192];
    while(equal) {
        UINT gx, gy;
        assert(f_read(&x, bx, sizeof(bx), &gx) == FR_OK && f_read(&y, by, sizeof(by), &gy) == FR_OK);
        if(gx != gy || memcmp(bx, by, gx)) equal = false;
        if(!gx) break;
    }
    assert(f_close(&x) == FR_OK && f_close(&y) == FR_OK);
    return equal;
}
/* Entries left by an unfinished copy, in one folder. */
static unsigned parts(const char *path) {
    char c[600]; card_path(c, path);
    DIR dir; FILINFO info; unsigned n = 0;
    assert(f_opendir(&dir, c) == FR_OK);
    while(f_readdir(&dir, &info) == FR_OK && info.fname[0]) if(strstr(info.fname, "KUI-copy-")) ++n;
    assert(f_closedir(&dir) == FR_OK);
    return n;
}
static unsigned entries(const char *path) {
    char c[600]; card_path(c, path);
    DIR dir; FILINFO info; unsigned n = 0;
    assert(f_opendir(&dir, c) == FR_OK);
    while(f_readdir(&dir, &info) == FR_OK && info.fname[0]) ++n;
    assert(f_closedir(&dir) == FR_OK);
    return n;
}

/* ---- Operations as the menu runs them ---- */
static struct kui_files_job job(enum kui_files_op op, const char *source, const char *target, const char *name) {
    struct kui_files_job j; memset(&j, 0, sizeof(j));
    j.op = op;
    snprintf(j.source, sizeof(j.source), "%s", source);
    snprintf(j.target, sizeof(j.target), "%s", target ? target : "");
    snprintf(j.name, sizeof(j.name), "%s", name ? name : "");
    return j;
}
static bool check(struct kui_files_job j, struct kui_files_preview *out) {
    test.progress = 0;
    bool ok = kui_files_preview(&j, out, log_line, cancel, progress);
    assert(ok == out->status.passed && out->status.complete && out->status.message[0]);
    assert(!ok || !strcmp(out->job.source, j.source));
    return ok;
}
static bool run(struct kui_files_job j, const struct kui_files_preview *totals, struct kui_app_status *out) {
    test.progress = 0;
    bool ok = kui_files_commit(&j, totals, out, log_line, cancel, progress);
    assert(ok == out->passed && out->complete && out->message[0]);
    return ok;
}
/* Checks, then runs with the name the check chose. */
static bool checked_run(enum kui_files_op op, const char *source, const char *target, struct kui_files_preview *pv,
                        struct kui_app_status *status) {
    if(!check(job(op, source, target, NULL), pv)) return false;
    assert(pv->ready);
    return run(job(op, source, target, pv->job.name), pv, status);
}
static void page(const char *path, enum kui_files_seek seek, const char *anchor, bool directory, bool folders,
                 struct kui_files_page *out) {
    struct kui_files_request r; memset(&r, 0, sizeof(r));
    snprintf(r.path, sizeof(r.path), "%s", path);
    snprintf(r.anchor, sizeof(r.anchor), "%s", anchor ? anchor : "");
    r.anchor_directory = directory; r.folders_only = folders; r.seek = seek;
    assert(kui_files_list(&r, out, log_line, cancel));
    assert(out->ok && !strcmp(out->path, path) && out->folders_only == folders);
}
static void rows(const struct kui_files_page *p, unsigned before, unsigned total, const char *const *names, unsigned count) {
    assert(p->before == before && p->total == total && p->count == count);
    for(unsigned i = 0; i < count; ++i) assert(!strcmp(p->entries[i].name, names[i]));
}

/* ---- Cases ---- */
static char long_name[160];
static void seed_list(void) {
    static const char *const dirs[] = {"zeta", "Beta", "alpha", "EPSILON", "Delta", "gamma"};
    folder("/List");
    for(unsigned i = 0; i < 6; ++i) { char p[64]; snprintf(p, sizeof(p), "/List/%s", dirs[i]); folder(p); }
    for(unsigned i = 15; i-- > 0;) {
        char p[64]; snprintf(p, sizeof(p), i % 2 ? "/List/F%02u.TXT" : "/List/f%02u.txt", i);
        put(p, 100u + i, i);
    }
    memset(long_name, 'x', sizeof(long_name) - 1u);
    memcpy(long_name, "long-", 5);
    memcpy(long_name + sizeof(long_name) - 5u, ".txt", 5);
    char p[200]; snprintf(p, sizeof(p), "/List/%s", long_name); put(p, 7, 99);
}
static void case_list(void) {
    struct kui_files_page p;
    static const char *const first[] = {"alpha", "Beta", "Delta", "EPSILON", "gamma", "zeta", "f00.txt", "F01.TXT"};
    static const char *const second[] = {"f02.txt", "F03.TXT", "f04.txt", "F05.TXT", "f06.txt", "F07.TXT", "f08.txt", "F09.TXT"};
    static const char *const third[] = {"f10.txt", "F11.TXT", "f12.txt", "F13.TXT", "f14.txt", "[Name too long]"};
    page("/List", KUI_FILES_SEEK_FIRST, NULL, false, false, &p);
    rows(&p, 0, 22, first, 8);
    assert(p.entries[0].directory && !p.entries[6].directory && p.entries[6].bytes == 100u && !p.entries[0].disabled);
    assert(!strcmp(p.first, "alpha") && p.first_directory && !strcmp(p.last, "F01.TXT") && !p.last_directory);
    page("/List", KUI_FILES_SEEK_NEXT, p.last, p.last_directory, false, &p);
    rows(&p, 8, 22, second, 8);
    page("/List", KUI_FILES_SEEK_NEXT, p.last, p.last_directory, false, &p);
    rows(&p, 16, 22, third, 6);
    assert(p.entries[5].disabled && !strcmp(p.last, long_name) && !p.entries[4].disabled);
    struct kui_files_page end;
    page("/List", KUI_FILES_SEEK_NEXT, p.last, false, false, &end);
    assert(!end.count && end.before == 22 && end.total == 22);
    page("/List", KUI_FILES_SEEK_PREVIOUS, p.first, p.first_directory, false, &p);
    rows(&p, 8, 22, second, 8);
    page("/List", KUI_FILES_SEEK_PREVIOUS, p.first, p.first_directory, false, &p);
    rows(&p, 0, 22, first, 8);
    /* Too little before the anchor for a page: the first page. */
    page("/List", KUI_FILES_SEEK_PREVIOUS, "Delta", true, false, &p);
    rows(&p, 0, 22, first, 8);
    page("/List", KUI_FILES_SEEK_AT, "gamma", true, false, &p);
    assert(p.before == 4 && !strcmp(p.entries[0].name, "gamma") && !strcmp(p.entries[2].name, "f00.txt"));
    /* An anchor that is gone: the rows after where it was. */
    page("/List", KUI_FILES_SEEK_AT, "f045", false, false, &p);
    assert(p.before == 11 && !strcmp(p.entries[0].name, "F05.TXT"));
    page("/List", KUI_FILES_SEEK_FIRST, NULL, false, true, &p);
    rows(&p, 0, 6, first, 6);
    page("/List/alpha", KUI_FILES_SEEK_FIRST, NULL, false, false, &p);
    assert(!p.count && !p.total && !strcmp(p.message, "This folder is empty."));
    page("/List/alpha", KUI_FILES_SEEK_FIRST, NULL, false, true, &p);
    assert(!strcmp(p.message, "No folders here."));
    struct kui_files_request r; memset(&r, 0, sizeof(r));
    snprintf(r.path, sizeof(r.path), "/Nope");
    assert(!kui_files_list(&r, &p, log_line, cancel) && !p.ok && strstr(p.message, "no longer on the card"));
    snprintf(r.path, sizeof(r.path), "/List/../x");
    assert(!kui_files_list(&r, &p, log_line, cancel) && !strcmp(p.message, "Invalid folder"));
    snprintf(r.path, sizeof(r.path), "/List"); r.seek = KUI_FILES_SEEK_NEXT;
    assert(!kui_files_list(&r, &p, log_line, cancel)); /* NEXT needs an anchor */
    r.seek = KUI_FILES_SEEK_FIRST; test.cancelled = true;
    assert(!kui_files_list(&r, &p, log_line, cancel) && strstr(p.message, "stopped"));
    test.cancelled = false;
    page("/", KUI_FILES_SEEK_FIRST, NULL, false, false, &p);
    assert(p.count >= 1 && !strcmp(p.entries[0].name, "List"));
}
static void seed_tree(void) {
    folder("/Src"); folder("/Dst");
    put("/Src/data.bin", 100000, 1); stamp("/Src/data.bin");
    folder("/Src/tree"); folder("/Src/tree/sub"); folder("/Src/tree/sub/deeper"); folder("/Src/tree/empty");
    put("/Src/tree/a.txt", 0, 2);
    put("/Src/tree/sub/b.bin", 70000, 3); stamp("/Src/tree/sub/b.bin");
    put("/Src/tree/sub/deeper/c.txt", 5, 4);
    stamp("/Src/tree/sub");
}
static void same_tree(const char *a, const char *b) {
    static const char *const files[] = {"/a.txt", "/sub/b.bin", "/sub/deeper/c.txt"};
    static const char *const dirs[] = {"/sub", "/sub/deeper", "/empty"};
    char x[300], y[300];
    for(unsigned i = 0; i < 3; ++i) {
        snprintf(x, sizeof(x), "%s%s", a, files[i]); snprintf(y, sizeof(y), "%s%s", b, files[i]);
        assert(same(x, y));
        snprintf(x, sizeof(x), "%s%s", b, dirs[i]);
        FILINFO info; assert(look(x, &info) == FR_OK && (info.fattrib & AM_DIR));
    }
    snprintf(x, sizeof(x), "%s", b); assert(entries(x) == 3);
}
static void case_copy(void) {
    struct kui_files_preview pv; struct kui_app_status st;
    mounted(true); seed_tree(); mounted(false);
    assert(checked_run(KUI_FILES_OP_COPY, "/Src/data.bin", "/Dst", &pv, &st));
    assert(!pv.directory && pv.files == 1 && pv.bytes == 100000 && pv.free_known && pv.free_bytes > 100000);
    assert(!pv.renamed && !strcmp(pv.job.name, "data.bin") && test.progress);
    assert(!strcmp(st.message, "Copied to /Dst") && st.line_count && strstr(st.lines[0], "read back and checked"));
    assert(st.done == st.total && st.total == 200000);
    mounted(true);
    FILINFO info;
    assert(same("/Src/data.bin", "/Dst/data.bin") && look("/Dst/data.bin", &info) == FR_OK);
    assert(info.fdate == stamp_date && info.ftime == stamp_time && !parts("/Dst"));
    mounted(false);
    /* The same name again takes the next number; so does a copy beside itself. */
    assert(checked_run(KUI_FILES_OP_COPY, "/Src/data.bin", "/Dst", &pv, &st));
    assert(pv.renamed && !strcmp(pv.job.name, "data (2).bin") && st.line_count >= 2);
    assert(checked_run(KUI_FILES_OP_COPY, "/Src/data.bin", "/Src", &pv, &st) && !strcmp(pv.job.name, "data (2).bin"));
    mounted(true);
    assert(same("/Src/data.bin", "/Dst/data (2).bin") && same("/Src/data.bin", "/Src/data (2).bin"));
    mounted(false);
    /* A folder with everything below it. */
    assert(checked_run(KUI_FILES_OP_COPY, "/Src/tree", "/Dst", &pv, &st));
    assert(pv.directory && pv.files == 3 && pv.folders == 3 && pv.bytes == 70005);
    mounted(true);
    same_tree("/Src/tree", "/Dst/tree");
    assert(look("/Dst/tree/sub", &info) == FR_OK && info.fdate == stamp_date && info.ftime == stamp_time);
    assert(look("/Dst/tree/sub/b.bin", &info) == FR_OK && info.fdate == stamp_date);
    assert(!parts("/Dst") && entries("/Dst") == 3);
    mounted(false);
    /* Never into itself; the check refuses before anything is written. */
    assert(!check(job(KUI_FILES_OP_COPY, "/Src/tree", "/Src/tree/sub", NULL), &pv));
    assert(strstr(pv.status.message, "copied into itself") && !pv.ready);
    assert(!check(job(KUI_FILES_OP_COPY, "/Src/tree", "/Src/tree", NULL), &pv));
    assert(!check(job(KUI_FILES_OP_COPY, "/Src/missing", "/Dst", NULL), &pv) && strstr(pv.status.message, "no longer"));
    assert(!check(job(KUI_FILES_OP_COPY, "/Src/data.bin", "/Nope", NULL), &pv) && strstr(pv.status.message, "destination"));
    assert(!check(job(KUI_FILES_OP_COPY, "/Src/data.bin", "/Src/data.bin", NULL), &pv) && strstr(pv.status.message, "not a folder"));
    /* A commit whose name was taken since its check refuses to replace it. */
    assert(!run(job(KUI_FILES_OP_COPY, "/Src/data.bin", "/Dst", "data.bin"), NULL, &st) && strstr(st.message, "now taken"));
    assert(!run(job(KUI_FILES_OP_COPY, "/Src/tree", "/Src/tree/sub", "tree"), NULL, &st) && strstr(st.message, "into itself"));
    /* Copies of the K-UI files themselves are allowed. */
    mounted(true); folder("/KUI"); put("/KUI/runtime.kui", 3000, 9); mounted(false);
    assert(checked_run(KUI_FILES_OP_COPY, "/KUI/runtime.kui", "/Dst", &pv, &st));
    mounted(true); assert(same("/KUI/runtime.kui", "/Dst/runtime.kui")); mounted(false);
}
static void case_cancel(void) {
    struct kui_files_preview pv; struct kui_app_status st;
    mounted(true); seed_tree(); mounted(false);
    assert(check(job(KUI_FILES_OP_COPY, "/Src/tree", "/Dst", NULL), &pv));
    test.cancel_after = 3;
    assert(!run(job(KUI_FILES_OP_COPY, "/Src/tree", "/Dst", pv.job.name), &pv, &st));
    assert(st.stopped && !st.errors && !strcmp(st.message, "Copy stopped") && strstr(st.lines[0], "partial copy was removed"));
    test.cancelled = false; test.cancel_after = 0;
    mounted(true);
    assert(!entries("/Dst") && entries("/Src/tree") == 3);
    mounted(false);
    /* A single large file stopped part way leaves nothing either. */
    test.cancel_after = 2;
    assert(!run(job(KUI_FILES_OP_COPY, "/Src/data.bin", "/Dst", "data.bin"), NULL, &st) && st.stopped);
    test.cancelled = false; test.cancel_after = 0;
    mounted(true); assert(!entries("/Dst")); mounted(false);
    /* Stop while counting. */
    test.cancel_after = 1;
    assert(!check(job(KUI_FILES_OP_DELETE, "/Src/tree", NULL, NULL), &pv) && pv.status.stopped);
    test.cancelled = false; test.cancel_after = 0;
    /* Stop part way through a delete keeps what is left. */
    assert(check(job(KUI_FILES_OP_DELETE, "/Src/tree", NULL, NULL), &pv) && pv.files == 3 && pv.folders == 3);
    test.cancel_after = 2;
    assert(!run(job(KUI_FILES_OP_DELETE, "/Src/tree", NULL, NULL), &pv, &st));
    assert(st.stopped && !strcmp(st.message, "Delete stopped") && strstr(st.lines[0], "the rest remain"));
    test.cancelled = false; test.cancel_after = 0;
    mounted(true); assert(present("/Src/tree")); mounted(false);
    /* Stopped before starting: nothing happens. */
    test.cancelled = true;
    assert(!run(job(KUI_FILES_OP_MKDIR, "/", NULL, "Never"), NULL, &st) && st.stopped);
    test.cancelled = false;
    mounted(true); assert(!present("/Never")); mounted(false);
}
static void case_write_fail(void) {
    struct kui_app_status st;
    mounted(true); seed_tree(); mounted(false);
    test.fail_after = 1; test.writes = 0;
    assert(!run(job(KUI_FILES_OP_COPY, "/Src/data.bin", "/Dst", "data.bin"), NULL, &st));
    assert(!st.stopped && st.errors && !strcmp(st.message, "Cannot write the copy"));
    assert(strstr(st.lines[0], "partial copy was removed"));
    test.writes = 0;
    assert(!run(job(KUI_FILES_OP_COPY, "/Src/tree", "/Dst", "tree"), NULL, &st));
    test.fail_after = 0;
    mounted(true); assert(!entries("/Dst") && entries("/Src/tree") == 3); mounted(false);
}
static void case_verify_fail(void) {
    struct kui_app_status st;
    mounted(true); seed_tree(); mounted(false);
    assert(!run(job(KUI_FILES_OP_COPY, "/Src/data.bin", "/Dst", "data.bin"), NULL, &st));
    assert(!strcmp(st.message, "The copy does not match the source") && strstr(st.lines[0], "partial copy was removed"));
    assert(!run(job(KUI_FILES_OP_COPY, "/Src/tree", "/Dst", "tree"), NULL, &st));
    assert(!strcmp(st.message, "The copy does not match the source"));
    mounted(true); assert(!entries("/Dst")); mounted(false);
}
static void case_full(void) {
    struct kui_files_preview pv; struct kui_app_status st;
    mounted(true);
    folder("/Big");
    DWORD clusters; FATFS *mounted_fs;
    assert(f_getfree("0:", &clusters, &mounted_fs) == FR_OK);
    uint64_t free_bytes = (uint64_t)clusters * mounted_fs->csize * 512u;
    /* More than half the free space: one copy cannot fit. */
    put("/Big/big.bin", free_bytes / 2u + 4u * 1048576u, 5);
    mounted(false);
    assert(!check(job(KUI_FILES_OP_COPY, "/Big/big.bin", "/", NULL), &pv));
    assert(strstr(pv.status.message, "Not enough free space") && pv.free_known);
    /* The card filling part way (the check skipped) removes the partial copy. */
    assert(!run(job(KUI_FILES_OP_COPY, "/Big/big.bin", "/", "big.bin"), NULL, &st));
    assert(!strcmp(st.message, "The card is full") && strstr(st.lines[0], "partial copy was removed"));
    mounted(true); assert(!present("/big.bin") && !parts("/")); mounted(false);
}
static void case_move(void) {
    struct kui_files_preview pv; struct kui_app_status st;
    mounted(true); seed_tree(); folder("/KUI"); put("/KUI/runtime.kui", 100, 7); put("/Dst/data.bin", 10, 8); mounted(false);
    assert(checked_run(KUI_FILES_OP_MOVE, "/Src/data.bin", "/Dst", &pv, &st));
    assert(pv.renamed && !strcmp(pv.job.name, "data (2).bin") && !strcmp(st.message, "Moved to /Dst"));
    mounted(true);
    FILINFO info;
    assert(!present("/Src/data.bin") && look("/Dst/data (2).bin", &info) == FR_OK && info.fsize == 100000);
    assert(info.fdate == stamp_date);
    mounted(false);
    assert(checked_run(KUI_FILES_OP_MOVE, "/Src/tree", "/Dst", &pv, &st) && !pv.renamed);
    mounted(true);
    assert(!present("/Src/tree") && present("/Dst/tree/sub/deeper/c.txt") && entries("/Dst/tree") == 3);
    mounted(false);
    assert(!check(job(KUI_FILES_OP_MOVE, "/Dst/tree", "/Dst/tree/sub", NULL), &pv) && strstr(pv.status.message, "moved into itself"));
    assert(!check(job(KUI_FILES_OP_MOVE, "/Dst/tree", "/Dst", NULL), &pv) && strstr(pv.status.message, "already in this folder"));
    assert(!check(job(KUI_FILES_OP_MOVE, "/KUI/runtime.kui", "/Dst", NULL), &pv) && strstr(pv.status.message, "K-UI needs"));
    assert(!check(job(KUI_FILES_OP_MOVE, "/KUI", "/Dst", NULL), &pv) && strstr(pv.status.message, "cannot be moved"));
    assert(!run(job(KUI_FILES_OP_MOVE, "/KUI/runtime.kui", "/Dst", "runtime.kui"), NULL, &st) && strstr(st.message, "K-UI needs"));
    /* A name taken since the check is never replaced. */
    mounted(true); put("/Src/x.txt", 3, 11); put("/Dst/x.txt", 4, 12); mounted(false);
    assert(!run(job(KUI_FILES_OP_MOVE, "/Src/x.txt", "/Dst", "x.txt"), NULL, &st) && strstr(st.message, "now taken"));
    assert(!check(job(KUI_FILES_OP_MOVE, "/Src/x.txt", "/Src/x.txt", NULL), &pv) && strstr(pv.status.message, "not a folder"));
    mounted(true);
    FILINFO kept;
    assert(present("/KUI/runtime.kui") && look("/Dst/x.txt", &kept) == FR_OK && kept.fsize == 4 && present("/Src/x.txt"));
    mounted(false);
}
static void case_delete(void) {
    struct kui_files_preview pv; struct kui_app_status st;
    mounted(true);
    seed_tree(); folder("/KUI"); folder("/KUI/apps"); folder("/KUI/apps/games");
    put("/KUI/runtime.kui", 100, 7); put("/KUI/apps/games/retail-boot.kui", 100, 8); put("/KUI/apps/games/probe.kui", 10, 9);
    folder("/KUI/covers"); put("/KUI/covers/x.kcv", 512, 10);
    char c[600]; card_path(c, "/Src/tree/sub/b.bin"); assert(f_chmod(c, AM_RDO, AM_RDO) == FR_OK);
    card_path(c, "/Src/tree/empty"); assert(f_chmod(c, AM_RDO, AM_RDO) == FR_OK);
    mounted(false);
    assert(check(job(KUI_FILES_OP_DELETE, "/Src/data.bin", NULL, NULL), &pv) && pv.files == 1 && pv.bytes == 100000);
    assert(run(job(KUI_FILES_OP_DELETE, "/Src/data.bin", NULL, NULL), &pv, &st) && !strcmp(st.message, "Deleted data.bin"));
    assert(check(job(KUI_FILES_OP_DELETE, "/Src/tree", NULL, NULL), &pv));
    assert(pv.directory && pv.files == 3 && pv.folders == 3 && pv.read_only == 2 && pv.bytes == 70005);
    assert(run(job(KUI_FILES_OP_DELETE, "/Src/tree", NULL, NULL), &pv, &st) && st.done == st.total && st.total == 7);
    mounted(true); assert(!present("/Src/tree") && !entries("/Src")); mounted(false);
    /* What K-UI needs to start stays, whichever way it is named. */
    static const char *const kept[] = {"/KUI/runtime.kui", "/KUI", "/KUI/apps", "/KUI/apps/games",
        "/KUI/apps/games/retail-boot.kui", "/kui/RUNTIME.KUI"};
    for(unsigned i = 0; i < 6; ++i) {
        assert(!check(job(KUI_FILES_OP_DELETE, kept[i], NULL, NULL), &pv) && strstr(pv.status.message, "K-UI needs"));
        assert(!run(job(KUI_FILES_OP_DELETE, kept[i], NULL, NULL), NULL, &st) && strstr(st.message, "cannot be deleted"));
    }
    assert(run(job(KUI_FILES_OP_DELETE, "/KUI/apps/games/probe.kui", NULL, NULL), NULL, &st));
    assert(run(job(KUI_FILES_OP_DELETE, "/KUI/covers", NULL, NULL), NULL, &st));
    assert(!run(job(KUI_FILES_OP_DELETE, "/KUI/covers", NULL, NULL), NULL, &st) && strstr(st.message, "no longer"));
    assert(!run(job(KUI_FILES_OP_DELETE, "/", NULL, NULL), NULL, &st));
    mounted(true);
    assert(present("/KUI/runtime.kui") && present("/KUI/apps/games/retail-boot.kui"));
    assert(!present("/KUI/apps/games/probe.kui") && !present("/KUI/covers"));
    mounted(false);
}
static void case_rename(void) {
    struct kui_app_status st;
    mounted(true); seed_tree(); folder("/KUI"); put("/KUI/runtime.kui", 100, 7); mounted(false);
    assert(run(job(KUI_FILES_OP_RENAME, "/Src/data.bin", NULL, "renamed.bin"), NULL, &st) && !strcmp(st.message, "Renamed to renamed.bin"));
    assert(!run(job(KUI_FILES_OP_RENAME, "/Src/renamed.bin", NULL, "tree"), NULL, &st) && strstr(st.message, "already has that name"));
    assert(run(job(KUI_FILES_OP_RENAME, "/Src/renamed.bin", NULL, "RENAMED.BIN"), NULL, &st));
    assert(run(job(KUI_FILES_OP_RENAME, "/Src/tree", NULL, "Tree 2"), NULL, &st));
    assert(run(job(KUI_FILES_OP_RENAME, "/Src/Tree 2", NULL, "Tree 2"), NULL, &st) && strstr(st.message, "unchanged"));
    assert(!run(job(KUI_FILES_OP_RENAME, "/KUI/runtime.kui", NULL, "old.kui"), NULL, &st) && strstr(st.message, "cannot be renamed"));
    assert(!run(job(KUI_FILES_OP_RENAME, "/Src/missing", NULL, "x"), NULL, &st) && strstr(st.message, "no longer"));
    assert(!run(job(KUI_FILES_OP_RENAME, "/Src/Tree 2", NULL, "bad/name"), NULL, &st) && !strcmp(st.message, "Invalid operation"));
    mounted(true);
    DIR dir; FILINFO info; char c[600]; card_path(c, "/Src");
    bool upper = false;
    assert(f_opendir(&dir, c) == FR_OK);
    while(f_readdir(&dir, &info) == FR_OK && info.fname[0]) if(!strcmp(info.fname, "RENAMED.BIN")) upper = true;
    assert(f_closedir(&dir) == FR_OK);
    assert(upper && present("/Src/Tree 2/sub/b.bin") && !present("/Src/tree/sub") && present("/KUI/runtime.kui"));
    mounted(false);
}
static void case_mkdir(void) {
    struct kui_app_status st;
    mounted(true); seed_tree(); mounted(false);
    assert(run(job(KUI_FILES_OP_MKDIR, "/", NULL, "New Folder"), NULL, &st) && !strcmp(st.message, "Created folder New Folder"));
    assert(!run(job(KUI_FILES_OP_MKDIR, "/", NULL, "new folder"), NULL, &st) && strstr(st.message, "already here"));
    assert(run(job(KUI_FILES_OP_MKDIR, "/Src/tree", NULL, "inner"), NULL, &st));
    assert(!run(job(KUI_FILES_OP_MKDIR, "/Nope", NULL, "x"), NULL, &st) && strstr(st.message, "no longer"));
    assert(!run(job(KUI_FILES_OP_MKDIR, "/", NULL, "trailing."), NULL, &st));
    mounted(true);
    FILINFO info;
    assert(look("/New Folder", &info) == FR_OK && (info.fattrib & AM_DIR) && present("/Src/tree/inner"));
    mounted(false);
    struct kui_files_preview pv;
    assert(check(job(KUI_FILES_OP_DETAILS, "/Src/tree", NULL, NULL), &pv) && !pv.ready);
    assert(pv.files == 3 && pv.folders == 4 && pv.bytes == 70005 && pv.directory);
    assert(check(job(KUI_FILES_OP_DETAILS, "/Src/data.bin", NULL, NULL), &pv));
    assert(pv.files == 1 && pv.bytes == 100000 && pv.date == stamp_date && pv.time == stamp_time && !pv.directory);
    assert(!check(job(KUI_FILES_OP_DETAILS, "/", NULL, NULL), &pv));
}
static void pvr_stride(uint8_t *out, size_t *bytes) {
    /* 64x32 RGB565 stride texture, left half magenta, right half green. */
    size_t payload = 64u * 32u * 2u;
    memcpy(out, "PVRT", 4);
    uint32_t chunk = (uint32_t)(8u + payload);
    for(unsigned i = 0; i < 4; ++i) out[4 + i] = (uint8_t)(chunk >> (8 * i));
    out[8] = 1; out[9] = 9; out[10] = out[11] = 0;
    out[12] = 64; out[13] = 0; out[14] = 32; out[15] = 0;
    for(unsigned y = 0; y < 32; ++y)
        for(unsigned x = 0; x < 64; ++x) {
            uint16_t c = x < 32 ? 0xF81F : 0x07E0;
            out[16 + (y * 64 + x) * 2] = (uint8_t)c; out[17 + (y * 64 + x) * 2] = (uint8_t)(c >> 8);
        }
    *bytes = 16 + payload;
}
static void case_picture(void) {
    static uint16_t pixels[KUI_FILES_PICTURE_EDGE * KUI_FILES_PICTURE_EDGE];
    static uint8_t texture[16 + 64 * 32 * 2];
    size_t texture_bytes;
    pvr_stride(texture, &texture_bytes);
    mounted(true);
    folder("/Pictures");
    put_bytes("/Pictures/rgb.png", cover_png_rgb, sizeof(cover_png_rgb));
    put_bytes("/Pictures/alpha.png", cover_png_rgba, sizeof(cover_png_rgba));
    put_bytes("/Pictures/photo.jpg", cover_jpeg, sizeof(cover_jpeg));
    put_bytes("/Pictures/0GDTEX.PVR", texture, texture_bytes);
    put_bytes("/Pictures/fake.png", "not a picture", 13);
    put("/Pictures/huge.png", KUI_FILES_PICTURE_MAX_BYTES + 1u, 3);
    put("/Pictures/notes.txt", 10, 4);
    mounted(false);
    struct kui_files_picture pic;
    assert(kui_files_picture("/Pictures/rgb.png", pixels, &pic, log_line, cancel));
    assert(pic.ok && !strcmp(pic.format, "PNG") && pic.width == 64 && pic.height == 48 && pic.bytes == sizeof(cover_png_rgb));
    /* Wider than tall: navy above and below, picture in the middle. */
    assert(pixels[0] == 0x0864u && pixels[(KUI_FILES_PICTURE_EDGE / 2u) * KUI_FILES_PICTURE_EDGE + 136u] != 0x0864u);
    assert(kui_files_picture("/Pictures/alpha.png", pixels, &pic, log_line, cancel) && pic.ok);
    assert(kui_files_picture("/Pictures/photo.jpg", pixels, &pic, log_line, cancel) && !strcmp(pic.format, "JPEG"));
    assert(kui_files_picture("/Pictures/0GDTEX.PVR", pixels, &pic, log_line, cancel));
    assert(!strcmp(pic.format, "PVR texture") && pic.width == 64 && pic.height == 32);
    unsigned mid = KUI_FILES_PICTURE_EDGE / 2u;
    assert(pixels[mid * KUI_FILES_PICTURE_EDGE + 20u] == 0xF81Fu && pixels[mid * KUI_FILES_PICTURE_EDGE + 250u] == 0x07E0u);
    assert(!kui_files_picture("/Pictures/fake.png", pixels, &pic, log_line, cancel) && strstr(pic.message, "not a picture"));
    assert(!kui_files_picture("/Pictures/huge.png", pixels, &pic, log_line, cancel) && strstr(pic.message, "up to 3 MB"));
    assert(!kui_files_picture("/Pictures/notes.txt", pixels, &pic, log_line, cancel) && strstr(pic.message, "Only PNG"));
    assert(!kui_files_picture("/Pictures/gone.png", pixels, &pic, log_line, cancel) && strstr(pic.message, "no longer"));
    assert(!kui_files_picture("bad", pixels, &pic, log_line, cancel) && !pic.ok);
}

int main(int argc, char **argv) {
    if(argc != 3) { fprintf(stderr, "usage: files-image IMAGE CASE\n"); return 2; }
    test.image = fopen(argv[1], "r+b");
    assert(test.image);
    struct stat st;
    assert(!fstat(fileno(test.image), &st));
    test.blocks = (uint64_t)st.st_size / 512u;
    test.fault = argv[2];
    const char *name = argv[2];
    if(!strcmp(name, "list")) { mounted(true); seed_list(); mounted(false); case_list(); }
    else if(!strcmp(name, "copy")) case_copy();
    else if(!strcmp(name, "cancel")) case_cancel();
    else if(!strcmp(name, "write-fail")) case_write_fail();
    else if(!strcmp(name, "verify-fail")) case_verify_fail();
    else if(!strcmp(name, "full")) case_full();
    else if(!strcmp(name, "move")) case_move();
    else if(!strcmp(name, "delete")) case_delete();
    else if(!strcmp(name, "rename")) case_rename();
    else if(!strcmp(name, "mkdir")) case_mkdir();
    else if(!strcmp(name, "picture")) case_picture();
    else { fprintf(stderr, "unknown case %s\n", name); return 2; }
    assert(!test.connected && !fclose(test.image));
    printf("PASS File Manager image %s\n", name);
    return 0;
}
