/* SPDX-License-Identifier: GPL-3.0-only */
/* Exercise the actual cache helper through patched upstream FatFs. The device
 * counts real FAT-region reads, not calls to a mirrored filesystem helper. */
#define _POSIX_C_SOURCE 200809L
#include "kui/capture.h"
#include "kui/salvage.h"
#include "diskio.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FILE_SECTORS ((64u*1024u*1024u)/KUI_RAW_BYTES)
#define FILE_BYTES ((uint64_t)FILE_SECTORS*KUI_RAW_BYTES)
#define TARGETS 128u
static FILE *image;
static bool stopped,deny_map,fail_fat;
static unsigned close_calls,fail_close;
static uint64_t read_calls,read_sectors,fat_reads,write_calls,sync_calls;
static size_t map_allocated,map_peak;
static void *map_pointer;
static const uint32_t fat_begin=32u,fat_length=2521u;
static char last_error[128];

DWORD get_fattime(void) {return 0;}
DSTATUS disk_initialize(BYTE drive) {return drive?STA_NOINIT:0;}
DSTATUS disk_status(BYTE drive) {return drive?STA_NOINIT:0;}
DRESULT disk_read(BYTE drive,BYTE *data,LBA_t sector,UINT count) {
    assert(!drive);++read_calls;read_sectors+=count;
    bool fat=false;
    for(UINT n=0;n<count;++n)if(sector+n>=fat_begin && sector+n<fat_begin+2u*fat_length) {++fat_reads;fat=true;}
    if(fat && fail_fat)return RES_ERROR;
    return fseeko(image,(off_t)sector*512,SEEK_SET) || fread(data,512,count,image)!=count?RES_ERROR:RES_OK;
}
DRESULT disk_write(BYTE drive,const BYTE *data,LBA_t sector,UINT count) {
    assert(!drive);++write_calls;
    return fseeko(image,(off_t)sector*512,SEEK_SET) || fwrite(data,512,count,image)!=count?RES_ERROR:RES_OK;
}
DRESULT disk_ioctl(BYTE drive,BYTE command,void *data) {
    assert(!drive);
    switch(command) {
    case CTRL_SYNC:++sync_calls;return fflush(image)?RES_ERROR:RES_OK;
    case GET_SECTOR_COUNT:*(LBA_t *)data=327680u;return RES_OK;
    case GET_SECTOR_SIZE:*(WORD *)data=512;return RES_OK;
    case GET_BLOCK_SIZE:*(DWORD *)data=1;return RES_OK;
    default:return RES_PARERR;
    }
}

/* The production helper is included unchanged with its internal state fields.
 * Optical calls and journal logic remain covered by the salvage image suite. */
struct salvage {
    const struct kui_capture_plan *plan;
    const struct kui_capture_ops *ops;
    struct kui_salvage_status *status;
    bool ready,failed,track_open,packed_open,track_write,packed_write;
    unsigned open_track;
    FIL track_file,packed_file;
    DWORD track_map_small[32],*track_map_heap;
    char path[128];
};
static bool stop(struct salvage *s) {(void)s;return stopped;}
static void update(struct salvage *s,bool force) {(void)s;(void)force;}
static bool fail(struct salvage *s,const char *message) {
    s->failed=true;snprintf(last_error,sizeof(last_error),"%s",message);return false;
}
static bool io_close(struct salvage *s,FIL *file,bool ok) {
    ++close_calls;FRESULT result=f_close(file);
    if(close_calls==fail_close || result!=FR_OK)return fail(s,"Storage close failed");
    return ok;
}
static void path(struct salvage *s,const char *name) {snprintf(s->path,sizeof(s->path),"0:/%s",name);}
static void track_path(struct salvage *s,unsigned track) {assert(track<2);path(s,track?"TRACK2.BIN":"TRACK.BIN");}
static void log_line(const char *format,...) {(void)format;}
static void *profile_malloc(size_t bytes) {
    assert(!map_pointer && bytes<=32768u);
    if(deny_map)return NULL;
    void *p=malloc(bytes);assert(p);map_pointer=p;map_allocated=bytes;
    if(bytes>map_peak)map_peak=bytes;
    return p;
}
static void profile_free(void *p) {
    if(p) {assert(p==map_pointer);map_pointer=NULL;map_allocated=0;}
    free(p);
}
#define malloc profile_malloc
#define free profile_free
#include "../src/core/salvage_files.inc"
#undef malloc
#undef free

