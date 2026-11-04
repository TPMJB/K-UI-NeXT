/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/update.h"
#include "kui/runtime.h"
#include "kui/hash.h"
#include "kui/update_handoff.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHUNK 32768u
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static uint64_t get64(const uint8_t *p) {return get32(p)|(uint64_t)get32(p+4u)<<32;}
static bool stopped(kui_cancel_fn cancel) {return cancel && cancel();}
static void phase(struct kui_app_status *s,const char *message,uint64_t total,kui_app_progress_fn progress) {
    if(!s)return;
    *s=(struct kui_app_status){.total=total};
    snprintf(s->message,sizeof(s->message),"%s",message);
    if(progress)progress(s);
}
static enum kui_update_result finish(enum kui_update_result r,struct kui_app_status *s,
    kui_log_fn log,kui_app_progress_fn progress,const char *success) {
    if(s) {
        s->complete=true;s->passed=r==KUI_UPDATE_OK;s->stopped=r==KUI_UPDATE_CANCELLED;
        if(r==KUI_UPDATE_OK)s->done=s->total;
        s->errors=(r!=KUI_UPDATE_OK && r!=KUI_UPDATE_CANCELLED)?1u:0u;
        snprintf(s->message,sizeof(s->message),"%s",r==KUI_UPDATE_OK?success:kui_update_result_name(r));
        if(progress)progress(s);
    }
    if(log)log("Offline update: %s",r==KUI_UPDATE_OK?success:kui_update_result_name(r));
    return r;
}
static enum kui_update_result read_exact(FIL *f,void *data,UINT bytes,kui_cancel_fn cancel) {
    if(stopped(cancel))return KUI_UPDATE_CANCELLED;
    UINT got=0;
    return f_read(f,data,bytes,&got)==FR_OK && got==bytes?KUI_UPDATE_OK:KUI_UPDATE_IO;
}
static enum kui_update_result write_exact(FIL *f,const void *data,UINT bytes) {
    UINT done=0;
    return f_write(f,data,bytes,&done)==FR_OK && done==bytes?KUI_UPDATE_OK:KUI_UPDATE_IO;
}
static enum kui_update_result package_read(FIL *file,bool receipt,struct kui_update_package *out,
    kui_cancel_fn cancel) {
    if(f_size(file)<KUI_UPDATE_HEADER_BYTES)return KUI_UPDATE_FORMAT;
    uint8_t header[128];enum kui_update_result r=read_exact(file,header,sizeof(header),cancel);
    if(r)return r;
    uint64_t length=f_size(file);
    if(receipt) {
        uint32_t count=get32(header+16u);uint64_t payload=get64(header+24u);
        if(count>KUI_UPDATE_MAX_ENTRIES || payload>KUI_UPDATE_MAX_PAYLOAD)return KUI_UPDATE_FORMAT;
        if(length!=128u+(uint64_t)count*128u)return KUI_UPDATE_FORMAT;
        length+=payload;
    }
    r=kui_update_header_decode(header,length,out);if(r)return r;
    size_t bytes=(size_t)out->count*128u;uint8_t *manifest=malloc(bytes);
    if(!manifest)return KUI_UPDATE_MEMORY;
    r=read_exact(file,manifest,(UINT)bytes,cancel);
    if(!r)r=kui_update_manifest_decode(out,manifest,bytes);
    free(manifest);return r;
}
static unsigned capability_count(const uint8_t *p,size_t n) {
    unsigned count=0u;
    for(size_t i=0;i+16u<=n;i++)
        if(p[i]=='K' && !memcmp(p+i,"KUIBANK1",8u) && get32(p+i+8u)==1u && get32(p+i+12u)==UINT32_MAX-1u)
            ++count;
    return count;
}
struct capability {uint8_t tail[15];size_t bytes;unsigned count;};
static void capability_update(struct capability *c,const uint8_t *p,size_t n) {
    uint8_t edge[30];size_t first=n<15u?n:15u;
    memcpy(edge,c->tail,c->bytes);memcpy(edge+c->bytes,p,first);
    c->count+=capability_count(edge,c->bytes+first)+capability_count(p,n);
    if(n>=15u) {memcpy(c->tail,p+n-15u,15u);c->bytes=15u;}
    else {
        size_t keep=c->bytes+n>15u?15u-n:c->bytes;
        memmove(c->tail,c->tail+c->bytes-keep,keep);memcpy(c->tail+keep,p,n);c->bytes=keep+n;
    }
}
/* Optional sink is a newly staged, unselected bank member. No bytes from a
 * bank with a complete receipt are ever passed as a writable destination. */
