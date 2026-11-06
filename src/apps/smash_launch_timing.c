/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_launch_timing.h"
#if KUI_SMASH_LAUNCH_TIMING
#include "platform.h"
#include "kui/target.h"
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if KUI_ON_CONSOLE
#include <kos/timer.h>
#endif

const volatile uint32_t kui_smash_launch_timing_profile[4] __attribute__((used))={
    UINT32_C(0x5349554b),UINT32_C(0x31544955),1u,16u};
enum { REPORT_BYTES=8192 };
struct sample {uint64_t ms;struct kui_smash_phase_stamp clock;};
struct account {
    uint64_t ms,cycles,cpu_ms;
    uint32_t calls,flags;
    bool wall,cycle,nominal;
};
static struct {
    bool running,selected,finished,prepared,report_allowed,native,virtual_vmu;
    unsigned phase;
    char image[512],build[13];
    char product[12],mode[64];
    struct sample begin,last,end;
    struct account total,phases[KUI_SMASH_UI_PHASES],details[KUI_SMASH_UI_DETAILS];
    uint64_t metrics[KUI_SMASH_UI_METRICS];
    bool metric_present[KUI_SMASH_UI_METRICS];
    struct {
        bool present;
        char build[13];
        uint32_t bytes,crc32,helper_crc32;
    } components[KUI_SMASH_UI_COMPONENTS];
} timing;
static uint64_t (*test_clock)(void *);
static void *test_context;
static const char *const phases[KUI_SMASH_UI_PHASES]={
    "request_setup","metadata","menu_proof","track_map","prepare_other","child_module",
    "mde_read_hash","mde_extent","mde_proof","shared_card","capsules","report_cleanup",
    "post_prepare","activity","audio_shutdown","module_load","module_verify","ip_check",
    "session_prepare","file_cleanup"};
static const char *const details[KUI_SMASH_UI_DETAILS]={
    "history_total","history_resume","history_prelaunch","history_receipt","profile_cache",
    "raw_filesystem","snapshot259","profile_save","sdk_replay","sdk_discovery",
    "capsule_inspect","capsule_child","capsule_outer","early_history_resume",
    "executable_read","executable_sha256","executable_crc32","program_extent"};
static const char *const metrics[KUI_SMASH_UI_METRICS]={
    "sdk_cache_hit","mde_bytes","profile_cache_hit","raw_reads","fat_entries","directory_sectors",
    "child_capsule_written","outer_capsule_written",
    "outer_module_crc32","child_module_crc32","outer_payload_bytes","child_payload_bytes",
    "outer_capsule_bytes","child_capsule_bytes","outer_capsule_crc32","child_capsule_crc32",
    "native_profile_flags","module_crc32","module_bytes","executable_source_bytes",
    "executable_sha256_bytes","program_bytes","source_read_callbacks","source_read_bytes","source_seeks",
    "history_read_calls","history_read_bytes","verify_batch_bytes","raw_sessions","raw_blocks"};
