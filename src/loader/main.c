/* SPDX-License-Identifier: GPL-3.0-only */
#include "display.h"
#include "sd_reader.h"
#include "kui/loader_layout.h"
#include "kui/loader_probe.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

extern const uint8_t __client_image_start[] __asm__("__client_image_start");
extern const uint8_t __client_image_end[] __asm__("__client_image_end");
extern uint8_t __bss_end[] __asm__("__bss_end");
static struct kui_loader_probe_service service;
static struct kui_loader_probe_manifest manifest;
static struct kui_loader_sd card;
static enum kui_loader_sd_result last_sd_result;

static int read_block(void *context, uint32_t lba, uint8_t output[512]) {
    last_sd_result = kui_loader_sd_read(context, lba, 1, output);
    return last_sd_result == KUI_LOADER_SD_OK ? 0 : -1;
}
static enum kui_loader_probe_result submit(
    const struct kui_loader_probe_request *request, void *output,
    uint32_t capacity, uint32_t *token) {
    return kui_loader_probe_submit(&service, request, output, capacity, token);
}
static enum kui_loader_probe_result poll(uint32_t token, uint32_t *bytes) {
    return kui_loader_probe_poll(&service, token, bytes);
}
static enum kui_loader_probe_result cancel(uint32_t token) {
    return kui_loader_probe_cancel(&service, token);
}
static uint32_t read_count(void) { return service.blocks_read; }
static const struct kui_loader_probe_api api = {
    KUI_LOADER_PROBE_VERSION, sizeof(struct kui_loader_probe_api),
    submit, poll, cancel, kui_loader_display_result, read_count
};

static void erase_range(uintptr_t start, uintptr_t end) {
    volatile uint32_t *p = (volatile uint32_t *)start;
    while((uintptr_t)p < end) *p++ = 0;
}
static void erase_retired_runtime(void) {
    /* Erase every main-RAM byte other than the BIOS area, our new resident
     * image/BSS, and our current 64 KiB stack reservation. Even old shell heap
     * allocations or a trampoline near the top of RAM cannot be read later.
     * The first 64 KiB are firmware-owned and are not used by the service. */
    erase_range(KUI_LOADER_CLIENT_ADDRESS, KUI_LOADER_RESIDENT_ADDRESS);
    erase_range((uintptr_t)__bss_end, KUI_LOADER_RESIDENT_MEMORY_END);
    erase_range(KUI_LOADER_RESIDENT_STACK, 0x8d000000u);
}

void kui_loader_resident_main(const uint8_t wire[KUI_LOADER_MANIFEST_BYTES]) {
    kui_loader_display_init();
    kui_loader_display_line("Resident entered; launcher has shut down.");
    enum kui_loader_probe_result result = kui_loader_probe_manifest_decode(wire, &manifest);
    if(result != KUI_LP_OK) {
        kui_loader_display_result("Launch extent manifest", 0, (uint32_t)result);
        kui_loader_display_summary(1, 0);
        return;
    }
    size_t client_bytes = (size_t)(__client_image_end - __client_image_start);
    if(!client_bytes || client_bytes > KUI_LOADER_CLIENT_MAX_BYTES) {
        kui_loader_display_result("Independent client bounds", 0, (uint32_t)client_bytes);
        kui_loader_display_summary(1, 0);
        return;
    }

    erase_retired_runtime();
    memcpy((void *)(uintptr_t)KUI_LOADER_CLIENT_ADDRESS, __client_image_start, client_bytes);
    /* Cache-off code/data remain coherent; no retired KOS cache helper is
     * invoked. The client entry itself clears only its own BSS. */
    kui_loader_display_line("Old launcher RAM erased; own client installed.");
    kui_loader_display_line("Initializing independent read-only SD backend...");
    last_sd_result = kui_loader_sd_init(&card);
    if(last_sd_result != KUI_LOADER_SD_OK) {
        kui_loader_display_result("SD initialization", 0, (uint32_t)last_sd_result);
        kui_loader_display_line(kui_loader_sd_result_name(last_sd_result));
        kui_loader_display_number("Last command: ", card.last_command);
        kui_loader_display_number("Last response: ", card.last_response);
        kui_loader_display_summary(1, 0);
        return;
    }
    /* The manifest's bound is the mapped partition end; the physical card may
     * contain additional partitions or unused sectors beyond that bound. */
    if(card.blocks < manifest.card_sectors) {
        kui_loader_display_result("Mapped volume fits card capacity", 0, 0);
        kui_loader_sd_shutdown(&card);
        kui_loader_display_summary(1, 0);
        return;
    }
    result = kui_loader_probe_init(&service, &manifest, read_block, &card);
    if(result != KUI_LP_OK) {
        kui_loader_display_result("Resident service initialization", 0, (uint32_t)result);
        kui_loader_sd_shutdown(&card);
        kui_loader_display_summary(1, 0);
        return;
    }

    uint32_t (*client_entry)(const struct kui_loader_probe_api *) =
        (uint32_t (*)(const struct kui_loader_probe_api *))(uintptr_t)KUI_LOADER_CLIENT_ADDRESS;
    uint32_t failures = client_entry(&api);
    if(!service.blocks_read) {
        kui_loader_display_result("Actual post-handoff SD reads", 0, 0);
        failures++;
    }
    if(last_sd_result != KUI_LOADER_SD_OK) {
        kui_loader_display_line(kui_loader_sd_result_name(last_sd_result));
        kui_loader_display_number("Last command: ", card.last_command);
        kui_loader_display_number("Last response: ", card.last_response);
    }
    kui_loader_sd_shutdown(&card);
    kui_loader_display_summary(failures, service.blocks_read);
}
