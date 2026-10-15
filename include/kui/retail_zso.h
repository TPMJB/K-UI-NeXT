/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_RETAIL_ZSO_H
#define KUI_RETAIL_ZSO_H
#include "kui/retail_image.h"
#include "kui/zso.h"

#define KUI_RETAIL_ZSO_SLOTS 32u
#define KUI_RETAIL_ZSO_EXTENTS (KUI_RETAIL_ZSO_SLOTS-1u)
_Static_assert(KUI_RETAIL_MANIFEST_SLOTS>=KUI_RETAIL_ZSO_SLOTS,"ZSO manifest must fit its32slots");
#define KUI_RETAIL_ZSO_WIRE_MAGIC "KUIRZS01"
#define KUI_RETAIL_ZSO_PACKAGE_MAGIC "KUIRZB01"
#define KUI_RETAIL_ZSO_VERSION 1u
/* The ordinary manifest ABI is unchanged. Only separately compiled ZSO
 * stages/residents use this wrapper and a 32-slot manifest allocation. The
 * launcher may use its larger manifest allocation: only the wire crosses
 * that boundary. Slot0 is one cooked ISO track; remaining slots map the
 * compressed physical FILE (header/index included), never logical ISO bytes. */
struct kui_retail_zso_manifest {
    struct kui_retail_manifest manifest;
    struct kui_zso_info zso;
};
/* Distinct canonical4096-byte LE wire. Common header fields retain ordinary
 * offsets0..271; words272/276/280/284/288 are file_bytes/total_bytes/
 * block_count/index_bytes/shift. Bytes292..319 and unused slots must be zero.
 * CRC at16 covers all4096 bytes with its own word zeroed. One track starts
 * at session_lba; logical block is lba-start. At most31 nonoverlapping extents
 * cover ceil(file_bytes/512), with only the last sector containing padding.
 * Encode preserves output on failure; decode clears output on failure. */
enum kui_game_result kui_retail_zso_manifest_validate(const struct kui_retail_zso_manifest *);
enum kui_game_result kui_retail_zso_manifest_encode(const struct kui_retail_zso_manifest *,uint8_t[4096]);
enum kui_game_result kui_retail_zso_manifest_decode(const uint8_t[4096],struct kui_retail_zso_manifest *);

struct kui_retail_zso_image {
    const struct kui_retail_zso_manifest *map;
    kui_retail_read_block read_block;
    kui_retail_read_run read_run;
    void *context;
    uint32_t blocks_read,cached_lba,cache_valid;
    _Alignas(32) uint8_t block[512];
};
_Static_assert(offsetof(struct kui_retail_zso_image,block)%32u==0,"ZSO physical cache alignment");
/* Init validates geometry/map only and performs no I/O. Highstage then
 * verifies the actual header and entire index before using logical reads.
 * Resident receives the same immutable validated prefix and keeps no pointer
 * into departed highstage RAM. All output must be independent of image/map. */
enum kui_game_result kui_retail_zso_image_init(struct kui_retail_zso_image *,const struct kui_retail_zso_manifest *,kui_retail_read_block,void *);
enum kui_game_result kui_retail_zso_verify_source(struct kui_retail_zso_image *);
enum kui_game_result kui_retail_zso_image_check(const struct kui_retail_manifest *,uint32_t,uint32_t,enum kui_game_sector_format);
/* Only whole2048-byte Mode1 sectors, at most64, decoded into caller output.
 * Rejected requests perform no I/O/output changes. Later I/O/codec failure
 * may leave partial output and invalidates the physical cache. Caller must
 * stop/release its stream on every return, as with ordinary image_read. */
enum kui_game_result kui_retail_zso_image_read(struct kui_retail_zso_image *,uint32_t,uint32_t,enum kui_game_sector_format,void *,size_t);
#endif
