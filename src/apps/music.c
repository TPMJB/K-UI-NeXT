/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/music.h"
#include "kui/wav.h"
#include "platform.h"
#include <dc/sound/stream.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MUSIC_STREAM_BYTES 16384u
#define MUSIC_CALLBACK_BYTES (MUSIC_STREAM_BYTES*2u)
static const char *const titles[KUI_MUSIC_TRACKS]={
    "After Hours","Neon Circuit","Orbital Drift","Midnight Vector","Chrome Horizon"};
static const char *const files[KUI_MUSIC_TRACKS]={
    "menu.wav","neon-circuit.wav","orbital-drift.wav","midnight-vector.wav","chrome-horizon.wav"};
static struct {
    struct kui_music_status status;
    kui_log_fn log;
    uint8_t *file;
    struct kui_pcm_loop loop;
    snd_stream_hnd_t stream;
    bool audio_initialized,playback_failed;
    unsigned next_buffer;
} music={.stream=SND_STREAM_INVALID};
/* KOS starts the next callback before waiting for the previous transfer. Two
 * aligned buffers keep a callback from modifying PCM still owned by AICA DMA. */
static _Alignas(32) uint8_t callback_data[2][MUSIC_CALLBACK_BYTES];
static void message(const char *text) {
    snprintf(music.status.message,sizeof(music.status.message),"%s",text);
    if(music.log) music.log("Music: %s",text);
}
const char *kui_music_track_name(unsigned index) {return index<KUI_MUSIC_TRACKS?titles[index]:"Unknown";}
const char *kui_music_track_file(unsigned index) {return index<KUI_MUSIC_TRACKS?files[index]:NULL;}
const struct kui_music_status *kui_music_status(void) {return &music.status;}
static void *stream_data(snd_stream_hnd_t hnd,int requested,int *received) {
    (void)hnd;
    if(!received) return NULL;
    *received=0;
    /* The pinned KOS snd_stream_fill passes bytes (including both channels),
     * although its public callback typedef documents these values as samples. */
    if(requested<=0 || (unsigned)requested>MUSIC_CALLBACK_BYTES || !music.status.loaded) return NULL;
    uint8_t *out=callback_data[music.next_buffer];music.next_buffer^=1u;
    size_t count=kui_pcm_loop_fill(&music.loop,out,(size_t)requested);
    if(!count) return NULL;
    *received=(int)count;return out;
}
void kui_music_pause(void) {
    if(music.stream!=SND_STREAM_INVALID) {
        /* KOS destroy waits for stream DMA before stopping/releasing channels.
         * stop alone would leave the last transfer in flight. */
        snd_stream_destroy(music.stream);music.stream=SND_STREAM_INVALID;
    }
    music.status.playing=false;
    music.status.paused=music.status.loaded && music.status.enabled;
    if(music.status.paused && !music.playback_failed)
        snprintf(music.status.message,sizeof(music.status.message),"Paused during operation");
}
void kui_music_release_audio(void) {
    kui_music_pause();
    if(music.audio_initialized) {snd_stream_shutdown();music.audio_initialized=false;}
}
void kui_music_shutdown(void) {
    kui_music_release_audio();
    free(music.file);music.file=NULL;memset(&music.loop,0,sizeof(music.loop));
    music.playback_failed=false;
    memset(&music.status,0,sizeof(music.status));
    snprintf(music.status.message,sizeof(music.status.message),"Music off");
}
void kui_music_init(kui_log_fn log) {
    kui_music_shutdown();music.log=log;music.next_buffer=0;music.status.volume=30;
}
void kui_music_set_config(bool enabled,unsigned volume_percent) {
    /* This is an explicit settings action, unlike the periodic idle service. */
    music.playback_failed=false;
    music.status.enabled=enabled;music.status.volume=volume_percent>100?100:volume_percent;
    if(!enabled) {kui_music_pause();snprintf(music.status.message,sizeof(music.status.message),"Music off");}
    else if(music.status.playing) snd_stream_volume(music.stream,(int)kui_music_aica_volume(music.status.volume));
}
static bool stopped(kui_cancel_fn cancel) {return cancel && cancel();}
bool kui_music_load(unsigned index,kui_cancel_fn cancel) {
    if(index>=KUI_MUSIC_TRACKS) {message("Unknown track");return false;}
    music.playback_failed=false;
    kui_music_pause();free(music.file);music.file=NULL;memset(&music.loop,0,sizeof(music.loop));
    music.status.loaded=false;music.status.paused=false;music.status.sample_rate=music.status.pcm_bytes=0;
    music.status.current_index=index;
    snprintf(music.status.title,sizeof(music.status.title),"%s",titles[index]);
    if(stopped(cancel)) {message("Load cancelled");return false;}
    if(!kui_sd_connect()) {message("SD card unavailable");return false;}
    FATFS fs;FIL file;bool opened=false,ok=false;
    const char *problem="Cannot mount SD card";
    uint8_t *loaded=NULL;size_t size=0;
    if(!kui_mount(&fs,music.log)) goto out;
    if(stopped(cancel)) {problem="Load cancelled";goto out;}
    char path[96];snprintf(path,sizeof(path),"0:/KUI/apps/music/%s",files[index]);
    FRESULT r=f_open(&file,path,FA_READ);
    if(r!=FR_OK) {problem=r==FR_NO_FILE || r==FR_NO_PATH?"Track file missing; copy the music folder":"Cannot open track";goto out;}
    opened=true;
    FSIZE_t length=f_size(&file);
    if(length<44 || length>KUI_MUSIC_FILE_MAX) {problem="Track must be a WAV no larger than 2 MiB";goto out;}
    size=(size_t)length;loaded=malloc(size);
    if(!loaded) {problem="Not enough RAM for this track";goto out;}
    for(size_t at=0;at<size;) {
        if(stopped(cancel)) {problem="Load cancelled";goto out;}
        UINT requested=(UINT)(size-at);if(requested>32768u) requested=32768u;
        UINT got=0;r=f_read(&file,loaded+at,requested,&got);
        if(r!=FR_OK || got!=requested) {problem="Track read failed";goto out;}
        at+=got;
    }
    if(stopped(cancel)) {problem="Load cancelled";goto out;}
    ok=true;
out:
    if(opened && f_close(&file)!=FR_OK) {ok=false;problem="Cannot close track";}
    if(f_mount(NULL,"0:",0)!=FR_OK) {ok=false;problem="Cannot release SD filesystem";}
    kui_sd_disconnect();
    struct kui_wav wav;
    if(ok && !kui_wav_parse(loaded,size,&wav)) {ok=false;problem="Unsupported WAV: use mono/stereo PCM16, 8-44.1 kHz";}
    if(!ok) {free(loaded);message(problem);return false;}
    if(!kui_pcm_loop_init(&music.loop,loaded+wav.offset,wav.bytes,wav.frame_bytes)) {
        free(loaded);message("Invalid PCM loop");return false;
    }
    music.file=loaded;music.status.loaded=true;music.status.paused=music.status.enabled;
    music.status.sample_rate=wav.rate;music.status.pcm_bytes=(uint32_t)wav.bytes;
    message("Track cached in RAM");return true;
}
void kui_music_resume(void) {
    if(!music.status.enabled || !music.status.loaded || music.status.playing || music.playback_failed) return;
    if(!music.audio_initialized) {
        if(snd_stream_init_ex(2,MUSIC_STREAM_BYTES)<0) {
            /* Pinned KOS may allocate its separation buffer before failing. */
            snd_stream_shutdown();music.playback_failed=true;message("Audio initialization failed; change Music settings to retry");return;
        }
        music.audio_initialized=true;
    }
    music.stream=snd_stream_alloc(stream_data,MUSIC_STREAM_BYTES);
    if(music.stream==SND_STREAM_INVALID) {
        music.playback_failed=true;message("Audio stream unavailable; change Music settings to retry");return;
    }
    music.next_buffer=0;
    /* start installs volume 255 internally. Queue start and the intended level
     * together, so enabling a quiet track does not briefly start at full volume. */
    snd_stream_queue_enable(music.stream);
    snd_stream_start(music.stream,music.status.sample_rate,music.loop.frame_bytes==4);
    snd_stream_volume(music.stream,(int)kui_music_aica_volume(music.status.volume));
    snd_stream_queue_go(music.stream);snd_stream_queue_disable(music.stream);
    music.status.playing=true;music.status.paused=false;
    snprintf(music.status.message,sizeof(music.status.message),"Playing from RAM");
}
void kui_music_service(void) {
    if(music.status.playing && snd_stream_poll(music.stream)<0) {
        kui_music_pause();music.playback_failed=true;
        message("Audio stopped after a playback error; change Music settings to retry");
    }
}
