/* SPDX-License-Identifier: GPL-3.0-only */
/* Test-only image formatter; compiled with a private FF_USE_MKFS=1 config. */
#define _POSIX_C_SOURCE 200809L
#include "ff.h"
#include "diskio.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static FILE *image;
static const LBA_t blocks=96u*1024u*1024u/512u;
DSTATUS disk_initialize(BYTE drive) {return drive || !image?STA_NOINIT:0;}
DSTATUS disk_status(BYTE drive) {return disk_initialize(drive);}
static bool range(BYTE drive,LBA_t start,UINT count) {
    return !drive && image && count && start<blocks && count<=blocks-start &&
        !fseeko(image,(off_t)start*512,SEEK_SET);
}
DRESULT disk_read(BYTE drive,BYTE *out,LBA_t start,UINT count) {
    if(!range(drive,start,count))return RES_PARERR;
    return fread(out,512,count,image)==count?RES_OK:RES_ERROR;
}
DRESULT disk_write(BYTE drive,const BYTE *in,LBA_t start,UINT count) {
    if(!range(drive,start,count))return RES_PARERR;
    return fwrite(in,512,count,image)==count?RES_OK:RES_ERROR;
}
DRESULT disk_ioctl(BYTE drive,BYTE operation,void *out) {
    if(drive || !image)return RES_NOTRDY;
    switch(operation) {
    case CTRL_SYNC:return !fflush(image) && !fsync(fileno(image))?RES_OK:RES_ERROR;
    case GET_SECTOR_COUNT:*(LBA_t *)out=blocks;return RES_OK;
    case GET_SECTOR_SIZE:*(WORD *)out=512;return RES_OK;
    case GET_BLOCK_SIZE:*(DWORD *)out=1;return RES_OK;
    default:return RES_PARERR;
    }
}
DWORD get_fattime(void) {return ((2026u-1980u)<<25)|(11u<<21)|(1u<<16);}
int main(int argc,char **argv) {
    if(argc!=3 || (strcmp(argv[1],"fat32") && strcmp(argv[1],"exfat")))return 2;
    image=fopen(argv[2],"wb+x");if(!image)return 2;
    bool ok=!ftruncate(fileno(image),(off_t)blocks*512);
    BYTE work[65536];bool exfat=!strcmp(argv[1],"exfat");
    MKFS_PARM parameters={.fmt=(BYTE)((exfat?FM_EXFAT:FM_FAT32)|FM_SFD),
        .n_fat=2,.align=1,.n_root=512,.au_size=exfat?4096u:512u};
    FRESULT result=ok?f_mkfs("0:",&parameters,work,sizeof(work)):FR_DISK_ERR;
    FATFS fs;ok=result==FR_OK && f_mount(&fs,"0:",1)==FR_OK;
    DWORD free_clusters=0;FATFS *mounted=NULL;
    ok=ok && f_getfree("0:",&free_clusters,&mounted)==FR_OK && mounted==&fs;
    if(ok)printf("FatFs fixture: %s, type=%u, free clusters=%lu\n",argv[1],fs.fs_type,(unsigned long)free_clusters);
    if(f_mount(NULL,"0:",0)!=FR_OK)ok=false;
    if(fflush(image))ok=false;
    if(fclose(image))ok=false;
    if(!ok)fprintf(stderr,"Fixture formatting failed: FatFs=%u\n",(unsigned)result);
    return ok?0:1;
}
