/* SPDX-License-Identifier: GPL-3.0-only */
/* Exercise the actual worker against an independent asynchronous PCM consumer.
 * AICA playback advances during card and G2 operations. The fixture checks the
 * audible sample sequence and physical copy destinations, not model bank IDs.
 */
#define KUI_TOY_PILOT_WORKER_TEST 1
#ifndef TOY_WORKER_SOURCE
#define TOY_WORKER_SOURCE "../src/loader/toy_pilot_worker.c"
#endif
#include TOY_WORKER_SOURCE
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>

uint8_t __toy_pilot_stack_bottom[64],__toy_pilot_stack_top[64],__toy_pilot_worker_end[32];
volatile uint32_t kui_toy_pilot_native_diagnostics[8];
static uint8_t sound_memory[SOUND_END-SOUND_BASE];
static struct kui_retail_manifest admitted;
static uint32_t sr,entry_sr,now,producer,consumer,packets,copies,reads,reports;
static uint32_t starts,stops,data_command,raw_ticks,copy_ticks,read_ticks;
static uint32_t raw_tail_period,raw_tail_ticks;
static uint32_t copies_at_last_read;
static uint32_t pcm_address[2],pcm_frames[2],voice_started,voice_rendered;
static uint32_t published_position,source_frames,checked_frames,silent_frames;
static uint32_t physical_phase[2],published_channel_position[2];
static uint32_t right_phase,flag_delay,stop_dispatched;
static uint32_t cursor_interval,cursor_tick,cursor_bias;
static uint32_t serial_period,serial_gap,serial_left_delay,serial_right_delay;
static uint32_t serial_scan,serial_event,serial_step,serial_saved[2],serial_offset;
static uint32_t serial_sh_left;
static uint32_t serial_card_publications,serial_copy_publications,torn_observations,torn_halves,torn_wraps;
static uint32_t timer_reads,diagnostic_revokes;
static uint32_t window_start_observations,window_half_observations;
static uint16_t first_sample[16];
static bool running,looping,hold_queue,hold_cursor,check_pcm,repeat_pcm;
static bool stop_queued,stopped_live_voice,defer_right_copy;
static bool revoke_before_mask;
static bool serial_cursor,in_card,in_copy,serial_sh_left_seen;
static bool strict_source_reads,retry_pending;
static uint32_t expected_read_lba,deferred_reads,deferred_address,deferred_bank;
static uint32_t deferred_filled,deferred_fill_frame,deferred_stream,deferred_raw_lba,deferred_raw_generation;
static uint8_t deferred_raw[KUI_TOY_PILOT_RAW_BYTES];

static uint32_t sound_get(uint32_t address) {
    uint32_t value;assert(address>=SOUND_BASE && address<=SOUND_END-4u);
    memcpy(&value,sound_memory+address-SOUND_BASE,4u);return value;
}
static void sound_set(uint32_t address,uint32_t value) {
    assert(address>=SOUND_BASE && address<=SOUND_END-4u);
    memcpy(sound_memory+address-SOUND_BASE,&value,4u);
}
static int16_t sample(uint32_t address) {
    int16_t value;assert(address>=SOUND_BASE && address<=SOUND_END-2u);
    memcpy(&value,sound_memory+address-SOUND_BASE,2u);return value;
}
static uint32_t physical_channel_position(unsigned channel) {
    assert(channel<2u && pcm_frames[channel]);
    return (voice_rendered+physical_phase[channel])%pcm_frames[channel];
}
static uint32_t physical_position(void) { return physical_channel_position(0u); }
static void render(void) {
    if(!running) return;
    uint32_t target=(uint32_t)((uint64_t)(now-voice_started)*44100u/781250u);
    if(!looping && target>pcm_frames[0]) target=pcm_frames[0];
    while(voice_rendered<target) {
        uint32_t position[2]={physical_channel_position(0u),physical_channel_position(1u)};
        int16_t left=sample(pcm_address[0]+position[0]*2u);
        int16_t right=sample(pcm_address[1]+position[1]*2u);
        if(check_pcm) {
            if(repeat_pcm || checked_frames<source_frames) {
                uint32_t left_source=checked_frames+physical_phase[0],right_source=checked_frames+physical_phase[1];
                if(repeat_pcm) { left_source%=source_frames;right_source%=source_frames; }
                assert((uint16_t)left==(left_source<source_frames?(uint16_t)left_source:0u));
                assert((uint16_t)right==(right_source<source_frames?(uint16_t)~right_source:0u));
            } else { assert(!left && !right);++silent_frames; }
            ++checked_frames;
        }
        ++voice_rendered;
    }
    if(!looping && voice_rendered==pcm_frames[0]) running=false;
}
/* The ARM scans the two ports serially. Each hardware capture and later
 * shared-table write is a time event, independent of SH cursor reads. A
 * delayed publication retains its captured value while PCM keeps playing. */
