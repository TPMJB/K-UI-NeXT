/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/destination.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static bool qualify(char out[KUI_DEST_PATH_CAP],const char *root) {
    char normalized[KUI_DEST_ROOT_CAP];
    if(!kui_destination_normalize(normalized,root)) return false;
    int n=snprintf(out,KUI_DEST_PATH_CAP,"0:%s",normalized);
    return n>=0 && n<(int)KUI_DEST_PATH_CAP;
}
bool kui_destination_mkdirs(const char *root,kui_log_fn log) {
    char path[KUI_DEST_PATH_CAP];
    if(!qualify(path,root)) {if(log) log("Invalid destination path");return false;}
    if(!strcmp(path,"0:/")) return true;
    size_t size=strlen(path);
    for(size_t at=3;at<=size;at++) {
        if(path[at] && path[at]!='/') continue;
        char saved=path[at];path[at]=0;
        FILINFO info;FRESULT r=f_stat(path,&info);
        if(r==FR_NO_FILE || r==FR_NO_PATH) r=f_mkdir(path);
        else if(r==FR_OK && !(info.fattrib&AM_DIR)) r=FR_EXIST;
        if(r!=FR_OK) {if(log) log("Cannot create destination %s: FatFs=%u",path+2,(unsigned)r);return false;}
        path[at]=saved;
    }
    return true;
}

/* Bounded lexicographic insertion is adapted from TPMJB's independently
 * authored applications/gd_ripper/modules/folders.h, introduced in
 * 3a5d0cd50ad84bde7efe74aba88e2ad04d6ccfe3 (source at
 * 2a5309298dde8fb100da1e2e4e10517695c9780f). Eight visible rows plus one
 * insertion slot; only directory metadata is scanned, never game contents.
 * Offset requests advance a name boundary through bounded metadata passes.
 * Unlike the old KOS adapter, FatFs errors are distinguished from EOF and no
 * caller's path is changed on failure. Full names remain sorting keys even
 * when the UI must show a disabled name-too-long entry. */
#define SCAN_NAME_CAP (sizeof(((FILINFO *)0)->fname))
struct names {char name[KUI_DEST_PAGE_SIZE+1][SCAN_NAME_CAP];unsigned count;bool more;};
static bool scan(DIR *directory,const char *bound,struct names *out,kui_log_fn log) {
    memset(out,0,sizeof(*out));
    FRESULT r=f_readdir(directory,NULL);
    if(r!=FR_OK) {if(log) log("Cannot rewind destination directory: FatFs=%u",(unsigned)r);return false;}
    FILINFO info;
    for(;;) {
        r=f_readdir(directory,&info);
        if(r!=FR_OK) {if(log) log("Cannot list destination directory: FatFs=%u",(unsigned)r);return false;}
        if(!info.fname[0]) break;
        if(!(info.fattrib&AM_DIR) || !strcmp(info.fname,".") || !strcmp(info.fname,"..") ||
           strcmp(info.fname,bound)<=0) continue;
        unsigned at=0;while(at<out->count && strcmp(out->name[at],info.fname)<0) ++at;
        if(at<out->count) memmove(out->name[at+1],out->name[at],(out->count-at)*SCAN_NAME_CAP);
        strcpy(out->name[at],info.fname);
        if(++out->count>KUI_DEST_PAGE_SIZE) {out->count=KUI_DEST_PAGE_SIZE;out->more=true;}
    }
    return true;
}
bool kui_destination_list(const char *root,unsigned offset,struct kui_destination_page *page,kui_log_fn log) {
    if(!page) return false;
    memset(page,0,sizeof(*page));
    char path[KUI_DEST_PATH_CAP],bound[SCAN_NAME_CAP]="";
    if(!qualify(path,root)) {if(log) log("Invalid destination path");return false;}
    DIR directory;FRESULT r=f_opendir(&directory,path);
    if(r!=FR_OK) {if(log) log("Cannot open destination %s: FatFs=%u",path+2,(unsigned)r);return false;}
    struct names names;bool ok=true;
    for(;;) {
        if(!scan(&directory,bound,&names,log)) {ok=false;break;}
        if(!offset || !names.count) break;
        unsigned skip=offset<names.count?offset:names.count;
        strcpy(bound,names.name[skip-1]);offset-=skip;
        if(skip==names.count && !names.more) {names.count=0;names.more=false;break;}
    }
    r=f_closedir(&directory);
    if(r!=FR_OK) {if(log) log("Cannot close destination directory: FatFs=%u",(unsigned)r);ok=false;}
    if(!ok) return false;
    page->count=names.count;page->has_more=names.more;
    for(unsigned i=0;i<names.count;i++) {
        struct kui_destination_entry *entry=&page->entries[i];
        if(strlen(names.name[i])>=sizeof(entry->name)) {
            strcpy(entry->name,"[Name too long]");entry->disabled=true;
        } else {
            strcpy(entry->name,names.name[i]);
            char joined[KUI_DEST_ROOT_CAP];entry->disabled=!kui_destination_join(joined,root,entry->name);
        }
    }
    return true;
}

