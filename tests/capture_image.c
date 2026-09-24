/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "kui/capture.h"
#include "kui/media.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static struct {
    FILE *image;uint64_t blocks,ticks,profile_ticks;unsigned writes,reads,failures,bad_attempts;
    const char *fault;bool stop,injected;enum kui_capture_phase phase;
    /* Split DMA. `pend` is a read in flight; the harness asserts it is always ended. */
    bool pend;uint32_t pend_fad;unsigned pend_n;uint8_t *pend_out;
    unsigned begins,ends,end_fails;
    struct kui_capture_plan plan;
} test;
static void log_line(const char *format,...) {
    va_list args;va_start(args,format);vprintf(format,args);va_end(args);puts("");
}
static bool fault(const char *s) {return test.fault && !strcmp(test.fault,s);}
static bool contains(const uint8_t *data,size_t bytes,const char *text) {
    size_t size=strlen(text);
    for(size_t i=0;i+size<=bytes;++i) if(!memcmp(data+i,text,size)) return true;
    return false;
}
static uint64_t blocks(void *p) {(void)p;return test.blocks;}
static int read_image(void *p,uint32_t sector,size_t count,uint8_t *data) {
    (void)p;++test.reads;test.profile_ticks+=2300;
    if(fault("read-fail") && test.phase==KUI_VERIFYING) return -1;
    return fseeko(test.image,(off_t)sector*512,SEEK_SET) || fread(data,512,count,test.image)!=count?-1:0;
}
static int write_image(void *p,uint32_t sector,size_t count,const uint8_t *data) {
    (void)p;++test.writes;test.profile_ticks+=3100;
    if(fault("manifest-write-fail") && contains(data,count*512,"\"profile\":\"" KUI_CAPTURE_PROFILE "\"")) {
        test.injected=true;return -1;
    }
    if(fault("write-fail") && test.phase==KUI_CAPTURING && test.ticks>24) return -1;
    if(fault("corrupt-write") && test.phase==KUI_CAPTURING && !test.injected &&
       count && data[0]==0 && kui_guard_is(data+1,10,255) && data[15]==1) {
        test.injected=true;size_t bytes=count*512;uint8_t *changed=malloc(bytes);assert(changed);
        memcpy(changed,data,bytes);changed[33]^=0x80;
        int result=fseeko(test.image,(off_t)sector*512,SEEK_SET) || fwrite(changed,512,count,test.image)!=count?-1:0;
        free(changed);return result;
    }
    return fseeko(test.image,(off_t)sector*512,SEEK_SET) || fwrite(data,512,count,test.image)!=count?-1:0;
}
static int sync_image(void *p) {
    (void)p;test.profile_ticks+=4700;
    if(fault("sync-fail") && test.phase==KUI_CAPTURING) return -1;
    return fflush(test.image) || fsync(fileno(test.image))?-1:0;
}
static bool cancelled(void *p) {(void)p;return test.stop || fault("cancel-before");}
static uint64_t now(void *p) {(void)p;return test.ticks*100;}
/* Deterministic observation clock, independent of fault/cancellation ticks. */
static uint64_t now_us(void *p) {(void)p;return test.profile_ticks+=13;}
static void exhaust_space(void) {
    FIL f;assert(f_open(&f,"0:/fill.bin",FA_CREATE_NEW|FA_WRITE)==FR_OK);
    static uint8_t zeros[65536];UINT done;
    do {assert(f_write(&f,zeros,sizeof(zeros),&done)==FR_OK);} while(done==sizeof(zeros));
    assert(f_close(&f)==FR_OK);
}
static void progress(void *p,const struct kui_capture_progress *status) {
    (void)p;test.phase=status->phase;
    if(status->phase==KUI_CAPTURING) {
        if((fault("stop-early") && status->done>=128*2352) ||
           (fault("stop-middle") && status->done>=5000*2352) ||
           (fault("stop-late") && status->done==status->total)) test.stop=true;
        if(fault("full-during") && !test.injected) {test.injected=true;exhaust_space();}
    }
    if(fault("stop-verify") && status->phase==KUI_VERIFYING && status->done>=1024*1024) test.stop=true;
}
static void put32(uint8_t *p,uint32_t n) {for(unsigned i=0;i<4;i++) p[i]=(uint8_t)(n>>(i*8));}
static void sector(uint32_t fad,bool data,uint8_t *out) {
    kui_pattern(out,(uint64_t)fad*KUI_RAW_BYTES,KUI_RAW_BYTES);
    if(fault("wrong-disc")) out[123]^=0x42;
    const char *variant=getenv("KUI_TEST_DISC_VARIANT");
    if(variant) out[124]^=(uint8_t)strtoul(variant,NULL,10);
    if(data) {
        out[0]=out[11]=0;memset(out+1,255,10);out[12]=0x10;out[13]=out[14]=0;out[15]=1;
        if(fad==45150) {
            memcpy(out+16,"SEGA SEGAKATANA",14);memset(out+16+128,' ',128);
            const char *title=getenv("KUI_TEST_TITLE");
            if(title) {assert(strlen(title)<=128);memcpy(out+16+128,title,strlen(title));}
            else memcpy(out+16+128,"KUI SYNTHETIC SIX TRACK DISC",28);
        }
        put32(out+2064,kui_cd_edc(out,2064));
    }
}
static enum kui_read_result read_disc(void *p,uint32_t fad,unsigned count,uint8_t *out) {
    (void)p;++test.ticks;test.profile_ticks+=17000;
    assert(count && count<=KUI_CAPTURE_CHUNK);
    const struct kui_capture_track *track=NULL;
    for(unsigned i=0;i<test.plan.count;i++)
        if(fad>=test.plan.tracks[i].start && fad<test.plan.tracks[i].end) track=&test.plan.tracks[i];
    assert(track && count<=track->end-fad);
    bool hits=fad<=45250 && fad+count>45250;
    if(hits) {
        if(fad==45250) ++test.bad_attempts;
        if(fault("disc-fail")) return KUI_READ_RETRY;
        if(fault("disc-change")) {++test.failures;return KUI_READ_FATAL;}
        if(fault("transient") && test.failures++<2) return KUI_READ_RETRY;
    }
    for(unsigned i=0;i<count;i++) {
        sector(fad+i,track->control==4,out+i*KUI_RAW_BYTES);
        if(fault("edc-fail") && fad+i==45250) out[i*KUI_RAW_BYTES+40]^=1;
    }
    return KUI_READ_OK;
}
static bool read_begin_fake(void *p,uint32_t fad,unsigned sectors,uint8_t *out) {
    (void)p;
    assert(!test.pend);                       /* one in flight at a time, ever */
    assert(out && sectors && !(sectors&1u));  /* DMA needs an even sector count */
    assert(((uintptr_t)out&31u)==0);          /* and a 32-byte aligned buffer */
    if(fault("dma-nobegin")) return false;
    ++test.begins;test.pend=true;test.pend_fad=fad;test.pend_n=sectors;test.pend_out=out;
    return true;
}
static enum kui_read_result read_end_fake(void *p) {
    assert(test.pend);
    ++test.ends;test.pend=false;
    if(fault("dma-endfail")&&test.end_fails++<2) return KUI_READ_RETRY;
    if(fault("dma-endfatal")) return KUI_READ_FATAL;
    /* Same bytes the synchronous path would produce, faults included. */
    return read_disc(p,test.pend_fad,test.pend_n,test.pend_out);
}
static bool first_job(char path[512]) {
    const char *selected=getenv("KUI_TEST_JOB");
    if(selected) {assert(strlen(selected)<512);strcpy(path,selected);return true;}
    DIR d;FILINFO info;
    if(f_opendir(&d,"0:/KUI/dumps")!=FR_OK) return false;
    char name[32]={0};FRESULT r;
    while((r=f_readdir(&d,&info))==FR_OK && info.fname[0])
        if((info.fattrib&AM_DIR) && strlen(info.fname)==22 && strcmp(info.fname,name)>0) strcpy(name,info.fname);
    bool ok=r==FR_OK && name[0];if(f_closedir(&d)!=FR_OK) ok=false;
    snprintf(path,512,"0:/KUI/dumps/%s",name);return ok;
}
static void export_file(const char *source,const char *dest) {
    FIL file;assert(f_open(&file,source,FA_READ)==FR_OK);FILE *output=fopen(dest,"wb");assert(output);
    uint8_t b[65536];UINT got;
    do {assert(f_read(&file,b,sizeof(b),&got)==FR_OK);assert(fwrite(b,1,got,output)==got);} while(got);
    assert(f_close(&file)==FR_OK);assert(fclose(output)==0);
}
static void export_tree(const char *source,const char *target) {
    DIR dir;FILINFO item;assert(f_opendir(&dir,source)==FR_OK);FRESULT r;
    while((r=f_readdir(&dir,&item))==FR_OK && item.fname[0]) {
        char from[1024],to[2048];
        int n=snprintf(from,sizeof(from),"%s/%s",source,item.fname);assert(n>0 && (size_t)n<sizeof(from));
        n=snprintf(to,sizeof(to),"%s/%s",target,item.fname);assert(n>0 && (size_t)n<sizeof(to));
        if(item.fattrib&AM_DIR) {assert(mkdir(to,0700)==0);export_tree(from,to);}
        else export_file(from,to);
    }
    assert(r==FR_OK && f_closedir(&dir)==FR_OK);
}
static void export_all(const char *target) {
    FIL keep;char retained[15];UINT got_keep;
    assert(f_open(&keep,"0:/keep.txt",FA_READ)==FR_OK && f_size(&keep)==sizeof(retained));
    assert(f_read(&keep,retained,sizeof(retained),&got_keep)==FR_OK && got_keep==sizeof(retained));
    assert(!memcmp(retained,"KEEP THIS FILE\n",sizeof(retained)) && f_close(&keep)==FR_OK);
    const char *parent=getenv("KUI_TEST_OUTPUT_ROOT");
    if(parent) {
        char source[512];int n=snprintf(source,sizeof(source),"0:%s",parent);
        assert(n>0 && (size_t)n<sizeof(source));export_tree(source,target);return;
    }
    DIR dirs;FILINFO info;assert(f_opendir(&dirs,"0:/KUI/dumps")==FR_OK);
    while(f_readdir(&dirs,&info)==FR_OK && info.fname[0]) if(info.fattrib&AM_DIR) {
        char from[128],to[1024];assert(strlen(info.fname)==22);
        snprintf(from,sizeof(from),"0:/KUI/dumps/%.22s",info.fname);
        snprintf(to,sizeof(to),"%s/%s",target,info.fname);assert(mkdir(to,0700)==0);
        DIR dir;FILINFO item;assert(f_opendir(&dir,from)==FR_OK);
        while(f_readdir(&dir,&item)==FR_OK && item.fname[0]) {
            assert(!(item.fattrib&AM_DIR));char source[1024],dest[2048];
            snprintf(source,sizeof(source),"%s/%s",from,item.fname);
            snprintf(dest,sizeof(dest),"%s/%s",to,item.fname);
            FIL file;assert(f_open(&file,source,FA_READ)==FR_OK);FILE *output=fopen(dest,"wb");assert(output);
            uint8_t b[65536];UINT got;
            do {assert(f_read(&file,b,sizeof(b),&got)==FR_OK);assert(fwrite(b,1,got,output)==got);} while(got);
            assert(f_close(&file)==FR_OK);assert(fclose(output)==0);
        }
        assert(f_closedir(&dir)==FR_OK);
    }
    assert(f_closedir(&dirs)==FR_OK);
}
/* Copy a host file (a small catalogue) onto the card, creating KUI/ if needed.
 * FatFs does the write, so it works on exFAT as well as FAT32. */
