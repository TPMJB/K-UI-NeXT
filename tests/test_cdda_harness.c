/* SPDX-License-Identifier: GPL-3.0-only */
/* Integration simulation of the actual homebrew profile orchestrator. Real
 * PCM, ring and stream code runs against deterministic card/AICA models. This
 * checks control flow and buffer contents, not audible quality or hardware. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CDDA_HARNESS_HOST_TEST 1
#if CDDA_TEST_PROFILE == 5
/* Observe actual public command APIs while keeping the production core and
 * orchestrator unchanged. Function-like macros preserve the struct tags. */
#define kui_cdda_control_status(...) harness_control_status(__VA_ARGS__)
#define kui_cdda_control_request(...) harness_control_request(__VA_ARGS__)
#define kui_cdda_control_observe(...) harness_control_observe(__VA_ARGS__)
#endif
#include "../src/loader/cdda_main.c"
#if CDDA_TEST_PROFILE == 5
#undef kui_cdda_control_status
#undef kui_cdda_control_request
#undef kui_cdda_control_observe
extern enum kui_cdda_control_result kui_cdda_control_status(
    const struct kui_cdda_control *,struct kui_cdda_control_status *);
extern enum kui_cdda_control_result kui_cdda_control_request(
    struct kui_cdda_control *,const struct kui_cdda_control_request *,
    struct kui_cdda_control_action *);
extern enum kui_cdda_control_result kui_cdda_control_observe(
    struct kui_cdda_control *,uint32_t,uint32_t);
#endif

#define FIXTURE_BYTES (529200u*4u)
#define TRACK_BYTES 7222992u
#define STRESS_BYTES (8u*1024u*1024u)
#define MAX_STARTS 16u
enum fault_kind { NO_FAULT, PCM_FAULT, DATA_FAULT, DATA_CORRUPTION, DATA_DELAY,
    DATA_STARVATION, TIMING_READ_DELAY, COMMAND_REPRIME_FAULT };
static enum fault_kind fault;
static uint64_t sim_ticks,key_tick,clock_origin;
static bool adjusted_aica_rate;
static bool running,storage_ready,audio_open,data_open,injected,finished;
static unsigned storage_kind,starts,stops,position_checks,write_faults;
static unsigned post_fault_writes;
static uint32_t pcm_reads,data_reads,blocks,reported_failures,last_observed_elapsed;
static int16_t samples_left[KUI_CDDA_RING_FRAMES];
static int16_t samples_right[KUI_CDDA_RING_FRAMES];
struct playback_record {
    uint32_t first,end,loop_first,stop_cursor,prefetch;
    uint64_t start_tick,stop_tick;
    bool repeat;
};
static struct playback_record records[MAX_STARTS];
#if CDDA_TEST_PROFILE == 5
static unsigned model_status_checks,model_paused_statuses,model_eof_statuses;
static unsigned model_invalid_active_seeks;
static unsigned model_played_observations;
static uint32_t failed_command_epoch;
#endif
#if CDDA_TEST_PROFILE == 4
struct model_observation {uint32_t before,after,frame;};
static struct model_observation first_endpoint,last_endpoint;
static uint32_t initial_observed_frame;
#endif
volatile struct kui_storage_boot_marker cdda_storage_boot_marker={
    KUI_STORAGE_BOOT_MAGIC1,KUI_STORAGE_BOOT_MAGIC2,1u,KUI_STORAGE_SCI,
    ~(uint32_t)KUI_STORAGE_SCI};

