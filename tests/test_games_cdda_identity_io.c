/* SPDX-License-Identifier: GPL-3.0-only */
/* The production inspector's bounded identity pass, with generated sectors
 * and public geometry. No proprietary boot or audio content is present. */
#include "../src/apps/games.c"
#define main cdda_status_policy_fixture_main
#include "test_games_cdda_status.c"
#undef main

static struct {
    unsigned fault,opens,closes,reads,seeks,track,profile_lookups;
    bool cancelled;
    FIL *active;
} identity_io;
static bool identity_cancel(void) {return identity_io.cancelled;}
static void identity_log(const char *format,...) {CHECK(format!=NULL);}
unsigned kui_storage_active(void) {return KUI_STORAGE_SCI;}
FRESULT f_stat(const TCHAR *path,FILINFO *info) {
    unsigned track=0;
    CHECK(sscanf(strrchr(path,'/')+1u,"track%u.bin",&track)==1 && track && track<=image.count);
    memset(info,0,sizeof(*info));info->fsize=image.tracks[track-1u].file_bytes;
    return FR_OK;
}
FRESULT f_open(FIL *file,const TCHAR *path,BYTE mode) {
    CHECK(!identity_io.active && mode==FA_READ);
    if(!strcmp(path,KUI_GAME_PROFILE_PACK_PATH)) {++identity_io.profile_lookups;return FR_NO_FILE;}
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
        strcpy(meta.version,"V1.000");strcpy(meta.region,"U");
        if(fault==8u) identity_io.cancelled=true;
        if(fault==9u) --meta.boot_bytes; /* Noncandidate: no identity reads. */
        struct image_files files={.cancel=identity_cancel,.log=identity_log};
        strcpy(files.root,"/Games/Test");
        image.files=(struct kui_game_file_ops){&files,image_stat,image_read};
        enum kui_game_cdda_compatibility status=KUI_GAME_CDDA_TESTED;
        struct kui_game_profile_identity candidate={.transport=KUI_STORAGE_SCI,.format=image.format,
            .audio_candidate=kui_games_cdda_test_candidate(&image,&meta),.boot_bytes=meta.boot_bytes,
            .windows_ce=meta.windows_ce,.native_gd=meta.native_gd};
        strcpy(candidate.product,meta.product);strcpy(candidate.version,meta.version);strcpy(candidate.region,meta.region);
        CHECK(kui_game_profile_candidate(&candidate,NULL)==(fault!=9u));
        struct kui_game_profile_match profile;memset(&profile,0xa5,sizeof(profile));
        enum kui_game_result result=tested_audio_status(&image,&files,&meta,&status,&profile,identity_log);
        enum kui_game_result expected=(fault==0u || fault==9u)?KUI_GAME_OK:
            (fault==5u || fault==8u)?KUI_GAME_CANCELLED:KUI_GAME_IO;
        CHECK(result==expected && status==KUI_GAME_CDDA_UNTESTED);
        CHECK(!profile.matched && !profile.automatic && !profile.name[0] && !profile.evidence[0]);
        CHECK(identity_io.profile_lookups==1u);
        CHECK(!identity_io.active && !files.identity_reader && !files.identity_open);
        CHECK(identity_io.opens==identity_io.closes+(fault==6u?1u:0u));
        if(fault==0u) {
            CHECK(identity_io.opens==2u && identity_io.closes==2u);
            CHECK(identity_io.reads==16u+(meta.boot_bytes+2047u)/2048u);
        }
        if(fault>=8u) CHECK(!identity_io.opens && !identity_io.closes && !identity_io.reads);
    }
    printf("PASS inspector identity IO: %u checks; generated unknown SHA never tested or profile-matched, cached handles close on read/short-read/seek/open/close/cancel failures, noncandidates read no images\n",checks);
    return 0;
}
