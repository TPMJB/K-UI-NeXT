/* SPDX-License-Identifier: GPL-3.0-only */
/* The real worker request/service, Toy GD adapter and base GD protocol share
 * one fixture. Sound queue effects happen only in the separate ARM visit;
 * publishing a packet never dispatches it synchronously. */
#define KUI_TOY_PILOT_WORKER_TEST 1
#include "../src/loader/toy_pilot_worker.c"
#include "kui/toy_pilot_gd.h"
#include <assert.h>
#include <stdio.h>

#define GUEST UINT32_C(0x8c100000)
#define PARAM GUEST
#define CHECK (GUEST+32u)
#define RESPONSE (GUEST+64u)
#define FRAME_TICKS 13021u

uint8_t __toy_pilot_stack_bottom[64],__toy_pilot_stack_top[64],__toy_pilot_worker_end[32];
static struct kui_retail_manifest disc;
static struct kui_retail_gd gd;
static uint8_t guest_memory[4096], sound_memory[SOUND_END-SOUND_BASE];
static uint32_t sr, now, producer, consumer, bus_calls, leases, publications, dispatches, copies, reads, reports;
static uint32_t voice_start, voice_frames, starts, last_read_lba;
static uint32_t drain_reads, drain_resume_frame, first_after_resume;
static uint32_t drain_ack_delay,drain_begin;
static bool capture_next_read;
static bool voice_running, voice_looping, draining_pause;
static unsigned flag_delay;
static void clock_advance(uint32_t);
extern int32_t kui_toy_pilot_gd_dispatch(struct kui_retail_gd *,uint32_t,
    uint32_t,uint32_t,uintptr_t);
extern void kui_toy_pilot_pause_after(int32_t);

