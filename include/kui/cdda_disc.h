/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_CDDA_DISC_H
#define KUI_CDDA_DISC_H
#include "kui/game_image.h"
#include "kui/cdda_pcm.h"
#include "kui/gd_service.h"

#define KUI_CDDA_DISC_TRACK_MAX 99u
#define KUI_CDDA_DISC_NAME_CAP 128u
#define KUI_CDDA_DISC_TOC_BYTES 408u
#define KUI_CDDA_DISC_HIGH_FAD 45150u

enum kui_cdda_disc_result { KUI_CDDA_DISC_OK, KUI_CDDA_DISC_INVALID,
    KUI_CDDA_DISC_UNSUPPORTED, KUI_CDDA_DISC_RANGE, KUI_CDDA_DISC_GAP,
    KUI_CDDA_DISC_OVERLAP, KUI_CDDA_DISC_FILE_SIZE };
struct kui_cdda_disc_track {
    uint32_t number,start_fad,end_fad,control,stride,data_offset,file_bytes,file_offset;
    char name[KUI_CDDA_DISC_NAME_CAP];
};
/* Caller retains the map unchanged throughout queue/PCM use. complete means
 * every represented image track has validated backing metadata. A selected
 * audio-only map contains one track and cannot produce a complete TOC. */
struct kui_cdda_disc_map {
    uint32_t count;
    bool complete,cd_image;
    struct kui_cdda_disc_track tracks[KUI_CDDA_DISC_TRACK_MAX];
};
struct kui_cdda_disc_audio {
    uint32_t track,first_frame,end_frame;
    struct kui_cdda_pcm_source source; /* Explicit little-endian PCM. */
};
struct kui_cdda_disc_data {
    uint32_t track,offset,bytes; /* Track-relative virtual 2048-byte payload. */
    uint32_t file_offset,stride,data_offset;
};
/* Pure metadata builders: no callbacks/I/O. Only cooked2048 or raw2352 Mode1
 * data and raw2352 audio are supported. All offsets and lengths fit32 bits.
 * Output remains unchanged on failure. */
enum kui_cdda_disc_result kui_cdda_disc_from_image(const struct kui_game_image *,struct kui_cdda_disc_map *);
enum kui_cdda_disc_result kui_cdda_disc_selected_audio(const void *gdi,size_t bytes,
    uint32_t number,uint64_t actual_file_bytes,struct kui_cdda_disc_map *);
enum kui_cdda_disc_result kui_cdda_disc_validate(const struct kui_cdda_disc_map *);
enum kui_cdda_disc_result kui_cdda_disc_track(const struct kui_cdda_disc_map *,uint32_t number,
    const struct kui_cdda_disc_track **);
/* Ranges are FAD[first,end), wholly within one backed track. No track crossing
 * or gap filling. Incomplete maps refuse missing tracks/extents as unsupported. */
enum kui_cdda_disc_result kui_cdda_disc_audio_range(const struct kui_cdda_disc_map *,
    uint32_t first_fad,uint32_t end_fad,struct kui_cdda_disc_audio *);
enum kui_cdda_disc_result kui_cdda_disc_data_range(const struct kui_cdda_disc_map *,
    uint32_t first_fad,uint32_t count,struct kui_cdda_disc_data *);
/* Derived metadata, not a captured BIOS TOC. area0=low/CD,area1=GD high;
 * unused entries areffffffff,ADR1,control0/4,leadout=last backed endFAD.
 * Refused for selected/incomplete maps or an empty area. */
enum kui_cdda_disc_result kui_cdda_disc_toc(const struct kui_cdda_disc_map *,uint32_t area,
    uint8_t output[KUI_CDDA_DISC_TOC_BYTES]);
#endif
