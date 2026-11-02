/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/screenshot.h"
#include <stdio.h>
#include <string.h>

#define SCREENSHOT_LAST 999999u
static bool folder(const char *path) {
    FILINFO info;FRESULT result=f_stat(path,&info);
    if(result==FR_OK)return (info.fattrib&AM_DIR)!=0;
    if(result!=FR_NO_FILE && result!=FR_NO_PATH)return false;
    result=f_mkdir(path);
    return result==FR_OK ||
        (result==FR_EXIST && f_stat(path,&info)==FR_OK && (info.fattrib&AM_DIR));
}
static unsigned numbered(const char *name) {
    static const char prefix[]="KUI-";
    for(unsigned i=0;i<4u;i++) {
        char value=name[i];if(value>='a' && value<='z')value=(char)(value-'a'+'A');
        if(value!=prefix[i])return 0;
    }
    unsigned number=0;
    for(unsigned i=4u;i<10u;i++) {
        if(name[i]<'0' || name[i]>'9')return 0;
        number=number*10u+(unsigned)(name[i]-'0');
    }
    char suffix[6];size_t length=strlen(name+10u);
    if(length!=4u && length!=5u)return 0;
    for(size_t i=0;i<=length;i++) {
        char value=name[10u+i];suffix[i]=value>='A' && value<='Z'?(char)(value-'A'+'a'):value;
    }
    return !strcmp(suffix,".bmp") || !strcmp(suffix,".part")?number:0;
}
static bool next_number(unsigned *next) {
    DIR directory;FRESULT result=f_opendir(&directory,KUI_SCREENSHOT_FOLDER);
    if(result!=FR_OK)return false;
    unsigned highest=0;FILINFO info;
    for(;;) {
        result=f_readdir(&directory,&info);
        if(result!=FR_OK || !info.fname[0])break;
        unsigned number=numbered(info.fname);if(number>highest)highest=number;
    }
    FRESULT closed=f_closedir(&directory);
    if(result!=FR_OK || closed!=FR_OK)return false;
    *next=highest==SCREENSHOT_LAST?1u:highest+1u;return true;
}
static bool write_bytes(void *context,const void *data,size_t size) {
    UINT written=0;FRESULT result=f_write(context,data,(UINT)size,&written);
    return result==FR_OK && written==size;
}
bool kui_screenshot_save_rgb565(const uint16_t *pixels,size_t pixel_count,
    unsigned width,unsigned height,unsigned stride,
    char path[KUI_SCREENSHOT_PATH_CAP],kui_log_fn log) {
    if(!path)return false;
    path[0]=0;uint32_t bytes;
    if(!pixels || !kui_screenshot_bmp_size(pixel_count,width,height,stride,&bytes))return false;
    if(!folder("0:/KUI") || !folder(KUI_SCREENSHOT_FOLDER)) {
        if(log)log("Screenshot: cannot create /KUI/screenshots");
        return false;
    }
    unsigned next;
    if(!next_number(&next)) {if(log)log("Screenshot: cannot read output folder");return false;}
    char final[KUI_SCREENSHOT_PATH_CAP],part[KUI_SCREENSHOT_PATH_CAP];FIL file;
    bool opened=false;
    for(unsigned tried=0u;tried<SCREENSHOT_LAST;tried++) {
        snprintf(final,sizeof(final),KUI_SCREENSHOT_FOLDER "/KUI-%06u.bmp",next);
        snprintf(part,sizeof(part),KUI_SCREENSHOT_FOLDER "/KUI-%06u.part",next);
        FILINFO info;FRESULT result=f_stat(final,&info);
        if(result==FR_NO_FILE) {
            result=f_open(&file,part,FA_WRITE|FA_CREATE_NEW);
            if(result==FR_OK) {opened=true;break;}
            if(result!=FR_EXIST)break;
        } else if(result!=FR_OK)break;
        next=next==SCREENSHOT_LAST?1u:next+1u;
    }
    if(!opened) {if(log)log("Screenshot: no new output file available");return false;}
    bool ok=kui_screenshot_bmp_rgb565(pixels,pixel_count,width,height,stride,write_bytes,&file);
    if(ok)ok=f_size(&file)==bytes && f_sync(&file)==FR_OK;
    FRESULT closed=f_close(&file);if(closed!=FR_OK)ok=false;
    if(ok)ok=f_rename(part,final)==FR_OK;
    if(!ok) {
        /* Only this call's create-new temporary file is eligible for cleanup.
         * Never remove final: an uncertain rename may already have published
         * it, or a competing owner may have created that name. */
        if(closed==FR_OK)(void)f_unlink(part);
        if(log)log("Screenshot save not confirmed; existing captures retained");
        return false;
    }
    memcpy(path,final,strlen(final)+1u);
    if(log)log("Screenshot saved: %s (%lu bytes)",final+2,(unsigned long)bytes);
    return true;
}
