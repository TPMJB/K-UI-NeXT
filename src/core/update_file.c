/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/update.h"
#include "kui/runtime.h"
#include "kui/hash.h"
#include "kui/update_handoff.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHUNK 32768u
#define REPAIR_BANKS 16u
/* GUI read wrappers resolve logical software paths into the active bank.
 * Engine admission and baseline cloning must inspect the physical root. */
extern FRESULT kui_update_physical_open(FIL *,const TCHAR *,BYTE) __attribute__((weak));
extern int kui_update_effective_snapshot(char [13],uint8_t [32]) __attribute__((weak));
extern unsigned kui_update_boot_engine_minimum(void) __attribute__((weak));
static FRESULT physical_open(FIL *file,const char *path,BYTE flags) {
    return kui_update_physical_open?kui_update_physical_open(file,path,flags):f_open(file,path,flags);
}
struct snapshot_source {bool package;char path[160];uint64_t offset;};
static enum kui_update_result snapshot_prepare(const struct kui_update_package *,
    const struct kui_update_activation *,struct kui_update_package *,struct snapshot_source *,
    struct kui_app_status *,kui_cancel_fn,kui_app_progress_fn);
static enum kui_update_result baseline_verify(struct kui_app_status *,kui_cancel_fn,kui_app_progress_fn,unsigned *);
static enum kui_update_result activation_for_update(struct kui_update_activation *,unsigned *,kui_cancel_fn,bool *);
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
        uint32_t row=get32(header+8u)==2u?160u:128u;
        if(length!=128u+(uint64_t)count*row)return KUI_UPDATE_FORMAT;
        length+=payload;
    }
    r=kui_update_header_decode(header,length,out);if(r)return r;
    size_t bytes=(size_t)out->count*kui_update_entry_bytes(out);uint8_t *manifest=malloc(bytes?bytes:1u);
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
static unsigned engine_count(const uint8_t *p,size_t n,unsigned level) {
    unsigned count=0u;
    for(size_t i=0;i+16u<=n;i++)
        if(p[i]=='K' && !memcmp(p+i,level==3u?"KUIENG3!":"KUIENG2!",8u) &&
           get32(p+i+8u)==level && get32(p+i+12u)==~(uint32_t)level)
            ++count;
    return count;
}
static void marker_update(struct capability *c,const uint8_t *p,size_t n,unsigned engine) {
    uint8_t edge[30];size_t first=n<15u?n:15u;
    memcpy(edge,c->tail,c->bytes);memcpy(edge+c->bytes,p,first);
    c->count+=engine?engine_count(edge,c->bytes+first,engine)+engine_count(p,n,engine):
        capability_count(edge,c->bytes+first)+capability_count(p,n);
    if(n>=15u) {memcpy(c->tail,p+n-15u,15u);c->bytes=15u;}
    else {
        size_t keep=c->bytes+n>15u?15u-n:c->bytes;
        memmove(c->tail,c->tail+c->bytes-keep,keep);memcpy(c->tail+keep,p,n);c->bytes=keep+n;
    }
}
static void capability_update(struct capability *c,const uint8_t *p,size_t n) {marker_update(c,p,n,false);}
static unsigned engine_marker_level(const struct capability *two,const struct capability *three) {
    return two->count+three->count==1u?(three->count?3u:2u):0u;
}
static bool has_payload(const struct kui_update_entry *entry) {
    return entry->policy!=KUI_UPDATE_INHERIT_ROOT && entry->policy!=KUI_UPDATE_REMOVE;
}
/* Optional sink is a newly staged, unselected bank member. No bytes from a
 * bank with a complete receipt are ever passed as a writable destination. */