static void advance(uint32_t delta) {
    sim_ticks+=delta;
    assert(sim_ticks-clock_origin<UINT64_C(12500000)*1000u);
}
void cdda_harness_host_clock_init(void) {
#if CDDA_TEST_PROFILE == 4
    /* Calibration samples must bracket an actual32-bit hardware wrap. */
    sim_ticks=UINT32_MAX-UINT64_C(12500000)*10u;
#else
    sim_ticks=0;
#endif
    clock_origin=sim_ticks;
}
uint32_t cdda_harness_host_ticks(void) {advance(250u);return (uint32_t)sim_ticks;}
static uint32_t hardware_frame(void) {
    uint32_t scale=adjusted_aica_rate?10022u:10000u;
    return (uint32_t)((sim_ticks-key_tick)*44100u*scale/(UINT64_C(12500000)*10000u));
}
static uint32_t decode_frame(unsigned position) {
    return (uint32_t)(uint16_t)samples_left[position] |
        ((uint32_t)(uint16_t)samples_right[position]<<16);
}
static uint32_t source_frame(const struct playback_record *p,uint32_t elapsed) {
    uint32_t span=p->end-p->first;
    if(p->repeat) return p->loop_first+
        (p->first-p->loop_first+elapsed)%(p->end-p->loop_first);
    return elapsed<span?p->first+elapsed:0u;
}
#if CDDA_TEST_PROFILE == 5
enum kui_cdda_control_result harness_control_status(
        const struct kui_cdda_control *control,struct kui_cdda_control_status *out) {
    enum kui_cdda_control_result result=kui_cdda_control_status(control,out);
    if(result!=KUI_CDDA_CONTROL_OK) return result;
    assert(control==&command_control);model_status_checks++;
    if(out->state==KUI_CDDA_CONTROL_PLAYING && !out->pending) {
        assert(running && command_running && starts);
        /* The adapter credits its checked key-on latch immediately after a
         * matching completion; pure snapshots and synced STATUS both report
         * the last actual hardware observation, never queued PCM. */
        assert(out->frame==source_frame(&records[starts-1u],last_observed_elapsed));
    }
    if(out->state==KUI_CDDA_CONTROL_PAUSED) {
        assert(!running && !command_running);model_paused_statuses++;
    }
    if(out->state==KUI_CDDA_CONTROL_EOF) {
        assert(!running && !command_running && starts);
        assert(out->frame==records[starts-1u].end);model_eof_statuses++;
    }
    return result;
}
enum kui_cdda_control_result harness_control_observe(struct kui_cdda_control *control,
        uint32_t epoch,uint32_t played) {
    enum kui_cdda_control_result result=kui_cdda_control_observe(control,epoch,played);
    if(result==KUI_CDDA_CONTROL_OK || result==KUI_CDDA_CONTROL_ENDED) {
        assert(control==&command_control && running && command_running && starts);
        assert(played==last_observed_elapsed);model_played_observations++;
        uint32_t expected=result==KUI_CDDA_CONTROL_ENDED?records[starts-1u].end:
            source_frame(&records[starts-1u],last_observed_elapsed);
        assert(control->current.frame==expected);
    }
    return result;
}
enum kui_cdda_control_result harness_control_request(struct kui_cdda_control *control,
        const struct kui_cdda_control_request *request,struct kui_cdda_control_action *out) {
    bool was_running=running;unsigned prior_starts=starts,prior_stops=stops;
    enum kui_cdda_control_result result=kui_cdda_control_request(control,request,out);
    if(result==KUI_CDDA_CONTROL_INVALID && request->command==KUI_CDDA_CONTROL_SEEK) {
        assert(was_running && running && starts==prior_starts && stops==prior_stops);
        model_invalid_active_seeks++;
    }
    return result;
}
#endif
static void check_current_sample(uint32_t elapsed) {
    const struct playback_record *p=&records[starts-1u];
    uint32_t wanted=source_frame(p,elapsed);
    unsigned at=elapsed%KUI_CDDA_RING_FRAMES;
    if(decode_frame(at)!=wanted) {
        fprintf(stderr,"profile%d bad ring sample at%u elapsed%u wanted%u got%u\n",
            CDDA_TEST_PROFILE,at,elapsed,wanted,decode_frame(at));
        abort();
    }
}
enum kui_cdda_aica_result kui_cdda_aica_init(void) {
    assert(!running);return KUI_CDDA_AICA_OK;
}
enum kui_cdda_aica_result kui_cdda_aica_start(void) {
    assert(!running && audio_open && starts<MAX_STARTS && !injected);
    struct playback_record *p=&records[starts++];
    p->first=decode_frame(0);p->end=segment_end;p->start_tick=sim_ticks;
    p->loop_first=p->first;
#if CDDA_TEST_PROFILE > 0
    p->repeat=repeat_audio;
    p->loop_first=loop_first;
    if(!storage_kind) assert(p->first==session_first);
#endif
    p->prefetch=pcm.position;
    key_tick=sim_ticks;last_observed_elapsed=0;running=true;return KUI_CDDA_AICA_OK;
}
enum kui_cdda_aica_result kui_cdda_aica_stop(void) {
    if(running) {
        struct playback_record *p=&records[starts-1u];
        p->stop_tick=sim_ticks;p->prefetch=pcm.position;
        /* Independent observed hardware cursor, not the core's ring.played. */
        p->stop_cursor=p->repeat?source_frame(p,last_observed_elapsed):
            p->first+last_observed_elapsed;
        stops++;
    }
    running=false;return KUI_CDDA_AICA_OK;
}
enum kui_cdda_aica_result kui_cdda_aica_position(uint32_t *frame) {
    assert(frame && running);
#if CDDA_TEST_PROFILE == 4
    /* Position reads cost250us–1ms, so endpoint timing brackets matter. */
    uint32_t before=(uint32_t)sim_ticks,read_cost;
    if(fault==TIMING_READ_DELAY && position_checks && !injected) {
        injected=true;read_cost=112500u; /* 9ms violates the8ms endpoint limit. */
    } else read_cost=(position_checks%4u+1u)*3125u;
    /* Latch the hardware position at different points inside the read. The
     * remainder models bus/verification latency after the actual sample. */
    uint32_t pre_cost=read_cost*((position_checks/4u)%3u+1u)/4u;
    advance(pre_cost);last_observed_elapsed=hardware_frame();
    check_current_sample(last_observed_elapsed);advance(read_cost-pre_cost);
    last_endpoint=(struct model_observation){before,(uint32_t)sim_ticks+250u,
        last_observed_elapsed};
    if(!position_checks) initial_observed_frame=last_observed_elapsed;
    else if(position_checks==1u) first_endpoint=last_endpoint;
#else
    advance(3125u);last_observed_elapsed=hardware_frame();
    check_current_sample(last_observed_elapsed);
#endif
    position_checks++;
    /* Each observation checks an independently generated sample marker. */
    *frame=last_observed_elapsed%KUI_CDDA_RING_FRAMES;return KUI_CDDA_AICA_OK;
}
static bool writable(unsigned half) {
    if(!running) return true;
    unsigned at=hardware_frame()%KUI_CDDA_RING_FRAMES;
    return at/KUI_CDDA_HALF_FRAMES!=half &&
        KUI_CDDA_HALF_FRAMES-at%KUI_CDDA_HALF_FRAMES>32u;
}
enum kui_cdda_aica_result kui_cdda_aica_write_samples(unsigned half,
        unsigned offset,const int16_t *l,const int16_t *r,unsigned count) {
    assert(half<2 && count && count<=128 && offset+count<=KUI_CDDA_HALF_FRAMES);
    if(injected && fault!=DATA_STARVATION) post_fault_writes++;
    if(!writable(half)) {write_faults++;running=false;return KUI_CDDA_AICA_ACTIVE_HALF;}
    advance(125u);
    if(!writable(half)) {write_faults++;running=false;return KUI_CDDA_AICA_ACTIVE_HALF;}
    unsigned at=half*KUI_CDDA_HALF_FRAMES+offset;
    memcpy(samples_left+at,l,count*sizeof(*l));
    memcpy(samples_right+at,r,count*sizeof(*r));
    return KUI_CDDA_AICA_OK;
}
int cdda_storage_init(void) {storage_ready=true;return 0;}
int cdda_storage_open(const char *path,uint32_t *bytes) {
    assert(storage_ready && !audio_open && path && bytes);
    assert(!strcmp(path,"0:/KUI/tests/cdda/stereo.raw") ||
        !strcmp(path,"0:/KUI/tests/cdda/track14.raw"));
    storage_kind=strstr(path,"track14.raw")?1u:0u;
    *bytes=storage_kind?TRACK_BYTES:FIXTURE_BYTES;audio_open=true;return 0;
}
int cdda_storage_read_at(uint32_t offset,void *out,uint32_t bytes) {
    uint32_t size=storage_kind?TRACK_BYTES:FIXTURE_BYTES;
    assert(audio_open && out && bytes<=512u && offset<=size && bytes<=size-offset);
    assert(((uintptr_t)out&31u)==0);
    advance(5000u);pcm_reads++;blocks+=(bytes+511u)/512u;
    if(fault==PCM_FAULT && running && !injected) {injected=true;return -1;}
#if CDDA_TEST_PROFILE == 5
    if(fault==COMMAND_REPRIME_FAULT && starts==1u && !running && !injected) {
        assert(command_control.current.pending &&
            command_control.action.command==KUI_CDDA_CONTROL_SEEK);
        failed_command_epoch=command_control.action.epoch;injected=true;return -1;
    }
#endif
    uint8_t *destination=out;
    /* Stereo samples encode their absolute source frame in little endian,
     * allowing the AICA model to check playback continuity across loops. */
    for(uint32_t i=0;i<bytes;i++) {
        uint32_t at=offset+i,frame=at/4u;
        destination[i]=(uint8_t)(frame>>(8u*(at%4u)));
    }
    return 0;
}
void cdda_storage_close(void) {audio_open=false;}
int cdda_storage_data_open(uint32_t *bytes) {
    assert(storage_ready && !data_open);*bytes=STRESS_BYTES;data_open=true;return 0;
}
int cdda_storage_data_read_at(uint32_t offset,uint8_t *out,uint32_t bytes) {
    assert(data_open && out && offset<=STRESS_BYTES && bytes<=STRESS_BYTES-offset);
    /* A successful90ms job is safe by itself. Its learned2x budget cannot
     * fit inside a185.76ms half, so the workload-liveness guard must fail. */
    advance(fault==DATA_STARVATION?1125000u:25000u);
    data_reads++;blocks+=(bytes+511u)/512u;
    if(fault==DATA_STARVATION) injected=true;
    if(!injected && fault==DATA_DELAY) {injected=true;advance(2500000u);}
    if(!injected && fault==DATA_FAULT) {injected=true;return -1;}
    for(uint32_t i=0;i<bytes;i++) {
        uint32_t at=offset+i;
        uint32_t mixed=at^0x9e3779b9u;
        mixed^=mixed>>16;mixed*=0x7feb352du;
        mixed^=mixed>>15;mixed*=0x846ca68bu;mixed^=mixed>>16;
        out[i]=(uint8_t)mixed;
    }
    if(!injected && fault==DATA_CORRUPTION) {injected=true;out[bytes/2u]^=1u;}
    return 0;
}
void cdda_storage_data_close(void) {data_open=false;}
void cdda_storage_shutdown(void) {audio_open=data_open=storage_ready=false;}
uint32_t cdda_storage_blocks_read(void) {return blocks;}
const char *cdda_storage_last_failure(void) {return "injected host card read failure";}
void cdda_display_init(void) {}
void cdda_display_line(const char *text) {(void)text;}
void cdda_display_number(const char *label,uint32_t value) {(void)label;(void)value;}
void cdda_display_finish(unsigned result) {finished=true;reported_failures=result;}

