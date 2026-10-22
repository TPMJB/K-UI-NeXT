/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_IMAGE_CLIENT_H
#define KUI_IMAGE_CLIENT_H

#include <stdint.h>

#define KUI_IMAGE_CLIENT_VERSION 1u
#define KUI_IMAGE_CLIENT_TRACKS 99u
#define KUI_IMAGE_CLIENT_SAMPLES 16u
#define KUI_IMAGE_CLIENT_MAX_SECTORS 4u
#define KUI_IMAGE_CLIENT_SERVER_STEPS 128u

/* Resident-owned reference data copied from the validated launch manifest.
 * LBAs are zero based; the BIOS request interface uses FAD = LBA + 150.
 * crc32 is over count * sector_bytes bytes read by the launcher from the
 * selected image, before handoff. The client computes its own CRC32 over the
 * bytes returned through the GD BIOS vector after handoff. */
struct kui_image_client_sample {
    uint32_t lba, count, sector_bytes, crc32;
};
struct kui_image_client_track {
    uint32_t number, control, start_lba, end_lba;
};
struct kui_image_client_api {
    uint32_t version, bytes;
    uint32_t track_count;
    const struct kui_image_client_track *tracks;
    uint32_t sample_count;
    const struct kui_image_client_sample *samples;
    /* These exports only display results and observe physical card traffic.
     * Disc status, TOC and all sector reads use the actual GD BIOS vector. */
    void (*report)(const char *label, uint32_t pass, uint32_t detail);
    uint32_t (*read_count)(void);
};

/* Independently linked low-RAM executable entry; zero means all tests passed. */
uint32_t kui_image_client_main(const struct kui_image_client_api *);

/* Native implementation calls *(0x8c0000bc), with r6 = 0 and r7 = function.
 * Host tests supply this exact symbol to model the BIOS boundary. uintptr_t
 * keeps pointer arguments intact on both the 32-bit target and 64-bit host. */
int32_t kui_image_client_gd_call(uint32_t function, uintptr_t arg0,
                               uintptr_t arg1);
#ifndef KUI_ON_CONSOLE
/* Host-only address translation for pointers inside 32-bit BIOS structures. */
uint32_t kui_image_client_host_address(const void *);
#endif

#endif
