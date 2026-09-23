/* SPDX-License-Identifier: GPL-3.0-only */
/* Independent VMU adapter. Layout and block API follow upstream
 * KallistiOS fcfa7d869471591ca1c777543261a7bfea7cb726 dc/vmufs.h and
 * dc/maple/vmu.h. Restore uses upstream's locked low-level filesystem APIs.
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
#include <stddef.h>
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
_Static_assert(offsetof(struct snapshot,fat)==BLOCK_BYTES,"VMU snapshot FAT offset");
_Static_assert(offsetof(struct snapshot,directory)==2*BLOCK_BYTES,"VMU snapshot directory offset");
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
static struct {
    char path[KUI_VMU_BACKUP_PATH_CAP];
    maple_device_t *device;
    uint32_t fingerprint,proof_crc;
    unsigned slot;
    bool valid;
} restore_preview;
static struct {
    maple_device_t *source_device,*destination_device;
    uint32_t source_fingerprint,destination_fingerprint,payload_crc;
    unsigned source_slot,destination_slot,page,selected;
    bool valid,copy;
} managed_preview;

static uint16_t le16(const uint8_t *p) {return (uint16_t)(p[0]|((unsigned)p[1]<<8));}
static uint32_t le32(const uint8_t *p) {return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static void put32(uint8_t *p,uint32_t n) {for(unsigned i=0;i<4;i++) p[i]=(uint8_t)(n>>(i*8));}
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
        s->out->status.stopped=true;status(s,"Stopped; existing saves and backups are preserved");
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
        if(entry[28]) return fail(s,"VMU directory contains an invalid dirty flag");
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
        return fail(s,"VMU contents changed; operation stopped, refresh the list");
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
        uint8_t proof[32]={0};memcpy(proof,"KUIVMU1",7);
        put32(proof+8,(uint32_t)bytes);put32(proof+12,kui_crc32(0,entry,32));
        put32(proof+16,kui_crc32(0,data,bytes));put32(proof+28,kui_crc32(0,proof,28));
        snprintf(path,sizeof(path),"%s/%s.crc",folder,base);
        if(!save_file(&s,path,proof,sizeof(proof))) goto done;
        free(data);data=NULL;out->status.done+=bytes;
        status(&s,"Verified %s (%u/%u)",name,i-first+1,end-first);
    }
    if(!check_snapshot(&s,snap,check)) goto done;
    static const uint8_t note[]=
        "K-UI VMU backup completed. Each .vms is a raw, 512-byte-block-padded\n"
        "VMU file payload. Its .dir is the original 32-byte directory entry,\n"
        "not a VMI file. Preserve both files. This is not a whole-card image.\n"
        "All saved files were closed, reopened and compared byte-for-byte\n"
        "with the VMU bytes read, with CRC32 also checked. The .crc proof\n"
        "records protect the metadata and payload for restore. Preserve them.\n"
        "The VMU was read only during this backup.\n";
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

static bool source_path(const char *path,char peer[KUI_VMU_BACKUP_PATH_CAP],const char *ext) {
    static const char prefix[]="0:" BACKUP_ROOT "/";
    if(!path || strncmp(path,prefix,sizeof(prefix)-1)) return false;
    size_t length=strlen(path);
    if(length>=KUI_VMU_BACKUP_PATH_CAP || length<sizeof(prefix)+5 || strcmp(path+length-4,".vms")) return false;
    const char *folder=path+sizeof(prefix)-1,*slash=strchr(folder,'/');
    if(!slash || slash-folder!=7 || path+length-4==slash+1 || folder[0]<'A' || folder[0]>'D' ||
       folder[1]<'1' || folder[1]>'2' || folder[2]!='-') return false;
    for(unsigned i=3;i<7;i++) if(folder[i]<'0' || folder[i]>'9') return false;
    for(const char *p=slash+1;p<path+length-4;p++)
        if(!((*p>='a'&&*p<='z') || (*p>='A'&&*p<='Z') || (*p>='0'&&*p<='9') || *p=='_' || *p=='-')) return false;
    memcpy(peer,path,length-3);memcpy(peer+length-3,ext,4);return true;
}
static bool load_exact(struct session *s,const char *path,void *data,size_t size) {
    FIL file;FRESULT r=f_open(&file,path,FA_READ);
    if(r!=FR_OK) return fail(s,"Backup is incomplete: .vms, .dir and .crc are required");
    bool ok=f_size(&file)==size;
    if(!ok) fail(s,"Backup file length differs from its recorded size");
    size_t at=0;
    while(ok && at<size) {
        if(cancelled(s)) {ok=false;break;}
        UINT amount=(UINT)(size-at>4096?4096:size-at),got=0;
        r=f_read(&file,(uint8_t *)data+at,amount,&got);
        if(r!=FR_OK || got!=amount) {ok=fail(s,"Cannot read complete SD backup");break;}
        at+=got;
    }
    if(f_close(&file)!=FR_OK) ok=fail(s,"Cannot close SD backup");
    return ok;
}
static bool load_metadata(struct session *s,const char *path,uint8_t entry[32],uint8_t proof[32]) {
    char peer[KUI_VMU_BACKUP_PATH_CAP];
    if(!source_path(path,peer,"crc")) return fail(s,"Select a K-UI backup from the backup browser");
    if(!load_exact(s,peer,proof,32)) return false;
    if(memcmp(proof,"KUIVMU1\0",8) || le32(proof+28)!=kui_crc32(0,proof,28) ||
       le32(proof+20) || le32(proof+24)) return fail(s,"Backup CRC record is damaged or unsupported");
    source_path(path,peer,"dir");
    if(!load_exact(s,peer,entry,32)) return false;
    unsigned blocks=le16(entry+24),header=le16(entry+26);
    if(kui_crc32(0,entry,32)!=le32(proof+12) || (entry[0]!=0x33 && entry[0]!=0xcc) ||
       (entry[1]!=0 && entry[1]!=0xff) || !blocks || blocks>255 || header>=blocks ||
       entry[28] || le32(proof+8)!=blocks*BLOCK_BYTES)
        return fail(s,"Backup metadata or its CRC is invalid");
    if(!entry[4]) return fail(s,"Backup has no VMU filename");
    return true;
}
void kui_vmu_backups_run(unsigned page,struct kui_vmu_backup_view *out,
    kui_log_fn log,kui_cancel_fn cancel,kui_app_progress_fn progress) {
    if(!out) return;
    memset(out,0,sizeof(*out));out->page=page;
    struct kui_vmu_view scratch={0};struct session s={&scratch,log?log:quiet_log,cancel,progress,NULL,0,0};
    FATFS fs;bool connected=false,mounted=false,ok=false,root_open=false,sub_open=false;
    DIR root,sub;FILINFO folder,file;
    if(page>UINT32_MAX/KUI_VMU_ROWS) {fail(&s,"Invalid backup page");goto done;}
    if(cancelled(&s)) goto done;
    kui_sd_set_params(0,true);
    if(!kui_sd_connect()) {fail(&s,"Cannot connect SD card for VMU backups");goto done;}
    connected=true;
    if(!kui_mount(&fs,s.log)) {fail(&s,"Cannot mount SD card for VMU backups");goto done;}
    mounted=true;FRESULT r=f_opendir(&root,"0:" BACKUP_ROOT);
    if(r==FR_NO_PATH || r==FR_NO_FILE) {status(&s,"No backups yet; back up a VMU save first");ok=true;goto done;}
    if(r!=FR_OK) {fail(&s,"Cannot open VMU backup folder");goto done;}
    root_open=true;status(&s,"Reading verified VMU backups...");
    while(true) {
        if(cancelled(&s)) goto done;
        r=f_readdir(&root,&folder);
        if(r!=FR_OK) {fail(&s,"Cannot read VMU backup folders");goto done;}
        if(!folder.fname[0]) break;
        if(!(folder.fattrib&AM_DIR) || strlen(folder.fname)!=7) continue;
        char directory[KUI_VMU_BACKUP_PATH_CAP];
        int n=snprintf(directory,sizeof(directory),"0:" BACKUP_ROOT "/%s",folder.fname);
        if(n<0 || n>=(int)sizeof(directory)) continue;
        if(f_opendir(&sub,directory)!=FR_OK) {fail(&s,"Cannot open saved VMU backup");goto done;}
        sub_open=true;
        while(true) {
            if(cancelled(&s)) goto done;
            r=f_readdir(&sub,&file);
            if(r!=FR_OK) {fail(&s,"Cannot read VMU backup files");goto done;}
            if(!file.fname[0]) break;
            size_t length=strlen(file.fname);
            if((file.fattrib&AM_DIR) || length<5 || strcmp(file.fname+length-4,".vms")) continue;
            char path[KUI_VMU_BACKUP_PATH_CAP],peer[KUI_VMU_BACKUP_PATH_CAP];
            n=snprintf(path,sizeof(path),"%s/%s",directory,file.fname);
            if(n<0 || n>=(int)sizeof(path) || !source_path(path,peer,"crc")) continue;
            FILINFO proof_info;FRESULT proof_result=f_stat(peer,&proof_info);
            if(proof_result==FR_NO_FILE || proof_result==FR_NO_PATH) continue; /* Legacy backup: no restore proof. */
            if(proof_result!=FR_OK) {fail(&s,"Cannot inspect backup CRC record");goto done;}
            if(out->total>=page*KUI_VMU_ROWS && out->count<KUI_VMU_ROWS) {
                struct kui_vmu_backup_entry *row=&out->entries[out->count++];
                /* Full validation is performed at preview. A damaged source remains
                 * visible and gets an explicit error rather than disappearing. */
                snprintf(row->name,sizeof(row->name),"%.15s",file.fname);
                snprintf(row->folder,sizeof(row->folder),"%.15s",folder.fname);
                memcpy(row->path,path,strlen(path)+1);row->bytes=(uint32_t)file.fsize;
            }
            ++out->total;
        }
        if(f_closedir(&sub)!=FR_OK) {sub_open=false;fail(&s,"Cannot close backup directory");goto done;}
        sub_open=false;
    }
    status(&s,out->total?"%u restore candidates; select one to verify":"No restorable backups; make a new backup with CRC proof",out->total);ok=true;
