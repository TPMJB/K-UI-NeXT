/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/music.h"
#include "kui/music_ogg.h"
#include <dc/sound/stream.h>
#include <kos/thread.h>
#include <kos/timer.h>
#include <stdlib.h>
#include <string.h>
#include "../../build/startup_ogg.inc"
#define STARTUP_RING_BYTES 16384u
#define STARTUP_CALLBACK_BYTES 32768u
#define STARTUP_BUDGET_MS 2700u
#define STARTUP_PCM_BYTES ((size_t)KUI_STARTUP_COUNT*2u)
/* The runtime keeps only the compressed cue. Its decoder arena exists while
 * the cue plays, before any menu song is cached, and is then released. */
static struct {
    uint8_t *buffers[2];
    void *arena;
    struct kui_ogg ogg;
    size_t position;
    unsigned next;
    kui_cancel_fn cancel;
    uint64_t deadline;
    bool failed,stopped;
} startup;
static bool startup_cancelled(void) {
    return timer_ms_gettime64()>=startup.deadline || (startup.cancel && startup.cancel());
}
static void *startup_data(snd_stream_hnd_t stream,int requested,int *received) {
    (void)stream;
    if(!received) return NULL;
    *received=0;
    if(startup_cancelled()) {startup.stopped=true;return NULL;}
    if(requested<=0 || (unsigned)requested>STARTUP_CALLBACK_BYTES || (requested&1) ||
       !startup.buffers[0] || !startup.buffers[1]) {startup.failed=true;return NULL;}
    uint8_t *out=startup.buffers[startup.next];startup.next^=1u;
    /* Decode no further than the cue's last sample, so it never rewinds,
     * then pad with silence as before. */
    size_t count=STARTUP_PCM_BYTES-startup.position;
    if(count>(size_t)requested) count=(size_t)requested;
    if(count && kui_ogg_fill(&startup.ogg,out,count)!=count) {startup.failed=true;return NULL;}
    memset(out+count,0,(size_t)requested-count);startup.position+=count;
    *received=requested;return out;
}
void kui_music_play_boot_chime(kui_cancel_fn cancel) {
    uint64_t deadline=timer_ms_gettime64()+STARTUP_BUDGET_MS;
    if(cancel && cancel()) return;
    kui_music_release_audio();memset(&startup,0,sizeof(startup));startup.cancel=cancel;
    startup.deadline=deadline;
    startup.buffers[0]=aligned_alloc(32,STARTUP_CALLBACK_BYTES);
    startup.buffers[1]=aligned_alloc(32,STARTUP_CALLBACK_BYTES);
    startup.arena=aligned_alloc(16,KUI_OGG_WORKSPACE_BYTES);
    snd_stream_hnd_t stream=SND_STREAM_INVALID;bool audio=false;
    if(!startup.buffers[0] || !startup.buffers[1] || !startup.arena) goto done;
    if(!kui_ogg_open(&startup.ogg,kui_startup_ogg,sizeof(kui_startup_ogg),startup.arena,
       KUI_OGG_WORKSPACE_BYTES,startup_cancelled) || startup.ogg.channels!=1u ||
       startup.ogg.rate!=KUI_STARTUP_RATE || startup.ogg.frames!=KUI_STARTUP_COUNT) goto done;
    if(snd_stream_init_ex(1,STARTUP_RING_BYTES)<0) {snd_stream_shutdown();goto done;}
    audio=true;stream=snd_stream_alloc(startup_data,STARTUP_RING_BYTES);
    if(stream==SND_STREAM_INVALID) goto done;
    snd_stream_queue_enable(stream);
    snd_stream_start(stream,KUI_STARTUP_RATE,0);snd_stream_volume(stream,128);
    if(startup.failed || startup.stopped || startup_cancelled()) {
        /* start queued a global AICA pause. Release that queue even on Skip,
         * at zero volume, before destroy submits the stop command. */
        snd_stream_volume(stream,0);snd_stream_queue_go(stream);snd_stream_queue_disable(stream);goto done;
    }
    snd_stream_queue_go(stream);snd_stream_queue_disable(stream);
    uint64_t end=timer_ms_gettime64()+((uint64_t)KUI_STARTUP_COUNT*1000u+KUI_STARTUP_RATE-1u)/KUI_STARTUP_RATE+32u;
    if(end>deadline) end=deadline;
    while(timer_ms_gettime64()<end && !startup_cancelled()) {
        if(snd_stream_poll(stream)<0 || startup.failed || startup.stopped) break;
        uint64_t now=timer_ms_gettime64();
        if(now<end) thd_sleep(end-now<8u?(unsigned)(end-now):8u);
    }
done:
    if(stream!=SND_STREAM_INVALID) snd_stream_destroy(stream);
    if(audio) snd_stream_shutdown();
    /* Destroy has drained DMA and callbacks; only then release the decoder. */
    kui_ogg_close(&startup.ogg);
    free(startup.buffers[0]);free(startup.buffers[1]);free(startup.arena);memset(&startup,0,sizeof(startup));
}
