/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/startup_report.h"
#include "ff.h"

bool kui_startup_report_append_mounted(const void *data,size_t bytes) {
    if(!data || !bytes || bytes>KUI_STARTUP_REPORT_RECORD_MAX)return false;
    FIL file;
    if(f_open(&file,KUI_STARTUP_REPORT_PATH,FA_WRITE|FA_OPEN_ALWAYS)!=FR_OK)return false;
    FSIZE_t previous=f_size(&file);
    if(previous>KUI_STARTUP_REPORT_MAX_BYTES-bytes) {
        if(f_close(&file)!=FR_OK)return false;
        FRESULT removed=f_unlink(KUI_STARTUP_REPORT_PREVIOUS_PATH);
        if(removed!=FR_OK && removed!=FR_NO_FILE && removed!=FR_NO_PATH)return false;
        if(f_rename(KUI_STARTUP_REPORT_PATH,KUI_STARTUP_REPORT_PREVIOUS_PATH)!=FR_OK)return false;
        if(f_open(&file,KUI_STARTUP_REPORT_PATH,FA_WRITE|FA_CREATE_NEW)!=FR_OK)return false;
        previous=0u;
    }
    bool ok=f_lseek(&file,previous)==FR_OK;
    if(ok) {
        UINT done=0;
        ok=f_write(&file,data,(UINT)bytes,&done)==FR_OK && done==bytes;
    }
    if(ok)ok=f_sync(&file)==FR_OK;
    if(f_close(&file)!=FR_OK)ok=false;
    return ok;
}