static uint32_t sound_get(uint32_t address) {
    uint32_t value;assert(address>=SOUND_BASE && address<=SOUND_END-4u);
    memcpy(&value,sound_memory+address-SOUND_BASE,4u);return value;
}
static void sound_set(uint32_t address,uint32_t value) {
    assert(address>=SOUND_BASE && address<=SOUND_END-4u);
    memcpy(sound_memory+address-SOUND_BASE,&value,4u);
}
static uint8_t *map(void *context,uint32_t address,uint32_t bytes,int writing) {
    (void)context;(void)writing;
    if(address<GUEST || address-GUEST>sizeof(guest_memory) ||
       bytes>sizeof(guest_memory)-(address-GUEST)) return NULL;
    return guest_memory+address-GUEST;
}
static int extent(void *context,uint32_t lba,uint32_t count,uint32_t bytes) {
    (void)context;(void)lba;(void)count;(void)bytes;return 0;
}
static int image_read(void *context,uint32_t lba,uint32_t count,uint32_t bytes,void *out) {
    (void)context;(void)lba;(void)count;(void)bytes;(void)out;
    assert(!"Scalar GD flow must not execute a game-data read");return -1;
}
static void put(uint32_t address,uint32_t value) { memcpy(map(NULL,address,4u,1),&value,4u); }
static uint32_t get(uint32_t address) { uint32_t value;memcpy(&value,map(NULL,address,4u,0),4u);return value; }
static int32_t base(uint32_t r4,uint32_t r5,uint32_t r6,uint32_t r7) {
    return kui_retail_gd_dispatch(&gd,r4,r5,r6,r7);
}
static int32_t syscall(uint32_t r4,uint32_t r5,uint32_t function) {
    uint32_t inherited=sr;
    int32_t result=kui_toy_pilot_gd_dispatch(&gd,r4,r5,function,(uintptr_t)base);
    assert(sr==inherited);return result;
}
uint32_t kui_toy_pilot_worker_test_sr(void) { return sr; }
void kui_toy_pilot_worker_test_set_sr(uint32_t value) { sr=value; }
void kui_toy_pilot_worker_test_terminal(void) { assert(sr==0x40000001u);++reports; }
uint32_t kui_toy_pilot_worker_test_read(uint32_t address,unsigned width) {
    (void)width;
    switch(address) {
    case 0x8c0a7318u:case 0x8c0a8940u:case 0x8c0af74cu:return 1u;
    case 0x8c112b08u:return QUEUE_BASE;
    case 0x8c112b0cu:return producer;
    case TIMER_TSTR:return 1u;
    case TIMER_TCOR:return UINT32_MAX;
    case TIMER_TCNT:return ~(now++);
    case TIMER_TCR:return 2u;
    case CLOCK_FRQCR:return 0xe0au;
    case CPU_CCR:return 0x101u;
    default:return 0u;
    }
}
enum kui_toy_pilot_bus_result kui_toy_pilot_bus_read(uint32_t address,uint32_t *value) {
    assert((sr&0xf0u)==0xf0u);++bus_calls;clock_advance(12u);
    *value=sound_get(address);return KUI_TOY_PILOT_BUS_OK;
}
enum kui_toy_pilot_bus_result kui_toy_pilot_bus_publish(const uint32_t words[4],uint32_t *slot) {
    assert((sr&0xf0u)==0xf0u);
    uint32_t next=(producer+1u)&31u,address=QUEUE_BASE+next*16u;
    if(sound_get(address)&0xffffu) return KUI_TOY_PILOT_BUS_BUSY;
    for(unsigned i=0;i<4u;i++) sound_set(address+i*4u,words[i]);
    producer=next;*slot=address;++publications;return KUI_TOY_PILOT_BUS_OK;
}
enum kui_toy_pilot_bus_result kui_toy_pilot_lease_allocate(uint32_t bytes,uint32_t alignment,uint32_t *address) {
    assert((sr&0xf0u)==0xf0u && bytes==KUI_TOY_PILOT_SOUND_BYTES && alignment==32u);
    *address=SOUND_END-bytes;++leases;return KUI_TOY_PILOT_BUS_OK;
}
enum kui_toy_pilot_bus_result kui_toy_pilot_bus_copy(uint32_t address,const void *source,uint32_t bytes) {
    assert((sr&0xf0u)==0xf0u && bytes && bytes<=256u && !(bytes&3u));
    assert(address>=owner.stats.sound_address && address<=SOUND_END-bytes);
    uint32_t offset=(address-owner.stats.sound_address)%65536u;
    assert(offset/8192u==(offset+bytes-1u)/8192u);
    if(voice_running) {
        uint32_t position=(uint32_t)((uint64_t)(now-voice_start)*44100u/781250u)%voice_frames;
        assert(voice_looping && offset/8192u!=position/4096u);
    }
    memcpy(sound_memory+address-SOUND_BASE,source,bytes);++copies;return KUI_TOY_PILOT_BUS_OK;
}
static int raw(uint32_t lba,uint32_t count,void *out) {
    assert((sr&0xf0u)==0xf0u && count==1u && !((uintptr_t)out&31u));
    assert((lba>=16u && lba+count<=48u) || (lba>=64u && lba+count<=72u));
    uint8_t *p=out;
    for(uint32_t frame=0;frame<count*588u;frame++) {
        uint16_t value=(uint16_t)(lba*588u+frame);
        p[frame*4u]=(uint8_t)value;p[frame*4u+1u]=(uint8_t)(value>>8);
        p[frame*4u+2u]=(uint8_t)~value;p[frame*4u+3u]=(uint8_t)(~value>>8);
    }
    if(capture_next_read) { first_after_resume=lba;capture_next_read=false; }
    ++reads;last_read_lba=lba;return 0;
}
/* Independently authored scalar queue protocol: dispatch in queue order,
 * then clear the consumed header. The worker cannot observe consumption
 * during the call that publishes a command. */
