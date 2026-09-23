/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/music_player.h"
#include "kui/music.h"
#include "platform.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct {
    bool connected,cancel,load_error,audio_error;
    unsigned connects,disconnects,unmounts,dirpos,dircloses,loads,progress,resumes,volume;
    struct kui_music_status status;
} fake;
static bool cancel(void) {return fake.cancel;}
static void log_line(const char *fmt,...) {(void)fmt;}
static void progress(const struct kui_app_status *s) {assert(s->done<=s->total);++fake.progress;}
bool kui_music_load_path(const char *path,const char *title,kui_cancel_fn cancelled) {
    assert(!strcmp(path,"/Music/test.wav") && !strcmp(title,"test.wav") && cancelled==cancel);++fake.loads;
    if(fake.load_error) {strcpy(fake.status.message,"Track read failed; previous song retained");return false;}
    fake.status.loaded=true;fake.status.pcm_bytes=3u*1024u*1024u;fake.status.sample_rate=22050;return true;
}
void kui_music_set_config(bool enabled,unsigned volume) {assert(enabled);fake.volume=volume;}
void kui_music_resume(void) {++fake.resumes;fake.status.playing=!fake.audio_error;if(fake.audio_error) strcpy(fake.status.message,"Audio stream unavailable");}
void kui_music_status_copy(struct kui_music_status *out) {*out=fake.status;}
bool kui_sd_connect(void) {assert(!fake.connected);fake.connected=true;++fake.connects;return true;}
void kui_sd_disconnect(void) {assert(fake.connected);fake.connected=false;++fake.disconnects;}
bool kui_mount(FATFS *fs,kui_log_fn log) {(void)fs;(void)log;assert(fake.connected);return true;}
FRESULT f_mount(FATFS *fs,const TCHAR *path,BYTE mode) {
    assert(!fs && !strcmp(path,"0:") && !mode);++fake.unmounts;return FR_OK;
}
FRESULT f_opendir(DIR *dir,const TCHAR *path) {
    (void)dir;assert(!strcmp(path,"0:/Music") && fake.connected);fake.dirpos=0;return FR_OK;
}
FRESULT f_readdir(DIR *dir,FILINFO *info) {
    (void)dir;memset(info,0,sizeof(*info));unsigned at=fake.dirpos++;
    if(at==0) {strcpy(info->fname,"Albums");info->fattrib=AM_DIR;}
    else if(at==1) strcpy(info->fname,"ignore.mp3");
    else if(at==2) {strcpy(info->fname,"hidden.wav");info->fattrib=AM_HID;}
    else if(at<13) snprintf(info->fname,sizeof(info->fname),"song%02u.%s",at-3u,at&1u?"WAV":"wav");
    return FR_OK;
}
FRESULT f_closedir(DIR *dir) {(void)dir;++fake.dircloses;return FR_OK;}
int main(void) {
    struct kui_app_status out;struct kui_music_player_page page;
    kui_music_player_run("/Music/test.wav",75,&out,log_line,cancel,progress);
    assert(out.complete && out.passed && !out.stopped && !out.errors && out.done==3u*1024u*1024u);
    assert(fake.loads==1 && fake.resumes==1 && fake.volume==75 && fake.status.playing);
    assert(!fake.connects && strstr(out.message,"Background song selected"));
    /* Return completed to the shell while music remains playing: navigation
     * no longer waits for EOF or requires a Stop action. */
    fake.load_error=true;
    kui_music_player_run("/Music/test.wav",20,&out,log_line,cancel,progress);
    assert(out.complete && !out.passed && out.errors==1 && fake.status.playing && fake.volume==75);
    assert(strstr(out.message,"previous song retained"));
    fake.load_error=false;fake.audio_error=true;
    kui_music_player_run("/Music/test.wav",20,&out,log_line,cancel,progress);
    assert(out.complete && !out.passed && out.errors==1 && strstr(out.message,"Audio stream unavailable"));
    fake.audio_error=false;fake.status.playing=true;
    unsigned loads=fake.loads;fake.cancel=true;
    kui_music_player_run("/Music/test.wav",20,&out,log_line,cancel,progress);
    assert(out.stopped && !out.errors && fake.loads==loads && fake.status.playing);
    fake.cancel=false;
    kui_music_player_run("/Music/../bad.wav",10,&out,log_line,cancel,progress);assert(!out.passed && fake.loads==loads);
    kui_music_player_run("/Music/test.mp3",10,&out,log_line,cancel,progress);assert(!out.passed && fake.loads==loads);
    assert(kui_music_player_list("/Music",0,&page,log_line,cancel));
    assert(page.count==8 && page.has_more && page.entries[0].directory && !strcmp(page.entries[0].name,"Albums"));
    assert(!strcmp(page.entries[1].name,"song00.WAV") && !page.entries[1].directory);
    assert(fake.disconnects==1 && fake.dircloses==1 && fake.status.playing);
    assert(kui_music_player_list("/Music",8,&page,log_line,cancel));
    assert(page.count==3 && !page.has_more && !strcmp(page.entries[0].name,"song07.wav"));
    unsigned connects=fake.connects;fake.cancel=true;
    assert(!kui_music_player_list("/Music",0,&page,log_line,cancel) && fake.connects==connects);
    puts("PASS Music browser: bounded background selection returns, old song retained on fault/cancel, read-only paging never pauses audio");return 0;
}