done:
    if(sub_open && f_closedir(&sub)!=FR_OK) {fail(&s,"Cannot close backup directory");ok=false;}
    if(root_open && f_closedir(&root)!=FR_OK) {fail(&s,"Cannot close backup directory");ok=false;}
    if(mounted && f_mount(NULL,"0:",0)!=FR_OK) {fail(&s,"Cannot unmount SD backup filesystem");ok=false;}
    if(connected) kui_sd_disconnect();
    scratch.status.complete=ok;scratch.status.passed=ok;out->status=scratch.status;
    if(progress) progress(&out->status);
}
static bool fits_destination(struct session *s,const struct snapshot *snap,const uint8_t entry[32]) {
    if(le16(entry+24)>snap->free_blocks) return fail(s,"Not enough free blocks; existing saves will not be removed");
    if(snap->count>=snap->directory_blocks*16) return fail(s,"VMU directory is full");
    unsigned owned=0,allocated=0;
    for(unsigned i=0;i<snap->user_blocks;i++) if(le16(snap->fat+i*2)!=0xfffc) ++allocated;
    for(unsigned i=0;i<snap->count;i++) {
        const uint8_t *old=snap->directory+snap->entries[i]*32;
        owned+=le16(old+24);
        if(!strncmp((const char *)old+4,(const char *)entry+4,12))
            return fail(s,"That save already exists; choose another VMU (no overwrite)");
        for(unsigned j=0;j<i;j++)
            if(!strncmp((const char *)old+4,(const char *)snap->directory+snap->entries[j]*32+4,12))
                return fail(s,"Destination has duplicate VMU names; restore refused");
    }
    if(owned!=allocated) return fail(s,"Destination has unowned allocated blocks; restore refused");
    return true;
}
/* Uses upstream metadata commit primitives and data-down/game-up free-block
 * selection. Each payload write checks device identity; the upstream whole-file
 * writer lacks that boundary. Preserves metadata and verifies free-block data
 * before publishing FAT/directory. No rollback is attempted after metadata
 * failure: a torn flash write cannot safely be undone by assuming its result. */
