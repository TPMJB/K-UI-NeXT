/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/games.h"
#include "kui/game_image.h"
#include "kui/game_metadata.h"
#include "kui/retail_image.h"
#include "kui/retail_loader_layout.h"
#include "platform.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool stopped(kui_cancel_fn cancel) { return cancel && cancel(); }
static bool gdi_name(const char *s) {
    size_t n=strlen(s);
    return n>4 && s[n-4]=='.' && (s[n-3]=='g'||s[n-3]=='G') &&
        (s[n-2]=='d'||s[n-2]=='D') && (s[n-1]=='i'||s[n-1]=='I');
}
static bool file_join(char out[KUI_GAMES_FILE_CAP],const char *root,const char *name) {
    if(!kui_destination_name_valid(name)) return false;
    int n=snprintf(out,KUI_GAMES_FILE_CAP,"%s%s%s",root,!strcmp(root,"/")?"":"/",name);
    return n>0 && n<(int)KUI_GAMES_FILE_CAP;
}
static bool split_file(const char *path,char root[KUI_DEST_ROOT_CAP],char name[KUI_DEST_NAME_CAP]) {
    if(!path || strlen(path)>=KUI_GAMES_FILE_CAP || !gdi_name(path)) return false;
    const char *slash=strrchr(path,'/');
    if(!slash || !slash[1] || strlen(slash+1)>=KUI_DEST_NAME_CAP) return false;
    size_t size=(size_t)(slash-path);
    if(size>=KUI_DEST_ROOT_CAP) return false;
    char parent[KUI_DEST_ROOT_CAP];
    if(!size) strcpy(parent,"/"); else {memcpy(parent,path,size);parent[size]=0;}
    if(!kui_destination_normalize(root,parent) || strcmp(root,parent)) return false;
    strcpy(name,slash+1);
    char joined[KUI_GAMES_FILE_CAP];
    return file_join(joined,root,name) && !strcmp(joined,path);
}
/* Resolve only an unambiguous immediate GDI. A folder with too many entries
 * remains browsable; reaching the budget never establishes uniqueness. */
