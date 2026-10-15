/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_CDDA_AUDIO64_PROFILE_H
#define KUI_CDDA_AUDIO64_PROFILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Authored interface metadata from owner-supplied drivers. No driver bytes
 * are embedded here. Offsets are ARM sound-RAM offsets, without the host G2
 * alias. A profile identifies a driver contract; it does not reserve ports,
 * allocate sound memory, establish a service deadline, or authorize patches. */
struct kui_cdda_audio64_range {
    uint32_t start, end, crc32;
};

enum kui_cdda_audio64_id {
    KUI_CDDA_AUDIO64_TOY_180D2 = 1,
    KUI_CDDA_AUDIO64_MDK2_190B1 = 2
};

struct kui_cdda_audio64_profile {
    enum kui_cdda_audio64_id id;
    const char *version;
    uint32_t file_bytes, file_crc32;
    uint8_t file_sha256[32];
    uint32_t queue_offset, notify_offset;
    uint32_t playing_offset, current_offset, previous_offset;
    uint32_t template_offset, default_template_offset;
    uint32_t hardware_base_word_offset, hardware_channel_base;
    uint32_t first_free_offset;
    const struct kui_cdda_audio64_range *ranges;
    size_t range_count;
};

/* Ranges exclude identified mutable headers, templates, cursors and inline
 * work words. The full file digest is suitable before installation; driver
 * initialization makes a full installed-image digest unsuitable. */
static const struct kui_cdda_audio64_range kui_cdda_audio64_toy_ranges[] = {
    {0x0000u, 0x0020u, UINT32_C(0x81ec56e2)},
    {0x0100u, 0x0188u, UINT32_C(0x4edae6a3)},
    {0x1af0u, 0x1b38u, UINT32_C(0x09dfe1c1)},
    {0x39a0u, 0x39fcu, UINT32_C(0x760de545)},
    {0x3a08u, 0x3c84u, UINT32_C(0x353d913f)},
    {0x4338u, 0x456cu, UINT32_C(0xc36eea6f)},
    {0x4570u, 0x46d0u, UINT32_C(0x876af872)},
    {0x4720u, 0x47c8u, UINT32_C(0x44871c65)},
    {0x4aa8u, 0x4bdcu, UINT32_C(0x786f7c6c)},
    {0x4be4u, 0x4cb4u, UINT32_C(0x1bd0f36a)},
    {0x4d3cu, 0x4d64u, UINT32_C(0xb6ec0894)},
    {0x0020u, 0x00c0u, UINT32_C(0xf89d79c5)}
};

static const struct kui_cdda_audio64_range kui_cdda_audio64_mdk2_ranges[] = {
    {0x0000u, 0x0020u, UINT32_C(0x81ec56e2)},
    {0x0100u, 0x0188u, UINT32_C(0x199732e5)},
    {0x1b30u, 0x1b78u, UINT32_C(0x09dfe1c1)},
    {0x39e0u, 0x3a3cu, UINT32_C(0x760de545)},
    {0x3a48u, 0x3cc4u, UINT32_C(0x519d0773)},
    {0x4378u, 0x45b8u, UINT32_C(0x557749b0)},
    {0x45bcu, 0x471cu, UINT32_C(0x2380d0e0)},
    {0x476cu, 0x4814u, UINT32_C(0xa335b71e)},
    {0x4af4u, 0x4c2cu, UINT32_C(0x1516a8f5)},
    {0x4c34u, 0x4d04u, UINT32_C(0x598e669b)},
    {0x4d8cu, 0x4dd4u, UINT32_C(0x50e376da)},
    {0x0020u, 0x00c0u, UINT32_C(0x6399976b)}
};

