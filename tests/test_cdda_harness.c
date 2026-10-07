/* SPDX-License-Identifier: GPL-3.0-only */
/* Integration simulation of the actual homebrew profile orchestrator. Real
 * PCM, ring and stream code runs against deterministic card/AICA models. This
 * checks control flow and buffer contents, not audible quality or hardware. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CDDA_HARNESS_HOST_TEST 1
#include "../src/loader/cdda_main.c"

#define FIXTURE_BYTES (529200u*4u)
#define TRACK_BYTES 7222992u
#define STRESS_BYTES (8u*1024u*1024u)
#define MAX_STARTS 16u
enum fault_kind { NO_FAULT, PCM_FAULT, DATA_FAULT, DATA_CORRUPTION, DATA_DELAY,
    DATA_STARVATION };
static enum fault_kind fault;
static uint64_t sim_ticks,key_tick;
static bool running,storage_ready,audio_open,data_open,injected,finished;
static unsigned storage_kind,starts,stops,position_checks,write_faults;
static unsigned post_fault_writes;
static uint32_t pcm_reads,data_reads,blocks,reported_failures,last_observed_elapsed;
static int16_t samples_left[KUI_CDDA_RING_FRAMES];
static int16_t samples_right[KUI_CDDA_RING_FRAMES];
struct playback_record {
    uint32_t first,end,stop_cursor,prefetch;
    uint64_t start_tick,stop_tick;
    bool repeat;
};
static struct playback_record records[MAX_STARTS];
volatile struct kui_storage_boot_marker cdda_storage_boot_marker={
    KUI_STORAGE_BOOT_MAGIC1,KUI_STORAGE_BOOT_MAGIC2,1u,KUI_STORAGE_SCI,
    ~(uint32_t)KUI_STORAGE_SCI};

static void advance(uint32_t delta) {
    sim_ticks+=delta;
    assert(sim_ticks<UINT64_C(12500000)*1000u);
}
void cdda_harness_host_clock_init(void) {sim_ticks=0;}
uint32_t cdda_harness_host_ticks(void) {advance(250u);return (uint32_t)sim_ticks;}
static uint32_t hardware_frame(void) {
    return (uint32_t)((sim_ticks-key_tick)*44100u/12500000u);
}
static uint32_t decode_frame(unsigned position) {
    return (uint32_t)(uint16_t)samples_left[position] |
        ((uint32_t)(uint16_t)samples_right[position]<<16);
}
static void check_current_sample(void) {
    const struct playback_record *p=&records[starts-1u];
    uint32_t elapsed=hardware_frame(),span=p->end-p->first;
    uint32_t wanted=p->repeat?p->first+elapsed%span:
        elapsed<span?p->first+elapsed:0u;
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
#if CDDA_TEST_PROFILE > 0
    p->repeat=repeat_audio;
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
        p->stop_cursor=p->first+last_observed_elapsed;
        stops++;
    }
    running=false;return KUI_CDDA_AICA_OK;
}
enum kui_cdda_aica_result kui_cdda_aica_position(uint32_t *frame) {
    assert(frame && running);advance(3125u);position_checks++;
    /* Each observation checks an independently generated sample marker. */
    check_current_sample();last_observed_elapsed=hardware_frame();
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
    else assert(!strcmp(argv[1],"pass"));
    cdda_main();
    assert(finished && !running && !audio_open && !data_open && !storage_ready);
    assert(!write_faults && !post_fault_writes && starts==stops);
    assert(pcm_reads && position_checks);
    if(fault!=NO_FAULT) {
        assert(injected && failures==1u && reported_failures==1u && !completed);
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
#else
        assert(completed==1u && starts==1u);
        assert(profile_clock.seconds>=900u && profile_clock.wraps>=2u && loop_count>=900u);
        assert(sim_ticks>=UINT64_C(12500000)*900u);
#if CDDA_TEST_PROFILE == 3
        assert(data_checked>=STRESS_BYTES && !data_errors && data_reads>=STRESS_BYTES/2048u);
#endif
#endif
    }
    printf("profile%d %s: stages=%u failures=%u starts=%u position_checks=%u card_blocks=%u",
        CDDA_TEST_PROFILE,argv[1],completed,failures,starts,position_checks,blocks);
#if CDDA_TEST_PROFILE > 0
    printf(" seconds=%u wraps=%u loops=%u expected_recoveries=%u",
        profile_clock.seconds,profile_clock.wraps,loop_count,recovered_deadlines);
#if CDDA_TEST_PROFILE == 3
    printf(" checked_data=%u data_errors=%u",data_checked,data_errors);
#endif
#endif
    puts("");return 0;
}
