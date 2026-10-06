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
 * Firmware low32KiB and the owner's IP metadata/TOC stay in place throughout.
 * Bootstrap2 runs at its original address with the independent reader already
 * installed in the unused lower IP region. No proprietary bootstrap is bundled. */
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

static void install_resident(void) {
    size_t bytes=resident_bytes;
    if(!bytes || bytes>resident_limit-KUI_RETAIL_RESIDENT_ADDRESS)
        stopped("RESIDENT BOUNDS FAILED",(uint32_t)bytes);
    uint32_t firmware=*(volatile uint32_t *)(uintptr_t)0x8c0000bcu;
    uintptr_t canonical=(firmware&0x1fffffffu)|0x80000000u;
    if((firmware&1u) || canonical<0x8c000100u || canonical>=KUI_RETAIL_IP_ADDRESS)
        stopped("UNSUPPORTED FIRMWARE GD VECTOR",firmware);
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
    retail_display_pause(STEP_PAUSE_FRAMES);
    kui_retail_bootstrap_enter();
}

/* Called from P2 assembly with IRQs masked and caches clean/disabled. The
 * supported initial stack/VBR follow the conventional native GD bootstrap
 * layout; reject an incompatible entry before restoring executable entry.
 * General registers, SR, VBR, GBR, PR, MAC and cache configuration are restored
 * by assembly; fixed FPU registers, integer division and the linked machine-
 * code audit keep every floating-point register unchanged. */
void kui_retail_stage_relay(const uint32_t *frame,uint32_t ccr) {
    retail_display_restore(&display);
    retail_display_line("BOOTSTRAP 2 REACHED GAME ENTRY");
    uintptr_t address=(uintptr_t)frame;
    address=(address&0x1fffffffu)|0x80000000u;
    if((address&3u) || address<KUI_RETAIL_BOOT2_ADDRESS ||
       address>KUI_RETAIL_EXEC_ADDRESS-21u*4u)
        stopped("UNSUPPORTED BOOT STACK",(uint32_t)(uintptr_t)frame);
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
        stopped("UNSUPPORTED BOOT CPU STATE",frame[3]);
    retail_display_hex("BOOT STACK",(uint32_t)address+21u*4u);
    retail_display_hex("BOOT SR",frame[4]);
    retail_display_hex("BOOT CACHE",ccr);
    uint8_t *boot=(uint8_t *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS;
    memcpy(boot,original_entry,ENTRY_PATCH_BYTES);
    uint32_t crc=kui_retail_crc32(0,boot,exec_bytes);
    if(crc!=boot_crc) stopped("BOOTSTRAP ALTERED EXECUTABLE",crc);
    const uint8_t *resident=(const uint8_t *)(uintptr_t)KUI_RETAIL_RESIDENT_ADDRESS;
    size_t bytes=resident_bytes;
#ifdef KUI_RETAIL_CE
    /* Except the slot the stage filled with CE's kernel addresses. */
    const size_t slot=KUI_RETAIL_CE_KERNEL-KUI_RETAIL_RESIDENT_ADDRESS;
    if(memcmp(resident+slot,ce_kernel,sizeof(ce_kernel)))
        stopped("BOOTSTRAP ALTERED RESIDENT",(uint32_t)slot);
#endif
    for(size_t i=0;i<bytes;i++) {
#ifdef KUI_RETAIL_CE
        if(i-slot<sizeof(ce_kernel)) continue;
#endif
        /* Detail: the offset of the first changed byte. */
        if(resident[i]!=resident_blob[i]) stopped("BOOTSTRAP ALTERED RESIDENT",(uint32_t)i);
    }
    retail_display_line("READER INTACT - ORIGINAL ENTRY RESTORED");
#ifdef KUI_RETAIL_CE
    retail_display_hex("BODY CRC32",crc);
    retail_display_line("ENTERING WINDOWS CE");
#else
    retail_display_line("ENTERING GAME");
#endif
    retail_display_line(manifest.title);
    retail_display_line("IF IT STOPS PHOTOGRAPH THE LAST SCREEN");
    retail_display_line("POWER OFF AND ON TO RETURN");
#ifdef KUI_RETAIL_CE
    retail_display_line("A RESET NOW MEANS WINDOWS CE ITSELF FAILED");
#endif
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
