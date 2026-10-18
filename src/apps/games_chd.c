/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/games_chd.h"
#include "kui/chd_lib.h"
#include "kui/chd_cache.h"
#include "kui/hash.h"
#include "platform.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CACHE_ROOT "/KUI/Cache/CHD"
#define STAMP_HEADER 80u
#define STAMP_TRACK 40u
#define STAMP_CAP (STAMP_HEADER + KUI_CHD_CACHE_TRACK_MAX * STAMP_TRACK + 4u)
static const uint8_t stamp_magic[8]={'K','C','H','D','C','0','1',0};
struct chd_files {
    FIL source,output;
    struct kui_chdl *reader;
    struct kui_chd_cache_plan plan;
    uint8_t identity[32],modes[KUI_CHD_CACHE_TRACK_MAX];
    char cue[KUI_CHD_CACHE_GDI_BYTES];
    size_t cue_bytes;
    bool gdrom;
    char final[KUI_DEST_ROOT_CAP],stage[KUI_DEST_ROOT_CAP];
    bool source_open,output_open,stage_created,published;
    bool created[KUI_CHD_CACHE_TRACK_MAX],gdi_created,cue_created,stamp_created;
    unsigned output_track;
    kui_log_fn log;
    kui_cancel_fn cancel;
    kui_app_progress_fn progress;
};
static bool stopped(struct chd_files *f) {return f->cancel && f->cancel();}
static bool qualify(char out[KUI_DEST_PATH_CAP],const char *path) {
    int n=snprintf(out,KUI_DEST_PATH_CAP,"0:%s",path);
    return n>0 && n<(int)KUI_DEST_PATH_CAP;
}
static bool child(char out[KUI_DEST_PATH_CAP],const char *dir,const char *name) {
    int n=snprintf(out,KUI_DEST_PATH_CAP,"0:%s/%s",dir,name);
    return n>0 && n<(int)KUI_DEST_PATH_CAP;
}
static bool exact_read(FIL *file,uint64_t at,void *out,size_t bytes) {
    if(at>(uint64_t)(FSIZE_t)-1 || bytes>UINT32_MAX || at>f_size(file) ||
       bytes>f_size(file)-at || f_lseek(file,(FSIZE_t)at)!=FR_OK) return false;
    UINT got=0;return f_read(file,out,(UINT)bytes,&got)==FR_OK && got==bytes;
}
static bool exact_write(FIL *file,uint64_t at,const void *data,size_t bytes) {
    if(at>(uint64_t)(FSIZE_t)-1 || bytes>UINT32_MAX ||
       bytes>(uint64_t)(FSIZE_t)-1-at || f_lseek(file,(FSIZE_t)at)!=FR_OK) return false;
    UINT put=0;return f_write(file,data,(UINT)bytes,&put)==FR_OK && put==bytes;
}
static enum kui_chdl_result source_read(void *ctx,uint64_t at,void *data,size_t bytes) {
    struct chd_files *f=ctx;
    if(stopped(f)) return KUI_CHDL_CANCELLED;
    return exact_read(&f->source,at,data,bytes)?KUI_CHDL_OK:KUI_CHDL_IO;
}
static int source_cancel(void *ctx) {return stopped(ctx);}
static void put32(uint8_t *p,uint32_t value) {
    for(unsigned i=0;i<4;i++) p[i]=(uint8_t)(value>>(i*8u));
}
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void put64(uint8_t *p,uint64_t value) {
    put32(p,(uint32_t)value);put32(p+4,(uint32_t)(value>>32));
}
static uint64_t get64(const uint8_t *p) {return get32(p)|(uint64_t)get32(p+4)<<32;}
static bool source_close(struct chd_files *f) {
    kui_chdl_close(f->reader);f->reader=NULL;
    bool ok=true;
    if(f->source_open) {ok=f_close(&f->source)==FR_OK;f->source_open=false;}
    return ok;
}
/* A GDI always describes GD media to the established reader. CD caches also
 * need a CUE so the boot session and audio inventory keep their CD identity.
 * The admitted CHD profile has no pregaps/postgaps; separate zero-index FILE
 * stanzas therefore retain the exact cumulative disc LBAs. */
