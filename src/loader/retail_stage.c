/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/retail_loader_layout.h"
#include "kui/retail_image.h"
#include "kui/retail_resident.h"
#include "retail_storage.h"
#include "retail_display.h"
#include "retail_boot.h"
#ifdef KUI_RETAIL_SD_BENCH
#include "retail_sd_bench.h"
#endif
#ifdef KUI_RETAIL_CE
#include "kui/ce_load_plan.h"
#include <stdbool.h>
#endif
#include <stddef.h>
#include <stdint.h>
#include <string.h>

extern const uint8_t __retail_resident_scif_blob_start[] __asm__("__retail_resident_scif_blob_start");
extern const uint8_t __retail_resident_scif_blob_end[] __asm__("__retail_resident_scif_blob_end");
extern const uint8_t __retail_resident_sci_blob_start[] __asm__("__retail_resident_sci_blob_start");
extern const uint8_t __retail_resident_sci_blob_end[] __asm__("__retail_resident_sci_blob_end");
extern const uint8_t __retail_resident_ide_blob_start[] __asm__("__retail_resident_ide_blob_start");
extern const uint8_t __retail_resident_ide_blob_end[] __asm__("__retail_resident_ide_blob_end");
extern const uint8_t __retail_resident_scia_blob_start[] __asm__("__retail_resident_scia_blob_start");
extern const uint8_t __retail_resident_scia_blob_end[] __asm__("__retail_resident_scia_blob_end");
extern const uint8_t __retail_trampoline_start[] __asm__("__retail_trampoline_start");
extern const uint8_t __retail_trampoline_end[] __asm__("__retail_trampoline_end");
extern void kui_retail_bootstrap_enter(void) __attribute__((noreturn));
/* Handoff screens stay up about half a second: long enough to see, while a
 * failure still leaves its last screen for a photograph. */
#define HANDOFF_PAUSE_FRAMES 30u
#ifdef KUI_RETAIL_CE
/* The Windows CE boot test holds each handoff screen for two seconds, so a
 * reset right afterwards still leaves the last step in a photograph. It
 * places only the trampoline's first 64 bytes (all of its code): the CE body
 * keeps its "ECEC" signature and ROM header pointer at offset 0x40 while
 * bootstrap 2 runs. */
#define STEP_PAUSE_FRAMES 120u
#define ENTRY_PATCH_BYTES 64u
extern const uint8_t __retail_ce_vbr[] __asm__("__retail_ce_vbr");
#else
#define STEP_PAUSE_FRAMES HANDOFF_PAUSE_FRAMES
#define ENTRY_PATCH_BYTES KUI_RETAIL_TRAMPOLINE_BYTES
#endif
extern void kui_retail_stage_sync(void);

/* High storage is temporary: no pointer to it survives the final handoff.
 * The ordinary reader leaves firmware low32KiB in place; the opt-in low
 * placement preserves its published lower16KiB and checks retained vectors.
 * The owner's IP metadata/TOC stay in place throughout.
 * Bootstrap2 runs at its original address with the independent reader already
 * installed in its reserved RAM. No proprietary bootstrap is bundled. */
static uint8_t wire_copy[KUI_RETAIL_MAP_BYTES];
static uint8_t original_entry[KUI_RETAIL_TRAMPOLINE_BYTES];
static struct kui_retail_manifest manifest;
static struct kui_retail_image image;
static struct kui_retail_storage card;
static struct retail_display_state display;
static enum kui_loader_sd_result last_card_result;
/* Raw boot sectors are checked, then their 2048 data bytes copied into place.
 * Cooked boot sectors are copied directly and checked against the executable
 * CRC captured while preparing the image, including CE's load prefix.
 * The executable's CRC32 is taken after loading, for the relay's check that
 * bootstrap 2 left it unchanged. */
#define BOOT_CHUNK_SECTORS 16u
static uint8_t raw_boot[BOOT_CHUNK_SECTORS*KUI_GAME_RAW_BYTES];
static uint32_t boot_crc;
static uint32_t source_boot_crc, boot_cooked;
#ifndef KUI_RETAIL_CE
static uint16_t scramble_index[KUI_RETAIL_SCRAMBLE_SLICES];
static uint8_t scramble_seen[KUI_RETAIL_SCRAMBLE_SLICES/8u];
#endif
/* Bytes of the executable at KUI_RETAIL_EXEC_ADDRESS: the boot file, or for
 * Windows CE its body (the file less its 2048-byte load prefix). */
static uint32_t exec_bytes;
static const uint8_t *resident_blob;
static size_t resident_bytes;
static uint32_t resident_limit;

#ifndef KUI_RETAIL_SONIC_STACK_TEST
#define KUI_RETAIL_SONIC_STACK_TEST 0
#endif
#if KUI_RETAIL_SONIC_STACK_TEST && !defined(KUI_RETAIL_CE)
#if defined(KUI_RETAIL_STARTUP_TRACE) && KUI_RETAIL_STARTUP_TRACE
#error Sonic scoped stack and startup trace must be separate builds
#endif
/* An exact-owner, RAM-only proof. Keep the caller's frames on its real stack,
 * and relocate only the two confirmed startup frames that hit the reader.
 * Public workspace registration is cleared before return; internal flash
 * state can retain a stored address until the next operation resets it. */
#define SONIC_STACK_PATCH_BYTES 12u
#define SONIC_STACK_RETURN_POINT 7u
#define SONIC_STACK_FRAME_WORDS 21u
#define SONIC_STACK_FRAME_BYTES (SONIC_STACK_FRAME_WORDS*4u)
#define SONIC_STACK_OWNER_BYTES 32768u
#define SONIC_STACK_BOOK_BYTES 4096u
#define SONIC_STACK_WAIT_BUDGET 3000000u
#define SONIC_STACK_GUARD_WORD 0x4b554953u
#define SONIC_STACK_SNAPSHOT_BEGIN KUI_RETAIL_IP_ADDRESS
#define SONIC_STACK_RESIDENT_BYTES (KUI_RETAIL_ASYNC_LIMIT-SONIC_STACK_SNAPSHOT_BEGIN)
uint8_t kui_retail_sonic_owner_stack[SONIC_STACK_OWNER_BYTES]
    __attribute__((aligned(32),section(".bss.sonic_owner_stack")));
uint8_t kui_retail_sonic_texture_stack[SONIC_STACK_OWNER_BYTES]
    __attribute__((aligned(32),section(".bss.sonic_texture_stack")));
uint32_t kui_retail_sonic_stack_resume;
uint8_t kui_retail_sonic_book_stack[SONIC_STACK_BOOK_BYTES]
    __attribute__((aligned(32),section(".bss.sonic_book_stack")));
struct sonic_stack_state {
    uint32_t resume,armed,restored,active,completed,scope;
    uint32_t owner_bottom,owner_top,book_bottom,book_top,stage_end;
    uint32_t original_frame,original_sp,original_pr,entry_ccr,return_ccr;
    uint32_t first[2],last[2],reads[2],ccr[7];
    uint32_t asset_status,asset_handle,asset_bytes;
    uint32_t resident_end,resident_crc,return_crc,mismatch_address;
    uint32_t transport,reader,changed_bytes,last_changed;
    uint32_t code_end,code_crc,return_code_crc;
    uint32_t word_address,word_old,word_p1,word_p2;
    uint32_t bss_begin,bss_end,manifest_address,manifest_bytes,manifest_matches;
    uint32_t first_changed;
    uint32_t compressed_bytes,compressed_source,decoded_bytes;
    uint8_t saved[7][SONIC_STACK_PATCH_BYTES];
    uint8_t resident[SONIC_STACK_RESIDENT_BYTES];
};
struct sonic_stack_state kui_retail_sonic_stack_state __attribute__((aligned(32)));
struct sonic_stack_state kui_retail_sonic_texture_state __attribute__((aligned(32)));
extern void kui_retail_sonic_g2_fifo(void);
extern void kui_retail_sonic_g2_busy(void);
extern void kui_retail_sonic_scope_entry(void);
extern void kui_retail_sonic_scope_return(void);
extern void kui_retail_sonic_asset_guard(void);
extern void kui_retail_sonic_texture_entry(void);
extern void kui_retail_sonic_texture_return(void);
extern void kui_retail_sonic_compressed_guard(void);
extern void kui_retail_sonic_decode_guard(void);
static const uint32_t sonic_stack_address[7]={0x8c110b00u,0x8c1107ccu,0x8c094c88u,0x8c095b20u,
    0x8c09b3f0u,0x8c09b4b8u,0x8c09b4f0u};
static void (*const sonic_stack_target[7])(void)={kui_retail_sonic_g2_fifo,
    kui_retail_sonic_g2_busy,kui_retail_sonic_scope_entry,kui_retail_sonic_asset_guard,
    kui_retail_sonic_texture_entry,kui_retail_sonic_compressed_guard,kui_retail_sonic_decode_guard};
#ifdef KUI_RETAIL_SONIC_STACK_TEST_HOST
extern uint32_t kui_retail_sonic_stack_read(uint32_t address);
extern void kui_retail_sonic_stack_publish(uint32_t address,size_t bytes);
extern uint32_t kui_retail_sonic_stack_allocation(unsigned allocation);
#else
extern const uint8_t __retail_stage_bss_end[] __asm__("__retail_stage_bss_end");
static uint32_t kui_retail_sonic_stack_read(uint32_t address) {
    return *(volatile const uint32_t *)(uintptr_t)address;
}
static void kui_retail_sonic_stack_publish(uint32_t address,size_t bytes) {
    for(uint32_t line=address&~31u;line<address+(uint32_t)bytes;line+=32u)
        __asm__ volatile("ocbi @%0" : : "r"(line) : "memory");
    __asm__ volatile("" : : : "memory");
}
static uint32_t kui_retail_sonic_stack_allocation(unsigned allocation) {
    if(allocation==0u) return (uint32_t)(uintptr_t)kui_retail_sonic_owner_stack;
    if(allocation==1u) return (uint32_t)(uintptr_t)kui_retail_sonic_book_stack;
    if(allocation==2u) return (uint32_t)(uintptr_t)&kui_retail_sonic_stack_state;
    if(allocation==4u) return (uint32_t)(uintptr_t)kui_retail_sonic_texture_stack;
    if(allocation==5u) return (uint32_t)(uintptr_t)&kui_retail_sonic_texture_state;
    return (uint32_t)(uintptr_t)__retail_stage_bss_end;
}
#endif
static void sonic_stack_arm(void);
#endif

#if defined(KUI_RETAIL_STARTUP_TRACE) && KUI_RETAIL_STARTUP_TRACE && !defined(KUI_RETAIL_CE)
/* Opt-in owner startup diagnosis, entirely in the temporary high stage.
 * Addresses identify the supplied MK-51000 V1.004 image; instruction bytes
 * are saved from the verified RAM image, never bundled with K-UI. */
#define STARTUP_TRACE_BYTES 12u
#if KUI_RETAIL_STARTUP_TRACE >= 3
#define STARTUP_TRACE_POINTS 5u
extern void kui_retail_startup_trace_mount(void);
#else
#define STARTUP_TRACE_POINTS 4u
#endif
#define STARTUP_TRACE_BUDGET 3000000u
#define STARTUP_TRACE_STACK_BYTES 4096u
extern void kui_retail_startup_trace_scan(void);
extern void kui_retail_startup_trace_g2(void);
extern void kui_retail_startup_trace_pvr(void);
extern void kui_retail_startup_trace_gd(void);
uint8_t kui_retail_startup_trace_stack[STARTUP_TRACE_STACK_BYTES]
    __attribute__((aligned(32),section(".bss.startup_trace_stack")));
