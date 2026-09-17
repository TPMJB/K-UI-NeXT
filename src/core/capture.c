/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/capture.h"
#include "kui/timing.h"
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* Bounded memory; this module is called only by the single I/O worker. */
static struct {
    const struct kui_capture_plan *plan;
    const struct kui_capture_ops *ops;
    struct kui_checkpoint state;
    struct kui_sha256 hashes[99];
    uint8_t data[KUI_CAPTURE_CHUNK*KUI_RAW_BYTES], record[KUI_CHECKPOINT_BYTES];
    char dir[80], path[112], title[129], identity[65], text[32768];
    uint64_t started, committed;
    struct kui_capture_progress progress;
    struct kui_timing timing;
    struct {
        uint64_t wall_us,bytes,us[KUI_TIME_BUCKETS];
        uint32_t first_fad,next_fad;
        bool attempted;
    } track_timing[99];
    uint64_t track_started,track_before[KUI_TIME_BUCKETS];
} job;

static void track_timing_start(unsigned i,uint32_t position) {
    if(!job.ops->now_us) return;
    job.track_started=job.ops->now_us(job.ops->ctx);
    job.track_timing[i].attempted=true;
    job.track_timing[i].first_fad=job.plan->tracks[i].start+position;
    for(unsigned b=0;b<KUI_TIME_BUCKETS;b++)
        job.track_before[b]=job.timing.samples[KUI_TIME_CAPTURE][b].us;
}
static void track_timing_finish(unsigned i) {
    if(!job.ops->now_us) return;
    job.track_timing[i].wall_us=job.ops->now_us(job.ops->ctx)-job.track_started;
    job.track_timing[i].next_fad=job.plan->tracks[i].start+job.state.track[i].sectors;
    job.track_timing[i].bytes=(uint64_t)(job.track_timing[i].next_fad-job.track_timing[i].first_fad)*KUI_RAW_BYTES;
    for(unsigned b=0;b<KUI_TIME_BUCKETS;b++)
        job.track_timing[i].us[b]=job.timing.samples[KUI_TIME_CAPTURE][b].us-job.track_before[b];
}
static void report_timing(void) {
    static const char *phases[]={"setup","resume","capture","verify","finish"};
    static const char *buckets[]={"disc","edc","write","read","sha256","crc32","checkpoint"};
    kui_timing_finish(&job.timing);
    if(!job.ops->now_us) return;
    job.ops->log("TIMING: wall time in microseconds; categories do not overlap");
    for(unsigned p=0;p<KUI_TIME_PHASES;p++) {
        uint64_t elapsed=job.timing.elapsed[p],accounted=0;
        if(!elapsed) continue;
        job.ops->log("TIMING %s wall_us=%" PRIu64,phases[p],elapsed);
        for(unsigned b=0;b<KUI_TIME_BUCKETS;b++) {
            const struct kui_timing_sample *s=&job.timing.samples[p][b];
            if(!s->calls) continue;
            uint64_t share=s->us*1000/elapsed;
            job.ops->log("%s us=%" PRIu64 " pct=%" PRIu64 ".%" PRIu64 " bytes=%" PRIu64 " calls=%" PRIu64,
                buckets[b],s->us,share/10,share%10,s->bytes,s->calls);
            accounted+=s->us;
        }
        job.ops->log("other us=%" PRIu64,elapsed>accounted?elapsed-accounted:0);
    }
    job.ops->log("TRACK TIMING: capture intervals, including checkpoints and close");
    for(unsigned i=0;i<job.plan->count;i++) {
        if(!job.track_timing[i].attempted) continue;
        const uint64_t *us=job.track_timing[i].us;uint64_t accounted=0;
        for(unsigned b=0;b<KUI_TIME_BUCKETS;b++) accounted+=us[b];
        job.ops->log("TRACK T%02u %s FAD=[%" PRIu32 ",%" PRIu32 ")",i+1,
            job.plan->tracks[i].control==4?"data":"audio",
            job.track_timing[i].first_fad,job.track_timing[i].next_fad);
        job.ops->log("track wall_us=%" PRIu64 " bytes=%" PRIu64,
            job.track_timing[i].wall_us,job.track_timing[i].bytes);
        job.ops->log("track disc_us=%" PRIu64 " write_us=%" PRIu64,us[KUI_TIME_DISC],us[KUI_TIME_WRITE]);
        job.ops->log("track sha256_us=%" PRIu64 " crc32_us=%" PRIu64,us[KUI_TIME_SHA256],us[KUI_TIME_CRC32]);
        job.ops->log("track edc_us=%" PRIu64 " checkpoint_us=%" PRIu64,us[KUI_TIME_EDC],us[KUI_TIME_CHECKPOINT]);
        job.ops->log("track other_us=%" PRIu64,job.track_timing[i].wall_us>=accounted?job.track_timing[i].wall_us-accounted:0);
    }
}
static void hash_track(struct kui_sha256 *hash,const void *data,size_t bytes) {
    uint64_t start=kui_timing_begin(&job.timing);
    kui_sha256_update(hash,data,bytes);
    kui_timing_end(&job.timing,KUI_TIME_SHA256,start,bytes);
}
static uint32_t crc_track(uint32_t crc,const void *data,size_t bytes) {
    uint64_t start=kui_timing_begin(&job.timing);
    crc=kui_crc32(crc,data,bytes);
    kui_timing_end(&job.timing,KUI_TIME_CRC32,start,bytes);
    return crc;
}
static bool cancelled(void) { return job.ops->cancelled(job.ops->ctx); }
static uint64_t saved_bytes(void) {
    uint64_t n=0;
    for(unsigned i=0;i<job.plan->count;i++) n+=(uint64_t)job.state.track[i].sectors*KUI_RAW_BYTES;
    return n;
}
static bool all_captured(void) { return saved_bytes()==job.plan->bytes; }
static void progress(enum kui_capture_phase phase,unsigned i,uint64_t done,uint64_t total) {
    job.progress.phase=phase;job.progress.track=i+1;job.progress.tracks=job.plan->count;
    job.progress.fad=job.plan->tracks[i].start+job.state.track[i].sectors;
    job.progress.done=done;job.progress.total=total;job.progress.committed=job.committed;
    job.progress.retries=job.state.retries;
    job.progress.elapsed_ms=job.ops->now_ms(job.ops->ctx)-job.started;
    if(job.ops->progress) job.ops->progress(job.ops->ctx,&job.progress);
}
static void track_path(unsigned i) {
    snprintf(job.path,sizeof(job.path),"%s/track%02" PRIu32 ".%s",job.dir,
        job.plan->tracks[i].number,job.plan->tracks[i].control==4?"bin":"raw");
}
static void path_for(const char *name) { snprintf(job.path,sizeof(job.path),"%s/%s",job.dir,name); }
static bool close_file(FIL *file) {
    FRESULT r=f_close(file);
    if(r!=FR_OK) job.ops->log("File close failed: FatFs=%u",(unsigned)r);
    return r==FR_OK;
}
static bool exact_read(FIL *file,void *data,UINT bytes) {
    UINT done=0;FRESULT r=f_read(file,data,bytes,&done);
    if(r!=FR_OK || done!=bytes) {
        job.ops->log("SD read failed: FatFs=%u bytes=%u/%u",(unsigned)r,done,bytes);return false;
    }
    return true;
}
static bool exact_write(FIL *file,const void *data,UINT bytes) {
    UINT done=0;FRESULT r=f_write(file,data,bytes,&done);
    if(r!=FR_OK || done!=bytes) {
        job.ops->log("SD write failed/full: FatFs=%u bytes=%u/%u",(unsigned)r,done,bytes);return false;
    }
    return true;
}
static bool sync_file(FIL *file) {
    FRESULT r=f_sync(file);
    if(r!=FR_OK) job.ops->log("SD sync failed: FatFs=%u",(unsigned)r);
    return r==FR_OK;
}

