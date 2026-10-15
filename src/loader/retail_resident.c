/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/retail_resident.h"
#include "kui/retail_gd.h"
#include "kui/retail_image.h"
#include "kui/retail_pace.h"
#include "retail_storage.h"
#include "retail_display.h"
#include "kui/toy_loader_schedule.h"
#include "kui/armada_ce_audio_observe.h"
#if KUI_ARMADA_CE_EXCERPT
#include "kui/armada_ce_excerpt_pool.h"
#endif
#include <stddef.h>
#include <string.h>

#if KUI_RETAIL_RETURN_TRACE && (defined(KUI_RETAIL_ASYNC) || defined(KUI_RETAIL_CE) || KUI_RETAIL_TOY_PILOT || KUI_RETAIL_OBSERVE || KUI_RETAIL_TRANSPORT != 1 || !KUI_RETAIL_LOW_RESIDENT)
#error "Return trace is restricted to the low standard native SCI reader"
#endif

#if KUI_TOY_PILOT_GD_FIXED_STEP && !KUI_TOY_PILOT_SHARED_SCI
#if !KUI_RETAIL_TOY_PILOT || (KUI_TOY_PILOT_GD_FIXED_STEP != 2 && KUI_TOY_PILOT_GD_FIXED_STEP != 3) || defined(KUI_RETAIL_ASYNC) || defined(KUI_RETAIL_CE)
#error "The fixed GD step is restricted to the separate synchronous Toy test"
#endif
#include "kui/toy_pilot_clock.h"
/* Cumulative data-step measurements, separate from the audio snapshot ABI.
 * Read only TMU0; reject timing across an incompatible timer profile. */
static struct {
    uint32_t steps, sectors, ticks, max_ticks, max_sectors, invalid;
} toy_gd_timing;
static int toy_gd_clock_valid(void) {
    return kui_toy_pilot_clock_profile(
        *(volatile const uint16_t *)(uintptr_t)0xffc00000u,
        *(volatile const uint32_t *)(uintptr_t)0xffd80008u,
        *(volatile const uint16_t *)(uintptr_t)0xffd80010u,
        *(volatile const uint8_t *)(uintptr_t)0xffd80004u);
}
#endif

#if KUI_RETAIL_MDK2_CDDA
#include "kui/mdk2_cdda_map.h"
static const struct kui_retail_manifest *mdk2_manifest;
#define RETAIL_MANIFEST mdk2_manifest
#else
static struct kui_retail_manifest manifest;
#define RETAIL_MANIFEST (&manifest)
#endif
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
#if KUI_TOY_PILOT_DATA_SCHEDULE_MODE
#if !KUI_RETAIL_TOY_PILOT || defined(KUI_RETAIL_ASYNC) || defined(KUI_RETAIL_CE)
#error Toy loader DATA scheduling is restricted to the separate native Toy test
#endif
/* BSS is cleared once by the cold boot stage. Keep this latch independent
 * of service INIT/RESET, which belong to the game's GD protocol. */
static uint32_t toy_data_play_accepted;
void kui_retail_toy_data_schedule_play(void) {
    toy_data_play_accepted=1u;
}
#endif
uint32_t kui_retail_original_menu;
extern void kui_retail_menu_hook(void);
extern void kui_retail_gd_c0_hook(void);
extern void kui_retail_gd_1000_hook(void);
extern void kui_retail_gd_10f0_hook(void);
/* Source: 0=BC supervisor vector, 1=C0 raw GD vector, 2/3=direct firmware
 * entries. Assembly publishes this only after acquiring the resident lock. */
volatile uint32_t kui_retail_hook_source, kui_retail_hook_sr;
#if !defined(KUI_RETAIL_CE) && !defined(KUI_RETAIL_ASYNC)
/* Standard native entry captures these before replacing the owner's stack:
 * word 0 is its PR, word 1 its original SP. Never infer SP from our C frame. */