static const char *const slots[]={KUI_DEST_PATH_A,KUI_DEST_PATH_B};
struct record {char root[KUI_DEST_ROOT_CAP];uint64_t sequence;bool valid;};
static bool read_slot(unsigned index,struct record *out,kui_log_fn log) {
    memset(out,0,sizeof(*out));
    FIL file;FRESULT r=f_open(&file,slots[index],FA_READ);
    if(r==FR_NO_FILE || r==FR_NO_PATH) return true;
    if(r!=FR_OK) {if(log) log("Cannot read %s: FatFs=%u",slots[index]+2,(unsigned)r);return false;}
    uint8_t record[KUI_DEST_RECORD_SIZE];UINT got=0;
    bool sized=f_size(&file)==sizeof(record);
    if(sized) r=f_read(&file,record,sizeof(record),&got);
    FRESULT closed=f_close(&file);
    if(r!=FR_OK || closed!=FR_OK) {
        if(log) log("Cannot read/close %s: FatFs=%u/%u",slots[index]+2,(unsigned)r,(unsigned)closed);
        return false;
    }
    if(sized && got==sizeof(record)) out->valid=kui_destination_decode(out->root,&out->sequence,record,sizeof(record));
    if(!out->valid && log) log("Ignoring invalid destination record: %s",slots[index]+2);
    return true;
}
static int latest(const struct record records[2]) {
    if(!records[0].valid) return records[1].valid?1:-1;
    if(!records[1].valid) return 0;
    return records[1].sequence>records[0].sequence?1:0;
}
bool kui_destination_load(char out[KUI_DEST_ROOT_CAP],kui_log_fn log) {
    if(!out) return false;
    kui_destination_default(out);struct record records[2];
    if(!read_slot(0,&records[0],log) || !read_slot(1,&records[1],log)) {
        if(log) log("Destination unavailable; /Games remains in memory");
        return false;
    }
    int chosen=latest(records);
    if(chosen<0) {if(log) log("No valid saved destination; using /Games");}
    else {
        strcpy(out,records[chosen].root);
        if(log) log("Loaded destination %s sequence=%" PRIu64,out,records[chosen].sequence);
    }
    return true;
}
bool kui_destination_save(const char *root,kui_log_fn log) {
    char normalized[KUI_DEST_ROOT_CAP];
    if(!kui_destination_normalize(normalized,root)) {if(log) log("Invalid destination path; save refused");return false;}
    struct record records[2];
    if(!read_slot(0,&records[0],log) || !read_slot(1,&records[1],log)) return false;
    int current=latest(records);unsigned target=current<0?0:(unsigned)(1-current);
    uint64_t sequence=current<0?1:records[current].sequence+1;
    if(!sequence) {if(log) log("Destination sequence exhausted; save refused");return false;}
    uint8_t record[KUI_DEST_RECORD_SIZE];
    if(!kui_destination_encode(record,normalized,sequence)) return false;
    FRESULT r=f_mkdir("0:/KUI");
    if(r!=FR_OK && r!=FR_EXIST) {if(log) log("Cannot create /KUI for destination: FatFs=%u",(unsigned)r);return false;}
    FIL file;r=f_open(&file,slots[target],FA_WRITE|FA_CREATE_ALWAYS);
    if(r!=FR_OK) {if(log) log("Cannot save destination: FatFs=%u",(unsigned)r);return false;}
    UINT written=0;r=f_write(&file,record,sizeof(record),&written);bool ok=r==FR_OK && written==sizeof(record);
    if(ok) {r=f_sync(&file);ok=r==FR_OK;}
    FRESULT closed=f_close(&file);ok=ok && closed==FR_OK;
    if(!ok) {
        if(log) log("Destination save failed: FatFs=%u/%u bytes=%u; prior slot retained",(unsigned)r,(unsigned)closed,(unsigned)written);
        return false;
    }
    struct record saved;
    if(!read_slot(target,&saved,log) || !saved.valid || saved.sequence!=sequence || strcmp(saved.root,normalized)) {
        if(log) log("Destination reread failed; prior slot retained");
        return false;
    }
    if(log) log("Saved destination %s sequence=%" PRIu64,normalized,sequence);
    return true;
}
