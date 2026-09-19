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
/* "uihz=4 if=scif crc=on"; prefixed to every SD line so a swept report is unambiguous. */
static char transport[48];
/* "uihz=full": the UI setting of the pass every line below it belongs to. */
static char ui_tag[16];

static bool cancelled(void) { return ops->cancelled(ops->ctx); }
static uint64_t now(void) { return ops->now_us(ops->ctx); }
/* Busy-wait, deliberately not a sleep: it stands in for the CPU being busy
 * elsewhere (SD write, hashing) while the drive waits to be serviced. */
static void spin_us(uint64_t us) {
    uint64_t end = now() + us;
    while(now() < end) { }
}

/* One line per measurement, always the same columns, so two reports can be
 * compared side by side. kib_s carries one decimal. */
static void result(const char *name, const char *detail, uint64_t bytes, uint64_t us) {
    uint64_t tenths = us ? bytes * 10000000ull / (us * 1024ull) : 0;
    ops->log("BENCH %s %s bytes=%" PRIu64 " us=%" PRIu64 " kib_s=%" PRIu64 ".%" PRIu64,
        name, detail, bytes, us, tenths / 10, tenths % 10);
}

/* --- CPU census ------------------------------------------------------------ */

/* The single most important line this file can produce. Every rate here is
 * bytes over wall-clock time, and the UI thread shares the one CPU with the
 * worker: any time it runs during a measurement is time the measured stage did
 * not have. ui_pct is the share of the interval the UI thread was on the CPU. */
void kui_cpu_census_log(kui_log_fn log, const char *what, const char *detail,
                        const struct kui_cpu_census *from, const struct kui_cpu_census *to) {
    uint64_t wall = (to->wall_us - from->wall_us) / 1000;
    uint64_t wk = to->worker_ms - from->worker_ms, ui = to->ui_ms - from->ui_ms;
    uint64_t total = to->total_ms - from->total_ms;
    uint64_t other = total > wk + ui ? total - wk - ui : 0;   /* idle, reaper, kernel */
    uint64_t pct = wall ? ui * 1000 / wall : 0;
    log("BENCH cpu %s %s wall=%" PRIu64 " wk=%" PRIu64 " ui=%" PRIu64 " oth=%" PRIu64
        " ui_pct=%" PRIu64 ".%" PRIu64, what, detail ? detail : "", wall, wk, ui, other,
        pct / 10, pct % 10);
}
static struct kui_cpu_census cpu_from;
static void cpu_begin(void) { if(ops->cpu_mark) ops->cpu_mark(ops->ctx, &cpu_from); }
static void cpu_end(const char *what, const char *detail) {
    struct kui_cpu_census to;
    if(!ops->cpu_mark) return;
    ops->cpu_mark(ops->ctx, &to);
    kui_cpu_census_log(ops->log, what, detail, &cpu_from, &to);
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
    uint64_t done = 0;
    cpu_begin();
    uint64_t start = now();
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
    snprintf(detail, sizeof(detail), "%s fad=%" PRIu32 " sectors=%u retries=%u",
        ui_tag, o->optical_fad, o->optical_sectors, retries);
    result("optical", detail, done, us);
    cpu_end("optical", ui_tag);
    return KUI_BENCH_COMPLETE;
}

/* --- hash ----------------------------------------------------------------- */

/* Both hashes are data-independent in time, so any bytes will do. The digest
 * and CRC are logged so the compiler cannot discard the work. cyc_b is
 * cycles per byte on the 200 MHz SH4: the number to compare implementations.
 * It is wall-clock cycles, so it includes whatever the UI thread took; the
 * census line after each result says how much that was. */
static enum kui_bench_result bench_hash(unsigned mib) {
    uint64_t target = (uint64_t)mib * MIB, done = 0;
    struct kui_sha256 sha;
    uint8_t digest[32];
    char hex[65], detail[48];
    kui_sha256_init(&sha);
    cpu_begin();
    uint64_t start = now();
    while(done < target) {
        kui_sha256_update(&sha, buffer, sizeof(buffer));
        done += sizeof(buffer);
        if(cancelled()) return KUI_BENCH_STOPPED;
    }
    kui_sha256_digest(&sha, digest);
    uint64_t us = now() - start, cyc_b = done ? SH4_HZ * us / 1000000ull / done : 0;
    snprintf(detail, sizeof(detail), "%s sha256 cyc_b=%" PRIu64, ui_tag, cyc_b);
    result("hash", detail, done, us);
    cpu_end("sha256", ui_tag);

