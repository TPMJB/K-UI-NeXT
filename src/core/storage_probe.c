/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/probe.h"
#include "kui/media.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static uint8_t buffer[32768], expected[32768];

bool kui_mount(FATFS *fs, kui_log_fn log) {
    FRESULT r = f_mount(fs, "0:", 1);
    if(r != FR_OK) {
        log("Mount failed: FatFs=%u", (unsigned)r);
        const char *problem = kui_media_problem();
        if(problem) log("%s", problem);
        else log("Requires a valid FAT32 or exFAT filesystem");
        return false;
    }
    if(fs->fs_type != FS_FAT32 && fs->fs_type != FS_EXFAT) {
        log("Unsupported filesystem: FAT32 or exFAT required");
        f_mount(NULL, "0:", 0);
        return false;
    }
    const struct kui_volume *v = kui_media_volume();
    log("SD %s, %" PRIu32 " sectors, cluster=%lu bytes",
        fs->fs_type == FS_EXFAT ? "exFAT" : "FAT32", v->count,
        (unsigned long)fs->csize * 512);
    log("Volume start=%" PRIu32 " (%s)", v->start,
        v->partitioned ? "MBR" : "superfloppy");
    return true;
}

bool kui_new_probe_dir(char path[64], kui_log_fn log) {
    const char *parents[] = {"0:/KUI", "0:/KUI/probes"};
    for(unsigned i = 0; i < 2; ++i) {
        FRESULT r = f_mkdir(parents[i]);
        if(r != FR_OK && r != FR_EXIST) {
            log("Create directory failed: FatFs=%u", (unsigned)r); return false;
        }
    }
    for(unsigned i = 1; i <= 9999; ++i) {
        snprintf(path, 64, "0:/KUI/probes/p%04u", i);
        FRESULT r = f_mkdir(path);
        if(r == FR_OK) return true;
        if(r != FR_EXIST) {
            log("Create probe directory failed: FatFs=%u", (unsigned)r);
            return false;
        }
    }
    log("All 9999 probe directories already exist");
    return false;
}

bool kui_write_new_file(const char *path, const void *data, size_t size,
                         kui_log_fn log) {
    if(size > UINT32_MAX) return false;
    FIL file;
    FRESULT r = f_open(&file, path, FA_WRITE | FA_CREATE_NEW);
    if(r != FR_OK) { log("Create failed: FatFs=%u", (unsigned)r); return false; }
    UINT done = 0;
    r = f_write(&file, data, (UINT)size, &done);
    FRESULT sync = f_sync(&file), close = f_close(&file);
    if(r != FR_OK || done != size || sync != FR_OK || close != FR_OK) {
        log("Save failed: write=%u bytes=%u sync=%u close=%u",
            (unsigned)r, done, (unsigned)sync, (unsigned)close);
        return false;
    }
    return true;
}

bool kui_storage_probe(kui_log_fn log, kui_cancel_fn cancelled) {
    FATFS fs;
    FIL file;
    char dir[64], path[96];
    bool success = false, opened = false;
    if(!kui_mount(&fs, log)) { f_mount(NULL, "0:", 0); return false; }
    if(!kui_new_probe_dir(dir, log)) goto out;
    /* Cross at least two clusters, plus a partial sector, on either format. */
    uint64_t total = (uint64_t)fs.csize * 512 * 2;
    if(total < 2 * 1024 * 1024) total = 2 * 1024 * 1024;
    total += 173;
    log("Storage test: %" PRIu64 " bytes in %s", total, dir + 2);
    snprintf(path, sizeof(path), "%s/storage.bin", dir);
    FRESULT r = f_open(&file, path, FA_WRITE | FA_CREATE_NEW);
    if(r != FR_OK) { log("Create failed: FatFs=%u", (unsigned)r); goto out; }
    opened = true;
    uint32_t written_crc = 0, read_crc = 0;
    uint64_t offset = 0;
    while(offset < total && !cancelled()) {
        UINT n = total - offset > sizeof(buffer) ? sizeof(buffer) : (UINT)(total - offset);
        kui_pattern(buffer, offset, n);
        UINT done = 0;
        r = f_write(&file, buffer, n, &done);
        if(r != FR_OK || done != n) {
            log("Write failed at %" PRIu64 ": FatFs=%u bytes=%u/%u",
                offset, (unsigned)r, done, n); goto out;
        }
        written_crc = kui_crc32(written_crc, buffer, n);
        offset += n;
        if(offset % (1024 * 1024) == 0) log("Written %" PRIu64 " bytes", offset);
    }
    FRESULT sync = f_sync(&file), close = f_close(&file);
    opened = false;
    if(sync != FR_OK || close != FR_OK) {
        log("Flush failed: sync=%u close=%u", (unsigned)sync, (unsigned)close);
        goto out;
    }
    if(offset != total || cancelled()) {
        log("Stopped; partial storage.bin retained (%" PRIu64 " bytes)", offset);
        goto out;
    }
    /* Drop FatFs volume/file caches before verification. No host/device cache
     * is claimed to be bypassed; the SD backend uses synchronous SPI reads. */
    f_mount(NULL, "0:", 0);
    if(!kui_mount(&fs, log)) goto out;
    r = f_open(&file, path, FA_READ);
    if(r != FR_OK) { log("Reopen failed: FatFs=%u", (unsigned)r); goto out; }
    opened = true;
    if(f_size(&file) != total) { log("Reopened file has wrong size"); goto out; }
    offset = 0;
    while(offset < total && !cancelled()) {
        UINT n = total - offset > sizeof(buffer) ? sizeof(buffer) : (UINT)(total - offset);
        UINT done = 0;
        r = f_read(&file, buffer, n, &done);
        kui_pattern(expected, offset, n);
        if(r != FR_OK || done != n || memcmp(buffer, expected, n)) {
            log("Readback failed at %" PRIu64 ": FatFs=%u bytes=%u/%u",
                offset, (unsigned)r, done, n); goto out;
        }
        read_crc = kui_crc32(read_crc, buffer, n);
        offset += n;
        if(offset % (1024 * 1024) == 0) log("Verified %" PRIu64 " bytes", offset);
    }
    close = f_close(&file); opened = false;
    if(offset != total || close != FR_OK || cancelled()) {
        log("Verification stopped/failed; no completion manifest"); goto out;
    }
    if(read_crc != written_crc) { log("Capture/readback CRC mismatch"); goto out; }
    char manifest[320];
    int n = snprintf(manifest, sizeof(manifest),
        "{\n  \"schema\": 1,\n  \"complete\": true,\n"
        "  \"pattern\": \"kui-xorshift32-v1\",\n  \"bytes\": %" PRIu64 ",\n"
        "  \"crc32\": \"%08" PRIx32 "\"\n}\n", total, read_crc);
    if(n < 0 || (size_t)n >= sizeof(manifest)) goto out;
    snprintf(path, sizeof(path), "%s/storage.json", dir);
    if(!kui_write_new_file(path, manifest, (size_t)n, log)) goto out;
    log("STORAGE PASS: CRC32=%08" PRIx32, read_crc);
    log("Check storage.bin + storage.json on PC before acceptance");
    success = true;
out:
    if(opened) {
        FRESULT c = f_close(&file);
        if(c != FR_OK) log("Close after failure: FatFs=%u", (unsigned)c);
    }
    f_mount(NULL, "0:", 0);
    return success;
}
