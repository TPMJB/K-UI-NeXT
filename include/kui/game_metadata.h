/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_GAME_METADATA_H
#define KUI_GAME_METADATA_H

#include <stdbool.h>
#include <stdint.h>

#define KUI_GAME_METADATA_SECTOR_BYTES 2048u
#define KUI_GAME_METADATA_MAX_DIRECTORY_BYTES (256u * 1024u)
#define KUI_GAME_METADATA_MAX_BOOT_BYTES (16u * 1024u * 1024u)

enum kui_game_metadata_io_result {
    KUI_GAME_METADATA_IO_OK, KUI_GAME_METADATA_IO_ERROR,
    KUI_GAME_METADATA_IO_CANCELLED
};
struct kui_game_metadata_ops {
    void *ctx;
    /* Read exactly one logical data sector, using absolute disc LBA (not FAD).
     * The callback handles cancellation and rejects audio/gaps/truncation. */
    enum kui_game_metadata_io_result (*read_sector)(void *ctx, uint32_t lba,
                                                   uint8_t data[2048]);
    /* Validate all sectors against actual data-track/file extents, without
     * reading them. Called only with a nonzero sector count. */
    bool (*data_range)(void *ctx, uint32_t lba, uint32_t count);
};
enum kui_game_metadata_status {
    KUI_GAME_METADATA_OK, KUI_GAME_METADATA_ARGUMENT,
    KUI_GAME_METADATA_IO, KUI_GAME_METADATA_CANCELLED,
    KUI_GAME_METADATA_IP_HEADER, KUI_GAME_METADATA_ISO,
    KUI_GAME_METADATA_BOOT_NOT_FOUND, KUI_GAME_METADATA_LIMIT,
    KUI_GAME_METADATA_UNSUPPORTED
};
struct kui_game_metadata {
    /* IP fields remain available if subsequent filesystem inspection fails.
     * Neither flag asserts retail compatibility or verifies all file bytes. */
    bool ip_valid, boot_valid;
    char title[129], product[11], version[7], region[9], bootfile[17];
    /* volume_blocks is the recorded volume size, NOT an absolute end LBA.
     * Multisession writers may exclude the session lead-in from that count. */
    uint32_t session_lba, volume_blocks, root_lba, root_bytes;
    uint32_t boot_lba, boot_bytes, sectors_read;
};
/* Inspects IP header, volume descriptor(s), bounded root directory and the
 * boot-file extent. No executable is launched and no whole-file hash occurs.
 * Output is reset on entry. Directory lookup supports a single root filename,
 * ISO9660 primary descriptors and one contiguous regular-file extent; Joliet,
 * Rock Ridge aliases, interleaving, multi-extents and subdirectories are not
 * interpreted. ISO extents are absolute LBAs, never session-relative offsets.
 * Maximum reads: one IP header + 16 descriptors + 128 root sectors.
 */
enum kui_game_metadata_status kui_game_metadata_read(
    const struct kui_game_metadata_ops *ops, uint32_t session_lba,
    struct kui_game_metadata *out);
const char *kui_game_metadata_status_text(enum kui_game_metadata_status status);

#endif
