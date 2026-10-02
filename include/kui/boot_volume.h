/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_BOOT_VOLUME_H
#define KUI_BOOT_VOLUME_H
#include "kui/media.h"

enum kui_boot_volume_result {
    KUI_BOOT_VOLUME_OK, KUI_BOOT_VOLUME_IO, KUI_BOOT_VOLUME_INVALID,
    KUI_BOOT_VOLUME_UNSUPPORTED, KUI_BOOT_VOLUME_AMBIGUOUS
};
enum kui_boot_volume_kind {
    KUI_BOOT_VOLUME_RAW, KUI_BOOT_VOLUME_FAT_CANDIDATE,
    KUI_BOOT_VOLUME_LINUX_CANDIDATE
};
struct kui_boot_candidate {
    enum kui_boot_volume_kind kind;
    struct kui_volume volume;
};
struct kui_boot_layout {
    unsigned count;
    struct kui_boot_candidate candidates[2];
};
/* Read-only discovery, independent of the runtime FatFs selector. A candidate
 * type is only a hint; its filesystem must validate before any use. Raw media
 * has one RAW candidate; partitioned media has at most one FAT and one Linux
 * candidate, in unspecified order. MBR allows 0x0b/0x0c/0x07 and 0x83 only.
 * GPT recognizes ESP/Basic Data and Linux-filesystem GUIDs; unrelated GPT
 * entries remain range/overlap checked but are not boot candidates.
 * GPT accepts revision 1.0, 128-byte entries, at most 128 entries, and requires
 * both headers and arrays to agree and validate. No backup-only repair, hybrid
 * MBR, extended partitions or multiple candidates of the same kind. Names,
 * labels and boot flags never resolve an ambiguity. The 512-byte media
 * interface restricts total sectors to UINT32_MAX. On failure out is cleared;
 * no write/sync callback is ever invoked. */
enum kui_boot_volume_result kui_boot_volume_scan(const struct kui_media_ops *,
                                                 struct kui_boot_layout *out);
/* Compatibility helper for the ext4 reader: selects RAW or the unique Linux
 * candidate, including a Linux partition beside a FAT boot/recovery partition.
 * It never returns a FAT candidate. On failure out is cleared. */
enum kui_boot_volume_result kui_boot_volume_select(const struct kui_media_ops *,
                                                   struct kui_volume *out);
#endif
