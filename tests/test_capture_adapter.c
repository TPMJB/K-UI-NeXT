/* SPDX-License-Identifier: GPL-3.0-only */
/* Exercise the real console capture adapter with no optical or filesystem I/O.
 * A rejected destination/preference load must never reach the drive or engine;
 * outcome details must come from this operation, not an earlier successful one. */
#include "platform.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

struct kui_options kui_options;
static struct {
    bool refresh_ok, prepare_ok, plan_ok, connect_ok, stop;
    bool cancel_in_prepare;
    enum kui_capture_mode mode;
    enum kui_capture_result result;
    struct kui_capture_stats stats;
    unsigned engine_calls, simulated_writes;
    char order[32], log[512], destination[KUI_DEST_ROOT_CAP];
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
    fake.mode=KUI_CAPTURE_NEW;fake.result=KUI_CAPTURE_COMPLETE;
    /* Deliberately nonzero options so a missing/incorrect mapping is visible. */
    kui_options.capture_crc_only[0]=true;
    kui_options.end_readback[0]=false;
    kui_options.resume_size[0]=true;
    kui_options.sample_readback[0]=17;
    kui_options.capture_dma[0]=true;
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

bool kui_options_refresh(void) {note('O');return fake.refresh_ok;}
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
    return fake.prepare_ok;
}
bool kui_plan_tracks(const struct kui_toc sessions[2],struct kui_capture_plan *out) {
    note('P');assert(sessions[0].count==2 && sessions[1].count==1);
    memset(out,0,sizeof(*out));out->count=3;out->bytes=987654321;
    return fake.plan_ok;
}
bool kui_sd_connect(void) {note('S');return fake.connect_ok;}
void kui_sd_disconnect(void) {note('U');}
enum kui_read_result kui_disc_read_raw(void *ctx,uint32_t fad,unsigned sectors,uint8_t *out) {
    (void)ctx;(void)fad;(void)sectors;(void)out;assert(!"unexpected disc read");return KUI_READ_FATAL;
}
bool kui_disc_read_begin(void *ctx,uint32_t fad,unsigned sectors,uint8_t *out) {
    (void)ctx;(void)fad;(void)sectors;(void)out;assert(!"unexpected DMA read");return false;
}
enum kui_read_result kui_disc_read_end(void *ctx) {
    (void)ctx;assert(!"unexpected DMA completion");return KUI_READ_FATAL;
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
    assert(ops->options->resume_size_only==kui_options.resume_size[0]);
    assert(ops->options->sample_every==kui_options.sample_readback[0]);
    assert(ops->options->read_dma==kui_options.capture_dma[0]);
    assert(ops->options->output && ops->options->output->game_names);
    assert(!strcmp(ops->options->output->parent,fake.destination));
    assert(!ops->options->bench);
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
int main(void) {
    expect_empty_stats();
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
    puts("PASS capture adapter: invalid paths/preferences stop before I/O, named output/reference outcomes forwarded, stale stats cleared");
    return 0;
}
