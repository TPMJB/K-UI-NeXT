/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "kui/storage_test.h"
#include "kui/media.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Real FatFs on disposable regular images, with narrowly injected filesystem
 * faults. Wrappers touch only the owned scratch file, never result-store IO. */
static struct {
    FILE *image;
    const char *test;
    uint64_t blocks, us, progress_us;
    unsigned mounts, writes, reads, write_opens, read_opens, progress_calls;
    unsigned unmounts, errors_captured, volume_writes;
    FIL *scratch;
    FATFS *registered;
    bool armed, injected, in_io, writing;
    bool captured_first_cycle;
    uint8_t first_cycle[65536];
    struct kui_storage_errors errors;
    char scratch_path[112];
} t;
static bool is(const char *name) { return !strcmp(t.test, name); }
static void log_line(const char *format, ...) {
    va_list args; va_start(args, format); vprintf(format, args); va_end(args); puts("");
}
static uint64_t blocks(void *ctx) { (void)ctx; return t.blocks; }
static int read_image(void *ctx, uint32_t sector, size_t count, uint8_t *data) {
    (void)ctx;
    t.us += count * (is("soak") ? 1000 : 50);
    return fseeko(t.image, (off_t)sector * 512, SEEK_SET) ||
        fread(data, 512, count, t.image) != count ? -1 : 0;
}
static int write_image(void *ctx, uint32_t sector, size_t count, const uint8_t *data) {
    (void)ctx;
    ++t.volume_writes;
    t.us += count * (is("soak") ? 2000 : 80);
    return fseeko(t.image, (off_t)sector * 512, SEEK_SET) ||
        fwrite(data, 512, count, t.image) != count ? -1 : 0;
}
static int sync_image(void *ctx) {
    (void)ctx; t.us += 1000;
    return fflush(t.image) || fsync(fileno(t.image)) ? -1 : 0;
}
static uint64_t now_us(void *ctx) { (void)ctx; return ++t.us; }
static bool cancelled(void *ctx) {
    (void)ctx;
    return is("cancel-before") || (is("cancel-write") && t.writes >= 3) ||
        (is("cancel-read") && t.reads >= 3);
}
static void progress(void *ctx, const struct kui_storage_test_progress *value) {
    (void)ctx;
    assert(!t.in_io);
    assert(value->done <= value->total && value->total);
    assert(!t.progress_calls || t.us - t.progress_us >= 250000);
    t.progress_us = t.us;
    ++t.progress_calls;
}
static void snapshot(void *ctx, struct kui_storage_errors *out) {
    (void)ctx; *out = t.errors; ++t.errors_captured;
}
static void inject_error(void) {
    t.injected = true;
    t.errors.total = 1;
    t.errors.read_errors = 1;
    t.errors.crc_errors = 1;
    t.errors.last_operation = KUI_STORAGE_ERROR_READ;
    t.errors.last_result = KUI_STORAGE_ERROR_CRC;
    t.errors.last_lba = 12345;
    t.errors.last_count = 7;
    t.errors.sd_detail_valid = true;
    t.errors.sd_command = 18;
}