static const struct kui_cdda_audio64_profile kui_cdda_audio64_toy_180d2 = {
    .id = KUI_CDDA_AUDIO64_TOY_180D2,
    .version = "1.80d.2",
    .file_bytes = 20740u,
    .file_crc32 = UINT32_C(0x70cceeb2),
    .file_sha256 = {
        0x47,0x7e,0xde,0x37,0x66,0xc2,0x7f,0xa5,
        0x8e,0x4c,0x14,0xd5,0x21,0x8c,0x58,0x3b,
        0x78,0x06,0xa2,0x9c,0x32,0x8a,0x6e,0x0d,
        0x49,0x65,0x29,0x33,0x75,0xb7,0x04,0xe5
    },
    .queue_offset = 0xb200u, .notify_offset = 0xb400u,
    .playing_offset = 0x1468u, .current_offset = 0x14f0u,
    .previous_offset = 0x15f0u,
    .template_offset = 0x1b38u, .default_template_offset = 0x1af0u,
    .hardware_base_word_offset = 0x1464u,
    .hardware_channel_base = 0x00800000u,
    .first_free_offset = 0x30040u,
    .ranges = kui_cdda_audio64_toy_ranges,
    .range_count = sizeof(kui_cdda_audio64_toy_ranges) /
                   sizeof(kui_cdda_audio64_toy_ranges[0])
};

static const struct kui_cdda_audio64_profile kui_cdda_audio64_mdk2_190b1 = {
    .id = KUI_CDDA_AUDIO64_MDK2_190B1,
    .version = "1.90b.1",
    .file_bytes = 20824u,
    .file_crc32 = UINT32_C(0xe6e90572),
    .file_sha256 = {
        0xaf,0x8f,0x90,0x44,0x0e,0xdc,0xea,0xcf,
        0x33,0xa9,0x0d,0x09,0x4c,0x9b,0x57,0x0d,
        0x8d,0x40,0xc2,0x77,0x4a,0x5f,0x60,0x67,
        0x95,0x32,0x77,0x85,0x9a,0x6b,0xa4,0x13
    },
    .queue_offset = 0xb200u, .notify_offset = 0xb400u,
    .playing_offset = 0x1468u, .current_offset = 0x1530u,
    .previous_offset = 0x1630u,
    .template_offset = 0x1b78u, .default_template_offset = 0x1b30u,
    .hardware_base_word_offset = 0x1464u,
    .hardware_channel_base = 0x00800000u,
    .first_free_offset = 0x30040u,
    .ranges = kui_cdda_audio64_mdk2_ranges,
    .range_count = sizeof(kui_cdda_audio64_mdk2_ranges) /
                   sizeof(kui_cdda_audio64_mdk2_ranges[0])
};

/* The caller chooses and actually reserves this adjacent stereo pair. In
 * particular MDK2's candidate 61/62 does not presume ownership of port 63. */
static inline bool kui_cdda_audio64_pair_valid(uint32_t first, uint32_t second) {
    return first < 63u && second == first + 1u;
}

static inline bool kui_cdda_audio64_pair_masks(uint32_t first, uint32_t second,
                                             uint32_t *upper, uint32_t *lower) {
    if(!upper || !lower || upper == lower ||
       !kui_cdda_audio64_pair_valid(first, second)) return false;
    uint32_t high = 0u, low = 0u;
    if(first < 32u) low |= UINT32_C(1) << first;
    else high |= UINT32_C(1) << (first - 32u);
    if(second < 32u) low |= UINT32_C(1) << second;
    else high |= UINT32_C(1) << (second - 32u);
    *upper = high; *lower = low;
    return true;
}

/* Invalid input returns UINT32_MAX, outside every admitted sound-RAM span. */
static inline uint32_t kui_cdda_audio64_playing(
        const struct kui_cdda_audio64_profile *profile, uint32_t port) {
    return profile && port < 64u ? profile->playing_offset + port : UINT32_MAX;
}
static inline uint32_t kui_cdda_audio64_cursor(
        const struct kui_cdda_audio64_profile *profile, uint32_t port) {
    return profile && port < 64u ? profile->current_offset + 4u * port : UINT32_MAX;
}
static inline uint32_t kui_cdda_audio64_previous(
        const struct kui_cdda_audio64_profile *profile, uint32_t port) {
    return profile && port < 64u ? profile->previous_offset + 4u * port : UINT32_MAX;
}
static inline uint32_t kui_cdda_audio64_template(
        const struct kui_cdda_audio64_profile *profile, uint32_t port) {
    return profile && port < 64u ? profile->template_offset + 72u * port : UINT32_MAX;
}

#endif
