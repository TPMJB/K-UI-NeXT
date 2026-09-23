/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TEST_VMU_BLOCK_H
#define KUI_TEST_VMU_BLOCK_H
#include <dc/maple.h>
int vmu_block_read(maple_device_t *device,uint16_t block,uint8_t *buffer);
#endif
