/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_GAME_COVER_H
#define KUI_GAME_COVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Box art prepared once by the Games artwork scan, in three square RGB565
 * sizes: the List view and image details, the Gallery, and the Compact view.
 * One record per game lives in KUI/covers/<game>.kcv, named after the game's
 * folder (or its GDI without .gdi when several share a folder). */
#define KUI_COVER_LARGE 160u
#define KUI_COVER_MEDIUM 104u
#define KUI_COVER_SMALL 56u
#define KUI_COVER_HEADER_BYTES 512u
#define KUI_COVER_TITLE_CAP 129u
#define KUI_COVER_PATH_CAP 256u
#define KUI_COVER_PIXELS (KUI_COVER_LARGE * KUI_COVER_LARGE)
#define KUI_COVER_FILE_BYTES (KUI_COVER_HEADER_BYTES + 2u * (KUI_COVER_LARGE * KUI_COVER_LARGE + \
    KUI_COVER_MEDIUM * KUI_COVER_MEDIUM + KUI_COVER_SMALL * KUI_COVER_SMALL))
/* Composited behind transparent texels and around art that is not square:
 * the launcher's navy background (RGB 8,15,35). */
#define KUI_COVER_BACKGROUND 0x0864u

enum kui_cover_size { KUI_COVER_SIZE_LARGE, KUI_COVER_SIZE_MEDIUM, KUI_COVER_SIZE_SMALL };
enum kui_cover_source { KUI_COVER_SOURCE_NONE, KUI_COVER_SOURCE_DISC, KUI_COVER_SOURCE_USER };
/* Size and FAT date/time of a file when the record was made; a change means
 * the record is out of date. A user stamp can be set with disc art or none:
 * the owner's image existed but could not be used. */
struct kui_cover_stamp { uint64_t bytes; uint16_t date, time; };
struct kui_cover_record {
    /* The record belongs to this card-root GDI path and no other. */
    char gdi_path[KUI_COVER_PATH_CAP];
    char title[KUI_COVER_TITLE_CAP], product[11], version[7], region[9];
    struct kui_cover_stamp gdi, user;
    enum kui_cover_source source; /* NONE: title only, no pixels follow. */
};

unsigned kui_cover_edge(enum kui_cover_size size);
/* Byte offset of one size's pixels (little-endian RGB565, row-major). */
size_t kui_cover_offset(enum kui_cover_size size);
/* Fixed little-endian version 1 with a CRC32 over the header. Strings must be
 * printable ASCII and terminated; anything else is rejected, never repaired. */
bool kui_cover_header_encode(uint8_t out[KUI_COVER_HEADER_BYTES], const struct kui_cover_record *record);
bool kui_cover_header_decode(struct kui_cover_record *record, const uint8_t in[KUI_COVER_HEADER_BYTES]);
/* The record name for a Games list entry: a folder keeps its name, a GDI
 * loses its extension. False if the result is not a safe file name. */
bool kui_cover_key(const char *entry_name, char out[KUI_COVER_PATH_CAP]);
/* Text for lists: the disc title with runs of spaces collapsed, or the
 * entry's own name when the title is empty or has unreadable characters. */
void kui_cover_display_title(const char *title, const char *fallback, char out[KUI_COVER_TITLE_CAP]);

/* The largest square kui_cover_scale fills: box art, and the File Manager's
 * picture view. */
#define KUI_COVER_SCALE_MAX 512u
/* Fits an image inside edge*edge (edge at most KUI_COVER_SCALE_MAX), centred
 * on KUI_COVER_BACKGROUND, keeping its shape: area-averaged when shrinking,
 * bilinear when enlarging, alpha composited. Pixels are 8-bit RGB (3
 * channels) or RGBA (4), row-major. */
bool kui_cover_scale(const uint8_t *pixels, unsigned width, unsigned height, unsigned channels,
                     uint16_t *out, unsigned edge);
/* Halves a very large image in place (2x2 averages) until it is at most
 * about four times the size the largest cover needs, so scaling stays quick. */
void kui_cover_reduce(uint8_t *pixels, unsigned *width, unsigned *height, unsigned channels);

#endif