    uint32_t crc = 0;
    done = 0;
    cpu_begin();
    start = now();
    while(done < target) {
        crc = kui_crc32(crc, buffer, sizeof(buffer));
        done += sizeof(buffer);
        if(cancelled()) return KUI_BENCH_STOPPED;
    }
    us = now() - start; cyc_b = done ? SH4_HZ * us / 1000000ull / done : 0;
    snprintf(detail, sizeof(detail), "%s crc32 cyc_b=%" PRIu64, ui_tag, cyc_b);
    result("hash", detail, done, us);
    cpu_end("crc32", ui_tag);
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

/* Per-call latency of every f_write / f_read in one run. A card that stalls
 * (garbage collection, erase) shows as a p95 or max far above the median even
 * when the average looks fine, and the size of those stalls is what decides
 * how deep a write-behind buffer would need to be. */
#define LAT_MAX 512u
static uint32_t lat_us[LAT_MAX];
static unsigned lat_n, lat_dropped;
static void lat_add(uint64_t us) {
    if(lat_n < LAT_MAX) lat_us[lat_n++] = us > 0xffffffffull ? 0xffffffffu : (uint32_t)us;
    else ++lat_dropped;
}
static void lat_report(const char *what, const char *brief) {
    static uint32_t sorted[LAT_MAX];
    if(!lat_n) return;
    memcpy(sorted, lat_us, lat_n * sizeof(sorted[0]));
    for(unsigned i = 1; i < lat_n; ++i) {   /* insertion sort: n <= 512, once per run */
        uint32_t v = sorted[i]; unsigned j = i;
        while(j && sorted[j - 1] > v) { sorted[j] = sorted[j - 1]; --j; }
        sorted[j] = v;
    }
    unsigned p95i = lat_n * 95 / 100;
    if(p95i >= lat_n) p95i = lat_n - 1;
    uint32_t p50 = sorted[lat_n / 2];
    unsigned slow = 0; uint64_t excess = 0;
    for(unsigned i = 0; i < lat_n; ++i)
        if(lat_us[i] > 2 * (uint64_t)p50) { ++slow; excess += lat_us[i] - p50; }
    ops->log("BENCH lat %s %s n=%u min=%u p50=%u p95=%u max=%u slow=%u slow_ms=%" PRIu64,
        what, brief, lat_n, (unsigned)sorted[0], (unsigned)p50, (unsigned)sorted[p95i],
        (unsigned)sorted[lat_n - 1], slow, excess / 1000);
    if(lat_dropped) ops->log("BENCH lat %s: %u further calls not sampled", what, lat_dropped);
}

/* One SD run: write total bytes in `bytes`-sized f_write calls, sync, close,
 * read it all back in the same size, delete. Write, sync and read are timed
 * separately because they answer different questions. With expand, the file
 * is preallocated contiguously first (exFAT then skips the FAT chain on every
 * later access), which is the test for fragmentation cost. `label` is either
 * "chunk=N" (N raw sectors) or "wbytes=N" (N bytes: the alignment test, since
 * FatFs splits every write at the 128 KiB cluster boundary). */
static enum kui_bench_result bench_sd_run(UINT bytes, const char *label, bool expand, uint64_t total) {
    char detail[96], brief[40];
    snprintf(detail, sizeof(detail), "%s %s expand=%u", transport, label, expand ? 1 : 0);
    snprintf(brief, sizeof(brief), "%s e=%u", label, expand ? 1 : 0);
    FIL file;
    FRESULT r = f_open(&file, SCRATCH, FA_WRITE | FA_CREATE_ALWAYS);
    if(r != FR_OK) { ops->log("Bench scratch open failed: FatFs=%u", (unsigned)r); return KUI_BENCH_FAILED; }
    enum kui_bench_result rc = KUI_BENCH_FAILED;
    uint64_t start, t0, us, done = 0;
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
    lat_n = 0; lat_dropped = 0;
    cpu_begin();
    start = now();
    while(done < total) {
        UINT written = 0;
        t0 = now();
        r = f_write(&file, buffer, bytes, &written);
        lat_add(now() - t0);
        if(r != FR_OK || written != bytes) {
            ops->log("Bench write failed: FatFs=%u bytes=%u/%u", (unsigned)r, written, bytes);
            goto out;
        }
        done += bytes;
        if(cancelled()) { rc = KUI_BENCH_STOPPED; goto out; }
    }
    us = now() - start;
    result("sd write", detail, done, us);
    cpu_end("sdw", brief);
    lat_report("write", brief);
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
    done = 0; lat_n = 0; lat_dropped = 0;
    cpu_begin();
    start = now();
    while(done < total) {
        UINT got = 0;
        t0 = now();
        r = f_read(&file, buffer, bytes, &got);
        lat_add(now() - t0);
        if(r != FR_OK || got != bytes) {
            ops->log("Bench read failed: FatFs=%u bytes=%u/%u", (unsigned)r, got, bytes);
            goto out;
        }
        done += bytes;
        if(cancelled()) { rc = KUI_BENCH_STOPPED; goto out; }
    }
    us = now() - start;
    result("sd read", detail, done, us);
    cpu_end("sdr", brief);
    lat_report("read", brief);
    rc = KUI_BENCH_COMPLETE;
out:
    f_close(&file);
    f_unlink(SCRATCH);
    return rc;
}

/* Every chunk/expand combination, then every explicit byte size, on one
 * already-mounted link. */
static enum kui_bench_result bench_sd_chunks(const struct kui_options *o, uint64_t want) {
    if(!free_space_ok(want)) return KUI_BENCH_FAILED;
    if(!ensure_dir("0:/KUI") || !ensure_dir(SCRATCH_DIR)) return KUI_BENCH_FAILED;
    char label[24];
    for(unsigned i = 0; i < o->chunk_count; ++i) {
        unsigned chunk = o->chunks[i];
        uint64_t bytes = (uint64_t)chunk * KUI_RAW_BYTES;
        uint64_t total = want / bytes * bytes;   /* whole chunks only */
        if(total < bytes) total = bytes;
        snprintf(label, sizeof(label), "chunk=%u", chunk);
        for(unsigned e = 0; e <= (o->expand ? 1u : 0u); ++e) {
            enum kui_bench_result rc = bench_sd_run((UINT)bytes, label, e == 1, total);
            if(rc != KUI_BENCH_COMPLETE) return rc;
        }
    }
    for(unsigned i = 0; i < o->sd_bytes_count; ++i) {
        uint64_t bytes = o->sd_bytes[i];
        uint64_t total = want / bytes * bytes;
        if(total < bytes) total = bytes;
        snprintf(label, sizeof(label), "wbytes=%u", o->sd_bytes[i]);
        for(unsigned e = 0; e <= (o->expand ? 1u : 0u); ++e) {
            enum kui_bench_result rc = bench_sd_run((UINT)bytes, label, e == 1, total);
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
            snprintf(transport, sizeof(transport), "%s if=%s crc=%s",
                ui_tag, got ? "sci" : "scif", crc ? "on" : "off");
            last = bench_sd_chunks(o, want);
            f_mount(NULL, "0:", 0);
            if(last == KUI_BENCH_STOPPED) return last;
        }
    }
    return last;
}

/* --- optical sweep -------------------------------------------------------- */

/* Reads `sectors` starting at `fad` in `chunk`-sector commands, untimed, and
 * returns their CRC32. Reading the same range with different command sizes
 * must give identical bytes; if it does not, a faster size is not usable.
 * Meaningful on data tracks only: raw audio may legitimately differ per read. */
static bool sweep_crc(unsigned fad, unsigned chunk, unsigned sectors, uint32_t *crc_out) {
    struct kui_probe_stats st;
    uint32_t crc = 0;
    memset(&st, 0, sizeof(st));
    while(sectors) {
        unsigned n = sectors > chunk ? chunk : sectors;
        const uint8_t *data = NULL;
        if(ops->read_probe(ops->ctx, fad, n, 0, &data, &st) != KUI_READ_OK || !data) return false;
        crc = kui_crc32(crc, data, (size_t)n * KUI_RAW_BYTES);
        fad += n; sectors -= n;
        if(cancelled()) return false;
    }
    *crc_out = crc;
    return true;
}

static void poll_lines(const char *brief, const struct kui_probe_stats *st) {
    ops->log("BENCH poll %s n=%" PRIu64 "/%" PRIu64 "/%" PRIu64 "/%" PRIu64 "/%" PRIu64, brief,
        st->poll_n[0], st->poll_n[1], st->poll_n[2], st->poll_n[3], st->poll_n[4]);
    ops->log("BENCH poll %s ms=%" PRIu64 "/%" PRIu64 "/%" PRIu64 "/%" PRIu64 "/%" PRIu64, brief,
        st->poll_us[0] / 1000, st->poll_us[1] / 1000, st->poll_us[2] / 1000,
        st->poll_us[3] / 1000, st->poll_us[4] / 1000);
}

/* One (fad, chunk, gap, service) point. Returns COMPLETE for a point that had
 * to be skipped (its reason is logged) so one unreadable range cannot end a
 * long sweep; STOPPED and FAILED end it. */
static enum kui_bench_result sweep_point(unsigned fad, unsigned chunk, unsigned gap,
                                         unsigned svc, unsigned sectors) {
    struct kui_probe_stats st;
    const uint8_t *data = NULL;
    memset(&st, 0, sizeof(st));
    /* Discarded: seek, spin-up and the drive's first-command overhead. */
    enum kui_read_result r = ops->read_probe(ops->ctx, fad, chunk, 0, &data, &st);
    if(r == KUI_READ_FATAL) {
        ops->log("BENCH sweep warm-up failed fad=%u chunk=%u", fad, chunk);
        return KUI_BENCH_FAILED;
    }
    if(r != KUI_READ_OK) {
        ops->log("BENCH sweep skipped fad=%u chunk=%u: warm-up read failed", fad, chunk);
        return KUI_BENCH_COMPLETE;
    }
    memset(&st, 0, sizeof(st));
    unsigned remaining = sectors, cur = fad, retries = 0;
    uint64_t bytes = 0, reads = 0, min_us = UINT64_MAX, max_us = 0;
    cpu_begin();
    uint64_t start = now();
    while(remaining) {
        unsigned n = remaining > chunk ? chunk : remaining;
        r = ops->read_probe(ops->ctx, cur, n, svc, &data, &st);
        if(r == KUI_READ_OK) {
            cur += n; remaining -= n; bytes += (uint64_t)n * KUI_RAW_BYTES; ++reads;
            if(st.last_us < min_us) min_us = st.last_us;
            if(st.last_us > max_us) max_us = st.last_us;
        } else if(r == KUI_READ_RETRY && retries < 2) {
            ++retries;
        } else {
            ops->log("BENCH sweep skipped fad=%u chunk=%u: read failed at %u", fad, chunk, cur);
            return r == KUI_READ_FATAL ? KUI_BENCH_FAILED : KUI_BENCH_COMPLETE;
        }
        if(cancelled()) return KUI_BENCH_STOPPED;
        if(gap && remaining) spin_us(gap);
    }
    uint64_t wall = now() - start;
    uint64_t rd = st.cmd_us ? bytes * 10000000ull / (st.cmd_us * 1024ull) : 0;   /* KiB/s x10 */
    char detail[112], brief[48];
    snprintf(detail, sizeof(detail),
        "%s fad=%u chunk=%u gap=%u svc=%u reads=%" PRIu64 " retry=%u rd=%" PRIu64 ".%" PRIu64
        " min=%" PRIu64 " max=%" PRIu64, ui_tag, fad, chunk, gap, svc, reads, retries,
        rd / 10, rd % 10, reads ? min_us : 0, max_us);
    result("sweep", detail, bytes, wall);
    snprintf(brief, sizeof(brief), "c=%u g=%u s=%u", chunk, gap, svc);
    cpu_end("sweep", brief);
    /* Poll buckets only where they mean something: back-to-back commands with
     * the normal policy. With a gap or a service spin they measure the spin. */
    if(!gap && !svc) {
        snprintf(brief, sizeof(brief), "fad=%u c=%u", fad, chunk);
        poll_lines(brief, &st);
    }
    return KUI_BENCH_COMPLETE;
}

static enum kui_bench_result bench_sweep(const struct kui_options *o) {
    if(!ops->read_probe) {
        ops->log("BENCH sweep skipped: no readable disc");
        return KUI_BENCH_COMPLETE;
    }
    ops->log("BENCH poll buckets: call duration <25us / <100us / <400us / <1.6ms / more");
    unsigned nf = o->sweep_fad_count ? o->sweep_fad_count : 1;
    unsigned ng = o->sweep_gap_count ? o->sweep_gap_count : 1;
    unsigned ns = o->sweep_service_count ? o->sweep_service_count : 1;
    unsigned vsec = o->sweep_sectors < 512 ? o->sweep_sectors : 512;
    for(unsigned f = 0; f < nf; ++f) {
        unsigned fad = o->sweep_fad_count ? o->sweep_fads[f] : o->optical_fad;
        uint32_t ref = 0;
        bool have_ref = false;
        if(o->sweep_verify) {
            have_ref = sweep_crc(fad, KUI_CAPTURE_CHUNK, vsec, &ref);
            if(!have_ref && cancelled()) return KUI_BENCH_STOPPED;
            if(!have_ref) ops->log("BENCH verify fad=%u: reference read failed; skipped", fad);
        }
        for(unsigned c = 0; c < o->sweep_chunk_count; ++c) {
            unsigned chunk = o->sweep_chunks[c];
            for(unsigned g = 0; g < ng; ++g) {
                for(unsigned s = 0; s < ns; ++s) {
                    enum kui_bench_result rc = sweep_point(fad, chunk,
                        o->sweep_gap_count ? o->sweep_gap_us[g] : 0,
                        o->sweep_service_count ? o->sweep_service_us[s] : 0, o->sweep_sectors);
                    if(rc != KUI_BENCH_COMPLETE) return rc;
                }
            }
            if(have_ref) {
                uint32_t crc = 0;
                if(sweep_crc(fad, chunk, vsec, &crc))
                    ops->log("BENCH verify fad=%u chunk=%u sectors=%u crc32=%08" PRIx32 " ref=%08" PRIx32 " %s",
                        fad, chunk, vsec, crc, ref, crc == ref ? "OK" : "MISMATCH");
                else if(cancelled()) return KUI_BENCH_STOPPED;
                else ops->log("BENCH verify fad=%u chunk=%u: read failed", fad, chunk);
            }
        }
    }
    return KUI_BENCH_COMPLETE;
}

/* --- capture engine ------------------------------------------------------- */

/* A bench job is one track and two checkpoints (nothing is published), so its few
 * files are collected first and removed after: deleting while iterating a FAT
 * directory can skip entries. Leaves the card as it found it. */
static void delete_job(const char *dir) {
    char names[16][24];
    unsigned n = 0;
    FATFS fs;
    DIR d;
    FILINFO info;
    if(!dir[0] || !kui_mount(&fs, ops->log)) return;
    if(f_opendir(&d, dir) == FR_OK) {
        while(n < 16 && f_readdir(&d, &info) == FR_OK && info.fname[0])
            if(!(info.fattrib & AM_DIR) && strlen(info.fname) < sizeof(names[0])) strcpy(names[n++], info.fname);
        f_closedir(&d);
        for(unsigned i = 0; i < n; ++i) {
            char path[128];
            snprintf(path, sizeof(path), "%.90s/%.23s", dir, names[i]);   /* 90 + 1 + 23 < 128, provably */
            f_unlink(path);
        }
        f_unlink(dir);
    }
    f_mount(NULL, "0:", 0);
}
static const char *capture_text(enum kui_capture_result r) {
    return r == KUI_CAPTURE_COMPLETE ? "ok" : r == KUI_CAPTURE_STOPPED ? "stopped" : "FAILED";
}
static uint64_t tenths_of(uint64_t bytes, uint64_t us) { return us ? bytes * 10000000ull / (us * 1024ull) : 0; }

/* The real capture engine, on the real disc, at every combination of the
 * capture_* lists: what each choice actually costs in a capture, not just in
 * isolation. Per point: a NEW run (capture phase and end read-back timed
 * separately, plus the split by stage), then a RESUME run per resume_check to
 * time the prefix check on the job just made, then the job is deleted. A run's
 * setup (three sample reads) is outside every rate. */
static enum kui_bench_result bench_capture(const struct kui_options *o) {
    if(!ops->capture_run) {
        ops->log("BENCH capture skipped: no readable disc");
        return KUI_BENCH_COMPLETE;
    }
    FATFS fs;
    bool room;
    if(!kui_mount(&fs, ops->log)) return KUI_BENCH_FAILED;
    room = free_space_ok((uint64_t)o->capture_sectors * KUI_RAW_BYTES) && ensure_dir("0:/KUI");
    f_mount(NULL, "0:", 0);
    if(!room) return KUI_BENCH_FAILED;
    unsigned fad = o->capture_fad ? o->capture_fad : o->optical_fad;
    for(unsigned h = 0; h < o->capture_hash_count; ++h)
    for(unsigned e = 0; e < o->end_readback_count; ++e)
    for(unsigned s = 0; s < o->sample_readback_count; ++s) {
        struct kui_capture_options co;
        struct kui_capture_stats st;
        char brief[64], detail[112];
        memset(&co, 0, sizeof(co));
        memset(&st, 0, sizeof(st));
        co.crc_only = o->capture_crc_only[h];
        co.skip_end_readback = !o->end_readback[e];
        co.sample_every = o->sample_readback[s];
        snprintf(brief, sizeof(brief), "hash=%s end=%s sample=%u", co.crc_only ? "crc32" : "both",
            o->end_readback[e] ? "on" : "off", co.sample_every);
        cpu_begin();
        enum kui_capture_result r = ops->capture_run(ops->ctx, fad, o->capture_sectors,
            o->capture_audio, KUI_CAPTURE_NEW, &co, &st);
        cpu_end("capture", brief);
        if(r != KUI_CAPTURE_COMPLETE) {
            ops->log("BENCH capture %s %s result=%s", ui_tag, brief, capture_text(r));
            delete_job(st.job_dir);
            return r == KUI_CAPTURE_STOPPED ? KUI_BENCH_STOPPED : KUI_BENCH_FAILED;
        }
        uint64_t cap = st.phase_us[KUI_TIME_CAPTURE], ver = st.phase_us[KUI_TIME_VERIFY];
        const uint64_t *b = st.capture_bucket_us;
        snprintf(detail, sizeof(detail), "%s %s sectors=%u result=ok", ui_tag, brief, o->capture_sectors);
        result("capture", detail, st.bytes, cap);   /* the capture phase alone */
        uint64_t all = tenths_of(st.bytes, cap + ver);
        ops->log("BENCH capture total %s verify_us=%" PRIu64 " total_kib_s=%" PRIu64 ".%" PRIu64
            " sampled=%u verified=%d", brief, ver, all / 10, all % 10, (unsigned)st.sampled, st.verified ? 1 : 0);
        ops->log("BENCH capture parts %s ms disc=%" PRIu64 " edc=%" PRIu64 " write=%" PRIu64 " read=%" PRIu64
            " sha=%" PRIu64 " crc=%" PRIu64 " ckpt=%" PRIu64, brief,
            b[KUI_TIME_DISC] / 1000, b[KUI_TIME_EDC] / 1000, b[KUI_TIME_WRITE] / 1000, b[KUI_TIME_READ] / 1000,
            b[KUI_TIME_SHA256] / 1000, b[KUI_TIME_CRC32] / 1000, b[KUI_TIME_CHECKPOINT] / 1000);
        for(unsigned c = 0; c < o->resume_check_count; ++c) {
            struct kui_capture_options ro = co;
            struct kui_capture_stats rs;
            memset(&rs, 0, sizeof(rs));
            if(o->resume_size[c] && !st.crc_only) {
                ops->log("BENCH resume %s hash=both check=size skipped: a SHA-256 job re-reads to rebuild it", ui_tag);
                continue;
            }
            ro.resume_size_only = o->resume_size[c];
            enum kui_capture_result rr = ops->capture_run(ops->ctx, fad, o->capture_sectors,
                o->capture_audio, KUI_CAPTURE_RESUME, &ro, &rs);
            snprintf(detail, sizeof(detail), "%s hash=%s check=%s sectors=%u result=%s", ui_tag,
                st.crc_only ? "crc32" : "both", o->resume_size[c] ? "size" : "full", o->capture_sectors, capture_text(rr));
            result("resume", detail, rs.bytes, rs.phase_us[KUI_TIME_RESUME]);
            if(rr != KUI_CAPTURE_COMPLETE) { delete_job(st.job_dir); return rr == KUI_CAPTURE_STOPPED ? KUI_BENCH_STOPPED : KUI_BENCH_FAILED; }
        }
        delete_job(st.job_dir);
        if(cancelled()) return KUI_BENCH_STOPPED;
    }
    return KUI_BENCH_COMPLETE;
}

/* --- entry ---------------------------------------------------------------- */

enum kui_bench_result kui_bench(const struct kui_bench_ops *o, const struct kui_options *opt) {
    if(!o || !o->cancelled || !o->now_us || !o->log || !opt || !opt->chunk_count ||
       !opt->sd_if_count || !opt->sd_crc_count || !opt->ui_count)
        return KUI_BENCH_FAILED;
    ops = o;
    kui_pattern(buffer, 0, sizeof(buffer));
    ops->log("BENCH: isolated measurements; each line is one component alone");
    snprintf(transport, sizeof(transport), "if=? crc=?");
    enum kui_bench_result rc = KUI_BENCH_COMPLETE;
    /* One full pass of the selected sections per UI setting. The UI thread
     * shares the CPU with everything measured here, so the setting is part of
     * every result; the tag says which pass a line belongs to. */
    for(unsigned u = 0; u < opt->ui_count && rc == KUI_BENCH_COMPLETE; ++u) {
        unsigned hz = opt->ui_hz[u];
        if(o->set_ui) o->set_ui(o->ctx, hz);
        if(hz == KUI_OPT_UI_FULL) snprintf(ui_tag, sizeof(ui_tag), "uihz=full");
        else snprintf(ui_tag, sizeof(ui_tag), "uihz=%u", hz);
        ops->log("BENCH pass %u/%u %s%s", u + 1, opt->ui_count, ui_tag,
            o->set_ui ? "" : " (no UI control on this platform)");
        /* Optical and hash touch no filesystem, so they run before any mount and
         * are unaffected by the transport sweep that follows. */
        if(rc == KUI_BENCH_COMPLETE && (opt->sections & KUI_SEC_OPTICAL)) rc = bench_optical(opt);
        if(rc == KUI_BENCH_COMPLETE && (opt->sections & KUI_SEC_HASH)) rc = bench_hash(opt->hash_mib);
        if(rc == KUI_BENCH_COMPLETE && (opt->sections & KUI_SEC_SD)) rc = bench_sd(opt);
        if(rc == KUI_BENCH_COMPLETE && (opt->sections & KUI_SEC_SWEEP) && opt->sweep_chunk_count)
            rc = bench_sweep(opt);
        if(rc == KUI_BENCH_COMPLETE && (opt->sections & KUI_SEC_CAPTURE)) rc = bench_capture(opt);
    }
    if(o->set_ui) o->set_ui(o->ctx, KUI_OPT_UI_FULL);   /* the report save that follows is not a measurement */
    if(rc == KUI_BENCH_STOPPED) ops->log("BENCH stopped; lines already printed are valid");
    ops->log("BENCH %s", rc == KUI_BENCH_COMPLETE ? "complete" : rc == KUI_BENCH_STOPPED ? "stopped" : "FAILED");
    return rc;
}
