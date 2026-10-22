/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_SD_PROFILE_H
#define KUI_VMU_SD_PROFILE_H
#include <stddef.h>
#include <stdint.h>

#define KUI_VMU_SD_PROFILE_WIRE_BYTES 1536u
#define KUI_VMU_SD_PROFILE_VERSION 1u
#define KUI_VMU_SD_PROFILE_TRANSPORT 1u
#define KUI_VMU_SD_PROFILE_IMAGE_BYTES 131072u
#define KUI_VMU_SD_PROFILE_BLOCKS 256u
#define KUI_VMU_SD_PROFILE_MAX_EXTENTS 16u

struct kui_vmu_sd_profile_extent {uint32_t logical_block,lba,blocks;};
/* Host-owned decoded state, never copied directly onto disk. Partition end
 * is exclusive. Extents are in logical file order, cover exactly0..255, and
 * coalesce physically adjacent runs. Unused extent slots must remain zero. */
struct kui_vmu_sd_profile {
    uint64_t card_sectors,partition_first,partition_end;
    uint32_t transport,image_bytes,high_capacity,extent_count;
    uint8_t cid[16],image_id[16],sha256[32];
    struct kui_vmu_sd_profile_extent extents[KUI_VMU_SD_PROFILE_MAX_EXTENTS];
    uint32_t block_crc[KUI_VMU_SD_PROFILE_BLOCKS];
};
_Static_assert(sizeof(struct kui_vmu_sd_profile)<=2048u,"VMU profile memory bound");

enum kui_vmu_sd_profile_result {
    KUI_VMU_SD_PROFILE_OK=0,
    KUI_VMU_SD_PROFILE_ARGUMENT=-1,
    KUI_VMU_SD_PROFILE_WIRE=-2,
    KUI_VMU_SD_PROFILE_CRC=-3,
    KUI_VMU_SD_PROFILE_FORMAT=-4,
    KUI_VMU_SD_PROFILE_MAPPING=-5,
    KUI_VMU_SD_PROFILE_IDENTITY=-6,
    KUI_VMU_SD_PROFILE_IO=-7,
    KUI_VMU_SD_PROFILE_VERIFY=-8
};

/* Canonical LE wire offsets: magic0(8),version8,length12,CRC16,transport20,
 * imagebytes24,HC28,capacity32(u64),partitionfirst40(u64),partitionend48(u64),
 * CID56(16),imageID72(16),SHA88(32),extentcount120,zero124(4),extents128(192),
 * blockCRCs320(1024),zero1344(192). CRC covers all1536 bytes with CRCword zero.
 * Buffers/state must not overlap. Decode clears out on every failure; encode
 * clears wire on failure. Public validation requires a nonzero image digest.
 * These are consistency checks, not authentication or filesystem ownership. */
int kui_vmu_sd_profile_validate(const struct kui_vmu_sd_profile *);
int kui_vmu_sd_profile_encode(const struct kui_vmu_sd_profile *,uint8_t wire[1536]);
int kui_vmu_sd_profile_decode(struct kui_vmu_sd_profile *out,const uint8_t *wire,size_t bytes);
/* Mapping validates identity/extents but does not require preexisting hashes,
 * allowing a raw callback to map this profile while collect_hashes builds it. */
int kui_vmu_sd_profile_map_lba(const struct kui_vmu_sd_profile *,uint16_t block,uint32_t *out_lba);

/* Callback supplies exactly one logical512B image block,0..255. It performs
 * no writes and retains no buffer pointer; zero means success. A raw adapter
 * can map_lba() then read its admitted card sector. Filesystem admission and
 * canonical VMU validation belong to the caller before handoff. */
typedef int (*kui_vmu_sd_profile_read_block)(void *,uint16_t,uint8_t out[512]);
/* Validates identity/map before any callback, but does not require old hashes.
 * Reads all256 blocks and fills CRCs/SHA, using bounded one-block scratch.
 * Any failure clears every hash while retaining caller's identity/map fields. */
int kui_vmu_sd_profile_collect_hashes(struct kui_vmu_sd_profile *,
    kui_vmu_sd_profile_read_block,void *);
/* Validates the complete profile before any callback. Reads and checks each
 * blockCRC, then the full SHA; any read or mismatch fails closed. No profile
 * or image byte is changed, and no journal recovery or filesystem is invoked. */
int kui_vmu_sd_profile_verify_image(const struct kui_vmu_sd_profile *,
    kui_vmu_sd_profile_read_block,void *);
#endif
