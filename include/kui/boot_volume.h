/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_BOOT_VOLUME_H
#define KUI_BOOT_VOLUME_H
#include "kui/media.h"

enum kui_boot_volume_result {
    KUI_BOOT_VOLUME_OK, KUI_BOOT_VOLUME_IO, KUI_BOOT_VOLUME_INVALID,
    KUI_BOOT_VOLUME_UNSUPPORTED, KUI_BOOT_VOLUME_AMBIGUOUS
};
/* Read-only boot selection, independent of the runtime FatFs selector.
 * A raw whole-device candidate still requires the filesystem's own validation.
 * Partitioned media must have exactly one Linux filesystem partition: a sole
 * MBR type 0x83 primary partition, or Linux-filesystem GUID on valid GPT.
 * GPT accepts revision 1.0, 128-byte entries, at most 128 entries, and requires
 * both headers and arrays to agree and validate. No backup-only repair, hybrid
 * MBR, extended partitions, or selection among multiple Linux filesystems.
 * The 512-byte media interface restricts total sectors to UINT32_MAX.
 * On failure out is cleared; no write/sync callback is ever invoked. */
enum kui_boot_volume_result kui_boot_volume_select(const struct kui_media_ops *,
                                                   struct kui_volume *out);
#endif
