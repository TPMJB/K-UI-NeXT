/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "kui/maintenance.h"
#include "kui/media.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static FILE *image;static uint64_t blocks_count;static const char *fault;static unsigned reads,writes;static bool injected,payload;static FATFS fs;
static bool is(const char *name){return fault&&!strcmp(fault,name);}
static uint64_t blocks(void *ctx){(void)ctx;return blocks_count;}
static uint8_t byte(uint32_t offset){return (uint8_t)(((offset*97u)^(offset>>8))&255);}
static bool content(const uint8_t *data,size_t n){if(n<32)return false;for(unsigned i=0;i<32;i++)if(data[i]!=byte(i))return false;return true;}
static int rd(void *ctx,uint32_t block,size_t n,uint8_t *data){(void)ctx;if(fseeko(image,(off_t)block*512,SEEK_SET)||fread(data,512,n,image)!=n)return -1;if(payload&&content(data,n*512)&&(is("readback-fail")||is("readback-corrupt"))){injected=true;if(is("readback-fail"))return -1;data[20]^=1;}return 0;}
static int wr(void *ctx,uint32_t block,size_t n,const uint8_t *data){(void)ctx;++writes;if(content(data,n*512)){payload=true;if(is("write-fail")){injected=true;return -1;}}return fseeko(image,(off_t)block*512,SEEK_SET)||fwrite(data,512,n,image)!=n?-1:0;}
static int sync_image(void *ctx){(void)ctx;if(payload&&is("sync-fail")){injected=true;return -1;}return fflush(image)||fsync(fileno(image))?-1:0;}
static const struct kui_media_ops media={NULL,blocks,rd,wr,sync_image};
static bool ident(void *ctx,struct kui_maintenance_identity *out){(void)ctx;memset(out,0,sizeof(*out));out->region=2;out->language=1;out->audio=1;out->settings_valid=true;
    unsigned sizes[]={8192,8192,16384,32768,65536};uint32_t at=0;for(unsigned i=0;i<5;i++){out->start[i]=at;out->size[i]=sizes[i];at+=sizes[i];}
    if(is("bad-layout")){out->start[4]=0;injected=true;}if(is("identity-fail")){injected=true;return false;}if(is("identity-change")&&reads){out->region=3;injected=true;}return true;}
static int source_read(void *ctx,bool bios,uint32_t offset,uint8_t *data,size_t n){(void)ctx;assert(offset+n<=(bios?KUI_MAINTENANCE_BIOS_BYTES:KUI_MAINTENANCE_FLASH_BYTES));++reads;
    if(is("source-fail")){injected=true;return -1;}for(size_t i=0;i<n;i++)data[i]=byte(offset+(uint32_t)i);
    if((is("source-change")&&reads==2)||(is("verify-source-change")&&reads>64)){data[0]^=1;injected=true;}return 0;}
static bool cancel(void){if(is("cancel-before")||(is("cancel-writing")&&reads>=4)||(is("cancel-verifying")&&reads>=64)){injected=true;return true;}return false;}
static void logline(const char *format,...){(void)format;}
static void check_file(const char *path,unsigned bytes){FIL file;assert(f_open(&file,path,FA_READ)==FR_OK);assert(f_size(&file)==bytes);uint8_t data[4096];for(unsigned i=0;i<bytes;i+=sizeof(data)){UINT got;assert(f_read(&file,data,sizeof(data),&got)==FR_OK&&got==sizeof(data));for(unsigned j=0;j<sizeof(data);j++)assert(data[j]==byte(i+j));}assert(f_close(&file)==FR_OK);}
int main(int argc,char **argv){if(argc!=3)return 2;struct stat st;assert(stat(argv[1],&st)==0);blocks_count=(uint64_t)st.st_size/512;image=fopen(argv[1],"r+b");assert(image);kui_media_set(&media);assert(kui_mount(&fs,logline));fault=argv[2];const struct kui_maintenance_source src={NULL,ident,source_read};struct kui_app_status out;unsigned action=is("inspect")?0:is("bios")?2:1;
    kui_maintenance_execute(action,&src,&out,logline,cancel,NULL);
    bool good=is("good")||is("inspect")||is("bios");assert(out.passed==good);
    if(good){assert(out.complete&&!out.errors);if(is("inspect"))assert(!writes&&!reads);else{char path[128];snprintf(path,sizeof(path),"0:/KUI/backups/system/%s-0001/image.bin",action==2?"bios":"flash");check_file(path,action==2?KUI_MAINTENANCE_BIOS_BYTES:KUI_MAINTENANCE_FLASH_BYTES);
        kui_maintenance_execute(action,&src,&out,logline,cancel,NULL);assert(out.passed);check_file(path,action==2?KUI_MAINTENANCE_BIOS_BYTES:KUI_MAINTENANCE_FLASH_BYTES);snprintf(path,sizeof(path),"0:/KUI/backups/system/%s-0002/image.bin",action==2?"bios":"flash");check_file(path,action==2?KUI_MAINTENANCE_BIOS_BYTES:KUI_MAINTENANCE_FLASH_BYTES);}}
    else{assert(injected);if(is("cancel-before")||is("identity-fail")||is("bad-layout"))assert(!writes);fault=NULL;assert(f_mount(NULL,"0:",0)==FR_OK);kui_media_set(&media);assert(kui_mount(&fs,logline));FILINFO info;FRESULT r=f_stat("0:/KUI/backups/system/flash-0001/image.bin",&info);assert(r==FR_NO_FILE||r==FR_NO_PATH);}
    assert(f_mount(NULL,"0:",0)==FR_OK);assert(fclose(image)==0);printf("PASS maintenance %s\n",argv[2]);return 0;}