int main(int argc,char **argv) {
    assert(argc==2);
    if(!strcmp(argv[1],"pcm-fail")) fault=PCM_FAULT;
    else if(!strcmp(argv[1],"data-fail")) fault=DATA_FAULT;
    else if(!strcmp(argv[1],"data-corrupt")) fault=DATA_CORRUPTION;
    else if(!strcmp(argv[1],"data-delay")) fault=DATA_DELAY;
    else if(!strcmp(argv[1],"data-starvation")) fault=DATA_STARVATION;
    else if(!strcmp(argv[1],"rate-offset")) adjusted_aica_rate=true;
    else if(!strcmp(argv[1],"timing-read-delay")) fault=TIMING_READ_DELAY;
    else if(!strcmp(argv[1],"command-reprime-fail")) fault=COMMAND_REPRIME_FAULT;
    else assert(!strcmp(argv[1],"pass"));
    cdda_main();
    assert(finished && !running && !audio_open && !data_open && !storage_ready);
    assert(!write_faults && !post_fault_writes && starts==stops);
    assert(pcm_reads && position_checks);
    if(fault!=NO_FAULT) {
        assert(injected && failures==1u && reported_failures==1u);
        if(fault!=COMMAND_REPRIME_FAULT) assert(!completed);
#if CDDA_TEST_PROFILE == 3
        if(fault==DATA_FAULT || fault==DATA_CORRUPTION) assert(data_errors==1u);
        if(fault!=PCM_FAULT && fault!=DATA_STARVATION)
            assert(data_reads==1u && !data_checked);
        if(fault==DATA_STARVATION) {
            assert(data_reads==1u && data_checked==2048u && !data_errors);
            assert(sim_ticks-data_last_tick>=5u*KUI_CDDA_CLOCK_HZ);
            assert(sim_ticks-data_last_tick<5u*KUI_CDDA_CLOCK_HZ+12500u);
            assert(profile_clock.seconds<6u);
        }
#endif
#if CDDA_TEST_PROFILE == 4
        assert(!timing_valid && !timing_result.played_frames);
#elif CDDA_TEST_PROFILE == 5
        assert(command_control.current.state==KUI_CDDA_CONTROL_FAULT);
        assert(!command_control.current.pending && !command_running);
        if(fault==COMMAND_REPRIME_FAULT) {
            assert(completed==1u && starts==1u && failed_command_epoch);
            assert(command_control.current.epoch==failed_command_epoch);
        }
#endif
    } else {
        assert(!injected && !failures && !reported_failures);
#if CDDA_TEST_PROFILE == 0
        assert(completed==5u && starts==5u);
        const uint32_t expected[]={0u,300001u,300001u,0u,1761648u};
        for(unsigned i=0;i<5;i++) assert(records[i].first==expected[i]);
        assert(records[3].end==TRACK_BYTES/4u);
#elif CDDA_TEST_PROFILE == 1
        assert(completed==5u && starts==8u && recovered_deadlines==1u);
        assert(expected_deadline_ticks>=2375000u);
        assert(records[3].first==records[2].stop_cursor);
        assert(records[3].first!=records[2].prefetch);
        assert(records[3].start_tick-records[2].stop_tick>=KUI_CDDA_CLOCK_HZ);
        assert(records[7].first==TONE_FIRST && !records[7].repeat);
        assert(records[5].first==1761648u && records[5].end==TRACK_BYTES/4u);
#elif CDDA_TEST_PROFILE == 2 || CDDA_TEST_PROFILE == 3
        assert(completed==1u && starts==1u);
        assert(profile_clock.seconds>=900u && profile_clock.wraps>=2u && loop_count>=900u);
        assert(sim_ticks>=UINT64_C(12500000)*900u);
#if CDDA_TEST_PROFILE == 3
        assert(data_checked>=STRESS_BYTES && !data_errors && data_reads>=STRESS_BYTES/2048u);
#endif
#elif CDDA_TEST_PROFILE == 4
        assert(completed==1u && starts==1u && timing_valid);
        assert(timing_start.before_tick==first_endpoint.before &&
            timing_start.after_tick==first_endpoint.after &&
            timing_start.played_frames==first_endpoint.frame-initial_observed_frame);
        assert(timing_end.before_tick==last_endpoint.before &&
            timing_end.after_tick==last_endpoint.after &&
            timing_end.played_frames==last_endpoint.frame-initial_observed_frame);
        assert(timing_end.before_tick<timing_start.before_tick);
        assert(profile_clock.wraps==1u);
        assert(timing_result.lower_ticks>=750000000u);
        assert(timing_result.upper_ticks>timing_result.lower_ticks);
        assert(timing_result.start_read_ticks>=3125u && timing_result.end_read_ticks>=3125u);
        assert(timing_result.upper_ticks-timing_result.lower_ticks==
            timing_result.start_read_ticks+timing_result.end_read_ticks);
        /* Known hardware sample rate must fit the matched raw-tick interval.
         * These deliberately wide endpoint read windows also contain the
         * uncertainty from integer quantization of the two sample counters. */
        uint64_t frame_scaled=(uint64_t)timing_result.played_frames*12500000u*10000u;
        uint64_t known_rate=UINT64_C(44100)*(adjusted_aica_rate?10022u:10000u);
        assert(known_rate*timing_result.lower_ticks<=frame_scaled);
        assert(known_rate*timing_result.upper_ticks>=frame_scaled);
        assert(!data_reads);
#elif CDDA_TEST_PROFILE == 5
        assert(completed==7u && starts==15u);
        assert(command_expected_refusals==2u && command_stale_refusals==4u);
        assert(command_max_loops>=4u && command_status_checks>=10u);
        assert(model_status_checks>=command_status_checks && model_paused_statuses>=3u);
        assert(model_played_observations>=command_status_checks);
        assert(model_eof_statuses>=2u && model_invalid_active_seeks==1u);
        assert(command_control.current.state==KUI_CDDA_CONTROL_STOPPED &&
            !command_control.current.pending && !command_running);
        assert(records[2].first==command_paused_frame &&
            records[2].first==records[1].stop_cursor &&
            records[2].first!=records[1].prefetch);
        assert(records[2].start_tick-records[1].stop_tick>=KUI_CDDA_CLOCK_HZ);
        assert(records[3].first==TONE_FIRST);
        assert(records[6].repeat && records[7].repeat && records[8].repeat);
        assert(records[6].loop_first==TONE_FIRST && records[7].loop_first==TONE_FIRST &&
            records[8].loop_first==TONE_FIRST);
        assert(!data_reads);
        /* Exercise the actual adapter with a canceled saved action while a
         * newer action is pending. Refusal must happen before any hardware or
         * card work, even though the stale PLAY would require re-priming. */
        struct kui_cdda_control saved_control=command_control;
        struct kui_cdda_control_request request={KUI_CDDA_CONTROL_PLAY,
            0u,3u*44100u,0u,false};
        struct kui_cdda_control_action canceled,newer;
        assert(kui_cdda_control_request(&command_control,&request,&canceled)==KUI_CDDA_CONTROL_OK);
        request.first=TONE_FIRST;request.end=TONE_FIRST+TONE_FRAMES;
        assert(kui_cdda_control_request(&command_control,&request,&newer)==KUI_CDDA_CONTROL_OK);
        assert(canceled.pending && newer.pending && newer.epoch>canceled.epoch);
        struct kui_cdda_control_status before=command_control.current;
        unsigned prior_starts=starts,prior_stops=stops,prior_positions=position_checks;
        uint32_t prior_reads=pcm_reads,prior_completions=command_completions;
        assert(!command_finish(canceled));
        assert(command_same_status(&before,&command_control.current));
        assert(starts==prior_starts && stops==prior_stops && position_checks==prior_positions &&
            pcm_reads==prior_reads && command_completions==prior_completions);
        assert(!running && !audio_open && !command_running);
        command_control=saved_control;
#endif
    }
    printf("profile%d %s: stages=%u failures=%u starts=%u position_checks=%u card_blocks=%u",
        CDDA_TEST_PROFILE,argv[1],completed,failures,starts,position_checks,blocks);
#if CDDA_TEST_PROFILE > 0
    printf(" seconds=%u wraps=%u loops=%u expected_recoveries=%u",
        profile_clock.seconds,profile_clock.wraps,loop_count,recovered_deadlines);
#if CDDA_TEST_PROFILE == 3
    printf(" checked_data=%u data_errors=%u",data_checked,data_errors);
#elif CDDA_TEST_PROFILE == 4
    printf(" paired_frames=%u ticks=%u..%u read_ticks=%u,%u relative_rate_ppm=%u",
        timing_result.played_frames,timing_result.lower_ticks,timing_result.upper_ticks,
        timing_result.start_read_ticks,timing_result.end_read_ticks,
        adjusted_aica_rate?1002200u:1000000u);
#elif CDDA_TEST_PROFILE == 5
    printf(" commands=%u completions=%u status_checks=%u model_status_checks=%u"
        " expected_refusals=%u stale_refusals=%u elapsed_ms=%llu",
        command_accepted,command_completions,command_status_checks,model_status_checks,
        command_expected_refusals,command_stale_refusals,
        (unsigned long long)((sim_ticks-clock_origin)/12500u));
#endif
#endif
    puts("");return 0;
}