static bool restore_write(struct session *s,const struct snapshot *original,struct snapshot *check,
                          const uint8_t entry[32],uint8_t *data) {
    vmu_root_t root;vmu_dir_t added;uint16_t fat[256];
    vmu_dir_t *dir=malloc(original->directory_blocks*BLOCK_BYTES);
    if(!dir) return fail(s,"Not enough RAM for VMU restore directory");
    _Static_assert(sizeof(root)==BLOCK_BYTES,"VMU root layout");
    _Static_assert(sizeof(added)==32,"VMU directory layout");
    memcpy(&root,original->root,sizeof(root));memcpy(&added,entry,sizeof(added));
    memcpy(fat,original->fat,sizeof(fat));memcpy(dir,original->directory,original->directory_blocks*BLOCK_BYTES);
    bool ok=false,locked=false,metadata_started=false;
    if(vmufs_mutex_lock()!=0) {fail(s,"Cannot lock VMU restore");goto done;}
    locked=true;
    if(!read_snapshot(s,check) || check->fingerprint!=original->fingerprint) {
        if(!s->out->status.errors && !s->out->status.stopped) fail(s,"VMU changed before restore; preview again");
        goto done;
    }
    if(cancelled(s) || !same_device(s)) goto done;
    status(s,"Writing new save to free VMU blocks...");
    unsigned free_entry=0;
    while(free_entry<root.dir_size*16u && dir[free_entry].filetype) ++free_entry;
    if(free_entry>=root.dir_size*16u) {fail(s,"VMU directory became full");goto done;}
    uint16_t selected[256];unsigned found=0;
    for(unsigned i=0;i<root.blk_cnt && found<added.filesize;i++) {
        unsigned block=added.filetype==0xcc?i:root.blk_cnt-1-i;
        if(fat[block]==0xfffc) selected[found++]=(uint16_t)block;
    }
    if(found!=added.filesize) {fail(s,"VMU free-block count changed");goto done;}
    added.firstblk=selected[0];
    for(unsigned i=0;i<found;i++) {
        if(cancelled(s) || !same_device(s)) goto done;
        if(vmu_block_write(s->device,selected[i],data+i*BLOCK_BYTES)!=0) {
            fail(s,"VMU data write failed; no new FAT or directory was published");goto done;
        }
        if(cancelled(s) || !same_device(s)) goto done;
        fat[selected[i]]=i+1<found?selected[i+1]:0xfffa;
    }
    dir[free_entry]=added;dir[free_entry].dirty=1;
    unsigned at=added.firstblk;uint8_t bytes[BLOCK_BYTES];
    for(unsigned i=0;i<added.filesize;i++) {
        if(!read_block(s,at,bytes)) goto done;
        if(memcmp(bytes,data+i*BLOCK_BYTES,BLOCK_BYTES)) {fail(s,"VMU data readback mismatch; save was not published");goto done;}
        at=fat[at];
    }
    if(cancelled(s) || !same_device(s)) goto done;
    status(s,"Committing VMU save; keep the VMU connected...");
    /* The final commit and check must finish even if Stop is pressed. */
    kui_cancel_fn previous_cancel=s->cancel;s->cancel=NULL;metadata_started=true;
    if(vmufs_fat_write(s->device,&root,fat)!=0 || !same_device(s)) {
        fail(s,"VMU FAT commit failed; card is unverified, retain the SD backup");goto committed_done;
    }
    if(!read_block(s,root.fat_loc,bytes) || memcmp(bytes,fat,BLOCK_BYTES)) {
        fail(s,"VMU FAT readback failed; card is unverified, retain the SD backup");goto committed_done;
    }
    if(vmufs_dir_write(s->device,&root,dir)!=0 || !same_device(s)) {
        fail(s,"VMU directory commit failed; card is unverified, retain the SD backup");goto committed_done;
    }
    if(!read_snapshot(s,check) || memcmp(check->root,original->root,BLOCK_BYTES) ||
       memcmp(check->fat,fat,BLOCK_BYTES) || memcmp(check->directory,dir,original->directory_blocks*BLOCK_BYTES)) {
        fail(s,"VMU metadata readback differs; restore is not verified");goto committed_done;
    }
    at=added.firstblk;
    for(unsigned i=0;i<added.filesize;i++) {
        if(!read_block(s,at,bytes) || memcmp(bytes,data+i*BLOCK_BYTES,BLOCK_BYTES)) {
            fail(s,"Final VMU payload readback failed; restore is not verified");goto committed_done;
        }
        at=fat[at];s->out->status.done+=(uint64_t)BLOCK_BYTES;
    }
    ok=true;
committed_done:
    s->cancel=previous_cancel;
done:
    if(locked && vmufs_mutex_unlock()!=0) {fail(s,"Cannot unlock VMU restore");ok=false;}
    if(metadata_started && !ok && s->log) s->log("VMU RESTORE UNVERIFIED: metadata commit began; inspect the card, do not automatically retry");
    free(dir);return ok;
}
void kui_vmu_restore_run(const char *path,unsigned slot,bool commit,
    struct kui_vmu_view *out,kui_log_fn log,kui_cancel_fn cancel,kui_app_progress_fn progress) {
    if(!out) return;
    memset(out,0,sizeof(*out));out->slot=slot;
    struct session s={out,log?log:quiet_log,cancel,progress,NULL,slot/2,slot%2+1};
    struct snapshot *snap=NULL,*check=NULL;uint8_t *data=NULL,entry[32],proof[32];
    bool connected=false,mounted=false,ok=false;FATFS fs;
    if(!commit) restore_preview.valid=false;
    if(slot>=8 || !path || strlen(path)>=KUI_VMU_BACKUP_PATH_CAP) {fail(&s,"Invalid restore destination or backup path");goto done;}
    if(commit && (!restore_preview.valid || restore_preview.slot!=slot || strcmp(path,restore_preview.path))) {
        fail(&s,"Preview this backup and destination before confirming restore");goto done;
    }
    if(cancelled(&s)) goto done;
    s.device=maple_enum_dev((int)s.port,(int)s.unit);
    if(!s.device || !s.device->valid || !(s.device->info.functions&MAPLE_FUNC_MEMCARD)) {fail(&s,"No VMU in the selected destination slot");goto done;}
    out->present=true;kui_sd_set_params(0,true);
    if(!kui_sd_connect()) {fail(&s,"Cannot connect SD card for VMU restore");goto done;}
    connected=true;
    if(!kui_mount(&fs,s.log)) {fail(&s,"Cannot mount SD card for VMU restore");goto done;}
    mounted=true;
    if(!load_metadata(&s,path,entry,proof)) goto done;
    size_t length=le32(proof+8);data=malloc(length);snap=malloc(sizeof(*snap));check=malloc(sizeof(*check));
    if(!data || !snap || !check) {fail(&s,"Not enough RAM for VMU restore verification");goto done;}
    status(&s,"Verifying complete SD backup...");
    if(!load_exact(&s,path,data,length)) goto done;
    if(kui_crc32(0,data,length)!=le32(proof+16)) {fail(&s,"Backup payload CRC32 mismatch; no VMU writes");goto done;}
    if(!locked_snapshot(&s,snap) || !fits_destination(&s,snap,entry)) goto done;
    out->free_blocks=snap->free_blocks;out->status.total=length;
    display_name(out->entries[0].name,entry);out->entries[0].bytes=(uint32_t)length;out->count=1;
    if(!commit) {
        memcpy(restore_preview.path,path,strlen(path)+1);restore_preview.device=s.device;
        restore_preview.fingerprint=snap->fingerprint;restore_preview.proof_crc=le32(proof+28);
        restore_preview.slot=slot;restore_preview.valid=true;out->restore_ready=true;
        status(&s,"Restore %s: %u blocks to %c%u? Existing saves stay.",out->entries[0].name,(unsigned)(length/BLOCK_BYTES),'A'+s.port,s.unit);
        ok=true;goto done;
    }
    bool matches=restore_preview.device==s.device && restore_preview.fingerprint==snap->fingerprint &&
        restore_preview.proof_crc==le32(proof+28);
    restore_preview.valid=false;
    if(!matches) {fail(&s,"Backup or VMU changed since preview; preview again");goto done;}
    /* Save exact destination metadata before touching free blocks. This does
     * not claim power-fail atomicity, and is never replayed automatically. */
    char folder[KUI_DEST_JOB_CAP],metadata[KUI_DEST_PATH_CAP];
    if(!new_folder(&s,folder)) goto done;
    snprintf(metadata,sizeof(metadata),"%s/before-restore.bin",folder);
    if(!save_file(&s,metadata,(const uint8_t *)snap,2*BLOCK_BYTES+snap->directory_blocks*BLOCK_BYTES)) goto done;
    s.log("VMU restore source=%s destination=%c%u metadata backup=%s",path+2,'A'+s.port,s.unit,metadata+2);
    listed[slot].valid=false;
    if(!restore_write(&s,snap,check,entry,data)) goto done;
    out->free_blocks=check->free_blocks;
    status(&s,"Restored %s to %c%u; every byte verified",out->entries[0].name,'A'+s.port,s.unit);
    s.log("VMU restore verified name=%s bytes=%u CRC32=%08" PRIx32,out->entries[0].name,(unsigned)length,le32(proof+16));ok=true;
done:
    if(!ok) restore_preview.valid=false;
    free(data);free(snap);free(check);
    if(mounted && f_mount(NULL,"0:",0)!=FR_OK) {fail(&s,"Cannot unmount SD backup filesystem");ok=false;out->restore_ready=false;restore_preview.valid=false;}
    if(connected) kui_sd_disconnect();
    out->status.complete=ok;out->status.passed=ok;
    if(progress) progress(&out->status);
}

