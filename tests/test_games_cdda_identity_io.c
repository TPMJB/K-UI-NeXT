/* SPDX-License-Identifier: GPL-3.0-only */
/* The production inspector's bounded identity pass, with generated sectors
 * and public geometry. No proprietary boot or audio content is present. */
#include "../src/apps/games.c"
#define main cdda_status_policy_fixture_main
#include "test_games_cdda_status.c"
#undef main

static struct {
    unsigned fault,opens,closes,reads,seeks,track;
    bool cancelled;
    FIL *active;
} identity_io;
static bool identity_cancel(void) {return identity_io.cancelled;}
static void identity_log(const char *format,...) {CHECK(format!=NULL);}
FRESULT f_stat(const TCHAR *path,FILINFO *info) {
    unsigned track=0;
    CHECK(sscanf(strrchr(path,'/')+1u,"track%u.bin",&track)==1 && track && track<=image.count);
    memset(info,0,sizeof(*info));info->fsize=image.tracks[track-1u].file_bytes;
    return FR_OK;
}
FRESULT f_open(FIL *file,const TCHAR *path,BYTE mode) {
    CHECK(!identity_io.active && mode==FA_READ);
    unsigned track=0;
    CHECK(sscanf(strrchr(path,'/')+1u,"track%u.bin",&track)==1 && track && track<=image.count);
    ++identity_io.opens;
    if(identity_io.fault==6u && track==5u) return FR_DISK_ERR;
    memset(file,0,sizeof(*file));file->obj.objsize=image.tracks[track-1u].file_bytes;
    identity_io.active=file;identity_io.track=track;return FR_OK;
}
FRESULT f_close(FIL *file) {
    CHECK(file==identity_io.active);identity_io.active=NULL;++identity_io.closes;
    if(identity_io.fault==4u && identity_io.track==5u) return FR_DISK_ERR;
    if(identity_io.fault==7u && identity_io.track==3u) return FR_DISK_ERR;
    return FR_OK;
}
FRESULT f_lseek(FIL *file,FSIZE_t offset) {
    CHECK(file==identity_io.active && offset<=file->obj.objsize);++identity_io.seeks;
    if(identity_io.fault==3u && identity_io.track==5u) return FR_DISK_ERR;
    file->fptr=offset;return FR_OK;
}
FRESULT f_read(FIL *file,void *out,UINT bytes,UINT *got) {
    CHECK(file==identity_io.active && bytes==2352u && bytes<=file->obj.objsize-file->fptr);
    ++identity_io.reads;
    if(identity_io.fault==1u && identity_io.reads==19u) {*got=0;return FR_DISK_ERR;}
    uint8_t *data=out;
    for(unsigned i=0;i<bytes;i++) data[i]=(uint8_t)(file->fptr+i*29u+53u);
    data[0]=data[11]=0;memset(data+1u,255,10u);data[15]=1u;
    file->fptr+=bytes;*got=bytes;
    if(identity_io.fault==2u && identity_io.reads==19u) --*got;
    if(identity_io.fault==5u && identity_io.reads==19u) identity_io.cancelled=true;
    return FR_OK;
}
int main(void) {
    for(unsigned fault=0;fault<10u;fault++) {
        fixture(1u);memset(&identity_io,0,sizeof(identity_io));identity_io.fault=fault;
        if(fault==8u) identity_io.cancelled=true;
        if(fault==9u) --meta.boot_bytes; /* Noncandidate: no identity reads. */
        struct image_files files={.cancel=identity_cancel,.log=identity_log};
        strcpy(files.root,"/Games/Test");
        image.files=(struct kui_game_file_ops){&files,image_stat,image_read};
        enum kui_game_cdda_compatibility status=KUI_GAME_CDDA_TESTED;
        enum kui_game_result result=tested_audio_status(&image,&files,&meta,&status);
        enum kui_game_result expected=(fault==0u || fault==9u)?KUI_GAME_OK:
            (fault==5u || fault==8u)?KUI_GAME_CANCELLED:KUI_GAME_IO;
        CHECK(result==expected && status==KUI_GAME_CDDA_UNTESTED);
        CHECK(!identity_io.active && !files.identity_reader && !files.identity_open);
        CHECK(identity_io.opens==identity_io.closes+(fault==6u?1u:0u));
        if(fault==0u) {
            CHECK(identity_io.opens==2u && identity_io.closes==2u);
            CHECK(identity_io.reads==16u+(meta.boot_bytes+2047u)/2048u);
        }
        if(fault>=8u) CHECK(!identity_io.opens && !identity_io.closes && !identity_io.reads);
    }
    printf("PASS inspector identity IO: %u checks; generated unknown SHA never tested, cached handles close on read/short-read/seek/open/close/cancel failures, noncandidates read nothing\n",checks);
    return 0;
}