volatile uint32_t kui_retail_native_caller[2];
#endif
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
#if KUI_ARMADA_CE_AUDIO_OBSERVE
#include "armada_ce_audio_observe.inc"
#endif
#if KUI_RETAIL_OBSERVE
#if defined(KUI_RETAIL_CE) || defined(KUI_RETAIL_ASYNC) || KUI_RETAIL_TRANSPORT != 1 || !KUI_RETAIL_LOW_RESIDENT
#error "Observation is native standard SCI in the original low reservation only"
#endif
static enum kui_game_result image_result=(enum kui_game_result)UINT32_MAX;
static enum kui_loader_sd_result observe_read_card_result=(enum kui_loader_sd_result)UINT32_MAX;
static enum kui_loader_sd_result observe_stop_result=(enum kui_loader_sd_result)UINT32_MAX;
static uint32_t observe_read_lba=UINT32_MAX,observe_read_count=UINT32_MAX,observe_card_lba=UINT32_MAX;
#if KUI_RETAIL_SCI_DIAGNOSTIC
#include "retail_sci_observe.inc"
#else
#include "retail_observe.inc"
#endif
#endif
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
#if KUI_ARMADA_CE_EXCERPT
    /* CE's allocator never backs a virtual buffer with this pre-boot
     * reservation. Reject explicit RAM aliases before purge/copy too. */
    if(!kui_armada_ce_excerpt_pool_destination_safe(address,bytes)) return NULL;
#endif
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
#if KUI_RETAIL_OBSERVE
    observe_card_lba=lba; /* Last physical block actually attempted, not cached. */