/* An unsuccessful batch is retried as a single sector at the same FAD.
 * The ten additional attempts are not reset by reducing the request size. */
static bool read_raw(unsigned i,uint32_t fad,unsigned *count) {
    for(unsigned attempt=0;attempt<=KUI_CAPTURE_RETRIES;attempt++) {
        if(cancelled()) return false;
        uint64_t start=kui_timing_begin(&job.timing);
        enum kui_read_result r=job.ops->read(job.ops->ctx,fad,*count,job.data);
        kui_timing_end(&job.timing,KUI_TIME_DISC,start,r==KUI_READ_OK?*count*KUI_RAW_BYTES:0);
        if(r==KUI_READ_FATAL) return false;
        if(r==KUI_READ_OK && job.plan->tracks[i].control==4) {
            unsigned checked=0;start=kui_timing_begin(&job.timing);
            for(unsigned s=0;s<*count;s++) {
                ++checked;
                if(!kui_sector_edc_valid(job.data+s*KUI_RAW_BYTES)) {
                    job.ops->log("Invalid data-sector layout/EDC at FAD=%" PRIu32,fad+s);
                    r=KUI_READ_RETRY;break;
                }
            }
            kui_timing_end(&job.timing,KUI_TIME_EDC,start,checked*KUI_RAW_BYTES);
        }
        if(r==KUI_READ_OK) return true;
        if(attempt==KUI_CAPTURE_RETRIES) break;
        if(job.state.retries!=UINT32_MAX) ++job.state.retries;
        *count=1;
        job.ops->log("Read retry %u/%u: track %02u FAD=%" PRIu32,
            attempt+1,KUI_CAPTURE_RETRIES,i+1,fad);
    }
    job.ops->log("Read retries exhausted: track %02u FAD=%" PRIu32,i+1,fad);
    return false;
}
static void hash_u32(struct kui_sha256 *hash,uint32_t n) {
    uint8_t b[4];for(unsigned i=0;i<4;i++) b[i]=(uint8_t)(n>>(8*i));
    kui_sha256_update(hash,b,4);
}
static bool identify(void) {
    struct kui_sha256 hash;kui_sha256_init(&hash);
    kui_sha256_update(&hash,KUI_CAPTURE_PROFILE,strlen(KUI_CAPTURE_PROFILE));
    hash_u32(&hash,job.plan->count);
    for(unsigned i=0;i<job.plan->count;i++) {
        const struct kui_capture_track *t=&job.plan->tracks[i];
        hash_u32(&hash,t->number);hash_u32(&hash,t->control);hash_u32(&hash,t->session);
        hash_u32(&hash,t->start);hash_u32(&hash,t->end);hash_u32(&hash,t->toc_end);
        /* TOCs alone cannot identify a disc. Hash three independently read
         * raw content samples per track, including the high-density IP.BIN. */
        uint32_t points[3]={t->start,t->start+(t->end-t->start-1)/2,t->end-1};
        for(unsigned k=0;k<3;k++) {
            unsigned one=1;
            if(!read_raw(i,points[k],&one)) return false;
            hash_u32(&hash,points[k]);kui_sha256_update(&hash,job.data,KUI_RAW_BYTES);
            if(t->start==45150 && k==0) {
                int offset=kui_data_offset(job.data);
                if(offset<0 || memcmp(job.data+offset,"SEGA SEGAKATANA",14)) {
                    job.ops->log("Missing retail Dreamcast IP.BIN signature; capture refused");return false;
                }
                for(unsigned c=0;c<128;c++) {
                    unsigned ch=job.data[offset+128+c];
                    job.title[c]=(ch>=32 && ch<=126 && ch!='"' && ch!='\\')?(char)ch:'_';
                }
                for(unsigned c=128;c && job.title[c-1]==' ';c--) job.title[c-1]=0;
            }
        }
        progress(KUI_IDENTIFY,i,i+1,job.plan->count);
    }
    kui_sha256_digest(&hash,job.state.identity);kui_hex(job.state.identity,32,job.identity);
    job.ops->log("Disc: %s",job.title);
    job.ops->log("Identity: %s",job.identity);
    job.ops->log("Profile: %s",KUI_CAPTURE_PROFILE);
    for(unsigned i=0;i<job.plan->count;i++) {
        const struct kui_capture_track *t=&job.plan->tracks[i];
        job.ops->log("T%02u FAD [%" PRIu32 ",%" PRIu32 ") %s; gap excluded=%" PRIu32,
            i+1,t->start,t->end,t->control==4?"data":"audio",t->toc_end-t->end);
    }
    return !cancelled();
}
static bool choose_dir(enum kui_capture_mode mode) {
    DIR dir;FILINFO info;unsigned latest=0;
    char prefix[20];snprintf(prefix,sizeof(prefix),"d%.16s-",job.identity);
    FRESULT r=f_opendir(&dir,"0:/KUI/dumps");
    if(r==FR_OK) {
        for(;;) {
            r=f_readdir(&dir,&info);
            if(r!=FR_OK || !info.fname[0] || cancelled()) break;
            if(!(info.fattrib&AM_DIR) || strlen(info.fname)!=22 || strncmp(info.fname,prefix,18)) continue;
            unsigned n=0;bool valid=true;
            for(unsigned i=18;i<22;i++) {
                if(info.fname[i]<'0'||info.fname[i]>'9') {valid=false;break;}
                n=n*10+(unsigned)(info.fname[i]-'0');
            }
            if(valid && n>latest) latest=n;
        }
        FRESULT c=f_closedir(&dir);
        if(r!=FR_OK || c!=FR_OK || cancelled()) return false;
    } else if(r!=FR_NO_PATH && r!=FR_NO_FILE) {job.ops->log("Dump directory scan failed: %u",(unsigned)r);return false;}
    if(mode!=KUI_CAPTURE_NEW && !latest) {job.ops->log("No saved job for this disc identity");return false;}
    if(mode==KUI_CAPTURE_NEW) {
        if(latest==9999) {job.ops->log("All job numbers for this disc are used");return false;}
        const char *parents[]={"0:/KUI","0:/KUI/dumps"};
        for(unsigned i=0;i<2;i++) {
            r=f_mkdir(parents[i]);if(r!=FR_OK && r!=FR_EXIST) return false;
        }
        ++latest;
    }
    snprintf(job.dir,sizeof(job.dir),"0:/KUI/dumps/%s%04u",prefix,latest);
    if(mode==KUI_CAPTURE_NEW && f_mkdir(job.dir)!=FR_OK) {
        job.ops->log("Cannot create a new job; existing files preserved");return false;
    }
    job.ops->log("Job: %s",job.dir+2);return true;
}
static bool save_checkpoint_inner(FIL *track) {
    if(track && !sync_file(track)) return false;
    if(job.state.sequence>=UINT64_MAX-1) return false;
    ++job.state.sequence;
    for(unsigned i=0;i<job.plan->count;i++) if(job.state.track[i].sectors)
        kui_sha256_digest(&job.hashes[i],job.state.track[i].sha256);
    kui_checkpoint_encode(&job.state,job.record);
    path_for(job.state.sequence&1?"checkpoint-a.bin":"checkpoint-b.bin");
    FIL file;
    FRESULT r=f_open(&file,job.path,FA_WRITE|FA_CREATE_ALWAYS);
    bool ok=false;
    if(r==FR_OK) {
        ok=exact_write(&file,job.record,sizeof(job.record)) && sync_file(&file);
        if(!close_file(&file)) ok=false;
    } else job.ops->log("Checkpoint open failed: FatFs=%u",(unsigned)r);
    if(!ok) {
        --job.state.sequence;
        job.ops->log("Checkpoint not committed; resume uses the last valid record");return false;
    }
    job.committed=saved_bytes();return true;
}
static bool save_checkpoint(FIL *track) {
    uint64_t start=kui_timing_begin(&job.timing);
    bool ok=save_checkpoint_inner(track);
    kui_timing_end(&job.timing,KUI_TIME_CHECKPOINT,start,ok?sizeof(job.record):0);
    return ok;
}
static bool load_checkpoint(void) {
    struct kui_checkpoint best={0},candidate;
    for(unsigned i=0;i<2;i++) {
        path_for(i?"checkpoint-b.bin":"checkpoint-a.bin");
        FIL file;FRESULT r=f_open(&file,job.path,FA_READ);
        if(r==FR_NO_FILE) continue;
        if(r!=FR_OK) {job.ops->log("Checkpoint read failed: FatFs=%u",(unsigned)r);return false;}
        bool good=f_size(&file)==sizeof(job.record) && exact_read(&file,job.record,sizeof(job.record));
        if(!close_file(&file)) return false;
        if(good && kui_checkpoint_decode(job.record,job.plan,job.state.identity,&candidate)) {
            if(best.sequence==candidate.sequence && (best.retries!=candidate.retries ||
               memcmp(best.build,candidate.build,sizeof(best.build)) ||
               memcmp(best.track,candidate.track,sizeof(best.track)))) {
                job.ops->log("Conflicting checkpoint records; preserving job");return false;
            }
            if(candidate.sequence>best.sequence) best=candidate;
        } else job.ops->log("Ignoring invalid checkpoint %c",i?'B':'A');
    }
    if(!best.sequence) {job.ops->log("No valid checkpoint for this disc/profile; preserving job");return false;}
    uint32_t identification_retries=job.state.retries;
    job.state=best;
    job.state.retries=UINT32_MAX-job.state.retries<identification_retries?UINT32_MAX:job.state.retries+identification_retries;
    job.committed=saved_bytes();
    job.ops->log("Checkpoint #%" PRIu64 ": %" PRIu64 " committed bytes",job.state.sequence,job.committed);
    return true;
}
/* Check every committed byte before any resume writes or truncation. The
 * first incomplete file may contain a later uncommitted suffix after reset.
 * Other unexpected files/sizes cause refusal, never silent replacement. */