void kui_smash_launch_timing_clock(uint64_t (*read)(void *),void *context) {
    test_clock=read;test_context=context;
}
static bool wall_available(void) {
    if(test_clock)return true;
#if KUI_ON_CONSOLE
    return true;
#else
    return false;
#endif
}
static struct sample sample(void) {
    struct sample s={0};
    if(test_clock)s.ms=test_clock(test_context);
#if KUI_ON_CONSOLE
    else s.ms=timer_ms_gettime64();
#endif
    kui_smash_phase_clock_read(&s.clock);return s;
}
static void account(struct account *a,const struct sample *begin,const struct sample *end) {
    struct kui_smash_phase_delta d;kui_smash_phase_clock_delta(&begin->clock,&end->clock,&d);
    bool wall=wall_available() && end->ms>=begin->ms;
    bool cycle=(d.flags&KUI_SMASH_PHASE_CYCLES)!=0u,nominal=(d.flags&KUI_SMASH_PHASE_NOMINAL_MS)!=0u;
    if(!a->calls) {a->wall=wall;a->cycle=cycle;a->nominal=nominal;}
    else {a->wall&=wall;a->cycle&=cycle;a->nominal&=nominal;}
    if(wall)a->ms+=end->ms-begin->ms;
    if(cycle)a->cycles+=d.cycles;
    if(nominal)a->cpu_ms+=d.cpu_ms;
    a->flags|=d.flags;++a->calls;
}
void kui_smash_launch_timing_request(bool enabled,uint64_t action_ms) {
    memset(&timing,0,sizeof(timing));
    if(!enabled || kui_smash_launch_timing_profile[2]!=1u)return;
    timing.running=true;timing.report_allowed=true;timing.phase=KUI_SMASH_UI_REQUEST_SETUP;
    timing.begin=timing.last=sample();
    /* The controller timestamp precedes publication to the storage worker.
     * PRFC observation begins here and is labelled separately in the report. */
    if(action_ms<=timing.begin.ms)timing.begin.ms=timing.last.ms=action_ms;
}
void kui_smash_launch_timing_activate(const char *image,const char *build) {
    if(!timing.running || !image || !build)return;
    timing.selected=true;snprintf(timing.image,sizeof(timing.image),"%s",image);
    snprintf(timing.build,sizeof(timing.build),"%.12s",build);
}
void kui_smash_launch_timing_native(const char *image,const char *build,const char *product,
    const char *mode,bool virtual_vmu,uint32_t module_crc32,uint32_t module_bytes) {
    if(!timing.running || !image || !build || !product || !mode)return;
    kui_smash_launch_timing_activate(image,build);timing.native=true;timing.virtual_vmu=virtual_vmu;
    snprintf(timing.product,sizeof(timing.product),"%.11s",product);
    snprintf(timing.mode,sizeof(timing.mode),"%s",mode);
    kui_smash_launch_timing_metric(KUI_SMASH_UI_MODULE_CRC32,module_crc32);
    kui_smash_launch_timing_metric(KUI_SMASH_UI_MODULE_BYTES,module_bytes);
}
void kui_smash_launch_timing_report_allowed(bool allowed) {timing.report_allowed=allowed;}
void kui_smash_launch_timing_phase(enum kui_smash_ui_phase phase) {
    if(!timing.running || phase>=KUI_SMASH_UI_PHASES || phase==timing.phase)return;
    struct sample end=sample();account(&timing.phases[timing.phase],&timing.last,&end);
    timing.last=end;timing.phase=(unsigned)phase;
}
void kui_smash_launch_timing_metric(enum kui_smash_ui_metric id,uint64_t value) {
    if(!timing.running || id>=KUI_SMASH_UI_METRICS)return;
    timing.metrics[id]=value;timing.metric_present[id]=true;
}
void kui_smash_launch_timing_component(enum kui_smash_ui_component id,const char *build,
    uint32_t bytes,uint32_t crc,uint32_t helper_crc) {
    if(!timing.running || id>=KUI_SMASH_UI_COMPONENTS || !build || !bytes)return;
    char copy[13];snprintf(copy,sizeof(copy),"%.12s",build);
    if(strlen(copy)!=12u)return;
    for(unsigned i=0u;i<12u;i++)
        if(!((copy[i]>='0' && copy[i]<='9') || (copy[i]>='a' && copy[i]<='f')))return;
    memcpy(timing.components[id].build,copy,sizeof(copy));
    timing.components[id].bytes=bytes;timing.components[id].crc32=crc;
    timing.components[id].helper_crc32=helper_crc;timing.components[id].present=true;
}
void kui_smash_launch_timing_add(enum kui_smash_ui_metric id,uint64_t value) {
    if(!timing.running || id>=KUI_SMASH_UI_METRICS)return;
    uint64_t old=timing.metric_present[id]?timing.metrics[id]:0u;
    timing.metrics[id]=value>UINT64_MAX-old?UINT64_MAX:old+value;timing.metric_present[id]=true;
}
struct kui_smash_launch_span kui_smash_launch_timing_span_begin(enum kui_smash_ui_detail id) {
    struct kui_smash_launch_span span={0};
    if(timing.running && id<KUI_SMASH_UI_DETAILS) {
        struct sample s=sample();span.ms=s.ms;span.clock=s.clock;span.id=(unsigned)id;span.enabled=true;
    }
    return span;
}
void kui_smash_launch_timing_span_end(const struct kui_smash_launch_span *span) {
    if(!span || !span->enabled || !timing.running || span->id>=KUI_SMASH_UI_DETAILS)return;
    struct sample begin={span->ms,span->clock},end=sample();account(&timing.details[span->id],&begin,&end);
}
static uint64_t ms(unsigned phase) {return timing.phases[phase].ms;}
void kui_smash_launch_timing_freeze(bool prepared,kui_log_fn log) {
    if(!timing.running || timing.finished)return;
    timing.end=sample();account(&timing.phases[timing.phase],&timing.last,&timing.end);
    account(&timing.total,&timing.begin,&timing.end);
    timing.running=false;timing.finished=true;timing.prepared=prepared;
    if(!timing.selected || !log)return;
    if(!timing.total.wall) {log("Launch UI timing: monotonic clock unavailable");return;}
    log("%s UI action-to-ready: %" PRIu64 " ms (%s); diagnostic I/O excluded",
        timing.native?"Native":"Smash",timing.total.ms,prepared?"prepared":"failed");
    log("UI early ms: setup=%" PRIu64 " meta=%" PRIu64 " menu=%" PRIu64 " map=%" PRIu64,
        ms(KUI_SMASH_UI_REQUEST_SETUP),ms(KUI_SMASH_UI_METADATA),ms(KUI_SMASH_UI_MENU_PROOF),ms(KUI_SMASH_UI_TRACK_MAP));
    log("UI native ms: module=%" PRIu64 " read/hash=%" PRIu64 " extent=%" PRIu64 " proof=%" PRIu64,
        ms(KUI_SMASH_UI_CHILD_MODULE),ms(KUI_SMASH_UI_MDE_READ_HASH),ms(KUI_SMASH_UI_MDE_EXTENT),ms(KUI_SMASH_UI_MDE_PROOF));
    log("UI tail ms: card=%" PRIu64 " caps=%" PRIu64 " report=%" PRIu64 " activity=%" PRIu64 " audio=%" PRIu64,
        ms(KUI_SMASH_UI_CARD),ms(KUI_SMASH_UI_CAPSULES),ms(KUI_SMASH_UI_REPORT_CLEANUP),ms(KUI_SMASH_UI_ACTIVITY),ms(KUI_SMASH_UI_AUDIO));
}
struct builder {char *out;size_t used,capacity;bool ok;};
static void append(struct builder *b,const char *format,...) {
    if(!b->ok)return;
    va_list ap;va_start(ap,format);int n=vsnprintf(b->out+b->used,b->capacity-b->used,format,ap);va_end(ap);
    if(n<0 || (size_t)n>=b->capacity-b->used)b->ok=false;else b->used+=(size_t)n;
}
static void row(struct builder *b,const char *kind,const char *name,const struct account *a) {
    append(b,"%s.%s: calls=%" PRIu32 " ms=",kind,name,a->calls);
    if(a->calls && a->wall)append(b,"%" PRIu64,a->ms);else append(b,"unavailable");
    append(b," cycles=");if(a->calls && a->cycle)append(b,"%" PRIu64,a->cycles);else append(b,"unavailable");
    append(b," cpu_nominal_ms=");if(a->calls && a->nominal)append(b,"%" PRIu64,a->cpu_ms);else append(b,"unavailable");
    append(b," flags=%08" PRIX32 "\n",a->flags);
}
size_t kui_smash_launch_timing_format(char *out,size_t capacity) {
    if(!out || !capacity || !timing.finished || !timing.selected)return 0u;
    struct builder b={out,0u,capacity,true};
    append(&b,"KUI %s UI launch timing v1\nbuild: %s\nimage: %s\nstate: %s\n",timing.native?"native":"Smash",timing.build,timing.image,
        timing.prepared?"prepared before handoff; game start unconfirmed":"preparation failed");
    if(timing.native)append(&b,"product: %s\nreader: %s\nvirtual_vmu: %s\n",
        timing.product,timing.mode,timing.virtual_vmu?"On":"Off");
    static const char *const component_names[KUI_SMASH_UI_COMPONENTS]={
        "retail-smash-menu.kui","retail-smash-native-menu.kui","retail-smash-vc2.kui","retail-smash-swirl.kui"};
    for(unsigned i=0u;i<KUI_SMASH_UI_COMPONENTS;i++)if(timing.components[i].present)
        append(&b,"component.%s: build=%s payload_bytes=%" PRIu32 " crc32=%08" PRIX32 " helper_crc32=%08" PRIX32 "\n",
            component_names[i],timing.components[i].build,timing.components[i].bytes,
            timing.components[i].crc32,timing.components[i].helper_crc32);
    append(&b,"scope: controller launch action to worker handoff-ready\nwall_clock: %s\n",
        test_clock?"injected host test milliseconds":"KOS monotonic milliseconds");
    append(&b,"report_io: excluded from all measured intervals\nprfc: observation starts at request publication; nominal CPU ms are not calibrated wall time\n");
    if(timing.native)append(&b,"counter_scope: source callbacks/bytes/seeks are attempted file I/O; history calls are read attempts and bytes are delivered; executable SHA bytes count GUI source/prefix hashes\n");
    row(&b,"total","action_to_ready",&timing.total);
    for(unsigned i=0u;i<KUI_SMASH_UI_PHASES;++i)row(&b,"phase",phases[i],&timing.phases[i]);
    append(&b,"details: nested intervals; do not add to flat phases\n");
    for(unsigned i=0u;i<KUI_SMASH_UI_DETAILS;++i)if(timing.details[i].calls)row(&b,"detail",details[i],&timing.details[i]);
    for(unsigned i=0u;i<KUI_SMASH_UI_METRICS;++i)if(timing.metric_present[i]) {
        bool crc=i==KUI_SMASH_UI_OUTER_MODULE_CRC32 || i==KUI_SMASH_UI_CHILD_MODULE_CRC32 ||
            i==KUI_SMASH_UI_OUTER_CAPSULE_CRC32 || i==KUI_SMASH_UI_CHILD_CAPSULE_CRC32 ||
            i==KUI_SMASH_UI_MODULE_CRC32;
        if(crc)append(&b,"metric.%s: 0x%08" PRIX32 "\n",metrics[i],(uint32_t)timing.metrics[i]);
        else append(&b,"metric.%s: %" PRIu64 "\n",metrics[i],timing.metrics[i]);
    }
    append(&b,"clock_begin: flags=%08" PRIX32 " pmcr=%08" PRIX32 " frqcr=%08" PRIX32 " hi=%08" PRIX32 " lo=%08" PRIX32 "\n",
        timing.begin.clock.flags,timing.begin.clock.pmcr,timing.begin.clock.frqcr,timing.begin.clock.cycles_hi,timing.begin.clock.cycles_lo);
    append(&b,"clock_end: flags=%08" PRIX32 " pmcr=%08" PRIX32 " frqcr=%08" PRIX32 " hi=%08" PRIX32 " lo=%08" PRIX32 "\n",
        timing.end.clock.flags,timing.end.clock.pmcr,timing.end.clock.frqcr,timing.end.clock.cycles_hi,timing.end.clock.cycles_lo);
    if(!b.ok)return 0u;
    uint32_t crc=kui_crc32(0u,b.out,b.used);append(&b,"crc32: %08" PRIX32 "\n",crc);
    return b.ok?b.used:0u;
}
bool kui_smash_launch_timing_save_mounted(kui_log_fn log,kui_cancel_fn cancel) {
    if(!timing.finished || !timing.selected || (!timing.prepared && !timing.native) ||
       !timing.report_allowed || !log || !cancel || cancel())return false;
    char *text=malloc(REPORT_BYTES);if(!text)return false;
    size_t bytes=kui_smash_launch_timing_format(text,REPORT_BYTES);bool ok=false;FIL file;
    const char *path=timing.native?KUI_NATIVE_LAUNCH_TIMING_PATH:KUI_SMASH_LAUNCH_TIMING_PATH;
    if(!bytes || f_open(&file,path,FA_WRITE|FA_CREATE_ALWAYS)!=FR_OK)goto done;
    UINT got=0u;ok=!cancel() && f_write(&file,text,(UINT)bytes,&got)==FR_OK && got==bytes;
    if(ok)ok=f_sync(&file)==FR_OK;
    if(f_close(&file)!=FR_OK)ok=false;
    if(!ok || cancel()) {ok=false;goto done;}
    if(f_open(&file,path,FA_READ)!=FR_OK) {ok=false;goto done;}
    ok=f_size(&file)==bytes;uint8_t check[512];
    for(size_t at=0u;ok && at<bytes;) {
        UINT take=bytes-at>sizeof(check)?sizeof(check):(UINT)(bytes-at);got=0u;
        ok=!cancel() && f_read(&file,check,take,&got)==FR_OK && got==take && !memcmp(check,text+at,take);at+=take;
    }
    if(f_close(&file)!=FR_OK)ok=false;
    if(cancel())ok=false;
done:
    free(text);return ok;
}
void kui_smash_launch_timing_finish(bool prepared,kui_log_fn log,kui_cancel_fn cancel) {
    kui_smash_launch_timing_freeze(prepared,log);
    if(!timing.selected || (!timing.prepared && !timing.native) || !timing.report_allowed || !log || !cancel || cancel())return;
    struct sample before=sample();bool saved=false;
    if(kui_sd_connect()) {
        FATFS fs;
        if(kui_mount(&fs,log))saved=kui_smash_launch_timing_save_mounted(log,cancel);
        if(f_mount(NULL,"0:",0)!=FR_OK)saved=false;
        kui_sd_disconnect();
    }
    struct sample after=sample();
    if(saved)log("Launch UI timing saved: %s",(timing.native?KUI_NATIVE_LAUNCH_TIMING_PATH:KUI_SMASH_LAUNCH_TIMING_PATH)+2);
    else log("Launch UI timing report unavailable; continuing prepared launch");
    if(wall_available() && after.ms>=before.ms)log("UI diagnostic report I/O: %" PRIu64 " ms (outside action-to-ready)",after.ms-before.ms);
}
#endif
