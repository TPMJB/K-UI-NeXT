/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/music.h"
#include "kui/wav.h"
#include "kui/music_ogg.h"
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
/* One decoder arena, plus 16-byte alignment slack, serves whichever cached
 * Ogg is selected. Unselected Oggs retain only their compressed bytes. */
#define MUSIC_ARENA_BYTES (KUI_OGG_WORKSPACE_BYTES+15u)
static const char *const titles[KUI_MUSIC_TRACKS]={
    "After Hours","Neon Circuit","Orbital Drift","Midnight Vector","Chrome Horizon","Harbor Lights"};
/* Bundled songs ship as Ogg Vorbis. Cards set up for 1.5 hold only the
 * original PCM WAVs, which still load whenever the Ogg is absent. Harbor
 * Lights joined the rotation later, so it has no WAV fallback. */
static const char *const files[KUI_MUSIC_TRACKS]={
    "menu.ogg","neon-circuit.ogg","orbital-drift.ogg","midnight-vector.ogg","chrome-horizon.ogg",
    "harbor-lights.ogg"};
static const char *const legacy_files[KUI_MUSIC_TRACKS]={
    "menu.wav","neon-circuit.wav","orbital-drift.wav","midnight-vector.wav","chrome-horizon.wav",NULL};
struct cached_track {uint8_t *file;size_t size;struct kui_wav wav;bool compressed;char title[40];};
static struct {
    struct kui_music_status status;
    kui_log_fn log;
    struct cached_track tracks[MUSIC_SLOTS];
    struct kui_pcm_loop loop;
    /* The selected Ogg's decoder, inside arena. The arena is retained while
     * any cached song is an Ogg, so reselection never allocates. */
    struct kui_ogg ogg;
    uint8_t *arena;
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
const char *kui_music_track_name(unsigned index) {return index<KUI_MUSIC_TRACKS?titles[index]:"Selected song";}
const char *kui_music_track_file(unsigned index) {return index<KUI_MUSIC_TRACKS?files[index]:NULL;}
void kui_music_status_copy(struct kui_music_status *out) {
    if(!out) return;
    lock_audio();*out=music.status;unlock_audio();
}
void kui_music_log_stats(const char *reason) {
    struct kui_music_status s;
    lock_audio();s=music.status;kui_log_fn log=music.log;unlock_audio();
    if(!log) return;
    log("Music RAM [%s]: cached=%lu (decoder %lu) loading=%lu peak files=%lu budget=%u bytes",
        reason?reason:"snapshot",(unsigned long)s.cache_bytes,(unsigned long)s.decoder_bytes,
        (unsigned long)s.loading_bytes,(unsigned long)s.peak_file_bytes,KUI_MUSIC_CACHE_MAX);
    log("Music file allocations=%lu frees=%lu cached mask=0x%x missing mask=0x%x; Stop/mute retains cache",
        (unsigned long)s.file_allocations,(unsigned long)s.file_frees,s.cached_mask,s.missing_mask);
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
    size_t count=music.ogg.decoder?kui_ogg_fill(&music.ogg,out,(size_t)requested):
        kui_pcm_loop_fill(&music.loop,out,(size_t)requested);
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
    kui_music_release_audio();lock_audio();kui_ogg_close(&music.ogg);
    for(unsigned i=0;i<MUSIC_SLOTS;i++) {free(music.tracks[i].file);memset(&music.tracks[i],0,sizeof(music.tracks[i]));}
    free(music.arena);music.arena=NULL;
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
    music.status.cached_mask=0;music.status.decoder_bytes=music.arena?MUSIC_ARENA_BYTES:0;
    music.status.cache_bytes=music.status.decoder_bytes;
    for(unsigned i=0;i<MUSIC_SLOTS;i++) if(music.tracks[i].file) {
        music.status.cached_mask|=1u<<i;music.status.cache_bytes+=(uint32_t)music.tracks[i].size;
    }
}
static void release_file_locked(uint8_t *file) {
    if(file) {free(file);++music.status.file_frees;}
}
static void allocated_locked(size_t bytes) {
    ++music.status.file_allocations;music.status.loading_bytes+=(uint32_t)bytes;
    uint32_t total=music.status.cache_bytes+music.status.loading_bytes;
    if(total>music.status.peak_file_bytes) music.status.peak_file_bytes=total;
}
static void *arena_workspace(uint8_t *arena) {
    return (void *)(((uintptr_t)arena+15u)&~(uintptr_t)15u);
}
/* Only the selected song can own the open decoder; the arena itself stays
 * while any cached song is an Ogg that may be selected later. */
static void trim_arena_locked(void) {
    if(!music.arena || music.ogg.decoder) return;
    for(unsigned i=0;i<MUSIC_SLOTS;i++) if(music.tracks[i].file && music.tracks[i].compressed) return;
    release_file_locked(music.arena);music.arena=NULL;
}
static void unload_locked(void) {
    memset(&music.loop,0,sizeof(music.loop));
    music.status.loaded=false;music.status.paused=false;music.status.compressed=false;
    music.status.current_index=0;music.status.sample_rate=0;music.status.pcm_bytes=0;music.status.title[0]=0;
}
static bool reserve_locked(size_t size) {
    cache_status_locked();
    /* Never discard the playing/selected song before a successful replacement.
     * Unselected slots can be reclaimed, including a previous custom song. */
    for(unsigned i=0;i<MUSIC_SLOTS && size>KUI_MUSIC_CACHE_MAX-music.status.cache_bytes;i++) {
        if(music.status.loaded && music.status.current_index==i) continue;
        release_file_locked(music.tracks[i].file);memset(&music.tracks[i],0,sizeof(music.tracks[i]));
        trim_arena_locked();cache_status_locked();
    }
    return size<=KUI_MUSIC_CACHE_MAX-music.status.cache_bytes;
}
static bool stopped(kui_cancel_fn cancel) {return cancel && cancel();}
static bool select_locked(unsigned index);
static bool is_ogg_path(const char *path) {
    size_t n=strlen(path);return n>=4u && path[n-4]=='.' &&
        (path[n-3]=='o'||path[n-3]=='O') && (path[n-2]=='g'||path[n-2]=='G') &&
        (path[n-1]=='g'||path[n-1]=='G');
}
/* legacy, if any, is tried only when path itself is missing. */
static bool cache_path(unsigned index,const char *path,const char *legacy,const char *title,
    size_t limit,kui_cancel_fn cancel) {
    if(stopped(cancel)) {report("Load cancelled; previous song retained");return false;}
    if(!kui_sd_connect()) {report("SD card unavailable; previous song retained");return false;}
    FATFS fs;FIL file;bool opened=false,ok=false;
    const char *problem="Cannot mount SD card; previous song retained";
    uint8_t *loaded=NULL,*staging=NULL;size_t file_size=0;struct kui_wav wav={0};
    bool compressed=is_ogg_path(path),missing=false;
    if(!kui_mount(&fs,music.log)) goto out;
    if(stopped(cancel)) {problem="Load cancelled; previous song retained";goto out;}
    FRESULT r=f_open(&file,path,FA_READ);
    if(r==FR_NO_FILE && legacy) {r=f_open(&file,legacy,FA_READ);compressed=is_ogg_path(legacy);}
    if(r!=FR_OK) {missing=r==FR_NO_FILE || r==FR_NO_PATH;problem=missing?"Track file missing; previous song retained":"Cannot open track";goto out;}
    opened=true;
    FSIZE_t length=f_size(&file);
    if(length<44 || length>limit) {problem=index==KUI_MUSIC_CUSTOM_INDEX?"Song file must be 44 bytes to 6 MiB":"Bundled song is larger than 2 MiB";goto out;}
    file_size=(size_t)length;
    /* An Ogg is fully validated in its own staging arena, so the song playing
     * from the shared arena continues. The budget includes both. */
    size_t staging_size=compressed?MUSIC_ARENA_BYTES:0;
    lock_audio();bool room=reserve_locked(file_size+staging_size);
    if(room && (loaded=malloc(file_size))) {
        allocated_locked(file_size);
        if(staging_size && (staging=malloc(staging_size))) allocated_locked(staging_size);
    }
    unlock_audio();
    if(!room) {problem="Not enough cache space; choose a smaller song first";goto out;}
    if(!loaded || (staging_size && !staging)) {problem="Not enough RAM; previous song retained";goto out;}
    /* No audio mutex here: the independent service continues the previous PCM
     * throughout SD reads. The temporary allocation was included in reserve. */
    for(size_t at=0;at<file_size;) {
        if(stopped(cancel)) {problem="Load cancelled; previous song retained";goto out;}
        UINT requested=(UINT)(file_size-at);if(requested>32768u) requested=32768u;
        UINT got=0;r=f_read(&file,loaded+at,requested,&got);
        if(r!=FR_OK || got!=requested) {problem="Track read failed; previous song retained";goto out;}
        at+=got;
    }
    if(stopped(cancel)) {problem="Load cancelled; previous song retained";goto out;}
    if(compressed) {
        struct kui_ogg ogg;
        if(!kui_ogg_open(&ogg,loaded,file_size,arena_workspace(staging),KUI_OGG_WORKSPACE_BYTES,cancel)) {
            problem=stopped(cancel)?"Load cancelled; previous song retained":
                "Invalid/unsupported Ogg: mono/stereo Vorbis, 8-44.1 kHz, bounded decoder";goto out;
        }
        wav=(struct kui_wav){.bytes=(size_t)ogg.frames*ogg.channels*2u,.rate=ogg.rate,
            .channels=ogg.channels,.frame_bytes=ogg.channels*2u};
        kui_ogg_close(&ogg);
    } else if(!kui_wav_parse(loaded,file_size,&wav)) {
        problem="Unsupported WAV: use mono/stereo PCM16, 8-44.1 kHz";goto out;
    }
    ok=true;
out:
    if(opened && f_close(&file)!=FR_OK) {ok=false;problem="Cannot close track; previous song retained";}
    if(f_mount(NULL,"0:",0)!=FR_OK) {ok=false;problem="Cannot release SD filesystem; previous song retained";}
    kui_sd_disconnect();
    if(!ok) {
        lock_audio();release_file_locked(loaded);release_file_locked(staging);music.status.loading_bytes=0;
        if(missing && index<KUI_MUSIC_TRACKS) music.status.missing_mask|=1u<<index;
        unlock_audio();report(problem);return false;
    }
    lock_audio();
    /* Only a selected custom slot can be replaced; drain it before swapping its
     * allocation, after the replacement has been fully validated and closed. */
    bool selected=music.status.loaded && music.status.current_index==index;
    if(selected) {pause_locked();kui_ogg_close(&music.ogg);}
    release_file_locked(music.tracks[index].file);
    music.tracks[index]=(struct cached_track){.file=loaded,.size=file_size,.wav=wav,.compressed=compressed};
    snprintf(music.tracks[index].title,sizeof(music.tracks[index].title),"%s",title);
    /* The first cached Ogg keeps its staging arena as the shared decoder arena. */
    if(compressed && !music.arena) {music.arena=staging;staging=NULL;}
    release_file_locked(staging);music.status.loading_bytes=0;music.status.missing_mask&=~(1u<<index);
    trim_arena_locked();cache_status_locked();
    /* Custom loads also select their new PCM in this same transaction. Leaving
     * the old loop visible after freeing its selected file would let a Resume
     * on another thread dereference that freed memory at the unlock boundary. */
    if(selected || index==KUI_MUSIC_CUSTOM_INDEX) (void)select_locked(index);
    unlock_audio();return true;
}
bool kui_music_cache_menu(unsigned index,kui_cancel_fn cancel) {
    if(index>=KUI_MUSIC_TRACKS) {report("Unknown track");return false;}
    lock_audio();bool cached=music.tracks[index].file!=NULL;unlock_audio();
    if(cached) return true;
    char path[96],legacy[96]="";
    snprintf(path,sizeof(path),"0:/KUI/apps/music/%s",files[index]);
    if(legacy_files[index]) snprintf(legacy,sizeof(legacy),"0:/KUI/apps/music/%s",legacy_files[index]);
    return cache_path(index,path,legacy_files[index]?legacy:NULL,titles[index],KUI_MUSIC_FILE_MAX,cancel);
}
static bool select_locked(unsigned index) {
    if(index>=MUSIC_SLOTS || !music.tracks[index].file) return false;
    music.playback_failed=false;
    if(music.status.loaded && music.status.current_index==index && music.loop.data==music.tracks[index].file+music.tracks[index].wav.offset) {
        resume_locked();return true;
    }
    pause_locked();kui_ogg_close(&music.ogg);struct cached_track *track=&music.tracks[index];
    /* The file passed the full check when cached. Reopening only rebuilds the
     * codec tables in the shared arena; it neither allocates nor reads SD. */
    if(track->compressed && (!music.arena || !kui_ogg_reopen(&music.ogg,track->file,track->size,
       arena_workspace(music.arena),KUI_OGG_WORKSPACE_BYTES))) {
        unload_locked();music.playback_failed=true;notice_locked("Cached Ogg did not reopen; choose another song");
        return false;
    }
    (void)kui_pcm_loop_init(&music.loop,track->file+track->wav.offset,track->wav.bytes,track->wav.frame_bytes);
    music.playback_failed=false;music.status.loaded=true;music.status.current_index=index;
    music.status.sample_rate=track->wav.rate;music.status.pcm_bytes=(uint32_t)track->wav.bytes;
    music.status.compressed=track->compressed;
    snprintf(music.status.title,sizeof(music.status.title),"%s",track->title);
    music.status.paused=music.status.enabled;notice_locked("Track cached in RAM");resume_locked();return true;
}
bool kui_music_select_cached(unsigned index) {
    lock_audio();bool ok=select_locked(index);unlock_audio();return ok;
}
/* Bundled songs this card lacks drop out of the cycle. With nothing else
 * eligible, the current index is kept. */
static unsigned next_index_locked(unsigned current,int direction) {
    unsigned count=music.tracks[KUI_MUSIC_CUSTOM_INDEX].file?MUSIC_SLOTS:KUI_MUSIC_TRACKS;
    if(current>=count) current=direction<0?0:count-1u;
    for(unsigned step=1,index=current;step<count;step++) {
        index=direction<0?(index+count-1u)%count:(index+1u)%count;
        if(!(music.status.missing_mask>>index&1u)) return index;
    }
    return current;
}
unsigned kui_music_next_index(unsigned current,int direction) {
    lock_audio();unsigned index=next_index_locked(current,direction);unlock_audio();return index;
}
bool kui_music_step_cached(int direction,unsigned *wanted) {
    lock_audio();unsigned index=next_index_locked(music.status.current_index,direction);
    if(wanted) *wanted=index;
    bool ok=select_locked(index);unlock_audio();return ok;
}
void kui_music_clear_cache(void) {
    lock_audio();pause_locked();kui_ogg_close(&music.ogg);
    for(unsigned i=0;i<MUSIC_SLOTS;i++) {
        release_file_locked(music.tracks[i].file);memset(&music.tracks[i],0,sizeof(music.tracks[i]));
    }
    release_file_locked(music.arena);music.arena=NULL;
    /* The card may have changed; look for missing songs again. */
    unload_locked();cache_status_locked();music.playback_failed=false;music.status.missing_mask=0;
    notice_locked("Music stopped; song cache released");
    unlock_audio();
}
bool kui_music_load(unsigned index,kui_cancel_fn cancel) {
    return kui_music_cache_menu(index,cancel) && kui_music_select_cached(index);
}
static bool same_card_path(const char *a,const char *b) {
    /* The caller normalizes separators/components; FatFs names are insensitive
     * to ASCII case. The bundled Ogg and legacy WAV paths are ASCII only. */
    while(*a && *b) {
        unsigned char x=(unsigned char)*a++,y=(unsigned char)*b++;
        if(x>='A' && x<='Z') x=(unsigned char)(x+('a'-'A'));
        if(y>='A' && y<='Z') y=(unsigned char)(y+('a'-'A'));
        if(x!=y) return false;
    }
    return !*a && !*b;
}
bool kui_music_load_path(const char *path,const char *title,kui_cancel_fn cancel) {
    if(!path || path[0]!='/' || strlen(path)>127u || !title) {report("Invalid song path");return false;}
    if(stopped(cancel)) {report("Load cancelled; previous song retained");return false;}
    for(unsigned i=0;i<KUI_MUSIC_TRACKS;i++) {
        char bundled[96],legacy[96]="";
        snprintf(bundled,sizeof(bundled),"/KUI/apps/music/%s",files[i]);
        if(legacy_files[i]) snprintf(legacy,sizeof(legacy),"/KUI/apps/music/%s",legacy_files[i]);
        if(same_card_path(path,bundled) || (legacy[0] && same_card_path(path,legacy))) return kui_music_load(i,cancel);
    }
    char qualified[131];snprintf(qualified,sizeof(qualified),"0:%s",path);
    return cache_path(KUI_MUSIC_CUSTOM_INDEX,qualified,NULL,title,KUI_MUSIC_CUSTOM_MAX,cancel);
}
