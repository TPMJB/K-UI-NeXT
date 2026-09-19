/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/bench.h"
#include "kui/hash.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define SCRATCH_DIR "0:/KUI/bench"
#define SCRATCH SCRATCH_DIR "/scratch.bin"
#define MIB (1024ull * 1024ull)
#define SH4_HZ 200000000ull   /* Dreamcast CPU clock, for cycles-per-byte */

/* One buffer for every bench, sized for the largest SD chunk so nothing here
 * allocates. It is ~1.2 MB of the 15+ MB the runtime leaves free. */
static uint8_t buffer[KUI_OPT_CHUNK_MAX * KUI_RAW_BYTES];
static const struct kui_bench_ops *ops;
/* "if=scif crc=on"; prefixed to every SD line so a swept report is unambiguous. */
static char transport[24];

static bool cancelled(void) { return ops->cancelled(ops->ctx); }
static uint64_t now(void) { return ops->now_us(ops->ctx); }

/* One line per measurement, always the same columns, so two reports can be
 * compared side by side. kib_s carries one decimal. */
static void result(const char *name, const char *detail, uint64_t bytes, uint64_t us) {
    uint64_t tenths = us ? bytes * 10000000ull / (us * 1024ull) : 0;
    ops->log("BENCH %s %s bytes=%" PRIu64 " us=%" PRIu64 " kib_s=%" PRIu64 ".%" PRIu64,
        name, detail, bytes, us, tenths / 10, tenths % 10);
}

/* --- optical -------------------------------------------------------------- */

static enum kui_bench_result bench_optical(const struct kui_options *o) {
    if(!ops->read) {
        ops->log("BENCH optical skipped: no readable disc");
        return KUI_BENCH_COMPLETE;
    }
    uint32_t fad = o->optical_fad;
    unsigned remaining = o->optical_sectors, retries = 0;
    /* The first read after INIT includes spin-up and the seek to the range.
     * It is issued and discarded so the timed loop measures steady reading. */
    enum kui_read_result r = ops->read(ops->ctx, fad, KUI_CAPTURE_CHUNK, buffer);
    if(r == KUI_READ_FATAL) {
        ops->log("BENCH optical warm-up read failed at fad=%" PRIu32, fad);
        return KUI_BENCH_FAILED;
    }
    uint64_t done = 0, start = now();
    while(remaining) {
        unsigned n = remaining > KUI_CAPTURE_CHUNK ? KUI_CAPTURE_CHUNK : remaining;
        r = ops->read(ops->ctx, fad, n, buffer);
        if(r == KUI_READ_OK) {
            fad += n; remaining -= n; done += (uint64_t)n * KUI_RAW_BYTES;
        } else if(r == KUI_READ_RETRY && retries < KUI_CAPTURE_RETRIES) {
            ++retries;
        } else {
            ops->log("BENCH optical read failed at fad=%" PRIu32 " after %u retries", fad, retries);
            return KUI_BENCH_FAILED;
        }
        if(cancelled()) return KUI_BENCH_STOPPED;
    }
    uint64_t us = now() - start;
    char detail[64];
    snprintf(detail, sizeof(detail), "fad=%" PRIu32 " sectors=%u retries=%u",
        o->optical_fad, o->optical_sectors, retries);
    result("optical", detail, done, us);
    return KUI_BENCH_COMPLETE;
}

/* --- hash ----------------------------------------------------------------- */

/* Both hashes are data-independent in time, so any bytes will do. The digest
 * and CRC are logged so the compiler cannot discard the work. cyc_b is
 * cycles per byte on the 200 MHz SH4: the number to compare implementations. */
static enum kui_bench_result bench_hash(unsigned mib) {
    uint64_t target = (uint64_t)mib * MIB, done = 0;
    struct kui_sha256 sha;
    uint8_t digest[32];
    char hex[65], detail[48];
    kui_sha256_init(&sha);
    uint64_t start = now();
    while(done < target) {
        kui_sha256_update(&sha, buffer, sizeof(buffer));
        done += sizeof(buffer);
        if(cancelled()) return KUI_BENCH_STOPPED;
    }
    kui_sha256_digest(&sha, digest);
    uint64_t us = now() - start, cyc_b = done ? SH4_HZ * us / 1000000ull / done : 0;
    snprintf(detail, sizeof(detail), "sha256 cyc_b=%" PRIu64, cyc_b);
    result("hash", detail, done, us);

