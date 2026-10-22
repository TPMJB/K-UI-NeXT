/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/music_player.h"
#include "kui/music.h"
#include "kui/wav.h"
#include "platform.h"
#include <stdio.h>
#include <string.h>

static bool cancelled(kui_cancel_fn cancel) {return cancel && cancel();}
static bool audio_name(const char *name) {
    size_t n=strlen(name);if(n<4 || name[n-4]!='.') return false;
    return ((name[n-3]=='w'||name[n-3]=='W') && (name[n-2]=='a'||name[n-2]=='A') &&
        (name[n-1]=='v'||name[n-1]=='V')) ||
        ((name[n-3]=='o'||name[n-3]=='O') && (name[n-2]=='g'||name[n-2]=='G') &&
        (name[n-1]=='g'||name[n-1]=='G'));
}
bool kui_music_player_list(const char *root,unsigned offset,struct kui_music_player_page *out,
    kui_log_fn log,kui_cancel_fn cancel) {
    if(!out) return false;
    memset(out,0,sizeof(*out));
    if(!kui_destination_normalize(out->root,root) || offset>8192u) {
        snprintf(out->message,sizeof(out->message),"Invalid music folder/page");return false;
    }
    if(cancelled(cancel)) {snprintf(out->message,sizeof(out->message),"Browse stopped");return false;}
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
        if(!directory && !audio_name(info.fname)) continue;
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
    snprintf(out->message,sizeof(out->message),"%s",ok?"WAV / Ogg Vorbis; mono/stereo 8-44.1 kHz; file limit 6 MiB":problem);
    if(!ok) {out->count=0;out->has_more=false;if(log) log("Music browser: %s",problem);}
    return ok;
}
void kui_music_player_run(const char *path,unsigned volume,struct kui_app_status *out,
    kui_log_fn log,kui_cancel_fn cancel,kui_app_progress_fn progress) {
    if(!out) return;
    memset(out,0,sizeof(*out));
    char normalized[KUI_DEST_ROOT_CAP];
    bool ok=false;
    const char *problem="Invalid WAV/Ogg path";
    if(!kui_destination_normalize(normalized,path) || !audio_name(normalized)) goto done;
    const char *name=strrchr(normalized,'/');name=name?name+1:normalized;
    snprintf(out->lines[out->line_count++],KUI_APP_LINE_CAP,"%.79s",name);
    snprintf(out->message,sizeof(out->message),"Loading song into RAM; B cancels");
    if(progress) progress(out);
    if(cancelled(cancel)) {out->stopped=true;snprintf(out->message,sizeof(out->message),"Song load stopped; previous song retained");goto done;}
    /* This is a bounded preload, not a foreground streaming loop. The backend
     * keeps the prior cached song playing while the I/O worker reads the new
     * file, then closes/unmounts SD before atomically selecting the new PCM. */
    ok=kui_music_load_path(normalized,name,cancel);
    if(ok) {
        kui_music_set_config(true,volume);kui_music_resume();
        struct kui_music_status status;kui_music_status_copy(&status);
        out->done=out->total=status.pcm_bytes;
        if(!status.playing) {
            ok=false;snprintf(out->message,sizeof(out->message),"%s",status.message);
            goto done;
        }
        snprintf(out->lines[out->line_count++],KUI_APP_LINE_CAP,"%u Hz | %s | cached in RAM",(unsigned)status.sample_rate,status.compressed?"Ogg Vorbis":"PCM16");
        snprintf(out->lines[out->line_count++],KUI_APP_LINE_CAP,"Playback continues in other apps, including the ripper.");
        snprintf(out->lines[out->line_count++],KUI_APP_LINE_CAP,"Home Y changes volume/off. Triggers include your selected song.");
        problem="Background song selected; continue browsing or press Start for Home";
    } else {
        struct kui_music_status status;kui_music_status_copy(&status);
        snprintf(out->message,sizeof(out->message),"%s",status.message);
        out->stopped=cancelled(cancel);
    }
done:
    out->complete=true;out->passed=ok;out->errors=ok||out->stopped?0u:1u;
    if(ok || !out->message[0]) snprintf(out->message,sizeof(out->message),"%s",problem);
    if(log) log("Music player: %s",out->message);
    if(progress) progress(out);
}
