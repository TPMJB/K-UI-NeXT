/* SPDX-License-Identifier: GPL-3.0-only */
#include "retail_sd_bench.h"
#include "retail_sd.h"
#include <string.h>

static uint8_t reference[KUI_SD_BENCH_BLOCKS * 512] __attribute__((aligned(32)));
static uint8_t sample[10 * 512] __attribute__((aligned(32)));
static const uint32_t batches[3] = {2, 8, 10};

static bool bench_window(const struct kui_retail_manifest *m,
                         uint64_t card_blocks, uint32_t *lba) {
    if(!m || !m->track_count || m->track_count > KUI_RETAIL_IMAGE_TRACKS ||
       m->extent_count > KUI_RETAIL_IMAGE_EXTENTS ||
       m->partition_start >= m->partition_end ||
       m->partition_end > m->card_sectors || m->card_sectors > card_blocks)
        return false;
    for(uint32_t i = 0; i < m->track_count; ++i) {
        const struct kui_retail_track *t = &m->tracks[i];
        if(t->control != 4 || t->start_lba < 45000 || t->end_lba <= t->start_lba ||
           t->first_extent >= m->extent_count ||
           t->extent_count > m->extent_count - t->first_extent) continue;
        uint64_t full_blocks = (uint64_t)(t->end_lba - t->start_lba) * 2352 / 512;
        for(uint32_t j = 0; j < t->extent_count; ++j) {
            const struct kui_retail_extent *e = &m->extents[t->first_extent + j];
            uint64_t end = (uint64_t)e->card_lba + KUI_SD_BENCH_BLOCKS;
            if(e->blocks < KUI_SD_BENCH_BLOCKS ||
               (uint64_t)e->file_block + KUI_SD_BENCH_BLOCKS > full_blocks ||
               e->card_lba < m->partition_start || end > m->partition_end ||
               end > (UINT64_C(1) << 32)) continue;
            *lba = e->card_lba;
            return true;
        }
    }
    return false;
}

static enum kui_sd_bench_status bench_read(struct kui_loader_sd *card,
    struct kui_sd_bench_result *result, uint32_t method, uint32_t lba,
    uint32_t count, uint8_t *out, uint32_t (*clock_ticks)(void),
    struct kui_sd_bench_stat *stat) {
    result->failed_lba = lba;
    result->failed_method = method ? 18 : 17;
    uint32_t start = clock_ticks();
    result->card_result = kui_retail_sd_acquire();
    if(result->card_result != KUI_LOADER_SD_OK) return KUI_SD_BENCH_IO;
    result->card_result = method ? kui_loader_sd_read_multi(card, lba, count, out) :
                                  kui_loader_sd_read(card, lba, count, out);
    kui_retail_sd_release();
    uint32_t elapsed = clock_ticks() - start;
    if(result->card_result != KUI_LOADER_SD_OK) return KUI_SD_BENCH_IO;
    if(!elapsed) return KUI_SD_BENCH_CLOCK;
    if(stat) {
        stat->ticks += elapsed;
        stat->blocks += count;
        if(elapsed > stat->max_ticks) stat->max_ticks = elapsed;
    }
    return KUI_SD_BENCH_OK;
}

enum kui_sd_bench_status kui_retail_sd_bench_run(struct kui_loader_sd *card,
    const struct kui_retail_manifest *m, struct kui_sd_bench_result *result,
    uint32_t (*clock_ticks)(void)) {
    memset(result, 0, sizeof(*result));
    if(!card || !clock_ticks || !bench_window(m, card->blocks, &result->lba))
        return KUI_SD_BENCH_WINDOW;
    enum kui_sd_bench_status status = bench_read(card, result, 0, result->lba,
        KUI_SD_BENCH_BLOCKS, reference, clock_ticks, NULL);
    if(status != KUI_SD_BENCH_OK) return status;
    result->crc = kui_retail_crc32(0, reference, sizeof(reference));
    /* Identical 20 KiB window for every case, AB then BA. Reference comparison
     * and display work are outside timed calls. CMD17 after every CMD18 pass
     * checks stop/reselection independently of the CMD12 response itself. */
    for(uint32_t group = 0; group < 3; ++group) {
        uint32_t count = batches[group];
        for(uint32_t round = 0; round < 2; ++round) {
            for(uint32_t order = 0; order < 2; ++order) {
                uint32_t method = order ^ round;
                for(uint32_t offset = 0; offset < KUI_SD_BENCH_BLOCKS; offset += count) {
                    status = bench_read(card, result, method, result->lba + offset,
                        count, sample, clock_ticks, &result->stats[group][method]);
                    if(status != KUI_SD_BENCH_OK) return status;
                    if(memcmp(sample, reference + offset * 512, count * 512))
                        return KUI_SD_BENCH_MISMATCH;
                }
                if(method) {
                    status = bench_read(card, result, 0, result->lba, 1, sample,
                                        clock_ticks, NULL);
                    if(status != KUI_SD_BENCH_OK) return status;
                    if(memcmp(sample, reference, 512)) return KUI_SD_BENCH_MISMATCH;
                }
            }
        }
    }
    return KUI_SD_BENCH_OK;
}