static enum kui_update_result admit(FIL *source,uint64_t offset,const struct kui_update_entry *entry,
    const char *build,bool gui,FIL *sink,uint8_t *buffer,struct kui_app_status *s,
    kui_cancel_fn cancel,kui_app_progress_fn progress) {
    if(stopped(cancel))return KUI_UPDATE_CANCELLED;
    if(entry->policy==KUI_UPDATE_INHERIT_ROOT)return KUI_UPDATE_OK;
    if(entry->bytes<64u || offset>(uint64_t)(FSIZE_t)-1 || f_lseek(source,(FSIZE_t)offset)!=FR_OK)
        return KUI_UPDATE_IO;
    uint8_t header[64];enum kui_update_result r=read_exact(source,header,sizeof(header),cancel);
    if(r)return r;
    struct kui_runtime_info runtime;
    const char *expected_build=kui_update_entry_build(build,entry);
    if(!expected_build || kui_runtime_header(header,entry->bytes,&runtime)!=KUI_RUNTIME_OK ||
       memcmp(runtime.build,expected_build,13u))
        return KUI_UPDATE_RUNTIME;
    struct kui_sha256 sha;kui_sha256_init(&sha);kui_sha256_update(&sha,header,sizeof(header));
    if(sink && (r=write_exact(sink,header,sizeof(header)))!=KUI_UPDATE_OK)return r;
    if(s)s->done+=sizeof(header);
    struct capability capability={0};struct kui_update_handoff_scan handoff={0};
    uint32_t crc=0u;uint64_t done=64u;
    while(done<entry->bytes) {
        UINT n=(UINT)(entry->bytes-done>CHUNK?CHUNK:entry->bytes-done);
        r=read_exact(source,buffer,n,cancel);if(r)return r;
        kui_sha256_update(&sha,buffer,n);crc=kui_crc32(crc,buffer,n);
        if(gui) {capability_update(&capability,buffer,n);kui_update_handoff_scan_update(&handoff,buffer,n);}
        if(sink && (r=write_exact(sink,buffer,n))!=KUI_UPDATE_OK)return r;
        done+=n;
        if(s) {s->done+=n;if(progress)progress(s);}
    }
    uint8_t digest[32];kui_sha256_digest(&sha,digest);
    if(crc!=runtime.crc32 || memcmp(digest,entry->sha256,32u))return KUI_UPDATE_CHECKSUM;
    if(gui && (capability.count!=1u || !kui_update_handoff_scan_valid(&handoff)))return KUI_UPDATE_RUNTIME;
    return stopped(cancel)?KUI_UPDATE_CANCELLED:KUI_UPDATE_OK;
}
enum kui_update_result kui_update_inspect(const char *path,struct kui_update_package *out,
    struct kui_app_status *s,kui_log_fn log,kui_cancel_fn cancel,kui_app_progress_fn progress) {
    if(out)memset(out,0,sizeof(*out));
    phase(s,"Checking offline update package...",0u,progress);
    enum kui_update_result r=KUI_UPDATE_ARGUMENT;
    if(!path || !path[0] || !out)return finish(r,s,log,progress,"");
    if(stopped(cancel))return finish(KUI_UPDATE_CANCELLED,s,log,progress,"");
    FIL file;FRESULT opened=f_open(&file,path,FA_READ);
    if(opened!=FR_OK)return finish(opened==FR_NO_FILE || opened==FR_NO_PATH?KUI_UPDATE_NOT_FOUND:KUI_UPDATE_IO,s,log,progress,"");
    r=package_read(&file,false,out,cancel);uint8_t *buffer=NULL;
    if(!r) {buffer=malloc(CHUNK);if(!buffer)r=KUI_UPDATE_MEMORY;}
    if(!r && s)s->total=out->payload_bytes;
    for(uint32_t i=0;!r && i<out->count;i++)
        r=admit(&file,out->entries[i].payload_offset,&out->entries[i],out->build,i==0u,NULL,buffer,s,cancel,progress);
    free(buffer);if(f_close(&file)!=FR_OK && !r)r=KUI_UPDATE_IO;
    if(r)memset(out,0,sizeof(*out));
    return finish(r,s,log,progress,"Offline update package verified.");
}
enum kui_update_result kui_update_activation_read(unsigned slot,struct kui_update_activation *out,kui_cancel_fn cancel) {
    if(!out || slot>1u)return KUI_UPDATE_ARGUMENT;
    memset(out,0,sizeof(*out));if(stopped(cancel))return KUI_UPDATE_CANCELLED;
    FIL file;FRESULT f=f_open(&file,slot?KUI_UPDATE_ACTIVE1:KUI_UPDATE_ACTIVE0,FA_READ);
    if(f!=FR_OK)return f==FR_NO_FILE || f==FR_NO_PATH?KUI_UPDATE_NOT_FOUND:KUI_UPDATE_IO;
    uint8_t wire[128];enum kui_update_result r=f_size(&file)==sizeof(wire)?read_exact(&file,wire,sizeof(wire),cancel):KUI_UPDATE_FORMAT;
    if(!r && !kui_update_activation_decode(wire,out))r=KUI_UPDATE_FORMAT;
    if(f_close(&file)!=FR_OK)r=KUI_UPDATE_IO;
    if(r)memset(out,0,sizeof(*out));
    return r;
}
static bool activation_equal(const struct kui_update_activation *a,const struct kui_update_activation *b) {
    return a->sequence==b->sequence && !memcmp(a->selected,b->selected,13u) &&
        !memcmp(a->previous,b->previous,13u) && !memcmp(a->manifest_sha256,b->manifest_sha256,32u);
}
enum kui_update_result kui_update_activation_latest(struct kui_update_activation *out,unsigned *slot,kui_cancel_fn cancel) {
    if(!out || !slot)return KUI_UPDATE_ARGUMENT;
    memset(out,0,sizeof(*out));*slot=0u;struct kui_update_activation a,b;
    enum kui_update_result ar=kui_update_activation_read(0u,&a,cancel),br=kui_update_activation_read(1u,&b,cancel);
    if(ar==KUI_UPDATE_CANCELLED || br==KUI_UPDATE_CANCELLED)return KUI_UPDATE_CANCELLED;
    if(ar==KUI_UPDATE_IO || br==KUI_UPDATE_IO)return KUI_UPDATE_IO;
    if(!ar && !br && a.sequence==b.sequence && !activation_equal(&a,&b))return KUI_UPDATE_FORMAT;
    if(!ar || !br) {
        if(ar || (!br && b.sequence>a.sequence)) {*out=b;*slot=1u;} else *out=a;
        return KUI_UPDATE_OK;
    }
    return ar==KUI_UPDATE_NOT_FOUND && br==KUI_UPDATE_NOT_FOUND?KUI_UPDATE_NOT_FOUND:KUI_UPDATE_FORMAT;
}
enum kui_update_result kui_update_bank_receipt_read(const char *build,struct kui_update_package *out,kui_cancel_fn cancel) {
    if(out)memset(out,0,sizeof(*out));
    if(!out || !kui_update_build_valid(build))return KUI_UPDATE_ARGUMENT;
    char path[160];int n=snprintf(path,sizeof(path),KUI_UPDATE_SYSTEM "/%s/package.bin",build);
    if(n<0 || (size_t)n>=sizeof(path))return KUI_UPDATE_ARGUMENT;
    if(stopped(cancel))return KUI_UPDATE_CANCELLED;
    FIL file;FRESULT f=f_open(&file,path,FA_READ);
    if(f!=FR_OK)return f==FR_NO_FILE || f==FR_NO_PATH?KUI_UPDATE_NOT_FOUND:KUI_UPDATE_IO;
    enum kui_update_result r=package_read(&file,true,out,cancel);
    if(!r && strcmp(build,out->build))r=KUI_UPDATE_STALE;
    if(f_close(&file)!=FR_OK && !r)r=KUI_UPDATE_IO;
    if(r)memset(out,0,sizeof(*out));
    return r;
}
static enum kui_update_result verify_member(const char *build,const struct kui_update_entry *entry,
    bool gui,uint8_t *buffer,struct kui_app_status *s,kui_cancel_fn cancel,kui_app_progress_fn progress) {
    if(stopped(cancel))return KUI_UPDATE_CANCELLED;
    if(entry->policy==KUI_UPDATE_INHERIT_ROOT)return KUI_UPDATE_OK;
    char path[160];if(!kui_update_bank_path(build,entry->path,path,sizeof(path)))return KUI_UPDATE_ARGUMENT;
    FIL file;FRESULT f=f_open(&file,path,FA_READ);
    if(f!=FR_OK)return f==FR_NO_FILE || f==FR_NO_PATH?KUI_UPDATE_NOT_FOUND:KUI_UPDATE_IO;
    enum kui_update_result r=f_size(&file)==entry->bytes?
        admit(&file,0u,entry,build,gui,NULL,buffer,s,cancel,progress):KUI_UPDATE_CHECKSUM;
    if(f_close(&file)!=FR_OK && !r)r=KUI_UPDATE_IO;
    return r;
}
enum kui_update_result kui_update_bank_verify(const char *build,struct kui_update_package *out,
    struct kui_app_status *s,kui_log_fn log,kui_cancel_fn cancel,kui_app_progress_fn progress) {
    phase(s,"Verifying installed update bank...",0u,progress);
    if(!out)return finish(KUI_UPDATE_ARGUMENT,s,log,progress,"");
    enum kui_update_result r=kui_update_bank_receipt_read(build,out,cancel);uint8_t *buffer=NULL;
    if(!r) {buffer=malloc(CHUNK);if(!buffer)r=KUI_UPDATE_MEMORY;}
    if(!r && s)s->total=out->payload_bytes;
    for(uint32_t i=0;!r && i<out->count;i++)r=verify_member(build,&out->entries[i],i==0u,buffer,s,cancel,progress);
    free(buffer);if(r)memset(out,0,sizeof(*out));
    return finish(r,s,log,progress,"Installed update bank verified.");
}
static enum kui_update_result directory(const char *path) {
    FILINFO info;FRESULT r=f_stat(path,&info);
    if(r==FR_OK)return info.fattrib&AM_DIR?KUI_UPDATE_OK:KUI_UPDATE_PROTECTED;
    if(r!=FR_NO_FILE && r!=FR_NO_PATH)return KUI_UPDATE_IO;
    return f_mkdir(path)==FR_OK?KUI_UPDATE_OK:KUI_UPDATE_IO;
}
static enum kui_update_result directories(const char *build) {
    static const char *const tails[]={"","/apps","/apps/games","/apps/disc"};
    enum kui_update_result r=directory("0:/KUI");if(!r)r=directory(KUI_UPDATE_SYSTEM);
    char path[160];
    for(unsigned i=0;!r && i<sizeof(tails)/sizeof(tails[0]);i++) {
        snprintf(path,sizeof(path),KUI_UPDATE_SYSTEM "/%s%s",build,tails[i]);r=directory(path);
    }
    return r;
}
static enum kui_update_result quota(const struct kui_update_package *p,uint64_t *needed,uint64_t *available) {
    FATFS *fs=NULL;DWORD clusters=0;
    if(f_getfree("0:",&clusters,&fs)!=FR_OK || !fs || !fs->csize)return KUI_UPDATE_IO;
    uint64_t cluster=(uint64_t)fs->csize*512u,required=16u*cluster,reclaim=0u;
    for(uint32_t i=0;i<p->count;i++)if(p->entries[i].policy!=KUI_UPDATE_INHERIT_ROOT)
        required+=((p->entries[i].bytes+cluster-1u)/cluster)*cluster;
    required+=((128u+(uint64_t)p->count*128u+cluster-1u)/cluster)*cluster;
    char path[160];FILINFO info;
    static const char *const tails[]={"","/apps","/apps/games","/apps/disc"};
    for(unsigned i=0;i<sizeof(tails)/sizeof(tails[0]);i++) {
        snprintf(path,sizeof(path),KUI_UPDATE_SYSTEM "/%s%s",p->build,tails[i]);
        FRESULT f=f_stat(path,&info);
        if(f==FR_OK) {if(!(info.fattrib&AM_DIR))return KUI_UPDATE_PROTECTED;reclaim+=cluster;}
        else if(f!=FR_NO_FILE && f!=FR_NO_PATH)return KUI_UPDATE_IO;
    }
    FRESULT system=f_stat(KUI_UPDATE_SYSTEM,&info);
    if(system==FR_OK) {if(!(info.fattrib&AM_DIR))return KUI_UPDATE_PROTECTED;reclaim+=cluster;}
    else if(system!=FR_NO_FILE && system!=FR_NO_PATH)return KUI_UPDATE_IO;
    for(uint32_t i=0;i<=p->count;i++) {
        if(i<p->count && p->entries[i].policy==KUI_UPDATE_INHERIT_ROOT)continue;
        if(i==p->count)snprintf(path,sizeof(path),KUI_UPDATE_SYSTEM "/%s/package.bin",p->build);
        else if(!kui_update_bank_path(p->build,p->entries[i].path,path,sizeof(path)))return KUI_UPDATE_ARGUMENT;
        FRESULT f=f_stat(path,&info);
        if(f==FR_OK) {if(info.fattrib&AM_DIR)return KUI_UPDATE_PROTECTED;
            reclaim+=((info.fsize+cluster-1u)/cluster)*cluster;}
        else if(f!=FR_NO_FILE && f!=FR_NO_PATH)return KUI_UPDATE_IO;
    }
    *needed=required>reclaim?required-reclaim:0u;*available=(uint64_t)clusters*cluster;
    return *available>=*needed?KUI_UPDATE_OK:KUI_UPDATE_SPACE;
}
enum kui_update_result kui_update_plan_space(const struct kui_update_package *p,
    uint64_t *needed,uint64_t *available,kui_cancel_fn cancel) {
    if(!needed || !available)return KUI_UPDATE_ARGUMENT;
    *needed=*available=0u;
    if(!p || !kui_update_package_find(p,"runtime.kui"))return KUI_UPDATE_ARGUMENT;
    struct kui_update_activation a;unsigned slot;
    enum kui_update_result r=kui_update_activation_latest(&a,&slot,cancel);
    if(r==KUI_UPDATE_NOT_FOUND)memset(&a,0,sizeof(a));else if(r)return r;
    struct kui_update_package *bank=malloc(sizeof(*bank));if(!bank)return KUI_UPDATE_MEMORY;
    r=kui_update_bank_receipt_read(p->build,bank,cancel);
    if(!r) {
        struct kui_update_pin pin;kui_update_package_pin(p,&pin);
        if(!kui_update_pin_matches(&pin,bank))r=KUI_UPDATE_STALE;
        else {
            FATFS *fs=NULL;DWORD free_clusters=0u;
            if(f_getfree("0:",&free_clusters,&fs)!=FR_OK || !fs)r=KUI_UPDATE_IO;
            else *available=(uint64_t)free_clusters*fs->csize*512u;
        }
    } else if(r==KUI_UPDATE_NOT_FOUND || r==KUI_UPDATE_FORMAT || r==KUI_UPDATE_CHECKSUM || r==KUI_UPDATE_COHORT || r==KUI_UPDATE_STALE) {
        if(!strcmp(a.selected,p->build) || !strcmp(a.previous,p->build))r=KUI_UPDATE_PROTECTED;
        else if(stopped(cancel))r=KUI_UPDATE_CANCELLED;
        else r=quota(p,needed,available);
    }
    free(bank);return r;
}
static enum kui_update_result clear_partial(const struct kui_update_package *p,kui_cancel_fn cancel) {
    char path[160];FILINFO info;
    for(uint32_t i=0;i<=p->count;i++) {
        if(stopped(cancel))return KUI_UPDATE_CANCELLED;
        if(i<p->count && p->entries[i].policy==KUI_UPDATE_INHERIT_ROOT)continue;
        if(i==p->count)snprintf(path,sizeof(path),KUI_UPDATE_SYSTEM "/%s/package.bin",p->build);
        else if(!kui_update_bank_path(p->build,p->entries[i].path,path,sizeof(path)))return KUI_UPDATE_ARGUMENT;
        FRESULT f=f_stat(path,&info);
        if(f==FR_OK) {if(info.fattrib&AM_DIR)return KUI_UPDATE_PROTECTED;
            if(f_unlink(path)!=FR_OK)return KUI_UPDATE_IO;}
        else if(f!=FR_NO_FILE && f!=FR_NO_PATH)return KUI_UPDATE_IO;
    }
    return KUI_UPDATE_OK;
}
static enum kui_update_result record_write(const char *path,const uint8_t *wire,size_t bytes,bool activation) {
    FILINFO info;FRESULT found=f_stat(path,&info);bool create=found==FR_NO_FILE || found==FR_NO_PATH;
    if(found!=FR_OK && !create)return KUI_UPDATE_IO;
    if(!create && ((info.fattrib&AM_DIR) || !activation))return KUI_UPDATE_PROTECTED;
    if(!create && info.fsize!=bytes) {if(f_unlink(path)!=FR_OK)return KUI_UPDATE_IO;create=true;}
    FIL file;if(f_open(&file,path,FA_WRITE|(create?FA_CREATE_NEW:FA_OPEN_EXISTING))!=FR_OK)return KUI_UPDATE_IO;
    enum kui_update_result r=write_exact(&file,wire,(UINT)bytes);
    if(f_sync(&file)!=FR_OK)r=KUI_UPDATE_IO;
    if(f_close(&file)!=FR_OK)r=KUI_UPDATE_IO;
    if(r)return r;
    if(f_open(&file,path,FA_READ)!=FR_OK)return KUI_UPDATE_IO;
    uint8_t actual[512];bool ok=f_size(&file)==bytes;
    for(size_t at=0;ok && at<bytes;at+=sizeof(actual)) {
        UINT n=(UINT)(bytes-at>sizeof(actual)?sizeof(actual):bytes-at),got=0u;
        ok=f_read(&file,actual,n,&got)==FR_OK && got==n && !memcmp(actual,wire+at,n);
    }
    if(f_close(&file)!=FR_OK)ok=false;
    return ok?KUI_UPDATE_OK:KUI_UPDATE_IO;
}
static enum kui_update_result publish(const char *build,const uint8_t *hash,
    const struct kui_update_activation *review,unsigned review_slot,struct kui_app_status *s,
    kui_log_fn log,kui_cancel_fn cancel,kui_app_progress_fn progress) {
    struct kui_update_activation current;unsigned slot;
    enum kui_update_result r=kui_update_activation_latest(&current,&slot,cancel);
    if(r==KUI_UPDATE_NOT_FOUND) {memset(&current,0,sizeof(current));slot=0u;}
    else if(r)return r;
    if(!activation_equal(review,&current) || (current.sequence && slot!=review_slot))return KUI_UPDATE_STALE;
    if(!strcmp(current.selected,build) && (!build[0] || !memcmp(current.manifest_sha256,hash,32u)))return KUI_UPDATE_OK;
    if(current.sequence==UINT64_MAX)return KUI_UPDATE_SEQUENCE;
    struct kui_update_activation next={.sequence=current.sequence+1u};
    memcpy(next.selected,build,strlen(build));
    if(!kui_update_build_withdrawn(current.selected))memcpy(next.previous,current.selected,sizeof(next.previous));
    if(build[0])memcpy(next.manifest_sha256,hash,32u);
    uint8_t wire[128];if(!kui_update_activation_encode(&next,wire))return KUI_UPDATE_FORMAT;
    if(stopped(cancel))return KUI_UPDATE_CANCELLED;
    r=directory(KUI_UPDATE_SYSTEM);if(r)return r;
    /* Stop is deferred across this fixed128-byte publication and readback.
     * A failed sync/readback may still leave a complete newer record. The
     * retained former newest record and every complete bank remain intact. */
    phase(s,"Activating verified update; checking selection...",128u,progress);
    r=record_write(current.sequence?(slot?KUI_UPDATE_ACTIVE0:KUI_UPDATE_ACTIVE1):KUI_UPDATE_ACTIVE0,wire,sizeof(wire),true);
    if(r) {
        if(s) {s->complete=true;s->passed=false;s->errors=1u;
            snprintf(s->message,sizeof(s->message),"Activation could not be confirmed; restart and check selected version.");
            if(progress)progress(s);}
        if(log)log("Offline update: activation could not be confirmed; retained prior record is unchanged");
    }
    return r;
}
static enum kui_update_result current(struct kui_update_activation *a,unsigned *slot,kui_cancel_fn cancel) {
    enum kui_update_result r=kui_update_activation_latest(a,slot,cancel);
    if(r==KUI_UPDATE_NOT_FOUND) {memset(a,0,sizeof(*a));*slot=0u;return KUI_UPDATE_OK;}
    return r;
}
static enum kui_update_result baseline_verify(struct kui_app_status *s,kui_cancel_fn cancel,
    kui_app_progress_fn progress) {
    phase(s,"Verifying immutable baseline runtime...",0u,progress);
    if(stopped(cancel))return KUI_UPDATE_CANCELLED;
    FIL file;FRESULT opened=f_open(&file,KUI_RUNTIME_PATH,FA_READ);
    if(opened!=FR_OK)return opened==FR_NO_FILE || opened==FR_NO_PATH?KUI_UPDATE_NOT_FOUND:KUI_UPDATE_IO;
    uint8_t header[64],buffer[4096];struct kui_runtime_info info;
    enum kui_update_result r=read_exact(&file,header,sizeof(header),cancel);
    if(!r && (kui_runtime_header(header,f_size(&file),&info)!=KUI_RUNTIME_OK ||
       kui_update_build_withdrawn(info.build)))r=KUI_UPDATE_RUNTIME;
    struct capability capability={0};struct kui_update_handoff_scan handoff={0};
    uint32_t crc=0u,done=0u;
    if(!r && s)s->total=info.payload_bytes;
    while(!r && done<info.payload_bytes) {
        UINT n=info.payload_bytes-done;if(n>sizeof(buffer))n=sizeof(buffer);
        r=read_exact(&file,buffer,n,cancel);
        if(!r) {
            crc=kui_crc32(crc,buffer,n);capability_update(&capability,buffer,n);
            kui_update_handoff_scan_update(&handoff,buffer,n);done+=n;
            if(s) {s->done=done;if(progress)progress(s);}
        }
    }
    if(!r && (crc!=info.crc32 || capability.count!=1u || !kui_update_handoff_scan_valid(&handoff)))r=KUI_UPDATE_CHECKSUM;
    if(f_close(&file)!=FR_OK && !r)r=KUI_UPDATE_IO;
    if(!r && stopped(cancel))r=KUI_UPDATE_CANCELLED;
    return r;
}
static enum kui_update_result final_selection(enum kui_update_result r,struct kui_app_status *s,
    kui_log_fn log,kui_app_progress_fn progress,const char *message) {
    if(r==KUI_UPDATE_IO && s && s->complete && strstr(s->message,"Activation could not"))return r;
    return finish(r,s,log,progress,message);
}
enum kui_update_result kui_update_select(const char *build,const uint8_t expected[32],
    struct kui_app_status *s,kui_log_fn log,kui_cancel_fn cancel,kui_app_progress_fn progress) {
    phase(s,"Checking selected update version...",0u,progress);
    if(!build || (build[0] && (!kui_update_build_valid(build) || !expected)) || (!build[0] && expected))
        return finish(KUI_UPDATE_ARGUMENT,s,log,progress,"");
    struct kui_update_activation a;unsigned slot;enum kui_update_result r=current(&a,&slot,cancel);
    struct kui_update_package *p=NULL;
    if(!r && build[0]) {
        p=malloc(sizeof(*p));if(!p)r=KUI_UPDATE_MEMORY;
        else r=kui_update_bank_verify(build,p,s,log,cancel,progress);
        if(!r && memcmp(expected,p->manifest_sha256,32u))r=KUI_UPDATE_STALE;
    }
    if(!r && !build[0])r=baseline_verify(s,cancel,progress);
    if(!r)r=publish(build,expected,&a,slot,s,log,cancel,progress);
    free(p);return final_selection(r,s,log,progress,"Selected update version saved; restart to use it.");
}
enum kui_update_result kui_update_rollback(struct kui_app_status *s,kui_log_fn log,
    kui_cancel_fn cancel,kui_app_progress_fn progress) {
    phase(s,"Checking previous update version...",0u,progress);
    struct kui_update_activation a;unsigned slot;enum kui_update_result r=current(&a,&slot,cancel);
    struct kui_update_package *p=NULL;uint8_t *hash=NULL;
    const char *target=!r && !kui_update_build_withdrawn(a.previous)?a.previous:"";
    if(!r && target[0]) {
        p=malloc(sizeof(*p));if(!p)r=KUI_UPDATE_MEMORY;
        else r=kui_update_bank_verify(target,p,s,log,cancel,progress);
        if(!r)hash=p->manifest_sha256;
    }
    if(!r && !target[0])r=baseline_verify(s,cancel,progress);
    if(!r)r=publish(target,hash,&a,slot,s,log,cancel,progress);
    free(p);return final_selection(r,s,log,progress,"Previous version selected; restart to use it.");
}
enum kui_update_result kui_update_install(const char *path,const struct kui_update_pin *expected,
    struct kui_app_status *s,kui_log_fn log,kui_cancel_fn cancel,kui_app_progress_fn progress) {
    phase(s,"Checking update before installation...",0u,progress);
    if(!expected)return finish(KUI_UPDATE_ARGUMENT,s,log,progress,"");
    struct kui_update_package *p=malloc(sizeof(*p)),*other=malloc(sizeof(*other));
    uint8_t *buffer=NULL,*receipt=NULL;FIL source;bool source_open=false;
    enum kui_update_result r=p && other?KUI_UPDATE_OK:KUI_UPDATE_MEMORY;
    if(!r)r=kui_update_inspect(path,p,s,log,cancel,progress);
    if(!r && !kui_update_pin_matches(expected,p))r=KUI_UPDATE_STALE;
    struct kui_update_activation active;unsigned slot=0u;
    if(!r)r=current(&active,&slot,cancel);
    if(!r && active.sequence==UINT64_MAX)r=KUI_UPDATE_SEQUENCE;
    bool complete=false;
    if(!r) {
        enum kui_update_result found=kui_update_bank_receipt_read(p->build,other,cancel);
        if(found==KUI_UPDATE_OK) {
            complete=true;
            if(!kui_update_pin_matches(expected,other))r=KUI_UPDATE_STALE;
            else r=kui_update_bank_verify(p->build,other,s,log,cancel,progress);
        } else if(found==KUI_UPDATE_IO || found==KUI_UPDATE_CANCELLED || found==KUI_UPDATE_MEMORY)r=found;
        else if(!strcmp(active.selected,p->build) || !strcmp(active.previous,p->build))r=KUI_UPDATE_PROTECTED;
    }
    uint64_t required=0u,available=0u;
    if(!r && !complete)r=quota(p,&required,&available);
    if(!r && !complete)r=directories(p->build);
    char path_buffer[160];
    if(!r && !complete)r=clear_partial(p,cancel);
    if(!r && !complete) {
        if(f_open(&source,path,FA_READ)!=FR_OK)r=KUI_UPDATE_IO;
        else {source_open=true;r=package_read(&source,false,other,cancel);
            if(!r && !kui_update_pin_matches(expected,other))r=KUI_UPDATE_STALE;}
        buffer=malloc(CHUNK);if(!buffer && !r)r=KUI_UPDATE_MEMORY;
        phase(s,"Installing and rereading unselected update bank...",p->payload_bytes*2u,progress);
    }
    for(uint32_t i=0;!r && !complete && i<p->count;i++) {
        if(stopped(cancel)) {r=KUI_UPDATE_CANCELLED;break;}
        if(p->entries[i].policy==KUI_UPDATE_INHERIT_ROOT)continue;
        if(!kui_update_bank_path(p->build,p->entries[i].path,path_buffer,sizeof(path_buffer))) {r=KUI_UPDATE_ARGUMENT;break;}
        FIL destination;
        if(f_open(&destination,path_buffer,FA_WRITE|FA_CREATE_ALWAYS)!=FR_OK) {r=KUI_UPDATE_IO;break;}
        r=admit(&source,p->entries[i].payload_offset,&p->entries[i],p->build,i==0u,&destination,buffer,s,cancel,progress);
        if(f_sync(&destination)!=FR_OK)r=KUI_UPDATE_IO;
        if(f_close(&destination)!=FR_OK)r=KUI_UPDATE_IO;
        if(!r)r=verify_member(p->build,&p->entries[i],i==0u,buffer,s,cancel,progress);
    }
    if(source_open && f_close(&source)!=FR_OK && !r)r=KUI_UPDATE_IO;
    if(!r && !complete) {
        receipt=malloc(KUI_UPDATE_RECEIPT_MAX);size_t bytes=0u;
        if(!receipt)r=KUI_UPDATE_MEMORY;
        else r=kui_update_receipt_encode(p,receipt,KUI_UPDATE_RECEIPT_MAX,&bytes);
        if(!r && stopped(cancel))r=KUI_UPDATE_CANCELLED;
        if(!r) {
            snprintf(path_buffer,sizeof(path_buffer),KUI_UPDATE_SYSTEM "/%s/package.bin",p->build);
            r=record_write(path_buffer,receipt,bytes,false);
        }
        if(!r)r=kui_update_bank_receipt_read(p->build,other,cancel);
        if(!r && !kui_update_pin_matches(expected,other))r=KUI_UPDATE_STALE;
    }
    if(!r)r=publish(p->build,p->manifest_sha256,&active,slot,s,log,cancel,progress);
    free(receipt);free(buffer);free(other);free(p);
    return final_selection(r,s,log,progress,"Update installed and selected; restart to use it.");
}
