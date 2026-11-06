#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the actual two child-job mappings, snapshots and prepare dispatch."""
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
source=(ROOT/'src/dreamcast/main.c').read_text()


def block(at):
    brace=source.index('{',at);depth=1;end=brace+1
    while depth:
        if source[end]=='{':depth+=1
        elif source[end]=='}':depth-=1
        end+=1
    return source[at:end]


def function(name):
    match=re.search(r'(?m)^static (?:bool|unsigned) '+re.escape(name)+r'\(',source)
    assert match,name
    return block(match.start())


snapshot=block(source.index('if(action==54 || action==55 || action==57 || action==58 || action==79'))
override=block(source.index('if(action==121 || action==122) {'))
at=source.index('if(action==58 || action==121 || action==122)snprintf(games_activity_root_pending')
activity=source[at:source.index(';',at)+1]
at=source.index('bool prepared=action==121 || action==122?')
prepare=source[at:source.index('\n                if(prepared',at)]

prefix=r'''
#include "kui/shell.h"
#include "kui/games_retail.h"
#include "kui/smash_native_child.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct kui_shell shell;
static char games_path_pending[KUI_GAMES_FILE_CAP],games_activity_root_pending[KUI_DEST_ROOT_CAP];
static unsigned games_offset_pending,games_reader_pending,calls,prepared_action,prepared_child;
static struct kui_game_ram_patches games_patches_pending;
static struct kui_vmu_game_selection games_vmu_selection_pending;
static struct kui_runtime_image probe_image;
static bool disc_vmu_launch_pending;
static void kui_log(const char *format,...) {(void)format;}
static bool kui_cancelled(void) {return false;}
static bool other_prepare(unsigned action) {++calls;prepared_action=action;return true;}
bool kui_games_smash_native_prepare(unsigned child,const char *path,uint32_t reader,
    const struct kui_game_ram_patches *patches,const struct kui_vmu_game_selection *selection,
    const struct kui_native_session_request *session,struct kui_runtime_image *image,
    kui_log_fn log,kui_cancel_fn cancel) {
    ++calls;prepared_action=121u+child-KUI_SMASH_NATIVE_CHILD_PHANTASY_STAR_II;prepared_child=child;
    assert(path==games_path_pending && !strcmp(path,"/Games/Smash/disc.gdi"));
    assert(reader==KUI_RETAIL_READER_STANDARD && patches==&games_patches_pending);
    struct kui_game_ram_patches zero={0};assert(!memcmp(patches,&zero,sizeof(zero)));
    assert(selection==&games_vmu_selection_pending && !selection->selected && !selection->pair);
    assert(!session && image==&probe_image && log==kui_log && cancel==kui_cancelled);
    return true;
}
#define kui_disc_launcher_prepare_vmu(...) other_prepare(104u)
#define kui_disc_launcher_prepare(...) other_prepare(104u)
#define kui_games_retail_prepare_launch_session_card(...) other_prepare(58u)
#define kui_games_image_probe_prepare(...) other_prepare(57u)
#define kui_games_probe_prepare(...) other_prepare(56u)
'''

suffix=r'''
int main(void) {
    assert(worker_action(KUI_SHELL_GAMES_SMASH_PHANTASY_STAR_II)==121u);
    assert(worker_action(KUI_SHELL_GAMES_SMASH_SHINING_FORCE)==122u);
    assert(worker_action(KUI_SHELL_GAMES_RETAIL)==58u);
    assert(worker_action(KUI_SHELL_GAMES_PROBE)==56u && worker_action(KUI_SHELL_GAMES_IMAGE_PROBE)==57u);
    assert(needs_cd_handoff(121u) && needs_cd_handoff(122u));
    for(unsigned action=121u;action<=122u;action++) {
        memset(&shell,0,sizeof(shell));strcpy(shell.games_selected_path,"/Games/Smash/disc.gdi");
        strcpy(shell.games_path,"/Games");shell.games_retail_reader=KUI_RETAIL_READER_ASYNC;
        shell.games_vmu_selection.selected=true;shell.games_vmu_selection.pair=27u;
        snapshot(action);
        assert(!strcmp(games_path_pending,shell.games_selected_path) && !strcmp(games_activity_root_pending,"/Games"));
        assert(games_reader_pending==KUI_RETAIL_READER_STANDARD && !games_vmu_selection_pending.selected);
        assert(shell.games_retail_reader==KUI_RETAIL_READER_ASYNC && shell.games_vmu_selection.pair==27u);
        strcpy(shell.games_selected_path,"/Games/Later/disc.gdi"); /* no mutable GUI path reaches the worker */
        assert(prepare_job(action) && prepared_action==action);
        assert(prepared_child==(action==121u?KUI_SMASH_NATIVE_CHILD_PHANTASY_STAR_II:KUI_SMASH_NATIVE_CHILD_SHINING_FORCE));
    }
    assert(calls==2u && prepare_job(58u) && prepared_action==58u);
    assert(prepare_job(56u) && prepared_action==56u);
    assert(prepare_job(57u) && prepared_action==57u);
    puts("PASS Smash child dispatch: explicit121/122, immutable original image path, fixed temporary launch options, exact child enum, ordinary58/56/57 unchanged");
    return 0;
}
'''

fragments='\n\n'.join((function('worker_action'),function('needs_cd_handoff'),
    'static void snapshot(unsigned action){'+snapshot+override+activity+'}',
    'static bool prepare_job(unsigned action){bool session_ready=false;struct kui_native_session_request session_request={0};'+prepare+'return prepared;}'))
with tempfile.TemporaryDirectory(prefix='kui-smash-child-dispatch-') as tmp:
    c=Path(tmp)/'dispatch.c';binary=Path(tmp)/'dispatch';c.write_text(prefix+fragments+suffix)
    subprocess.run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wpedantic',
        '-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-pie','-no-pie',
        '-Iinclude','-I.deps/fatfs/source','-Ifirmware/kui-wifi/components/kwlink/include',
        str(c),'-o',str(binary)],cwd=ROOT,check=True)
    subprocess.run([str(binary)],cwd=ROOT,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'),check=True)