#ifdef KUI_ON_CONSOLE
/* SH7750 hardware manual section 12. TMU2, Pck/4, no interrupts. Only this
 * one-shot high-stage diagnostic borrows a timer after KOS has shut down;
 * the game reader still uses bounded byte-work budgets, not hardware time. */
#define BENCH_TSTR (*(volatile uint8_t *)(uintptr_t)0xffd80004u)
#define BENCH_TCOR (*(volatile uint32_t *)(uintptr_t)0xffd80020u)
#define BENCH_TCNT (*(volatile uint32_t *)(uintptr_t)0xffd80024u)
#define BENCH_TCR (*(volatile uint16_t *)(uintptr_t)0xffd80028u)
static struct { uint32_t tcor, tcnt; uint16_t tcr; uint8_t tstr; } saved_clock;
static struct kui_sd_bench_result report;
static uint32_t bench_clock_ticks(void) { return UINT32_MAX - BENCH_TCNT; }
static bool bench_clock_begin(void) {
    saved_clock.tstr = BENCH_TSTR;
    BENCH_TSTR = saved_clock.tstr & (uint8_t)~4u;
    saved_clock.tcor = BENCH_TCOR;
    saved_clock.tcnt = BENCH_TCNT;
    saved_clock.tcr = BENCH_TCR;
    BENCH_TCR = 0;
    BENCH_TCOR = UINT32_MAX;
    BENCH_TCNT = UINT32_MAX;
    BENCH_TSTR = saved_clock.tstr | 4u;
    uint32_t before = bench_clock_ticks();
    for(unsigned i = 0; i < 1024; ++i) __asm__ volatile("nop");
    return bench_clock_ticks() != before;
}
static void bench_clock_end(void) {
    BENCH_TSTR &= (uint8_t)~4u;
    BENCH_TCR = saved_clock.tcr;
    BENCH_TCOR = saved_clock.tcor;
    BENCH_TCNT = saved_clock.tcnt;
    BENCH_TSTR = saved_clock.tstr;
}
static char *bench_text(char *out, const char *text) {
    while(*text) *out++ = *text++;
    return out;
}
static char *bench_number(char *out, uint32_t value) {
    char digits[10]; unsigned n = 0;
    do { digits[n++] = (char)('0' + value % 10); value /= 10; } while(value);
    while(n) *out++ = digits[--n];
    return out;
}
static void bench_stat_line(uint32_t group, uint32_t method) {
    const struct kui_sd_bench_stat *s = &report.stats[group][method];
    uint32_t rate = (uint32_t)((uint64_t)s->blocks * (KUI_SD_BENCH_HZ / 2) / s->ticks);
    uint32_t us = (uint32_t)(((uint64_t)s->max_ticks * 2 + 24) / 25);
    char line[64]; char *p = bench_text(line, "B");
    p = bench_number(p, batches[group]); p = bench_text(p, method ? " C18 " : " C17 ");
    p = bench_number(p, rate); p = bench_text(p, " KIB/S MAX ");
    p = bench_number(p, us); p = bench_text(p, " US"); *p = 0;
    retail_display_line(line);
}
void kui_retail_sd_benchmark(struct kui_loader_sd *card,
    const struct kui_retail_manifest *m, const struct retail_display_state *display) {
    retail_display_restore(display);
    retail_display_line("SD CMD17 AND CMD18 COMPARISON");
    retail_display_line("READING A SMALL WINDOW - PLEASE WAIT");
    bool running = bench_clock_begin();
    enum kui_sd_bench_status status = running ?
        kui_retail_sd_bench_run(card, m, &report, bench_clock_ticks) : KUI_SD_BENCH_CLOCK;
    bench_clock_end();
    retail_display_restore(display);
    retail_display_line("SD CMD17 AND CMD18 COMPARISON");
    if(status == KUI_SD_BENCH_OK) {
        retail_display_hex("WINDOW LBA", report.lba);
        retail_display_hex("REFERENCE CRC32", report.crc);
        for(uint32_t group = 0; group < 3; ++group)
            for(uint32_t method = 0; method < 2; ++method) bench_stat_line(group, method);
        retail_display_line("DATA AND STOP CHECKS PASSED");
        retail_display_line("B IS 512 BYTE BLOCKS PER CALL");
        retail_display_line("MAX IS LONGEST CALL - STOCK CLOCK");
    } else {
        retail_display_line(status == KUI_SD_BENCH_WINDOW ? "NO CONTIGUOUS TEST WINDOW" :
            status == KUI_SD_BENCH_MISMATCH ? "READ DATA MISMATCH" :
            status == KUI_SD_BENCH_CLOCK ? "BENCHMARK CLOCK NOT RUNNING" : "SD COMPARISON READ FAILED");
        retail_display_hex("LBA", report.failed_lba);
        retail_display_hex("READ METHOD", report.failed_method);
        retail_display_hex("SD RESULT", report.card_result);
        retail_display_hex("LAST COMMAND", card->last_command);
        retail_display_hex("LAST RESPONSE", card->last_response);
        retail_display_hex("CARD READY", card->ready);
    }
    retail_display_line("PHOTOGRAPH THIS SCREEN");
    retail_display_line("POWER OFF AND ON TO RETURN");
    retail_display_line("SD CARD WAS READ ONLY");
    for(;;) __asm__ volatile("nop");
}
#endif
