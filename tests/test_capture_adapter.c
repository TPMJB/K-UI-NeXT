/* SPDX-License-Identifier: GPL-3.0-only */
/* Exercise the real console capture adapter with no optical or filesystem I/O.
 * A rejected destination/preference load must never reach the drive or engine;
 * outcome details must come from this operation, not an earlier successful one. */
#include "platform.h"
#include "kui/capture_network.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

struct kui_options kui_options;
static struct {
    bool refresh_ok, prepare_ok, plan_ok, connect_ok, stop;
    bool cancel_in_prepare, quick_resume;
    enum kui_capture_mode mode;
    enum kui_capture_result result;
    struct kui_capture_stats stats;
    unsigned engine_calls, simulated_writes;
    unsigned storage, dma_ends;
    bool exercise_dma, dma_pending;
    bool load_format;
    bool cd, mount_ok,config_ok,remote_ok,remote_close_ok;
    enum kui_capture_format loaded_format;
    char order[32], log[512], destination[KUI_DEST_ROOT_CAP];
    char selected_job[KUI_DEST_JOB_CAP],folder_label[KUI_DEST_TITLE_CAP];
    char *mutate_destination,*mutate_job,*mutate_label;
    size_t order_size, log_size;
} fake;

static void note(char event) {
    assert(fake.order_size+1<sizeof(fake.order));
    fake.order[fake.order_size++]=event;
    fake.order[fake.order_size]=0;
}
static void reset(void) {
    memset(&fake,0,sizeof(fake));
    memset(&kui_options,0,sizeof(kui_options));
    fake.refresh_ok=fake.prepare_ok=fake.plan_ok=fake.connect_ok=true;
    fake.mount_ok=fake.config_ok=fake.remote_ok=fake.remote_close_ok=true;
    fake.mode=KUI_CAPTURE_NEW;fake.result=KUI_CAPTURE_COMPLETE;
    /* Deliberately nonzero options so a missing/incorrect mapping is visible. */
    kui_options.capture_crc_only[0]=true;
    kui_options.end_readback[0]=false;
    kui_options.resume_size[0]=true;
    kui_options.sample_readback[0]=17;
    kui_options.capture_dma[0]=true;
    kui_options.capture_retry_limit=20u;kui_options.capture_strict_sectors=true;
    fake.stats.bytes=12345678;fake.stats.sampled=9;
    fake.stats.verified=true;fake.stats.crc_only=true;
    strcpy(fake.destination,"/Games");
    strcpy(fake.stats.job_dir,"0:/Games/MDK2 (7)");
    strcpy(fake.stats.disc_title,"MDK2");strcpy(fake.stats.gdi_name,"MDK2.gdi");
    fake.stats.reference_checked=true;fake.stats.reference.result=KUI_KNOWN_FULL_MATCH;
    strcpy(fake.stats.reference.catalog,"TOSEC");
    strcpy(fake.stats.reference.name,"MDK2 (USA)");
    for(unsigned i=0;i<KUI_TIME_PHASES;++i) fake.stats.phase_us[i]=1000+i;
    for(unsigned i=0;i<KUI_TIME_BUCKETS;++i) fake.stats.capture_bucket_us[i]=2000+i;
}

