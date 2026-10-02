/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/storage_test.h"
#include "kui/storage.h"
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* Only the filesystem worker uses this module. Records are explicitly encoded
 * little endian, never C structure dumps. result.bin is the final commit marker;
 * STARTED survives both interruptions and failures to save a completed run. */
#define ROOT "0:/KUI/tests"
#define RECORD_BYTES 1536u
#define MAX_ID 999999u
struct wire { uint8_t *bytes; size_t at; bool ok; };
static void put(struct wire *w, uint64_t value, unsigned size) {
    if(w->at + size > RECORD_BYTES - 4) { w->ok = false; return; }
    for(unsigned i = 0; i < size; ++i) w->bytes[w->at++] = (uint8_t)(value >> (8*i));
}
static uint64_t get(struct wire *w, unsigned size) {
    uint64_t value = 0;
    if(w->at + size > RECORD_BYTES - 4) { w->ok = false; return 0; }
    for(unsigned i = 0; i < size; ++i) value |= (uint64_t)w->bytes[w->at++] << (8*i);
    return value;
}
static void text_put(struct wire *w, const char *s, size_t capacity) {
    size_t n = 0;
    while(n < capacity && s[n]) ++n;
    if(n == capacity || n > 255) { w->ok = false; return; }
    put(w, n, 1);
    for(size_t i = 0; i < n; ++i) put(w, (uint8_t)s[i], 1);
}
static void text_get(struct wire *w, char *s, size_t capacity) {
    size_t n = (size_t)get(w, 1);
    if(n >= capacity) { w->ok = false; return; }
    for(size_t i = 0; i < n; ++i) {
        s[i] = (char)get(w, 1);
        if(!s[i]) w->ok = false;
    }
    s[n] = 0;
}
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static void put32(uint8_t *p, uint32_t n) {
    for(unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(n >> (8*i));
}
static void run_path(char path[KUI_STORAGE_TEST_PATH], uint32_t id) {
    snprintf(path, KUI_STORAGE_TEST_PATH, ROOT "/t%06" PRIu32, id);
}
static bool valid(const struct kui_storage_test_result *r, bool started) {
    if(!r->id || r->id > MAX_ID || !kui_storage_test_request_valid(&r->request) ||
       r->metadata.transport > KUI_STORAGE_IDE || r->sample_count > KUI_STORAGE_TEST_SAMPLES ||
       r->errors.last_transport > KUI_STORAGE_IDE ||
       (unsigned)r->errors.last_operation > KUI_STORAGE_ERROR_INIT ||
       (unsigned)r->errors.last_result > KUI_STORAGE_ERROR_REJECTED || r->fatfs_error > FR_INVALID_PARAMETER ||
       r->metadata.local_seconds < 0 || r->verified_bytes > r->written_bytes ||
       r->written_bytes > UINT64_MAX / UINT64_C(1000000))
        return false;
    if(started) return r->outcome == KUI_STORAGE_TEST_RUNNING && !r->sample_count;
    if(r->outcome < KUI_STORAGE_TEST_PASSED || r->outcome > KUI_STORAGE_TEST_FAILED) return false;
    uint64_t sample_bytes=0;
    for(unsigned i=0;i<r->sample_count;++i) {
        const struct kui_storage_test_sample *s=&r->samples[i];
        if(!s->chunk_bytes || s->chunk_bytes>262144u || !s->repeat || s->repeat>5 ||
           s->bytes>UINT64_MAX/UINT64_C(1000000) || s->bytes>UINT64_MAX-sample_bytes) return false;
        sample_bytes+=s->bytes;
    }
    if(r->outcome == KUI_STORAGE_TEST_PASSED) {
        if(!r->sample_count || !r->written_bytes || r->verified_bytes != r->written_bytes ||
           r->cleanup_failed || sample_bytes!=r->verified_bytes) return false;
        for(unsigned i = 0; i < r->sample_count; ++i)
            if(!r->samples[i].verified || !r->samples[i].bytes) return false;
    }
    return true;
}
static void latency_put(struct wire *w, const struct kui_storage_test_latency *l) {
    put(w,l->calls,8); put(w,l->total_us,8); put(w,l->over_20ms,8); put(w,l->over_100ms,8);
    put(w,l->min_us,4); put(w,l->max_us,4); put(w,l->p95_upper_us,4);
}
static void latency_get(struct wire *w, struct kui_storage_test_latency *l) {
    l->calls=get(w,8); l->total_us=get(w,8); l->over_20ms=get(w,8); l->over_100ms=get(w,8);
    l->min_us=(uint32_t)get(w,4); l->max_us=(uint32_t)get(w,4); l->p95_upper_us=(uint32_t)get(w,4);
    if(l->over_100ms > l->over_20ms || l->over_20ms > l->calls ||
       (l->calls && l->min_us > l->max_us)) w->ok=false;
}
static void errors_put(struct wire *w, const struct kui_storage_errors *e) {
    put(w,e->total,4); put(w,e->read_errors,4); put(w,e->write_errors,4); put(w,e->sync_errors,4);
    put(w,e->init_errors,4); put(w,e->timeout_errors,4); put(w,e->crc_errors,4);
    put(w,e->rejected_errors,4); put(w,e->io_errors,4);
    put(w,e->last_operation,1); put(w,e->last_result,1); put(w,e->last_transport,1);
    put(w,e->last_lba,4); put(w,e->last_count,4); put(w,e->sd_command,1); put(w,e->sd_response,1);
    put(w,e->sd_detail_valid,1);
}
static bool boolean_get(struct wire *w) {
    unsigned value=(unsigned)get(w,1);
    if(value > 1) w->ok=false;
    return value != 0;
}
static void errors_get(struct wire *w, struct kui_storage_errors *e) {
    e->total=(uint32_t)get(w,4); e->read_errors=(uint32_t)get(w,4);
    e->write_errors=(uint32_t)get(w,4); e->sync_errors=(uint32_t)get(w,4);
    e->init_errors=(uint32_t)get(w,4); e->timeout_errors=(uint32_t)get(w,4);
    e->crc_errors=(uint32_t)get(w,4); e->rejected_errors=(uint32_t)get(w,4); e->io_errors=(uint32_t)get(w,4);
    e->last_operation=(enum kui_storage_error_operation)get(w,1);
    e->last_result=(enum kui_storage_error_result)get(w,1); e->last_transport=(unsigned)get(w,1);
    e->last_lba=(uint32_t)get(w,4); e->last_count=(uint32_t)get(w,4);
    e->sd_command=(uint8_t)get(w,1); e->sd_response=(uint8_t)get(w,1); e->sd_detail_valid=boolean_get(w);
}
static bool encode(uint8_t bytes[RECORD_BYTES], const struct kui_storage_test_result *r, bool started) {
    if(!valid(r,started)) return false;
    memset(bytes,0,RECORD_BYTES); memcpy(bytes,"KUITEST1",8);
    struct wire w={bytes,8,true};
    put(&w,1,4); put(&w,RECORD_BYTES,4); put(&w,r->id,4);
    put(&w,r->request.preset,1); put(&w,r->request.repeats,4); put(&w,r->request.soak_minutes,4);
    text_put(&w,r->request.card_label,sizeof(r->request.card_label));
    text_put(&w,r->metadata.build,sizeof(r->metadata.build)); put(&w,r->metadata.transport,1);
    put(&w,r->metadata.music_playing,1); put(&w,r->metadata.ui_hz,4);
    put(&w,(uint64_t)r->metadata.local_seconds,8); put(&w,r->metadata.volume_start,4);
    put(&w,r->metadata.volume_sectors,4); put(&w,r->metadata.cluster_bytes,4); put(&w,r->metadata.free_bytes,8);
    text_put(&w,r->metadata.filesystem,sizeof(r->metadata.filesystem));
    put(&w,r->outcome,1); put(&w,r->sample_count,1); put(&w,r->elapsed_us,8);
    put(&w,r->written_bytes,8); put(&w,r->verified_bytes,8); put(&w,r->cycles,8);
    latency_put(&w,&r->write_latency); latency_put(&w,&r->read_latency);
    for(unsigned i=0;i<r->sample_count;++i) {
        const struct kui_storage_test_sample *s=&r->samples[i];
        put(&w,s->chunk_bytes,4); put(&w,s->repeat,4); put(&w,s->bytes,8);
        put(&w,s->write_us,8); put(&w,s->read_us,8); put(&w,s->sync_us,8); put(&w,s->verified,1);
    }
    errors_put(&w,&r->errors); put(&w,r->fatfs_error,4); put(&w,r->failure_offset,8);
    text_put(&w,r->failure_phase,sizeof(r->failure_phase)); text_put(&w,r->message,sizeof(r->message));
    put(&w,r->cleanup_failed,1);
    put32(bytes+RECORD_BYTES-4,kui_crc32(0,bytes,RECORD_BYTES-4));
    return w.ok;
}
static bool decode(struct kui_storage_test_result *r, uint8_t bytes[RECORD_BYTES], uint32_t id, bool started) {
    if(memcmp(bytes,"KUITEST1",8) || get32(bytes+8)!=1 || get32(bytes+12)!=RECORD_BYTES ||
       get32(bytes+RECORD_BYTES-4)!=kui_crc32(0,bytes,RECORD_BYTES-4)) return false;
    memset(r,0,sizeof(*r)); struct wire w={bytes,16,true};
    r->id=(uint32_t)get(&w,4); r->request.preset=(unsigned)get(&w,1);
    r->request.repeats=(unsigned)get(&w,4); r->request.soak_minutes=(unsigned)get(&w,4);
    text_get(&w,r->request.card_label,sizeof(r->request.card_label));
    text_get(&w,r->metadata.build,sizeof(r->metadata.build)); r->metadata.transport=(unsigned)get(&w,1);
    r->metadata.music_playing=boolean_get(&w); r->metadata.ui_hz=(unsigned)get(&w,4);
    uint64_t seconds=get(&w,8);
    if(seconds > INT64_MAX) w.ok=false;
    r->metadata.local_seconds=(int64_t)(seconds & INT64_MAX);
    r->metadata.volume_start=(uint32_t)get(&w,4); r->metadata.volume_sectors=(uint32_t)get(&w,4);
    r->metadata.cluster_bytes=(uint32_t)get(&w,4); r->metadata.free_bytes=get(&w,8);
    text_get(&w,r->metadata.filesystem,sizeof(r->metadata.filesystem));
    r->outcome=(unsigned)get(&w,1); r->sample_count=(unsigned)get(&w,1); r->elapsed_us=get(&w,8);
    r->written_bytes=get(&w,8); r->verified_bytes=get(&w,8); r->cycles=get(&w,8);
    latency_get(&w,&r->write_latency); latency_get(&w,&r->read_latency);
    if(r->sample_count > KUI_STORAGE_TEST_SAMPLES) return false;
    for(unsigned i=0;i<r->sample_count;++i) {
        struct kui_storage_test_sample *s=&r->samples[i];
        s->chunk_bytes=(uint32_t)get(&w,4); s->repeat=(uint32_t)get(&w,4); s->bytes=get(&w,8);
        s->write_us=get(&w,8); s->read_us=get(&w,8); s->sync_us=get(&w,8); s->verified=boolean_get(&w);
    }
    errors_get(&w,&r->errors); r->fatfs_error=(unsigned)get(&w,4); r->failure_offset=get(&w,8);
    text_get(&w,r->failure_phase,sizeof(r->failure_phase)); text_get(&w,r->message,sizeof(r->message));
    r->cleanup_failed=boolean_get(&w);
    while(w.at < RECORD_BYTES-4) if(bytes[w.at++]) w.ok=false;
    if(!w.ok || r->id != id || !valid(r,started)) return false;
    run_path(r->path,r->id); r->saved=!started;
    return true;
}
/* Missing/corrupt records are recoverable; media I/O errors must stop writes. */
static bool read_bytes(const char *path, uint8_t *bytes, UINT size, bool *present) {
    *present=false; FIL file; FRESULT fr=f_open(&file,path,FA_READ);
    if(fr==FR_NO_FILE || fr==FR_NO_PATH) return true;
    if(fr!=FR_OK) return false;
    UINT got=0; bool sized=f_size(&file)==size;
    if(sized) fr=f_read(&file,bytes,size,&got);
    FRESULT closed=f_close(&file);
    if(fr!=FR_OK || closed!=FR_OK) return false;
    *present=sized && got==size;
    return true;
}
static bool read_result(uint32_t id, bool started, struct kui_storage_test_result *r, bool *present) {
    uint8_t bytes[RECORD_BYTES]; char dir[KUI_STORAGE_TEST_PATH],path[112]; run_path(dir,id);
    snprintf(path,sizeof(path),"%s/%s",dir,started?"STARTED":"result.bin");
    if(!read_bytes(path,bytes,sizeof(bytes),present)) return false;
    if(*present) *present=decode(r,bytes,id,started);
    return true;
}
static FRESULT write_bytes(const char *path, const void *data, UINT size, bool replace) {
    FIL file; FRESULT fr=f_open(&file,path,FA_WRITE|(replace?FA_CREATE_ALWAYS:FA_CREATE_NEW));
    if(fr!=FR_OK) return fr;
    UINT done=0; fr=f_write(&file,data,size,&done);
    if(fr==FR_OK && done!=size) fr=FR_DENIED;
    if(fr==FR_OK) fr=f_sync(&file);
    FRESULT closed=f_close(&file);
    return fr==FR_OK?closed:fr;
}
static FRESULT mount_store(FATFS *fs) {
    FRESULT fr=f_mount(fs,"0:",1);
    if(fr==FR_OK && fs->fs_type!=FS_FAT32 && fs->fs_type!=FS_EXFAT) fr=FR_NO_FILESYSTEM;
    return fr;
}
static uint32_t directory_id(const char *s) {
    if((s[0]!='t' && s[0]!='T') || strlen(s)!=7) return 0;
    uint32_t id=0;
    for(unsigned i=1;i<7;++i) { if(s[i]<'0' || s[i]>'9') return 0; id=id*10+(unsigned)(s[i]-'0'); }
    return id;
}
static FRESULT scan_ids(uint32_t ids[KUI_STORAGE_TEST_HISTORY], unsigned *count, uint32_t *maximum) {
    *count=0; *maximum=0; DIR dir; FRESULT fr=f_opendir(&dir,ROOT);
    if(fr==FR_NO_PATH) return FR_OK;
    if(fr!=FR_OK) return fr;
    FILINFO info;
    while((fr=f_readdir(&dir,&info))==FR_OK && info.fname[0]) {
        uint32_t id=directory_id(info.fname);
        if(id>*maximum) *maximum=id; /* Even conflicting files reserve their IDs. */
        if(!id || !(info.fattrib&AM_DIR)) continue;
        unsigned at=0;
        while(at<*count && ids[at]>id) ++at;
        if(at==KUI_STORAGE_TEST_HISTORY) continue;
        if(*count<KUI_STORAGE_TEST_HISTORY) ++*count;
        for(unsigned i=*count-1;i>at;--i) ids[i]=ids[i-1];
        ids[at]=id;
    }
    FRESULT closed=f_closedir(&dir);
    return fr==FR_OK?closed:fr;
}
bool kui_storage_test_begin(struct kui_storage_test_result *r, kui_log_fn log) {
    if(!r) return false;
    r->id=0; r->path[0]=0; r->saved=false; r->outcome=KUI_STORAGE_TEST_RUNNING;
    FATFS fs; FRESULT fr=FR_INVALID_PARAMETER;
    if(!kui_storage_test_request_valid(&r->request)) goto out;
    fr=mount_store(&fs);
    if(fr!=FR_OK) goto out;
    const char *parents[]={"0:/KUI",ROOT};
    for(unsigned i=0;i<2;++i) {
        fr=f_mkdir(parents[i]);
        if(fr!=FR_OK && fr!=FR_EXIST) goto out;
    }
    uint32_t ids[KUI_STORAGE_TEST_HISTORY],maximum; unsigned count;
    fr=scan_ids(ids,&count,&maximum); if(fr!=FR_OK) goto out;
    if(maximum==MAX_ID) { fr=FR_DENIED; goto out; }
    uint32_t next_id=maximum+1; char next_path[KUI_STORAGE_TEST_PATH]; run_path(next_path,next_id);
    fr=f_mkdir(next_path); if(fr!=FR_OK) goto out;
    /* Only publish a savable ID after this call owns a newly created folder.
     * A failed mkdir, including a case-insensitive collision, grants no right
     * to append a failed result to any pre-existing test. */
    r->id=next_id; memcpy(r->path,next_path,strlen(next_path)+1);
    uint8_t bytes[RECORD_BYTES]; char path[112];
    if(!encode(bytes,r,true)) { fr=FR_INVALID_PARAMETER; goto out; }
    snprintf(path,sizeof(path),"%s/STARTED",r->path);
    fr=write_bytes(path,bytes,sizeof(bytes),false);
    if(fr==FR_OK) {
        struct kui_storage_test_result check; bool present;
        if(!read_result(r->id,true,&check,&present) || !present) fr=FR_DISK_ERR;
    }
out:
    f_mount(NULL,"0:",0);
    if(fr!=FR_OK) {
        r->outcome=KUI_STORAGE_TEST_FAILED; r->fatfs_error=fr;
        snprintf(r->failure_phase,sizeof(r->failure_phase),"save-start");
        snprintf(r->message,sizeof(r->message),"Cannot create test record (FatFs %u).",(unsigned)fr);
        if(log) log("%s",r->message);
    }
    return fr==FR_OK;
}

struct output { FIL file; FRESULT error; };
static void emit(struct output *o, const char *text) {
    if(o->error!=FR_OK) return;
    UINT n=(UINT)strlen(text),done=0; o->error=f_write(&o->file,text,n,&done);
    if(o->error==FR_OK && done!=n) o->error=FR_DENIED;
}
static void format(struct output *o, const char *fmt, ...) {
    char buf[384]; va_list args; va_start(args,fmt); int n=vsnprintf(buf,sizeof(buf),fmt,args); va_end(args);
    if(n<0 || (unsigned)n>=sizeof(buf)) { o->error=FR_INVALID_PARAMETER; return; }
    emit(o,buf);
}
static void quote(struct output *o, const char *s, bool csv) {
    emit(o,"\"");
    for(;*s;++s) {
        unsigned char c=(unsigned char)*s;
        if(csv && c=='"') emit(o,"\"\"");
        else if(!csv && (c=='"' || c=='\\')) { char escaped[3]={'\\',(char)c,0}; emit(o,escaped); }
        else if(!csv && c<32) format(o,"\\u%04x",(unsigned)c);
        else { char character[2]={(char)c,0}; emit(o,character); }
    }
    emit(o,"\"");
}
static void json_latency(struct output *o,const char *key,const struct kui_storage_test_latency *l) {
    format(o,"  \"%s\": {\"calls\":%" PRIu64 ",\"total_us\":%" PRIu64
        ",\"over_20ms\":%" PRIu64 ",\"over_100ms\":%" PRIu64
        ",\"min_us\":%" PRIu32 ",\"max_us\":%" PRIu32 ",\"p95_upper_us\":%" PRIu32 "},\n",
        key,l->calls,l->total_us,l->over_20ms,l->over_100ms,l->min_us,l->max_us,l->p95_upper_us);
}
static void json_sci_profile(struct output *o,const struct kui_storage_test_sci_profile *p) {
    if(!p->present) return;
    format(o,"  \"sci_profile\":{\"timing_scope\":\"successful_dma_payloads\","
        "\"rx_dma_blocks\":%" PRIu32 ",\"tx_dma_blocks\":%" PRIu32
        ",\"polled_blocks\":%" PRIu32 ",\"dma_failures\":%" PRIu32 ",\n",
        p->rx_dma_blocks,p->tx_dma_blocks,p->polled_blocks,p->dma_failures);
    format(o,"    \"profiled_rx_blocks\":%" PRIu32 ",\"profiled_tx_blocks\":%" PRIu32
        ",\"rx_setup_us\":%" PRIu64 ",\"rx_transfer_us\":%" PRIu64
        ",\"rx_check_us\":%" PRIu64 ",\"tx_setup_us\":%" PRIu64
        ",\"tx_transfer_us\":%" PRIu64 "},\n",
        p->profiled_rx_blocks,p->profiled_tx_blocks,p->rx_setup_us,p->rx_transfer_us,
        p->rx_check_us,p->tx_setup_us,p->tx_transfer_us);
}
static FRESULT reports(const struct kui_storage_test_result *r, bool csv) {
    char path[112]; snprintf(path,sizeof(path),"%s/result.%s",r->path,csv?"csv":"json");
    struct output o; o.error=f_open(&o.file,path,FA_WRITE|FA_CREATE_NEW); if(o.error!=FR_OK) return o.error;
    if(csv) {
        emit(&o,"id,build,card_label,transport,filesystem,cluster_bytes,music_playing,ui_hz,preset,outcome,repeat,chunk_bytes,bytes,write_us,read_us,sync_us,verified\r\n");
        for(unsigned i=0;i<r->sample_count;++i) {
            const struct kui_storage_test_sample *s=&r->samples[i];
            format(&o,"%" PRIu32 ",",r->id); quote(&o,r->metadata.build,true); emit(&o,","); quote(&o,r->request.card_label,true);
            format(&o,",%u,",r->metadata.transport); quote(&o,r->metadata.filesystem,true);
            format(&o,",%" PRIu32 ",%u,%u,%u,%u,%" PRIu32 ",%" PRIu32 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%u\r\n",
                r->metadata.cluster_bytes,r->metadata.music_playing?1u:0u,r->metadata.ui_hz,r->request.preset,r->outcome,
                s->repeat,s->chunk_bytes,s->bytes,s->write_us,s->read_us,s->sync_us,s->verified?1u:0u);
        }
    } else {
        format(&o,"{\n  \"schema\":1,\"id\":%" PRIu32 ",\"build\":",r->id); quote(&o,r->metadata.build,false);
        emit(&o,",\"card_label\":"); quote(&o,r->request.card_label,false);
        format(&o,",\n  \"preset\":%u,\"repeats\":%u,\"soak_minutes\":%u,\"outcome\":%u,\n  \"transport\":%u,\"filesystem\":",
            r->request.preset,r->request.repeats,r->request.soak_minutes,r->outcome,r->metadata.transport);
        quote(&o,r->metadata.filesystem,false);
        format(&o,",\"cluster_bytes\":%" PRIu32 ",\"volume_start\":%" PRIu32 ",\"volume_sectors\":%" PRIu32
            ",\"free_bytes\":%" PRIu64 ",\n  \"music_playing\":%s,\"ui_hz\":%u,\"local_seconds\":%" PRId64 ",\n",
            r->metadata.cluster_bytes,r->metadata.volume_start,r->metadata.volume_sectors,r->metadata.free_bytes,
            r->metadata.music_playing?"true":"false",r->metadata.ui_hz,r->metadata.local_seconds);
        format(&o,"  \"elapsed_us\":%" PRIu64 ",\"written_bytes\":%" PRIu64 ",\"verified_bytes\":%" PRIu64 ",\"cycles\":%" PRIu64 ",\n",
            r->elapsed_us,r->written_bytes,r->verified_bytes,r->cycles);
        json_latency(&o,"write_latency",&r->write_latency); json_latency(&o,"read_latency",&r->read_latency);
        json_sci_profile(&o,&r->sci_profile);
        const struct kui_storage_errors *e=&r->errors;
        format(&o,"  \"errors\":{\"total\":%" PRIu32 ",\"read\":%" PRIu32 ",\"write\":%" PRIu32 ",\"sync\":%" PRIu32 ",\"init\":%" PRIu32
            ",\"timeout\":%" PRIu32 ",\"crc\":%" PRIu32 ",\"rejected\":%" PRIu32 ",\"io\":%" PRIu32 ",\n",
            e->total,e->read_errors,e->write_errors,e->sync_errors,e->init_errors,e->timeout_errors,e->crc_errors,e->rejected_errors,e->io_errors);
        format(&o,"    \"operation\":%u,\"result\":%u,\"transport\":%u,\"lba\":%" PRIu32 ",\"count\":%" PRIu32
            ",\"sd_command\":%u,\"sd_response\":%u,\"sd_detail_valid\":%s},\n",
            (unsigned)e->last_operation,(unsigned)e->last_result,e->last_transport,e->last_lba,e->last_count,
            (unsigned)e->sd_command,(unsigned)e->sd_response,e->sd_detail_valid?"true":"false");
        format(&o,"  \"fatfs_error\":%u,\"failure_offset\":%" PRIu64 ",\"cleanup_failed\":%s,\"failure_phase\":",
            r->fatfs_error,r->failure_offset,r->cleanup_failed?"true":"false");
        quote(&o,r->failure_phase,false); emit(&o,",\"message\":"); quote(&o,r->message,false); emit(&o,",\n  \"samples\":[");
        for(unsigned i=0;i<r->sample_count;++i) {
            const struct kui_storage_test_sample *s=&r->samples[i];
            format(&o,"%s\n    {\"chunk_bytes\":%" PRIu32 ",\"repeat\":%" PRIu32 ",\"bytes\":%" PRIu64
                ",\"write_us\":%" PRIu64 ",\"read_us\":%" PRIu64 ",\"sync_us\":%" PRIu64 ",\"verified\":%s}",
                i?",":"",s->chunk_bytes,s->repeat,s->bytes,s->write_us,s->read_us,s->sync_us,s->verified?"true":"false");
        }
        emit(&o,"\n  ]\n}\n");
    }
    if(o.error==FR_OK) o.error=f_sync(&o.file);
    FRESULT closed=f_close(&o.file); return o.error==FR_OK?closed:o.error;
}
bool kui_storage_test_save(struct kui_storage_test_result *r, kui_log_fn log) {
    if(!r) return false;
    r->saved=false; char path[KUI_STORAGE_TEST_PATH]; run_path(path,r->id);
    uint8_t bytes[RECORD_BYTES];
    if(!memchr(r->path,0,sizeof(r->path)) || strcmp(path,r->path) || !encode(bytes,r,false)) return false;
    FATFS fs; FRESULT fr=mount_store(&fs); if(fr!=FR_OK) goto out;
    struct kui_storage_test_result started; bool present=false;
    if(!read_result(r->id,true,&started,&present) || !present ||
       started.request.preset!=r->request.preset || started.request.repeats!=r->request.repeats ||
       started.request.soak_minutes!=r->request.soak_minutes || strcmp(started.request.card_label,r->request.card_label)) {
        fr=FR_INVALID_OBJECT; goto out;
    }
    fr=reports(r,true); if(fr!=FR_OK) goto out;
    fr=reports(r,false); if(fr!=FR_OK) goto out;
    char record_path[112]; snprintf(record_path,sizeof(record_path),"%s/result.bin",r->path);
    fr=write_bytes(record_path,bytes,sizeof(bytes),false); if(fr!=FR_OK) goto out;
    /* Drop the volume cache before validating the committed bytes. */
    f_mount(NULL,"0:",0); fr=mount_store(&fs); if(fr!=FR_OK) goto out;
    uint8_t check[RECORD_BYTES];
    if(!read_bytes(record_path,check,sizeof(check),&present) || !present || memcmp(check,bytes,sizeof(check))) fr=FR_DISK_ERR;
out:
    f_mount(NULL,"0:",0); r->saved=fr==FR_OK;
    if(log) log(r->saved?"Saved test %06" PRIu32 " in %s":"Test %06" PRIu32 " save failed in %s",r->id,r->path+2);
    return r->saved;
}

struct baseline { uint32_t id; uint64_t generation; bool valid; };
static const char *const baselines[]={ROOT "/base-a.bin",ROOT "/base-b.bin"};
static bool baseline_read(unsigned slot, struct baseline *b) {
    uint8_t bytes[28]; bool present; *b=(struct baseline){0};
    if(!read_bytes(baselines[slot],bytes,sizeof(bytes),&present)) return false;
    if(!present || memcmp(bytes,"KUIBASE1",8) || get32(bytes+8)!=1 ||
       get32(bytes+24)!=kui_crc32(0,bytes,24)) return true;
    b->id=get32(bytes+12); b->generation=(uint64_t)get32(bytes+16)|(uint64_t)get32(bytes+20)<<32;
    b->valid=b->id>0 && b->id<=MAX_ID && b->generation>0;
    return true;
}
static int latest_baseline(const struct baseline b[2]) {
    if(!b[0].valid) return b[1].valid?1:-1;
    return b[1].valid && b[1].generation>b[0].generation?1:0;
}
bool kui_storage_test_set_baseline(uint32_t id, kui_log_fn log) {
    if(!id || id>MAX_ID) return false;
    bool ok=false,present=false; FATFS fs;
    if(mount_store(&fs)!=FR_OK) goto out;
    struct kui_storage_test_result result;
    if(!read_result(id,false,&result,&present) || !present || result.outcome!=KUI_STORAGE_TEST_PASSED) goto out;
    struct baseline b[2]; if(!baseline_read(0,&b[0]) || !baseline_read(1,&b[1])) goto out;
    int latest=latest_baseline(b);
    uint64_t generation=latest<0?1:b[latest].generation+1;
    if(!generation) goto out;
    /* A slot with a broken/deleted target is not the usable prior baseline.
     * Retain the newest reference whose target can actually be validated. */
    struct baseline usable[2]={b[0],b[1]};
    for(unsigned i=0;i<2;++i) if(usable[i].valid) {
        if(!read_result(usable[i].id,false,&result,&present)) goto out;
        if(!present || result.outcome!=KUI_STORAGE_TEST_PASSED) usable[i].valid=false;
    }
    int current=latest_baseline(usable);
    unsigned target=current<0?0u:(unsigned)(1-current);
    uint8_t bytes[28]={0}; memcpy(bytes,"KUIBASE1",8); put32(bytes+8,1); put32(bytes+12,id);
    put32(bytes+16,(uint32_t)generation); put32(bytes+20,(uint32_t)(generation>>32));
    put32(bytes+24,kui_crc32(0,bytes,24));
    if(write_bytes(baselines[target],bytes,sizeof(bytes),true)!=FR_OK) goto out;
    f_mount(NULL,"0:",0); if(mount_store(&fs)!=FR_OK) goto out;
    struct baseline check;
    ok=baseline_read(target,&check) && check.valid && check.id==id && check.generation==generation;
out:
    f_mount(NULL,"0:",0);
    if(log) log(ok?"Baseline saved: test %06" PRIu32:"Baseline save not confirmed: test %06" PRIu32,id);
    return ok;
}
bool kui_storage_test_load_history(struct kui_storage_test_history *h, kui_log_fn log) {
    if(!h) return false;
    memset(h,0,sizeof(*h)); bool ok=false; FATFS fs;
    if(mount_store(&fs)!=FR_OK) goto out;
    uint32_t ids[KUI_STORAGE_TEST_HISTORY],maximum; unsigned count;
    if(scan_ids(ids,&count,&maximum)!=FR_OK) goto out;
    for(unsigned i=0;i<count;++i) {
        struct kui_storage_test_result *r=&h->rows[h->count]; bool present;
        if(!read_result(ids[i],false,r,&present)) goto out;
        if(!present) {
            if(!read_result(ids[i],true,r,&present)) goto out;
            if(!present) { memset(r,0,sizeof(*r)); r->id=ids[i]; run_path(r->path,r->id); }
            r->outcome=KUI_STORAGE_TEST_FAILED; r->saved=false;
            snprintf(r->failure_phase,sizeof(r->failure_phase),"interrupted");
            snprintf(r->message,sizeof(r->message),"Interrupted or result unavailable; no completed result was validated.");
        }
        ++h->count;
    }
    struct baseline b[2]; if(!baseline_read(0,&b[0]) || !baseline_read(1,&b[1])) goto out;
    for(unsigned attempt=0;attempt<2;++attempt) {
        int chosen=latest_baseline(b); if(chosen<0) break;
        bool present;
        if(!read_result(b[chosen].id,false,&h->baseline,&present)) goto out;
        if(present && h->baseline.outcome==KUI_STORAGE_TEST_PASSED) { h->baseline_valid=true; break; }
        b[chosen].valid=false;
    }
    ok=true;
    snprintf(h->message,sizeof(h->message),h->count?"Showing latest %u tests; all results remain in /KUI/tests.":"No saved tests yet.",h->count);
out:
    f_mount(NULL,"0:",0);
    if(!ok) {
        snprintf(h->message,sizeof(h->message),"Test history could not be read from this device.");
        if(log) log("%s",h->message);
    }
    return ok;
}
