/* SPDX-License-Identifier: GPL-3.0-only */
#include "display.h"
#include "sd_reader.h"
#include "kui/gd_service.h"
#include "kui/image_client.h"
#include "kui/image_loader_layout.h"
#include "kui/resident_image.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

extern const uint8_t __client_image_start[] __asm__("__client_image_start");
extern const uint8_t __client_image_end[] __asm__("__client_image_end");
extern uint8_t __bss_end[] __asm__("__bss_end");
extern int32_t kui_image_gd_hook(uint32_t, uint32_t, uint32_t, uint32_t);
extern void kui_image_display_title(const char *);

/* All live state belongs to the independently linked resident. In particular,
 * the large decoded extent map lives in BSS, never on a 64 KiB stack. */
static struct kui_resident_manifest manifest;
static struct kui_resident_image image;
static struct kui_gd_service service;
static struct kui_loader_sd card;
static struct kui_gd_track tracks[KUI_GD_TRACK_MAX];
static struct kui_image_client_track client_tracks[KUI_IMAGE_CLIENT_TRACKS];
static struct kui_image_client_sample samples[KUI_IMAGE_CLIENT_SAMPLES];
static enum kui_loader_sd_result last_sd_result;

static int read_block(void *context, uint32_t lba, uint8_t output[512]) {
    last_sd_result = kui_loader_sd_read(context, lba, 1, output);
    return last_sd_result == KUI_LOADER_SD_OK ? 0 : -1;
}
static uint8_t *map_guest(void *context, uint32_t address, uint32_t bytes,
                          int writing) {
    (void)context;
    (void)writing;
    /* The portable service normalizes P1/P2/physical aliases first. Keep a
     * second native boundary check before converting a guest integer pointer. */
    if(address < KUI_IMAGE_CLIENT_ADDRESS || address >= KUI_IMAGE_RESIDENT_ADDRESS ||
       bytes > KUI_IMAGE_RESIDENT_ADDRESS - address) return NULL;
    return (uint8_t *)(uintptr_t)address;
}
static int check_sectors(void *context, uint32_t lba, uint32_t count,
                         uint32_t bytes) {
    (void)context;
    if(bytes != KUI_GAME_DATA_BYTES && bytes != KUI_GAME_RAW_BYTES) return -1;
    enum kui_game_sector_format format = bytes == KUI_GAME_RAW_BYTES ?
        KUI_GAME_SECTOR_RAW : KUI_GAME_SECTOR_MODE1;
    return kui_resident_image_check(&manifest, lba, count, format) == KUI_GAME_OK ? 0 : -1;
}
static int read_sectors(void *context, uint32_t lba, uint32_t count,
                        uint32_t bytes, void *output) {
    (void)context;
    if(bytes != KUI_GAME_DATA_BYTES && bytes != KUI_GAME_RAW_BYTES) return -1;
    enum kui_game_sector_format format = bytes == KUI_GAME_RAW_BYTES ?
        KUI_GAME_SECTOR_RAW : KUI_GAME_SECTOR_MODE1;
    return kui_resident_image_read(&image, lba, count, format, output,
                                  (size_t)count * bytes) == KUI_GAME_OK ? 0 : -1;
}
static uint32_t read_count(void) { return image.blocks_read; }

/* Assembly preserves the SH-4 callee-saved registers and uses our dedicated
 * stack before arriving here. Only the actual GD superfunction is served;
 * unsupported MISC calls are explicitly rejected in this own-client probe. */
int32_t kui_image_gd_dispatch(uint32_t r4, uint32_t r5, uint32_t r6, uint32_t r7) {
    return kui_gd_service_dispatch(&service, r4, r5, r6, r7);
}

static void erase_range(uintptr_t start, uintptr_t end) {
    volatile uint32_t *p = (volatile uint32_t *)start;
    while((uintptr_t)p < end) *p++ = 0;
}
static void erase_retired_runtime(void) {
    /* Preserve only firmware's low 64 KiB, this resident/BSS and main stack.
     * The separately reserved GD hook stack is initialized after this erase.
     * No prior shell allocation or callback can provide image data later. */
    erase_range(KUI_IMAGE_CLIENT_ADDRESS, KUI_IMAGE_RESIDENT_ADDRESS);
    erase_range((uintptr_t)__bss_end, KUI_IMAGE_RESIDENT_MEMORY_END);
    erase_range(KUI_IMAGE_RESIDENT_STACK, 0x8d000000u);
}
static void display_sd_failure(void) {
    kui_loader_display_line(kui_loader_sd_result_name(last_sd_result));
    kui_loader_display_number("Last command: ", card.last_command);
    kui_loader_display_number("Last response: ", card.last_response);
}