static bool cue_prepare(struct chd_files *f) {
    f->cue_bytes=0;f->cue[0]=0;
    if(f->gdrom) return true;
    uint32_t disc_lba=0;
    for(unsigned i=0;i<f->plan.count;i++) {
        const struct kui_chd_cache_track *t=&f->plan.tracks[i];
        if(t->disc_lba!=disc_lba) return false;
        disc_lba+=t->frames;
        char mode[16];
        if(!t->control) strcpy(mode,"AUDIO");
        else {
            if((f->modes[i]!=1u && f->modes[i]!=2u) ||
               (t->sector_bytes!=2048u && t->sector_bytes!=2352u)) return false;
            int n=snprintf(mode,sizeof(mode),"MODE%u/%u",(unsigned)f->modes[i],(unsigned)t->sector_bytes);
            if(n<0 || (size_t)n>=sizeof(mode)) return false;
        }
        size_t left=sizeof(f->cue)-f->cue_bytes;
        int n=snprintf(f->cue+f->cue_bytes,left,
            "FILE \"track%02u.bin\" BINARY\n  TRACK %02u %s\n    INDEX 01 00:00:00\n",
            i+1u,i+1u,mode);
        if(n<0 || (size_t)n>=left) return false;
        f->cue_bytes+=(size_t)n;
    }
    return f->cue_bytes!=0;
}
/* Cache identity follows normal filesystem change detection, plus the whole
 * CHD header (including content/metadata SHA1s). It is not a full-source hash
 * on every visit. Conversion still decodes every exported sector and verifies
 * every output byte. Version salt prevents reusing older conversion rules. */
