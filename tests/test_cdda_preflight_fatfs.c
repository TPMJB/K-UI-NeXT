/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual upstream FatFs/exFAT integration. The separately compiled writer
 * creates synthetic files; the reader has only a read-capable card handle.
 * Allocation expectations are parsed independently from exFAT by Python. */
#include "ff.h"
#include "diskio.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE *card_image;
static void path(char *out,size_t capacity,const char *base,const char *name) {
    int n=snprintf(out,capacity,"%s/%s",base,name);
    assert(n>0 && (size_t)n<capacity);
}
static void track_name(unsigned n,char out[12]) {
    assert(n>=1 && n<=15);
    int size=snprintf(out,12,"track%02u.%s",n,n==1 || n==3 || n==15?"bin":"raw");
    assert(size==11);
}

#ifdef KUI_CDDA_TEST_POPULATE
DSTATUS disk_initialize(BYTE drive) {return drive?STA_NOINIT:0;}
DSTATUS disk_status(BYTE drive) {return disk_initialize(drive);}
DRESULT disk_read(BYTE drive,BYTE *out,LBA_t sector,UINT count) {
    return drive || fseek(card_image,(long)sector*512,SEEK_SET) ||
        fread(out,512,count,card_image)!=count?RES_ERROR:RES_OK;
}
DRESULT disk_write(BYTE drive,const BYTE *out,LBA_t sector,UINT count) {
    return drive || fseek(card_image,(long)sector*512,SEEK_SET) ||
        fwrite(out,512,count,card_image)!=count?RES_ERROR:RES_OK;
}
DRESULT disk_ioctl(BYTE drive,BYTE command,void *out) {
    (void)out;return drive || command!=CTRL_SYNC?RES_PARERR:RES_OK;
}
static void mkdir_checked(const char *name) {
    FRESULT result=f_mkdir(name);assert(result==FR_OK || result==FR_EXIST);
}
static void copy_file(const char *inputs,const char *folder,const char *name,
    uint32_t cluster_bytes,bool fragment) {
    char source[1024],destination[1024];path(source,sizeof(source),inputs,name);
    path(destination,sizeof(destination),folder,name);
    FILE *input=fopen(source,"rb");assert(input);
    FIL output,spacer;assert(f_open(&output,destination,FA_CREATE_ALWAYS|FA_WRITE)==FR_OK);
    if(fragment) assert(f_open(&spacer,"0:/Games/space.bin",FA_CREATE_ALWAYS|FA_WRITE)==FR_OK);
    uint8_t data[4096];size_t got;uint32_t chunk=0;
    while((got=fread(data,1,sizeof(data),input))) {
        UINT put=0;assert(f_write(&output,data,(UINT)got,&put)==FR_OK && put==got);
        chunk+=(uint32_t)got;
        if(fragment && chunk==cluster_bytes) {
            /* Allocate a whole intervening cluster before growing this file. */
            memset(data,0xa7,sizeof(data));
            for(uint32_t at=0;at<cluster_bytes;at+=sizeof(data))
                assert(f_write(&spacer,data,sizeof(data),&put)==FR_OK && put==sizeof(data));
            chunk=0;
        }
    }
    assert(!ferror(input));assert(fclose(input)==0);
    assert(f_close(&output)==FR_OK);if(fragment) assert(f_close(&spacer)==FR_OK);
}
static void populate_folder(const char *inputs,const char *folder,uint32_t cluster_bytes,bool fragment) {
    mkdir_checked(folder);copy_file(inputs,folder,"fixture.gdi",cluster_bytes,false);
    char descriptor[1024];path(descriptor,sizeof(descriptor),folder,"fixture.gdi");
    char renamed[1024];path(renamed,sizeof(renamed),folder,"TOY_COMMANDER.gdi");
    assert(f_rename(descriptor,renamed)==FR_OK);
    for(unsigned n=1;n<=15;n++) {
        char name[12];track_name(n,name);copy_file(inputs,folder,name,cluster_bytes,fragment && n==3);
    }
}
int main(int argc,char **argv) {
    assert(argc==4);card_image=fopen(argv[1],"r+b");assert(card_image);
    FATFS fs;assert(f_mount(&fs,"0:",1)==FR_OK && fs.fs_type==FS_EXFAT);
    uint32_t cluster_bytes=(uint32_t)fs.csize*512;
    const char *mode=argv[3];
    if(!strcmp(mode,"populate")) {
        mkdir_checked("0:/KUI");mkdir_checked("0:/KUI/tests");mkdir_checked("0:/KUI/tests/cdda");
        mkdir_checked("0:/Games");mkdir_checked("0:/Games/Nested");
        populate_folder(argv[2],"0:/Games/Nested/Original",cluster_bytes,true);
        /* A complete fixture under the excluded harness folder cannot win. */
        populate_folder(argv[2],"0:/KUI/tests/cdda/Excluded",cluster_bytes,false);
        mkdir_checked("0:/Games/Incomplete");
        copy_file(argv[2],"0:/Games/Incomplete","fixture.gdi",cluster_bytes,false);
    } else if(!strcmp(mode,"duplicate")) {
        populate_folder(argv[2],"0:/Games/Second",cluster_bytes,false);
    } else {
        const char *value=!strcmp(mode,"config")?"0:/Games/Nested/Original/TOY_COMMANDER.gdi\r\n":
            !strcmp(mode,"fixture-config")?"0:/KUI/tests/cdda/Excluded/TOY_COMMANDER.gdi\n":
            "0:/Games/../Nested/Original/TOY_COMMANDER.gdi\n";
        FIL config;assert(f_open(&config,"0:/KUI/tests/cdda/preflight.cfg",FA_CREATE_ALWAYS|FA_WRITE)==FR_OK);
        UINT put=0;assert(f_write(&config,value,(UINT)strlen(value),&put)==FR_OK && put==strlen(value));
        assert(f_close(&config)==FR_OK);
    }
    assert(f_mount(NULL,"0:",0)==FR_OK);assert(fclose(card_image)==0);return 0;
}
#else
#include "cdda_storage.h"
#include "cdda_preflight_storage.h"
#include "sd_reader.h"
#include "sci_sd_bus.h"
#include "kui/cdda_preflight.h"
#include "kui/storage_policy.h"
#include "kui/retail_image.h"
#include "kui/hash.h"

