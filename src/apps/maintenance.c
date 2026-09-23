/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/maintenance.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#define ROOT "0:/KUI/backups/system"
struct session {struct kui_app_status *out;kui_log_fn log;kui_cancel_fn cancel;kui_app_progress_fn progress;};
static void message(struct session *s,const char *text){snprintf(s->out->message,sizeof(s->out->message),"%s",text);if(s->progress)s->progress(s->out);}
static bool error(struct session *s,const char *text){++s->out->errors;message(s,text);if(s->log)s->log("System backup: %s",text);return false;}
static bool stopped(struct session *s){if(s->cancel&&s->cancel()){s->out->stopped=true;message(s,"Stopped; incomplete backup is not published");return true;}return false;}
bool kui_maintenance_layout_valid(const struct kui_maintenance_identity *id){
    if(!id)return false;
    uint32_t total=0;
    for(unsigned i=0;i<5;i++){
        if(!id->size[i]||id->start[i]>=KUI_MAINTENANCE_FLASH_BYTES||id->size[i]>KUI_MAINTENANCE_FLASH_BYTES-id->start[i])return false;
        for(unsigned j=0;j<i;j++)if(id->start[i]<id->start[j]+id->size[j]&&id->start[j]<id->start[i]+id->size[i])return false;
        total+=id->size[i];
    }return total==KUI_MAINTENANCE_FLASH_BYTES;
}
static bool mkdir_checked(struct session *s,const char *path){FRESULT r=f_mkdir(path);if(r==FR_OK)return true;if(r==FR_EXIST){FILINFO info;if(f_stat(path,&info)==FR_OK&&(info.fattrib&AM_DIR))return true;}return error(s,"Cannot create backup directory");}
static bool read_source(struct session *s,const struct kui_maintenance_source *source,bool bios,uint32_t off,uint8_t *data,uint8_t *check,size_t bytes){
    if(stopped(s))return false;
    if(source->read(source->ctx,bios,off,data,bytes)||source->read(source->ctx,bios,off,check,bytes))return error(s,"Console memory read failed");
    if(memcmp(data,check,bytes))return error(s,"Console memory changed between reads; backup refused");
    return true;
}
static void describe(struct kui_app_status *out,const struct kui_maintenance_identity *id){
    const char *region=id->region==1?"Japan":id->region==2?"US/Canada":id->region==3?"Europe":"Unknown";
    out->line_count=10;snprintf(out->lines[0],KUI_APP_LINE_CAP,"Reported console region: %s",region);
    if(id->settings_valid)snprintf(out->lines[1],KUI_APP_LINE_CAP,"BIOS language=%d; audio=%s; disc autostart=%s",id->language,id->audio==1?"stereo":id->audio==0?"mono":"unknown",id->autostart==1?"on":id->autostart==0?"off":"unknown");
    else snprintf(out->lines[1],KUI_APP_LINE_CAP,"BIOS user settings: unavailable");
    for(unsigned i=0;i<5;i++)snprintf(out->lines[2+i],KUI_APP_LINE_CAP,"Flash partition %u: offset=%" PRIu32 " bytes=%" PRIu32,i,id->start[i],id->size[i]);
    snprintf(out->lines[7],KUI_APP_LINE_CAP,"Visible boot BIOS: 2 MiB read window; chip type unknown");
    snprintf(out->lines[8],KUI_APP_LINE_CAP,"No chip-ID commands, bank switches, erases or programming");
    snprintf(out->lines[9],KUI_APP_LINE_CAP,"System backups can contain private network settings");
}
void kui_maintenance_execute(unsigned action,const struct kui_maintenance_source *source,struct kui_app_status *out,kui_log_fn log,kui_cancel_fn cancel,kui_app_progress_fn progress){
    if(!out)return;
    memset(out,0,sizeof(*out));struct session s={out,log,cancel,progress};struct kui_maintenance_identity id;
    FIL file;bool opened=false;uint8_t data[4096],check[4096];char folder[96]={0},part[128],final[128],report[128];uint32_t crc=0;bool ok=false;
    if(action>KUI_MAINTENANCE_BIOS_BACKUP||!source||!source->identity||!source->read){error(&s,"Invalid maintenance request");goto done;}
    if(stopped(&s))goto done;
    memset(&id,0,sizeof(id));if(!source->identity(source->ctx,&id)){error(&s,"Cannot read console identity/flash partition layout");goto done;}
    describe(out,&id);
    if(!kui_maintenance_layout_valid(&id)){error(&s,"Unsupported or overlapping flash partition layout");goto done;}
    if(action==KUI_MAINTENANCE_INSPECT){ok=true;message(&s,"Console inventory complete (read-only)");goto done;}
    bool bios=action==KUI_MAINTENANCE_BIOS_BACKUP;uint32_t bytes=bios?KUI_MAINTENANCE_BIOS_BYTES:KUI_MAINTENANCE_FLASH_BYTES;
    out->total=(uint64_t)bytes*2;message(&s,bios?"Backing up the currently visible BIOS bank":"Backing up all settings-flash partitions");
    if(!mkdir_checked(&s,"0:/KUI")||!mkdir_checked(&s,"0:/KUI/backups")||!mkdir_checked(&s,ROOT))goto done;
    unsigned index;for(index=1;index<=9999;index++){snprintf(folder,sizeof(folder),ROOT "/%s-%04u",bios?"bios":"flash",index);FRESULT r=f_mkdir(folder);if(r==FR_OK)break;if(r!=FR_EXIST){error(&s,"Cannot allocate a new backup directory");goto done;}}
    if(index>9999){error(&s,"Backup directory sequence is full");goto done;}
    snprintf(part,sizeof(part),"%s/image.part",folder);snprintf(final,sizeof(final),"%s/image.bin",folder);snprintf(report,sizeof(report),"%s/verified.txt",folder);
    if(f_open(&file,part,FA_WRITE|FA_CREATE_NEW)!=FR_OK){error(&s,"Cannot create backup file");goto done;}opened=true;
    for(uint32_t off=0;off<bytes;off+=sizeof(data)){
        UINT amount=(UINT)(bytes-off>sizeof(data)?sizeof(data):bytes-off),written=0;
        if(!read_source(&s,source,bios,off,data,check,amount))goto done;
        if(f_write(&file,data,amount,&written)!=FR_OK||written!=amount){error(&s,"Backup write failed or card is full");goto done;}
        crc=kui_crc32(crc,data,amount);out->done+=amount;if(progress)progress(out);
    }
    if(f_sync(&file)!=FR_OK){error(&s,"Backup sync failed");goto done;}
    FRESULT closed=f_close(&file);opened=false;if(closed!=FR_OK){error(&s,"Backup close failed");goto done;}
    if(stopped(&s))goto done;
    message(&s,"Comparing saved backup with console memory");
    if(f_open(&file,part,FA_READ)!=FR_OK){error(&s,"Cannot reopen backup for verification");goto done;}opened=true;
    if(f_size(&file)!=bytes){error(&s,"Backup length mismatch");goto done;}
    uint32_t verified=0;
    for(uint32_t off=0;off<bytes;off+=sizeof(data)){
        UINT amount=(UINT)(bytes-off>sizeof(data)?sizeof(data):bytes-off),got=0;if(stopped(&s))goto done;
        if(f_read(&file,data,amount,&got)!=FR_OK||got!=amount){error(&s,"Backup reread failed");goto done;}
        if(source->read(source->ctx,bios,off,check,amount)||memcmp(data,check,amount)){error(&s,"Backup differs from console memory; not published");goto done;}
        verified=kui_crc32(verified,data,amount);out->done+=amount;if(progress)progress(out);
    }
    closed=f_close(&file);opened=false;if(closed!=FR_OK){error(&s,"Backup verification close failed");goto done;}
    if(verified!=crc){error(&s,"Backup checksum mismatch");goto done;}
    struct kui_maintenance_identity after;memset(&after,0,sizeof(after));
    if(!source->identity(source->ctx,&after)||memcmp(&after,&id,sizeof(id))){error(&s,"Console identity changed during backup");goto done;}
    if(stopped(&s))goto done;
    if(f_rename(part,final)!=FR_OK){error(&s,"Cannot publish verified backup");goto done;}
    char text[768];int length=snprintf(text,sizeof(text),"K-UI verified %s backup\nBytes: %" PRIu32 "\nCRC32: %08" PRIx32 "\nConsole memory read twice, SD reopened and compared byte for byte.\nVisible bank only; no chip identification or write compatibility is implied.\nRegion code: %d\nPartitions: %" PRIu32 ":%" PRIu32 ", %" PRIu32 ":%" PRIu32 ", %" PRIu32 ":%" PRIu32 ", %" PRIu32 ":%" PRIu32 ", %" PRIu32 ":%" PRIu32 "\n",bios?"BIOS":"settings flash",bytes,crc,id.region,id.start[0],id.size[0],id.start[1],id.size[1],id.start[2],id.size[2],id.start[3],id.size[3],id.start[4],id.size[4]);
    if(length<0||(size_t)length>=sizeof(text)||f_open(&file,report,FA_WRITE|FA_CREATE_NEW)!=FR_OK){error(&s,"Backup verified, but its record could not be written");goto done;}opened=true;UINT written;
    if(f_write(&file,text,(UINT)length,&written)!=FR_OK||written!=(UINT)length||f_sync(&file)!=FR_OK){error(&s,"Backup verified, but its record write/sync failed");goto done;}
    closed=f_close(&file);opened=false;if(closed!=FR_OK){error(&s,"Backup verified, but its record close failed");goto done;}
    ok=true;snprintf(out->message,sizeof(out->message),"Verified backup: %.96s",final+2);
    if(log)log("System backup verified: %s bytes=%" PRIu32 " CRC32=%08" PRIx32,final+2,bytes,crc);
done:
    if(opened&&f_close(&file)!=FR_OK)error(&s,"Backup cleanup close failed");
    out->complete=ok;out->passed=ok&&!out->errors&&!out->stopped;
    if(log){log("System maintenance: %s",out->message);if(action==KUI_MAINTENANCE_INSPECT)for(unsigned i=0;i<out->line_count;i++)log("%s",out->lines[i]);}
    if(progress)progress(out);
}
