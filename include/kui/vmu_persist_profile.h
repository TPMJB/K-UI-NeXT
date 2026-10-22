/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_PERSIST_PROFILE_H
#define KUI_VMU_PERSIST_PROFILE_H
#include <stddef.h>
#include <stdint.h>

#define KUI_VMU_PERSIST_PROFILE_WIRE_BYTES 4096u
#define KUI_VMU_PERSIST_PROFILE_VERSION 2u
#define KUI_VMU_PERSIST_PROFILE_TRANSPORT 1u
#define KUI_VMU_PERSIST_PROFILE_PREP_ONLY 1u
#define KUI_VMU_PERSIST_PROFILE_MAX_EXTENTS 16u
#define KUI_VMU_PERSIST_PROFILE_PATH_BYTES 96u
#define KUI_VMU_PERSIST_PROFILE_FILES 3u

enum kui_vmu_persist_file_role {
    KUI_VMU_PERSIST_BASE=0, KUI_VMU_PERSIST_JOURNAL=1, KUI_VMU_PERSIST_IDENTITY=2
};
enum kui_vmu_persist_fs_type { KUI_VMU_PERSIST_FAT32=1, KUI_VMU_PERSIST_EXFAT=2 };
struct kui_vmu_persist_extent {uint32_t logical_block,lba,blocks;};
/* All LBAs are absolute card sectors. FAT sectors is per FAT copy. Bitmap and
 * upcase descriptors are zero on FAT32. Mutable dirty/percent-used flags are
 * intentionally absent: these fields describe stable semantic geometry. */
struct kui_vmu_persist_geometry {
    uint32_t fs_type,sector_bytes,sectors_per_cluster,volume_serial;
    uint32_t fat_count,active_fat;
    uint64_t fat_first_lba,fat_sectors,data_first_lba;
    uint32_t cluster_count,root_cluster,fat_mirrored;
    uint32_t bitmap_first_cluster,upcase_first_cluster;
    uint64_t bitmap_bytes,upcase_bytes;
};
/* Useful maps cover exactly256/2/1 sectors for BASE/JOURNAL/IDENTITY. Full
 * allocated maps cover ceil(file_bytes/cluster_bytes) whole clusters, including
 * slack. Each map is logical-order, coalesced, and has zero unused slots.
 * Role limits are16/2/1 extents respectively, despite fixed16-slot storage.
 * directory_first_cluster names the parent START, not its whole current chain.
 * entry_lba/entry_offset identify FAT's primary short entry or exFAT's85 entry;
 * entry_count includes preceding LFN+short (FAT) or the complete set (exFAT).
 * Locators are assertions checked by a fresh semantic lookup, never shortcuts
 * authorizing an old physical map. valid_bytes equals file_bytes, no holes. */
struct kui_vmu_persist_file {
    uint64_t file_bytes,valid_bytes;
    uint32_t first_cluster,allocation_flags,directory_first_cluster;
    uint32_t entry_offset;
    uint64_t entry_lba;
    uint32_t entry_count,entry_kind,useful_count,allocated_count;
    struct kui_vmu_persist_extent useful[KUI_VMU_PERSIST_PROFILE_MAX_EXTENTS];
    struct kui_vmu_persist_extent allocated[KUI_VMU_PERSIST_PROFILE_MAX_EXTENTS];
};
struct kui_vmu_persist_profile {
    uint64_t card_sectors,partition_first,partition_end;
    uint32_t transport,high_capacity,flags;
    uint8_t cid[16],image_id[16];
    char pair_path[KUI_VMU_PERSIST_PROFILE_PATH_BYTES];
    struct kui_vmu_persist_geometry geometry;
    struct kui_vmu_persist_file files[KUI_VMU_PERSIST_PROFILE_FILES];
    uint8_t base_sha256[32],journal_sha256[32],identity_sha256[32],marker_sha256[32];
    uint32_t block_crc[256];
    uint64_t journal_sequence; /* Initial physical binding requires zero. */
};
_Static_assert(sizeof(struct kui_vmu_persist_profile)<=4096u,"Persistent prep profile RAM bound");

