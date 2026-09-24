/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/maintenance.h"
#include "platform.h"
#include <dc/flashrom.h>
#include <stdio.h>
#include <string.h>
static bool identity(void *ctx,struct kui_maintenance_identity *out){
    (void)ctx;memset(out,0,sizeof(*out));out->region=flashrom_get_region();flashrom_syscfg_t cfg;
    if(flashrom_get_syscfg(&cfg)==0){out->settings_valid=true;out->language=cfg.language;out->audio=cfg.audio;out->autostart=cfg.autostart;}
    for(unsigned i=0;i<5;i++){int start,size;if(flashrom_info((int)i,&start,&size)!=0||start<0||size<=0)return false;out->start[i]=(uint32_t)start;out->size[i]=(uint32_t)size;}
    return true;
}
static int read_memory(void *ctx,bool bios,uint32_t offset,uint8_t *data,size_t bytes){
    (void)ctx;uint32_t limit=bios?KUI_MAINTENANCE_BIOS_BYTES:KUI_MAINTENANCE_FLASH_BYTES;
    if(offset>limit||bytes>limit-offset)return -1;
    if(!bios)return flashrom_read((int)offset,data,(int)bytes)==(int)bytes?0:-1;
#ifdef KUI_ON_CONSOLE
    /* Standard 2 MiB boot-ROM window, uncached P2 alias. Reads never enter a
     * chip-ID/program mode or change an external bank selector. */
    volatile const uint8_t *rom=(volatile const uint8_t *)(uintptr_t)(UINT32_C(0xa0000000)+offset);
    for(size_t i=0;i<bytes;i++)data[i]=rom[i];
    return 0;
#else
    (void)data;return -1;
#endif
}
static const struct kui_maintenance_source source={NULL,identity,read_memory};
static void quiet(const char *format,...){(void)format;}
void kui_maintenance_run(unsigned action,struct kui_app_status *out,kui_log_fn log,kui_cancel_fn cancel,kui_app_progress_fn progress){
    if(!out)return;
    memset(out,0,sizeof(*out));bool connected=false,mounted=false;FATFS fs;
    if(cancel&&cancel()){out->stopped=true;snprintf(out->message,sizeof(out->message),"System maintenance stopped");goto done;}
    if(action!=KUI_MAINTENANCE_INSPECT){
        if(action>KUI_MAINTENANCE_BIOS_BACKUP){snprintf(out->message,sizeof(out->message),"Invalid maintenance action");++out->errors;goto done;}
        if(!kui_sd_connect()){snprintf(out->message,sizeof(out->message),"Cannot connect SD for system backup");++out->errors;goto done;}connected=true;
        if(!kui_mount(&fs,log?log:quiet)){snprintf(out->message,sizeof(out->message),"Cannot mount SD for system backup");++out->errors;goto done;}mounted=true;
    }
    kui_maintenance_execute(action,&source,out,log,cancel,progress);
done:
    if(mounted&&f_mount(NULL,"0:",0)!=FR_OK){++out->errors;out->passed=false;snprintf(out->message,sizeof(out->message),"System backup unmount failed");}
    if(connected)kui_sd_disconnect();
    if(progress)progress(out);
}
