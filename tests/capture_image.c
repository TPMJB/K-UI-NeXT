/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "kui/capture.h"
#include "kui/media.h"
#include "kui/recovery_guard.h"
#include "kui/repair_copy.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static struct {
    FILE *image,*disc;uint64_t blocks,ticks,profile_ticks;unsigned writes,reads,failures,bad_attempts;
    uint32_t first_fad,bad_fad;
    const char *fault;bool stop,injected;enum kui_capture_phase phase;
    /* Split DMA. `pend` is a read in flight; the harness asserts it is always ended. */
    bool pend;uint32_t pend_fad;unsigned pend_n;uint8_t *pend_out;
    unsigned begins,ends,end_fails;
    uint8_t *live_buffer;size_t live_bytes;
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
    if(fault("cue-write-fail") && contains(data,count*512,"REM CAPTURE_PROFILE " KUI_CAPTURE_PROFILE)) {
        test.injected=true;return -1;
    }
    if(fault("write-fail") && test.phase==KUI_CAPTURING && test.ticks>24) return -1;
    if((fault("buffer-mutate") || fault("buffer-mutate-writefail")) && test.phase==KUI_CAPTURING && !test.injected &&
       test.ticks>24 && test.live_buffer && test.live_bytes>33) {
        /* Mutate the actual optical source during FatFs's write call, rather
         * than only corrupting the card copy. A post-write-only stream hash
         * can otherwise bless these changed bytes in a no-readback job. */
        test.injected=true;test.live_buffer[33]^=0x80;
        if(fault("buffer-mutate-writefail")) {
            assert(count && fseeko(test.image,(off_t)sector*512,SEEK_SET)==0);
            assert(fwrite(data,1,257,test.image)==257);return -1;
        }
    }
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
    if(fault("stop-identify") && status->phase==KUI_IDENTIFY && status->done) test.stop=true;
    if(fault("stop-prefix") && status->phase==KUI_PREFIX_CHECK && status->done>=KUI_RAW_BYTES) test.stop=true;
    if(status->phase==KUI_CAPTURING) {
        if((fault("stop-small") && status->done>=16*2352) || (fault("stop-early") && status->done>=128*2352) ||
           (fault("stop-middle") && status->done>=5000*2352) ||
           (fault("stop-late") && status->done==status->total)) test.stop=true;
        if(fault("full-during") && !test.injected) {test.injected=true;exhaust_space();}
    }
    if(fault("stop-verify") && status->phase==KUI_VERIFYING && status->done>=1024*1024) test.stop=true;
    if(fault("stop-export") && status->phase==KUI_EXPORTING && status->done>=status->total/4u) test.stop=true;
    if(fault("stop-export-verify") && status->phase==KUI_VERIFYING && !status->track && status->done>=status->total/4u)
        test.stop=true;
}
static void put32(uint8_t *p,uint32_t n) {for(unsigned i=0;i<4;i++) p[i]=(uint8_t)(n>>(i*8));}
static void sector(uint32_t fad,bool data,uint8_t *out) {
    if(test.disc) {
        assert(fad>=test.first_fad && fseeko(test.disc,(off_t)(fad-test.first_fad)*KUI_RAW_BYTES,SEEK_SET)==0);
        assert(fread(out,1,KUI_RAW_BYTES,test.disc)==KUI_RAW_BYTES);
        return;
    }
    kui_pattern(out,(uint64_t)fad*KUI_RAW_BYTES,KUI_RAW_BYTES);
    if(fault("wrong-disc")) out[123]^=0x42;
    const char *variant=getenv("KUI_TEST_DISC_VARIANT");
    if(variant) out[124]^=(uint8_t)strtoul(variant,NULL,10);
    if(data) {
        const char *setting=getenv("KUI_TEST_DATA_MODE");
        unsigned mode=(setting && !strcmp(setting,"2")) || (fault("mixed-mode") && fad==45250u)?2u:1u;
        unsigned offset=mode==2u?24u:16u;
        out[0]=out[11]=0;memset(out+1,255,10);out[15]=(uint8_t)mode;
        const unsigned address[3]={fad/4500u,(fad/75u)%60u,fad%75u};
        for(unsigned i=0;i<3;i++)out[12+i]=(uint8_t)((address[i]/10u)*16u+address[i]%10u);
        if(mode==2u) memset(out+16,0,8);
        if(fad==45150) {
            memcpy(out+offset,"SEGA SEGAKATANA",14);memset(out+offset+128,' ',128);
            const char *title=getenv("KUI_TEST_TITLE");
            if(title) {assert(strlen(title)<=128);memcpy(out+offset+128,title,strlen(title));}
            else memcpy(out+offset+128,"KUI SYNTHETIC SIX TRACK DISC",28);
        }
        if(mode==2u) put32(out+2072,kui_cd_edc(out+16,2056));
        else put32(out+2064,kui_cd_edc(out,2064));
    }
}
static enum kui_read_result read_disc(void *p,uint32_t fad,unsigned count,uint8_t *out) {
    (void)p;++test.ticks;test.profile_ticks+=17000;
    assert(count && count<=KUI_CAPTURE_CHUNK);
    const struct kui_capture_track *track=NULL;
    for(unsigned i=0;i<test.plan.count;i++)
        if(fad>=test.plan.tracks[i].start && fad<test.plan.tracks[i].end) track=&test.plan.tracks[i];
    assert(track && count<=track->end-fad);
    bool hits=fad<=test.bad_fad && fad+count>test.bad_fad;
    if(hits) {
        if(fad==test.bad_fad) ++test.bad_attempts;
        if(fault("disc-fail")) return KUI_READ_RETRY;
        if(fault("disc-change")) {++test.failures;return KUI_READ_FATAL;}
        if(fault("transient") && test.failures++<2) return KUI_READ_RETRY;
    }
    for(unsigned i=0;i<count;i++) {
        sector(fad+i,track->control==4,out+i*KUI_RAW_BYTES);
        if(fault("wrong-address") && fad+i==test.bad_fad && track->control==4)
            sector(fad+i+1,true,out+i*KUI_RAW_BYTES);
        if(fault("edc-fail") && fad+i==test.bad_fad) out[i*KUI_RAW_BYTES+40]^=1;
    }
    test.live_buffer=out;test.live_bytes=count*KUI_RAW_BYTES;
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
static void job_alias(void) {
    char path[512];FILINFO info;assert(first_job(path));
    assert(f_stat(path,&info)==FR_OK && (info.fattrib&AM_DIR) && info.altname[0]);
    char *slash=strrchr(path,'/');assert(slash);*slash=0;
    printf("JOB_ALIAS %s/%s\n",path,info.altname);
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
    else if(!strcmp(kind,"cue")) {
        DIR entries;FILINFO info;bool found=false;assert(f_opendir(&entries,dir)==FR_OK);
        while(f_readdir(&entries,&info)==FR_OK && info.fname[0]) {
            size_t n=strlen(info.fname);
            if(n>4u && !strcmp(info.fname+n-4u,".cue")) {
                assert(n<128u);snprintf(path,sizeof(path),"%s/%.127s",dir,info.fname);found=true;break;
            }
        }
        assert(f_closedir(&entries)==FR_OK && found);flip_byte(path,12);
    }
    else if(!strcmp(kind,"gdi-internal")) {snprintf(path,sizeof(path),"%s/%s",dir,KUI_CAPTURE_INTERNAL_GDI);flip_byte(path,12);}
    else if(!strcmp(kind,"gdi-cooked")) {snprintf(path,sizeof(path),"%s/disc.gdi",dir);flip_byte(path,12);}
    else if(!strcmp(kind,"repair-cd-target")) {snprintf(path,sizeof(path),"%s/track01.bin",dir);flip_byte(path,2u*KUI_RAW_BYTES+37);}
    else if(!strcmp(kind,"repair-target")) {snprintf(path,sizeof(path),"%s/track03.bin",dir);flip_byte(path,2u*KUI_RAW_BYTES+37);}
    else if(!strcmp(kind,"repair-receipt")) {snprintf(path,sizeof(path),"%s/.kui-inplace-complete",dir);flip_byte(path,16);}
    else if(!strcmp(kind,"repair-manifest")) {snprintf(path,sizeof(path),"%s/manifest.json",dir);flip_byte(path,22);}
    else if(!strcmp(kind,"repair-receipt-path")) {
        snprintf(path,sizeof(path),"%s/.kui-inplace-complete",dir);FIL f;UINT n;uint8_t receipt[512];
        assert(f_open(&f,path,FA_READ|FA_WRITE)==FR_OK && f_read(&f,receipt,sizeof(receipt),&n)==FR_OK && n==sizeof(receipt));
        receipt[80]^=1;put32(receipt+508,kui_crc32(0,receipt,508));
        assert(f_lseek(&f,0)==FR_OK && f_write(&f,receipt,sizeof(receipt),&n)==FR_OK && n==sizeof(receipt) && f_close(&f)==FR_OK);
    }
    else if(!strcmp(kind,"repair-partial-checkpoint")) {
        FIL f;UINT n;uint8_t record[KUI_CHECKPOINT_BYTES],raw[KUI_RAW_BYTES];struct kui_checkpoint checkpoint;
        snprintf(path,sizeof(path),"%s/checkpoint-a.bin",dir);assert(f_open(&f,path,FA_READ)==FR_OK && f_read(&f,record,sizeof(record),&n)==FR_OK && n==sizeof(record) && f_close(&f)==FR_OK);
        assert(kui_checkpoint_decode(record,&test.plan,record+24,&checkpoint));
        snprintf(path,sizeof(path),"%s/track03.bin",dir);assert(f_open(&f,path,FA_READ)==FR_OK && f_read(&f,raw,sizeof(raw),&n)==FR_OK && n==sizeof(raw) && f_close(&f)==FR_OK);
        checkpoint.track[2].sectors=1;checkpoint.track[2].crc32=kui_crc32(0,raw,sizeof(raw));struct kui_sha256 hash;kui_sha256_init(&hash);kui_sha256_update(&hash,raw,sizeof(raw));kui_sha256_digest(&hash,checkpoint.track[2].sha256);
        kui_checkpoint_encode(&checkpoint,record);
        for(unsigned i=0;i<2;i++) {snprintf(path,sizeof(path),"%s/checkpoint-%c.bin",dir,i?'b':'a');assert(f_open(&f,path,FA_WRITE)==FR_OK && f_write(&f,record,sizeof(record),&n)==FR_OK && n==sizeof(record) && f_close(&f)==FR_OK);}
    }
    else if(!strcmp(kind,"repair-checkpoints")) {
        for(unsigned i=0;i<2;i++) {snprintf(path,sizeof(path),"%s/checkpoint-%c.bin",dir,i?'b':'a');flip_byte(path,130);}
    }
    else if(!strcmp(kind,"repair-receipt-delete")) {snprintf(path,sizeof(path),"%s/.kui-inplace-complete",dir);assert(f_unlink(path)==FR_OK);}
    else if(!strcmp(kind,"cooked-track")) {snprintf(path,sizeof(path),"%s/track03.iso",dir);flip_byte(path,12);}
    else if(!strcmp(kind,"output")) {
        DIR entries;FILINFO info;bool found=false;assert(f_opendir(&entries,dir)==FR_OK);
        while(f_readdir(&entries,&info)==FR_OK && info.fname[0]) {
            size_t n=strlen(info.fname);
            if(n>4u && (!strcmp(info.fname+n-4u,".cso") || !strcmp(info.fname+n-4u,".zso") || !strcmp(info.fname+n-4u,".chd"))) {
                assert(n<128u);snprintf(path,sizeof(path),"%s/%.127s",dir,info.fname);found=true;break;
            }
        }
        assert(f_closedir(&entries)==FR_OK && found);flip_byte(path,12);
    }
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
    memset(&output,0,sizeof(output));
    if(parent) {
        output.parent=parent;
        const char *names=getenv("KUI_TEST_GAME_NAMES");output.game_names=names && !strcmp(names,"1");
        output.folder_label=getenv("KUI_TEST_FOLDER_LABEL");
        o.output=&output;
    }
    o.job_directory=getenv("KUI_TEST_SELECTED_JOB");
    if(!env) return o;
    if(strstr(env,"cue")) {output.format=KUI_CAPTURE_FORMAT_BIN_CUE;o.output=&output;}
    else if(strstr(env,"cso")) {output.format=KUI_CAPTURE_FORMAT_CSO;o.output=&output;}
    else if(strstr(env,"zso")) {output.format=KUI_CAPTURE_FORMAT_ZSO;o.output=&output;}
    else if(strstr(env,"chd")) {output.format=KUI_CAPTURE_FORMAT_CHD;o.output=&output;}
    else if(strstr(env,"direct2048")) {output.format=KUI_CAPTURE_FORMAT_GDI_DIRECT_2048;o.output=&output;}
    else if(strstr(env,"cooked")) {output.format=KUI_CAPTURE_FORMAT_GDI_2048;o.output=&output;}
    o.crc_only=strstr(env,"crc32")!=NULL;o.skip_end_readback=strstr(env,"noend")!=NULL;
    o.resume_size_only=strstr(env,"size")!=NULL;
    const char *sample=strstr(env,"sample=");if(sample) o.sample_every=(unsigned)atoi(sample+7);
    o.read_dma=strstr(env,"dma")!=NULL;
    o.strict_sectors=strstr(env,"strict")!=NULL;
    const char *retry=strstr(env,"retry=");if(retry) o.retry_limit=(unsigned)strtoul(retry+6,NULL,10);
    return o;
}
static void print_stats(const struct kui_capture_stats *st) {
    printf("STATS verified=%d crc_only=%d sampled=%u bytes=%llu setup_us=%llu resume_us=%llu capture_us=%llu verify_us=%llu job=%s\n",
        st->verified,st->crc_only,(unsigned)st->sampled,(unsigned long long)st->bytes,
        (unsigned long long)st->phase_us[KUI_TIME_SETUP],(unsigned long long)st->phase_us[KUI_TIME_RESUME],
        (unsigned long long)st->phase_us[KUI_TIME_CAPTURE],(unsigned long long)st->phase_us[KUI_TIME_VERIFY],st->job_dir);
    printf("DISC_TITLE %s\nGDI_NAME %s\n",st->disc_title,st->gdi_name);
    printf("OUTPUT_NAME %s\nOUTPUT_FORMAT %u\n",st->output_name,(unsigned)st->format);
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
    const char *layout=getenv("KUI_TEST_LAYOUT");
    if(layout && !strcmp(layout,"single-data")) {
        sessions[1].count=1u;sessions[1].tracks[0].end=51813u;
    }
    if(layout && !strcmp(layout,"repair-small")) {
        sessions[0]=(struct kui_toc){.tracks={{1,4,150,340},{2,0,340,344}},.count=2};
        sessions[1]=(struct kui_toc){.tracks={{3,4,45150,45190}},.count=1};
    }
    if(layout && !strncmp(layout,"cd-",3u)) {
        struct kui_toc cd={.tracks={{1,4,150,790}},.count=1};
        if(!strcmp(layout,"cd-audio")) cd=(struct kui_toc){.tracks={{1,0,150,470},{2,0,470,790}},.count=2};
        else if(!strcmp(layout,"cd-mixed")) cd=(struct kui_toc){.tracks={{1,4,150,390},{2,0,390,550},{3,4,550,790}},.count=3};
        else if(!strcmp(layout,"cd-offset")) cd=(struct kui_toc){.tracks={{1,4,225,865}},.count=1};
        else assert(!strcmp(layout,"cd-data"));
        assert(kui_plan_cd_tracks(&cd,&test.plan));
    } else assert(kui_plan_tracks(sessions,&test.plan));
    test.first_fad=test.plan.tracks[0].start;
    test.bad_fad=test.plan.profile==KUI_CAPTURE_PROFILE_CD_TOC?test.first_fad+100u:45250u;
    const char *disc=getenv("KUI_TEST_DISC_FILE");if(disc) {test.disc=fopen(disc,"rb");assert(test.disc);}
    int result=0;
    if(!strcmp(argv[2],"bench")) {   /* bench new|resume <sectors>: the benchmark entry point, real engine */
        assert(argc==5);
        struct kui_capture_options opts=test_options();struct kui_capture_stats stats;
        struct kui_capture_ops ops={NULL,read_disc,cancelled,now,progress,log_line,"0123456789ab",now_us,NULL,read_begin_fake,read_end_fake,&opts,&stats,NULL};
        enum kui_capture_result r=kui_capture_bench(&ops,45150,(unsigned)atoi(argv[4]),false,
            !strcmp(argv[3],"resume")?KUI_CAPTURE_RESUME:KUI_CAPTURE_NEW);
        printf("RESULT %u\n",r);print_stats(&stats);
        assert(fclose(test.image)==0);return r==KUI_CAPTURE_COMPLETE?0:1;
    }
    if(!strcmp(argv[2],"export") || !strcmp(argv[2],"mutate") || !strcmp(argv[2],"seed") || !strcmp(argv[2],"put") || !strcmp(argv[2],"mkdir") || !strcmp(argv[2],"alias") || !strcmp(argv[2],"quarantine")) {
        FATFS fs;assert(kui_mount(&fs,log_line));
        if(!strcmp(argv[2],"put")) {assert(argc==5);put_file(argv[3],argv[4]);}
        else if(!strcmp(argv[2],"export")) {assert(argc==4);export_all(argv[3]);}
        else if(!strcmp(argv[2],"mutate")) {assert(argc==4);mutate(argv[3]);}
        else if(!strcmp(argv[2],"mkdir")) {assert(argc==4);assert(f_mkdir(argv[3])==FR_OK);}
        else if(!strcmp(argv[2],"alias")) {assert(argc==3);job_alias();}
        else if(!strcmp(argv[2],"quarantine")) {
            char directory[512];
            if(argc==4) {assert(strlen(argv[3])<sizeof(directory));strcpy(directory,argv[3]);}
            else {assert(argc==3);assert(first_job(directory));}
            assert(kui_recovery_guard_mark(directory,log_line));
            assert(!kui_recovery_guard_check(directory,log_line));
            puts("QUARANTINE_PUBLISHED 1");
        }
        else assert(kui_write_new_file("0:/keep.txt","KEEP THIS FILE\n",15,log_line));
        assert(f_mount(NULL,"0:",0)==FR_OK);
#ifndef KUI_CAPTURE_NO_REPAIR_FIXTURE
    } else if(!strcmp(argv[2],"repair-inplace")) {
        FATFS fs;assert(kui_mount(&fs,log_line));char directory[512];assert(first_job(directory));assert(f_mount(NULL,"0:",0)==FR_OK);
        struct kui_capture_ops ops={.read=read_disc,.cancelled=cancelled,.now_ms=now,.log=log_line,.build="0123456789ab"};
        struct kui_repair_options options={.attempts=5,.continuous=true,.in_place=true};struct kui_repair_status status;
        enum kui_repair_result r=kui_repair_copy(directory,&test.plan,&ops,&options,&status);
        printf("REPAIR_RESULT %u WRITES %u MESSAGE %s\n",r,test.writes,status.message);
        result=r==KUI_REPAIR_COMPLETE?0:r==KUI_REPAIR_STOPPED?3:1;
#endif
    } else {
        enum kui_capture_mode mode=!strcmp(argv[2],"new")?KUI_CAPTURE_NEW:
            !strcmp(argv[2],"resume")?KUI_CAPTURE_RESUME:KUI_CAPTURE_VERIFY;
        struct kui_capture_options opts=test_options();struct kui_capture_stats stats;
        struct kui_capture_ops ops={NULL,read_disc,cancelled,now,progress,log_line,"0123456789ab",now_us,NULL,read_begin_fake,read_end_fake,&opts,&stats,NULL};
        enum kui_capture_result r=kui_capture(&test.plan,&ops,mode);
        printf("RESULT %u WRITES %u BAD_ATTEMPTS %u FATAL %u OPTICAL_READS %llu\n",r,test.writes,test.bad_attempts,test.failures,(unsigned long long)test.ticks);
        printf("FAULT_INJECTED %u\n",test.injected?1u:0u);
        /* Every begun read must have been ended: an unfinished one still owns its buffer. */
        printf("DMA BEGINS %u ENDS %u PENDING %u\n",test.begins,test.ends,test.pend?1u:0u);
        print_stats(&stats);
        result=r==KUI_CAPTURE_COMPLETE?0:r==KUI_CAPTURE_STOPPED?3:1;
    }
    if(test.disc) assert(fclose(test.disc)==0);
    assert(fclose(test.image)==0);return result;
}
