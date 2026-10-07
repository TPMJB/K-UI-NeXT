/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual cooperative engine/client integration against independent generated
 * storage and AICA models. Native bridge calls preserve ordinary host C
 * semantics only: this does not establish SH register or stack isolation. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CDDA_HARNESS_HOST_TEST 1
#define kui_cdda_control_status(...) model_control_status(__VA_ARGS__)
#define kui_cdda_control_observe(...) model_control_observe(__VA_ARGS__)
#define kui_cdda_control_fail(...) model_control_fail(__VA_ARGS__)
#define kui_cdda_service_start(...) model_service_start(__VA_ARGS__)
#define kui_cdda_service_enter(...) model_service_enter(__VA_ARGS__)
#define kui_cdda_service_leave(...) model_service_leave(__VA_ARGS__)
#include "../src/loader/cdda_main.c"
#undef kui_cdda_control_status
#undef kui_cdda_control_observe
#undef kui_cdda_control_fail
#undef kui_cdda_service_start
#undef kui_cdda_service_enter
#undef kui_cdda_service_leave
extern enum kui_cdda_control_result kui_cdda_control_status(
    const struct kui_cdda_control *,struct kui_cdda_control_status *);
extern enum kui_cdda_control_result kui_cdda_control_observe(
    struct kui_cdda_control *,uint32_t,uint32_t);
extern enum kui_cdda_control_result kui_cdda_control_fail(struct kui_cdda_control *,uint32_t);
extern enum kui_cdda_service_result kui_cdda_service_start(struct kui_cdda_service_guard *,
    uint32_t,uint32_t,uint32_t *);
extern enum kui_cdda_service_result kui_cdda_service_enter(struct kui_cdda_service_guard *,
    uint32_t,uint32_t,struct kui_cdda_service_ticket *);
extern enum kui_cdda_service_result kui_cdda_service_leave(struct kui_cdda_service_guard *,
    const struct kui_cdda_service_ticket *,uint32_t,bool);

#define FIXTURE_BYTES (529200u*4u)
#define DATA_BYTES (8u*1024u*1024u)
#define MAX_STARTS 64u
enum fault_kind { NO_FAULT, PCM_FAULT, DATA_FAULT, DATA_CORRUPTION,
    DATA_DELAY90, DATA_DELAY200, REPRIME_FAULT, CLIENT_CLOCK_STALL };
static enum fault_kind fault;
static uint64_t sim_ticks,key_tick,clock_origin;
static bool running,storage_ready,audio_open,data_open,injected,finished,terminal_fault;
static unsigned starts,stops,position_reads,hardware_stop_calls,hardware_write_calls;
static unsigned write_faults,post_fault_writes,clock_reads,pcm_reads,data_reads,blocks;
static unsigned data_open_calls,data_close_calls,reported_failures,model_statuses;
static unsigned model_observations;
static uint32_t last_observed_elapsed,failed_command_epoch;
static uint32_t model_bytes_read;
static struct kui_cdda_service_guard *model_guard;
static uint32_t model_epoch,model_lease_tick,model_gap,model_call;
static unsigned model_starts,model_enters,model_leaves,model_deadlines;
static bool model_busy;
static const struct cdda_client_exports *original_exports;
static struct cdda_client_exports wrapped_exports;
static unsigned bridge_depth,client_handoffs,worker_handoffs,probe_calls,client_clock_reads;
static unsigned model_ops[CDDA_CLIENT_REPORT+1u],model_stale_exports,model_reentries;
static unsigned model_expected_gaps,model_recoveries,stack_initializations,stack_checks,context_reads;
static uint32_t model_paused_frame,frozen_client_clock;
static uint64_t model_pause_tick,model_arm_tick;
static int16_t samples_left[KUI_CDDA_RING_FRAMES];
static int16_t samples_right[KUI_CDDA_RING_FRAMES];
struct playback_record {
    uint32_t first,end,loop_first,stop_cursor,prefetch;
    uint64_t start_tick,stop_tick;
    bool repeat;
};
static struct playback_record records[MAX_STARTS];
volatile struct kui_storage_boot_marker cdda_storage_boot_marker={
    KUI_STORAGE_BOOT_MAGIC1,KUI_STORAGE_BOOT_MAGIC2,1u,KUI_STORAGE_SCI,
    ~(uint32_t)KUI_STORAGE_SCI};