static bool source_open(struct chd_files *f,const char *path,const char **problem) {
    char normalized[KUI_DEST_JOB_CAP],qualified[KUI_DEST_PATH_CAP];
    if(!kui_destination_job_normalize(normalized,path) || strcmp(normalized,path) ||
       !qualify(qualified,path)) {*problem="Invalid CHD source path";return false;}
    FILINFO stat;
    if(f_stat(qualified,&stat)!=FR_OK || (stat.fattrib&AM_DIR) ||
       f_open(&f->source,qualified,FA_READ)!=FR_OK) {*problem="Cannot open CHD source";return false;}
    f->source_open=true;
    uint8_t header[124],size_time[16];
    if(f_size(&f->source)!=stat.fsize || stat.fsize<sizeof(header) ||
       !exact_read(&f->source,0,header,sizeof(header))) {*problem="Cannot read CHD header";return false;}
    struct kui_chdl_io io={f,source_read,source_cancel};
    enum kui_chdl_result result=kui_chdl_open(&io,stat.fsize,&f->reader);
    if(result!=KUI_CHDL_OK) {*problem=kui_chdl_result_name(result);return false;}
    const struct kui_chdl_info *info=kui_chdl_get_info(f->reader);
    if(!info || !info->track_count || info->track_count>KUI_CHD_CACHE_TRACK_MAX) {
        *problem="Unsupported CHD track inventory";return false;
    }
    f->plan.count=info->track_count;f->gdrom=info->gdrom!=0;
    for(unsigned i=0;i<info->track_count;i++) {
        const struct kui_chdl_track *t=&info->tracks[i];
        if(t->frames<=t->pad) {*problem="CHD track contains no captured sectors";return false;}
        if((uint64_t)(t->frames-t->pad)*t->sector_bytes>UINT32_MAX) {
            *problem="CHD track exceeds the supported cache file size";return false;
        }
        f->plan.tracks[i]=(struct kui_chd_cache_track){t->disc_lba,t->frames-t->pad,t->sector_bytes,t->control};
        f->modes[i]=t->mode;
    }
    uint64_t bound;
    if(kui_chd_cache_bound(&f->plan,&bound)!=KUI_CHD_CACHE_OK) {
        *problem="CHD tracks cannot be represented by this GDI cache";return false;
    }
    if(!cue_prepare(f)) {*problem="CHD CD track geometry cannot be represented by CUE";return false;}
    put64(size_time,stat.fsize);put32(size_time+8,stat.fdate);put32(size_time+12,stat.ftime);
    struct kui_sha256 hash;kui_sha256_init(&hash);
    static const char salt[]="K-UI CHD cache 1";
    kui_sha256_update(&hash,salt,sizeof(salt));
    kui_sha256_update(&hash,path,strlen(path)+1u);
    kui_sha256_update(&hash,size_time,sizeof(size_time));
    kui_sha256_update(&hash,header,sizeof(header));
    /* Bind the normalized geometry too, without hashing C struct padding. */
    for(unsigned i=0;i<f->plan.count;i++) {
        const struct kui_chd_cache_track *t=&f->plan.tracks[i];
        uint8_t geometry[16];put32(geometry,t->disc_lba);put32(geometry+4,t->frames);
        put32(geometry+8,t->sector_bytes);put32(geometry+12,t->control);
        kui_sha256_update(&hash,geometry,sizeof(geometry));
    }
    kui_sha256_digest(&hash,f->identity);
    char hex[65];kui_hex(f->identity,32,hex);
    int n=snprintf(f->final,sizeof(f->final),CACHE_ROOT "/%s",hex);
    if(n<0 || (size_t)n>=sizeof(f->final)) {*problem="CHD cache path too long";return false;}
    return true;
}
static uint64_t track_bytes(const struct chd_files *f,unsigned index) {
    return (uint64_t)f->plan.tracks[index].frames*f->plan.tracks[index].sector_bytes;
}
static void track_name(char name[KUI_CHD_CACHE_NAME_BYTES],unsigned index) {
    memcpy(name,"track00.bin",sizeof("track00.bin"));
    unsigned number=index+1u;
    name[5]=(char)('0'+number/10u);name[6]=(char)('0'+number%10u);
}
static bool ready(struct chd_files *f) {
    uint8_t stamp[STAMP_CAP];char path[KUI_DEST_PATH_CAP];FIL file;
    size_t length=STAMP_HEADER+f->plan.count*STAMP_TRACK+4u;
    if(!child(path,f->final,"cache.bin") || f_open(&file,path,FA_READ)!=FR_OK) return false;
    bool ok=f_size(&file)==length && exact_read(&file,0,stamp,length);
    if(f_close(&file)!=FR_OK) ok=false;
    if(!ok || memcmp(stamp,stamp_magic,8) || memcmp(stamp+8,f->identity,32) ||
       get32(stamp+40)!=f->plan.count || get32(stamp+44)>KUI_CHD_CACHE_GDI_BYTES ||
       get32(stamp+length-4)!=kui_crc32(0,stamp,length-4)) return false;
    for(unsigned i=0;i<f->plan.count;i++) {
        char name[KUI_CHD_CACHE_NAME_BYTES];FILINFO info;track_name(name,i);
        if(stopped(f) || get64(stamp+STAMP_HEADER+i*STAMP_TRACK)!=track_bytes(f,i) ||
           !child(path,f->final,name) || f_stat(path,&info)!=FR_OK ||
           (info.fattrib&AM_DIR) || info.fsize!=track_bytes(f,i)) return false;
    }
    if(!child(path,f->final,"disc.gdi") || f_open(&file,path,FA_READ)!=FR_OK) return false;
    uint8_t data[512],digest[32];struct kui_sha256 hash;kui_sha256_init(&hash);
    uint64_t size=get32(stamp+44);ok=size && f_size(&file)==size;
    for(uint64_t at=0;ok && at<size;) {
        size_t take=size-at>sizeof(data)?sizeof(data):(size_t)(size-at);
        ok=!stopped(f) && exact_read(&file,at,data,take);
        if(ok) {kui_sha256_update(&hash,data,take);at+=take;}
    }
    if(f_close(&file)!=FR_OK) ok=false;
    kui_sha256_digest(&hash,digest);
    if(!ok || memcmp(digest,stamp+48,32)) return false;
    if(f->gdrom) return true;
    /* CUE geometry is regenerated from the current source. Exact comparison
     * validates its media/mode/index semantics without changing stamp v1. */
    if(!child(path,f->final,"disc.cue") || f_open(&file,path,FA_READ)!=FR_OK) return false;
    ok=f->cue_bytes && f_size(&file)==f->cue_bytes;
    for(size_t at=0;ok && at<f->cue_bytes;) {
        size_t take=f->cue_bytes-at>sizeof(data)?sizeof(data):f->cue_bytes-at;
        ok=!stopped(f) && exact_read(&file,at,data,take) && !memcmp(data,f->cue+at,take);
        at+=take;
    }
    if(f_close(&file)!=FR_OK) ok=false;
    return ok;
}
bool kui_games_chd_inspect_mounted(const char *path,struct kui_games_detail *out,
    kui_log_fn log,kui_cancel_fn cancel) {
    if(!out) return false;
    out->chd=true;out->chd_ready=false;out->chd_prepare=false;out->cache_path[0]=0;
    struct chd_files *f=calloc(1,sizeof(*f));
    const char *problem="Insufficient memory for CHD inspection";bool ok=false;
    if(!f) goto done;
    f->log=log;f->cancel=cancel;
    if(!source_open(f,path,&problem)) goto close;
    const struct kui_chdl_info *info=kui_chdl_get_info(f->reader);
    out->tracks=info->track_count;out->bytes=info->file_bytes;
    for(unsigned i=0;i<f->plan.count;i++)
        if(f->plan.tracks[i].control==4) ++out->data_tracks;else ++out->audio_tracks;
    uint64_t bound=0;kui_chd_cache_bound(&f->plan,&bound);
    out->cache_bytes=bound+f->cue_bytes;out->chd_ready=ready(f);out->chd_prepare=true;
    if(out->chd_ready) {
        snprintf(out->cache_path,sizeof(out->cache_path),"%s/disc.%s",f->final,f->gdrom?"gdi":"cue");
        problem="Verified CHD cache ready";
    } else problem="CHD ready to prepare; expanded SD space required";
    if(log) log("CHD v%lu, %lu-byte hunks, %lu tracks; cache=%s; expanded=%" PRIu64,
        (unsigned long)info->version,(unsigned long)info->hunk_bytes,
        (unsigned long)info->track_count,out->chd_ready?"ready":"not prepared",bound);
    ok=true;
close:
    if(!source_close(f)) {ok=false;out->chd_ready=false;out->chd_prepare=false;problem="Cannot close CHD source";}
    free(f);
done:
    if(cancel && cancel()) {out->stopped=true;out->chd_prepare=false;ok=false;problem="CHD inspection stopped";}
    snprintf(out->message,sizeof(out->message),"%s",problem);return ok;
}
static bool cache_read_track(void *ctx,unsigned index,uint32_t first,uint32_t count,void *data,size_t bytes) {
    struct chd_files *f=ctx;
    enum kui_chdl_result result=kui_chdl_read_track(f->reader,index,first,count,data,bytes);
    if(result!=KUI_CHDL_OK && f->log) f->log("CHD decode track %u sector %lu: %s",index+1u,
        (unsigned long)first,kui_chdl_result_name(result));
    return result==KUI_CHDL_OK;
}
static bool cache_create(void *ctx,unsigned index,const char *name,uint64_t bytes) {
    struct chd_files *f=ctx;char path[KUI_DEST_PATH_CAP];
    if(f->output_open || index>=f->plan.count || bytes!=track_bytes(f,index) ||
       !child(path,f->stage,name) || f_open(&f->output,path,FA_READ|FA_WRITE|FA_CREATE_NEW)!=FR_OK) return false;
    f->created[index]=true;f->output_open=true;f->output_track=index;return true;
}
static bool cache_write(void *ctx,unsigned index,uint64_t at,const void *data,size_t bytes) {
    struct chd_files *f=ctx;
    return f->output_open && f->output_track==index && exact_write(&f->output,at,data,bytes);
}
static bool cache_read(void *ctx,unsigned index,uint64_t at,void *data,size_t bytes) {
    struct chd_files *f=ctx;
    return f->output_open && f->output_track==index && exact_read(&f->output,at,data,bytes);
}
static bool cache_sync(void *ctx,unsigned index) {
    struct chd_files *f=ctx;return f->output_open && f->output_track==index && f_sync(&f->output)==FR_OK;
}
static bool cache_finish(void *ctx,unsigned index,uint64_t bytes) {
    struct chd_files *f=ctx;
    if(!f->output_open || f->output_track!=index) return false;
    bool ok=f_size(&f->output)==bytes;
    if(f_close(&f->output)!=FR_OK) ok=false;
    f->output_open=false;return ok;
}
static bool write_small(struct chd_files *f,const char *name,const void *data,size_t bytes,bool *created) {
    char path[KUI_DEST_PATH_CAP];FIL file;
    if(!child(path,f->stage,name) || f_open(&file,path,FA_READ|FA_WRITE|FA_CREATE_NEW)!=FR_OK) return false;
    *created=true;
    bool ok=exact_write(&file,0,data,bytes) && f_sync(&file)==FR_OK && f_size(&file)==bytes;
    uint8_t verify[512];
    for(size_t at=0;ok && at<bytes;) {
        size_t take=bytes-at>sizeof(verify)?sizeof(verify):bytes-at;
        ok=exact_read(&file,at,verify,take) && !memcmp(verify,(const uint8_t *)data+at,take);at+=take;
    }
    if(f_close(&file)!=FR_OK) ok=false;
    return ok;
}
static bool cache_publish(void *ctx,const char *gdi,size_t bytes,const struct kui_chd_cache_report *report) {
    struct chd_files *f=ctx;uint8_t stamp[STAMP_CAP]={0};
    if(bytes>UINT32_MAX || report->count!=f->plan.count || memcmp(report->source_id,f->identity,32)) return false;
    /* All decoding is complete. A failed source close must happen before the
     * cache is visible, just like a failed output close. */
    if(!source_close(f)) return false;
    memcpy(stamp,stamp_magic,8);memcpy(stamp+8,f->identity,32);put32(stamp+40,report->count);
    put32(stamp+44,(uint32_t)bytes);memcpy(stamp+48,report->descriptor_sha256,32);
    size_t length=STAMP_HEADER+report->count*STAMP_TRACK+4u;
    for(unsigned i=0;i<report->count;i++) {
        uint8_t *entry=stamp+STAMP_HEADER+i*STAMP_TRACK;
        put64(entry,track_bytes(f,i));memcpy(entry+8,report->track_sha256[i],32);
    }
    put32(stamp+length-4,kui_crc32(0,stamp,length-4));
    if(stopped(f) || !write_small(f,"cache.bin",stamp,length,&f->stamp_created) || stopped(f)) return false;
    if(!f->gdrom && (!write_small(f,"disc.cue",f->cue,f->cue_bytes,&f->cue_created) || stopped(f))) return false;
    if(!write_small(f,"disc.gdi",gdi,bytes,&f->gdi_created) || stopped(f)) return false;
    char from[KUI_DEST_PATH_CAP],to[KUI_DEST_PATH_CAP];FILINFO info;
    if(!qualify(from,f->stage) || !qualify(to,f->final)) return false;
    FRESULT exists=f_stat(to,&info);
    if(exists!=FR_NO_FILE && exists!=FR_NO_PATH) return false;
    if(f_rename(from,to)!=FR_OK) return false;
    f->published=true;f->stage_created=false;return true;
}
static void cache_abort(void *ctx) {
    struct chd_files *f=ctx;if(f->published || !f->stage_created) return;
    if(f->output_open) {f_close(&f->output);f->output_open=false;}
    char path[KUI_DEST_PATH_CAP];
    for(unsigned i=0;i<f->plan.count;i++) if(f->created[i]) {
        char name[KUI_CHD_CACHE_NAME_BYTES];track_name(name,i);
        if(child(path,f->stage,name)) f_unlink(path);
    }
    if(f->gdi_created && child(path,f->stage,"disc.gdi")) f_unlink(path);
    if(f->cue_created && child(path,f->stage,"disc.cue")) f_unlink(path);
    if(f->stamp_created && child(path,f->stage,"cache.bin")) f_unlink(path);
    if(qualify(path,f->stage)) f_unlink(path);
    f->stage_created=false;
}
static bool cache_cancel(void *ctx) {return stopped(ctx);}
static void cache_progress(void *ctx,uint64_t done,uint64_t total,bool verify) {
    struct chd_files *f=ctx;if(!f->progress) return;
    struct kui_app_status status={.done=done,.total=total};
    snprintf(status.message,sizeof(status.message),"%s CHD cache; B stops safely",verify?"Verifying":"Preparing");
    f->progress(&status);
}
static bool stage_create(struct chd_files *f,const char **problem) {
    DWORD free_clusters=0;FATFS *fs=NULL;
    if(f_getfree("0:",&free_clusters,&fs)!=FR_OK || !fs || !fs->csize) {
        *problem="Cannot determine free SD space";return false;
    }
    uint64_t cluster=(uint64_t)fs->csize*512u,needed=0;
    for(unsigned i=0;i<f->plan.count;i++) {
        uint64_t bytes=track_bytes(f,i);needed+=((bytes+cluster-1u)/cluster)*cluster;
    }
    /* Descriptor, manifest and conservatively bounded directory growth. */
    needed+=((2u*KUI_CHD_CACHE_GDI_BYTES+STAMP_CAP+cluster-1u)/cluster)*cluster+
        (f->plan.count+8u)*cluster;
    if((uint64_t)free_clusters*cluster<needed) {
        *problem="Not enough SD space for expanded CHD cache";return false;
    }
    if(!kui_destination_mkdirs(CACHE_ROOT,f->log)) {*problem="Cannot create CHD cache root";return false;}
    char path[KUI_DEST_PATH_CAP];FILINFO info;
    if(!qualify(path,f->final)) return false;
    FRESULT exists=f_stat(path,&info);
    if(exists==FR_OK) {*problem="Existing CHD cache is incomplete; move its folder before preparing again";return false;}
    if(exists!=FR_NO_FILE && exists!=FR_NO_PATH) {*problem="Cannot check CHD cache destination";return false;}
    for(unsigned attempt=1;attempt<=999u;attempt++) {
        if(stopped(f)) return false;
        int n=snprintf(f->stage,sizeof(f->stage),"%s.work%03u",f->final,attempt);
        if(n<0 || (size_t)n>=sizeof(f->stage) || !qualify(path,f->stage)) return false;
        FRESULT result=f_mkdir(path);
        if(result==FR_OK) {f->stage_created=true;return true;}
        if(result!=FR_EXIST) {*problem="Cannot create CHD cache staging folder";return false;}
    }
    *problem="Too many unfinished CHD cache folders";return false;
}
bool kui_games_chd_prepare(const char *path,struct kui_games_detail *out,
    kui_log_fn log,kui_cancel_fn cancel,kui_app_progress_fn progress) {
    if(!out) return false;
    memset(out,0,sizeof(*out));out->chd=true;
    if(path && strlen(path)<sizeof(out->path)) strcpy(out->path,path);
    struct chd_files *f=calloc(1,sizeof(*f));void *workspace=NULL;
    const char *problem="Insufficient memory to prepare CHD";bool connected=false,mounted=false,ok=false;
    FATFS fs;
    if(!f) goto done;
    f->log=log;f->cancel=cancel;f->progress=progress;
    if(stopped(f)) goto done;
    if(!kui_sd_connect()) {problem="SD card unavailable";goto done;}
    connected=true;
    if(!kui_mount(&fs,log)) {problem="Cannot mount SD card";goto done;}
    mounted=true;
    if(!source_open(f,path,&problem)) goto done;
    if(ready(f)) {ok=true;goto done;}
    uint64_t bound=0;
    if(kui_chd_cache_bound(&f->plan,&bound)!=KUI_CHD_CACHE_OK) {problem="Unsupported CHD cache geometry";goto done;}
    if(!stage_create(f,&problem)) goto done;
    workspace=malloc(kui_chd_cache_workspace_bytes());
    if(!workspace) goto done;
    if(log) log("Preparing CHD cache: %s -> %s; expanded bytes=%" PRIu64,path,f->final,bound);
    struct kui_chd_cache_io io={f,cache_read_track,cache_create,cache_write,cache_read,
        cache_sync,cache_finish,cache_publish,cache_abort,cache_cancel,cache_progress};
    struct kui_chd_cache_report *report=malloc(sizeof(*report));
    if(!report) goto done;
    enum kui_chd_cache_result result=kui_chd_cache_export(&f->plan,f->identity,&io,workspace,
        kui_chd_cache_workspace_bytes(),report);
    free(report);ok=result==KUI_CHD_CACHE_OK;
    if(!ok) problem=kui_chd_cache_result_name(result);
done:
    if(f) {
        cache_abort(f);
        if(!source_close(f)) {ok=false;problem="Cannot close CHD source";}
    }
    free(workspace);
    if(mounted && f_mount(NULL,"0:",0)!=FR_OK) {ok=false;problem="Cannot release CHD cache filesystem";}
    if(connected) kui_sd_disconnect();
    free(f);
    if(cancel && cancel()) {out->stopped=true;ok=false;problem="CHD preparation stopped; source preserved";}
    if(ok) return kui_games_inspect(path,out,log,cancel);
    snprintf(out->message,sizeof(out->message),"%s",problem);
    if(log) log("CHD preparation: %s",out->message);
    return false;
}
