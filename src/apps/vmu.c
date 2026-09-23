/* SPDX-License-Identifier: GPL-3.0-only */
/* Independent read-only VMU adapter. Layout and block API follow upstream
 * KallistiOS fcfa7d869471591ca1c777543261a7bfea7cb726 dc/vmufs.h and
 * dc/maple/vmu.h. No DreamShell source or VMU write API is used here.
 * A .vms contains the complete, block-padded file payload; .dir is the original
 * 32-byte directory entry, NOT a VMI file. These are not whole-card images. */
#include "kui/apps.h"
#include "kui/destination.h"
#include "platform.h"
#include <dc/maple.h>
#include <dc/maple/vmu.h>
#include <dc/vmufs.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLOCK_BYTES 512u
#define DIR_BLOCKS_MAX 16u
#define FILES_MAX (DIR_BLOCKS_MAX*16u)
#define BACKUP_ROOT "/KUI/backups/vmu"

struct snapshot {
    uint8_t root[BLOCK_BYTES],fat[BLOCK_BYTES],directory[DIR_BLOCKS_MAX*BLOCK_BYTES];
    uint16_t entries[FILES_MAX];
    unsigned directory_blocks,directory_start,user_blocks,count,free_blocks;
    uint32_t fingerprint;
};
struct session {
    struct kui_vmu_view *out;
    kui_log_fn log;
    kui_cancel_fn cancel;
    kui_app_progress_fn progress;
    maple_device_t *device;
    unsigned port,unit;
};
/* Only the existing I/O worker calls this module. Remember the view the user
 * selected, so a changed VMU cannot silently turn a selected row into another
 * save. A card with identical metadata is indistinguishable at the Maple API. */
static struct { maple_device_t *device;uint32_t fingerprint;bool valid; } listed[8];

static uint16_t le16(const uint8_t *p) {return (uint16_t)(p[0]|((unsigned)p[1]<<8));}
static void quiet_log(const char *format,...) {(void)format;}
static void status(struct session *s,const char *format,...) {
    va_list args;va_start(args,format);
    vsnprintf(s->out->status.message,sizeof(s->out->status.message),format,args);
    va_end(args);
    if(s->progress) s->progress(&s->out->status);
}
static bool fail(struct session *s,const char *message) {
    ++s->out->status.errors;status(s,"%s",message);
    if(s->log) s->log("VMU: %s",message);
    return false;
}
static bool cancelled(struct session *s) {
    if(s->cancel && s->cancel()) {
        s->out->status.stopped=true;status(s,"Stopped; previous backups are preserved");
        return true;
    }
    return false;
}
static bool same_device(struct session *s) {
    maple_device_t *now=maple_enum_dev((int)s->port,(int)s->unit);
    if(now!=s->device || !now || !now->valid || !(now->info.functions&MAPLE_FUNC_MEMCARD))
        return fail(s,"VMU removed or changed; refresh the list");
    return true;
}
/* Upstream's block API has no actual-length output. Differently initialized
 * copies detect short replies, and oversized scratch buffers detect an
 * overlong reply before accepting its first 512 bytes. The protocol response
 * is bounded by one Maple frame (less than 1024 bytes). */
