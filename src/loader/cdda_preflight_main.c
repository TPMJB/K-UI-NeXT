/* SPDX-License-Identifier: GPL-3.0-only */
/* Separate read-only complete-image diagnostic. No AICA, launch or BIOS hook. */
#include "cdda_preflight_storage.h"
#include "cdda_storage.h"
#include "cdda_display.h"
#include "kui/cdda_preflight.h"
#include "kui/retail_image.h"
#include "kui/storage_policy.h"
#include "kui/hash.h"
#include <string.h>

#define PREFLIGHT_TRACKS 15u
#define PREFLIGHT_TICKS_SECOND 12500000u
#define PREFLIGHT_DEADLINE_TICKS (180u * PREFLIGHT_TICKS_SECOND)
static struct kui_game_image preflight_image;
static struct kui_cdda_preflight_report preflight_report;
static struct kui_retail_manifest preflight_manifest;
static struct cdda_preflight_storage_extents preflight_extents[PREFLIGHT_TRACKS];
static uint8_t preflight_gdi[KUI_GAME_GDI_LIMIT],preflight_gdi_sha256[32],preflight_map_sha256[32];
static uint32_t preflight_stages,preflight_failures,preflight_stack_used,preflight_start;
static unsigned preflight_phase=3u;
static uint32_t preflight_progress;
static bool preflight_clock_active,preflight_timed_out;
static const char *preflight_failure;
static const uint8_t original_gdi_sha256[32]={
    0x96,0xe3,0xa5,0x4b,0x9a,0xa5,0x28,0xc7,0x17,0x21,0x21,0xba,0x3c,0x6c,0xfa,0xbf,
    0x39,0x1a,0xac,0xb5,0xa8,0x4f,0x29,0x2e,0x89,0x84,0x3f,0xf2,0xd8,0x85,0x38,0x03};
