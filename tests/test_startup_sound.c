/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/music.h"
#include <dc/sound/stream.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../build/startup_pcm.inc"
static struct {
    uint64_t ms,next_fill;size_t seen,previous_size;
    unsigned release,init,shutdown,destroy,starts,volume,callbacks;
    bool active,queued,cancel,init_error,alloc_error,poll_error;
    uint64_t cancel_ms;unsigned cancel_callbacks;
    snd_stream_callback_t callback;
    void *previous;uint8_t previous_copy[32768];
} fake;
static bool cancel(void) {return fake.cancel || (fake.cancel_ms && fake.ms>=fake.cancel_ms) ||
    (fake.cancel_callbacks && fake.callbacks>=fake.cancel_callbacks);}
void kui_music_release_audio(void) {assert(!fake.active);++fake.release;}
uint64_t timer_ms_gettime64(void) {return fake.ms;}
void thd_sleep(unsigned ms) {fake.ms+=ms;}
int snd_stream_init_ex(int channels,size_t size) {assert(channels==1 && size==16384);++fake.init;return fake.init_error?-1:0;}
void snd_stream_shutdown(void) {assert(!fake.active);++fake.shutdown;}
snd_stream_hnd_t snd_stream_alloc(snd_stream_callback_t cb,int size) {
    assert(size==16384);fake.callback=cb;return fake.alloc_error?SND_STREAM_INVALID:0;
}
void snd_stream_destroy(snd_stream_hnd_t hnd) {assert(hnd==0 && !fake.queued);fake.active=false;++fake.destroy;}
void snd_stream_queue_enable(snd_stream_hnd_t hnd) {assert(hnd==0);fake.queued=true;}
void snd_stream_queue_disable(snd_stream_hnd_t hnd) {assert(hnd==0);fake.queued=false;}
void snd_stream_queue_go(snd_stream_hnd_t hnd) {assert(hnd==0 && fake.queued);fake.active=true;}
static bool fill(void) {
    int got=0;++fake.callbacks;void *data=fake.callback(0,8192,&got);
    if(!data) {assert(cancel() && got==0);return false;}
    assert(got==8192 && !((uintptr_t)data&31));
    if(fake.previous) {assert(data!=fake.previous);assert(!memcmp(fake.previous,fake.previous_copy,fake.previous_size));}
    size_t count=sizeof(kui_startup_pcm)-fake.seen;if(count>8192) count=8192;
    assert(!memcmp(data,(const uint8_t *)kui_startup_pcm+fake.seen,count));
    for(size_t i=count;i<8192;i++) assert(((uint8_t *)data)[i]==0);
    fake.seen+=count;memcpy(fake.previous_copy,data,8192);fake.previous=data;fake.previous_size=8192;return true;
}
void snd_stream_start(snd_stream_hnd_t hnd,uint32_t rate,int stereo) {
    assert(hnd==0 && rate==KUI_STARTUP_RATE && !stereo && fake.queued);++fake.starts;
    (void)fill();(void)fill();fake.next_fill=fake.ms+93;
}
void snd_stream_volume(snd_stream_hnd_t hnd,int volume) {assert(hnd==0);fake.volume=(unsigned)volume;}
int snd_stream_poll(snd_stream_hnd_t hnd) {
    assert(hnd==0 && fake.active);if(fake.poll_error) return -1;
    if(fake.ms>=fake.next_fill) {fake.next_fill=fake.ms+93;return fill()?0:-3;}return 0;
}
int main(void) {
    kui_music_play_boot_chime(cancel);
    assert(fake.seen==sizeof(kui_startup_pcm) && fake.ms>=4400 && fake.ms<4600);
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
        assert(!fake.active && !fake.queued && fake.seen<sizeof(kui_startup_pcm));
        if(fault==0) assert(!fake.init && !fake.release);
        else assert(fake.shutdown==1);
        if(fault==5) assert(fake.volume==0 && fake.destroy==1);
    }
    puts("PASS startup sound: original PCM, exact tail, DMA buffer lifetime, B skip and audio cleanup");return 0;
}
