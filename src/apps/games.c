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
#define VARIANT_LIMIT 512u
#define LIST_CACHE_LIMIT 1024u
struct cached_entry {
    char name[KUI_DEST_NAME_CAP],gdi[KUI_DEST_NAME_CAP];
    char cooked[KUI_DEST_NAME_CAP],cooked_gdi[KUI_DEST_NAME_CAP];
    bool folder,resolved,disabled;
};
static struct {
    struct cached_entry *entries;
    unsigned count;
    bool valid;
    char root[KUI_DEST_ROOT_CAP],diagnostic_root[KUI_DEST_ROOT_CAP];
} listing_cache;
void kui_games_cache_clear(void) {
    free(listing_cache.entries);memset(&listing_cache,0,sizeof(listing_cache));
}
static bool same_component(const char *a,const char *b) {
    while(*a && *b) {
        unsigned x=(unsigned char)*a++,y=(unsigned char)*b++;
        if(x>='A' && x<='Z') x+='a'-'A';
        if(y>='A' && y<='Z') y+='a'-'A';
        if(x!=y) return false;
    }
    return *a==*b;
}
struct variant {
    char original[KUI_DEST_NAME_CAP],cooked[KUI_DEST_NAME_CAP];
    char original_gdi[KUI_DEST_NAME_CAP],cooked_gdi[KUI_DEST_NAME_CAP];
    bool paired;
};
static bool cooked_folder(const char *name) {
    size_t size=strlen(name);
    return size>5u && size<KUI_DEST_NAME_CAP && !strcmp(name+size-5u,"-2048") &&
        !(size>10u && !strcmp(name+size-10u,"-2048-2048"));
}
static bool variant_pair(const char *root,struct variant *pair,kui_cancel_fn cancel,const char **reason,bool *retry);
/* One bounded directory snapshot avoids probing a sibling for every ordinary
 * folder. Only suffix candidates allocate memory or need descriptor checks. */
static bool index_variants(DIR *dir,const char *root,struct variant **out,unsigned *count,kui_cancel_fn cancel,kui_log_fn log,bool *retry) {
    struct variant *pairs=NULL;unsigned used=0;bool complete=false;
    for(unsigned scanned=0;scanned<32768u;scanned++) {
        if(stopped(cancel)) {free(pairs);return false;}
        FILINFO info;
        if(f_readdir(dir,&info)!=FR_OK) {*retry=true;break;}
        if(!info.fname[0]) {complete=true;break;}
        if(!(info.fattrib&AM_DIR) || (info.fattrib&(AM_HID|AM_SYS)) ||
           info.fname[0]=='.' || !cooked_folder(info.fname)) continue;
        if(used==VARIANT_LIMIT) break;
        if(!pairs) {pairs=calloc(VARIANT_LIMIT,sizeof(*pairs));if(!pairs) {*retry=true;break;}}
        strcpy(pairs[used++].cooked,info.fname);
    }
    /* Incomplete discovery, overflow or allocation failure leaves every row
     * standalone. No partial index may hide an otherwise accessible image. */
    if(!complete) {free(pairs);pairs=NULL;used=0;}
    if(f_readdir(dir,NULL)!=FR_OK) {free(pairs);return false;}
    /* Bind to an actual visible root row before hiding a copy. A path lookup
     * can accept an SFN/Unicode alias that our component matcher cannot attach
     * to a listing row; such copies must remain independently accessible. */
    if(used) {
        bool bound=false;
        for(unsigned scanned=0;scanned<32768u;scanned++) {
            if(stopped(cancel)) {free(pairs);return false;}
            FILINFO info;
            if(f_readdir(dir,&info)!=FR_OK) {*retry=true;break;}
            if(!info.fname[0]) {bound=true;break;}
            if(!(info.fattrib&AM_DIR) || (info.fattrib&(AM_HID|AM_SYS)) || info.fname[0]=='.') continue;
            for(unsigned i=0;i<used;i++) {
                char base[KUI_DEST_NAME_CAP];size_t size=strlen(pairs[i].cooked)-5u;
                memcpy(base,pairs[i].cooked,size);base[size]=0;
                if(strlen(info.fname)<sizeof(pairs[i].original) && same_component(info.fname,base))
                    strcpy(pairs[i].original,info.fname);
            }
        }
        if(!bound) {free(pairs);pairs=NULL;used=0;complete=false;}
        if(f_readdir(dir,NULL)!=FR_OK) {free(pairs);return false;}
    }
    unsigned paired=0,reported=0;
    for(unsigned i=0;i<used;i++) {
        const char *reason="layout check failed";
        pairs[i].paired=variant_pair(root,&pairs[i],cancel,&reason,retry);
        if(pairs[i].paired) ++paired;
        else if(log && reported++<8u) log("Games variant %.72s: %s",pairs[i].cooked,reason);
        if(stopped(cancel)) {free(pairs);return false;}
    }
    if(log) log("Games variants: %u candidates, %u paired%s",used,paired,
        complete?"":"; discovery limit/error, standalone entries retained");
    *out=pairs;*count=used;return true;
}
/* Resolve only an unambiguous immediate GDI. A folder with too many entries
 * remains browsable; reaching the budget never establishes uniqueness. */