bool kui_options_refresh(void) {
    note('O');
    if(fake.mutate_destination)strcpy(fake.mutate_destination,"/Changed during refresh");
    if(fake.mutate_job)strcpy(fake.mutate_job,"/Changed during refresh/Job");
    if(fake.mutate_label)strcpy(fake.mutate_label,"Changed during refresh");
    if(fake.refresh_ok && fake.load_format) kui_options.capture_format=fake.loaded_format;
    return fake.refresh_ok;
}
bool kui_cancelled(void) {return fake.stop;}
uint64_t timer_ms_gettime64(void) {return 1234;}
uint64_t timer_us_gettime64(void) {return 1234567;}
void kui_log(const char *format,...) {
    va_list args;va_start(args,format);
    int n=vsnprintf(fake.log+fake.log_size,sizeof(fake.log)-fake.log_size,format,args);
    va_end(args);
    assert(n>=0 && (size_t)n<sizeof(fake.log)-fake.log_size);
    fake.log_size+=(size_t)n;
}
void kui_disc_timing_reset(void) {note('T');}
void kui_disc_timing_phase(void *ctx,bool capturing) {(void)ctx;(void)capturing;}
void kui_disc_timing_report(void) {note('R');}
bool kui_disc_prepare(struct kui_toc sessions[2]) {
    note('D');memset(sessions,0,2*sizeof(*sessions));
    sessions[0].count=2;sessions[1].count=1;
    if(fake.cancel_in_prepare) fake.stop=true;
    return fake.prepare_ok && !fake.cd;
}
bool kui_disc_prepare_capture(struct kui_toc sessions[2],bool *cd) {
    *cd=fake.cd;bool prepared=kui_disc_prepare(sessions);return fake.cd?fake.prepare_ok:prepared;
}
bool kui_plan_cd_tracks(const struct kui_toc *toc,struct kui_capture_plan *out) {
    (void)toc;note('P');memset(out,0,sizeof(*out));out->profile=KUI_CAPTURE_PROFILE_CD_TOC;
    out->count=3;out->bytes=987654321;return fake.plan_ok;
}
bool kui_mount(FATFS *fs,kui_log_fn log) {(void)fs;(void)log;note('M');return fake.mount_ok;}
FRESULT f_mount(FATFS *fs,const TCHAR *drive,BYTE now) {
    assert(!fs && !strcmp(drive,"0:") && !now);note('u');return FR_OK;
}
const struct kui_w5500_port *kui_w5500_console_port(void) {return NULL;}
const struct kui_wifi_port *kui_wifi_console_port(void) {return NULL;}
bool kui_capture_network_config_load(struct kui_network_rip_config *out,char problem[128]) {
    memset(out,0,sizeof(*out));if(!fake.config_ok)snprintf(problem,128,"invalid local configuration");
    return fake.config_ok;
}
struct kui_capture_network *kui_capture_network_start(const struct kui_net_ports *ports,
    const struct kui_network_rip_config *config,kui_log_fn log,kui_cancel_fn cancel,
    void (*stage)(const char *),char problem[128]) {
    (void)ports;(void)config;(void)log;(void)cancel;(void)stage;note('N');
    if(!fake.remote_ok)snprintf(problem,128,"receiver unavailable");
    return fake.remote_ok?(struct kui_capture_network *)&fake:NULL;
}
bool kui_capture_network_end(struct kui_capture_network *p) {assert(p==(struct kui_capture_network *)&fake);note('E');return fake.remote_close_ok;}
const char *kui_capture_network_problem(const struct kui_capture_network *p) {(void)p;return "";}
bool kui_plan_tracks(const struct kui_toc sessions[2],struct kui_capture_plan *out) {
    note('P');assert(sessions[0].count==2 && sessions[1].count==1);
    memset(out,0,sizeof(*out));out->count=3;out->bytes=987654321;
    return fake.plan_ok;
}
bool kui_sd_connect(void) {note('S');return fake.connect_ok;}
unsigned kui_storage_active(void) {return fake.storage;}
void kui_sd_disconnect(void) {note('U');}
enum kui_read_result kui_disc_read_raw(void *ctx,uint32_t fad,unsigned sectors,uint8_t *out) {
    (void)ctx;(void)fad;(void)sectors;(void)out;assert(!"unexpected disc read");return KUI_READ_FATAL;
}
bool kui_disc_read_begin(void *ctx,uint32_t fad,unsigned sectors,uint8_t *out) {
    (void)ctx;(void)fad;(void)sectors;(void)out;
    assert(fake.exercise_dma && !fake.dma_pending);fake.dma_pending=true;return true;
}
enum kui_read_result kui_disc_read_end(void *ctx) {
    (void)ctx;assert(fake.exercise_dma && fake.dma_pending);
    fake.dma_pending=false;++fake.dma_ends;return KUI_READ_OK;
}
void kui_capture_status(void *ctx,const struct kui_capture_progress *progress) {
    (void)ctx;(void)progress;
}
void kui_cpu_census_mark(struct kui_cpu_census *out) {note('C');memset(out,0,sizeof(*out));}
void kui_cpu_census_log(kui_log_fn log,const char *what,const char *detail,
    const struct kui_cpu_census *from,const struct kui_cpu_census *to) {
    note('L');assert(log==kui_log && from && to);
    assert(!strcmp(what,"operation") && !strcmp(detail,"(setup+capture+verify)"));
}
enum kui_capture_result kui_capture(const struct kui_capture_plan *plan,
    const struct kui_capture_ops *ops,enum kui_capture_mode mode) {
    note('E');++fake.engine_calls;
    assert(mode==fake.mode && plan->count==3 && plan->bytes==987654321);
    assert(ops->ctx==NULL && ops->read==kui_disc_read_raw);
    assert(ops->progress==kui_capture_status && ops->log==kui_log);
    assert(!strcmp(ops->build,"123456abcdef"));
    assert(!ops->cancelled(ops->ctx));
    assert(ops->now_ms(ops->ctx)==1234 && ops->now_us(ops->ctx)==1234567);
    assert(ops->read_phase==kui_disc_timing_phase && ops->read_begin && ops->read_end);
    assert(ops->options && ops->stats);
    assert(ops->options->crc_only==kui_options.capture_crc_only[0]);
    assert(ops->options->skip_end_readback==!kui_options.end_readback[0]);
    assert(ops->options->resume_size_only==(fake.quick_resume||kui_options.resume_size[0]));
    assert(ops->options->sample_every==kui_options.sample_readback[0]);
    assert(ops->options->read_dma==kui_options.capture_dma[0]);
    assert(ops->options->output && ops->options->output->game_names);
    assert(ops->options->output->format==(fake.cd?KUI_CAPTURE_FORMAT_BIN_CUE:kui_options.capture_format));
    assert(ops->options->retry_limit==kui_options.capture_retry_limit);
    assert(ops->options->strict_sectors==kui_options.capture_strict_sectors);
    assert(!strcmp(ops->options->output->parent,fake.destination));
    assert(ops->options->output->parent!=fake.mutate_destination);
    assert(fake.selected_job[0]?(ops->options->job_directory && !strcmp(ops->options->job_directory,fake.selected_job)):
        !ops->options->job_directory);
    assert(fake.folder_label[0]?(ops->options->output->folder_label && !strcmp(ops->options->output->folder_label,fake.folder_label)):
        !ops->options->output->folder_label);
    if(fake.mutate_job)assert(ops->options->job_directory!=fake.mutate_job);
    if(fake.mutate_label)assert(ops->options->output->folder_label!=fake.mutate_label);
    assert(!ops->options->bench);
    if(fake.exercise_dma) {
        uint8_t buffer[32];
        assert(ops->read_begin(NULL,45150,1,buffer));
        /* This is where the engine can write the previous chunk. IDE must
         * have finished optical DMA already; serial SD retains overlap. */
        assert(fake.dma_pending==(fake.storage!=KUI_STORAGE_IDE));
        assert(ops->read_end(NULL)==KUI_READ_OK);
        assert(!fake.dma_pending && fake.dma_ends==1);
    }
    *ops->stats=fake.stats;
    if(mode!=KUI_CAPTURE_VERIFY) ++fake.simulated_writes;
    return fake.result;
}
enum kui_capture_result kui_capture_bench(const struct kui_capture_ops *ops,
    uint32_t fad,unsigned sectors,bool audio,enum kui_capture_mode mode) {
    (void)ops;(void)fad;(void)sectors;(void)audio;(void)mode;
    assert(!"unexpected benchmark");return KUI_CAPTURE_FAILED;
}

