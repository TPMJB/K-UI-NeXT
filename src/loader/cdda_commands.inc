/* SPDX-License-Identifier: GPL-3.0-only */
/* Included by the standalone profile5 orchestrator after its session helpers.
 * These are our portable command semantics, not BIOS IDs or a retail hook. */
#include "kui/cdda_control.h"

static struct kui_cdda_control command_control;
static bool command_running;
static uint32_t command_accepted,command_completions,command_status_checks;
#if CDDA_TEST_PROFILE == 5
static uint32_t command_expected_refusals,command_stale_refusals;
#endif
static uint32_t command_paused_frame,command_max_loops;

#if CDDA_TEST_PROFILE != 8
static struct kui_cdda_control_request command_request(enum kui_cdda_control_command kind) {
    return (struct kui_cdda_control_request){.command=kind};
}
#endif
#if CDDA_TEST_PROFILE != 7 && CDDA_TEST_PROFILE != 8
static bool command_same_status(const struct kui_cdda_control_status *a,
    const struct kui_cdda_control_status *b) {
    return a->state==b->state && a->first==b->first && a->end==b->end &&
        a->frame==b->frame && a->loops==b->loops && a->epoch==b->epoch &&
        a->pending_epoch==b->pending_epoch && a->repeat==b->repeat && a->pending==b->pending;
}
#endif
/* Preserve the original selected loop range when resuming/seeking mid-loop.
 * The ring begins at frame, but subsequent wrap returns to first, not frame. */
