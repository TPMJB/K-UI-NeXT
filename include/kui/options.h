/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_OPTIONS_H
#define KUI_OPTIONS_H
#include "kui/probe.h"

/* Runtime test options, read from /KUI/bench.cfg on the SD card.
 *
 * Why this exists: every hardware trip costs minutes and tests one build.
 * Reading the knobs from a text file lets one build test many configurations,
 * and echoing the parsed values into the log makes every diagnostics.txt
 * self-describing: you can always see which settings produced which numbers.
 *
 * File format: one "key=value" per line. '#' starts a comment. Blank lines
 * and unknown keys are ignored (unknown keys are logged so a typo is visible).
 * A missing file means defaults. A malformed value rejects the whole file so
 * a typo can never silently run the wrong test.
 *
 *   chunks=32,128,512      SD write/read chunk sizes to sweep, in raw sectors
 *   sd_mib=8               bytes written per SD run
 *   expand=on              also run each SD write with f_expand() preallocation
 *   hash_mib=8             bytes hashed per SHA-256 / CRC32 run
 *   optical_fad=45150      start of the fixed optical read range
 *   optical_sectors=4096   length of that range
 *   yield_us=2000          PIO service quantum used by disc.c
 *   sd_if=scif,sci         SD transports to sweep: scif (bit-bang), sci (+DMA)
 *   sd_crc=on,off          CRC16 read-verification settings to sweep
 *   note=any text          echoed into the log (card model, drive, etc.)
 */

#define KUI_OPT_CHUNK_MAX 512u   /* raw sectors; sizes the bench buffer */
#define KUI_OPT_LIST_MAX 8u
#define KUI_OPT_SD_MAX 2u
#define KUI_OPT_NOTE_MAX 64u
#define KUI_OPT_FILE_MAX 2048u   /* a larger bench.cfg is refused */

struct kui_options {
    unsigned chunks[KUI_OPT_LIST_MAX], chunk_count;
    unsigned sd_mib, hash_mib, optical_sectors, yield_us;
    uint32_t optical_fad;
    /* Swept in one run so a transport comparison needs no card removal. */
    unsigned sd_if[KUI_OPT_SD_MAX], sd_if_count;   /* 0 = SCIF, 1 = SCI */
    bool sd_crc[KUI_OPT_SD_MAX];
    unsigned sd_crc_count;
    bool expand;
    char note[KUI_OPT_NOTE_MAX];
};

void kui_options_default(struct kui_options *out);

/* Pure text parser; no filesystem, testable on the PC. On any malformed line
 * it logs the line number, leaves *opt untouched, and returns false. */
bool kui_options_parse(struct kui_options *opt, const char *text, size_t size,
                       kui_log_fn log);

/* Echo every value in one block so the log documents the run. */
void kui_options_log(const struct kui_options *opt, kui_log_fn log);

/* Read path on a mounted FatFs volume, then parse (options_file.c).
 * Missing file: defaults and true. Unreadable or malformed: defaults, false. */
bool kui_options_load(struct kui_options *opt, const char *path, kui_log_fn log);

#endif
