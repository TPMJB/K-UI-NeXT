/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/retail_loader_layout.h"
#include "kui/retail_image.h"
#include "retail_sd.h"
#include "retail_display.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

extern const uint8_t __retail_resident_blob_start[] __asm__("__retail_resident_blob_start");
extern const uint8_t __retail_resident_blob_end[] __asm__("__retail_resident_blob_end");
extern const uint8_t __retail_trampoline_start[] __asm__("__retail_trampoline_start");
extern const uint8_t __retail_trampoline_end[] __asm__("__retail_trampoline_end");
extern void kui_retail_bootstrap_enter(void) __attribute__((noreturn));

/* High storage is temporary: no pointer to it survives the final handoff.
 * Firmware low32KiB and the owner's IP metadata/TOC stay in place throughout.
 * Bootstrap1 and2 run at their original addresses before the lower IP region
 * is retired for the small resident. No copyrighted bootstrap is bundled. */
static uint8_t wire_copy[KUI_RETAIL_MAP_BYTES];
static uint8_t original_entry[KUI_RETAIL_TRAMPOLINE_BYTES];
static struct kui_retail_manifest manifest;
static struct kui_retail_image image;
static struct kui_loader_sd card;
static struct retail_display_state display;
static enum kui_loader_sd_result last_card_result;

