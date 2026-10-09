/* SPDX-License-Identifier: GPL-3.0-only */
/* Exercise the actual worker against an independent asynchronous PCM consumer.
 * AICA playback advances during card and G2 operations. The fixture checks the
 * audible sample sequence and physical copy destinations, not model bank IDs.
 */
#define KUI_TOY_PILOT_WORKER_TEST 1
#include "../src/loader/toy_pilot_worker.c"
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
static uint32_t right_phase,flag_delay,stop_dispatched;
static uint32_t cursor_interval,cursor_tick,cursor_bias;
static uint32_t serial_period,serial_gap,serial_left_delay,serial_right_delay;
static uint32_t serial_scan,serial_event,serial_step,serial_saved[2],serial_offset;
static uint32_t serial_sh_left;
static uint32_t serial_card_publications,serial_copy_publications,torn_observations,torn_halves,torn_wraps;
static uint32_t timer_reads,diagnostic_revokes;
static uint32_t window_start_observations,window_block_observations;
static uint16_t first_sample[16];
static bool running,looping,hold_queue,hold_cursor,check_pcm,repeat_pcm;
static bool stop_queued,stopped_live_voice,defer_right_copy;
static bool revoke_before_mask;
static bool serial_cursor,in_card,in_copy,serial_sh_left_seen;

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
static uint32_t physical_position(void) {
    assert(pcm_frames[0]);return voice_rendered%pcm_frames[0];
}
static void render(void) {
    if(!running) return;
    uint32_t target=(uint32_t)((uint64_t)(now-voice_started)*44100u/781250u);
    if(!looping && target>pcm_frames[0]) target=pcm_frames[0];
    while(voice_rendered<target) {
        uint32_t position=voice_rendered%pcm_frames[0];
        int16_t left=sample(pcm_address[0]+position*2u);
        int16_t right=sample(pcm_address[1]+position*2u);
        if(check_pcm) {
            if(repeat_pcm || checked_frames<source_frames) {
                uint32_t source=repeat_pcm?checked_frames%source_frames:checked_frames;
                assert((uint16_t)left==(uint16_t)source);
                assert((uint16_t)right==(uint16_t)~source);
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
            serial_saved[channel]=(physical_position()+cursor_bias+(channel?right_phase:0u))%pcm_frames[0];
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
        published_position=(physical_position()+cursor_bias)%pcm_frames[0];cursor_tick=now;
    }
    if(!serial_cursor) {
        sound_set(SOUND_BASE+0x15e8u,published_position);
        sound_set(SOUND_BASE+0x15ecu,pcm_frames[0]?(published_position+right_phase)%pcm_frames[0]:0u);
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
static void assert_copy_inactive(uint32_t address,uint32_t bytes) {
    if(!running) return;
    assert(looping && pcm_frames[0]==32768u);
    for(unsigned channel=0;channel<2u;channel++) {
        if(address>=pcm_address[channel] && address<pcm_address[channel]+pcm_frames[channel]*2u) {
            uint32_t first=(address-pcm_address[channel])/2u,last=first+bytes/2u-1u;
            assert(last<pcm_frames[channel] && first/4096u==last/4096u);
            assert(first/4096u!=physical_position()/4096u);return;
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
    render();assert_copy_inactive(address,bytes);in_copy=true;advance(copy_ticks);in_copy=false;
    assert_copy_inactive(address,bytes);
    if(defer_right_copy && address>=owner.stats.sound_address+KUI_TOY_PILOT_RING_MONO_BYTES) {
        defer_right_copy=false;return KUI_TOY_PILOT_BUS_BUSY;
    }
    memcpy(sound_memory+address-SOUND_BASE,source,bytes);++copies;return KUI_TOY_PILOT_BUS_OK;
}
static int raw(uint32_t lba,uint32_t sectors,void *destination) {
    assert((sr&0xf0u)==0xf0u && sectors==1u && !((uintptr_t)destination&31u));
    assert(lba>=50u && lba<250u && data_command!=16u && data_command!=17u);
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
    window_start_observations=window_block_observations=0u;
    serial_cursor=in_card=in_copy=serial_sh_left_seen=false;serial_sh_left=0u;
    serial_period=serial_gap=serial_left_delay=serial_right_delay=serial_scan=serial_event=serial_step=serial_offset=0u;
    serial_card_publications=serial_copy_publications=torn_observations=torn_halves=torn_wraps=0u;
    memset(first_sample,0,sizeof(first_sample));
    assert(kui_toy_pilot_request(CMD_PLAY,2u,2u,0u)==2u);
}
static void visit(uint32_t elapsed) {
    advance(elapsed);arm_visit();entry_sr=sr;
    uint32_t entered=now,previous_starts=owner.stats.started_observed;
    uint32_t previous_ends=owner.stats.bank_ends;
    uint32_t previous_tick[KUI_TOY_PILOT_BLOCKS],previous_calls[KUI_TOY_PILOT_BLOCKS];
    memcpy(previous_tick,owner.ring_window_tick,sizeof(previous_tick));
    memcpy(previous_calls,owner.ring_window_calls,sizeof(previous_calls));
    kui_toy_pilot_worker_step();assert(sr==entry_sr);
    /* Each block owns an observed retirement anchor. START stamps all slots;
     * ordinary proof renewal, controls and idle visits cannot slide it. */
    for(unsigned slot=0u;slot<KUI_TOY_PILOT_BLOCKS;slot++) {
        bool changed=owner.ring_window_tick[slot]!=previous_tick[slot] ||
            owner.ring_window_calls[slot]!=previous_calls[slot];
        if(changed) {
            assert(owner.stats.started_observed!=previous_starts || owner.stats.bank_ends!=previous_ends);
            assert(owner.ring_window_calls[slot]==owner.stats.service_calls);
            assert(owner.ring_window_tick[slot]-entered<=now-entered);
        }
    }
    window_start_observations+=owner.stats.started_observed-previous_starts;
    window_block_observations+=owner.stats.bank_ends-previous_ends;
}
static void ready(void) {
    for(unsigned i=0;i<1000u && owner.stats.state!=KUI_TOY_PILOT_PLAYING;i++) {
        visit(13021u);assert(!owner.stats.fault && !reports);
    }
    assert(owner.stats.state==KUI_TOY_PILOT_PLAYING && starts==1u && running && looping);
    assert(owner.stats.version==8u && owner.stats.bytes==448u);
    assert(offsetof(struct kui_toy_pilot_snapshot,recovery_counts)==320u);
    assert(offsetof(struct kui_toy_pilot_snapshot,reserve_last_cursor)==384u);
    assert(offsetof(struct kui_toy_pilot_snapshot,reserve_last_window_ticks)==440u);
    assert(offsetof(struct kui_toy_pilot_snapshot,reserve_last_window_calls)==444u);
    assert(window_start_observations==1u && owner.ring_window_calls[0]);
    assert(owner.ring_window_tick[0]-voice_started<kui_toy_ring_ticks(16384u));
    assert(pcm_frames[0]==32768u && pcm_frames[1]==32768u);
    assert(pcm_address[1]==pcm_address[0]+65536u);
}
static void no_recoveries(void) {
    for(unsigned i=0;i<8u;i++) assert(!owner.stats.recovery_counts[i]);
    assert(!owner.stats.recovery_last_reason && !owner.stats.recovery_last_gd_command &&
           !owner.stats.recovery_last_cursor_age);
}
static void single_recovery(uint32_t reason,uint32_t command) {
    for(unsigned i=0;i<8u;i++) assert(owner.stats.recovery_counts[i]==(i+1u==reason?1u:0u));
    assert(owner.stats.recovery_last_reason==reason && owner.stats.recovery_last_gd_command==command);
    assert(stop_queued || stops>1u);
}
static void smooth_stereo_and_once_only_tail(uint32_t sectors,uint32_t publication_ticks) {
    prepare(sectors);cursor_interval=publication_ticks;ready();
    for(unsigned i=0;i<1000u && owner.stats.state!=KUI_TOY_PILOT_EOF;i++) {
        visit(13021u);visit(0u);assert(!owner.stats.fault && !reports);
    }
    assert(owner.stats.state==KUI_TOY_PILOT_EOF && starts==1u && stops>=2u && !running);
    no_recoveries(); /* Source EOF is not an internal restart. */
    assert(checked_frames>=source_frames && !owner.stats.active_bank_writes);
    uint32_t tail_limit=publication_ticks?
        (uint32_t)(((uint64_t)(39063u+2u*13021u)*44100u+781249u)/781250u)+64u:2000u;
    assert(first_sample[0]==0u && silent_frames<tail_limit);
    uint32_t old_copies=copies,old_reads=reads,old_packets=packets;
    visit(CLOCK_GAP_LIMIT*2u);
    assert(copies==old_copies && reads==old_reads && packets==old_packets && !reports);
    if(sectors==5u) {
        /* RELEASE retains the exhausted source cursor. There is no remaining
         * PCM to prime, even though the native scalar command is accepted. */
        assert(kui_toy_pilot_request(CMD_RELEASE,0u,0u,0u)==3u);
        for(unsigned i=0;i<10u && owner.stats.applied_generation!=3u;i++) visit(13021u);
        assert(owner.stats.applied_generation==3u && owner.stats.state==KUI_TOY_PILOT_EOF);
        assert(copies==old_copies && reads==old_reads && starts==1u && !running && !reports);
    }
}
static void infinite_repeat_has_no_block_restarts(void) {
    prepare(60u);repeat_pcm=true;
    assert(kui_toy_pilot_request(CMD_PLAY,2u,2u,15u)==3u);ready();
    for(unsigned i=0;i<400u;i++) {
        visit(13021u);visit(0u);assert(!owner.stats.fault && !reports);
    }
    assert(checked_frames>source_frames*6u && starts==1u && stops==1u && running && !silent_frames);
    assert(owner.stats.state==KUI_TOY_PILOT_PLAYING && !owner.stats.active_bank_writes);
    assert(window_start_observations==1u && window_block_observations>10u);
    no_recoveries();
}
static void serial_publications_preserve_pcm_and_block_ownership(void) {
    const uint32_t schedules[][3]={{4001u,1000u,0u},{7813u,1563u,991u},{6503u,2000u,3181u}};
    uint32_t half_mixtures=0u,wrap_mixtures=0u;
    for(unsigned schedule=0;schedule<sizeof(schedules)/sizeof(*schedules);schedule++) {
        prepare(200u);serial_cursor=true;
        serial_period=schedules[schedule][0];serial_right_delay=schedules[schedule][1];
        serial_offset=schedules[schedule][2];serial_gap=8u;
        assert(serial_period>serial_gap+serial_right_delay);
        ready();
        for(unsigned i=0;i<1000u && owner.stats.state!=KUI_TOY_PILOT_EOF;i++) {
            visit(13021u);visit(0u);assert(!owner.stats.fault && !reports);
        }
        /* Render verifies every physical stereo sample throughout multiple
         * half crossings and wraps. G2 copies separately assert their real
         * physical destination is inactive before AND after transfer. */
        assert(owner.stats.state==KUI_TOY_PILOT_EOF && starts==1u && !running);
        assert(checked_frames>=source_frames && !owner.stats.active_bank_writes);
        assert(torn_observations && serial_card_publications && serial_copy_publications);
        half_mixtures+=torn_halves;
        wrap_mixtures+=torn_wraps;
        no_recoveries();
    }
    assert(half_mixtures && wrap_mixtures);
}
static void delayed_proof_with_slow_card_preserves_pcm(void) {
    const uint32_t schedules[][3]={{6800u,1000u,0u},{7000u,500u,5334u},{7000u,1000u,0u}};
    for(unsigned schedule=0;schedule<sizeof(schedules)/sizeof(*schedules);schedule++) {
        prepare(60u);repeat_pcm=true;raw_ticks=schedules[schedule][0];
        serial_cursor=true;serial_period=16001u;serial_gap=8u;
        serial_right_delay=schedules[schedule][1];serial_offset=schedules[schedule][2];
        assert(kui_toy_pilot_request(CMD_PLAY,2u,2u,15u)==3u);ready();
        uint32_t cadence=now;
        /* Absolute60Hz visits model two hooks within one update, with card
         * and G2 work consuming real wall time. Overrunning hooks delay the
         * next update. A20.48ms ARM scan needs more than two updates for some
         * fresh-pair proofs. An initial-probe capture bound formerly caused
         * COPY_RESERVE in the first two schedules; the third still exhausted
         * the previous4ms refill budget. Retain repeated physical PCM and
         * per-plane destination checks throughout600updates instead. */
        for(unsigned i=0;i<600u;i++) {
            cadence+=13021u;if(cadence<now) cadence=now;
            visit(cadence-now);
            assert(!owner.stats.recovery_last_reason && !owner.stats.fault && !reports);
            visit(0u);
            assert(!owner.stats.recovery_last_reason && !owner.stats.fault && !reports);
        }
        assert(starts==1u && running && checked_frames>source_frames*6u && !silent_frames);
        assert(!owner.stats.active_bank_writes && torn_observations && serial_card_publications && serial_copy_publications);
        /* Timer reads cost one independently modeled tick. Cached remnants
         * must not count another callback or its latency. */
        assert(owner.stats.raw_read_timing_calls==reads && owner.stats.raw_calls==reads);
        assert(owner.stats.raw_read_ticks_last==raw_ticks+1u && owner.stats.raw_read_ticks_max==raw_ticks+1u);
        assert(owner.stats.raw_read_ticks_total==reads*(raw_ticks+1u));
        no_recoveries();
    }
}
static void measured_latency_hooks_preserve_pcm_across_update_gaps(void) {
    const uint32_t gap_phases[]={0u,7u,15u,31u},publication_phases[]={0u,2604u,5208u};
    bool used_full_quantum_cap=false;
    for(unsigned gap=0u;gap<sizeof(gap_phases)/sizeof(*gap_phases);gap++)
        for(unsigned publication=0u;publication<sizeof(publication_phases)/sizeof(*publication_phases);publication++) {
            prepare(60u);repeat_pcm=true;raw_ticks=3358u;raw_tail_period=32u;raw_tail_ticks=8835u;
            serial_cursor=true;serial_period=7813u;serial_gap=8u;serial_right_delay=1000u;
            serial_offset=publication_phases[publication];
            assert(kui_toy_pilot_request(CMD_PLAY,2u,2u,15u)==3u);ready();
            uint32_t next=now,max_visit=0u,max_reads=0u;
            /* Main-context updates target30Hz; the two hooks begin10ms apart.
             * Every32nd update has a73.87904ms gap instead. Card operations
             * independently cost4.29824ms, with every32nd callback11.3088ms:
             * their mean is4.51732ms. Actual card/G2 time delays an overrun's
             * next hook/update; this fixture never compresses those costs.
             * Sweep gap and serial-publication phases through repeated PCM
             * half/full wraps, retaining exact samples and copy destinations. */
            for(unsigned update=0u;update<300u;update++) {
                if(next<now) next=now;
                uint32_t first=next;
                for(unsigned hook=0u;hook<2u;hook++) {
                    uint32_t scheduled=first;
                    if(hook) scheduled+=update%32u==gap_phases[gap]?57718u:7813u;
                    if(scheduled<now) scheduled=now;
                    advance(scheduled-now);
                    uint32_t entered=now,before_reads=reads;
                    visit(0u);
                    uint32_t spent=now-entered,raw_count=reads-before_reads;
                    if(spent>max_visit) max_visit=spent;
                    if(raw_count>max_reads) max_reads=raw_count;
                    assert(raw_count<=4u);
                    assert(!owner.stats.recovery_last_reason && !owner.stats.fault && !reports);
                }
                next=first+26042u;
            }
            assert(starts==1u && running && checked_frames>source_frames*12u && !silent_frames);
            assert(!owner.stats.active_bank_writes && torn_observations &&
                   serial_card_publications && serial_copy_publications);
            /* At most four sectors enter one hook, with a single tail among
             * them. Bound elapsed work too, allowing2ms per sector for G2 and
             * command/ownership checks in this independent bus model. */
            used_full_quantum_cap|=max_reads==4u;
            assert(max_visit<=3u*raw_ticks+raw_tail_ticks+6u*1563u);
            uint32_t tails=reads/raw_tail_period;
            assert(tails>20u && owner.stats.raw_read_timing_calls==reads && owner.stats.raw_calls==reads);
            assert(owner.stats.raw_read_ticks_max==raw_tail_ticks+1u);
            assert(owner.stats.raw_read_ticks_last==(!(reads%raw_tail_period)?raw_tail_ticks:raw_ticks)+1u);
            assert(owner.stats.raw_read_ticks_total==(reads-tails)*(raw_ticks+1u)+tails*(raw_tail_ticks+1u));
            no_recoveries();
        }
    assert(used_full_quantum_cap);
}
static void clustered_measured_gaps_preserve_ordered_blocks(void) {
    prepare(60u);repeat_pcm=true;raw_ticks=3396u;raw_tail_period=32u;raw_tail_ticks=8837u;
    serial_cursor=true;serial_period=7813u;serial_gap=8u;serial_right_delay=1000u;
    serial_offset=5208u;
    assert(kui_toy_pilot_request(CMD_PLAY,2u,2u,15u)==3u);ready();
    uint32_t next=now,max_reads=0u,max_visit=0u;
    /* Nominal30Hz updates, two hooks10ms apart. After100 updates, each
     * fourth update's second hook waits82.57024ms instead. Both raw costs
     * advance physical PCM; the callback mean with the separately charged
     * timer read is4.5658ms, close to the console's4.56621ms. The schedule
     * models clustered long service gaps, not a measured reconstruction.
     * Unlike an isolated gap every32 updates, these gaps leave insufficient
     * refill opportunities after the half transition is actually observed. */
    for(unsigned update=0u;update<300u && !owner.stats.recovery_last_reason;update++) {
        if(next<now) next=now;
        uint32_t first=next;
        for(unsigned hook=0u;hook<2u && !owner.stats.recovery_last_reason;hook++) {
            uint32_t scheduled=first;
            if(hook) scheduled+=update>=100u && update%4u==2u?64508u:7813u;
            if(scheduled<now) scheduled=now;
            advance(scheduled-now);
            uint32_t entered=now,before_reads=reads;
            visit(0u);
            uint32_t spent=now-entered,raw_count=reads-before_reads;
            if(spent>max_visit) max_visit=spent;
            if(raw_count>max_reads) max_reads=raw_count;
            assert(raw_count<=4u && !owner.stats.fault && !reports);
        }
        next=first+26042u;
    }
    /* This previously exhausted a half. Early4096-block reclamation now
     * sustains it without STOP, while physical PCM continues during reads. */
    no_recoveries();
    assert(starts==1u && running && !owner.stats.active_bank_writes);
    assert(max_reads && max_reads<=4u && max_visit<=3u*raw_ticks+raw_tail_ticks+6u*1563u);
    assert(checked_frames>source_frames*8u && !silent_frames &&
           torn_observations && serial_card_publications && serial_copy_publications);
    assert(window_block_observations>80u);
    uint32_t tails=reads/raw_tail_period;
    assert(owner.stats.raw_read_timing_calls==reads && owner.stats.raw_calls==reads);
    assert(owner.stats.raw_read_ticks_total==(reads-tails)*(raw_ticks+1u)+tails*(raw_tail_ticks+1u));
}

static void sustained_slow_card_stops_with_capture_evidence(void) {
    prepare(60u);repeat_pcm=true;raw_ticks=15625u;
    serial_cursor=true;serial_period=7813u;serial_gap=8u;serial_right_delay=1000u;
    assert(kui_toy_pilot_request(CMD_PLAY,2u,2u,15u)==3u);ready();
    uint32_t next=now,updates=0u;
    /* Independently costed20ms reads on EVERY callback exceed this
     *30Hz/occasional73.87904ms-gap schedule's refill capacity. More admitted
     * quanta cannot create safe ownership time. Keep exact PCM and physical
     * destination checks through STOP; no fabricated bank state or test-side
     * cursor publication creates this pressure. */
    while(updates++<600u && !owner.stats.recovery_last_reason) {
        if(next<now) next=now;
        uint32_t first=next;visit(first-now);
        if(!owner.stats.recovery_last_reason) {
            uint32_t second=first+((updates-1u)%32u==0u?57718u:7813u);
            if(second<now) second=now;
            visit(second-now);
        }
        next=first+26042u;
        assert(!owner.stats.fault && !reports);
    }
    single_recovery(KUI_TOY_PILOT_RECOVERY_COPY_RESERVE,0u);
    assert(starts==1u && stop_queued && !owner.stats.active_bank_writes);
    assert(owner.stats.reserve_last_site==KUI_TOY_PILOT_RESERVE_POST_CARD);
    assert(owner.stats.reserve_last_bank<KUI_TOY_PILOT_BLOCKS &&
           owner.stats.reserve_last_bank!=owner.model.active_bank);
    assert(owner.stats.reserve_last_bank_state==KUI_TOY_PILOT_BANK_FILLING &&
           owner.stats.reserve_last_bank_filled<KUI_TOY_RING_BLOCK);
    assert(owner.stats.reserve_last_bank_filled==owner.model.banks[owner.stats.reserve_last_bank].filled &&
           owner.stats.reserve_last_cursor==owner.ring_cursor);
    assert(owner.stats.reserve_last_remaining==owner.model.banks[owner.stats.reserve_last_bank].first_frame-owner.ring_played);
    uint32_t remaining_ticks=(uint32_t)((uint64_t)owner.stats.reserve_last_remaining*781250u/44100u);
    assert(owner.stats.reserve_last_proof_age>=remaining_ticks ||
           remaining_ticks-owner.stats.reserve_last_proof_age<=KUI_TOY_RING_RESERVE_TICKS);
    assert(owner.stats.reserve_last_sample_age<=KUI_TOY_RING_FRESH_TICKS &&
           owner.stats.reserve_last_proof_age>=owner.stats.reserve_last_sample_age);
    assert(owner.stats.reserve_last_fill_stream==owner.ring_fill_stream &&
           owner.stats.reserve_last_probe_age<=KUI_TOY_RING_FRESH_TICKS);
    assert(owner.stats.reserve_last_window_calls==owner.stats.service_calls-owner.ring_window_calls[owner.stats.reserve_last_bank]+1u);
    assert(owner.stats.reserve_last_window_calls>1u &&
           owner.stats.reserve_last_window_ticks>owner.stats.reserve_last_proof_age);
    uint32_t denied_tick=owner.ring_window_tick[owner.stats.reserve_last_bank]+owner.stats.reserve_last_window_ticks;
    assert(now-denied_tick<2000u); /* STOP publication follows the denied gate. */
    assert(owner.stats.raw_read_timing_calls==reads && owner.stats.raw_calls==reads);
    assert(owner.stats.raw_read_ticks_last==raw_ticks+1u && owner.stats.raw_read_ticks_max==raw_ticks+1u &&
           owner.stats.raw_read_ticks_total==reads*(raw_ticks+1u));
    uint32_t before_copies=copies,before_reads=reads;
    visit(0u);assert(!running && copies==before_copies && reads==before_reads);
    uint32_t previous_window_tick=owner.ring_window_tick[0],previous_window_calls=owner.ring_window_calls[0];
    check_pcm=false;raw_ticks=200u;
    for(unsigned i=0u;i<300u && owner.stats.started_observed<2u;i++) {
        visit(13021u);assert(!owner.stats.fault && !reports);
    }
    assert(starts==2u && owner.stats.started_observed==2u && running);
    assert(window_start_observations==2u);
    assert(owner.ring_window_tick[0]!=previous_window_tick && owner.ring_window_calls[0]!=previous_window_calls);
    assert(owner.ring_window_tick[0]-voice_started<kui_toy_ring_ticks(16384u));
    /* Historical denial telemetry survives; the new ownership epoch receives
     * its own anchor and cannot count the STOP fence or reprime as refill. */
    assert(owner.stats.reserve_last_window_ticks && owner.stats.reserve_last_window_calls>1u);
}
static void wait_for_recovery_stop(uint32_t previous_stops,uint32_t previous_copies) {
    for(unsigned i=0;i<100u && stops==previous_stops && !stop_queued;i++) {
        visit(13021u);assert(!owner.stats.fault && !reports && copies==previous_copies);
    }
    assert(stop_queued || stops>previous_stops);
    assert(!owner.stats.active_bank_writes);
}
static void recover_and_restart(uint32_t previous_starts) {
    for(unsigned i=0;i<200u && starts==previous_starts;i++) {
        visit(13021u);visit(0u);assert(!owner.stats.fault && !reports);
    }
    assert(starts==previous_starts+1u && running && looping);
}
static void bad_published_cursors_stop_before_reuse(void) {
    /* Hardware continues; immutable published positions cannot authorize a
     * fill. They must eventually cause STOP and a fresh full ownership wait. */
    prepare(200u);ready();
    hold_cursor=true;uint32_t before_stops=stops,before_copies=copies;
    wait_for_recovery_stop(before_stops,before_copies);
    single_recovery(KUI_TOY_PILOT_RECOVERY_UNOBSERVED_HALF,0u);
    assert(owner.stats.recovery_last_cursor_age>=kui_toy_ring_ticks(KUI_TOY_RING_HALF));
    check_pcm=false;hold_cursor=false;recover_and_restart(1u);

    /* A permanent65-frame disagreement never authorizes a new proof. It
     * can be publication skew, so retain the old proof until its bounded
     * ownership deadline rather than immediately treating it as bad phase. */
    prepare(200u);ready();right_phase=65u;before_stops=stops;before_copies=copies;
    wait_for_recovery_stop(before_stops,before_copies);
    single_recovery(KUI_TOY_PILOT_RECOVERY_UNOBSERVED_HALF,0u);
    assert(owner.stats.recovery_last_cursor_age>=kui_toy_ring_ticks(KUI_TOY_RING_HALF));
    check_pcm=false;right_phase=0u;recover_and_restart(1u);

    /*2300 frames exceed even a full50ms probe's publication-age allowance;
     * this gross, independently injected separation remains a phase fault. */
    prepare(200u);ready();right_phase=2300u;before_stops=stops;before_copies=copies;
    wait_for_recovery_stop(before_stops,before_copies);
    single_recovery(KUI_TOY_PILOT_RECOVERY_PHASE,0u);
    check_pcm=false;right_phase=0u;recover_and_restart(1u);
}
static void reserve_window_wraps_keep_the_observed_epoch(void) {
    prepare(200u);
    /* Select timer/counter origins before any playback or ownership proof.
     * Real PCM and service visits then cross both32-bit wraps naturally. */
    now=UINT32_MAX-700000u;owner.stats.service_calls=UINT32_MAX-45u;
    ready();check_pcm=false;data_command=16u;
    while(owner.model.active_bank==0u) { visit(13021u);assert(!owner.stats.fault && !reports); }
    assert(window_start_observations==1u && window_block_observations==1u);
    assert(now>0xf0000000u && owner.stats.service_calls>0xffffffe0u);
    /* Ordinary proof visits cross the call counter without renewing its anchor. */
    for(unsigned i=0u;i<10u;i++) visit(0u);
    uint32_t target_tick=(uint32_t)(((uint64_t)(32768u-300u)*781250u+44099u)/44100u);
    assert(target_tick>now-voice_started);
    /* Keep observing during the longer block window. A single skipped
     * half interval must still fail the unchanged progress guard. */
    while(target_tick-(now-voice_started)>13021u) visit(13021u);
    uint32_t elapsed=target_tick-(now-voice_started),before_reads=reads;
    /* The physical cached remainder and independently costed2ms G2 scopes
     * reproduce COPY_PLANE pressure after the timer wraps as well. */
    copy_ticks=1563u;data_command=0u;visit(elapsed);
    single_recovery(KUI_TOY_PILOT_RECOVERY_COPY_RESERVE,0u);
    assert(owner.stats.reserve_last_site==KUI_TOY_PILOT_RESERVE_COPY_PLANE && reads==before_reads);
    assert(now<owner.ring_window_tick[owner.stats.reserve_last_bank] && owner.stats.service_calls<owner.ring_window_calls[owner.stats.reserve_last_bank]);
    assert(owner.stats.reserve_last_window_calls==owner.stats.service_calls-owner.ring_window_calls[owner.stats.reserve_last_bank]+1u);
    assert(owner.stats.reserve_last_window_calls>10u && owner.stats.reserve_last_window_calls<100u);
    assert(owner.stats.reserve_last_window_ticks>owner.stats.reserve_last_proof_age &&
           owner.stats.reserve_last_window_ticks<kui_toy_ring_ticks(32768u));
    uint32_t denied_tick=owner.ring_window_tick[owner.stats.reserve_last_bank]+owner.stats.reserve_last_window_ticks;
    assert(now-denied_tick<2000u && !owner.stats.active_bank_writes);
    uint32_t before_copies=copies;
    visit(0u);assert(!running && copies==before_copies && reads==before_reads);
}
static void locked_publication_cadence_stops_before_unsafe_reuse(void) {
    prepare(200u);ready();
    uint32_t anchor=now,before_copies=copies,before_reads=reads,before_stops=stops;
    serial_cursor=true;serial_period=13021u;serial_gap=8u;serial_right_delay=10000u;
    serial_scan=serial_event=anchor;serial_step=0u;
    /* Every visit begins shortly after L is published; R still contains the
     * previous scan. Host elapsed time is scheduled absolutely, so worker
     * execution cannot accidentally drift this fixture into a coherent pair.
     * Both real voices remain perfectly paired throughout. */
    for(unsigned i=1u;i<100u && !stop_queued && stops==before_stops;i++) {
        uint32_t at=anchor+i*serial_period+100u;assert(at>=now);
        visit(at-now);assert(!owner.stats.fault && !reports);
        assert(copies==before_copies && reads==before_reads);
    }
    assert(stop_queued || stops>before_stops);
    single_recovery(KUI_TOY_PILOT_RECOVERY_UNOBSERVED_HALF,0u);
    assert(torn_observations && !owner.stats.recovery_counts[KUI_TOY_PILOT_RECOVERY_PHASE-1u]);
    assert(owner.stats.recovery_last_cursor_age>=kui_toy_ring_ticks(KUI_TOY_RING_HALF));
    assert(checked_frames<32768u && !owner.stats.active_bank_writes);
    serial_cursor=false;check_pcm=false;recover_and_restart(1u);
}
static void long_gap_and_unacknowledged_stop_hold_the_lease(void) {
    prepare(200u);ready();check_pcm=false;hold_queue=true;
    uint32_t before_copies=copies,before_reads=reads,before_stops=stops;
    /* More than a complete hardware wrap makes equal modulo CA ambiguous. */
    visit((uint32_t)((uint64_t)32768u*781250u/44100u)+13021u);
    assert(stop_queued && stops==before_stops && copies==before_copies && reads==before_reads);
    single_recovery(KUI_TOY_PILOT_RECOVERY_UNOBSERVED_HALF,0u);
    for(unsigned i=0;i<8u;i++) {
        visit(13021u);assert(copies==before_copies && reads==before_reads && !owner.stats.fault && !reports);
    }
    hold_queue=false;visit(1u);assert(!running && stops==before_stops+1u);
    /* Consumed STOP does not authorize immediate writes, even though the
     * hardware key-on is already clear. Production requires its full horizon. */
    visit(13021u);assert(copies==before_copies && reads==before_reads);
    recover_and_restart(1u);
}
static void data_priority_underrun_stops_and_reprimes(void) {
    prepare(200u);ready();data_command=16u;check_pcm=false;
    uint32_t before_stops=stops,before_copies=copies,before_reads=reads;
    wait_for_recovery_stop(before_stops,before_copies);
    single_recovery(KUI_TOY_PILOT_RECOVERY_MISSING_HALF,16u);
    assert(owner.stats.data_blocked_calls && owner.stats.data_blocked_intervals==1u &&
           owner.stats.data_blocked_ticks_max && !owner.stats.data_blocked_open);
    assert(reads==before_reads);visit(1u);assert(!running);
    for(unsigned i=0;i<8u;i++) { visit(13021u);assert(copies==before_copies && reads==before_reads); }
    data_command=0u;recover_and_restart(1u);
    assert(first_sample[1]==32768u);
}
static void late_card_return_requires_new_ownership_evidence(void) {
    prepare(200u);ready();data_command=16u;check_pcm=false;
    while(voice_rendered<16384u) { visit(13021u);assert(!owner.stats.fault); }
    uint32_t before_reads=reads;
    data_command=0u;
    /* The card succeeds after consuming an entire half interval. The old
     * prior ownership decision must not survive that independently costed read. */
    raw_ticks=(uint32_t)((uint64_t)16384u*781250u/44100u)+13021u;
    visit(0u);
    assert(reads==before_reads+1u && copies==copies_at_last_read && stop_queued && !reports);
    assert(owner.stats.raw_read_timing_calls==reads && owner.stats.raw_read_ticks_last==raw_ticks+1u &&
           owner.stats.raw_read_ticks_max==raw_ticks+1u);
    single_recovery(KUI_TOY_PILOT_RECOVERY_UNOBSERVED_HALF,0u);
    raw_ticks=200u;recover_and_restart(1u);
}
static void deferred_second_plane_does_not_publish_partial_stereo(void) {
    prepare(200u);ready();defer_right_copy=true;
    for(unsigned i=0;i<1000u && owner.stats.state!=KUI_TOY_PILOT_EOF;i++) {
        visit(13021u);visit(0u);assert(!owner.stats.fault && !reports);
    }
    assert(!defer_right_copy && owner.stats.bus_deferrals && starts==1u);
    assert(owner.stats.state==KUI_TOY_PILOT_EOF && checked_frames>=source_frames);
}
static void maximum_latency_copies_recheck_each_plane(void) {
    prepare(200u);ready();check_pcm=false;data_command=16u;
    while(voice_rendered<16384u) { visit(13021u);assert(!owner.stats.fault); }
    uint32_t target=32768u-300u;
    uint32_t target_tick=(uint32_t)(((uint64_t)target*781250u+44099u)/44100u);
    assert(target_tick>now-voice_started);
    while(target_tick-(now-voice_started)>13021u) visit(13021u);
    uint32_t elapsed=target_tick-(now-voice_started),before_copies=copies;
    uint32_t before_reads=reads,before_read_ticks=owner.stats.raw_read_ticks_total;
    /* The cached160-frame remainder needs four plane transfers. Four allowed
     *2ms transfers exceed the independently scheduled6.8ms target-entry margin.
     * A single admission decision for that complete burst is unsafe. */
    copy_ticks=1563u;data_command=0u;visit(elapsed);
    assert(copies>before_copies && copies<before_copies+4u);
    assert(stop_queued && !owner.stats.fault && !reports && running);
    single_recovery(KUI_TOY_PILOT_RECOVERY_COPY_RESERVE,0u);
    assert(owner.stats.reserve_last_site==KUI_TOY_PILOT_RESERVE_COPY_PLANE &&
           owner.stats.reserve_last_bank==0u && owner.stats.reserve_last_bank_state==KUI_TOY_PILOT_BANK_FILLING);
    assert(owner.stats.reserve_last_bank_filled==owner.model.banks[0].filled &&
           owner.stats.reserve_last_fill_stream==owner.ring_fill_stream);
    assert(owner.stats.reserve_last_remaining==owner.model.banks[owner.stats.reserve_last_bank].first_frame-owner.ring_played);
    assert(owner.stats.reserve_last_sample_age<=KUI_TOY_RING_FRESH_TICKS &&
           owner.stats.reserve_last_proof_age>=owner.stats.reserve_last_sample_age);
    assert(reads==before_reads && owner.stats.raw_read_timing_calls==reads &&
           owner.stats.raw_read_ticks_total==before_read_ticks); /* Cached remainder: no callback timing. */
    copy_ticks=12u;recover_and_restart(1u);
}
static void pause_movie_release_and_reset(void) {
    prepare(200u);ready();
    for(unsigned i=0;i<8u;i++) visit(13021u);
    uint32_t previous_window_tick=owner.ring_window_tick[0],previous_window_calls=owner.ring_window_calls[0];
    assert(kui_toy_pilot_request(CMD_PAUSE,0u,0u,0u)==3u);visit(0u);
    uint32_t before_copies=copies,before_reads=reads;assert(stop_queued);
    check_pcm=false;
    for(unsigned i=0;i<100u && owner.stats.applied_generation!=3u;i++) {
        visit(13021u);assert(!owner.stats.fault && !reports && copies==before_copies && reads==before_reads);
    }
    assert(owner.stats.applied_generation==3u && owner.stats.state==KUI_TOY_PILOT_PAUSED && !running);
    assert(owner.ring_window_tick[0]==previous_window_tick && owner.ring_window_calls[0]==previous_window_calls);
    no_recoveries(); /* Intentional PAUSE/RELEASE/RESET are excluded. */
    uint32_t position=owner.stats.position_fad,before_packets=packets;
    assert(position>=200u && position<200u+source_frames/588u);
    visit(CLOCK_GAP_LIMIT*2u);
    assert(copies==before_copies && reads==before_reads && packets==before_packets && !owner.stats.fault);
    assert(kui_toy_pilot_request(CMD_RELEASE,0u,0u,0u)==4u);recover_and_restart(1u);
    for(unsigned i=0u;i<100u && owner.stats.started_observed<2u;i++) {
        visit(13021u);assert(!owner.stats.fault && !reports);
    }
    assert(owner.stats.started_observed==2u && window_start_observations==2u);
    assert(owner.ring_window_tick[0]!=previous_window_tick && owner.ring_window_calls[0]!=previous_window_calls);
    assert(first_sample[1]>=(position-200u)*588u && first_sample[1]<(position-199u)*588u);
    assert(kui_toy_pilot_request(KUI_TOY_PILOT_RESET,0u,0u,0u)==5u);
    for(unsigned i=0;i<100u && owner.stats.applied_generation!=5u;i++) { visit(13021u);assert(!reports); }
    assert(owner.stats.applied_generation==5u && owner.stats.state==KUI_TOY_PILOT_STOPPED && !running);
    before_copies=copies;before_reads=reads;before_packets=packets;
    assert(kui_toy_pilot_request(CMD_RELEASE,0u,0u,0u)==6u);visit(1u);
    assert(owner.stats.state==KUI_TOY_PILOT_STOPPED && copies==before_copies && reads==before_reads && packets==before_packets);
    no_recoveries();
}
static void missing_activity_and_impossible_cursor_have_distinct_reasons(void) {
    prepare(200u);ready();sound_set(SOUND_BASE+0x14a4u,0xff000000u);
    uint32_t before_copies=copies,before_reads=reads;
    visit(0u);
    single_recovery(KUI_TOY_PILOT_RECOVERY_PORTS,0u);
    assert(copies==before_copies && reads==before_reads && !reports);

    /* Both published channels advance coherently, but by four thousand
     * frames without corresponding consumer time. Stereo agreement alone
     * must not credit that fabricated progress or authorize a sample write. */
    prepare(200u);ready();cursor_bias=4000u;
    before_copies=copies;before_reads=reads;
    visit(0u);
    single_recovery(KUI_TOY_PILOT_RECOVERY_PROGRESS,0u);
    assert(copies==before_copies && reads==before_reads && !reports);
}
static void initial_cursor_timeout_and_stream_overflow_are_counted(void) {
    prepare(200u);hold_cursor=true;
    for(unsigned i=0;i<1000u && !owner.stats.recovery_last_reason;i++) {
        visit(13021u);assert(!owner.stats.fault && !reports);
    }
    single_recovery(KUI_TOY_PILOT_RECOVERY_START_PROOF,0u);
    assert(!owner.stats.recovery_last_cursor_age && !owner.stats.data_blocked_intervals);

    /* A long repeat can approach the stream counter's representable end.
     * The real fill entry must stop instead of wrapping its ownership tag. */
    prepare(200u);ready();check_pcm=false;data_command=16u;
    while(owner.model.active_bank==0u) { visit(13021u);assert(!owner.stats.fault && !reports); }
    data_command=0u;owner.ring_fill_stream=UINT32_MAX-KUI_TOY_RING_BLOCK+1u;
    uint32_t before_copies=copies,before_reads=reads;
    fill_step();
    single_recovery(KUI_TOY_PILOT_RECOVERY_STREAM_OVERFLOW,0u);
    assert(copies==before_copies && reads==before_reads && !owner.stats.data_blocked_open);
}
static void data_denial_spans_are_sampled_and_control_closes_them(void) {
    prepare(200u);ready();data_command=16u;
    /* A command owned while all eight blocks are primed is not a missed fill
     * opportunity. Count only after the slower consumer frees the next ordered block. */
    uint32_t before_reads=reads,before_copies=copies;
    fill_step();assert(!owner.stats.data_blocked_calls && !owner.stats.data_blocked_intervals);
    while(owner.model.active_bank==0u) { visit(13021u);assert(!owner.stats.fault && !reports); }
    assert(owner.stats.data_blocked_open && owner.stats.data_blocked_calls==1u &&
           !owner.stats.data_blocked_intervals && !owner.stats.data_blocked_ticks_total);
    assert(reads==before_reads && copies==before_copies);
    uint32_t began=owner.data_blocked_tick;
    advance(20000u);fill_step();data_command=17u;advance(10000u);fill_step();
    assert(owner.stats.data_blocked_calls==3u && !owner.stats.data_blocked_intervals &&
           owner.stats.data_blocked_open && !owner.stats.data_blocked_ticks_total);
    assert(reads==before_reads && copies==before_copies);no_recoveries();
    data_command=0u;uint32_t close_tick=now+1u;fill_step();
    assert(!owner.stats.data_blocked_open && owner.stats.data_blocked_ticks_total==close_tick-began &&
           owner.stats.data_blocked_ticks_max==close_tick-began &&
           owner.stats.data_blocked_intervals==1u && copies>before_copies);
    no_recoveries();

    prepare(200u);ready();data_command=16u;
    while(owner.model.active_bank==0u) { visit(13021u);assert(!owner.stats.fault && !reports); }
    assert(owner.stats.data_blocked_open);
    advance(10000u);assert(kui_toy_pilot_request(CMD_PAUSE,0u,0u,0u)==3u);visit(0u);
    assert(!owner.stats.data_blocked_open && owner.stats.data_blocked_ticks_max>=10000u);
    assert(owner.stats.data_blocked_ticks_total==owner.stats.data_blocked_ticks_max &&
           owner.stats.data_blocked_intervals==1u);
    no_recoveries();

    prepare(200u);ready();data_command=16u;
    while(owner.model.active_bank==0u) { visit(13021u);assert(!owner.stats.fault && !reports); }
    advance(12345u);kui_toy_pilot_worker_shutdown();
    assert(!owner.stats.data_blocked_open && owner.stats.data_blocked_ticks_max>=12345u &&
           owner.stats.data_blocked_intervals==1u && owner.stats.state==KUI_TOY_PILOT_OFF);
    no_recoveries();
}
static void refused_stop_attempt_does_not_count_recovery(void) {
    prepare(200u);ready();hold_queue=true;
    uint32_t slot=QUEUE_BASE+((producer+1u)&31u)*16u;
    sound_set(slot,1u); /* Independently owned queue slot refuses publication. */
    sound_set(SOUND_BASE+0x14a4u,0xff000000u);
    uint32_t before_copies=copies,before_reads=reads;
    visit(0u);
    assert(owner.stats.bus_deferrals && !stop_queued && !owner.stop_wait && !reports);
    assert(copies==before_copies && reads==before_reads);no_recoveries();
}
static void lifecycle_interrupt_serializes_diagnostic_publication(void) {
    for(unsigned closing=0u;closing<2u;closing++) {
        prepare(200u);ready();data_command=16u;
        while(owner.model.active_bank==0u) { visit(13021u);assert(!owner.stats.fault && !reports); }
        assert(owner.stats.data_blocked_open && owner.stats.data_blocked_calls==1u &&
               !owner.stats.data_blocked_intervals);
        uint32_t before_copies=copies,before_reads=reads,before_packets=packets;
        advance(10000u);revoke_before_mask=true;
        if(closing) data_blocked_close();else data_blocked_sample(16u);
        assert(diagnostic_revokes==1u && !revoke_before_mask && sr==entry_sr);
        assert(owner.disabled && !owner.sdk_ready && owner.mailbox.generation!=owner.model.generation);
        assert(!owner.stats.data_blocked_open && owner.stats.data_blocked_calls==1u &&
               owner.stats.data_blocked_intervals==1u && owner.stats.data_blocked_ticks_max>=10000u &&
               owner.stats.data_blocked_ticks_total==owner.stats.data_blocked_ticks_max);
        assert(copies==before_copies && reads==before_reads && packets==before_packets);
        /* Later samples cannot recreate an interval on the old running-ring
         * fields that revoke deliberately retains for ownership safety. */
        uint32_t clock_reads=timer_reads;
        for(unsigned i=0u;i<100u;i++) { data_blocked_sample(16u);data_blocked_close(); }
        assert(!owner.stats.data_blocked_open && owner.stats.data_blocked_calls==1u &&
               owner.stats.data_blocked_intervals==1u && timer_reads==clock_reads && sr==entry_sr);
        no_recoveries();
    }
}
static void native_shutdown_revokes_worker_writes(void) {
    prepare(200u);ready();check_pcm=false;
    uint32_t before_copies=copies,before_reads=reads,before_packets=packets;
    kui_toy_pilot_worker_shutdown();
    /* The original native shutdown owns global sound stop. Do not attribute
     * its key-off to this authored worker or assume revoke is itself STOP. */
    render();running=looping=false;flag_delay=6u;
    for(unsigned i=0;i<8u;i++) visit(13021u);
    visit(CLOCK_GAP_LIMIT*2u);
    assert(owner.stats.state==KUI_TOY_PILOT_OFF && owner.disabled && !owner.sdk_ready);
    assert(copies==before_copies && reads==before_reads && packets==before_packets && !reports);
    assert(!kui_toy_pilot_request(CMD_PLAY,2u,2u,0u));
}
static void delayed_stop_ack_keeps_full_ring_settlement(void) {
    prepare(200u);ready();hold_queue=true;
    uint32_t before_copies=copies,before_reads=reads,began=now;
    assert(kui_toy_pilot_request(CMD_PAUSE,0u,0u,0u)==3u);visit(0u);
    assert(stop_queued);visit(390625u); /* Independently scheduled500ms SDK delay. */
    assert(stop_queued && running && owner.stats.applied_generation!=3u);
    assert(copies==before_copies && reads==before_reads);
    check_pcm=false;hold_queue=false;visit(0u);
    assert(!running && !stop_queued);
    while(now-began<=781250u) {
        visit(13021u);assert(owner.stats.applied_generation!=3u && copies==before_copies);
    }
    for(unsigned i=0;i<100u && owner.stats.applied_generation!=3u;i++) {
        visit(13021u);assert(!owner.stats.fault && !reports && copies==before_copies);
    }
    assert(now-began>781250u && now-began<1562500u && owner.stats.applied_generation==3u);
    assert(owner.stats.state==KUI_TOY_PILOT_PAUSED && reads==before_reads);
}
static void one_sector_quantum_spans_two_ordered_blocks(void) {
    prepare(200u);data_command=16u;
    for(unsigned i=0;i<20u && (owner.handled_generation!=2u || owner.stop_wait);i++) visit(13021u);
    assert(owner.stats.state==KUI_TOY_PILOT_PREFILL && !owner.ring_queued && !reads);
    data_command=0u;
    for(unsigned q=0;q<7u;q++) {
        uint32_t before_reads=reads,before=owner.ring_fill_stream;
        fill_quantum();
        assert(reads==before_reads+1u && owner.ring_fill_stream==before+588u && !owner.stats.fault);
    }
    assert(owner.model.banks[0].state==KUI_TOY_PILOT_BANK_READY &&
           owner.model.banks[0].filled==4096u && owner.model.banks[0].first_frame==0u);
    assert(owner.model.banks[1].state==KUI_TOY_PILOT_BANK_FILLING &&
           owner.model.banks[1].filled==20u && owner.model.banks[1].first_frame==4096u);
    for(uint32_t frame=0;frame<4116u;frame++) {
        assert((uint16_t)sample(owner.stats.sound_address+frame*2u)==(uint16_t)frame);
        assert((uint16_t)sample(owner.stats.sound_address+65536u+frame*2u)==(uint16_t)~frame);
    }
    /* A deferred right plane leaves the quantum uncommitted. Retry consumes
     * the same cached sector, with no extra callback or skipped frames. */
    defer_right_copy=true;uint32_t before_reads=reads,before=owner.ring_fill_stream;
    fill_quantum();assert(owner.bus_deferred && reads==before_reads+1u && owner.ring_fill_stream==before);
    owner.bus_deferred=0u;fill_quantum();
    assert(reads==before_reads+1u && owner.ring_fill_stream==before+588u && !owner.stats.fault);
}
static void delayed_proof_checks_all_crossed_block_epochs(void) {
    for(unsigned damage=0;damage<3u;damage++) {
        prepare(200u);ready();data_command=16u;
        if(damage==1u) owner.model.banks[1].generation=owner.model.generation-1u;
        if(damage==2u) owner.model.banks[2].first_frame+=32768u;
        uint32_t target=3u*4096u+100u,before_reads=reads,before_copies=copies;
        assert(physical_position()<target);
        visit(kui_toy_ring_ticks(target-physical_position()));
        assert(reads==before_reads && copies==before_copies && !owner.stats.fault && !reports);
        if(!damage) {
            no_recoveries();assert(owner.model.active_bank==3u && owner.stats.bank_ends==3u);
            for(unsigned slot=0;slot<3u;slot++) assert(owner.model.banks[slot].state==KUI_TOY_PILOT_BANK_EMPTY);
        } else {
            single_recovery(KUI_TOY_PILOT_RECOVERY_MISSING_HALF,16u);
            assert(owner.ring_consumed==damage*4096u && owner.resume_frame==damage*4096u);
        }
    }
    /* Missing next-block metadata cannot credit the last frames still held
     * by the slower capture when the accepted pair straddles its start. */
    prepare(200u);ready();data_command=16u;right_phase=24u;read_ticks=1u;
    owner.model.banks[1].filled=0u;
    uint32_t target=4080u;
    assert(physical_position()<target);visit(kui_toy_ring_ticks(target-physical_position()));
    single_recovery(KUI_TOY_PILOT_RECOVERY_MISSING_HALF,16u);
    assert(owner.ring_consumed>=4080u && owner.ring_consumed<4096u);
    assert(owner.resume_frame==(owner.ring_consumed&~1u));
}
int main(void) {
    one_sector_quantum_spans_two_ordered_blocks();delayed_proof_checks_all_crossed_block_epochs();
    smooth_stereo_and_once_only_tail(200u,0u);smooth_stereo_and_once_only_tail(5u,0u);
    smooth_stereo_and_once_only_tail(200u,7813u);smooth_stereo_and_once_only_tail(5u,7813u);
    infinite_repeat_has_no_block_restarts();serial_publications_preserve_pcm_and_block_ownership();
    delayed_proof_with_slow_card_preserves_pcm();
    measured_latency_hooks_preserve_pcm_across_update_gaps();
    clustered_measured_gaps_preserve_ordered_blocks();
    sustained_slow_card_stops_with_capture_evidence();
    reserve_window_wraps_keep_the_observed_epoch();
    bad_published_cursors_stop_before_reuse();locked_publication_cadence_stops_before_unsafe_reuse();
    long_gap_and_unacknowledged_stop_hold_the_lease();data_priority_underrun_stops_and_reprimes();
    late_card_return_requires_new_ownership_evidence();deferred_second_plane_does_not_publish_partial_stereo();
    maximum_latency_copies_recheck_each_plane();
    pause_movie_release_and_reset();native_shutdown_revokes_worker_writes();
    delayed_stop_ack_keeps_full_ring_settlement();
    missing_activity_and_impossible_cursor_have_distinct_reasons();
    initial_cursor_timeout_and_stream_overflow_are_counted();
    data_denial_spans_are_sampled_and_control_closes_them();
    refused_stop_attempt_does_not_count_recovery();
    lifecycle_interrupt_serializes_diagnostic_publication();
    puts("Toy ring worker: physical PCM/block ownership under serial publication, capture-bound pressure/latency diagnostics, eight recovery causes, lifecycle/EOF/control and pause/reset pass");
    return 0;
}
