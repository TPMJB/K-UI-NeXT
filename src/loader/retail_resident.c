/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/retail_resident.h"
#include "kui/retail_gd.h"
#include "kui/retail_image.h"
#include "kui/retail_pace.h"
#include "retail_storage.h"
#include "retail_display.h"
#include <stddef.h>
#include <string.h>

static struct kui_retail_manifest manifest;
#ifdef KUI_RETAIL_ASYNC
/* The background reader keeps these between its interrupt vectors. */
#include "retail_async.h"
#define service (kui_retail_async_region.shared.service)
#define card (kui_retail_async_region.shared.card)
#define display (kui_retail_async_region.shared.display)
#define reader (kui_retail_async_region.engine)
#else
static struct kui_retail_image image;
static struct kui_retail_gd service;
static struct kui_retail_storage card;
static struct retail_display_state display;
#ifndef KUI_RETAIL_CE
static struct kui_retail_pace pace;
/* Cumulative menu-return diagnostics; game GD resets do not clear them. */
static struct { uint32_t paced, spun; } pacing;
#endif
static enum kui_loader_sd_result card_result;
#endif
uint32_t kui_retail_original_menu;
extern void kui_retail_menu_hook(void);
extern void kui_retail_gd_c0_hook(void);
extern void kui_retail_gd_1000_hook(void);
extern void kui_retail_gd_10f0_hook(void);
/* Source: 0=BC supervisor vector, 1=C0 raw GD vector, 2/3=direct firmware
 * entries. Assembly publishes this only after acquiring the resident lock. */
volatile uint32_t kui_retail_hook_source, kui_retail_hook_sr;
#ifdef KUI_RETAIL_CE
/* Windows CE boot test: the caller's PR and stack (set by the entry), and
 * the last four calls' R7, R4, R5 and R6, shown if a call fails. */
volatile uint32_t kui_retail_hook_caller[2];
static uint32_t ce_calls[4][4], ce_count;
extern volatile uint32_t kui_retail_ce_kernel[KUI_RETAIL_CE_KERNEL_WORDS];
/* A callback the hook's exit makes once the lock is released: function and
 * argument (retail_resident.S, .Ldeferred_call); zero when none. */
volatile uint32_t kui_retail_ce_deferred[2];
#ifndef KUI_RETAIL_ASYNC
static enum kui_game_result image_result; /* The last image read's, for the trace. */
#endif
/* The GD service hands RAM over as P1 (DMA destinations converted from
 * physical); anything else is one of CE's virtual addresses. */
static int ram_alias(uint32_t address) {
    uint32_t area = address & 0xff000000u;
    return area == 0x8c000000u || area == 0xac000000u;
}
#endif
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
#ifdef KUI_RETAIL_CE
    /* Windows CE's virtual addresses (U0 or P3, with its MMU on) are used
     * as they are: the resident runs with SR.BL clear, so CE's own TLB-miss
     * handler maps each page as it is touched, through CE's cache. */
    if(!ram_alias(address)) {
        uint32_t end = address + bytes;
        if(!(*(volatile const uint32_t *)(uintptr_t)0xff000010u & 1u) || end < address)
            return NULL;
        return end <= 0x80000000u || (address >= 0xc0000000u && end <= 0xe0000000u) ?
            (uint8_t *)(uintptr_t)address : NULL;
    }
#endif
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
#ifndef KUI_RETAIL_ASYNC
#ifdef KUI_RETAIL_CE
/* The Windows CE boot test reads a fixed step per call: no pacing. */
static void video_sample(void) {}
#else
/* Read-only PowerVR status, vblank-in, counter period and framebuffer.
 * No video state is changed. */
