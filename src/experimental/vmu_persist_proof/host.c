/* SPDX-License-Identifier: GPL-3.0-only */
/* Run the exact standalone proof through real FatFs on disposable images.
 * Only KOS display/controller and the connected physical card are replaced. */
#define _POSIX_C_SOURCE 200809L
#define main kui_vmu_physical_console_main
#include "main.c"
#undef main
#include "kui/media.h"
#include <assert.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static FILE *disk_image;
static uint64_t image_blocks;
static bool connected,proof_raw_phase,inject_identity_change,inject_raw_failure;
static bool inject_final_release_failure,inject_profile_release_failure;
static unsigned raw_reads,raw_writes,raw_syncs,identity_reads,disconnect_calls;
static const uint8_t host_cid[16]={0x12,0x34,0x56,0x78,0x9a,0xbc,0xde,0xf0,
                                 0x13,0x57,0x9b,0xdf,0x24,0x68,0xac,0xe0};
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
    (void)context;assert(connected && count && (uint64_t)lba+count<=image_blocks);
    if(proof_raw_phase) {
        assert(!volume.fs_type && count==1u);++raw_reads;
        if(inject_raw_failure && raw_reads==7u) return -1;
    }
    return fseeko(disk_image,(off_t)lba*512,SEEK_SET) ||
        fread(bytes,512,count,disk_image)!=count?-1:0;
}
static int raw_write(void *context,uint32_t lba,size_t count,const uint8_t *bytes) {
    (void)context;
    /* No filesystem write or mounted raw write is authorized by this proof. */
    assert(connected && proof_raw_phase && !volume.fs_type && count==1u &&
        (uint64_t)lba+count<=image_blocks);
    ++raw_writes;printf("RAW-WRITE %08x %zu\n",lba,count);
    return fseeko(disk_image,(off_t)lba*512,SEEK_SET) ||
        fwrite(bytes,512,count,disk_image)!=count?-1:0;
}
static int raw_sync(void *context) {
    (void)context;if(proof_raw_phase) ++raw_syncs;
    return fflush(disk_image)||fsync(fileno(disk_image))?-1:0;
}
void kui_sd_set_params(unsigned selected,bool crc) {assert(selected==KUI_STORAGE_SCI && crc);}
bool kui_sd_connect(void) {
    assert(!connected);connected=true;proof_raw_phase=false;
    const struct kui_media_ops operations={NULL,blocks,raw_read,raw_write,raw_sync};
    kui_media_set(&operations);return true;
}
void kui_sd_disconnect(void) {
    kui_media_set(NULL);proof_raw_phase=false;connected=false;assert(raw_sync(NULL)==0);
}
bool kui_sd_vmu_proof_disconnect(void) {
    kui_sd_disconnect();++disconnect_calls;
    return !((inject_profile_release_failure && disconnect_calls==1u) ||
        (inject_final_release_failure && disconnect_calls==2u));
}
bool kui_sd_vmu_profile_identity(struct kui_vmu_sd_identity *out) {
    if(!out || !connected) return false;
    memset(out,0,sizeof(*out));memcpy(out->cid,host_cid,sizeof(host_cid));
    out->sectors=image_blocks;out->high_capacity=true;++identity_reads;
    if(inject_identity_change) out->cid[0]^=1u;
    return true;
}
bool kui_sd_raw_read_ops(struct kui_media_ops *out) {
    if(!out || !connected) return false;
    assert(!volume.fs_type);proof_raw_phase=true;
    *out=(struct kui_media_ops){.blocks=blocks,.read=raw_read};return true;
}
bool kui_sd_vmu_proof_raw_ops(struct kui_media_ops *out) {
    if(!out || !connected) return false;
    assert(!volume.fs_type);proof_raw_phase=true;
    *out=(struct kui_media_ops){NULL,blocks,raw_read,raw_write,raw_sync};return true;
}
bool kui_sd_vmu_proof_healthy(void) {return connected;}
void kui_storage_boot_begin(void) {}
unsigned kui_storage_selected(void) {return KUI_STORAGE_SCI;}

int main(int argc,char **argv) {
    if(argc!=3) return 2;
    struct stat info;assert(stat(argv[1],&info)==0 && info.st_size%512==0);
    image_blocks=(uint64_t)info.st_size/512;disk_image=fopen(argv[1],"r+b");assert(disk_image);
    bool commit=true,expect_failure=false,expect_committed_failure=false;
    if(!strcmp(argv[2],"commit")) {}
    else if(!strcmp(argv[2],"check")) commit=false;
    else if(!strcmp(argv[2],"refuse-commit")) expect_failure=true;
    else if(!strcmp(argv[2],"refuse-check")) {expect_failure=true;commit=false;}
    else if(!strcmp(argv[2],"refuse-cid")) {expect_failure=true;inject_identity_change=true;}
    else if(!strcmp(argv[2],"refuse-raw")) {expect_failure=true;inject_raw_failure=true;}
    else if(!strcmp(argv[2],"refuse-profile-release")) {expect_failure=true;inject_profile_release_failure=true;}
    else if(!strcmp(argv[2],"refuse-release")) {
        expect_failure=true;expect_committed_failure=true;inject_final_release_failure=true;
    }
    else return 2;
    bool ok=run_physical_proof(commit);
    for(unsigned row=0;row<line_count;row++) printf("%s\n",lines[row]);
    assert(!connected && !volume.fs_type);
    if(expect_failure) {
        if(expect_committed_failure) assert(raw_writes>0u && session.faulted);
        else assert(raw_writes==0u);
        for(unsigned row=0;row<line_count;row++) assert(strncmp(lines[row],"READY:",6u));
    }
    assert(ok!=expect_failure);
    assert(fclose(disk_image)==0);
    printf("RAW-SUMMARY READS %u WRITES %u SYNCS %u CID %u\n",
        raw_reads,raw_writes,raw_syncs,identity_reads);
    printf("PASS physical-map proof host sequence: %s\n",argv[2]);return 0;
}
