/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SMASH_FAT_READ_CACHE_H
#define KUI_SMASH_FAT_READ_CACHE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define KUI_SMASH_FAT_CACHE_WINDOW_BLOCKS 16u
#define KUI_SMASH_FAT_CACHE_COPIES 2u
struct kui_smash_fat_read_window {
    uint32_t first,count;
    _Alignas(32) uint8_t data[KUI_SMASH_FAT_CACHE_WINDOW_BLOCKS*512u];
};
struct kui_smash_fat_read_cache {
    uint32_t magic,epoch,fat_count,fat_blocks;
    uint64_t card_sectors,partition_first,partition_end;
    uint32_t fat_first[KUI_SMASH_FAT_CACHE_COPIES];
    struct kui_smash_fat_read_window windows[KUI_SMASH_FAT_CACHE_COPIES];
};
struct kui_smash_fat_read_ops {
    void *context;
    int (*read_one)(void *,uint32_t,uint8_t [512]);
    /* A successful call returns a CRC-verified prefix of1..count blocks and
     * has stopped its stream in a healthy state. An unrequested look-ahead
     * block may fail after that prefix; it must never become cached data.
     * A failed first block or uncertain teardown returns nonzero. */
    int (*read_window)(void *,uint32_t,uint32_t,uint8_t *,uint32_t *verified);
};
bool kui_smash_fat_cache_configure(struct kui_smash_fat_read_cache *,uint64_t card_sectors,
    uint64_t partition_first,uint64_t partition_end);
/* Every identity/verification epoch and every stage write clears both data
 * windows and learned geometry. Only a fresh successful VBR read enables
 * later read-ahead; prepared profile geometry is never a cache input. */
void kui_smash_fat_cache_begin_epoch(struct kui_smash_fat_read_cache *);
int kui_smash_fat_cache_read(struct kui_smash_fat_read_cache *,
    const struct kui_smash_fat_read_ops *,uint32_t,uint8_t [512]);
#endif