static void serial_publication(void) {
    unsigned channel=serial_step/2u;
    if(!(serial_step&1u)) {
        if(!hold_cursor && pcm_frames[0])
            serial_saved[channel]=(physical_channel_position(channel)+cursor_bias+(channel?right_phase:0u))%pcm_frames[channel];
    } else if(!hold_cursor) {
        sound_set(SOUND_BASE+0x15e8u+channel*4u,serial_saved[channel]);
        if(in_card) ++serial_card_publications;
        if(in_copy) ++serial_copy_publications;
    }
    switch(serial_step++) {
    case 0u:serial_event+=serial_left_delay;break;
    case 1u:serial_event+=serial_gap;break;
    case 2u:serial_event+=serial_right_delay;break;
    default:serial_step=0u;serial_scan+=serial_period;serial_event=serial_scan;break;
    }
}
static void advance(uint32_t ticks) {
    uint32_t target=now+ticks;
    if(serial_cursor && running) while(serial_event<=target) {
        assert(serial_event>=now);now=serial_event;render();serial_publication();
    }
    now=target;render();
}
static void publish_cursor(void) {
    render();
    if(!serial_cursor && !hold_cursor && pcm_frames[0] && now-cursor_tick>=cursor_interval) {
        published_position=(physical_position()+cursor_bias)%pcm_frames[0];
        published_channel_position[0]=published_position;
        published_channel_position[1]=(physical_channel_position(1u)+cursor_bias+right_phase)%pcm_frames[1];
        cursor_tick=now;
    }
    if(!serial_cursor) {
        sound_set(SOUND_BASE+0x15e8u,published_channel_position[0]);
        sound_set(SOUND_BASE+0x15ecu,published_channel_position[1]);
    }
    if(!running && flag_delay && !--flag_delay) sound_set(SOUND_BASE+0x14a4u,0u);
}
uint32_t kui_toy_pilot_worker_test_sr(void) { return sr; }
void kui_toy_pilot_worker_test_set_sr(uint32_t value) {
    /* Deterministic lifecycle interrupt between the worker's SR load and
     * mask publication. A real interrupt cannot enter once IMASK is set. */
    if(revoke_before_mask && (sr&0xf0u)!=0xf0u && (value&0xf0u)==0xf0u) {
        revoke_before_mask=false;++diagnostic_revokes;
        uint32_t before=sr;kui_toy_pilot_worker_revoke();assert(sr==before);
    }
    sr=value;
}
void kui_toy_pilot_worker_test_terminal(void) { assert(sr==entry_sr);++reports; }
uint32_t kui_toy_pilot_worker_test_read(uint32_t address,unsigned width) {
    (void)width;
    switch(address) {
    case 0x8c0a7318u:case 0x8c0a8940u:case 0x8c0af74cu:return 1u;
    case 0x8c004010u:return 0u;
    case 0x8c004014u:return data_command;
    case 0x8c112b08u:return QUEUE_BASE;
    case 0x8c112b0cu:return producer;
    case TIMER_TSTR:return 1u;
    case TIMER_TCOR:return UINT32_MAX;
    case TIMER_TCNT:++timer_reads;advance(1u);return ~now;
    case TIMER_TCR:return 2u;
    case CLOCK_FRQCR:return 0xe0au;
    case CPU_CCR:return 0x101u;
    default:return 0u;
    }
}
enum kui_toy_pilot_bus_result kui_toy_pilot_bus_read(uint32_t address,uint32_t *value) {
    assert((sr&0xf0u)==0xf0u);advance(read_ticks);publish_cursor();
    *value=sound_get(address);
    if(serial_cursor && running && address==SOUND_BASE+0x15e8u) {
        serial_sh_left=*value;serial_sh_left_seen=true;
    }
    if(serial_cursor && running && serial_sh_left_seen && address==SOUND_BASE+0x15ecu) {
        /* Count the pair SH actually read, including an ARM publication
         * between the two G2 reads, rather than a simultaneous table view. */
        uint32_t l=serial_sh_left,r=*value;serial_sh_left_seen=false;
        uint32_t d=l>r?l-r:r-l;
        if(d>64u && d<32768u-64u) ++torn_observations;
        if(l/16384u!=r/16384u) ++torn_halves;
        if((l<8192u && r>24576u) || (r<8192u && l>24576u)) ++torn_wraps;
    }
    return KUI_TOY_PILOT_BUS_OK;
}
enum kui_toy_pilot_bus_result kui_toy_pilot_bus_publish(const uint32_t packet[4],uint32_t *slot) {
    assert((sr&0xf0u)==0xf0u);
    uint32_t next=(producer+1u)&31u;*slot=QUEUE_BASE+next*16u;
    if(sound_get(*slot)&0xffffu) return KUI_TOY_PILOT_BUS_BUSY;
    for(unsigned i=0;i<4u;i++) sound_set(*slot+i*4u,packet[i]);
    producer=next;++packets;
    if((packet[0]&0xffffu)==0xff9du) stop_queued=true;
    return KUI_TOY_PILOT_BUS_OK;
}
/* STOP changes hardware key-on/loop control immediately. Driver-owned playing
 * bytes lag until independent cursor service; ACK alone is not quiescence. */