static void expect_empty_stats(void) {
    const unsigned char *bytes=(const unsigned char *)kui_capture_last_stats();
    for(size_t i=0;i<sizeof(struct kui_capture_stats);++i) assert(bytes[i]==0);
}
static void expect_stats(void) {
    const struct kui_capture_stats *got=kui_capture_last_stats();
    assert(got->bytes==fake.stats.bytes && got->sampled==fake.stats.sampled);
    assert(got->verified==fake.stats.verified && got->crc_only==fake.stats.crc_only);
    assert(!strcmp(got->job_dir,fake.stats.job_dir));
    assert(!strcmp(got->disc_title,fake.stats.disc_title));
    assert(!strcmp(got->gdi_name,fake.stats.gdi_name));
    assert(got->reference_checked==fake.stats.reference_checked);
    assert(got->reference.result==fake.stats.reference.result);
    assert(!strcmp(got->reference.catalog,fake.stats.reference.catalog));
    assert(!strcmp(got->reference.name,fake.stats.reference.name));
    for(unsigned i=0;i<KUI_TIME_PHASES;++i) assert(got->phase_us[i]==fake.stats.phase_us[i]);
    for(unsigned i=0;i<KUI_TIME_BUCKETS;++i)
        assert(got->capture_bucket_us[i]==fake.stats.capture_bucket_us[i]);
}
static void run_destination(const char *destination) {
    assert(kui_capture_start(fake.mode,"123456abcdef",destination)==fake.result);
    assert(!strcmp(fake.order,"OTDPSCECLUR"));
    assert(fake.engine_calls==1);
    assert(fake.simulated_writes==(fake.mode!=KUI_CAPTURE_VERIFY?1u:0u));
    expect_stats();
}
static void run_success(void) {run_destination("/Games");}
static void selected_requests(void) {
    /* Copy all request strings before a refresh callback can change UI-owned
     * buffers. Sanitizing the folder label must not change optical metadata. */
    reset();char parent[128]="//Games/./My Discs//",label[96]="  New/Folder:* Label. ";
    strcpy(fake.destination,"/Games/My Discs");strcpy(fake.folder_label,"New_Folder__ Label");
    fake.mutate_destination=parent;fake.mutate_label=label;
    assert(kui_capture_start_selected(KUI_CAPTURE_NEW,"123456abcdef",parent,NULL,label,false)==KUI_CAPTURE_COMPLETE);
    assert(!strcmp(fake.order,"OTDPSCECLUR") && fake.engine_calls==1 && fake.simulated_writes==1);
    assert(!strcmp(parent,"/Changed during refresh") && !strcmp(label,"Changed during refresh"));expect_stats();
    for(unsigned mode=KUI_CAPTURE_RESUME;mode<=KUI_CAPTURE_VERIFY;mode++) {
        reset();fake.mode=(enum kui_capture_mode)mode;
        char exact[256]="0://Games/./Older Job (2)//",ignored[96];memset(ignored,'Q',sizeof(ignored));
        strcpy(fake.selected_job,"/Games/Older Job (2)");fake.mutate_job=exact;
        assert(kui_capture_start_selected(fake.mode,"123456abcdef","/Games",exact,ignored,false)==KUI_CAPTURE_COMPLETE);
        assert(!strcmp(fake.order,"OTDPSCECLUR") && fake.engine_calls==1);
        assert(fake.simulated_writes==(mode==KUI_CAPTURE_RESUME?1u:0u));
        assert(!strcmp(exact,"/Changed during refresh/Job"));expect_stats();
    }
    /* The exact selector has a larger bound than the destination parent. */
    reset();fake.mode=KUI_CAPTURE_RESUME;
    char exact[256];exact[0]='/';memset(exact+1,'A',127);exact[128]='/';memset(exact+129,'B',124);exact[253]=0;
    strcpy(fake.selected_job,exact);
    assert(kui_capture_start_selected(fake.mode,"123456abcdef","/Games",exact,NULL,false)==KUI_CAPTURE_COMPLETE);
    assert(fake.engine_calls==1);expect_stats();
    for(unsigned mode=KUI_CAPTURE_NEW;mode<=KUI_CAPTURE_VERIFY;mode++) {
        reset();fake.mode=(enum kui_capture_mode)mode;
        assert(kui_capture_start_selected(fake.mode,"123456abcdef","/Games","","",false)==KUI_CAPTURE_COMPLETE);
        assert(fake.engine_calls==1);expect_stats();
    }
    reset();fake.mode=KUI_CAPTURE_RESUME;fake.quick_resume=true;
    kui_options.resume_size[0]=false;strcpy(fake.selected_job,"/Games/Older");
    assert(kui_capture_start_selected(fake.mode,"123456abcdef","/Games","/Games/Older",NULL,true)==KUI_CAPTURE_COMPLETE);
    assert(!kui_options.resume_size[0]);expect_stats();

    const char *bad[]={"/","0:/","/Games/../Older","1:/Games/Older","/Games/NUL","/Games/trailing."};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++)for(unsigned stopped=0;stopped<2;stopped++) {
        reset();run_success();reset();fake.stop=stopped!=0;
        assert(kui_capture_start_selected(KUI_CAPTURE_RESUME,"123456abcdef","/Games",bad[i],NULL,false)==
            (stopped?KUI_CAPTURE_STOPPED:KUI_CAPTURE_FAILED));
        assert(!fake.order[0] && !fake.engine_calls && !fake.simulated_writes);expect_empty_stats();
    }
    for(unsigned stopped=0;stopped<2;stopped++) {
        reset();run_success();reset();fake.stop=stopped!=0;
        assert(kui_capture_start_selected(KUI_CAPTURE_NEW,"123456abcdef","/Games","/Games/Older",NULL,false)==
            (stopped?KUI_CAPTURE_STOPPED:KUI_CAPTURE_FAILED));
        assert(!fake.order[0] && !fake.engine_calls && !fake.simulated_writes);expect_empty_stats();
        reset();run_success();reset();fake.stop=stopped!=0;
        char too_long_label[96];memset(too_long_label,'X',sizeof(too_long_label));
        assert(kui_capture_start_selected(KUI_CAPTURE_NEW,"123456abcdef","/Games",NULL,too_long_label,false)==
            (stopped?KUI_CAPTURE_STOPPED:KUI_CAPTURE_FAILED));
        assert(!fake.order[0] && !fake.engine_calls && !fake.simulated_writes);expect_empty_stats();
        for(unsigned mode=KUI_CAPTURE_NEW;mode<=KUI_CAPTURE_VERIFY;mode+=2u) {
            reset();run_success();reset();fake.stop=stopped!=0;
            assert(kui_capture_start_selected((enum kui_capture_mode)mode,"123456abcdef","/Games",NULL,NULL,true)==
                (stopped?KUI_CAPTURE_STOPPED:KUI_CAPTURE_FAILED));
            assert(!fake.order[0] && !fake.engine_calls && !fake.simulated_writes);expect_empty_stats();
        }
    }
    reset();run_success();reset();fake.refresh_ok=false;
    assert(kui_capture_start_selected(KUI_CAPTURE_VERIFY,"123456abcdef","/Games","/Games/Older",NULL,false)==KUI_CAPTURE_FAILED);
    assert(!strcmp(fake.order,"O") && !fake.engine_calls);expect_empty_stats();
}
int main(void) {
    expect_empty_stats();
    reset();fake.cd=true;
    assert(kui_capture_start(KUI_CAPTURE_NEW,"123456abcdef","/Games")==KUI_CAPTURE_FAILED);
    assert(!fake.engine_calls);
    reset();fake.cd=true;
    assert(kui_capture_start_target(KUI_CAPTURE_NEW,"123456abcdef","/Games",NULL,NULL,false,false,true)==KUI_CAPTURE_COMPLETE);
    assert(fake.engine_calls==1);
    reset();
    assert(kui_capture_start_target(KUI_CAPTURE_NEW,"123456abcdef","/Games",NULL,NULL,false,true,false)==KUI_CAPTURE_COMPLETE);
    assert(!strcmp(fake.order,"OTDPSMuUNCECLuER"));
    reset();fake.remote_close_ok=false;
    assert(kui_capture_start_target(KUI_CAPTURE_NEW,"123456abcdef","/Games",NULL,NULL,false,true,false)==KUI_CAPTURE_FAILED);
    assert(!strcmp(fake.order,"OTDPSMuUNCECLuER"));
    reset();fake.config_ok=false;
    assert(kui_capture_start_target(KUI_CAPTURE_NEW,"123456abcdef","/Games",NULL,NULL,false,true,false)==KUI_CAPTURE_FAILED);
    assert(!strcmp(fake.order,"OTDPSMuU") && !fake.engine_calls);
    reset();fake.remote_ok=false;
    assert(kui_capture_start_target(KUI_CAPTURE_NEW,"123456abcdef","/Games",NULL,NULL,false,true,false)==KUI_CAPTURE_FAILED);
    assert(!strcmp(fake.order,"OTDPSMuUN") && !fake.engine_calls);
    for(unsigned storage=KUI_STORAGE_SCIF;storage<=KUI_STORAGE_IDE;storage++) {
        reset();fake.storage=storage;fake.exercise_dma=true;run_success();
    }
    /* Completion is not necessarily read-back verification. Preserve exactly
     * what the engine reports for every action and returned outcome. */
    for(unsigned mode=KUI_CAPTURE_NEW;mode<=KUI_CAPTURE_VERIFY;++mode) {
        for(unsigned verified=0;verified<2;++verified) {
            reset();fake.mode=(enum kui_capture_mode)mode;
            fake.stats.verified=verified!=0;fake.stats.crc_only=verified==0;
            kui_options.capture_crc_only[0]=verified==0;
            kui_options.end_readback[0]=verified!=0;
            run_success();
        }
        for(unsigned stopped=0;stopped<2;++stopped) {
            /* Seed an earlier complete outcome, then reject before any I/O. */
            reset();run_success();reset();fake.mode=(enum kui_capture_mode)mode;
            fake.refresh_ok=false;fake.stop=stopped!=0;
            enum kui_capture_result want=stopped?KUI_CAPTURE_STOPPED:KUI_CAPTURE_FAILED;
            assert(kui_capture_start(fake.mode,"123456abcdef","/Games")==want);
            assert(!strcmp(fake.order,"O"));
            assert(fake.engine_calls==0 && fake.simulated_writes==0);
            assert(strstr(fake.log,"Capture refused") && strstr(fake.log,"no dump writes"));
            expect_empty_stats();
        }
    }
    /* Output selection comes from the preferences loaded for this operation,
     * rather than a stale global value from before refresh. */
    for(unsigned format=0;format<KUI_CAPTURE_FORMAT_COUNT;++format) {
        reset();fake.load_format=true;fake.loaded_format=(enum kui_capture_format)format;
        run_success();assert(kui_options.capture_format==fake.loaded_format);
    }
    /* Quick resume overrides only the local options passed to a Resume call.
     * It does not overwrite configured full resume, hash/readback choices, or
     * bench lists. Job hash compatibility is checked by the engine, not guessed
     * from today's preferences (covered by test_capture_options.py). */
    for(unsigned crc_only=0;crc_only<2;++crc_only) {
        reset();fake.mode=KUI_CAPTURE_RESUME;fake.quick_resume=true;
        kui_options.capture_crc_only[0]=crc_only!=0;
        kui_options.resume_size[0]=false;
        kui_options.resume_size[1]=true;kui_options.resume_check_count=2;
        struct kui_options before=kui_options;
        assert(kui_capture_resume_quick("123456abcdef","/Games")==KUI_CAPTURE_COMPLETE);
        assert(!strcmp(fake.order,"OTDPSCECLUR") && fake.engine_calls==1 && fake.simulated_writes==1);
        assert(!memcmp(&before,&kui_options,sizeof(before)));expect_stats();
        for(unsigned mode=KUI_CAPTURE_NEW;mode<=KUI_CAPTURE_VERIFY;++mode) {
            fake.order_size=0;fake.order[0]=0;fake.engine_calls=fake.simulated_writes=0;
            fake.mode=(enum kui_capture_mode)mode;fake.quick_resume=false;
            run_success();
            assert(!memcmp(&before,&kui_options,sizeof(before)));
        }
    }
    /* Opting in cannot bypass either fail-closed destination or configuration
     * validation. A failed quick attempt must not leak its mode to another run. */
    reset();fake.mode=KUI_CAPTURE_RESUME;fake.quick_resume=true;
    kui_options.resume_size[0]=false;
    assert(kui_capture_resume_quick("123456abcdef","/../Games")==KUI_CAPTURE_FAILED);
    assert(!fake.order[0] && !fake.engine_calls && !fake.simulated_writes);expect_empty_stats();
    fake.refresh_ok=false;
    assert(kui_capture_resume_quick("123456abcdef","/Games")==KUI_CAPTURE_FAILED);
    assert(!strcmp(fake.order,"O") && !fake.engine_calls && !fake.simulated_writes);expect_empty_stats();
    fake.refresh_ok=true;fake.quick_resume=false;fake.order_size=0;fake.order[0]=0;
    run_success();assert(!kui_options.resume_size[0]);
    /* Normalize once before options/media access, and retain that stable path
     * throughout the synchronous engine call. */
    reset();strcpy(fake.destination,"/Games/My Discs");
    run_destination("//Games/./My Discs//");
    const char *invalid[]={NULL,"","Games","0:/Games","/../Games","/Games/..",
        "/Games/Bad\"Name","/Games/Bad?Name","/Games/NUL","/Games/trailing."};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i) {
        for(unsigned stopped=0;stopped<2;++stopped) {
            reset();run_success();reset();fake.stop=stopped!=0;
            enum kui_capture_result want=stopped?KUI_CAPTURE_STOPPED:KUI_CAPTURE_FAILED;
            assert(kui_capture_start(fake.mode,"123456abcdef",invalid[i])==want);
            assert(!fake.order[0] && !fake.engine_calls && !fake.simulated_writes);
            assert(strstr(fake.log,"invalid or too-long destination") && strstr(fake.log,"no dump writes"));
            expect_empty_stats();
        }
    }
    reset();run_success();reset();
    char too_long[KUI_DEST_ROOT_CAP+1];memset(too_long,'x',sizeof(too_long)-1);
    too_long[0]='/';too_long[sizeof(too_long)-1]=0;
    assert(kui_capture_start(fake.mode,"123456abcdef",too_long)==KUI_CAPTURE_FAILED);
    assert(!fake.order[0] && !fake.engine_calls && !fake.simulated_writes);expect_empty_stats();
    /* A catalog result is an observation, not an adapter-generated success.
     * Missing, inconclusive and unchecked outcomes preserve their own grade. */
    for(int grade=KUI_KNOWN_ERROR;grade<=KUI_KNOWN_FULL_MATCH;++grade) {
        reset();fake.stats.reference.result=(enum kui_known_result)grade;
        fake.stats.reference_checked=grade!=KUI_KNOWN_CANCELLED;run_success();
    }
    for(unsigned failure=0;failure<4;++failure) {
        reset();run_success();reset();
        if(failure==0) fake.prepare_ok=false;
        if(failure==1) fake.cancel_in_prepare=true;
        if(failure==2) fake.plan_ok=false;
        if(failure==3) fake.connect_ok=false;
        assert(kui_capture_start(fake.mode,"123456abcdef","/Games")==
            (failure==1?KUI_CAPTURE_STOPPED:KUI_CAPTURE_FAILED));
        assert(!strcmp(fake.order,failure<2?"OTD":failure==2?"OTDP":"OTDPS"));
        assert(fake.engine_calls==0 && fake.simulated_writes==0);expect_empty_stats();
    }
    /* Interrupted engine runs retain their partial bytes and job location. */
    for(unsigned result=KUI_CAPTURE_FAILED;result<=KUI_CAPTURE_STOPPED;++result) {
        reset();fake.result=(enum kui_capture_result)result;fake.stats.verified=false;
        fake.stats.bytes=321;strcpy(fake.stats.job_dir,"/KUI/dumps/partial");run_success();
    }
    selected_requests();
    puts("PASS capture adapter: fail-closed inputs, per-action quick resume without persistent changes, named output/reference outcomes, fresh stats");
    return 0;
}
