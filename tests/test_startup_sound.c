/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/music.h"
#include "kui/music_ogg.h"
#include <dc/sound/stream.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../build/startup_ogg.inc"
#define STARTUP_BYTES ((size_t)KUI_STARTUP_COUNT*2u)
/* The cue as decoded independently from the same embedded Ogg. */
static int16_t reference[KUI_STARTUP_COUNT];
static _Alignas(16) uint8_t reference_arena[KUI_OGG_WORKSPACE_BYTES];
static struct {
    uint64_t ms,next_fill;size_t seen,previous_size;
    unsigned release,init,shutdown,destroy,starts,volume,callbacks,crossed;
    bool active,queued,cancel,init_error,alloc_error,poll_error;
    uint64_t cancel_ms;unsigned cancel_callbacks,setup_delay_ms;
    snd_stream_callback_t callback;
    void *previous;uint8_t previous_copy[32768];
} fake;
static bool cancel(void) {return fake.cancel || (fake.cancel_ms && fake.ms>=fake.cancel_ms) ||
    (fake.cancel_callbacks && fake.callbacks>=fake.cancel_callbacks);}
void kui_music_release_audio(void) {assert(!fake.active);++fake.release;}
uint64_t timer_ms_gettime64(void) {return fake.ms;}
void thd_sleep(unsigned ms) {fake.ms+=ms;}
int snd_stream_init_ex(int channels,size_t size) {
    assert(channels==1 && size==16384);++fake.init;fake.ms+=fake.setup_delay_ms;
    return fake.init_error?-1:0;
}
void snd_stream_shutdown(void) {assert(!fake.active);++fake.shutdown;}
snd_stream_hnd_t snd_stream_alloc(snd_stream_callback_t cb,int size) {
    assert(size==16384);fake.callback=cb;return fake.alloc_error?SND_STREAM_INVALID:0;
}
void snd_stream_destroy(snd_stream_hnd_t hnd) {assert(hnd==0 && !fake.queued);fake.active=false;++fake.destroy;}
void snd_stream_queue_enable(snd_stream_hnd_t hnd) {assert(hnd==0);fake.queued=true;}
void snd_stream_queue_disable(snd_stream_hnd_t hnd) {assert(hnd==0);fake.queued=false;}
void snd_stream_queue_go(snd_stream_hnd_t hnd) {assert(hnd==0 && fake.queued);fake.active=true;}
static bool fill(unsigned bytes) {
    int got=0;++fake.callbacks;void *data=fake.callback(0,(int)bytes,&got);
    if(!data) {assert((cancel() || fake.ms>=2700) && got==0);return false;}
    assert(got==(int)bytes && !((uintptr_t)data&31));
    if(fake.previous) {assert(data!=fake.previous);assert(!memcmp(fake.previous,fake.previous_copy,fake.previous_size));}
    size_t count=STARTUP_BYTES-fake.seen;if(count>bytes) count=bytes;
    assert(!memcmp(data,(const uint8_t *)reference+fake.seen,count));
    for(size_t i=count;i<bytes;i++) assert(((uint8_t *)data)[i]==0);
    fake.seen+=count;memcpy(fake.previous_copy,data,bytes);fake.previous=data;fake.previous_size=bytes;return true;
}
void snd_stream_start(snd_stream_hnd_t hnd,uint32_t rate,int stereo) {
    assert(hnd==0 && rate==KUI_STARTUP_RATE && !stereo && fake.queued);++fake.starts;
    (void)fill(8192);(void)fill(8192);fake.next_fill=fake.ms+93;
}
void snd_stream_volume(snd_stream_hnd_t hnd,int volume) {assert(hnd==0);fake.volume=(unsigned)volume;}
int snd_stream_poll(snd_stream_hnd_t hnd) {
    assert(hnd==0 && fake.active);if(fake.poll_error) return -1;
    if(fake.ms<fake.next_fill) return 0;
    /* The request reaching the cue's end asks for the full 32 KiB. All of it
     * past the last sample must be silence: the decoder never rewinds, which
     * the chime's own 80 ms of opening silence would otherwise hide. */
    fake.next_fill=fake.ms+93;
    unsigned bytes=STARTUP_BYTES-fake.seen<8192u?32768u:8192u;fake.crossed+=bytes==32768u;
    return fill(bytes)?0:-3;
}
int main(void) {
    struct kui_ogg ogg;
    assert(kui_ogg_open(&ogg,kui_startup_ogg,sizeof(kui_startup_ogg),reference_arena,sizeof(reference_arena),NULL));
    assert(ogg.channels==1u && ogg.rate==KUI_STARTUP_RATE && ogg.frames==KUI_STARTUP_COUNT);
    for(size_t at=0;at<STARTUP_BYTES;) {
        size_t count=STARTUP_BYTES-at<131072u?STARTUP_BYTES-at:131072u;
        assert(kui_ogg_fill(&ogg,(uint8_t *)reference+at,count)==count);at+=count;
    }
    kui_ogg_close(&ogg);
    kui_music_play_boot_chime(cancel);
    assert(fake.seen==STARTUP_BYTES && fake.crossed && fake.ms>=2650 && fake.ms<=2700);
    assert(fake.release==1 && fake.init==1 && fake.destroy==1 && fake.shutdown==1 && fake.volume==128 && !fake.active);
    for(unsigned fault=0;fault<6;fault++) {
        memset(&fake,0,sizeof(fake));
        if(fault==0) fake.cancel=true;
        if(fault==1) fake.cancel_ms=300;
        if(fault==2) fake.init_error=true;
        if(fault==3) fake.alloc_error=true;
        if(fault==4) fake.poll_error=true;
        if(fault==5) fake.cancel_callbacks=1;
        kui_music_play_boot_chime(cancel);
        assert(!fake.active && !fake.queued && fake.seen<STARTUP_BYTES);
        if(fault==0) assert(!fake.init && !fake.release);
        else assert(fake.shutdown==1);
        if(fault==5) assert(fake.volume==0 && fake.destroy==1);
    }
    /* Audio setup is charged to the same deadline. It must not add another
     * whole cue after slow initialization, including cancellation at prefill. */
    for(unsigned delay=2400;delay<=3200;delay+=800) {
        memset(&fake,0,sizeof(fake));fake.setup_delay_ms=delay;
        kui_music_play_boot_chime(cancel);
        assert(fake.ms==(delay>2700?delay:2700));
        assert(fake.seen<STARTUP_BYTES && fake.destroy==1 && fake.shutdown==1);
        assert(!fake.active && !fake.queued);
        if(delay>2700) assert(!fake.seen && fake.volume==0);
    }
    puts("PASS startup sound: compressed cue decoded in order, 2.7-second budget including setup, exact tail, DMA buffer lifetime, B skip and cleanup");return 0;
}