/* Independent64-bit conversions and cursor generation. Real production
 * fixed-width conversions and ring deadline logic remain linked below. */
static uint32_t model_us_ticks(uint32_t microseconds) {
    return (uint32_t)(((uint64_t)KUI_CDDA_TMU_HZ*microseconds+999999u)/1000000u);
}
static void advance(uint32_t ticks) {
    sim_ticks+=ticks;
    assert(sim_ticks-clock_origin<(uint64_t)KUI_CDDA_TMU_HZ*120u);
}
void cdda_harness_host_clock_init(void) {
    sim_ticks=UINT32_MAX-(uint64_t)KUI_CDDA_TMU_HZ*30u;clock_origin=sim_ticks;
}
uint32_t cdda_harness_host_ticks(void) {
    clock_reads++;advance(model_us_ticks(20u));return (uint32_t)sim_ticks;
}
static uint32_t hardware_frame(void) {
    return (uint32_t)((sim_ticks-key_tick)*44100u/KUI_CDDA_TMU_HZ);
}
static uint32_t decode_frame(unsigned position) {
    return (uint32_t)(uint16_t)samples_left[position]|
        ((uint32_t)(uint16_t)samples_right[position]<<16);
}
static uint32_t source_frame(const struct playback_record *p,uint32_t elapsed) {
    if(p->repeat) return p->loop_first+
        (p->first-p->loop_first+elapsed)%(p->end-p->loop_first);
    return elapsed<p->end-p->first?p->first+elapsed:0u;
}
enum kui_cdda_service_result model_service_start(struct kui_cdda_service_guard *guard,
    uint32_t now,uint32_t gap,uint32_t *epoch) {
    /* A fresh key-on may precede the cooperative lease anchor. The old
     * session must have been stopped and the synchronous owner quiesced. */
    assert(!model_busy && running && starts && stops+1u==starts);
    assert(guard->state==KUI_CDDA_SERVICE_IDLE || guard->state==KUI_CDDA_SERVICE_STOPPED ||
        guard->state==KUI_CDDA_SERVICE_FAULT);
    enum kui_cdda_service_result result=kui_cdda_service_start(guard,now,gap,epoch);
    if(result==KUI_CDDA_SERVICE_OK) {
        assert(*epoch>model_epoch && guard->state==KUI_CDDA_SERVICE_ACTIVE);
        model_guard=guard;model_epoch=*epoch;model_lease_tick=now;model_gap=gap;model_starts++;
    }
    return result;
}
enum kui_cdda_service_result model_service_enter(struct kui_cdda_service_guard *guard,
    uint32_t epoch,uint32_t now,struct kui_cdda_service_ticket *ticket) {
    assert(guard==model_guard);
    struct kui_cdda_service_guard before=*guard;
    enum kui_cdda_service_result result=kui_cdda_service_enter(guard,epoch,now,ticket);
    if(result==KUI_CDDA_SERVICE_OK) {
        assert(!model_busy && epoch==model_epoch && now-model_lease_tick<model_gap);
        assert(ticket->epoch==model_epoch && ticket->call>model_call);
        model_call=ticket->call;model_busy=true;model_enters++;
        assert(guard->last_tick==model_lease_tick);
    } else if(result==KUI_CDDA_SERVICE_STALE || result==KUI_CDDA_SERVICE_BUSY_RESULT) {
        assert(!memcmp(&before,guard,sizeof(before)));
    } else if(result==KUI_CDDA_SERVICE_DEADLINE) {
        assert(now-model_lease_tick>=model_gap);model_deadlines++;
    }
    return result;
}
enum kui_cdda_service_result model_service_leave(struct kui_cdda_service_guard *guard,
    const struct kui_cdda_service_ticket *ticket,uint32_t now,bool success) {
    assert(guard==model_guard && model_busy && ticket->epoch==model_epoch && ticket->call==model_call);
    enum kui_cdda_service_result result=kui_cdda_service_leave(guard,ticket,now,success);
    model_busy=false;model_leaves++;
    if(result==KUI_CDDA_SERVICE_OK) {
        assert(success && now-model_lease_tick<model_gap);
        model_lease_tick=now;
    } else {
        assert(guard->state==KUI_CDDA_SERVICE_FAULT && !guard->pending.call);
        if(result==KUI_CDDA_SERVICE_DEADLINE) {
            assert(now-model_lease_tick>=model_gap);model_deadlines++;
        } else assert(result==KUI_CDDA_SERVICE_IO && !success);
        if(injected) terminal_fault=true;
    }
    return result;
}
static void check_current_sample(uint32_t elapsed) {
    const struct playback_record *p=&records[starts-1u];
    uint32_t expected=source_frame(p,elapsed),actual=decode_frame(elapsed%KUI_CDDA_RING_FRAMES);
    if(actual!=expected) {
        fprintf(stderr,"service bad ring sample elapsed%u expected%u actual%u\n",elapsed,expected,actual);
        abort();
    }
}
enum kui_cdda_control_result model_control_status(
    const struct kui_cdda_control *control,struct kui_cdda_control_status *out) {
    enum kui_cdda_control_result result=kui_cdda_control_status(control,out);
    if(result!=KUI_CDDA_CONTROL_OK) return result;
    assert(control==&command_control);model_statuses++;
    if(out->state==KUI_CDDA_CONTROL_PLAYING && !out->pending) {
        assert(running && command_running && starts);
        assert(out->frame==source_frame(&records[starts-1u],last_observed_elapsed));
    }
    if(out->state==KUI_CDDA_CONTROL_PAUSED) assert(!running && !command_running);
    return result;
}
enum kui_cdda_control_result model_control_observe(struct kui_cdda_control *control,
    uint32_t epoch,uint32_t played) {
    enum kui_cdda_control_result result=kui_cdda_control_observe(control,epoch,played);
    if(result==KUI_CDDA_CONTROL_OK || result==KUI_CDDA_CONTROL_ENDED) {
        assert(control==&command_control && running && starts);
        assert(played==last_observed_elapsed);model_observations++;
        assert(control->current.frame==(result==KUI_CDDA_CONTROL_ENDED?
            records[starts-1u].end:source_frame(&records[starts-1u],last_observed_elapsed)));
    }
    return result;
}
enum kui_cdda_control_result model_control_fail(struct kui_cdda_control *control,uint32_t epoch) {
    enum kui_cdda_control_result result=kui_cdda_control_fail(control,epoch);
    /* The intended190ms omission is followed by an explicitly fresh session.
     * Injected I/O/model failures must remain stopped through final cleanup. */
    if(result==KUI_CDDA_CONTROL_IO && injected && service_errors) terminal_fault=true;
    return result;
}
enum kui_cdda_aica_result kui_cdda_aica_init(void) {
    assert(!running);return KUI_CDDA_AICA_OK;
}
enum kui_cdda_aica_result kui_cdda_aica_start(void) {
    assert(!running && audio_open && starts<MAX_STARTS && !terminal_fault);
    struct playback_record *p=&records[starts++];
    p->first=decode_frame(0);p->end=segment_end;p->loop_first=loop_first;p->repeat=repeat_audio;
    p->start_tick=sim_ticks;p->prefetch=pcm.position;
    assert(p->first==session_first);
    key_tick=sim_ticks;last_observed_elapsed=0;running=true;return KUI_CDDA_AICA_OK;
}
enum kui_cdda_aica_result kui_cdda_aica_stop(void) {
    hardware_stop_calls++;
    if(running) {
        struct playback_record *p=&records[starts-1u];
        p->stop_tick=sim_ticks;p->prefetch=pcm.position;
        p->stop_cursor=p->repeat?source_frame(p,last_observed_elapsed):p->first+last_observed_elapsed;
        stops++;
    }
    running=false;return KUI_CDDA_AICA_OK;
}
enum kui_cdda_aica_result kui_cdda_aica_position(uint32_t *frame) {
    assert(frame && running);advance(model_us_ticks(250u));last_observed_elapsed=hardware_frame();
    if(!injected || fault==CLIENT_CLOCK_STALL) check_current_sample(last_observed_elapsed);
    position_reads++;
    *frame=last_observed_elapsed%KUI_CDDA_RING_FRAMES;return KUI_CDDA_AICA_OK;
}
static bool writable(unsigned half) {
    if(!running) return true;
    unsigned at=hardware_frame()%KUI_CDDA_RING_FRAMES;
    return at/KUI_CDDA_HALF_FRAMES!=half && KUI_CDDA_HALF_FRAMES-at%KUI_CDDA_HALF_FRAMES>32u;
}
enum kui_cdda_aica_result kui_cdda_aica_write_samples(unsigned half,unsigned offset,
    const int16_t *l,const int16_t *r,unsigned count) {
    hardware_write_calls++;
    assert(half<2u && count && count<=128u && offset+count<=KUI_CDDA_HALF_FRAMES);
    if((injected && fault!=CLIENT_CLOCK_STALL && fault!=DATA_DELAY90) || terminal_fault) post_fault_writes++;
    if(!writable(half)) {write_faults++;running=false;return KUI_CDDA_AICA_ACTIVE_HALF;}
    advance(model_us_ticks(10u));
    if(!writable(half)) {write_faults++;running=false;return KUI_CDDA_AICA_ACTIVE_HALF;}
    unsigned at=half*KUI_CDDA_HALF_FRAMES+offset;
    memcpy(samples_left+at,l,count*sizeof(*l));memcpy(samples_right+at,r,count*sizeof(*r));
    return KUI_CDDA_AICA_OK;
}
int cdda_storage_init(void) {storage_ready=true;return 0;}
int cdda_storage_open(const char *path,uint32_t *bytes) {
    assert(storage_ready && !audio_open && path && bytes);
    assert(!strcmp(path,"0:/KUI/tests/cdda/stereo.raw"));
    *bytes=FIXTURE_BYTES;audio_open=true;return 0;
}
int cdda_storage_read_at(uint32_t offset,void *out,uint32_t bytes) {
    assert(audio_open && out && bytes<=512u && offset<=FIXTURE_BYTES && bytes<=FIXTURE_BYTES-offset);
    assert(((uintptr_t)out&31u)==0u);
    advance(model_us_ticks(400u));pcm_reads++;blocks+=(bytes+511u)/512u;
    if(fault==PCM_FAULT && running && !injected) {injected=true;return -1;}
    if(fault==REPRIME_FAULT && starts==1u && !running && !injected) {
        assert(command_control.current.pending);
        failed_command_epoch=command_control.current.pending_epoch;injected=true;return -1;
    }
    uint8_t *destination=out;
    for(uint32_t i=0;i<bytes;i++) {
        uint32_t at=offset+i,frame=at/4u;destination[i]=(uint8_t)(frame>>(8u*(at%4u)));
    }
    return 0;
}
void cdda_storage_close(void) {audio_open=false;}
int cdda_storage_data_open(uint32_t *bytes) {
    assert(storage_ready && !data_open);*bytes=DATA_BYTES;data_open=true;data_open_calls++;return 0;
}
int cdda_storage_data_read_at(uint32_t offset,uint8_t *out,uint32_t bytes) {
    assert(data_open && out && bytes && bytes<=2048u && offset<=DATA_BYTES && bytes<=DATA_BYTES-offset);
    /* Ranges are advertised by the actual engine's portable job claim. They
     * may be noncontiguous; independently validate each exact physical read. */
    assert(service_job.state==KUI_CDDA_JOB_READING && service_job.pending.offset==offset &&
        service_job.pending.bytes==bytes && service_job.pending.session_epoch==model_epoch);
    advance(model_us_ticks(2000u));data_reads++;blocks+=(bytes+511u)/512u;
    if(!injected && (fault==DATA_DELAY90 || fault==DATA_DELAY200)) {
        injected=true;advance(model_us_ticks(fault==DATA_DELAY90?90000u:200000u));
    }
    if(!injected && fault==DATA_FAULT) {injected=true;return -1;}
    for(uint32_t i=0;i<bytes;i++) {
        uint32_t x=(offset+i)^0x9e3779b9u;
        x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;out[i]=(uint8_t)x;
    }
    if(!injected && fault==DATA_CORRUPTION) {injected=true;out[bytes/2u]^=1u;}
    if(!injected) {
        model_bytes_read+=bytes;
    }
    return 0;
}
void cdda_storage_data_close(void) {data_close_calls++;data_open=false;}
void cdda_storage_shutdown(void) {audio_open=data_open=storage_ready=false;}
uint32_t cdda_storage_blocks_read(void) {return blocks;}
const char *cdda_storage_last_failure(void) {return "injected host card read failure";}
void cdda_display_init(void) {}
void cdda_display_line(const char *text) {(void)text;}
void cdda_display_number(const char *label,uint32_t value) {(void)label;(void)value;}
void cdda_display_finish(unsigned result) {finished=true;reported_failures=result;}

