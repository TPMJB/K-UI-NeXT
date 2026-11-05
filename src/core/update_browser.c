/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/update_browser.h"
#include "ff.h"
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
struct entry {char path[KUI_UPDATE_BROWSER_PATH_BYTES];uint64_t bytes;bool directory,zip;};
static int order(const void *aa,const void *bb) {
    const struct entry *a=aa,*b=bb;
    if(a->directory!=b->directory)return a->directory?-1:1;
    const unsigned char *x=(const unsigned char *)strrchr(a->path,'/')+1u;
    const unsigned char *y=(const unsigned char *)strrchr(b->path,'/')+1u;
    while(*x && *y) {
        unsigned ax=*x++,by=*y++;
        if(ax>='A' && ax<='Z')ax+='a'-'A';
        if(by>='A' && by<='Z')by+='a'-'A';
        if(ax!=by)return ax<by?-1:1;
    }
    if(*x || *y)return *x?1:-1;
    return strcmp(a->path,b->path);
}
enum kui_update_browser_result kui_update_browser_list(const char *directory,unsigned page,
    struct kui_update_browser_view *out,kui_cancel_fn cancel) {
    if(!out)return KUI_UPDATE_BROWSER_ARGUMENT;
    memset(out,0,sizeof(*out));
    if(!kui_update_browser_path(directory) || page>UINT_MAX/KUI_UPDATE_BROWSER_ROWS)return KUI_UPDATE_BROWSER_ARGUMENT;
    strcpy(out->directory,directory);out->page=page;
    if(cancel && cancel())return KUI_UPDATE_BROWSER_STOPPED;
    DIR folder;FILINFO info;FRESULT fr=f_opendir(&folder,directory);
    if(fr==FR_NO_FILE || fr==FR_NO_PATH)return strcmp(directory,KUI_UPDATE_BROWSER_ROOT)?KUI_UPDATE_BROWSER_NOT_FOUND:
        page?KUI_UPDATE_BROWSER_STALE:KUI_UPDATE_BROWSER_OK;
    if(fr!=FR_OK)return KUI_UPDATE_BROWSER_IO;
    struct entry *entries=NULL;size_t count=0u,capacity=0u;unsigned visited=0u;
    enum kui_update_browser_result result=KUI_UPDATE_BROWSER_OK;
    while((fr=f_readdir(&folder,&info))==FR_OK && info.fname[0]) {
        if(cancel && cancel()) {result=KUI_UPDATE_BROWSER_STOPPED;break;}
        if(++visited>4096u) {result=KUI_UPDATE_BROWSER_LIMIT;break;}
        if(info.fattrib&(AM_HID|AM_SYS))continue;
        char path[KUI_UPDATE_BROWSER_PATH_BYTES];int n=snprintf(path,sizeof(path),"%s/%s",directory,info.fname);
        if(n<0 || (size_t)n>=sizeof(path) || !kui_update_browser_child(directory,path))continue;
        bool is_directory=(info.fattrib&AM_DIR)!=0u;
        if(!is_directory && !kui_update_browser_package(path))continue;
        if(count==capacity) {
            size_t next=capacity?capacity*2u:16u;struct entry *grown=realloc(entries,next*sizeof(*entries));
            if(!grown) {result=KUI_UPDATE_BROWSER_MEMORY;break;}
            entries=grown;capacity=next;
        }
        strcpy(entries[count].path,path);entries[count].bytes=info.fsize;entries[count].directory=is_directory;
        entries[count].zip=!is_directory && kui_update_browser_suffix(path,".zip");++count;
    }
    if(f_closedir(&folder)!=FR_OK || fr!=FR_OK)result=KUI_UPDATE_BROWSER_IO;
    if(result==KUI_UPDATE_BROWSER_OK && page && (uint64_t)page*KUI_UPDATE_BROWSER_ROWS>=count)result=KUI_UPDATE_BROWSER_STALE;
    if(result==KUI_UPDATE_BROWSER_OK) {
        if(count>1u)qsort(entries,count,sizeof(*entries),order);
        out->total=(unsigned)count;
        size_t first=(size_t)page*KUI_UPDATE_BROWSER_ROWS;
        for(size_t i=first;i<count && out->count<KUI_UPDATE_BROWSER_ROWS;i++) {
            struct kui_update_browser_row *row=out->rows+out->count++;strcpy(row->path,entries[i].path);
            const char *name=strrchr(row->path,'/')+1u;size_t n=strlen(name);if(n>=sizeof(row->name))n=sizeof(row->name)-1u;
            while(n && ((unsigned char)name[n]&0xc0u)==0x80u)--n;
            memcpy(row->name,name,n);row->name[n]=0;row->bytes=entries[i].bytes;
            row->is_directory=entries[i].directory;row->is_zip=entries[i].zip;
        }
    }
    free(entries);return result;
}