static void dispatch(const uint32_t packet[4]) {
    uint32_t command=packet[0]&0xffffu,port=(packet[0]>>16)&255u;
    if(command==0xff9du) {
        assert(packet[1]==PORT_MASK);render();
        if(running) { stopped_live_voice=true;stop_dispatched=now; }
        stop_queued=false;
        running=looping=false;flag_delay=6u;++stops;
    } else if(command==0xff91u) {
        assert(port==62u || port==63u);
        uint32_t t=SOUND_BASE+0x2ca8u+(port-62u)*0x48u;
        for(unsigned i=0;i<0x48u;i+=4u) sound_set(t+i,0u);
    } else if(command==0xff90u) {
        assert(port==62u || port==63u);assert(packet[2] && !packet[3]);
        unsigned channel=port-62u;uint32_t t=SOUND_BASE+0x2ca8u+channel*0x48u;
        pcm_address[channel]=SOUND_BASE+(packet[1]&0x7fffffu);
        pcm_frames[channel]=packet[2]/2u;
        sound_set(t,(packet[1]>>16)&0x7fu);sound_set(t+4u,packet[1]&0xffffu);
        sound_set(t+8u,0u);sound_set(t+12u,pcm_frames[channel]-1u);sound_set(t+0x18u,0u);
    } else if(command==0xff9cu) {
        assert(packet[1]==PORT_MASK && pcm_frames[0]==pcm_frames[1]);
        looping=(packet[0]>>16)&1u;
        for(unsigned channel=0;channel<2u;channel++) {
            uint32_t t=SOUND_BASE+0x2ca8u+channel*0x48u;
            sound_set(t,sound_get(t)|(looping?0x200u:0u));
        }
        voice_started=now;voice_rendered=published_position=0u;running=true;flag_delay=0u;++starts;
        stopped_live_voice=false;
        cursor_tick=now;
        serial_scan=serial_event=now+serial_offset;serial_step=0u;
        serial_saved[0]=serial_saved[1]=0u;
        assert(starts<=sizeof(first_sample)/sizeof(*first_sample));
        first_sample[starts-1u]=(uint16_t)sample(pcm_address[0]);
        sound_set(SOUND_BASE+0x14a4u,0xffff0000u);
        sound_set(SOUND_BASE+0x15e8u,0u);sound_set(SOUND_BASE+0x15ecu,0u);
    } else assert(command==0xff96u || command==0xff97u);
}
static void arm_visit(void) {
    publish_cursor();if(hold_queue) return;
    for(unsigned i=0;i<32u;i++) {
        uint32_t slot=QUEUE_BASE+consumer*16u,packet[4];
        if(!(sound_get(slot)&0xffffu)) break;
        for(unsigned j=0;j<4u;j++) packet[j]=sound_get(slot+j*4u);
        dispatch(packet);sound_set(slot,packet[0]&0xffff0000u);consumer=(consumer+1u)&31u;
    }
    sound_set(SOUND_BASE+0x399cu,consumer);
}
enum kui_toy_pilot_bus_result kui_toy_pilot_lease_allocate(uint32_t bytes,uint32_t alignment,uint32_t *address) {
    assert((sr&0xf0u)==0xf0u);assert(bytes==KUI_TOY_PILOT_SOUND_BYTES && alignment==32u);
    *address=SOUND_END-KUI_TOY_PILOT_SOUND_BYTES;return KUI_TOY_PILOT_BUS_OK;
}
/* Independent physical safety rule: no write may touch the currently playing
 * 4096-frame block of either channel. A transfer may span inactive blocks. */