static bool check_files(bool exact,enum kui_capture_phase phase) {
    kui_timing_phase(&job.timing,exact?KUI_TIME_VERIFY:KUI_TIME_RESUME);
    uint64_t done=0,total=saved_bytes();bool partial_seen=false;
    for(unsigned i=0;i<job.plan->count;i++) {
        if(cancelled()) return false;
        uint64_t bytes=(uint64_t)job.state.track[i].sectors*KUI_RAW_BYTES;
        uint64_t expected=(uint64_t)(job.plan->tracks[i].end-job.plan->tracks[i].start)*KUI_RAW_BYTES;
        bool first_partial=!partial_seen && bytes<expected;
        if(bytes<expected) partial_seen=true;
        kui_sha256_init(&job.hashes[i]);track_path(i);
        FILINFO info;FRESULT r=f_stat(job.path,&info);
        if(r==FR_NO_FILE && bytes==0 && !exact) continue;
        if(r!=FR_OK || (info.fattrib&AM_DIR) || info.fsize<bytes || info.fsize>expected ||
           ((exact || !first_partial) && info.fsize!=bytes) ||
           (!first_partial && partial_seen)) {
            job.ops->log("Unexpected size/file for track %02u; saved job preserved",i+1);return false;
        }
        FIL file;r=f_open(&file,job.path,FA_READ);
        if(r!=FR_OK) {job.ops->log("Cannot reopen track %02u: FatFs=%u",i+1,(unsigned)r);return false;}
        uint32_t crc=0;uint64_t offset=0;bool ok=true;
        while(offset<bytes && !cancelled()) {
            UINT n=bytes-offset>sizeof(job.data)?sizeof(job.data):(UINT)(bytes-offset);
            uint64_t start=kui_timing_begin(&job.timing);
            bool read_ok=exact_read(&file,job.data,n);
            kui_timing_end(&job.timing,KUI_TIME_READ,start,read_ok?n:0);
            if(!read_ok) {ok=false;break;}
            hash_track(&job.hashes[i],job.data,n);crc=crc_track(crc,job.data,n);
            offset+=n;done+=n;progress(phase,i,done,total);
        }
        if(!close_file(&file)) ok=false;
        if(!ok || cancelled()) return false;
        uint8_t digest[32];uint64_t start=kui_timing_begin(&job.timing);
        kui_sha256_digest(&job.hashes[i],digest);
        kui_timing_end(&job.timing,KUI_TIME_SHA256,start,0);
        if(bytes && (crc!=job.state.track[i].crc32 || memcmp(digest,job.state.track[i].sha256,32))) {
            job.ops->log("Saved data mismatch: track %02u; refusing further writes",i+1);return false;
        }
        if(bytes) {
            char hex[65];kui_hex(digest,32,hex);
            job.ops->log("T%02u verified %" PRIu64 " bytes; CRC32=%08" PRIx32,i+1,bytes,crc);
            job.ops->log("SHA256: %s",hex);
        }
    }
    return !cancelled();
}
static bool capture_tracks(void) {
    kui_timing_phase(&job.timing,KUI_TIME_CAPTURE);
    if(job.ops->read_phase) job.ops->read_phase(job.ops->ctx,true);
    for(unsigned i=0;i<job.plan->count;i++) {
        const struct kui_capture_track *t=&job.plan->tracks[i];
        uint32_t total=t->end-t->start,position=job.state.track[i].sectors;
        if(position==total) continue;
        if(cancelled()) return false;
        track_timing_start(i,position);
        track_path(i);FIL file;
        FRESULT r=f_open(&file,job.path,FA_WRITE|FA_OPEN_ALWAYS);
        if(r!=FR_OK) {
            job.ops->log("Open track %02u failed: FatFs=%u",i+1,(unsigned)r);
            track_timing_finish(i);return false;
        }
        FSIZE_t offset=(FSIZE_t)position*KUI_RAW_BYTES;
        bool ok=f_lseek(&file,offset)==FR_OK && f_tell(&file)==offset && f_truncate(&file)==FR_OK;
        uint32_t checkpoint_at=position,recovery_until=position;
        job.ops->log("Capturing T%02u at FAD=%" PRIu32 "; B stops safely",i+1,t->start+position);
        bool storage_failed=!ok;
        while(ok && position<total && !cancelled()) {
            unsigned n=total-position>KUI_CAPTURE_CHUNK?KUI_CAPTURE_CHUNK:total-position;
            if(position<recovery_until) n=1;
            unsigned requested=n;
            if(!read_raw(i,t->start+position,&n)) {ok=false;break;}
            if(n<requested) recovery_until=position+requested;
            if(cancelled()) break;
            UINT bytes=n*KUI_RAW_BYTES;
            uint64_t start=kui_timing_begin(&job.timing);
            bool write_ok=exact_write(&file,job.data,bytes);
            kui_timing_end(&job.timing,KUI_TIME_WRITE,start,write_ok?bytes:0);
            if(!write_ok) {ok=false;storage_failed=true;break;}
            hash_track(&job.hashes[i],job.data,bytes);
            job.state.track[i].crc32=crc_track(job.state.track[i].crc32,job.data,bytes);
            position+=n;job.state.track[i].sectors=position;
            progress(KUI_CAPTURING,i,saved_bytes(),job.plan->bytes);
            if(position-checkpoint_at>=KUI_CHECKPOINT_SECTORS || position==total) {
                if(!save_checkpoint(&file)) {ok=false;storage_failed=true;break;}
                checkpoint_at=position;
                job.ops->log("T%02u %" PRIu32 "/%" PRIu32 " sectors; checkpoint saved",i+1,position,total);
            }
        }
        /* Stop/read errors can still commit the successfully written prefix.
         * Storage failures never advance the committed checkpoint. */
        if(!storage_failed && position!=checkpoint_at && !save_checkpoint(&file)) ok=false;
        if(!close_file(&file)) ok=false;
        track_timing_finish(i);
        if(!ok || cancelled()) return false;
    }
    return all_captured();
}
static bool append(size_t *used,const char *format,...) {
    va_list args;va_start(args,format);
    int n=vsnprintf(job.text+*used,sizeof(job.text)-*used,format,args);va_end(args);
    if(n<0 || (size_t)n>=sizeof(job.text)-*used) return false;
    *used+=(size_t)n;return true;
}
static bool matches_file(const char *path,const char *text,size_t bytes) {
    FIL file;if(f_open(&file,path,FA_READ)!=FR_OK) return false;
    bool ok=f_size(&file)==bytes;size_t at=0;
    while(ok && at<bytes) {
        UINT n=bytes-at>sizeof(job.data)?sizeof(job.data):(UINT)(bytes-at);
        ok=exact_read(&file,job.data,n) && !memcmp(job.data,text+at,n);at+=n;
    }
    if(!close_file(&file)) ok=false;
    return ok;
}
/* Publish only verified complete jobs. Existing final metadata must match;
 * an interrupted temporary file can be regenerated from validated state. */
