/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_CAPTURE_EXPORT_INTERNAL_H
#define KUI_CAPTURE_EXPORT_INTERNAL_H
#include "kui/capture_export.h"
size_t kui_export_deflate(const uint8_t *,size_t,uint8_t *,size_t);
bool kui_export_inflate(const uint8_t *,size_t,uint8_t *,size_t);
enum kui_capture_export_result kui_capture_chd_write(const struct kui_capture_plan *,
    const struct kui_checkpoint *,const struct kui_capture_export_io *,struct kui_capture_export_report *);
#endif
