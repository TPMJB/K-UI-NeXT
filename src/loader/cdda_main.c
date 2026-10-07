/* SPDX-License-Identifier: GPL-3.0-only */
#include "cdda_aica.h"
#include "cdda_display.h"
#include "cdda_storage.h"
#include "kui/cdda_pcm.h"
#include "kui/cdda_ring.h"
#include "kui/cdda_clock.h"
#ifndef CDDA_TEST_PROFILE
#define CDDA_TEST_PROFILE 0
#endif
#if CDDA_TEST_PROFILE < 0 || CDDA_TEST_PROFILE > 7
#error "CDDA_TEST_PROFILE must be 0..7 (baseline through controlled service)"
#endif
#if CDDA_TEST_PROFILE > 0
#include "kui/cdda_stream.h"
#endif
#if CDDA_TEST_PROFILE == 4
#include "kui/cdda_timing.h"
#endif
#include "kui/storage_policy.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* This runtime envelope is accepted by the existing bootstrap. It supplies
 * its own filesystem, buses, main RAM, stack, sound RAM/channels and clock;
 * no pointer or audio service from the old shell survives. SCI is the only
 * storage backend admitted by this controlled homebrew experiment. */
extern volatile struct kui_storage_boot_marker cdda_storage_boot_marker;
extern uint32_t __cdda_stack_bottom[] __asm__("__cdda_stack_bottom");
extern uint32_t __cdda_stack_top[] __asm__("__cdda_stack_top");
#define MMIO8(a) (*(volatile uint8_t *)(uintptr_t)(a))
#define MMIO16(a) (*(volatile uint16_t *)(uintptr_t)(a))
#define MMIO32(a) (*(volatile uint32_t *)(uintptr_t)(a))

static struct kui_cdda_pcm pcm;
static struct kui_cdda_ring ring;
static int16_t left[128],right[128];
static uint32_t segment_end,max_refill_ticks,max_service_ticks,min_margin_ticks;
static unsigned failures,completed;
#ifdef CDDA_HARNESS_HOST_TEST
extern void cdda_harness_host_clock_init(void);
extern uint32_t cdda_harness_host_ticks(void);
#endif
#if CDDA_TEST_PROFILE > 0
static uint32_t loop_first,session_first,session_initial,session_frames;
static uint32_t loop_count,recovered_deadlines,expected_deadline_ticks;
static bool repeat_audio;
static struct kui_cdda_stream_clock profile_clock;
#define TONE_FIRST (6u*44100u+4410u)
#define TONE_FRAMES 44100u
#define SOAK_FRAMES (900u*44100u)
#if CDDA_TEST_PROFILE == 4
#define TIMING_DURATION_TICKS (60u*KUI_CDDA_CLOCK_HZ)
#define TIMING_READ_LIMIT KUI_CDDA_MARGIN_TICKS
#define TIMING_ELAPSED_LIMIT (TIMING_DURATION_TICKS+KUI_CDDA_HALF_TICKS+2u*TIMING_READ_LIMIT)
static struct kui_cdda_timing_snapshot timing_last,timing_start,timing_end;
static struct kui_cdda_timing_result timing_result;
static uint16_t timing_tcr_start,timing_tcr_end,timing_frq_start,timing_frq_end;
static bool timing_valid;
#endif
#if CDDA_TEST_PROFILE == 3
static _Alignas(32) uint8_t data_buffer[2048];
static uint32_t data_bytes,data_position,data_checked,data_errors,max_data_ticks;
static uint32_t data_budget_ticks; /* Initial allowance: ceil 10ms per job. */
static uint32_t data_checkpoint_seconds,data_checkpoint_bytes;
static uint32_t data_last_tick,data_max_gap_ticks;
#endif
#endif
#if CDDA_TEST_PROFILE == 6
static bool mixed_job_step(void);
#endif

/* SH7091 TMU1: peripheral clock /4 (documented reference), no interrupts. Exclusive
 * clock ownership is granted by this homebrew harness, never a retail game. */