static void counters_reset(void) {read_calls=read_sectors=fat_reads=write_calls=sync_calls=0;}
static void repair_bytes(FIL *file,FSIZE_t offset,bool write) {
    uint8_t source[KUI_RAW_BYTES],saved[KUI_RAW_BYTES];UINT done;
    memset(source,0x3c,sizeof(source));
    assert(f_lseek(file,offset)==FR_OK && f_tell(file)==offset);
    if(!write) {assert(f_read(file,saved,sizeof(saved),&done)==FR_OK && done==sizeof(saved));return;}
    assert(f_write(file,source,sizeof(source),&done)==FR_OK && done==sizeof(source));
    assert(f_sync(file)==FR_OK);
    assert(f_lseek(file,offset)==FR_OK && f_tell(file)==offset);
    assert(f_read(file,saved,sizeof(saved),&done)==FR_OK && done==sizeof(saved));
    assert(!memcmp(saved,source,sizeof(saved)));
}
int main(int argc,char **argv) {
    assert(argc==3);const char *mode=argv[2];image=fopen(argv[1],"r+b");assert(image);
    FATFS fs;assert(f_mount(&fs,"0:",1)==FR_OK && fs.fs_type==FS_FAT32);
    struct kui_capture_plan plan={.count=2,.tracks={{1,4,0,150,150+FILE_SECTORS,150+FILE_SECTORS},{2,4,0,150,151,151}}};
    const struct kui_capture_ops ops={.log=log_line};
    struct kui_salvage_status status={0};
    struct salvage s={.plan=&plan,.ops=&ops,.status=&status,.ready=true};
    uint64_t setup_fat=0;unsigned targets=0;
    if(!strncmp(mode,"readonly",8) || !strcmp(mode,"upgrade")) {
        FIL seed;UINT done;uint8_t data[5120]={0};
        assert(f_open(&seed,"0:/patches.bin",FA_READ|FA_WRITE|FA_CREATE_NEW)==FR_OK &&
            f_write(&seed,data,sizeof(data),&done)==FR_OK && done==sizeof(data) && f_close(&seed)==FR_OK);
        bool readonly=strncmp(mode,"readonly",8)==0;
        if(readonly)assert(f_chmod("0:/TRACK.BIN",AM_RDO,AM_RDO)==FR_OK && f_chmod("0:/patches.bin",AM_RDO,AM_RDO)==FR_OK);
        FIL *track=cached_track(&s,0,false),*packed=cached_packed(&s,false);
        assert(track && packed && !s.track_write && !s.packed_write && !s.failed);
        if(!strcmp(mode,"readonly-track-upgrade"))assert(!cached_track(&s,0,true) && s.failed);
        else if(!strcmp(mode,"readonly-packed-upgrade"))assert(!cached_packed(&s,true) && s.failed);
        else if(!strcmp(mode,"upgrade")) {
            unsigned before=close_calls;
            assert(cached_track(&s,0,true)==track && cached_packed(&s,true)==packed && s.track_write && s.packed_write);
            assert(close_calls==before+2);
            assert(cached_track(&s,0,false)==track && cached_packed(&s,false)==packed && close_calls==before+2);
        }
        if(readonly) {FILINFO info;assert(f_stat("0:/TRACK.BIN",&info)==FR_OK && (info.fattrib&AM_RDO));assert(f_stat("0:/patches.bin",&info)==FR_OK && (info.fattrib&AM_RDO));}
    } else if(!strcmp(mode,"packed")) {
        uint8_t data[5120],readback[5120];memset(data,0x69,sizeof(data));
        FIL *file=cached_packed(&s,true);assert(file && !file->cltbl);
        for(unsigned n=0;n<64;++n) {
            assert(cached_packed(&s,true)==file && !file->cltbl);UINT done;FSIZE_t offset=(FSIZE_t)n*sizeof(data);
            assert(f_lseek(file,offset)==FR_OK && f_write(file,data,sizeof(data),&done)==FR_OK && done==sizeof(data));
            assert(f_sync(file)==FR_OK && f_lseek(file,offset)==FR_OK && f_read(file,readback,sizeof(readback),&done)==FR_OK && done==sizeof(readback));
            assert(!memcmp(data,readback,sizeof(data)));
        }
        assert(close_salvage_files(&s) && !s.packed_open);
        file=cached_packed(&s,false);assert(file && !file->cltbl && f_size(file)==64u*sizeof(data));
    } else if(!strcmp(mode,"missing-packed")) {
        assert(!cached_packed(&s,false) && s.failed && !s.packed_open);
    } else if(!strcmp(mode,"close-error")) {
        assert(cached_track(&s,0,true) && cached_packed(&s,true));fail_close=close_calls+1;
        unsigned before=close_calls;assert(!close_salvage_files(&s) && s.failed);
        assert(close_calls==before+2 && !s.track_open && !s.packed_open && !map_allocated);
    } else if(!strcmp(mode,"track-change-close-error")) {
        assert(cached_track(&s,0,true));fail_close=close_calls+1;
        assert(!cached_track(&s,1,true) && s.failed && !s.track_open && !map_allocated);
    } else if(!strcmp(mode,"map-error")) {
        fail_fat=true;assert(!cached_track(&s,0,true) && s.failed && s.track_open);fail_fat=false;
    } else if(!strcmp(mode,"stop")) {
        stopped=true;counters_reset();assert(!cached_track(&s,0,true) && !cached_packed(&s,true));
        assert(!read_calls && !write_calls && !s.failed && !s.track_open && !s.packed_open);stopped=false;
    } else if(!strcmp(mode,"not-ready")) {
        s.ready=false;counters_reset();assert(!cached_track(&s,0,true) && s.failed && !read_calls && !write_calls && !s.track_open);
    } else if(!strcmp(mode,"length")) {
        ++plan.tracks[0].end;assert(!cached_track(&s,0,true) && s.failed && s.track_open && !s.track_file.cltbl);
    } else {
        bool fresh=!strncmp(mode,"fresh-",6),reverse=strstr(mode,"reverse")!=NULL;
        deny_map=!strcmp(mode,"allocation-fallback");
        FIL *cached=NULL;
        if(!fresh) {
            uint64_t before=fat_reads;cached=cached_track(&s,0,true);assert(cached && !s.failed);setup_fat=fat_reads-before;
            if(deny_map || !strcmp(mode,"map-cap"))assert(!cached->cltbl && !map_allocated);
            else assert(cached->cltbl);
            if(!strcmp(mode,"grown-map"))assert(s.track_map_heap && map_allocated>sizeof(s.track_map_small));
        }
        counters_reset();targets=fresh || !strncmp(mode,"cached-",7)?TARGETS:2u;
        for(unsigned n=0;n<targets;++n) {
            unsigned at=reverse?targets-1u-n:n;FSIZE_t offset=FILE_BYTES-(targets-at)*KUI_RAW_BYTES;
            if(fresh) {
                FIL file;assert(f_open(&file,"0:/TRACK.BIN",FA_READ)==FR_OK);repair_bytes(&file,offset,false);assert(f_close(&file)==FR_OK);
                assert(f_open(&file,"0:/TRACK.BIN",FA_READ|FA_WRITE)==FR_OK);repair_bytes(&file,offset,true);assert(f_close(&file)==FR_OK);
            } else {assert(cached_track(&s,0,true)==cached);repair_bytes(cached,offset,false);repair_bytes(cached,offset,true);}
        }
    }
    bool closed=close_salvage_files(&s);
    if(!strcmp(mode,"map-error"))assert(s.failed);
    else assert(closed);
    assert(!map_pointer && !map_allocated && !s.track_open && !s.packed_open);
    assert(f_mount(NULL,"0:",0)==FR_OK && !fclose(image));
    printf("{\"mode\":\"%s\",\"targets\":%u,\"setup_fat_sectors\":%llu,\"read_calls\":%llu,\"read_sectors\":%llu,\"fat_sectors\":%llu,\"write_calls\":%llu,\"sync_calls\":%llu,\"map_peak_bytes\":%zu}\n",
        mode,targets,(unsigned long long)setup_fat,(unsigned long long)read_calls,(unsigned long long)read_sectors,
        (unsigned long long)fat_reads,(unsigned long long)write_calls,(unsigned long long)sync_calls,map_peak);
    return 0;
}
