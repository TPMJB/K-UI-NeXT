/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TEST_VMUFSMUTEX_H
#define KUI_TEST_VMUFSMUTEX_H
#include <stdint.h>
#include <dc/maple.h>
typedef struct {uint8_t bytes[8];} vmu_timestamp_t;
typedef struct {
    uint8_t magic[16],use_custom,custom_color[4],pad1[27];
    vmu_timestamp_t timestamp;uint8_t pad2[8],unk1[6];
    uint16_t fat_loc,fat_size,dir_loc,dir_size,icon_shape,blk_cnt;
    uint8_t unk2[430];
} vmu_root_t;
typedef struct {
    uint8_t filetype,copyprotect;uint16_t firstblk;char filename[12];
    vmu_timestamp_t timestamp;uint16_t filesize,hdroff;uint8_t dirty,pad1[3];
} vmu_dir_t;
int vmufs_mutex_lock(void);
int vmufs_mutex_unlock(void);
int vmufs_fat_write(maple_device_t *,vmu_root_t *,uint16_t *);
int vmufs_dir_write(maple_device_t *,vmu_root_t *,vmu_dir_t *);
#endif
