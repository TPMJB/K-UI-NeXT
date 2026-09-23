/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/music.h"
#include <dc/sound/stream.h>
#include <kos/thread.h>
#include <kos/timer.h>
#include <stdlib.h>
#include <string.h>
#include "../../build/startup_pcm.inc"
#define STARTUP_RING_BYTES 16384u
#define STARTUP_CALLBACK_BYTES 32768u
static struct {
    uint8_t *buffers[2];
    size_t position;
    unsigned next;
    kui_cancel_fn cancel;
    bool failed,stopped;
} startup;
static void *startup_data(snd_stream_hnd_t stream,int requested,int *received) {
    (void)stream;
    if(!received) return NULL;
    *received=0;
    if(startup.cancel && startup.cancel()) {startup.stopped=true;return NULL;}
    if(requested<=0 || (unsigned)requested>STARTUP_CALLBACK_BYTES || (requested&1) ||
       !startup.buffers[0] || !startup.buffers[1]) {startup.failed=true;return NULL;}
    uint8_t *out=startup.buffers[startup.next];startup.next^=1u;
    size_t total=sizeof(kui_startup_pcm),count=total-startup.position;
    if(count>(size_t)requested) count=(size_t)requested;
    memcpy(out,(const uint8_t *)kui_startup_pcm+startup.position,count);
    memset(out+count,0,(size_t)requested-count);startup.position+=count;
    *received=requested;return out;
}
void kui_music_play_boot_chime(kui_cancel_fn cancel) {
    if(cancel && cancel()) return;
    kui_music_release_audio();memset(&startup,0,sizeof(startup));startup.cancel=cancel;
    startup.buffers[0]=aligned_alloc(32,STARTUP_CALLBACK_BYTES);
    startup.buffers[1]=aligned_alloc(32,STARTUP_CALLBACK_BYTES);
    snd_stream_hnd_t stream=SND_STREAM_INVALID;bool audio=false;
    if(!startup.buffers[0] || !startup.buffers[1]) goto done;
    if(snd_stream_init_ex(1,STARTUP_RING_BYTES)<0) {snd_stream_shutdown();goto done;}
    audio=true;stream=snd_stream_alloc(startup_data,STARTUP_RING_BYTES);
    if(stream==SND_STREAM_INVALID) goto done;
    snd_stream_queue_enable(stream);
    snd_stream_start(stream,KUI_STARTUP_RATE,0);snd_stream_volume(stream,128);
    if(startup.failed || startup.stopped || (cancel && cancel())) {
        /* start queued a global AICA pause. Release that queue even on Skip,
         * at zero volume, before destroy submits the stop command. */
        snd_stream_volume(stream,0);snd_stream_queue_go(stream);snd_stream_queue_disable(stream);goto done;
    }
    snd_stream_queue_go(stream);snd_stream_queue_disable(stream);
    uint64_t end=timer_ms_gettime64()+((uint64_t)KUI_STARTUP_COUNT*1000u+KUI_STARTUP_RATE-1u)/KUI_STARTUP_RATE+32u;
    while(timer_ms_gettime64()<end && !(cancel && cancel())) {
        if(snd_stream_poll(stream)<0 || startup.failed || startup.stopped) break;
        thd_sleep(8);
    }
done:
    if(stream!=SND_STREAM_INVALID) snd_stream_destroy(stream);
    if(audio) snd_stream_shutdown();
    free(startup.buffers[0]);free(startup.buffers[1]);memset(&startup,0,sizeof(startup));
}
