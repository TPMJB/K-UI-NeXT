/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/cover_image.h"

#include <limits.h>
#include <string.h>

/* stb_image (MIT alternative A; see third_party/stb/README.md) is built with
 * only its PNG and JPEG readers, from memory, with no stdio or HDR support. */
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_SIMD
#define STBI_MAX_DIMENSIONS 8192
#define STB_IMAGE_IMPLEMENTATION
#include "../../third_party/stb/stb_image.h"

static enum kui_cover_image_status failed(uint8_t **pixels, unsigned *width, unsigned *height,
                                          unsigned *channels, enum kui_cover_image_status status) {
    if(pixels) *pixels = NULL;
    if(width) *width = 0;
    if(height) *height = 0;
    if(channels) *channels = 0;
    return status;
}
enum kui_cover_image_status kui_cover_image_decode(const uint8_t *data, size_t size,
    uint8_t **pixels, unsigned *width, unsigned *height, unsigned *channels) {
    if(!pixels || !width || !height || !channels)
        return failed(pixels, width, height, channels, KUI_COVER_IMAGE_CORRUPT);
    failed(pixels, width, height, channels, KUI_COVER_IMAGE_OK);
    static const uint8_t png[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    if(!data || size < 8u || (memcmp(data, png, 8) && (data[0] != 0xffu || data[1] != 0xd8u || data[2] != 0xffu)))
        return KUI_COVER_IMAGE_UNSUPPORTED;
    if(size > KUI_COVER_IMAGE_MAX_BYTES || size > (size_t)INT_MAX) return KUI_COVER_IMAGE_TOO_LARGE;
    int w = 0, h = 0, components = 0;
    if(!stbi_info_from_memory(data, (int)size, &w, &h, &components) || w <= 0 || h <= 0)
        return KUI_COVER_IMAGE_CORRUPT;
    if((uint64_t)w * (uint64_t)h > KUI_COVER_IMAGE_MAX_PIXELS) return KUI_COVER_IMAGE_TOO_LARGE;
    int want = components == 2 || components == 4 ? 4 : 3, got = 0;
    stbi_uc *out = stbi_load_from_memory(data, (int)size, &w, &h, &got, want);
    if(!out) {
        const char *reason = stbi_failure_reason();
        return reason && !strcmp(reason, "outofmem") ? KUI_COVER_IMAGE_MEMORY : KUI_COVER_IMAGE_CORRUPT;
    }
    *pixels = out;
    *width = (unsigned)w;
    *height = (unsigned)h;
    *channels = (unsigned)want;
    return KUI_COVER_IMAGE_OK;
}
void kui_cover_image_free(uint8_t *pixels) { stbi_image_free(pixels); }
const char *kui_cover_image_status_text(enum kui_cover_image_status status) {
    switch(status) {
    case KUI_COVER_IMAGE_OK: return "Image decoded";
    case KUI_COVER_IMAGE_UNSUPPORTED: return "Not a PNG or JPEG image";
    case KUI_COVER_IMAGE_TOO_LARGE: return "Image larger than 1.2 megapixels or 3 MB";
    case KUI_COVER_IMAGE_CORRUPT: return "Image data is damaged or unsupported";
    case KUI_COVER_IMAGE_MEMORY: return "Not enough memory to decode the image";
    default: return "Unknown image result";
    }
}