    uint32_t crc = 0;
    done = 0; start = now();
    while(done < target) {
        crc = kui_crc32(crc, buffer, sizeof(buffer));
        done += sizeof(buffer);
        if(cancelled()) return KUI_BENCH_STOPPED;
    }
    us = now() - start; cyc_b = done ? SH4_HZ * us / 1000000ull / done : 0;
    snprintf(detail, sizeof(detail), "crc32 cyc_b=%" PRIu64, cyc_b);
    result("hash", detail, done, us);
    kui_hex(digest, 8, hex);
    ops->log("bench hash sha256=%s... crc32=%08" PRIx32, hex, crc);
    return KUI_BENCH_COMPLETE;
}

/* --- SD ------------------------------------------------------------------- */

static bool free_space_ok(uint64_t needed) {
    DWORD clusters; FATFS *fs;
    FRESULT r = f_getfree("0:", &clusters, &fs);
    if(r != FR_OK) { ops->log("Free-space check failed: FatFs=%u", (unsigned)r); return false; }
    uint64_t free_bytes = (uint64_t)clusters * fs->csize * 512ull;
    if(free_bytes < needed + 4 * MIB) {
        ops->log("Not enough free space for bench: need %" PRIu64 " MiB", (needed + 4 * MIB) / MIB);
        return false;
    }
    return true;
}
static bool ensure_dir(const char *path) {
    FRESULT r = f_mkdir(path);
    if(r != FR_OK && r != FR_EXIST) { ops->log("Cannot create %s: FatFs=%u", path + 2, (unsigned)r); return false; }
    return true;
}

/* One SD run: write total bytes in chunk-sized f_write calls, sync, close,
 * read it all back in the same chunk size, delete. Write, sync and read are
 * timed separately because they answer different questions. With expand,
 * the file is preallocated contiguously first (exFAT then skips the FAT
 * chain on every later access), which is the test for fragmentation cost. */
static enum kui_bench_result bench_sd_run(unsigned chunk, bool expand, uint64_t total) {
    UINT bytes = chunk * KUI_RAW_BYTES;
    char detail[64];
    snprintf(detail, sizeof(detail), "%s chunk=%u expand=%u", transport, chunk, expand ? 1 : 0);
    FIL file;
    FRESULT r = f_open(&file, SCRATCH, FA_WRITE | FA_CREATE_ALWAYS);
    if(r != FR_OK) { ops->log("Bench scratch open failed: FatFs=%u", (unsigned)r); return KUI_BENCH_FAILED; }
    enum kui_bench_result rc = KUI_BENCH_FAILED;
    uint64_t start, us, done = 0;
    if(expand) {
#if FF_USE_EXPAND
        start = now();
        r = f_expand(&file, (FSIZE_t)total, 1);
        us = now() - start;
        if(r != FR_OK) {
            ops->log("f_expand failed: FatFs=%u (needs contiguous free space)", (unsigned)r);
            goto out;
        }
        ops->log("BENCH sd expand %s us=%" PRIu64, detail, us);
#else
        ops->log("f_expand not compiled in (FF_USE_EXPAND=0); expand run skipped");
        rc = KUI_BENCH_COMPLETE;
        goto out;
#endif
    }
    start = now();
    while(done < total) {
        UINT written = 0;
        r = f_write(&file, buffer, bytes, &written);
        if(r != FR_OK || written != bytes) {
            ops->log("Bench write failed: FatFs=%u bytes=%u/%u", (unsigned)r, written, bytes);
            goto out;
        }
        done += bytes;
        if(cancelled()) { rc = KUI_BENCH_STOPPED; goto out; }
    }
    us = now() - start;
    result("sd write", detail, done, us);
    start = now();
    r = f_sync(&file);
    us = now() - start;
    if(r != FR_OK) { ops->log("Bench sync failed: FatFs=%u", (unsigned)r); goto out; }
    ops->log("BENCH sd sync %s us=%" PRIu64, detail, us);
    f_close(&file);

    r = f_open(&file, SCRATCH, FA_READ);
    if(r != FR_OK) {
        ops->log("Bench scratch reopen failed: FatFs=%u", (unsigned)r);
        f_unlink(SCRATCH);
        return KUI_BENCH_FAILED;
    }
    done = 0; start = now();
    while(done < total) {
        UINT got = 0;
        r = f_read(&file, buffer, bytes, &got);
        if(r != FR_OK || got != bytes) {
            ops->log("Bench read failed: FatFs=%u bytes=%u/%u", (unsigned)r, got, bytes);
            goto out;
        }
        done += bytes;
        if(cancelled()) { rc = KUI_BENCH_STOPPED; goto out; }
    }
    us = now() - start;
    result("sd read", detail, done, us);
    rc = KUI_BENCH_COMPLETE;
out:
    f_close(&file);
    f_unlink(SCRATCH);
    return rc;
}