FRESULT __real_f_open(FIL *, const TCHAR *, BYTE);
FRESULT __wrap_f_open(FIL *file, const TCHAR *path, BYTE mode) {
    bool scratch = t.armed && strstr(path, "/scratch.bin");
    if(scratch && is("collision") && !t.injected) {
        FIL existing; UINT done;
        assert(__real_f_open(&existing, path, FA_WRITE | FA_CREATE_NEW) == FR_OK);
        assert(f_write(&existing, "KEEP", 4, &done) == FR_OK && done == 4);
        assert(f_close(&existing) == FR_OK);
        t.injected = true;
    }
    FRESULT status = __real_f_open(file, path, mode);
    if(scratch) {
        snprintf(t.scratch_path, sizeof(t.scratch_path), "%s", path);
        if(status == FR_OK) {
            t.scratch = file;
            t.writing = (mode & FA_WRITE) != 0;
            if(t.writing) ++t.write_opens; else ++t.read_opens;
            if(!t.writing && is("size") && !t.injected) {
                --file->obj.objsize;
                inject_error();
            }
        }
    }
    return status;
}
FRESULT __real_f_write(FIL *, const void *, UINT, UINT *);
FRESULT __wrap_f_write(FIL *file, const void *data, UINT count, UINT *done) {
    if(!t.armed || file != t.scratch) return __real_f_write(file, data, count, done);
    ++t.writes;
    if(!t.injected && t.writes == 3 && (is("write") || is("short-write"))) {
        inject_error();
        if(is("write")) { *done = 0; return FR_DISK_ERR; }
        return __real_f_write(file, data, count / 2, done);
    }
    t.in_io = true;
    FRESULT status = __real_f_write(file, data, count, done);
    t.in_io = false;
    /* A late stall proves calls beyond the old 512-entry recorder are retained. */
    if(t.writes == 600) t.us += 200000;
    return status;
}
FRESULT __real_f_read(FIL *, void *, UINT, UINT *);
FRESULT __wrap_f_read(FIL *file, void *data, UINT count, UINT *done) {
    if(!t.armed || file != t.scratch) return __real_f_read(file, data, count, done);
    ++t.reads;
    if(!t.injected && t.reads == 3 && (is("read") || is("short-read"))) {
        inject_error();
        if(is("read")) { *done = 0; return FR_DISK_ERR; }
        return __real_f_read(file, data, count / 2, done);
    }
    t.in_io = true;
    FRESULT status = __real_f_read(file, data, count, done);
    t.in_io = false;
    if(status == FR_OK && *done == count) {
        if(is("corrupt") && !t.injected && t.reads == 3) {
            ((uint8_t *)data)[137] ^= 1;
            inject_error();
        }
        if(is("stale")) {
            assert(count <= sizeof(t.first_cycle));
            if(t.read_opens == 1 && !t.captured_first_cycle) {
                memcpy(t.first_cycle, data, count);
                t.captured_first_cycle = true;
            } else if(t.read_opens == 2 && !t.injected) {
                memcpy(data, t.first_cycle, count);
                inject_error();
            }
        }
    }
    return status;
}
FRESULT __real_f_sync(FIL *);
FRESULT __wrap_f_sync(FIL *file) {
    if(t.armed && file == t.scratch && is("sync") && !t.injected) {
        inject_error(); return FR_DISK_ERR;
    }
    return __real_f_sync(file);
}
FRESULT __real_f_close(FIL *);
FRESULT __wrap_f_close(FIL *file) {
    if(t.armed && file == t.scratch) {
        if(!t.injected && ((t.writing && is("close-write")) ||
                          (!t.writing && is("close-read")))) {
            inject_error(); return FR_DISK_ERR;
        }
        /* Cleanup changes the backend detail; result must retain first failure. */
        if(t.injected) t.errors.last_lba = 99999;
        FRESULT status = __real_f_close(file);
        if(status == FR_OK) t.scratch = NULL;
        return status;
    }
    return __real_f_close(file);
}
FRESULT __real_f_mount(FATFS *, const TCHAR *, BYTE);
FRESULT __wrap_f_mount(FATFS *fs, const TCHAR *path, BYTE option) {
    if(t.armed) {
        if(fs) {
            ++t.mounts;
            if(!t.injected && ((is("mount") && t.mounts == 2) ||
                              (is("remount") && t.mounts == 3))) {
                inject_error(); return FR_DISK_ERR;
            }
        } else {
            ++t.unmounts;
            if(is("unmount") && !t.injected && t.writes && !t.reads) {
                inject_error(); return FR_INT_ERR;
            }
        }
    }
    FRESULT status = __real_f_mount(fs, path, option);
    /* FatFs registers the pointer even if the immediate mount fails. */
    t.registered = fs;
    return status;
}
FRESULT __real_f_unlink(const TCHAR *);
FRESULT __wrap_f_unlink(const TCHAR *path) {
    if(t.armed && strstr(path, "/scratch.bin") && is("unlink") && !t.injected) {
        inject_error(); return FR_DENIED;
    }
    return __real_f_unlink(path);
}
FRESULT __real_f_getfree(const TCHAR *, DWORD *, FATFS **);
FRESULT __wrap_f_getfree(const TCHAR *path, DWORD *free_clusters, FATFS **fs) {
    if(t.armed && t.mounts >= 2 && is("free-error") && !t.injected) {
        inject_error(); return FR_DISK_ERR;
    }
    FRESULT status = __real_f_getfree(path, free_clusters, fs);
    if(t.armed && t.mounts >= 2 && is("full") && status == FR_OK) {
        *free_clusters = 1;
        t.injected = true;
    }
    return status;
}

