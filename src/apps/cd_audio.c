/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/cd_audio.h"
#include "platform.h"
#include <dc/syscalls.h>
#include <dc/spu.h>
#include <dc/sound/sound.h>
#include <kos/thread.h>
#include <kos/timer.h>
#include <stdio.h>
#include <string.h>

/* Independent bounded CD-audio adapter. Every firmware-visible object is static
 * and is never changed again after a failed abort. No GD capture internals or
 * standard unbounded KOS CD-command wrappers are used. */
static struct {
    struct kui_cd_audio_status status;
    bool bus_initialized,may_play;
    unsigned volume;
    kui_log_fn log;kui_cancel_fn cancel;
    cd_cmd_chk_status_t detail;
    cd_cmd_toc_params_t toc_params;
    cd_cmd_play_params_t play_params;
    struct {uint8_t before[32];cd_toc_t data;uint8_t after[32];} toc;
    cd_toc_t listed;
} audio={.volume=30};
static void message(const char *text) {
    snprintf(audio.status.message,sizeof(audio.status.message),"%s",text);
}
static uint64_t now(void *ctx) {(void)ctx;return timer_ms_gettime64();}
static void wait_worker(void *ctx) {(void)ctx;thd_sleep(1);}
static bool cancelled(void *ctx) {(void)ctx;return audio.cancel && audio.cancel();}
static int submit(void *ctx,int code,void *params) {
    (void)ctx;int handle=syscall_gdrom_send_command((cd_cmd_code_t)code,params);
    syscall_gdrom_exec_server();return handle;
}
static int poll(void *ctx,int handle) {
    (void)ctx;syscall_gdrom_exec_server();return syscall_gdrom_check_command(handle,&audio.detail);
}
static void abort_command(void *ctx,int handle) {(void)ctx;syscall_gdrom_abort_command(handle);}
static bool command(int code,void *params,uint32_t timeout) {
    if(audio.status.poisoned) {message("RESET REQUIRED: CD audio command recovery failed");return false;}
    memset(&audio.detail,0,sizeof(audio.detail));
    const struct kui_command_ops ops={NULL,now,wait_worker,cancelled,submit,poll,abort_command};
    enum kui_command_result result=kui_command(&ops,code,params,timeout,1000);
    if(result==KUI_CMD_OK) return true;
    if(audio.log) audio.log("CMD %d %s",code,kui_command_name(result));
    if(result==KUI_CMD_RECOVERY_FAILED) audio.status.poisoned=true;
    snprintf(audio.status.message,sizeof(audio.status.message),"Audio CD: %s",kui_command_name(result));
    return false;
}
static bool cd_present(cd_check_drive_status_t *drive) {
    memset(drive,0,sizeof(*drive));
    if(syscall_gdrom_check_drive(drive)<0) {message("Cannot read drive status");return false;}
    if(drive->status==CD_STATUS_OPEN || drive->status==CD_STATUS_NO_DISC) {
        audio.status.loaded=false;audio.status.playing=false;audio.status.paused=false;
        audio.may_play=false;message("Insert an audio CD and Refresh");return false;
    }
    if(drive->status==CD_STATUS_FATAL || drive->disc_type==CD_FAIL) {
        audio.status.poisoned=true;message("Drive reports a fatal error; RESET REQUIRED");
        if(audio.log) audio.log("CMD %d ABORT FAILED: RESET REQUIRED (fatal drive status)",CD_CMD_INIT);
        return false;
    }
    if(drive->disc_type!=CD_CDDA && drive->disc_type!=CD_CDROM && drive->disc_type!=CD_CDROM_XA) {
        audio.status.loaded=false;audio.status.playing=false;audio.status.paused=false;audio.may_play=false;
        message("Insert an audio or enhanced CD; GD-ROM is not played here");return false;
    }
    return true;
}
static bool wait_ready(void) {
    /* STOP/INIT can acknowledge before the drive leaves BUSY. Observe the
     * transition briefly; never retry the command or issue it while busy. */
    uint64_t deadline=timer_ms_gettime64()+2000;
    for(;;) {
        if(cancelled(NULL)) {message("Audio CD operation cancelled");return false;}
        cd_check_drive_status_t drive;
        syscall_gdrom_exec_server();
        if(!cd_present(&drive)) return false;
        if(drive.status==CD_STATUS_PAUSED || drive.status==CD_STATUS_STANDBY ||
           drive.status==CD_STATUS_PLAYING || drive.status==CD_STATUS_RETRY) return true;
        if(drive.status!=CD_STATUS_BUSY && drive.status!=CD_STATUS_SEEKING &&
           drive.status!=CD_STATUS_SCANNING) {
            snprintf(audio.status.message,sizeof(audio.status.message),
                "Audio CD drive status %d; Refresh before playing",(int)drive.status);return false;
        }
        if(timer_ms_gettime64()>=deadline) {
            snprintf(audio.status.message,sizeof(audio.status.message),
                "Audio CD drive still busy (status %d); wait, then Refresh",(int)drive.status);return false;
        }
        wait_worker(NULL);
    }
}
static bool stop_drive(void) {
    if(audio.status.poisoned) {message("RESET REQUIRED: CD audio command recovery failed");return false;}
    if(!audio.may_play) {audio.status.playing=false;audio.status.paused=false;return true;}
    /* Stop must complete even when B is held: cancellation cannot transfer a
     * potentially still-playing drive to the capture worker. */
    kui_cancel_fn previous=audio.cancel;audio.cancel=NULL;
    bool ok=command(CD_CMD_STOP,NULL,3000);audio.cancel=previous;
    spu_cdda_volume(0,0);
    if(!ok) return false;
    audio.may_play=false;audio.status.playing=false;audio.status.paused=false;message("Audio CD stopped");return true;
}
bool kui_cd_audio_stop_for_io(kui_log_fn log) {audio.log=log;return stop_drive();}
bool kui_cd_audio_owns_drive(void) {return audio.may_play || audio.status.poisoned;}
void kui_cd_audio_status_copy(struct kui_cd_audio_status *out) {if(out) *out=audio.status;}
void kui_cd_audio_set_volume(unsigned percent) {
    audio.volume=percent>100u?100u:percent;
    if(audio.may_play) {int level=(int)(audio.volume*15u/100u);spu_cdda_volume(level,level);}
}
void kui_cd_audio_poll(struct kui_cd_audio_status *out,kui_log_fn log) {
    if(audio.may_play && !audio.status.poisoned) {
        audio.log=log;
        cd_check_drive_status_t drive;
        /* A BUSY/SEEKING sample during playback is not a new command failure.
         * Use one status snapshot, preserving drive ownership until stopped. */
        if(cd_present(&drive) && audio.status.playing &&
           (drive.status==CD_STATUS_PAUSED || drive.status==CD_STATUS_STANDBY)) {
            audio.status.playing=false;audio.status.paused=false;audio.may_play=false;
            message("Audio CD track finished");
        }
    }
    if(out) *out=audio.status;
}
static bool read_toc(void) {
    if(!audio.bus_initialized) {
        /* A status query is safe before initialization, but servicing the
         * firmware command queue requires its initialized workspace first. */
        cd_check_drive_status_t drive;
        if(!cd_present(&drive)) return false;
        kui_drive_init_bus();audio.bus_initialized=true;
    }
    if(!wait_ready()) return false;
    if(!command(CD_CMD_INIT,NULL,12000) || !wait_ready()) return false;
    memset(&audio.toc,0xa5,sizeof(audio.toc));
    audio.toc_params=(cd_cmd_toc_params_t){CD_AREA_LOW,&audio.toc.data};
    if(!command(CD_CMD_GETTOC2,&audio.toc_params,5000)) return false;
    for(unsigned i=0;i<sizeof(audio.toc.before);i++) {
        if(audio.toc.before[i]!=0xa5 || audio.toc.after[i]!=0xa5) {
            audio.status.poisoned=true;message("RESET REQUIRED: CD audio TOC guard changed");
            if(audio.log) audio.log("CMD %d ABORT FAILED: RESET REQUIRED (TOC guard)",CD_CMD_GETTOC2);
            return false;
        }
    }
    struct kui_toc parsed;
    if(!kui_parse_toc(audio.toc.data.entry,audio.toc.data.first,audio.toc.data.last,
                     audio.toc.data.leadout_sector,&parsed)) {message("Invalid audio CD track table");return false;}
    for(unsigned i=0;i<parsed.count;i++) {
        if(parsed.tracks[i].end<=parsed.tracks[i].start) {message("Invalid audio CD track length");return false;}
    }
    return true;
}
static bool list_tracks(void) {
    if(!stop_drive()) return false;
    audio.status.loaded=false;audio.status.count=0;
    if(!read_toc()) return false;
    struct kui_toc parsed;
    if(!kui_parse_toc(audio.toc.data.entry,audio.toc.data.first,audio.toc.data.last,
                     audio.toc.data.leadout_sector,&parsed)) return false;
    audio.listed=audio.toc.data;
    for(unsigned i=0;i<parsed.count;i++) {
        if(parsed.tracks[i].control&4u) continue;
        audio.status.tracks[audio.status.count++]=(struct kui_cd_audio_track){
            parsed.tracks[i].number,(parsed.tracks[i].end-parsed.tracks[i].start)/75u};
    }
    if(audio.log) audio.log("Audio CD TOC: %u audio tracks; %u data tracks skipped",
        audio.status.count,parsed.count-audio.status.count);
    if(!audio.status.count) {message("No audio tracks on this CD; data tracks are never played");return false;}
    audio.status.first=audio.status.tracks[0].number;
    audio.status.last=audio.status.tracks[audio.status.count-1u].number;
    audio.status.current=0;audio.status.loaded=true;message("Choose an audio track; playback uses the drive directly");return true;
}
static bool play_track(unsigned track) {
    bool selected_audio=false;
    for(unsigned i=0;i<audio.status.count;i++)
        if(audio.status.tracks[i].number==track) selected_audio=true;
    if(!audio.status.loaded || !selected_audio) {
        message("Refresh the audio CD before choosing a track");return false;
    }
    if(!stop_drive() || !read_toc()) return false;
    if(memcmp(&audio.listed,&audio.toc.data,sizeof(audio.listed))) {
        audio.status.loaded=false;message("Disc changed; Refresh before playing");return false;
    }
    if(snd_init()<0) {message("Sound initialization failed; audio CD not started");return false;}
    audio.play_params=(cd_cmd_play_params_t){track,track,0};
    /* The drive may have begun playback even if a terminal reply is lost. */
    audio.may_play=true;
    spu_cdda_pan(0,31);kui_cd_audio_set_volume(audio.volume);
    if(!command(CD_CMD_PLAY_TRACKS,&audio.play_params,5000)) {
        /* An acknowledged command failure may still have begun CD audio. Stop
         * it before returning, unless failed recovery forbids all new commands. */
        char failure[sizeof(audio.status.message)];strcpy(failure,audio.status.message);
        if(!audio.status.poisoned && stop_drive()) message(failure);
        return false;
    }
    audio.status.current=track;audio.status.playing=true;audio.status.paused=false;
    snprintf(audio.status.message,sizeof(audio.status.message),"Playing audio CD track %u",track);return true;
}
void kui_cd_audio_run(enum kui_cd_audio_action action,unsigned track,
    struct kui_cd_audio_status *out,kui_log_fn log,kui_cancel_fn cancel) {
    audio.log=log;audio.cancel=cancel;bool ok=false;
    if(audio.status.poisoned) message("RESET REQUIRED: CD audio command recovery failed");
    else if(cancel && cancel() && action!=KUI_CD_AUDIO_STOP) message("Audio CD operation cancelled");
    else if(action==KUI_CD_AUDIO_LIST) ok=list_tracks();
    else if(action==KUI_CD_AUDIO_PLAY) ok=play_track(track);
    else if(action==KUI_CD_AUDIO_STOP) ok=stop_drive();
    else if(action==KUI_CD_AUDIO_PAUSE && audio.status.playing) {
        if(wait_ready() && command(CD_CMD_PAUSE,NULL,3000)) {audio.status.playing=false;audio.status.paused=true;message("Audio CD paused");ok=true;}
    } else if(action==KUI_CD_AUDIO_RESUME && audio.status.paused) {
        if(wait_ready() && command(CD_CMD_RELEASE,NULL,3000)) {audio.status.playing=true;audio.status.paused=false;message("Audio CD resumed");ok=true;}
    } else message("Refresh or select an audio CD track first");
    if(log) log("Audio CD %s: %s",ok?"OK":"stopped/failed",audio.status.message);
    if(out) *out=audio.status;
    audio.cancel=NULL;
}
