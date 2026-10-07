/* SPDX-License-Identifier: GPL-3.0-only */
#include "cdda_aica.h"
#include "cdda_display.h"
#include "cdda_storage.h"
#include "kui/cdda_pcm.h"
#include "kui/cdda_ring.h"
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

/* SH7091 TMU1: peripheral clock /4 (nominal12.5MHz), no interrupts. Exclusive
 * clock ownership is granted by this homebrew harness, never a retail game. */
static void clock_init(void) {
    uint8_t run=MMIO8(0xffd80004u);
    MMIO8(0xffd80004u)=run & (uint8_t)~2u;
    MMIO16(0xffd8001cu)=0;
    MMIO32(0xffd80014u)=UINT32_MAX;
    MMIO32(0xffd80018u)=UINT32_MAX;
    MMIO8(0xffd80004u)=run | 2u;
}
static uint32_t ticks(void) {return ~MMIO32(0xffd80018u);}
static uint32_t us(uint32_t t) {return (t/25u)*2u+(t%25u)*2u/25u;}
static bool read_at(void *ctx,uint32_t at,uint8_t *out,size_t bytes) {
    (void)ctx;
    return bytes<=UINT32_MAX && !cdda_storage_read_at(at,out,(uint32_t)bytes);
}
static bool observe(void) {
    uint32_t frame;
    enum kui_cdda_aica_result a=kui_cdda_aica_position(&frame);
    if(a!=KUI_CDDA_AICA_OK) {
        (void)kui_cdda_aica_stop();
        cdda_display_number("AICA fault: ",(uint32_t)a);return false;
    }
    enum kui_cdda_ring_result r=kui_cdda_ring_observe(&ring,ticks(),frame);
    if(r!=KUI_CDDA_RING_OK) {
        (void)kui_cdda_aica_stop();
        cdda_display_number("Audio deadline/state fault: ",(uint32_t)r);return false;
    }
    return true;
}
static bool fill_half(unsigned half,bool playing) {
    uint32_t start=ticks();
    for(unsigned offset=0;offset<KUI_CDDA_HALF_FRAMES;offset+=128u) {
        size_t count=segment_end-pcm.position;
        if(count>128) count=128;
        size_t done=0;
        if(count && (kui_cdda_pcm_read_frames(&pcm,left,right,count,&done)!=KUI_CDDA_PCM_OK || done!=count)) {
            (void)kui_cdda_aica_stop();
            cdda_display_line("PCM/card read failed; owned audio stopped.");
            cdda_display_line(cdda_storage_last_failure());return false;
        }
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
static bool play(const char *path,uint32_t first,uint32_t frames) {
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
static bool check_stack(uint32_t *used) {
    for(unsigned i=0;i<16;i++) if(__cdda_stack_bottom[i]!=0x43444441u) return false;
    uint32_t *p=__cdda_stack_bottom+16;
    while(p<__cdda_stack_top && *p==0xa5a5a5a5u) p++;
    *used=(uint32_t)((uintptr_t)__cdda_stack_top-(uintptr_t)p);
    return true;
}
void cdda_main(void) {
    bool audio_owned=false;
    clock_init();cdda_display_init();min_margin_ticks=UINT32_MAX;
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
finish:
    if(audio_owned && kui_cdda_aica_stop()!=KUI_CDDA_AICA_OK) {
        cdda_display_line("Final owned-channel stop could not be confirmed.");failures++;
    }
    cdda_storage_shutdown();
    uint32_t stack_used=0;
    if(!check_stack(&stack_used)) {cdda_display_line("PRIVATE STACK GUARD FAILED");failures++;}
    cdda_display_number("Completed playback stages: ",completed);
    cdda_display_number("Worst half refill (us): ",us(max_refill_ticks));
    cdda_display_number("Maximum service gap (us): ",us(max_service_ticks));
    cdda_display_number("Minimum refill margin (us): ",min_margin_ticks==UINT32_MAX?0:us(min_margin_ticks));
    cdda_display_number("Checked card blocks: ",cdda_storage_blocks_read());
    cdda_display_number("Observed private stack use (bytes): ",stack_used);
    cdda_display_number("Failures / stopped audio: ",failures);
    cdda_display_finish(failures);
    for(;;) __asm__ __volatile__("nop");
}