static bool publish(const char *name,size_t size,bool create) {
    char final[112],temp[116];
    snprintf(final,sizeof(final),"%s/%s",job.dir,name);
    FILINFO info;FRESULT r=f_stat(final,&info);
    if(r==FR_OK) {
        if(matches_file(final,job.text,size)) return true;
        job.ops->log("Existing %s differs from verified checkpoint; preserved",name);return false;
    }
    if(r!=FR_NO_FILE || !create || cancelled()) {job.ops->log("Missing/unreadable %s",name);return false;}
    snprintf(temp,sizeof(temp),"%s.tmp",final);
    FIL file;r=f_open(&file,temp,FA_WRITE|FA_CREATE_ALWAYS);
    if(r!=FR_OK) return false;
    bool ok=exact_write(&file,job.text,(UINT)size) && sync_file(&file);
    if(!close_file(&file)) ok=false;
    if(!ok || cancelled()) return false;
    if(f_rename(temp,final)!=FR_OK) return false;
    return matches_file(final,job.text,size);
}
static bool final_metadata(bool create) {
    size_t used=0;
    if(!append(&used,"%u\n",job.plan->count)) return false;
    for(unsigned i=0;i<job.plan->count;i++) {
        const struct kui_capture_track *t=&job.plan->tracks[i];uint32_t lba;
        if(!kui_fad_to_lba(t->start,&lba) || !append(&used,"%u %" PRIu32 " %" PRIu32 " 2352 track%02u.%s 0\n",
            i+1,lba,t->control,i+1,t->control==4?"bin":"raw")) return false;
    }
    if(!publish("disc.gdi",used,create)) return false;
    used=0;
    if(!append(&used,"{\n  \"schema\":1,\"complete\":true,\"saved_data_verified\":true,\n"
        "  \"profile\":\"%s\",\"identity\":\"%s\",\n  \"capture_build\":\"%s\",\n"
        "  \"title\":\"%s\",\"sector_bytes\":2352,\"reference\":\"not compared\",\n"
        "  \"audio\":\"raw drive bytes; no offset/subchannel correction\",\n  \"tracks\":[\n",
        KUI_CAPTURE_PROFILE,job.identity,job.state.build,job.title)) return false;
    for(unsigned i=0;i<job.plan->count;i++) {
        const struct kui_capture_track *t=&job.plan->tracks[i];char sha[65];
        kui_hex(job.state.track[i].sha256,32,sha);
        if(!append(&used,"    {\"number\":%u,\"session\":%" PRIu32 ",\"control\":%" PRIu32
            ",\"start_fad\":%" PRIu32 ",\"end_fad\":%" PRIu32 ",\"toc_end_fad\":%" PRIu32
            ",\"excluded_tail_sectors\":%" PRIu32 ",\"file\":\"track%02u.%s\",\"bytes\":%" PRIu64
            ",\"crc32\":\"%08" PRIx32 "\",\"sha256\":\"%s\"}%s\n",i+1,t->session,t->control,
            t->start,t->end,t->toc_end,t->toc_end-t->end,i+1,t->control==4?"bin":"raw",
            (uint64_t)job.state.track[i].sectors*KUI_RAW_BYTES,job.state.track[i].crc32,sha,
            i+1==job.plan->count?"":",")) return false;
    }
    if(!append(&used,"  ]\n}\n")) return false;
    return publish("manifest.json",used,create);
}
static bool enough_space(FATFS *fs) {
    DWORD clusters;FATFS *mounted;
    FRESULT r=f_getfree("0:",&clusters,&mounted);
    if(r!=FR_OK || mounted!=fs) {job.ops->log("Free-space check failed: %u",(unsigned)r);return false;}
    uint64_t cluster=(uint64_t)fs->csize*512;
    uint64_t free_bytes=(uint64_t)clusters*cluster;
    /* Account for allocation rounding on large-cluster exFAT cards too.
     * Reserve both checkpoints, maximum manifest/GDI sizes and directories. */
    uint64_t needed=(2*((KUI_CHECKPOINT_BYTES+cluster-1)/cluster)+
        (sizeof(job.text)+cluster-1)/cluster+(8192+cluster-1)/cluster+3)*cluster;
    for(unsigned i=0;i<job.plan->count;i++) {
        uint64_t expected=(uint64_t)(job.plan->tracks[i].end-job.plan->tracks[i].start)*KUI_RAW_BYTES;
        uint64_t present=(uint64_t)job.state.track[i].sectors*KUI_RAW_BYTES;
        needed+=((expected+cluster-1)/cluster-(present+cluster-1)/cluster)*cluster;
    }
    if(free_bytes<needed) {
        job.ops->log("Insufficient SD space: need %" PRIu64 ", available %" PRIu64 " bytes",needed,free_bytes);return false;
    }
    return true;
}
enum kui_capture_result kui_capture(const struct kui_capture_plan *plan,
    const struct kui_capture_ops *ops,enum kui_capture_mode mode) {
    if(!plan || !plan->count || plan->count>99 || !ops || !ops->read || !ops->cancelled ||
       !ops->now_ms || !ops->log || !ops->build || strlen(ops->build)!=12 || mode>KUI_CAPTURE_VERIFY)
        return KUI_CAPTURE_FAILED;
    for(unsigned i=0;i<12;i++) if(!((ops->build[i]>='0'&&ops->build[i]<='9') ||
                                  (ops->build[i]>='a'&&ops->build[i]<='f'))) return KUI_CAPTURE_FAILED;
    memset(&job,0,sizeof(job));job.plan=plan;job.ops=ops;
    if(ops->read_phase) ops->read_phase(ops->ctx,false);
    kui_timing_start(&job.timing,ops->now_us,ops->ctx);
    job.started=ops->now_ms(ops->ctx);job.state.count=plan->count;memcpy(job.state.build,ops->build,12);
    for(unsigned i=0;i<plan->count;i++) kui_sha256_init(&job.hashes[i]);
    FATFS fs;enum kui_capture_result result=KUI_CAPTURE_FAILED;
    ops->log("%s: guarded raw reads, data EDC, no zero-fill",mode==KUI_CAPTURE_NEW?"NEW CAPTURE":
        mode==KUI_CAPTURE_RESUME?"RESUME LATEST MATCHING JOB":"VERIFY LATEST MATCHING JOB");
    if(cancelled() || !identify()) {
        result=cancelled()?KUI_CAPTURE_STOPPED:KUI_CAPTURE_FAILED;
        report_timing();return result;
    }
    if(!kui_mount(&fs,ops->log)) goto out;
    if(cancelled()) goto out;
    if(mode==KUI_CAPTURE_NEW && !enough_space(&fs)) goto out;
    if(!choose_dir(mode) || cancelled()) goto out;
    if(mode==KUI_CAPTURE_NEW) {
        if(!save_checkpoint(NULL)) goto out;
    } else {
        if(!load_checkpoint()) goto out;
        if(mode==KUI_CAPTURE_VERIFY && !all_captured()) {ops->log("Job is incomplete; use Resume");goto out;}
        ops->log("Checking saved bytes before any resume writes...");
        if(!check_files(mode==KUI_CAPTURE_VERIFY,KUI_PREFIX_CHECK)) goto out;
    }
    bool already_complete=all_captured();
    if(mode!=KUI_CAPTURE_VERIFY && !already_complete) {
        if(!enough_space(&fs) || !capture_tracks() || cancelled()) goto out;
        kui_timing_phase(&job.timing,KUI_TIME_VERIFY);
        if(f_mount(NULL,"0:",0)!=FR_OK || !kui_mount(&fs,ops->log)) goto out;
        ops->log("Capture written. Rereading all saved tracks...");
        if(!check_files(true,KUI_VERIFYING)) goto out;
    }
    if(cancelled() || !all_captured()) goto out;
    kui_timing_phase(&job.timing,KUI_TIME_FINISH);
    if(!final_metadata(mode!=KUI_CAPTURE_VERIFY)) goto out;
    result=KUI_CAPTURE_COMPLETE;progress(KUI_FINISHED,plan->count-1,plan->bytes,plan->bytes);
    ops->log("SAVED DATA VERIFIED: all %u tracks; CRC32 and SHA-256",plan->count);
    ops->log("No independent reference compared. Output: %s/disc.gdi",job.dir+2);
out:
    if(result!=KUI_CAPTURE_COMPLETE) {
        if(cancelled()) result=KUI_CAPTURE_STOPPED;
        ops->log("%s: last committed bytes=%" PRIu64 "; retries=%" PRIu32,
            result==KUI_CAPTURE_STOPPED?"STOPPED":"CAPTURE/VERIFY FAILED",job.committed,job.state.retries);
        if(job.dir[0]) ops->log("Partial job preserved: %s; Resume checks saved bytes first",job.dir+2);
    }
    if(f_mount(NULL,"0:",0)!=FR_OK) result=KUI_CAPTURE_FAILED;
    report_timing();
    return result;
}
