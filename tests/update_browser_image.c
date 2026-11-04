/* SPDX-License-Identifier: GPL-3.0-only */
/* Production read-only browser over real FAT32/exFAT; only media is synthetic. */
#include "kui/update_browser.h"
#include "kui/media.h"
#include "ff.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *disk;
static size_t disk_bytes;
static FATFS fs;
static unsigned checks,writes,reads,cancel_calls;
static int cancel_at=-1;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"Update browser failed %u: %s\n",__LINE__,#x);exit(1);}}while(0)
#define ROOT KUI_UPDATE_BROWSER_ROOT

static uint64_t blocks(void *context) {(void)context;return disk_bytes/512u;}
static int read_blocks(void *context,uint32_t block,size_t count,uint8_t *out) {
    (void)context;if((uint64_t)block+count>blocks(NULL))return -1;
    ++reads;memcpy(out,disk+(size_t)block*512u,count*512u);return 0;
}
static int write_blocks(void *context,uint32_t block,size_t count,const uint8_t *in) {
    (void)context;if((uint64_t)block+count>blocks(NULL))return -1;
    ++writes;memcpy(disk+(size_t)block*512u,in,count*512u);return 0;
}
static int sync_blocks(void *context) {(void)context;return 0;}
static const struct kui_media_ops media={NULL,blocks,read_blocks,write_blocks,sync_blocks};
uint32_t kui_clock_fattime(void) {return ((2026u-1980u)<<25)|(11u<<21)|(4u<<16);}
static bool cancelled(void) {return cancel_at>=0 && cancel_calls++>=(unsigned)cancel_at;}
static void directory(const char *path) {
    FRESULT result=f_mkdir(path);CHECK(result==FR_OK || result==FR_EXIST);
}
static void file(const char *path,unsigned bytes) {
    FIL output;UINT done=0;uint8_t data[64];CHECK(bytes<=sizeof(data));
    memset(data,(int)bytes,sizeof(data));CHECK(f_open(&output,path,FA_CREATE_NEW|FA_WRITE)==FR_OK);
    CHECK(f_write(&output,data,bytes,&done)==FR_OK && done==bytes);CHECK(f_close(&output)==FR_OK);
}
static enum kui_update_browser_result listing(const char *path,unsigned page,struct kui_update_browser_view *view) {
    unsigned before=writes;cancel_calls=0;
    enum kui_update_browser_result result=kui_update_browser_list(path,page,view,cancelled);
    CHECK(writes==before);
    if(result==KUI_UPDATE_BROWSER_OK) {
        CHECK(!strcmp(view->directory,path) && view->page==page && view->count<=8u);
        for(unsigned i=0;i<view->count;i++) {
            CHECK(kui_update_browser_child(path,view->rows[i].path));
            CHECK(kui_update_browser_path(view->rows[i].path));
            CHECK(!view->rows[i].is_directory || !view->rows[i].is_zip);
        }
    } else CHECK(!view->count && !view->total);
    return result;
}
static void paths(void) {
    static const char *const bad[]={"", "0:/", "0:/KUI", "0:/KUI/updates-extra", "1:/KUI/updates",
        "/KUI/updates", "KUI/updates", "0:/KUI/updates/", "0:/KUI/updates//a", "0:/KUI/updates/.",
        "0:/KUI/updates/..", "0:/KUI/updates/../private", "0:/KUI/updates/a/../../private",
        "0:/KUI/updates/.hidden", "0:/KUI/updates/a/.hidden", "0:/KUI/updates/a/./b",
        "0:/KUI/updates/a\\b", "0:/KUI/updates/a:b", "0:/KUI/updates/a\001b",
        "0:/KUI/updates/a\177b", "0:/KUI/updates/\302\200", "0:/KUI/updates/\300\257",
        "0:/KUI/updates/\342\202", "0:/KUI/updates/\355\240\200", "0:/KUI/updates/\364\220\200\200",
        "0:/KUI/updates/\200", "0:/KUI/updates/\360\200\200\257"};
    struct kui_update_browser_view view;unsigned before=reads;
    CHECK(!kui_update_browser_path(NULL));
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
        if(kui_update_browser_path(bad[i]))fprintf(stderr,"Accepted invalid path case %u: %s\n",i,bad[i]);
        CHECK(!kui_update_browser_path(bad[i]));
        CHECK(listing(bad[i],0u,&view)==KUI_UPDATE_BROWSER_ARGUMENT);
    }
    char long_path[KUI_UPDATE_BROWSER_PATH_BYTES+8u];memset(long_path,'a',sizeof(long_path));
    memcpy(long_path,ROOT "/",sizeof(ROOT));long_path[sizeof(long_path)-1u]=0;
    CHECK(!kui_update_browser_path(long_path));CHECK(listing(long_path,0u,&view)==KUI_UPDATE_BROWSER_ARGUMENT);
    memset(long_path,'a',sizeof(long_path));CHECK(!kui_update_browser_path(long_path));
    CHECK(listing(long_path,0u,&view)==KUI_UPDATE_BROWSER_ARGUMENT);
    CHECK(listing(ROOT,UINT_MAX,&view)==KUI_UPDATE_BROWSER_ARGUMENT);
    CHECK(kui_update_browser_list(ROOT,0u,NULL,cancelled)==KUI_UPDATE_BROWSER_ARGUMENT);
    CHECK(reads==before);
    CHECK(kui_update_browser_path(ROOT));CHECK(kui_update_browser_path(ROOT "/Saison été/日本語 更新"));
    CHECK(kui_update_browser_package(ROOT "/a.KUP"));CHECK(kui_update_browser_package(ROOT "/b.ZiP"));
    CHECK(!kui_update_browser_package(ROOT));CHECK(!kui_update_browser_package(ROOT "/x.zip.old"));
    CHECK(!kui_update_browser_package(ROOT "/x.txt"));
    CHECK(kui_update_browser_child(ROOT,ROOT "/folder"));
    CHECK(!kui_update_browser_child(ROOT,ROOT "/folder/a.kup"));
    CHECK(!kui_update_browser_child(ROOT "/folder",ROOT "/folder2/a.kup"));
    char parent[KUI_UPDATE_BROWSER_PATH_BYTES];
    CHECK(kui_update_browser_parent(ROOT "/Saison été/日本語 更新",parent));CHECK(!strcmp(parent,ROOT "/Saison été"));
    CHECK(kui_update_browser_parent(parent,parent));CHECK(!strcmp(parent,ROOT));
    CHECK(!kui_update_browser_parent(ROOT,parent));CHECK(!kui_update_browser_parent(ROOT "/../private",parent));
    char boundary[KUI_UPDATE_BROWSER_PATH_BYTES];memcpy(boundary,ROOT "/",sizeof(ROOT));
    memset(boundary+sizeof(ROOT),'x',sizeof(boundary)-sizeof(ROOT)-1u);boundary[sizeof(boundary)-1u]=0;
    CHECK(strlen(boundary)==KUI_UPDATE_BROWSER_PATH_BYTES-1u && kui_update_browser_path(boundary));
}
static const char *const folders[]={"alpha folder","Empty folder","Saison été","Zulu.kup"};
static const char *const packages[]={"AA.kup","b package.ZIP","c.KUP","D.zip","e.kup","F.kup",
    "g.zip","H.kup","i.kup","J.zip","k.kup","L.zip",NULL,"N.ZIP","o.kup","P.zip","q.kup","R.zip","日本語.zip"};