static bool read_block(struct session *s,unsigned block,uint8_t out[BLOCK_BYTES]) {
    _Alignas(32) uint8_t first[1024],second[1024];
    if(block>=256) return fail(s,"Invalid VMU block address");
    if(cancelled(s) || !same_device(s)) return false;
    memset(first,0xa5,sizeof(first));memset(second,0x5a,sizeof(second));
    int result=vmu_block_read(s->device,(uint16_t)block,first);
    if(result!=0) return fail(s,"VMU block read failed");
    if(cancelled(s) || !same_device(s)) return false;
    result=vmu_block_read(s->device,(uint16_t)block,second);
    if(result!=0) return fail(s,"VMU block reread failed");
    if(!same_device(s)) return false;
    for(unsigned i=BLOCK_BYTES;i<sizeof(first);i++)
        if(first[i]!=0xa5 || second[i]!=0x5a) return fail(s,"VMU returned an oversized block");
    if(memcmp(first,second,BLOCK_BYTES)) return fail(s,"VMU block was short or changed while reading");
    memcpy(out,first,BLOCK_BYTES);return true;
}
static bool read_snapshot(struct session *s,struct snapshot *snap) {
    memset(snap,0,sizeof(*snap));
    if(!read_block(s,255,snap->root)) return false;
    for(unsigned i=0;i<16;i++) if(snap->root[i]!=0x55) return fail(s,"VMU is unformatted or its root is damaged");
    unsigned fat=le16(snap->root+70),fat_blocks=le16(snap->root+72);
    snap->directory_start=le16(snap->root+74);snap->directory_blocks=le16(snap->root+76);
    snap->user_blocks=le16(snap->root+80);
    if(fat_blocks!=1 || fat>=255 || !snap->directory_blocks || snap->directory_blocks>DIR_BLOCKS_MAX ||
       snap->directory_start>=255 || snap->directory_blocks>snap->directory_start+1 ||
       !snap->user_blocks || snap->user_blocks>255 || fat<snap->user_blocks ||
       snap->directory_start+1-snap->directory_blocks<snap->user_blocks ||
       (fat<=snap->directory_start && fat>=snap->directory_start+1-snap->directory_blocks))
        return fail(s,"Unsupported or damaged VMU filesystem layout");
    if(!read_block(s,fat,snap->fat)) return false;
    for(unsigned i=0;i<snap->directory_blocks;i++)
        if(!read_block(s,snap->directory_start-i,snap->directory+i*BLOCK_BYTES)) return false;
    bool used[256]={false};
    for(unsigned i=0;i<snap->user_blocks;i++) if(le16(snap->fat+i*2)==0xfffc) ++snap->free_blocks;
    for(unsigned i=0;i<snap->directory_blocks*16;i++) {
        const uint8_t *entry=snap->directory+i*32;
        if(!entry[0]) continue;
        unsigned blocks=le16(entry+24),at=le16(entry+2);
        if((entry[0]!=0x33 && entry[0]!=0xcc) || !blocks || blocks>snap->user_blocks || le16(entry+26)>=blocks)
            return fail(s,"Unsupported or damaged VMU directory entry");
        for(unsigned n=0;n<blocks;n++) {
            if(at>=snap->user_blocks || used[at]) return fail(s,"VMU file chain is out of range, cyclic, or shared");
            used[at]=true;unsigned next=le16(snap->fat+at*2);
            if(n+1==blocks) {
                if(next!=0xfffa) return fail(s,"VMU file length disagrees with its allocation chain");
            } else at=next;
        }
        snap->entries[snap->count++]=(uint16_t)i;
    }
    snap->fingerprint=kui_crc32(0,snap->root,sizeof(snap->root));
    snap->fingerprint=kui_crc32(snap->fingerprint,snap->fat,sizeof(snap->fat));
    snap->fingerprint=kui_crc32(snap->fingerprint,snap->directory,snap->directory_blocks*BLOCK_BYTES);
    return true;
}
static bool locked_snapshot(struct session *s,struct snapshot *snap) {
    if(vmufs_mutex_lock()!=0) return fail(s,"Cannot lock VMU reader");
    bool ok=read_snapshot(s,snap);
    if(vmufs_mutex_unlock()!=0) return fail(s,"Cannot unlock VMU reader");
    return ok;
}
static void display_name(char out[16],const uint8_t *entry) {
    unsigned length=12;
    while(length && (!entry[4+length-1] || entry[4+length-1]==' ')) --length;
    for(unsigned i=0;i<length;i++) out[i]=entry[i+4]>=32 && entry[i+4]<127?(char)entry[i+4]:'_';
    out[length]=0;if(!length) strcpy(out,"Unnamed save");
}
static void filename(char out[32],const uint8_t *entry,unsigned index) {
    char name[16];display_name(name,entry);
    for(unsigned i=0;name[i];i++) {
        unsigned char c=(unsigned char)name[i];
        if(!((c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9') || c=='-' || c=='_')) name[i]='_';
    }
    snprintf(out,32,"%03u_%s",index+1,name);
}
static void fill_view(struct session *s,const struct snapshot *snap,unsigned page) {
    struct kui_vmu_view *out=s->out;
    out->total=snap->count;out->free_blocks=snap->free_blocks;
    unsigned pages=(snap->count+KUI_VMU_ROWS-1)/KUI_VMU_ROWS;
    out->page=pages && page<pages?page:0;
    unsigned start=out->page*KUI_VMU_ROWS;
    for(unsigned i=start;i<snap->count && out->count<KUI_VMU_ROWS;i++) {
        const uint8_t *entry=snap->directory+snap->entries[i]*32;
        struct kui_vmu_entry *row=&out->entries[out->count++];
        display_name(row->name,entry);row->bytes=(uint32_t)le16(entry+24)*BLOCK_BYTES;
    }
}
static bool check_snapshot(struct session *s,const struct snapshot *original,struct snapshot *check) {
    if(!locked_snapshot(s,check)) return false;
    if(check->fingerprint!=original->fingerprint || memcmp(check->root,original->root,BLOCK_BYTES) ||
       memcmp(check->fat,original->fat,BLOCK_BYTES) ||
       memcmp(check->directory,original->directory,original->directory_blocks*BLOCK_BYTES))
        return fail(s,"VMU contents changed; backup stopped, refresh the list");
    return true;
}
static bool read_save(struct session *s,const struct snapshot *snap,const uint8_t *entry,uint8_t *data) {
    if(vmufs_mutex_lock()!=0) return fail(s,"Cannot lock VMU reader");
    unsigned at=le16(entry+2),blocks=le16(entry+24);bool ok=true;
    for(unsigned n=0;n<blocks;n++) {
        if(!read_block(s,at,data+n*BLOCK_BYTES)) {ok=false;break;}
        at=le16(snap->fat+at*2);
    }
    if(vmufs_mutex_unlock()!=0) return fail(s,"Cannot unlock VMU reader");
    return ok;
}
/* Publish only after sync, close, reopen, exact length and every-byte/CRC
 * comparison. A failure leaves only new .part files in the new backup folder.
 * CREATE_NEW and rename prevent overwriting any existing backup. */
static bool save_file(struct session *s,const char *path,const uint8_t *bytes,size_t length) {
    char part[KUI_DEST_PATH_CAP];int n=snprintf(part,sizeof(part),"%s.part",path);
    if(n<0 || n>=(int)sizeof(part)) return fail(s,"VMU backup path is too long");
    if(cancelled(s) || !same_device(s)) return false;
    FIL file;FRESULT r=f_open(&file,part,FA_WRITE|FA_CREATE_NEW);
    if(r!=FR_OK) return fail(s,"Cannot create new SD backup file");
    bool ok=true;size_t at=0;
    while(at<length) {
        if(cancelled(s) || !same_device(s)) {ok=false;break;}
        UINT count=(UINT)(length-at>4096?4096:length-at),written=0;
        r=f_write(&file,bytes+at,count,&written);
        if(r!=FR_OK || written!=count) {ok=fail(s,"SD backup write failed or card is full");break;}
        at+=written;
    }
    if(ok && f_sync(&file)!=FR_OK) ok=fail(s,"SD backup sync failed");
    if(f_close(&file)!=FR_OK) ok=fail(s,"SD backup close failed");
    if(!ok) return false;
    r=f_open(&file,part,FA_READ);
    if(r!=FR_OK) return fail(s,"Cannot reopen SD backup for verification");
    if(f_size(&file)!=length) ok=fail(s,"SD backup verification length mismatch");
    uint8_t buffer[4096];uint32_t crc=0;at=0;
    while(ok && at<length) {
        if(cancelled(s) || !same_device(s)) {ok=false;break;}
        UINT count=(UINT)(length-at>sizeof(buffer)?sizeof(buffer):length-at),got=0;
        r=f_read(&file,buffer,count,&got);
        if(r!=FR_OK || got!=count || memcmp(buffer,bytes+at,count)) {
            ok=fail(s,"SD backup verification read failed or bytes differ");break;
        }
        crc=kui_crc32(crc,buffer,count);at+=count;
    }
    if(f_close(&file)!=FR_OK) ok=fail(s,"SD verification close failed");
    if(ok && crc!=kui_crc32(0,bytes,length)) ok=fail(s,"SD backup CRC32 mismatch");
    if(!ok || cancelled(s) || !same_device(s)) return false;
    if(f_rename(part,path)!=FR_OK) return fail(s,"Cannot publish verified backup file");
    if(s->log) s->log("VMU backup verified %s bytes=%u CRC32=%08" PRIx32,path+2,(unsigned)length,crc);
    return true;
}
static bool new_folder(struct session *s,char folder[KUI_DEST_JOB_CAP]) {
    if(!kui_destination_mkdirs(BACKUP_ROOT,s->log)) return fail(s,"Cannot create VMU backup directory");
    for(unsigned i=1;i<=9999;i++) {
        if(cancelled(s)) return false;
        snprintf(folder,KUI_DEST_JOB_CAP,"0:%s/%c%u-%04u",BACKUP_ROOT,'A'+s->port,s->unit,i);
        FRESULT r=f_mkdir(folder);
        if(r==FR_OK) return true;
        if(r!=FR_EXIST) return fail(s,"Cannot create a new VMU backup folder");
    }
    return fail(s,"VMU backup folder numbers are exhausted");
}
void kui_vmu_app_run(unsigned action,unsigned slot,unsigned page,unsigned selected,
    struct kui_vmu_view *out,kui_log_fn log,kui_cancel_fn cancel,kui_app_progress_fn progress) {
    if(!out) return;
    if(!log) log=quiet_log;
    memset(out,0,sizeof(*out));out->slot=slot;
    struct session s={out,log,cancel,progress,NULL,slot/2,slot%2+1};
    struct snapshot *snap=NULL,*check=NULL;uint8_t *data=NULL;
    bool connected=false,mounted=false,ok=false;FATFS fs;
    if(slot>=8 || action>2) {fail(&s,"Invalid VMU slot or action");goto done;}
    if(cancelled(&s)) goto done;
    s.device=maple_enum_dev((int)s.port,(int)s.unit);
    if(!s.device || !s.device->valid || !(s.device->info.functions&MAPLE_FUNC_MEMCARD)) {
        listed[slot].valid=false;fail(&s,"No VMU in the selected slot");goto done;
    }
    out->present=true;status(&s,"Reading VMU %c%u...",'A'+s.port,s.unit);
    if(action && !listed[slot].valid) {
        fail(&s,"Refresh the selected VMU list before backing up");goto done;
    }
    snap=malloc(sizeof(*snap));
    if(!snap) {fail(&s,"Not enough RAM for VMU directory");goto done;}
    if(!locked_snapshot(&s,snap)) goto done;
    fill_view(&s,snap,page);
    if(!action) {
        listed[slot].device=s.device;listed[slot].fingerprint=snap->fingerprint;listed[slot].valid=true;
        status(&s,"VMU %c%u: %u saves, %u free blocks",'A'+s.port,s.unit,snap->count,snap->free_blocks);
        if(!snap->count) status(&s,"VMU %c%u is empty; %u free blocks",'A'+s.port,s.unit,snap->free_blocks);
        ok=true;goto done;
    }
    if(listed[slot].device!=s.device || listed[slot].fingerprint!=snap->fingerprint) {
        listed[slot].valid=false;fail(&s,"VMU changed since listing; refresh before backup");goto done;
    }
    if(!snap->count) {fail(&s,"The selected VMU has no saves to back up");goto done;}
    if(action==1 && (page!=out->page || selected>=out->count)) {
        fail(&s,"Selected VMU save is no longer available");goto done;
    }
    unsigned first=action==1?page*KUI_VMU_ROWS+selected:0,end=action==1?first+1:snap->count;
    for(unsigned i=first;i<end;i++) out->status.total+=(uint64_t)le16(snap->directory+snap->entries[i]*32+24)*BLOCK_BYTES;
    check=malloc(sizeof(*check));
    if(!check) {fail(&s,"Not enough RAM for VMU verification");goto done;}
    kui_sd_set_params(0,true);
    if(!kui_sd_connect()) {fail(&s,"Cannot connect SD card for VMU backup");goto done;}
    connected=true;
    if(!kui_mount(&fs,log)) {fail(&s,"Cannot mount SD card for VMU backup");goto done;}
    mounted=true;
    char folder[KUI_DEST_JOB_CAP];
    if(!new_folder(&s,folder)) goto done;
    if(log) log("VMU backup: %s (.vms raw payload; .dir original metadata; no VMU writes)",folder+2);
    for(unsigned i=first;i<end;i++) {
        const uint8_t *entry=snap->directory+snap->entries[i]*32;
        size_t bytes=(size_t)le16(entry+24)*BLOCK_BYTES;
        char name[16],base[32],path[KUI_DEST_PATH_CAP];display_name(name,entry);filename(base,entry,i);
        status(&s,"Reading %s (%u/%u)...",name,i-first+1,end-first);
        data=malloc(bytes);
        if(!data) {fail(&s,"Not enough RAM for VMU save payload");goto done;}
        if(!read_save(&s,snap,entry,data) || !check_snapshot(&s,snap,check)) goto done;
        status(&s,"Saving and rereading %s...",name);
        snprintf(path,sizeof(path),"%s/%s.dir",folder,base);
        if(!save_file(&s,path,entry,32)) goto done;
        snprintf(path,sizeof(path),"%s/%s.vms",folder,base);
        if(!save_file(&s,path,data,bytes)) goto done;
        free(data);data=NULL;out->status.done+=bytes;
        status(&s,"Verified %s (%u/%u)",name,i-first+1,end-first);
    }
    if(!check_snapshot(&s,snap,check)) goto done;
    static const uint8_t note[]=
        "K-UI VMU backup completed. Each .vms is a raw, 512-byte-block-padded\n"
        "VMU file payload. Its .dir is the original 32-byte directory entry,\n"
        "not a VMI file. Preserve both files. This is not a whole-card image.\n"
        "All saved files were closed, reopened and compared byte-for-byte\n"
        "with the VMU bytes read, with CRC32 also checked. Restore is not yet\n"
        "implemented. The VMU was read only.\n";
    char marker[KUI_DEST_PATH_CAP];snprintf(marker,sizeof(marker),"%s/complete.txt",folder);
    if(!save_file(&s,marker,note,sizeof(note)-1)) goto done;
    status(&s,"Verified %u saves: %s",end-first,folder+2);ok=true;
done:
    free(data);free(check);free(snap);
    if(mounted && f_mount(NULL,"0:",0)!=FR_OK) {fail(&s,"Cannot unmount SD backup filesystem");ok=false;}
    if(connected) kui_sd_disconnect();
    out->status.complete=ok;out->status.passed=ok;
    if(progress) progress(&out->status);
}
