#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute Games layout's actual coalescing worker and UI publication code.

Preference I/O can yield to controller input or a foreground job. The host
harness checks the production request/snapshot/result state at those yields.
"""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'src/dreamcast/main.c').read_text()
shell_source = (ROOT / 'src/core/shell.c').read_text()


def block(text, at):
    brace = text.index('{', at)
    depth, end = 1, brace + 1
    while depth:
        if text[end] == '{':
            depth += 1
        elif text[end] == '}':
            depth -= 1
        end += 1
    return text[at:end]


def function(text, name):
    definition = re.search(r'^[A-Za-z_][A-Za-z_0-9 \t*]*\b' + re.escape(name) +
                           r'\s*\([^;{}]*\)\s*\{', text, re.MULTILINE)
    assert definition, name
    return block(text, definition.start())


start = source.index('static struct {unsigned generation,view;bool valid;} games_layout_request')
globals_source = source[start:source.index('/* Flow neighbors', start)]
publication = block(source, source.index('if(games_layout_result_ready) {'))
scheduler = block(source, source.index('if(idle && !games_layout_idle_work() && !games_flow_idle_work()'))
update_at = source.index('games_layout_request_update(requested);', source.index('int main(void)'))
publish_at = source.index('if(games_layout_result_ready) {', source.index('int main(void)'))
for at in (update_at, publish_at):
    assert source.rfind('mutex_lock(&lock);', 0, at) > source.rfind('mutex_unlock(&lock);', 0, at)
worker = function(source, 'games_layout_idle_work')
assert 'kui_shell_set_games_layout_saved' not in worker
assert not re.search(r'\bbusy\s*=(?!=)', worker)

prefix = r'''
#include "kui/shell.h"
#include "kui/storage.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct kui_shell shell;
static int lock;
static unsigned held,saves,sets,acks,flow_reads,art_reads,other_idle;
static unsigned saved_generation,saved_view;
static bool busy,cancel_requested,video_preview,games_cache_clear_pending;
static bool startup_finished,splash_active,boot_ready,probe_launch_ready;
static unsigned pending;
static struct {bool requested;} sci_video_quiet;
static bool cd_owned,save_ok,games_ui_changed;
enum change {SAME,NEW_LR,LEGACY_A,BUSY,PENDING,STOP,VIDEO,QUIET,CACHE,
             STARTUP,SPLASH,BOOT,PROBE,CHANGE_COUNT};
static enum change change_during_save;
static bool change_after_write;
static void games_layout_request_update(enum kui_shell_action requested);
static void mutate(enum change change);
static void mutex_lock(int *value){assert(value==&lock && !held);held=1u;}
static void mutex_unlock(int *value){assert(value==&lock && held);held=0u;}
bool kui_cd_audio_owns_drive(void){assert(!held);return cd_owned;}
static void kui_sd_set_params(unsigned transport,bool crc){assert(!held && transport==KUI_STORAGE_AUTO && crc);}
static void kui_log(const char *format,...){(void)format;assert(!held);}
static bool games_flow_idle_work(void){++flow_reads;return false;}
static bool games_art_idle_work(void){++art_reads;return false;}
static bool games_selection_store(bool force){assert(!force);++other_idle;return false;}
static void music_idle_work(void){++other_idle;}
static void games_warm_idle_work(void){++other_idle;}
static bool games_prefetch_idle_work(void){++other_idle;return false;}
'''

stubs = r'''
bool kui_games_library_save_view(unsigned view,kui_log_fn log,kui_cancel_fn cancel){
    assert(!held && !busy && !pending && log && cancel && !games_layout_result_ready);
    const unsigned generation=games_layout_running.generation;
    assert(games_layout_running.valid && view==games_layout_running.view);
    ++saves;
    if(!change_after_write)mutate(change_during_save);
    /* New input must not edit the in-flight worker snapshot. */
    assert(games_layout_running.generation==generation && games_layout_running.view==view);
    if(cancel())return false;
    if(save_ok){saved_generation=generation;saved_view=view;}
    if(change_after_write)mutate(change_during_save);
    assert(games_layout_running.generation==generation && games_layout_running.view==view);
    return save_ok && !cancel();
}
'''

suffix = r'''
bool kui_shell_set_games_layout_saved(struct kui_shell *s,unsigned generation,unsigned view,
        bool ok,const char *notice){
    assert(held && s==&shell);++sets;
    bool accepted=actual_set_games_layout_saved(s,generation,view,ok,notice);
    if(accepted)++acks;
    return accepted;
}
static void queue(unsigned generation,unsigned view,bool local){
    mutex_lock(&lock);
    shell.games_layout_generation=generation;shell.games_view=view;shell.games_layout_saving=local;
    games_layout_request_update(local?KUI_SHELL_GAMES_LAYOUT_SAVE:KUI_SHELL_GAMES_LIST);
    mutex_unlock(&lock);
}
static void mutate(enum change change){
    switch(change){
    case SAME:break;
    case NEW_LR:queue(shell.games_layout_generation+1u,(shell.games_view+1u)%KUI_GAMES_VIEW_COUNT,true);break;
    case LEGACY_A:queue(shell.games_layout_generation+1u,(shell.games_view+1u)%KUI_GAMES_VIEW_COUNT,false);break;
    case BUSY:busy=true;break;
    case PENDING:pending=54u;break;
    case STOP:cancel_requested=true;break;
    case VIDEO:video_preview=true;break;
    case QUIET:sci_video_quiet.requested=true;break;
    case CACHE:games_cache_clear_pending=true;break;
    case STARTUP:startup_finished=false;break;
    case SPLASH:splash_active=true;break;
    case BOOT:boot_ready=true;break;
    case PROBE:probe_launch_ready=true;break;
    default:assert(false);
    }
}
static void clear_fences(void){
    busy=cancel_requested=video_preview=games_cache_clear_pending=false;
    splash_active=boot_ready=probe_launch_ready=sci_video_quiet.requested=false;
    startup_finished=true;pending=0u;
}
static void reset(void){
    memset(&shell,0,sizeof(shell));memset(&games_layout_request,0,sizeof(games_layout_request));
    memset(&games_layout_running,0,sizeof(games_layout_running));
    held=saves=sets=acks=flow_reads=art_reads=other_idle=saved_generation=saved_view=0u;
    games_layout_seen=games_layout_result_generation=games_layout_result_view=0u;
    games_layout_result_ready=games_layout_result_ok=games_ui_changed=cd_owned=false;
    save_ok=true;change_during_save=SAME;change_after_write=false;clear_fences();
    shell.page=KUI_SHELL_GAMES_ADVANCED;
}
static void publish(void){mutex_lock(&lock);apply_layout_result();mutex_unlock(&lock);}
int main(void){
    reset();
    assert(worker_action(KUI_SHELL_GAMES_LAYOUT_SAVE)==0u); /* no foreground/busy job */
    assert(worker_action(KUI_SHELL_GAMES_LIST)==54u);
    assert(!games_layout_idle_work() && !saves);
    /* All LR input before worker dispatch collapses into one latest request. */
    queue(1u,KUI_GAMES_VIEW_COMPACT,true);queue(2u,KUI_GAMES_VIEW_GALLERY,true);
    queue(3u,KUI_GAMES_VIEW_FLOW,true);
    mutex_lock(&lock);games_layout_request_update(KUI_SHELL_NONE);mutex_unlock(&lock);
    idle_scheduler(true);
    assert(saves==1u && saved_generation==3u && saved_view==KUI_GAMES_VIEW_FLOW);
    assert(!busy && !pending && !sets && !acks && games_layout_result_ready && !games_layout_running.valid);
    assert(!flow_reads && !art_reads && !other_idle && !games_layout_request.valid);
    assert(shell.games_layout_saving);publish();
    assert(sets==1u && acks==1u && games_ui_changed && !shell.games_layout_saving);
    assert(!strcmp(shell.games_layout_notice,"Layout saved.") && !games_layout_result_ready);
    assert(!games_layout_idle_work() && saves==1u);
    /* A ready old acknowledgement cannot consume or overwrite a newer request. */
    reset();queue(1u,KUI_GAMES_VIEW_COMPACT,true);assert(games_layout_idle_work());
    queue(2u,KUI_GAMES_VIEW_FLOW,true);
    assert(!games_layout_idle_work() && saves==1u && games_layout_result_generation==1u);
    publish();assert(sets==1u && !acks && !games_ui_changed && shell.games_layout_saving);
    assert(games_layout_request.valid && games_layout_request.generation==2u);
    assert(games_layout_idle_work() && saves==2u && saved_generation==2u);publish();
    assert(acks==1u && !shell.games_layout_saving);
    /* An LR received during either side of I/O remains queued; running stays frozen. */
    for(unsigned after=0u;after<2u;after++){
        reset();queue(7u,KUI_GAMES_VIEW_COMPACT,true);
        change_during_save=NEW_LR;change_after_write=after!=0u;
        assert(games_layout_idle_work() && saves==1u && !games_layout_result_ready && !sets);
        assert(games_layout_request.valid && games_layout_request.generation==8u &&
               games_layout_request.view==KUI_GAMES_VIEW_GALLERY && !games_layout_running.valid);
        assert(saved_generation==(after?7u:0u));
        change_during_save=SAME;assert(games_layout_idle_work() && saves==2u && saved_generation==8u);
        publish();assert(acks==1u && !shell.games_layout_saving);
    }
    /* Legacy A retires old queued LR hints and suppresses older replies/writes. */
    reset();queue(1u,KUI_GAMES_VIEW_COMPACT,true);queue(2u,KUI_GAMES_VIEW_GALLERY,false);
    assert(!games_layout_request.valid && !games_layout_idle_work() && !saves);
    for(unsigned after=0u;after<2u;after++){
        reset();queue(3u,KUI_GAMES_VIEW_COMPACT,true);
        change_during_save=LEGACY_A;change_after_write=after!=0u;
        assert(games_layout_idle_work() && saves==1u && !games_layout_result_ready);
        assert(!games_layout_request.valid && !shell.games_layout_saving && shell.games_layout_generation==4u);
        assert(!games_layout_idle_work() && saves==1u);
    }
    reset();queue(1u,KUI_GAMES_VIEW_COMPACT,true);assert(games_layout_idle_work());
    queue(2u,KUI_GAMES_VIEW_GALLERY,false);publish();
    assert(sets==1u && !acks && !games_ui_changed && !games_layout_request.valid && !shell.games_layout_saving);
    /* Foreground, drive ownership and quiet states defer I/O without losing the request. */
    for(unsigned change=BUSY;change<CHANGE_COUNT;change++){
        reset();queue(1u,KUI_GAMES_VIEW_FLOW,true);mutate((enum change)change);
        assert(!games_layout_idle_work() && !saves && games_layout_request.valid);
        clear_fences();assert(games_layout_idle_work() && saves==1u);publish();assert(acks==1u);
    }
    reset();queue(1u,KUI_GAMES_VIEW_FLOW,true);cd_owned=true;
    assert(!games_layout_idle_work() && !saves && games_layout_request.valid);
    cd_owned=false;assert(games_layout_idle_work() && saves==1u);
    /* A foreground request that arrives during I/O cancels safely and retries on idle. */
    for(unsigned change=BUSY;change<CHANGE_COUNT;change++)for(unsigned after=0u;after<2u;after++){
        reset();queue(9u,KUI_GAMES_VIEW_FLOW,true);
        change_during_save=(enum change)change;change_after_write=after!=0u;
        assert(games_layout_idle_work() && saves==1u && !games_layout_result_ready && !sets);
        assert(games_layout_request.valid && shell.games_layout_saving && !games_layout_running.valid);
        clear_fences();change_during_save=SAME;
        assert(games_layout_idle_work() && saves==2u && saved_generation==9u);publish();assert(acks==1u);
    }
    /* A confirmed failure has one visible completion and no retry storm. */
    reset();queue(1u,KUI_GAMES_VIEW_FLOW,true);save_ok=false;
    assert(games_layout_idle_work() && games_layout_result_ready && !games_layout_result_ok);
    publish();assert(acks==1u && !shell.games_layout_saving && !games_layout_request.valid);
    assert(strstr(shell.games_layout_notice,"not confirmed") && shell.games_view==KUI_GAMES_VIEW_FLOW);
    assert(!games_layout_idle_work() && saves==1u);
    queue(2u,KUI_GAMES_VIEW_FLOW,true);save_ok=true;
    assert(games_layout_idle_work() && saves==2u);publish();assert(acks==2u);
    reset();idle_scheduler(false);assert(!saves && !flow_reads && !art_reads && !other_idle);
    reset();idle_scheduler(true);assert(!saves && flow_reads==1u && art_reads==1u && other_idle==4u);
    puts("PASS Games layout dispatch: actual LR coalescing, immutable snapshots, generation/legacy-A fences, foreground priority/cancel retry, main-only publication and retained latest request");
    return 0;
}
'''

setter = function(shell_source, 'kui_shell_set_games_layout_saved').replace(
    'bool kui_shell_set_games_layout_saved(', 'static bool actual_set_games_layout_saved(', 1)
helpers = '\n\n'.join((
    function(source, 'games_idle_cancelled'),
    function(source, 'games_layout_request_update'),
    function(source, 'games_layout_current'),
    function(source, 'games_layout_cancelled'),
    function(source, 'games_layout_idle_work'),
    function(source, 'worker_action'),
    'static void idle_scheduler(bool idle){' + scheduler + '}',
))
publication_function = 'static void apply_layout_result(void){' + publication + '}'
# The actual setter is wrapped solely to assert main-thread lock ownership.
split = suffix.index('static void queue(')
program = prefix + globals_source + stubs + setter + helpers + suffix[:split] + \
    publication_function + suffix[split:]
with tempfile.TemporaryDirectory(prefix='kui-layout-dispatch-') as tmp:
    c = Path(tmp) / 'layout.c'
    binary = Path(tmp) / 'layout'
    c.write_text(program)
    subprocess.run([*shlex.split(os.environ.get('CC', 'cc')), '-std=c11', '-O1', '-g',
                    '-Wall', '-Wextra', '-Werror', '-Wpedantic', '-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer', '-fno-pie', '-no-pie', '-Iinclude', '-I.deps/fatfs/source',
                    '-Ifirmware/kui-wifi/components/kwlink/include', str(c), '-o', str(binary)], cwd=ROOT, check=True)
    subprocess.run([str(binary)], cwd=ROOT, check=True, env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='halt_on_error=1'))