struct effects {
    uint64_t tick;
    unsigned clocks,positions,reads,data,writes,keyons,keyoffs,calls;
    struct kui_cdda_service_guard guard;
    struct kui_cdda_control control;
    struct kui_cdda_job job;
    bool audio,card_audio,card_data,native_busy;
};
static struct effects capture(void) {
    return (struct effects){sim_ticks,clock_reads,position_reads,pcm_reads,data_reads,
        hardware_write_calls,starts,hardware_stop_calls,service_calls,service_guard,
        command_control,service_job,running,audio_open,data_open,service_native_busy};
}
static void assert_unchanged(const struct effects *before) {
    assert(sim_ticks==before->tick && clock_reads==before->clocks && position_reads==before->positions);
    assert(pcm_reads==before->reads && data_reads==before->data && hardware_write_calls==before->writes);
    assert(starts==before->keyons && hardware_stop_calls==before->keyoffs && service_calls==before->calls);
    assert(!memcmp(&service_guard,&before->guard,sizeof(service_guard)));
    assert(!memcmp(&command_control,&before->control,sizeof(command_control)));
    assert(!memcmp(&service_job,&before->job,sizeof(service_job)));
    assert(running==before->audio && audio_open==before->card_audio && data_open==before->card_data);
    assert(service_native_busy==before->native_busy);
}
static uint32_t wrapped_call(void *context,struct cdda_client_request *request) {
    assert(original_exports && context==original_exports->context && bridge_depth==1u);
    assert(request && request->op<=CDDA_CLIENT_REPORT && request->pc==0x8c300100u &&
        request->sp==0x8c31ffe0u);
    uint32_t op=request->op,epoch=request->epoch,value=request->value;
    struct effects before=capture();model_ops[op]++;
    uint32_t result=original_exports->call(context,request);
    assert(result==request->result);
    if(epoch!=before.guard.generation) {
        assert(result==KUI_CDDA_SERVICE_STALE);assert_unchanged(&before);model_stale_exports++;
    } else if(result==KUI_CDDA_SERVICE_OK) {
        switch(op) {
        case CDDA_CLIENT_PAUSE:
            assert(before.audio && !running && !command_running && starts==before.keyons);
            model_paused_frame=command_control.current.frame;model_pause_tick=sim_ticks;
            assert(model_paused_frame==records[starts-1u].stop_cursor &&
                model_paused_frame!=records[starts-1u].prefetch);break;
        case CDDA_CLIENT_RESUME:
            assert(!before.audio && running && starts==before.keyons+1u);
            assert(records[starts-1u].first==model_paused_frame &&
                sim_ticks-model_pause_tick>=KUI_CDDA_TMU_HZ);break;
        case CDDA_CLIENT_SEEK:
            assert(running && starts==before.keyons+1u && records[starts-1u].first==value);break;
        case CDDA_CLIENT_STATUS:
            assert(request->state==value);
            if(value==KUI_CDDA_CONTROL_PLAYING)
                assert(request->frame==source_frame(&records[starts-1u],last_observed_elapsed));
            if(value==KUI_CDDA_CONTROL_PAUSED) assert(!running && request->frame==model_paused_frame);
            if(value==KUI_CDDA_CONTROL_STOPPED) assert(!running);
            break;
        case CDDA_CLIENT_STOP:
            assert(!running && !command_running && starts==before.keyons);break;
        case CDDA_CLIENT_ARM_GAP:model_arm_tick=sim_ticks;break;
        case CDDA_CLIENT_RESTART:
            assert(before.guard.state==KUI_CDDA_SERVICE_FAULT && !before.audio &&
                request->epoch>epoch && request->epoch==service_guard.generation);
            assert(running && starts==before.keyons+1u && records[starts-1u].first==TONE_FIRST);
            assert(command_control.current.frame==source_frame(&records[starts-1u],last_observed_elapsed));
            model_recoveries++;break;
        default:break;
        }
    } else if(result==KUI_CDDA_SERVICE_DEADLINE && fault==NO_FAULT) {
        assert(op==CDDA_CLIENT_SERVICE && model_arm_tick && sim_ticks-model_arm_tick>=model_us_ticks(190000u));
        assert(starts==before.keyons && position_reads==before.positions && pcm_reads==before.reads &&
            data_reads==before.data && hardware_write_calls==before.writes);
        assert(!running && !command_running && service_guard.state==KUI_CDDA_SERVICE_FAULT);
        model_expected_gaps++;
    }
    if(fault==NO_FAULT && result==KUI_CDDA_SERVICE_OK) assert(service_checked==model_bytes_read);
    return result;
}
static uint32_t wrapped_clock(void *context) {
    assert(original_exports && context==original_exports->context && bridge_depth==1u);
    uint32_t actual=original_exports->clock(context);client_clock_reads++;
    if(fault!=CLIENT_CLOCK_STALL) return actual;
    if(!injected) {injected=true;frozen_client_clock=actual;}
    return frozen_client_clock;
}
uint32_t kui_cdda_client_call(uint32_t (*entry)(const void *),const void *context,void *stack_top) {
    assert(entry);
    uint32_t destination=(uint32_t)(uintptr_t)stack_top;
    if(destination==0x8c320000u) {
        assert(!bridge_depth && entry==cdda_client_entry && !original_exports);
        original_exports=context;wrapped_exports=*original_exports;
        assert(wrapped_exports.magic==CDDA_CLIENT_MAGIC && wrapped_exports.bytes==sizeof(wrapped_exports));
        wrapped_exports.call=wrapped_call;wrapped_exports.clock=wrapped_clock;
        client_handoffs++;bridge_depth++;
        uint32_t result=entry(&wrapped_exports);
        bridge_depth--;return result;
    }
    assert(destination==0x8c230000u && bridge_depth<=1u);worker_handoffs++;
    if(entry==service_worker) {
        const struct cdda_client_request *request=context;
        assert(service_native_busy);
        assert(request->op==CDDA_CLIENT_RESTART || (model_busy && service_guard.state==KUI_CDDA_SERVICE_BUSY));
        struct effects before=capture();bridge_depth++;
        uint32_t result=entry(context);bridge_depth--;
        if(request->op==CDDA_CLIENT_REENTRY) {
            /* The worker itself reads the timer once and updates its clock;
             * its nested export must add no timer, hardware or card effects. */
            assert(result==KUI_CDDA_SERVICE_OK && clock_reads==before.clocks+1u &&
                position_reads==before.positions && pcm_reads==before.reads && data_reads==before.data &&
                hardware_write_calls==before.writes && starts==before.keyons && hardware_stop_calls==before.keyoffs);
            assert(!memcmp(&service_guard,&before.guard,sizeof(service_guard)));
            assert(!memcmp(&command_control,&before.control,sizeof(command_control)));
            model_reentries++;
        }
        return result;
    }
    assert(entry==service_fault_worker && !context);
    bridge_depth++;uint32_t result=entry(context);bridge_depth--;return result;
}
uint32_t kui_cdda_bridge_probe(uint32_t (*service)(void *),void *context) {
    assert(service && bridge_depth==1u);probe_calls++;(void)service(context);return 0u;
}
uint32_t cdda_host_client_sp(void) {assert(bridge_depth==1u);return 0x8c31ffe0u;}
uint32_t cdda_host_client_pc(void) {assert(bridge_depth==1u);return 0x8c300100u;}
uint32_t cdda_host_service_sp(void) {assert(bridge_depth==2u || bridge_depth==1u);return 0x8c22ffe0u;}
void cdda_host_service_context(uint32_t *out) {
    assert(!bridge_depth && out);context_reads++;
    /* Ordinary integer C comparisons may change T; the platform adapter must
     * compare owned configuration while ignoring this caller-clobbered bit. */
    out[0]=0x400000f0u|(context_reads&1u);out[1]=0u;out[2]=0u;
}
void cdda_host_service_stacks_init(void) {assert(!bridge_depth);stack_initializations++;}
bool cdda_host_service_stack_check(unsigned which,uint32_t *used) {
    assert(which<2u && used && !bridge_depth);*used=which?128u:2048u;stack_checks++;return true;
}
static void pure_adapter_refusals(void) {
    struct cdda_client_request request={.epoch=service_guard.generation,.op=CDDA_CLIENT_SERVICE,
        .pc=0x8c300100u,.sp=0x8c31ffe0u};
    struct effects before=capture();
    assert(service_export(NULL,&request)==KUI_CDDA_SERVICE_INVALID);assert_unchanged(&before);
    assert(service_export(&service_exports,NULL)==KUI_CDDA_SERVICE_INVALID);assert_unchanged(&before);
    for(unsigned i=0;i<5u;i++) {
        struct cdda_client_request malformed=request;
        switch(i) {
        case 0:malformed.pc=0x8c310000u;break;case 1:malformed.pc++;break;
        case 2:malformed.sp=0x8c220100u;break;case 3:malformed.sp++;break;
        default:malformed.op=CDDA_CLIENT_REPORT+1u;break;
        }
        assert(service_export(&service_exports,&malformed)==KUI_CDDA_SERVICE_INVALID);assert_unchanged(&before);
    }
    struct kui_cdda_service_guard saved_guard=service_guard;
    service_guard.state=KUI_CDDA_SERVICE_BUSY;
    service_guard.pending=(struct kui_cdda_service_ticket){service_guard.generation,service_guard.call_generation};
    before=capture();
    assert(service_export(&service_exports,&request)==KUI_CDDA_SERVICE_BUSY_RESULT);assert_unchanged(&before);
    request.op=CDDA_CLIENT_STOP;
    assert(service_export(&service_exports,&request)==KUI_CDDA_SERVICE_BUSY_RESULT);assert_unchanged(&before);
    service_guard=saved_guard;
    service_native_busy=true;before=capture();request.op=CDDA_CLIENT_SERVICE;
    assert(service_export(&service_exports,&request)==KUI_CDDA_SERVICE_BUSY_RESULT);assert_unchanged(&before);
    request.op=CDDA_CLIENT_STOP;
    assert(service_export(&service_exports,&request)==KUI_CDDA_SERVICE_BUSY_RESULT);assert_unchanged(&before);
    request.op=CDDA_CLIENT_RESTART;
    assert(service_export(&service_exports,&request)==KUI_CDDA_SERVICE_BUSY_RESULT);assert_unchanged(&before);
    service_native_busy=false;
}
int main(int argc,char **argv) {
    assert(argc==2);
    if(!strcmp(argv[1],"pcm-fail")) fault=PCM_FAULT;
    else if(!strcmp(argv[1],"data-fail")) fault=DATA_FAULT;
    else if(!strcmp(argv[1],"data-corrupt")) fault=DATA_CORRUPTION;
    else if(!strcmp(argv[1],"data-delay90")) fault=DATA_DELAY90;
    else if(!strcmp(argv[1],"data-delay200")) fault=DATA_DELAY200;
    else if(!strcmp(argv[1],"command-reprime-fail")) fault=REPRIME_FAULT;
    else if(!strcmp(argv[1],"client-clock-stall")) fault=CLIENT_CLOCK_STALL;
    else assert(!strcmp(argv[1],"pass"));
    cdda_main();
    assert(finished && !running && !command_running && !audio_open && !data_open && !storage_ready);
    assert(!write_faults && !post_fault_writes && starts==stops && pcm_reads && position_reads);
    assert(data_open_calls==1u && data_close_calls==1u && stack_initializations==1u);
    assert(client_handoffs==1u && !bridge_depth && !model_busy);
    if(fault==NO_FAULT) {
        assert(!failures && !reported_failures && !injected && completed==7u && stack_checks==2u);
        assert(service_elapsed.seconds>=90u && sim_ticks-clock_origin>=(uint64_t)KUI_CDDA_TMU_HZ*90u);
        assert(service_elapsed.wraps==1u && starts==4u && model_starts==2u);
        assert(model_expected_gaps==1u && model_recoveries==1u && model_deadlines==1u);
        assert(service_deadlines==1u && service_stales==2u && model_stale_exports==2u);
        assert(service_reentries==1u && model_reentries==1u && service_rejects==8u);
        assert(service_checked>=65536u && service_checked==model_bytes_read && service_reads==data_reads);
        assert(!service_errors && command_control.current.state==KUI_CDDA_CONTROL_STOPPED);
        assert(command_completions==6u && command_status_checks==5u);
        assert(probe_calls==service_abi_checks+1u && model_enters==model_leaves);
        assert(context_reads==2u);
        pure_adapter_refusals();
    } else {
        assert(injected && failures==1u && reported_failures==1u && service_errors);
        assert(command_control.current.state==KUI_CDDA_CONTROL_FAULT && !command_control.current.pending);
        if(fault==REPRIME_FAULT) assert(starts==1u && failed_command_epoch &&
            command_control.current.epoch==failed_command_epoch);
        if(fault==CLIENT_CLOCK_STALL) {
            assert(client_clock_reads>=2000000u && client_clock_reads<2000010u);
            assert(sim_ticks-clock_origin<41u*(uint64_t)KUI_CDDA_TMU_HZ);
        }
        if(fault==DATA_DELAY90) {
            assert(data_reads==1u && service_checked==2048u && !service_deadlines && !model_deadlines);
            assert(service_job.state==KUI_CDDA_JOB_DONE && service_job.done==2048u &&
                !service_job.pending.bytes);
            uint32_t gap=(uint32_t)sim_ticks-service_last_data;
            assert(gap>=5u*KUI_CDDA_TMU_HZ && gap<5u*KUI_CDDA_TMU_HZ+model_us_ticks(8000u));
            assert(service_elapsed.seconds<6u && starts==1u);
        }
        if(fault==DATA_FAULT || fault==DATA_CORRUPTION || fault==DATA_DELAY200) {
            assert(data_reads==1u && !service_checked);
            assert(service_job.state==KUI_CDDA_JOB_FAULT && !service_job.done &&
                !service_job.pending.bytes);
        }
        if(fault!=CLIENT_CLOCK_STALL) assert(service_guard.state==KUI_CDDA_SERVICE_FAULT);
    }
    printf("service %s: stages=%u failures=%u starts=%u service_calls=%u checked=%u"
        " actions=%u statuses=%u gaps=%u stale=%u reentry=%u elapsed_ms=%llu clock_reads=%u\n",
        argv[1],completed,failures,starts,service_calls,service_checked,command_completions,
        command_status_checks,service_deadlines,service_stales,service_reentries,
        (unsigned long long)((sim_ticks-clock_origin)*1000u/KUI_CDDA_TMU_HZ),clock_reads);
    return 0;
}
