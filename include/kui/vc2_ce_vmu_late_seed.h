/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VC2_CE_VMU_LATE_SEED_H
#define KUI_VC2_CE_VMU_LATE_SEED_H
#include <stddef.h>
#include <stdint.h>
#define KUI_VC2_LATE_SEED_MAGIC0 UINT32_C(0x32554d56)
#define KUI_VC2_LATE_SEED_MAGIC1 UINT32_C(0x44414f4c)
#define KUI_VC2_LATE_SEED_VERSION 1u
#define KUI_VC2_PERSIST_SEED_VERSION 2u
#define KUI_VC2_LATE_SEED_BYTES 44u
#define KUI_VC2_LATE_FILE_BYTES 49152u
#define KUI_VC2_LATE_FILE_BLOCKS 96u
#define KUI_VC2_LATE_RAW_P1 UINT32_C(0x8c008100)
#define KUI_VC2_LATE_RAW_P2 UINT32_C(0xac008100)
#define KUI_VC2_LATE_RAW_BYTES 512u
#define KUI_VC2_LATE_FILE_PATH "0:/KUI/apps/games/vc2-vmu-helper.bin"
/* Stage-owned expected size/CRC, followed by a launch-local read-only FAT
 * mapping. seed_crc32 covers all44 bytes with that word zero. Every field
 * uses the native little-endian ABI shared by the UI, stage and target. */
struct kui_vc2_vmu_late_seed {
    uint32_t magic0,magic1,version,bytes,helper_bytes,helper_crc32;
    uint32_t first_lba,first_blocks,second_lba,reserved,seed_crc32;
};
_Static_assert(sizeof(struct kui_vc2_vmu_late_seed)==44u,"VC2 late seed ABI");
_Static_assert(offsetof(struct kui_vc2_vmu_late_seed,seed_crc32)==40u,"VC2 late seed CRC ABI");
#endif
