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

static struct kui_capture_plan plan={.count=3,.bytes=84u*KUI_RAW_BYTES,.tracks={
    {1,4,0,150,166,600},{2,0,0,600,604,1825},{3,4,1,45150,45214,45214}}};
static uint8_t *original[3],card[32]={0x37,0x19};
static unsigned targets=8,saved_repairs=1;
static struct {
    FILE *image;uint64_t blocks,now;const char *mode;
    unsigned stage,journal_phases,target_phases,work_reads,stop_reads,stop_repairs;
    bool cancelled,repair,inject,injected,incomplete;
    unsigned files_open,max_files,reads,writes;
    struct {FIL *file;char name[128];} files[16];
} t;
static FATFS fs;
static uint32_t word(const uint8_t *p) {return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put(uint8_t *p,uint32_t n) {for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i));}
static bool mode(const char *name) {return !strcmp(t.mode,name);}
static void log_line(const char *format,...) {va_list a;va_start(a,format);vprintf(format,a);va_end(a);puts("");}
static const char *file_name(FIL *file) {
    for(unsigned i=0;i<16;i++)if(t.files[i].file==file)return t.files[i].name;
    return "";
}
static bool named(FIL *file,const char *name) {const char *p=strrchr(file_name(file),'/');return !strcmp(p?p+1:file_name(file),name);}
FRESULT __real_f_open(FIL *,const TCHAR *,BYTE);
FRESULT __wrap_f_open(FIL *file,const TCHAR *name,BYTE flags) {
    FRESULT result=__real_f_open(file,name,flags);
    if(result==FR_OK) {
        unsigned i=0;while(i<16 && t.files[i].file)i++;assert(i<16);
        t.files[i].file=file;snprintf(t.files[i].name,sizeof(t.files[i].name),"%s",name);
        ++t.files_open;if(t.files_open>t.max_files)t.max_files=t.files_open;
    }
    return result;
}
FRESULT __real_f_close(FIL *);
FRESULT __wrap_f_close(FIL *file) {
    bool fail=t.inject && !t.injected && t.stage==2 && t.cancelled && mode("warm-close-fail") && named(file,"journal.bin");
    FRESULT result=__real_f_close(file);
    for(unsigned i=0;i<16;i++)if(t.files[i].file==file) {t.files[i].file=NULL;assert(t.files_open);--t.files_open;break;}
    if(fail) {t.injected=true;return FR_DISK_ERR;}
    return result;
}
FRESULT __real_f_write(FIL *,const void *,UINT,UINT *);
FRESULT __wrap_f_write(FIL *file,const void *data,UINT bytes,UINT *put_bytes) {
    FRESULT result=__real_f_write(file,data,bytes,put_bytes);
    if(t.inject && !t.injected && t.stage==2 && mode("warm-pending-candidate") &&
       result==FR_OK && *put_bytes==bytes && bytes==512 && !memcmp(data,"KUISJNL1",8) && word((const uint8_t *)data+16)==7) {
        t.injected=true;t.cancelled=true;
    }
    return result;
}
FRESULT __real_f_mount(FATFS *,const TCHAR *,BYTE);
FRESULT __wrap_f_mount(FATFS *volume,const TCHAR *path,BYTE immediate) {
    FRESULT result=__real_f_mount(volume,path,immediate);
    if(t.inject && !t.injected && t.stage==2 && t.cancelled && mode("warm-unmount-fail") && !volume) {
        t.injected=true;return FR_DISK_ERR;
    }
    return result;
}
FRESULT __real_f_read(FIL *,void *,UINT,UINT *);
FRESULT __wrap_f_read(FIL *file,void *data,UINT bytes,UINT *got) {
    if(t.inject && !t.injected && t.stage==3 && named(file,"targets.cache") &&
       (mode("warm-index-read-fail") || mode("warm-index-short-read"))) {
        t.injected=true;*got=0;return mode("warm-index-read-fail")?FR_DISK_ERR:FR_OK;
    }
    FRESULT result=__real_f_read(file,data,bytes,got);
    if(t.inject && !t.injected && t.stage==3 && mode("warm-cancel-preflight") && named(file,"header.bin")) {
        t.injected=true;t.cancelled=true;
    }
    return result;
}
static uint64_t blocks(void *ctx) {(void)ctx;return t.blocks;}
static int image_read(void *ctx,uint32_t block,size_t count,uint8_t *data) {
    (void)ctx;++t.reads;
    return fseeko(t.image,(off_t)block*512,SEEK_SET) || fread(data,512,count,t.image)!=count?-1:0;
}
static int image_write(void *ctx,uint32_t block,size_t count,const uint8_t *data) {
    (void)ctx;++t.writes;
    return fseeko(t.image,(off_t)block*512,SEEK_SET) || fwrite(data,512,count,t.image)!=count?-1:0;
}
static int image_sync(void *ctx) {
    (void)ctx;
    if(t.inject && !t.injected && t.stage==2 && t.cancelled && mode("warm-sync-fail")) {t.injected=true;return -1;}
    return fflush(t.image) || fsync(fileno(t.image))?-1:0;
}
static const struct kui_media_ops media={NULL,blocks,image_read,image_write,image_sync};
static bool cancelled(void *ctx) {(void)ctx;return t.cancelled;}
static uint64_t now_ms(void *ctx) {(void)ctx;return ++t.now;}
static void progress(void *ctx,const struct kui_salvage_status *s) {
    (void)ctx;assert(s->recovered<=s->targets && s->remaining==s->targets-s->recovered);
    if(s->sd_phase==KUI_SALVAGE_SD_JOURNAL)++t.journal_phases;
    if(s->sd_phase==KUI_SALVAGE_SD_TARGETS)++t.target_phases;
    if(t.stop_repairs && s->pass && s->recovered>=t.stop_repairs)t.cancelled=true;
    if(t.incomplete && s->collection_active && s->done>=32u*KUI_RAW_BYTES)t.cancelled=true;
}
static enum kui_read_result optical(void *ctx,uint32_t fad,unsigned count,uint8_t *data) {
    (void)ctx;unsigned ti=0;while(ti<3 && !(fad>=plan.tracks[ti].start && fad<plan.tracks[ti].end))ti++;
    assert(ti<3 && count && count<=plan.tracks[ti].end-fad);
    memcpy(data,original[ti]+(size_t)(fad-plan.tracks[ti].start)*KUI_RAW_BYTES,(size_t)count*KUI_RAW_BYTES);
    bool target=ti==2 && fad<45158+targets && fad+count>45158;
    if(count>1 && target) {
        for(unsigned i=0;i<count;i++)if(fad+i>=45158 && fad+i<45158+targets)data[i*KUI_RAW_BYTES+2200]^=1;
    } else if(target) {
        ++t.work_reads;
        if(t.stop_reads && t.work_reads>=t.stop_reads)t.cancelled=true;
        if(!t.repair)return KUI_READ_RETRY;
    }
    return KUI_READ_OK;
}
static enum kui_salvage_result run(struct kui_salvage_options *options,enum kui_salvage_action action,struct kui_salvage_status *out) {
    t.journal_phases=t.target_phases=t.work_reads=0;t.cancelled=false;
    /* Match the GUI adapter's disconnect/reconnect boundary between actions.
     * A failed disk session must not recover silently inside one action. */
    assert(!t.files_open);kui_media_set(NULL);kui_media_set(&media);
    const struct kui_capture_ops ops={.read=optical,.cancelled=cancelled,.now_ms=now_ms,.log=log_line};
    enum kui_salvage_result result=kui_salvage_run(&plan,&ops,options,action,out);
    assert(!t.files_open);return result;
}
static void mount_card(void) {assert(f_mount(NULL,"0:",0)==FR_OK);kui_media_set(&media);assert(kui_mount(&fs,log_line));}
static void mutate(const char *job,const char *name,bool append,bool truncate,bool coherent) {
    mount_card();char path[128];snprintf(path,sizeof(path),"0:%s/%s",job,name);FIL file;UINT n=0;
    FILINFO stamp;assert(f_stat(path,&stamp)==FR_OK);
    assert(f_open(&file,path,FA_READ|FA_WRITE)==FR_OK);FSIZE_t size=f_size(&file);
    if(append) {uint8_t zero[512]={0};assert(f_lseek(&file,size)==FR_OK && f_write(&file,zero,sizeof(zero),&n)==FR_OK && n==sizeof(zero));}
    else if(truncate) {assert(size && f_lseek(&file,size-1)==FR_OK && f_truncate(&file)==FR_OK);}
    else {
        uint8_t bytes[5120];unsigned count=!strcmp(name,"header.bin")?4096u:!strcmp(name,"patches.bin")?5120u:!strcmp(name,"targets.cache")?32u:512u;
        FSIZE_t at=!strcmp(name,"journal.bin")?size-count:!strncmp(name,"track",5)?8u*KUI_RAW_BYTES:0;
        assert(f_lseek(&file,at)==FR_OK && f_read(&file,bytes,count,&n)==FR_OK && n==count);
        if(!strcmp(name,"header.bin"))bytes[20]^=1;
        /* Keep record ordering/slot/counts and the outer record CRC valid;
         * only the receipt-bound index digest can detect this substitution. */
        else if(!strcmp(name,"targets.cache"))bytes[16]^=1;
        else if(!strcmp(name,"journal.bin"))bytes[100]^=1;
        else if(!strcmp(name,"patches.bin"))bytes[2500]^=1;
        else bytes[17]^=1;
        if(coherent)put(bytes+count-4,kui_crc32(0,bytes,count-4));
        assert(f_lseek(&file,at)==FR_OK && f_write(&file,bytes,count,&n)==FR_OK && n==count);
    }
    assert(f_sync(&file)==FR_OK && f_close(&file)==FR_OK);
    /* Preserve the timestamp to exercise byte/length fences rather than
     * relying on FAT's coarse two-second modification time. */
    assert(f_utime(path,&stamp)==FR_OK);
    assert(f_mount(NULL,"0:",0)==FR_OK);
}
static void verify_tracks(const char *job,bool resolved) {
    mount_card();size_t capacity=(size_t)(plan.tracks[2].end-plan.tracks[2].start)*KUI_RAW_BYTES;
    uint8_t *bytes=malloc(capacity),*expected=malloc(capacity);assert(bytes && expected);char path[128];
    for(unsigned i=0;i<3;i++) {
        unsigned count=(plan.tracks[i].end-plan.tracks[i].start)*KUI_RAW_BYTES;UINT got=0;FIL file;
        memcpy(expected,original[i],count);
        if(!resolved && i==2)for(unsigned n=8+saved_repairs;n<8+targets;n++)memset(expected+n*KUI_RAW_BYTES,0,KUI_RAW_BYTES);
        snprintf(path,sizeof(path),"0:%s/track%02u.%s",job,i+1,i==1?"raw":"bin");
        assert(f_open(&file,path,FA_READ)==FR_OK && f_size(&file)==count && f_read(&file,bytes,count,&got)==FR_OK && got==count && f_close(&file)==FR_OK);
        assert(!memcmp(bytes,expected,count));
    }
    free(bytes);free(expected);assert(f_mount(NULL,"0:",0)==FR_OK);
}
static void change_volume_identity(void) {
    uint8_t boot[24*512];assert(f_mount(NULL,"0:",0)==FR_OK && !fseeko(t.image,0,SEEK_SET));
    assert(fread(boot,1,sizeof(boot),t.image)==sizeof(boot));
    if(!memcmp(boot+3,"EXFAT   ",8)) {
        for(unsigned region=0;region<2;region++) {
            uint8_t *b=boot+region*12*512;b[100]^=1;uint32_t sum=0;
            for(unsigned i=0;i<11*512;i++)if(i!=106 && i!=107 && i!=112)
                sum=((sum>>1)|(sum<<31))+b[i];
            for(unsigned i=11*512;i<12*512;i+=4)put(b+i,sum);
        }
    } else {
        boot[67]^=1;unsigned backup=(unsigned)boot[50]|(unsigned)boot[51]<<8;
        assert(backup && backup<24);boot[backup*512+67]^=1;
    }
    assert(!fseeko(t.image,0,SEEK_SET) && fwrite(boot,1,sizeof(boot),t.image)==sizeof(boot) && !fflush(t.image));
}
int main(int argc,char **argv) {
    setvbuf(stdout,NULL,_IONBF,0);
    assert(argc==4);t.mode=argv[3];struct stat st;assert(!stat(argv[1],&st) && st.st_size%512==0);
    if(mode("warm-large")) {
        targets=2049;saved_repairs=33;plan.tracks[2].end=plan.tracks[2].toc_end=45150+targets+16;
        plan.bytes=(uint64_t)(20u+targets+16u)*KUI_RAW_BYTES;
    }
    t.blocks=(uint64_t)st.st_size/512;t.image=fopen(argv[1],"r+b");assert(t.image);
    for(unsigned i=0;i<3;i++) {
        char path[1024];snprintf(path,sizeof(path),"%s/track%02u.%s",argv[2],i+1,i==1?"raw":"bin");
        FILE *f=fopen(path,"rb");assert(f);size_t bytes=(size_t)(plan.tracks[i].end-plan.tracks[i].start)*KUI_RAW_BYTES;
        original[i]=malloc(bytes);assert(original[i] && fread(original[i],1,bytes,f)==bytes && !fclose(f));
    }
    kui_media_set(&media);
    struct kui_salvage_options options={.passes=1,.progress=progress,.session_epoch=17,.session_card=card};
    if(mode("warm-reboot-resumed")) {
        options.job="/KUI/salvage/job-0001";t.stage=3;t.stop_reads=1;struct kui_salvage_status resumed;
        assert(run(&options,KUI_SALVAGE_RESUME,&resumed)==KUI_SALVAGE_STOPPED && !resumed.session_reused &&
            t.journal_phases && t.target_phases && resumed.recovered==1 && resumed.remaining==7);
        verify_tracks(options.job,false);assert(!fclose(t.image));for(unsigned i=0;i<3;i++)free(original[i]);
        puts("PASS salvage session warm-reboot");return 0;
    }
    kui_salvage_session_invalidate();
    struct kui_salvage_status out;t.stage=1;t.repair=false;
    if(mode("warm-incomplete"))t.incomplete=true;
    enum kui_salvage_result result=run(&options,KUI_SALVAGE_NEW,&out);
    char job[KUI_DEST_JOB_CAP];snprintf(job,sizeof(job),"%s",out.job);options.job=job;
    if(t.incomplete) {
        assert(result==KUI_SALVAGE_STOPPED && !out.first_pass_complete && !out.session_reused && !out.session_retained);
        t.incomplete=false;t.stop_reads=1;t.stage=3;
        result=run(&options,KUI_SALVAGE_RESUME,&out);
        assert(result==KUI_SALVAGE_STOPPED && !out.session_reused && t.journal_phases);goto done;
    }
    assert(result==KUI_SALVAGE_UNRESOLVED && out.targets==targets && !out.recovered && !out.session_reused);
    t.stage=2;t.repair=true;t.stop_repairs=saved_repairs;
    t.inject=mode("warm-close-fail") || mode("warm-sync-fail") || mode("warm-unmount-fail") || mode("warm-pending-candidate");
    result=run(&options,KUI_SALVAGE_RESUME,&out);
    assert(t.journal_phases && t.target_phases && out.targets==targets &&
        out.recovered==(mode("warm-pending-candidate")?0u:saved_repairs) && !out.session_reused);
    if(mode("warm-pending-candidate")) {
        assert(result==KUI_SALVAGE_STOPPED && t.injected && !out.session_retained);
        mount_card();char path[512];snprintf(path,sizeof(path),"0:%s/journal.bin",job);FIL file;uint8_t event[512];UINT got=0;
        assert(f_open(&file,path,FA_READ)==FR_OK && f_size(&file)>=512 && f_lseek(&file,f_size(&file)-512)==FR_OK &&
            f_read(&file,event,sizeof(event),&got)==FR_OK && got==sizeof(event) && word(event+16)==7 && f_close(&file)==FR_OK);
        assert(f_mount(NULL,"0:",0)==FR_OK);
    } else if(t.inject)assert(result==KUI_SALVAGE_FAILED && t.injected && !out.session_retained);
    else assert(result==KUI_SALVAGE_STOPPED && out.session_retained);
    if(mode("warm-reboot")) {
        assert(f_mount(NULL,"0:",0)==FR_OK && !fclose(t.image));for(unsigned i=0;i<3;i++)free(original[i]);
        execl(argv[0],argv[0],argv[1],argv[2],"warm-reboot-resumed",(char *)NULL);assert(!"exec fresh process failed");
    }
    t.inject=false;t.stop_repairs=0;t.repair=false;t.stop_reads=1;t.stage=3;
    if(mode("warm-one-pass"))t.stop_reads=0;
    if(mode("warm-epoch"))++options.session_epoch;
    else if(mode("warm-card"))card[0]^=1;
    else if(mode("warm-volume"))change_volume_identity();
    else if(mode("warm-wrong-disc")) {
        char path[1024];snprintf(path,sizeof(path),"%s/alternate-anchor.raw",argv[2]);FILE *f=fopen(path,"rb");assert(f);
        assert(fread(original[2],1,KUI_RAW_BYTES,f)==KUI_RAW_BYTES && !fclose(f));
    }
    else if(mode("warm-invalidate"))kui_salvage_session_invalidate();
    else if(mode("warm-disabled"))options.session_epoch=0;
    else if(mode("warm-null-card"))options.session_card=NULL;
    else if(mode("warm-guard")) {mount_card();assert(kui_recovery_guard_mark(job,log_line));assert(f_mount(NULL,"0:",0)==FR_OK);}
    else if(mode("warm-header"))mutate(job,"header.bin",false,false,true);
    else if(mode("warm-index"))mutate(job,"targets.cache",false,false,true);
    else if(mode("warm-index-length"))mutate(job,"targets.cache",true,false,false);
    else if(mode("warm-journal-tail"))mutate(job,"journal.bin",false,false,true);
    else if(mode("warm-journal-length"))mutate(job,"journal.bin",true,false,false);
    else if(mode("warm-track-length"))mutate(job,"track03.bin",false,true,false);
    else if(mode("warm-patch-length"))mutate(job,"patches.bin",true,false,false);
    else if(mode("warm-track-data")) {mutate(job,"track03.bin",false,false,false);++options.session_epoch;}
    else if(mode("warm-patch-data")) {mutate(job,"patches.bin",false,false,false);++options.session_epoch;}
    else if(mode("warm-index-read-fail") || mode("warm-index-short-read") || mode("warm-cancel-preflight"))t.inject=true;
    else if(mode("warm-new-job")) {
        struct kui_salvage_options fresh=options;fresh.job=NULL;t.stage=4;t.stop_reads=0;
        struct kui_salvage_status other;assert(run(&fresh,KUI_SALVAGE_NEW,&other)==KUI_SALVAGE_UNRESOLVED && strcmp(other.job,job));
        t.stage=3;t.stop_reads=1;
    }
    unsigned writes_before=t.writes;
    result=run(&options,KUI_SALVAGE_RESUME,&out);
    bool failed=mode("warm-guard") || mode("warm-header") || mode("warm-wrong-disc") || mode("warm-journal-tail") || mode("warm-track-length") ||
        mode("warm-track-data") || mode("warm-patch-data") || mode("warm-index-read-fail") || mode("warm-index-short-read");
    bool reused=mode("warm-clean") || mode("warm-one-pass") || mode("warm-large");
    assert(!out.session_reused || reused);
    if(failed)assert(result==KUI_SALVAGE_FAILED);
    else if(mode("warm-cancel-preflight"))assert(result==KUI_SALVAGE_STOPPED && !out.session_retained);
    else if(mode("warm-one-pass"))assert(result==KUI_SALVAGE_UNRESOLVED && out.session_retained && out.recovered==saved_repairs && out.remaining==targets-saved_repairs && t.work_reads==targets-saved_repairs);
    else assert(result==KUI_SALVAGE_STOPPED && out.recovered==saved_repairs && out.remaining==targets-saved_repairs);
    if(reused)assert(out.session_reused && !t.journal_phases && !t.target_phases && t.work_reads==(mode("warm-one-pass")?targets-saved_repairs:1u));
    else if(!failed && !mode("warm-cancel-preflight"))assert(!out.session_reused && t.journal_phases && t.target_phases);
    if(t.inject)assert(t.injected);
    if(mode("warm-index-read-fail") || mode("warm-index-short-read") || mode("warm-wrong-disc") || mode("warm-guard"))
        assert(!t.journal_phases && !t.target_phases && !t.work_reads && t.writes==writes_before);
    if(mode("warm-pending-candidate"))verify_tracks(job,false);
    if(reused) {
        verify_tracks(job,false);t.repair=true;t.stop_reads=0;t.stage=4;
        assert(run(&options,KUI_SALVAGE_RESUME,&out)==KUI_SALVAGE_RESOLVED && out.session_reused &&
            !t.journal_phases && !t.target_phases && out.recovered==targets && !out.remaining);
        verify_tracks(job,true);
    }
    if(mode("warm-index-read-fail") || mode("warm-index-short-read") || mode("warm-cancel-preflight")) {
        t.inject=false;t.stage=4;
        assert(run(&options,KUI_SALVAGE_RESUME,&out)==KUI_SALVAGE_STOPPED && !out.session_reused && t.journal_phases && t.target_phases);
    }
done:
    assert(!t.files_open && t.max_files<=8 && f_mount(NULL,"0:",0)==FR_OK && !fclose(t.image));
    for(unsigned i=0;i<3;i++)free(original[i]);
    printf("PASS salvage session %s\n",t.mode);return 0;
}