static void stopped(const char *message,uint32_t detail) __attribute__((noreturn));
static void stopped(const char *message,uint32_t detail) {
    retail_display_line(message);
    retail_display_hex("DETAIL",detail);
    retail_display_line("LAUNCH STOPPED - PHOTOGRAPH THIS SCREEN");
    retail_display_line("POWER OFF AND ON TO RETURN");
    retail_display_line("SD CARD WAS READ ONLY");
    for(;;) __asm__ volatile("nop");
}
static int physical_read(void *context,uint32_t lba,uint8_t out[512]) {
    last_card_result=kui_loader_sd_read(context,lba,1,out);
    return last_card_result==KUI_LOADER_SD_OK?0:-1;
}
static void read_sectors(uint32_t lba,uint32_t count,void *out) {
    last_card_result=kui_retail_sd_acquire();
    if(last_card_result!=KUI_LOADER_SD_OK)
        stopped("SERIAL SD PINS NOT AVAILABLE",(uint32_t)last_card_result);
    enum kui_game_result result=kui_retail_image_read(&image,lba,count,
        KUI_GAME_SECTOR_MODE1,out,(size_t)count*KUI_GAME_DATA_BYTES);
    kui_retail_sd_release();
    if(result!=KUI_GAME_OK) {
        retail_display_hex("IMAGE LBA",lba);
        retail_display_hex("SD RESULT",(uint32_t)last_card_result);
        retail_display_hex("SD COMMAND",card.last_command);
        retail_display_hex("SD RESPONSE",card.last_response);
        stopped("IMAGE READ FAILED",(uint32_t)result);
    }
}
void kui_retail_stage_main(const uint8_t *wire) {
    retail_display_capture(&display);
    retail_display_restore(&display);
    retail_display_line("LAUNCHER HAS SHUT DOWN");
    memcpy(wire_copy,wire,sizeof(wire_copy));
    enum kui_game_result result=kui_retail_manifest_decode(wire_copy,&manifest);
    if(result!=KUI_GAME_OK) stopped("INVALID RETAIL MAP",(uint32_t)result);
    if(memcmp(manifest.title,"DEAD OR ALIVE 2",sizeof("DEAD OR ALIVE 2")) ||
       memcmp(manifest.bootfile,"1ST_READ.BIN",sizeof("1ST_READ.BIN")) ||
       manifest.boot_bytes<KUI_RETAIL_TRAMPOLINE_BYTES ||
       manifest.boot_bytes>KUI_RETAIL_EXEC_MAX_BYTES ||
       manifest.session_lba<45000)
        stopped("UNSUPPORTED RETAIL PROFILE",manifest.boot_bytes);
    last_card_result=kui_retail_sd_init(&card);
    if(last_card_result!=KUI_LOADER_SD_OK) {
        retail_display_hex("SD COMMAND",card.last_command);
        retail_display_hex("SD RESPONSE",card.last_response);
        stopped("READ ONLY SD INIT FAILED",(uint32_t)last_card_result);
    }
    /* Preparation knows the validated partition end, a lower bound on the
     * physical card size. Trailing unpartitioned sectors are legitimate. */
    if(card.blocks<manifest.card_sectors)
        stopped("SD CARD TOO SMALL FOR IMAGE MAP",(uint32_t)card.blocks);
    result=kui_retail_image_init(&image,&manifest,physical_read,&card);
    if(result!=KUI_GAME_OK) stopped("IMAGE READER INIT FAILED",(uint32_t)result);
    retail_display_line("LOADING OWNER IP AND EXECUTABLE");
    uint8_t *ip=(uint8_t *)(uintptr_t)KUI_RETAIL_IP_ADDRESS;
    read_sectors(manifest.session_lba,16,ip);
    uint32_t crc=kui_retail_crc32(0,ip,KUI_RETAIL_IP_BYTES);
    if(crc!=manifest.ip_crc32) stopped("IP CHECKSUM CHANGED",crc);
    uint8_t *boot=(uint8_t *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS;
    uint32_t sectors=(manifest.boot_bytes+2047u)/2048u;
    for(uint32_t done=0;done<sectors;) {
        uint32_t count=sectors-done;
        if(count>32) count=32;
        read_sectors(manifest.boot_lba+done,count,boot+(size_t)done*2048);
        done+=count;
    }
    crc=kui_retail_crc32(0,boot,manifest.boot_bytes);
    if(crc!=manifest.boot_crc32) stopped("EXECUTABLE CHECKSUM CHANGED",crc);
    if((size_t)(__retail_trampoline_end-__retail_trampoline_start)!=sizeof(original_entry))
        stopped("INVALID ENTRY TRAMPOLINE",0);
    memcpy(original_entry,boot,sizeof(original_entry));
    memcpy(boot,__retail_trampoline_start,sizeof(original_entry));
    retail_display_line("IP AND EXECUTABLE CHECKSUMS PASSED");
    retail_display_hex("BOOT BYTES",manifest.boot_bytes);
    retail_display_hex("SD BLOCKS READ",image.blocks_read);
    retail_display_line("ENTERING OWNER BOOTSTRAPS");
    kui_retail_bootstrap_enter();
}

/* Called from P2 assembly with IRQs masked and caches clean/disabled. The
 * supported initial stack/VBR follow the conventional native GD bootstrap
 * layout; reject an incompatible entry before replacing any bootstrap code.
 * General registers, SR, VBR, GBR, PR, MAC and cache configuration are restored
 * by assembly; -m4-nofpu leaves every floating-point register unchanged. */
void kui_retail_stage_relay(const uint32_t *frame,uint32_t ccr) {
    retail_display_restore(&display);
    retail_display_line("OWNER BOOTSTRAPS REACHED GAME ENTRY");
    uintptr_t address=(uintptr_t)frame;
    address=(address&0x1fffffffu)|0x80000000u;
    if((address&3u) || address<KUI_RETAIL_BOOT2_ADDRESS ||
       address>KUI_RETAIL_EXEC_ADDRESS-21u*4u)
        stopped("UNSUPPORTED BOOT STACK",(uint32_t)(uintptr_t)frame);
    if(frame[3]!=KUI_RETAIL_BOOT_VBR || !(frame[4]&0x40000000u))
        stopped("UNSUPPORTED BOOT CPU STATE",frame[3]);
    retail_display_hex("BOOT STACK",(uint32_t)address+21u*4u);
    retail_display_hex("BOOT SR",frame[4]);
    retail_display_hex("BOOT CACHE",ccr);
    uint8_t *boot=(uint8_t *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS;
    memcpy(boot,original_entry,sizeof(original_entry));
    uint32_t crc=kui_retail_crc32(0,boot,manifest.boot_bytes);
    if(crc!=manifest.boot_crc32) stopped("BOOTSTRAP ALTERED EXECUTABLE",crc);
    size_t resident_bytes=(size_t)(__retail_resident_blob_end-__retail_resident_blob_start);
    if(!resident_bytes || resident_bytes>KUI_RETAIL_RESIDENT_LIMIT-KUI_RETAIL_RESIDENT_ADDRESS)
        stopped("RESIDENT BOUNDS FAILED",(uint32_t)resident_bytes);
    uint32_t firmware=*(volatile uint32_t *)(uintptr_t)0x8c0000bcu;
    uintptr_t canonical=(firmware&0x1fffffffu)|0x80000000u;
    if((firmware&1u) || canonical<0x8c000100u || canonical>=KUI_RETAIL_IP_ADDRESS)
        stopped("UNSUPPORTED FIRMWARE GD VECTOR",firmware);
    memcpy((void *)(uintptr_t)KUI_RETAIL_RESIDENT_ADDRESS,
           __retail_resident_blob_start,resident_bytes);
    typedef int (*resident_init_fn)(const uint8_t *,uint32_t,
                                    const struct retail_display_state *);
    resident_init_fn init=(resident_init_fn)(uintptr_t)KUI_RETAIL_RESIDENT_ADDRESS;
    int initialized=init(wire_copy,firmware,&display);
    if(initialized) stopped("RETAIL RESIDENT INIT FAILED",(uint32_t)initialized);
    retail_display_line("RESIDENT INSTALLED - ORIGINAL ENTRY RESTORED");
    retail_display_line("ENTERING DEAD OR ALIVE 2");
    retail_display_line("IF IT STOPS PHOTOGRAPH THE LAST SCREEN");
    retail_display_line("POWER OFF AND ON TO RETURN");
}