static void put_file(const char *host,const char *card) {
    static uint8_t data[65536];FILE *in=fopen(host,"rb");assert(in);
    size_t n=fread(data,1,sizeof(data),in);assert(!ferror(in) && n<sizeof(data));assert(fclose(in)==0);
    FRESULT r=f_mkdir("0:/KUI");assert(r==FR_OK || r==FR_EXIST);
    assert(kui_write_new_file(card,data,n,log_line));
}
static void flip_byte(const char *path,FSIZE_t pos) {
    FIL f;UINT done;uint8_t byte;
    assert(f_open(&f,path,FA_READ|FA_WRITE)==FR_OK && f_lseek(&f,pos)==FR_OK);
    assert(f_read(&f,&byte,1,&done)==FR_OK && done==1);byte^=0x80;
    assert(f_lseek(&f,pos)==FR_OK && f_write(&f,&byte,1,&done)==FR_OK && done==1);
    assert(f_close(&f)==FR_OK);
}
static void mutate(const char *kind) {
    char dir[512],path[1024];assert(first_job(dir));
    if(!strcmp(kind,"prefix")) {snprintf(path,sizeof(path),"%s/track01.bin",dir);flip_byte(path,123);}
    else if(!strcmp(kind,"manifest")) {snprintf(path,sizeof(path),"%s/manifest.json",dir);flip_byte(path,123);}
    else if(!strcmp(kind,"tail")) {
        snprintf(path,sizeof(path),"%s/track03.bin",dir);FIL f;UINT done;uint8_t junk[4704];memset(junk,0x35,sizeof(junk));
        assert(f_open(&f,path,FA_WRITE)==FR_OK && f_lseek(&f,f_size(&f))==FR_OK);
        assert(f_write(&f,junk,sizeof(junk),&done)==FR_OK && done==sizeof(junk));assert(f_close(&f)==FR_OK);
    } else if(!strcmp(kind,"short")) {
        snprintf(path,sizeof(path),"%s/track01.bin",dir);FIL f;
        assert(f_open(&f,path,FA_WRITE)==FR_OK && f_lseek(&f,2352)==FR_OK && f_truncate(&f)==FR_OK);
        assert(f_close(&f)==FR_OK);
    } else if(!strcmp(kind,"checkpoint") || !strcmp(kind,"both-checkpoints")) {
        uint64_t seq[2]={0};
        for(unsigned i=0;i<2;i++) {
            snprintf(path,sizeof(path),"%s/checkpoint-%c.bin",dir,i?'b':'a');FIL f;UINT done;uint8_t b[24];
            if(f_open(&f,path,FA_READ)!=FR_OK) continue;
            assert(f_read(&f,b,sizeof(b),&done)==FR_OK && done==sizeof(b));assert(f_close(&f)==FR_OK);
            for(unsigned k=0;k<8;k++) seq[i]|=(uint64_t)b[16+k]<<(k*8);
        }
        for(unsigned i=0;i<2;i++) if(seq[i] && (!strcmp(kind,"both-checkpoints") || seq[i]>=seq[1-i])) {
            snprintf(path,sizeof(path),"%s/checkpoint-%c.bin",dir,i?'b':'a');flip_byte(path,20);
        }
    } else assert(!"Unknown mutation");
}
/* Engine options from KUI_TEST_OPTS, e.g. "crc32,noend,size,sample=3". */
static struct kui_capture_options test_options(void) {
    struct kui_capture_options o={0};const char *env=getenv("KUI_TEST_OPTS");
    const char *parent=getenv("KUI_TEST_OUTPUT_ROOT");
    static struct kui_capture_output output;
    if(parent) {
        output.parent=parent;
        const char *names=getenv("KUI_TEST_GAME_NAMES");output.game_names=names && !strcmp(names,"1");
        o.output=&output;
    }
    if(!env) return o;
    o.crc_only=strstr(env,"crc32")!=NULL;o.skip_end_readback=strstr(env,"noend")!=NULL;
    o.resume_size_only=strstr(env,"size")!=NULL;
    const char *sample=strstr(env,"sample=");if(sample) o.sample_every=(unsigned)atoi(sample+7);
    o.read_dma=strstr(env,"dma")!=NULL;
    return o;
}
static void print_stats(const struct kui_capture_stats *st) {
    printf("STATS verified=%d crc_only=%d sampled=%u bytes=%llu setup_us=%llu resume_us=%llu capture_us=%llu verify_us=%llu job=%s\n",
        st->verified,st->crc_only,(unsigned)st->sampled,(unsigned long long)st->bytes,
        (unsigned long long)st->phase_us[KUI_TIME_SETUP],(unsigned long long)st->phase_us[KUI_TIME_RESUME],
        (unsigned long long)st->phase_us[KUI_TIME_CAPTURE],(unsigned long long)st->phase_us[KUI_TIME_VERIFY],st->job_dir);
    printf("DISC_TITLE %s\nGDI_NAME %s\n",st->disc_title,st->gdi_name);
    printf("REFERENCE checked=%u result=%d catalog=%s\nREFERENCE_NAME %s\n",
        st->reference_checked?1u:0u,st->reference.result,st->reference.catalog,st->reference.name);
}
int main(int argc,char **argv) {
    if(argc<3 || argc>5) return 2;
    struct stat st;if(lstat(argv[1],&st) || !S_ISREG(st.st_mode) || st.st_size%512) return 2;
    test.image=fopen(argv[1],"r+b");if(!test.image) return 2;test.blocks=(uint64_t)st.st_size/512;
    test.fault=argc>3?argv[3]:"none";
    struct kui_media_ops media={NULL,blocks,read_image,write_image,sync_image};kui_media_set(&media);
    struct kui_toc sessions[2]={ {.tracks={{1,4,150,650},{2,0,650,950}},.count=2},
        {.tracks={{3,4,45150,50000},{4,0,50000,50400},{5,0,50400,51000},{6,4,51000,51813}},.count=4} };
    assert(kui_plan_tracks(sessions,&test.plan));
    int result=0;
    if(!strcmp(argv[2],"bench")) {   /* bench new|resume <sectors>: the benchmark entry point, real engine */
        assert(argc==5);
        struct kui_capture_options opts=test_options();struct kui_capture_stats stats;
        struct kui_capture_ops ops={NULL,read_disc,cancelled,now,progress,log_line,"0123456789ab",now_us,NULL,read_begin_fake,read_end_fake,&opts,&stats};
        enum kui_capture_result r=kui_capture_bench(&ops,45150,(unsigned)atoi(argv[4]),false,
            !strcmp(argv[3],"resume")?KUI_CAPTURE_RESUME:KUI_CAPTURE_NEW);
        printf("RESULT %u\n",r);print_stats(&stats);
        assert(fclose(test.image)==0);return r==KUI_CAPTURE_COMPLETE?0:1;
    }
    if(!strcmp(argv[2],"export") || !strcmp(argv[2],"mutate") || !strcmp(argv[2],"seed") || !strcmp(argv[2],"put") || !strcmp(argv[2],"mkdir")) {
        FATFS fs;assert(kui_mount(&fs,log_line));
        if(!strcmp(argv[2],"put")) {assert(argc==5);put_file(argv[3],argv[4]);}
        else if(!strcmp(argv[2],"export")) {assert(argc==4);export_all(argv[3]);}
        else if(!strcmp(argv[2],"mutate")) {assert(argc==4);mutate(argv[3]);}
        else if(!strcmp(argv[2],"mkdir")) {assert(argc==4);assert(f_mkdir(argv[3])==FR_OK);}
        else assert(kui_write_new_file("0:/keep.txt","KEEP THIS FILE\n",15,log_line));
        assert(f_mount(NULL,"0:",0)==FR_OK);
    } else {
        enum kui_capture_mode mode=!strcmp(argv[2],"new")?KUI_CAPTURE_NEW:
            !strcmp(argv[2],"resume")?KUI_CAPTURE_RESUME:KUI_CAPTURE_VERIFY;
        struct kui_capture_options opts=test_options();struct kui_capture_stats stats;
        struct kui_capture_ops ops={NULL,read_disc,cancelled,now,progress,log_line,"0123456789ab",now_us,NULL,read_begin_fake,read_end_fake,&opts,&stats};
        enum kui_capture_result r=kui_capture(&test.plan,&ops,mode);
        printf("RESULT %u WRITES %u BAD_ATTEMPTS %u FATAL %u\n",r,test.writes,test.bad_attempts,test.failures);
        /* Every begun read must have been ended: an unfinished one still owns its buffer. */
        printf("DMA BEGINS %u ENDS %u PENDING %u\n",test.begins,test.ends,test.pend?1u:0u);
        print_stats(&stats);
        result=r==KUI_CAPTURE_COMPLETE?0:r==KUI_CAPTURE_STOPPED?3:1;
    }
    assert(fclose(test.image)==0);return result;
}