/* Every chunk/expand combination on one already-mounted link. */
static enum kui_bench_result bench_sd_chunks(const struct kui_options *o, uint64_t want) {
    if(!free_space_ok(want)) return KUI_BENCH_FAILED;
    if(!ensure_dir("0:/KUI") || !ensure_dir(SCRATCH_DIR)) return KUI_BENCH_FAILED;
    for(unsigned i = 0; i < o->chunk_count; ++i) {
        unsigned chunk = o->chunks[i];
        uint64_t bytes = (uint64_t)chunk * KUI_RAW_BYTES;
        uint64_t total = want / bytes * bytes;   /* whole chunks only */
        if(total < bytes) total = bytes;
        for(unsigned e = 0; e <= (o->expand ? 1u : 0u); ++e) {
            enum kui_bench_result rc = bench_sd_run(chunk, e == 1, total);
            if(rc != KUI_BENCH_COMPLETE) return rc;
        }
    }
    return KUI_BENCH_COMPLETE;
}

/* Sweeps transport x CRC around that, remounting between each, so comparing
 * transports costs one boot instead of one card removal per setting. */
static enum kui_bench_result bench_sd(const struct kui_options *o) {
    uint64_t want = (uint64_t)o->sd_mib * MIB;
    unsigned done_mask = 0;   /* actual (interface, crc) pairs already measured */
    enum kui_bench_result last = KUI_BENCH_FAILED;
    for(unsigned a = 0; a < o->sd_if_count; ++a) {
        for(unsigned b = 0; b < o->sd_crc_count; ++b) {
            unsigned want_if = o->sd_if[a];
            bool crc = o->sd_crc[b];
            int got = (int)want_if;
            if(ops->reconnect) {
                got = ops->reconnect(ops->ctx, want_if, crc);
                if(got < 0) {
                    ops->log("BENCH sd if=%s crc=%s skipped: card did not come back",
                        want_if ? "sci" : "scif", crc ? "on" : "off");
                    continue;
                }
                if((unsigned)got != want_if)
                    /* kui_sd_connect fell back. Running anyway would file the
                     * other transport's numbers under this one's name. */
                    ops->log("BENCH sd if=%s skipped: fell back to %s",
                        want_if ? "sci" : "scif", got ? "sci" : "scif");
            }
            unsigned bit = 1u << ((unsigned)got * 2 + (crc ? 1u : 0u));
            if(done_mask & bit) continue;      /* same actual pair, already measured */
            done_mask |= bit;
            FATFS fs;
            if(!kui_mount(&fs, ops->log)) { last = KUI_BENCH_FAILED; continue; }
            ops->log("BENCH sd transport if=%s crc=%s", got ? "sci" : "scif", crc ? "on" : "off");
            snprintf(transport, sizeof(transport), "if=%s crc=%s",
                got ? "sci" : "scif", crc ? "on" : "off");
            last = bench_sd_chunks(o, want);
            f_mount(NULL, "0:", 0);
            if(last == KUI_BENCH_STOPPED) return last;
        }
    }
    return last;
}

/* --- entry ---------------------------------------------------------------- */

enum kui_bench_result kui_bench(const struct kui_bench_ops *o, const struct kui_options *opt) {
    if(!o || !o->cancelled || !o->now_us || !o->log || !opt || !opt->chunk_count ||
       !opt->sd_if_count || !opt->sd_crc_count)
        return KUI_BENCH_FAILED;
    ops = o;
    kui_pattern(buffer, 0, sizeof(buffer));
    ops->log("BENCH: isolated measurements; each line is one component alone");
    snprintf(transport, sizeof(transport), "if=? crc=?");
    /* Optical and hash touch no filesystem, so they run before any mount and
     * are unaffected by the transport sweep that follows. */
    enum kui_bench_result rc = bench_optical(opt);
    if(rc == KUI_BENCH_COMPLETE) rc = bench_hash(opt->hash_mib);
    if(rc == KUI_BENCH_COMPLETE) rc = bench_sd(opt);
    if(rc == KUI_BENCH_STOPPED) ops->log("BENCH stopped; lines already printed are valid");
    ops->log("BENCH %s", rc == KUI_BENCH_COMPLETE ? "complete" : rc == KUI_BENCH_STOPPED ? "stopped" : "FAILED");
    return rc;
}