static void check_model(void) {
    struct kui_storage_test_request request;
    kui_storage_test_defaults(&request);
    assert(request.preset == KUI_STORAGE_TEST_QUICK && request.repeats == 1 &&
           request.soak_minutes == 15 && kui_storage_test_request_valid(&request));
    request.repeats = 2; assert(!kui_storage_test_request_valid(&request));
    request.repeats = 3;
    memset(request.card_label, 'a', sizeof(request.card_label));
    assert(!kui_storage_test_request_valid(&request));
    request.card_label[2] = '\n'; request.card_label[3] = 0;
    assert(!kui_storage_test_request_valid(&request));
    struct kui_storage_test_result a = {.outcome = KUI_STORAGE_TEST_PASSED};
    kui_storage_test_defaults(&a.request);
    a.metadata.cluster_bytes = 32768;
    snprintf(a.metadata.filesystem, sizeof(a.metadata.filesystem), "exFAT");
    struct kui_storage_test_result b = a;
    b.metadata.transport = 1;
    strcpy(b.metadata.build, "other"); strcpy(b.request.card_label, "other card");
    assert(kui_storage_test_comparable(&a, &b));
    b.metadata.cluster_bytes *= 2; assert(!kui_storage_test_comparable(&a, &b));
    b = a; b.metadata.music_playing = true; assert(!kui_storage_test_comparable(&a, &b));
    b = a; b.metadata.ui_hz = 2; assert(!kui_storage_test_comparable(&a, &b));
    b = a; b.request.repeats = 5; assert(!kui_storage_test_comparable(&a, &b));
    b = a; b.request.soak_minutes = 60; assert(kui_storage_test_comparable(&a, &b));
    a.request.preset = b.request.preset = KUI_STORAGE_TEST_SOAK;
    assert(!kui_storage_test_comparable(&a, &b));
    b = a; b.request.repeats = 5; assert(kui_storage_test_comparable(&a, &b));
    b = a; b.outcome = KUI_STORAGE_TEST_STOPPED; assert(!kui_storage_test_comparable(&a, &b));
    a.sample_count = 5;
    const uint64_t durations[] = {1000000, 500000, 2000000, 250000, 9000000};
    for(unsigned i = 0; i < 5; ++i)
        a.samples[i] = (struct kui_storage_test_sample){.chunk_bytes = 65536,
            .bytes = 1048576, .write_us = durations[i], .read_us = durations[i], .verified = i < 3};
    assert(kui_storage_test_rate(&a, 65536, true) == 1024);
    assert(kui_storage_test_rate(&a, 16384, false) == 0);
    a.samples[2].verified = false;
    assert(kui_storage_test_rate(&a, 65536, false) == 1536);
    a.samples[3].verified = true; a.samples[3].bytes = UINT64_MAX;
    assert(kui_storage_test_rate(&a, 65536, false) == 1536);
}

static void preserved_file(bool create) {
    FATFS fs; FIL file; UINT done; char data[4];
    assert(kui_mount(&fs, log_line));
    assert(f_open(&file, "0:/user-file.bin", create ? FA_WRITE | FA_CREATE_NEW : FA_READ) == FR_OK);
    if(create) assert(f_write(&file, "USER", 4, &done) == FR_OK && done == 4);
    else assert(f_read(&file, data, 4, &done) == FR_OK && done == 4 && !memcmp(data, "USER", 4));
    assert(f_close(&file) == FR_OK && f_mount(NULL, "0:", 0) == FR_OK);
}