static void video_sample(void) {
    volatile const uint32_t *pvr = (volatile const uint32_t *)(uintptr_t)0xa05f8000u;
    kui_retail_pace_sample(&pace, pvr[0x10c / 4], pvr[0xcc / 4],
                           pvr[0xd8 / 4], pvr[0x50 / 4]);
}
#endif
static int read_run(void *unused, uint32_t lba, uint32_t available, uint8_t output[512]) {
    (void)unused;
    video_sample(); /* Each block is ~1 ms: long steps still count wraps. */
    card_result = kui_retail_storage_read_run(&card, lba, available, output);
    return card_result == KUI_LOADER_SD_OK ? 0 : -1;
}
/* The image API requires this fallback; with read_run bound it is unused. */
static int read_block(void *unused, uint32_t lba, uint8_t output[512]) {
    return read_run(unused, lba, 1, output);
}
#endif
static enum kui_game_sector_format sector_format(uint32_t bytes) {
    return bytes == 2352 ? KUI_GAME_SECTOR_RAW : KUI_GAME_SECTOR_MODE1;
}
static int check_sectors(void *unused, uint32_t lba, uint32_t count, uint32_t bytes) {
    (void)unused;
    if(bytes != 2048 && bytes != 2352) return -1;
    return kui_retail_image_check_validated(&manifest, lba, count, sector_format(bytes)) == KUI_GAME_OK ? 0 : -1;
}
#ifndef KUI_RETAIL_ASYNC
static int read_sectors(void *unused, uint32_t lba, uint32_t count,
                        uint32_t bytes, void *out) {
    (void)unused;
    if(bytes != 2048 && bytes != 2352) return -1;
    card_result = kui_retail_storage_acquire(&card);
    if(card_result != KUI_LOADER_SD_OK) return -1;
    enum kui_game_result result = kui_retail_image_read(&image, lba, count,
        sector_format(bytes), out, (size_t)count * bytes);
#ifdef KUI_RETAIL_CE
    image_result = result;
#endif
    enum kui_loader_sd_result stopped = kui_retail_storage_stop(&card);
    if(card_result == KUI_LOADER_SD_OK) card_result = stopped;
    if(stopped != KUI_LOADER_SD_OK) image.cache_valid = 0;
    kui_retail_storage_release(&card);
    return result == KUI_GAME_OK && card_result == KUI_LOADER_SD_OK ? 0 : -1;
}
#endif
#if defined(KUI_RETAIL_CE) && !defined(KUI_RETAIL_ASYNC)
/* Windows CE's DMA stream pieces: part of the user data of a run of
 * sectors, under the same card ownership rules as read_sectors. */
static int read_part(void *unused, uint32_t lba, uint32_t sector_bytes,
                     uint32_t skip, uint32_t bytes, void *out) {
    (void)unused;
    card_result = kui_retail_storage_acquire(&card);
    if(card_result != KUI_LOADER_SD_OK) return -1;
    enum kui_game_result result = image_result = kui_retail_image_read_part(&image, lba,
        skip, bytes, sector_format(sector_bytes), out);
    enum kui_loader_sd_result stopped = kui_retail_storage_stop(&card);
    if(card_result == KUI_LOADER_SD_OK) card_result = stopped;
    if(stopped != KUI_LOADER_SD_OK) image.cache_valid = 0;
    kui_retail_storage_release(&card);
    return result == KUI_GAME_OK && card_result == KUI_LOADER_SD_OK ? 0 : -1;
}
#endif
#ifdef KUI_RETAIL_CE
/* Raise a device interrupt (SYSINTR) in Windows CE as its kernel's own
 * interrupt dispatch does when the platform handler returns one: mark it
 * pending, queue its index (SYSINTR - 8) in the 32-entry ring after the
 * ring's head index, and request a reschedule. The scheduler then sets the
 * event the driver waits on. Called with interrupts masked, so CE's
 * dispatch cannot run in between. */
static void ce_raise(uint32_t sysintr) {
    volatile uint32_t *pending = (volatile uint32_t *)(uintptr_t)kui_retail_ce_kernel[0];
    volatile uint32_t *head = (volatile uint32_t *)(uintptr_t)kui_retail_ce_kernel[1];
    uint32_t index = sysintr - 8u, bit = 1u << index;
    if(!(*pending & bit)) {
        *pending |= bit;
        uint32_t next = *head;
        ((volatile uint8_t *)head)[4u + next] = (uint8_t)index;
        *head = (next + 1u) & 31u;
    }
    *(volatile uint8_t *)(uintptr_t)kui_retail_ce_kernel[2] |= 1u;
}
#endif
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
#ifdef KUI_RETAIL_ASYNC
/* Counter rows for both screens; every counter is a uint32_t in order. */
#ifdef KUI_RETAIL_CE
_Static_assert(sizeof(struct kui_sci_stream_stats) == 21u * 4u, "CE stream counters");
_Static_assert(offsetof(struct kui_retail_async, pio_bytes) ==
               offsetof(struct kui_retail_async, polled_irq_max) + 12u, "CE work counters");