static bool command_audio_start(const struct kui_cdda_control_action *action) {
    uint32_t bytes;
    ring=(struct kui_cdda_ring){0};
    if(cdda_storage_open("0:/KUI/tests/cdda/stereo.raw",&bytes)) return false;
    if(!bytes || bytes%2352u) goto failed;
    const struct kui_cdda_pcm_source source={bytes,0,2352u,bytes/2352u,false};
    if(kui_cdda_pcm_init(&pcm,&source,read_at,NULL)!=KUI_CDDA_PCM_OK ||
       pcm.total_frames!=command_control.source_frames || action->frame>=action->end ||
       action->end>pcm.total_frames) goto failed;
    session_first=action->frame;session_frames=action->end-action->frame;
    segment_end=action->end;loop_first=action->first;repeat_audio=action->repeat;
    if(kui_cdda_pcm_seek(&pcm,action->frame)!=KUI_CDDA_PCM_OK ||
       !fill_half(0,false) || !fill_half(1,false) || kui_cdda_aica_start()!=KUI_CDDA_AICA_OK ||
       kui_cdda_aica_position(&session_initial)!=KUI_CDDA_AICA_OK ||
       (!action->repeat && session_initial>=session_frames) ||
       kui_cdda_ring_init(&ring,ticks(),session_initial)!=KUI_CDDA_RING_OK) goto failed;
    command_running=true;return true;
failed:
    (void)kui_cdda_aica_stop();cdda_storage_close();repeat_audio=false;
    return false;
}
static bool command_sync(void) {
    if(!command_running) return true;
    if(!observe() || ring.played>UINT32_MAX-session_initial) return false;
    enum kui_cdda_control_result result=kui_cdda_control_observe(&command_control,
        command_control.current.epoch,session_initial+ring.played);
    if(result!=KUI_CDDA_CONTROL_OK && result!=KUI_CDDA_CONTROL_ENDED) return false;
    if(command_control.current.loops>command_max_loops) command_max_loops=command_control.current.loops;
    if(result==KUI_CDDA_CONTROL_ENDED) {
        command_running=false;
        if(!session_close()) return false;
    }
    return true;
}
static bool command_finish(struct kui_cdda_control_action action) {
    /* Reject canceled/replayed callbacks before touching owned hardware. Use
     * the validated stored action rather than trusting a caller's copied data. */
    if(!command_control.current.pending || action.epoch!=command_control.current.pending_epoch)
        return false;
    action=command_control.action;
    bool success=true;
    if(command_running) {
        command_running=false;
        success=session_close();
    }
    if(success && action.target_state==KUI_CDDA_CONTROL_PLAYING)
        success=command_audio_start(&action);
    enum kui_cdda_control_result result=kui_cdda_control_complete(&command_control,action.epoch,success);
    if(!success || result!=KUI_CDDA_CONTROL_OK) {
        (void)kui_cdda_aica_stop();cdda_storage_close();command_running=false;return false;
    }
    /* Key-on and its first position observation are not instantaneous. Credit
     * that already latched actual position before exposing STATUS, rather than
     * temporarily reporting the requested origin after playback has advanced. */
    if(action.target_state==KUI_CDDA_CONTROL_PLAYING &&
       kui_cdda_control_observe(&command_control,action.epoch,session_initial)!=KUI_CDDA_CONTROL_OK) {
        (void)kui_cdda_control_fail(&command_control,action.epoch);
        (void)kui_cdda_aica_stop();cdda_storage_close();command_running=false;return false;
    }
    command_completions++;
    return true;
}
static bool command_accept(struct kui_cdda_control_request request) {
    if(!command_sync()) return false;
    struct kui_cdda_control_action action;
    if(kui_cdda_control_request(&command_control,&request,&action)!=KUI_CDDA_CONTROL_OK) return false;
    command_accepted++;
    return !action.pending || command_finish(action);
}
#if CDDA_TEST_PROFILE != 8
static bool command_play(uint32_t first,uint32_t end,bool repeat) {
    return command_accept((struct kui_cdda_control_request){KUI_CDDA_CONTROL_PLAY,first,end,0,repeat});
}
#endif
#if CDDA_TEST_PROFILE != 7 && CDDA_TEST_PROFILE != 8
static bool command_run(uint32_t additional_frames) {
    if(!command_running || !command_sync()) return false;
    if(!command_running) return command_control.current.state==KUI_CDDA_CONTROL_EOF;
    uint32_t target=command_control.last_played;
    if(additional_frames>UINT32_MAX-target) return false;
    target+=additional_frames;
    if(!command_control.current.repeat) {
        uint32_t available=command_control.current.end-command_control.origin_frame;
        if(target>available) target=available;
    }
    if(!session_pump(target,false)) return false;
    return command_sync();
}
#endif
#if CDDA_TEST_PROFILE != 8
static bool command_status(enum kui_cdda_control_state expected,
    struct kui_cdda_control_status *out) {
    if(!command_sync()) return false;
    struct kui_cdda_control_action action;
    struct kui_cdda_control_request request=command_request(KUI_CDDA_CONTROL_STATUS);
    if(kui_cdda_control_request(&command_control,&request,&action)!=KUI_CDDA_CONTROL_OK || action.pending ||
       kui_cdda_control_status(&command_control,out)!=KUI_CDDA_CONTROL_OK || out->state!=expected) return false;
    command_accepted++;command_status_checks++;
    return true;
}
#endif
#if CDDA_TEST_PROFILE == 5
static bool command_refuse(struct kui_cdda_control_request request,
    enum kui_cdda_control_result expected) {
    if(!command_sync()) return false;
    struct kui_cdda_control_status before,after;
    struct kui_cdda_control_action action;
    bool running=command_running;
    if(kui_cdda_control_status(&command_control,&before)!=KUI_CDDA_CONTROL_OK ||
       kui_cdda_control_request(&command_control,&request,&action)!=expected ||
       kui_cdda_control_status(&command_control,&after)!=KUI_CDDA_CONTROL_OK ||
       !command_same_status(&before,&after) || command_running!=running) return false;
    command_expected_refusals++;return true;
}
static bool command_stale(uint32_t epoch,bool observation) {
    struct kui_cdda_control_status before,after;
    if(kui_cdda_control_status(&command_control,&before)!=KUI_CDDA_CONTROL_OK) return false;
    if(!observation) {
        struct kui_cdda_control_action old={.epoch=epoch};
        if(command_finish(old)) return false;
    }
    enum kui_cdda_control_result result=observation?
        kui_cdda_control_observe(&command_control,epoch,111):
        kui_cdda_control_complete(&command_control,epoch,true);
    if(result!=KUI_CDDA_CONTROL_STALE ||
       kui_cdda_control_status(&command_control,&after)!=KUI_CDDA_CONTROL_OK ||
       !command_same_status(&before,&after)) return false;
    command_stale_refusals++;return true;
}
static bool command_cancel_cases(void) {
    struct kui_cdda_control_request request={KUI_CDDA_CONTROL_PLAY,0,3u*44100u,0,false};
    struct kui_cdda_control_action old,stop,newer;
    if(!command_accept(command_request(KUI_CDDA_CONTROL_STOP))) return false;
    if(kui_cdda_control_request(&command_control,&request,&old)!=KUI_CDDA_CONTROL_OK || !old.pending) return false;
    command_accepted++;
    struct kui_cdda_control_request halt=command_request(KUI_CDDA_CONTROL_STOP);
    if(kui_cdda_control_request(&command_control,&halt,&stop)!=KUI_CDDA_CONTROL_OK || !stop.pending ||
       stop.epoch<=old.epoch || !command_stale(old.epoch,false)) return false;
    command_accepted++;
    if(!command_finish(stop) || !command_stale(stop.epoch,false)) return false;
    if(kui_cdda_control_request(&command_control,&request,&old)!=KUI_CDDA_CONTROL_OK || !old.pending) return false;
    command_accepted++;
    request.first=TONE_FIRST;request.end=TONE_FIRST+TONE_FRAMES;
    if(kui_cdda_control_request(&command_control,&request,&newer)!=KUI_CDDA_CONTROL_OK || !newer.pending ||
       newer.epoch<=old.epoch || !command_stale(old.epoch,false)) return false;
    command_accepted++;
    if(!command_finish(newer) || !command_stale(old.epoch,true) || !command_run(11025u)) return false;
    return command_accept(command_request(KUI_CDDA_CONTROL_STOP));
}
static bool commands(void) {
    command_accepted=command_completions=command_status_checks=command_expected_refusals=0;
    command_stale_refusals=command_paused_frame=command_max_loops=0;command_running=false;
    uint32_t bytes;
    if(cdda_storage_open("0:/KUI/tests/cdda/stereo.raw",&bytes)) goto failed;
    cdda_storage_close();
    if(!bytes || bytes%2352u ||
       kui_cdda_control_init(&command_control,bytes/4u)!=KUI_CDDA_CONTROL_OK) goto failed;
    struct kui_cdda_control_status status,paused;
    struct kui_cdda_control_request request=command_request(KUI_CDDA_CONTROL_STOP);
    cdda_display_line("1/7: idempotent STOP, PLAY left, played STATUS");
    if(!command_accept(request) || !command_accept(request) || !command_play(0,9u*44100u,false) ||
       !command_run(44100u) || !command_status(KUI_CDDA_CONTROL_PLAYING,&status) ||
       status.frame==pcm.position || status.frame>=9u*44100u) goto failed;
    completed++;
    cdda_display_line("2/7: invalid SEEK preserved; SEEK right region");
    request=command_request(KUI_CDDA_CONTROL_SEEK);request.frame=9u*44100u+1u;
    if(!command_refuse(request,KUI_CDDA_CONTROL_INVALID)) goto failed;
    request.frame=3u*44100u+4410u;
    if(!command_accept(request) || !command_run(44100u)) goto failed;
    completed++;
    cdda_display_line("3/7: PAUSE twice, muted STATUS, actual RESUME");
    request=command_request(KUI_CDDA_CONTROL_PAUSE);
    if(!command_accept(request) || !command_status(KUI_CDDA_CONTROL_PAUSED,&paused)) goto failed;
    command_paused_frame=paused.frame;
    if(command_paused_frame==pcm.position || !command_accept(request) || !paused_silence() ||
       !command_status(KUI_CDDA_CONTROL_PAUSED,&status) || !command_same_status(&status,&paused) ||
       !command_accept(command_request(KUI_CDDA_CONTROL_RESUME)) || session_first!=command_paused_frame ||
       !command_run(44100u)) goto failed;
    completed++;
    cdda_display_line("4/7: paused SEEK/STATUS; stereo RESUME");
    if(!command_accept(command_request(KUI_CDDA_CONTROL_PAUSE))) goto failed;
    request=command_request(KUI_CDDA_CONTROL_SEEK);request.frame=TONE_FIRST;
    if(!command_accept(request) || !command_status(KUI_CDDA_CONTROL_PAUSED,&status) ||
       status.frame!=TONE_FIRST || !command_accept(command_request(KUI_CDDA_CONTROL_RESUME)) ||
       !command_run(44100u) || !command_accept(command_request(KUI_CDDA_CONTROL_STOP))) goto failed;
    completed++;
    cdda_display_line("5/7: EOF; LOOP enable, pause/resume, SEEK, off");
    if(!command_play(TONE_FIRST,TONE_FIRST+TONE_FRAMES,false) || !command_run(TONE_FRAMES) ||
       !command_status(KUI_CDDA_CONTROL_EOF,&status) || status.frame!=TONE_FIRST+TONE_FRAMES ||
       !command_refuse(command_request(KUI_CDDA_CONTROL_RESUME),KUI_CDDA_CONTROL_STATE) ||
       !command_play(TONE_FIRST,TONE_FIRST+TONE_FRAMES,false) || !command_run(8820u)) goto failed;
    request=command_request(KUI_CDDA_CONTROL_LOOP);request.repeat=true;
    if(!command_accept(request) || !command_run(3u*TONE_FRAMES) ||
       !command_status(KUI_CDDA_CONTROL_PLAYING,&status) || status.loops<3u || !status.repeat ||
       !command_accept(command_request(KUI_CDDA_CONTROL_PAUSE)) || !paused_silence() ||
       !command_status(KUI_CDDA_CONTROL_PAUSED,&status) || !status.repeat ||
       !command_accept(command_request(KUI_CDDA_CONTROL_RESUME)) || !command_run(TONE_FRAMES)) goto failed;
    request=command_request(KUI_CDDA_CONTROL_SEEK);request.frame=TONE_FIRST+12345u;
    if(!command_accept(request) || !command_run(TONE_FRAMES)) goto failed;
    request=command_request(KUI_CDDA_CONTROL_LOOP);request.repeat=false;
    if(!command_accept(request) || !command_run(TONE_FRAMES) || !command_status(KUI_CDDA_CONTROL_EOF,&status)) goto failed;
    completed++;
    cdda_display_line("6/7: canceled PLAY tokens; old completions refused");
    if(!command_cancel_cases()) goto failed;
    completed++;
    cdda_display_line("7/7: four rapid PLAY/STOP cycles; repeated STOP");
    for(unsigned i=0;i<4u;i++) {
        if(!command_play(TONE_FIRST,TONE_FIRST+TONE_FRAMES,false) || !command_run(11025u) ||
           !command_accept(command_request(KUI_CDDA_CONTROL_STOP)) ||
           !command_accept(command_request(KUI_CDDA_CONTROL_STOP)) ||
           !command_status(KUI_CDDA_CONTROL_STOPPED,&status)) goto failed;
    }
    if(command_expected_refusals!=2u || command_stale_refusals!=4u || command_max_loops<4u) goto failed;
    completed++;
    return true;
failed:
    cdda_display_line("Command/model test failed; owned audio stopped.");
    if(command_control.initialized) {
        uint32_t epoch=command_control.current.pending?
            command_control.current.pending_epoch:command_control.current.epoch;
        (void)kui_cdda_control_fail(&command_control,epoch);
    }
    if(command_running) (void)session_close();
    else {(void)kui_cdda_aica_stop();cdda_storage_close();}
    command_running=false;return false;
}
static void commands_report(void) {
    cdda_display_number("Accepted model commands: ",command_accepted);
    cdda_display_number("Completed command actions: ",command_completions);
    cdda_display_number("Checked STATUS snapshots: ",command_status_checks);
    cdda_display_number("EXPECTED invalid/state refusals: ",command_expected_refusals);
    cdda_display_number("EXPECTED stale token refusals: ",command_stale_refusals);
    cdda_display_number("Saved PAUSE played frame: ",command_paused_frame);
    cdda_display_number("Maximum played loop passes: ",command_max_loops);
}
#endif
