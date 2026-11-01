/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_DISC_BOOT_H
#define KUI_DISC_BOOT_H

#include "kui/game_metadata.h"
#include <stdbool.h>
#include <stdint.h>

#define KUI_DISC_BOOT_GD_TYPE UINT32_C(0x80)
#define KUI_DISC_BOOT_IP_BYTES UINT32_C(32768)
#define KUI_DISC_BOOT_MAX_EXEC_BYTES (12u * 1024u * 1024u)
#define KUI_DISC_BOOT_RUN_SECTORS 8u

enum kui_disc_boot_io_result {
    KUI_DISC_BOOT_IO_OK, KUI_DISC_BOOT_IO_ERROR, KUI_DISC_BOOT_IO_CANCELLED
};
struct kui_disc_boot_source {
    void *context;
    /* Exactly one cooked physical-disc sector. fad is absolute FAD, including
     * the 150-sector lead-in. No SD image map or filesystem is involved. */
    enum kui_disc_boot_io_result (*read_sector)(void *context, uint32_t fad,
                                               uint8_t data[2048]);
    bool (*cancelled)(void *context);
    /* Positively observed physical media type and one contiguous DATA extent
     * of its high-density session. first_fad is the IP.BIN sector; end_fad is
     * exclusive and must stop before audio, a gap or the leadout. The caller
     * owns the optical device and must keep this media/geometry unchanged. */
    uint32_t disc_type, first_fad, end_fad;
    /* Optional bounded cooked-sector reader for hashing known extents. count
     * is 1..KUI_DISC_BOOT_RUN_SECTORS; data has count * 2048 bytes. Metadata
     * always uses read_sector, with no lookahead beyond its requested sector. */
    enum kui_disc_boot_io_result (*read_run)(void *context, uint32_t fad,
                                            uint32_t count, uint8_t *data);
    /* Zero selects the 12 MiB format limit. A smaller nonzero limit refuses
     * after checked metadata, before any IP/executable hashing. */
    uint32_t max_exec_bytes;
};
enum kui_disc_boot_status {
    KUI_DISC_BOOT_OK, KUI_DISC_BOOT_ARGUMENT, KUI_DISC_BOOT_MEDIA,
    KUI_DISC_BOOT_IO, KUI_DISC_BOOT_CANCELLED, KUI_DISC_BOOT_IP,
    KUI_DISC_BOOT_ISO, KUI_DISC_BOOT_NOT_FOUND, KUI_DISC_BOOT_LIMIT,
    KUI_DISC_BOOT_UNSUPPORTED, KUI_DISC_BOOT_WINDOWS_CE, KUI_DISC_BOOT_CHANGED
};
struct kui_disc_boot_plan {
    struct kui_game_metadata metadata;
    uint32_t first_fad, end_fad, boot_fad, boot_sectors, sectors_read;
    uint32_t ip_crc32, boot_crc32;
    uint8_t ip_sha256[32], boot_sha256[32];
};
/* Read-only physical-GD preflight. Reuses the checked ISO9660/IP parser, then
 * hashes all 32 KiB of IP.BIN and the boot file's exact declared bytes. Sector
 * padding is excluded from the executable hashes. Bounds and cancellation
 * apply before EVERY callback; unknown callback results refuse as I/O errors.
 *
 * Only native, unscrambled GD-ROM media is admitted. CD/MIL-CD and Windows CE
 * need separate launch paths. This is NOT a launch, a resident-memory lifetime
 * proof or permission to install VMU hooks. No filesystem, SD card, device
 * write, reset or game handoff is performed. A later loader must bind the plan
 * to fresh media and verify the bytes it actually loads against these hashes.
 *
 * Source and out must be disjoint. A null/overlapping argument leaves output
 * unchanged and makes no callback. Other failures clear the whole output;
 * only OK publishes a complete plan. */
enum kui_disc_boot_status kui_disc_boot_prepare(const struct kui_disc_boot_source *source,
                                               struct kui_disc_boot_plan *out);
const char *kui_disc_boot_status_text(enum kui_disc_boot_status status);

#endif
