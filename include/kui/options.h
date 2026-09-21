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
 *
 * Experiment keys (docs/experiment-plan.md). All default to the shipped behaviour
 * except ui_hz, which defaults to 2: the unthrottled UI was measured taking ~32% of
 * the CPU (docs/evidence/t1-ui-census-2026-09-19.json).
 *
 *   ui_hz=full,4,0         max UI redraws per second WHILE an operation runs;
 *                          each value is one full pass of the sections below
 *                          (default 2; 'full' = the old unthrottled loop)
 *   sections=...,pipeline  measure a capture's inner loop three ways on the real drive and
 *                          the real card: PIO reads, DMA reads, and DMA reads OVERLAPPED with
 *                          the SD write (double-buffered). EXPERIMENTAL build only; without it
 *                          the DMA rows say so. This measures the overlap before the capture
 *                          engine is restructured to use it
 *   sections=optical,hash,sd,sweep
 *                          which bench sections run (default: optical,hash,sd)
 *   sweep_chunks=8,32,128  optical read sizes in sectors (1..128); the sweep
 *                          section runs only if this is set and 'sweep' is
 *                          in sections
 *   sweep_fads=45150,...   start sectors to sweep (default: optical_fad)
 *   sweep_gap_us=0,5000    idle spin after each read command (drive unfed)
 *   sweep_service_us=0     spin after each firmware poll inside a command
 *                          (0 = today's yield policy)
 *   sweep_sectors=2048     sectors read per sweep point
 *   sweep_verify=on        re-read and CRC each (fad, chunk) to prove larger
 *                          reads return identical bytes
 *   sweep_mode=pio,dma     how the sweep reads: PIO (as capture does) and/or GD-ROM
 *                          DMA. EXPERIMENTAL: DMA needs an even sector count, skips
 *                          sweep_service_us (nothing to poll), and if a DMA read
 *                          never completes the run says so and DMA stays off until
 *                          a reboot
 *   sweep_spin=off,on      run a competing CPU-bound thread during each point and
 *                          report how much CPU it got (free=) and how the read fared:
 *                          what a thread that writes to the SD card would see and cost
 *   sd_bytes=131072,...    extra SD write sizes in BYTES (multiples of 512),
 *                          to test alignment against the 128 KiB clusters
 *
 * Capture engine choices. Each is a real option of the capture engine, so a
 * REAL capture uses the FIRST value of each list, and the bench's `capture`
 * section (sections=...,capture) runs the engine at every combination. The
 * defaults are the engine as it has always been.
 *
 *   capture_hash=both,crc32    both = SHA-256 and CRC32 per track; crc32 = CRC32
 *                              only (a schema 2 manifest; a job keeps the mode
 *                              it started with)
 *   capture_read=dma,pio       DEFAULT dma: read the disc with GD-ROM DMA and overlap it with
 *                              the SD write (+36% on a whole disc, same bytes). Falls back to
 *                              PIO for any chunk it cannot overlap. pio = the old engine
 *   end_readback=on,off        re-read every saved byte after capture. off
 *                              applies to crc32 jobs only; Verify always reads
 *   resume_check=full,size     full re-reads the committed bytes on resume;
 *                              size checks sizes only (crc32 jobs only, and it
 *                              cannot see a corrupted prefix)
 *   sample_readback=0,32       re-read and compare 1 chunk in N while capturing
 *   capture_sectors=4096       sectors per bench capture run
 *   capture_fad=45150          where the bench capture starts (default:
 *                              optical_fad)
 *   capture_type=data|audio    data checks each sector's EDC; audio does not
 */

#define KUI_OPT_CHUNK_MAX 512u   /* raw sectors; sizes the bench buffer */
#define KUI_OPT_LIST_MAX 8u
#define KUI_OPT_SD_MAX 2u
#define KUI_OPT_UI_FULL 1000u    /* ui_hz value meaning "unthrottled, as before" */
#define KUI_OPT_UI_MAX 4u
#define KUI_OPT_UI_HZ_MAX 30u
#define KUI_OPT_SWEEP_LIST_MAX 4u
#define KUI_OPT_SWEEP_CHUNK_MAX 128u   /* sizes the optical probe buffer */
#define KUI_OPT_SDBYTES_MAX 6u
/* Bench sections; 'sections=' is a mask of these. */
#define KUI_SEC_OPTICAL 1u
#define KUI_SEC_HASH 2u
#define KUI_SEC_SD 4u
#define KUI_SEC_SWEEP 8u
#define KUI_SEC_CAPTURE 16u
#define KUI_SEC_PIPELINE 32u
#define KUI_OPT_CAPTURE_MAX 4u   /* values per numeric capture list */
#define KUI_OPT_NOTE_MAX 64u
/* A larger bench.cfg is refused. 2048 was too small for the commented example this repository
 * ships (5.3 KB), which is exactly the file whose name invites copying to the card: on
 * 2026-09-20 that silently cost a run. 8192 fits every shipped file with room to spare and
 * costs 6 KB of BSS out of the ~14 MB free. tests/test_options.c checks every shipped file. */
#define KUI_OPT_FILE_MAX 8192u

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
    /* Experiment keys. A count of 0 means "single default", see options.c. */
    unsigned ui_hz[KUI_OPT_UI_MAX], ui_count;
    unsigned sections;
    unsigned sweep_chunks[KUI_OPT_LIST_MAX], sweep_chunk_count;
    unsigned sweep_fads[KUI_OPT_SWEEP_LIST_MAX], sweep_fad_count;   /* FAD < 2^24 */
    unsigned sweep_gap_us[KUI_OPT_SWEEP_LIST_MAX], sweep_gap_count;
    unsigned sweep_service_us[KUI_OPT_SWEEP_LIST_MAX], sweep_service_count;
    unsigned sweep_sectors;
    bool sweep_verify;
    bool sweep_dma[KUI_OPT_SD_MAX];  unsigned sweep_mode_count;   /* false = PIO, true = DMA */
    bool sweep_spin[KUI_OPT_SD_MAX]; unsigned sweep_spin_count;
    unsigned sd_bytes[KUI_OPT_SDBYTES_MAX], sd_bytes_count;
    /* Capture engine choices; every list always has at least one value. */
    bool capture_crc_only[KUI_OPT_SD_MAX];   unsigned capture_hash_count;
    bool end_readback[KUI_OPT_SD_MAX];       unsigned end_readback_count;
    bool capture_dma[KUI_OPT_SD_MAX];        unsigned capture_read_count;
    bool resume_size[KUI_OPT_SD_MAX];        unsigned resume_check_count;
    unsigned sample_readback[KUI_OPT_CAPTURE_MAX], sample_readback_count;
    unsigned capture_sectors, capture_fad;   /* capture_fad 0 = optical_fad */
    bool capture_audio;
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