uint32_t kui_retail_startup_trace_resume;
static const uint32_t startup_trace_address[STARTUP_TRACE_POINTS] = {
    0x8c6082d4u,0x8c6083b0u,0x8c6085bcu,0x8c603d78u
#if KUI_RETAIL_STARTUP_TRACE >= 3
    ,0x8c603d4cu /* Common SDK mount epilogue; never resumed. */
#endif
};
static void (*const startup_trace_target[STARTUP_TRACE_POINTS])(void) = {
    kui_retail_startup_trace_scan,kui_retail_startup_trace_g2,
    kui_retail_startup_trace_pvr,kui_retail_startup_trace_gd
#if KUI_RETAIL_STARTUP_TRACE >= 3
    ,kui_retail_startup_trace_mount
#endif
};
static struct {
    uint8_t saved[STARTUP_TRACE_POINTS][STARTUP_TRACE_BYTES];
    uint32_t first[STARTUP_TRACE_POINTS],last[STARTUP_TRACE_POINTS];
    uint32_t reads[STARTUP_TRACE_POINTS],ccr[STARTUP_TRACE_POINTS];
    uint32_t armed,passed;
} startup_trace;

#ifdef KUI_RETAIL_STARTUP_TRACE_TEST
extern uint32_t kui_retail_startup_trace_read(uint32_t address);
extern void kui_retail_startup_trace_publish(uint32_t address,size_t bytes);
extern uint32_t kui_retail_startup_trace_stack_address(void);
#else
static uint32_t kui_retail_startup_trace_read(uint32_t address) {
    return *(volatile const uint32_t *)(uintptr_t)address;
}
static uint32_t kui_retail_startup_trace_stack_address(void) {
    return (uint32_t)(uintptr_t)kui_retail_startup_trace_stack;
}
static void kui_retail_startup_trace_publish(uint32_t address,size_t bytes) {
    uint32_t begin=address&~31u,end=address+(uint32_t)bytes;
    for(uint32_t line=begin;line<end;line+=32u)
        __asm__ volatile("ocbi @%0" : : "r"(line) : "memory");
    /* Both the installer and one-shot wrappers run with caches disabled.
     * Publish RAM bytes before the wrapper restores the owner's CCR. */
    __asm__ volatile("" : : : "memory");
}
#endif
static void startup_trace_arm(void);
#if KUI_RETAIL_STARTUP_TRACE >= 2
/* Mutable observer state is accessed only through the explicit P2 pointer
 * passed by assembly. The native reader, its stack and its service are intact. */
#define STARTUP_GD_CALL_BUDGET 4096u
struct startup_gd_state {
    uint32_t handler,active,calls;
    uint32_t original[21],frame_address,ccr;
    uint32_t function,command,caller,result;
    uint32_t init_result,init_token,init_status;
    uint32_t drive_result,drive_status,drive_type;
    uint32_t version_token,version_status,version_destination,version_crc;
    uint32_t read_command,read_fad,read_count,read_destination,read_token;
    uint32_t read_status,check[4],pvd_header,pvd_crc;
    uint32_t candidate_fad,candidate_count,candidate_destination;
#if KUI_RETAIL_STARTUP_TRACE >= 3
    uint32_t mount_result,read_requests,completed_reads,read_completed,pvd_captured,pvd_valid;
    uint32_t pvd_sector_bytes,root_lba,root_bytes,path_lba,path_bytes;
#endif
};
struct startup_gd_state kui_retail_startup_gd_state;
extern void kui_retail_startup_gd_proxy(void);
extern void kui_retail_startup_gd_return(void);
static void startup_gd_install(void);
#endif
#endif

static void select_resident(void) {
    const uint8_t *end;
    resident_limit=KUI_RETAIL_STANDARD_LIMIT;
    /* Decoding accepted the background reader only for SCI with tracks and
     * extents in at most KUI_RETAIL_ASYNC_SLOTS slots, the most its resident
     * holds. */
    if(manifest.reader!=KUI_RETAIL_READER_STANDARD) {
        resident_blob=__retail_resident_scia_blob_start;
        resident_bytes=(size_t)(__retail_resident_scia_blob_end-resident_blob);
#ifdef KUI_RETAIL_CE
        resident_limit=KUI_RETAIL_CE_ASYNC_LIMIT; /* The Windows CE background reader. */
#else
        resident_limit=KUI_RETAIL_ASYNC_LIMIT;
#endif
        return;
    }
    switch(manifest.storage_transport) {
        case KUI_STORAGE_SCIF:
            resident_blob=__retail_resident_scif_blob_start;
            end=__retail_resident_scif_blob_end;
            break;
        case KUI_STORAGE_SCI:
            resident_blob=__retail_resident_sci_blob_start;
            end=__retail_resident_sci_blob_end;
#ifdef KUI_RETAIL_CE
            resident_limit=KUI_RETAIL_CE_LIMIT; /* The Windows CE reader. */
#endif
            break;
        default: /* Manifest decoding has already rejected every other ID. */
            resident_blob=__retail_resident_ide_blob_start;
            end=__retail_resident_ide_blob_end;
            break;
    }
    resident_bytes=(size_t)(end-resident_blob);
}

static void retire_launcher_serial(void) {
    /* KOS scif_spi_shutdown() calls scif_init(), leaving TE/RE enabled; its
     * arch_shutdown does not turn them off. We are now the sole owner, with
     * KOS stopped and interrupts masked. Retire that launcher-only UART/FIFO
     * state once, using the pinned KOS SCIF-SPI initialization sequence.
     * This function is absent from the resident: game-time acquisition must
     * continue to reject active serial IO and preserve the game's controls. */
    *(volatile uint16_t *)(uintptr_t)0xffe80008u=0;
    *(volatile uint16_t *)(uintptr_t)0xffe80018u=6;
    *(volatile uint16_t *)(uintptr_t)0xffe80018u=0;
}

static void stopped(const char *message,uint32_t detail) __attribute__((noreturn));
static void stopped(const char *message,uint32_t detail) {
    retail_display_line(message);
    retail_display_hex("DETAIL",detail);
    retail_display_line("LAUNCH STOPPED - PHOTOGRAPH THIS SCREEN");
    retail_display_line("POWER OFF AND ON TO RETURN");
    retail_display_line("STORAGE WAS READ ONLY");
    for(;;) __asm__ volatile("nop");
}
static int physical_read(void *context,uint32_t lba,uint8_t out[512]) {
    last_card_result=kui_retail_storage_read_run(context,lba,1,out);
    return last_card_result==KUI_LOADER_SD_OK?0:-1;
}
static int physical_run(void *context,uint32_t lba,uint32_t available,uint8_t out[512]) {
    last_card_result=kui_retail_storage_read_run(context,lba,available,out);
    return last_card_result==KUI_LOADER_SD_OK?0:-1;
}
static void read_sectors(uint32_t lba,uint32_t count,enum kui_game_sector_format format,void *out) {
    last_card_result=kui_retail_storage_acquire(&card);
    if(last_card_result!=KUI_LOADER_SD_OK)
        stopped("STORAGE BUS NOT AVAILABLE",(uint32_t)last_card_result);
    size_t bytes=format==KUI_GAME_SECTOR_RAW?KUI_GAME_RAW_BYTES:KUI_GAME_DATA_BYTES;
    enum kui_game_result result=kui_retail_image_read(&image,lba,count,
        format,out,(size_t)count*bytes);
    enum kui_loader_sd_result stop_result=kui_retail_storage_stop(&card);
    if(last_card_result==KUI_LOADER_SD_OK) last_card_result=stop_result;
    if(stop_result!=KUI_LOADER_SD_OK) image.cache_valid=0;
    kui_retail_storage_release(&card);
    if(result!=KUI_GAME_OK || last_card_result!=KUI_LOADER_SD_OK) {
        retail_display_hex("IMAGE LBA",lba);
        retail_display_hex("STORAGE RESULT",(uint32_t)last_card_result);
        if(card.transport!=KUI_STORAGE_IDE) {
            retail_display_hex("SD COMMAND",card.device.sd.last_command);
            retail_display_hex("SD RESPONSE",card.device.sd.last_response);
        }
        stopped("IMAGE READ FAILED",(uint32_t)result);
    }
}
/* Loads payload sectors from lba. Each chunk stays within one backing track;
 * mixed cooked data/raw audio maps retain their physical file stride. Raw
 * data keeps the original header checks, including its absolute address. */
static void load_sectors(uint32_t lba,uint32_t sectors,uint32_t file_bytes,uint8_t *out) {
    for(uint32_t done=0;done<sectors;) {
        const struct kui_retail_track *track=NULL;
        for(uint32_t i=0;i<manifest.track_count;i++) {
            const struct kui_retail_track *t=&manifest.slots[i].track;
            if(lba+done>=t->start_lba && lba+done<t->end_lba) { track=t; break; }
        }
        if(!track) stopped("BOOT SECTOR OUTSIDE TRACK",lba+done);
        uint32_t count=sectors-done;
        if(count>BOOT_CHUNK_SECTORS) count=BOOT_CHUNK_SECTORS;
        if(count>track->end_lba-lba-done) count=track->end_lba-lba-done;
        uint32_t stride=kui_retail_track_sector_bytes(track),failed_lba=0;
        uint8_t *destination=out+(size_t)done*KUI_GAME_DATA_BYTES;
        enum kui_retail_header header=KUI_RETAIL_HEADER_OK;
        if(stride==KUI_GAME_RAW_BYTES && !(track->control & KUI_RETAIL_TRACK_MODE2) &&
            !(manifest.flags & KUI_RETAIL_IMAGE_CD)) {
            read_sectors(lba+done,count,KUI_GAME_SECTOR_RAW,raw_boot);
            header=kui_retail_boot_copy(raw_boot,destination,
                lba+done,count,stride,&failed_lba);
        } else {
            read_sectors(lba+done,count,KUI_GAME_SECTOR_MODE1,raw_boot);
            memcpy(destination,raw_boot,(size_t)count*KUI_GAME_DATA_BYTES);
        }
        if(header!=KUI_RETAIL_HEADER_OK) {
            retail_display_hex("BOOT SECTOR LBA",failed_lba);
            stopped(header==KUI_RETAIL_HEADER_ADDRESS?"BOOT SECTOR ADDRESS MISMATCH":
                "BOOT SECTOR IS NOT MODE 1 DATA",(uint32_t)header);
        }
        if(boot_cooked || (manifest.flags & KUI_RETAIL_IMAGE_BOOT_CRC)) {
            uint32_t bytes=count*KUI_GAME_DATA_BYTES;
            uint32_t left=file_bytes-done*KUI_GAME_DATA_BYTES;
            if(bytes>left) bytes=left;
            source_boot_crc=kui_retail_crc32(source_boot_crc,destination,bytes);
        }
        done+=count;
        retail_display_progress(done,sectors);
    }
}
#ifdef KUI_RETAIL_CE
/* Windows CE boot test. The IP's peripheral field (seven hexadecimal digits
 * at 0x38, read as game_metadata.c does) must select Windows CE. The boot
 * file's first sector is CE's load prefix, the rest its body. Both are loaded
 * where the CE layout puts them and checked; the stage then enters bootstrap 2
 * as for a native game, whose jump to the body start reaches the relay.
 * Returns the body's byte count. */