static void clock_init(void) {
#ifdef CDDA_HARNESS_HOST_TEST
    cdda_harness_host_clock_init();
#else
    uint8_t run=MMIO8(0xffd80004u);
    MMIO8(0xffd80004u)=run & (uint8_t)~2u;
    MMIO16(0xffd8001cu)=0;
    MMIO32(0xffd80014u)=UINT32_MAX;
    MMIO32(0xffd80018u)=UINT32_MAX;
    MMIO8(0xffd80004u)=run | 2u;
#endif
}
static uint32_t ticks(void) {
#ifdef CDDA_HARNESS_HOST_TEST
    return cdda_harness_host_ticks();
#else
    return ~MMIO32(0xffd80018u);
#endif
}
static uint32_t us(uint32_t t) {
    uint32_t value;
    return kui_cdda_ticks_to_us(t,&value)?value:UINT32_MAX;
}
static bool read_at(void *ctx,uint32_t at,uint8_t *out,size_t bytes) {
    (void)ctx;
    return bytes<=UINT32_MAX && !cdda_storage_read_at(at,out,(uint32_t)bytes);
}
static bool observe(void) {
    uint32_t frame;
#if CDDA_TEST_PROFILE == 4
    uint32_t before=ticks();
#endif
    enum kui_cdda_aica_result a=kui_cdda_aica_position(&frame);
    if(a!=KUI_CDDA_AICA_OK) {
        (void)kui_cdda_aica_stop();
        cdda_display_number("AICA fault: ",(uint32_t)a);return false;
    }
    uint32_t now=ticks();
#if CDDA_TEST_PROFILE == 4
    if(now-before>TIMING_READ_LIMIT) {
        (void)kui_cdda_aica_stop();
        cdda_display_line("Timing observation window exceeded8ms; stopped.");return false;
    }
#endif
#if CDDA_TEST_PROFILE > 0
    if(!kui_cdda_stream_clock_update(&profile_clock,now)) {
        (void)kui_cdda_aica_stop();
        cdda_display_line("Owned clock accumulation failed.");return false;
    }
#endif
    enum kui_cdda_ring_result r=kui_cdda_ring_observe(&ring,now,frame);
    if(r!=KUI_CDDA_RING_OK) {
        (void)kui_cdda_aica_stop();
        cdda_display_number("Audio deadline/state fault: ",(uint32_t)r);return false;
    }
#if CDDA_TEST_PROFILE == 4
    /* Store the SAME position accepted by the ring. Never substitute PCM
     * prefetch progress or sample the position again for endpoint reporting. */
    timing_last=(struct kui_cdda_timing_snapshot){before,now,ring.played};
#endif
    return true;
}
static bool fill_half(unsigned half,bool playing) {
    uint32_t start=ticks();
    for(unsigned offset=0;offset<KUI_CDDA_HALF_FRAMES;offset+=128u) {
#if CDDA_TEST_PROFILE > 0
        size_t done=0;
        while(done<128u) {
            if(pcm.position==segment_end && repeat_audio &&
               kui_cdda_pcm_seek(&pcm,loop_first)!=KUI_CDDA_PCM_OK) return false;
            size_t count=segment_end-pcm.position;
            if(count>128u-done) count=128u-done;
            if(!count) break;
            size_t got=0;
            if(kui_cdda_pcm_read_frames(&pcm,left+done,right+done,count,&got)!=KUI_CDDA_PCM_OK || got!=count) {
                (void)kui_cdda_aica_stop();
                cdda_display_line("PCM/card read failed; owned audio stopped.");
                cdda_display_line(cdda_storage_last_failure());return false;
            }
            done+=got;
        }
#else
        size_t count=segment_end-pcm.position;
        if(count>128) count=128;
        size_t done=0;
        if(count && (kui_cdda_pcm_read_frames(&pcm,left,right,count,&done)!=KUI_CDDA_PCM_OK || done!=count)) {
            (void)kui_cdda_aica_stop();
            cdda_display_line("PCM/card read failed; owned audio stopped.");
            cdda_display_line(cdda_storage_last_failure());return false;
        }
#endif
        for(size_t i=done;i<128;i++) left[i]=right[i]=0;
        /* A card read may have blocked. Check time/positions BEFORE publishing
         * bytes to AICA, including a missed whole ring while the CPU was busy. */
        if(playing && (!observe() || kui_cdda_ring_can_fill(&ring,half)!=KUI_CDDA_RING_OK)) {
            (void)kui_cdda_aica_stop();
            cdda_display_line("Refill safety margin exhausted; audio stopped.");return false;
        }
        enum kui_cdda_aica_result a=kui_cdda_aica_write_samples(half,offset,left,right,128);
        if(a!=KUI_CDDA_AICA_OK) {
            (void)kui_cdda_aica_stop();
            cdda_display_number("AICA refill refused: ",(uint32_t)a);return false;
        }
        if(playing && !observe()) return false;
    }
    if(playing) {
        if(kui_cdda_ring_commit(&ring,half)!=KUI_CDDA_RING_OK) {
            (void)kui_cdda_aica_stop();
            cdda_display_line("Refill completed too late; audio stopped.");return false;
        }
        uint32_t elapsed=ticks()-start;
        if(elapsed>max_refill_ticks) max_refill_ticks=elapsed;
    }
    return true;
}
#if CDDA_TEST_PROFILE <= 1
static bool play(const char *path,uint32_t first,uint32_t frames) {
#if CDDA_TEST_PROFILE > 0
    repeat_audio=false;
#endif
    ring=(struct kui_cdda_ring){0};
    uint32_t bytes;
    if(cdda_storage_open(path,&bytes)) {
        cdda_display_line(cdda_storage_last_failure());return false;
    }
    if(!bytes || bytes%2352u) {
        cdda_display_line("Source must contain complete2352-byte audio sectors.");
        cdda_storage_close();return false;
    }
    const struct kui_cdda_pcm_source source={bytes,0,2352,bytes/2352u,false};
    bool success=false;
    if(kui_cdda_pcm_init(&pcm,&source,read_at,NULL)!=KUI_CDDA_PCM_OK ||
       first>=pcm.total_frames || (frames && frames>pcm.total_frames-first)) goto close;
    if(!frames) frames=pcm.total_frames-first;
    segment_end=first+frames;
    if(kui_cdda_pcm_seek(&pcm,first)!=KUI_CDDA_PCM_OK) goto close;
    if(!fill_half(0,false) || !fill_half(1,false)) goto close;
    enum kui_cdda_aica_result result=kui_cdda_aica_start();
    if(result!=KUI_CDDA_AICA_OK) {
        cdda_display_number("AICA start refused: ",(uint32_t)result);goto close;
    }
    uint32_t position;
    if(kui_cdda_aica_position(&position)!=KUI_CDDA_AICA_OK || position>=frames ||
       kui_cdda_ring_init(&ring,ticks(),position)!=KUI_CDDA_RING_OK) goto stop;
    frames-=position;
    uint32_t progress_tick=ticks(),progress=0,idle=0;
    while(ring.played<frames) {
        if(!observe()) goto stop;
        if(ring.played>=frames) break;
        if(ring.played!=progress) {progress=ring.played;progress_tick=ticks();idle=0;}
        else if(++idle>=1000000u || ticks()-progress_tick>=KUI_CDDA_HALF_TICKS) {
            (void)kui_cdda_aica_stop();
            cdda_display_line("AICA/clock progress watchdog stopped playback.");goto stop;
        }
        unsigned active=ring.last_position/KUI_CDDA_HALF_FRAMES;
        if(!ring.ready[1u-active] && !fill_half(1u-active,true)) goto stop;
    }
    success=true;
stop:
    if(kui_cdda_aica_stop()!=KUI_CDDA_AICA_OK) success=false;
    if(ring.max_service_ticks>max_service_ticks) max_service_ticks=ring.max_service_ticks;
    if(ring.min_margin_ticks<min_margin_ticks) min_margin_ticks=ring.min_margin_ticks;
close:
    cdda_storage_close();return success;
}
#endif
#if CDDA_TEST_PROFILE > 0
/* Every restart discards queued PCM and refills from an actual played cursor.
 * The initial hardware sample position is included; ring.played starts at zero
 * at its first observation, rather than assuming that key-on is instantaneous. */
