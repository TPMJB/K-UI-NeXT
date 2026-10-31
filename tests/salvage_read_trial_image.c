/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "kui/salvage.h"
#include "kui/recovery_guard.h"
#include "kui/media.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Real job publication/replay over real FatFs, with optical content depending
 * on the actual requested window and approach. All fixture sectors have real
 * Mode 1 address, EDC and P/Q; alternate sectors are distinct valid codewords. */
static struct kui_capture_plan plan={.count=3,.bytes=116u*KUI_RAW_BYTES,.tracks={
    {1,4,0,150,166,600},{2,0,0,600,604,1825},{3,4,1,45150,45246,45246}}};
static const unsigned small_targets[]={0,1,8,15,24,40,80,95};
static uint8_t *original[3],*alternate[3],card[32]={0x29,0x44};
static unsigned target_sector[32],target_count;
static FATFS fs;
static struct {
    FILE *image;uint64_t blocks,now;const char *mode;unsigned stage;
    struct kui_salvage_status progress;
    bool cancelled,seed_cancelled,injected,fatal,warmup;
    unsigned optical_calls,trial_calls,approach_calls,target_calls,recoveries;
    unsigned media_reads,media_writes,journal_phases,target_phases;
    unsigned step_calls[64],seen[32][4],order[64],fads[64],starts[64],counts[64];
    unsigned current_step;
    struct {FIL *file;char name[128];} files[16];
    unsigned files_open,max_files;
} t;
static bool mode(const char *name) {return !strcmp(t.mode,name);}
static uint32_t word(const uint8_t *p) {return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void log_line(const char *format,...) {va_list a;va_start(a,format);vprintf(format,a);va_end(a);puts("");}
static bool named(FIL *file,const char *name) {
    for(unsigned i=0;i<16;i++)if(t.files[i].file==file) {
        const char *p=strrchr(t.files[i].name,'/');return !strcmp(p?p+1:t.files[i].name,name);
    }
    return false;
}
FRESULT __real_f_open(FIL *,const TCHAR *,BYTE);
FRESULT __wrap_f_open(FIL *file,const TCHAR *name,BYTE flags) {
    FRESULT r=__real_f_open(file,name,flags);
    if(r==FR_OK) {
        unsigned i=0;while(i<16 && t.files[i].file)i++;assert(i<16);
        t.files[i].file=file;snprintf(t.files[i].name,sizeof(t.files[i].name),"%s",name);
        ++t.files_open;if(t.files_open>t.max_files)t.max_files=t.files_open;
    }
    return r;
}
FRESULT __real_f_close(FIL *);
FRESULT __wrap_f_close(FIL *file) {
    FRESULT r=__real_f_close(file);
    for(unsigned i=0;i<16;i++)if(t.files[i].file==file) {t.files[i].file=NULL;assert(t.files_open);--t.files_open;break;}
    return r;
}
FRESULT __real_f_write(FIL *,const void *,UINT,UINT *);
FRESULT __wrap_f_write(FIL *file,const void *data,UINT bytes,UINT *put) {
    if(t.stage==2 && !t.injected &&
       ((mode("patch-write-fail") && bytes==5120 && !memcmp(data,"KUISPAT2",8)) ||
        (mode("track-write-fail") && named(file,"track03.bin")))) {
        t.injected=true;*put=0;return FR_DISK_ERR;
    }
    return __real_f_write(file,data,bytes,put);
}
FRESULT __real_f_sync(FIL *);
FRESULT __wrap_f_sync(FIL *file) {
    if(t.stage==2 && !t.injected && mode("patch-sync-fail") && named(file,"patches.bin") && f_size(file)) {
        t.injected=true;return FR_DISK_ERR;
    }
    return __real_f_sync(file);
}
static uint64_t blocks(void *ctx) {(void)ctx;return t.blocks;}
static int image_read(void *ctx,uint32_t block,size_t count,uint8_t *data) {
    (void)ctx;++t.media_reads;
    return fseeko(t.image,(off_t)block*512,SEEK_SET) || fread(data,512,count,t.image)!=count?-1:0;
}
static int image_write(void *ctx,uint32_t block,size_t count,const uint8_t *data) {
    (void)ctx;++t.media_writes;
    return fseeko(t.image,(off_t)block*512,SEEK_SET) || fwrite(data,512,count,t.image)!=count?-1:0;
}
static int image_sync(void *ctx) {(void)ctx;return fflush(t.image) || fsync(fileno(t.image))?-1:0;}
static const struct kui_media_ops media={NULL,blocks,image_read,image_write,image_sync};
static bool cancelled(void *ctx) {(void)ctx;return t.cancelled;}
static uint64_t now_ms(void *ctx) {(void)ctx;return ++t.now;}
static void progress(void *ctx,const struct kui_salvage_status *s) {
    (void)ctx;t.progress=*s;
    assert(s->recovered<=s->targets && s->remaining==s->targets-s->recovered);
    if(s->sd_phase==KUI_SALVAGE_SD_JOURNAL)++t.journal_phases;
    if(s->sd_phase==KUI_SALVAGE_SD_TARGETS)++t.target_phases;
    if(t.stage==1 && ((mode("unfinished") && s->collection_active && s->done>=32u*KUI_RAW_BYTES) ||
                     (!mode("unfinished") && s->first_pass_complete && !s->pass))) {
        t.cancelled=t.seed_cancelled=true;
    }
    if(t.stage==2 && s->trial_active && s->read_state==KUI_SALVAGE_READ_READING) {
        if(mode("stop-before-target") && !t.trial_calls)t.cancelled=true;
        if(mode("stop-before-approach") && s->trial_profile==3 && !t.approach_calls)t.cancelled=true;
    }
}
static bool is_target(unsigned ti,unsigned sector) {
    if(ti==1)return mode("audio-mixed") || mode("audio-only");
    if(ti!=2 || mode("audio-only"))return false;
    for(unsigned i=0;i<target_count;i++)if(target_sector[i]==sector)return true;
    return false;
}
static bool no_target_success(void) {
    return mode("all-bad") || mode("anchor-bad") || mode("anchor-different") || mode("fatal-anchor") ||
        mode("stop-before-approach") || mode("stop-after-approach") || mode("budget-after-approach");
}
static enum kui_read_result optical(void *ctx,uint32_t fad,unsigned count,uint8_t *data) {
    (void)ctx;assert(!t.fatal && !t.cancelled);++t.optical_calls;
    unsigned ti=0;while(ti<3 && !(fad>=plan.tracks[ti].start && fad<plan.tracks[ti].end))ti++;
    assert(ti<3 && count && count<=32 && count<=plan.tracks[ti].end-fad);
    unsigned at=fad-plan.tracks[ti].start;
    memcpy(data,original[ti]+(size_t)at*KUI_RAW_BYTES,(size_t)count*KUI_RAW_BYTES);
    if(t.stage==1) {
        if(t.progress.collection_active) {
            if(ti==1 && is_target(ti,at))return KUI_READ_RETRY;
            for(unsigned n=0;n<count;n++)if(is_target(ti,at+n))data[(size_t)n*KUI_RAW_BYTES+2200]^=1;
        }
        return KUI_READ_OK;
    }
    if(t.warmup && t.progress.pass && ti==2 && is_target(ti,at)) {t.cancelled=true;return KUI_READ_RETRY;}
    if(!t.progress.trial_active)return KUI_READ_OK; /* Fresh identity admission. */
    assert(t.stage==2 && ti!=1 && t.progress.trial_profile<4);
    unsigned p=t.progress.trial_profile,step=t.progress.trial_done;
    assert(step<64);++t.trial_calls;unsigned sub=t.step_calls[step]++;
    t.now+=100u*(p+1u);
    unsigned samples=target_count<16?target_count:16;
    unsigned sample=step/4u,rank=samples==1?0:(unsigned)((uint64_t)sample*(target_count-1u)/(samples-1u));
    assert(sample<samples);unsigned target=target_sector[rank];uint32_t target_fad=plan.tracks[2].start+target;
    if(p==3 && sub==0) {
        ++t.approach_calls;assert(count==1);
        /* Saved anchors are the first/last data sectors, equidistant ties use
         * anchor0, and the more distant anchor must be selected. */
        uint32_t a=plan.tracks[2].start,b=plan.tracks[2].end-1;
        assert(fad==((target_fad-a>=b-target_fad)?a:b));
        if(mode("anchor-bad"))return KUI_READ_RETRY;
        if(mode("anchor-different"))memcpy(data,alternate[ti]+(size_t)at*KUI_RAW_BYTES,KUI_RAW_BYTES);
        if(mode("fatal-anchor")) {t.fatal=true;return KUI_READ_FATAL;}
        if(mode("stop-after-approach"))t.cancelled=true;
        if(mode("budget-after-approach"))t.now+=600001u;
        return KUI_READ_OK;
    }
    assert(ti==2 && fad<=target_fad && target_fad<fad+count);
    unsigned offset=target_fad-fad;
    if(p==0)assert(fad==target_fad && count==1 && sub==0);
    if(p==1)assert(count==(target+1u<4u?target+1u:4u) && offset+1==count && sub==0);
    if(p==2) {
        unsigned length=plan.tracks[2].end-plan.tracks[2].start,width=length<16?length:16;
        unsigned start=target>=8?target-8:0;if(start>length-width)start=length-width;
        assert(fad==45150+start && count==width && sub==0);
    }
    if(p==3)assert(count==(target+1u<16u?target+1u:16u) && offset+1==count && sub==1);
    assert(!t.seen[rank][p]);++t.seen[rank][p];
    unsigned call=t.target_calls++;assert(call<64);
    t.order[call]=p;t.fads[call]=target_fad;t.starts[call]=fad;t.counts[call]=count;
    if(mode("budget")) {t.now+=600001u;return KUI_READ_RETRY;}
    if(mode("fatal")) {t.fatal=true;return KUI_READ_FATAL;}
    if(mode("stop-read") || mode("stop-partial")) {t.cancelled=true;return mode("stop-partial")?KUI_READ_PARTIAL:KUI_READ_OK;}
    if(no_target_success())return KUI_READ_RETRY;
    if(mode("windows") && count==1)return KUI_READ_RETRY;
    uint8_t *raw=data+(size_t)offset*KUI_RAW_BYTES;
    if(mode("wrong-fad"))memcpy(raw,original[2]+(size_t)(target?target-1:1)*KUI_RAW_BYTES,KUI_RAW_BYTES);
    if(mode("bad-edc"))raw[2064]^=1;
    if(mode("bad-pq"))raw[2200]^=1;
    if(mode("bad-payload"))raw[17]^=1;
    if(mode("conflict") && t.target_calls==2)memcpy(raw,alternate[2]+(size_t)target*KUI_RAW_BYTES,KUI_RAW_BYTES);
    if(mode("partial") || mode("partial-invalid")) {
        if(mode("partial-invalid"))raw[17]^=1;
        /* Other returned slots are deliberately invalid; only target bytes
         * authorize the selected repair, including when transport failed. */
        for(unsigned i=0;i<count;i++)if(i!=offset)data[(size_t)i*KUI_RAW_BYTES+2200]^=1;
        return KUI_READ_PARTIAL;
    }
    return KUI_READ_OK;
}
static bool recover(void *ctx,const struct kui_capture_plan *p) {(void)ctx;(void)p;++t.recoveries;return true;}
static enum kui_salvage_result run(struct kui_salvage_options *o,enum kui_salvage_action action,struct kui_salvage_status *s,bool clock) {
    assert(!t.files_open);t.cancelled=false;t.journal_phases=t.target_phases=0;
    kui_media_set(NULL);kui_media_set(&media);
    const struct kui_capture_ops ops={.read=optical,.cancelled=cancelled,.now_ms=clock?now_ms:NULL,.log=log_line,.recover=recover};
    enum kui_salvage_result r=kui_salvage_run(&plan,&ops,o,action,s);assert(!t.files_open);return r;
}
static void mount_card(void) {assert(f_mount(NULL,"0:",0)==FR_OK);kui_media_set(&media);assert(kui_mount(&fs,log_line));}
static unsigned inspect(const char *job,bool *repaired) {
    mount_card();char path[512];FIL file;UINT got;uint8_t event[512];unsigned targets=0,repairs=0,candidates=0;
    uint32_t candidate_targets[32],target_tracks[32],target_sectors[32];
    snprintf(path,sizeof(path),"0:%s/journal.bin",job);assert(f_open(&file,path,FA_READ)==FR_OK);
    while(f_tell(&file)<f_size(&file)) {
        assert(f_read(&file,event,512,&got)==FR_OK && got==512 && word(event+508)==kui_crc32(0,event,508));
        unsigned type=word(event+16);
        if(type==1) {assert(targets<32);target_tracks[targets]=word(event+20);target_sectors[targets]=word(event+24);++targets;}
        if(type==7) {assert(candidates<32 && word(event+24)==candidates);candidate_targets[candidates++]=word(event+20);}
        if(type==5) {unsigned index=word(event+20);assert(index<32 && !repaired[index]);repaired[index]=true;++repairs;}
    }
    assert(f_close(&file)==FR_OK && repairs<=candidates);
    assert(targets==target_count+(mode("audio-mixed") || mode("audio-only")?4u:0u));
    snprintf(path,sizeof(path),"0:%s/patches.bin",job);FRESULT packed=f_open(&file,path,FA_READ);
    assert(packed==FR_OK || (packed==FR_NO_FILE && !candidates));
    if(packed==FR_OK)assert(f_size(&file)==(uint64_t)(candidates+(mode("patch-sync-fail")?1u:0u))*5120u);
    for(unsigned slot=0;slot<candidates;slot++) {
        uint8_t patch[5120];assert(f_read(&file,patch,sizeof(patch),&got)==FR_OK && got==sizeof(patch));
        unsigned target=candidate_targets[slot];assert(target<targets);
        assert(!memcmp(patch,"KUISPAT2",8) && word(patch+8)==target && word(patch+12)==target_tracks[target] &&
            word(patch+16)==target_sectors[target] && word(patch+20)==1 && word(patch+5116)==kui_crc32(0,patch,5116));
        uint8_t zero[KUI_RAW_BYTES]={0};assert(!memcmp(patch+64,zero,sizeof(zero)));
        assert(!memcmp(patch+64+KUI_RAW_BYTES,original[target_tracks[target]]+(size_t)target_sectors[target]*KUI_RAW_BYTES,KUI_RAW_BYTES));
    }
    if(packed==FR_OK)assert(f_close(&file)==FR_OK);
    for(unsigned ti=0;ti<3;ti++) {
        size_t size=(size_t)(plan.tracks[ti].end-plan.tracks[ti].start)*KUI_RAW_BYTES;
        uint8_t *data=malloc(size),*expected=malloc(size);assert(data && expected);memcpy(expected,original[ti],size);
        if(ti==1 && (mode("audio-mixed") || mode("audio-only")))memset(expected,0,size);
        if(ti==2)for(unsigned n=0;n<target_count;n++) {
            unsigned index=n+(mode("audio-mixed")?4u:0u);
            if(!repaired[index])memset(expected+(size_t)target_sector[n]*KUI_RAW_BYTES,0,KUI_RAW_BYTES);
        }
        snprintf(path,sizeof(path),"0:%s/track%02u.%s",job,ti+1,ti==1?"raw":"bin");
        assert(f_open(&file,path,FA_READ)==FR_OK && f_size(&file)==size && f_read(&file,data,(UINT)size,&got)==FR_OK && got==size && f_close(&file)==FR_OK);
        assert(!memcmp(data,expected,size));free(data);free(expected);
    }
    assert(f_mount(NULL,"0:",0)==FR_OK);return repairs;
}
int main(int argc,char **argv) {
    setvbuf(stdout,NULL,_IONBF,0);assert(argc==4);t.mode=argv[3];struct stat st;
    assert(!stat(argv[1],&st) && st.st_size%512==0);t.blocks=(uint64_t)st.st_size/512;t.image=fopen(argv[1],"r+b");assert(t.image);
    target_count=mode("spread")?25u:mode("audio-only")?0u:mode("single")?1u:8u;
    if(mode("short-track")) {plan.tracks[2].end=plan.tracks[2].toc_end=45160;plan.bytes=30u*KUI_RAW_BYTES;}
    for(unsigned i=0;i<target_count;i++)target_sector[i]=mode("spread")?8u+i:mode("single")?24u:mode("short-track")?(unsigned[]){0,1,3,4,6,7,8,9}[i]:small_targets[i];
    for(unsigned i=0;i<3;i++) {
        size_t bytes=(size_t)(plan.tracks[i].end-plan.tracks[i].start)*KUI_RAW_BYTES;
        original[i]=malloc(bytes);alternate[i]=malloc(bytes);assert(original[i] && alternate[i]);
        for(unsigned other=0;other<2;other++) {
            char path[1024];snprintf(path,sizeof(path),"%s/%strack%02u.%s",argv[2],other?"alternate-":"",i+1,i==1?"raw":"bin");
            FILE *file=fopen(path,"rb");assert(file && fread(other?alternate[i]:original[i],1,bytes,file)==bytes && !fclose(file));
        }
    }
    kui_media_set(&media);kui_salvage_session_invalidate();
    struct kui_salvage_options o={.passes=1,.progress=progress,.session_epoch=31,.session_card=card};
    struct kui_salvage_status s;enum kui_salvage_result result;
    if(mode("invalid-new")) {
        o.read_trial=true;result=run(&o,KUI_SALVAGE_NEW,&s,true);
        assert(result==KUI_SALVAGE_FAILED && !t.optical_calls && !t.media_reads && !t.media_writes);goto done;
    }
    t.stage=1;result=run(&o,KUI_SALVAGE_NEW,&s,true);
    assert(result==KUI_SALVAGE_STOPPED && t.seed_cancelled && s.job[0] && !s.recovered);
    char job[KUI_DEST_JOB_CAP];snprintf(job,sizeof(job),"%s",s.job);o.job=job;
    if(!mode("unfinished"))assert(s.first_pass_complete && s.targets==target_count+(mode("audio-mixed") || mode("audio-only")?4u:0u));
    if(mode("session")) {
        t.stage=2;t.warmup=true;result=run(&o,KUI_SALVAGE_RESUME,&s,true);t.warmup=false;
        assert(result==KUI_SALVAGE_STOPPED && s.session_retained && !s.session_reused);
    }
    if(mode("guard")) {mount_card();assert(kui_recovery_guard_mark(job,log_line));assert(f_mount(NULL,"0:",0)==FR_OK);}
    t.stage=2;o.read_trial=true;
    if(mode("mutually-exclusive"))o.control_trial=true;
    if(mode("defer-exclusive"))o.defer_next_range=true;
    unsigned before_reads=t.media_reads,before_writes=t.media_writes,before_optical=t.optical_calls;
    result=run(&o,mode("invalid-resume")?KUI_SALVAGE_RESUME:KUI_SALVAGE_RECOVER,&s,!mode("missing-clock"));
    assert(!s.trial_active && !s.session_reused && !s.session_retained && !t.recoveries);
    if(mode("invalid-resume") || mode("mutually-exclusive") || mode("defer-exclusive") || mode("missing-clock")) {
        assert(result==KUI_SALVAGE_FAILED && t.media_reads==before_reads && t.media_writes==before_writes && t.optical_calls==before_optical);goto done;
    }
    if(mode("unfinished")) {assert(result==KUI_SALVAGE_FAILED && !t.trial_calls);goto done;}
    bool repaired[32]={false};unsigned durable=inspect(job,repaired);assert(durable==s.recovered);
    if(mode("guard") || mode("audio-only")) {
        assert(result==KUI_SALVAGE_FAILED && !t.trial_calls && !s.recovered);
        if(mode("guard"))assert(t.media_writes==before_writes);
        goto done;
    }
    assert(s.trial_read_strategy && s.trial_sample_count==(target_count<16?target_count:16));
    if(mode("session"))assert(t.journal_phases && t.target_phases);
    if(mode("stop-before-target") || mode("stop-read") || mode("stop-partial") || mode("stop-before-approach") || mode("stop-after-approach")) {
        assert(result==KUI_SALVAGE_STOPPED && !s.trial_complete && !s.recovered);
        if(mode("stop-before-target"))assert(!t.trial_calls);
        if(mode("stop-read") || mode("stop-partial"))assert(t.trial_calls==1);
        if(mode("stop-before-approach"))assert(!t.approach_calls && t.target_calls==3);
        if(mode("stop-after-approach"))assert(t.approach_calls==1 && t.target_calls==3);
    } else if(mode("budget") || mode("budget-after-approach")) {
        assert(result==KUI_SALVAGE_UNRESOLVED && s.trial_budget_expired && !s.trial_complete && !s.recovered);
        if(mode("budget"))assert(t.target_calls==1 && !t.approach_calls);
        else assert(t.target_calls==3 && t.approach_calls==1);
    } else if(mode("fatal") || mode("fatal-anchor") || mode("anchor-different") || mode("conflict") ||
              mode("patch-write-fail") || mode("patch-sync-fail") || mode("track-write-fail")) {
        assert(result==KUI_SALVAGE_FAILED && !s.trial_complete);
        if(mode("conflict"))assert(s.recovered==1 && t.target_calls==2 && repaired[0]);
        else assert(!s.recovered);
        if(mode("fatal"))assert(t.target_calls==1 && !t.approach_calls);
        if(mode("fatal-anchor") || mode("anchor-different"))assert(t.target_calls==3 && t.approach_calls==1);
        if(mode("patch-write-fail") || mode("patch-sync-fail") || mode("track-write-fail"))assert(t.injected && t.target_calls==1);
    } else {
        unsigned samples=target_count<16?target_count:16;
        bool bad=no_target_success() || mode("wrong-fad") || mode("bad-edc") || mode("bad-pq") || mode("bad-payload") || mode("partial-invalid");
        assert(s.trial_complete && !s.trial_budget_expired && s.trial_done==samples*4 && s.trial_total==samples*4);
        assert(s.recovered==(bad?0u:samples));
        assert(result==((s.remaining || bad)?KUI_SALVAGE_UNRESOLVED:KUI_SALVAGE_RESOLVED));
        assert(t.approach_calls==samples && t.target_calls==samples*(mode("anchor-bad")?3u:4u));
        unsigned call=0;
        for(unsigned sample=0;sample<samples;sample++)for(unsigned turn=0;turn<4;turn++) {
            unsigned p=(sample+turn)%4,rank=samples==1?0:(unsigned)((uint64_t)sample*(target_count-1)/(samples-1));
            if(p==3 && mode("anchor-bad"))continue;
            assert(t.order[call]==p && t.fads[call]==45150+target_sector[rank] && t.seen[rank][p]==1);++call;
        }
        assert(call==t.target_calls);
        unsigned new_repairs=0;
        for(unsigned p=0;p<4;p++) {
            unsigned reads_per_method=p==3 && !mode("anchor-bad")?2u:1u;
            assert(s.trial_profile_reads[p]==samples && s.trial_profile_ms[p]>=100u*(p+1)*samples*reads_per_method);
            new_repairs+=s.trial_profile_repairs[p];
            if(bad)assert(!s.trial_profile_valid[p]);
            else if(mode("windows"))assert(s.trial_profile_valid[p]==(p==0?0u:p==2?samples:samples-1u));
            else assert(s.trial_profile_valid[p]==samples);
        }
        assert(new_repairs==s.recovered);
    }
done:
    assert(!t.files_open && t.max_files<=8 && f_mount(NULL,"0:",0)==FR_OK && !fclose(t.image));
    for(unsigned i=0;i<3;i++) {free(original[i]);free(alternate[i]);}
    printf("PASS salvage read trial %s\n",t.mode);return 0;
}