#define PHYSICAL_BLOCK_FRAMES 4096u
static void assert_copy_inactive(uint32_t address,uint32_t bytes) {
    if(!running) return;
    assert(looping && pcm_frames[0]==32768u);
    for(unsigned channel=0;channel<2u;channel++) {
        if(address>=pcm_address[channel] && address<pcm_address[channel]+pcm_frames[channel]*2u) {
            uint32_t first=(address-pcm_address[channel])/2u,last=first+bytes/2u-1u;
            uint32_t active=physical_channel_position(channel)/PHYSICAL_BLOCK_FRAMES;
            assert(last<pcm_frames[channel]);
            assert(active<first/PHYSICAL_BLOCK_FRAMES || active>last/PHYSICAL_BLOCK_FRAMES);
            return;
        }
    }
    assert(!"Copy outside the admitted stereo planes");
}
enum kui_toy_pilot_bus_result kui_toy_pilot_bus_copy(uint32_t address,const void *source,uint32_t bytes) {
    assert((sr&0xf0u)==0xf0u);assert(bytes && bytes<=256u && !(bytes&3u));
    assert(address>=owner.stats.sound_address && address<=SOUND_END-bytes);
    assert(!stop_queued);
    if(stopped_live_voice)
        assert(now-stop_dispatched>=(uint32_t)((uint64_t)32768u*781250u/44100u));
    if(retry_pending) {
        assert(reads==deferred_reads && owner.raw_lba==deferred_raw_lba &&
               owner.raw_generation==deferred_raw_generation);
        assert(!memcmp(owner.raw,deferred_raw,sizeof(deferred_raw)));
    }
    render();assert_copy_inactive(address,bytes);in_copy=true;advance(copy_ticks);in_copy=false;
    assert_copy_inactive(address,bytes);
    if(defer_right_copy && running && address>=pcm_address[1]) {
        defer_right_copy=false;retry_pending=true;
        deferred_reads=reads;deferred_address=address;
        deferred_bank=(owner.ring_fill_stream/KUI_TOY_PILOT_BANK_FRAMES)%
            (sizeof(owner.model.banks)/sizeof(owner.model.banks[0]));
        deferred_filled=owner.model.banks[deferred_bank].filled;
        deferred_fill_frame=owner.fill_frame;deferred_stream=owner.ring_fill_stream;
        deferred_raw_lba=owner.raw_lba;deferred_raw_generation=owner.raw_generation;
        memcpy(deferred_raw,owner.raw,sizeof(deferred_raw));
        return KUI_TOY_PILOT_BUS_BUSY;
    }
    memcpy(sound_memory+address-SOUND_BASE,source,bytes);++copies;
    if(retry_pending && address==deferred_address) retry_pending=false;
    return KUI_TOY_PILOT_BUS_OK;
}
static int raw(uint32_t lba,uint32_t sectors,void *destination) {
    assert((sr&0xf0u)==0xf0u && sectors==1u && !((uintptr_t)destination&31u));
    assert(lba>=50u && lba<50u+source_frames/588u && data_command!=16u && data_command!=17u);
    assert(!stop_queued);
    if(stopped_live_voice)
        assert(now-stop_dispatched>=(uint32_t)((uint64_t)32768u*781250u/44100u));
    if(strict_source_reads) {
        assert(lba==expected_read_lba);
        ++expected_read_lba;
        if(expected_read_lba==50u+source_frames/588u && repeat_pcm) expected_read_lba=50u;
    }
    ++reads;copies_at_last_read=copies;in_card=true;
    advance(raw_tail_period && !(reads%raw_tail_period)?raw_tail_ticks:raw_ticks);
    in_card=false;uint8_t *p=destination;
    for(unsigned i=0;i<588u;i++) {
        uint16_t f=(uint16_t)((lba-50u)*588u+i),r=(uint16_t)~f;
        p[i*4u]=(uint8_t)f;p[i*4u+1u]=(uint8_t)(f>>8);
        p[i*4u+2u]=(uint8_t)r;p[i*4u+3u]=(uint8_t)(r>>8);
    }
    return 0;
}
static void prepare(uint32_t sectors) {
    assert(sectors && sectors<=200u);
    kui_toy_pilot_worker_test_prepare(KUI_TOY_PILOT_STOPPED,0u,false);
    admitted=(struct kui_retail_manifest){.track_count=2u};
    admitted.slots[1].track=(struct kui_retail_track){.start_lba=50u,.end_lba=50u+sectors,.extent_count=1u};
    owner.config.manifest=(uint32_t)(uintptr_t)&admitted;owner.config.read_raw=(uint32_t)(uintptr_t)raw;
    owner.config.resident_active=0x8c004010u;owner.config.data_pending=0x8c004014u;
    memset(sound_memory,0xa5,sizeof(sound_memory));
    for(unsigned i=0;i<32u;i++) sound_set(QUEUE_BASE+i*16u,0u);
    sound_set(SOUND_BASE+0x1464u,0x800000u);sound_set(SOUND_BASE+0x14a4u,0u);
    sound_set(SOUND_BASE+0xe0u,0x1468u);sound_set(SOUND_BASE+0xe8u,0x14f0u);
    sound_set(SOUND_BASE+0xecu,0x30040u);sound_set(SOUND_BASE+0x399cu,0u);
    sr=entry_sr=0x40000001u;now=consumer=packets=copies=reads=reports=0u;producer=0xffffu;
    starts=stops=data_command=voice_started=voice_rendered=published_position=copies_at_last_read=0u;
    checked_frames=silent_frames=right_phase=flag_delay=stop_dispatched=cursor_interval=cursor_tick=cursor_bias=0u;
    pcm_address[0]=pcm_address[1]=pcm_frames[0]=pcm_frames[1]=0u;
    raw_ticks=200u;raw_tail_period=raw_tail_ticks=0u;copy_ticks=12u;read_ticks=12u;
    source_frames=sectors*588u;running=looping=hold_queue=hold_cursor=false;check_pcm=true;
    repeat_pcm=stop_queued=stopped_live_voice=defer_right_copy=false;
    revoke_before_mask=false;timer_reads=diagnostic_revokes=0u;
    window_start_observations=window_half_observations=0u;
    serial_cursor=in_card=in_copy=serial_sh_left_seen=false;serial_sh_left=0u;
    serial_period=serial_gap=serial_left_delay=serial_right_delay=serial_scan=serial_event=serial_step=serial_offset=0u;
    serial_card_publications=serial_copy_publications=torn_observations=torn_halves=torn_wraps=0u;
    memset(first_sample,0,sizeof(first_sample));
    memset(physical_phase,0,sizeof(physical_phase));
    memset(published_channel_position,0,sizeof(published_channel_position));
    strict_source_reads=retry_pending=false;expected_read_lba=50u;
    deferred_reads=deferred_address=deferred_bank=deferred_filled=0u;
    deferred_fill_frame=deferred_stream=deferred_raw_lba=deferred_raw_generation=0u;
    assert(kui_toy_pilot_request(CMD_PLAY,2u,2u,0u)==2u);
}
static void visit(uint32_t elapsed) {
    advance(elapsed);arm_visit();entry_sr=sr;
    kui_toy_pilot_worker_step();assert(sr==entry_sr);
}
static void ready(void) {
    for(unsigned i=0;i<1000u && owner.stats.state!=KUI_TOY_PILOT_PLAYING;i++) {
        visit(13021u);assert(!owner.stats.fault && !reports);
    }
    assert(owner.stats.state==KUI_TOY_PILOT_PLAYING && starts==1u && running && looping);
    assert(pcm_frames[0]==32768u && pcm_frames[1]==32768u);
    assert(pcm_address[1]==pcm_address[0]+65536u);
}
