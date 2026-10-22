/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/storage_test.h"
#include "kui/media.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define MIB UINT64_C(1048576)
#define MAX_CHUNK (256u * 1024u)
#define LATENCY_BUCKETS 33u

/* One filesystem worker owns this buffer. Pattern work is deliberately outside
 * timed FatFs calls, including the full comparison after each read. */
static uint32_t buffer[MAX_CHUNK / sizeof(uint32_t)] __attribute__((aligned(32)));
struct test_run {
    const struct kui_storage_test_ops *ops;
    struct kui_storage_test_result *result;
    struct kui_storage_test_progress progress;
    uint64_t started, progress_at;
    bool progress_sent, captured_errors;
    uint64_t histogram[2][LATENCY_BUCKETS];
};

static uint64_t now(const struct test_run *run) {
    return run->ops->now_us(run->ops->ctx);
}
static void capture_errors(struct test_run *run) {
    if(!run->captured_errors && run->ops->errors) {
        run->ops->errors(run->ops->ctx, &run->result->errors);
        run->captured_errors = true;
    }
}
static void failure(struct test_run *run, const char *phase, FRESULT error,
                    uint64_t offset, const char *message) {
    /* Preserve the initiating failure, before close/unlink can change the
     * backend's last-error detail. Cleanup failures are recorded separately. */
    if(run->result->outcome == KUI_STORAGE_TEST_FAILED) return;
    capture_errors(run);
    run->result->outcome = KUI_STORAGE_TEST_FAILED;
    run->result->fatfs_error = (unsigned)error;
    run->result->failure_offset = offset;
    snprintf(run->result->failure_phase, sizeof(run->result->failure_phase), "%s", phase);
    snprintf(run->result->message, sizeof(run->result->message), "%s", message);
    if(run->ops->log) run->ops->log("STORAGE TEST %s at byte=%" PRIu64 ": %s; FatFs=%u",
                                   phase, offset, message, (unsigned)error);
}
static bool stopped(struct test_run *run) {
    if(!run->ops->cancelled || !run->ops->cancelled(run->ops->ctx)) return false;
    run->result->outcome = KUI_STORAGE_TEST_STOPPED;
    snprintf(run->result->message, sizeof(run->result->message), "Stopped by user");
    return true;
}
static void progress(struct test_run *run, const char *phase, uint64_t done,
                     uint64_t total) {
    if(!run->ops->progress) return;
    uint64_t stamp = now(run);
    if(run->progress_sent && stamp - run->progress_at < UINT64_C(250000)) return;
    run->progress_sent = true;
    run->progress_at = stamp;
    run->progress.done = done;
    run->progress.total = total;
    run->progress.elapsed_us = stamp - run->started;
    snprintf(run->progress.phase, sizeof(run->progress.phase), "%s", phase);
    run->ops->progress(run->ops->ctx, &run->progress);
}
static uint32_t pattern_word(uint64_t word, uint64_t cycle) {
    uint32_t value = (uint32_t)word ^ (uint32_t)(word >> 32) ^
        UINT32_C(0x4b554931) ^ (uint32_t)cycle * UINT32_C(0x9e3779b9) ^
        (uint32_t)(cycle >> 32) * UINT32_C(0x85ebca6b);
    value ^= value << 13; value ^= value >> 17; value ^= value << 5;
    return value;
}
static void fill_pattern(uint64_t offset, UINT bytes, uint64_t cycle) {
    for(UINT i = 0; i < bytes / 4; ++i) buffer[i] = pattern_word(offset / 4 + i, cycle);
}
static bool verify_pattern(uint64_t offset, UINT bytes, uint64_t cycle,
                            uint64_t *bad_offset) {
    for(UINT i = 0; i < bytes / 4; ++i) {
        uint32_t expected = pattern_word(offset / 4 + i, cycle);
        if(buffer[i] != expected) {
            const uint8_t *actual_bytes = (const uint8_t *)&buffer[i];
            const uint8_t *expected_bytes = (const uint8_t *)&expected;
            unsigned byte = 0;
            while(byte < 4 && actual_bytes[byte] == expected_bytes[byte]) ++byte;
            *bad_offset = offset + (uint64_t)i * 4 + byte;
            return false;
        }
    }
    return true;
}
static void latency_add(struct test_run *run, bool read, uint64_t us) {
    struct kui_storage_test_latency *latency = read ? &run->result->read_latency :
                                                      &run->result->write_latency;
    uint32_t bounded = us > UINT32_MAX ? UINT32_MAX : (uint32_t)us;
    if(!latency->calls || bounded < latency->min_us) latency->min_us = bounded;
    if(bounded > latency->max_us) latency->max_us = bounded;
    ++latency->calls;
    latency->total_us += us;
    if(us > 20000) ++latency->over_20ms;
    if(us > 100000) ++latency->over_100ms;
    unsigned bucket = 0;
    for(uint32_t value = bounded; value; value >>= 1) ++bucket;
    ++run->histogram[read ? 1 : 0][bucket];
}
static void latency_finish(struct test_run *run, bool read) {
    struct kui_storage_test_latency *latency = read ? &run->result->read_latency :
                                                      &run->result->write_latency;
    uint64_t threshold = latency->calls / 100 * 95 +
        (latency->calls % 100 * 95 + 99) / 100;
    uint64_t count = 0;
    if(!threshold) return;
    for(unsigned i = 0; i < LATENCY_BUCKETS; ++i) {
        count += run->histogram[read ? 1 : 0][i];
        if(count >= threshold) {
            latency->p95_upper_us = i == 32 ? UINT32_MAX : (UINT32_C(1) << i) - 1;
            return;
        }
    }
}
static bool mount_volume(struct test_run *run, FATFS *fs, const char *phase) {
    FRESULT status = f_mount(fs, "0:", 1);
    if(status != FR_OK) {
        failure(run, phase, status, 0, "Cannot mount the selected device");
        return false;
    }
    if(fs->fs_type != FS_FAT32 && fs->fs_type != FS_EXFAT) {
        failure(run, phase, FR_NO_FILESYSTEM, 0, "Storage tests require FAT32 or exFAT");
        return false;
    }
    return true;
}
static void cleanup_failure(struct test_run *run, const char *phase, FRESULT status) {
    run->result->cleanup_failed = true;
    failure(run, phase, status, 0, "Scratch cleanup did not complete");
    if(run->ops->log) run->ops->log("STORAGE TEST cleanup %s: FatFs=%u", phase, (unsigned)status);
}

