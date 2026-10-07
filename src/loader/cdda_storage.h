/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_CDDA_STORAGE_H
#define KUI_CDDA_STORAGE_H
#include <stdint.h>

/* One owner, one read-only file, no callbacks into the retired kernel.
 * All calls are synchronous and exact: zero means success. The caller keeps
 * CPU interrupts masked and serializes these calls with all other SCI use. */
int cdda_storage_init(void);
int cdda_storage_open(const char *path, uint32_t *file_bytes);
int cdda_storage_read_at(uint32_t offset, void *out, uint32_t bytes);
void cdda_storage_close(void);
void cdda_storage_shutdown(void);
uint32_t cdda_storage_blocks_read(void);
const char *cdda_storage_last_failure(void);
#endif