#if CDDA_TEST_PROFILE != 5 && CDDA_TEST_PROFILE != 6 && CDDA_TEST_PROFILE != 7
static bool session_open(uint32_t first,uint32_t frames,bool repeat) {
    uint32_t bytes;
    ring=(struct kui_cdda_ring){0};
    if(cdda_storage_open("0:/KUI/tests/cdda/stereo.raw",&bytes)) {
        cdda_display_line(cdda_storage_last_failure());return false;
    }
    if(!bytes || bytes%2352u) goto failed;
    const struct kui_cdda_pcm_source source={bytes,0,2352u,bytes/2352u,false};
    if(kui_cdda_pcm_init(&pcm,&source,read_at,NULL)!=KUI_CDDA_PCM_OK ||
       !frames || first>=pcm.total_frames || frames>pcm.total_frames-first) goto failed;
    session_first=first;session_frames=frames;loop_first=first;
    segment_end=first+frames;repeat_audio=repeat;
    if(kui_cdda_pcm_seek(&pcm,first)!=KUI_CDDA_PCM_OK ||
       !fill_half(0,false) || !fill_half(1,false) ||
       kui_cdda_aica_start()!=KUI_CDDA_AICA_OK ||
       kui_cdda_aica_position(&session_initial)!=KUI_CDDA_AICA_OK ||
       session_initial>=frames ||
       kui_cdda_ring_init(&ring,ticks(),session_initial)!=KUI_CDDA_RING_OK) goto failed;
    return true;
failed:
    (void)kui_cdda_aica_stop();cdda_storage_close();
    cdda_display_line("Controlled session could not start.");return false;
}
#endif
static bool session_close(void) {
    bool success=kui_cdda_aica_stop()==KUI_CDDA_AICA_OK;
    if(ring.max_service_ticks>max_service_ticks) max_service_ticks=ring.max_service_ticks;
    if(ring.min_margin_ticks<min_margin_ticks) min_margin_ticks=ring.min_margin_ticks;
    repeat_audio=false;cdda_storage_close();return success;
}
#if CDDA_TEST_PROFILE != 4 && CDDA_TEST_PROFILE != 7
static bool played_cursor(uint32_t *frame,uint32_t *loops) {
    if(ring.played>UINT32_MAX-session_initial) return false;
    return kui_cdda_stream_cursor(session_first,session_first+session_frames,
        repeat_audio,session_initial+ring.played,frame,loops);
}
#endif
#if CDDA_TEST_PROFILE == 3 || CDDA_TEST_PROFILE == 4 || CDDA_TEST_PROFILE == 6 || CDDA_TEST_PROFILE == 7
static void display_pair(const char *label,uint32_t a,uint32_t b) {
    char text[96],reverse[10];unsigned n=0;
    while(*label && n<60u) text[n++]=*label++;
    for(unsigned which=0;which<2u;which++) {
        uint32_t value=which?b:a;unsigned digits=0;
        do {reverse[digits++]=(char)('0'+value%10u);value/=10u;} while(value);
        while(digits) text[n++]=reverse[--digits];
        if(!which) {text[n++]=' ';text[n++]='/';text[n++]=' ';}
    }
    text[n]=0;cdda_display_line(text);
}
#endif
#if CDDA_TEST_PROFILE == 3
static uint8_t data_pattern(uint32_t offset) {
    uint32_t x=offset^0x9e3779b9u;
    x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;
    return (uint8_t)x;
}
static bool stress_job(void) {
    /* Storage calls remain serialized with audio reads. Both halves must be
     * ready: a data job can never postpone an already-needed audio refill. */
    if(!ring.ready[0] || !ring.ready[1]) return true;
    uint32_t margin;
    if(!kui_cdda_frames_to_ticks(KUI_CDDA_HALF_FRAMES-
        ring.last_position%KUI_CDDA_HALF_FRAMES,&margin)) return false;
    if(data_budget_ticks>(UINT32_MAX-KUI_CDDA_MARGIN_TICKS)/2u ||
       margin<=data_budget_ticks*2u+KUI_CDDA_MARGIN_TICKS) return true;
    uint32_t count=data_bytes-data_position;
    if(count>sizeof(data_buffer)) count=sizeof(data_buffer);
    uint32_t start=ticks();
    int result=cdda_storage_data_read_at(data_position,data_buffer,count);
    uint32_t elapsed=ticks()-start;
    if(elapsed>max_data_ticks) max_data_ticks=elapsed;
    if(elapsed>data_budget_ticks) data_budget_ticks=elapsed;
    if(result) data_errors++;
    /* A blocking read may cross a half boundary, or miss the conservative
     * scheduler deadline. Observe before another read or any audio write. */
    if(!observe()) return false;
    if(result) {cdda_display_line(cdda_storage_last_failure());return false;}
    for(uint32_t i=0;i<count;i++) {
        uint32_t at=data_position+i;
        uint8_t expected=data_pattern(at);
        if(data_buffer[i]!=expected) {data_errors++;cdda_display_line("Second-file pattern mismatch.");return false;}
    }
    data_position+=count;
    if(data_position==data_bytes) data_position=0;
    if(count>UINT32_MAX-data_checked) {data_errors++;return false;}
    data_checked+=count;
    uint32_t now=ticks(),gap=now-data_last_tick;
    elapsed=now-start; /* Include position recheck and byte verification in budget. */
    if(elapsed>max_data_ticks) max_data_ticks=elapsed;
    if(elapsed>data_budget_ticks) data_budget_ticks=elapsed;
    if(gap>data_max_gap_ticks) data_max_gap_ticks=gap;
    data_last_tick=now;
    return true;
}
#endif
#if CDDA_TEST_PROFILE != 4 && CDDA_TEST_PROFILE != 7
static bool session_pump(uint32_t frames,bool minimum_duration) {
    uint32_t progress_tick=ticks(),progress=ring.played,idle=0;
    for(;;) {
        if(!observe()) return false;
        if(ring.played>UINT32_MAX-session_initial) return false;
        uint32_t actual=session_initial+ring.played;
        if(repeat_audio) loop_count=actual/session_frames;
#if CDDA_TEST_PROFILE == 3
        uint32_t data_gap=ticks()-data_last_tick;
        if(data_gap>data_max_gap_ticks) data_max_gap_ticks=data_gap;
        if(data_gap>=5u*KUI_CDDA_CLOCK_HZ) {
            (void)kui_cdda_aica_stop();
            cdda_display_line("Stress workload starved: no verified job for5s.");return false;
        }
        if(profile_clock.seconds-data_checkpoint_seconds>=60u) {
            if(data_checked-data_checkpoint_bytes<65536u) {
                (void)kui_cdda_aica_stop();
                cdda_display_line("Stress admission stalled: below64KiB/minute.");return false;
            }
            data_checkpoint_seconds=profile_clock.seconds;data_checkpoint_bytes=data_checked;
        }
#endif
        if(actual>=frames && (!minimum_duration || profile_clock.seconds>=900u)) break;
        if(ring.played!=progress) {progress=ring.played;progress_tick=ticks();idle=0;}
        else if(++idle>=1000000u || ticks()-progress_tick>=KUI_CDDA_HALF_TICKS) {
            (void)kui_cdda_aica_stop();cdda_display_line("AICA/clock progress watchdog.");return false;
        }
        unsigned active=ring.last_position/KUI_CDDA_HALF_FRAMES;
        if(!ring.ready[1u-active] && !fill_half(1u-active,true)) return false;
#if CDDA_TEST_PROFILE == 3
        if(!stress_job()) return false;
#elif CDDA_TEST_PROFILE == 6
        if(!mixed_job_step()) return false;
#endif
    }
    uint32_t frame,loops;
    if(repeat_audio && !played_cursor(&frame,&loops)) return false;
    if(repeat_audio) loop_count=loops;
    return true;
}
#endif
#if CDDA_TEST_PROFILE == 1 || CDDA_TEST_PROFILE == 5 || CDDA_TEST_PROFILE == 6
static bool paused_silence(void) {
    uint32_t start=ticks();
    /* Key-off plus master mute is the silence contract. Do not query stopped
     * channel phase: the sequential stops need not preserve matched positions. */
    while(ticks()-start<KUI_CDDA_CLOCK_HZ) {
        if(!kui_cdda_stream_clock_update(&profile_clock,ticks())) return false;
    }
    return true;
}
#endif
#if CDDA_TEST_PROFILE == 1
static bool pause_resume(void) {
    if(!session_open(TONE_FIRST,2u*TONE_FRAMES,false)) return false;
    if(!session_pump(TONE_FRAMES,false) || !observe()) goto failed;
    uint32_t resume,loops;
    if(!played_cursor(&resume,&loops) || loops || resume>=segment_end ||
       resume==pcm.position) goto failed;
    uint32_t end=segment_end;
    /* Save before stopping. pcm.position is the deliberately discarded SD
     * prefetch cursor; resumed audio begins at the observed played frame. */
    if(!session_close()) return false;
    cdda_display_line("Muted pause (1s), resume from played cursor.");
    if(!paused_silence() || !session_open(resume,end-resume,false)) return false;
    if(!session_pump(end-resume,false)) goto failed;
    return session_close();
failed:
    (void)session_close();return false;
}
static bool bounds_and_eof(void) {
    if(!session_open(TONE_FIRST,TONE_FRAMES,false)) return false;
    if(!session_pump(TONE_FRAMES,false) || !session_close()) return false;
    uint32_t bytes;
    if(cdda_storage_open("0:/KUI/tests/cdda/stereo.raw",&bytes)) return false;
    const struct kui_cdda_pcm_source source={bytes,0,2352u,bytes/2352u,false};
    bool success=kui_cdda_pcm_init(&pcm,&source,read_at,NULL)==KUI_CDDA_PCM_OK;
    size_t done=9;
    if(success) success=kui_cdda_pcm_seek(&pcm,pcm.total_frames)==KUI_CDDA_PCM_OK &&
        kui_cdda_pcm_read_frames(&pcm,left,right,128,&done)==KUI_CDDA_PCM_EOF && !done &&
        kui_cdda_pcm_seek(&pcm,pcm.total_frames+1u)==KUI_CDDA_PCM_INVALID &&
        pcm.position==pcm.total_frames;
    cdda_storage_close();
    return success && play("0:/KUI/tests/cdda/track14.raw",1761648u,44100u);
}
static bool deadline_recovery(void) {
    if(!session_open(TONE_FIRST,TONE_FRAMES,true)) return false;
    if(!observe()) goto failed;
    uint32_t before=ring.last_tick;
    /* 190ms exceeds one185.76ms half, while remaining well below a full ring.
     * This checks the conservative observation deadline BEFORE a refill write;
     * it cannot promise no stale audio after arbitrarily long CPU stalls. */
    uint32_t delay_ticks;
    if(!kui_cdda_ms_to_ticks(190u,&delay_ticks)) goto failed;
    while(ticks()-before<delay_ticks) __asm__ __volatile__("nop");
    uint32_t position,now=ticks(),regular_gap=ring.max_service_ticks;
    if(kui_cdda_aica_position(&position)!=KUI_CDDA_AICA_OK ||
       kui_cdda_ring_observe(&ring,now,position)!=KUI_CDDA_RING_DEADLINE) goto failed;
    expected_deadline_ticks=now-before;
    ring.max_service_ticks=regular_gap;
    /* A rejected observation leaves the ring cursor stale. Never use it as a
     * resume point or publish a half. Mute, discard it, and start a new session. */
    if(!session_close()) return false;
    cdda_display_line("EXPECTED deadline refusal before refill; stopped.");
    cdda_display_line("Tests 190ms policy; arbitrary stalls can replay audio.");
    if(!session_open(TONE_FIRST,TONE_FRAMES,false)) return false;
    if(!session_pump(TONE_FRAMES,false) || !session_close()) return false;
    recovered_deadlines++;
    return true;
failed:
    (void)session_close();return false;
}
static bool controls(void) {
    cdda_display_line("1/5: play left; automatic seek/restart at1s");
    if(!session_open(0,3u*44100u,false) || !session_pump(44100u,false) || !session_close()) return false;
    completed++;
    cdda_display_line("2/5: restart at right-channel region (1s)");
    if(!session_open(3u*44100u+4410u,44100u,false) || !session_pump(44100u,false) || !session_close()) return false;
    completed++;
    cdda_display_line("3/5: stop, pause silence, resume played cursor");
    if(!pause_resume()) return false;
    completed++;
    cdda_display_line("4/5: EOF/bounds and Toy Commander tail");
    if(!bounds_and_eof()) return false;
    completed++;
    cdda_display_line("5/5: expected deadline and clean recovery");
    if(!deadline_recovery()) return false;
    completed++;
    return true;
}
#elif CDDA_TEST_PROFILE == 2 || CDDA_TEST_PROFILE == 3
static bool soak(void) {
#if CDDA_TEST_PROFILE == 3
    if(cdda_storage_data_open(&data_bytes) || data_bytes!=8u*1024u*1024u) {
        cdda_display_line("Stress requires exact8MiB stress.bin.");
        cdda_storage_data_close();return false;
    }
#endif
    cdda_display_line("15min seamless stereo loop; clock wrap test.");
#if CDDA_TEST_PROFILE == 3
    cdda_display_line("Serialized second-file reads; pattern checked.");
    data_checkpoint_seconds=profile_clock.seconds;data_checkpoint_bytes=data_checked;
    data_last_tick=ticks();data_max_gap_ticks=0;
#endif
    bool success=session_open(TONE_FIRST,TONE_FRAMES,true) && session_pump(SOAK_FRAMES,true);
    if(!session_close()) success=false;
    if(profile_clock.wraps<2u || profile_clock.seconds<900u || loop_count<900u) success=false;
#if CDDA_TEST_PROFILE == 3
    cdda_storage_data_close();
    if(data_checked<data_bytes || data_errors) success=false;
#endif
    if(success) completed++;
    return success;
}
#endif
#if CDDA_TEST_PROFILE == 4
static void timing_clock_config(uint16_t *tcr,uint16_t *frq) {
#ifdef CDDA_HARNESS_HOST_TEST
    /* The portable simulation does not claim a hardware register reading. */
    *tcr=*frq=0;
#else
    *tcr=MMIO16(0xffd8001cu);
    *frq=MMIO16(0xffc00000u);
#endif
}
static bool calibration(void) {
    cdda_display_line("60s paired AICA/TMU observations; documented clock.");
    if(!session_open(TONE_FIRST,TONE_FRAMES,true)) return false;
    timing_clock_config(&timing_tcr_start,&timing_frq_start);
    /* TPSC=0 selects peripheral-clock/4. UNF (bit8) may become sticky across
     * a timer wrap; compare control/divider bits independently of that flag. */
    if((timing_tcr_start & 0x0007u) || !observe()) goto failed;
    timing_start=timing_last;
    uint32_t progress=ring.played,progress_tick=ring.last_tick,idle=0;
    for(;;) {
        if(!observe()) goto failed;
        timing_end=timing_last;
        uint32_t elapsed=timing_end.after_tick-timing_start.before_tick;
        if(elapsed>TIMING_ELAPSED_LIMIT) goto failed;
        /* Lower bound reaches reference60s even with unequal endpoint
         * register-read windows. The oscillator itself is not measured here. */
        if(timing_end.before_tick-timing_start.after_tick>=TIMING_DURATION_TICKS) break;
        if(ring.played!=progress) {progress=ring.played;progress_tick=ring.last_tick;idle=0;}
        else if(++idle>=1000000u || ticks()-progress_tick>=KUI_CDDA_HALF_TICKS) {
            cdda_display_line("Timing AICA/clock progress watchdog.");goto failed;
        }
        unsigned active=ring.last_position/KUI_CDDA_HALF_FRAMES;
        if(!ring.ready[1u-active] && !fill_half(1u-active,true)) goto failed;
    }
    /* The final endpoint was observed before key-off; stopping and display
     * work are deliberately excluded from its bracket. */
    if(!session_close()) return false;
    timing_clock_config(&timing_tcr_end,&timing_frq_end);
    if((timing_tcr_start & (uint16_t)~0x0100u)!=(timing_tcr_end & (uint16_t)~0x0100u) ||
       timing_frq_start!=timing_frq_end ||
       !kui_cdda_timing_measure(&timing_start,&timing_end,TIMING_READ_LIMIT,
                               TIMING_ELAPSED_LIMIT,&timing_result)) return false;
    timing_valid=true;completed++;
    return true;
failed:
    (void)session_close();return false;
}
#endif
#if CDDA_TEST_PROFILE == 5 || CDDA_TEST_PROFILE == 6 || CDDA_TEST_PROFILE == 7
#include "cdda_commands.inc"
#endif
#if CDDA_TEST_PROFILE == 6
#include "cdda_mixed.inc"
#endif
#if CDDA_TEST_PROFILE == 7
#include "cdda_service.inc"
#endif
#endif
static bool check_stack(uint32_t *used) {
#ifdef CDDA_HARNESS_HOST_TEST
    *used=0;return true;
#else
    for(unsigned i=0;i<16;i++) if(__cdda_stack_bottom[i]!=0x43444441u) return false;
    uint32_t *p=__cdda_stack_bottom+16;
    while(p<__cdda_stack_top && *p==0xa5a5a5a5u) p++;
    *used=(uint32_t)((uintptr_t)__cdda_stack_top-(uintptr_t)p);
    return true;
#endif
}
void cdda_main(void) {
    bool audio_owned=false;
    failures=completed=0;max_refill_ticks=max_service_ticks=0;
    clock_init();cdda_display_init();min_margin_ticks=UINT32_MAX;
#if CDDA_TEST_PROFILE > 0
    loop_count=recovered_deadlines=expected_deadline_ticks=0;repeat_audio=false;
    if(!kui_cdda_stream_clock_init(&profile_clock,ticks(),KUI_CDDA_CLOCK_HZ)) {
        cdda_display_line("Owned clock could not initialize.");failures++;goto finish;
    }
#if CDDA_TEST_PROFILE == 3
    data_bytes=data_position=data_checked=data_errors=max_data_ticks=0;
    if(!kui_cdda_ms_to_ticks(10u,&data_budget_ticks)) {
        failures++;goto finish;
    }
    data_last_tick=data_max_gap_ticks=data_checkpoint_seconds=data_checkpoint_bytes=0;
#endif
#if CDDA_TEST_PROFILE == 4
    timing_last=timing_start=timing_end=(struct kui_cdda_timing_snapshot){0};
    timing_result=(struct kui_cdda_timing_result){0};timing_valid=false;
    timing_tcr_start=timing_tcr_end=timing_frq_start=timing_frq_end=0;
#endif
#endif
    if(cdda_storage_boot_marker.magic1!=KUI_STORAGE_BOOT_MAGIC1 ||
       cdda_storage_boot_marker.magic2!=KUI_STORAGE_BOOT_MAGIC2 ||
       cdda_storage_boot_marker.version!=1 || cdda_storage_boot_marker.transport!=KUI_STORAGE_SCI ||
       cdda_storage_boot_marker.inverse!=~(uint32_t)KUI_STORAGE_SCI) {
        cdda_display_line("Use this test from the SCI card boot menu.");failures++;goto finish;
    }
    if(kui_cdda_aica_init()!=KUI_CDDA_AICA_OK) {
        cdda_display_line("Could not claim controlled AICA resources.");failures++;goto finish;
    }
    audio_owned=true;
    if(cdda_storage_init()) {
        cdda_display_line(cdda_storage_last_failure());failures++;goto finish;
    }
#if CDDA_TEST_PROFILE == 0
    cdda_display_line("1/5: tones LEFT, RIGHT, STEREO, silence (12s)");
    if(!play("0:/KUI/tests/cdda/stereo.raw",0,0)) {failures++;goto finish;} completed++;
    cdda_display_line("2/5: seek into generated stereo fixture (2s)");
    if(!play("0:/KUI/tests/cdda/stereo.raw",300001,88200)) {failures++;goto finish;} completed++;
    cdda_display_line("3/5: repeat generated stereo region (2s)");
    if(!play("0:/KUI/tests/cdda/stereo.raw",300001,88200)) {failures++;goto finish;} completed++;
    cdda_display_line("4/5: Toy Commander track14 (40.95s expected)");
    if(!play("0:/KUI/tests/cdda/track14.raw",0,0)) {failures++;goto finish;} completed++;
    cdda_display_line("5/5: seek near Toy Commander EOF (1s)");
    if(!play("0:/KUI/tests/cdda/track14.raw",1761648,44100)) {failures++;goto finish;} completed++;
#elif CDDA_TEST_PROFILE == 1
    cdda_display_line("PROFILE1 CONTROLS / seek-pause-deadline");
    if(!controls()) failures++;
#elif CDDA_TEST_PROFILE == 2
    cdda_display_line("PROFILE2 SOAK /15min continuous audio");
    if(!soak()) failures++;
#elif CDDA_TEST_PROFILE == 3
    cdda_display_line("PROFILE3 STRESS /15min audio + second file");
    if(!soak()) failures++;
#elif CDDA_TEST_PROFILE == 4
    cdda_display_line("PROFILE4 TIMING / reference60s paired endpoints");
    if(!calibration()) failures++;
#elif CDDA_TEST_PROFILE == 5
    cdda_display_line("PROFILE5 COMMANDS / generated audio only");
    if(!commands()) failures++;
#elif CDDA_TEST_PROFILE == 6
    cdda_display_line("PROFILE6 MIXED / audio + bounded data jobs");
    if(!mixed()) failures++;
#else
    cdda_display_line("PROFILE7 SERVICE / separate controlled client");
    if(!service_test()) failures++;
#endif
finish:
    if(audio_owned && kui_cdda_aica_stop()!=KUI_CDDA_AICA_OK) {
        cdda_display_line("Final owned-channel stop could not be confirmed.");failures++;
    }
    cdda_storage_shutdown();
#if CDDA_TEST_PROFILE > 0
    if(ring.initialized) {
        if(ring.max_service_ticks>max_service_ticks) max_service_ticks=ring.max_service_ticks;
        if(ring.min_margin_ticks<min_margin_ticks) min_margin_ticks=ring.min_margin_ticks;
    }
#endif
    uint32_t stack_used=0;
    if(!check_stack(&stack_used)) {cdda_display_line("PRIVATE STACK GUARD FAILED");failures++;}
#if CDDA_TEST_PROFILE == 0
    cdda_display_line("PROFILE0 BASELINE / original five stages");
#elif CDDA_TEST_PROFILE == 1
    cdda_display_line("PROFILE1 CONTROLS / expected fault separate");
#elif CDDA_TEST_PROFILE == 2
    cdda_display_line("PROFILE2 SOAK /15min continuous stereo");
#elif CDDA_TEST_PROFILE == 3
    cdda_display_line("PROFILE3 STRESS /15min audio + data checks");
#elif CDDA_TEST_PROFILE == 4
    cdda_display_line("PROFILE4 TIMING / paired endpoints, reference60s");
#elif CDDA_TEST_PROFILE == 5
    cdda_display_line("PROFILE5 COMMANDS / controlled homebrew");
#elif CDDA_TEST_PROFILE == 6
    cdda_display_line("PROFILE6 MIXED / controlled audio + data");
#else
    cdda_display_line("PROFILE7 SERVICE / owned cooperative calls");
#endif
    cdda_display_number("Completed playback stages: ",completed);
    cdda_display_number("Worst half refill (us): ",us(max_refill_ticks));
    cdda_display_number("Maximum service gap (us): ",us(max_service_ticks));
    cdda_display_number("Minimum refill margin (us): ",min_margin_ticks==UINT32_MAX?0:us(min_margin_ticks));
    cdda_display_number("Checked card blocks: ",cdda_storage_blocks_read());
    cdda_display_number("Observed private stack use (bytes): ",stack_used);
#if CDDA_TEST_PROFILE > 0 && CDDA_TEST_PROFILE < 4
    cdda_display_number("Played loop passes: ",loop_count);
    cdda_display_number("Owned clock seconds: ",profile_clock.seconds);
    cdda_display_number("Observed clock wraps: ",profile_clock.wraps);
#if CDDA_TEST_PROFILE == 1
    cdda_display_number("EXPECTED deadline delay (us): ",us(expected_deadline_ticks));
    cdda_display_number("Recovered EXPECTED deadlines: ",recovered_deadlines);
#elif CDDA_TEST_PROFILE == 3
    cdda_display_number("Verified second-file bytes: ",data_checked);
    cdda_display_number("Verified complete8MiB passes: ",data_bytes?data_checked/data_bytes:0);
    cdda_display_number("Second-file read/check errors: ",data_errors);
    display_pair("Data job / completion gap (us): ",us(max_data_ticks),us(data_max_gap_ticks));
#endif
#endif
#if CDDA_TEST_PROFILE == 4
    cdda_display_number("Paired played frames: ",timing_result.played_frames);
    display_pair("Elapsed ticks lower / upper: ",timing_result.lower_ticks,timing_result.upper_ticks);
    display_pair("Read ticks start / end: ",timing_result.start_read_ticks,timing_result.end_read_ticks);
    display_pair("TMU1 TCR1 start / end: ",timing_tcr_start,timing_tcr_end);
    display_pair("FRQCR start / end: ",timing_frq_start,timing_frq_end);
    display_pair("TMU reference Hz / configured pitch: ",KUI_CDDA_CLOCK_HZ,0u);
    cdda_display_line(timing_valid?"Relative-rate data; endpoint quantization +/-1frame.":"Paired timing result unavailable; no rate inferred.");
#elif CDDA_TEST_PROFILE == 5
    commands_report();
#elif CDDA_TEST_PROFILE == 6
    mixed_report();
#elif CDDA_TEST_PROFILE == 7
    service_report();
#endif
    cdda_display_number("Failures / stopped audio: ",failures);
    cdda_display_finish(failures);
#ifndef CDDA_HARNESS_HOST_TEST
    for(;;) __asm__ __volatile__("nop");
#endif
}
