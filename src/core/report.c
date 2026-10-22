/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/report.h"
#include <stdio.h>

enum kui_report_result kui_report_save(const void *data,size_t bytes,char path[96],
    kui_log_fn log,kui_cancel_fn cancelled) {
    if(!path) return KUI_REPORT_FAILED;
    path[0]=0;
    if(!data || !bytes || bytes>UINT32_MAX || !log || !cancelled) return KUI_REPORT_FAILED;
    if(cancelled()) return KUI_REPORT_STOPPED;
    FATFS fs;FIL file;bool opened=false;
    enum kui_report_result result=KUI_REPORT_FAILED;
    if(!kui_mount(&fs,log)) goto out;
    if(cancelled()) {result=KUI_REPORT_STOPPED;goto out;}
    char dir[64],temp[96];
    if(!kui_new_probe_dir(dir,log)) goto out;
    snprintf(path,96,"%s/diagnostics.txt",dir);
    snprintf(temp,sizeof(temp),"%s/diagnostics.tmp",dir);
    if(cancelled()) {result=KUI_REPORT_STOPPED;goto out;}
    FRESULT r=f_open(&file,temp,FA_CREATE_NEW|FA_WRITE);
    if(r!=FR_OK) {log("Report create failed: FatFs=%u",(unsigned)r);goto out;}
    opened=true;size_t written=0;
    while(written<bytes) {
        if(cancelled()) {result=KUI_REPORT_STOPPED;goto out;}
        UINT count=bytes-written>4096?4096:(UINT)(bytes-written),done=0;
        r=f_write(&file,(const uint8_t *)data+written,count,&done);
        if(r!=FR_OK || done!=count) {
            log("Report write failed/full: FatFs=%u bytes=%u/%u",(unsigned)r,done,count);goto out;
        }
        written+=done;
    }
    r=f_sync(&file);
    FRESULT close=f_close(&file);opened=false;
    if(r!=FR_OK || close!=FR_OK) {
        log("Report flush failed: sync=%u close=%u",(unsigned)r,(unsigned)close);goto out;
    }
    if(cancelled()) {result=KUI_REPORT_STOPPED;goto out;}
    r=f_rename(temp,path);
    if(r!=FR_OK) {log("Report publish failed: FatFs=%u",(unsigned)r);goto out;}
    result=KUI_REPORT_SAVED;
out:
    if(opened && f_close(&file)!=FR_OK) {
        log("Report close after failure did not complete");result=KUI_REPORT_FAILED;
    }
    if(f_mount(NULL,"0:",0)!=FR_OK) result=KUI_REPORT_FAILED;
    return result;
}
