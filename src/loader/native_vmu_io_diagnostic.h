/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_NATIVE_VMU_IO_DIAGNOSTIC_H
#define KUI_NATIVE_VMU_IO_DIAGNOSTIC_H
#include "kui/retail_gd.h"
#include "sd_reader.h"
#include <stdbool.h>

/* Terminal-only snapshot, written into the retired 512-byte image cache.
 * It follows no game pointer, performs no I/O and grants no retry authority.
 * LAST LBA/CNT/DEST/DONE belong to the retained accepted command; a rejected
 * later REQUEST must not be interpreted as a newly accepted read. */
#define KUI_NATIVE_VMU_IO_DIAGNOSTIC_WORDS 9u
#define KUI_NATIVE_VMU_IO_IMAGE_NOT_ATTEMPTED UINT32_MAX
typedef uint32_t kui_native_vmu_io_word __attribute__((__may_alias__));
static inline void kui_native_vmu_io_snapshot(kui_native_vmu_io_word *out,
    const struct kui_retail_gd *gd,const struct kui_loader_sd *sd,
    uint32_t sd_result,uint32_t image_result,bool stream,bool fault,
    uint32_t function) {
    out[0]=function;out[1]=gd->diag.last_command;out[2]=sd_result;
    out[3]=gd->error==KUI_GD_ERROR_IO?image_result:UINT32_MAX;
    /* Low nibble: READY/SLOW/STREAM/FAULT. Next two bytes retain the exact
     * SD command and response without another value row or live MMIO. */
    out[4]=(uint32_t)sd->ready|((uint32_t)sd->slow<<1)|((uint32_t)stream<<2)|((uint32_t)fault<<3)|
        ((uint32_t)sd->last_command<<8)|((uint32_t)sd->last_response<<16);
    out[5]=gd->lba;out[6]=gd->count;out[7]=gd->destination;out[8]=gd->completed_bytes;
}
#endif
