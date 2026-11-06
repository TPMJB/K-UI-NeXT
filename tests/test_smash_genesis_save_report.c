/* SPDX-License-Identifier: GPL-3.0-only */
/* Execute the production GUI report with the real ISO parser and exact catalog.
 * Only the filesystem write/close boundary is replaced by adverse fixtures. */
#include "kui/game_metadata.h"
#include "kui/game_launch_settings.h"
#include "kui/smash_native_menu.h"
#include "kui/probe.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct files {char root[512];bool reader_open;};
static uint8_t disk[512][2048];
static unsigned source_reads,rom_reads[10],closes,opens,writes,syncs,report_closes;
static unsigned fail_read,fail_write,fail_open,fail_sync,fail_close,fail_report_close,cancel_after;
static bool cancel_now,report_open;
static char output[4096];
static bool close_reader(struct files *files) {
    ++closes;files->reader_open=false;return !fail_close;
}
FRESULT f_open(FIL *file,const char *name,BYTE flags) {
    assert(!strcmp(name,"0:/KUI/apps/games/smash-genesis-saves.txt"));
    assert(flags==(FA_WRITE|FA_CREATE_ALWAYS));++opens;
    if(fail_open)return FR_DISK_ERR;
    (void)file;report_open=true;return FR_OK;
}
FRESULT f_write(FIL *file,const void *data,UINT size,UINT *written) {
    (void)file;assert(report_open && size<sizeof(output));++writes;
    if(fail_write) {*written=size?size-1u:0u;return FR_DISK_ERR;}
    memcpy(output,data,size);output[size]=0;*written=size;return FR_OK;
}
FRESULT f_sync(FIL *file) {(void)file;assert(report_open);++syncs;return fail_sync?FR_DISK_ERR:FR_OK;}
FRESULT f_close(FIL *file) {(void)file;assert(report_open);report_open=false;++report_closes;return fail_report_close?FR_DISK_ERR:FR_OK;}
#include "../src/apps/smash_genesis_save_report.inc"
static bool cancelled(void) {return cancel_now || (cancel_after && source_reads>=cancel_after);}
static void logger(const char *format,...) {(void)format;}
static enum kui_game_metadata_io_result read_sector(void *context,uint32_t lba,uint8_t out[2048]) {
    assert(context==disk && lba<512u);++source_reads;
    if(lba>=300u && lba<310u)++rom_reads[lba-300u];
    if(lba==fail_read)return KUI_GAME_METADATA_IO_ERROR;
    memcpy(out,disk[lba],2048u);return KUI_GAME_METADATA_IO_OK;
}
static bool data_range(void *context,uint32_t lba,uint32_t count) {
    assert(context==disk);return count && lba>=100u && lba<512u && count<=512u-lba;
}
static void number(uint8_t *p,uint32_t value,unsigned bytes) {
    for(unsigned i=0u;i<bytes;i++) {p[i]=(uint8_t)(value>>(8u*i));p[bytes+i]=(uint8_t)(value>>(8u*(bytes-1u-i)));}
}
static size_t record(uint8_t *p,const char *name,uint32_t lba,uint32_t bytes,bool directory) {
    size_t n=strlen(name),size=33u+n+((n&1u)?0u:1u);assert(size<=255u);
    memset(p,0,size);p[0]=(uint8_t)size;number(p+2u,lba,4u);number(p+10u,bytes,4u);
    p[25]=directory?2u:0u;number(p+28u,1u,2u);p[32]=(uint8_t)n;memcpy(p+33u,name,n);return size;
}
static void reset(struct files *files,struct kui_game_metadata *metadata) {
    memset(disk,0,sizeof(disk));memset(files,0,sizeof(*files));strcpy(files->root,"/games/smash");files->reader_open=true;
    *metadata=(struct kui_game_metadata){.root_lba=100u,.root_bytes=2048u,.volume_blocks=512u};
    source_reads=closes=opens=writes=syncs=report_closes=0u;
    fail_read=fail_write=fail_open=fail_sync=fail_close=fail_report_close=cancel_after=0u;cancel_now=report_open=false;
    memset(rom_reads,0,sizeof(rom_reads));memset(output,0,sizeof(output));size_t at=0u;
    for(unsigned i=1u;i<=10u;i++) {
        at+=record(disk[100]+at,kui_smash_native_menu_folder(i),199u+i,2048u,true);
        char name[32];snprintf(name,sizeof(name),"%s;1",kui_smash_native_menu_rom(i));
        record(disk[199u+i],name,299u+i,2048u,false);
        if(i==4u || i==6u) {
            uint8_t *h=disk[299u+i];h[0x1b0]='R';h[0x1b1]='A';h[0x1b2]=0xf8u;h[0x1b3]=0x20u;h[0x1ba]=0x3fu;
        }
    }
}
static bool run(struct files *files,struct kui_game_metadata *metadata) {
    const struct kui_game_metadata_ops ops={disk,read_sector,data_range};
    return smash_genesis_save_report(files,&ops,metadata,"72a15727dfba",logger,cancelled);
}
int main(void) {
    struct files files;struct kui_game_metadata metadata;
    reset(&files,&metadata);assert(run(&files,&metadata));
    assert(source_reads==21u && closes==1u && opens==1u && writes==1u && syncs==1u && report_closes==1u);
    for(unsigned i=0u;i<10u;i++)assert(rom_reads[i]==1u);
    assert(strstr(output,"SRAM bytes: 8192") && strstr(output,"SRAM bytes: 0") && strstr(output,"Source sectors read: 21"));
    for(unsigned i=1u;i<=10u;i++)assert(strstr(output,kui_smash_native_menu_name(i)));
    assert(strstr(output,"Eligibility does not prove"));
    reset(&files,&metadata);fail_read=300u;assert(run(&files,&metadata));assert(strstr(output,"ROM header read unavailable"));
    reset(&files,&metadata);disk[200][2]^=1u;assert(run(&files,&metadata));assert(strstr(output,"original ROM file unavailable"));
    reset(&files,&metadata);memset(disk[100],0,2048u);metadata.root_bytes=128u*2048u;
    assert(run(&files,&metadata) && source_reads==64u && strstr(output,"scan read budget exhausted"));
    reset(&files,&metadata);cancel_now=true;assert(!run(&files,&metadata));assert(!source_reads && !opens);
    reset(&files,&metadata);cancel_after=3u;assert(!run(&files,&metadata));assert(!opens && closes==1u);
    reset(&files,&metadata);strcpy(files.root,"/KUI/apps/GAMES");assert(!run(&files,&metadata));assert(!source_reads && !opens);
    reset(&files,&metadata);strcpy(files.root,"../games");assert(!run(&files,&metadata));assert(!source_reads && !opens);
    reset(&files,&metadata);fail_close=1u;assert(!run(&files,&metadata));assert(!opens && closes==1u);
    reset(&files,&metadata);fail_open=1u;assert(!run(&files,&metadata));assert(opens==1u && !writes && !report_closes);
    reset(&files,&metadata);fail_write=1u;assert(!run(&files,&metadata));assert(writes==1u && !syncs && report_closes==1u);
    reset(&files,&metadata);fail_sync=1u;assert(!run(&files,&metadata));assert(syncs==1u && report_closes==1u);
    reset(&files,&metadata);fail_report_close=1u;assert(!run(&files,&metadata));assert(syncs==1u && report_closes==1u);
    puts("PASS automatic Genesis report: actual ISO parser/catalog; ten 512-byte headers, 21 sectors; 64-sector budget, cancellation and write/close failures");
    return 0;
}