static bool single_gdi(const char *root,char selected[KUI_GAMES_FILE_CAP],kui_cancel_fn cancel) {
    DIR dir;char path[KUI_GAMES_FILE_CAP+3];
    snprintf(path,sizeof(path),"0:%s",root);
    if(f_opendir(&dir,path)!=FR_OK) return false;
    unsigned matches=0;bool complete=false,ok=true;
    for(unsigned i=0;i<512;i++) {
        if(stopped(cancel)) {ok=false;break;}
        FILINFO info;FRESULT r=f_readdir(&dir,&info);
        if(r!=FR_OK) {ok=false;break;}
        if(!info.fname[0]) {complete=true;break;}
        if(info.fname[0]=='.' || (info.fattrib&(AM_DIR|AM_HID|AM_SYS)) || !gdi_name(info.fname)) continue;
        if(++matches>1) break;
        if(!file_join(selected,root,info.fname)) {ok=false;break;}
    }
    if(f_closedir(&dir)!=FR_OK) ok=false;
    return ok && complete && matches==1;
}
bool kui_games_list(const char *root,unsigned offset,struct kui_games_page *out,
    kui_log_fn log,kui_cancel_fn cancel) {
    if(!out) return false;
    memset(out,0,sizeof(*out));
    if(!kui_destination_normalize(out->root,root) || offset>8192u) {
        snprintf(out->message,sizeof(out->message),"Invalid Games folder/page");return false;
    }
    if(stopped(cancel)) {snprintf(out->message,sizeof(out->message),"Games browse stopped");return false;}
    if(!kui_sd_connect()) {snprintf(out->message,sizeof(out->message),"SD card unavailable");return false;}
    FATFS fs;DIR dir;bool opened=false,ok=false;
    const char *problem="Cannot mount SD card";
    char path[KUI_DEST_ROOT_CAP+3];snprintf(path,sizeof(path),"0:%s",out->root);
    if(!kui_mount(&fs,log)) goto done;
    FRESULT r=f_opendir(&dir,path);
    if(r!=FR_OK) {
        problem=(r==FR_NO_PATH || r==FR_NO_FILE) && !strcmp(out->root,"/Games")?
            "No /Games folder. Rip a game or use Advanced > Browse SD.":"Cannot open Games folder";
        goto done;
    }
    opened=true;
    for(unsigned scanned=0;;scanned++) {
        if(stopped(cancel)) {problem="Games browse stopped";goto done;}
        if(scanned==32768u) {problem="Directory too large to browse";goto done;}
        FILINFO info;r=f_readdir(&dir,&info);
        if(r!=FR_OK) {problem="Cannot read Games folder";goto done;}
        if(!info.fname[0]) break;
        if(info.fname[0]=='.' || (info.fattrib&(AM_HID|AM_SYS))) continue;
        bool directory=(info.fattrib&AM_DIR)!=0;
        if(!directory && !gdi_name(info.fname)) continue;
        if(offset) {--offset;continue;}
        if(out->count==KUI_GAMES_ROWS) {out->has_more=true;break;}
        struct kui_games_entry *entry=&out->entries[out->count++];
        entry->directory=directory;
        if(strlen(info.fname)>=sizeof(entry->name)) {
            snprintf(entry->name,sizeof(entry->name),"[Name too long]");entry->disabled=true;continue;
        }
        strcpy(entry->name,info.fname);
        if(directory) {
            char child[KUI_DEST_ROOT_CAP];
            entry->disabled=!kui_destination_join(child,out->root,entry->name);
            if(!entry->disabled) {
                if(single_gdi(child,entry->path,cancel)) entry->directory=false;
                else strcpy(entry->path,child);
            }
        } else entry->disabled=!file_join(entry->path,out->root,entry->name);
    }
    ok=true;
done:
    if(stopped(cancel)) {ok=false;problem="Games browse stopped";}
    if(opened && f_closedir(&dir)!=FR_OK) {ok=false;problem="Cannot close Games folder";}
    if(f_mount(NULL,"0:",0)!=FR_OK) {ok=false;problem="Cannot release SD filesystem";}
    kui_sd_disconnect();
    snprintf(out->message,sizeof(out->message),"%s",ok?
        "Select a GDI to inspect and launch.":problem);
    if(!ok) {out->count=0;out->has_more=false;if(log) log("Games browse: %s",problem);}
    return ok;
}

