/* SPDX-License-Identifier: GPL-3.0-only */
#include "retail_sd_bench.h"
#include "retail_sd.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint32_t clock_value, reads, claims, releases, followups;
static bool owned, last_multi, corrupt_multi, fail_multi, frozen, refuse;
static uint32_t sequence[128], sequence_count;
static struct kui_loader_sd card;
static struct kui_retail_manifest manifest;
static struct kui_sd_bench_result report;
static uint32_t clock_ticks(void) { return clock_value; }
enum kui_loader_sd_result kui_retail_sd_acquire(void) {
    assert(!owned);
    if(refuse) return KUI_LOADER_SD_UNSUPPORTED;
    owned = true; ++claims;
    if(!frozen) clock_value += 10;
    return KUI_LOADER_SD_OK;
}
void kui_retail_sd_release(void) {
    assert(owned); owned = false; ++releases;
    if(!frozen) clock_value += 20;
}
static enum kui_loader_sd_result read_mock(struct kui_loader_sd *c, uint32_t lba,
    uint32_t count, void *out, bool multi) {
    assert(c == &card && owned && c->ready);
    assert(lba >= 1000 && (uint64_t)lba + count <= 1040);
    assert(sequence_count < sizeof(sequence) / sizeof(sequence[0]));
    sequence[sequence_count++] = (multi ? 1800 : 1700) + count;
    ++reads;
    if(last_multi && !multi && count == 1) ++followups;
    last_multi = multi;
    if(!frozen) clock_value += count * 100 + (multi ? 50 : 200);
    if(multi && fail_multi) {
        c->ready = false;
        return KUI_LOADER_SD_TIMEOUT;
    }
    uint8_t *p = out;
    for(uint32_t i = 0; i < count * 512; ++i)
        p[i] = (uint8_t)((lba + i / 512) ^ (i % 512) ^ ((i % 512) >> 8));
    if(multi && corrupt_multi) p[count * 512 - 1] ^= 1;
    return KUI_LOADER_SD_OK;
}
enum kui_loader_sd_result kui_loader_sd_read(struct kui_loader_sd *c,
    uint32_t lba, uint32_t count, void *out) { return read_mock(c, lba, count, out, false); }
enum kui_loader_sd_result kui_loader_sd_read_multi(struct kui_loader_sd *c,
    uint32_t lba, uint32_t count, void *out) { return read_mock(c, lba, count, out, true); }
static void reset(void) {
    memset(&card, 0, sizeof(card)); memset(&manifest, 0, sizeof(manifest));
    clock_value = UINT32_MAX - 40;
    reads = claims = releases = followups = sequence_count = 0;
    owned = last_multi = corrupt_multi = fail_multi = frozen = refuse = false;
    card.blocks = 2000; card.ready = true;
    manifest.card_sectors = 2000; manifest.partition_start = 100;
    manifest.partition_end = 1900; manifest.track_count = manifest.extent_count = 1;
    manifest.tracks[0] = (struct kui_retail_track){1, 45000, 45009, 4, 0, 1};
    manifest.extents[0] = (struct kui_retail_extent){0, 1000, 42};
}
static enum kui_sd_bench_status run(void) {
    enum kui_sd_bench_status s = kui_retail_sd_bench_run(&card, &manifest, &report, clock_ticks);
    assert(!owned && claims == releases);
    return s;
}
int main(void) {
    reset(); assert(run() == KUI_SD_BENCH_OK);
    assert(report.lba == 1000 && followups == 6 && reads == 123);
    /* First reference, AB/BA at B2, then B8/B10. Stop-check CMD17 is excluded. */
    assert(sequence[0] == 1740 && sequence[1] == 1702 && sequence[21] == 1802);
    assert(sequence[41] == 1701 && sequence[42] == 1802 && sequence[62] == 1701);
    uint32_t groups[] = {2, 8, 10};
    for(unsigned g = 0; g < 3; ++g) for(unsigned method = 0; method < 2; ++method) {
        const struct kui_sd_bench_stat *s = &report.stats[g][method];
        uint32_t cost = groups[g] * 100 + (method ? 80 : 230);
        assert(s->blocks == 80 && s->max_ticks == cost);
        assert(s->ticks == (uint64_t)(80 / groups[g]) * cost);
    }
    reset(); manifest.tracks[0].end_lba = 45008; /* Only 36 full blocks. */
    assert(run() == KUI_SD_BENCH_WINDOW && reads == 0);
    reset(); manifest.extents[0].file_block = 2; /* Last block is padding. */
    assert(run() == KUI_SD_BENCH_WINDOW && reads == 0);
    reset(); manifest.partition_end = 1039;
    assert(run() == KUI_SD_BENCH_WINDOW && reads == 0);
    reset(); manifest.extents[0].card_lba = UINT32_MAX - 20;
    assert(run() == KUI_SD_BENCH_WINDOW && reads == 0);
    reset(); manifest.tracks[0].control = 0;
    assert(run() == KUI_SD_BENCH_WINDOW && reads == 0);
    reset(); manifest.tracks[0].first_extent = UINT32_MAX;
    assert(run() == KUI_SD_BENCH_WINDOW && reads == 0);
    reset(); corrupt_multi = true;
    assert(run() == KUI_SD_BENCH_MISMATCH && report.failed_method == 18 && reads == 22);
    reset(); fail_multi = true;
    assert(run() == KUI_SD_BENCH_IO && !card.ready && reads == 22);
    assert(report.card_result == KUI_LOADER_SD_TIMEOUT);
    reset(); frozen = true;
    assert(run() == KUI_SD_BENCH_CLOCK && reads == 1);
    reset(); refuse = true;
    assert(run() == KUI_SD_BENCH_IO && reads == 0 && claims == 0);
    puts("retail SD benchmark: window bounds, timing, data equality and failure cleanup passed");
    return 0;
}
