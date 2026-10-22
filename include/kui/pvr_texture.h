/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_PVR_TEXTURE_H
#define KUI_PVR_TEXTURE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Sega .PVR texture files, as games ship their disc artwork (0GDTEX.PVR).
 * Only the largest level of 16-bit ARGB1555/RGB565/ARGB4444 textures is
 * decoded: square or rectangular twiddled, stride (linear) and VQ layouts,
 * with or without mipmaps. Palettized, YUV and bump textures are reported
 * unsupported rather than guessed. */
#define KUI_PVR_MIN_EDGE 8u
#define KUI_PVR_MAX_EDGE 1024u

enum kui_pvr_status {
    KUI_PVR_OK, KUI_PVR_NOT_PVR, KUI_PVR_TRUNCATED, KUI_PVR_UNSUPPORTED, KUI_PVR_SIZE
};
struct kui_pvr_info {
    unsigned width, height;
    uint8_t pixel_format, layout; /* The file's own type bytes. */
    bool twiddled, vq, mipmaps;
    unsigned codebook_entries;    /* VQ only. */
    size_t codebook_offset;       /* VQ only: byte offset in the file. */
    size_t data_offset;           /* Largest level's texels or indices. */
};

/* Validates the chunk chain (an optional GBIX chunk, then PVRT) and locates
 * the largest level without decoding it. out is zeroed on failure. */
enum kui_pvr_status kui_pvr_parse(const uint8_t *file, size_t size, struct kui_pvr_info *out);
/* Decodes the largest level parsed from the same bytes into width*height
 * RGBA pixels (four bytes each, row-major). A texture whose alpha is zero
 * everywhere is treated as opaque: disc art is not meant to be invisible. */
enum kui_pvr_status kui_pvr_decode(const uint8_t *file, size_t size,
    const struct kui_pvr_info *info, uint8_t *rgba);
const char *kui_pvr_status_text(enum kui_pvr_status status);

#endif
