/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/pvr_texture.h"

#include <string.h>

/* Original K-UI implementation. Format references, not imported code, all at
 * KallistiOS fcfa7d869471591ca1c777543261a7bfea7cb726:
 * utils/pvrtex/file_pvr.c: the optional GBIX chunk, the PVRT chunk (type
 *   bytes, 16-bit width/height), layout codes, small-VQ codebook sizes, and
 *   locating a mipmapped texture's largest level from the end of its chunk.
 * utils/pvrtex/pvr_texture.c: Morton (twiddled) order with y in the even
 *   bits, a rectangle's extra bits above the shared ones, and VQ expanding each
 *   index into one 8-byte codebook entry of four consecutive twiddled texels.
 * kernel/arch/dreamcast/include/dc/pvr.h: the 16-bit pixel formats. */

enum {
    LAYOUT_SQUARE = 1, LAYOUT_SQUARE_MIP = 2, LAYOUT_VQ = 3, LAYOUT_VQ_MIP = 4,
    LAYOUT_STRIDE = 9, LAYOUT_RECT = 13, LAYOUT_SMALL_VQ = 16,
    LAYOUT_SMALL_VQ_MIP = 17, LAYOUT_SQUARE_MIP_ALT = 18
};
enum { FORMAT_ARGB1555 = 0, FORMAT_RGB565 = 1, FORMAT_ARGB4444 = 2, FORMAT_PAL8 = 6 };

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static unsigned le16(const uint8_t *p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }
static bool power_of_two(unsigned v) { return v && !(v & (v - 1u)); }
static unsigned log2_of(unsigned v) {
    unsigned n = 0;
    while(v > 1u) { v >>= 1; ++n; }
    return n;
}
/* Small VQ keeps fewer codebook entries for small textures. */
static unsigned small_codebook(unsigned edge, bool mipmaps) {
    if(edge <= 16u) return 16u;
    if(edge <= 32u) return mipmaps ? 64u : 32u;
    if(edge <= 64u) return mipmaps ? 256u : 128u;
    return 256u;
}

enum kui_pvr_status kui_pvr_parse(const uint8_t *file, size_t size, struct kui_pvr_info *out) {
    if(!out) return KUI_PVR_NOT_PVR;
    memset(out, 0, sizeof(*out));
    if(!file || size < 8u) return KUI_PVR_NOT_PVR;
    size_t at = 0;
    if(!memcmp(file, "GBIX", 4)) {
        uint32_t skip = le32(file + 4);
        if(skip > size - 8u) return KUI_PVR_TRUNCATED;
        at = 8u + skip;
    }
    if(size - at < 4u || memcmp(file + at, "PVRT", 4)) return KUI_PVR_NOT_PVR;
    if(size - at < 16u) return KUI_PVR_TRUNCATED;
    uint32_t length = le32(file + at + 4);
    if(length < 8u) return KUI_PVR_TRUNCATED;
    bool whole = length <= size - at - 8u;
    size_t data = at + 16u, end = whole ? at + 8u + length : size;
    struct kui_pvr_info info = {0};
    info.pixel_format = file[at + 8];
    info.layout = file[at + 9];
    info.width = le16(file + at + 12);
    info.height = le16(file + at + 14);
    unsigned w = info.width, h = info.height, format = info.pixel_format;
    switch(info.layout) {
    case LAYOUT_SQUARE: info.twiddled = true; break;
    case LAYOUT_SQUARE_MIP: case LAYOUT_SQUARE_MIP_ALT: info.twiddled = info.mipmaps = true; break;
    case LAYOUT_VQ: case LAYOUT_SMALL_VQ: info.twiddled = info.vq = true; break;
    case LAYOUT_VQ_MIP: case LAYOUT_SMALL_VQ_MIP: info.twiddled = info.vq = info.mipmaps = true; break;
    case LAYOUT_RECT: info.twiddled = true; break;
    case LAYOUT_STRIDE: break;
    default: return KUI_PVR_UNSUPPORTED; /* Palettized or not a hardware layout. */
    }
    /* KallistiOS's loader reads 8-bit "palette" data in these direct layouts
     * as ARGB1555 (seen in Headhunter); the palette layouts stay rejected. */
    if(format == FORMAT_PAL8) format = FORMAT_ARGB1555;
    if(format != FORMAT_ARGB1555 && format != FORMAT_RGB565 && format != FORMAT_ARGB4444)
        return KUI_PVR_UNSUPPORTED;
    info.pixel_format = (uint8_t)format;
    if(w < KUI_PVR_MIN_EDGE || h < KUI_PVR_MIN_EDGE || w > KUI_PVR_MAX_EDGE || h > KUI_PVR_MAX_EDGE)
        return KUI_PVR_SIZE;
    if(info.twiddled && (!power_of_two(w) || !power_of_two(h))) return KUI_PVR_SIZE;
    if(info.layout != LAYOUT_RECT && info.layout != LAYOUT_STRIDE && w != h) return KUI_PVR_SIZE;
    size_t top = info.vq ? (size_t)(w / 2u) * (h / 2u) : (size_t)w * h * 2u;
    size_t codebook = 0;
    if(info.vq) {
        info.codebook_entries = info.layout == LAYOUT_SMALL_VQ || info.layout == LAYOUT_SMALL_VQ_MIP ?
            small_codebook(w, info.mipmaps) : 256u;
        codebook = (size_t)info.codebook_entries * 8u;
        info.codebook_offset = data;
    }
    if(info.mipmaps) {
        /* Writers pad the smallest levels differently; the largest level
         * always ends the chunk, so an incomplete chunk cannot be placed. */
        if(!whole || end - data < codebook + top) return KUI_PVR_TRUNCATED;
        info.data_offset = end - top;
    } else {
        if(end - data < codebook + top) return KUI_PVR_TRUNCATED;
        info.data_offset = data + codebook;
    }
    *out = info;
    return KUI_PVR_OK;
}