struct image_files {
    char root[KUI_DEST_ROOT_CAP];
    kui_cancel_fn cancel;
    kui_log_fn log;
    enum kui_game_result last;
    uint64_t read_bytes;
};
static enum kui_game_result fs_problem(struct image_files *files,FRESULT result,const char *name) {
    enum kui_game_result value=(result==FR_NO_FILE || result==FR_NO_PATH)?KUI_GAME_NOT_FOUND:KUI_GAME_IO;
    if(files->log) files->log("Games file %.120s: FatFs error %u",name,(unsigned)result);
    files->last=value;return value;
}
static bool fs_path(struct image_files *files,const char *name,char out[KUI_GAMES_FILE_CAP+3]) {
    char joined[KUI_GAMES_FILE_CAP];
    if(!file_join(joined,files->root,name)) return false;
    snprintf(out,KUI_GAMES_FILE_CAP+3,"0:%s",joined);return true;
}
static enum kui_game_result image_stat(void *ctx,const char *name,uint64_t *bytes) {
    struct image_files *files=ctx;
    if(stopped(files->cancel)) return files->last=KUI_GAME_CANCELLED;
    char path[KUI_GAMES_FILE_CAP+3];
    if(!fs_path(files,name,path)) return files->last=KUI_GAME_INVALID;
    FILINFO info;FRESULT r=f_stat(path,&info);
    if(r!=FR_OK) return fs_problem(files,r,name);
    if(info.fattrib&AM_DIR) return files->last=KUI_GAME_FILE_SIZE;
    *bytes=(uint64_t)info.fsize;return files->last=KUI_GAME_OK;
}
static enum kui_game_result image_read(void *ctx,const char *name,uint64_t offset,void *out,size_t size) {
    struct image_files *files=ctx;
    if(stopped(files->cancel)) return files->last=KUI_GAME_CANCELLED;
    char path[KUI_GAMES_FILE_CAP+3];
    if(!fs_path(files,name,path) || size>UINT_MAX || (uint64_t)(FSIZE_t)offset!=offset)
        return files->last=KUI_GAME_INVALID;
    FIL file;FRESULT r=f_open(&file,path,FA_READ);
    if(r!=FR_OK) return fs_problem(files,r,name);
    enum kui_game_result result=KUI_GAME_OK;
    if(offset>(uint64_t)f_size(&file) || (uint64_t)size>(uint64_t)f_size(&file)-offset) result=KUI_GAME_FILE_SIZE;
    if(result==KUI_GAME_OK) {
        r=f_lseek(&file,(FSIZE_t)offset);
        if(r!=FR_OK) result=fs_problem(files,r,name);
        else if((uint64_t)f_tell(&file)!=offset) result=KUI_GAME_IO;
    }
    if(result==KUI_GAME_OK) {
        UINT got=0;r=f_read(&file,out,(UINT)size,&got);
        files->read_bytes+=got;
        if(r!=FR_OK) result=fs_problem(files,r,name);
        else if(got!=size) result=KUI_GAME_IO;
    }
    r=f_close(&file);
    if(r!=FR_OK) result=fs_problem(files,r,name);
    if(stopped(files->cancel)) result=KUI_GAME_CANCELLED;
    return files->last=result;
}
struct metadata_reader {const struct kui_game_image *image;struct image_files *files;};
static enum kui_game_metadata_io_result metadata_sector(void *ctx,uint32_t lba,uint8_t out[2048]) {
    struct metadata_reader *reader=ctx;
    enum kui_game_result r=kui_game_image_read(reader->image,lba,1,KUI_GAME_SECTOR_MODE1,out,2048);
    reader->files->last=r;
    return r==KUI_GAME_OK?KUI_GAME_METADATA_IO_OK:
        r==KUI_GAME_CANCELLED?KUI_GAME_METADATA_IO_CANCELLED:KUI_GAME_METADATA_IO_ERROR;
}
static bool metadata_range(void *ctx,uint32_t lba,uint32_t count) {
    struct metadata_reader *reader=ctx;
    return kui_game_image_check(reader->image,lba,count,KUI_GAME_SECTOR_MODE1)==KUI_GAME_OK;
}
bool kui_games_inspect(const char *path,struct kui_games_detail *out,kui_log_fn log,kui_cancel_fn cancel) {
    if(!out) return false;
    memset(out,0,sizeof(*out));
    struct image_files files={.cancel=cancel,.log=log};
    char descriptor[KUI_DEST_NAME_CAP];
    if(!split_file(path,files.root,descriptor)) {
        snprintf(out->message,sizeof(out->message),"Invalid GDI path");return false;
    }
    strcpy(out->path,path);
    if(stopped(cancel)) {out->stopped=true;snprintf(out->message,sizeof(out->message),"Games inspection stopped");return false;}
    if(!kui_sd_connect()) {snprintf(out->message,sizeof(out->message),"SD card unavailable");return false;}
    FATFS fs;struct kui_game_image *image=NULL;uint8_t *gdi=NULL;
    const char *problem="Cannot mount SD card";
    if(!kui_mount(&fs,log)) goto done;
    uint64_t size=0;
    enum kui_game_result result=image_stat(&files,descriptor,&size);
    if(result!=KUI_GAME_OK) {problem=kui_game_result_name(result);goto done;}
    if(!size || size>KUI_GAME_GDI_LIMIT) {problem="GDI descriptor is empty or too large";goto done;}
    image=malloc(sizeof(*image));gdi=malloc((size_t)size);
    if(!image || !gdi) {problem="Insufficient memory for Games inspection";goto done;}
    result=image_read(&files,descriptor,0,gdi,(size_t)size);
    if(result!=KUI_GAME_OK) {problem=kui_game_result_name(result);goto done;}
    struct kui_game_file_ops ops={&files,image_stat,image_read};
    result=kui_game_image_open(gdi,(size_t)size,&ops,image);
    if(result!=KUI_GAME_OK) {
        problem=result==KUI_GAME_UNSUPPORTED?
            "Raw 2352-byte GDI tracks with zero file offsets required":kui_game_result_name(result);
        goto done;
    }
    uint32_t session=0;
    out->tracks=image->count;
    for(unsigned i=0;i<image->count;i++) {
        const struct kui_game_image_track *t=&image->tracks[i];
        out->bytes+=t->file_bytes;
        if(t->control==4) {
            ++out->data_tracks;
            if(!session && t->start_lba>=45000u) session=t->start_lba;
        } else {
            ++out->audio_tracks;
            if(t->start_lba>=45000u) out->high_density_audio=true;
        }
    }
    if(!session) {problem="No high-density data track; raw GD-ROM GDI required";goto done;}
    struct metadata_reader reader={image,&files};
    struct kui_game_metadata_ops metadata_ops={&reader,metadata_sector,metadata_range};
    struct kui_game_metadata metadata;
    enum kui_game_metadata_status status=kui_game_metadata_read(&metadata_ops,session,&metadata);
    if(metadata.ip_valid) {
        snprintf(out->title,sizeof(out->title),"%s",metadata.title);
        snprintf(out->product,sizeof(out->product),"%s",metadata.product);
        snprintf(out->region,sizeof(out->region),"%s",metadata.region);
        snprintf(out->boot_file,sizeof(out->boot_file),"%s",metadata.bootfile);
        out->native_gd=metadata.native_gd;
        out->windows_ce=metadata.windows_ce;
    }
    if(status!=KUI_GAME_METADATA_OK) {
        problem=status==KUI_GAME_METADATA_IO?kui_game_result_name(files.last):kui_game_metadata_status_text(status);
        goto done;
    }
    out->boot_bytes=metadata.boot_bytes;out->boot_lba=metadata.boot_lba;
    if(metadata.boot_lba<session+16u ||
        kui_game_image_check(image,session,16,KUI_GAME_SECTOR_MODE1)!=KUI_GAME_OK) {
        problem="Boot executable and full IP must be in high-density data tracks";goto done;
    }
    out->valid=true;
    problem=out->windows_ce?"Image inspected; Windows CE launching is not supported":
        !out->native_gd?"Image inspected; native GD-ROM with valid IP flags required":
        out->tracks>KUI_RETAIL_IMAGE_TRACKS?"Image inspected; launch map supports at most 16 tracks":
        out->boot_bytes<KUI_RETAIL_TRAMPOLINE_BYTES || out->boot_bytes>KUI_RETAIL_EXEC_MAX_BYTES?
            "Image inspected; boot executable must be 128 bytes to 12 MiB":
        out->high_density_audio?"Image inspected; CD audio unsupported, audio requests may stop the game":
        "Image inspected; native game launch available, compatibility varies";
done:
    if(stopped(cancel)) {out->valid=false;out->stopped=true;problem="Games inspection stopped";}
    if(f_mount(NULL,"0:",0)!=FR_OK) {out->valid=false;problem="Cannot release SD filesystem";}
    kui_sd_disconnect();free(gdi);free(image);
    snprintf(out->message,sizeof(out->message),"%s",problem);
    if(log) {
        log("Games inspect: %s",out->path);
        log("Games result: %s",out->message);
        if(out->title[0]) log("Games title: %s; product=%s region=%s",out->title,out->product,out->region);
        if(out->valid) {
            log("Games tracks=%u data=%u audio=%u bytes=%llu",out->tracks,out->data_tracks,
                out->audio_tracks,(unsigned long long)out->bytes);
            log("Games boot: %s LBA=%lu bytes=%lu",out->boot_file,
                (unsigned long)out->boot_lba,(unsigned long)out->boot_bytes);
        }
        log("Games metadata bytes read=%llu; no complete-file verification or launch",
            (unsigned long long)files.read_bytes);
    }
    return out->valid;
}
