/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual mapped-disc BIOS-vector engine/client integration against independent generated
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
#define kui_cdda_disc_bios_take(...) model_disc_take(__VA_ARGS__)
#define kui_cdda_disc_bios_complete(...) model_disc_complete(__VA_ARGS__)
#include "../src/loader/cdda_main.c"
#undef kui_cdda_control_status
#undef kui_cdda_control_observe
#undef kui_cdda_control_fail
#undef kui_cdda_service_start
#undef kui_cdda_service_enter
#undef kui_cdda_service_leave
#undef kui_cdda_disc_bios_take
#undef kui_cdda_disc_bios_complete
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

extern enum kui_cdda_disc_bios_result kui_cdda_disc_bios_take(
    struct kui_cdda_disc_bios *,struct kui_cdda_disc_bios_work *);
extern enum kui_cdda_disc_bios_result kui_cdda_disc_bios_complete(
    struct kui_cdda_disc_bios *,const struct kui_cdda_disc_bios_work *,bool,uint32_t);

struct reference_track {uint32_t number,fad,sectors,stride,prefix,file_bytes;bool audio;const char *name;};
static const struct reference_track tracks[]={
 {1u,150u,16u,2048u,0u,32768u,false,"track01.bin"},
 {2u,170u,75u,2352u,0u,176400u,true,"track02.raw"},
 {3u,45150u,64u,2048u,0u,131072u,false,"track03.bin"},
 {4u,45300u,150u,2352u,512u,353312u,true,"track04.raw"},
 {5u,45450u,225u,2352u,1024u,530224u,true,"track05.raw"},
 {6u,45825u,32u,2352u,0u,75264u,false,"track06.bin"},
 {14u,374351u,3071u,2352u,0u,7222992u,true,"track14.raw"}
};
static const char fixture_gdi[]="6\n1 0 4 2048 track01.bin 0\n2 20 0 2352 track02.raw 0\n"
 "3 45000 4 2048 track03.bin 0\n4 45150 0 2352 track04.raw 512\n"
 "5 45300 0 2352 track05.raw 1024\n6 45675 4 2352 track06.bin 0\n";
/* Only the selected14/next15 geometry is drawn from the supplied descriptor;
 * unused rows are synthetic and no missing original file is opened/stat'ed. */
static const char selected_gdi[]="15\n1 0 4 2352 track01.bin 0\n2 100 0 2352 track02.raw 0\n"
 "3 45000 4 2352 track03.bin 0\n4 46000 0 2352 track04.raw 0\n5 47000 0 2352 track05.raw 0\n"
 "6 48000 0 2352 track06.raw 0\n7 49000 0 2352 track07.raw 0\n8 50000 0 2352 track08.raw 0\n"
 "9 51000 0 2352 track09.raw 0\n10 52000 0 2352 track10.raw 0\n11 53000 0 2352 track11.raw 0\n"
 "12 54000 0 2352 track12.raw 0\n13 55000 0 2352 track13.raw 0\n"
 "14 374201 0 2352 track14.raw 0\n15 377422 4 2352 track15.bin 0\n";
static const struct reference_track *reference_track(uint32_t number) {
 for(unsigned i=0;i<sizeof(tracks)/sizeof(tracks[0]);i++) if(tracks[i].number==number) return tracks+i;
 assert(false);return NULL;
}
static const struct reference_track *path_track(const char *path) {
 for(unsigned i=0;i<sizeof(tracks)/sizeof(tracks[0]);i++) {
  const char *name=strrchr(path,'/');name=name?name+1:path;
  if(!strcmp(name,tracks[i].name)) return tracks+i;
 }
 return NULL;
}
#define MAX_STARTS 64u
enum fault_kind { NO_FAULT, PCM_FAULT, DATA_FAULT, DATA_CORRUPTION,
    DATA_DELAY90, DATA_DELAY200, LEAVE_DELAY200, REPRIME_FAULT, CLIENT_CLOCK_STALL, RESTORE_READBACK_FAILURE,
    MAP_PARSE, MAP_MISSING, MAP_TRUNCATED, MAP_OVERLAP, PREFIX_CORRUPTION, RAW_MODE_FAULT,
    SOURCE_SWITCH_FAULT };