static enum kui_update_result admit(FIL *source,uint64_t offset,const struct kui_update_entry *entry,
    const char *build,bool gui,unsigned minimum,FIL *sink,uint8_t *buffer,struct kui_app_status *s,
    kui_cancel_fn cancel,kui_app_progress_fn progress) {
    if(stopped(cancel))return KUI_UPDATE_CANCELLED;
    if(!has_payload(entry))return KUI_UPDATE_OK;
    if(offset>(uint64_t)(FSIZE_t)-1 || f_lseek(source,(FSIZE_t)offset)!=FR_OK)
        return KUI_UPDATE_IO;
    bool raw=entry->policy==KUI_UPDATE_RAW;
    uint8_t header[64];enum kui_update_result r=KUI_UPDATE_OK;struct kui_runtime_info runtime={0};
    struct kui_sha256 sha;kui_sha256_init(&sha);uint64_t done=0u;
    if(!raw) {
        if(entry->bytes<sizeof(header))return KUI_UPDATE_RUNTIME;
        r=read_exact(source,header,sizeof(header),cancel);if(r)return r;
        const char *expected_build=kui_update_entry_build(build,entry);
        if(!expected_build || kui_runtime_header(header,entry->bytes,&runtime)!=KUI_RUNTIME_OK ||
           memcmp(runtime.build,expected_build,13u))return KUI_UPDATE_RUNTIME;
        kui_sha256_update(&sha,header,sizeof(header));
        if(sink && (r=write_exact(sink,header,sizeof(header)))!=KUI_UPDATE_OK)return r;
        done=sizeof(header);if(s)s->done+=done;
    }
    struct capability capability={0},engine2={0},engine3={0};struct kui_update_handoff_scan handoff={0};
    uint32_t crc=0u;
    while(done<entry->bytes) {
        UINT n=(UINT)(entry->bytes-done>CHUNK?CHUNK:entry->bytes-done);
        r=read_exact(source,buffer,n,cancel);if(r)return r;
        kui_sha256_update(&sha,buffer,n);if(!raw)crc=kui_crc32(crc,buffer,n);
        if(gui) {capability_update(&capability,buffer,n);marker_update(&engine2,buffer,n,2u);marker_update(&engine3,buffer,n,3u);
            kui_update_handoff_scan_update(&handoff,buffer,n);}
        if(sink && (r=write_exact(sink,buffer,n))!=KUI_UPDATE_OK)return r;
        done+=n;
        if(s) {s->done+=n;if(progress)progress(s);}
    }
    uint8_t digest[32];kui_sha256_digest(&sha,digest);
    if((!raw && crc!=runtime.crc32) || memcmp(digest,entry->sha256,32u))return KUI_UPDATE_CHECKSUM;
    if(gui && (capability.count!=1u || !kui_update_handoff_scan_valid(&handoff)))return KUI_UPDATE_RUNTIME;
    if(gui && minimum && engine_marker_level(&engine2,&engine3)<minimum)return KUI_UPDATE_ENGINE;
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
    unsigned minimum=kui_update_engine_minimum(out),running_required=kui_update_boot_engine_minimum?kui_update_boot_engine_minimum():0u;
    if(minimum<running_required)minimum=running_required;
    for(uint32_t i=0;!r && i<out->count;i++)
        r=admit(&file,out->entries[i].payload_offset,&out->entries[i],out->build,
            !strcmp(out->entries[i].path,"runtime.kui"),minimum,NULL,buffer,s,cancel,progress);
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
    /* Incoming v2 deliveries may omit the GUI. A completed installed receipt
     * is a materialized snapshot and always pins its GUI, without KEEP rows. */
    if(!r && kui_update_package_version(out)==2u) {
        if(strcmp(out->entries[0].path,"runtime.kui") || out->entries[0].policy!=KUI_UPDATE_SELF_KUI)r=KUI_UPDATE_FORMAT;
        for(uint32_t i=0;!r && i<out->count;i++)if(out->entries[i].policy==KUI_UPDATE_KEEP)r=KUI_UPDATE_FORMAT;
    }
    if(f_close(&file)!=FR_OK && !r)r=KUI_UPDATE_IO;
    if(r)memset(out,0,sizeof(*out));
    return r;
}
static enum kui_update_result bank_withdrawn(const char *build,bool *withdrawn,kui_cancel_fn cancel) {
    *withdrawn=false;
    if(!kui_update_build_withdrawn(build))return KUI_UPDATE_OK;
    struct kui_update_package *p=malloc(sizeof(*p));if(!p)return KUI_UPDATE_MEMORY;
    enum kui_update_result r=kui_update_bank_receipt_read(build,p,cancel);
    if(!r)*withdrawn=kui_update_package_version(p)==1u;
    else if(r!=KUI_UPDATE_IO && r!=KUI_UPDATE_CANCELLED && r!=KUI_UPDATE_MEMORY) {*withdrawn=true;r=KUI_UPDATE_OK;}
    free(p);return r;
}
static enum kui_update_result verify_member(const char *build,const struct kui_update_entry *entry,
    bool gui,unsigned minimum,uint8_t *buffer,struct kui_app_status *s,kui_cancel_fn cancel,kui_app_progress_fn progress) {
    if(stopped(cancel))return KUI_UPDATE_CANCELLED;
    if(entry->policy==KUI_UPDATE_INHERIT_ROOT)return KUI_UPDATE_OK;
    char path[160];if(!kui_update_bank_path(build,entry->path,path,sizeof(path)))return KUI_UPDATE_ARGUMENT;
    if(entry->policy==KUI_UPDATE_REMOVE) {
        FILINFO info;FRESULT found=f_stat(path,&info);
        return found==FR_NO_FILE || found==FR_NO_PATH?KUI_UPDATE_OK:
            found==FR_OK?KUI_UPDATE_CHECKSUM:KUI_UPDATE_IO;
    }
    FIL file;FRESULT f=f_open(&file,path,FA_READ);
    if(f!=FR_OK)return f==FR_NO_FILE || f==FR_NO_PATH?KUI_UPDATE_NOT_FOUND:KUI_UPDATE_IO;
    enum kui_update_result r=f_size(&file)==entry->bytes?
        admit(&file,0u,entry,build,gui,minimum,NULL,buffer,s,cancel,progress):KUI_UPDATE_CHECKSUM;
    if(f_close(&file)!=FR_OK && !r)r=KUI_UPDATE_IO;
    return r;
}
static enum kui_update_result bank_verify_minimum(const char *build,struct kui_update_package *out,
    unsigned minimum,struct kui_app_status *s,kui_log_fn log,kui_cancel_fn cancel,kui_app_progress_fn progress) {
    phase(s,"Verifying installed update bank...",0u,progress);
    if(!out)return finish(KUI_UPDATE_ARGUMENT,s,log,progress,"");
    enum kui_update_result r=kui_update_bank_receipt_read(build,out,cancel);uint8_t *buffer=NULL;
    if(!r && minimum>=3u && kui_update_engine_minimum(out)<minimum)r=KUI_UPDATE_ENGINE;
    if(!r) {buffer=malloc(CHUNK);if(!buffer)r=KUI_UPDATE_MEMORY;}
    if(!r && s)s->total=out->payload_bytes;
    if(!r && minimum<kui_update_engine_minimum(out))minimum=kui_update_engine_minimum(out);
    for(uint32_t i=0;!r && i<out->count;i++)r=verify_member(build,&out->entries[i],!strcmp(out->entries[i].path,"runtime.kui"),minimum,buffer,s,cancel,progress);
    free(buffer);if(r)memset(out,0,sizeof(*out));
    return finish(r,s,log,progress,"Installed update bank verified.");
}
enum kui_update_result kui_update_bank_verify(const char *build,struct kui_update_package *out,
    struct kui_app_status *s,kui_log_fn log,kui_cancel_fn cancel,kui_app_progress_fn progress) {
    return bank_verify_minimum(build,out,0u,s,log,cancel,progress);
}
static bool snapshot_same(const struct kui_update_package *a,const struct kui_update_package *b) {
    return kui_update_package_version(a)==kui_update_package_version(b) && a->channel==b->channel && a->flags==b->flags &&
        a->count==b->count && a->payload_bytes==b->payload_bytes && a->file_bytes==b->file_bytes &&
        !memcmp(a->manifest_sha256,b->manifest_sha256,32u);
}
static bool recoverable_bank_error(enum kui_update_result r) {
    return r==KUI_UPDATE_NOT_FOUND || r==KUI_UPDATE_FORMAT || r==KUI_UPDATE_CHECKSUM ||
        r==KUI_UPDATE_COHORT || r==KUI_UPDATE_RUNTIME || r==KUI_UPDATE_STALE || r==KUI_UPDATE_ENGINE;
}
/* A damaged completed bank is never a writable destination. FULL delivery
 * bytes can repair its content through a separate deterministic instance.
 * Nonempty interrupted instances are retained too; a retry advances to the
 * next free instance and reuses any already verified matching completion. */
