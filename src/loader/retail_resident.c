/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/retail_resident.h"
#include "kui/retail_gd.h"
#include "kui/retail_image.h"
#include "kui/retail_pace.h"
#include "retail_sd.h"
#include "retail_display.h"
#include <stddef.h>
#include <string.h>

static struct kui_retail_manifest manifest;
static struct kui_retail_image image;
static struct kui_retail_gd service;
static struct kui_loader_sd card;
static struct kui_loader_sd_stream stream;
static struct kui_gd_track tracks[KUI_RETAIL_IMAGE_TRACKS];
static struct retail_display_state display;
static struct kui_retail_pace pace;
/* Menu-return diagnostics: steps longer than two sectors, steps run for a
 * game spinning on CHECK, and the caller SR of the last step that read. */
static struct { uint32_t paced, spun, sr; } pacing;
uint32_t kui_retail_original_menu;
extern void kui_retail_menu_hook(void);
extern void kui_retail_gd_c0_hook(void);
extern void kui_retail_gd_1000_hook(void);
extern void kui_retail_gd_10f0_hook(void);
/* Source: 0=BC supervisor vector, 1=C0 raw GD vector, 2/3=direct firmware
 * entries. Assembly publishes this only after acquiring the resident lock. */
volatile uint32_t kui_retail_hook_source, kui_retail_hook_sr;
static enum kui_loader_sd_result card_result;
volatile uint32_t kui_retail_hook_active, kui_retail_hook_fault;
extern uint8_t __retail_resident_bss_begin[] __asm__("__retail_resident_bss_begin");
extern uint8_t __retail_resident_bss_end[] __asm__("__retail_resident_bss_end");

/* Fixed FPU registers, integer-only division and a linked machine-code audit
 * prevent the resident from touching the game's FPSCR/FPU register state.
 * Every native address is checked before its P2 conversion.
 * OCBP writes any dirty P1 line back and invalidates it: the P2 copy then
 * observes request parameters and leaves no stale cached destination alias. */
static void purge(uint32_t address, uint32_t bytes) {
    uint32_t end = address + bytes;
    for(uint32_t p = address & ~31u; p < end; p += 32u)
        __asm__ __volatile__("ocbp @%0" : : "r"(p) : "memory");
}
static uint8_t *map_guest(void *unused, uint32_t address, uint32_t bytes,
                          int writing) {
    (void)unused;
    if(address < KUI_RETAIL_IP_ADDRESS || address >= KUI_RETAIL_RAM_END ||
       !bytes || bytes > KUI_RETAIL_RAM_END - address) return NULL;
    uint32_t end = address + bytes;
    if(address < KUI_RETAIL_HOOK_STACK && end > KUI_RETAIL_RESIDENT_ADDRESS)
        return NULL;
    /* Submission checks the entire destination without touching its cache.
     * EXEC purges only the chunk it is about to copy through P2. */
    if(writing != KUI_RETAIL_MAP_VALIDATE) purge(address, bytes);
    return (uint8_t *)(uintptr_t)((address & 0x1fffffffu) | 0xa0000000u);
}
/* Read-only PowerVR SPG_STATUS, SPG_VBLANK_INT and FB_R_SOF1: scanline,
 * vblank-in line and the displayed framebuffer. No video state is changed. */
