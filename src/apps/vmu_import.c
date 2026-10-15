/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_import.h"
#include "kui/files.h"
#include "kui/hash.h"
#include "platform.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct session {struct kui_vmu_view *out;kui_log_fn log;kui_cancel_fn cancel;kui_app_progress_fn progress;};
struct preview {char path[KUI_FILES_PATH_CAP];uint8_t digest[32];bool valid;};
static struct preview pinned;
static void quiet_log(const char *format,...){(void)format;}
static bool fail(struct session *s,const char *text){s->out->status.errors++;snprintf(s->out->status.message,sizeof(s->out->status.message),"%s",text);if(s->log)s->log("VMU import: %s",text);return false;}
static bool stopped(struct session *s){if(s->cancel&&s->cancel()){s->out->status.stopped=true;snprintf(s->out->status.message,sizeof(s->out->status.message),"Import stopped; source files unchanged");return true;}return false;}
static void notify(struct session *s,const char *text){snprintf(s->out->status.message,sizeof(s->out->status.message),"%s",text);if(s->progress)s->progress(&s->out->status);}
static unsigned get16(const uint8_t *p){return p[0]|((unsigned)p[1]<<8);}
static unsigned unbcd(uint8_t n){return (n>>4)*10+(n&15);}
static bool extension(const char *path,const char *ext){size_t n=strlen(path);if(n<4||path[n-4]!='.')return false;for(unsigned i=0;i<3;i++){unsigned c=(unsigned char)path[n-3+i];if(c>='A'&&c<='Z')c+=32;if(c!=(unsigned char)ext[i])return false;}return true;}
static bool normalize(const char *path,char normalized[KUI_FILES_PATH_CAP]){
    if(path&&path[0]=='0'&&path[1]==':')path+=2;
    if(!kui_files_path_valid(path)||!path[1])return false;
    memcpy(normalized,path,strlen(path)+1);return true;
}
/* The second independent open/reread must agree byte-for-byte, as well as in
 * SHA256. Metadata and exact length must remain unchanged around both reads. */
static bool read_source(struct session *s,const char *path,uint8_t *data,size_t capacity,size_t *length,struct kui_sha256 *combined){
    char local[KUI_FILES_PATH_CAP+2];snprintf(local,sizeof(local),"0:%s",path);
    FILINFO before,after;if(f_stat(local,&before)!=FR_OK||(before.fattrib&AM_DIR)||!before.fsize||before.fsize>capacity)return fail(s,"Source is missing, a folder or outside the import size limit");
    size_t size=(size_t)before.fsize;uint8_t first[32],check[4096];
    s->out->status.total+=(uint64_t)size*2;
    for(unsigned pass=0;pass<2;pass++){
        FIL file;if(stopped(s))return false;if(f_open(&file,local,FA_READ)!=FR_OK)return fail(s,"Cannot open import source read-only");
        bool ok=true;struct kui_sha256 hash;kui_sha256_init(&hash);
        if(f_size(&file)!=size)ok=fail(s,"Import source length changed");
        for(size_t at=0;ok&&at<size;){
            if(stopped(s)){ok=false;break;}UINT count=(UINT)(size-at>sizeof(check)?sizeof(check):size-at),got=0;
            uint8_t *target=pass?check:data+at;
            if(f_read(&file,target,count,&got)!=FR_OK||got!=count){ok=fail(s,"Cannot read complete import source");break;}
            kui_sha256_update(&hash,target,count);
            if(pass&&memcmp(check,data+at,count)){ok=fail(s,"Import source changed between reads");break;}
            at+=count;s->out->status.done+=count;if(s->progress)s->progress(&s->out->status);
        }
        if(f_close(&file)!=FR_OK)ok=fail(s,"Cannot close import source");
        if(!ok)return false;
        uint8_t digest[32];kui_sha256_digest(&hash,digest);if(!pass)memcpy(first,digest,32);else if(memcmp(first,digest,32))return fail(s,"Import source checksum changed");
        if(f_stat(local,&after)!=FR_OK||before.fsize!=after.fsize||before.fdate!=after.fdate||before.ftime!=after.ftime||before.fattrib!=after.fattrib)return fail(s,"Import source metadata changed during reads");
    }
    uint8_t size_bytes[8];for(unsigned i=0;i<8;i++)size_bytes[i]=(uint8_t)((uint64_t)size>>(i*8));
    kui_sha256_update(combined,path,strlen(path)+1);kui_sha256_update(combined,size_bytes,sizeof(size_bytes));kui_sha256_update(combined,data,size);
    *length=size;return true;
}
static void describe(struct session *s,enum kui_vmu_save_format format,const struct kui_vmu_save_info *info){
    struct kui_vmu_view *out=s->out;const uint8_t *e=info->entry;char hex[25];kui_hex(e+4,12,hex);
    out->count=out->total=1;out->entries[0].bytes=(uint32_t)info->padded_bytes;
    for(unsigned i=0;i<12&&e[4+i];i++)out->entries[0].name[i]=(e[4+i]>=32&&e[4+i]<127)?(char)e[4+i]:'?';
    out->status.line_count=9;
    snprintf(out->status.lines[0],KUI_APP_LINE_CAP,"Source: %s; read-only input",format==KUI_VMU_SAVE_DCI?"DCI":format==KUI_VMU_SAVE_VMI?"VMI + VMS":"VMS");
    snprintf(out->status.lines[1],KUI_APP_LINE_CAP,"VMU name bytes: %s",hex);
    snprintf(out->status.lines[2],KUI_APP_LINE_CAP,"Name from %s",info->name_from_filename?"SD filename (not a name in the VMS header)":format==KUI_VMU_SAVE_DCI?"original DCI directory entry":"VMI internal filename field");
    snprintf(out->status.lines[3],KUI_APP_LINE_CAP,"Type: %s; copy flag: %s",e[0]==0xcc?"VMU mini-game":"data save",e[1]?"protected":"unprotected");
    snprintf(out->status.lines[4],KUI_APP_LINE_CAP,"%u blocks (%u bytes); header block: %u",get16(e+24),(unsigned)info->padded_bytes,get16(e+26));
    if(info->timestamp_known)snprintf(out->status.lines[5],KUI_APP_LINE_CAP,"Original date: %04u-%02u-%02u %02u:%02u:%02u",unbcd(e[16])*100+unbcd(e[17]),unbcd(e[18]),unbcd(e[19]),unbcd(e[20]),unbcd(e[21]),unbcd(e[22]));
    else snprintf(out->status.lines[5],KUI_APP_LINE_CAP,"Original date: unknown (zero timestamp)");
    snprintf(out->status.lines[6],KUI_APP_LINE_CAP,"%s",e[0]==0xcc?"Mini-game header bounds checked; format has no payload CRC":"VMS package CRC16 checked; complete inputs read twice");
    snprintf(out->status.lines[7],KUI_APP_LINE_CAP,"Confirm creates a new .vms/.dir/.crc bundle on SD");
    snprintf(out->status.lines[8],KUI_APP_LINE_CAP,"No physical VMU is accessed; restore is a separate action");
}
/* Persist how external metadata was obtained, alongside (and separate from)
 * the restore proof. CREATE_NEW plus exact readback prevents overwrites. */