/* Low bits interleaved with y first; a rectangle's remaining bits follow. */
static uint32_t spread(uint32_t v) {
    v = (v | v << 8) & 0x00ff00ffu;
    v = (v | v << 4) & 0x0f0f0f0fu;
    v = (v | v << 2) & 0x33333333u;
    return (v | v << 1) & 0x55555555u;
}
static uint32_t twiddled(unsigned x, unsigned y, unsigned shared) {
    uint32_t mask = (1u << shared) - 1u;
    return spread(y & mask) | spread(x & mask) << 1 | (uint32_t)((x | y) >> shared) << (2u * shared);
}
static void expand(unsigned texel, unsigned format, uint8_t *px) {
    if(format == FORMAT_RGB565) {
        unsigned r = texel >> 11 & 31u, g = texel >> 5 & 63u, b = texel & 31u;
        px[0] = (uint8_t)(r << 3 | r >> 2); px[1] = (uint8_t)(g << 2 | g >> 4);
        px[2] = (uint8_t)(b << 3 | b >> 2); px[3] = 255u;
    } else if(format == FORMAT_ARGB1555) {
        unsigned r = texel >> 10 & 31u, g = texel >> 5 & 31u, b = texel & 31u;
        px[0] = (uint8_t)(r << 3 | r >> 2); px[1] = (uint8_t)(g << 3 | g >> 2);
        px[2] = (uint8_t)(b << 3 | b >> 2); px[3] = texel & 0x8000u ? 255u : 0u;
    } else {
        px[0] = (uint8_t)((texel >> 8 & 15u) * 17u); px[1] = (uint8_t)((texel >> 4 & 15u) * 17u);
        px[2] = (uint8_t)((texel & 15u) * 17u); px[3] = (uint8_t)((texel >> 12 & 15u) * 17u);
    }
}

static bool same(const struct kui_pvr_info *a, const struct kui_pvr_info *b) {
    return a->width == b->width && a->height == b->height &&
        a->pixel_format == b->pixel_format && a->layout == b->layout &&
        a->twiddled == b->twiddled && a->vq == b->vq && a->mipmaps == b->mipmaps &&
        a->codebook_entries == b->codebook_entries &&
        a->codebook_offset == b->codebook_offset && a->data_offset == b->data_offset;
}
enum kui_pvr_status kui_pvr_decode(const uint8_t *file, size_t size,
    const struct kui_pvr_info *info, uint8_t *rgba) {
    struct kui_pvr_info check;
    enum kui_pvr_status status = kui_pvr_parse(file, size, &check);
    if(status != KUI_PVR_OK) return status;
    if(!info || !rgba || !same(&check, info)) return KUI_PVR_NOT_PVR;
    unsigned w = info->width, h = info->height, format = info->pixel_format;
    unsigned shared = log2_of(w < h ? w : h);
    const uint8_t *texels = file + info->data_offset, *codebook = file + info->codebook_offset;
    bool alpha = false;
    for(unsigned y = 0; y < h; ++y) {
        for(unsigned x = 0; x < w; ++x) {
            unsigned texel = 0;
            if(info->vq) {
                uint32_t t = twiddled(x, y, shared);
                unsigned entry = texels[t >> 2];
                /* Entries past a small codebook decode as zero, as a loader
                 * with a zero-filled 256-entry table would. */
                if(entry < info->codebook_entries) texel = le16(codebook + entry * 8u + (t & 3u) * 2u);
            } else {
                size_t t = info->twiddled ? twiddled(x, y, shared) : (size_t)y * w + x;
                texel = le16(texels + t * 2u);
            }
            uint8_t *px = rgba + ((size_t)y * w + x) * 4u;
            expand(texel, format, px);
            if(px[3]) alpha = true;
        }
    }
    if(!alpha) for(size_t i = 0; i < (size_t)w * h; ++i) rgba[i * 4u + 3u] = 255u;
    return KUI_PVR_OK;
}

const char *kui_pvr_status_text(enum kui_pvr_status status) {
    switch(status) {
    case KUI_PVR_OK: return "Texture decoded";
    case KUI_PVR_NOT_PVR: return "Not a PVR texture";
    case KUI_PVR_TRUNCATED: return "Texture file is incomplete";
    case KUI_PVR_UNSUPPORTED: return "Unsupported texture format";
    case KUI_PVR_SIZE: return "Unsupported texture size";
    default: return "Unknown texture result";
    }
}