static void video_sample(void) {
    volatile const uint32_t *pvr = (volatile const uint32_t *)(uintptr_t)0xa05f8000u;
    kui_retail_pace_sample(&pace, pvr[0x10c / 4], pvr[0xcc / 4], pvr[0x50 / 4]);
}
static int read_run(void *unused, uint32_t lba, uint32_t available, uint8_t output[512]) {
    (void)unused;
    video_sample(); /* Each block is ~1 ms: long steps still count wraps. */
    card_result = kui_retail_sd_read_run(&card, &stream, lba, available, output);
    return card_result == KUI_LOADER_SD_OK ? 0 : -1;
}
/* The image API requires this fallback; with read_run bound it is unused. */
static int read_block(void *unused, uint32_t lba, uint8_t output[512]) {
    return read_run(unused, lba, 1, output);
}
static enum kui_game_sector_format sector_format(uint32_t bytes) {
    return bytes == 2352 ? KUI_GAME_SECTOR_RAW : KUI_GAME_SECTOR_MODE1;
}
static int check_sectors(void *unused, uint32_t lba, uint32_t count, uint32_t bytes) {
    (void)unused;
    if(bytes != 2048 && bytes != 2352) return -1;
    return kui_retail_image_check(&manifest, lba, count, sector_format(bytes)) == KUI_GAME_OK ? 0 : -1;
}
static int read_sectors(void *unused, uint32_t lba, uint32_t count,
                        uint32_t bytes, void *out) {
    (void)unused;
    if(bytes != 2048 && bytes != 2352) return -1;
    card_result = kui_retail_sd_acquire();
    if(card_result != KUI_LOADER_SD_OK) return -1;
    enum kui_game_result result = kui_retail_image_read(&image, lba, count,
        sector_format(bytes), out, (size_t)count * bytes);
    enum kui_loader_sd_result stopped = kui_loader_sd_stream_stop(&card, &stream);
    if(card_result == KUI_LOADER_SD_OK) card_result = stopped;
    if(stopped != KUI_LOADER_SD_OK) image.cache_valid = 0;
    kui_retail_sd_release();
    return result == KUI_GAME_OK && card_result == KUI_LOADER_SD_OK ? 0 : -1;
}
static void redirect_entry(uint32_t address,void (*target)(void)) {
    /* Aligned SH-4 tail jump: MOV.L @(1,PC),R0; JMP @R0; NOP; NOP;
     * target. All addresses are fixed firmware RAM entries, not game code.
     * Stage bootstrap_enter publishes the writes and invalidates I-cache
     * before first use. This helper is called only during resident init. */
    purge(address,12);
    volatile uint16_t *code=(volatile uint16_t *)(uintptr_t)(address|0x20000000u);
    code[0]=0xd001u; code[1]=0x402bu; code[2]=0x0009u; code[3]=0x0009u;
    *(volatile uint32_t *)(code+4)=(uint32_t)(uintptr_t)target;
}
static void install_hook(void) {
    /* Publish adjacent firmware vector writes before touching the shared
     * cache line, then install through P2 with no stale P1 alias left over. */
    purge(KUI_GD_VECTOR_ADDRESS, sizeof(uint32_t));
    volatile uint32_t *vector = (volatile uint32_t *)(uintptr_t)
        ((KUI_GD_VECTOR_ADDRESS & 0x1fffffffu) | 0xa0000000u);
    *vector = (uint32_t)(uintptr_t)kui_retail_resident_hook;
    purge(0x8c0000c0u, sizeof(uint32_t));
    *(volatile uint32_t *)(uintptr_t)0xac0000c0u=(uint32_t)(uintptr_t)kui_retail_gd_c0_hook;
    redirect_entry(0x8c001000u,kui_retail_gd_1000_hook);
    redirect_entry(0x8c0010f0u,kui_retail_gd_10f0_hook);
    purge(0x8c0000e0u, sizeof(uint32_t));
    *(volatile uint32_t *)(uintptr_t)0xac0000e0u=(uint32_t)(uintptr_t)kui_retail_menu_hook;
    __asm__ __volatile__("" : : : "memory");
}
static void report_fault(const char *reason, uint32_t function) {
    retail_display_restore(&display);
    retail_display_line("K-UI GAME READER");
    retail_display_line(manifest.title);
    retail_display_line(reason);
    retail_display_hex("GD function", function);
    retail_display_hex("Command", service.diag.last_command);
    retail_display_hex(service.diag.last_command == KUI_RETAIL_GD_GETSCD ? "Format" : "LBA", service.diag.last_lba);
    retail_display_hex(service.diag.last_command == KUI_RETAIL_GD_GETSCD ? "Bytes" : "Sectors", service.diag.last_count);
    retail_display_hex("Destination", service.diag.last_destination);
    retail_display_hex("SD result", (uint32_t)card_result);
    retail_display_hex("SD blocks read", image.blocks_read);
    retail_display_line("LAUNCH STOPPED - PHOTOGRAPH THIS SCREEN");
    retail_display_line("POWER OFF AND ON TO RETURN");
    for(;;) __asm__ volatile("nop");
}
void kui_retail_menu_return(uint32_t command,uint32_t caller,uint32_t stack) {
    (void)command; /* Assembly reaches this only for menu return command 1. */
    retail_display_restore(&display);
    retail_display_line("GAME REQUESTED BIOS MENU RETURN");
    retail_display_hex("CALLER PR",caller);
    retail_display_hex("CALLER STACK",stack);
    retail_display_hex("HOOK GUARD FAULT",kui_retail_hook_fault);
    retail_display_hex("LAST GD COMMAND",service.diag.last_command);
    retail_display_hex("SD BLOCKS READ",image.blocks_read);
    /* How the game drives reads: ABXY+Start after a load shows these. */
    retail_display_hex("GD CALLS",service.diag.calls);
    retail_display_hex("EXEC CALLS",service.diag.exec_calls);
    retail_display_hex("READ STEPS",service.diag.read_steps);
    retail_display_hex("SECTORS READ",service.diag.sectors_read);
    retail_display_hex("FRAMES SEEN",pace.frames);
    retail_display_hex("PACED STEPS",pacing.paced);
    retail_display_hex("SPIN STEPS",pacing.spun);
    retail_display_hex("STEP CALLER SR",pacing.sr);
    retail_display_line("LAUNCH STOPPED - PHOTOGRAPH THIS SCREEN");
    retail_display_line("POWER OFF AND ON TO RETURN");
    for(;;) __asm__ volatile("nop");
}
int kui_retail_resident_init(const struct kui_retail_manifest *prepared,
    const struct kui_loader_sd *prepared_card, uint32_t original_gd_vector,
    const struct retail_display_state *saved_display) {
    uint32_t area = original_gd_vector & 0xff000000u;
    uint32_t p1 = (original_gd_vector & 0x00ffffffu) | 0x8c000000u;
    if(!prepared || !prepared_card || !saved_display || (original_gd_vector & 1u) ||
       (area != 0x8c000000u && area != 0xac000000u && area != 0x0c000000u) ||
       p1 < 0x8c000100u || p1 >= KUI_RETAIL_IP_ADDRESS)
        return KUI_RETAIL_RESIDENT_ARGUMENT;
    display = *saved_display;
    if(!prepared->track_count || prepared->track_count > KUI_RETAIL_IMAGE_TRACKS ||
       !prepared->extent_count || prepared->extent_count > KUI_RETAIL_IMAGE_EXTENTS)
        return KUI_RETAIL_RESIDENT_MAP;
    /* The high stage already decoded/validated this map and CRC-checked the
     * owner IP/executable. Copy it and rebind initialized card state to local
     * callbacks. No second SD reset or manifest parser remains in low RAM. */
    manifest = *prepared;
    card_result = kui_retail_sd_adopt(&card, prepared_card);
    if(card_result != KUI_LOADER_SD_OK || card.blocks < manifest.card_sectors)
        return KUI_RETAIL_RESIDENT_SD;
    /* _start cleared all resident BSS, including image/cache/stream/counters. */
    image.manifest = &manifest;
    image.read_block = read_block;
    image.read_run = read_run;
    for(uint32_t i = 0; i < manifest.track_count; ++i) {
        const struct kui_retail_track *t = &manifest.tracks[i];
        tracks[i] = (struct kui_gd_track){t->number, t->control, t->start_lba, t->end_lba};
    }
    const struct kui_gd_ops ops = {NULL, map_guest, check_sectors, read_sectors};
    if(kui_retail_gd_init(&service, tracks, manifest.track_count, &ops,
                         KUI_RETAIL_IP_ADDRESS, KUI_RETAIL_RAM_END))
        return KUI_RETAIL_RESIDENT_SERVICE;
    volatile uint32_t *guard = (volatile uint32_t *)(uintptr_t)KUI_RETAIL_HOOK_STACK_BOTTOM;
    for(unsigned i = 0; i < 4; ++i) guard[i] = 0x4b554947u;
    kui_retail_original_menu=*(volatile uint32_t *)(uintptr_t)0x8c0000e0u;
    /* The game's first instructions may reinitialize caches. Do not leave
     * newly decoded manifest/card/guard state only in dirty cache lines. */
    purge((uint32_t)(uintptr_t)__retail_resident_bss_begin,
          (uint32_t)(__retail_resident_bss_end - __retail_resident_bss_begin));
    purge(KUI_RETAIL_HOOK_STACK_BOTTOM, 16);
    install_hook();
    return KUI_RETAIL_RESIDENT_OK;
}
/* One EXEC, sized by the pacing policy and timed for the next estimate. */
static int32_t step(uint32_t r4, uint32_t r5) {
    uint32_t frames = pace.frames, line = pace.line, before = service.diag.sectors_read;
    pace.spin = 0;
    service.step = kui_retail_pace_budget(&pace, KUI_RETAIL_GD_STEP_SECTORS,
                                          KUI_RETAIL_GD_STEP_MAX);
    int32_t result = kui_retail_gd_dispatch(&service, r4, r5, 0, KUI_GD_EXEC);
    if(service.error == KUI_GD_ERROR_IO)
        report_fault("IMAGE READ FAILED", KUI_GD_EXEC);
    uint32_t sectors = service.diag.sectors_read - before;
    if(sectors) {
        video_sample();
        kui_retail_pace_measure(&pace, frames, line, sectors);
        if(service.step > KUI_RETAIL_GD_STEP_SECTORS) ++pacing.paced;
        pacing.sr = kui_retail_hook_sr;
    }
    return result;
}
int32_t kui_retail_resident_dispatch(uint32_t r4, uint32_t r5,
                                    uint32_t r6, uint32_t r7) {
    /* All paths arrive with the resident entry lock held and interrupts
     * masked. Interface behavior cross-checked against DreamShell ISO Loader
     * gdc_syscall.s (GPL-3.0, Copyright 2009-2023 SWAT): C0 ignores R6;
     * BC and both direct firmware entries acknowledge R6=-1 setup calls.
     * Do not load/register the physical GD driver over the image service.
     * No original GD forwarding remains: those entries now lead back here.
     * Font/flash/system BIOS vectors are independent and unchanged. */
    uint32_t source=kui_retail_hook_source;
    if(source>3) return -1;
    if(source!=1 && r6==UINT32_MAX) {
        return 0;
    }
    video_sample();
    uint32_t pending = service.pending;
    /* A game polling CHECK in a tight loop is idle until the read finishes;
     * give it a step, as its EXEC would. The CHECK itself still never reads. */
    if(r7 == KUI_GD_CHECK && pending && (service.command == KUI_GD_PIOREAD ||
       service.command == KUI_GD_DMAREAD) && kui_retail_pace_spin(&pace)) {
        ++pacing.spun;
        (void)step(0, 0);
    }
    int32_t result = r7 == KUI_GD_EXEC ? step(r4, r5) :
        kui_retail_gd_dispatch(&service, r4, r5, 0, r7);
    if(r7 == KUI_GD_REQUEST && !pending && result == 0)
        report_fault("GD REQUEST REJECTED", r7);
    else if(result < 0 && (r7 > KUI_GD_DATATYPE ||
            r7 == KUI_GD_DMA_CALLBACK || r7 == KUI_GD_DMA_TRANSFER || r7 == KUI_GD_DMA_CHECK))
        report_fault("GD FUNCTION UNSUPPORTED", r7);
    return result;
}
