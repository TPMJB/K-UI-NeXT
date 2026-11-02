/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SCREENSHOT_H
#define KUI_SCREENSHOT_H
#include "kui/probe.h"

#define KUI_SCREENSHOT_WIDTH_MAX 640u
#define KUI_SCREENSHOT_HEIGHT_MAX 480u
#define KUI_SCREENSHOT_PATH_CAP 64u
#define KUI_SCREENSHOT_FOLDER "0:/KUI/screenshots"

/* A successful callback has consumed every supplied byte. The encoder uses
 * one bounded scanline and never changes the supplied top-down RGB565 frame.
 * pixel_count is the accessible number of uint16_t pixels, including stride
 * padding. Unsupported geometry is refused before the first callback. */
typedef bool (*kui_screenshot_write_fn)(void *context,const void *data,size_t size);
bool kui_screenshot_bmp_size(size_t pixel_count,unsigned width,unsigned height,
    unsigned stride,uint32_t *bytes);
bool kui_screenshot_bmp_rgb565(const uint16_t *pixels,size_t pixel_count,
    unsigned width,unsigned height,unsigned stride,kui_screenshot_write_fn write,
    void *context);

/* Menu/runtime only. Caller owns an immutable frame and exclusive access to
 * the mounted selected FatFs volume. Does not mount/unmount, poll hardware,
 * or retain the frame. Creates a new numbered BMP, with sync/close before
 * publication. Existing captures and unfinished files are never overwritten.
 * path is cleared on failure, including an uncertain close/rename result.
 * Interrupted saves may leave a .part file, which is never reported saved. */
bool kui_screenshot_save_rgb565(const uint16_t *pixels,size_t pixel_count,
    unsigned width,unsigned height,unsigned stride,
    char path[KUI_SCREENSHOT_PATH_CAP],kui_log_fn log);
#endif
