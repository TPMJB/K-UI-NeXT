/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/screenshot.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct entry {char name[40];uint8_t data[128];size_t size;bool used;};
static struct entry entries[16];
static bool kui_folder,shots_folder,folder_is_file;
enum fault {NONE,DIR_OPEN,DIR_READ,DIR_CLOSE,MKDIR,CREATE,WRITE,SHORT_WRITE,SYNC,CLOSE,RENAME};
static enum fault fault;
static unsigned reads,opens,writes,syncs,closes,renames,unlinks;
static const uint16_t frame[]={0xf800,0x07e0,0x001f,0xffff};
static const char *leaf(const char *path) {
    static const char prefix[]=KUI_SCREENSHOT_FOLDER "/";
    assert(!strncmp(path,prefix,sizeof(prefix)-1));return path+sizeof(prefix)-1;
}
static int find(const char *name) {
    for(unsigned i=0;i<16u;i++)if(entries[i].used && !strcmp(entries[i].name,name))return (int)i;
    return -1;
}
static unsigned add(const char *name) {
    assert(find(name)<0);
    for(unsigned i=0;i<16u;i++)if(!entries[i].used) {
        entries[i]=(struct entry){.used=true};snprintf(entries[i].name,sizeof(entries[i].name),"%s",name);return i;
    }
    assert(false);return 0;
}
FRESULT f_stat(const TCHAR *path,FILINFO *info) {
    memset(info,0,sizeof(*info));
    if(!strcmp(path,"0:/KUI")) {if(!kui_folder)return FR_NO_FILE;info->fattrib=AM_DIR;return FR_OK;}
    if(!strcmp(path,KUI_SCREENSHOT_FOLDER)) {
        if(!shots_folder)return FR_NO_FILE;
        info->fattrib=folder_is_file?0:AM_DIR;return FR_OK;
    }
    return find(leaf(path))<0?FR_NO_FILE:FR_OK;
}
FRESULT f_mkdir(const TCHAR *path) {
    if(fault==MKDIR)return FR_DISK_ERR;
    if(!strcmp(path,"0:/KUI"))kui_folder=true;
    else {assert(!strcmp(path,KUI_SCREENSHOT_FOLDER));assert(kui_folder);shots_folder=true;}
    return FR_OK;
}
FRESULT f_opendir(DIR *directory,const TCHAR *path) {
    assert(!strcmp(path,KUI_SCREENSHOT_FOLDER));memset(directory,0,sizeof(*directory));
    return fault==DIR_OPEN?FR_DISK_ERR:FR_OK;
}
FRESULT f_readdir(DIR *directory,FILINFO *info) {
    ++reads;memset(info,0,sizeof(*info));if(fault==DIR_READ)return FR_DISK_ERR;
    while(directory->dptr<16u) {
        unsigned index=(unsigned)directory->dptr++;
        if(entries[index].used) {snprintf(info->fname,sizeof(info->fname),"%s",entries[index].name);break;}
    }
    return FR_OK;
}
FRESULT f_closedir(DIR *directory) {(void)directory;return fault==DIR_CLOSE?FR_DISK_ERR:FR_OK;}
FRESULT f_open(FIL *file,const TCHAR *path,BYTE mode) {
    assert(mode==(FA_WRITE|FA_CREATE_NEW));++opens;
    if(fault==CREATE)return FR_DISK_ERR;
    const char *name=leaf(path);if(find(name)>=0)return FR_EXIST;
    unsigned index=add(name);memset(file,0,sizeof(*file));file->obj.id=(WORD)(index+1u);return FR_OK;
}
FRESULT f_write(FIL *file,const void *data,UINT size,UINT *written) {
    ++writes;unsigned index=file->obj.id-1u;assert(index<16u);
    UINT count=fault==SHORT_WRITE?size-1u:size;
    assert(entries[index].size+count<=sizeof(entries[index].data));
    memcpy(entries[index].data+entries[index].size,data,count);entries[index].size+=count;
    file->obj.objsize=entries[index].size;*written=count;
    return fault==WRITE?FR_DISK_ERR:FR_OK;
}
FRESULT f_sync(FIL *file) {(void)file;++syncs;return fault==SYNC?FR_DISK_ERR:FR_OK;}
FRESULT f_close(FIL *file) {(void)file;++closes;return fault==CLOSE?FR_DISK_ERR:FR_OK;}
FRESULT f_rename(const TCHAR *from,const TCHAR *to) {
    ++renames;int index=find(leaf(from));assert(index>=0);
    if(fault==RENAME)return FR_DISK_ERR;
    if(find(leaf(to))>=0)return FR_EXIST;
    snprintf(entries[index].name,sizeof(entries[index].name),"%s",leaf(to));return FR_OK;
}
FRESULT f_unlink(const TCHAR *path) {
    ++unlinks;int index=find(leaf(path));if(index<0)return FR_NO_FILE;
    assert(strstr(entries[index].name,".part"));entries[index].used=false;return FR_OK;
}
static void reset(void) {
    memset(entries,0,sizeof(entries));kui_folder=shots_folder=folder_is_file=false;fault=NONE;
    reads=opens=writes=syncs=closes=renames=unlinks=0;
}
static bool save(char path[KUI_SCREENSHOT_PATH_CAP]) {
    memset(path,'x',KUI_SCREENSHOT_PATH_CAP);
    return kui_screenshot_save_rgb565(frame,4,2,2,2,path,NULL);
}
static void successful(void) {
    reset();char path[KUI_SCREENSHOT_PATH_CAP];assert(save(path));
    assert(!strcmp(path,KUI_SCREENSHOT_FOLDER "/KUI-000001.bmp"));
    assert(kui_folder && shots_folder && opens==1 && writes==3 && syncs==1 && closes==1 && renames==1);
    int first=find("KUI-000001.bmp");assert(first>=0 && entries[first].size==70);
    struct entry original=entries[first];
    assert(save(path) && !strcmp(path,KUI_SCREENSHOT_FOLDER "/KUI-000002.bmp"));
    assert(!memcmp(&entries[first],&original,sizeof(original)));
    add("kui-000009.bmp");add("KUI-000010.part");add("KUI-abc123.bmp");add("KUI-.bmp");
    assert(save(path) && !strcmp(path,KUI_SCREENSHOT_FOLDER "/KUI-000011.bmp"));
    assert(find("KUI-000010.part")>=0);
    reset();add("KUI-999999.bmp");add("KUI-000001.bmp");add("KUI-000002.part");
    assert(save(path) && !strcmp(path,KUI_SCREENSHOT_FOLDER "/KUI-000003.bmp"));
    assert(find("KUI-000002.part")>=0);
    puts("PASS screenshot files: numbered new captures, existing bytes retained, abandoned parts, numbering wrap");
}
static void failures(void) {
    char path[KUI_SCREENSHOT_PATH_CAP];
    for(enum fault failed=DIR_OPEN;failed<=RENAME;failed++) {
        reset();unsigned old=add("KUI-000001.bmp");entries[old].data[0]=91;entries[old].size=1;
        struct entry original=entries[old];fault=failed;
        assert(!save(path) && !path[0]);assert(!memcmp(&entries[old],&original,sizeof(original)));
        assert(find("KUI-000002.bmp")<0);
        if(failed>=WRITE) {
            assert(closes==1);
            assert(failed==CLOSE?find("KUI-000002.part")>=0:find("KUI-000002.part")<0);
        } else assert(opens==0 || failed==CREATE);
    }
    reset();kui_folder=shots_folder=folder_is_file=true;
    assert(!save(path) && !path[0] && !opens);
    reset();path[0]='x';assert(!kui_screenshot_save_rgb565(frame,3,2,2,2,path,NULL));
    assert(!path[0] && !reads && !opens && !kui_folder);
    puts("PASS screenshot files: directory, open, short-write, write, sync, close and rename failures never report success");
}
int main(void) {successful();failures();return 0;}
