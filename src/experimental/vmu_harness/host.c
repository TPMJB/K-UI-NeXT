/* SPDX-License-Identifier: GPL-3.0-only */
/* Execute the ACTUAL console sequence and file callbacks against real FatFs.
 * Only the display, controller and physical media transport are substituted. */
#define _POSIX_C_SOURCE 200809L
#define main kui_vmu_console_main
#include "main.c"
#undef main
#include "kui/media.h"
#include <assert.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
static FILE *disk_image;
static uint64_t image_blocks;
static uint16_t frame[640u*480u];
uint16_t *vram_s=frame;
void vid_waitvbl(void) {}
void vid_flip(int page) {(void)page;}
void vid_set_mode(int mode,int format) {(void)mode;(void)format;}
void thd_sleep(unsigned milliseconds) {(void)milliseconds;}
maple_device_t *maple_enum_type(int n,uint32_t functions) {(void)n;(void)functions;return NULL;}
void *maple_dev_status(maple_device_t *controller) {(void)controller;return NULL;}
void kui_shell_font_draw(uint16_t *pixels,int x,int y,uint16_t color,const char *text,bool large) {
    (void)pixels;(void)x;(void)y;(void)color;(void)text;(void)large;
}
static uint64_t blocks(void *context) {(void)context;return image_blocks;}
static int raw_read(void *context,uint32_t lba,size_t count,uint8_t *bytes) {
    (void)context;assert((uint64_t)lba+count<=image_blocks);
    return fseeko(disk_image,(off_t)lba*512,SEEK_SET) || fread(bytes,512,count,disk_image)!=count?-1:0;
}
static int raw_write(void *context,uint32_t lba,size_t count,const uint8_t *bytes) {
    (void)context;assert((uint64_t)lba+count<=image_blocks);
    return fseeko(disk_image,(off_t)lba*512,SEEK_SET) || fwrite(bytes,512,count,disk_image)!=count?-1:0;
}
static int raw_sync(void *context) {(void)context;return fflush(disk_image)||fsync(fileno(disk_image))?-1:0;}
void kui_sd_set_params(unsigned selected,bool crc) {assert(selected==KUI_STORAGE_SCI && crc);}
bool kui_sd_connect(void) {
    const struct kui_media_ops operations={NULL,blocks,raw_read,raw_write,raw_sync};
    kui_media_set(&operations);return true;
}
void kui_sd_disconnect(void) {kui_media_set(NULL);assert(raw_sync(NULL)==0);}
void kui_storage_boot_begin(void) {}
unsigned kui_storage_selected(void) {return KUI_STORAGE_SCI;}
static uint32_t image_crc(unsigned number) {
    assert(connect_volume());char name[96];assert(path(name,number,"card.vmd"));
    FIL file;assert(f_open(&file,name,FA_READ)==FR_OK);assert(f_size(&file)==131072);
    uint32_t crc=0;uint8_t bytes[512];
    for(unsigned i=0;i<256;i++) {
        UINT done=0;assert(f_read(&file,bytes,512,&done)==FR_OK && done==512);
        if(i==PROBE_BLOCK) {pattern(number,expected);assert(!memcmp(bytes,expected,512));}
        crc=kui_crc32(crc,bytes,512);
    }
    assert(f_close(&file)==FR_OK);assert(disconnect_volume());return crc;
}
int main(int argc,char **argv) {
    if(argc!=3) return 2;
    struct stat info;assert(stat(argv[1],&info)==0 && info.st_size%512==0);
    image_blocks=(uint64_t)info.st_size/512;disk_image=fopen(argv[1],"r+b");assert(disk_image);
    bool ok,expect_failure=false;
    if(!strcmp(argv[2],"create")) {
        ok=run_test(true);
    } else if(!strcmp(argv[2],"verify")) {
        ok=run_test(false);
    } else if(!strcmp(argv[2],"refuse-create")) {
        expect_failure=true;ok=run_test(true);
    } else if(!strcmp(argv[2],"refuse-verify")) {
        expect_failure=true;ok=run_test(false);
    } else if(!strcmp(argv[2],"bad-identity")) {
        assert(connect_volume());char name[96];assert(path(name,2,"identity.bin"));
        FIL file;assert(f_open(&file,name,FA_READ|FA_WRITE)==FR_OK);
        UINT done=0;uint8_t byte=0xff;assert(f_write(&file,&byte,1,&done)==FR_OK && done==1);
        assert(f_close(&file)==FR_OK);assert(disconnect_volume());ok=true;
    } else if(!strcmp(argv[2],"short-journal")) {
        assert(connect_volume());char name[96];assert(path(name,2,"card.journal"));
        FIL file;assert(f_open(&file,name,FA_READ|FA_WRITE)==FR_OK);
        assert(f_lseek(&file,512)==FR_OK && f_truncate(&file)==FR_OK);
        assert(f_close(&file)==FR_OK);assert(disconnect_volume());ok=true;
    } else if(!strcmp(argv[2],"repeat")) {
        uint32_t before=image_crc(1);ok=run_test(true);
        assert(image_crc(1)==before);assert(image_crc(2)!=before);
    } else return 2;
    if(ok==expect_failure) {for(unsigned i=0;i<line_count;i++) fprintf(stderr,"%s\n",lines[i]);}
    close_pair();(void)f_mount(NULL,"0:",0);kui_sd_disconnect();assert(fclose(disk_image)==0);
    assert(ok!=expect_failure);printf("PASS native harness host sequence: %s\n",argv[2]);return 0;
}
