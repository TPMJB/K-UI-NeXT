/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/music.h"
#include "kui/wav.h"
#include "platform.h"
#include <dc/sound/stream.h>
#ifdef KUI_ON_CONSOLE
#include <kos/mutex.h>
#include <kos/thread.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* At 44.1 kHz stereo this gives about 740 ms of queued audio. The callback
 * copies RAM only, leaving the single SD/optical worker free to run captures. */
#define MUSIC_STREAM_BYTES 65536u
#define MUSIC_CALLBACK_BYTES (MUSIC_STREAM_BYTES*2u)
#define MUSIC_SLOTS (KUI_MUSIC_TRACKS+1u)
static const char *const titles[KUI_MUSIC_TRACKS]={
    "After Hours","Neon Circuit","Orbital Drift","Midnight Vector","Chrome Horizon"};
static const char *const files[KUI_MUSIC_TRACKS]={
    "menu.wav","neon-circuit.wav","orbital-drift.wav","midnight-vector.wav","chrome-horizon.wav"};
struct cached_track {uint8_t *file;size_t size;struct kui_wav wav;char title[40];};
static struct {
    struct kui_music_status status;
    kui_log_fn log;
    struct cached_track tracks[MUSIC_SLOTS];
    struct kui_pcm_loop loop;
    snd_stream_hnd_t stream;
    bool audio_initialized,playback_failed,thread_stop;
    unsigned next_buffer;
#ifdef KUI_ON_CONSOLE
    kthread_t *thread;
#endif
} music={.stream=SND_STREAM_INVALID};
#ifdef KUI_ON_CONSOLE
static mutex_t audio_lock=MUTEX_INITIALIZER;
static void lock_audio(void) {mutex_lock(&audio_lock);}
static void unlock_audio(void) {mutex_unlock(&audio_lock);}
#else
static void lock_audio(void) {}
static void unlock_audio(void) {}
#endif
/* KOS requests the next callback before draining the preceding transfer. */
static _Alignas(32) uint8_t callback_data[2][MUSIC_CALLBACK_BYTES];
static void notice_locked(const char *text) {
    snprintf(music.status.message,sizeof(music.status.message),"%s",text);
}
static void report(const char *text) {
    lock_audio();notice_locked(text);kui_log_fn log=music.log;unlock_audio();
    if(log) log("Music: %s",text);
}
const char *kui_music_track_name(unsigned index) {return index<KUI_MUSIC_TRACKS?titles[index]:"Selected WAV";}
const char *kui_music_track_file(unsigned index) {return index<KUI_MUSIC_TRACKS?files[index]:NULL;}
void kui_music_status_copy(struct kui_music_status *out) {
    if(!out) return;
    lock_audio();*out=music.status;unlock_audio();
}
const struct kui_music_status *kui_music_status(void) {
    static struct kui_music_status snapshot;kui_music_status_copy(&snapshot);return &snapshot;
}
static void *stream_data(snd_stream_hnd_t hnd,int requested,int *received) {
    (void)hnd;
    if(!received) return NULL;
    *received=0;
    /* Called synchronously by KOS start/poll while audio_lock is already held.
     * Its arguments are bytes, including both channels, in the pinned KOS. */
    if(requested<=0 || (unsigned)requested>MUSIC_CALLBACK_BYTES || !music.status.loaded) return NULL;
    uint8_t *out=callback_data[music.next_buffer];music.next_buffer^=1u;
    size_t count=kui_pcm_loop_fill(&music.loop,out,(size_t)requested);
    if(!count) return NULL;
    *received=(int)count;return out;
}
static void pause_locked(void) {
    if(music.stream!=SND_STREAM_INVALID) {
        snd_stream_destroy(music.stream);music.stream=SND_STREAM_INVALID;
    }
    music.status.playing=false;
    music.status.paused=music.status.loaded && music.status.enabled;
}
void kui_music_pause(void) {
    lock_audio();pause_locked();
    if(music.status.paused && !music.playback_failed) notice_locked("Music paused");
    unlock_audio();
}
void kui_music_release_audio(void) {
    lock_audio();pause_locked();
    if(music.audio_initialized) {snd_stream_shutdown();music.audio_initialized=false;}
    unlock_audio();
}
static void resume_locked(void) {
#ifdef KUI_ON_CONSOLE
    if(!music.thread) {notice_locked("Audio service unavailable; restart K-UI");return;}
#endif
    if(!music.status.enabled || !music.status.loaded || music.status.playing || music.playback_failed) return;
    if(!music.audio_initialized) {
        if(snd_stream_init_ex(2,MUSIC_STREAM_BYTES)<0) {
            snd_stream_shutdown();music.playback_failed=true;notice_locked("Audio initialization failed; choose Music again");return;
        }
        music.audio_initialized=true;
    }
    music.stream=snd_stream_alloc(stream_data,MUSIC_STREAM_BYTES);
    if(music.stream==SND_STREAM_INVALID) {
        music.playback_failed=true;notice_locked("Audio stream unavailable; choose Music again");return;
    }
    music.next_buffer=0;
    snd_stream_queue_enable(music.stream);
    snd_stream_start(music.stream,music.status.sample_rate,music.loop.frame_bytes==4);
    snd_stream_volume(music.stream,(int)kui_music_aica_volume(music.status.volume));
    snd_stream_queue_go(music.stream);snd_stream_queue_disable(music.stream);
    music.status.playing=true;music.status.paused=false;notice_locked("Playing from RAM");
}
void kui_music_resume(void) {lock_audio();resume_locked();unlock_audio();}
void kui_music_service(void) {
    lock_audio();
    if(music.status.playing && snd_stream_poll(music.stream)<0) {
        pause_locked();music.playback_failed=true;notice_locked("Audio stopped after a playback error; choose Music again");
    }
    unlock_audio();
}
#ifdef KUI_ON_CONSOLE
static void *audio_worker(void *unused) {
    (void)unused;
    for(;;) {
        lock_audio();bool stop=music.thread_stop;unlock_audio();
        if(stop) break;
        kui_music_service();thd_sleep(8);
    }
    return NULL;
}
#endif
void kui_music_shutdown(void) {
#ifdef KUI_ON_CONSOLE
    lock_audio();music.thread_stop=true;kthread_t *thread=music.thread;unlock_audio();
    if(thread) thd_join(thread,NULL);
#endif
    kui_music_release_audio();lock_audio();
    for(unsigned i=0;i<MUSIC_SLOTS;i++) {free(music.tracks[i].file);memset(&music.tracks[i],0,sizeof(music.tracks[i]));}
    memset(&music.loop,0,sizeof(music.loop));music.playback_failed=false;
    memset(&music.status,0,sizeof(music.status));notice_locked("Music off");
#ifdef KUI_ON_CONSOLE
    music.thread=NULL;
#endif
    unlock_audio();
}
void kui_music_init(kui_log_fn log) {
    kui_music_shutdown();lock_audio();music.log=log;music.next_buffer=0;
    music.status.volume=30;music.thread_stop=false;unlock_audio();
#ifdef KUI_ON_CONSOLE
    kthread_attr_t attrs={.stack_size=32u*1024u,.label="kui-audio"};
    music.thread=thd_create_ex(&attrs,audio_worker,NULL);
    if(!music.thread) {lock_audio();music.playback_failed=true;notice_locked("Audio service unavailable");unlock_audio();}
#endif
}
void kui_music_set_config(bool enabled,unsigned volume_percent) {
    lock_audio();music.playback_failed=false;
    music.status.enabled=enabled;music.status.volume=volume_percent>100?100:volume_percent;
    if(!enabled) {pause_locked();notice_locked("Music off");}
    else if(music.status.playing) snd_stream_volume(music.stream,(int)kui_music_aica_volume(music.status.volume));
    unlock_audio();
}
static void cache_status_locked(void) {
    music.status.cached_mask=0;music.status.cache_bytes=0;
    for(unsigned i=0;i<MUSIC_SLOTS;i++) if(music.tracks[i].file) {
        music.status.cached_mask|=1u<<i;music.status.cache_bytes+=(uint32_t)music.tracks[i].size;
    }
}
static bool reserve_locked(size_t size) {
    cache_status_locked();
    /* Never discard the playing/selected song before a successful replacement.
     * Unselected slots can be reclaimed, including a previous custom song. */
    for(unsigned i=0;i<MUSIC_SLOTS && size>KUI_MUSIC_CACHE_MAX-music.status.cache_bytes;i++) {
        if(music.status.loaded && music.status.current_index==i) continue;
        free(music.tracks[i].file);memset(&music.tracks[i],0,sizeof(music.tracks[i]));cache_status_locked();
    }
    return size<=KUI_MUSIC_CACHE_MAX-music.status.cache_bytes;
}
static bool stopped(kui_cancel_fn cancel) {return cancel && cancel();}
static bool cache_path(unsigned index,const char *path,const char *title,size_t limit,kui_cancel_fn cancel) {
    if(stopped(cancel)) {report("Load cancelled; previous song retained");return false;}
    if(!kui_sd_connect()) {report("SD card unavailable; previous song retained");return false;}
    FATFS fs;FIL file;bool opened=false,ok=false;
    const char *problem="Cannot mount SD card; previous song retained";
    uint8_t *loaded=NULL;size_t size=0;struct kui_wav wav;
    if(!kui_mount(&fs,music.log)) goto out;
    if(stopped(cancel)) {problem="Load cancelled; previous song retained";goto out;}
    FRESULT r=f_open(&file,path,FA_READ);
    if(r!=FR_OK) {problem=r==FR_NO_FILE || r==FR_NO_PATH?"Track file missing; previous song retained":"Cannot open track";goto out;}
    opened=true;
    FSIZE_t length=f_size(&file);
    if(length<44 || length>limit) {problem=index==KUI_MUSIC_CUSTOM_INDEX?"WAV is larger than the 6 MiB background limit":"Bundled WAV is larger than 2 MiB";goto out;}
    size=(size_t)length;
    lock_audio();bool room=reserve_locked(size);unlock_audio();
    if(!room) {problem="Not enough cache space; choose a smaller song first";goto out;}
    loaded=malloc(size);
    if(!loaded) {problem="Not enough RAM; previous song retained";goto out;}
    /* No audio mutex here: the independent service continues the previous PCM
     * throughout SD reads. The temporary allocation was included in reserve. */
    for(size_t at=0;at<size;) {
        if(stopped(cancel)) {problem="Load cancelled; previous song retained";goto out;}
        UINT requested=(UINT)(size-at);if(requested>32768u) requested=32768u;
        UINT got=0;r=f_read(&file,loaded+at,requested,&got);
        if(r!=FR_OK || got!=requested) {problem="Track read failed; previous song retained";goto out;}
        at+=got;
    }
    if(stopped(cancel)) {problem="Load cancelled; previous song retained";goto out;}
    if(!kui_wav_parse(loaded,size,&wav)) {problem="Unsupported WAV: use mono/stereo PCM16, 8-44.1 kHz";goto out;}
    ok=true;
out:
    if(opened && f_close(&file)!=FR_OK) {ok=false;problem="Cannot close track; previous song retained";}
    if(f_mount(NULL,"0:",0)!=FR_OK) {ok=false;problem="Cannot release SD filesystem; previous song retained";}
    kui_sd_disconnect();
    if(!ok) {free(loaded);report(problem);return false;}
    lock_audio();
    /* Only a selected custom slot can be replaced; drain it before swapping its
     * allocation, after the replacement has been fully validated and closed. */
    bool selected=music.status.loaded && music.status.current_index==index;
    if(selected) pause_locked();
    free(music.tracks[index].file);
    music.tracks[index]=(struct cached_track){.file=loaded,.size=size,.wav=wav};
    snprintf(music.tracks[index].title,sizeof(music.tracks[index].title),"%s",title);
    cache_status_locked();unlock_audio();return true;
}
bool kui_music_cache_menu(unsigned index,kui_cancel_fn cancel) {
    if(index>=KUI_MUSIC_TRACKS) {report("Unknown track");return false;}
    lock_audio();bool cached=music.tracks[index].file!=NULL;unlock_audio();
    if(cached) return true;
    char path[96];snprintf(path,sizeof(path),"0:/KUI/apps/music/%s",files[index]);
    return cache_path(index,path,titles[index],KUI_MUSIC_FILE_MAX,cancel);
}
static bool select_locked(unsigned index) {
    if(index>=MUSIC_SLOTS || !music.tracks[index].file) return false;
    music.playback_failed=false;
    if(music.status.loaded && music.status.current_index==index && music.loop.data==music.tracks[index].file+music.tracks[index].wav.offset) {
        resume_locked();return true;
    }
    pause_locked();struct cached_track *track=&music.tracks[index];
    (void)kui_pcm_loop_init(&music.loop,track->file+track->wav.offset,track->wav.bytes,track->wav.frame_bytes);
    music.playback_failed=false;music.status.loaded=true;music.status.current_index=index;
    music.status.sample_rate=track->wav.rate;music.status.pcm_bytes=(uint32_t)track->wav.bytes;
    snprintf(music.status.title,sizeof(music.status.title),"%s",track->title);
    music.status.paused=music.status.enabled;notice_locked("Track cached in RAM");resume_locked();return true;
}
bool kui_music_select_cached(unsigned index) {
    lock_audio();bool ok=select_locked(index);unlock_audio();return ok;
}
bool kui_music_step_cached(int direction,unsigned *wanted) {
    lock_audio();unsigned current=music.status.current_index;
    unsigned index=current<KUI_MUSIC_TRACKS?(current+KUI_MUSIC_TRACKS+(direction<0?-1:1))%KUI_MUSIC_TRACKS:
        (direction<0?KUI_MUSIC_TRACKS-1u:0u);
    if(wanted) *wanted=index;
    bool ok=select_locked(index);unlock_audio();return ok;
}
bool kui_music_load(unsigned index,kui_cancel_fn cancel) {
    return kui_music_cache_menu(index,cancel) && kui_music_select_cached(index);
}
bool kui_music_load_path(const char *path,const char *title,kui_cancel_fn cancel) {
    if(!path || path[0]!='/' || strlen(path)>127u || !title) {report("Invalid WAV path");return false;}
    char qualified[131];snprintf(qualified,sizeof(qualified),"0:%s",path);
    return cache_path(KUI_MUSIC_CUSTOM_INDEX,qualified,title,KUI_MUSIC_CUSTOM_MAX,cancel) &&
        kui_music_select_cached(KUI_MUSIC_CUSTOM_INDEX);
}
