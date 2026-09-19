/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "kui/bench.h"
#include "kui/media.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* A fake platform for kui_bench. The SD side is real FatFs on an image file, so
 * open/expand/write/sync/read/unlink actually run. The drive is synthetic: a
 * sector's bytes depend only on its absolute address, so ANY read size must
 * return identical bytes (which is exactly what the sweep's verify checks), and
 * time is a fake clock the fake reads advance, so every rate is deterministic.
 * The scheduler census gives the UI a fixed 25% so the arithmetic can be
 * asserted. Driven by tests/test_bench_images.py. */
static struct {
    FILE *image; uint64_t blocks, clock_us;
    unsigned ui_calls, ui_seq[16], probe_calls, read_calls;
    uint64_t cancel_after_us;
    unsigned corrupt_chunk, retry_fad;
    bool fatal_probe;
} t;

static void log_line(const char *format, ...) {
    va_list args; va_start(args, format); vprintf(format, args); va_end(args); puts("");
}
static uint64_t blocks(void *p) { (void)p; return t.blocks; }
static int read_image(void *p, uint32_t sector, size_t count, uint8_t *data) {
    (void)p; t.clock_us += 250 * count;   /* a card that takes time, so SD intervals have width */
    return fseeko(t.image, (off_t)sector * 512, SEEK_SET) || fread(data, 512, count, t.image) != count ? -1 : 0;
}
static int write_image(void *p, uint32_t sector, size_t count, const uint8_t *data) {
    (void)p; t.clock_us += 400 * count;
    return fseeko(t.image, (off_t)sector * 512, SEEK_SET) || fwrite(data, 512, count, t.image) != count ? -1 : 0;
}
static int sync_image(void *p) { (void)p; return fflush(t.image) || fsync(fileno(t.image)) ? -1 : 0; }

static bool cancelled(void *p) { (void)p; return t.cancel_after_us && t.clock_us >= t.cancel_after_us; }
static uint64_t now_us(void *p) { (void)p; return ++t.clock_us; }   /* spin loops must terminate */
static void sector_data(uint32_t fad, uint8_t *out) {
    for(unsigned i = 0; i < KUI_RAW_BYTES; ++i) out[i] = (uint8_t)(fad * 31u + i * 7u + (fad >> 8));
}
/* The capture-path read the baseline optical bench uses: ~1.5 MiB/s. */
static enum kui_read_result read_disc(void *p, uint32_t fad, unsigned sectors, uint8_t *out) {
    (void)p; ++t.read_calls; t.clock_us += 1000 + sectors * 1600u;
    for(unsigned i = 0; i < sectors; ++i) sector_data(fad + i, out + (size_t)i * KUI_RAW_BYTES);
    return KUI_READ_OK;
}
static uint8_t probe_buf[KUI_OPT_SWEEP_CHUNK_MAX * KUI_RAW_BYTES];
static enum kui_read_result read_probe(void *p, uint32_t fad, unsigned sectors, unsigned service_us,
                                       const uint8_t **data, struct kui_probe_stats *st) {
    (void)p; ++t.probe_calls;
    if(t.fatal_probe) return KUI_READ_FATAL;
    if(fad == t.retry_fad) return KUI_READ_RETRY;
    uint64_t cost = 800 + sectors * 1500u + service_us * 4u;   /* 4 polls per command */
    t.clock_us += cost; st->cmd_us += cost; st->last_us = cost;
    st->polls += 4; st->poll_n[0] += 3; st->poll_us[0] += 30; st->poll_n[2] += 1; st->poll_us[2] += 200;
    for(unsigned i = 0; i < sectors; ++i) sector_data(fad + i, probe_buf + (size_t)i * KUI_RAW_BYTES);
    if(t.corrupt_chunk && sectors == t.corrupt_chunk) probe_buf[100] ^= 1;   /* a size that returns wrong bytes */
    *data = probe_buf;
    return KUI_READ_OK;
}
static void set_ui(void *p, unsigned hz) { (void)p; if(t.ui_calls < 16) t.ui_seq[t.ui_calls] = hz; ++t.ui_calls; }
static void cpu_mark(void *p, struct kui_cpu_census *out) {
    (void)p;
    out->wall_us = t.clock_us; out->total_ms = t.clock_us / 1000;
    out->ui_ms = out->total_ms / 4; out->worker_ms = out->total_ms - out->ui_ms;   /* UI holds 25% */
}

/* The capture engine's view of the fake disc: real EDC-valid data sectors, and the
 * IP.BIN signature at FAD 45150 that identification insists on, so the REAL engine
 * (kui_capture_bench) runs unmodified against real FatFs. */
static void put32(uint8_t *p, uint32_t n) { for(unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(n >> (i * 8)); }
static void valid_sector(uint32_t fad, uint8_t *out) {
    kui_pattern(out, (uint64_t)fad * KUI_RAW_BYTES, KUI_RAW_BYTES);
    out[0] = out[11] = 0; memset(out + 1, 255, 10); out[12] = 0x10; out[13] = out[14] = 0; out[15] = 1;
    if(fad == 45150) {
        memcpy(out + 16, "SEGA SEGAKATANA", 14); memset(out + 16 + 128, ' ', 128);
        memcpy(out + 16 + 128, "KUI SYNTHETIC BENCH DISC", 24);
    }
    put32(out + 2064, kui_cd_edc(out, 2064));
}
static enum kui_read_result read_valid(void *p, uint32_t fad, unsigned sectors, uint8_t *out) {
    (void)p; t.clock_us += 1000 + sectors * 1600u;
    for(unsigned i = 0; i < sectors; ++i) valid_sector(fad + i, out + (size_t)i * KUI_RAW_BYTES);
    return KUI_READ_OK;
}
static void quiet_log(const char *format, ...) { (void)format; }
static uint64_t now_ms(void *p) { (void)p; return t.clock_us / 1000; }
static enum kui_capture_result capture_run(void *p, uint32_t fad, unsigned sectors, bool audio,
        enum kui_capture_mode mode, const struct kui_capture_options *opt, struct kui_capture_stats *st) {
    (void)p;
    struct kui_capture_ops cops = {NULL, read_valid, cancelled, now_ms, NULL, quiet_log, "0123456789ab",
                                   now_us, NULL, opt, st};
    return kui_capture_bench(&cops, fad, sectors, audio, mode);
}
/* Job directories still on the card: the bench must leave none behind. */
static unsigned jobs_left(void) {
    FATFS fs; DIR d; FILINFO info; unsigned n = 0;
    assert(kui_mount(&fs, quiet_log));
    if(f_opendir(&d, "0:/KUI/dumps") == FR_OK) {
        while(f_readdir(&d, &info) == FR_OK && info.fname[0]) if(info.fattrib & AM_DIR) ++n;
        f_closedir(&d);
    }
    assert(f_mount(NULL, "0:", 0) == FR_OK);
    return n;
}

int main(int argc, char **argv) {
    if(argc != 3) return 2;
    struct stat st;
    if(lstat(argv[1], &st) || !S_ISREG(st.st_mode) || st.st_size % 512) return 2;
    t.image = fopen(argv[1], "r+b"); if(!t.image) return 2;
    t.blocks = (uint64_t)st.st_size / 512;
    struct kui_media_ops media = {NULL, blocks, read_image, write_image, sync_image};
    kui_media_set(&media);

    const char *scenario = argv[2];
    struct kui_options o; kui_options_default(&o);
    o.sd_mib = 1; o.hash_mib = 1; o.optical_sectors = 64; o.expand = false;
    struct kui_bench_ops ops = {NULL, read_disc, NULL, cancelled, now_us, log_line, set_ui, cpu_mark, read_probe,
                                capture_run};

    if(!strcmp(scenario, "default")) {
        /* Nothing new was asked for: the run must look like the bench before the
         * experiment keys existed, plus the census and latency lines. */
    } else if(!strcmp(scenario, "ui")) {
        o.sections = KUI_SEC_OPTICAL | KUI_SEC_HASH;
        o.ui_hz[0] = KUI_OPT_UI_FULL; o.ui_hz[1] = 4; o.ui_hz[2] = 0; o.ui_count = 3;
    } else if(!strcmp(scenario, "sweep") || !strcmp(scenario, "sweep-corrupt") ||
              !strcmp(scenario, "sweep-retry") || !strcmp(scenario, "sweep-fatal") ||
              !strcmp(scenario, "sweep-cancel") || !strcmp(scenario, "sweep-noprobe")) {
        o.sections = KUI_SEC_SWEEP; o.sweep_sectors = 256;
        o.sweep_chunks[0] = 8; o.sweep_chunks[1] = 32; o.sweep_chunks[2] = 128; o.sweep_chunk_count = 3;
        o.sweep_fads[0] = 45150; o.sweep_fads[1] = 300000; o.sweep_fad_count = 2;
        o.sweep_gap_us[0] = 0; o.sweep_gap_us[1] = 1000; o.sweep_gap_count = 2;
        if(!strcmp(scenario, "sweep-corrupt")) { o.sweep_chunks[1] = 64; t.corrupt_chunk = 64; o.sweep_chunk_count = 2;
            o.sweep_chunks[0] = 32; o.sweep_fad_count = 1; o.sweep_gap_count = 1; }
        if(!strcmp(scenario, "sweep-retry")) t.retry_fad = 300000;
        if(!strcmp(scenario, "sweep-fatal")) t.fatal_probe = true;
        if(!strcmp(scenario, "sweep-cancel")) t.cancel_after_us = 1000000;   /* after the reference read and a first point */
        if(!strcmp(scenario, "sweep-noprobe")) ops.read_probe = NULL;
    } else if(!strcmp(scenario, "svc")) {
        o.sections = KUI_SEC_SWEEP; o.sweep_sectors = 64;
        o.sweep_chunks[0] = 32; o.sweep_chunk_count = 1; o.sweep_verify = false;
        o.sweep_service_us[0] = 0; o.sweep_service_us[1] = 500; o.sweep_service_count = 2;
    } else if(!strcmp(scenario, "sd-bytes")) {
        o.sections = KUI_SEC_SD; o.sd_mib = 2;
        o.chunks[0] = 32; o.chunk_count = 1;
        o.sd_bytes[0] = 65536; o.sd_bytes[1] = 131072; o.sd_bytes[2] = 1048576; o.sd_bytes_count = 3;
    } else if(!strcmp(scenario, "expand")) {
        o.sections = KUI_SEC_SD; o.expand = true; o.chunks[0] = 32; o.chunk_count = 1;
    } else if(!strcmp(scenario, "capture") || !strcmp(scenario, "capture-noop")) {
        /* Every combination: 2 hashes x 2 end read-backs x 2 sample rates, and both resume checks. */
        o.sections = KUI_SEC_CAPTURE; o.capture_sectors = 256;
        o.capture_crc_only[0] = false; o.capture_crc_only[1] = true; o.capture_hash_count = 2;
        o.end_readback[0] = true; o.end_readback[1] = false; o.end_readback_count = 2;
        o.sample_readback[0] = 0; o.sample_readback[1] = 3; o.sample_readback_count = 2;
        o.resume_size[0] = false; o.resume_size[1] = true; o.resume_check_count = 2;
        if(!strcmp(scenario, "capture-noop")) ops.capture_run = NULL;
    } else if(!strcmp(scenario, "bare")) {
        /* A platform with no UI control and no scheduler census. */
        o.sections = KUI_SEC_OPTICAL | KUI_SEC_HASH;
        ops.set_ui = NULL; ops.cpu_mark = NULL;
    } else {
        return 2;
    }

    enum kui_bench_result r = kui_bench(&ops, &o);
    if(!strcmp(scenario, "capture")) printf("JOBS_LEFT %u\n", jobs_left());
    printf("RESULT %u READS %u PROBES %u UI_CALLS %u UI_SEQ", (unsigned)r, t.read_calls, t.probe_calls, t.ui_calls);
    for(unsigned i = 0; i < t.ui_calls && i < 16; ++i) printf(" %u", t.ui_seq[i]);
    puts("");
    assert(fclose(t.image) == 0);
    return r == KUI_BENCH_COMPLETE ? 0 : r == KUI_BENCH_STOPPED ? 3 : 1;
}
