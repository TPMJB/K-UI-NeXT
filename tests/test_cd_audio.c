/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/cd_audio.h"
#include <dc/syscalls.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
static struct {
    uint64_t now;
    cd_check_drive_status_t drive;
    cd_toc_t toc;
    int current,fail_command,busy_command,status_error;
    void *params;unsigned commands,reads,plays,stops,aborts,bus,logs,volume,pans;
    bool cancelled,abort_fails,guard_error,aborted,sound_error;
    char last_log[256];
} fake;
static bool cancel(void) {return fake.cancelled;}
uint64_t timer_ms_gettime64(void) {return fake.now;}
void thd_sleep(unsigned ms) {assert(ms==1);fake.now+=100;}
void kui_drive_init_bus(void) {++fake.bus;}
int snd_init(void) {return fake.sound_error?-1:0;}
void spu_cdda_volume(int left,int right) {assert(left==right && left>=0 && left<=15);fake.volume=(unsigned)left;}
void spu_cdda_pan(int left,int right) {assert(left==0 && right==31);++fake.pans;}
int syscall_gdrom_check_drive(cd_check_drive_status_t *out) {*out=fake.drive;return fake.status_error;}
int syscall_gdrom_send_command(cd_cmd_code_t cmd,void *params) {
    fake.current=cmd;fake.params=params;fake.aborted=false;++fake.commands;
    assert(cmd==CD_CMD_INIT || cmd==CD_CMD_GETTOC2 || cmd==CD_CMD_PLAY_TRACKS ||
           cmd==CD_CMD_PAUSE || cmd==CD_CMD_RELEASE || cmd==CD_CMD_STOP);
    if(cmd==CD_CMD_GETTOC2) {
        cd_cmd_toc_params_t *toc=params;assert(toc->area==CD_AREA_LOW);++fake.reads;
        *toc->toc=fake.toc;
        if(fake.guard_error) ((unsigned char *)toc->toc)[sizeof(cd_toc_t)]=0;
    } else if(cmd==CD_CMD_PLAY_TRACKS) {
        cd_cmd_play_params_t *play=params;assert(play && play->start==play->end && play->repeat==0);
        ++fake.plays;
    } else if(cmd==CD_CMD_STOP) {assert(!params);++fake.stops;}
    else assert(!params);
    return 42;
}
void syscall_gdrom_exec_server(void) {}
int syscall_gdrom_check_command(int handle,cd_cmd_chk_status_t *detail) {
    assert(handle==42);memset(detail,0,sizeof(*detail));
    if(fake.aborted) return fake.abort_fails?1:0;
    if(fake.current==fake.busy_command) return 1;
    if(fake.current==fake.fail_command) return -1;
    if(fake.current==CD_CMD_PLAY_TRACKS || fake.current==CD_CMD_RELEASE) fake.drive.status=CD_STATUS_PLAYING;
    if(fake.current==CD_CMD_PAUSE) fake.drive.status=CD_STATUS_PAUSED;
    if(fake.current==CD_CMD_STOP || fake.current==CD_CMD_INIT) fake.drive.status=CD_STATUS_STANDBY;
    return 2;
}
void syscall_gdrom_abort_command(int handle) {assert(handle==42);++fake.aborts;fake.aborted=true;}
static void log_line(const char *fmt,...) {
    ++fake.logs;va_list ap;va_start(ap,fmt);vsnprintf(fake.last_log,sizeof(fake.last_log),fmt,ap);va_end(ap);
}
static void make_toc(void) {
    memset(&fake.toc,0xff,sizeof(fake.toc));fake.toc.first=0x01010000;fake.toc.last=0x01030000;
    fake.toc.entry[0]=0x01000000|150u;fake.toc.entry[1]=0x01000000|7650u;
    fake.toc.entry[2]=0x01000000|15150u;fake.toc.leadout_sector=0x01000000|30150u;
}
int main(int argc,char **argv) {
    struct kui_cd_audio_status s;fake.drive=(cd_check_drive_status_t){CD_STATUS_STANDBY,CD_GDROM};make_toc();
    if(argc>1 && !strcmp(argv[1],"guard")) {
        fake.drive.disc_type=CD_CDDA;fake.guard_error=true;
        kui_cd_audio_run(KUI_CD_AUDIO_LIST,0,&s,log_line,cancel);
        assert(s.poisoned && !s.loaded && !fake.plays && !kui_cd_audio_stop_for_io(log_line));
        puts("PASS CD audio: overwritten TOC guard permanently refuses handoff");return 0;
    }
    kui_cd_audio_run(KUI_CD_AUDIO_LIST,0,&s,log_line,cancel);
    assert(!s.loaded && !fake.commands && !fake.bus && strstr(s.message,"Audio CDs only"));
    fake.drive.disc_type=CD_CDDA;fake.drive.status=CD_STATUS_OPEN;
    kui_cd_audio_run(KUI_CD_AUDIO_LIST,0,&s,log_line,cancel);assert(!s.loaded && !fake.commands);
    fake.drive.status=CD_STATUS_STANDBY;fake.cancelled=true;
    kui_cd_audio_run(KUI_CD_AUDIO_LIST,0,&s,log_line,cancel);assert(!s.loaded && !fake.commands);
    fake.cancelled=false;fake.toc.entry[1]|=0x40000000;
    kui_cd_audio_run(KUI_CD_AUDIO_LIST,0,&s,log_line,cancel);assert(!s.loaded && !fake.plays && strstr(s.message,"Mixed/data"));
    make_toc();fake.status_error=2; /* Any nonnegative firmware return is success. */
    kui_cd_audio_run(KUI_CD_AUDIO_LIST,0,&s,log_line,cancel);
    assert(s.loaded && s.count==3 && s.first==1 && s.last==3 && s.tracks[0].seconds==100 && s.tracks[2].seconds==200);
    assert(fake.bus==1 && !kui_cd_audio_owns_drive());
    unsigned commands=fake.commands;
    kui_cd_audio_run(KUI_CD_AUDIO_PLAY,4,&s,log_line,cancel);assert(!s.playing && fake.commands==commands);
    fake.toc.entry[1]++;
    kui_cd_audio_run(KUI_CD_AUDIO_PLAY,2,&s,log_line,cancel);assert(!s.playing && !s.loaded && !fake.plays && strstr(s.message,"Disc changed"));
    make_toc();kui_cd_audio_run(KUI_CD_AUDIO_LIST,0,&s,log_line,cancel);
    fake.sound_error=true;kui_cd_audio_run(KUI_CD_AUDIO_PLAY,2,&s,log_line,cancel);
    assert(!s.playing && !fake.plays && strstr(s.message,"Sound initialization"));fake.sound_error=false;
    kui_cd_audio_set_volume(60);
    kui_cd_audio_run(KUI_CD_AUDIO_PLAY,2,&s,log_line,cancel);
    assert(s.playing && !s.paused && s.current==2 && fake.plays==1 && fake.volume==9 && fake.pans==1);
    assert(kui_cd_audio_owns_drive());
    kui_cd_audio_run(KUI_CD_AUDIO_PAUSE,0,&s,log_line,cancel);assert(s.paused && !s.playing);
    kui_cd_audio_run(KUI_CD_AUDIO_RESUME,0,&s,log_line,cancel);assert(s.playing && !s.paused);
    fake.cancelled=true;assert(kui_cd_audio_stop_for_io(log_line));
    kui_cd_audio_status_copy(&s);assert(!s.playing && !s.paused && !kui_cd_audio_owns_drive() && !fake.volume);
    fake.cancelled=false;
    fake.fail_command=CD_CMD_PLAY_TRACKS;unsigned old_stops=fake.stops;
    kui_cd_audio_run(KUI_CD_AUDIO_PLAY,1,&s,log_line,cancel);
    assert(!s.playing && !kui_cd_audio_owns_drive() && fake.stops==old_stops+1 && strstr(s.message,"FAILED"));
    fake.fail_command=0;
    kui_cd_audio_run(KUI_CD_AUDIO_PLAY,1,&s,log_line,cancel);
    fake.drive.status=CD_STATUS_PAUSED;kui_cd_audio_poll(&s,log_line);
    assert(!s.playing && !kui_cd_audio_owns_drive() && strstr(s.message,"finished"));
    kui_cd_audio_run(KUI_CD_AUDIO_PLAY,1,&s,log_line,cancel);
    fake.fail_command=CD_CMD_STOP;assert(!kui_cd_audio_stop_for_io(log_line));
    assert(kui_cd_audio_owns_drive() && fake.volume==0);fake.fail_command=0;
    assert(kui_cd_audio_stop_for_io(log_line) && !kui_cd_audio_owns_drive());
    fake.busy_command=CD_CMD_INIT;uint64_t before=fake.now;
    kui_cd_audio_run(KUI_CD_AUDIO_LIST,0,&s,log_line,cancel);
    assert(!s.loaded && !s.poisoned && fake.aborts==1 && fake.now-before>=12000 && fake.now-before<=13000);
    fake.busy_command=0;kui_cd_audio_run(KUI_CD_AUDIO_LIST,0,&s,log_line,cancel);assert(s.loaded);
    kui_cd_audio_run(KUI_CD_AUDIO_PLAY,3,&s,log_line,cancel);
    fake.drive.status=CD_STATUS_OPEN;kui_cd_audio_poll(&s,log_line);
    assert(!s.loaded && !s.playing && !kui_cd_audio_owns_drive());
    fake.drive.status=CD_STATUS_STANDBY;
    /* A timed-out GETTOC abort leaves firmware owning its static parameter and
     * destination objects. Later calls refuse before changing those bytes. */
    fake.busy_command=CD_CMD_GETTOC2;fake.abort_fails=true;
    kui_cd_audio_run(KUI_CD_AUDIO_LIST,0,&s,log_line,cancel);assert(s.poisoned && kui_cd_audio_owns_drive());
    cd_cmd_toc_params_t *owned=fake.params;cd_toc_t saved=*owned->toc;
    unsigned char params[sizeof(*owned)];memcpy(params,owned,sizeof(params));commands=fake.commands;
    kui_cd_audio_run(KUI_CD_AUDIO_LIST,0,&s,log_line,cancel);
    kui_cd_audio_run(KUI_CD_AUDIO_PLAY,1,&s,log_line,cancel);
    assert(!kui_cd_audio_stop_for_io(log_line) && fake.commands==commands);
    assert(!memcmp(params,owned,sizeof(params)) && !memcmp(&saved,owned->toc,sizeof(saved)));
    puts("PASS CD audio: pure audio TOC, selected-track play, pause/resume, stop ownership, changed disc, deadlines, poisoned static firmware storage");
    return 0;
}