static enum fault_kind fault;
static uint64_t sim_ticks,key_tick,clock_origin;
static bool running,storage_ready,audio_open,data_open,injected,finished,terminal_fault;
static unsigned starts,stops,position_reads,hardware_stop_calls,hardware_write_calls;
static unsigned write_faults,post_fault_writes,clock_reads,pcm_reads,data_reads,blocks;
static unsigned data_open_calls,data_close_calls,reported_failures,model_statuses;
static const struct reference_track *audio_file,*data_file;
static bool descriptor_open,descriptor_closed;
static unsigned metadata_stats,descriptor_reads,source_switches,audio_track_reads[15],data_track_reads[15];
static uint32_t highest_audio_epoch,paused_track,pcm_first_read_offset;
static unsigned toc_completions,model_eofs,metadata_queries;
static uint8_t *private_audio,*private_descriptor;static size_t private_descriptor_bytes;
static uint8_t descriptor[2048];static uint32_t descriptor_bytes;
static unsigned model_observations;
static uint32_t last_observed_elapsed,failed_command_epoch;
static uint32_t model_bytes_read,model_confirmed_bytes,model_chunks,model_partial_aborts,model_partial_resets;
static uint32_t model_chunk_generation,model_progress_polls;
static uint8_t guest_initial[32768u+64u],incoming_initial[32768u+64u];
static struct kui_cdda_service_guard *model_guard;
static uint32_t model_epoch,model_lease_tick,model_gap,model_call;
static unsigned model_starts,model_enters,model_leaves,model_deadlines;
static bool model_busy,fault_reentry_checked;
static const struct cdda_disc_client_exports *original_exports;
static struct cdda_disc_client_exports wrapped_exports;
static unsigned bridge_depth,client_handoffs,worker_handoffs,probe_calls,client_clock_reads;
static unsigned vector_reads,vector_writes,map_calls,vector_calls,pure_calls,reentrant_calls;
static unsigned stack_initializations,stack_checks,context_reads,model_terminal_reads;
static uint32_t model_paused_frame,frozen_client_clock,vector_word=0x8c004000u;
static uint64_t model_pause_tick;
static uint32_t last_checked_elapsed;
struct request_ledger {uint32_t handle,epoch,command,track,fad,area,offset,destination,repeat,total,done,guest_bytes,polled_done;bool valid,cancelled;};
static struct request_ledger ledger;
static uint32_t model_queue_epoch;
static struct kui_cdda_disc_bios_work old_claim;
static bool old_claim_valid;
static unsigned stale_same_chunk,stale_old_request,stale_cancel,stale_reset,forged_destination;
static int16_t samples_left[KUI_CDDA_RING_FRAMES];
static int16_t samples_right[KUI_CDDA_RING_FRAMES];
struct playback_record {
    uint32_t track,epoch,first,end,loop_first,stop_cursor,prefetch;
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
    sim_ticks=UINT32_MAX-(uint64_t)KUI_CDDA_TMU_HZ*10u;clock_origin=sim_ticks;
}
uint32_t cdda_harness_host_ticks(void) {
    clock_reads++;advance(model_us_ticks(20u));return (uint32_t)sim_ticks;
}
static uint32_t hardware_frame(void) {
    return (uint32_t)((sim_ticks-key_tick)*44100u/KUI_CDDA_TMU_HZ);
}
static int16_t reference_sample(uint32_t track,uint32_t frame,unsigned channel) {
 if(track==14u && private_audio) {
  assert(frame<1805748u && channel<2u);
  uint32_t at=frame*4u+channel*2u;
  return (int16_t)((uint16_t)private_audio[at]|(uint16_t)private_audio[at+1u]<<8);
 }
 uint32_t period=track==2u?(channel?96u:64u):track==5u?(channel?120u:80u):(channel?150u:100u);
 return frame%period<period/2u?8192:-8192;
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
 bool padding=!p->repeat && elapsed>=p->end-p->first;
 uint32_t frame=source_frame(p,elapsed),at=elapsed%KUI_CDDA_RING_FRAMES;
 int16_t l=padding?0:reference_sample(p->track,frame,0),r=padding?0:reference_sample(p->track,frame,1);
 if(samples_left[at]!=l || samples_right[at]!=r) {
  fprintf(stderr,"disc bad ring track%u elapsed%u frame%u expected%d/%d actual%d/%d\n",p->track,
   elapsed,frame,l,r,samples_left[at],samples_right[at]);abort();
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
        if(result==KUI_CDDA_CONTROL_ENDED) {assert(!records[starts-1u].repeat);model_eofs++;}
        assert(control->current.frame==(result==KUI_CDDA_CONTROL_ENDED?
            records[starts-1u].end:source_frame(&records[starts-1u],last_observed_elapsed)));
    }
    return result;
}
enum kui_cdda_control_result model_control_fail(struct kui_cdda_control *control,uint32_t epoch) {
    enum kui_cdda_control_result result=kui_cdda_control_fail(control,epoch);
    /* Once retired, the planned deadline and unannounced I/O failures must
     * remain stopped through final cleanup. */
    if(result==KUI_CDDA_CONTROL_IO && injected && disc_errors) terminal_fault=true;
    return result;
}
enum kui_cdda_aica_result kui_cdda_aica_init(void) {
    assert(!running);return KUI_CDDA_AICA_OK;
}
enum kui_cdda_aica_result kui_cdda_aica_start(void) {
    assert(!running && audio_open && starts<MAX_STARTS && !terminal_fault);
    struct playback_record *p=&records[starts++];
    p->first=session_first;p->end=segment_end;p->loop_first=loop_first;p->repeat=repeat_audio;
    p->track=audio_file->number;p->epoch=command_control.current.pending_epoch;
    const struct reference_track *t=reference_track(p->track);
    assert(audio_file==t && t->audio && pcm.source.backing_offset==t->prefix &&
        pcm.source.file_bytes==t->file_bytes && pcm.total_frames==t->sectors*588u);
    assert(samples_left[0]==reference_sample(p->track,p->first,0) &&
        samples_right[0]==reference_sample(p->track,p->first,1));
    assert(p->epoch>highest_audio_epoch);highest_audio_epoch=p->epoch;
    if(starts>1u && records[starts-2u].track!=p->track) source_switches++;
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
int cdda_storage_named_stat(const char *path,uint32_t *bytes) {
 assert(storage_ready && path && bytes && !running && !bridge_depth);
 const struct reference_track *t=path_track(path);assert(t);metadata_stats++;
 if(CDDA_TEST_PROFILE==12) assert(t->number==14u);
 if(!injected && (fault==MAP_MISSING || fault==MAP_TRUNCATED || fault==MAP_OVERLAP) &&
    t->number==(CDDA_TEST_PROFILE==12?14u:4u)) {
  injected=true;
  if(fault==MAP_MISSING) return -1;
  *bytes=fault==MAP_TRUNCATED?t->file_bytes-1u:
      CDDA_TEST_PROFILE==12?3222u*2352u:t->file_bytes+2352u;
  return 0;
 }
 *bytes=t->file_bytes;return 0;
}
int cdda_storage_open(const char *path,uint32_t *bytes) {
 assert(storage_ready && !audio_open && path && bytes);
 const char *name=strrchr(path,'/');name=name?name+1:path;
 if(!strcmp(name,"fixture.gdi") || !strcmp(name,"TOY_COMMANDER.gdi")) {
  assert(!running && !starts && !bridge_depth && !descriptor_closed);
  const char *text=CDDA_TEST_PROFILE==12?selected_gdi:fixture_gdi;
  descriptor_bytes=private_descriptor?(uint32_t)private_descriptor_bytes:(uint32_t)strlen(text);
  assert(descriptor_bytes<sizeof(descriptor));
  memcpy(descriptor,private_descriptor?(const void *)private_descriptor:(const void *)text,descriptor_bytes);
  if(fault==MAP_PARSE) {descriptor[0]='0';injected=true;}
  if(fault==MAP_OVERLAP && CDDA_TEST_PROFILE==12) {
   const char *next="15 377422 4 2352";uint8_t *row=NULL;
   for(uint32_t i=0;i+strlen(next)<=descriptor_bytes;i++)
    if(!memcmp(descriptor+i,next,strlen(next))) {row=descriptor+i;break;}
   assert(row);memcpy(row+3u,"377271",6u);injected=true;
  }
  *bytes=descriptor_bytes;descriptor_open=true;audio_open=true;audio_file=NULL;return 0;
 }
 const struct reference_track *t=path_track(path);assert(t && t->audio);
 assert(descriptor_closed && !running);
 if(fault==SOURCE_SWITCH_FAULT && starts && !injected && t->number!=records[starts-1u].track) {
  injected=true;return -1;
 }
 *bytes=t->file_bytes;audio_file=t;audio_open=true;pcm_first_read_offset=UINT32_MAX;return 0;
}
int cdda_storage_read_at(uint32_t offset,void *out,uint32_t bytes) {
 assert(audio_open && out && bytes && ((uintptr_t)out&31u)==0u);
 if(descriptor_open) {
  assert(!running && !starts && !bridge_depth && offset<=descriptor_bytes && bytes<=descriptor_bytes-offset);
  descriptor_reads++;memcpy(out,descriptor+offset,bytes);blocks+=(bytes+511u)/512u;return 0;
 }
 assert(audio_file && audio_file->audio && bytes<=512u && offset>=audio_file->prefix &&
     offset<=audio_file->file_bytes && bytes<=audio_file->file_bytes-offset);
 if(pcm_first_read_offset==UINT32_MAX) pcm_first_read_offset=offset;
 advance(model_us_ticks(400u));pcm_reads++;blocks+=(bytes+511u)/512u;audio_track_reads[audio_file->number]++;
 if(fault==PCM_FAULT && running && !injected) {injected=true;return -1;}
 if(fault==REPRIME_FAULT && paused_track==audio_file->number && !running && !injected) {
  assert(command_control.current.pending);failed_command_epoch=command_control.current.pending_epoch;
  injected=true;return -1;
 }
 uint8_t *destination=out;
 for(uint32_t i=0;i<bytes;i++) {
  uint32_t at=offset+i-audio_file->prefix,frame=at/4u,channel=(at%4u)/2u;
  uint16_t value=(uint16_t)reference_sample(audio_file->number,frame,channel);
  destination[i]=(uint8_t)(value>>((at%2u)*8u));
 }
 return 0;
}
void cdda_storage_close(void) {
 if(descriptor_open) {descriptor_open=false;descriptor_closed=true;}
 audio_file=NULL;audio_open=false;
}
int cdda_storage_data_open_path(const char *path,uint32_t *bytes) {
 assert(storage_ready && path && bytes && bridge_depth>=2u && model_busy);
 if(data_open) cdda_storage_data_close();
 data_file=path_track(path);assert(data_file && !data_file->audio && CDDA_TEST_PROFILE==11);
 *bytes=data_file->file_bytes;data_open=true;data_open_calls++;return 0;
}
int cdda_storage_data_open(uint32_t *bytes) {(void)bytes;assert(false);return -1;}
static uint8_t independent_data_byte(uint32_t track,uint32_t at) {
 uint32_t x=at^(track*0x9e3779b9u);x^=x>>16;x*=0x7feb352du;
 x^=x>>15;x*=0x846ca68bu;x^=x>>16;return (uint8_t)x;
}
int cdda_storage_data_read_at(uint32_t offset,uint8_t *out,uint32_t bytes) {
 assert(data_open && data_file && out && ((uintptr_t)out&31u)==0u && bridge_depth>=2u && model_busy);
 assert(ledger.valid && ledger.command==KUI_GD_PIOREAD && ledger.track==data_file->number &&
     disc_data_job.state==KUI_CDDA_JOB_READING && disc_data_job.pending.offset==ledger.offset+ledger.done &&
     disc_data_job.pending.bytes==2048u && disc_data_job.pending.session_epoch==ledger.epoch);
 uint32_t logical=ledger.offset+ledger.done,sector=logical/2048u;
 assert(bytes==data_file->stride && offset==data_file->prefix+sector*data_file->stride &&
     offset<=data_file->file_bytes && bytes<=data_file->file_bytes-offset);
 advance(model_us_ticks(2000u));data_reads++;data_track_reads[data_file->number]++;blocks+=(bytes+511u)/512u;
 if(!injected && (fault==DATA_DELAY90 || (fault==DATA_DELAY200 && ledger.done>=2048u))) {
  injected=true;advance(model_us_ticks(fault==DATA_DELAY90?90000u:200000u));
 }
 if(!injected && fault==DATA_FAULT && ledger.done>=2048u) {injected=true;return -1;}
 if(data_file->stride==2352u) {
  memset(out,0,bytes);for(unsigned i=1u;i<11u;i++) out[i]=255u;out[15]=1u;
  for(unsigned i=0;i<2048u;i++) out[16u+i]=independent_data_byte(data_file->number,logical+i);
  if(fault==RAW_MODE_FAULT && !injected) {out[15]=2u;injected=true;}
 } else for(uint32_t i=0;i<bytes;i++) out[i]=independent_data_byte(data_file->number,logical+i);
 if(!injected && fault==DATA_CORRUPTION && ledger.done>=2048u) {
  injected=true;out[(data_file->stride==2352u?16u:0u)+1024u]^=1u;
 }
 model_bytes_read+=2048u;return 0;
}
void cdda_storage_data_close(void) {if(data_open) data_close_calls++;data_open=false;data_file=NULL;}
void cdda_storage_shutdown(void) {audio_file=data_file=NULL;audio_open=data_open=storage_ready=false;}
uint32_t cdda_storage_blocks_read(void) {return blocks;}
const char *cdda_storage_last_failure(void) {return "injected host mapped-file failure";}
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
uint32_t cdda_disc_host_address(const void *pointer,uint32_t bytes,bool writing) {
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
uint8_t *cdda_disc_host_map(uint32_t address,uint32_t bytes,int writing) {
    assert(writing>=0 && writing<=KUI_CDDA_DISC_BIOS_MAP_VALIDATE);map_calls++;
    return find_guest(address,bytes,writing);
}
uint32_t cdda_disc_host_vector_read(void) {
    vector_reads++;
    if(fault==RESTORE_READBACK_FAILURE && vector_writes==2u && !injected) {
        assert(vector_word==0x8c004000u);injected=true;return 0x8c010200u;
    }
    return vector_word;
}
void cdda_disc_host_vector_write(uint32_t value) {
    assert(!bridge_depth);vector_writes++;vector_word=value;
}
struct effects {
    uint64_t tick;
    unsigned clocks,positions,reads,data,writes,keyons,keyoffs,calls,maps;
    struct kui_cdda_service_guard guard;
    struct kui_cdda_control control;
    struct kui_cdda_job job;
    struct kui_cdda_disc_bios owner;
    bool audio,card_audio,card_data,native_busy;
};
static struct effects capture(void) {
    return (struct effects){sim_ticks,clock_reads,position_reads,pcm_reads,data_reads,
        hardware_write_calls,starts,hardware_stop_calls,disc_calls,map_calls,disc_guard,
        command_control,disc_data_job,disc_owner,running,audio_open,data_open,disc_native_busy};
}
static void assert_no_io(const struct effects *b) {
    assert(position_reads==b->positions && pcm_reads==b->reads && data_reads==b->data);
    assert(hardware_write_calls==b->writes && starts==b->keyons && hardware_stop_calls==b->keyoffs);
    assert(!memcmp(&command_control,&b->control,sizeof(command_control)));
    assert(!memcmp(&disc_data_job,&b->job,sizeof(disc_data_job)));
    assert(running==b->audio && audio_open==b->card_audio && data_open==b->card_data);
}
static void assert_unchanged(const struct effects *b) {
    assert_no_io(b);
    assert(sim_ticks==b->tick && clock_reads==b->clocks && disc_calls==b->calls+1u && map_calls==b->maps);
    assert(!memcmp(&disc_guard,&b->guard,sizeof(disc_guard)) && disc_native_busy==b->native_busy);
    assert(!memcmp(&disc_owner,&b->owner,sizeof(disc_owner)));
}
static uint32_t load32(const uint8_t *p) {
    assert(p);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static uint32_t reference_toc_word(uint32_t area,unsigned index) {
 if(index<99u) {
  if(index>=6u || (area?(index<2u):(index>=2u))) return UINT32_MAX;
  const struct reference_track *t=&tracks[index];
  return (t->audio?0u:0x40000000u)|0x01000000u|t->fad;
 }
 if(index==99u) return area?0x41030000u:0x41010000u;
 if(index==100u) return area?0x41060000u:0x01020000u;
 return area?0x41000000u|45857u:0x01000000u|245u;
}
static void verify_guest_prefix(bool discarded_chunk) {
 if(!ledger.valid || (ledger.command!=KUI_GD_PIOREAD && ledger.command!=KUI_GD_GETTOC2)) return;
 const uint8_t *g=find_guest(ledger.destination-32u,ledger.guest_bytes,1);assert(g);
 assert(ledger.done<=ledger.total);
 if(ledger.command==KUI_GD_PIOREAD) assert(!(ledger.done%2048u));
 for(uint32_t i=0;i<ledger.guest_bytes;i++) {
  if(i>=32u && i-32u<ledger.done) {
   uint32_t at=i-32u;
   uint8_t expected=ledger.command==KUI_GD_PIOREAD?independent_data_byte(ledger.track,ledger.offset+at):
     (uint8_t)(reference_toc_word(ledger.area,at/4u)>>((at%4u)*8u));
   assert(g[i]==expected);
  } else if(discarded_chunk && i>=32u+ledger.done && i<32u+ledger.done+2048u &&
       i<32u+ledger.total)
   assert(g[i]==guest_initial[i] || g[i]==independent_data_byte(ledger.track,ledger.offset+i-32u));
  else assert(g[i]==guest_initial[i]);
 }
}
static void refuse_callback(const struct kui_cdda_disc_bios_work *work) {
    struct effects before=capture();
    assert(!disc_read_work(work));
    assert(kui_cdda_disc_bios_complete(&disc_owner,work,true,work->bytes)==KUI_CDDA_DISC_BIOS_STALE);
    assert_no_io(&before);
    assert(sim_ticks==before.tick && clock_reads==before.clocks && disc_calls==before.calls &&
        map_calls==before.maps && !memcmp(&disc_guard,&before.guard,sizeof(disc_guard)) &&
        !memcmp(&disc_owner,&before.owner,sizeof(disc_owner)) && disc_native_busy==before.native_busy);
}
enum kui_cdda_disc_bios_result model_disc_take(struct kui_cdda_disc_bios *owner,
    struct kui_cdda_disc_bios_work *out) {
    enum kui_cdda_disc_bios_result result=kui_cdda_disc_bios_take(owner,out);
    if(result!=KUI_CDDA_DISC_BIOS_OK) return result;
    assert(owner==&disc_owner && ledger.valid && out->handle==ledger.handle && out->epoch==ledger.epoch);
    assert(out->chunk>model_chunk_generation && out->command==ledger.command);
    model_chunk_generation=out->chunk;
    if(out->kind==KUI_CDDA_DISC_BIOS_DATA) assert(out->offset==ledger.offset+ledger.done &&
        out->track==ledger.track && out->destination==ledger.destination+ledger.done && out->bytes==2048u &&
        out->total_bytes==ledger.total && out->committed==ledger.done && ledger.done<ledger.total);
    else if(out->kind==KUI_CDDA_DISC_BIOS_TOC) assert(out->bytes==408u && out->total_bytes==408u &&
        !out->committed && out->destination==ledger.destination && out->area==ledger.area);
    else assert(!out->bytes && !out->total_bytes && !out->committed);
    if(out->kind==KUI_CDDA_DISC_BIOS_AUDIO && out->command==KUI_CDDA_DISC_BIOS_PLAY) {
        const struct reference_track *t=reference_track(ledger.track);
        assert(t->audio && out->track==t->number && out->audio.first==0u && out->audio.end==t->sectors*588u &&
            out->audio.repeat==(ledger.repeat==15u));
    }
    if(out->kind==KUI_CDDA_DISC_BIOS_DATA) {
        if(old_claim_valid && old_claim.handle==out->handle && !stale_same_chunk) {
            refuse_callback(&old_claim);stale_same_chunk++;
        }
        if(old_claim_valid && old_claim.handle!=out->handle && !stale_old_request) {
            refuse_callback(&old_claim);stale_old_request++;
        }
        if(!forged_destination) {
            struct kui_cdda_disc_bios_work forged=*out;forged.destination+=2048u;
            refuse_callback(&forged);forged_destination++;
        }
        old_claim=*out;old_claim_valid=true;
    }
    return result;
}
enum kui_cdda_disc_bios_result model_disc_complete(struct kui_cdda_disc_bios *owner,
    const struct kui_cdda_disc_bios_work *work,bool success,uint32_t bytes) {
    enum kui_cdda_disc_bios_result result=kui_cdda_disc_bios_complete(owner,work,success,bytes);
    if(result!=KUI_CDDA_DISC_BIOS_OK) return result;
    assert(owner==&disc_owner && ledger.valid && work->handle==ledger.handle && work->epoch==ledger.epoch);
    if(success && work->kind==KUI_CDDA_DISC_BIOS_DATA) {
        assert(bytes==2048u && work->committed==ledger.done && work->offset==ledger.offset+ledger.done);
        ledger.done+=bytes;model_confirmed_bytes+=bytes;model_chunks++;
        assert(owner->completed_bytes==ledger.done);
        if(ledger.done<ledger.total) assert(owner->state==KUI_CDDA_DISC_BIOS_QUEUED &&
            owner->status==KUI_GD_PROCESSING && !owner->pending.bytes);
        else assert(owner->state==KUI_CDDA_DISC_BIOS_TERMINAL && owner->status==KUI_GD_COMPLETED);
    } else if(success && work->kind==KUI_CDDA_DISC_BIOS_TOC) {
        assert(bytes==408u && !ledger.done && owner->completed_bytes==408u &&
            owner->state==KUI_CDDA_DISC_BIOS_TERMINAL && owner->status==KUI_GD_COMPLETED);
        ledger.done=408u;
    } else if(!success) assert(!bytes && owner->completed_bytes==ledger.done &&
        owner->state==KUI_CDDA_DISC_BIOS_TERMINAL && owner->status==KUI_GD_FAILED);
    return result;
}
static unsigned model_requests,model_execs,model_checks,model_aborts,model_resets;
int32_t cdda_disc_host_vector_call(uint32_t r4,uint32_t r5,uint32_t r6,uint32_t r7) {
    assert(vector_word==0x8c010200u && bridge_depth>=1u);
    struct effects before=capture();struct request_ledger submitted={.command=r4};
    bool incoming_read=false;
    if(r7==KUI_GD_REQUEST && !r6 && !disc_native_busy) {
        uint32_t count=r4==KUI_GD_PIOREAD?16u:r4==KUI_GD_GETTOC2?8u:12u;
        const uint8_t *parameters=find_guest(r5,count,0);
        if(parameters && r4==KUI_GD_PIOREAD) {
            submitted.fad=load32(parameters);submitted.total=load32(parameters+4u)*2048u;
            submitted.destination=(load32(parameters+8u)&0x00ffffffu)|0x8c000000u;
            for(unsigned i=0;i<6u && CDDA_TEST_PROFILE==11;i++) {
                const struct reference_track *t=tracks+i;
                if(!t->audio && submitted.fad>=t->fad && submitted.fad<t->fad+t->sectors) {
                    submitted.track=t->number;submitted.offset=(submitted.fad-t->fad)*2048u;break;
                }
            }
        } else if(parameters && r4==KUI_GD_GETTOC2) {
            submitted.area=load32(parameters);submitted.total=408u;
            submitted.destination=(load32(parameters+4u)&0x00ffffffu)|0x8c000000u;
        } else if(parameters && r4==KUI_CDDA_DISC_BIOS_PLAY) {
            submitted.track=load32(parameters);submitted.repeat=load32(parameters+8u);
        }
        if(parameters && (r4==KUI_GD_PIOREAD || r4==KUI_GD_GETTOC2)) {
            for(unsigned i=0;i<guest_count;i++) if(guest_spans[i].address==submitted.destination-32u) {
                submitted.guest_bytes=guest_spans[i].bytes;
                assert(submitted.guest_bytes<=sizeof(incoming_initial));
                memcpy(incoming_initial,guest_spans[i].host,submitted.guest_bytes);incoming_read=true;break;
            }
        }
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
        if(result==-1 && ledger.valid) assert(disc_owner.state==KUI_CDDA_DISC_BIOS_TERMINAL &&
            disc_owner.status==KUI_GD_FAILED && disc_owner.error==KUI_GD_ERROR_IO &&
            disc_owner.completed_bytes==ledger.done);
        verify_guest_prefix(result==-1 && fault==LEAVE_DELAY200);
    }
    if(incoming_read) assert(!memcmp(find_guest(submitted.destination-32u,submitted.guest_bytes,1),
        incoming_initial,submitted.guest_bytes));
    if(r6) return result;
    if(r7==KUI_GD_REQUEST && result>0) {
        assert(!ledger.valid && (uint32_t)result>ledger.handle);
        submitted.handle=(uint32_t)result;submitted.epoch=++model_queue_epoch;submitted.valid=true;
        ledger=submitted;model_requests++;
        assert(disc_owner.work.command==submitted.command && disc_owner.work.handle==submitted.handle &&
            disc_owner.work.epoch==submitted.epoch);
        if(submitted.command==KUI_GD_PIOREAD || submitted.command==KUI_GD_GETTOC2) {
            assert(incoming_read && disc_owner.work.destination==submitted.destination &&
                disc_owner.work.bytes==submitted.total && disc_owner.work.total_bytes==submitted.total);
            if(submitted.command==KUI_GD_PIOREAD) assert(submitted.track && submitted.total>=2048u &&
                submitted.total<=32768u && disc_owner.work.offset==submitted.offset &&
                disc_owner.work.track==submitted.track);
            else assert(CDDA_TEST_PROFILE==11 && submitted.area<=1u && submitted.total==408u);
            memcpy(guest_initial,incoming_initial,submitted.guest_bytes);verify_guest_prefix(false);
        }
        if(submitted.command==KUI_CDDA_DISC_BIOS_PLAY) {
            const struct reference_track *t=reference_track(submitted.track);
            assert(t->audio && disc_owner.work.audio.first==0u && disc_owner.work.audio.end==t->sectors*588u &&
                disc_owner.work.audio.repeat==(submitted.repeat==15u));
        }
    } else if(r7==KUI_GD_CHECK) {
        model_checks++;
        if(ledger.valid && r4==ledger.handle) {
            const uint8_t *status=find_guest(r5,16u,1);assert(status && load32(status+8u)==ledger.done);
            if(result==KUI_GD_PROCESSING) {
                assert(!load32(status) && !load32(status+4u) && load32(status+12u)==4u);
                if(ledger.command==KUI_GD_PIOREAD && ledger.done>ledger.polled_done) {
                    assert(ledger.done-ledger.polled_done==2048u);model_progress_polls++;
                }
                ledger.polled_done=ledger.done;
            }
            if(result==KUI_GD_COMPLETED || result==KUI_GD_FAILED) {
                assert(!load32(status+12u));
                if(result==KUI_GD_COMPLETED) {
                    assert(!load32(status) && !load32(status+4u));
                    if(ledger.command==KUI_GD_PIOREAD) {
                        assert(ledger.done==ledger.total && !ledger.cancelled);verify_guest_prefix(false);
                        model_terminal_reads++;
                    } else if(ledger.command==KUI_GD_GETTOC2) {
                        assert(ledger.done==408u);verify_guest_prefix(false);toc_completions++;
                    }
                } else assert(load32(status)==1u && load32(status+4u)==
                    (ledger.cancelled?KUI_GD_ERROR_CANCELLED:KUI_GD_ERROR_IO));
                ledger.valid=false;
            }
        }
    } else if(r7==KUI_GD_ABORT && result==0) {
        assert(ledger.valid && r4==ledger.handle);verify_guest_prefix(false);
        ledger.cancelled=true;model_aborts++;if(ledger.done) {assert(ledger.done==2048u);model_partial_aborts++;}
        assert(disc_owner.completed_bytes==ledger.done);
        if(ledger.done && old_claim_valid && !stale_cancel) {refuse_callback(&old_claim);stale_cancel++;}
    } else if((r7==KUI_GD_RESET || r7==KUI_GD_INIT) && result==0) {
        verify_guest_prefix(false);if(ledger.done && ledger.valid) {assert(ledger.done==2048u);model_partial_resets++;}
        ledger.valid=false;model_queue_epoch++;model_resets++;
        if(ledger.done && old_claim_valid && !stale_reset) {refuse_callback(&old_claim);stale_reset++;}
    } else if(r7==KUI_GD_EXEC && result==0) {
        model_execs++;
        if(before.control.current.state==KUI_CDDA_CONTROL_PLAYING && queued_command==KUI_CDDA_DISC_BIOS_PAUSE &&
           disc_owner.state==KUI_CDDA_DISC_BIOS_TERMINAL && disc_owner.status==KUI_GD_COMPLETED) {
            assert(!running && !command_running && starts==before.keyons);
            model_paused_frame=command_control.current.frame;model_pause_tick=sim_ticks;paused_track=records[starts-1u].track;
            assert(model_paused_frame==records[starts-1u].stop_cursor && model_paused_frame!=records[starts-1u].prefetch);
        }
        if(queued_command==KUI_CDDA_DISC_BIOS_RELEASE && starts==before.keyons+1u) {
            assert(running && records[starts-1u].first==model_paused_frame &&
                sim_ticks-model_pause_tick>=KUI_CDDA_TMU_HZ);
            assert(records[starts-1u].track==paused_track && records[starts-1u].loop_first==0u &&
                records[starts-1u].end==reference_track(paused_track)->sectors*588u);
        }
        if(queued_command==KUI_GD_STOP && disc_owner.status==KUI_GD_COMPLETED) assert(!running && !command_running);
    }
    assert(disc_checked==model_confirmed_bytes);
    return result;
}
static uint32_t wrapped_diagnostic(void *context,struct cdda_disc_client_diagnostic *r) {
 struct effects before=capture();uint32_t result=original_exports->diagnostic(context,r);
 if(r->op==CDDA_DISC_CLIENT_TRACK_QUERY || r->op==CDDA_DISC_CLIENT_MAP_QUERY) {
  assert_no_io(&before);assert(sim_ticks==before.tick && clock_reads==before.clocks &&
      map_calls==before.maps && !memcmp(&disc_guard,&before.guard,sizeof(disc_guard)) &&
      !memcmp(&disc_owner,&before.owner,sizeof(disc_owner)));metadata_queries++;
  const struct reference_track *match=NULL;
  unsigned count=CDDA_TEST_PROFILE==12?1u:6u;
  for(unsigned i=0;i<count;i++) {
   const struct reference_track *t=CDDA_TEST_PROFILE==12?tracks+6:tracks+i;
   if(r->op==CDDA_DISC_CLIENT_TRACK_QUERY?t->number==r->value:
       r->aux && r->value>=t->fad && r->value<t->fad+t->sectors && r->aux<=t->fad+t->sectors-r->value) match=t;
  }
  assert(!result && !r->result && (r->query_result==0u)==(match!=NULL));
  if(match && r->op==CDDA_DISC_CLIENT_TRACK_QUERY) assert(r->track==match->number &&
      r->start_fad==match->fad && r->end_fad==match->fad+match->sectors && r->stride==match->stride &&
      r->control==(match->audio?0u:4u) && r->backing_offset==match->prefix && r->file_bytes==match->file_bytes &&
      r->source_frames==(match->audio?match->sectors*588u:0u) && r->map_count==count &&
      r->map_complete==(CDDA_TEST_PROFILE==11));
 }
 if(r->op==CDDA_DISC_CLIENT_SNAPSHOT && !result && !r->result) {
  assert(r->epoch==command_control.current.epoch && r->frame==command_control.current.frame &&
      r->track==disc_active_track && r->state==(uint32_t)command_control.current.state);
 }
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
        assert(!bridge_depth && entry==cdda_disc_client_entry && !original_exports);
        original_exports=context;wrapped_exports=*original_exports;
        assert(wrapped_exports.magic==CDDA_DISC_CLIENT_MAGIC && wrapped_exports.bytes==sizeof(wrapped_exports));
        wrapped_exports.clock=wrapped_clock;wrapped_exports.diagnostic=wrapped_diagnostic;client_handoffs++;bridge_depth++;
        uint32_t result=entry(&wrapped_exports);bridge_depth--;return result;
    }
    assert(destination==0x8c230000u && bridge_depth<=1u && disc_native_busy);worker_handoffs++;
    if(entry==disc_worker) assert(model_busy && disc_guard.state==KUI_CDDA_SERVICE_BUSY);
    else assert(entry==disc_fault_worker && !context);
    bridge_depth++;
    if(entry==disc_fault_worker && disc_installed && vector_word==0x8c010200u && !fault_reentry_checked &&
       true) {
        fault_reentry_checked=true;
        assert(cdda_disc_host_vector_call(0,0,0,KUI_GD_EXEC)==-1);
        assert(cdda_disc_host_vector_call(KUI_GD_STOP,UINT32_MAX,0,KUI_GD_REQUEST)==0);
        assert(cdda_disc_host_vector_call(UINT32_MAX,UINT32_MAX,0,KUI_GD_CHECK)==4);
    }
    uint32_t result=entry(context);
    if(entry==disc_worker && fault==LEAVE_DELAY200 && !injected && disc_completion_pending &&
       disc_completion_work.kind==KUI_CDDA_DISC_BIOS_DATA && ledger.done>=2048u) {
        assert(disc_owner.state==KUI_CDDA_DISC_BIOS_RUNNING && disc_data_job.state==KUI_CDDA_JOB_READY &&
            disc_checked==model_confirmed_bytes && ledger.done==2048u);
        injected=true;advance(model_us_ticks(200000u));
    }
    bridge_depth--;return result;
}
uint32_t kui_cdda_bridge_probe(uint32_t (*service)(void *),void *context) {
    assert(service && bridge_depth==1u);probe_calls++;(void)service(context);return 0u;
}
uint32_t cdda_disc_host_client_pc(void) {assert(bridge_depth==1u);return 0x8c300100u;}
uint32_t cdda_disc_host_client_sp(void) {assert(bridge_depth==1u);return 0x8c31ffe0u;}
uint32_t cdda_disc_host_caller_sp(void) {assert(bridge_depth>=1u);return bridge_depth==1u?0x8c31ffe0u:0x8c22ffe0u;}
uint32_t cdda_host_disc_sp(void) {assert(bridge_depth>=1u);return 0x8c22ffe0u;}
void cdda_host_disc_context(uint32_t *out) {
    assert(!bridge_depth && out);context_reads++;out[0]=0x400000f0u|(context_reads&1u);out[1]=out[2]=0;
}
void cdda_host_disc_stacks_init(void) {assert(!bridge_depth);stack_initializations++;}
bool cdda_host_disc_stack_check(unsigned which,uint32_t *used) {
    assert(which<2u && used && !bridge_depth);*used=which?34000u:2048u; /* Simulated guard result, not a native watermark. */stack_checks++;return true;
}
static uint8_t *load_private(const char *path,size_t expected,size_t *actual) {
 FILE *f=fopen(path,"rb");assert(f && !fseek(f,0,SEEK_END));long length=ftell(f);assert(length>0);
 assert(!fseek(f,0,SEEK_SET) && (!expected || (size_t)length==expected));
 uint8_t *out=malloc((size_t)length);assert(out && fread(out,1,(size_t)length,f)==(size_t)length);
 assert(!fclose(f));if(actual) *actual=(size_t)length;return out;
}
int main(int argc,char **argv) {
 assert(argc>=2 && argc<=4);
 if(argc>=3) {assert(CDDA_TEST_PROFILE==12);private_audio=load_private(argv[2],7222992u,NULL);}
 if(argc==4) {private_descriptor=load_private(argv[3],0,&private_descriptor_bytes);assert(private_descriptor_bytes<sizeof(descriptor));}
 if(!strcmp(argv[1],"pcm-fail")) fault=PCM_FAULT;
 else if(!strcmp(argv[1],"data-fail")) fault=DATA_FAULT;
 else if(!strcmp(argv[1],"data-corrupt")) fault=DATA_CORRUPTION;
 else if(!strcmp(argv[1],"data-delay90")) fault=DATA_DELAY90;
 else if(!strcmp(argv[1],"data-delay200")) fault=DATA_DELAY200;
 else if(!strcmp(argv[1],"leave-delay200")) fault=LEAVE_DELAY200;
 else if(!strcmp(argv[1],"command-reprime-fail")) fault=REPRIME_FAULT;
 else if(!strcmp(argv[1],"source-switch-fail")) fault=SOURCE_SWITCH_FAULT;
 else if(!strcmp(argv[1],"client-clock-stall")) fault=CLIENT_CLOCK_STALL;
 else if(!strcmp(argv[1],"restore-readback-fail")) fault=RESTORE_READBACK_FAILURE;
 else if(!strcmp(argv[1],"map-parse")) fault=MAP_PARSE;
 else if(!strcmp(argv[1],"map-missing")) fault=MAP_MISSING;
 else if(!strcmp(argv[1],"map-truncated")) fault=MAP_TRUNCATED;
 else if(!strcmp(argv[1],"map-overlap")) fault=MAP_OVERLAP;
 else if(!strcmp(argv[1],"raw-mode")) fault=RAW_MODE_FAULT;
 else assert(!strcmp(argv[1],"pass"));
 cdda_main();
 assert(finished && !running && !command_running && !audio_open && !data_open && !storage_ready);
 assert(!write_faults && !post_fault_writes && starts==stops && !bridge_depth && !model_busy);
 assert(data_open_calls==data_close_calls && stack_initializations==1u);
 assert(probe_calls==vector_calls-reentrant_calls && vector_word==0x8c004000u && !disc_installed);
 assert(disc_checked==model_confirmed_bytes && disc_chunks==model_chunks);
 bool metadata_fault=fault==MAP_PARSE || fault==MAP_MISSING || fault==MAP_TRUNCATED || fault==MAP_OVERLAP;
 if(metadata_fault) {
  assert(injected && failures==1u && reported_failures==1u && !starts && !client_handoffs &&
      !vector_calls && !vector_writes && !pcm_reads && !data_reads && !model_confirmed_bytes);
 } else {
  assert(client_handoffs==1u && disc_vector_restored==1u);
  assert(vector_writes==(fault==RESTORE_READBACK_FAILURE?3u:2u));
  if(fault==NO_FAULT) {
   assert(!failures && !reported_failures && !injected && completed==8u && stack_checks==2u);
   assert(descriptor_reads && descriptor_closed && metadata_stats==(CDDA_TEST_PROFILE==11?6u:1u));
   assert(!disc_errors && command_control.current.state==KUI_CDDA_CONTROL_STOPPED &&
       !ledger.valid && disc_owner.state==KUI_CDDA_DISC_BIOS_EMPTY);
   assert(reentrant_calls==1u && model_enters==model_leaves && context_reads==2u && disc_context_checks==1u);
   assert(model_requests==disc_accepted && disc_accepted==disc_completions &&
       model_progress_polls==disc_progress_polls && command_completions==(CDDA_TEST_PROFILE==11?7u:4u));
   if(CDDA_TEST_PROFILE==11) {
    assert(starts==5u && source_switches==3u && model_eofs==2u && highest_audio_epoch>=6u && command_control.current.epoch>=7u);
    assert(audio_track_reads[4] && audio_track_reads[5] && !audio_track_reads[2]);
    assert(model_confirmed_bytes==196608u && model_chunks==96u && data_track_reads[3]==64u &&
        data_track_reads[6]==32u && !data_track_reads[1] && model_progress_polls==90u);
    assert(toc_completions==2u && model_terminal_reads==6u && disc_completions==16u &&
        stale_same_chunk==1u && stale_old_request==1u && forged_destination==1u);
   } else {
    assert(starts==2u && !source_switches && model_eofs==1u && audio_track_reads[14] &&
        !data_reads && !model_confirmed_bytes && !toc_completions && disc_completions==5u);
    assert(records[0].end==1805748u && records[1].first==model_paused_frame &&
        records[1].end==1805748u && disc_elapsed.wraps==1u);
   }
  } else {
   assert(injected && failures==1u && reported_failures==1u && disc_errors &&
       command_control.current.state==KUI_CDDA_CONTROL_FAULT && !command_control.current.pending);
   if(fault==RESTORE_READBACK_FAILURE) assert(completed==7u && stack_checks==2u && vector_reads==4u);
   if(fault==SOURCE_SWITCH_FAULT) assert(CDDA_TEST_PROFILE==11 && starts==1u && model_eofs==1u);
   if(fault==REPRIME_FAULT) assert(failed_command_epoch && command_control.current.epoch==failed_command_epoch);
   if(fault==CLIENT_CLOCK_STALL) assert(client_clock_reads>=2000000u && client_clock_reads<2000100u);
   if(fault==DATA_FAULT || fault==DATA_CORRUPTION || fault==DATA_DELAY200 || fault==LEAVE_DELAY200)
    assert(ledger.valid && ledger.done==2048u && disc_owner.completed_bytes==2048u &&
        disc_owner.state==KUI_CDDA_DISC_BIOS_TERMINAL && disc_owner.error==KUI_GD_ERROR_IO);
   if(fault==RAW_MODE_FAULT) assert(ledger.valid && ledger.track==6u && !ledger.done &&
       disc_owner.state==KUI_CDDA_DISC_BIOS_TERMINAL && disc_owner.error==KUI_GD_ERROR_IO);
   if(fault==LEAVE_DELAY200) assert(model_deadlines==1u && disc_data_job.state==KUI_CDDA_JOB_FAULT &&
       !disc_data_job.done && !disc_data_job.pending.bytes);
  }
 }
 printf("disc BIOS profile%u %s: stages%u failures%u starts%u EOF%u switches%u EXEC%u vector%u"
   " checked%u chunks%u TOC%u prefix%u actions%u reentry%u elapsed_ms%llu\n",CDDA_TEST_PROFILE,argv[1],
   completed,failures,starts,model_eofs,source_switches,disc_execs,vector_calls,disc_checked,model_chunks,
   toc_completions,ledger.done,command_completions,reentrant_calls,
   (unsigned long long)((sim_ticks-clock_origin)*1000u/KUI_CDDA_TMU_HZ));
 free(private_audio);free(private_descriptor);return 0;
}
