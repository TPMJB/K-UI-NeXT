/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_COVER_IMAGE_H
#define KUI_COVER_IMAGE_H

#include <stddef.h>
#include <stdint.h>

/* Owner-supplied box art: PNG or baseline/progressive JPEG, decoded in RAM
 * by the pinned stb_image. The limits keep one decode well inside console
 * memory; box art larger than this should be resized on a PC first. */
#define KUI_COVER_IMAGE_MAX_BYTES (3u * 1024u * 1024u)
#define KUI_COVER_IMAGE_MAX_PIXELS 1200000u

enum kui_cover_image_status {
    KUI_COVER_IMAGE_OK, KUI_COVER_IMAGE_UNSUPPORTED, KUI_COVER_IMAGE_TOO_LARGE,
    KUI_COVER_IMAGE_CORRUPT, KUI_COVER_IMAGE_MEMORY
};
/* On success *pixels holds width*height pixels of 8-bit RGB (channels 3) or
 * RGBA (channels 4, for images with transparency), row-major; release it
 * with kui_cover_image_free. Outputs are cleared on failure. */
enum kui_cover_image_status kui_cover_image_decode(const uint8_t *data, size_t size,
    uint8_t **pixels, unsigned *width, unsigned *height, unsigned *channels);
void kui_cover_image_free(uint8_t *pixels);
const char *kui_cover_image_status_text(enum kui_cover_image_status status);

#endif