static uint8_t ce_prefix[KUI_CE_LOAD_PREFIX_BYTES];
static bool ip_windows_ce(const uint8_t *ip) {
    if((memcmp(ip+37,"GD-ROM",6) && memcmp(ip+37,"CD-ROM",6)) || ip[63]!=' ') return false;
    uint32_t flags=0;
    for(unsigned i=56;i<63;i++) {
        unsigned digit=ip[i];
        if(digit>='0' && digit<='9') digit-='0';
        else if(digit>='A' && digit<='F') digit-='A'-10u;
        else if(digit>='a' && digit<='f') digit-='a'-10u;
        else return false;
        flags=flags<<4|digit;
    }
    return flags&1u;
}
static uint32_t word(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
/* Windows CE's interrupt dispatch, just after the platform's handler
 * returns a SYSINTR in R0 (ARMADA's nk.exe; matched, not copied): a device
 * interrupt sets its pending bit, queues SYSINTR-8 in a 32-entry ring and
 * requests a reschedule. Values with a zero mask byte are PC-relative
 * displacements; the three MOV.L literals they load (instructions 4, 11 and
 * 20) are the pending mask, the ring head and the reschedule flag. */
static const uint16_t ce_irq_code[21][2]={
    {0x70f8,0xffff},{0x4011,0xffff},{0x8f00,0xff00},{0xe301,0xffff},
    {0xd100,0xff00},{0x6212,0xffff},{0x430d,0xffff},{0x2238,0xffff},
    {0x8f00,0xff00},{0x223b,0xffff},{0x2122,0xffff},{0xd100,0xff00},
    {0x6312,0xffff},{0x313c,0xffff},{0x8014,0xffff},{0x3138,0xffff},
    {0x6033,0xffff},{0x7001,0xffff},{0xc91f,0xffff},{0x2102,0xffff},
    {0xd100,0xff00},
};
/* The same dispatch's start, its VBR+0x600 entry, up to the code above:
 * the handler for the interrupt is called from a table indexed by INTEVT/8
 * (the MOV.L literal of instruction 1), with CE's PR kept in R7 meanwhile,
 * and a SYSINTR of 0 or 2 is handled apart. */
static const uint16_t ce_entry_code[15][2]={
    {0x567a,0xffff},{0xd000,0xff00},{0x6163,0xffff},{0x4109,0xffff},
    {0x4101,0xffff},{0x011e,0xffff},{0x072a,0xffff},{0x410b,0xffff},
    {0x0009,0xffff},{0x472a,0xffff},{0xd700,0xff00},{0x8800,0xffff},
    {0x8900,0xff00},{0x8802,0xffff},{0x8900,0xff00},
};
static uint32_t ce_kernel[KUI_RETAIL_CE_KERNEL_WORDS];
static uint32_t ce_literal(uint32_t at) {
    uint16_t code=*(const uint16_t *)(uintptr_t)at;
    return word((const uint8_t *)(uintptr_t)(((at&~3u)+4u)+(code&0xffu)*4u));
}
static bool ce_matches(uint32_t at,const uint16_t (*pattern)[2],unsigned count) {
    const uint16_t *code=(const uint16_t *)(uintptr_t)at;
    for(unsigned i=0;i<count;i++) if((code[i]&pattern[i][1])!=pattern[i][0]) return false;
    return true;
}
/* Exactly one match in the body, each literal a word-aligned P1 RAM address,
 * or ce_kernel stays zero and the reader stops if CE needs it. The handler
 * table (for the background reader) is taken only from the matching entry
 * and only where CE's kernel data keeps it: 0xC4 bytes after the pending
 * mask in every kernel seen (KData + 0x404); otherwise it stays zero. */
static void ce_find_kernel(uint32_t body,uint32_t bytes) {
    uint32_t found=0,at=0;
    for(uint32_t p=body;p+sizeof(ce_irq_code)/2u<=body+bytes;p+=2u)
        if(ce_matches(p,ce_irq_code,21u)) { found++; at=p; }
    if(found!=1) return;
    uint32_t value[KUI_RETAIL_CE_KERNEL_WORDS]={ce_literal(at+8u),ce_literal(at+22u),ce_literal(at+40u),0};
    for(unsigned i=0;i<3;i++)
        if((value[i]&3u) || value[i]<0x8c010000u || value[i]>=KUI_RETAIL_RAM_END-40u) return;
    const uint32_t entry=at-sizeof(ce_entry_code)/2u;
    if(entry>=body && ce_matches(entry,ce_entry_code,15u) &&
       ce_literal(entry+2u)==value[0]+0xc4u) value[3]=value[0]+0xc4u;
    memcpy(ce_kernel,value,sizeof(ce_kernel));
}
static uint32_t ce_load(const uint8_t *ip) {
    if(!ip_windows_ce(ip)) stopped("IP DOES NOT SELECT WINDOWS CE",word(ip+56));
    load_sectors(manifest.boot_lba,1,KUI_CE_LOAD_PREFIX_BYTES,ce_prefix);
    /* Everything still in use: firmware, IP, the bootstrap area below the
     * body, and this stage with its BSS and stack. */
    const struct kui_ce_live_range live[]={
        {0x8c000000u,KUI_RETAIL_EXEC_ADDRESS-0x8c000000u},
        {KUI_RETAIL_CE_STAGE_ADDRESS,KUI_RETAIL_STAGE_STACK-KUI_RETAIL_CE_STAGE_ADDRESS},
    };
    struct kui_ce_load_plan plan;
    enum kui_ce_load_result planned=kui_ce_load_plan_build(true,ce_prefix,sizeof(ce_prefix),
        manifest.boot_bytes,live,sizeof(live)/sizeof(live[0]),&plan);
    if(planned!=KUI_CE_LOAD_OK) {
        uint32_t fields[5]={word(ce_prefix+0x10),word(ce_prefix+0x14),word(ce_prefix+0x18),
            word(ce_prefix+0x1c),word(ce_prefix+0x20)};
        retail_display_values("COUNT    ADDRESS  OFFSET   BYTES    ENTRY",fields,5);
        stopped("CE LOAD PLAN REJECTED",(uint32_t)planned);
    }
    uint8_t *body=(uint8_t *)(uintptr_t)plan.body.address;
    retail_display_line("LOADING WINDOWS CE BODY");
    load_sectors(manifest.boot_lba+1u,plan.body.sector_count,plan.body.bytes,body);
    uint8_t *prefix=(uint8_t *)(uintptr_t)plan.prefix.address;
    memcpy(prefix,ce_prefix,sizeof(ce_prefix));
    uint32_t prefix_crc=kui_retail_crc32(0,prefix,plan.prefix.bytes);
    uint32_t body_crc=kui_retail_crc32(0,body,plan.body.bytes);
    if(prefix_crc!=kui_retail_crc32(0,ce_prefix,sizeof(ce_prefix)))
        stopped("CE PREFIX CHANGED IN PLACE",prefix_crc);
    /* A CE ROM image normally carries "ECEC" and its ROM header's address at
     * offset 0x40. Shown for the record; not every title's body has one. */
    uint32_t rom[5]={0};
    uint32_t header=word(body+0x44);
    bool rom_header=plan.body.bytes>=0x48u && word(body+0x40)==0x43454345u && !(header&3u) &&
        header>=plan.body.address && header-plan.body.address<=plan.body.bytes-32u;
    if(rom_header) {
        const uint8_t *h=(const uint8_t *)(uintptr_t)header;
        rom[0]=header;rom[1]=word(h+8);rom[2]=word(h+12);rom[3]=word(h+20);rom[4]=word(h+28);
    }
    uint32_t placed[4]={plan.body.address,plan.body.bytes,plan.body.sector_count,plan.entry_address};
    retail_display_restore(&display);
    retail_display_line(manifest.title);
    retail_display_hex("PREFIX CRC32",prefix_crc);
    retail_display_hex("BODY CRC32",body_crc);
    retail_display_hex("PREFIX AT",plan.prefix.address);
    retail_display_values("BODY AT  BYTES    SECTORS  ENTRY",placed,4);
    if(rom_header)
        retail_display_values("ROMHDR   PHYSFRST PHYSLAST RAMSTART RAMEND",rom,5);
    else retail_display_line("NO CE ROM HEADER AT BODY OFFSET 40");
    /* Where the reader raises CE's disc interrupts for stream reads. */
    ce_find_kernel(plan.body.address,plan.body.bytes);
    if(ce_kernel[0])
        retail_display_values("CE PEND  CE RING  RESCHED  ISR TABLE",ce_kernel,4);
    else retail_display_line("CE KERNEL INTERRUPTS NOT FOUND");
    /* The background reader is reached through CE's handler table. */
    if(manifest.reader!=KUI_RETAIL_READER_STANDARD && !ce_kernel[3]) {
        retail_display_line("NO CE HANDLER TABLE - STANDARD READER");
        manifest.reader=KUI_RETAIL_READER_STANDARD;
        select_resident();
    }
    /* Bootstrap 2 jumps to the body start, where the relay's trampoline
     * goes; a CE entry elsewhere has no known handoff yet. Only the SCI
     * resident is built CE-safe: it touches CE's stack (a virtual address)
     * only with exceptions enabled. */
    if(plan.entry_address!=plan.body.address)
        stopped("CE ENTRY IS NOT THE BODY START",plan.entry_address);
    if(manifest.storage_transport!=KUI_STORAGE_SCI)
        stopped("THE CE BOOT TEST NEEDS SCI MICROSD",manifest.storage_transport);
    return plan.body.bytes;
}
#endif
void kui_retail_boot_returned(void) {
    retail_display_restore(&display);
    stopped("OWNER BOOTSTRAP RETURNED",KUI_RETAIL_BOOT2_ADDRESS);
}

#if KUI_RETAIL_LOW_RESIDENT && !defined(KUI_RETAIL_CE)
/* DreamShell's pinned native layout places its loader at RAM+4000 and loads
 * firmware/syscalls only below it. K-UI retains font/flash/sysinfo and the
 * original system entry, so reject an unsupported target before the first
 * copy instead of overwriting a routine those services would still call. */
static int low_firmware_code(uint32_t address,int gd) {
    uint32_t area=address&0xff000000u,physical=address&0x1fffffffu;
    if(address&1u) return 0;
    if(area==0x0c000000u || area==0x8c000000u || area==0xac000000u)
        return physical>=0x0c000100u && physical<0x0c004000u;
    if(gd) return 0;
    return (area==0u || area==0x80000000u || area==0xa0000000u) &&
        physical && physical<0x00200000u;
}
static void low_firmware_preflight(uint32_t firmware) {
    const uint32_t vector[4]={0x8c0000b0u,0x8c0000b4u,0x8c0000b8u,0x8c0000e0u};
    if(!low_firmware_code(firmware,1))
        stopped("LOW RESIDENT GD VECTOR UNSUPPORTED",firmware);
    for(unsigned i=0;i<4u;i++) {
        uint32_t entry=*(volatile const uint32_t *)(uintptr_t)vector[i];
        if(!low_firmware_code(entry,0)) {
            retail_display_hex("FIRMWARE VECTOR",vector[i]);
            stopped("LOW RESIDENT FIRMWARE VECTOR UNSUPPORTED",entry);
        }
    }
    retail_display_line("NATIVE LOW RESIDENT");
}
#endif

static void install_resident(void) {
    size_t bytes=resident_bytes;
    if(!bytes || bytes>resident_limit-KUI_RETAIL_RESIDENT_ADDRESS)
        stopped("RESIDENT BOUNDS FAILED",(uint32_t)bytes);
    uint32_t firmware=*(volatile uint32_t *)(uintptr_t)0x8c0000bcu;
    uintptr_t canonical=(firmware&0x1fffffffu)|0x80000000u;
    if((firmware&1u) || canonical<0x8c000100u || canonical>=KUI_RETAIL_IP_ADDRESS)
        stopped("UNSUPPORTED FIRMWARE GD VECTOR",firmware);
#if KUI_RETAIL_LOW_RESIDENT && !defined(KUI_RETAIL_CE)
    low_firmware_preflight(firmware);
#endif
    memcpy((void *)(uintptr_t)KUI_RETAIL_RESIDENT_ADDRESS,
           resident_blob,bytes);
#ifdef KUI_RETAIL_CE
    memcpy((void *)(uintptr_t)KUI_RETAIL_CE_KERNEL,ce_kernel,sizeof(ce_kernel));
#endif
    kui_retail_stage_sync();
    kui_retail_resident_entry init=(kui_retail_resident_entry)(uintptr_t)KUI_RETAIL_RESIDENT_ADDRESS;
    int initialized=init(&manifest,&card,firmware,&display);
    if(initialized) stopped("RETAIL RESIDENT INIT FAILED",(uint32_t)initialized);
}
void kui_retail_stage_main(const uint8_t *wire) {
    retail_display_capture(&display);
    retail_display_restore(&display);
    retail_display_line("LAUNCHER HAS SHUT DOWN");
    memcpy(wire_copy,wire,sizeof(wire_copy));
    enum kui_game_result result=kui_retail_manifest_decode(wire_copy,&manifest);
    if(result!=KUI_GAME_OK) stopped("INVALID RETAIL MAP",(uint32_t)result);
    select_resident();
    if(manifest.boot_bytes<KUI_RETAIL_TRAMPOLINE_BYTES ||
       manifest.boot_bytes>KUI_RETAIL_EXEC_MAX_BYTES ||
       (!(manifest.flags & KUI_RETAIL_IMAGE_CD) && manifest.session_lba<45000))
        stopped("UNSUPPORTED BOOT LAYOUT",manifest.boot_bytes);
    if(manifest.storage_transport==KUI_STORAGE_SCIF) retire_launcher_serial();
    retail_display_line(kui_retail_storage_name(manifest.storage_transport));
#ifdef KUI_RETAIL_CE
    if(manifest.reader!=KUI_RETAIL_READER_STANDARD)
        retail_display_line("BACKGROUND READER - CE INTERRUPT DELIVERY");
#else
    if(manifest.reader!=KUI_RETAIL_READER_STANDARD)
        retail_display_line(manifest.reader==KUI_RETAIL_READER_ASYNC_EAGER?
            "BACKGROUND READER Y - 25 PER CALL":"BACKGROUND READER X - 20 PER CALL");
#endif
    last_card_result=kui_retail_storage_init(&card,manifest.storage_transport);
    if(last_card_result!=KUI_LOADER_SD_OK) {
        if(card.transport!=KUI_STORAGE_IDE) {
            retail_display_hex("SD COMMAND",card.device.sd.last_command);
            retail_display_hex("SD RESPONSE",card.device.sd.last_response);
        }
        stopped("READ ONLY STORAGE INIT FAILED",(uint32_t)last_card_result);
    }
    /* Preparation knows the validated partition end, a lower bound on the
     * physical card size. Trailing unpartitioned sectors are legitimate. */
    if(kui_retail_storage_blocks(&card)<manifest.card_sectors)
        stopped("STORAGE TOO SMALL FOR IMAGE MAP",(uint32_t)kui_retail_storage_blocks(&card));
#ifdef KUI_RETAIL_SD_BENCH
    if(manifest.storage_transport!=KUI_STORAGE_SCIF)
        stopped("THIS COMPARISON BENCHMARK REQUIRES SCIF",manifest.storage_transport);
    kui_retail_sd_benchmark(&card.device.sd,&manifest,&display);
#endif
    result=kui_retail_image_init(&image,&manifest,physical_read,&card);
    if(result!=KUI_GAME_OK) stopped("IMAGE READER INIT FAILED",(uint32_t)result);
    image.read_run=physical_run;
    retail_display_line("LOADING OWNER IP AND EXECUTABLE");
    uint8_t *ip=(uint8_t *)(uintptr_t)KUI_RETAIL_IP_ADDRESS;
    read_sectors(manifest.session_lba,16,KUI_GAME_SECTOR_MODE1,ip);
    uint32_t crc=kui_retail_crc32(0,ip,KUI_RETAIL_IP_BYTES);
    if(crc!=manifest.ip_crc32) stopped("IP CHECKSUM CHANGED",crc);
    /* Raw maps retain the fast header/address validation. Cooked data has
     * discarded those headers, so preparation fingerprints the executable
     * and the stage checks its exact file bytes after this mapped read. */
    uint32_t boot_end=manifest.boot_lba+(manifest.boot_bytes+2047u)/2048u;
    for(uint32_t i=0;i<manifest.track_count;i++) {
        const struct kui_retail_track *t=&manifest.slots[i].track;
        if(t->end_lba>manifest.boot_lba && t->start_lba<boot_end &&
           kui_retail_track_sector_bytes(t)==KUI_GAME_DATA_BYTES) boot_cooked=1;
    }
    uint8_t *boot=(uint8_t *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS;
#ifdef KUI_RETAIL_CE
    exec_bytes=ce_load(ip);
#else
    exec_bytes=manifest.boot_bytes;
    load_sectors(manifest.boot_lba,(manifest.boot_bytes+2047u)/2048u,manifest.boot_bytes,boot);
#endif
    if((boot_cooked || (manifest.flags & KUI_RETAIL_IMAGE_BOOT_CRC)) && source_boot_crc!=manifest.boot_crc32)
        stopped("EXECUTABLE CHECKSUM CHANGED",source_boot_crc);
#ifndef KUI_RETAIL_CE
    if(manifest.flags & KUI_RETAIL_IMAGE_SCRAMBLED) {
        retail_display_line("DESCRAMBLING EXPLICITLY MARKED CD EXECUTABLE");
        kui_retail_boot_descramble(boot,exec_bytes,scramble_index,scramble_seen);
    }
#else
    if(manifest.flags & KUI_RETAIL_IMAGE_SCRAMBLED)
        stopped("SCRAMBLED WINDOWS CE BOOT IS UNSUPPORTED",0);
#endif
    boot_crc=kui_retail_crc32(0,boot,exec_bytes);
    if((size_t)(__retail_trampoline_end-__retail_trampoline_start)!=sizeof(original_entry))
        stopped("INVALID ENTRY TRAMPOLINE",0);
    memcpy(original_entry,boot,ENTRY_PATCH_BYTES);
    memcpy(boot,__retail_trampoline_start,ENTRY_PATCH_BYTES);
    retail_display_line((boot_cooked || (manifest.flags & KUI_RETAIL_IMAGE_BOOT_CRC))?"IP AND EXECUTABLE CHECKSUMS PASSED":
        "IP CHECKSUM AND BOOT SECTOR HEADERS PASSED");
    retail_display_hex("BOOT BYTES",exec_bytes);
    retail_display_hex("STORAGE BLOCKS READ",image.blocks_read);
    /* DreamShell's native Katana path clears this IP bootstrap flag before
     * entering bootstrap2, including its truncated-IP mode. Only this RAM
     * copy changes; the original IP checksum was checked above. Every
     * working launch has had it cleared; Windows CE gets the same (its
     * first boot test, with the flag left set, reset during bootstrap 2). */
    ip[0xfcu]&=(uint8_t)~0x20u;
    install_resident();
    retail_display_line("READER INSTALLED BEFORE BOOTSTRAP 2");
    retail_display_line("ENTERING OWNER BOOTSTRAP 2");
#if KUI_RETAIL_SONIC_STACK_TEST && !defined(KUI_RETAIL_CE)
    if(exec_bytes==6751168u && manifest.ip_crc32==0x22de24d8u && boot_crc==0x73f4277bu)
        retail_display_line("SONIC STACK TEST ACTIVE");
#endif
    retail_display_pause(STEP_PAUSE_FRAMES);
    kui_retail_bootstrap_enter();
}

/* Called from P2 assembly with IRQs masked and caches clean/disabled. The
 * supported initial stack/VBR follow the conventional native GD bootstrap
 * layout; reject an incompatible entry before restoring executable entry.
 * General registers, SR, VBR, GBR, PR, MAC and cache configuration are restored
 * by assembly; fixed FPU registers, integer division and the linked machine-
 * code audit keep every floating-point register unchanged. */
static void relay_stopped(const char *message,uint32_t detail) __attribute__((noreturn));
static void relay_stopped(const char *message,uint32_t detail) {
#ifndef KUI_RETAIL_CE
    /* Only a terminal failure may reclaim video after the owner's bootstrap.
     * Successful native handoff must preserve its scanout and VRAM contents. */
    retail_display_restore(&display);
#endif
    stopped(message,detail);
}
#if KUI_RETAIL_SONIC_STACK_TEST && !defined(KUI_RETAIL_CE)
static uint32_t sonic_stack_p1(uint32_t address) {
    return (address&0x1fffffffu)|0x80000000u;
}
static uint8_t *sonic_stack_p2(uint32_t address) {
    return (uint8_t *)(uintptr_t)((address&0x1fffffffu)|0xa0000000u);
}
static struct sonic_stack_state *sonic_stack_uncached_scope(unsigned scope) {
#ifdef KUI_RETAIL_SONIC_STACK_TEST_HOST
    return scope?&kui_retail_sonic_texture_state:&kui_retail_sonic_stack_state;
#else
    return (struct sonic_stack_state *)sonic_stack_p2((uint32_t)(uintptr_t)
        (scope?&kui_retail_sonic_texture_state:&kui_retail_sonic_stack_state));
#endif
}
static void sonic_stack_report(struct sonic_stack_state *s,const char *message,
    unsigned point,const uint32_t *frame) __attribute__((noreturn));
static void sonic_stack_report(struct sonic_stack_state *s,const char *message,
    unsigned point,const uint32_t *frame) {
    /* Point 7 is reserved for return failures, distinct from texture entry
     * point 4. Keep the established displayed return point/detail 4 while
     * reporting the actual observed CCR, including an observed zero. */
    unsigned shown=point==SONIC_STACK_RETURN_POINT?4u:point;
    uint32_t cpu[5]={shown,(uint32_t)(uintptr_t)frame,frame?frame[4]:0u,
        frame?frame[5]:0u,point<7u?s->ccr[point]:s->return_ccr};
    uint32_t bounds[5]={s->owner_bottom,s->owner_top,s->original_sp,
        s->active,s->completed};
    uint32_t g2_now=kui_retail_sonic_stack_read(0xa05f688cu);
    uint32_t rows[3][5];
    if(s->mismatch_address) {
        rows[0][0]=s->transport;rows[0][1]=s->reader;
        rows[0][2]=s->changed_bytes;rows[0][3]=s->last_changed;rows[0][4]=s->restored;
        rows[1][0]=s->code_end;rows[1][1]=s->code_crc;rows[1][2]=s->return_code_crc;
        rows[1][3]=s->resident_crc;rows[1][4]=s->return_crc;
        rows[2][0]=s->word_address;rows[2][1]=s->word_old;rows[2][2]=s->word_p1;
        rows[2][3]=s->word_p2;rows[2][4]=g2_now;
    } else {
        rows[0][0]=s->last[0];rows[0][1]=s->reads[0];rows[0][2]=s->last[1];
        rows[0][3]=s->reads[1];rows[0][4]=g2_now;
        rows[1][0]=SONIC_STACK_SNAPSHOT_BEGIN;rows[1][1]=s->resident_end;
        rows[1][2]=s->resident_crc;rows[1][3]=s->return_crc;rows[1][4]=s->restored;
        rows[2][0]=s->asset_status;
        rows[2][1]=s->scope?s->compressed_source:s->asset_handle;
        rows[2][2]=s->scope?s->compressed_bytes:s->asset_bytes;
        rows[2][3]=s->scope?s->decoded_bytes:0x8cd00000u;
        rows[2][4]=s->scope?s->stage_end:0x8ce00000u;
    }
    retail_display_restore(&display);
    retail_display_line(s->scope?"SONIC TEXTURE STACK":"SONIC STACK TEST");
    retail_display_values("POINT FRAME SR PR CCR",cpu,5);
    retail_display_values("STACK LOW HIGH OLDSP ACTIVE DONE",bounds,5);
    retail_display_values(s->mismatch_address?"TYPE READER CHANGES LAST RESTORED":
        "FIFO READS BUSY READS G2 NOW",rows[0],5);
    retail_display_values(s->mismatch_address?"CODE END CODE OLD CODE NEW FULL OLD FULL NEW":
        "READER LOW HIGH BEFORE AFTER RESTORED",rows[1],5);
    retail_display_values(s->mismatch_address?"WORD AT OLD P1 P2 G2 NOW":
        (s->scope?"BOOL SOURCE BYTES DECODED STAGEEND":"FILE STATUS HANDLE BYTES LOW LIMIT"),rows[2],5);
    stopped(message,s->mismatch_address?s->mismatch_address:shown);
}
static int sonic_stack_range(uint32_t begin,uint32_t bytes,uint32_t end) {
    return !(begin&31u) && begin>=KUI_RETAIL_STAGE_ADDRESS &&
        end<=0x8cf00000u && begin<=end && bytes<=end-begin;
}
static int sonic_stack_disjoint(uint32_t a,uint32_t bytes,uint32_t b,uint32_t n) {
    return a+bytes<=b || b+n<=a;
}
static uint32_t sonic_stack_map_bytes(const struct sonic_stack_state *s) {
    if(s->transport!=manifest.storage_transport || s->reader!=manifest.reader) return 0u;
    uint32_t slots=KUI_RETAIL_IMAGE_SLOTS;
    if(s->reader!=KUI_RETAIL_READER_STANDARD) {
        if(s->reader!=KUI_RETAIL_READER_ASYNC && s->reader!=KUI_RETAIL_READER_ASYNC_EAGER)
            return 0u;
        if(s->transport!=KUI_STORAGE_SCI) return 0u;
        slots=KUI_RETAIL_ASYNC_SLOTS;
    }
    if(!manifest.track_count || manifest.track_count>KUI_RETAIL_IMAGE_TRACKS ||
       manifest.track_count>slots || !manifest.extent_count ||
       manifest.extent_count>slots-manifest.track_count) return 0u;
    return slots==KUI_RETAIL_IMAGE_SLOTS?sizeof(manifest):
        offsetof(struct kui_retail_manifest,slots)+slots*sizeof(union kui_retail_slot);
}
static int sonic_stack_map_bounds(const struct sonic_stack_state *s) {
    return !(s->bss_begin&3u) && !(s->bss_end&3u) &&
        s->bss_begin>=s->code_end && s->bss_begin<s->bss_end &&
        s->bss_end<=s->resident_end && s->manifest_bytes && !(s->manifest_bytes&3u) &&
        s->manifest_bytes<=s->bss_end-s->bss_begin;
}
static uint32_t sonic_stack_blob_word(unsigned offset) {
    uint32_t value;
    memcpy(&value,resident_blob+offset,4u);
    return value;
}
static void sonic_stack_find_map(struct sonic_stack_state *s,const uint32_t *frame) {
    /* Owned resident entry literals identify its exact linked BSS. No map
     * symbol or low-reader ABI change is needed. Discovery runs through P2
     * after the entry wrapper published RAM and disabled both caches. */
    s->bss_begin=kui_retail_sonic_stack_read(KUI_RETAIL_RESIDENT_ADDRESS+0x20000018u);
    s->bss_end=kui_retail_sonic_stack_read(KUI_RETAIL_RESIDENT_ADDRESS+0x2000001cu);
    s->manifest_bytes=sonic_stack_map_bytes(s);
    if(!s->manifest_bytes) sonic_stack_report(s,"RESIDENT MAP SHAPE INVALID",2u,frame);
    if(!sonic_stack_map_bounds(s) || s->bss_begin!=sonic_stack_blob_word(0x18u) ||
       s->bss_end!=sonic_stack_blob_word(0x1cu))
        sonic_stack_report(s,"RESIDENT BSS BOUNDS INVALID",2u,frame);
    for(uint32_t at=s->bss_begin;at<=s->bss_end-s->manifest_bytes;at+=4u) {
        if(memcmp(sonic_stack_p2(at),&manifest,s->manifest_bytes)) continue;
        s->manifest_address=at;++s->manifest_matches;
    }
    if(s->manifest_matches!=1u)
        sonic_stack_report(s,"RESIDENT MAP NOT UNIQUE",2u,frame);
}
static uint32_t sonic_stack_immutable_change(const struct sonic_stack_state *s,
    uint32_t begin,uint32_t end) {
    /* Check both aliases without purging. An old cached map must not hide a
     * physical RAM change, and a damaged cached map must not reach the reader. */
    for(uint32_t at=begin;at<end;at+=4u) {
        uint32_t old;
        memcpy(&old,s->resident+(at-SONIC_STACK_SNAPSHOT_BEGIN),4u);
        uint32_t p1=*(volatile const uint32_t *)(uintptr_t)at;
        uint32_t p2=kui_retail_sonic_stack_read(at|0x20000000u);
        uint32_t changed=(p1^old)|(p2^old);
        if(!changed) continue;
        for(unsigned byte=0;byte<4u;byte++)
            if(changed&(0xffu<<(byte*8u))) return at+byte;
    }
    return 0u;
}
static unsigned sonic_stack_mask(const struct sonic_stack_state *s) {
    return s->scope?0x70u:0x0fu;
}
static int sonic_stack_state_bounds(const struct sonic_stack_state *s) {
    unsigned owner=s->scope?4u:0u;
    return s->scope<2u && s->owner_bottom==kui_retail_sonic_stack_allocation(owner) &&
        s->book_bottom==kui_retail_sonic_stack_allocation(1) &&
        s->stage_end==kui_retail_sonic_stack_allocation(3) &&
        sonic_stack_range(s->owner_bottom,SONIC_STACK_OWNER_BYTES,s->stage_end) &&
        s->owner_top==s->owner_bottom+SONIC_STACK_OWNER_BYTES &&
        sonic_stack_range(s->book_bottom,SONIC_STACK_BOOK_BYTES,s->stage_end) &&
        s->book_top==s->book_bottom+SONIC_STACK_BOOK_BYTES;
}
static void sonic_stack_restore(struct sonic_stack_state *s,unsigned mask) {
    for(unsigned i=0;i<7u;i++) if((mask&(1u<<i)) && !(s->restored&(1u<<i))) {
        memcpy(sonic_stack_p2(sonic_stack_address[i]),s->saved[i],SONIC_STACK_PATCH_BYTES);
        kui_retail_sonic_stack_publish(sonic_stack_address[i],SONIC_STACK_PATCH_BYTES);
        s->restored|=1u<<i;
    }
}
static void sonic_stack_arm(void) {
    struct sonic_stack_state *s=sonic_stack_uncached_scope(0),*t=sonic_stack_uncached_scope(1);
    if(exec_bytes!=6751168u || manifest.ip_crc32!=0x22de24d8u ||
       boot_crc!=0x73f4277bu) return;
    uint32_t address[5]={kui_retail_sonic_stack_allocation(0),
        kui_retail_sonic_stack_allocation(1),kui_retail_sonic_stack_allocation(2),
        kui_retail_sonic_stack_allocation(4),kui_retail_sonic_stack_allocation(5)};
    const uint32_t bytes[5]={SONIC_STACK_OWNER_BYTES,SONIC_STACK_BOOK_BYTES,
        sizeof(*s),SONIC_STACK_OWNER_BYTES,sizeof(*t)};
    uint32_t end=kui_retail_sonic_stack_allocation(3);
    for(unsigned i=0;i<5u;i++) {
        if(!sonic_stack_range(address[i],bytes[i],end))
            sonic_stack_report(s,"PRIVATE STACK BOUNDS INVALID",3u,NULL);
        for(unsigned j=0;j<i;j++) if(!sonic_stack_disjoint(address[i],bytes[i],address[j],bytes[j]))
            sonic_stack_report(s,"PRIVATE STACK BOUNDS INVALID",3u,NULL);
    }
    if((resident_limit&3u) || resident_limit<KUI_RETAIL_RESIDENT_ADDRESS ||
       resident_limit-SONIC_STACK_SNAPSHOT_BEGIN>sizeof(s->resident) ||
       resident_bytes<0x20u || (resident_bytes&3u) ||
       resident_bytes>resident_limit-KUI_RETAIL_RESIDENT_ADDRESS)
        sonic_stack_report(s,"READER SNAPSHOT BOUNDS INVALID",3u,NULL);
    for(unsigned i=0;i<7u;i++) {
        uint32_t at=sonic_stack_address[i];
        if((at&3u) || at<KUI_RETAIL_EXEC_ADDRESS ||
           at-KUI_RETAIL_EXEC_ADDRESS>exec_bytes-SONIC_STACK_PATCH_BYTES)
            sonic_stack_report(s,"OWNER WINDOW BOUNDS INVALID",i,NULL);
    }
    for(unsigned scope=0;scope<2u;scope++) {
        struct sonic_stack_state *q=scope?t:s;
        q->scope=scope;q->owner_bottom=address[scope?3u:0u];
        q->owner_top=q->owner_bottom+SONIC_STACK_OWNER_BYTES;
        q->book_bottom=address[1];q->book_top=address[1]+SONIC_STACK_BOOK_BYTES;q->stage_end=end;
        q->resident_end=resident_limit;q->transport=manifest.storage_transport;q->reader=manifest.reader;
        q->code_end=KUI_RETAIL_RESIDENT_ADDRESS+(uint32_t)resident_bytes;
    }
    /* Every original window is read from the exact-CRC owner's RAM. The two
     * scopes retain distinct backing: the first flash helper may retain its
     * private workspace until a later operation resets the internal pointer. */
    for(unsigned i=0;i<7u;i++) {
        struct sonic_stack_state *q=i<4u?s:t;
        uint8_t *p=sonic_stack_p2(sonic_stack_address[i]);
        memcpy(q->saved[i],p,SONIC_STACK_PATCH_BYTES);
        volatile uint16_t *code=(volatile uint16_t *)p;
        code[0]=0x2f06u;code[1]=0xd001u;code[2]=0x402bu;code[3]=0x0009u;
        uint32_t target=(uint32_t)(uintptr_t)sonic_stack_target[i]|0x20000000u;
        memcpy((void *)(code+4),&target,4u);
        kui_retail_sonic_stack_publish(sonic_stack_address[i],SONIC_STACK_PATCH_BYTES);
    }
    s->armed=t->armed=1u;
}
/* Audit the owner's bitstream before its decoder writes a byte. This counts
 * output only; it never copies proprietary bytes or materializes decoded data. */
struct sonic_lz_scan { const uint8_t *source;uint32_t bytes,at,bits,flags; };
static int sonic_lz_byte(struct sonic_lz_scan *s,uint32_t *value) {
    if(s->at>=s->bytes) return 0;
    *value=s->source[s->at++];return 1;
}
static int sonic_lz_bit(struct sonic_lz_scan *s,uint32_t *value) {
    if(!s->bits) {
        if(!sonic_lz_byte(s,&s->flags)) return 0;
        s->bits=8u;
    }
    *value=s->flags&1u;s->flags>>=1;--s->bits;return 1;
}
static int sonic_lz_bounds(const uint8_t *source,uint32_t bytes,
    uint32_t limit,uint32_t *decoded) {
    struct sonic_lz_scan s={source,bytes,0u,0u,0u};uint32_t out=0u;
    for(;;) {
        uint32_t bit,a,b,length,distance;
        if(!sonic_lz_bit(&s,&bit)) return 0;
        if(bit) {
            if(out==limit || !sonic_lz_byte(&s,&a)) return 0;
            ++out;continue;
        }
        if(!sonic_lz_bit(&s,&bit)) return 0;
        if(!bit) {
            if(!sonic_lz_bit(&s,&a) || !sonic_lz_bit(&s,&b) ||
               !sonic_lz_byte(&s,&distance)) return 0;
            length=2u+(a<<1)+b;distance=256u-distance;
        } else {
            if(!sonic_lz_byte(&s,&a) || !sonic_lz_byte(&s,&b)) return 0;
            uint32_t word=a|(b<<8);
            if(!word) {*decoded=out;return 1;}
            distance=8192u-(word>>3);length=word&7u;
            if(length) length+=2u;
            else {if(!sonic_lz_byte(&s,&length)) return 0;++length;}
        }
        if(distance>out || length>limit-out) return 0;
        out+=length;
    }
}
static uint32_t sonic_stack_prepare(struct sonic_stack_state *s,uint32_t *frame,
    uint32_t raw,uint32_t ccr) {
    sonic_stack_find_map(s,frame);
    s->original_frame=raw;s->original_sp=raw+SONIC_STACK_FRAME_BYTES;
    s->original_pr=frame[5];s->entry_ccr=ccr;
    uint32_t *guard=(uint32_t *)sonic_stack_p2(s->owner_bottom);
    for(unsigned i=0;i<8u;i++) guard[i]=SONIC_STACK_GUARD_WORD;
    s->resident_crc=kui_retail_crc32(0,sonic_stack_p2(SONIC_STACK_SNAPSHOT_BEGIN),
        s->resident_end-SONIC_STACK_SNAPSHOT_BEGIN);
    memcpy(s->resident,sonic_stack_p2(SONIC_STACK_SNAPSHOT_BEGIN),
        s->resident_end-SONIC_STACK_SNAPSHOT_BEGIN);
    s->code_crc=kui_retail_crc32(0,s->resident,s->code_end-SONIC_STACK_SNAPSHOT_BEGIN);
    uint32_t prepared=s->owner_top-SONIC_STACK_FRAME_BYTES;
    uint32_t *copy=(uint32_t *)sonic_stack_p2(prepared);
    memcpy(copy,frame,SONIC_STACK_FRAME_BYTES);
    copy[5]=(uint32_t)(uintptr_t)(s->scope?kui_retail_sonic_texture_return:
        kui_retail_sonic_scope_return)|0x20000000u;
    s->active=1u;return prepared;
}
uint32_t kui_retail_sonic_stack_checkpoint(uint32_t *frame,unsigned point,
    uint32_t ccr,struct sonic_stack_state *s) {
    uint32_t raw=(uint32_t)(uintptr_t)frame,at=sonic_stack_p1(raw);
    struct sonic_stack_state *other=sonic_stack_uncached_scope(s->scope?0u:1u);
    int interior=point==5u || point==6u;
    if(point>=7u || s->scope!=(point>=4u) || !s->armed || (raw&3u) ||
       !sonic_stack_state_bounds(s) || other->active || s->completed ||
       (s->restored&(1u<<point)) ||
       (interior?(!s->active || at<s->owner_bottom+32u ||
          at>s->owner_top-SONIC_STACK_FRAME_BYTES-8u):
         (s->active || at<KUI_RETAIL_HOOK_STACK ||
          at>KUI_RETAIL_EXEC_ADDRESS-SONIC_STACK_FRAME_BYTES)))
        sonic_stack_report(s,"STACK CHECKPOINT STATE INVALID",point,NULL);
    s->ccr[point]=ccr;
    sonic_stack_restore(s,1u<<point);
    s->resume=sonic_stack_address[point];
#ifdef KUI_RETAIL_SONIC_STACK_TEST_HOST
    kui_retail_sonic_stack_resume=s->resume;
#else
    *(volatile uint32_t *)sonic_stack_p2((uint32_t)(uintptr_t)&kui_retail_sonic_stack_resume)=s->resume;
#endif
    if(point==5u) {
        /* The size query writes at the descendant's native SP. The saved
         * 21-word frame leaves that word immediately above it. */
        uint32_t bytes=frame[21];
        s->asset_status=frame[20];s->compressed_bytes=bytes;
        if(frame[20]!=1u || !bytes || bytes>0x7ffff800u)
            sonic_stack_report(s,"COMPRESSED SIZE INVALID",point,frame);
        uint32_t rounded=(bytes+0x7ffu)&~0x7ffu;
        if(s->stage_end>0x8cf00000u || rounded>0x8cf00000u-s->stage_end)
            sonic_stack_report(s,"COMPRESSED READ OVERLAPS TEST RAM",point,frame);
        s->compressed_source=0x8cf00000u-rounded;
        return raw;
    }
    if(point==6u) {
        uint32_t source=frame[8]; /* Real R12 before the decoder's R4 setup. */
        uint32_t rounded=(s->compressed_bytes+0x7ffu)&~0x7ffu;
        if(!(s->restored&0x20u) || !s->compressed_bytes ||
           source!=s->compressed_source-0x80000000u ||
           frame[9]!=(rounded>>11) || s->compressed_source<s->stage_end ||
           s->compressed_source>0x8cf00000u ||
           s->compressed_bytes>0x8cf00000u-s->compressed_source)
            sonic_stack_report(s,"COMPRESSED SOURCE INVALID",point,frame);
        if(!sonic_lz_bounds(sonic_stack_p2(s->compressed_source),
            s->compressed_bytes,0x100000u,&s->decoded_bytes))
            sonic_stack_report(s,"TEXTURE DECODE EXCEEDS TEST RAM BOUNDS",point,frame);
        return raw;
    }
    if(point==4u) {
        if(!other->completed || other->restored!=0x0fu)
            sonic_stack_report(s,"TEXTURE SCOPE BEFORE FLASH RETURN",point,frame);
        /* The guard hooks survive only this call. They run before either
         * temporary source buffer or decompressed output can reach the stage. */
        return sonic_stack_prepare(s,frame,raw,ccr);
    }
    if(point==3u) {
        s->asset_status=frame[20];s->asset_handle=frame[6];
        if(at>KUI_RETAIL_EXEC_ADDRESS-92u)
            sonic_stack_report(s,"ASSET SIZE FRAME INVALID",point,frame);
        s->asset_bytes=frame[22];
        if(s->asset_status!=1u || !s->asset_handle || !s->asset_bytes ||
           s->asset_bytes>0x100000u ||
           ((s->asset_bytes+0x7ffu)&~0x7ffu)>0x8ce00000u-0x8cd00000u)
            sonic_stack_report(s,"FIRST ASSET EXCEEDS TEST RAM BOUNDS",point,frame);
        return raw;
    }
    if(point<2u) {
        uint32_t mask=point==0u?32u:1u;
        for(uint32_t i=0;i<SONIC_STACK_WAIT_BUDGET;i++) {
            uint32_t value=kui_retail_sonic_stack_read(0xa05f688cu);
            if(!i) s->first[point]=value;
            s->last[point]=value;++s->reads[point];
            if(!(value&mask)) return raw;
        }
        sonic_stack_report(s,"LATE AUDIO G2 WAIT TIMED OUT",point,frame);
    }
    if(!(s->restored&8u) || s->asset_status!=1u || !s->asset_handle ||
       !s->asset_bytes || s->asset_bytes>0x100000u)
        sonic_stack_report(s,"SCOPED CALL BEFORE ASSET GUARD",point,frame);
    /* Preserve the independent texture checkpoints until their one startup
     * call; every first-scope helper is removed before the flash delegate. */
    sonic_stack_restore(s,sonic_stack_mask(s));
    return sonic_stack_prepare(s,frame,raw,ccr);
}
uint32_t kui_retail_sonic_stack_after(uint32_t *frame,uint32_t ccr,
    struct sonic_stack_state *s) {
    uint32_t raw=(uint32_t)(uintptr_t)frame;
    s->return_ccr=ccr;
    if(!s->armed || !s->active || s->completed ||
       !sonic_stack_state_bounds(s) || sonic_stack_uncached_scope(s->scope?0u:1u)->active ||
       raw!=s->owner_top-SONIC_STACK_FRAME_BYTES || ccr!=s->entry_ccr ||
       s->resident_end!=resident_limit || (s->resident_end&3u) || s->resident_end<KUI_RETAIL_RESIDENT_ADDRESS ||
       s->resident_end-SONIC_STACK_SNAPSHOT_BEGIN>sizeof(s->resident) ||
       s->code_end!=KUI_RETAIL_RESIDENT_ADDRESS+resident_bytes ||
       s->transport!=manifest.storage_transport || s->reader!=manifest.reader ||
       s->code_end<=KUI_RETAIL_RESIDENT_ADDRESS || s->code_end>s->resident_end ||
       (s->code_end&3u) || !sonic_stack_map_bounds(s) || s->manifest_matches!=1u ||
       s->bss_begin!=sonic_stack_blob_word(0x18u) || s->bss_end!=sonic_stack_blob_word(0x1cu) ||
       s->manifest_bytes!=sonic_stack_map_bytes(s) || (s->manifest_address&3u) ||
       s->manifest_address<s->bss_begin || s->manifest_address>s->bss_end ||
       s->manifest_bytes>s->bss_end-s->manifest_address ||
       sonic_stack_p1(s->original_frame)<KUI_RETAIL_HOOK_STACK ||
       sonic_stack_p1(s->original_frame)>KUI_RETAIL_EXEC_ADDRESS-SONIC_STACK_FRAME_BYTES ||
       s->original_sp!=s->original_frame+SONIC_STACK_FRAME_BYTES)
        sonic_stack_report(s,"SCOPED RETURN FRAME INVALID",SONIC_STACK_RETURN_POINT,NULL);
    /* The unique entry match anchors the pinned address in the saved map.
     * An in-range metadata shift must not silently skip its leading bytes. */
    if(memcmp(s->resident+(s->manifest_address-SONIC_STACK_SNAPSHOT_BEGIN),
        &manifest,s->manifest_bytes))
        sonic_stack_report(s,"SCOPED RETURN FRAME INVALID",SONIC_STACK_RETURN_POINT,NULL);
    const uint32_t *guard=(const uint32_t *)(uintptr_t)s->owner_bottom;
    for(unsigned i=0;i<8u;i++) if(guard[i]!=SONIC_STACK_GUARD_WORD)
        sonic_stack_report(s,"PRIVATE STACK GUARD CHANGED",SONIC_STACK_RETURN_POINT,frame);
    /* Keep full coherent-P1 change evidence, while protecting only the
     * immutable IP/code prefix and uniquely pinned validated resident map.
     * Service counters, sector caches and card state can change during calls.
     * Neither cache mode nor RAM cache ownership changes on return. */
    const uint8_t *resident=(const uint8_t *)(uintptr_t)SONIC_STACK_SNAPSHOT_BEGIN;
    size_t bytes=s->resident_end-SONIC_STACK_SNAPSHOT_BEGIN;
    s->return_crc=kui_retail_crc32(0,resident,bytes);
    s->return_code_crc=kui_retail_crc32(0,resident,s->code_end-SONIC_STACK_SNAPSHOT_BEGIN);
    for(size_t i=0;i<bytes;i++) if(resident[i]!=s->resident[i]) {
        uint32_t changed=SONIC_STACK_SNAPSHOT_BEGIN+(uint32_t)i;
        if(!s->changed_bytes) s->first_changed=changed;
        ++s->changed_bytes;s->last_changed=changed;
    }
    s->mismatch_address=sonic_stack_immutable_change(s,SONIC_STACK_SNAPSHOT_BEGIN,s->code_end);
    if(!s->mismatch_address) s->mismatch_address=sonic_stack_immutable_change(s,
        s->manifest_address,s->manifest_address+s->manifest_bytes);
    if(s->changed_bytes || s->mismatch_address) {
        s->word_address=(s->mismatch_address?s->mismatch_address:s->first_changed)&~3u;
        memcpy(&s->word_old,s->resident+(s->word_address-SONIC_STACK_SNAPSHOT_BEGIN),4u);
        s->word_p1=*(volatile const uint32_t *)(uintptr_t)s->word_address;
        s->word_p2=kui_retail_sonic_stack_read((uint32_t)(uintptr_t)sonic_stack_p2(s->word_address));
        if(s->mismatch_address)
            sonic_stack_report(s,"IMMUTABLE READER CHANGED",SONIC_STACK_RETURN_POINT,frame);
    }
    /* Preserve actual callee register results, including R0 and SR. Only PR
     * is substituted, and popping this real frame restores the original SP. */
    uint32_t *original=(uint32_t *)(uintptr_t)s->original_frame;
    memcpy(original,frame,SONIC_STACK_FRAME_BYTES);original[5]=s->original_pr;
    /* Error/empty paths can return without reaching either write guard.
     * Remove those unused hooks before returning to code that may reclaim
     * the high stage. No hook remains armed after this one-shot texture call. */
    sonic_stack_restore(s,sonic_stack_mask(s));
    s->active=0u;s->completed=1u;
    return s->original_frame;
}
#endif
#if defined(KUI_RETAIL_STARTUP_TRACE) && KUI_RETAIL_STARTUP_TRACE && !defined(KUI_RETAIL_CE)
static uint8_t *startup_trace_owner(uint32_t address) {
    return (uint8_t *)(uintptr_t)((address&0x1fffffffu)|0xa0000000u);
}
static void startup_trace_arm(void) {
    startup_trace.armed=0;
    startup_trace.passed=0;
    if(exec_bytes!=6751168u || manifest.ip_crc32!=0x22de24d8u ||
       boot_crc!=0x73f4277bu) return;
    uint32_t stack=kui_retail_startup_trace_stack_address();
    if((stack&31u) || stack<KUI_RETAIL_STAGE_ADDRESS ||
       stack>0x8cf00000u-STARTUP_TRACE_STACK_BYTES)
        relay_stopped("STARTUP TRACE STACK INVALID",stack);
    for(unsigned i=0;i<STARTUP_TRACE_POINTS;i++) {
        uint32_t address=startup_trace_address[i];
        if((address&3u) || address<KUI_RETAIL_EXEC_ADDRESS ||
           address-KUI_RETAIL_EXEC_ADDRESS>exec_bytes-STARTUP_TRACE_BYTES)
            relay_stopped("STARTUP TRACE ADDRESS INVALID",address);
    }
    for(unsigned i=0;i<STARTUP_TRACE_POINTS;i++) {
        uint32_t address=startup_trace_address[i];
        uint8_t *owner=startup_trace_owner(address);
        memcpy(startup_trace.saved[i],owner,STARTUP_TRACE_BYTES);
        volatile uint16_t *code=(volatile uint16_t *)owner;
        /* Save original R0 before loading the trace-specific P2 wrapper. */
        code[0]=0x2f06u;code[1]=0xd001u;code[2]=0x402bu;code[3]=0x0009u;
        *(volatile uint32_t *)(code+4)=
            (uint32_t)(uintptr_t)startup_trace_target[i]|0x20000000u;
        kui_retail_startup_trace_publish(address,STARTUP_TRACE_BYTES);
        startup_trace.first[i]=startup_trace.last[i]=startup_trace.reads[i]=0;
        startup_trace.ccr[i]=0;
    }
    startup_trace.armed=1;
}
static uint32_t startup_trace_sample(unsigned point,uint32_t address) {
    uint32_t value=kui_retail_startup_trace_read(address);
    if(!startup_trace.reads[point]) startup_trace.first[point]=value;
    startup_trace.last[point]=value;
    ++startup_trace.reads[point];
    return value;
}
static int startup_trace_wait(unsigned point,uint32_t address,
    uint32_t mask,int set) {
    for(uint32_t reads=0;reads<STARTUP_TRACE_BUDGET;reads++) {
        uint32_t value=startup_trace_sample(point,address);
        if(((value&mask)!=0)==set) return 1;
    }
    return 0;
}
static void startup_trace_report(const char *message,uint32_t point,
    const uint32_t *frame) __attribute__((noreturn));
static void startup_trace_report(const char *message,uint32_t point,
    const uint32_t *frame) {
    /* Gather evidence before a terminal diagnostic reclaims video. */
    uint32_t snapshot[3]={kui_retail_startup_trace_read(0xa05f810cu),
        kui_retail_startup_trace_read(0xa05f688cu),
        kui_retail_startup_trace_read(0xa05f6900u)};
    uint32_t cpu[5]={point,startup_trace.passed,frame?frame[4]:0,frame?frame[3]:0,
        point<STARTUP_TRACE_POINTS?startup_trace.ccr[point]:0};
    retail_display_restore(&display);
    retail_display_line("SONIC STARTUP TRACE - INTENTIONAL STOP");
    retail_display_values("POINT PASSED SR VBR CCR",cpu,5);
    for(unsigned i=0;i<3;i++) {
        uint32_t values[3]={startup_trace.first[i],startup_trace.last[i],
            startup_trace.reads[i]};
        retail_display_values(i==0?"SCAN FIRST LAST READS":
            i==1?"G2 FIRST LAST READS":"PVR FIRST LAST READS",values,3);
    }
    retail_display_values("SCAN G2 ISTNRM NOW",snapshot,3);
    stopped(message,point);
}
#if KUI_RETAIL_STARTUP_TRACE >= 2
static int startup_gd_frame_valid(const uint32_t *frame) {
    uintptr_t address=((uintptr_t)frame&0x1fffffffu)|0x80000000u;
    return !(address&3u) && address>=KUI_RETAIL_HOOK_STACK &&
        address<=KUI_RETAIL_EXEC_ADDRESS-21u*4u;
}
static int startup_gd_guest(uint32_t address,uint32_t bytes) {
    uint32_t area=address&0xff000000u;
    uint32_t p1=(address&0x1fffffffu)|0x80000000u;
    return (area==0x0c000000u || area==0x8c000000u || area==0xac000000u) &&
        bytes && p1>=KUI_RETAIL_HOOK_STACK && p1<KUI_RETAIL_STAGE_ADDRESS &&
        bytes<=KUI_RETAIL_STAGE_ADDRESS-p1;
}
static const void *startup_gd_pointer(uint32_t address) {
    /* GD read parameters use physical RAM destinations. Observe those via
     * cached P1, as the native reader does before publishing through P2;
     * owner-supplied P1/P2 pointers keep their original cache alias. */
    if((address&0xff000000u)==0x0c000000u) address|=0x80000000u;
    return (const void *)(uintptr_t)address;
}
static void startup_gd_report(struct startup_gd_state *s,const char *message)
    __attribute__((noreturn));
static void startup_gd_report(struct startup_gd_state *s,const char *message) {
    uint32_t rows[5][5]={
        {s->calls,s->function,s->command,s->result,s->caller},
#if KUI_RETAIL_STARTUP_TRACE >= 3
        {s->mount_result,s->read_requests,s->completed_reads,s->path_lba,s->path_bytes},
        {s->pvd_crc,s->pvd_valid,s->pvd_sector_bytes,s->root_lba,s->root_bytes},
#else
        {s->init_result,s->init_token,s->init_status,s->version_token,s->version_status},
        {s->drive_result,s->drive_status,s->drive_type,s->pvd_header,s->pvd_crc},
#endif
        {s->read_command,s->read_fad,s->read_count,s->read_destination,s->read_token},
        {s->read_status,s->check[0],s->check[1],s->check[2],s->check[3]}
    };
    retail_display_restore(&display);
#if KUI_RETAIL_STARTUP_TRACE >= 3
    retail_display_line("SONIC MOUNT TRACE - INTENTIONAL STOP");
#else
    retail_display_line("SONIC GD TRACE - INTENTIONAL STOP");
#endif
    retail_display_values("CALLS FN CMD RESULT PR",rows[0],5);
#if KUI_RETAIL_STARTUP_TRACE >= 3
    retail_display_values("MOUNT READS DONE PATHLBA BYTES",rows[1],5);
    retail_display_values("PVDCRC ID SIZE ROOTLBA BYTES",rows[2],5);
#else
    retail_display_values("INIT3 INITTOK STAT VERSTOK STAT",rows[1],5);
    retail_display_values("DRIVERET STATE TYPE PVDWORD PVDCRC",rows[2],5);
#endif
    retail_display_values("CMD FAD COUNT DST TOKEN",rows[3],5);
    retail_display_values("CHECK ERR1 ERR2 BYTES ATA",rows[4],5);
    stopped(message,s->calls);
}
static struct startup_gd_state *startup_gd_uncached_state(void) {
#ifdef KUI_RETAIL_STARTUP_TRACE_TEST
    return &kui_retail_startup_gd_state;
#else
    return (struct startup_gd_state *)startup_trace_owner(
        (uint32_t)(uintptr_t)&kui_retail_startup_gd_state);
#endif
}
#if KUI_RETAIL_STARTUP_TRACE >= 3
static uint32_t startup_gd_le32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static void startup_gd_capture_pvd(struct startup_gd_state *s) {
    s->pvd_captured=1u;
    if(!s->read_count || s->check[2]<2048u || (s->read_destination&3u) ||
       !startup_gd_guest(s->read_destination,2048u)) return;
    const uint8_t *p=startup_gd_pointer(s->read_destination);
    s->pvd_header=startup_gd_le32(p);
    s->pvd_crc=kui_retail_crc32(0,p,2048u);
    s->pvd_valid=p[0]==1u && p[1]=='C' && p[2]=='D' && p[3]=='0' &&
        p[4]=='0' && p[5]=='1' && p[6]==1u;
    s->pvd_sector_bytes=(uint32_t)p[128]|((uint32_t)p[129]<<8);
    s->path_bytes=startup_gd_le32(p+132);
    s->path_lba=startup_gd_le32(p+140);
    s->root_lba=startup_gd_le32(p+158);
    s->root_bytes=startup_gd_le32(p+166);
}
#endif
static void startup_gd_install(void) {
    /* Point 3 runs after the one-shot wrapper published RAM and disabled
     * caches. Only the proven native BC vector is replaced, through P2. */
    const uint8_t *resident=startup_trace_owner(KUI_RETAIL_RESIDENT_ADDRESS);
    for(size_t i=0;i<resident_bytes;i++)
        if(resident[i]!=resident_blob[i])
            relay_stopped("OWNER STARTUP ALTERED READER",(uint32_t)i);
    volatile uint32_t *vector=(volatile uint32_t *)(uintptr_t)0xac0000bcu;
    uint32_t handler=*vector,area=handler&0xff000000u;
    uint32_t p1=(handler&0x1fffffffu)|0x80000000u;
    if((handler&1u) || (area!=0x8c000000u && area!=0xac000000u) ||
       p1<KUI_RETAIL_RESIDENT_ADDRESS || !resident_bytes ||
       p1-KUI_RETAIL_RESIDENT_ADDRESS>=resident_bytes || p1>=resident_limit)
        relay_stopped("GD TRACE VECTOR INVALID",handler);
    struct startup_gd_state *s=startup_gd_uncached_state();
    memset(s,0,sizeof(*s));
    s->handler=handler;
    s->init_result=s->init_status=s->version_status=s->read_status=UINT32_MAX;
    s->drive_result=s->drive_status=s->drive_type=UINT32_MAX;
#if KUI_RETAIL_STARTUP_TRACE >= 3
    s->mount_result=UINT32_MAX;
#endif
    *vector=(uint32_t)(uintptr_t)kui_retail_startup_gd_proxy|0x20000000u;
    kui_retail_startup_trace_publish(0x8c0000bcu,4u);
}
void kui_retail_startup_gd_before(uint32_t *frame,
    struct startup_gd_state *s,uint32_t ccr) {
    if(!startup_gd_frame_valid(frame) || s->active!=1u)
        startup_gd_report(s,"GD TRACE FRAME INVALID");
    for(unsigned i=0;i<21u;i++) s->original[i]=frame[i];
    s->frame_address=(uint32_t)(uintptr_t)frame;
    s->ccr=ccr;
    s->function=frame[13]; /* R7 */
    s->command=frame[16]; /* R4: command, token, or function-specific argument. */
    s->caller=frame[5];
    s->result=UINT32_MAX;
    ++s->calls;
    if(frame[14]!=UINT32_MAX && s->function==0u) { /* R6=-1 is setup. */
        uint32_t params=frame[15];
        if(s->command==16u || s->command==17u) {
            if((params&3u) || !startup_gd_guest(params,16u))
                startup_gd_report(s,"GD TRACE READ PARAMS INVALID");
            const volatile uint32_t *p=startup_gd_pointer(params);
            /* Record a candidate without losing the first accepted request
             * if a second submission is rejected while it is in flight. */
            s->candidate_fad=p[0];s->candidate_count=p[1];
            s->candidate_destination=p[2];
        } else if(s->command==40u) {
            if((params&3u) || !startup_gd_guest(params,4u))
                startup_gd_report(s,"GD TRACE VERSION PARAMS INVALID");
            s->candidate_destination=*(volatile const uint32_t *)startup_gd_pointer(params);
        }
    }
    /* Assembly resumes the original native stack and register state, with
     * only PR changed so the actual handler's result can be observed. */
    frame[5]=(uint32_t)(uintptr_t)kui_retail_startup_gd_return|0x20000000u;
}
void kui_retail_startup_gd_after(uint32_t *frame,
    struct startup_gd_state *s,uint32_t ccr) {
    if(!startup_gd_frame_valid(frame) || s->active!=1u ||
       (uint32_t)(uintptr_t)frame!=s->frame_address || ccr!=s->ccr)
        startup_gd_report(s,"GD TRACE RETURN FRAME INVALID");
    uint32_t result=frame[20];
    s->result=result;
    if(s->original[14]!=UINT32_MAX) {
        if(s->function==3u) s->init_result=result;
        else if(s->function==0u) {
            if(s->command==24u && !s->init_token) s->init_token=result;
            else if(s->command==40u && !s->version_token && (int32_t)result>0) {
                s->version_token=result;
                s->version_destination=s->candidate_destination;
            } else if(s->command==16u || s->command==17u) {
#if KUI_RETAIL_STARTUP_TRACE >= 3
                if((int32_t)result>0) ++s->read_requests;
                if((int32_t)result>0 || !s->read_token) {
                    s->read_status=UINT32_MAX;s->read_completed=0;
                    memset(s->check,0,sizeof(s->check));
#else
                if(!s->read_token) {
#endif
                    s->read_token=(int32_t)result>0?result:0u;s->read_command=s->command;
                    s->read_fad=s->candidate_fad;s->read_count=s->candidate_count;
                    s->read_destination=s->candidate_destination;
                }
            }
        } else if(s->function==4u) {
            uint32_t output=s->original[16];
            s->drive_result=result;
            if((output&3u) || !startup_gd_guest(output,8u))
                startup_gd_report(s,"GD TRACE DRIVE OUTPUT INVALID");
            const volatile uint32_t *p=startup_gd_pointer(output);
            s->drive_status=p[0];s->drive_type=p[1];
        } else if(s->function==1u) {
            uint32_t output=s->original[15];
            if((output&3u) || !startup_gd_guest(output,16u))
                startup_gd_report(s,"GD TRACE CHECK OUTPUT INVALID");
            if(s->command==s->init_token && s->init_token) s->init_status=result;
            if(s->command==s->version_token && s->version_token) {
                s->version_status=result;
                if(result==2u && startup_gd_guest(s->version_destination,28u))
                    s->version_crc=kui_retail_crc32(0,
                        startup_gd_pointer(s->version_destination),28u);
            }
            if(s->command==s->read_token && s->read_token) {
                s->read_status=result;
                const volatile uint32_t *p=startup_gd_pointer(output);
                for(unsigned i=0;i<4u;i++) s->check[i]=p[i];
                if(result==2u) {
#if KUI_RETAIL_STARTUP_TRACE >= 3
                    if(!s->read_completed) ++s->completed_reads;
                    s->read_completed=1u;
                    if(!s->pvd_captured) startup_gd_capture_pvd(s);
#else
                    if(s->read_count && s->check[2]>=2048u &&
                       !(s->read_destination&3u) &&
                       startup_gd_guest(s->read_destination,2048u)) {
                        s->pvd_header=*(volatile const uint32_t *)startup_gd_pointer(s->read_destination);
                        s->pvd_crc=kui_retail_crc32(0,
                            startup_gd_pointer(s->read_destination),2048u);
                    }
                    startup_gd_report(s,"FIRST GD READ COMPLETED");
#endif
                }
            }
        }
        if((int32_t)result<0 || (s->function==0u && result==0u))
            startup_gd_report(s,"GD CALL FAILED OR REJECTED");
    }
    if(s->calls>=STARTUP_GD_CALL_BUDGET)
        startup_gd_report(s,"GD CALL LIMIT - LAST RESULT SHOWN");
    /* Restore every original CPU word except the actual ABI return in R0.
     * No floating-point instruction, cache-mode change or GD call is made. */
    for(unsigned i=0;i<21u;i++) frame[i]=s->original[i];
    frame[20]=result;
    s->active=0;
}
#endif
void kui_retail_startup_trace_checkpoint(const uint32_t *frame,
    uint32_t point,uint32_t ccr) {
    uintptr_t address=((uintptr_t)frame&0x1fffffffu)|0x80000000u;
    if(point>=STARTUP_TRACE_POINTS || !startup_trace.armed ||
       (address&3u) || address<KUI_RETAIL_HOOK_STACK ||
       address>KUI_RETAIL_EXEC_ADDRESS-21u*4u)
        startup_trace_report("STARTUP TRACE STATE INVALID",point,NULL);
    startup_trace.ccr[point]=ccr;
    /* Restore this one shot before probing/resuming. Other checkpoints stay
     * armed. The wrapper invalidates I-cache before restoring the owner CCR. */
    uint32_t owner=startup_trace_address[point];
    memcpy(startup_trace_owner(owner),startup_trace.saved[point],STARTUP_TRACE_BYTES);
    kui_retail_startup_trace_publish(owner,STARTUP_TRACE_BYTES);
    kui_retail_startup_trace_resume=owner;
    if(startup_trace.passed!=((1u<<point)-1u))
        startup_trace_report("STARTUP TRACE ORDER CHANGED",point,frame);
#if KUI_RETAIL_STARTUP_TRACE >= 3
    if(point==4u) {
        struct startup_gd_state *s=startup_gd_uncached_state();
        /* R4 holds the real mount result at the common epilogue; R0 is the
         * saved interrupt mask. This checkpoint is terminal and never resumes. */
        s->mount_result=frame[16];
        startup_trace.passed|=1u<<point;
        startup_gd_report(s,"SDK MOUNT RETURN REACHED");
    }
#endif
    if(point==3u) {
#if KUI_RETAIL_STARTUP_TRACE >= 2
        startup_gd_install();
        startup_trace.passed|=1u<<point;
        return;
#else
        startup_trace_report("FIRST SDK GD INIT REACHED",point,frame);
#endif
    }
    int ready;
    if(point==0u) {
        ready=startup_trace_wait(point,0xa05f810cu,0x1ffu,0);
        if(ready) ready=startup_trace_wait(point,0xa05f810cu,0x1ffu,1);
    } else if(point==1u) {
        ready=startup_trace_wait(point,0xa05f688cu,32u,0);
    } else {
        ready=startup_trace_wait(point,0xa05f6900u,8u,1);
    }
    if(!ready) startup_trace_report("OWNER HARDWARE WAIT TIMED OUT",point,frame);
    startup_trace.passed|=1u<<point;
}
#endif
void kui_retail_stage_relay(const uint32_t *frame,uint32_t ccr) {
#ifdef KUI_RETAIL_CE
    retail_display_restore(&display);
    retail_display_line("BOOTSTRAP 2 REACHED GAME ENTRY");
#else
    (void)ccr;
#endif
    uintptr_t address=(uintptr_t)frame;
    address=(address&0x1fffffffu)|0x80000000u;
    if((address&3u) || address<KUI_RETAIL_BOOT2_ADDRESS ||
       address>KUI_RETAIL_EXEC_ADDRESS-21u*4u)
        relay_stopped("UNSUPPORTED BOOT STACK",(uint32_t)(uintptr_t)frame);
#ifdef KUI_RETAIL_CE
    /* Bootstrap 2 ran under the stage's exception table with SR.BL clear;
     * the entry gets the conventional boot VBR and BL back. */
    if(frame[3]==(uint32_t)(uintptr_t)__retail_ce_vbr) {
        uint32_t *state=(uint32_t *)(uintptr_t)frame;
        state[3]=KUI_RETAIL_BOOT_VBR;
        state[4]|=0x10000000u;
    }
#endif
    if(frame[3]!=KUI_RETAIL_BOOT_VBR || !(frame[4]&0x40000000u))
        relay_stopped("UNSUPPORTED BOOT CPU STATE",frame[3]);
#ifdef KUI_RETAIL_CE
    retail_display_hex("BOOT STACK",(uint32_t)address+21u*4u);
    retail_display_hex("BOOT SR",frame[4]);
    retail_display_hex("BOOT CACHE",ccr);
#endif
    uint8_t *boot=(uint8_t *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS;
    memcpy(boot,original_entry,ENTRY_PATCH_BYTES);
    uint32_t crc=kui_retail_crc32(0,boot,exec_bytes);
    if(crc!=boot_crc) relay_stopped("BOOTSTRAP ALTERED EXECUTABLE",crc);
    const uint8_t *resident=(const uint8_t *)(uintptr_t)KUI_RETAIL_RESIDENT_ADDRESS;
    size_t bytes=resident_bytes;
#ifdef KUI_RETAIL_CE
    /* Except the slot the stage filled with CE's kernel addresses. */
    const size_t slot=KUI_RETAIL_CE_KERNEL-KUI_RETAIL_RESIDENT_ADDRESS;
    if(memcmp(resident+slot,ce_kernel,sizeof(ce_kernel)))
        relay_stopped("BOOTSTRAP ALTERED RESIDENT",(uint32_t)slot);
#endif
    for(size_t i=0;i<bytes;i++) {
#ifdef KUI_RETAIL_CE
        if(i-slot<sizeof(ce_kernel)) continue;
#endif
        /* Detail: the offset of the first changed byte. */
        if(resident[i]!=resident_blob[i]) relay_stopped("BOOTSTRAP ALTERED RESIDENT",(uint32_t)i);
    }
#if defined(KUI_RETAIL_STARTUP_TRACE) && KUI_RETAIL_STARTUP_TRACE && !defined(KUI_RETAIL_CE)
    /* Intentional RAM-only patches follow the unmodified owner CRC check. */
    startup_trace_arm();
#endif
#if KUI_RETAIL_SONIC_STACK_TEST && !defined(KUI_RETAIL_CE)
    sonic_stack_arm();
#endif
#ifdef KUI_RETAIL_CE
    retail_display_line("READER INTACT - ORIGINAL ENTRY RESTORED");
    retail_display_hex("BODY CRC32",crc);
    retail_display_line("ENTERING WINDOWS CE");
    retail_display_line(manifest.title);
    retail_display_line("IF IT STOPS PHOTOGRAPH THE LAST SCREEN");
    retail_display_line("POWER OFF AND ON TO RETURN");
    retail_display_line("A RESET NOW MEANS WINDOWS CE ITSELF FAILED");
#endif
    /* Preserve the existing read-only frame delay for this isolated video
     * comparison. CPU/cache restoration remains entirely in the assembly. */
    retail_display_pause(STEP_PAUSE_FRAMES);
}
#ifdef KUI_RETAIL_CE
/* Set by the exception table in retail_stage.S: vector (1 general, 4 TLB
 * miss), then SPC, SSR, R15 (SGR) and PR when the exception was taken. */
uint32_t kui_retail_ce_fault[5];
void kui_retail_stage_exception(void) __attribute__((noreturn));
/* Entered from that table on the stage's stack while bootstrap 2 ran: show
 * where it faulted instead of letting the console reset. */
void kui_retail_stage_exception(void) {
    retail_display_restore(&display);
    retail_display_line("EXCEPTION WHILE BOOTSTRAP 2 RAN");
    uint32_t spc=kui_retail_ce_fault[1];
    uint32_t regs[5]={kui_retail_ce_fault[0],*(volatile uint32_t *)(uintptr_t)0xff000024u,
        *(volatile uint32_t *)(uintptr_t)0xff00000cu,spc,kui_retail_ce_fault[2]};
    retail_display_values("VECTOR   EXPEVT   TEA      SPC      SSR",regs,5);
    retail_display_values("R15      PR",kui_retail_ce_fault+3,2);
    /* The instructions around SPC, when it is in main RAM (MMU off). */
    uint32_t phys=spc&0x1fffffffu, area=spc>>29;
    if((area==0u || area==4u || area==5u) && phys>=0x0c000004u && phys<0x0cfffff0u) {
        const volatile uint32_t *at=(const volatile uint32_t *)(uintptr_t)(((phys&~3u)-4u)|0xa0000000u);
        uint32_t code[4]={at[0],at[1],at[2],at[3]};
        retail_display_values("CODE FROM SPC-4",code,4);
    }
    stopped("STOPPED IN BOOTSTRAP 2",spc);
}
#endif