static enum kui_update_result snapshot_target(struct kui_update_package *snapshot,
    const struct kui_update_activation *active,struct kui_update_package *other,bool allow_repair,bool *complete,
    struct kui_app_status *s,kui_log_fn log,kui_cancel_fn cancel,kui_app_progress_fn progress) {
    char canonical[13],effective[13]={0};uint8_t effective_hash[32]={0};memcpy(canonical,snapshot->build,13u);
    int known=kui_update_effective_snapshot?kui_update_effective_snapshot(effective,effective_hash):0;
    if(!known)memcpy(effective,active->selected,sizeof(effective));
    struct kui_update_activation retained[2];bool retained_valid[2]={false,false};
    for(unsigned i=0;i<2u;i++) {
        enum kui_update_result record=kui_update_activation_read(i,retained+i,cancel);
        if(record==KUI_UPDATE_IO || record==KUI_UPDATE_CANCELLED)return record;
        retained_valid[i]=record==KUI_UPDATE_OK;
    }
    bool full=allow_repair;*complete=false;
    if(!full && effective[0] && strcmp(effective,canonical)) {
        enum kui_update_result found=kui_update_bank_receipt_read(effective,other,cancel);
        if(!found && snapshot_same(snapshot,other)) {
            enum kui_update_result verified=kui_update_bank_verify(effective,other,s,log,cancel,progress);
            if(verified)return verified;
            memcpy(snapshot->build,effective,13u);*complete=true;return KUI_UPDATE_OK;
        }
        if(found && !recoverable_bank_error(found))return found;
    }
    for(uint32_t attempt=0u;attempt<=REPAIR_BANKS;attempt++) {
        if(stopped(cancel))return KUI_UPDATE_CANCELLED;
        if(attempt) {
            struct kui_sha256 hash;uint8_t identity[32],index[4];char hex[65];
            for(unsigned i=0;i<4u;i++)index[i]=(uint8_t)(attempt>>(8u*i));
            kui_sha256_init(&hash);kui_sha256_update(&hash,"KUIREPAIR",9u);kui_sha256_update(&hash,canonical,12u);
            kui_sha256_update(&hash,snapshot->manifest_sha256,32u);kui_sha256_update(&hash,&snapshot->flags,1u);
            kui_sha256_update(&hash,index,sizeof(index));kui_sha256_digest(&hash,identity);kui_hex(identity,32u,hex);
            memcpy(snapshot->build,hex,12u);snapshot->build[12]=0;
        }
        enum kui_update_result found=kui_update_bank_receipt_read(snapshot->build,other,cancel);
        if(!found) {
            if(!snapshot_same(snapshot,other)) {if(full)continue;return KUI_UPDATE_STALE;}
            enum kui_update_result verified=kui_update_bank_verify(snapshot->build,other,s,log,cancel,progress);
            if(!verified) {*complete=true;return KUI_UPDATE_OK;}
            if(!full || !recoverable_bank_error(verified))return verified;
            continue;
        }
        if(!recoverable_bank_error(found))return found;
        bool protected=!strcmp(active->selected,snapshot->build) || !strcmp(active->previous,snapshot->build) ||
            (effective[0] && !strcmp(effective,snapshot->build));
        for(unsigned i=0;i<2u;i++)if(retained_valid[i] &&
            (!strcmp(retained[i].selected,snapshot->build) || !strcmp(retained[i].previous,snapshot->build)))protected=true;
        if(protected) {if(full)continue;return KUI_UPDATE_PROTECTED;}
        char directory_path[160];FILINFO info;
        snprintf(directory_path,sizeof(directory_path),KUI_UPDATE_SYSTEM "/%s",snapshot->build);
        FRESULT stat=f_stat(directory_path,&info);
        if(stat==FR_OK && !(info.fattrib&AM_DIR)) {if(full)continue;return KUI_UPDATE_PROTECTED;}
        if(stat!=FR_OK && stat!=FR_NO_FILE && stat!=FR_NO_PATH)return KUI_UPDATE_IO;
        if(full && stat==FR_OK) {
            /* Missing/bad receipts cannot prove that an old bank was never
             * complete. Preserve every nonempty candidate, including failed
             * repair instances. An empty directory is safe to reuse. */
            DIR directory;FILINFO member;
            if(f_opendir(&directory,directory_path)!=FR_OK)return KUI_UPDATE_IO;
            FRESULT read=f_readdir(&directory,&member),closed=f_closedir(&directory);
            if(read!=FR_OK || closed!=FR_OK)return KUI_UPDATE_IO;
            if(member.fname[0])continue;
        }
        if(attempt && log)log("Offline update: recovery %u/%u bank %s; original %s retained",attempt,REPAIR_BANKS,snapshot->build,canonical);
        return KUI_UPDATE_OK;
    }
    if(log)log("Offline update: all %u recovery bank identities are occupied; original banks retained",REPAIR_BANKS);
    return KUI_UPDATE_LIMIT;
}
static enum kui_update_result directory(const char *path) {
    FILINFO info;FRESULT r=f_stat(path,&info);
    if(r==FR_OK)return info.fattrib&AM_DIR?KUI_UPDATE_OK:KUI_UPDATE_PROTECTED;
    if(r!=FR_NO_FILE && r!=FR_NO_PATH)return KUI_UPDATE_IO;
    return f_mkdir(path)==FR_OK?KUI_UPDATE_OK:KUI_UPDATE_IO;
}
static enum kui_update_result directories(const struct kui_update_package *p) {
    enum kui_update_result r=directory("0:/KUI");if(!r)r=directory(KUI_UPDATE_SYSTEM);
    char path[160];
    snprintf(path,sizeof(path),KUI_UPDATE_SYSTEM "/%s",p->build);if(!r)r=directory(path);
    if(kui_update_package_version(p)==1u) {
        static const char *const tails[]={"/apps","/apps/games","/apps/disc"};
        for(unsigned i=0;!r && i<sizeof(tails)/sizeof(tails[0]);i++) {
            snprintf(path,sizeof(path),KUI_UPDATE_SYSTEM "/%s%s",p->build,tails[i]);r=directory(path);
        }
    }
    for(uint32_t i=0;!r && i<p->count;i++) {
        if(!has_payload(&p->entries[i]))continue;
        if(!kui_update_bank_path(p->build,p->entries[i].path,path,sizeof(path)))return KUI_UPDATE_ARGUMENT;
        size_t start=strlen(KUI_UPDATE_SYSTEM)+1u+strlen(p->build)+1u;
        for(char *at=path+start;!r && *at;at++)if(*at=='/') {*at=0;r=directory(path);*at='/';}
    }
    return r;
}
static enum kui_update_result quota_directory(const char *path,uint64_t cluster,uint64_t *required) {
    FILINFO info;FRESULT f=f_stat(path,&info);
    if(f==FR_OK)return info.fattrib&AM_DIR?KUI_UPDATE_OK:KUI_UPDATE_PROTECTED;
    if(f!=FR_NO_FILE && f!=FR_NO_PATH)return KUI_UPDATE_IO;
    *required+=cluster;return KUI_UPDATE_OK;
}
static enum kui_update_result quota(const struct kui_update_package *p,uint64_t *needed,uint64_t *available) {
    FATFS *fs=NULL;DWORD clusters=0;
    if(f_getfree("0:",&clusters,&fs)!=FR_OK || !fs || !fs->csize)return KUI_UPDATE_IO;
    /* Retain the metadata margin used by v1. In addition to parent clusters,
     * directory growth and the inactive activation record can allocate space
     * while the already written payload clusters are still live. */
    uint64_t cluster=(uint64_t)fs->csize*512u,required=16u*cluster,reclaim=0u;
    for(uint32_t i=0;i<p->count;i++)if(has_payload(&p->entries[i]))
        required+=((p->entries[i].bytes+cluster-1u)/cluster)*cluster;
    required+=((128u+(uint64_t)p->count*kui_update_entry_bytes(p)+cluster-1u)/cluster)*cluster;
    char path[160];FILINFO info;
    enum kui_update_result r=quota_directory("0:/KUI",cluster,&required);
    if(!r)r=quota_directory(KUI_UPDATE_SYSTEM,cluster,&required);
    snprintf(path,sizeof(path),KUI_UPDATE_SYSTEM "/%s",p->build);
    if(!r)r=quota_directory(path,cluster,&required);
    /* Count each distinct parent once, including future asset subdirectories.
     * Existing parents reduce required allocation without claiming to reclaim
     * storage occupied by another bank or by undeclared files. */
    for(uint32_t i=0;!r && i<p->count;i++)if(has_payload(&p->entries[i])) {
        const char *relative=p->entries[i].path;
        for(size_t end=0;!r && relative[end];end++)if(relative[end]=='/') {
            bool seen=false;
            for(uint32_t j=0;j<i;j++)if(has_payload(&p->entries[j]) &&
                !strncmp(p->entries[j].path,relative,end+1u)) {seen=true;break;}
            if(seen)continue;
            if(!kui_update_bank_path(p->build,relative,path,sizeof(path)))return KUI_UPDATE_ARGUMENT;
            path[strlen(KUI_UPDATE_SYSTEM)+1u+strlen(p->build)+1u+end]=0;
            r=quota_directory(path,cluster,&required);
        }
    }
    if(r)return r;
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
    if(!p || (kui_update_package_version(p)!=2u && !kui_update_package_find(p,"runtime.kui")))return KUI_UPDATE_ARGUMENT;
    bool allow_repair=(p->flags&KUI_UPDATE_FULL)!=0u;
    struct kui_update_activation a;unsigned slot;
    enum kui_update_result r=kui_update_activation_for_update(&a,&slot,cancel);
    if(r)return r;
    struct kui_update_package *bank=malloc(sizeof(*bank)),*snapshot=NULL;
    if(!bank)return KUI_UPDATE_MEMORY;
    unsigned root_engine=0u;
    if(kui_update_package_version(p)==1u) {
        r=baseline_verify(NULL,cancel,NULL,&root_engine);
        if(r==KUI_UPDATE_NOT_FOUND)r=KUI_UPDATE_OK;
        if(r) {free(bank);return r;}
    }
    if(kui_update_package_version(p)==2u || root_engine>=2u) {
        snapshot=malloc(sizeof(*snapshot));
        struct snapshot_source *sources=calloc(KUI_UPDATE_MAX_ENTRIES,sizeof(*sources));
        r=snapshot && sources?snapshot_prepare(p,&a,snapshot,sources,NULL,cancel,NULL):KUI_UPDATE_MEMORY;
        free(sources);
        if(r) {free(snapshot);free(bank);return r;}
        p=snapshot;
    }
    if(snapshot) {
        bool complete=false;r=snapshot_target(snapshot,&a,bank,allow_repair,&complete,NULL,NULL,cancel,NULL);
        if(!r && complete) {
            FATFS *fs=NULL;DWORD free_clusters=0u;
            if(f_getfree("0:",&free_clusters,&fs)!=FR_OK || !fs)r=KUI_UPDATE_IO;
            else *available=(uint64_t)free_clusters*fs->csize*512u;
        } else if(!r)r=quota(snapshot,needed,available);
        free(snapshot);free(bank);return r;
    }
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
    free(snapshot);free(bank);return r;
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
    struct kui_update_activation current;unsigned slot;bool recovering=false;
    enum kui_update_result r=activation_for_update(&current,&slot,cancel,&recovering);
    if(r)return r;
    if(!activation_equal(review,&current) || (current.sequence && slot!=review_slot))return KUI_UPDATE_STALE;
    if(!recovering && !strcmp(current.selected,build) && (!build[0] || !memcmp(current.manifest_sha256,hash,32u)))return KUI_UPDATE_OK;
    if(current.sequence==UINT64_MAX)return KUI_UPDATE_SEQUENCE;
    struct kui_update_activation next={.sequence=current.sequence+1u};
    memcpy(next.selected,build,strlen(build));
    bool withdrawn=false;r=bank_withdrawn(current.selected,&withdrawn,cancel);if(r)return r;
    if(!withdrawn)memcpy(next.previous,current.selected,sizeof(next.previous));
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
    return kui_update_activation_for_update(a,slot,cancel);
}
static enum kui_update_result activation_for_update(struct kui_update_activation *a,unsigned *slot,
    kui_cancel_fn cancel,bool *recovered) {
    if(recovered)*recovered=false;
    enum kui_update_result r=kui_update_activation_latest(a,slot,cancel);
    if(r==KUI_UPDATE_NOT_FOUND) {memset(a,0,sizeof(*a));*slot=0u;return KUI_UPDATE_OK;}
    if(r==KUI_UPDATE_FORMAT && kui_update_effective_snapshot) {
        char bank[13]={0};uint8_t hash[32]={0};
        if(kui_update_effective_snapshot(bank,hash)>0 && !bank[0]) {
            struct kui_update_activation first,second;
            enum kui_update_result ar=kui_update_activation_read(0u,&first,cancel),br=kui_update_activation_read(1u,&second,cancel);
            if(ar==KUI_UPDATE_IO || br==KUI_UPDATE_IO)return KUI_UPDATE_IO;
            if(ar==KUI_UPDATE_CANCELLED || br==KUI_UPDATE_CANCELLED)return KUI_UPDATE_CANCELLED;
            bool conflict=!ar && !br && first.sequence==second.sequence && !activation_equal(&first,&second);
            /* A valid choice appearing since the first read invalidates this
             * recovery review. A conflicting valid pair needs a higher epoch,
             * otherwise its retained record would outrank a fresh sequence1. */
            if((!ar || !br) && !conflict)return KUI_UPDATE_STALE;
            if(conflict && first.selected[0] && second.selected[0])return KUI_UPDATE_PROTECTED;
            uint64_t sequence=conflict?first.sequence:0u;
            unsigned keep=conflict && first.selected[0]?1u:0u;
            unsigned engine=0u;
            r=baseline_verify(NULL,cancel,NULL,&engine);
            unsigned required=kui_update_boot_engine_minimum?kui_update_boot_engine_minimum():0u;
            if(!r && (engine<2u || engine<required))r=KUI_UPDATE_ENGINE;
            if(!r) {
                memset(a,0,sizeof(*a));a->sequence=sequence;*slot=keep;if(recovered)*recovered=true;
            }
        }
    }
    return r;
}
enum kui_update_result kui_update_activation_for_update(struct kui_update_activation *a,unsigned *slot,kui_cancel_fn cancel) {
    return activation_for_update(a,slot,cancel,NULL);
}
static enum kui_update_result baseline_verify(struct kui_app_status *s,kui_cancel_fn cancel,
    kui_app_progress_fn progress,unsigned *engine_level) {
    if(engine_level)*engine_level=0u;
    phase(s,"Verifying immutable baseline runtime...",0u,progress);
    if(stopped(cancel))return KUI_UPDATE_CANCELLED;
    FIL file;FRESULT opened=physical_open(&file,KUI_RUNTIME_PATH,FA_READ);
    if(opened!=FR_OK)return opened==FR_NO_FILE || opened==FR_NO_PATH?KUI_UPDATE_NOT_FOUND:KUI_UPDATE_IO;
    uint8_t header[64],buffer[4096];struct kui_runtime_info info;
    enum kui_update_result r=read_exact(&file,header,sizeof(header),cancel);
    if(!r && (kui_runtime_header(header,f_size(&file),&info)!=KUI_RUNTIME_OK ||
       kui_update_build_withdrawn(info.build)))r=KUI_UPDATE_RUNTIME;
    struct capability capability={0},engine2={0},engine3={0};struct kui_update_handoff_scan handoff={0};
    uint32_t crc=0u,done=0u;
    if(!r && s)s->total=info.payload_bytes;
    while(!r && done<info.payload_bytes) {
        UINT n=info.payload_bytes-done;if(n>sizeof(buffer))n=sizeof(buffer);
        r=read_exact(&file,buffer,n,cancel);
        if(!r) {
            crc=kui_crc32(crc,buffer,n);capability_update(&capability,buffer,n);
            marker_update(&engine2,buffer,n,2u);marker_update(&engine3,buffer,n,3u);
            kui_update_handoff_scan_update(&handoff,buffer,n);done+=n;
            if(s) {s->done=done;if(progress)progress(s);}
        }
    }
    if(!r && (crc!=info.crc32 || capability.count!=1u || !kui_update_handoff_scan_valid(&handoff)))r=KUI_UPDATE_CHECKSUM;
    if(f_close(&file)!=FR_OK && !r)r=KUI_UPDATE_IO;
    if(!r && stopped(cancel))r=KUI_UPDATE_CANCELLED;
    if(!r && engine_level)*engine_level=engine_marker_level(&engine2,&engine3);
    return r;
}
static enum kui_update_result root_descriptor(struct kui_update_entry *entry,kui_cancel_fn cancel) {
    FIL file;FRESULT opened=physical_open(&file,KUI_RUNTIME_PATH,FA_READ);
    if(opened!=FR_OK)return opened==FR_NO_FILE || opened==FR_NO_PATH?KUI_UPDATE_NOT_FOUND:KUI_UPDATE_IO;
    uint8_t buffer[4096],header[64];struct kui_runtime_info info;
    enum kui_update_result r=read_exact(&file,header,sizeof(header),cancel);
    if(!r && kui_runtime_header(header,f_size(&file),&info)!=KUI_RUNTIME_OK)r=KUI_UPDATE_RUNTIME;
    struct kui_sha256 sha;kui_sha256_init(&sha);
    if(!r) {
        memset(entry,0,sizeof(*entry));strcpy(entry->path,"runtime.kui");
        entry->policy=KUI_UPDATE_SELF_KUI;entry->bytes=f_size(&file);memcpy(entry->build,info.build,13u);
        kui_sha256_update(&sha,header,sizeof(header));
        uint64_t done=sizeof(header);
        while(!r && done<entry->bytes) {
            UINT n=(UINT)(entry->bytes-done>sizeof(buffer)?sizeof(buffer):entry->bytes-done);
            r=read_exact(&file,buffer,n,cancel);
            if(!r) {kui_sha256_update(&sha,buffer,n);done+=n;}
        }
        if(!r)kui_sha256_digest(&sha,entry->sha256);
    }
    if(f_close(&file)!=FR_OK && !r)r=KUI_UPDATE_IO;
    return r;
}
static enum kui_update_result snapshot_put(struct kui_update_package *snapshot,
    struct snapshot_source *sources,const struct kui_update_entry *entry,const struct snapshot_source *source) {
    uint32_t i=0u;
    while(i<snapshot->count && strcmp(snapshot->entries[i].path,entry->path))++i;
    if(i==snapshot->count) {
        if(i==KUI_UPDATE_MAX_ENTRIES)return KUI_UPDATE_LIMIT;
        ++snapshot->count;
    }
    snapshot->entries[i]=*entry;
    if(sources)sources[i]=*source;
    return KUI_UPDATE_OK;
}
static int snapshot_order(const char *a,const char *b) {
    if(!strcmp(a,"runtime.kui"))return strcmp(b,"runtime.kui")?-1:0;
    if(!strcmp(b,"runtime.kui"))return 1;
    return strcmp(a,b);
}
static enum kui_update_result snapshot_sources_verify(const struct kui_update_package *snapshot,
    const struct snapshot_source *sources,struct kui_app_status *s,kui_cancel_fn cancel,kui_app_progress_fn progress) {
    uint64_t bytes=0u;
    for(uint32_t i=0;i<snapshot->count;i++)if(!sources[i].package && has_payload(&snapshot->entries[i]))bytes+=snapshot->entries[i].bytes;
    phase(s,"Checking retained software snapshot...",bytes,progress);
    uint8_t *buffer=malloc(CHUNK);if(!buffer)return KUI_UPDATE_MEMORY;
    enum kui_update_result r=KUI_UPDATE_OK;
    for(uint32_t i=0;!r && i<snapshot->count;i++) {
        const struct kui_update_entry *entry=&snapshot->entries[i];
        if(sources[i].package || !has_payload(entry))continue;
        FIL file;FRESULT opened=physical_open(&file,sources[i].path,FA_READ);
        if(opened!=FR_OK) {r=opened==FR_NO_FILE || opened==FR_NO_PATH?KUI_UPDATE_NOT_FOUND:KUI_UPDATE_IO;break;}
        if(f_size(&file)!=entry->bytes)r=KUI_UPDATE_CHECKSUM;
        if(!r)r=admit(&file,sources[i].offset,entry,snapshot->build,!strcmp(entry->path,"runtime.kui"),kui_update_engine_minimum(snapshot),
            NULL,buffer,s,cancel,progress);
        if(f_close(&file)!=FR_OK && !r)r=KUI_UPDATE_IO;
    }
    free(buffer);return r;
}
static enum kui_update_result snapshot_prepare(const struct kui_update_package *incoming,
    const struct kui_update_activation *active,struct kui_update_package *snapshot,
    struct snapshot_source *sources,struct kui_app_status *s,kui_cancel_fn cancel,kui_app_progress_fn progress) {
    uint8_t *metadata=malloc(KUI_UPDATE_RECEIPT_MAX);size_t metadata_bytes=0u;
    if(!metadata)return KUI_UPDATE_MEMORY;
    enum kui_update_result r=kui_update_receipt_encode(incoming,metadata,KUI_UPDATE_RECEIPT_MAX,&metadata_bytes);
    free(metadata);if(r)return r;
    /* Saved activation may name a pending selection or a rejected bank after
     * boot fallback. Partial deliveries inherit the software actually running;
     * the caller retains the saved record solely for guarded publication. */
    struct kui_update_activation base=*active;
    if(!(incoming->flags&KUI_UPDATE_FULL) && kui_update_effective_snapshot) {
        char bank[13]={0};uint8_t hash[32]={0};
        int known=kui_update_effective_snapshot(bank,hash);
        if(known<0)return KUI_UPDATE_IO;
        if(known>0) {
            if(bank[12] || (bank[0] && !kui_update_build_valid(bank)))return KUI_UPDATE_IO;
            memset(base.selected,0,sizeof(base.selected));memset(base.manifest_sha256,0,sizeof(base.manifest_sha256));
            if(bank[0]) {memcpy(base.selected,bank,sizeof(base.selected));memcpy(base.manifest_sha256,hash,sizeof(hash));}
        }
    }
    unsigned engine=0u;
    r=baseline_verify(s,cancel,progress,&engine);
    unsigned required=kui_update_engine_minimum(incoming);if(required<2u)required=2u;
    unsigned running_required=kui_update_boot_engine_minimum?kui_update_boot_engine_minimum():0u;
    if(required<running_required)required=running_required;
    if(!r && engine<required)r=KUI_UPDATE_ENGINE;
    if(r)return r;
    memset(snapshot,0,sizeof(*snapshot));snapshot->version=2u;snapshot->flags=incoming->flags;
    /* A snapshot names applied software, independent of the incoming package's
     * delivery channel, build label, ordering, or source configuration. */
    memcpy(snapshot->build,"000000000000",13u);
    struct kui_update_package *prior=malloc(sizeof(*prior));
    if(!prior)return KUI_UPDATE_MEMORY;
    bool withdrawn=false;
    if(!(incoming->flags&KUI_UPDATE_FULL))r=bank_withdrawn(base.selected,&withdrawn,cancel);
    if(!(incoming->flags&KUI_UPDATE_FULL) && base.selected[0] && !withdrawn && !r) {
        r=kui_update_bank_receipt_read(base.selected,prior,cancel);
        if(!r && memcmp(base.manifest_sha256,prior->manifest_sha256,32u))r=KUI_UPDATE_STALE;
        if(!r)snapshot->flags=prior->flags;
        for(uint32_t i=0;!r && i<prior->count;i++) {
            struct kui_update_entry entry=prior->entries[i];struct snapshot_source source={0};
            if(entry.policy==KUI_UPDATE_INHERIT_ROOT)continue;
            if(has_payload(&entry)) {
                if(entry.policy!=KUI_UPDATE_RAW) {
                    const char *build=kui_update_entry_build(prior->build,&prior->entries[i]);
                    if(!build) {r=KUI_UPDATE_RUNTIME;break;}
                    memcpy(entry.build,build,13u);entry.policy=KUI_UPDATE_SELF_KUI;
                }
                if(!kui_update_bank_path(base.selected,entry.path,source.path,sizeof(source.path))) {
                    r=KUI_UPDATE_ARGUMENT;break;
                }
            }
            r=snapshot_put(snapshot,sources,&entry,&source);
        }
    }
    free(prior);
    if(r)return r;
    if(engine>=3u)snapshot->flags|=KUI_UPDATE_REQUIRES_ENGINE3;
    if(engine<kui_update_engine_minimum(snapshot))return KUI_UPDATE_ENGINE;
    bool have_gui=false;
    for(uint32_t i=0;i<snapshot->count;i++)if(!strcmp(snapshot->entries[i].path,"runtime.kui"))have_gui=true;
    bool incoming_gui=false;
    for(uint32_t i=0;i<incoming->count;i++)if(!strcmp(incoming->entries[i].path,"runtime.kui") &&
        incoming->entries[i].policy!=KUI_UPDATE_KEEP)incoming_gui=true;
    if(!have_gui && !incoming_gui) {
        struct kui_update_entry entry;struct snapshot_source source={0};
        r=root_descriptor(&entry,cancel);if(r)return r;
        strcpy(source.path,KUI_RUNTIME_PATH);r=snapshot_put(snapshot,sources,&entry,&source);
    }
    for(uint32_t i=0;!r && i<incoming->count;i++) {
        struct kui_update_entry entry=incoming->entries[i];
        if(entry.policy==KUI_UPDATE_INHERIT_ROOT)continue;
        if(kui_update_package_version(incoming)==1u) {
            const char *build=kui_update_entry_build(incoming->build,&incoming->entries[i]);
            if(!build) {r=KUI_UPDATE_RUNTIME;break;}
            memcpy(entry.build,build,13u);entry.policy=KUI_UPDATE_SELF_KUI;
        }
        struct snapshot_source source={.package=true,.offset=entry.payload_offset};
        r=snapshot_put(snapshot,sources,&entry,&source);
    }
    if(r)return r;
    for(uint32_t i=1u;i<snapshot->count;i++) {
        struct kui_update_entry entry=snapshot->entries[i];struct snapshot_source source={0};
        if(sources)source=sources[i];
        uint32_t j=i;
        while(j && snapshot_order(snapshot->entries[j-1u].path,entry.path)>0) {
            snapshot->entries[j]=snapshot->entries[j-1u];if(sources)sources[j]=sources[j-1u];--j;
        }
        snapshot->entries[j]=entry;if(sources)sources[j]=source;
    }
    uint64_t payload=0u;
    for(uint32_t i=0;i<snapshot->count;i++) {
        if(snapshot->entries[i].bytes>KUI_UPDATE_MAX_PAYLOAD-payload)return KUI_UPDATE_LIMIT;
        payload+=snapshot->entries[i].bytes;
    }
    r=kui_update_package_seal(snapshot);
    if(!r) {
        static const char hex[]="0123456789abcdef";uint8_t identity[32];struct kui_sha256 hash;
        kui_sha256_init(&hash);kui_sha256_update(&hash,"KUISNAP2",8u);
        kui_sha256_update(&hash,&snapshot->flags,1u);kui_sha256_update(&hash,snapshot->manifest_sha256,32u);
        kui_sha256_digest(&hash,identity);
        for(unsigned i=0;i<6u;i++) {snapshot->build[i*2u]=hex[identity[i]>>4u];
            snapshot->build[i*2u+1u]=hex[identity[i]&15u];}
        snapshot->build[12]=0;
    }
    if(!r)r=snapshot_sources_verify(snapshot,sources,s,cancel,progress);
    return r;
}
static enum kui_update_result final_selection(enum kui_update_result r,struct kui_app_status *s,
    kui_log_fn log,kui_app_progress_fn progress,const char *message) {
    if(r==KUI_UPDATE_IO && s && s->complete && strstr(s->message,"Activation could not"))return r;
    return finish(r,s,log,progress,message);
}
enum kui_update_result kui_update_target_verify(const char *build,struct kui_update_package *out,
    struct kui_app_status *s,kui_log_fn log,kui_cancel_fn cancel,kui_app_progress_fn progress) {
    if(out)memset(out,0,sizeof(*out));
    phase(s,"Checking restore target and updater engine...",0u,progress);
    if(!build || (build[0] && (!kui_update_build_valid(build) || !out)))
        return finish(KUI_UPDATE_ARGUMENT,s,log,progress,"");
    unsigned root_engine=0u;
    enum kui_update_result r=baseline_verify(NULL,cancel,NULL,&root_engine);
    unsigned minimum=kui_update_boot_engine_minimum?kui_update_boot_engine_minimum():0u;
    if(minimum<root_engine)minimum=root_engine;
    if(!r && root_engine<minimum)r=KUI_UPDATE_ENGINE;
    if(!r && build[0])r=bank_verify_minimum(build,out,minimum,s,log,cancel,progress);
    return finish(r,s,log,progress,"Restore target verified.");
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
        else r=kui_update_target_verify(build,p,s,log,cancel,progress);
        if(!r && memcmp(expected,p->manifest_sha256,32u))r=KUI_UPDATE_STALE;
    }
    if(!r && !build[0])r=kui_update_target_verify(build,NULL,s,log,cancel,progress);
    if(!r)r=publish(build,expected,&a,slot,s,log,cancel,progress);
    free(p);return final_selection(r,s,log,progress,"Selected update version saved; restart to use it.");
}
enum kui_update_result kui_update_rollback(struct kui_app_status *s,kui_log_fn log,
    kui_cancel_fn cancel,kui_app_progress_fn progress) {
    phase(s,"Checking previous update version...",0u,progress);
    struct kui_update_activation a;unsigned slot;enum kui_update_result r=current(&a,&slot,cancel);
    struct kui_update_package *p=NULL;uint8_t *hash=NULL;
    bool withdrawn=false;if(!r)r=bank_withdrawn(a.previous,&withdrawn,cancel);
    const char *target=!r && !withdrawn?a.previous:"";
    if(!r && target[0]) {
        p=malloc(sizeof(*p));if(!p)r=KUI_UPDATE_MEMORY;
        else r=kui_update_target_verify(target,p,s,log,cancel,progress);
        if(!r)hash=p->manifest_sha256;
    }
    if(!r && !target[0])r=kui_update_target_verify(target,NULL,s,log,cancel,progress);
    if(!r)r=publish(target,hash,&a,slot,s,log,cancel,progress);
    free(p);return final_selection(r,s,log,progress,"Previous version selected; restart to use it.");
}
static enum kui_update_result install_snapshot(const char *path,const struct kui_update_pin *expected,
    const struct kui_update_package *incoming,const struct kui_update_activation *active,unsigned slot,
    struct kui_app_status *s,kui_log_fn log,kui_cancel_fn cancel,kui_app_progress_fn progress) {
    struct kui_update_package *snapshot=malloc(sizeof(*snapshot)),*other=malloc(sizeof(*other));
    struct snapshot_source *sources=calloc(KUI_UPDATE_MAX_ENTRIES,sizeof(*sources));
    uint8_t *buffer=NULL,*receipt=NULL;FIL package;bool package_open=false;
    enum kui_update_result r=snapshot && other && sources?KUI_UPDATE_OK:KUI_UPDATE_MEMORY;
    if(!r)r=snapshot_prepare(incoming,active,snapshot,sources,s,cancel,progress);
    if(!r && kui_update_engine_minimum(snapshot)>kui_update_engine_minimum(incoming)) {
        const struct kui_update_entry *gui=kui_update_package_find(incoming,"runtime.kui");
        if(gui && has_payload(gui)) {
            buffer=malloc(CHUNK);if(!buffer)r=KUI_UPDATE_MEMORY;
            if(!r && f_open(&package,path,FA_READ)!=FR_OK)r=KUI_UPDATE_IO;
            else if(!r) {
                r=package_read(&package,false,other,cancel);
                if(!r && !kui_update_pin_matches(expected,other))r=KUI_UPDATE_STALE;
                if(!r)r=admit(&package,gui->payload_offset,gui,incoming->build,true,
                    kui_update_engine_minimum(snapshot),NULL,buffer,NULL,cancel,NULL);
                if(f_close(&package)!=FR_OK && !r)r=KUI_UPDATE_IO;
            }
            free(buffer);buffer=NULL;
        }
    }
    bool complete=false;
    if(!r)r=snapshot_target(snapshot,active,other,(incoming->flags&KUI_UPDATE_FULL)!=0u,&complete,s,log,cancel,progress);
    struct kui_update_pin snapshot_pin;
    if(!r)kui_update_package_pin(snapshot,&snapshot_pin);
    uint64_t required=0u,available=0u;
    if(!r && !complete)r=quota(snapshot,&required,&available);
    if(!r && !complete)r=directories(snapshot);
    if(!r && !complete)r=clear_partial(snapshot,cancel);
    if(!r && !complete) {
        if(f_open(&package,path,FA_READ)!=FR_OK)r=KUI_UPDATE_IO;
        else {package_open=true;r=package_read(&package,false,other,cancel);
            if(!r && !kui_update_pin_matches(expected,other))r=KUI_UPDATE_STALE;}
        buffer=malloc(CHUNK);if(!buffer && !r)r=KUI_UPDATE_MEMORY;
        phase(s,"Installing and rereading complete software snapshot...",snapshot->payload_bytes*2u,progress);
    }
    for(uint32_t i=0;!r && !complete && i<snapshot->count;i++) {
        const struct kui_update_entry *entry=&snapshot->entries[i];
        if(stopped(cancel)) {r=KUI_UPDATE_CANCELLED;break;}
        if(!has_payload(entry))continue;
        char destination_path[160];
        if(!kui_update_bank_path(snapshot->build,entry->path,destination_path,sizeof(destination_path))) {
            r=KUI_UPDATE_ARGUMENT;break;
        }
        FIL input,*source=&package;bool input_open=false;
        if(!sources[i].package) {
            FRESULT opened=physical_open(&input,sources[i].path,FA_READ);
            if(opened!=FR_OK) {r=opened==FR_NO_FILE || opened==FR_NO_PATH?KUI_UPDATE_NOT_FOUND:KUI_UPDATE_IO;break;}
            input_open=true;source=&input;
            if(f_size(source)!=entry->bytes)r=KUI_UPDATE_CHECKSUM;
        }
        FIL destination;bool destination_open=false;
        if(!r) {
            if(f_open(&destination,destination_path,FA_WRITE|FA_CREATE_ALWAYS)!=FR_OK)r=KUI_UPDATE_IO;
            else destination_open=true;
        }
        if(!r)r=admit(source,sources[i].offset,entry,snapshot->build,!strcmp(entry->path,"runtime.kui"),
            kui_update_engine_minimum(snapshot),&destination,buffer,s,cancel,progress);
        if(destination_open) {
            if(f_sync(&destination)!=FR_OK)r=KUI_UPDATE_IO;
            if(f_close(&destination)!=FR_OK)r=KUI_UPDATE_IO;
        }
        if(input_open && f_close(&input)!=FR_OK && !r)r=KUI_UPDATE_IO;
        if(!r)r=verify_member(snapshot->build,entry,!strcmp(entry->path,"runtime.kui"),kui_update_engine_minimum(snapshot),buffer,s,cancel,progress);
    }
    if(package_open && f_close(&package)!=FR_OK && !r)r=KUI_UPDATE_IO;
    if(!r && !complete) {
        receipt=malloc(KUI_UPDATE_RECEIPT_MAX);size_t bytes=0u;
        if(!receipt)r=KUI_UPDATE_MEMORY;
        else r=kui_update_receipt_encode(snapshot,receipt,KUI_UPDATE_RECEIPT_MAX,&bytes);
        if(!r && stopped(cancel))r=KUI_UPDATE_CANCELLED;
        if(!r) {
            char receipt_path[160];snprintf(receipt_path,sizeof(receipt_path),KUI_UPDATE_SYSTEM "/%s/package.bin",snapshot->build);
            r=record_write(receipt_path,receipt,bytes,false);
        }
        if(!r)r=kui_update_bank_receipt_read(snapshot->build,other,cancel);
        if(!r && !kui_update_pin_matches(&snapshot_pin,other))r=KUI_UPDATE_STALE;
    }
    if(!r)r=publish(snapshot->build,snapshot->manifest_sha256,active,slot,s,log,cancel,progress);
    free(receipt);free(buffer);free(sources);free(other);free(snapshot);
    return r;
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
    if(!r && kui_update_package_version(p)==2u) {
        r=install_snapshot(path,expected,p,&active,slot,s,log,cancel,progress);
        free(other);free(p);
        return final_selection(r,s,log,progress,"Update installed and selected; restart to use it.");
    }
    /* Once a capable physical root exists, a legacy delivery cannot replace
     * its GUI with code that loses the current updater engine. Historical v1
     * banks remain readable for verification and explicit rollback. */
    unsigned root_engine=0u;
    if(!r) {
        enum kui_update_result root=baseline_verify(NULL,cancel,NULL,&root_engine);
        if(root!=KUI_UPDATE_NOT_FOUND && root!=KUI_UPDATE_OK)r=root;
    }
    if(!r && root_engine>=2u) {
        /* Legacy wire packages remain usable after engine migration. Their
         * policy2 rows preserve the effective selected software, and exact
         * legacy replacements are normalized into the new snapshot model. */
        r=install_snapshot(path,expected,p,&active,slot,s,log,cancel,progress);
        free(other);free(p);
        return final_selection(r,s,log,progress,"Update installed and selected; restart to use it.");
    }
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
    if(!r && !complete)r=directories(p);
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
        r=admit(&source,p->entries[i].payload_offset,&p->entries[i],p->build,i==0u,root_engine,&destination,buffer,s,cancel,progress);
        if(f_sync(&destination)!=FR_OK)r=KUI_UPDATE_IO;
        if(f_close(&destination)!=FR_OK)r=KUI_UPDATE_IO;
        if(!r)r=verify_member(p->build,&p->entries[i],i==0u,0u,buffer,s,cancel,progress);
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