extern volatile struct kui_storage_boot_marker cdda_storage_boot_marker;
static uint64_t card_blocks;
static unsigned leases,releases,requests,cancel_calls,cancel_after;
static const struct kui_loader_sd_bus bus={0};
enum kui_loader_sd_result kui_sci_sd_acquire(void) {leases++;return KUI_LOADER_SD_OK;}
void kui_sci_sd_release(void) {releases++;}
const struct kui_loader_sd_bus *kui_sci_sd_bus(void) {return &bus;}
enum kui_loader_sd_result kui_loader_sd_init_bus(struct kui_loader_sd *card,const struct kui_loader_sd_bus *source) {
    card->bus=*source;card->blocks=card_blocks;card->ready=true;return KUI_LOADER_SD_OK;
}
void kui_loader_sd_shutdown(struct kui_loader_sd *card) {card->ready=false;}
const char *kui_loader_sd_result_name(enum kui_loader_sd_result result) {(void)result;return "mock SD failure";}
enum kui_loader_sd_result kui_loader_sd_read_multi(struct kui_loader_sd *card,uint32_t lba,uint32_t count,void *out) {
    assert(card->ready && count && count<=KUI_LOADER_SD_MAX_READ_BLOCKS && (uint64_t)lba+count<=card_blocks);
    requests++;
    return fseek(card_image,(long)lba*512,SEEK_SET) || fread(out,512,count,card_image)!=count?
        KUI_LOADER_SD_RANGE:KUI_LOADER_SD_OK;
}
static bool cancelled(void *context) {(void)context;return cancel_calls++==cancel_after;}
static struct kui_game_image image;
static struct kui_cdda_preflight_report report;
static void assert_hex(const uint8_t *digest,const char *expected) {
    char hex[65];kui_hex(digest,32,hex);assert(!strcmp(hex,expected));
}
static void compare_read(const struct kui_game_file_ops *ops,const char *name,FILE *reference,uint32_t offset,size_t bytes) {
    uint8_t got[4096],expected[4096];assert(bytes<=sizeof(got));
    assert(fseek(reference,offset,SEEK_SET)==0 && fread(expected,1,bytes,reference)==bytes);
    assert(ops->read(ops->ctx,name,offset,got,bytes)==KUI_GAME_OK && !memcmp(got,expected,bytes));
}
static unsigned verify_extents(const char *expectation,const char *inputs) {
    FILE *expected=fopen(expectation,"r");assert(expected);
    const struct kui_game_file_ops *ops=cdda_preflight_storage_files();unsigned fragmented=0;
    for(unsigned n=1;n<=15;n++) {
        char name[12],sha[65];track_name(n,name);
        unsigned file_bytes,cluster_bytes,clusters,extents,words,first;
        unsigned long long rounded;
        assert(fscanf(expected,"%u %u %u %u %u %u %llu %64s",&file_bytes,&cluster_bytes,&clusters,&extents,&words,&first,&rounded,sha)==8);
        struct cdda_preflight_storage_extents actual;
        assert(cdda_preflight_storage_extents(name,&actual)==0);
        assert(actual.file_bytes==file_bytes && actual.cluster_bytes==cluster_bytes && actual.clusters==clusters &&
            actual.extents==extents && actual.map_words==words && actual.first_volume_lba==first && actual.rounded_bytes==rounded);
        assert_hex(actual.sha256,sha);if(extents>1)fragmented++;
        for(unsigned r=0;r<extents;r++) {
            struct cdda_preflight_storage_extent_run run;unsigned cluster,count,lba,file_sector,sectors;
            assert(fscanf(expected,"%u %u %u %u %u",&cluster,&count,&lba,&file_sector,&sectors)==5);
            assert(cdda_preflight_storage_extent_run(r,&run)==0 && run.first_cluster==cluster && run.clusters==count &&
                run.first_volume_lba==lba && run.file_sector==file_sector && run.sectors==sectors);
        }
        struct cdda_preflight_storage_extent_run invalid;
        assert(cdda_preflight_storage_extent_run(extents,&invalid)<0);
        char source[1024];path(source,sizeof(source),inputs,name);FILE *reference=fopen(source,"rb");assert(reference);
        /* Compare actual mapped f_lseek/f_read results around every cluster
         * boundary and both sides of EOF, not just the exported map. */
        for(uint32_t at=0;at<file_bytes;) {
            size_t take=file_bytes-at;if(take>997)take=997;compare_read(ops,name,reference,at,take);at+=(uint32_t)take;
        }
        for(uint32_t at=cluster_bytes;at<file_bytes;at+=cluster_bytes)
            compare_read(ops,name,reference,at-1,2);
        compare_read(ops,name,reference,file_bytes-1,1);
        assert(ops->read(ops->ctx,name,file_bytes,NULL,0)==KUI_GAME_OK);
        uint8_t byte;assert(ops->read(ops->ctx,name,file_bytes,&byte,1)==KUI_GAME_RANGE);
        assert(fclose(reference)==0);
    }
    assert(fgetc(expected)=='\n' && fgetc(expected)==EOF);assert(fclose(expected)==0);return fragmented;
}
int main(int argc,char **argv) {
    assert(argc==9);card_image=fopen(argv[1],"rb");assert(card_image);
    assert(fseek(card_image,0,SEEK_END)==0);card_blocks=(uint64_t)ftell(card_image)/512;
    cdda_storage_boot_marker.transport=KUI_STORAGE_SCI;cdda_storage_boot_marker.inverse=~(uint32_t)KUI_STORAGE_SCI;
    const char *mode=argv[8];
    if(!strcmp(mode,"ambiguous") || !strcmp(mode,"invalid-config") || !strcmp(mode,"fixture-config")) {
        assert(cdda_preflight_storage_discover()<0 && !cdda_preflight_storage_path());
        const char *failure=cdda_preflight_storage_failure();
        assert(strstr(failure,!strcmp(mode,"ambiguous")?"Multiple":!strcmp(mode,"fixture-config")?"original game folder":"Invalid"));
    } else {
        /* Cancellation closes all live DIRs, and rediscovery can recover. */
        cancel_calls=0;cancel_after=6;cdda_preflight_storage_cancel(cancelled,NULL);
        assert(cdda_preflight_storage_discover()<0 && !cdda_preflight_storage_path());
        cdda_preflight_storage_cancel(NULL,NULL);
        assert(cdda_preflight_storage_discover()==0);
        assert(!strcmp(cdda_preflight_storage_path(),"0:/Games/Nested/Original/TOY_COMMANDER.gdi"));
        const struct cdda_preflight_storage_scan *scan=cdda_preflight_storage_scan();
        assert(scan->candidates==1 && scan->configured==!strcmp(mode,"configured"));
        if(!scan->configured)assert(scan->incomplete==1 && scan->descriptors==2);
        uint8_t descriptor[451];size_t bytes=0;
        assert(cdda_preflight_storage_descriptor(descriptor,sizeof(descriptor),&bytes)==0 && bytes==451);
        assert(kui_game_image_open(descriptor,bytes,cdda_preflight_storage_files(),&image)==KUI_GAME_OK && image.count==15);
        assert(kui_cdda_preflight_read(&image,NULL,&report)==KUI_CDDA_PREFLIGHT_OK);
        assert(report.map.complete && report.map.count==15 && report.metadata.native_gd && !report.metadata.windows_ce &&
            report.ip_lba==45000 && report.ip_bytes==32768 && report.metadata.boot_lba==45021 && report.metadata.boot_bytes==5003);
        assert(report.ip_crc32==(uint32_t)strtoul(argv[4],NULL,16));assert_hex(report.ip_sha256,argv[5]);
        assert(report.boot_crc32==(uint32_t)strtoul(argv[6],NULL,16));assert_hex(report.boot_sha256,argv[7]);
        unsigned fragmented=verify_extents(argv[3],argv[2]);assert(fragmented>=1);
        /* Switching cached files invalidates the former CLMT view. */
        uint8_t sample[16];const struct kui_game_file_ops *ops=cdda_preflight_storage_files();
        assert(ops->read(ops->ctx,"track03.bin",0,sample,sizeof(sample))==KUI_GAME_OK);
        struct cdda_preflight_storage_extent_run run;assert(cdda_preflight_storage_extent_run(0,&run)<0);
        struct cdda_preflight_storage_extents untouched,saved;memset(&untouched,0xa5,sizeof(untouched));saved=untouched;
        cancel_calls=0;cancel_after=2;cdda_preflight_storage_cancel(cancelled,NULL);
        assert(cdda_preflight_storage_extents("track03.bin",&untouched)<0 && !memcmp(&untouched,&saved,sizeof(saved)));
        assert(cdda_preflight_storage_extent_run(0,&run)<0);
        cdda_preflight_storage_cancel(NULL,NULL);
        assert(cdda_preflight_storage_extents("track03.bin",&untouched)==0);
        assert(kui_cdda_preflight_read(&image,NULL,&report)==KUI_CDDA_PREFLIGHT_OK);
        assert(report.boot_crc32==(uint32_t)strtoul(argv[6],NULL,16));assert_hex(report.boot_sha256,argv[7]);
        assert(disk_write(0,sample,0,1)==RES_WRPRT);
    }
    cdda_preflight_storage_close();cdda_storage_shutdown();assert(leases==1 && releases==1 && requests);
    assert(fclose(card_image)==0);
    printf("PASS actual FatFs preflight %s: %s; read-only card\n",mode,
        !strcmp(mode,"normal") || !strcmp(mode,"configured")?
        "discovery, exact identities, independent allocation/CLMT, cached seeks, cancellation":"discovery refusal");
    return 0;
}
#endif