int main(int argc, char **argv) {
    assert(argc == 3);
    struct stat st;
    assert(!lstat(argv[1], &st) && S_ISREG(st.st_mode) && st.st_size % 512 == 0);
    t.image = fopen(argv[1], "r+b"); assert(t.image);
    t.blocks = (uint64_t)st.st_size / 512; t.test = argv[2];
    struct kui_media_ops media = {.blocks = blocks, .read = read_image,
        .write = write_image, .sync = sync_image};
    kui_media_set(&media);
    check_model();
    preserved_file(true);
    struct kui_storage_test_request request;
    kui_storage_test_defaults(&request);
    if(is("compare")) { request.preset = KUI_STORAGE_TEST_COMPARE; request.repeats = 3; }
    if(is("compare-five")) { request.preset = KUI_STORAGE_TEST_COMPARE; request.repeats = 5; }
    if(is("soak")) { request.preset = KUI_STORAGE_TEST_SOAK; request.soak_minutes = 5; }
    if(is("stale")) request.repeats = 3;
    if(is("invalid")) request.repeats = 2;
    strcpy(request.card_label, "Test card");
    struct kui_storage_test_metadata metadata = {.transport = 1, .ui_hz = 2};
    strcpy(metadata.build, "test-build");
    struct kui_storage_test_ops ops = {.now_us = now_us, .cancelled = cancelled,
        .progress = progress, .errors = snapshot, .log = log_line};
    struct kui_storage_test_result result;
    t.armed = true;
    unsigned writes_before = t.volume_writes;
    kui_storage_test_run(&request, &metadata, &ops, &result);
    assert(!t.registered && !t.scratch);
    assert(result.metadata.transport == 1 && !result.saved);
    bool passing = is("quick") || is("compare") || is("compare-five") || is("soak");
    bool cancel = is("cancel-before") || is("cancel-write") || is("cancel-read");
    assert(result.outcome == (passing ? KUI_STORAGE_TEST_PASSED :
                              cancel ? KUI_STORAGE_TEST_STOPPED : KUI_STORAGE_TEST_FAILED));
    if(is("cancel-before") || is("invalid")) {
        assert(!result.id && t.volume_writes == writes_before);
        assert(!t.writes && !t.reads);
    } else {
        assert(result.id && result.path[0] && t.progress_calls);
        assert(result.elapsed_us);
    }
    if(passing) {
        assert(!result.cleanup_failed && result.written_bytes == result.verified_bytes);
        assert(result.write_latency.calls == t.writes && result.read_latency.calls == t.reads);
        assert(result.write_latency.total_us && result.read_latency.total_us);
        assert(result.write_latency.min_us <= result.write_latency.max_us);
        assert(result.write_latency.p95_upper_us >= result.write_latency.min_us);
        assert(result.metadata.free_bytes && result.metadata.cluster_bytes && result.metadata.filesystem[0]);
        if(is("quick")) {
            assert(result.cycles == 1 && result.sample_count == 1 && result.verified_bytes == 4u * 1048576u);
            assert(t.writes == 64 && t.reads == 64 && t.mounts >= 3);
        }
        if(is("compare") || is("compare-five")) {
            assert(result.cycles == request.repeats * 3 && result.sample_count == request.repeats * 3);
            assert(t.writes == 1344 * request.repeats && t.reads == 1344 * request.repeats);
            assert(result.write_latency.max_us >= 200000 && result.write_latency.over_100ms);
            assert(kui_storage_test_rate(&result, 16384, true));
            assert(kui_storage_test_rate(&result, 65536, false));
            assert(kui_storage_test_rate(&result, 262144, false));
        }
        if(is("soak")) {
            assert(result.sample_count == 1 && result.cycles >= 2);
            assert(result.elapsed_us >= 5u * 60000000u);
            assert(result.samples[0].bytes == result.verified_bytes);
            assert(result.write_latency.calls > 512);
        }
        for(unsigned i = 0; i < result.sample_count; ++i) assert(result.samples[i].verified);
    } else if(!cancel && !is("invalid") && !is("collision") && !is("full")) {
        assert(t.injected && result.failure_phase[0]);
        assert(result.errors.last_lba == 12345); /* before cleanup overwrote it */
    }
    if(is("corrupt")) assert(!strcmp(result.failure_phase, "verify") && result.failure_offset == 2u * 65536u + 137u);
    if(is("stale")) assert(result.cycles == 1 && !strcmp(result.failure_phase, "verify"));
    if(is("short-write")) assert(!strcmp(result.failure_phase, "write") && result.failure_offset == 2u * 65536u + 32768u);
    if(is("short-read")) assert(!strcmp(result.failure_phase, "read") && result.failure_offset == 2u * 65536u + 32768u);
    assert(result.cleanup_failed == is("unlink"));
    t.armed = false;
    if(result.id) {
        FATFS fs; FILINFO info;
        assert(kui_mount(&fs, log_line));
        char path[112]; snprintf(path, sizeof(path), "%s/scratch.bin", result.path);
        FRESULT status = f_stat(path, &info);
        if(is("unlink") || is("collision")) {
            assert(status == FR_OK);
            if(is("collision")) {
                FIL file; UINT done; char keep[4];
                assert(f_open(&file, path, FA_READ) == FR_OK && f_size(&file) == 4);
                assert(f_read(&file, keep, 4, &done) == FR_OK && done == 4 && !memcmp(keep, "KEEP", 4));
                assert(f_close(&file) == FR_OK);
            }
            assert(f_unlink(path) == FR_OK);
        } else assert(status == FR_NO_FILE);
        assert(f_mount(NULL, "0:", 0) == FR_OK);
        assert(kui_storage_test_save(&result, log_line));
        assert(result.saved);
    }
    preserved_file(false);
    assert(!t.registered);
    assert(!fclose(t.image));
    printf("PASS storage-test %s\n", t.test);
    return 0;
}
