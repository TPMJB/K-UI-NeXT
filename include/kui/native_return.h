/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_NATIVE_RETURN_H
#define KUI_NATIVE_RETURN_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define KUI_NATIVE_RETURN_VERSION 1u
#define KUI_NATIVE_RETURN_EXTENTS 32u
#define KUI_NATIVE_RETURN_WIRE_BYTES 1024u
#define KUI_NATIVE_RETURN_ADDRESS UINT32_C(0x8c010000)
struct kui_native_return_extent {uint32_t file_block,card_lba,blocks;};
struct kui_native_return_plan {
    uint32_t version,bytes,file_bytes,extent_count;
    /* GUI discovery pins the volume's minimum card extent, not a CID or an
     * exact hardware capacity. The live SD must contain at least this many
     * sectors; every read remains inside the pinned partition below. */
    uint64_t card_sectors,partition_first,partition_end;
    uint8_t header[64];
    struct kui_native_return_extent extents[KUI_NATIVE_RETURN_EXTENTS];
};
enum kui_native_return_result {
    KUI_NATIVE_RETURN_OK=0,KUI_NATIVE_RETURN_ARGUMENT,KUI_NATIVE_RETURN_MAP,
    KUI_NATIVE_RETURN_HEADER,KUI_NATIVE_RETURN_CHANGED,KUI_NATIVE_RETURN_IO,
    KUI_NATIVE_RETURN_CRC,KUI_NATIVE_RETURN_LEASE
};
/* Wire: KUINRT01, version at8, wire size12, CRC32 at16, reserved20,
 * file_bytes24, extent_count28, card_sectors32, first40,end48,
 * original runtime header64..127, extents128 onward; unused bytes zero. */
bool kui_native_return_plan_valid(const struct kui_native_return_plan *);
bool kui_native_return_encode(uint8_t wire[KUI_NATIVE_RETURN_WIRE_BYTES],
    const struct kui_native_return_plan *);
bool kui_native_return_decode(struct kui_native_return_plan *,const void *,size_t);
/* The complete live worker/stack allocation must exclude the incoming payload.
 * The runtime sets its own stack before zeroing BSS; the old worker is dead
 * after its final jump. This grants no scratch memory outside the lease. */
bool kui_native_return_lease_safe(const struct kui_native_return_plan *,uint32_t lease,uint32_t bytes);
typedef bool (*kui_native_return_read)(void *,uint32_t card_lba,uint8_t sector[512]);
enum kui_native_return_result kui_native_return_load(const struct kui_native_return_plan *,
    uint32_t lease,uint32_t lease_bytes,void *destination,size_t destination_bytes,
    kui_native_return_read read,void *context);
#endif
