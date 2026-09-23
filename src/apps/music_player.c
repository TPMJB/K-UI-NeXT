/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/music_player.h"
#include "kui/music.h"
#include "kui/wav.h"
#include "platform.h"
#include <dc/sound/stream.h>
#include <kos/thread.h>
#include <kos/timer.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PLAYER_RING_BYTES 65536u
#define PLAYER_CALLBACK_BYTES PLAYER_RING_BYTES
struct player {
    FIL file;
    struct kui_wav wav;
    kui_cancel_fn cancel;
    uint8_t *buffer[2];
    unsigned next_buffer;
    uint64_t sent,eof_at;
    bool failed,stopped,eof;
};
/* Single I/O worker only. KOS invokes get-data synchronously from start/poll;
 * no independent thread or interrupt is allowed to enter FatFs here. */
static struct player *active;
static bool cancelled(kui_cancel_fn cancel) {return cancel && cancel();}
static bool wav_name(const char *name) {
    size_t n=strlen(name);if(n<4 || name[n-4]!='.') return false;
    return (name[n-3]=='w'||name[n-3]=='W') && (name[n-2]=='a'||name[n-2]=='A') &&
        (name[n-1]=='v'||name[n-1]=='V');
}
bool kui_music_player_list(const char *root,unsigned offset,struct kui_music_player_page *out,
    kui_log_fn log,kui_cancel_fn cancel) {
    if(!out) return false;
    memset(out,0,sizeof(*out));
    if(!kui_destination_normalize(out->root,root) || offset>8192u) {
        snprintf(out->message,sizeof(out->message),"Invalid music folder/page");return false;
    }
    if(cancelled(cancel)) {snprintf(out->message,sizeof(out->message),"Browse stopped");return false;}
    kui_music_pause();
    if(!kui_sd_connect()) {snprintf(out->message,sizeof(out->message),"SD card unavailable");return false;}
    FATFS fs;DIR dir;bool opened=false,ok=false;
    const char *problem="Cannot mount SD card";
    char path[KUI_DEST_ROOT_CAP+3];snprintf(path,sizeof(path),"0:%s",out->root);
    if(!kui_mount(&fs,log)) goto done;
    if(f_opendir(&dir,path)!=FR_OK) {problem="Cannot open music folder";goto done;}
    opened=true;
    /* Filesystem order avoids allocating or repeatedly scanning the full
     * directory. Each next page rescans only through its preceding entries. */
    for(;;) {
        if(cancelled(cancel)) {problem="Browse stopped";goto done;}
        FILINFO info;FRESULT r=f_readdir(&dir,&info);
        if(r!=FR_OK) {problem="Cannot read music folder";goto done;}
        if(!info.fname[0]) break;
        if(!strcmp(info.fname,".") || !strcmp(info.fname,"..") ||
           (info.fattrib&(AM_HID|AM_SYS))) continue;
        bool directory=(info.fattrib&AM_DIR)!=0;
        if(!directory && !wav_name(info.fname)) continue;
        if(offset) {--offset;continue;}
        if(out->count==KUI_MUSIC_PLAYER_ROWS) {out->has_more=true;break;}
        struct kui_music_player_entry *entry=&out->entries[out->count++];
        entry->directory=directory;
        if(strlen(info.fname)>=sizeof(entry->name)) {
            snprintf(entry->name,sizeof(entry->name),"[Name too long]");entry->disabled=true;
        } else {
            strcpy(entry->name,info.fname);
            char joined[KUI_DEST_ROOT_CAP];
            entry->disabled=!kui_destination_join(joined,out->root,entry->name);
        }
    }
    ok=true;
done:
    if(opened && f_closedir(&dir)!=FR_OK) {ok=false;problem="Cannot close music folder";}
    if(f_mount(NULL,"0:",0)!=FR_OK) {ok=false;problem="Cannot release SD filesystem";}
    kui_sd_disconnect();
    snprintf(out->message,sizeof(out->message),"%s",ok?"PCM16 WAV, mono/stereo, 8-44.1 kHz":problem);
    if(!ok) {out->count=0;out->has_more=false;if(log) log("Music browser: %s",problem);}
    return ok;
}
static bool read_header(void *ctx,uint64_t at,void *out,size_t bytes) {
    struct player *p=ctx;
    if(cancelled(p->cancel)) {p->stopped=true;return false;}
    if(at>(uint64_t)f_size(&p->file) || bytes>(uint64_t)f_size(&p->file)-at ||
       bytes>32u || f_lseek(&p->file,(FSIZE_t)at)!=FR_OK) return false;
    UINT got=0;
    return f_read(&p->file,out,(UINT)bytes,&got)==FR_OK && got==bytes;
}
static void *stream_data(snd_stream_hnd_t stream,int requested,int *received) {
    (void)stream;
    if(!received) return NULL;
    *received=0;
    struct player *p=active;
    if(!p || requested<=0 || (unsigned)requested>PLAYER_CALLBACK_BYTES ||
       (unsigned)requested%p->wav.frame_bytes || p->failed || p->stopped) return NULL;
    uint8_t *out=p->buffer[p->next_buffer];p->next_buffer^=1u;
    memset(out,0,(size_t)requested);
    size_t needed=p->wav.bytes-(size_t)p->sent;
    if(needed>(size_t)requested) needed=(size_t)requested;
    size_t at=0;
    while(at<needed) {
        if(cancelled(p->cancel)) {p->stopped=true;return NULL;}
        UINT count=(UINT)(needed-at);if(count>32768u) count=32768u;
        UINT got=0;
        if(f_read(&p->file,out+at,count,&got)!=FR_OK || got!=count) {p->failed=true;return NULL;}
        at+=got;
    }
    p->sent+=needed;
    if(p->sent==p->wav.bytes) p->eof=true;
    /* Always return full, zero-padded frames. KOS rounds DMA sizes upward;
     * zero-padding prevents a short tail from exposing adjacent memory. */
    *received=requested;return out;
}
static void publish(struct kui_app_status *out,const struct player *p,kui_app_progress_fn progress) {
    out->done=p->sent;out->total=p->wav.bytes;
    snprintf(out->message,sizeof(out->message),"%s",p->eof?"Finishing queued audio; B stops":"Playing SD WAV; B stops");
    if(progress) progress(out);
}
void kui_music_player_run(const char *path,unsigned volume,struct kui_app_status *out,
    kui_log_fn log,kui_cancel_fn cancel,kui_app_progress_fn progress) {
    if(!out) return;
    memset(out,0,sizeof(*out));
    char normalized[KUI_DEST_ROOT_CAP];
    const char *problem="Invalid WAV path";
    bool connected=false,opened=false,audio=false,ok=false;
    snd_stream_hnd_t stream=SND_STREAM_INVALID;
    struct player p={.cancel=cancel};
    FATFS fs;
    if(!kui_destination_normalize(normalized,path) || !wav_name(normalized)) goto done;
    const char *name=strrchr(normalized,'/');name=name?name+1:normalized;
    snprintf(out->lines[out->line_count++],KUI_APP_LINE_CAP,"%.79s",name);
    if(cancelled(cancel)) {p.stopped=true;goto done;}
    /* KOS's streamer limits are global. Release its menu stream/separation
     * buffer before creating this larger ring, retaining cached menu PCM. */
    kui_music_release_audio();
    if(!kui_sd_connect()) {problem="SD card unavailable";goto done;}connected=true;
    if(!kui_mount(&fs,log)) {problem="Cannot mount SD card";goto done;}
    char qualified[KUI_DEST_ROOT_CAP+3];snprintf(qualified,sizeof(qualified),"0:%s",normalized);
    if(f_open(&p.file,qualified,FA_READ)!=FR_OK) {problem="Cannot open WAV";goto done;}opened=true;
    if(!kui_wav_read(read_header,&p,(uint64_t)f_size(&p.file),&p.wav)) {
        problem="Unsupported/damaged WAV: PCM16, mono/stereo, 8-44.1 kHz";goto done;
    }
    if(f_lseek(&p.file,(FSIZE_t)p.wav.offset)!=FR_OK) {problem="Cannot seek WAV audio";goto done;}
    p.buffer[0]=aligned_alloc(32,PLAYER_CALLBACK_BYTES);
    p.buffer[1]=aligned_alloc(32,PLAYER_CALLBACK_BYTES);
    if(!p.buffer[0] || !p.buffer[1]) {problem="Not enough RAM for audio buffers";goto done;}
    snprintf(out->lines[out->line_count++],KUI_APP_LINE_CAP,"%u Hz | %s | PCM16",
        p.wav.rate,p.wav.channels==2?"Stereo":"Mono");
    uint64_t seconds=p.wav.bytes/((uint64_t)p.wav.rate*p.wav.frame_bytes);
    snprintf(out->lines[out->line_count++],KUI_APP_LINE_CAP,"Duration %" PRIu64 ":%02" PRIu64 " | streaming from SD",seconds/60,seconds%60);
    snprintf(out->lines[out->line_count++],KUI_APP_LINE_CAP,"Audio CDs and compressed codecs: not available yet");
    if(snd_stream_init_ex(2,PLAYER_RING_BYTES)<0) {
        snd_stream_shutdown();problem="Cannot initialize audio";goto done;
    }
    audio=true;active=&p;
    stream=snd_stream_alloc(stream_data,PLAYER_RING_BYTES);
    if(stream==SND_STREAM_INVALID) {problem="Audio stream unavailable";goto done;}
    snd_stream_queue_enable(stream);
    snd_stream_start(stream,p.wav.rate,p.wav.channels==2);
    snd_stream_volume(stream,(int)kui_music_aica_volume(volume));
    if(p.failed || p.stopped || cancelled(cancel)) {
        if(cancelled(cancel)) p.stopped=true;
        problem="WAV read failed during preload";
        snd_stream_volume(stream,0);snd_stream_queue_go(stream);snd_stream_queue_disable(stream);goto done;
    }
    snd_stream_queue_go(stream);snd_stream_queue_disable(stream);
    const uint64_t drain_ms=((uint64_t)PLAYER_RING_BYTES*1000u+p.wav.rate*2u-1u)/(p.wav.rate*2u)+100u;
    uint64_t last_publish=0;
    for(;;) {
        if(cancelled(cancel)) {p.stopped=true;break;}
        uint64_t now=timer_ms_gettime64();
        if(p.eof && !p.eof_at) p.eof_at=now+drain_ms;
        if(p.eof_at && now>=p.eof_at) {ok=true;break;}
        if(snd_stream_poll(stream)<0 || p.failed) {problem="Audio or SD read failed";break;}
        if(p.stopped) break;
        now=timer_ms_gettime64();
        if(!last_publish || now-last_publish>=250u) {publish(out,&p,progress);last_publish=now;}
        thd_sleep(8);
    }
done:
    /* Destroy waits for queued AICA DMA, before freeing callback buffers or
     * handing SD/stream globals to another app. Never f_write on this path. */
    if(stream!=SND_STREAM_INVALID) snd_stream_destroy(stream);
    active=NULL;
    if(audio) snd_stream_shutdown();
    free(p.buffer[0]);free(p.buffer[1]);
    if(opened && f_close(&p.file)!=FR_OK) {ok=false;problem="Cannot close WAV";}
    if(connected) {
        if(f_mount(NULL,"0:",0)!=FR_OK) {ok=false;problem="Cannot release SD filesystem";}
        kui_sd_disconnect();
    }
    out->done=p.sent;out->total=p.wav.bytes;out->complete=true;
    out->stopped=p.stopped;out->passed=ok && !p.stopped;out->errors=out->passed||out->stopped?0u:1u;
    snprintf(out->message,sizeof(out->message),"%s",out->stopped?"Playback stopped":out->passed?"Playback complete":problem);
    if(log) log("Music player: %s",out->message);
    if(progress) progress(out);
}