#else
_Static_assert(sizeof(struct kui_sci_stream_stats) == 11u * 4u, "stream counters");
#endif
_Static_assert(sizeof(struct kui_retail_async_stats) == 7u * 4u, "reader counters");
_Static_assert(offsetof(struct kui_retail_async_release, vbr_changes) ==
               offsetof(struct kui_retail_async_release, rehooks) + 16u, "release counters");
static void stream_lines(void) {
    const uint32_t *st = (const uint32_t *)kui_sci_stream_stats();
    retail_display_values("DMA BLKS POLLED   STARTS   KEPT     OVERRUNS", st, 5);
    retail_display_values("CRC ERRS TOKENERR FOREIGN  REPAIRED AHEAD", st + 5, 5);
#ifdef KUI_RETAIL_CE
    /* Remaining DMA bytes in incomplete receptions, then late controller
     * snapshots and framing work. Counts only: none are elapsed times. */
    retail_display_values("LEFT0    LEFT128  LEFT384  LEFT513  CH2 LATE", st + 11, 5);
    retail_display_values("DMAORBAD TOKBYTES TOKMAX   STOPS    TOKYIELD", st + 16, 5);
#else
    retail_display_values("DEFERRED", st + 10, 1);
#endif
}
#endif
#if defined(KUI_RETAIL_CE) && !defined(KUI_RETAIL_ASYNC)
/* Who called last (CE's address, stack, SR, MMU state) and the last four
 * calls: value rows only, as this build has no single-value printer. */