void kui_storage_test_run(const struct kui_storage_test_request *request,
    const struct kui_storage_test_metadata *metadata,
    const struct kui_storage_test_ops *ops, struct kui_storage_test_result *result) {
    if(!result) return;
    /* Copies permit a caller to repeat result.request in place. */
    struct kui_storage_test_request copied_request = {0};
    struct kui_storage_test_metadata copied_metadata = {0};
    if(request) copied_request = *request;
    if(metadata) copied_metadata = *metadata;
    memset(result, 0, sizeof(*result));
    result->request = copied_request;
    result->metadata = copied_metadata;
    if(!ops || !ops->now_us || !request || !metadata ||
       !kui_storage_test_request_valid(&copied_request)) {
        result->outcome = KUI_STORAGE_TEST_FAILED;
        snprintf(result->failure_phase, sizeof(result->failure_phase), "request");
        snprintf(result->message, sizeof(result->message), "Invalid storage test request");
        return;
    }
    struct test_run run = {.ops = ops, .result = result};
    run.started = now(&run);
    result->outcome = KUI_STORAGE_TEST_RUNNING;
    run.progress.preset = copied_request.preset;
    run.progress.repeats = copied_request.repeats;
    bool soak = copied_request.preset == KUI_STORAGE_TEST_SOAK;
    unsigned sizes = copied_request.preset == KUI_STORAGE_TEST_COMPARE ? 3 : 1;
    unsigned planned_cycles = copied_request.repeats * sizes;
    uint64_t total = copied_request.preset == KUI_STORAGE_TEST_QUICK ? 4 * MIB : 16 * MIB;
    uint64_t target = soak ? copied_request.soak_minutes * UINT64_C(60000000) : 0;
    run.progress.target_us = target;
    run.progress.steps = soak ? 2 : planned_cycles * 2;
    FATFS fs;
    FIL file;
    bool opened = false, owned = false, mounted = false;
    char scratch[KUI_STORAGE_TEST_PATH + 16] = {0};
    FRESULT status;
    uint64_t offset = 0;
    if(stopped(&run)) goto out;
    progress(&run, "Preparing", 0, total);
    if(!kui_storage_test_begin(result, ops->log)) {
        capture_errors(&run);
        if(result->outcome != KUI_STORAGE_TEST_FAILED)
            failure(&run, "save-start", FR_DISK_ERR, 0, "Cannot create a test result directory");
        goto out;
    }
    if(stopped(&run)) goto out;
    if(snprintf(scratch, sizeof(scratch), "%s/scratch.bin", result->path) >= (int)sizeof(scratch)) {
        failure(&run, "scratch-path", FR_INVALID_NAME, 0, "Test directory path is too long");
        goto out;
    }
    if(!mount_volume(&run, &fs, "mount")) goto out;
    mounted = true;
    FATFS *volume = NULL;
    DWORD free_clusters = 0;
    status = f_getfree("0:", &free_clusters, &volume);
    if(status != FR_OK || !volume) {
        failure(&run, "free-space", status == FR_OK ? FR_INT_ERR : status, 0,
                "Cannot determine free space");
        goto out;
    }
    const struct kui_volume *layout = kui_media_volume();
    result->metadata.volume_start = layout->start;
    result->metadata.volume_sectors = layout->count;
    result->metadata.cluster_bytes = (uint32_t)volume->csize * 512u;
    result->metadata.free_bytes = (uint64_t)free_clusters * result->metadata.cluster_bytes;
    snprintf(result->metadata.filesystem, sizeof(result->metadata.filesystem), "%s",
             volume->fs_type == FS_EXFAT ? "exFAT" : "FAT32");
    /* Retain room for this run's tiny result/manifest files and normal metadata. */
    if(result->metadata.free_bytes < total + MIB) {
        failure(&run, "free-space", FR_DENIED, 0, "Not enough free space (test size plus 1 MiB required)");
        goto out;
    }
    for(uint64_t cycle = 0; soak || cycle < planned_cycles; ++cycle) {
        if(stopped(&run)) goto out;
        if(soak && result->cycles && now(&run) - run.started >= target) break;
        unsigned size_index = (unsigned)(cycle % sizes);
        uint32_t chunk = sizes == 3 ? (16u * 1024u << (size_index * 2)) : 64u * 1024u;
        struct kui_storage_test_sample current = {
            .chunk_bytes = chunk, .repeat = (unsigned)(cycle / sizes) + 1};
        unsigned sample_index = soak ? 0 : (unsigned)cycle;
        if(sample_index >= KUI_STORAGE_TEST_SAMPLES) {
            failure(&run, "sample-count", FR_INT_ERR, 0, "Test sample limit exceeded");
            goto out;
        }
        run.progress.repeat = current.repeat;
        run.progress.step = soak ? 1 : (unsigned)cycle * 2 + 1;
        offset = 0;
        progress(&run, "Writing", 0, total);
        /* Never replace a pre-existing name. Only this invocation's successful
         * CREATE_NEW authorizes subsequent truncation of the same scratch file. */
        status = f_open(&file, scratch, FA_WRITE | (owned ? FA_CREATE_ALWAYS : FA_CREATE_NEW));
        if(status != FR_OK) {
            failure(&run, "open-write", status, 0, "Cannot create scratch file");
            goto out;
        }
        owned = opened = true;
        while(offset < total) {
            if(stopped(&run)) goto out;
            UINT bytes = total - offset > chunk ? chunk : (UINT)(total - offset);
            fill_pattern(offset, bytes, cycle + 1);
            UINT done = 0;
            uint64_t start = now(&run);
            status = f_write(&file, buffer, bytes, &done);
            uint64_t elapsed = now(&run) - start;
            latency_add(&run, false, elapsed);
            current.write_us += elapsed;
            result->written_bytes += done;
            if(status != FR_OK || done != bytes) {
                failure(&run, "write", status, offset + done,
                        status == FR_OK ? "Short scratch write" : "Scratch write failed");
                goto out;
            }
            offset += bytes;
            progress(&run, "Writing", offset, total);
        }
        progress(&run, "Flushing", offset, total);
        uint64_t sync_start = now(&run);
        status = f_sync(&file);
        current.sync_us += now(&run) - sync_start;
        if(status != FR_OK) {
            failure(&run, "sync", status, offset, "Scratch flush failed");
            goto out;
        }
        status = f_close(&file);
        if(status != FR_OK) {
            failure(&run, "close-write", status, offset, "Scratch close failed");
            goto out;
        }
        opened = false;
        status = f_mount(NULL, "0:", 0);
        if(status != FR_OK) {
            failure(&run, "unmount", status, offset, "Cannot drop filesystem caches");
            goto out;
        }
        mounted = false;
        if(stopped(&run)) goto out;
        if(!mount_volume(&run, &fs, "remount")) goto out;
        mounted = true;
        status = f_open(&file, scratch, FA_READ);
        if(status != FR_OK) {
            failure(&run, "open-read", status, 0, "Cannot reopen scratch file");
            goto out;
        }
        opened = true;
        if(f_size(&file) != total) {
            failure(&run, "read-size", FR_OK, 0, "Scratch size changed after remount");
            goto out;
        }
        run.progress.step = soak ? 2 : (unsigned)cycle * 2 + 2;
        offset = 0;
        progress(&run, "Verifying", offset, total);
        while(offset < total) {
            if(stopped(&run)) goto out;
            UINT bytes = total - offset > chunk ? chunk : (UINT)(total - offset);
            UINT done = 0;
            uint64_t start = now(&run);
            status = f_read(&file, buffer, bytes, &done);
            uint64_t elapsed = now(&run) - start;
            latency_add(&run, true, elapsed);
            current.read_us += elapsed;
            if(status != FR_OK || done != bytes) {
                failure(&run, "read", status, offset + done,
                        status == FR_OK ? "Short scratch read" : "Scratch read failed");
                goto out;
            }
            uint64_t bad_offset;
            if(!verify_pattern(offset, bytes, cycle + 1, &bad_offset)) {
                failure(&run, "verify", FR_OK, bad_offset, "Scratch contents did not match");
                goto out;
            }
            offset += bytes;
            result->verified_bytes += bytes;
            progress(&run, "Verifying", offset, total);
        }
        status = f_close(&file);
        if(status != FR_OK) {
            failure(&run, "close-read", status, offset, "Verified scratch close failed");
            goto out;
        }
        opened = false;
        current.bytes = total;
        current.verified = true;
        if(soak) {
            struct kui_storage_test_sample *sample = &result->samples[0];
            sample->chunk_bytes = chunk;
            sample->repeat = 1;
            sample->bytes += total;
            sample->write_us += current.write_us;
            sample->read_us += current.read_us;
            sample->sync_us += current.sync_us;
            sample->verified = true;
        } else result->samples[sample_index] = current;
        result->sample_count = sample_index + 1;
        ++result->cycles;
    }
    if(stopped(&run)) goto out;
    result->outcome = KUI_STORAGE_TEST_PASSED;
    snprintf(result->message, sizeof(result->message), "All written data verified after remount");
out:
    if(opened) {
        status = f_close(&file);
        if(status != FR_OK) cleanup_failure(&run, "cleanup-close", status);
    }
    if(owned) {
        if(!mounted) {
            status = f_mount(&fs, "0:", 1);
            if(status == FR_OK) mounted = true;
            else cleanup_failure(&run, "cleanup-mount", status);
        }
        if(mounted) {
            status = f_unlink(scratch);
            if(status != FR_OK && status != FR_NO_FILE) cleanup_failure(&run, "cleanup-unlink", status);
        }
    }
    /* Even a failed mount registers its stack FATFS; always clear the pointer. */
    status = f_mount(NULL, "0:", 0);
    if(status != FR_OK) cleanup_failure(&run, "cleanup-unmount", status);
    capture_errors(&run);
    latency_finish(&run, false);
    latency_finish(&run, true);
    result->elapsed_us = now(&run) - run.started;
    if(ops->log) ops->log("STORAGE TEST %s: cycles=%" PRIu64 " written=%" PRIu64
        " verified=%" PRIu64 " cleanup=%s", kui_storage_test_outcome_name(result->outcome),
        result->cycles, result->written_bytes, result->verified_bytes,
        result->cleanup_failed ? "FAILED" : "OK");
}
