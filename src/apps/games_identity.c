/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/games_identity.h"
#include "kui/games.h"
#include "kui/games_storage.h"
#include "kui/game_metadata.h"
#include "kui/hash.h"
#include "ff.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define IDENTITY_CHUNK_BYTES 65536u
struct files {
    char root[KUI_DEST_ROOT_CAP],name[KUI_GAME_NAME_CAP];
    FIL file;
    bool opened;
    kui_cancel_fn cancel;
};
struct workspace {
    struct files files;
    struct kui_game_image image;
    struct kui_game_metadata metadata;
    struct kui_sha256 hash;
    uint8_t buffer[IDENTITY_CHUNK_BYTES];
};
static void quiet_log(const char *format,...) {(void)format;}
static bool stopped(const struct files *files) {return files->cancel && files->cancel();}
static bool join(const struct files *files,const char *name,char path[KUI_GAMES_FILE_CAP+3]) {
    if(!kui_destination_name_valid(name))return false;
    int n=snprintf(path,KUI_GAMES_FILE_CAP+3,"0:%s%s%s",files->root,strcmp(files->root,"/")?"/":"",name);
    return n>0 && n<(int)KUI_GAMES_FILE_CAP+3;
}
static bool split(const char *path,struct files *files,char name[KUI_GAME_NAME_CAP]) {
    if(!path || path[0]!='/' || !memchr(path,0,KUI_GAMES_FILE_CAP))return false;
    const char *slash=strrchr(path,'/');size_t root_bytes=(size_t)(slash-path);
    if(root_bytes>=sizeof(files->root) || !kui_destination_name_valid(slash+1) ||
       strlen(slash+1)>=KUI_GAME_NAME_CAP || !kui_game_image_name_supported(slash+1))return false;
    char root[KUI_DEST_ROOT_CAP];
    if(root_bytes) {memcpy(root,path,root_bytes);root[root_bytes]=0;}else strcpy(root,"/");
    if(!kui_destination_normalize(files->root,root) || strcmp(root,files->root))return false;
    strcpy(name,slash+1);return true;
}
static bool close_file(struct files *files) {
    if(!files->opened)return true;
    files->opened=false;files->name[0]=0;return f_close(&files->file)==FR_OK;
}
static enum kui_game_result stat_file(void *context,const char *name,uint64_t *bytes) {
    struct files *files=context;char path[KUI_GAMES_FILE_CAP+3];FILINFO info;
    if(stopped(files))return KUI_GAME_CANCELLED;
    if(!bytes || !join(files,name,path))return KUI_GAME_INVALID;
    FRESULT result=f_stat(path,&info);
    if(result!=FR_OK)return result==FR_NO_FILE || result==FR_NO_PATH?KUI_GAME_NOT_FOUND:KUI_GAME_IO;
    if(info.fattrib&AM_DIR)return KUI_GAME_FILE_SIZE;
    *bytes=info.fsize;return KUI_GAME_OK;
}
static enum kui_game_result read_file(void *context,const char *name,uint64_t offset,void *out,size_t bytes) {
    struct files *files=context;char path[KUI_GAMES_FILE_CAP+3];
    if(stopped(files))return KUI_GAME_CANCELLED;
    if(!out || bytes>UINT_MAX || (uint64_t)(FSIZE_t)offset!=offset || !join(files,name,path))return KUI_GAME_INVALID;
    if(!files->opened || strcmp(name,files->name)) {
        if(!close_file(files) || f_open(&files->file,path,FA_READ)!=FR_OK)return KUI_GAME_IO;
        files->opened=true;strcpy(files->name,name);
    }
    bool ok=offset<=f_size(&files->file) && bytes<=f_size(&files->file)-offset;
    if(ok && f_tell(&files->file)!=(FSIZE_t)offset)
        ok=f_lseek(&files->file,(FSIZE_t)offset)==FR_OK && f_tell(&files->file)==offset;
    UINT got=0;
    if(ok)ok=f_read(&files->file,out,(UINT)bytes,&got)==FR_OK && got==bytes;
    return stopped(files)?KUI_GAME_CANCELLED:ok?KUI_GAME_OK:KUI_GAME_IO;
}
static enum kui_game_metadata_io_result metadata_read(void *context,uint32_t lba,uint8_t out[2048]) {
    struct workspace *work=context;
    enum kui_game_result result=kui_game_image_read(&work->image,lba,1,KUI_GAME_SECTOR_MODE1,out,2048);
    return result==KUI_GAME_OK?KUI_GAME_METADATA_IO_OK:result==KUI_GAME_CANCELLED?
        KUI_GAME_METADATA_IO_CANCELLED:KUI_GAME_METADATA_IO_ERROR;
}
static bool metadata_range(void *context,uint32_t lba,uint32_t count) {
    return kui_game_image_check(&((struct workspace *)context)->image,lba,count,KUI_GAME_SECTOR_MODE1)==KUI_GAME_OK;
}
static void hash_word(struct kui_sha256 *hash,uint32_t value) {
    uint8_t bytes[4];for(unsigned i=0;i<4;i++)bytes[i]=(uint8_t)(value>>(8*i));kui_sha256_update(hash,bytes,sizeof(bytes));
}
static unsigned disc_number(const uint8_t ip[2048]) {
    if((memcmp(ip+37,"GD-ROM",6) && memcmp(ip+37,"CD-ROM",6)) ||
       ip[43]<'1' || ip[43]>'9' || ip[44]!='/' || ip[45]<ip[43] || ip[45]>'9' ||
       ip[46]!=' ' || ip[47]!=' ')return 0;
    return (unsigned)(ip[43]-'0');
}
enum kui_games_group_read_result kui_games_identity_group(const char *path,
    struct kui_games_group_evidence *out,kui_cancel_fn cancel) {
    if(!out || !kui_games_storage_active())return KUI_GAMES_GROUP_READ_IO;
    struct workspace *work=calloc(1,sizeof(*work));if(!work)return KUI_GAMES_GROUP_READ_IO;
    work->files.cancel=cancel;char name[KUI_GAME_NAME_CAP];
    struct kui_games_group_evidence evidence;
    enum kui_games_group_read_result answer=KUI_GAMES_GROUP_READ_UNSUPPORTED;
    if(!split(path,&work->files,name))goto done;
    const char *extension=strrchr(name,'.');
    if(extension && strlen(extension)==4u && (extension[1]=='z' || extension[1]=='Z') &&
       (extension[2]=='s' || extension[2]=='S') && (extension[3]=='o' || extension[3]=='O'))goto done;
    struct kui_game_file_ops files={&work->files,stat_file,read_file};
    enum kui_game_result result=kui_game_image_open_named_codec(name,&files,KUI_ZSO_LZ4,&work->image);
    if(result==KUI_GAME_OK && !work->image.compressed)
        result=kui_game_image_read(&work->image,work->image.data_lba,1,KUI_GAME_SECTOR_MODE1,work->buffer,2048);
    else if(result==KUI_GAME_OK)goto done;
    if(result==KUI_GAME_IO || result==KUI_GAME_NOT_FOUND)answer=KUI_GAMES_GROUP_READ_IO;
    else if(result==KUI_GAME_CANCELLED)answer=KUI_GAMES_GROUP_READ_CANCELLED;
    else if(result==KUI_GAME_OK && kui_games_group_evidence_read(work->buffer,&evidence))answer=KUI_GAMES_GROUP_READ_OK;
done:
    if(!close_file(&work->files))answer=KUI_GAMES_GROUP_READ_IO;
    if(stopped(&work->files))answer=KUI_GAMES_GROUP_READ_CANCELLED;
    if(answer==KUI_GAMES_GROUP_READ_OK)*out=evidence;
    free(work);return answer;
}
bool kui_games_identity_verify(const char *path,enum kui_zso_codec codec,
    struct kui_games_identity *out,kui_log_fn log,kui_cancel_fn cancel) {
    if(!out)return false;
    struct workspace *work=calloc(1,sizeof(*work));if(!work)return false;
    work->files.cancel=cancel;char name[KUI_GAME_NAME_CAP];struct kui_games_identity identity={0};
    bool opened=false,ok=false;const char *problem="invalid path or cancelled";
    if(!split(path,&work->files,name) || stopped(&work->files))goto done;
    opened=kui_games_storage_open(log?log:quiet_log)==KUI_GAMES_STORAGE_OK;
    if(!opened) {problem="storage unavailable";goto done;}
    struct kui_game_file_ops files={&work->files,stat_file,read_file};
    enum kui_game_result result=kui_game_image_open_named_codec(name,&files,codec,&work->image);
    if(result!=KUI_GAME_OK) {problem=kui_game_result_name(result);goto done;}
    if(work->image.compressed) {problem="compressed image identity is not implemented";goto done;}
    struct kui_game_metadata_ops metadata_ops={work,metadata_read,metadata_range};
    enum kui_game_metadata_status metadata=kui_game_metadata_read(&metadata_ops,work->image.data_lba,&work->metadata);
    if(metadata!=KUI_GAME_METADATA_OK) {problem=kui_game_metadata_status_text(metadata);goto done;}
    result=kui_game_image_read(&work->image,work->image.data_lba,1,KUI_GAME_SECTOR_MODE1,work->buffer,2048);
    if(result!=KUI_GAME_OK) {problem=kui_game_result_name(result);goto done;}
    memcpy(identity.product,work->metadata.product,sizeof(identity.product));
    memcpy(identity.version,work->metadata.version,sizeof(identity.version));
    memcpy(identity.region,work->metadata.region,sizeof(identity.region));
    memcpy(identity.title,work->metadata.title,sizeof(identity.title));identity.disc=disc_number(work->buffer);
    kui_sha256_init(&work->hash);static const char domain[]="K-UI represented image identity v1";
    kui_sha256_update(&work->hash,domain,sizeof(domain));hash_word(&work->hash,work->image.count);
    hash_word(&work->hash,work->image.data_lba);
    for(unsigned i=0;i<work->image.count;i++) {
        const struct kui_game_image_track *track=&work->image.tracks[i];
        if(track->control==4u)identity.representation|=track->sector_bytes==2048u?1u:2u;
        hash_word(&work->hash,track->number);hash_word(&work->hash,track->start_lba);hash_word(&work->hash,track->end_lba);
        hash_word(&work->hash,track->control);hash_word(&work->hash,track->sector_mode);
        unsigned bytes=track->control==4u?2048u:2352u;
        enum kui_game_sector_format format=track->control==4u?KUI_GAME_SECTOR_MODE1:KUI_GAME_SECTOR_RAW;
        if(log)log("Library identity: track %u/%u, %lu sectors",i+1u,work->image.count,
            (unsigned long)(track->end_lba-track->start_lba));
        for(uint32_t lba=track->start_lba;lba<track->end_lba;) {
            if(stopped(&work->files)) {problem="cancelled";goto done;}
            uint32_t count=IDENTITY_CHUNK_BYTES/bytes;
            if(count>track->end_lba-lba)count=track->end_lba-lba;
            result=kui_game_image_read(&work->image,lba,count,format,work->buffer,sizeof(work->buffer));
            if(result!=KUI_GAME_OK) {problem=kui_game_result_name(result);goto done;}
            size_t copied=(size_t)count*bytes;kui_sha256_update(&work->hash,work->buffer,copied);
            if(UINT64_MAX-identity.bytes<copied) {problem="represented byte count overflow";goto done;}
            identity.bytes+=copied;lba+=count;
        }
    }
    kui_sha256_digest(&work->hash,identity.digest);ok=true;
done:
    if(!close_file(&work->files)) {ok=false;problem="cannot close image file";}
    if(opened && !kui_games_storage_done(ok)) {ok=false;problem="cannot release storage";}
    if(stopped(&work->files)) {ok=false;problem="cancelled";}
    if(ok) {*out=identity;if(log)log("Library identity verified: %llu represented bytes",(unsigned long long)identity.bytes);}
    else if(log)log("Library identity not verified: %s",problem);
    free(work);return ok;
}
