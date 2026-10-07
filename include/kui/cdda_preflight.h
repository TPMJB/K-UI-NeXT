/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_CDDA_PREFLIGHT_H
#define KUI_CDDA_PREFLIGHT_H
#include "kui/cdda_disc.h"
#include "kui/game_metadata.h"

#define KUI_CDDA_PREFLIGHT_IP_BYTES 32768u
#define KUI_CDDA_PREFLIGHT_DIGEST_BYTES 32u
enum kui_cdda_preflight_result {
    KUI_CDDA_PREFLIGHT_OK,KUI_CDDA_PREFLIGHT_INVALID,KUI_CDDA_PREFLIGHT_MAP,
    KUI_CDDA_PREFLIGHT_METADATA,KUI_CDDA_PREFLIGHT_RANGE,KUI_CDDA_PREFLIGHT_HEADER,
    KUI_CDDA_PREFLIGHT_IO,KUI_CDDA_PREFLIGHT_CANCELLED
};
enum kui_cdda_preflight_phase {
    KUI_CDDA_PREFLIGHT_INSPECT,KUI_CDDA_PREFLIGHT_HASH_IP,KUI_CDDA_PREFLIGHT_HASH_BOOT
};
struct kui_cdda_preflight_ops {
    void *context;
    /* Optional bounded callbacks. cancelled=true or progress=false cancels.
     * Progress is metadata read count during INSPECT (maximum145), then exact
     * accepted payload bytes during hashing. Neither callback may modify the
     * opened image/files or retain borrowed pointers. No storage writes. */
    bool (*cancelled)(void *);
    bool (*progress)(void *,enum kui_cdda_preflight_phase,uint32_t done,uint32_t total);
};
struct kui_cdda_preflight_report {
    struct kui_cdda_disc_map map; /* Complete metadata for every backed track. */
    struct kui_game_metadata metadata; /* product/version/region/bootfile/LBA/length */
    bool scrambled; /* Explicit image transform flag; no transform is applied. */
    uint32_t ip_lba,ip_bytes,ip_crc32,boot_crc32,metadata_reads,hash_reads;
    uint8_t ip_sha256[32],boot_sha256[32];
};
/* Read-only complete GDI preflight. Caller supplies an already opened immutable
 * image, retaining every backing file and callback until return. Missing or
 * unsupported track metadata, gaps, cross-track boot/IP spans and boot extents
 * overlapping the session IP area are refused. Native/CE media flags are
 * reported as metadata; this read-only check does not establish compatibility.
 * IP is32KiB of logical Mode1 payload; boot is the exact ISO extent byte length,
 * including a final partial sector but excluding its padding. CRC32/SHA256
 * identify stored payloads equally in cooked2048 and raw2352 GDI layouts.
 * Every raw sector read checks sync/mode AND its FAD header before use.
 * All hash spans are checked before any hashing; metadata itself is inspected
 * through the existing bounded reader. One physical sector per read, no heap,
 * launch, AICA or write. Output is unchanged on every failure/cancellation.
 * Output must not overlap the input image or callbacks. */
enum kui_cdda_preflight_result kui_cdda_preflight_read(const struct kui_game_image *,
    const struct kui_cdda_preflight_ops *,struct kui_cdda_preflight_report *);
const char *kui_cdda_preflight_result_name(enum kui_cdda_preflight_result);
#endif
