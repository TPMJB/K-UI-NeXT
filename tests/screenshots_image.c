/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/screenshot.h"
#include "diskio.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Test-only formatter on a bounded RAM disk. The encoder, screenshot file
 * backend and FatFs sources are the same sources used by the runtime. */
#define TEST_SECTORS 196608u
static uint8_t *disk;
DSTATUS disk_initialize(BYTE drive) {return drive==0 && disk?0:STA_NOINIT;}
DSTATUS disk_status(BYTE drive) {return disk_initialize(drive);}
DRESULT disk_read(BYTE drive,BYTE *out,LBA_t sector,UINT count) {
    if(drive || !out || !count || sector>=TEST_SECTORS || count>TEST_SECTORS-sector)return RES_PARERR;
    memcpy(out,disk+(size_t)sector*512u,(size_t)count*512u);return RES_OK;
}
DRESULT disk_write(BYTE drive,const BYTE *data,LBA_t sector,UINT count) {
    if(drive || !data || !count || sector>=TEST_SECTORS || count>TEST_SECTORS-sector)return RES_PARERR;
    memcpy(disk+(size_t)sector*512u,data,(size_t)count*512u);return RES_OK;
}
DRESULT disk_ioctl(BYTE drive,BYTE command,void *out) {
    if(drive || !disk)return RES_NOTRDY;
    if(command==CTRL_SYNC)return RES_OK;
    if(command==GET_SECTOR_COUNT) {*(LBA_t *)out=TEST_SECTORS;return RES_OK;}
    if(command==GET_BLOCK_SIZE) {*(DWORD *)out=1;return RES_OK;}
    return RES_PARERR;
}
DWORD get_fattime(void) {return (DWORD)((46u<<25)|(11u<<21)|(1u<<16));}
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static uint8_t *readback(const char *path,size_t *size) {
    FIL file;assert(f_open(&file,path,FA_READ)==FR_OK);*size=(size_t)f_size(&file);
    uint8_t *data=malloc(*size);assert(data);UINT got=0;
    assert(f_read(&file,data,(UINT)*size,&got)==FR_OK && got==*size);
    assert(f_close(&file)==FR_OK);return data;
}
static void check_frame(const uint8_t *data,size_t bytes,unsigned phase) {
    assert(bytes==921654u && !memcmp(data,"BM",2));
    assert(get32(data+2)==bytes && get32(data+10)==54 && get32(data+14)==40);
    assert(get32(data+18)==640 && get32(data+22)==480 && data[26]==1 && data[28]==24);
    static const uint8_t expected[6][3]={{0,0,255},{0,255,0},{255,0,0},
        {255,255,255},{0,0,0},{8,4,8}};
    for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++) {
        const uint8_t *pixel=data+54u+(479u-y)*1920u+x*3u;
        assert(!memcmp(pixel,expected[(x+y+phase)%6u],3u));
    }
}
static void fill(uint16_t *frame,unsigned phase) {
    static const uint16_t palette[]={0xf800,0x07e0,0x001f,0xffff,0x0000,0x0821};
    for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++)frame[y*640u+x]=palette[(x+y+phase)%6u];
}
static void remount(FATFS *fs,BYTE expected) {
    assert(f_mount(NULL,"0:",0)==FR_OK);memset(fs,0,sizeof(*fs));
    assert(f_mount(fs,"0:",1)==FR_OK && fs->fs_type==expected);
}
static void trial(BYTE format,BYTE expected,const char *label) {
    memset(disk,0,(size_t)TEST_SECTORS*512u);uint8_t work[8192];
    MKFS_PARM parameters={.fmt=(BYTE)(format|FM_SFD),.n_fat=1,.align=1,
        .n_root=512,.au_size=format==FM_FAT32?512u:4096u};
    FRESULT result=f_mkfs("0:",&parameters,work,sizeof(work));
    if(result!=FR_OK)fprintf(stderr,"Cannot format %s: %u\n",label,(unsigned)result);
    assert(result==FR_OK);
    FATFS fs;remount(&fs,expected);
    uint16_t *frame=malloc(640u*480u*sizeof(*frame));assert(frame);fill(frame,0);
    char path[KUI_SCREENSHOT_PATH_CAP];
    assert(kui_screenshot_save_rgb565(frame,640u*480u,640,480,640,path,NULL));
    assert(!strcmp(path,KUI_SCREENSHOT_FOLDER "/KUI-000001.bmp"));
    size_t bytes;uint8_t *first=readback(path,&bytes);check_frame(first,bytes,0);
    remount(&fs,expected);fill(frame,1);
    assert(kui_screenshot_save_rgb565(frame,640u*480u,640,480,640,path,NULL));
    assert(!strcmp(path,KUI_SCREENSHOT_FOLDER "/KUI-000002.bmp"));
    size_t second_bytes;uint8_t *second=readback(path,&second_bytes);check_frame(second,second_bytes,1);free(second);
    second=readback(KUI_SCREENSHOT_FOLDER "/KUI-000001.bmp",&second_bytes);
    assert(second_bytes==bytes && !memcmp(first,second,bytes));free(second);free(first);
    FIL file;static const uint8_t kept[]={0,1,2,3,4,5};UINT written=0;
    assert(f_open(&file,KUI_SCREENSHOT_FOLDER "/KUI-000009.part",FA_WRITE|FA_CREATE_NEW)==FR_OK);
    assert(f_write(&file,kept,sizeof(kept),&written)==FR_OK && written==sizeof(kept));
    assert(f_close(&file)==FR_OK);remount(&fs,expected);
    assert(kui_screenshot_save_rgb565(frame,640u*480u,640,480,640,path,NULL));
    assert(!strcmp(path,KUI_SCREENSHOT_FOLDER "/KUI-000010.bmp"));
    second=readback(KUI_SCREENSHOT_FOLDER "/KUI-000009.part",&second_bytes);
    assert(second_bytes==sizeof(kept) && !memcmp(second,kept,sizeof(kept)));free(second);
    DIR directory;FILINFO info;unsigned files=0,parts=0;
    assert(f_opendir(&directory,KUI_SCREENSHOT_FOLDER)==FR_OK);
    do {
        assert(f_readdir(&directory,&info)==FR_OK);
        if(info.fname[0]) {++files;if(strstr(info.fname,".part"))++parts;}
    } while(info.fname[0]);
    assert(f_closedir(&directory)==FR_OK && files==4 && parts==1);
    assert(f_mount(NULL,"0:",0)==FR_OK);free(frame);
    printf("PASS screenshot actual FatFs %s: full BMP pixels, remount, unique names, original and partial files retained\n",label);
}
int main(void) {
    disk=malloc((size_t)TEST_SECTORS*512u);assert(disk);
    trial(FM_FAT32,FS_FAT32,"FAT32");trial(FM_EXFAT,FS_EXFAT,"exFAT");
    free(disk);disk=NULL;return 0;
}