static void ce_trace(unsigned earlier) {
    static uint32_t caller[5], row[5]; /* Static: the stack budget is full. */
    caller[0]=kui_retail_hook_caller[0]; caller[1]=kui_retail_hook_caller[1];
    caller[2]=kui_retail_hook_sr; caller[3]=*(volatile uint32_t *)(uintptr_t)0xff000010u;
    __asm__ __volatile__("stc vbr,%0" : "=r"(caller[4]));
    retail_display_values("CALLER   STACK    SR       MMUCR    VBR",caller,5);
    memcpy(row,ce_calls[(ce_count-1u)&3u],16); row[4]=ce_count;
    retail_display_values("R7       R4       R5       R6       CALLS",row,5);
    for(unsigned i=1;i<=earlier;i++)
        retail_display_values("EARLIER",ce_calls[(ce_count-1u-i)&3u],4);
}
#endif
static void report_fault(const char *reason, uint32_t function) {
    retail_display_restore(&display);
    retail_display_line("K-UI READER");
    retail_display_line(manifest.title);
#ifdef KUI_RETAIL_ASYNC
    /* Fifteen rows fit: command, LBA (GETSCD: format), sectors (bytes). */
    retail_display_line(reason);
    retail_display_hex("GD FUNCTION", function);
    _Static_assert(offsetof(struct kui_retail_gd_diagnostics, last_destination) ==
                   offsetof(struct kui_retail_gd_diagnostics, last_command) + 12u, "GD row");
    retail_display_values("COMMAND  LBA      SECTORS  DEST", &service.diag.last_command, 4);
    retail_display_hex("CARD BLOCK", reader.cursor.block);
    stream_lines();
#elif defined(KUI_RETAIL_CE)
    /* Who called (CE's address, stack, SR, MMU state) and what it asked:
     * value rows only, as this build has no single-value printer. */
    (void)function;
    retail_display_line(reason);
    ce_trace(2);
    /* How the last image read ended (SD and image results, card blocks)
     * and the DMA controller's state (DMAOR, channel 2 control). */
    static uint32_t io[5];
    io[0]=(uint32_t)card_result; io[1]=(uint32_t)image_result; io[2]=image.blocks_read;
    io[3]=*(volatile uint32_t *)(uintptr_t)0xffa00040u;
    io[4]=*(volatile uint32_t *)(uintptr_t)0xffa0002cu;
    retail_display_values("SD       IMAGE    BLOCKS   DMAOR    DMA2 CTL",io,5);
#else
    retail_display_line(kui_retail_storage_name(card.transport));
    retail_display_line(reason);
    retail_display_hex("GD function", function);
    retail_display_hex("GD COMMAND", service.diag.last_command);
    retail_display_hex(service.diag.last_command == KUI_RETAIL_GD_GETSCD ? "Format" : "LBA", service.diag.last_lba);
    retail_display_hex(service.diag.last_command == KUI_RETAIL_GD_GETSCD ? "Bytes" : "Sectors", service.diag.last_count);
    retail_display_hex("Destination", service.diag.last_destination);
    retail_display_hex("IO RESULT", (uint32_t)card_result);
    retail_display_hex("BLOCKS READ", image.blocks_read);
#endif
    retail_display_line("STOPPED: PHOTO THIS SCREEN");
    retail_display_line("POWER CYCLE FOR K-UI");
    for(;;) __asm__ volatile("nop");
}
void kui_retail_menu_return(uint32_t command,uint32_t caller,uint32_t stack) {
    (void)command; /* Assembly reaches this only for menu return command 1. */
    (void)caller; (void)stack;
    retail_display_restore(&display);
    retail_display_line("GAME RETURN");
#ifdef KUI_RETAIL_CE
    /* One value row: this build has no single-value printer (stack budget). */
    static uint32_t counts[3]; /* Static: the stack budget is full. */
    counts[0]=kui_retail_hook_fault; counts[1]=service.diag.read_steps;
    counts[2]=service.diag.sectors_read;
    retail_display_values("GUARD    STEPS    SECTORS",counts,3);
    /* Where CE was when it was reset: its last calls (the background
     * reader shows its counters in those rows: 16 lines fit) and disc
     * command. */
#ifndef KUI_RETAIL_ASYNC
    ce_trace(3);
    _Static_assert(offsetof(struct kui_retail_gd_diagnostics, last_destination) ==
                   offsetof(struct kui_retail_gd_diagnostics, last_command) + 12u, "GD row");
    retail_display_values("COMMAND  LBA      SECTORS  DEST", &service.diag.last_command, 4);
#endif
#else
    retail_display_hex("GUARD FAULT",kui_retail_hook_fault);
#endif
#if defined(KUI_RETAIL_ASYNC) && defined(KUI_RETAIL_CE)
    /* Six two-line counter rows plus guard/result fit on one photograph.
     * IRQ/CALL POL are maximum polled blocks in a delivery visit, not
     * time spent with interrupts masked or totals for an entire GD call. */
    const uint32_t *st=(const uint32_t *)&reader.stats;
    retail_display_values("IRQ BLKS CALLBLKS WAITS    EXECS    EXEC INT",st,5);
    retail_display_values("IRQ POL  CALL POL PIOCALLS PIOBYTES",&reader.polled_irq_max,4);
    stream_lines();
#elif defined(KUI_RETAIL_ASYNC)
    /* Background reader: where blocks were delivered (its interrupt or the
     * game's calls), how often an EXEC waited, how the vectors were handed
     * back and installed again, and the stream's errors. */
    const uint32_t *st=(const uint32_t *)&reader.stats;
    retail_display_hex("SECTORS READ",service.diag.sectors_read);
    retail_display_values("IRQ BLKS CALLBLKS WAITS    EXECS    EXEC INT",st,5);
    retail_display_values("HOOKS    RELEASES",st+5,2);
    retail_display_values("REHOOKS  REL 100  REL 400  REL 600  VBR CHGS",&reader.release.rehooks,5);
    stream_lines();
#elif !defined(KUI_RETAIL_CE)
    /* How the game drives reads: ABXY+Start after a load shows these. */
    retail_display_hex("READ STEPS",service.diag.read_steps);
    retail_display_hex("SECTORS READ",service.diag.sectors_read);
    retail_display_hex("PACED STEPS",pacing.paced);
    retail_display_hex("SPIN STEPS",pacing.spun);
    /* Latest sampled geometry/cost, possibly changed by a title-screen
     * reset. Unlike PACED STEPS, these are not a history of the fight. */
    retail_display_hex("PACE PERIOD",pace.period);
    retail_display_hex("PACE VBI",pace.vbi);
    retail_display_hex("PACE COST16",pace.per);
    retail_display_hex("PACE STILL",pace.still);
#endif
    retail_display_line("REBOOT K-UI");
    retail_display_pause(900u); /* ~15 seconds at 60 Hz to capture the counters */
    /* Leave through the boot ROM, as KOS arch_reboot() does, with interrupts
     * still masked: the console restarts and boots the K-UI disc in the drive.
     * No game, reader or vector state is relied on afterwards. */
    ((void (*)(void))(uintptr_t)0xa0000000u)();
    for(;;) __asm__ volatile("nop");
}
int kui_retail_resident_init(const struct kui_retail_manifest *prepared,
    const struct kui_retail_storage *prepared_card, uint32_t original_gd_vector,
    const struct retail_display_state *saved_display) {
    uint32_t area = original_gd_vector & 0xff000000u;
    uint32_t p1 = (original_gd_vector & 0x00ffffffu) | 0x8c000000u;
    if(!prepared || !prepared_card || !saved_display || (original_gd_vector & 1u) ||
       (area != 0x8c000000u && area != 0xac000000u && area != 0x0c000000u) ||
       p1 < 0x8c000100u || p1 >= KUI_RETAIL_IP_ADDRESS)
        return KUI_RETAIL_RESIDENT_ARGUMENT;
    display = *saved_display;
    if(!prepared->track_count || prepared->track_count > KUI_RETAIL_IMAGE_TRACKS ||
       prepared->track_count > KUI_RETAIL_MANIFEST_SLOTS || !prepared->extent_count ||
       prepared->extent_count > KUI_RETAIL_MANIFEST_SLOTS - prepared->track_count)
        return KUI_RETAIL_RESIDENT_MAP;
    /* The high stage already decoded/validated this map and CRC-checked the
     * owner IP/executable. Copy it and rebind initialized card state to local
     * callbacks. No second SD reset or manifest parser remains in low RAM. */
    manifest = *prepared;
    if(kui_retail_storage_adopt(&card, prepared_card) != KUI_LOADER_SD_OK ||
       kui_retail_storage_blocks(&card) < manifest.card_sectors ||
       card.transport != manifest.storage_transport)
        return KUI_RETAIL_RESIDENT_SD;
    /* _start cleared all resident BSS, including image/cache/stream/counters. */
#ifdef KUI_RETAIL_ASYNC
    kui_retail_async_init(&manifest);
    const struct kui_gd_ops ops = {NULL, map_guest, check_sectors, NULL};
#else
    image.manifest = &manifest;
    image.read_block = read_block;
    image.read_run = read_run;
    const struct kui_gd_ops ops = {NULL, map_guest, check_sectors, read_sectors};
#endif
    kui_retail_gd_init_prepared(&service, &manifest, &ops,
        KUI_RETAIL_IP_ADDRESS, KUI_RETAIL_RAM_END);
#if defined(KUI_RETAIL_CE) && defined(KUI_RETAIL_ASYNC)
    service.read_part = kui_retail_async_read_part;
#elif defined(KUI_RETAIL_CE)
    service.read_part = read_part;
#endif
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
#ifdef KUI_RETAIL_CE
/* How fast CE reads and how much of its time it spends inside this reader
 * (its calls and, in the background reader, its interrupt), over windows of
 * at least half a second, on a live status line drawn once per window
 * (drawing costs time too). CE's clock is its millisecond count (KData,
 * just after the reschedule flag); time inside the reader is counted from
 * TMU0, which drives CE's tick and is only read here. */
#define TCOR0 0xffd80008u
#define TCNT0 0xffd8000cu
#define TCR0 0xffd80010u
static struct { uint32_t since, sectors, busy, shown[5]; } meter;
/* n's decimal digits as hexadecimal ones, for the hexadecimal printer. */
static uint32_t decimal(uint32_t n) {
    uint32_t out = 0;
    for(unsigned shift = 0; shift < 32; shift += 4) { out |= (n % 10u) << shift; n /= 10u; }
    return out;
}
static void meter_busy(uint32_t started) {
    volatile const uint32_t *tmu = (volatile const uint32_t *)(uintptr_t)TCOR0;
    uint32_t ended = tmu[1], period = tmu[0] + 1u;
    /* TMU0 counts down; a call is far shorter than its 25 ms period. */
    meter.busy += started >= ended ? started - ended : started + period - ended;
}
static void meter_call(uint32_t started) {
    meter_busy(started);
    uint32_t now = kui_retail_ce_kernel[2] ?
        *(volatile const uint32_t *)(uintptr_t)(kui_retail_ce_kernel[2] + 4u) : ce_count * 8u;
    uint32_t elapsed = now - meter.since;
    if(elapsed < 500u) return;
    /* Pphi is 50 MHz; TPSC selects Pphi/4, /16, /64, /256 or /1024. */
    uint32_t per_ms = 50000u >> (2u + 2u * (*(volatile const uint16_t *)(uintptr_t)TCR0 & 7u));
    if(meter.since && per_ms) {
        meter.shown[2] = decimal((service.diag.sectors_read - meter.sectors) * 2000u / elapsed);
        meter.shown[3] = decimal(meter.busy / per_ms * 100u / elapsed);
    }
    meter.since = now; meter.sectors = service.diag.sectors_read; meter.busy = 0;
    meter.shown[0] = decimal(ce_count); meter.shown[1] = service.diag.last_command;
    meter.shown[4] = decimal(service.diag.sectors_read);
    retail_display_status("CALLS    COMMAND  KIB/S    BUSY PCT SECTORS", meter.shown, 5);
}
/* After each call: the interrupts the service raised, G1 DMA end (SYSINTR
 * 21) and the drive's (SYSINTR 20) as CE's platform maps them; and a PIO
 * stream's callback, due after a transfer, made when this EXEC returns, as
 * the BIOS would make it within its own EXEC. */
static void ce_events(uint32_t r7) {
    if(service.interrupts) {
        if(!kui_retail_ce_kernel[0])
            report_fault("CE KERNEL INTERRUPTS NOT FOUND", r7);
        if(service.interrupts & KUI_RETAIL_GD_IRQ_DMA_END) ce_raise(21u);
        if(service.interrupts & KUI_RETAIL_GD_IRQ_DRIVE) ce_raise(20u);
        service.interrupts = 0;
    }
    if(r7 == KUI_GD_EXEC && service.callback_due && service.pio_callback) {
        kui_retail_ce_deferred[1] = service.pio_argument;
        kui_retail_ce_deferred[0] = service.pio_callback;
        service.callback_due = 0;
    }
}
/* Who called: source 4 is BIOS system function 2, the disc check (recorded
 * as E0). Returns whether that is all the call asks: the image is in the
 * virtual drive, unchanged. */
static int ce_record(uint32_t source, uint32_t r4, uint32_t r5, uint32_t r6, uint32_t r7) {
    uint32_t *call=ce_calls[ce_count++&3u];
    call[0]=source==4u?0xe0u:r7; call[1]=r4; call[2]=r5; call[3]=r6;
    return source==4u;
}
#endif
#ifdef KUI_RETAIL_ASYNC
int32_t kui_retail_resident_dispatch(uint32_t r4, uint32_t r5,
                                    uint32_t r6, uint32_t r7) {
    /* Entry contract as below. The reader delivers what has arrived before
     * the service answers, and starts or stops a read after it. */
    uint32_t source=kui_retail_hook_source;
#ifdef KUI_RETAIL_CE
    uint32_t started=*(volatile const uint32_t *)(uintptr_t)TCNT0;
    if(ce_record(source,r4,r5,r6,r7)) return 0;
#endif
    if(source>3) return -1;
    if(source!=1 && r6==UINT32_MAX) return 0;
    uint32_t pending = service.pending;
    kui_retail_async_call(r7);
    int32_t result = kui_retail_gd_dispatch(&service, r4, r5, 0, r7);
    kui_retail_async_after(r7, result);
    if(service.error == KUI_GD_ERROR_IO)
        report_fault("IMAGE READ FAILED", r7);
    if(r7 == KUI_GD_REQUEST && !pending && result == 0)
        report_fault("GD REQUEST REJECTED", r7);
    else if(result < 0 && (r7 > KUI_GD_DATATYPE ||
            r7 == KUI_GD_DMA_CALLBACK || r7 == KUI_GD_DMA_TRANSFER || r7 == KUI_GD_DMA_CHECK))
        report_fault("GD FUNCTION UNSUPPORTED", r7);
#ifdef KUI_RETAIL_CE
    ce_events(r7);
    meter_call(started);
#endif
    return result;
}
#ifdef KUI_RETAIL_CE
/* CE's interrupt dispatch calls this for the SCI's events, through the
 * handler table entries the reader installs and kui_retail_ce_isr
 * (retail_resident.S): SR.BL set, on the private stack, which no GD call is
 * using. Returns the SYSINTR CE's dispatch then marks pending and schedules
 * as for any device: the drive's (20) or the G1 DMA end (21), the other
 * one, when both, raised here as CE would; 0 for none. */
uint32_t kui_retail_ce_irq(void) {
    uint32_t started=*(volatile const uint32_t *)(uintptr_t)TCNT0, sysintr=0;
    if(!kui_retail_async_irq()) {
        uint32_t raised=service.interrupts;
        service.interrupts=0;
        if(raised & KUI_RETAIL_GD_IRQ_DMA_END) sysintr=21u;
        if(raised & KUI_RETAIL_GD_IRQ_DRIVE) {
            if(sysintr && kui_retail_ce_kernel[0]) ce_raise(sysintr);
            sysintr=20u;
        }
    }
    meter_busy(started);
    return sysintr;
}
#endif
#else
#ifdef KUI_RETAIL_CE
/* One EXEC of the standard step. */
static int32_t step(uint32_t r4, uint32_t r5) {
    service.step = KUI_RETAIL_GD_STEP_SECTORS;
    int32_t result = kui_retail_gd_dispatch(&service, r4, r5, 0, KUI_GD_EXEC);
    if(service.error == KUI_GD_ERROR_IO)
        report_fault("IMAGE READ FAILED", KUI_GD_EXEC);
    return result;
}
#else
/* One EXEC, sized by the pacing policy and timed for the next estimate. */
static int32_t step(uint32_t r4, uint32_t r5) {
    uint32_t frames = pace.frames, line = pace.line, before = service.diag.sectors_read;
    uint32_t epoch = pace.epoch;
    pace.spin = 0;
    service.step = kui_retail_pace_budget(&pace, KUI_RETAIL_GD_STEP_SECTORS,
                                          KUI_RETAIL_GD_STEP_MAX);
    int32_t result = kui_retail_gd_dispatch(&service, r4, r5, 0, KUI_GD_EXEC);
    if(service.error == KUI_GD_ERROR_IO)
        report_fault("IMAGE READ FAILED", KUI_GD_EXEC);
    uint32_t sectors = service.diag.sectors_read - before;
    if(sectors) {
        video_sample();
        kui_retail_pace_measure(&pace, frames, line, epoch, sectors);
        if(service.step > KUI_RETAIL_GD_STEP_SECTORS) ++pacing.paced;
    }
    return result;
}
#endif
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
#ifdef KUI_RETAIL_CE
    uint32_t started=*(volatile const uint32_t *)(uintptr_t)TCNT0;
    if(ce_record(source,r4,r5,r6,r7)) return 0;
#endif
    if(source>3) return -1;
    if(source!=1 && r6==UINT32_MAX) {
        return 0;
    }
    video_sample();
    uint32_t pending = service.pending;
    /* A game polling CHECK in a tight loop is idle until the read finishes;
     * give it a step, as its EXEC would. The CHECK itself still never reads. */
#ifdef KUI_RETAIL_CE
    /* CE's driver polls CHECK between short sleeps: each one reads a step. */
    if(r7 == KUI_GD_CHECK && pending && (service.command == KUI_GD_PIOREAD ||
       service.command == KUI_GD_DMAREAD))
        (void)step(0, 0);
#else
    if(r7 == KUI_GD_CHECK && pending && (service.command == KUI_GD_PIOREAD ||
       service.command == KUI_GD_DMAREAD) && kui_retail_pace_spin(&pace)) {
        ++pacing.spun;
        (void)step(0, 0);
    }
#endif
    int32_t result = r7 == KUI_GD_EXEC ? step(r4, r5) :
        kui_retail_gd_dispatch(&service, r4, r5, 0, r7);
    if(r7 == KUI_GD_REQUEST && !pending && result == 0)
        report_fault("GD REQUEST REJECTED", r7);
    else if(result < 0 && (r7 > KUI_GD_DATATYPE ||
            r7 == KUI_GD_DMA_CALLBACK || r7 == KUI_GD_DMA_TRANSFER || r7 == KUI_GD_DMA_CHECK))
        report_fault("GD FUNCTION UNSUPPORTED", r7);
#ifdef KUI_RETAIL_CE
    if(service.error == KUI_GD_ERROR_IO)
        report_fault("IMAGE READ FAILED", r7);
    ce_events(r7);
    meter_call(started);
#endif
    return result;
}
#endif