/* A managed deletion/copy must not make damaged metadata harder to diagnose.
 * read_snapshot has already checked chain ranges, lengths and shared blocks. */
static bool healthy_metadata(struct session *s,const struct snapshot *snap) {
    unsigned owned=0,allocated=0;
    for(unsigned i=0;i<snap->user_blocks;i++) if(le16(snap->fat+i*2)!=0xfffc) ++allocated;
    for(unsigned i=0;i<snap->count;i++) {
        const uint8_t *entry=snap->directory+snap->entries[i]*32;
        owned+=le16(entry+24);
        for(unsigned j=0;j<i;j++)
            if(!strncmp((const char *)entry+4,(const char *)snap->directory+snap->entries[j]*32+4,12))
                return fail(s,"VMU has duplicate save names; no managed write is allowed");
    }
    return owned==allocated || fail(s,"VMU has unowned allocated blocks; no managed write is allowed");
}
static bool publish_save(struct session *s,const char *folder,const uint8_t entry[32],
                         const uint8_t *data,unsigned index) {
    char base[32],path[KUI_DEST_PATH_CAP];filename(base,entry,index);
    uint32_t length=(uint32_t)le16(entry+24)*BLOCK_BYTES;
    snprintf(path,sizeof(path),"%s/%s.dir",folder,base);
    if(!save_file(s,path,entry,32)) return false;
    snprintf(path,sizeof(path),"%s/%s.vms",folder,base);
    if(!save_file(s,path,data,length)) return false;
    if(strlen(path)>=sizeof(s->out->backup_path)) return fail(s,"Managed backup path is too long");
    char backup[KUI_VMU_BACKUP_PATH_CAP];strcpy(backup,path);
    uint8_t proof[32]={0};memcpy(proof,"KUIVMU1",7);
    put32(proof+8,length);put32(proof+12,kui_crc32(0,entry,32));
    put32(proof+16,kui_crc32(0,data,length));put32(proof+28,kui_crc32(0,proof,28));
    snprintf(path,sizeof(path),"%s/%s.crc",folder,base);
    if(!save_file(s,path,proof,sizeof(proof))) return false;
    strcpy(s->out->backup_path,backup);return true;
}
/* Remove the directory entry before freeing the chain. A failed/torn FAT write
 * then cannot cause a live entry to point at blocks marked free. A failed
 * directory write may affect neighboring entries: never retry it or report
 * success. Both exact metadata and restorable payload are already saved on SD. */