#ifdef CDDA_PREFLIGHT_HOST_TEST
extern uint32_t cdda_preflight_host_ticks(void);
extern bool cdda_preflight_host_stack(uint32_t *);
static uint32_t preflight_ticks(void) {return cdda_preflight_host_ticks();}
static bool preflight_stack(uint32_t *used) {return cdda_preflight_host_stack(used);}
#else
extern uint32_t __cdda_stack_bottom[] __asm__("__cdda_stack_bottom");
extern uint32_t __cdda_stack_top[] __asm__("__cdda_stack_top");
/* The private SCI lease already owns TMU1. Sampling never writes its registers. */
static uint32_t preflight_ticks(void) {
    return UINT32_MAX-*(volatile uint32_t *)(uintptr_t)0xffd80018u;
}
static bool preflight_stack(uint32_t *used) {
    for(unsigned i=0;i<16u;i++) if(__cdda_stack_bottom[i]!=0x43444441u) return false;
    const uint32_t *first=__cdda_stack_bottom+16u;
    while(first<__cdda_stack_top && *first==0xa5a5a5a5u) ++first;
    *used=(uint32_t)((uintptr_t)__cdda_stack_top-(uintptr_t)first);
    return *used && *used<=65536u-64u;
}
#endif
static bool preflight_cancelled(void *ctx) {
    (void)ctx;
    if(preflight_clock_active && (uint32_t)(preflight_ticks()-preflight_start)>=PREFLIGHT_DEADLINE_TICKS)
        preflight_timed_out=true;
    return preflight_timed_out;
}
static bool preflight_progress_hook(void *ctx,enum kui_cdda_preflight_phase phase,uint32_t done,uint32_t total) {
    (void)ctx;(void)total;
    if(preflight_cancelled(NULL)) return false;
    if(preflight_phase!=(unsigned)phase || done-preflight_progress>=524288u) {
        preflight_phase=(unsigned)phase;preflight_progress=done;
        cdda_display_line(phase==KUI_CDDA_PREFLIGHT_INSPECT?"Checking boot metadata and raw headers...":
            phase==KUI_CDDA_PREFLIGHT_HASH_IP?"Hashing logical IP header...":"Hashing exact boot-file bytes...");
    }
    return true;
}
static bool preflight_fail(const char *reason) {
    preflight_failure=preflight_timed_out?"180-second read deadline":reason;
    preflight_failures++;return false;
}
static void hash32(struct kui_sha256 *hash,uint32_t value) {
    uint8_t wire[4];for(unsigned i=0;i<4u;i++) wire[i]=(uint8_t)(value>>(8u*i));
    kui_sha256_update(hash,wire,sizeof(wire));
}
static void hash64(struct kui_sha256 *hash,uint64_t value) {
    hash32(hash,(uint32_t)value);hash32(hash,(uint32_t)(value>>32));
}
static void copy_field(char *out,size_t capacity,const char *in) {
    size_t n=0;while(in[n] && n+1u<capacity) {out[n]=in[n];++n;}out[n]=0;
}
static bool preflight_physical_map(void) {
    uint64_t card;uint32_t start,count;
    if(cdda_storage_geometry(&card,&start,&count) || !count || !card ||
       (uint64_t)start+count>card) return preflight_fail("Physical card/partition geometry");
    memset(&preflight_manifest,0,sizeof(preflight_manifest));
    preflight_manifest.card_sectors=card;preflight_manifest.partition_start=start;
    preflight_manifest.partition_end=(uint64_t)start+count;
    preflight_manifest.storage_transport=KUI_STORAGE_SCI;
    preflight_manifest.reader=KUI_RETAIL_READER_STANDARD;
    preflight_manifest.flags=KUI_RETAIL_IMAGE_BOOT_CRC;
    preflight_manifest.track_count=PREFLIGHT_TRACKS;
    preflight_manifest.session_lba=preflight_report.metadata.session_lba;
    preflight_manifest.boot_lba=preflight_report.metadata.boot_lba;
    preflight_manifest.boot_bytes=preflight_report.metadata.boot_bytes;
    preflight_manifest.boot_crc32=preflight_report.boot_crc32;
    preflight_manifest.ip_crc32=preflight_report.ip_crc32;
    preflight_manifest.gdi_crc32=kui_retail_crc32(0,preflight_gdi,451u);
    copy_field(preflight_manifest.title,sizeof(preflight_manifest.title),preflight_report.metadata.title);
    copy_field(preflight_manifest.product,sizeof(preflight_manifest.product),preflight_report.metadata.product);
    copy_field(preflight_manifest.bootfile,sizeof(preflight_manifest.bootfile),preflight_report.metadata.bootfile);
    copy_field(preflight_manifest.region,sizeof(preflight_manifest.region),preflight_report.metadata.region);
    struct kui_sha256 hash;kui_sha256_init(&hash);hash64(&hash,card);hash32(&hash,start);hash32(&hash,count);
    for(unsigned i=0;i<PREFLIGHT_TRACKS;i++) {
        if(preflight_cancelled(NULL)) return preflight_fail("Mapping cancelled");
        const struct kui_game_image_track *source=&preflight_image.tracks[i];
        struct cdda_preflight_storage_extents *geometry=&preflight_extents[i];
        if(cdda_preflight_storage_extents(source->name,geometry))
            return preflight_fail(cdda_preflight_storage_failure());
        if(geometry->file_bytes!=source->file_bytes || !geometry->extents ||
           geometry->extents>CDDA_PREFLIGHT_EXTENT_MAX ||
           PREFLIGHT_TRACKS+preflight_manifest.extent_count+geometry->extents>KUI_RETAIL_IMAGE_SLOTS)
            return preflight_fail("Complete physical map exceeds 160 slots");
        struct kui_retail_track *track=&preflight_manifest.slots[i].track;
        track->start_lba=source->start_lba;track->end_lba=source->end_lba;
        track->control=(uint8_t)source->control;
        track->first_extent=(uint16_t)(PREFLIGHT_TRACKS+preflight_manifest.extent_count);
        track->extent_count=(uint8_t)geometry->extents;
        uint32_t covered=0,needed=(uint32_t)((source->file_bytes+511u)/512u);
        hash32(&hash,i+1u);hash32(&hash,geometry->file_bytes);hash32(&hash,geometry->extents);
        kui_sha256_update(&hash,geometry->sha256,sizeof(geometry->sha256));
        for(unsigned e=0;e<geometry->extents;e++) {
            struct cdda_preflight_storage_extent_run run;
            if(cdda_preflight_storage_extent_run(e,&run) || run.file_sector!=covered || !run.sectors ||
               covered>=needed || (uint64_t)run.first_volume_lba+run.sectors>count)
                return preflight_fail("Invalid physical extent coverage");
            uint32_t take=run.sectors;
            if(take>needed-covered) take=needed-covered;
            if(e+1u<geometry->extents && take!=run.sectors)
                return preflight_fail("Allocation runs after logical EOF");
            uint64_t absolute=(uint64_t)start+run.first_volume_lba;
            if(absolute>UINT32_MAX || absolute+take>card || absolute+take>(uint64_t)start+count)
                return preflight_fail("Physical extent outside card/partition");
            for(unsigned p=0;p<preflight_manifest.extent_count;p++) {
                const struct kui_retail_extent *old=&preflight_manifest.slots[PREFLIGHT_TRACKS+p].extent;
                if(absolute<(uint64_t)old->card_lba+old->blocks && old->card_lba<absolute+take)
                    return preflight_fail("Backings share physical blocks");
            }
            struct kui_retail_extent *slot=&preflight_manifest.slots[PREFLIGHT_TRACKS+preflight_manifest.extent_count].extent;
            *slot=(struct kui_retail_extent){covered,(uint32_t)absolute,take};
            ++preflight_manifest.extent_count;covered+=take;
            hash32(&hash,slot->file_block);hash32(&hash,slot->card_lba);hash32(&hash,slot->blocks);
        }
        if(covered!=needed) return preflight_fail("Incomplete physical file map");
    }
    kui_sha256_digest(&hash,preflight_map_sha256);
    ++preflight_stages; /* Every backed byte now has a checked physical extent. */
    if(kui_retail_manifest_validate(&preflight_manifest)!=KUI_GAME_OK)
        return preflight_fail("Retail manifest validation");
    if(PREFLIGHT_TRACKS+preflight_manifest.extent_count>KUI_RETAIL_ASYNC_SLOTS)
        return preflight_fail("Complete map exceeds observer's 64 slots");
    ++preflight_stages;return true;
}
static bool cdda_preflight_test(void) {
    if(cdda_storage_init()) return preflight_fail(cdda_storage_last_failure());
    preflight_start=preflight_ticks();preflight_clock_active=true;
    cdda_storage_read_cancel(preflight_cancelled,NULL);
    cdda_preflight_storage_cancel(preflight_cancelled,NULL);
    if(cdda_preflight_storage_discover()) return preflight_fail(cdda_preflight_storage_failure());
    ++preflight_stages;
    size_t bytes=0;
    if(cdda_preflight_storage_descriptor(preflight_gdi,sizeof(preflight_gdi),&bytes))
        return preflight_fail(cdda_preflight_storage_failure());
    struct kui_sha256 hash;kui_sha256_init(&hash);kui_sha256_update(&hash,preflight_gdi,bytes);
    kui_sha256_digest(&hash,preflight_gdi_sha256);
    if(bytes!=451u || memcmp(preflight_gdi_sha256,original_gdi_sha256,32u))
        return preflight_fail("Original Toy descriptor fingerprint");
    enum kui_game_result open=kui_game_image_open(preflight_gdi,bytes,cdda_preflight_storage_files(),&preflight_image);
    if(open!=KUI_GAME_OK) return preflight_fail(kui_game_result_name(open));
    if(preflight_image.count!=PREFLIGHT_TRACKS || preflight_image.format!=KUI_GAME_IMAGE_GDI ||
       preflight_image.data_lba!=45000u || preflight_image.cd_image || preflight_image.scrambled)
        return preflight_fail("Original native GD image geometry");
    for(unsigned i=0;i<PREFLIGHT_TRACKS;i++) if(preflight_image.tracks[i].file_offset ||
        preflight_image.tracks[i].sector_bytes!=2352u || !preflight_image.tracks[i].file_bytes)
        return preflight_fail("Original complete raw backing geometry");
    ++preflight_stages;
    const struct kui_cdda_preflight_ops ops={NULL,preflight_cancelled,preflight_progress_hook};
    enum kui_cdda_preflight_result result=kui_cdda_preflight_read(&preflight_image,&ops,&preflight_report);
    if(result!=KUI_CDDA_PREFLIGHT_OK) return preflight_fail(kui_cdda_preflight_result_name(result));
    if(!preflight_report.map.complete || preflight_report.map.count!=PREFLIGHT_TRACKS ||
       !preflight_report.metadata.native_gd || preflight_report.metadata.native_cd ||
       preflight_report.metadata.windows_ce || !preflight_report.metadata.ip_valid ||
       !preflight_report.metadata.boot_valid || preflight_report.scrambled ||
       preflight_report.metadata.boot_bytes>KUI_RETAIL_IMAGE_BOOT_MAX)
        return preflight_fail("Unsupported native Toy boot metadata");
    ++preflight_stages;
    cdda_display_line("Mapping all 15 backing files (including audio)...");
    if(!preflight_physical_map()) return false;
    if(preflight_cancelled(NULL)) return preflight_fail("Preflight deadline");
    if(!preflight_stack(&preflight_stack_used)) return preflight_fail("Private stack guard/watermark");
    ++preflight_stages;return true;
}
/* Short bounded formatters avoid printf/newlib and preserve the integer ABI. */
static char line[96];
static size_t line_used;
static void begin(const char *label) {line_used=0;while(*label && line_used<sizeof(line)-1u) line[line_used++]=*label++;}
static void text(const char *value) {while(*value && line_used<sizeof(line)-1u) line[line_used++]=*value++;}
static void number(uint64_t value) {
    char digits[20];unsigned n=0;do {digits[n++]=(char)('0'+value%10u);value/=10u;} while(value);
    while(n && line_used<sizeof(line)-1u) line[line_used++]=digits[--n];
}
static void emit(void) {line[line_used]=0;cdda_display_line(line);}
static void pair(const char *label,uint64_t a,uint64_t b) {begin(label);number(a);text(" / ");number(b);emit();}
static void field(const char *label,const char *value) {begin(label);text(value);emit();}
static void crc(const char *label,uint32_t value) {
    static const char digits[]="0123456789abcdef";begin(label);
    for(unsigned i=8;i && line_used<sizeof(line)-1u;i--)
        line[line_used++]=digits[(value>>((i-1u)*4u))&15u];
    emit();
}
static void digest(const char *label,const uint8_t value[32]) {
    char encoded[65];kui_hex(value,32u,encoded);char second[33];memcpy(second,encoded+32,32u);second[32]=0;
    encoded[32]=0;field(label,encoded);field("  continued: ",second);
}
static void preflight_page(unsigned page) {
    cdda_display_init();begin("PROFILE13 PREFLIGHT / page ");number(page+1u);text(" of 6");emit();
    if(page==0u) {
        pair("Completed stages / failures: ",preflight_stages,preflight_failures);
        pair("Backed tracks / audio: ",preflight_image.count,12u);
        pair("Extent slots / 64-slot fit: ",PREFLIGHT_TRACKS+preflight_manifest.extent_count,
            PREFLIGHT_TRACKS+preflight_manifest.extent_count<=64u);
        field("Title: ",preflight_report.metadata.title);
        begin("Product / version: ");text(preflight_report.metadata.product);text(" / ");text(preflight_report.metadata.version);emit();
        field("Region: ",preflight_report.metadata.region);field("Boot file: ",preflight_report.metadata.bootfile);
        pair("Boot LBA / bytes: ",preflight_report.metadata.boot_lba,preflight_report.metadata.boot_bytes);
        crc("IP CRC32: ",preflight_report.ip_crc32);crc("Boot CRC32: ",preflight_report.boot_crc32);
        pair("IP / boot hashed bytes: ",preflight_report.ip_bytes,preflight_report.metadata.boot_bytes);
        cdda_display_number("Private stack bytes: ",preflight_stack_used);
        cdda_display_number("Checked card blocks: ",cdda_storage_blocks_read());
    } else if(page==1u) {
        digest("IP SHA256: ",preflight_report.ip_sha256);digest("Boot SHA256: ",preflight_report.boot_sha256);
        digest("Physical map SHA256: ",preflight_map_sha256);digest("GDI SHA256: ",preflight_gdi_sha256);
        cdda_display_line("Hashes identify this card map and stored payloads.");
        cdda_display_line("All track ends are exclusive; gaps stay unmapped.");
    } else if(page>=2u && page<=4u) {
        unsigned first=(page-2u)*5u;
        for(unsigned i=first;i<first+5u;i++) {
            const struct kui_cdda_disc_track *track=&preflight_report.map.tracks[i];
            begin("Track ");number(track->number);text(track->control?" D FAD[":" A FAD[");number(track->start_fad);
            text(",");number(track->end_fad);text(") bytes ");number(track->file_bytes);emit();
            begin("  runs ");number(preflight_extents[i].extents);text(" card ");
            number(preflight_manifest.partition_start+preflight_extents[i].first_volume_lba);
            text(" ");text(track->name);emit();
        }
    } else {
        cdda_display_line("Selected original descriptor:");
        const char *path=cdda_preflight_storage_path();
        while(*path) {begin("");for(unsigned n=0;n<64u && *path;n++) line[line_used++]=*path++;emit();}
        const struct cdda_preflight_storage_scan *scan=cdda_preflight_storage_scan();
        pair("Scan directories / entries: ",scan->directories,scan->entries);
        pair("Complete / incomplete matches: ",scan->candidates,scan->incomplete);
        cdda_display_number("Explicit path configured: ",scan->configured);
        begin("Card sectors: ");number(preflight_manifest.card_sectors);emit();
        pair("Partition start / sectors: ",preflight_manifest.partition_start,
            preflight_manifest.partition_end-preflight_manifest.partition_start);
    }
    cdda_display_finish(preflight_failures);
}
void cdda_main(void) {
    cdda_display_init();cdda_display_line("PROFILE13 PREFLIGHT / complete original Toy image");
    cdda_display_line("Finding the full dump already on the SCI card...");
    bool passed=cdda_preflight_test();
    cdda_preflight_storage_close();cdda_preflight_storage_cancel(NULL,NULL);cdda_storage_read_cancel(NULL,NULL);
    if(!passed) {
        cdda_display_init();cdda_display_line("PROFILE13 PREFLIGHT / stopped");
        cdda_display_number("Completed stages: ",preflight_stages);
        field("Reason: ",preflight_failure?preflight_failure:"Unknown preflight failure");
        cdda_display_line("Leave game files unchanged; photograph this screen.");
        cdda_display_line("Do not install test14 until this preflight passes.");
        cdda_display_finish(preflight_failures);cdda_storage_shutdown();
    }
#ifdef CDDA_PREFLIGHT_HOST_TEST
    if(passed) for(unsigned page=0;page<6u;page++) preflight_page(page);
#else
    if(!passed) for(;;) {__asm__ volatile("nop");}
    /* Keep the read-only SCI lease: its already-owned timer advances pages.
     * No further filesystem/card operations occur after the report is ready. */
    unsigned page=0;uint32_t last=preflight_ticks();preflight_page(page);
    for(;;) if((uint32_t)(preflight_ticks()-last)>=15u*PREFLIGHT_TICKS_SECOND) {
        last=preflight_ticks();page=(page+1u)%6u;preflight_page(page);
    }
#endif
}
