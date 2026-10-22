/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TEST_STORAGE_SD_H
#define KUI_TEST_STORAGE_SD_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define SD_IF_SCIF 0
typedef struct {unsigned interface;bool check_crc;} sd_init_params_t;
int sd_init_ex(const sd_init_params_t *params);
void sd_shutdown(void);
uint64_t sd_get_size(void);
int sd_read_blocks(uint32_t block,size_t count,uint8_t *data);
int sd_write_blocks(uint32_t block,size_t count,const uint8_t *data);
#endif