static char long_name[128];
static const char *package_name(unsigned index) {return packages[index]?packages[index]:long_name;}
static void seed(void) {
    directory("0:/KUI");directory(ROOT);
    char path[320];
    /* Create in reverse order so physical directory order cannot pass sorting. */
    for(unsigned i=sizeof(folders)/sizeof(folders[0]);i-->0u;) {
        snprintf(path,sizeof(path),ROOT "/%s",folders[i]);directory(path);
    }
    memset(long_name,'m',94u);strcpy(long_name+94u,"雪.kup");
    for(unsigned i=sizeof(packages)/sizeof(packages[0]);i-->0u;) {
        snprintf(path,sizeof(path),ROOT "/%s",package_name(i));file(path,i+1u);
    }
    file(ROOT "/notes.txt",3u);file(ROOT "/.dot.kup",3u);directory(ROOT "/.dot folder");
    file(ROOT "/hidden.kup",3u);CHECK(f_chmod(ROOT "/hidden.kup",AM_HID,AM_HID)==FR_OK);
    file(ROOT "/system.zip",3u);CHECK(f_chmod(ROOT "/system.zip",AM_SYS,AM_SYS)==FR_OK);
    directory(ROOT "/Hidden folder");CHECK(f_chmod(ROOT "/Hidden folder",AM_HID,AM_HID)==FR_OK);
    char oversized[220];memset(oversized,'v',200u);strcpy(oversized+200u,".kup");
    snprintf(path,sizeof(path),ROOT "/%s",oversized);file(path,3u);
    directory(ROOT "/Saison été/日本語 更新");file(ROOT "/Saison été/日本語 更新/Ver 1.ZIP",29u);
    file(ROOT "/alpha folder/second.kup",11u);file(ROOT "/alpha folder/first.zip",13u);
}
static void rows(void) {
    enum {FOLDERS=sizeof(folders)/sizeof(folders[0]),PACKAGES=sizeof(packages)/sizeof(packages[0]),TOTAL=FOLDERS+PACKAGES};
    struct kui_update_browser_view view,again;
    for(unsigned page=0;page<3u;page++) {
        CHECK(listing(ROOT,page,&view)==KUI_UPDATE_BROWSER_OK);
        CHECK(view.total==TOTAL && view.count==(page<2u?8u:TOTAL-16u));
        CHECK(listing(ROOT,page,&again)==KUI_UPDATE_BROWSER_OK);CHECK(!memcmp(&view,&again,sizeof(view)));
        for(unsigned i=0;i<view.count;i++) {
            unsigned index=page*8u+i;bool folder=index<FOLDERS;
            const char *name=folder?folders[index]:package_name(index-FOLDERS);char expected[256];
            snprintf(expected,sizeof(expected),ROOT "/%s",name);
            CHECK(!strcmp(view.rows[i].path,expected));CHECK(view.rows[i].is_directory==folder);
            if(folder)CHECK(!strcmp(view.rows[i].name,name));
            else {
                unsigned member=index-FOLDERS;CHECK(view.rows[i].bytes==member+1u);
                CHECK(view.rows[i].is_zip==(member==1u || member==3u || member==6u || member==9u ||
                    member==11u || member==13u || member==15u || member==17u || member==18u));
                if(member==12u)CHECK(strlen(view.rows[i].name)==94u && !memcmp(view.rows[i].name,long_name,94u));
                else CHECK(!strcmp(view.rows[i].name,name));
            }
        }
    }
    CHECK(listing(ROOT,3u,&view)==KUI_UPDATE_BROWSER_STALE);
    CHECK(listing(ROOT,UINT_MAX/8u,&view)==KUI_UPDATE_BROWSER_STALE);
    CHECK(listing(ROOT "/Empty folder",0u,&view)==KUI_UPDATE_BROWSER_OK && !view.count && !view.total);
    CHECK(listing(ROOT "/Empty folder",1u,&view)==KUI_UPDATE_BROWSER_STALE);
    CHECK(listing(ROOT "/alpha folder",0u,&view)==KUI_UPDATE_BROWSER_OK && view.count==2u && view.total==2u);
    CHECK(!strcmp(view.rows[0].name,"first.zip") && view.rows[0].is_zip && view.rows[0].bytes==13u);
    CHECK(!strcmp(view.rows[1].name,"second.kup") && !view.rows[1].is_zip && view.rows[1].bytes==11u);
    CHECK(listing(ROOT "/Saison été",0u,&view)==KUI_UPDATE_BROWSER_OK && view.count==1u && view.rows[0].is_directory);
    CHECK(!strcmp(view.rows[0].name,"日本語 更新"));
    CHECK(listing(ROOT "/Saison été/日本語 更新",0u,&view)==KUI_UPDATE_BROWSER_OK && view.count==1u);
    CHECK(!strcmp(view.rows[0].name,"Ver 1.ZIP") && view.rows[0].is_zip && view.rows[0].bytes==29u);
    cancel_at=0;CHECK(listing(ROOT,0u,&view)==KUI_UPDATE_BROWSER_STOPPED);
    cancel_at=4;CHECK(listing(ROOT,0u,&view)==KUI_UPDATE_BROWSER_STOPPED);CHECK(cancel_calls==5u);
    cancel_at=-1;CHECK(listing(ROOT,0u,&view)==KUI_UPDATE_BROWSER_OK && view.total==TOTAL);
}
int main(int argc,char **argv) {
    CHECK(argc==2);FILE *input=fopen(argv[1],"rb");CHECK(input && !fseek(input,0,SEEK_END));long length=ftell(input);
    CHECK(length>0 && !fseek(input,0,SEEK_SET));disk_bytes=(size_t)length;disk=malloc(disk_bytes);
    CHECK(disk && fread(disk,1,disk_bytes,input)==disk_bytes && !fclose(input));
    kui_media_set(&media);CHECK(f_mount(&fs,"0:",1)==FR_OK);paths();
    struct kui_update_browser_view view;CHECK(listing(ROOT,0u,&view)==KUI_UPDATE_BROWSER_OK && !view.count);
    seed();uint8_t *snapshot=malloc(disk_bytes);CHECK(snapshot);memcpy(snapshot,disk,disk_bytes);
    unsigned before=writes;rows();CHECK(writes==before && !memcmp(disk,snapshot,disk_bytes));
    CHECK(listing(ROOT "/Empty folder",0u,&view)==KUI_UPDATE_BROWSER_OK);
    CHECK(f_unlink(ROOT "/Empty folder")==FR_OK);before=writes;
    CHECK(listing(ROOT "/Empty folder",0u,&view)==KUI_UPDATE_BROWSER_NOT_FOUND);CHECK(writes==before);
    CHECK(listing(ROOT "/never existed",0u,&view)==KUI_UPDATE_BROWSER_NOT_FOUND);
    CHECK(f_mount(NULL,"0:",0)==FR_OK);kui_media_set(NULL);
    printf("PASS update browser real filesystem: %u checks; nested UTF-8 folders, sorted 8-row pages, package suffixes, hidden/path guards, stale/disappeared folders, cancellation and zero listing writes\n",checks);
    free(snapshot);free(disk);return 0;
}
