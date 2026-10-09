/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual controlled batch BIOS-vector engine/client integration against independent generated
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
#define kui_cdda_bios_batch_take(...) model_batch_take(__VA_ARGS__)
#define kui_cdda_bios_batch_complete(...) model_batch_complete(__VA_ARGS__)
#include "../src/loader/cdda_main.c"
#undef kui_cdda_control_status
#undef kui_cdda_control_observe
#undef kui_cdda_control_fail
#undef kui_cdda_service_start
#undef kui_cdda_service_enter
#undef kui_cdda_service_leave
#undef kui_cdda_bios_batch_take
#undef kui_cdda_bios_batch_complete
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

extern enum kui_cdda_bios_batch_result kui_cdda_bios_batch_take(
    struct kui_cdda_bios_batch *,struct kui_cdda_bios_batch_work *);
extern enum kui_cdda_bios_batch_result kui_cdda_bios_batch_complete(
    struct kui_cdda_bios_batch *,const struct kui_cdda_bios_batch_work *,bool,uint32_t);

#define FIXTURE_BYTES (529200u*4u)
#define DATA_BYTES (8u*1024u*1024u)
#define MAX_STARTS 64u
enum fault_kind { NO_FAULT, PCM_FAULT, DATA_FAULT, DATA_CORRUPTION,
    DATA_DELAY90, DATA_DELAY200, LEAVE_DELAY200, REPRIME_FAULT, CLIENT_CLOCK_STALL, RESTORE_READBACK_FAILURE };
static enum fault_kind fault;
static uint64_t sim_ticks,key_tick,clock_origin;
static bool running,storage_ready,audio_open,data_open,injected,finished,terminal_fault;
static unsigned starts,stops,position_reads,hardware_stop_calls,hardware_write_calls;
static unsigned write_faults,post_fault_writes,clock_reads,pcm_reads,data_reads,blocks;
static unsigned data_open_calls,data_close_calls,reported_failures,model_statuses;
static unsigned model_observations;
static uint32_t last_observed_elapsed,failed_command_epoch;
static uint32_t model_bytes_read,model_confirmed_bytes,model_chunks,model_partial_aborts,model_partial_resets;
static uint32_t model_fullpass_next,model_fullpasses,model_size_mask,model_chunk_generation;
static uint32_t model_class_counts[8],model_progress_polls;
static uint8_t guest_initial[32768u+64u],incoming_initial[32768u+64u];
static struct kui_cdda_service_guard *model_guard;
static uint32_t model_epoch,model_lease_tick,model_gap,model_call;
static unsigned model_starts,model_enters,model_leaves,model_deadlines;
static bool model_busy,fault_reentry_checked;
static const struct cdda_batch_client_exports *original_exports;
static struct cdda_batch_client_exports wrapped_exports;
static unsigned bridge_depth,client_handoffs,worker_handoffs,probe_calls,client_clock_reads;
static unsigned vector_reads,vector_writes,map_calls,vector_calls,pure_calls,reentrant_calls;
static unsigned stack_initializations,stack_checks,context_reads,model_terminal_reads;
static uint32_t model_paused_frame,frozen_client_clock,vector_word=0x8c004000u;
static uint64_t model_pause_tick;
static uint32_t last_checked_elapsed;
struct request_ledger {uint32_t handle,epoch,command,offset,destination,repeat,total,done;bool valid,cancelled;};
static struct request_ledger ledger;
static uint32_t model_queue_epoch;
static struct kui_cdda_bios_batch_work old_claim;
static bool old_claim_valid;
static unsigned stale_same_chunk,stale_old_request,stale_cancel,stale_reset,forged_destination;
#if CDDA_TEST_PROFILE == 10
static bool planned_gap_armed,planned_gap_seen,early_failure_guest_checked;
static uint32_t planned_gap_handle;
static uint64_t planned_gap_tick;
static unsigned planned_gap_data_reads,planned_gap_pcm_reads,planned_gap_positions,planned_gap_writes;
#endif
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
    assert(!model_busy);
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
#if CDDA_TEST_PROFILE == 10
        assert(planned_gap_armed && !planned_gap_seen && !injected &&
            ledger.valid && ledger.handle==planned_gap_handle && ledger.done==2048u);
        assert(sim_ticks-planned_gap_tick>=(uint64_t)KUI_CDDA_TMU_HZ/5u);
        assert(data_reads==planned_gap_data_reads && pcm_reads==planned_gap_pcm_reads &&
            position_reads==planned_gap_positions && hardware_write_calls==planned_gap_writes);
        planned_gap_seen=true;