void kui_image_resident_main(const uint8_t wire[KUI_IMAGE_MANIFEST_BYTES]) {
    kui_loader_display_init();
    kui_loader_display_line("Resident entered; launcher has shut down.");
    enum kui_game_result result = kui_resident_manifest_decode(wire, &manifest);
    if(result != KUI_GAME_OK) {
        kui_loader_display_result("Selected image manifest", 0, (uint32_t)result);
        kui_loader_display_summary(1, 0);
        return;
    }
    kui_image_display_title(manifest.title);
    size_t client_bytes = (size_t)(__client_image_end - __client_image_start);
    if(!client_bytes || client_bytes > KUI_IMAGE_CLIENT_MAX_BYTES ||
       manifest.track_count > KUI_IMAGE_CLIENT_TRACKS ||
       !manifest.sample_count || manifest.sample_count > KUI_IMAGE_CLIENT_SAMPLES) {
        kui_loader_display_result("Independent client bounds", 0, (uint32_t)client_bytes);
        kui_loader_display_summary(1, 0);
        return;
    }
    for(uint32_t n = 0; n < manifest.track_count; n++) {
        const struct kui_resident_track *t = &manifest.tracks[n];
        tracks[n] = (struct kui_gd_track){t->number, t->control, t->start_lba, t->end_lba};
        client_tracks[n] = (struct kui_image_client_track){
            t->number, t->control, t->start_lba, t->end_lba};
    }
    for(uint32_t n = 0; n < manifest.sample_count; n++) {
        const struct kui_resident_sample *s = &manifest.samples[n];
        if(!s->count || s->count > KUI_IMAGE_CLIENT_MAX_SECTORS) {
            kui_loader_display_result("Bounded client sample", 0, s->count);
            kui_loader_display_summary(1, 0);
            return;
        }
        samples[n] = (struct kui_image_client_sample){s->lba, s->count,
            s->format == KUI_GAME_SECTOR_RAW ? KUI_GAME_RAW_BYTES : KUI_GAME_DATA_BYTES,
            s->crc32};
    }

    erase_retired_runtime();
    memcpy((void *)(uintptr_t)KUI_IMAGE_CLIENT_ADDRESS, __client_image_start, client_bytes);
    kui_loader_display_line("Old launcher RAM erased; BIOS client installed.");
    kui_loader_display_line("Initializing independent read-only SD backend...");
    last_sd_result = kui_loader_sd_init(&card);
    if(last_sd_result != KUI_LOADER_SD_OK) {
        kui_loader_display_result("SD initialization", 0, (uint32_t)last_sd_result);
        display_sd_failure();
        kui_loader_display_summary(1, 0);
        return;
    }
    if(card.blocks < manifest.card_sectors) {
        kui_loader_display_result("Mapped image fits card capacity", 0, 0);
        kui_loader_sd_shutdown(&card);
        kui_loader_display_summary(1, 0);
        return;
    }
    result = kui_resident_image_init(&image, &manifest, read_block, &card);
    const struct kui_gd_ops ops = {NULL, map_guest, check_sectors, read_sectors};
    if(result != KUI_GAME_OK || kui_gd_service_init(&service, tracks,
        manifest.track_count, &ops, KUI_IMAGE_CLIENT_ADDRESS,
        KUI_IMAGE_RESIDENT_ADDRESS) != 0) {
        kui_loader_display_result("Resident GD service initialization", 0, (uint32_t)result);
        kui_loader_sd_shutdown(&card);
        kui_loader_display_summary(1, 0);
        return;
    }
    volatile uint32_t *const guard = (volatile uint32_t *)(uintptr_t)KUI_IMAGE_HOOK_STACK_BOTTOM;
    guard[0] = 0x47534430u;
    guard[1] = 0x47534431u;
    /* With interrupts masked and caches disabled, the vector store is visible
     * immediately to the independent client. The original firmware area and
     * all other vectors remain untouched. */
    volatile uint32_t *const vector = (volatile uint32_t *)(uintptr_t)KUI_GD_VECTOR_ADDRESS;
    *vector = (uint32_t)(uintptr_t)kui_image_gd_hook;
    const struct kui_image_client_api api = {
        KUI_IMAGE_CLIENT_VERSION, sizeof(struct kui_image_client_api),
        manifest.track_count, client_tracks, manifest.sample_count, samples,
        kui_loader_display_result, read_count
    };
    uint32_t (*client_entry)(const struct kui_image_client_api *) =
        (uint32_t (*)(const struct kui_image_client_api *))(uintptr_t)KUI_IMAGE_CLIENT_ADDRESS;
    uint32_t failures = client_entry(&api);
    uint32_t stack_ok = guard[0] == 0x47534430u && guard[1] == 0x47534431u;
    kui_loader_display_result("Resident hook stack guard", stack_ok, 0);
    if(!stack_ok) failures++;
    if(!image.blocks_read) {
        kui_loader_display_result("Actual post-handoff SD reads", 0, 0);
        failures++;
    }
    if(last_sd_result != KUI_LOADER_SD_OK) display_sd_failure();
    kui_loader_sd_shutdown(&card);
    kui_loader_display_summary(failures, image.blocks_read);
}