static bool provenance(struct session *s,const char *source,const char *companion,const uint8_t digest[32],const struct kui_vmu_save_info *info){
    char part[KUI_VMU_BACKUP_PATH_CAP],final[KUI_VMU_BACKUP_PATH_CAP],text[2048],check[2048],hex[65];
    const char *slash=strrchr(s->out->backup_path,'/');if(!slash)return fail(s,"Imported bundle path is invalid");
    size_t folder=(size_t)(slash-s->out->backup_path);
    int n=snprintf(part,sizeof(part),"%.*s/import.part",(int)folder,s->out->backup_path);
    int m=snprintf(final,sizeof(final),"%.*s/import.txt",(int)folder,s->out->backup_path);
    if(n<0||m<0||(size_t)n>=sizeof(part)||(size_t)m>=sizeof(final))return fail(s,"Import provenance path is too long");
    kui_hex(digest,32,hex);
    n=snprintf(text,sizeof(text),"K-UI external VMU save import\nSource: %s\nCompanion: %s\nInput snapshot SHA256: %s\nThe snapshot hashes normalized paths, exact lengths and original input bytes.\nVMU filename: %s\nTimestamp: %s\nNo physical VMU was accessed. Restore is a separate preview and confirmation.\n",source,companion&&companion[0]?companion:"none",hex,info->name_from_filename?"reconstructed from the explicit SD filename stem":"preserved from external metadata",info->timestamp_known?"preserved from external metadata":info->name_from_filename?"unknown; all-zero timestamp reconstructed":"unknown; external all-zero timestamp preserved");
    if(n<0||(size_t)n>=sizeof(text))return fail(s,"Import provenance record is too long");
    FIL file;if(f_open(&file,part,FA_WRITE|FA_CREATE_NEW)!=FR_OK)return fail(s,"Bundle is verified, but provenance cannot be created");
    UINT written=0;bool ok=true;
    if(f_write(&file,text,(UINT)n,&written)!=FR_OK||written!=(UINT)n||f_sync(&file)!=FR_OK)ok=fail(s,"Bundle is verified, but provenance write failed");
    if(f_close(&file)!=FR_OK)ok=fail(s,"Cannot close import provenance");
    if(!ok)return false;
    if(f_open(&file,part,FA_READ)!=FR_OK)return fail(s,"Cannot verify import provenance");
    UINT got=0;if(f_size(&file)!=(FSIZE_t)n||f_read(&file,check,(UINT)n,&got)!=FR_OK||got!=(UINT)n||memcmp(text,check,(size_t)n))ok=fail(s,"Import provenance readback differs");
    if(f_close(&file)!=FR_OK)ok=fail(s,"Cannot close import provenance verification");
    if(!ok)return false;
    if(f_rename(part,final)!=FR_OK)return fail(s,"Cannot publish verified import provenance");
    return true;
}
void kui_vmu_import_run(const char *path,bool commit,struct kui_vmu_view *out,kui_log_fn log,kui_cancel_fn cancel,kui_app_progress_fn progress){
    if(!out)return;
    memset(out,0,sizeof(*out));struct session s={out,log?log:quiet_log,cancel,progress};
    char source[KUI_FILES_PATH_CAP],companion[KUI_FILES_PATH_CAP]={0},stem[13],error[128];
    uint8_t vmi[108],digest[32];uint8_t *input=NULL;size_t length=0;struct kui_vmu_save_metadata metadata;struct kui_vmu_save_info info;
    bool connected=false,mounted=false,ok=false;FATFS fs;
    if(!commit)pinned.valid=false;
    if(!normalize(path,source)){fail(&s,"Select a safe local SD import path within the Files path limit");goto done;}
    if(commit&&(!pinned.valid||strcmp(source,pinned.path))){fail(&s,"Preview this exact import source before staging it");goto done;}
    enum kui_vmu_save_format format;
    if(extension(source,"vmi"))format=KUI_VMU_SAVE_VMI;
    else if(extension(source,"dci"))format=KUI_VMU_SAVE_DCI;
    else if(extension(source,"vms"))format=KUI_VMU_SAVE_VMS;
    else{fail(&s,"Select a VMS, VMI or DCI file");goto done;}
    if(stopped(&s))goto done;
    kui_sd_set_params(KUI_STORAGE_AUTO,true);
    if(!kui_sd_connect()){fail(&s,"Cannot connect SD card for VMU import");goto done;}connected=true;
    if(!kui_mount(&fs,s.log)){fail(&s,"Cannot mount SD card for VMU import");goto done;}mounted=true;
    input=calloc(1,KUI_VMU_IMPORT_MAX_BYTES+32u);if(!input){fail(&s,"Not enough RAM for VMU import");goto done;}
    struct kui_sha256 hash;kui_sha256_init(&hash);notify(&s,"Reading and checking import source (read-only)...");
    if(format==KUI_VMU_SAVE_VMI){
        if(!read_source(&s,source,vmi,sizeof(vmi),&length,&hash))goto done;
        if(!kui_vmu_save_parse_vmi(vmi,length,&metadata,error,sizeof(error))){fail(&s,error);goto done;}
        const char *slash=strrchr(source,'/');size_t folder=(size_t)(slash-source)+1;
        int n=snprintf(companion,sizeof(companion),"%.*s%s.vms",(int)folder,source,metadata.resource);
        if(n<0||(size_t)n>=sizeof(companion)||!kui_files_path_valid(companion)){fail(&s,"VMI companion path is too long or unsafe");goto done;}
        if(!read_source(&s,companion,input,KUI_VMU_IMPORT_MAX_BYTES,&length,&hash))goto done;
    }else if(!read_source(&s,source,input,KUI_VMU_IMPORT_MAX_BYTES+(format==KUI_VMU_SAVE_DCI?32u:0u),&length,&hash))goto done;
    kui_sha256_digest(&hash,digest);
    if(commit&&memcmp(digest,pinned.digest,32)){fail(&s,"Import source changed since preview; preview it again");goto done;}
    stem[0]=0;
    if(format==KUI_VMU_SAVE_VMS){const char *leaf=kui_files_leaf(source);size_t n=strlen(leaf)-4;if(!n||n>12){fail(&s,"Bare VMS filename stem must contain 1 to 12 characters");goto done;}memcpy(stem,leaf,n);stem[n]=0;}
    if(!kui_vmu_save_parse(format,input,length,format==KUI_VMU_SAVE_VMI?&metadata:NULL,stem,&info,error,sizeof(error))){fail(&s,error);goto done;}
    describe(&s,format,&info);
    if(!commit){strcpy(pinned.path,source);memcpy(pinned.digest,digest,32);pinned.valid=true;out->import_ready=true;notify(&s,"Import preview ready; confirm to stage a new SD backup bundle");ok=true;goto done;}
    pinned.valid=false;if(stopped(&s))goto done;
    /* calloc supplies only the missing allocation padding; all actual bytes,
     * including existing padding and meaningful metadata, remain unchanged. */
    struct kui_vmu_view published;
    if(!kui_vmu_import_publish(info.entry,input+info.payload_offset,info.padded_bytes,&published,s.log,cancel,progress)){out->status=published.status;memcpy(out->backup_path,published.backup_path,sizeof(out->backup_path));goto done;}
    memcpy(out->backup_path,published.backup_path,sizeof(out->backup_path));
    if(!provenance(&s,source,companion,digest,&info))goto done;
    out->status.done=out->status.total=info.padded_bytes;
    notify(&s,"Import staged as a verified SD bundle; restore separately when ready");ok=true;
done:
    if(mounted&&f_mount(NULL,"0:",0)!=FR_OK){fail(&s,"Cannot unmount SD card after VMU import");ok=false;}
    if(connected)kui_sd_disconnect();
    free(input);
    out->status.complete=ok;out->status.passed=ok&&!out->status.errors&&!out->status.stopped;
    if(!out->status.passed){out->import_ready=false;pinned.valid=false;}
    if(progress)progress(&out->status);
}
