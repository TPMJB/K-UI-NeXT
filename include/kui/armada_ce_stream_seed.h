/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_ARMADA_CE_STREAM_SEED_H
#define KUI_ARMADA_CE_STREAM_SEED_H
#include "kui/armada_ce_excerpt_pool.h"
#include "kui/retail_image.h"
#include "kui/wince_cdda_profiles.h"
#include "kui/wince_cdda_wwp_kernel.h"

/* Authored pre-boot metadata only. Audio remains in the user's image; the
 * package contains a blank fixed wire map, never proprietary PCM. */
#define KUI_ARMADA_CE_STREAM_MAGIC UINT32_C(0x32584543) /* CEX2 */
#define KUI_ARMADA_CE_STREAM_VERSION 2u
#define KUI_ARMADA_CE_STREAM_SEED_WORDS 16u
#define KUI_ARMADA_CE_STREAM_SEED_BYTES 64u
#define KUI_ARMADA_CE_STREAM_SOURCE_BEGIN (KUI_ARMADA_CE_EXCERPT_POOL_BEGIN+64u)
#define KUI_ARMADA_CE_STREAM_SOURCE_BYTES 352800u
#define KUI_ARMADA_CE_STREAM_RING_BYTES 352768u
#define KUI_ARMADA_CE_STREAM_CODE_BEGIN (KUI_ARMADA_CE_EXCERPT_POOL_BEGIN+0x57000u)
#define KUI_ARMADA_CE_STREAM_CODE_END (KUI_ARMADA_CE_EXCERPT_POOL_BEGIN+0x6f000u)
#define KUI_ARMADA_CE_STREAM_MAP_BEGIN KUI_ARMADA_CE_STREAM_CODE_END
#define KUI_ARMADA_CE_STREAM_WORK_BEGIN (KUI_ARMADA_CE_EXCERPT_POOL_BEGIN+0x70000u)
#define KUI_ARMADA_CE_STREAM_WORK_END (KUI_ARMADA_CE_EXCERPT_POOL_BEGIN+0x7f000u)
#define KUI_ARMADA_CE_STREAM_GUARD_END KUI_ARMADA_CE_EXCERPT_POOL_END
#define KUI_ARMADA_CE_STREAM_MAP_CRC_WORD 6u
struct kui_armada_ce_stream_seed {
    uint32_t words[KUI_ARMADA_CE_STREAM_SEED_WORDS];
    uint8_t wire[KUI_RETAIL_IMAGE_WIRE_BYTES];
};
#define KUI_ARMADA_CE_STREAM_BLANK_WORDS \
    {KUI_ARMADA_CE_STREAM_MAGIC,KUI_ARMADA_CE_STREAM_VERSION, \
     KUI_ARMADA_CE_STREAM_SOURCE_BEGIN,KUI_ARMADA_CE_STREAM_SOURCE_BYTES, \
     KUI_ARMADA_CE_STREAM_MAP_BEGIN,KUI_RETAIL_IMAGE_WIRE_BYTES,0, \
     KUI_ARMADA_CE_STREAM_RING_BYTES,0,0,0,0,0,0,0,0}
_Static_assert(sizeof(struct kui_armada_ce_stream_seed)==4160u,"stream seed ABI");
_Static_assert(KUI_ARMADA_CE_STREAM_SOURCE_BEGIN+KUI_ARMADA_CE_STREAM_SOURCE_BYTES<=
    KUI_ARMADA_CE_STREAM_CODE_BEGIN,"source/controller separation");
_Static_assert(KUI_ARMADA_CE_STREAM_CODE_END==KUI_ARMADA_CE_STREAM_MAP_BEGIN &&
    KUI_ARMADA_CE_STREAM_MAP_BEGIN+KUI_RETAIL_IMAGE_WIRE_BYTES==KUI_ARMADA_CE_STREAM_WORK_BEGIN,
    "controller/map/work separation");
static inline bool kui_armada_ce_stream_seed_header_valid(const uint32_t *words) {
    if(!words || words[0]!=KUI_ARMADA_CE_STREAM_MAGIC ||
       words[1]!=KUI_ARMADA_CE_STREAM_VERSION ||
       words[2]!=KUI_ARMADA_CE_STREAM_SOURCE_BEGIN || words[3]!=KUI_ARMADA_CE_STREAM_SOURCE_BYTES ||
       words[4]!=KUI_ARMADA_CE_STREAM_MAP_BEGIN || words[5]!=KUI_RETAIL_IMAGE_WIRE_BYTES ||
       words[7]!=KUI_ARMADA_CE_STREAM_RING_BYTES) return false;
    for(unsigned i=8u;i<KUI_ARMADA_CE_STREAM_SEED_WORDS;i++) if(words[i]) return false;
    return true;
}
/* The shared menu validates the selected module's placement rather than its
 * own compilation profile. Never fill one runtime's wire seed in another. */
static inline bool kui_wince_cdda_seed_header_valid(const uint32_t *words,
        enum kui_wince_cdda_profile profile) {
    uint32_t pool=profile==KUI_WINCE_CDDA_WORMS_WORLD_PARTY?KUI_WWP_CE_POOL_BEGIN:
        profile==KUI_WINCE_CDDA_WORMS_ARMAGEDDON?0x8ce70000u:0x8cf50000u;
    if(!words || !kui_wince_cdda_disc(profile) ||
       words[0]!=KUI_ARMADA_CE_STREAM_MAGIC || words[1]!=KUI_ARMADA_CE_STREAM_VERSION ||
       words[2]!=pool+64u || words[3]!=KUI_ARMADA_CE_STREAM_SOURCE_BYTES ||
       words[4]!=pool+0x6f000u || words[5]!=KUI_RETAIL_IMAGE_WIRE_BYTES ||
       words[7]!=KUI_ARMADA_CE_STREAM_RING_BYTES) return false;
    for(unsigned i=8u;i<KUI_ARMADA_CE_STREAM_SEED_WORDS;i++) if(words[i]) return false;
    return true;
}
/* Call after the canonical full wire decoder. Do not use an async-sized
 * truncated manifest: both raw audio files must have every mapped block. */
static inline bool kui_armada_ce_stream_map_valid(const struct kui_retail_manifest *map) {
    return kui_wince_cdda_runtime_manifest_valid(map,KUI_WINCE_CDDA_COMPILED_PROFILE);
}
static inline bool kui_armada_ce_stream_maps_match(const struct kui_retail_manifest *full,
        const struct kui_retail_manifest *data) {
    if(!full || !data || full->track_count!=data->track_count ||
       full->card_sectors!=data->card_sectors || full->partition_start!=data->partition_start ||
       full->partition_end!=data->partition_end || full->storage_transport!=data->storage_transport ||
       full->session_lba!=data->session_lba || full->boot_lba!=data->boot_lba ||
       full->boot_bytes!=data->boot_bytes || full->flags!=data->flags ||
       full->boot_crc32!=data->boot_crc32 || full->ip_crc32!=data->ip_crc32 ||
       full->gdi_crc32!=data->gdi_crc32) return false;
    for(unsigned i=0;i<full->track_count;i++) {
        const struct kui_retail_track *a=&full->slots[i].track,*b=&data->slots[i].track;
        if(a->start_lba!=b->start_lba || a->end_lba!=b->end_lba ||
           a->control!=b->control || kui_retail_track_file_offset(a)!=kui_retail_track_file_offset(b)) return false;
    }
    return true;
}
#endif