#endif
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
        fprintf(stderr,"BIOS bad ring sample elapsed%u expected%u actual%u\n",elapsed,expected,actual);
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
    /* Once retired, the planned deadline and unannounced I/O failures must
     * remain stopped through final cleanup. */
    if(result==KUI_CDDA_CONTROL_IO && injected && batch_errors) terminal_fault=true;
#if CDDA_TEST_PROFILE == 10
    if(result==KUI_CDDA_CONTROL_IO && planned_gap_seen) terminal_fault=true;
#endif
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
    key_tick=sim_ticks;last_observed_elapsed=last_checked_elapsed=0;running=true;return KUI_CDDA_AICA_OK;
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
#if CDDA_TEST_PROFILE == 10
    assert(!planned_gap_armed);
#endif
    assert(frame && running);advance(model_us_ticks(250u));last_observed_elapsed=hardware_frame();
    if(!(injected && fault==DATA_DELAY200)) {
        assert(last_observed_elapsed>=last_checked_elapsed &&
            last_observed_elapsed-last_checked_elapsed<KUI_CDDA_RING_FRAMES);
        for(uint32_t f=last_checked_elapsed;f<=last_observed_elapsed;f++) check_current_sample(f);
        last_checked_elapsed=last_observed_elapsed;
    }
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
    if(terminal_fault) post_fault_writes++;
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
#if CDDA_TEST_PROFILE == 10
    assert(!planned_gap_armed);
#endif
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
#if CDDA_TEST_PROFILE == 10
    assert(!planned_gap_armed);
#endif
    assert(data_open && out && bytes && bytes<=2048u && offset<=DATA_BYTES && bytes<=DATA_BYTES-offset);
    /* Ranges are advertised by the actual engine's portable job claim. They
     * may be noncontiguous; independently validate each exact physical read. */
    assert(batch_data_job.state==KUI_CDDA_JOB_READING && batch_data_job.pending.offset==offset &&
        batch_data_job.pending.bytes==bytes && batch_data_job.pending.session_epoch==ledger.epoch);
    assert(ledger.valid && ledger.command==KUI_GD_PIOREAD && ledger.offset+ledger.done==offset && bytes==2048u);
    assert(((uintptr_t)out&31u)==0u);
    advance(model_us_ticks(2000u));data_reads++;blocks+=(bytes+511u)/512u;
    bool fault_chunk=CDDA_TEST_PROFILE==10 || ledger.done>=2048u;
    if(!injected && ((fault==DATA_DELAY90) || (fault==DATA_DELAY200 && fault_chunk))) {
        injected=true;advance(model_us_ticks(fault==DATA_DELAY90?90000u:200000u));
    }
    if(!injected && fault==DATA_FAULT && fault_chunk) {injected=true;return -1;}
    for(uint32_t i=0;i<bytes;i++) {
        uint32_t x=(offset+i)^0x9e3779b9u;
        x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;out[i]=(uint8_t)x;
    }
    if(!injected && fault==DATA_CORRUPTION && fault_chunk) {injected=true;out[bytes/2u]^=1u;}
    model_bytes_read+=bytes;
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

/* Numeric guest addresses retain full host pointers. Each registered span is
 * in the client's usable stack; ownership checks never dereference it. */