static bool delete_write(struct session *s,const struct snapshot *original,
                         struct snapshot *check,unsigned entry_index) {
    vmu_root_t root;uint16_t fat[256];uint8_t bytes[BLOCK_BYTES];
    vmu_dir_t *dir=malloc(original->directory_blocks*BLOCK_BYTES);
    if(!dir) return fail(s,"Not enough RAM for VMU delete metadata");
    memcpy(&root,original->root,sizeof(root));memcpy(fat,original->fat,sizeof(fat));
    memcpy(dir,original->directory,original->directory_blocks*BLOCK_BYTES);
    bool ok=false,locked=false,metadata_started=false;kui_cancel_fn old_cancel=s->cancel;
    if(vmufs_mutex_lock()!=0) {fail(s,"Cannot lock VMU deletion");goto done;}
    locked=true;
    if(!read_snapshot(s,check) || check->fingerprint!=original->fingerprint ||
       memcmp(check->root,original->root,BLOCK_BYTES) || memcmp(check->fat,original->fat,BLOCK_BYTES) ||
       memcmp(check->directory,original->directory,original->directory_blocks*BLOCK_BYTES)) {
        if(!s->out->status.errors && !s->out->status.stopped) fail(s,"VMU changed before deletion; preview again");
        goto done;
    }
    unsigned at=dir[entry_index].firstblk,blocks=dir[entry_index].filesize;
    for(unsigned i=0;i<blocks;i++) {unsigned next=fat[at];fat[at]=0xfffc;at=next;}
    memset(&dir[entry_index],0,sizeof(dir[entry_index]));dir[entry_index].dirty=1;
    if(cancelled(s) || !same_device(s)) goto done;
    status(s,"Deleting backed-up save; keep the VMU connected...");
    s->cancel=NULL;metadata_started=true;
    if(vmufs_dir_write(s->device,&root,dir)!=0 || !same_device(s)) {
        fail(s,"VMU delete directory commit failed; card is unverified, retain the SD backup");goto done;
    }
    /* Inspect every directory block before releasing any old allocation. */
    for(unsigned i=0;i<root.dir_size;i++)
        if(!read_block(s,root.dir_loc-i,bytes) || memcmp(bytes,dir+i*16,BLOCK_BYTES)) {
            fail(s,"VMU delete directory readback differs; blocks were not released");goto done;
        }
    if(vmufs_fat_write(s->device,&root,fat)!=0 || !same_device(s)) {
        fail(s,"VMU delete FAT commit failed; card is unverified, retain the SD backup");goto done;
    }
    if(!read_snapshot(s,check) || memcmp(check->root,original->root,BLOCK_BYTES) ||
       memcmp(check->fat,fat,BLOCK_BYTES) || memcmp(check->directory,dir,original->directory_blocks*BLOCK_BYTES)) {
        fail(s,"VMU delete metadata readback differs; card is unverified");goto done;
    }
    s->out->status.done=s->out->status.total;ok=true;
done:
    s->cancel=old_cancel;
    if(locked && vmufs_mutex_unlock()!=0) {fail(s,"Cannot unlock VMU deletion");ok=false;}
    if(metadata_started && !ok) s->log("VMU DELETE UNVERIFIED: metadata commit began; retain backup and inspect card; do not automatically retry");
    free(dir);return ok;
}
static void managed_run(bool copy,unsigned source_slot,unsigned page,unsigned selected,unsigned destination_slot,
                        bool commit,struct kui_vmu_view *out,kui_log_fn log,kui_cancel_fn cancel,kui_app_progress_fn progress) {
    if(!out) return;
    memset(out,0,sizeof(*out));out->slot=copy?destination_slot:source_slot;
    struct session source={out,log?log:quiet_log,cancel,progress,NULL,source_slot/2,source_slot%2+1};
    struct session target={out,log?log:quiet_log,cancel,progress,NULL,destination_slot/2,destination_slot%2+1};
    struct snapshot *snap=NULL,*check=NULL,*destination=NULL;uint8_t *data=NULL;
    bool connected=false,mounted=false,ok=false;FATFS fs;
    if(!commit) managed_preview.valid=false;
    if(source_slot>=8 || destination_slot>=8 || (copy && source_slot==destination_slot)) {
        fail(&source,"Choose valid, different VMU source and destination slots");goto done;
    }
    if(commit && (!managed_preview.valid || managed_preview.copy!=copy ||
       managed_preview.source_slot!=source_slot || managed_preview.destination_slot!=destination_slot ||
       managed_preview.page!=page || managed_preview.selected!=selected)) {
        fail(&source,"Preview the selected save and destination before confirming");goto done;
    }
    if(cancelled(&source)) goto done;
    source.device=maple_enum_dev((int)source.port,(int)source.unit);
    if(!source.device || !source.device->valid || !(source.device->info.functions&MAPLE_FUNC_MEMCARD)) {
        fail(&source,"No VMU in the selected source slot");goto done;
    }
    if(!listed[source_slot].valid || listed[source_slot].device!=source.device) {
        fail(&source,"Refresh the source VMU list before selecting this save");goto done;
    }
    snap=malloc(sizeof(*snap));check=malloc(sizeof(*check));
    if(!snap || !check) {fail(&source,"Not enough RAM for VMU managed operation");goto done;}
    if(!locked_snapshot(&source,snap) || !healthy_metadata(&source,snap)) goto done;
    if(snap->fingerprint!=listed[source_slot].fingerprint) {
        listed[source_slot].valid=false;fail(&source,"Source VMU changed since listing; refresh the list");goto done;
    }
    if(page>UINT32_MAX/KUI_VMU_ROWS || selected>=KUI_VMU_ROWS ||
       (uint64_t)page*KUI_VMU_ROWS+selected>=snap->count) {
        fail(&source,"Selected VMU save is no longer available");goto done;
    }
    unsigned index=page*KUI_VMU_ROWS+selected,entry_index=snap->entries[index];
    const uint8_t *entry=snap->directory+entry_index*32;
    if(!entry[4] || (entry[1]!=0 && entry[1]!=0xff)) {
        fail(&source,"Save metadata cannot be restored by K-UI; managed operation refused");goto done;
    }
    size_t length=(size_t)le16(entry+24)*BLOCK_BYTES;
    data=malloc(length);if(!data) {fail(&source,"Not enough RAM for VMU save verification");goto done;}
    if(!read_save(&source,snap,entry,data) || !check_snapshot(&source,snap,check)) goto done;
    uint32_t crc=kui_crc32(0,data,length);
    if(copy) {
        target.device=maple_enum_dev((int)target.port,(int)target.unit);
        if(!target.device || target.device==source.device || !target.device->valid || !(target.device->info.functions&MAPLE_FUNC_MEMCARD)) {
            fail(&target,"No different VMU in the selected destination slot");goto done;
        }
        destination=malloc(sizeof(*destination));
        if(!destination) {fail(&target,"Not enough RAM for destination VMU metadata");goto done;}
        if(!locked_snapshot(&target,destination) || !fits_destination(&target,destination,entry)) goto done;
    }
    out->present=true;out->count=1;out->total=copy?destination->count:snap->count;
    out->free_blocks=copy?destination->free_blocks:snap->free_blocks;
    display_name(out->entries[0].name,entry);out->entries[0].bytes=(uint32_t)length;out->status.total=length;
    if(!commit) {
        managed_preview.source_device=source.device;managed_preview.destination_device=copy?target.device:source.device;
        managed_preview.source_fingerprint=snap->fingerprint;
        managed_preview.destination_fingerprint=copy?destination->fingerprint:snap->fingerprint;
        managed_preview.payload_crc=crc;managed_preview.source_slot=source_slot;managed_preview.destination_slot=destination_slot;
        managed_preview.page=page;managed_preview.selected=selected;managed_preview.copy=copy;managed_preview.valid=true;
        out->copy_ready=copy;out->delete_ready=!copy;
        if(copy) status(&source,"Copy %s: %u blocks, %c%u to %c%u? Source stays.",out->entries[0].name,(unsigned)(length/BLOCK_BYTES),
            'A'+source.port,source.unit,'A'+target.port,target.unit);
        else status(&source,"Back up then delete %s: %u blocks on %c%u?",out->entries[0].name,(unsigned)(length/BLOCK_BYTES),'A'+source.port,source.unit);
        ok=true;goto done;
    }
    bool matches=managed_preview.source_device==source.device && managed_preview.source_fingerprint==snap->fingerprint &&
        managed_preview.payload_crc==crc && (!copy || (managed_preview.destination_device==target.device &&
        managed_preview.destination_fingerprint==destination->fingerprint));
    managed_preview.valid=false;
    if(!matches) {fail(&source,"VMU or save bytes changed since preview; preview again");goto done;}
    kui_sd_set_params(0,true);
    if(!kui_sd_connect()) {fail(&source,"Cannot connect SD; VMU managed writes require a verified backup");goto done;}
    connected=true;
    if(!kui_mount(&fs,source.log)) {fail(&source,"Cannot mount SD for VMU safety backup");goto done;}
    mounted=true;
    char folder[KUI_DEST_JOB_CAP],metadata[KUI_DEST_PATH_CAP];
    if(!new_folder(&source,folder) || !publish_save(&source,folder,entry,data,index)) goto done;
    /* Also pass the published record through the normal restore parser. A
     * backup that the restore workflow cannot open must never permit delete. */
    uint8_t backup_entry[32],backup_proof[32];
    if(!load_metadata(&source,out->backup_path,backup_entry,backup_proof) ||
       memcmp(backup_entry,entry,32) || le32(backup_proof+16)!=crc) {
        if(!out->status.errors && !out->status.stopped) fail(&source,"Published backup is not restorable; VMU unchanged");
        goto done;
    }
    snprintf(metadata,sizeof(metadata),"%s/before-%s.bin",folder,copy?"copy-source":"delete");
    if(!save_file(&source,metadata,(const uint8_t *)snap,2*BLOCK_BYTES+snap->directory_blocks*BLOCK_BYTES)) goto done;
    if(!check_snapshot(&source,snap,check)) goto done;
    source.log("VMU %s backup=%s source=%c%u name=%s bytes=%u CRC32=%08" PRIx32,
        copy?"copy":"delete",out->backup_path+2,'A'+source.port,source.unit,out->entries[0].name,(unsigned)length,crc);
    if(copy) {
        snprintf(metadata,sizeof(metadata),"%s/before-copy-destination.bin",folder);
        if(!save_file(&target,metadata,(const uint8_t *)destination,2*BLOCK_BYTES+destination->directory_blocks*BLOCK_BYTES)) goto done;
        listed[destination_slot].valid=false;
        if(!restore_write(&target,destination,check,entry,data)) goto done;
        out->free_blocks=check->free_blocks;
        status(&source,"Copied %s to %c%u; every byte verified",out->entries[0].name,'A'+target.port,target.unit);
        source.log("VMU copy verified destination=%c%u; source and SD backup retained",'A'+target.port,target.unit);
    } else {
        listed[source_slot].valid=false;
        if(!delete_write(&source,snap,check,entry_index)) goto done;
        out->free_blocks=check->free_blocks;
        status(&source,"Deleted %s from %c%u; verified SD backup retained",out->entries[0].name,'A'+source.port,source.unit);
        source.log("VMU delete verified: %u blocks freed, restore source=%s",(unsigned)(length/BLOCK_BYTES),out->backup_path+2);
    }
    ok=true;
done:
    if(!ok) managed_preview.valid=false;
    free(data);free(snap);free(check);free(destination);
    if(mounted && f_mount(NULL,"0:",0)!=FR_OK) {
        fail(&source,"Cannot unmount SD backup filesystem");ok=false;managed_preview.valid=false;
    }
    if(connected) kui_sd_disconnect();
    if(!ok) out->copy_ready=out->delete_ready=false;
    out->status.complete=ok;out->status.passed=ok;
    if(progress) progress(&out->status);
}
void kui_vmu_delete_run(unsigned slot,unsigned page,unsigned selected,bool commit,
    struct kui_vmu_view *out,kui_log_fn log,kui_cancel_fn cancel,kui_app_progress_fn progress) {
    managed_run(false,slot,page,selected,slot,commit,out,log,cancel,progress);
}
void kui_vmu_copy_run(unsigned source_slot,unsigned page,unsigned selected,unsigned destination_slot,bool commit,
    struct kui_vmu_view *out,kui_log_fn log,kui_cancel_fn cancel,kui_app_progress_fn progress) {
    managed_run(true,source_slot,page,selected,destination_slot,commit,out,log,cancel,progress);
}