enum kui_vmu_persist_profile_result {
    KUI_VMU_PERSIST_PROFILE_OK=0, KUI_VMU_PERSIST_PROFILE_ARGUMENT=-1,
    KUI_VMU_PERSIST_PROFILE_WIRE=-2, KUI_VMU_PERSIST_PROFILE_CRC=-3,
    KUI_VMU_PERSIST_PROFILE_FORMAT=-4, KUI_VMU_PERSIST_PROFILE_MAPPING=-5,
    KUI_VMU_PERSIST_PROFILE_IDENTITY=-6, KUI_VMU_PERSIST_PROFILE_IO=-7,
    KUI_VMU_PERSIST_PROFILE_VERIFY=-8, KUI_VMU_PERSIST_PROFILE_MARKER=-9
};
/* Canonical LE wire (all unlisted bytes zero):
 * 0magic8 KUIVMUP2,8version,12bytes,16CRC,20transport,24HC,28flags;
 * 32capacity64,40partfirst64,48partend64,56CID16,72imageID16;
 * 88baseSHA32,120journalSHA32,152identitySHA32,184pathlength,192path96;
 * 288fs,292sectorbytes,296SPC,300serial,304FATcount,308activeFAT,
 * 312FATfirst64,320FATsectors64,328datafirst64,336clusters,340root,
 * 344mirrored,352bitmapfirst,356upcasefirst,360bitmapbytes64,368upcasebytes64;
 * 384markerSHA32. Three448-byte file records begin at512:
 * +0filebytes64,+8firstcluster,+12allocationflags,+16parentstart,
 * +20entryoffset,+24entryLBA64,+32entrycount,+36entrykind,+40usefulcount,
 * +44allocatedcount,+48validbytes64,+64useful16x12,+256allocated16x12.
 * 1856blockCRC256x4,2880sequence64. CRC covers4096B with CRCword zero.
 * PREP_ONLY is an immutable descriptor kind, NEVER permission to write SD.
 * Hashes describe preparation provenance, not future ownership authority.
 * Every persistent launch needs a fresh semantic allocation/exclusivity scan
 * before recovery or writes, then recovery+VMU validation+fresh data hashes.
 * This codec never mounts a filesystem, performs recovery or enables writers.
 * Buffers must not overlap. Encode/decode clear output on every failure. */
int kui_vmu_persist_profile_validate_ownership(const struct kui_vmu_persist_profile *);
int kui_vmu_persist_profile_validate(const struct kui_vmu_persist_profile *);
int kui_vmu_persist_profile_encode(const struct kui_vmu_persist_profile *,uint8_t wire[4096]);
int kui_vmu_persist_profile_decode(struct kui_vmu_persist_profile *,const uint8_t *,size_t);
/* Ignores provenance hashes/CRCs/sequence; both ownership records must be
 * structurally valid. Returns1 only for exact semantic ownership equality. */
int kui_vmu_persist_profile_same_ownership(const struct kui_vmu_persist_profile *,
    const struct kui_vmu_persist_profile *);
int kui_vmu_persist_profile_map_lba(const struct kui_vmu_persist_profile *,
    unsigned role,uint32_t logical,uint32_t *out_lba);
typedef int (*kui_vmu_persist_profile_read)(void *,unsigned role,uint32_t logical,uint8_t out[512]);
/* Reads BASE256+JOURNAL2+IDENTITY1. Only the16 meaningful identity bytes are
 * hashed and must equal image_id. The journal marker is logical block1 and
 * must be exactly512 zero bytes; payload block0 may contain old data. A valid
 * old synthetic258-sector marker must NEVER be rebound to physical extents.
 * collect clears all provenance on failure; verify mutates no caller state.
 * No callback before ownership admission, no writes and no hidden recovery. */
int kui_vmu_persist_profile_collect_snapshot(struct kui_vmu_persist_profile *,
    kui_vmu_persist_profile_read,void *);
int kui_vmu_persist_profile_verify_snapshot(const struct kui_vmu_persist_profile *,
    kui_vmu_persist_profile_read,void *);
#endif