struct guest_span {const uint8_t *host;uint32_t address,bytes;bool writable;};
static struct guest_span guest_spans[32];
static unsigned guest_count;
static uint32_t guest_next=0x8c310040u;
static uint8_t *find_guest(uint32_t address,uint32_t bytes,int writing) {
    address=(address&0x00ffffffu)|0x8c000000u;
    for(unsigned i=0;i<guest_count;i++) {
        const struct guest_span *g=&guest_spans[i];
        if(address>=g->address && address-g->address<g->bytes &&
           bytes<=g->bytes-(address-g->address) && (!writing || g->writable))
            return (uint8_t *)(uintptr_t)(g->host+(address-g->address));
    }
    return NULL;
}
uint32_t cdda_batch_host_address(const void *pointer,uint32_t bytes,bool writing) {
    assert(pointer && bytes && bridge_depth==1u);
    for(unsigned i=0;i<guest_count;i++) {
        if(guest_spans[i].host==pointer && bytes<=guest_spans[i].bytes) {
            guest_spans[i].writable|=writing;return guest_spans[i].address;
        }
    }
    assert(guest_count<32u && bytes<=0x8c320000u-guest_next);
    struct guest_span *g=&guest_spans[guest_count++];
    *g=(struct guest_span){pointer,guest_next,bytes,writing};
    guest_next+=(bytes+31u)&~31u;return g->address;
}
uint8_t *cdda_batch_host_map(uint32_t address,uint32_t bytes,int writing) {
    assert(writing>=0 && writing<=KUI_CDDA_BIOS_BATCH_MAP_VALIDATE);map_calls++;
    return find_guest(address,bytes,writing);
}
uint32_t cdda_batch_host_vector_read(void) {
    vector_reads++;
    if(fault==RESTORE_READBACK_FAILURE && vector_writes==2u && !injected) {
        assert(vector_word==0x8c004000u);injected=true;return 0x8c010200u;
    }
    return vector_word;
}
void cdda_batch_host_vector_write(uint32_t value) {
    assert(!bridge_depth);vector_writes++;vector_word=value;
}
struct effects {
    uint64_t tick;
    unsigned clocks,positions,reads,data,writes,keyons,keyoffs,calls,maps;
    struct kui_cdda_service_guard guard;
    struct kui_cdda_control control;
    struct kui_cdda_job job;
    struct kui_cdda_bios_batch owner;
    bool audio,card_audio,card_data,native_busy;
};
static struct effects capture(void) {
    return (struct effects){sim_ticks,clock_reads,position_reads,pcm_reads,data_reads,
        hardware_write_calls,starts,hardware_stop_calls,batch_calls,map_calls,batch_guard,
        command_control,batch_data_job,batch_owner,running,audio_open,data_open,batch_native_busy};
}
static void assert_no_io(const struct effects *b) {
    assert(position_reads==b->positions && pcm_reads==b->reads && data_reads==b->data);
    assert(hardware_write_calls==b->writes && starts==b->keyons && hardware_stop_calls==b->keyoffs);
    assert(!memcmp(&command_control,&b->control,sizeof(command_control)));
    assert(!memcmp(&batch_data_job,&b->job,sizeof(batch_data_job)));
    assert(running==b->audio && audio_open==b->card_audio && data_open==b->card_data);
}
static void assert_unchanged(const struct effects *b) {
    assert_no_io(b);
    assert(sim_ticks==b->tick && clock_reads==b->clocks && batch_calls==b->calls+1u && map_calls==b->maps);
    assert(!memcmp(&batch_guard,&b->guard,sizeof(batch_guard)) && batch_native_busy==b->native_busy);
    assert(!memcmp(&batch_owner,&b->owner,sizeof(batch_owner)));
}
static uint32_t load32(const uint8_t *p) {
    assert(p);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static uint8_t independent_data_byte(uint32_t at) {
    uint32_t x=at^0x9e3779b9u;x^=x>>16;x*=0x7feb352du;
    x^=x>>15;x*=0x846ca68bu;x^=x>>16;return (uint8_t)x;
}
static void verify_guest_prefix(bool discarded_chunk) {
    if(!ledger.valid || ledger.command!=KUI_GD_PIOREAD) return;
    const uint8_t *g=find_guest(ledger.destination-32u,sizeof(guest_initial),1);assert(g);
    assert(ledger.done<=ledger.total && !(ledger.done%2048u));
    for(uint32_t i=0;i<sizeof(guest_initial);i++) {
        if(i>=32u && i-32u<ledger.done)
            assert(g[i]==independent_data_byte(ledger.offset+i-32u));
        else if(discarded_chunk && i>=32u+ledger.done && i<32u+ledger.done+2048u &&
                i<32u+ledger.total)
            assert(g[i]==guest_initial[i] || g[i]==independent_data_byte(ledger.offset+i-32u));
        else assert(g[i]==guest_initial[i]);
    }
}
static void refuse_callback(const struct kui_cdda_bios_batch_work *work) {
    struct effects before=capture();
    assert(!batch_read_work(work));
    assert(kui_cdda_bios_batch_complete(&batch_owner,work,true,work->bytes)==KUI_CDDA_BIOS_BATCH_STALE);
    assert_no_io(&before);
    assert(sim_ticks==before.tick && clock_reads==before.clocks && batch_calls==before.calls &&
        map_calls==before.maps && !memcmp(&batch_guard,&before.guard,sizeof(batch_guard)) &&
        !memcmp(&batch_owner,&before.owner,sizeof(batch_owner)) && batch_native_busy==before.native_busy);
}
enum kui_cdda_bios_batch_result model_batch_take(struct kui_cdda_bios_batch *owner,
    struct kui_cdda_bios_batch_work *out) {
    enum kui_cdda_bios_batch_result result=kui_cdda_bios_batch_take(owner,out);
    if(result!=KUI_CDDA_BIOS_BATCH_OK) return result;
    assert(owner==&batch_owner && ledger.valid && out->handle==ledger.handle && out->epoch==ledger.epoch);
    assert(out->chunk>model_chunk_generation && out->command==ledger.command);
    model_chunk_generation=out->chunk;
    if(out->kind==KUI_CDDA_BIOS_BATCH_DATA) assert(out->offset==ledger.offset+ledger.done &&
        out->destination==ledger.destination+ledger.done && out->bytes==2048u &&
        out->total_bytes==ledger.total && out->committed==ledger.done && ledger.done<ledger.total);
    else assert(!out->bytes && !out->total_bytes && !out->committed);
    if(out->kind==KUI_CDDA_BIOS_BATCH_DATA) {
        if(old_claim_valid && old_claim.handle==out->handle && !stale_same_chunk) {
            refuse_callback(&old_claim);stale_same_chunk++;
        }
        if(old_claim_valid && old_claim.handle!=out->handle && !stale_old_request) {
            refuse_callback(&old_claim);stale_old_request++;
        }
        if(!forged_destination) {
            struct kui_cdda_bios_batch_work forged=*out;forged.destination+=2048u;
            refuse_callback(&forged);forged_destination++;
        }
        old_claim=*out;old_claim_valid=true;
    }
    return result;
}
enum kui_cdda_bios_batch_result model_batch_complete(struct kui_cdda_bios_batch *owner,
    const struct kui_cdda_bios_batch_work *work,bool success,uint32_t bytes) {
    enum kui_cdda_bios_batch_result result=kui_cdda_bios_batch_complete(owner,work,success,bytes);
    if(result!=KUI_CDDA_BIOS_BATCH_OK) return result;
    assert(owner==&batch_owner && ledger.valid && work->handle==ledger.handle && work->epoch==ledger.epoch);
    if(success && work->kind==KUI_CDDA_BIOS_BATCH_DATA) {
        assert(bytes==2048u && work->committed==ledger.done && work->offset==ledger.offset+ledger.done);
        ledger.done+=bytes;model_confirmed_bytes+=bytes;model_chunks++;
        assert(owner->completed_bytes==ledger.done);
        if(ledger.done<ledger.total) assert(owner->state==KUI_CDDA_BIOS_BATCH_QUEUED &&
            owner->status==KUI_GD_PROCESSING && !owner->pending.bytes);
        else {
            assert(owner->state==KUI_CDDA_BIOS_BATCH_TERMINAL && owner->status==KUI_GD_COMPLETED);
            static const uint32_t sizes[8]={1,2,3,4,7,8,15,16};
            for(unsigned i=0;i<8u;i++) if(ledger.total==sizes[i]*2048u) {model_size_mask|=1u<<i;model_class_counts[i]++;}
            if(running && repeat_audio) {
                if(ledger.offset!=model_fullpass_next) model_fullpass_next=ledger.offset==0u?0u:DATA_BYTES;
                if(ledger.offset==model_fullpass_next && ledger.total<=DATA_BYTES-model_fullpass_next) {
                    model_fullpass_next+=ledger.total;
                    if(model_fullpass_next==DATA_BYTES) {model_fullpasses++;model_fullpass_next=0;}
                }
            }
        }
    } else if(!success) assert(!bytes && owner->completed_bytes==ledger.done &&
        owner->state==KUI_CDDA_BIOS_BATCH_TERMINAL && owner->status==KUI_GD_FAILED);
    return result;
}
static unsigned model_requests,model_execs,model_checks,model_aborts,model_resets;
int32_t cdda_batch_host_vector_call(uint32_t r4,uint32_t r5,uint32_t r6,uint32_t r7) {
    assert(vector_word==0x8c010200u && bridge_depth>=1u);
    struct effects before=capture();struct request_ledger submitted={.command=r4};
    bool incoming_read=false;
    if(r7==KUI_GD_REQUEST && !r6 && !batch_native_busy) {
        const uint8_t *parameters=find_guest(r5,r4==KUI_GD_PIOREAD?16u:12u,0);
        if(parameters && r4==KUI_GD_PIOREAD) {
            submitted.offset=(load32(parameters)-45150u)*2048u;
            submitted.total=load32(parameters+4u)*2048u;
            submitted.destination=(load32(parameters+8u)&0x00ffffffu)|0x8c000000u;
            const uint8_t *g=find_guest(submitted.destination-32u,sizeof(incoming_initial),1);
            if(g) {memcpy(incoming_initial,g,sizeof(incoming_initial));incoming_read=true;}
        } else if(parameters && r4==KUI_CDDA_BIOS_BATCH_PLAY) submitted.repeat=load32(parameters+8u);
    }
    uint32_t queued_command=ledger.command;
    vector_calls++;
    int32_t result=cdda_bios_native_dispatch(r4,r5,r6,r7);
    if(before.native_busy) {
        assert(result==(r7==KUI_GD_REQUEST?0:r7==KUI_GD_CHECK?4:-1));
        assert_unchanged(&before);reentrant_calls++;return result;
    }
    if(r7!=KUI_GD_EXEC) {assert_no_io(&before);pure_calls++;}
    if(r7==KUI_GD_EXEC) {
        assert(data_reads-before.data<=1u);
        if(result==-1 && ledger.valid) assert(batch_owner.state==KUI_CDDA_BIOS_BATCH_TERMINAL &&
            batch_owner.status==KUI_GD_FAILED && batch_owner.error==KUI_GD_ERROR_IO &&
            batch_owner.completed_bytes==ledger.done);
        verify_guest_prefix(result==-1 && fault==LEAVE_DELAY200);
#if CDDA_TEST_PROFILE == 10
        if(result==-1 && injected) {assert(!ledger.done);early_failure_guest_checked=true;}
#endif
    }
    if(incoming_read) assert(!memcmp(find_guest(submitted.destination-32u,sizeof(incoming_initial),1),
        incoming_initial,sizeof(incoming_initial)));
    if(r6) return result;
    if(r7==KUI_GD_REQUEST && result>0) {
        assert(!ledger.valid && (uint32_t)result>ledger.handle);
        submitted.handle=(uint32_t)result;submitted.epoch=++model_queue_epoch;submitted.valid=true;
        ledger=submitted;model_requests++;
        assert(batch_owner.work.command==submitted.command && batch_owner.work.handle==submitted.handle &&
            batch_owner.work.epoch==submitted.epoch);
        if(submitted.command==KUI_GD_PIOREAD) {
            assert(incoming_read && submitted.total>=2048u && submitted.total<=32768u &&
                batch_owner.work.offset==submitted.offset && batch_owner.work.destination==submitted.destination &&
                batch_owner.work.bytes==submitted.total && batch_owner.work.total_bytes==submitted.total);
            memcpy(guest_initial,incoming_initial,sizeof(guest_initial));verify_guest_prefix(false);
        }
        if(submitted.command==KUI_CDDA_BIOS_BATCH_PLAY) assert(batch_owner.work.audio.first==268128u &&
            batch_owner.work.audio.end==312228u && batch_owner.work.audio.repeat==(submitted.repeat==15u));
    } else if(r7==KUI_GD_CHECK) {
        model_checks++;
        if(ledger.valid && r4==ledger.handle) {
            const uint8_t *status=find_guest(r5,16u,1);assert(status && load32(status+8u)==ledger.done);
            if(result==KUI_GD_PROCESSING) {
                assert(!load32(status) && !load32(status+4u) && load32(status+12u)==4u);
                if(ledger.done) model_progress_polls++;
            }
            if(result==KUI_GD_COMPLETED || result==KUI_GD_FAILED) {
                assert(!load32(status+12u));
                if(result==KUI_GD_COMPLETED) {
                    assert(!load32(status) && !load32(status+4u));
                    if(ledger.command==KUI_GD_PIOREAD) {
                        assert(ledger.done==ledger.total && !ledger.cancelled);verify_guest_prefix(false);
                        model_terminal_reads++;
                    }
                } else assert(load32(status)==1u && load32(status+4u)==
                    (ledger.cancelled?KUI_GD_ERROR_CANCELLED:KUI_GD_ERROR_IO));
                ledger.valid=false;
            }
        }
    } else if(r7==KUI_GD_ABORT && result==0) {
        assert(ledger.valid && r4==ledger.handle);verify_guest_prefix(false);
        ledger.cancelled=true;model_aborts++;if(ledger.done) {assert(ledger.done==2048u);model_partial_aborts++;}
        assert(batch_owner.completed_bytes==ledger.done);
        if(ledger.done && old_claim_valid && !stale_cancel) {refuse_callback(&old_claim);stale_cancel++;}
    } else if((r7==KUI_GD_RESET || r7==KUI_GD_INIT) && result==0) {
        verify_guest_prefix(false);if(ledger.done && ledger.valid) {assert(ledger.done==2048u);model_partial_resets++;}
        ledger.valid=false;model_queue_epoch++;model_resets++;
        if(ledger.done && old_claim_valid && !stale_reset) {refuse_callback(&old_claim);stale_reset++;}
    } else if(r7==KUI_GD_EXEC && result==0) {
        model_execs++;
        if(before.control.current.state==KUI_CDDA_CONTROL_PLAYING && queued_command==KUI_CDDA_BIOS_BATCH_PAUSE &&
           batch_owner.state==KUI_CDDA_BIOS_BATCH_TERMINAL && batch_owner.status==KUI_GD_COMPLETED) {
            assert(!running && !command_running && starts==before.keyons);
            model_paused_frame=command_control.current.frame;model_pause_tick=sim_ticks;
            assert(model_paused_frame==records[starts-1u].stop_cursor && model_paused_frame!=records[starts-1u].prefetch);
        }
        if(queued_command==KUI_CDDA_BIOS_BATCH_RELEASE && starts==before.keyons+1u) {
            assert(running && records[starts-1u].first==model_paused_frame &&
                sim_ticks-model_pause_tick>=KUI_CDDA_TMU_HZ);
            assert(records[starts-1u].loop_first==268128u && records[starts-1u].end==312228u);
        }
        if(queued_command==KUI_GD_STOP && batch_owner.status==KUI_GD_COMPLETED) assert(!running && !command_running);
    }
    assert(batch_checked==model_confirmed_bytes);
    return result;
}
static uint32_t wrapped_clock(void *context) {
    assert(original_exports && context==original_exports->context && bridge_depth==1u);
    uint32_t actual=original_exports->clock(context);client_clock_reads++;
    if(fault!=CLIENT_CLOCK_STALL) return actual;
    if(!injected) {injected=true;frozen_client_clock=actual;}
    return frozen_client_clock;
}
#if CDDA_TEST_PROFILE == 10
static uint32_t wrapped_diagnostic(void *context,struct cdda_batch_client_diagnostic *request) {
    struct effects before=capture();
    uint32_t result=original_exports->diagnostic(context,request);
    if(request->op==CDDA_BATCH_CLIENT_ARM_GAP && !result && !request->result) {
        assert_no_io(&before);
        assert(!planned_gap_armed && !injected && running && ledger.valid &&
            ledger.handle==request->value && ledger.total==32768u && ledger.done==2048u);
        assert(batch_owner.state==KUI_CDDA_BIOS_BATCH_QUEUED && !batch_owner.pending.bytes &&
            !memcmp(&before.owner,&batch_owner,sizeof(batch_owner)) &&
            !memcmp(&before.guard,&batch_guard,sizeof(batch_guard)));
        verify_guest_prefix(false);
        planned_gap_armed=true;planned_gap_handle=request->value;planned_gap_tick=sim_ticks;
        planned_gap_data_reads=data_reads;planned_gap_pcm_reads=pcm_reads;
        planned_gap_positions=position_reads;planned_gap_writes=hardware_write_calls;
    }
    return result;
}
#endif
uint32_t kui_cdda_client_call(uint32_t (*entry)(const void *),const void *context,void *stack_top) {
    assert(entry);
    uint32_t destination=(uint32_t)(uintptr_t)stack_top;
    if(destination==0x8c320000u) {
        assert(!bridge_depth && entry==cdda_batch_client_entry && !original_exports);
        original_exports=context;wrapped_exports=*original_exports;
        assert(wrapped_exports.magic==CDDA_BATCH_CLIENT_MAGIC && wrapped_exports.bytes==sizeof(wrapped_exports));
        wrapped_exports.clock=wrapped_clock;client_handoffs++;bridge_depth++;
#if CDDA_TEST_PROFILE == 10
        wrapped_exports.diagnostic=wrapped_diagnostic;
#endif
        uint32_t result=entry(&wrapped_exports);bridge_depth--;return result;
    }
    assert(destination==0x8c230000u && bridge_depth<=1u && batch_native_busy);worker_handoffs++;
    if(entry==batch_worker) assert(model_busy && batch_guard.state==KUI_CDDA_SERVICE_BUSY);
    else assert(entry==batch_fault_worker && !context);
    bridge_depth++;
    if(entry==batch_fault_worker && batch_installed && vector_word==0x8c010200u && !fault_reentry_checked &&
       (CDDA_TEST_PROFILE==9 || injected)) {
        fault_reentry_checked=true;
        assert(cdda_batch_host_vector_call(0,0,0,KUI_GD_EXEC)==-1);
        assert(cdda_batch_host_vector_call(KUI_GD_STOP,UINT32_MAX,0,KUI_GD_REQUEST)==0);
        assert(cdda_batch_host_vector_call(UINT32_MAX,UINT32_MAX,0,KUI_GD_CHECK)==4);
    }
    uint32_t result=entry(context);
    if(entry==batch_worker && fault==LEAVE_DELAY200 && !injected && batch_completion_pending &&
       batch_completion_work.kind==KUI_CDDA_BIOS_BATCH_DATA && ledger.done>=2048u) {
        assert(batch_owner.state==KUI_CDDA_BIOS_BATCH_RUNNING && batch_data_job.state==KUI_CDDA_JOB_READY &&
            batch_checked==model_confirmed_bytes && ledger.done==2048u);
        injected=true;advance(model_us_ticks(200000u));
    }
    bridge_depth--;return result;
}
uint32_t kui_cdda_bridge_probe(uint32_t (*service)(void *),void *context) {
    assert(service && bridge_depth==1u);probe_calls++;(void)service(context);return 0u;
}
uint32_t cdda_batch_host_client_pc(void) {assert(bridge_depth==1u);return 0x8c300100u;}
uint32_t cdda_batch_host_client_sp(void) {assert(bridge_depth==1u);return 0x8c31ffe0u;}
uint32_t cdda_batch_host_caller_sp(void) {assert(bridge_depth>=1u);return bridge_depth==1u?0x8c31ffe0u:0x8c22ffe0u;}
uint32_t cdda_host_batch_sp(void) {assert(bridge_depth>=1u);return 0x8c22ffe0u;}
void cdda_host_batch_context(uint32_t *out) {
    assert(!bridge_depth && out);context_reads++;out[0]=0x400000f0u|(context_reads&1u);out[1]=out[2]=0;
}
void cdda_host_batch_stacks_init(void) {assert(!bridge_depth);stack_initializations++;}
bool cdda_host_batch_stack_check(unsigned which,uint32_t *used) {
    assert(which<2u && used && !bridge_depth);*used=which?34000u:2048u; /* Simulated guard result, not a native watermark. */stack_checks++;return true;
}
int main(int argc,char **argv) {
    assert(argc==2);
    if(!strcmp(argv[1],"pcm-fail")) fault=PCM_FAULT;
    else if(!strcmp(argv[1],"data-fail")) fault=DATA_FAULT;
    else if(!strcmp(argv[1],"data-corrupt")) fault=DATA_CORRUPTION;
    else if(!strcmp(argv[1],"data-delay90")) fault=DATA_DELAY90;
    else if(!strcmp(argv[1],"data-delay200")) fault=DATA_DELAY200;
    else if(!strcmp(argv[1],"leave-delay200")) fault=LEAVE_DELAY200;
    else if(!strcmp(argv[1],"command-reprime-fail")) fault=REPRIME_FAULT;
    else if(!strcmp(argv[1],"client-clock-stall")) fault=CLIENT_CLOCK_STALL;
    else if(!strcmp(argv[1],"restore-readback-fail")) fault=RESTORE_READBACK_FAILURE;
    else assert(!strcmp(argv[1],"pass"));
    cdda_main();
    assert(finished && !running && !command_running && !audio_open && !data_open && !storage_ready);
    assert(!write_faults && !post_fault_writes && starts==stops);
    assert(data_open_calls==1u && data_close_calls==1u && stack_initializations==1u);
    assert(client_handoffs==1u && !bridge_depth && !model_busy);
    assert(probe_calls==vector_calls-reentrant_calls);
    assert(vector_word==0x8c004000u && batch_vector_restored==1u && !batch_installed);
    assert(vector_writes==(fault==RESTORE_READBACK_FAILURE?3u:2u));
    assert(batch_checked==model_confirmed_bytes);
    if(fault==NO_FAULT) {
        assert(!failures && !reported_failures && !injected && completed==8u && stack_checks==2u);
#if CDDA_TEST_PROFILE == 10
        assert(planned_gap_armed && planned_gap_seen && model_deadlines==1u && terminal_fault);
        assert(batch_expected_gaps==1u && batch_expected_prefix==2048u && !batch_gap_armed && !batch_errors);
        assert(data_reads==1u && model_confirmed_bytes==2048u && model_chunks==1u &&
            model_bytes_read==2048u && ledger.done==2048u && !ledger.valid);
        assert(starts==1u && command_completions==1u && batch_completions==1u &&
            batch_accepted==2u && model_requests==2u && model_terminal_reads==0u);
        assert(command_control.current.state==KUI_CDDA_CONTROL_FAULT && !command_control.current.pending &&
            batch_guard.state==KUI_CDDA_SERVICE_FAULT && batch_owner.state==KUI_CDDA_BIOS_BATCH_EMPTY);
        assert(batch_data_job.state==KUI_CDDA_JOB_DONE && !batch_data_job.pending.bytes);
        assert(probe_calls==16u && vector_calls==17u && reentrant_calls==1u &&
            batch_execs==2u && batch_checks==7u && batch_drive_checks==2u && batch_refusals==4u &&
            batch_report_stales==1u && batch_progress_polls==2u && model_progress_polls==2u &&
            model_enters==model_leaves && model_execs==2u);
        assert(!model_aborts && !model_resets && !model_fullpasses && !model_size_mask);
        assert(context_reads==2u && batch_context_checks==1u && sim_ticks-clock_origin<KUI_CDDA_TMU_HZ);
        assert(forged_destination==1u && stale_same_chunk==1u && !stale_old_request && !stale_cancel && !stale_reset);
#else
        assert(batch_elapsed.seconds>=90u && sim_ticks-clock_origin>=90u*(uint64_t)KUI_CDDA_TMU_HZ);
        assert(batch_elapsed.wraps==1u && starts==3u && pcm_reads && position_reads);
        assert(model_paused_frame && model_aborts && model_resets && model_partial_aborts==1u &&
            model_partial_resets==1u && model_terminal_reads>=32u);
        assert(model_fullpasses>=1u && model_size_mask==255u && model_confirmed_bytes>=DATA_BYTES);
        assert(!batch_errors && command_control.current.state==KUI_CDDA_CONTROL_STOPPED);
        assert(command_completions==6u && reentrant_calls && model_enters==model_leaves);
        assert(context_reads==2u && batch_context_checks==1u);
        assert(model_requests==batch_accepted && model_chunks==batch_chunks &&
            model_progress_polls==batch_progress_polls && model_progress_polls>=32u);
        for(unsigned i=0;i<8u;i++) assert(model_class_counts[i]==batch_class_counts[i] && model_class_counts[i]>=16u);
        assert(model_terminal_reads+6u==batch_completions && batch_accepted==batch_completions+2u);
        assert(stale_same_chunk==1u && stale_old_request==1u && stale_cancel==1u && stale_reset==1u &&
            forged_destination==1u);
#endif
    } else {
        assert(injected && failures==1u && reported_failures==1u && batch_errors);
        assert(command_control.current.state==KUI_CDDA_CONTROL_FAULT && !command_control.current.pending);
        if(fault==CLIENT_CLOCK_STALL) assert(client_clock_reads>=2000000u && client_clock_reads<2000020u);
#if CDDA_TEST_PROFILE == 10
        if(fault==RESTORE_READBACK_FAILURE) {
            assert(planned_gap_armed && planned_gap_seen && model_deadlines==1u && terminal_fault &&
                batch_expected_gaps==1u && batch_expected_prefix==2048u && !batch_gap_armed);
            assert(completed==7u && stack_checks==2u && starts==1u && vector_reads==4u &&
                data_reads==1u && model_confirmed_bytes==2048u && model_chunks==1u && !ledger.valid);
        } else {
            assert(!planned_gap_armed && !planned_gap_seen && !batch_expected_gaps && !batch_expected_prefix &&
                !model_confirmed_bytes && !model_chunks && ledger.valid && !ledger.done &&
                !batch_owner.completed_bytes && batch_owner.state==KUI_CDDA_BIOS_BATCH_TERMINAL &&
                batch_owner.status==KUI_GD_FAILED && batch_owner.error==KUI_GD_ERROR_IO);
            if(fault==CLIENT_CLOCK_STALL) assert(!data_reads && !starts && completed==1u && !model_deadlines);
            else {
                assert(fault==DATA_FAULT || fault==DATA_CORRUPTION || fault==DATA_DELAY200);
                assert(data_reads==1u && completed==2u && early_failure_guest_checked);
                assert(model_deadlines==(fault==DATA_DELAY200?1u:0u));
            }
        }
#else
        if(fault==RESTORE_READBACK_FAILURE) assert(completed==7u && stack_checks==2u &&
            batch_elapsed.seconds>=90u && starts==3u && vector_reads==4u && model_fullpasses>=1u);
        if(fault==REPRIME_FAULT) assert(starts==1u && failed_command_epoch &&
            command_control.current.epoch==failed_command_epoch);
        if(fault==DATA_DELAY90) {
            uint32_t gap=(uint32_t)sim_ticks-batch_last_data;
            assert(!model_deadlines && batch_elapsed.seconds<8u);
            assert(gap>=5u*KUI_CDDA_TMU_HZ && gap<5u*KUI_CDDA_TMU_HZ+model_us_ticks(8000u));
        }
        if(fault==DATA_FAULT || fault==DATA_CORRUPTION || fault==DATA_DELAY200 || fault==LEAVE_DELAY200)
            assert(ledger.valid && ledger.done==2048u && batch_owner.completed_bytes==2048u &&
                batch_owner.state==KUI_CDDA_BIOS_BATCH_TERMINAL);
        if(fault==LEAVE_DELAY200) assert(model_deadlines==1u && batch_data_job.state==KUI_CDDA_JOB_FAULT &&
            !batch_data_job.done && !batch_data_job.pending.bytes);
#endif
    }
    printf("batch BIOS profile %u %s: stages=%u failures=%u starts=%u EXEC=%u vector=%u pure=%u checked=%u"
        " chunks=%u passes=%u sizes=%02x prefix=%u actions=%u reentry=%u elapsed_ms=%llu\n",CDDA_TEST_PROFILE,argv[1],completed,failures,starts,batch_execs,
        vector_calls,pure_calls,batch_checked,model_chunks,model_fullpasses,model_size_mask,ledger.done,command_completions,reentrant_calls,
        (unsigned long long)((sim_ticks-clock_origin)*1000u/KUI_CDDA_TMU_HZ));
    return 0;
}