static void arm_dispatch(unsigned budget) {
    while(budget--) {
        uint32_t slot=QUEUE_BASE+consumer*16u,h=sound_get(slot);
        uint32_t opcode=h&0xffffu,port=(h>>16)&255u;
        if(!opcode) break;
        if(opcode==0xff9du) {
            assert(sound_get(slot+4u)==PORT_MASK);voice_running=false;flag_delay=6u;
        } else if(opcode==0xff91u || opcode==0xff90u) {
            assert(port==62u || port==63u);
            uint32_t template=SOUND_BASE+0x2ca8u+(port-62u)*0x48u;
            if(opcode==0xff91u) {
                for(unsigned offset=0;offset<0x48u;offset+=4u) sound_set(template+offset,0u);
                sound_set(template,0xc000u);
            } else {
                uint32_t address=sound_get(slot+4u),bytes=sound_get(slot+8u);
                assert(bytes && !(bytes&1u) && !sound_get(slot+12u));
                sound_set(template,sound_get(template)|((address>>16)&0x7fu));
                sound_set(template+4u,address&0xffffu);sound_set(template+8u,0u);
                sound_set(template+12u,bytes/2u-1u);sound_set(template+0x18u,0u);
            }
        } else if(opcode==0xff9cu) {
            assert(sound_get(slot+4u)==PORT_MASK && !sound_get(slot+8u));
            voice_frames=sound_get(SOUND_BASE+0x2ca8u+12u)+1u;
            assert(voice_frames==sound_get(SOUND_BASE+0x2cf0u+12u)+1u);
            voice_start=now;voice_running=true;voice_looping=(h>>16)&1u;flag_delay=0u;++starts;
            for(unsigned channel=0;channel<2u;channel++) {
                uint32_t t=SOUND_BASE+0x2ca8u+channel*0x48u;
                sound_set(t,sound_get(t)|(voice_looping?0x200u:0u));
            }
            sound_set(SOUND_BASE+0x14a4u,0xffff0000u);
            sound_set(SOUND_BASE+0x15e8u,0u);sound_set(SOUND_BASE+0x15ecu,0u);
        } else assert(opcode==0xff96u || opcode==0xff97u);
        sound_set(slot,h&0xffff0000u);consumer=(consumer+1u)&31u;++dispatches;
        sound_set(SOUND_BASE+0x399cu,0xb200u+consumer*16u);
    }
}
static void clock_advance(uint32_t elapsed) {
    now+=elapsed;
    if(!voice_running) {
        if(flag_delay && !--flag_delay) sound_set(SOUND_BASE+0x14a4u,0u);
        return;
    }
    uint32_t position=(uint32_t)((uint64_t)(now-voice_start)*44100u/781250u);
    if(voice_looping) position%=voice_frames;
    else if(position>=voice_frames) {
        position=voice_frames-1u;voice_running=false;sound_set(SOUND_BASE+0x14a4u,0u);
    }
    sound_set(SOUND_BASE+0x15e8u,position);sound_set(SOUND_BASE+0x15ecu,position);
}
uint32_t kui_toy_pilot_pause_test_sr(void) { return sr; }
void kui_toy_pilot_pause_test_set_sr(uint32_t value) { sr=value; }
uint32_t kui_toy_pilot_pause_test_game_sp(void) { return GUEST+sizeof(guest_memory); }
void kui_toy_pilot_pause_test_pump(uint32_t context,uint32_t borrowed_sp) {
    (void)context;(void)borrowed_sp;
    assert(!"A successful native PAUSE must not invoke an extra native SDK pump");
}
uint32_t kui_toy_pilot_pause_test_read(uint32_t address,unsigned width) {
    assert(draining_pause && address==TIMER_TCNT && width==4u);
    /* The hardware timer and ARM continue while the normal game updater is
     * suspended in pause_after. This independently scheduled visit is only
     * triggered by the pause adapter's clock read, never by packet publish. */
    clock_advance(1000u);
    if(now-drain_begin>=drain_ack_delay) arm_dispatch(32u);
    if(!drain_reads) drain_resume_frame=owner.resume_frame;
    ++drain_reads;return ~now;
}
static void native_pause_success(void) {
    uint32_t inherited=sr;drain_reads=drain_resume_frame=0u;draining_pause=true;drain_begin=now;
    kui_toy_pilot_pause_after(0);
    draining_pause=false;assert(sr==inherited && !reports && !owner.stats.fault);
    assert(!kui_toy_pilot_pause_pumps && !kui_toy_pilot_pause_detail);
}
static void worker(void) {
    uint32_t inherited=sr;kui_toy_pilot_worker_step();assert(sr==inherited);
    assert(!owner.stats.fault && !reports && !owner.stats.active_bank_writes);
}
static void frame(void) { clock_advance(FRAME_TICKS);arm_dispatch(32u);worker(); }
static void prepare(void) {
    memset(&disc,0,sizeof(disc));disc.track_count=4u;
    disc.slots[0].track=(struct kui_retail_track){.start_lba=0,.end_lba=8,.control=4};
    disc.slots[1].track=(struct kui_retail_track){.start_lba=16,.end_lba=48,.extent_count=1};
    disc.slots[2].track=(struct kui_retail_track){.start_lba=64,.end_lba=72,.extent_count=1};
    disc.slots[3].track=(struct kui_retail_track){.start_lba=45000,.end_lba=60000,.control=4};
    assert((uintptr_t)&disc<=UINT32_MAX && (uintptr_t)raw<=UINT32_MAX);
    const struct kui_gd_ops ops={NULL,map,extent,image_read};
    assert(!kui_retail_gd_init(&gd,disc.slots,disc.track_count,&ops,GUEST,GUEST+sizeof(guest_memory)));
    /* The launch starts in the admitted high-density data session. Idle
     * controls must preserve that data position until a PLAY selects audio. */
    kui_retail_gd_set_disc_type(&gd,0x80u,45000u);
    kui_toy_pilot_worker_test_prepare(KUI_TOY_PILOT_STOPPED,0u,false);
    owner.mailbox.first_fad=owner.mailbox.end_fad=owner.mailbox.track=0u;
    owner.config.manifest=(uint32_t)(uintptr_t)&disc;owner.config.read_raw=(uint32_t)(uintptr_t)raw;
    owner.config.resident_active=0x8c004010u;owner.config.data_pending=0x8c004014u;
    memset(guest_memory,0,sizeof(guest_memory));memset(sound_memory,0,sizeof(sound_memory));
    sound_set(SOUND_BASE+0x1464u,0x800000u);sound_set(SOUND_BASE+0xe0u,0x1468u);
    sound_set(SOUND_BASE+0xe8u,0x14f0u);sound_set(SOUND_BASE+0xecu,0x30040u);
    sound_set(SOUND_BASE+0x399cu,0xb200u);
    sr=0x40000001u;now=consumer=bus_calls=leases=publications=dispatches=copies=reads=reports=0u;
    producer=0xffffu;voice_start=voice_frames=starts=last_read_lba=0u;
    voice_running=voice_looping=false;flag_delay=0u;
    draining_pause=capture_next_read=false;drain_reads=drain_resume_frame=first_after_resume=0u;
    drain_ack_delay=drain_begin=0u;
}
static uint32_t accept(uint32_t command) {
    int32_t token=syscall(command,PARAM,KUI_GD_REQUEST);assert(token>0);
    assert(syscall(0,0,KUI_GD_EXEC)==0);
    assert(syscall((uint32_t)token,CHECK,KUI_GD_CHECK)==KUI_GD_COMPLETED);
    assert(!gd.pending && !gd.command);return (uint32_t)token;
}
static void play(uint32_t track,uint32_t repeats) {
    put(PARAM,track);put(PARAM+4u,track);put(PARAM+8u,repeats);(void)accept(KUI_RETAIL_GD_PLAY);
}
static uint32_t drive(void) { assert(syscall(RESPONSE,0,KUI_GD_DRIVE)==0);return get(RESPONSE); }
static void status(uint32_t expected_drive,uint32_t expected_track,uint32_t expected_fad) {
    for(unsigned i=0;i<4u;i++) put(PARAM+i*4u,RESPONSE+i*4u);
    (void)accept(KUI_RETAIL_GD_REQ_STAT);
    if(get(RESPONSE)!=expected_drive || get(RESPONSE+4u)!=expected_track)
        fprintf(stderr,"status: got drive=%u track=%u fad=%u; expected %u/%u/%u\n",
            get(RESPONSE),get(RESPONSE+4u),get(RESPONSE+8u)&0x00ffffffu,
            expected_drive,expected_track,expected_fad);
    assert(get(RESPONSE)==expected_drive && get(RESPONSE+4u)==expected_track);
    assert((get(RESPONSE+8u)&0x00ffffffu)==expected_fad && get(RESPONSE+12u)==1u);
}
static void subcode(uint32_t expected_audio) {
    put(PARAM,1u);put(PARAM+4u,14u);put(PARAM+8u,RESPONSE);
    (void)accept(KUI_RETAIL_GD_GETSCD);
    assert(guest_memory[RESPONSE-GUEST+1u]==expected_audio);
}
static void startup_scalar_controls(void) {
    prepare();assert(syscall(0,0,KUI_GD_INIT)==0);worker();
    (void)accept(KUI_GD_COMMAND_INIT);worker();
    assert(drive()==1u);status(1u,4u,45150u);subcode(0x15u);
    (void)accept(KUI_RETAIL_GD_PAUSE);
    assert(owner.stats.generation!=owner.stats.applied_generation);
    assert(drive()==1u);status(1u,4u,45150u);subcode(0x12u);
    native_pause_success();assert(drain_reads==1u);
    assert(owner.stats.state==KUI_TOY_PILOT_PAUSED && owner.stats.generation==owner.stats.applied_generation);
    assert(drive()==1u);status(1u,4u,45150u);subcode(0x12u);
    (void)accept(KUI_RETAIL_GD_RELEASE);
    assert(owner.stats.generation!=owner.stats.applied_generation);
    /* No PLAY selected an audio source. RELEASE is an idle control, not a
     * logical playing track, even before the worker handles its mailbox. */
    assert(drive()==1u);status(1u,4u,45150u);subcode(0x15u);
    worker();
    assert(owner.stats.state==KUI_TOY_PILOT_STOPPED && owner.stats.generation==owner.stats.applied_generation);
    assert(drive()==1u);status(1u,4u,45150u);subcode(0x15u);
    assert(syscall(0,0,KUI_GD_RESET)==0);worker();
    assert(!bus_calls && !leases && !publications && !reads && !copies);
}
static void delayed_application_and_eof(void) {
    prepare();play(2u,0u);
    assert(owner.stats.generation!=owner.stats.applied_generation && !reads && !publications);
    assert(drive()==3u);status(3u,2u,166u);
    worker();assert(publications==1u && !dispatches && !owner.stats.started_observed);
    for(unsigned i=0;i<1000u && owner.stats.state!=KUI_TOY_PILOT_EOF;i++) {
        frame();assert(drive()==(owner.stats.state==KUI_TOY_PILOT_EOF?1u:3u));
    }
    assert(owner.stats.state==KUI_TOY_PILOT_EOF && starts==1u);
    assert(owner.stats.retired_frames==32u*588u && owner.stats.filled_frames==32u*588u);
    assert(owner.stats.applied_generation==owner.stats.generation && owner.model.active_bank==NONE);
    status(1u,2u,197u);assert(reads && copies && leases==1u);
}
static void collapsed_pause_release(void) {
    prepare();play(2u,0u);
    (void)accept(KUI_RETAIL_GD_PAUSE);
    assert(drive()==1u);status(1u,2u,166u);subcode(0x12u);
    (void)accept(KUI_RETAIL_GD_RELEASE);
    assert(!reads && !publications && drive()==3u);
    status(3u,2u,166u);subcode(0x11u);
    for(unsigned i=0;i<1000u && owner.stats.state!=KUI_TOY_PILOT_EOF;i++) frame();
    assert(owner.stats.state==KUI_TOY_PILOT_EOF && owner.stats.track==2u);
    assert(owner.stats.filled_frames==32u*588u && owner.stats.retired_frames==32u*588u);
}
static void queried_play_reset_release_before_service(void) {
    prepare();play(2u,0u);
    assert(drive()==3u);status(3u,2u,166u);subcode(0x11u);
    /* A response publishes the pending PLAY selection into the RAM
     * snapshot, although no worker has selected or filled the source. */
    assert(owner.stats.track==2u && owner.selected_play_generation==0u);
    assert(!reads && !publications && gd.position_lba==16u);
    assert(syscall(0,0,KUI_GD_RESET)==0);
    (void)accept(KUI_RETAIL_GD_RELEASE);
    assert(!owner.mailbox.play_generation && !owner.selected_play_generation);
    assert(owner.stats.generation!=owner.stats.applied_generation);
    assert(drive()==1u);status(1u,2u,166u);subcode(0x15u);
    const struct kui_toy_pilot_snapshot *p=kui_toy_pilot_snapshot();
    assert(!p->track && !p->position_fad && !p->end_fad);
    /* GD RESET retains the last queried scalar position. An idle release
     * cancels audio selection without inventing a seek to the data session. */
    assert(gd.position_lba==16u);worker();
    assert(owner.stats.state==KUI_TOY_PILOT_STOPPED);
    assert(owner.stats.generation==owner.stats.applied_generation);
    assert(drive()==1u);status(1u,2u,166u);subcode(0x15u);
    p=kui_toy_pilot_snapshot();assert(!p->track && !p->position_fad && !p->end_fad);
    assert(!bus_calls && !leases && !publications && !reads && !copies);
}
static void selection_survives_pause_before_service(void) {
    prepare();play(2u,0u);(void)accept(KUI_RETAIL_GD_PAUSE);
    assert(drive()==1u);status(1u,2u,166u);subcode(0x12u);worker();
    assert(owner.stats.track==2u && owner.stats.position_fad==166u);
    assert(owner.stats.state==KUI_TOY_PILOT_PAUSED && !reads && !publications);
    play(3u,0u);(void)accept(KUI_RETAIL_GD_PAUSE);
    assert(drive()==1u);status(1u,3u,214u);subcode(0x12u);worker();
    assert(owner.stats.track==3u && owner.stats.position_fad==214u && owner.stats.end_fad==222u);
    assert(owner.stats.state==KUI_TOY_PILOT_PAUSED && !reads && !publications);
    status(1u,3u,214u);(void)accept(KUI_RETAIL_GD_RELEASE);
    for(unsigned i=0;i<1000u && owner.stats.state!=KUI_TOY_PILOT_EOF;i++) frame();
    assert(owner.stats.state==KUI_TOY_PILOT_EOF && starts==1u && last_read_lba>=64u);
    assert(owner.stats.filled_frames==8u*588u && owner.stats.retired_frames==8u*588u);
    status(1u,3u,221u);
}
static void repeat_policy_survives_pause_before_service(void) {
    prepare();play(3u,15u);(void)accept(KUI_RETAIL_GD_PAUSE);
    assert(drive()==1u);status(1u,3u,214u);subcode(0x12u);worker();
    assert(owner.stats.track==3u && owner.stats.position_fad==214u && owner.repeat_left==15u);
    assert(owner.stats.state==KUI_TOY_PILOT_PAUSED && !reads && !publications);
    (void)accept(KUI_RETAIL_GD_RELEASE);
    assert(drive()==3u);status(3u,3u,214u);subcode(0x11u);
    for(unsigned i=0;i<200u;i++) frame();
    assert(starts==1u && owner.repeat_left==15u && owner.stats.track==3u);
    assert(owner.stats.state!=KUI_TOY_PILOT_EOF && owner.stats.retired_frames>=8u*588u);
    assert(owner.stats.filled_frames>=16u*588u && last_read_lba>=64u);
    (void)accept(KUI_GD_STOP);
    for(unsigned i=0;i<1000u && owner.stats.applied_generation!=owner.stats.generation;i++) frame();
    assert(owner.stats.state==KUI_TOY_PILOT_STOPPED && !voice_running);
    assert(owner.model.active_bank==NONE && owner.model.pending_bank==NONE);
}
static void native_pause_drains_before_movie(void) {
    prepare();play(2u,0u);
    for(unsigned i=0;i<1000u && owner.stats.state!=KUI_TOY_PILOT_PLAYING;i++) frame();
    assert(owner.stats.state==KUI_TOY_PILOT_PLAYING && owner.model.active_bank!=NONE && voice_running);
    frame();assert(owner.stats.cursor_left>0u && owner.stats.cursor_left<KUI_TOY_PILOT_BANK_FRAMES);
    uint32_t observed_cursor=owner.stats.cursor_left;
    (void)accept(KUI_RETAIL_GD_PAUSE);
    assert(owner.stats.applied_generation!=owner.stats.generation);
    drain_ack_delay=390625u; /*500ms independently deferred ARM STOP dispatch.*/
    native_pause_success();
    assert(now-drain_begin>781250u && now-drain_begin<1562500u);
    assert(drain_reads>1u && owner.resume_frame>=observed_cursor-64u);
    assert(owner.stats.state==KUI_TOY_PILOT_PAUSED && !voice_running && !owner.stop_wait);
    assert(owner.stats.applied_generation==owner.stats.generation);
    assert(owner.model.active_bank==NONE && owner.model.pending_bank==NONE);
    assert(owner.resume_frame>=drain_resume_frame && owner.resume_frame<KUI_TOY_RING_HALF);
    uint32_t resume=owner.resume_frame, old_reads=reads;
    clock_advance(CLOCK_GAP_LIMIT+FRAME_TICKS); /* Native movie has no worker visits. */
    (void)accept(KUI_RETAIL_GD_RELEASE);capture_next_read=true;
    for(unsigned i=0;i<1000u && reads==old_reads;i++) frame();
    /* A visit can refill several sectors; verify the first resumed source. */
    assert(reads>old_reads && !capture_next_read && first_after_resume==16u+resume/588u);
    assert(owner.stats.track==2u && owner.first_fad==166u);
    for(unsigned i=0;i<1000u && owner.stats.state!=KUI_TOY_PILOT_EOF;i++) frame();
    assert(owner.stats.state==KUI_TOY_PILOT_EOF && starts>=2u && !reports);
}
int main(void) {
    startup_scalar_controls();delayed_application_and_eof();collapsed_pause_release();
    queried_play_reset_release_before_service();
    selection_survives_pause_before_service();repeat_policy_survives_pause_before_service();
    native_pause_drains_before_movie();
    puts("Toy integrated ring flow: real request/GD acceptance, idle controls, asynchronous ARM, collapsed controls, repeat metadata, native PAUSE drain and EOF pass");
    return 0;
}