enum single_result {SINGLE_FOUND,SINGLE_NONE,SINGLE_AMBIGUOUS,SINGLE_FAILED};
static enum single_result single_gdi(const char *root,char selected[KUI_GAMES_FILE_CAP],kui_cancel_fn cancel) {
    DIR dir;char path[KUI_GAMES_FILE_CAP+3];
    snprintf(path,sizeof(path),"0:%s",root);
    if(f_opendir(&dir,path)!=FR_OK) return SINGLE_FAILED;
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
    if(!ok) return SINGLE_FAILED;
    if(matches>1) return SINGLE_AMBIGUOUS;
    if(!complete) return SINGLE_FAILED;
    return matches?SINGLE_FOUND:SINGLE_NONE;
}
bool kui_games_single_gdi(const char *root,char selected[KUI_GAMES_FILE_CAP],kui_cancel_fn cancel) {
    return single_gdi(root,selected,cancel)==SINGLE_FOUND;
}
static bool cached_page_needs_io(unsigned offset) {
    for(unsigned i=offset;i<listing_cache.count && i-offset<KUI_GAMES_ROWS;i++) {
        const struct cached_entry *e=&listing_cache.entries[i];
        if(e->folder && !e->resolved && !e->disabled) return true;
    }
    return false;
}
static bool cached_page(unsigned offset,struct kui_games_page *out,kui_cancel_fn cancel) {
    out->total=listing_cache.count;
    for(unsigned i=offset;i<listing_cache.count && i-offset<KUI_GAMES_ROWS;i++) {
        if(stopped(cancel)) return false;
        struct cached_entry *saved=&listing_cache.entries[i];
        struct kui_games_entry *entry=&out->entries[out->count++];
        strcpy(entry->name,saved->name);entry->disabled=saved->disabled;
        entry->directory=saved->folder;
        if(entry->disabled) continue;
        char child[KUI_DEST_ROOT_CAP];
        if(saved->folder) {
            if(!kui_destination_join(child,out->root,saved->name)) {
                saved->disabled=entry->disabled=true;continue;
            }
            if(!saved->resolved) {
                char selected[KUI_GAMES_FILE_CAP];
                enum single_result result=single_gdi(child,selected,cancel);
                if(stopped(cancel)) return false;
                if(result==SINGLE_FOUND) strcpy(saved->gdi,strrchr(selected,'/')+1);
                /* A transient IO/budget failure is still a browsable folder,
                 * but must not become a permanent cached absence of a GDI. */
                saved->resolved=result!=SINGLE_FAILED;
            }
            if(saved->gdi[0]) {
                entry->directory=false;entry->disabled=!file_join(entry->path,child,saved->gdi);
            } else strcpy(entry->path,child);
            if(saved->cooked[0]) entry->disabled=entry->disabled ||
                !kui_destination_join(child,out->root,saved->cooked) ||
                !file_join(entry->variant_2048_path,child,saved->cooked_gdi);
        } else entry->disabled=!file_join(entry->path,out->root,saved->name);
    }
    out->has_more=offset<listing_cache.count && listing_cache.count-offset>out->count;
    return !stopped(cancel);
}
bool kui_games_list(const char *root,unsigned offset,struct kui_games_page *out,
    kui_log_fn log,kui_cancel_fn cancel) {
    return kui_games_list_with(root,offset,out,NULL,NULL,log,cancel);
}
bool kui_games_list_with(const char *root,unsigned offset,struct kui_games_page *out,
    kui_games_mounted_fn mounted,void *ctx,kui_log_fn log,kui_cancel_fn cancel) {
    if(!out) return false;
    memset(out,0,sizeof(*out));
    if(!kui_destination_normalize(out->root,root) || offset>8192u) {
        snprintf(out->message,sizeof(out->message),"Invalid Games folder/page");return false;
    }
    if(stopped(cancel)) {snprintf(out->message,sizeof(out->message),"Games browse stopped");return false;}
    if(listing_cache.valid && strcmp(listing_cache.root,out->root)) kui_games_cache_clear();
    bool cached=listing_cache.valid;
    if(cached && !mounted && !cached_page_needs_io(offset)) {
        bool ok=cached_page(offset,out,cancel);
        snprintf(out->message,sizeof(out->message),"%s",ok?"Select a GDI to inspect and launch.":"Games browse stopped");
        if(!ok) {out->count=0;out->has_more=false;}
        return ok;
    }
    if(!kui_sd_connect()) {kui_games_cache_clear();snprintf(out->message,sizeof(out->message),"SD card unavailable");return false;}
    FATFS fs;DIR dir;bool opened=false,ok=false;
    struct variant *pairs=NULL;unsigned pair_count=0;
    struct cached_entry *draft=NULL;bool draft_enabled=true,retry=false;
    bool counting=false,counted=true;unsigned listed=0;
    const char *problem="Cannot mount SD card";
    char path[KUI_DEST_ROOT_CAP+3];snprintf(path,sizeof(path),"0:%s",out->root);
    if(!kui_mount(&fs,log)) goto done;
    if(cached) {
        if(!cached_page(offset,out,cancel)) {problem="Games browse stopped";goto done;}
        ok=true;goto done;
    }
    FRESULT r=f_opendir(&dir,path);
    if(r!=FR_OK) {
        problem=(r==FR_NO_PATH || r==FR_NO_FILE) && !strcmp(out->root,"/Games")?
            "No /Games folder. Rip a game or use Advanced > Browse SD.":"Cannot open Games folder";
        goto done;
    }
    opened=true;
    bool diagnose=strcmp(listing_cache.diagnostic_root,out->root)!=0;
    if(!index_variants(&dir,out->root,&pairs,&pair_count,cancel,diagnose?log:NULL,&retry)) {
        problem=stopped(cancel)?"Games browse stopped":"Cannot read Games folder";goto done;
    }
    strcpy(listing_cache.diagnostic_root,out->root);
    /* After the page is full the rest is only counted; a count that cannot
     * finish leaves the total unknown rather than failing the page. */
    for(unsigned scanned=0;;scanned++) {
        if(stopped(cancel)) {problem="Games browse stopped";goto done;}
        if(scanned==32768u) {
            if(counting) {counted=false;break;}
            problem="Directory too large to browse";goto done;
        }
        FILINFO info;r=f_readdir(&dir,&info);
        if(r!=FR_OK) {
            if(counting) {counted=false;break;}
            problem="Cannot read Games folder";goto done;
        }
        if(!info.fname[0]) break;
        if(info.fname[0]=='.' || (info.fattrib&(AM_HID|AM_SYS))) continue;
        bool directory=(info.fattrib&AM_DIR)!=0;
        if(!directory && !gdi_name(info.fname)) continue;
        const struct variant *pair=NULL;bool alternate=false;
        for(unsigned i=0;directory && i<pair_count;i++) if(pairs[i].paired) {
            if(!strcmp(info.fname,pairs[i].cooked)) {pair=&pairs[i];alternate=true;break;}
            if(same_component(info.fname,pairs[i].original)) {pair=&pairs[i];break;}
        }
        /* Check before counting or skipping a page: a converted folder may
         * precede its original in the physical directory enumeration. */
        if(pair && alternate) continue;
        ++listed;
        struct cached_entry *saved=NULL;
        if(draft_enabled) {
            if(listed>LIST_CACHE_LIMIT) {free(draft);draft=NULL;draft_enabled=false;}
            else {
                if(!draft) {draft=calloc(LIST_CACHE_LIMIT,sizeof(*draft));if(!draft) draft_enabled=false;}
                if(draft) {
                    saved=&draft[listed-1u];saved->folder=directory;
                    saved->disabled=strlen(info.fname)>=sizeof(saved->name);
                    strcpy(saved->name,saved->disabled?"[Name too long]":info.fname);
                    if(pair) {
                        saved->resolved=true;strcpy(saved->gdi,pair->original_gdi);
                        strcpy(saved->cooked,pair->cooked);strcpy(saved->cooked_gdi,pair->cooked_gdi);
                    }
                }
            }
        }
        if(offset) {--offset;continue;}
        if(counting) continue;
        if(out->count==KUI_GAMES_ROWS) {out->has_more=true;counting=true;continue;}
        struct kui_games_entry *entry=&out->entries[out->count++];
        bool resolved=pair!=NULL || !directory;
        entry->directory=directory;
        if(strlen(info.fname)>=sizeof(entry->name)) {
            snprintf(entry->name,sizeof(entry->name),"[Name too long]");entry->disabled=true;continue;
        }
        strcpy(entry->name,info.fname);
        if(pair) {
            char child[KUI_DEST_ROOT_CAP];
            entry->directory=false;
            entry->disabled=!kui_destination_join(child,out->root,entry->name) ||
                !file_join(entry->path,child,pair->original_gdi) ||
                !kui_destination_join(child,out->root,pair->cooked) ||
                !file_join(entry->variant_2048_path,child,pair->cooked_gdi);
        } else if(directory) {
            char child[KUI_DEST_ROOT_CAP];
            entry->disabled=!kui_destination_join(child,out->root,entry->name);
            if(!entry->disabled) {
                enum single_result result=single_gdi(child,entry->path,cancel);
                if(stopped(cancel)) {problem="Games browse stopped";goto done;}
                if(result==SINGLE_FOUND) entry->directory=false;
                else strcpy(entry->path,child);
                resolved=result!=SINGLE_FAILED;
            }
        } else entry->disabled=!file_join(entry->path,out->root,entry->name);
        if(saved) {
            saved->disabled=entry->disabled;saved->resolved=resolved || entry->disabled;
            if(directory && !entry->directory && !entry->disabled) strcpy(saved->gdi,strrchr(entry->path,'/')+1);
        }
    }
    out->total=counted?listed:0;
    ok=true;
done:
    free(pairs);
    if(stopped(cancel)) {ok=false;problem="Games browse stopped";}
    if(opened && f_closedir(&dir)!=FR_OK) {ok=false;problem="Cannot close Games folder";}
    if(ok && mounted) mounted(ctx,log,cancel);
    if(stopped(cancel)) {ok=false;problem="Games browse stopped";}
    if(f_mount(NULL,"0:",0)!=FR_OK) {ok=false;problem="Cannot release SD filesystem";}
    kui_sd_disconnect();
    if(ok && !cached && counted && draft_enabled && !retry) {
        listing_cache.entries=draft;draft=NULL;listing_cache.count=listed;
        strcpy(listing_cache.root,out->root);listing_cache.valid=true;
    }
    free(draft);
    if(!ok && !stopped(cancel)) kui_games_cache_clear();
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
static enum kui_game_result read_layout(const char *path,struct kui_game_image *image,struct image_files *files,bool *retry) {
    char name[KUI_DEST_NAME_CAP];uint64_t bytes=0;
    if(!split_file(path,files->root,name)) return KUI_GAME_INVALID;
    enum kui_game_result result=image_stat(files,name,&bytes);
    if(result!=KUI_GAME_OK) return result;
    if(!bytes || bytes>KUI_GAME_GDI_LIMIT) return KUI_GAME_FILE_SIZE;
    uint8_t *descriptor=malloc((size_t)bytes);
    if(!descriptor) {*retry=true;return KUI_GAME_INVALID;}
    struct kui_game_file_ops ops={files,image_stat,image_read};
    result=image_read(files,name,0,descriptor,(size_t)bytes);
    if(result==KUI_GAME_OK) result=kui_game_image_open(descriptor,(size_t)bytes,&ops,image);
    free(descriptor);return result;
}
static bool variant_pair(const char *root,struct variant *pair,kui_cancel_fn cancel,const char **reason,bool *retry) {
    if(!pair->original[0]) {*reason="matching original folder not visible";return false;}
    char original_root[KUI_DEST_ROOT_CAP],cooked_root[KUI_DEST_ROOT_CAP],physical[KUI_DEST_ROOT_CAP+3];
    if(!kui_destination_join(original_root,root,pair->original) ||
       !kui_destination_join(cooked_root,root,pair->cooked)) {*reason="folder path too long/invalid";return false;}
    /* Most directories have no sibling; do no descriptor IO in that case. */
    snprintf(physical,sizeof(physical),"0:%s",original_root);
    FILINFO info;
    if(stopped(cancel)) {*reason="cancelled";return false;}
    FRESULT stat_result=f_stat(physical,&info);
    if(stat_result!=FR_OK) {
        *reason=stat_result==FR_NO_PATH || stat_result==FR_NO_FILE?"original sibling missing":"original sibling IO failed";
        if(stat_result!=FR_NO_PATH && stat_result!=FR_NO_FILE) *retry=true;
        return false;
    }
    if(!(info.fattrib&AM_DIR) || (info.fattrib&(AM_HID|AM_SYS))) {*reason="original sibling is not a visible folder";return false;}
    /* f_stat may report an SFN or differently-cased name. The root snapshot
     * already supplied the actual original row spelling. */
    char original[KUI_GAMES_FILE_CAP],cooked[KUI_GAMES_FILE_CAP];
    enum single_result single=single_gdi(original_root,original,cancel);
    if(single!=SINGLE_FOUND) {*reason="original needs one GDI (missing/ambiguous/IO)";if(single==SINGLE_FAILED) *retry=true;return false;}
    single=single_gdi(cooked_root,cooked,cancel);
    if(single!=SINGLE_FOUND) {*reason="copy needs one GDI (missing/ambiguous/IO)";if(single==SINGLE_FAILED) *retry=true;return false;}
    struct kui_game_image *images=malloc(2u*sizeof(*images));
    if(!images) {*reason="insufficient layout memory";*retry=true;return false;}
    struct image_files files[2]={{.cancel=cancel},{.cancel=cancel}};
    enum kui_game_result result=read_layout(original,&images[0],&files[0],retry);
    if(result==KUI_GAME_OK) result=read_layout(cooked,&images[1],&files[1],retry);
    bool ok=result==KUI_GAME_OK;
    if(!ok) {*reason=kui_game_result_name(result);if(result==KUI_GAME_IO) *retry=true;}
    else if(images[0].count!=images[1].count) {ok=false;*reason="track counts differ";}
    bool data=false;
    for(unsigned i=0;ok && i<images[0].count;i++) {
        const struct kui_game_image_track *a=&images[0].tracks[i],*b=&images[1].tracks[i];
        if(a->start_lba!=b->start_lba || a->end_lba!=b->end_lba) {ok=false;*reason="track LBA/sector counts differ";}
        else if(a->control!=b->control) {ok=false;*reason="track data/audio types differ";}
        if(b->control==4) {
            data=true;
            if(b->sector_bytes!=KUI_GAME_DATA_BYTES) {ok=false;*reason="copy contains raw data tracks";}
        } else if(b->sector_bytes!=KUI_GAME_RAW_BYTES) {ok=false;*reason="copy audio is not raw2352";}
    }
    free(images);
    if(ok && data && !stopped(cancel)) {
        strcpy(pair->original_gdi,strrchr(original,'/')+1);
        strcpy(pair->cooked_gdi,strrchr(cooked,'/')+1);
        return true;
    }
    if(ok && !data) *reason="no data tracks";
    return false;
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
    return kui_games_inspect_with(path,out,NULL,NULL,log,cancel);
}
bool kui_games_inspect_with(const char *path,struct kui_games_detail *out,
    kui_games_mounted_fn mounted,void *ctx,kui_log_fn log,kui_cancel_fn cancel) {
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
    FATFS fs;struct kui_game_image *image=NULL;uint8_t *gdi=NULL;bool mounted_card=false;
    const char *problem="Cannot mount SD card";
    if(!kui_mount(&fs,log)) goto done;
    mounted_card=true;
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
            "GDI needs 2048/2352-byte data, 2352-byte audio and zero offsets":kui_game_result_name(result);
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
    if(!session) {problem="No high-density data track; GD-ROM GDI required";goto done;}
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
    problem=out->windows_ce?"Image inspected; Windows CE SCI launch test, compatibility varies":
        !out->native_gd?"Image inspected; native GD-ROM with valid IP flags required":
        out->tracks>KUI_RETAIL_IMAGE_TRACKS?"Image inspected; launch map supports at most 99 tracks":
        out->boot_bytes<KUI_RETAIL_TRAMPOLINE_BYTES || out->boot_bytes>KUI_RETAIL_EXEC_MAX_BYTES?
            "Image inspected; boot executable must be 128 bytes to 12 MiB":
        out->high_density_audio?"Image inspected; CD audio unsupported, audio requests may stop the game":
        "Image inspected; native game launch available, compatibility varies";
done:
    if(stopped(cancel)) {out->valid=false;out->stopped=true;problem="Games inspection stopped";}
    if(mounted_card && mounted && !out->stopped) mounted(ctx,log,cancel);
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
