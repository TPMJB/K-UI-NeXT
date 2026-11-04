/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_UPDATE_BROWSER_H
#define KUI_UPDATE_BROWSER_H
#include "kui/apps.h"
#include <string.h>
#define KUI_UPDATE_BROWSER_ROOT "0:/KUI/updates"
#define KUI_UPDATE_BROWSER_PATH_BYTES 192u
#define KUI_UPDATE_BROWSER_ROWS 8u
/* Canonical UTF-8 paths, confined to the Updates subtree. No normalization
 * ever turns an untrusted path into permission to leave that subtree. */
static inline bool kui_update_browser_path(const char *path) {
    static const char root[]=KUI_UPDATE_BROWSER_ROOT;
    if(!path || !memchr(path,0,KUI_UPDATE_BROWSER_PATH_BYTES) || strncmp(path,root,sizeof(root)-1u))return false;
    const unsigned char *p=(const unsigned char *)path+sizeof(root)-1u;
    if(!*p)return true;
    if(*p++!='/' || !*p)return false;
    bool start=true;
    while(*p) {
        if(start && (*p=='.' || *p=='/'))return false;
        if(*p=='/') {start=true;++p;if(!*p)return false;continue;}
        start=false;
        uint32_t point;unsigned n;
        if(*p<0x80u) {point=*p;n=1u;}
        else if(*p>=0xc2u && *p<=0xdfu) {point=*p&31u;n=2u;}
        else if(*p>=0xe0u && *p<=0xefu) {point=*p&15u;n=3u;}
        else if(*p>=0xf0u && *p<=0xf4u) {point=*p&7u;n=4u;}
        else return false;
        for(unsigned i=1u;i<n;i++) {if((p[i]&0xc0u)!=0x80u)return false;point=(point<<6u)|(p[i]&63u);}
        if(point<32u || (point>=127u && point<=159u) || point=='\\' || point==':' ||
           (n==2u && point<0x80u) || (n==3u && point<0x800u) || (n==4u && point<0x10000u) ||
           (point>=0xd800u && point<=0xdfffu) || point>0x10ffffu)return false;
        p+=n;
    }
    return true;
}
static inline bool kui_update_browser_suffix(const char *path,const char *suffix) {
    size_t n=strlen(path),s=strlen(suffix);if(n<=s)return false;
    for(size_t i=0;i<s;i++) {
        unsigned char a=(unsigned char)path[n-s+i],b=(unsigned char)suffix[i];
        if(a>='A' && a<='Z')a=(unsigned char)(a+('a'-'A'));
        if(a!=b)return false;
    }
    return true;
}
static inline bool kui_update_browser_package(const char *path) {
    return kui_update_browser_path(path) && strcmp(path,KUI_UPDATE_BROWSER_ROOT) &&
        (kui_update_browser_suffix(path,".kup") || kui_update_browser_suffix(path,".zip"));
}
static inline bool kui_update_browser_child(const char *directory,const char *path) {
    if(!kui_update_browser_path(directory) || !kui_update_browser_path(path))return false;
    size_t n=strlen(directory);return !strncmp(directory,path,n) && path[n]=='/' && path[n+1u] && !strchr(path+n+1u,'/');
}
static inline bool kui_update_browser_parent(const char *path,char out[KUI_UPDATE_BROWSER_PATH_BYTES]) {
    if(!kui_update_browser_path(path) || !strcmp(path,KUI_UPDATE_BROWSER_ROOT))return false;
    size_t n=(size_t)(strrchr(path,'/')-path);memmove(out,path,n);out[n]=0;return true;
}
struct kui_update_browser_row {
    char name[96],path[KUI_UPDATE_BROWSER_PATH_BYTES];
    uint64_t bytes;
    bool is_directory,is_zip;
};
struct kui_update_browser_view {
    char directory[KUI_UPDATE_BROWSER_PATH_BYTES];
    unsigned page,count,total;
    struct kui_update_browser_row rows[KUI_UPDATE_BROWSER_ROWS];
};
enum kui_update_browser_result { KUI_UPDATE_BROWSER_OK, KUI_UPDATE_BROWSER_ARGUMENT,
    KUI_UPDATE_BROWSER_NOT_FOUND,KUI_UPDATE_BROWSER_IO,KUI_UPDATE_BROWSER_STOPPED,
    KUI_UPDATE_BROWSER_LIMIT,KUI_UPDATE_BROWSER_MEMORY,KUI_UPDATE_BROWSER_STALE };
/* Read-only mounted/exclusive FatFs listing. Directories first, then an ASCII
 * case-insensitive name order with exact UTF-8 byte ordering as its tie-break.
 * At most4096 visited entries; no filesystem writes or device discovery. */
enum kui_update_browser_result kui_update_browser_list(const char *directory,unsigned page,
    struct kui_update_browser_view *,kui_cancel_fn cancel);
#endif
