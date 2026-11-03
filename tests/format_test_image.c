/* SPDX-License-Identifier: GPL-3.0-only */
/* Private host-only FatFs formatter; never linked into console code. */
#include "ff.h"
#include "diskio.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define SECTORS 196608u
static FILE *image;
DSTATUS disk_initialize(BYTE drive) {return drive || !image?STA_NOINIT:0;}
DSTATUS disk_status(BYTE drive) {return disk_initialize(drive);}
DRESULT disk_read(BYTE drive,BYTE *out,LBA_t sector,UINT count) {
    if(drive || !count || sector>=SECTORS || count>SECTORS-sector) return RES_PARERR;
    return fseek(image,(long)sector*512L,SEEK_SET) || fread(out,512,count,image)!=count?RES_ERROR:RES_OK;
}
DRESULT disk_write(BYTE drive,const BYTE *data,LBA_t sector,UINT count) {
    if(drive || !count || sector>=SECTORS || count>SECTORS-sector) return RES_PARERR;
    return fseek(image,(long)sector*512L,SEEK_SET) || fwrite(data,512,count,image)!=count?RES_ERROR:RES_OK;
}
DRESULT disk_ioctl(BYTE drive,BYTE command,void *out) {
    if(drive || !image) return RES_NOTRDY;
    if(command==CTRL_SYNC) return fflush(image)?RES_ERROR:RES_OK;
    if(command==GET_SECTOR_COUNT) {*(LBA_t *)out=SECTORS;return RES_OK;}
    if(command==GET_BLOCK_SIZE) {*(DWORD *)out=1;return RES_OK;}
    return RES_PARERR;
}
DWORD get_fattime(void) {return (DWORD)((46u<<25)|(11u<<21)|(2u<<16));}
int main(int argc,char **argv) {
    assert(argc==3);image=fopen(argv[1],"w+b");assert(image);
    assert(!fseek(image,(long)SECTORS*512L-1L,SEEK_SET) && fputc(0,image)==0);
    BYTE format=atoi(argv[2])?FM_EXFAT:FM_FAT32;uint8_t work[8192];
    MKFS_PARM parameters={.fmt=(BYTE)(format|FM_SFD),.n_fat=1,.align=1,.n_root=512,
        .au_size=format==FM_FAT32?512u:4096u};
    assert(f_mkfs("0:",&parameters,work,sizeof(work))==FR_OK);
    assert(!fclose(image));return 0;
}
