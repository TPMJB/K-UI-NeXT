#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the actual runtime selector/snapshots and UI publication fences.
In particular no automatic VMU history can race a ready game/boot handoff.
"""
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
main=(ROOT/'src/dreamcast/main.c').read_text()
shell=(ROOT/'src/core/shell.c').read_text()


def block(source,at):
    brace=source.index('{',at)
    depth=1
    end=brace+1
    while depth:
        if source[end]=='{':depth+=1
        elif source[end]=='}':depth-=1
        end+=1
    return source[at:end]


def function(source,name):
    match=re.search(r'(?m)^(?:static\s+)?(?:bool|void|unsigned|enum\s+\w+)\s+'+re.escape(name)+r'\s*\(',source)
    assert match, name+' definition missing'
    at=match.start()
    return block(source,at)


selector=block(main,main.index('if(!action && vmu_history_resume_needed'))
forced=block(main,main.index('if((action==58 || action==121 || action==122) && vmu_history_resume_needed'))
snapshot=block(main,main.rfind('if(',0,main.index('games_query_pending=shell.games_query')))
cancel_dispatch=block(main,main.index('if(kui_cancelled()\n#ifdef KUI_SD_RUNTIME'))
# This is the real early-cancel branch, not a simulated cleanup policy. Once
# selected, automatic job117 consumes its attempt: B must not re-arm the idle
# selector and immediately start the same filesystem operation again.
assert not re.search(r'\bvmu_history_resume_needed\s*(?:=|\|=|\^=)',cancel_dispatch), \
    'early worker cancellation must not re-arm automatic history job117'

prefix=r'''
#include "kui/shell.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
static bool vmu_history_resume_needed,startup_finished,splash_active,busy,cancel_requested;
static bool vmu_emulation_ok,vmu_emulation_enabled,boot_ready,probe_launch_ready,video_preview;
static unsigned pending,vmu_emulation_generation,worker_active_action,resumed;
static struct {bool requested;} sci_video_quiet;
static struct {bool parked;} salvage_session;
static struct kui_shell shell;
static struct kui_games_query games_query_pending;
static unsigned games_view_pending,games_epoch_pending,games_filter_pending;
static char games_jump_pending;
static struct {char root[KUI_DEST_ROOT_CAP],group[33];unsigned page,view,filter,step;
    struct kui_games_query query;bool valid;} games_prefetch;
static bool games_art_request_valid,games_art_done_valid;
static struct kui_shell_games_art_key games_art_requested,games_art_done;
static void vmu_history_previous_session(void){++resumed;}
bool kui_shell_games_art_same(const struct kui_shell_games_art_key *a,const struct kui_shell_games_art_key *b){
    return !memcmp(a,b,sizeof(*a));
}
static void reset_selector(void){
    vmu_history_resume_needed=startup_finished=vmu_emulation_ok=vmu_emulation_enabled=true;
    splash_active=busy=cancel_requested=boot_ready=probe_launch_ready=video_preview=false;
    sci_video_quiet.requested=salvage_session.parked=false;
    pending=worker_active_action=resumed=0;vmu_emulation_generation=1;
}
'''

suffix=r'''
#define BLOCKED(field,value) do {reset_selector();field=value;assert(select_history(0)==0u);assert(vmu_history_resume_needed && !worker_active_action);}while(0)
int main(void){
    reset_selector();assert(select_history(0)==120u && worker_active_action==120u && busy && !vmu_history_resume_needed);
    busy=false;assert(select_history(0)==0u); /* one-shot; no idle rerun */
    reset_selector();assert(select_history(58)==58u && !worker_active_action && vmu_history_resume_needed && !busy);
    BLOCKED(vmu_emulation_enabled,false); /* explicit Off must cause zero job/I/O */
    BLOCKED(vmu_emulation_generation,0); /* no preference result is not permission */
    BLOCKED(vmu_emulation_ok,false);
    BLOCKED(startup_finished,false);BLOCKED(splash_active,true);
    BLOCKED(busy,true);BLOCKED(pending,110u);BLOCKED(cancel_requested,true);
    BLOCKED(boot_ready,true);BLOCKED(probe_launch_ready,true);
    BLOCKED(video_preview,true);BLOCKED(sci_video_quiet.requested,true);BLOCKED(salvage_session.parked,true);
    reset_selector();vmu_history_resume_needed=false;assert(select_history(0)==0);
    reset_selector();prepare_history(58);assert(resumed==1 && !vmu_history_resume_needed);
    prepare_history(58);assert(resumed==1);
    reset_selector();prepare_history(121);assert(resumed==1 && !vmu_history_resume_needed);
    reset_selector();prepare_history(122);assert(resumed==1 && !vmu_history_resume_needed);
    reset_selector();vmu_emulation_enabled=false;prepare_history(58);assert(!resumed && vmu_history_resume_needed);
    reset_selector();vmu_emulation_generation=0;prepare_history(58);assert(!resumed);
    reset_selector();vmu_emulation_ok=false;prepare_history(58);assert(!resumed);
    reset_selector();prepare_history(104);assert(!resumed);
    memset(&shell,0,sizeof(shell));
    shell.games_view=KUI_GAMES_VIEW_LIST; shell.games_generation=19;shell.games_filter=1;shell.games_jump='S';
    strcpy(shell.games_query.text,"Skies");shell.games_query.region=KUI_GAMES_REGION_USA;shell.games_query.disc=2;
    snapshot_query(54);
    assert(!strcmp(games_query_pending.text,"Skies") && games_query_pending.activity==1 && games_query_pending.disc==2);
    assert(games_epoch_pending==19 && games_view_pending==KUI_GAMES_VIEW_LIST && games_filter_pending==1 && games_jump_pending=='S');
    strcpy(shell.games_query.text,"Grandia");shell.games_query.disc=1;shell.games_generation=20;
    assert(!strcmp(games_query_pending.text,"Skies") && games_query_pending.disc==2 && games_epoch_pending==19);
    snapshot_query(112);assert(!strcmp(games_query_pending.text,"Skies"));
    snapshot_query(114);assert(!strcmp(games_query_pending.text,"Grandia") && games_query_pending.disc==1 && games_epoch_pending==20);
    shell.page=KUI_SHELL_GAMES;shell.games_page=2;strcpy(shell.games_path,"/Games");strcpy(shell.games_listing.root,"/Games");
    memset(&games_prefetch,0,sizeof(games_prefetch));games_prefetch.valid=true;games_prefetch.page=2;
    games_prefetch.view=KUI_GAMES_VIEW_LIST;games_prefetch.filter=1;games_prefetch.query=shell.games_query;
    strcpy(games_prefetch.root,"/Games");assert(games_prefetch_current());
    strcpy(shell.games_query.text,"Sonic");assert(!games_prefetch_current());
    shell.games_query=games_prefetch.query;shell.games_query.region=KUI_GAMES_REGION_JAPAN;assert(!games_prefetch_current());
    shell.games_query=games_prefetch.query;shell.games_query.disc=2;assert(!games_prefetch_current());
    shell.games_query=games_prefetch.query;assert(games_prefetch_current());
    /* Old worker replies must not populate a new/closed edit screen. */
    struct kui_games_library_edit edit={0};strcpy(edit.alias,"new alias");
    shell.page=KUI_SHELL_GAMES_EDIT;shell.games_edit_loading=true;shell.games_edit_generation=5;
    strcpy(shell.games_edit_path,"/Games/A/disc.gdi");strcpy(shell.games_edit.alias,"old alias");
    kui_shell_set_games_edit(&shell,4,shell.games_edit_path,&edit,true,"old result");assert(shell.games_edit_loading && !strcmp(shell.games_edit.alias,"old alias"));
    kui_shell_set_games_edit(&shell,5,"/Games/B/disc.gdi",&edit,true,"wrong target");assert(shell.games_edit_loading);
    shell.page=KUI_SHELL_GAMES;kui_shell_set_games_edit(&shell,5,shell.games_edit_path,&edit,true,"closed");assert(shell.games_edit_loading);
    shell.page=KUI_SHELL_GAMES_EDIT;kui_shell_set_games_edit(&shell,5,shell.games_edit_path,&edit,true,"saved");
    assert(!shell.games_edit_loading && !strcmp(shell.games_edit.alias,"new alias"));
    struct kui_vmu_history_request request={0};request.card.pair=3;
    struct kui_vmu_history_view view={0};view.known=true;view.restored=true;view.card.pair=77;
    shell.page=KUI_SHELL_VMU_HISTORY;shell.vmu_history_loading=true;shell.vmu_history_generation=11;shell.vmu_history_request=request;
    kui_shell_set_vmu_history(&shell,10,&request,&view);assert(shell.vmu_history_loading && !shell.games_vmu_selection.pair);
    request.sequence=1;kui_shell_set_vmu_history(&shell,11,&request,&view);assert(shell.vmu_history_loading);
    request.sequence=0;shell.page=KUI_SHELL_GAMES;kui_shell_set_vmu_history(&shell,11,&request,&view);assert(shell.vmu_history_loading);
    shell.page=KUI_SHELL_VMU_HISTORY;kui_shell_set_vmu_history(&shell,11,&request,&view);
    assert(!shell.vmu_history_loading && shell.games_vmu_selection.pair==77);
    puts("PASS frontend job fences: actual automatic120/launch gates, VMUOff/no prefs, immutable query, stale prefetch/edit/history results");
    return 0;
}
'''

fragments='\n\n'.join((
    'static unsigned select_history(unsigned action){'+selector+'return action;}',
    'static void prepare_history(unsigned action){'+forced+'}',
    'static void snapshot_query(unsigned action){'+snapshot+'}',
    function(main,'games_query_same'),function(main,'games_prefetch_current'),
    function(shell,'kui_shell_games_view'),function(shell,'kui_shell_set_games_edit'),
    function(shell,'kui_shell_set_vmu_history')))
with tempfile.TemporaryDirectory(prefix='kui-frontend-fences-') as tmp:
    c=Path(tmp)/'fences.c';binary=Path(tmp)/'fences';c.write_text(prefix+fragments+suffix)
    subprocess.run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wpedantic',
        '-fsanitize=address,undefined','-fno-omit-frame-pointer','-Iinclude','-I.deps/fatfs/source',
        '-Ifirmware/kui-wifi/components/kwlink/include',str(c),'-o',str(binary)],cwd=ROOT,check=True)
    subprocess.run([str(binary)],cwd=ROOT,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'),check=True)