#endif
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
    return kui_retail_image_check_validated(RETAIL_MANIFEST, lba, count, sector_format(bytes)) == KUI_GAME_OK ? 0 : -1;
}
#ifndef KUI_RETAIL_ASYNC
static int read_sectors(void *unused, uint32_t lba, uint32_t count,
                        uint32_t bytes, void *out) {
    (void)unused;
#if KUI_RETAIL_OBSERVE
    image_result=(enum kui_game_result)UINT32_MAX;
    observe_read_card_result=observe_stop_result=(enum kui_loader_sd_result)UINT32_MAX;
    observe_read_lba=lba;observe_read_count=count;observe_card_lba=UINT32_MAX;
#endif
    if(bytes != 2048 && bytes != 2352) return -1;
    card_result = kui_retail_storage_acquire(&card);
#if KUI_RETAIL_OBSERVE
    observe_read_card_result=card_result;
#endif
    if(card_result != KUI_LOADER_SD_OK) return -1;
    enum kui_game_result result = kui_retail_image_read(&image, lba, count,
        sector_format(bytes), out, (size_t)count * bytes);
#if defined(KUI_RETAIL_CE) || KUI_RETAIL_OBSERVE
    image_result = result;
#endif
#if KUI_RETAIL_OBSERVE
    observe_read_card_result=card_result;
#endif
    enum kui_loader_sd_result stopped = kui_retail_storage_stop(&card);
#if KUI_RETAIL_OBSERVE
    observe_stop_result=stopped;
#endif
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
#if !defined(KUI_RETAIL_CE) && !defined(KUI_RETAIL_ASYNC) && !KUI_RETAIL_OBSERVE
/* retail_display_values uses alias-qualified reads of these aligned words. */
#define terminal_row(legend, snapshot, count) \
    retail_display_values((legend), (const uint32_t *)(const void *)(snapshot), (count))
#if KUI_RETAIL_RETURN_TRACE != 3
static void capture_native_fault(uint32_t function, uint32_t current_param) {
#if KUI_RETAIL_RETURN_TRACE >= 2
    /* The RAM return diagnostic has its own bounded terminal windows.
     * Fatal request errors retain a compact summary with no owner reads. */
    typedef uint32_t terminal_word __attribute__((__may_alias__));
    terminal_word *terminal=(terminal_word *)(void *)image.block;
    _Static_assert(sizeof(image.block) >= 5u * sizeof(*terminal), "fault snapshot");
    terminal[0]=function; terminal[1]=service.diag.reject_reason;
    terminal[2]=kui_retail_native_caller[0]; terminal[3]=kui_retail_native_caller[1];
    terminal[4]=current_param;
    __asm__ __volatile__("" : : : "memory");
#else
    /* These are current caller/request fields. READ parameters were not
     * obtained after a PARAMETERS rejection, so previous LBA/count/destination
     * are deliberately absent. PARAM is the current call's R5, never a guessed
     * stack offset. The bounded trace words are offsets, not a claimed chain. */
    uint32_t sp = kui_retail_native_caller[1], pr = kui_retail_native_caller[0];
    uint32_t param = 0, s20 = 0, s36 = 0, s60 = 0;
    /* This unsigned interval admits P1 only, excludes anything below the IP,
     * and leaves 64 bytes before RAM end without overflowing an addition. */
    if(function == KUI_GD_REQUEST &&
       service.diag.reject_reason == KUI_RETAIL_GD_REJECT_PARAMETERS) {
        param = current_param;
        if(!(sp & 3u) && sp - KUI_RETAIL_IP_ADDRESS <=
           KUI_RETAIL_RAM_END - KUI_RETAIL_IP_ADDRESS - 64u) {
            typedef uint32_t owner_word __attribute__((__may_alias__));
            const volatile owner_word *owner = (const volatile owner_word *)(uintptr_t)sp;
            s20 = owner[5]; s36 = owner[9]; s60 = owner[15];
        }
    }
    /* All owner reads precede cache reuse even if the two ranges overlap.
     * Claiming video or rendering may also change the private-hook words. */
    __asm__ __volatile__("" : : : "memory");
    /* This path never resumes image I/O. Reuse its aligned byte-sector cache
     * rather than adding low BSS or a second trace array to the private stack. */
    typedef uint32_t terminal_word __attribute__((__may_alias__));
    terminal_word *terminal = (terminal_word *)(void *)image.block;
    _Static_assert(sizeof(image.block) >= 9u * sizeof(*terminal), "terminal snapshot");
    terminal[0] = function; terminal[1] = service.diag.last_command;
    terminal[2] = service.diag.reject_reason; terminal[3] = sp; terminal[4] = param;
    terminal[5] = pr; terminal[6] = s20; terminal[7] = s36; terminal[8] = s60;
    __asm__ __volatile__("" : : : "memory");
#endif
}
#endif
#endif
#if KUI_RETAIL_OBSERVE
static void report_fault(const char *reason,uint32_t function,uint32_t param) {
    observe_fault_report(reason,function,param);
    for(;;) __asm__ volatile("nop");
}
#else
#if !defined(KUI_RETAIL_CE) && !defined(KUI_RETAIL_ASYNC)
static void report_fault(const char *reason, uint32_t function, uint32_t current_param) {
#if KUI_RETAIL_RETURN_TRACE == 3
    (void)function;(void)current_param;
#else
    capture_native_fault(function, current_param);
#endif
#else
static void report_fault(const char *reason, uint32_t function) {
#endif
    retail_display_restore(&display);
#if !defined(KUI_RETAIL_CE) && !defined(KUI_RETAIL_ASYNC)
#if KUI_RETAIL_RETURN_TRACE == 3
    /* The early-read diagnostic has its own complete pre-cleanup snapshot.
     * A loader storage fault still stops with its reason, without owner reads. */
#elif KUI_RETAIL_RETURN_TRACE == 2
    retail_display_line("REQUEST REJECTION DETAILS");
#else
    retail_display_line("REQUEST REJECTION DETAILS / NATIVE CALLER STACK");
#endif
#else
    retail_display_line("K-UI READER");
#endif
    retail_display_line(RETAIL_MANIFEST->title);
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
    retail_display_line(reason);
#if KUI_RETAIL_RETURN_TRACE == 3
#elif KUI_RETAIL_RETURN_TRACE == 2
    terminal_row("FN REJECT PR SP PARAM",image.block,5);
#else
    terminal_row("FN CMD REJECT SP PARAM", image.block, 5);
    terminal_row("PR S20 S36 S60", image.block + 20, 4);
#endif
#endif
#if !defined(KUI_RETAIL_CE) && !defined(KUI_RETAIL_ASYNC)
    retail_display_line("STOPPED: PHOTO SCREEN");
    retail_display_line("POWER CYCLE");
#else
    retail_display_line("STOPPED: PHOTO THIS SCREEN");
    retail_display_line("POWER CYCLE FOR K-UI");
#endif
    for(;;) __asm__ volatile("nop");
}
#endif
#if defined(KUI_RETAIL_CE) || defined(KUI_RETAIL_ASYNC)
/* Current R5 is a standard-native terminal field. Keep the other readers'
 * original two-argument function and call ABI, including their stack budget. */
#define report_fault(reason, function, current_param) report_fault((reason), (function))
#endif
#if KUI_RETAIL_RETURN_TRACE == 2
/* Terminal CPU context can be stale; retain it without asserting it caused
 * the return. These reads neither acknowledge nor change exception state. */
static void capture_return_cpu(uint8_t *snapshot) {
    typedef uint32_t terminal_word __attribute__((__may_alias__));
    terminal_word *terminal=(terminal_word *)(void *)snapshot;
    __asm__ volatile("stc vbr,%0" : "=r"(terminal[1]));
    __asm__ volatile("stc spc,%0" : "=r"(terminal[2]));
    terminal[3]=*(volatile const uint32_t *)(uintptr_t)0xff000024u;
    terminal[4]=*(volatile const uint32_t *)(uintptr_t)0xff00000cu;
}
/* Fixed terminal windows, never a pointer walk. The unsigned interval accepts
 * only aligned P1 RAM above the IP and keeps the complete window in RAM.
 * Owner words use the current cached P1 alias; no cache operation or device
 * access is needed. Resident cache storage is below the admitted owner range. */
static void capture_return_memory(uint8_t *snapshot,uint32_t address,unsigned rows) {
    typedef uint32_t terminal_word __attribute__((__may_alias__));
    terminal_word *terminal=(terminal_word *)(void *)snapshot;
    unsigned valid=!(address & 3u) && address-KUI_RETAIL_IP_ADDRESS <=
        KUI_RETAIL_RAM_END-KUI_RETAIL_IP_ADDRESS-rows*16u;
    if(!valid) {
        for(unsigned word=0;word<rows*5u;word++) terminal[word]=0u;
        return;
    }
    for(unsigned row=0;row<rows;row++,address+=16u) {
        terminal[0]=address;
        const volatile terminal_word *owner=(const volatile terminal_word *)(uintptr_t)address;
        terminal[1]=owner[0]; terminal[2]=owner[1];
        terminal[3]=owner[2]; terminal[4]=owner[3];
        terminal+=5;
    }
}
#endif
#if KUI_RETAIL_RETURN_TRACE == 3
typedef uint32_t skies_word __attribute__((__may_alias__));
typedef uint16_t skies_half __attribute__((__may_alias__));
/* Only the fixed typed spans below are admitted. P2, physical, firmware and
 * device addresses are rejected; owner RAM is read through its cached P1
 * alias without cache maintenance or a game-time sample. */
static const volatile skies_word *skies_read_owner(uint32_t address,uint32_t bytes) __attribute__((noinline,noclone,const));
static const volatile skies_word *skies_read_owner(uint32_t address,uint32_t bytes) {
    if((address&3u) || address-KUI_RETAIL_IP_ADDRESS >
       KUI_RETAIL_RAM_END-KUI_RETAIL_IP_ADDRESS-bytes) return NULL;
    return (const volatile skies_word *)(uintptr_t)address;
}
static void capture_skies_read(uint32_t result,uint32_t caller,uint32_t stack) {
    skies_word *out=(skies_word *)(void *)image.block;
    _Static_assert(sizeof(image.block)>=35u*sizeof(*out),"Skies read snapshot");
    for(unsigned i=0;i<35u;i++) out[i]=UINT32_MAX;
    out[0]=result;out[1]=caller;out[2]=stack;out[3]=kui_retail_hook_fault;out[4]=0u;
    out[5]=service.diag.last_function;out[6]=service.diag.last_command;
    out[7]=(uint32_t)service.diag.last_result;out[8]=service.diag.last_error;
    out[9]=service.diag.reject_reason;out[10]=(uint32_t)service.status;
    out[11]=service.pending;out[12]=service.token;out[13]=service.command;
    out[14]=service.diag.last_lba;out[15]=service.diag.last_count;
    out[16]=service.diag.last_destination;
    /* These are the only two fingerprinted fatal branches. A normal E0
     * return is positive command 1 and never interprets owner pointers. */
    if((int32_t)result<=0 && (caller==0x8c0237a6u || caller==0x8c0237e6u)) {
        const volatile skies_word *sp=skies_read_owner(stack,40u);
        if(sp) {
            out[4]|=1u;out[17]=sp[6];out[28]=sp[7];out[34]=sp[8];
            const volatile skies_word *ctx=skies_read_owner(out[17],32u);
            if(ctx) {
                out[4]|=2u;out[18]=ctx[5];out[19]=ctx[4];
                const volatile skies_word *handle=skies_read_owner(out[18],84u);
                if(handle) {
                    out[4]|=4u;out[20]=handle[2];out[21]=handle[4];out[22]=handle[5];
                    out[23]=handle[19];out[25]=handle[11];out[26]=handle[12];
                    out[27]=handle[15];out[29]=((const volatile skies_half *)handle)[40];
                    out[30]=handle[0];
                    const volatile skies_word *device=skies_read_owner(out[30],32u);
                    if(device) {
                        out[4]|=8u;out[24]=device[7];out[31]=device[3];
                        const volatile skies_word *table=skies_read_owner(out[31],68u);
                        if(table) {out[4]|=16u;out[32]=table[1];out[33]=table[16];}
                    }
                }
            }
        }
    }
    __asm__ __volatile__("" : : : "memory");
}
#endif
void kui_retail_menu_return(uint32_t command,uint32_t caller,uint32_t stack
#if KUI_RETAIL_RETURN_TRACE == 2
                           ,uint32_t entry_sr
#endif
                           ) {
#if !KUI_RETAIL_RETURN_TRACE
    (void)command; /* Assembly reaches this only for menu return command 1. */
    (void)caller; (void)stack;
#endif
#if KUI_RETAIL_OBSERVE
    retail_display_restore(&display);
    observe_report();
    retail_display_pause(1200u);
    ((void (*)(void))(uintptr_t)0xa0000000u)();
    for(;;) __asm__ volatile("nop");
#else
#if !defined(KUI_RETAIL_CE) && !defined(KUI_RETAIL_ASYNC)
    /* The return path never resumes game I/O. Capture its terminal fields
     * before claiming video, using the same terminal cache workspace. */
#if KUI_RETAIL_RETURN_TRACE != 3
    typedef uint32_t terminal_word __attribute__((__may_alias__));
    terminal_word *terminal = (terminal_word *)(void *)image.block;
#endif
#if KUI_RETAIL_RETURN_TRACE == 3
    capture_skies_read(command,caller,stack);
#elif KUI_RETAIL_RETURN_TRACE == 2
    /* Entry captured PR and the owner's SP before the private stack switch.
     * Sample CPU context, two caller rows and three stack rows before video restore.
     * Retain the unrounded PR even if its instruction address is 2-aligned. */
    _Static_assert(sizeof(image.block) >= 35u * sizeof(*terminal), "return RAM snapshot");
    terminal[0]=command; terminal[1]=caller; terminal[2]=stack;
    terminal[3]=kui_retail_original_menu; terminal[4]=kui_retail_hook_fault;
    terminal[5]=entry_sr;
    capture_return_cpu(image.block+20u);
    capture_return_memory(image.block+40u,(caller & ~3u)-16u,2u);
    capture_return_memory(image.block+80u,stack,3u);
#elif KUI_RETAIL_RETURN_TRACE == 1
    /* Entry supplied PR and the original game SP before switching stacks.
     * Never dereference that SP. Snapshot before restoring video, and reuse
     * the sector cache only after the game has requested terminal return. */
    _Static_assert(sizeof(image.block) >= 15u * sizeof(*terminal), "return snapshot");
    terminal[0] = command; terminal[1] = caller; terminal[2] = stack;
    terminal[3] = kui_retail_hook_fault; terminal[4] = service.diag.rejected;
    terminal[5] = service.diag.last_function; terminal[6] = service.diag.last_command;
    terminal[7] = (uint32_t)service.diag.last_result; terminal[8] = service.diag.last_error;
    terminal[9] = service.diag.reject_reason;
    terminal[10] = (uint32_t)service.status; terminal[11] = service.pending;
    terminal[12] = service.diag.last_lba; terminal[13] = service.diag.last_count;
    terminal[14] = service.diag.last_destination;
#else
    terminal[0] = kui_retail_hook_fault;
    terminal[1] = service.diag.read_steps; terminal[2] = service.diag.sectors_read;
    terminal[3] = pacing.paced; terminal[4] = pacing.spun;
    terminal[5] = pace.period; terminal[6] = pace.vbi;
    terminal[7] = pace.per; terminal[8] = pace.still;
#endif
    __asm__ __volatile__("" : : : "memory");
#endif
#if KUI_ARMADA_CE_STREAM && KUI_ARMADA_CE_AUDIO_OBSERVE
armada_return_pages:
#endif
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
#elif defined(KUI_RETAIL_ASYNC)
    retail_display_hex("GUARD FAULT",kui_retail_hook_fault);
#endif
#if defined(KUI_RETAIL_ASYNC) && defined(KUI_RETAIL_CE)
    /* Six two-line counter rows plus guard/result fit on one photograph.
     * IRQ/CALL POL are maximum polled blocks in a delivery visit, not
     * time spent with interrupts masked or totals for an entire GD call. */
#if KUI_ARMADA_CE_AUDIO_OBSERVE
    /* A compact audio report replaces the verbose terminal transport rows.
     * Fault screens retain all original transport diagnostics. */
    armada_ce_audio_report();
#else
    const uint32_t *st=(const uint32_t *)&reader.stats;
    retail_display_values("IRQ BLKS CALLBLKS WAITS    EXECS    EXEC INT",st,5);
    retail_display_values("IRQ POL  CALL POL PIOCALLS PIOBYTES",&reader.polled_irq_max,4);
    stream_lines();
#endif
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
#if KUI_RETAIL_RETURN_TRACE == 3
    static const char labels[]="R0 PR SP GUARD V\0FN CMD R0 ERR WHY\0"
        "ST PEN TOK CMD LBA\0CNT DST CTX HND DST\0FAD LEN POS VM BUSY\0"
        "CB ARG TOK JOB ST\0DEV TAB RD EX PR";
    const char *label=labels;
    for(unsigned row=0;row<7u;row++) {
        terminal_row(label,image.block+row*20u,5);
        while(*label++) { }
    }
#elif KUI_RETAIL_RETURN_TRACE == 2
    terminal_row("CMD PR SP MENU GUARD",image.block,5);
    terminal_row("SR VBR SPC EXPEVT TEA",image.block+20u,5);
    for(unsigned row=2;row<7u;row++)
        terminal_row("ADDR WORD0 WORD1 WORD2 WORD3",image.block+row*20u,5);
#elif KUI_RETAIL_RETURN_TRACE == 1
    terminal_row("RETURN PR SP GUARD REFUSED", image.block, 5);
    terminal_row("FN CMD RESULT ERROR REJECT", image.block + 20, 5);
    terminal_row("STATUS PENDING LBA COUNT DEST", image.block + 40, 5);
#else
    /* How the game drives reads: ABXY+Start after a load shows these. */
    terminal_row("GUARD STEP SECT PACE SPIN", image.block, 5);
    /* Latest sampled geometry/cost, possibly changed by a title-screen
     * reset. Unlike PACED STEPS, these are not a history of the fight. */
    terminal_row("PERIOD VBI COST16 STILL", image.block + 20, 4);
#endif
#endif
#if KUI_RETAIL_RETURN_TRACE
#if KUI_RETAIL_RETURN_TRACE == 3
    retail_display_line("PHOTO POWER CYCLE");
#else
    retail_display_line("PHOTO THEN POWER CYCLE");
#endif
    /* Keep the diagnostic available until power-off, without resuming any
     * storage or game execution. Default readers retain their timed reboot. */
    for(;;) __asm__ volatile("nop");
#else
#if KUI_ARMADA_CE_STREAM && KUI_ARMADA_CE_AUDIO_OBSERVE
    retail_display_pause(300u);
#else
    retail_display_line("REBOOT K-UI");
    retail_display_pause(900u); /* ~15 seconds at 60 Hz to capture the counters */
#endif
#if KUI_ARMADA_CE_STREAM && KUI_ARMADA_CE_AUDIO_OBSERVE
    /* Additional terminal pages retain the timing missing from a single
     * cumulative photograph. No game, card, clock or native audio resumes. */
    for(unsigned page=0;page<3u;page++) {
        retail_display_restore(&display);
        armada_ce_audio_history(page);
        retail_display_pause(300u);
    }
    /* Keep all terminal evidence available until physical power-off. */
    goto armada_return_pages;
#endif
    /* Leave through the boot ROM, as KOS arch_reboot() does, with interrupts
     * still masked: the console restarts and boots the K-UI disc in the drive.
     * No game, reader or vector state is relied on afterwards. */
    ((void (*)(void))(uintptr_t)0xa0000000u)();
    for(;;) __asm__ volatile("nop");
#endif
#endif
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
#if KUI_RETAIL_MDK2_CDDA
    if(!kui_mdk2_cdda_stage_map_address((uint32_t)(uintptr_t)prepared))
        return KUI_RETAIL_RESIDENT_ARGUMENT;
    mdk2_manifest=prepared;
#else
    manifest = *prepared;
#endif
    if(kui_retail_storage_adopt(&card, prepared_card) != KUI_LOADER_SD_OK ||
       kui_retail_storage_blocks(&card) < RETAIL_MANIFEST->card_sectors ||
       card.transport != RETAIL_MANIFEST->storage_transport)
        return KUI_RETAIL_RESIDENT_SD;
    /* _start cleared all resident BSS, including image/cache/stream/counters. */
#ifdef KUI_RETAIL_ASYNC
    kui_retail_async_init(RETAIL_MANIFEST);
    const struct kui_gd_ops ops = {NULL, map_guest, check_sectors, NULL};
#else
    image.manifest = RETAIL_MANIFEST;
    image.read_block = read_block;
    image.read_run = read_run;
    const struct kui_gd_ops ops = {NULL, map_guest, check_sectors, read_sectors};
#endif
    kui_retail_gd_init_prepared(&service, RETAIL_MANIFEST, &ops,
        KUI_RETAIL_IP_ADDRESS, KUI_RETAIL_RAM_END);
#if defined(KUI_RETAIL_CE) && defined(KUI_RETAIL_ASYNC)
    service.read_part = kui_retail_async_read_part;
#elif defined(KUI_RETAIL_CE)
    service.read_part = read_part;
#endif
    volatile uint32_t *guard = (volatile uint32_t *)(uintptr_t)(KUI_RETAIL_HOOK_STACK_BOTTOM
#if defined(KUI_RETAIL_TOY_PILOT) && KUI_RETAIL_TOY_PILOT && KUI_TOY_PILOT_PRIVATE_P2
        +0x20000000u
#endif
        );
    for(unsigned i = 0; i < 4; ++i) guard[i] = 0x4b554947u;
    kui_retail_original_menu=*(volatile uint32_t *)(uintptr_t)0x8c0000e0u;
    /* The stage published and invalidated every RAM cache line before _start.
     * Private P2 initialization never creates a cached mutable alias. */
#if !(defined(KUI_RETAIL_TOY_PILOT) && KUI_RETAIL_TOY_PILOT && KUI_TOY_PILOT_PRIVATE_P2)
    /* The game's first instructions may reinitialize caches. Publish P1
     * manifest/card/guard state before that handoff. */
    purge((uint32_t)(uintptr_t)__retail_resident_bss_begin,
          (uint32_t)(__retail_resident_bss_end - __retail_resident_bss_begin));
    purge(KUI_RETAIL_HOOK_STACK_BOTTOM, 16);
#endif
    install_hook();
    return KUI_RETAIL_RESIDENT_OK;
}
#if KUI_RETAIL_MDK2_CDDA
int kui_mdk2_cdda_manifest_bind(const struct kui_retail_manifest *temporary,
                              const struct kui_retail_manifest *owned) {
    uint32_t sr;
    __asm__ volatile("stc sr,%0":"=r"(sr)::"memory");
    if((sr&UINT32_C(0x100000f0))!=UINT32_C(0x100000f0) ||
       kui_retail_hook_active || !service.initialized || service.executing ||
       !kui_mdk2_cdda_stage_map_address((uint32_t)(uintptr_t)temporary) ||
       !kui_mdk2_cdda_owned_map_address((uint32_t)(uintptr_t)owned) ||
       mdk2_manifest!=temporary || image.manifest!=temporary ||
       service.tracks!=temporary->slots ||
       memcmp(temporary,owned,sizeof(*owned))) return 0;
    kui_retail_hook_active=1u;
    /* No GD reset: token, pending command, cursor, cached card block, and
     * completion state continue to describe the exact same validated map. */
    image.manifest=owned;
    service.tracks=owned->slots;
    mdk2_manifest=owned;
    __asm__ volatile("":::"memory");
    kui_retail_hook_active=0u;
    return 1;
}
#endif
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
#if !KUI_ARMADA_CE_STREAM
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
#else
/* The full player keeps terminal counters in its high controller. Repeated
 * framebuffer drawing inside CE's masked reader competes with FMV audio and
 * needlessly occupies the constrained low resident. */
static inline void meter_busy(uint32_t started) {(void)started;}
static inline void meter_call(uint32_t started) {(void)started;}
#endif
/* After each call: the interrupts the service raised, G1 DMA end (SYSINTR
 * 21) and the drive's (SYSINTR 20) as CE's platform maps them; and a PIO
 * stream's callback, due after a transfer, made when this EXEC returns, as
 * the BIOS would make it within its own EXEC. */
static void ce_events(uint32_t r7) {
    if(service.interrupts) {
        if(!kui_retail_ce_kernel[0])
            report_fault("CE KERNEL INTERRUPTS NOT FOUND", r7, 0);
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
#if KUI_ARMADA_CE_STREAM
/* The audio worker can release SCI without another SCI interrupt. Flush the
 * DATA wake it has queued through CE's already admitted InterruptPending
 * entry. This caller holds the same bounded resident guard as transport;
 * PIO callbacks remain deferred exclusively by the driver's GD_EXEC. */
void kui_retail_ce_stream_wake(void) {
    uint32_t raised=service.interrupts;
    if(!raised || !kui_retail_ce_kernel[0]) return;
    service.interrupts=0;
    if(raised&KUI_RETAIL_GD_IRQ_DMA_END) ce_raise(21u);
    if(raised&KUI_RETAIL_GD_IRQ_DRIVE) ce_raise(20u);
}
#endif
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
    kui_retail_async_call(r7);
    int32_t result = kui_retail_gd_dispatch(&service, r4, r5, 0, r7);
#if KUI_ARMADA_CE_AUDIO_OBSERVE
    armada_ce_audio_record(r7, r4, result);
#endif
    kui_retail_async_after(r7, result);
    if(service.error == KUI_GD_ERROR_IO)
        report_fault("IMAGE READ FAILED", r7, r5);
    if(r7 == KUI_GD_REQUEST && !service.command && result == 0)
#if KUI_RETAIL_OBSERVE
        report_fault("REQ REFUSED", r7, r5);
#else
        report_fault("GD REQUEST REJECTED", r7, r5);
#endif
    else if(result < 0 && (r7 > KUI_GD_DATATYPE ||
            r7 == KUI_GD_DMA_CALLBACK || r7 == KUI_GD_DMA_TRANSFER || r7 == KUI_GD_DMA_CHECK))
#if KUI_RETAIL_OBSERVE
        report_fault("GD UNSUPPORTED", r7, r5);
#else
        report_fault("GD FUNCTION UNSUPPORTED", r7, r5);
#endif
#ifdef KUI_RETAIL_CE
    ce_events(r7);
#if KUI_ARMADA_CE_STREAM
    armada_ce_audio_kick(r7);
#endif
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
        report_fault("IMAGE READ FAILED", KUI_GD_EXEC, 0);
    return result;
}
#else
/* One EXEC, sized by the pacing policy and timed for the next estimate. */
#if KUI_TOY_PILOT_DATA_SCHEDULE_MODE == 1
/* Keep the single-sector branch on its own bounded frame rather than
 * duplicating that frame in the low dispatcher under size-optimizing LTO. */
__attribute__((noinline))
#endif
static int32_t step(uint32_t r4, uint32_t r5) {
#if KUI_TOY_PILOT_SHARED_SCI
    pace.spin=0;service.step=KUI_TOY_PILOT_GD_FIXED_STEP;
#elif KUI_TOY_PILOT_GD_FIXED_STEP
    uint32_t before = service.diag.sectors_read;
    /* Zero is also rejected at a counter-wrap boundary: count that sample
     * as untimed rather than retaining a separate flag on the small GD stack. */
    uint32_t started = toy_gd_clock_valid() ?
        *(volatile const uint32_t *)(uintptr_t)0xffd8000cu : 0u;
    pace.spin = 0;
#if KUI_TOY_PILOT_DATA_SCHEDULE_MODE == 1
    service.step = kui_toy_loader_schedule_step(toy_data_play_accepted);
#else
    service.step = KUI_TOY_PILOT_GD_FIXED_STEP;
#endif
#else
    uint32_t frames = pace.frames, line = pace.line, before = service.diag.sectors_read;
    uint32_t epoch = pace.epoch;
    pace.spin = 0;
    service.step = kui_retail_pace_budget(&pace, KUI_RETAIL_GD_STEP_SECTORS,
                                          KUI_RETAIL_GD_STEP_MAX);
#endif
    int32_t result = kui_retail_gd_dispatch(&service, r4, r5, 0, KUI_GD_EXEC);
    if(service.error == KUI_GD_ERROR_IO)
        report_fault("IMAGE READ FAILED", KUI_GD_EXEC, 0);
#if !KUI_TOY_PILOT_SHARED_SCI
    uint32_t sectors = service.diag.sectors_read - before;
    if(sectors) {
#if KUI_TOY_PILOT_GD_FIXED_STEP
        uint32_t elapsed = started - *(volatile const uint32_t *)(uintptr_t)0xffd8000cu;
        ++toy_gd_timing.steps;
        toy_gd_timing.sectors += sectors;
        if(sectors > toy_gd_timing.max_sectors) toy_gd_timing.max_sectors = sectors;
        if(started && toy_gd_clock_valid() && elapsed) {
            toy_gd_timing.ticks += elapsed;
            if(elapsed > toy_gd_timing.max_ticks) toy_gd_timing.max_ticks = elapsed;
        } else ++toy_gd_timing.invalid;
#else
        video_sample();
        kui_retail_pace_measure(&pace, frames, line, epoch, sectors);
        if(service.step > KUI_RETAIL_GD_STEP_SECTORS) ++pacing.paced;
#endif
    }
#endif
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
#if KUI_RETAIL_OBSERVE
    observe_call();
#endif
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
#if KUI_RETAIL_OBSERVE
    observe_result(r7,result);
#endif
    if(r7 == KUI_GD_REQUEST && !service.command && result == 0)
        report_fault("GD REQUEST REJECTED", r7, r5);
    else if(result < 0 && (r7 > KUI_GD_DATATYPE ||
            r7 == KUI_GD_DMA_CALLBACK || r7 == KUI_GD_DMA_TRANSFER || r7 == KUI_GD_DMA_CHECK))
        report_fault("GD FUNCTION UNSUPPORTED", r7, r5);
#ifdef KUI_RETAIL_CE
    if(service.error == KUI_GD_ERROR_IO)
        report_fault("IMAGE READ FAILED", r7, r5);
    ce_events(r7);
    meter_call(started);
#endif
    return result;
}
#endif
