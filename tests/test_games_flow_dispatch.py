#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute Flow's actual worker helpers and main publication boundary.

Catalogue/title I/O is stubbed so a UI/context change can occur at each
cooperative yield. The C being scheduled and tested comes from main.c.
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


start = source.index('static struct kui_games_page games_flow_rows;')
globals_source = source[start:source.index('static struct kui_games_entry games_art_entry', start)]
publication = block(source, source.index('if(games_flow_ready) {'))
scheduler = block(source, source.index('if(idle && !games_layout_idle_work() && !games_flow_idle_work() && !games_art_idle_work()'))
assert 'kui_shell_set_games_flow_rows' not in function(source, 'games_flow_idle_work')
assert source.index('kui_shell_games_flow_tick(&shell,input_at)') < source.index(
    'kui_shell_input(&shell', source.index('kui_shell_games_flow_tick(&shell,input_at)'))

prefix = r'''
#include "kui/shell.h"
#include "kui/storage.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
static struct kui_shell shell;
static int lock;
static unsigned held,reads,title_reads,sets,art_reads,other_idle;
static bool busy,cancel_requested,video_preview,games_cache_clear_pending;
static bool startup_finished,splash_active,boot_ready,probe_launch_ready;
static unsigned pending;
static struct {bool requested;} sci_video_quiet;
static bool cd_owned,needed,read_ok,prefetch_ok,games_ui_changed;
static unsigned needed_page,mutate_at;
static struct kui_games_page consumed;
enum change {
    SAME,PAGE,ORIGIN,EPOCH,QUERY_TEXT,QUERY_REGION,QUERY_DISC,QUERY_ACTIVITY,
    FILTER,ROOT_PATH,GROUP,LOADING,SCANNING,VIEW,BUSY,PENDING,STOP,VIDEO,
    QUIET,BOOT,PROBE,CACHE,SPLASH,STARTUP,CHANGE_COUNT
};
static enum change change_on_read;
static void mutex_lock(int *value){assert(value==&lock && !held);held=1u;}
static void mutex_unlock(int *value){assert(value==&lock && held);held=0u;}
bool kui_cd_audio_owns_drive(void){assert(!held);return cd_owned;}
static void kui_sd_set_params(unsigned transport,bool crc){assert(!held && transport==KUI_STORAGE_AUTO && crc);}
static void kui_log(const char *format,...){(void)format;assert(!held);}
static void mutate(enum change change){
    switch(change){
    case SAME:break;
    case PAGE:shell.page=KUI_SHELL_HOME;break;
    case ORIGIN:++shell.games_page;break;
    case EPOCH:++shell.games_flow_epoch;break;
    case QUERY_TEXT:strcpy(shell.games_query.text,"changed");break;
    case QUERY_REGION:shell.games_query.region=KUI_GAMES_REGION_JAPAN;break;
    case QUERY_DISC:shell.games_query.disc=2u;break;
    case QUERY_ACTIVITY:shell.games_query.activity=KUI_GAMES_FILTER_RECENT;break;
    case FILTER:shell.games_filter=KUI_GAMES_FILTER_RECENT;break;
    case ROOT_PATH:strcpy(shell.games_path,"/Other");break;
    case GROUP:strcpy(shell.games_group,"different-group");break;
    case LOADING:shell.games_loading=true;break;
    case SCANNING:shell.games_scanning=true;break;
    case VIEW:shell.games_view=KUI_GAMES_VIEW_LIST;break;
    case BUSY:busy=true;break;
    case PENDING:pending=58u;break;
    case STOP:cancel_requested=true;break;
    case VIDEO:video_preview=true;break;
    case QUIET:sci_video_quiet.requested=true;break;
    case BOOT:boot_ready=true;break;
    case PROBE:probe_launch_ready=true;break;
    case CACHE:games_cache_clear_pending=true;break;
    case SPLASH:splash_active=true;break;
    case STARTUP:startup_finished=false;break;
    default:assert(false);
    }
}
bool kui_shell_games_flow_needed(const struct kui_shell *s,unsigned *page,unsigned *epoch){
    assert(held && s==&shell && page && epoch);
    if(!needed || s->page!=KUI_SHELL_GAMES || s->games_loading || s->games_scanning ||
       s->games_view!=KUI_GAMES_VIEW_FLOW)return false;
    *page=needed_page;*epoch=s->games_flow_epoch;return true;
}
bool kui_games_library_rows_query(const char *root,const char *group,unsigned offset,unsigned view,
    const struct kui_games_query *query,struct kui_games_page *out,kui_log_fn log,kui_cancel_fn cancel){
    assert(!held && root && group && query && log && cancel);
    assert(out==&games_flow_rows && view==KUI_GAMES_VIEW_FLOW && offset==needed_page*KUI_GAMES_ROWS);
    assert(!strcmp(root,games_flow_request.root) && !strcmp(group,games_flow_request.group));
    assert(!memcmp(query,&games_flow_request.query,sizeof(*query)));
    ++reads;
    if(cancel())return false;
    memset(out,0,sizeof(*out));out->count=1u;out->view=view;out->query=*query;
    snprintf(out->entries[0].title,sizeof(out->entries[0].title),"read-%u",reads);
    if(reads==mutate_at)mutate(change_on_read);
    return read_ok && !cancel();
}
bool kui_games_prefetch_titles(const struct kui_games_page *rows,kui_log_fn log,kui_cancel_fn cancel){
    assert(!held && rows==&games_flow_rows && log && cancel);++title_reads;
    return prefetch_ok && !cancel();
}
bool kui_shell_set_games_flow_rows(struct kui_shell *s,const struct kui_games_page *rows,unsigned epoch,unsigned page){
    assert(held && s==&shell && rows==&games_flow_rows && epoch==games_flow_result_epoch && page==games_flow_result_page);
    ++sets;
    if(s->games_flow_epoch!=epoch)return false;
    consumed=*rows;needed=false;return true;
}
static bool games_layout_idle_work(void){return false;}
static bool games_art_idle_work(void){++art_reads;return false;}
static bool games_selection_store(bool force){assert(!force);++other_idle;return false;}
static void music_idle_work(void){++other_idle;}
static void games_warm_idle_work(void){++other_idle;}
static bool games_prefetch_idle_work(void){++other_idle;return false;}
'''

suffix = r'''
static void reset(void){
    memset(&shell,0,sizeof(shell));memset(&games_flow_request,0,sizeof(games_flow_request));
    memset(&games_flow_rows,0,sizeof(games_flow_rows));memset(&consumed,0,sizeof(consumed));
    shell.page=KUI_SHELL_GAMES;shell.games_page=3u;shell.games_view=KUI_GAMES_VIEW_FLOW;shell.games_flow_epoch=17u;
    strcpy(shell.games_path,"/Games");strcpy(shell.games_listing.root,"/Games");
    strcpy(shell.games_query.text,"Skies");shell.games_query.region=KUI_GAMES_REGION_USA;shell.games_query.disc=1u;
    shell.games_filter=shell.games_query.activity=KUI_GAMES_FILTER_FAVORITES;
    held=reads=title_reads=sets=art_reads=other_idle=pending=0u;
    busy=cancel_requested=video_preview=games_cache_clear_pending=splash_active=boot_ready=probe_launch_ready=false;
    sci_video_quiet.requested=cd_owned=games_flow_ready=games_ui_changed=false;
    startup_finished=needed=read_ok=prefetch_ok=true;needed_page=4u;mutate_at=0u;change_on_read=SAME;
    games_flow_result_page=games_flow_result_epoch=0u;
}
static void capture_context(void){
    strcpy(games_flow_request.root,shell.games_path);strcpy(games_flow_request.group,shell.games_group);
    games_flow_request.query=shell.games_query;games_flow_request.origin=shell.games_page;
    games_flow_request.page=needed_page;games_flow_request.epoch=shell.games_flow_epoch;games_flow_request.valid=true;
}
static void publish(void){mutex_lock(&lock);apply_flow_result();mutex_unlock(&lock);}
int main(void){
    reset();capture_context();assert(games_flow_current() && !games_flow_cancelled());
    for(unsigned change=PAGE;change<=VIEW;change++){
        reset();capture_context();mutate((enum change)change);
        assert(!games_flow_current() && games_flow_cancelled());
    }
    for(unsigned change=BUSY;change<CHANGE_COUNT;change++){
        reset();capture_context();mutate((enum change)change);assert(games_flow_cancelled());
        assert(!games_flow_idle_work() && !reads && !games_flow_ready);
    }
    reset();cd_owned=true;assert(!games_flow_idle_work() && !reads);
    reset();needed=false;assert(!games_flow_idle_work() && !reads);
    reset();idle_scheduler(true);
    assert(reads==2u && title_reads==1u && games_flow_ready && !sets && !art_reads && !other_idle);
    assert(games_flow_result_page==4u && games_flow_result_epoch==17u && !strcmp(games_flow_rows.entries[0].title,"read-2"));
    struct kui_games_page frozen=games_flow_rows;
    assert(!games_flow_idle_work() && reads==2u && !memcmp(&frozen,&games_flow_rows,sizeof(frozen)));
    /* Even a new viewport must wait for main to consume the existing buffer. */
    ++shell.games_flow_epoch;assert(!games_flow_idle_work() && reads==2u);
    publish();assert(sets==1u && !games_flow_ready && !games_ui_changed);
    reset();assert(games_flow_idle_work());publish();
    assert(sets==1u && games_ui_changed && !games_flow_ready && !needed && !memcmp(&consumed,&games_flow_rows,sizeof(consumed)));
    assert(!games_flow_idle_work() && reads==2u);
    for(unsigned change=PAGE;change<CHANGE_COUNT;change++){
        reset();change_on_read=(enum change)change;mutate_at=1u;
        assert(games_flow_idle_work() && reads==1u && !games_flow_ready && !sets);
        reset();change_on_read=(enum change)change;mutate_at=2u;
        assert(games_flow_idle_work() && reads==2u && title_reads==1u && !games_flow_ready && !sets);
    }
    reset();read_ok=false;assert(games_flow_idle_work() && reads==1u && games_flow_request.failed);
    assert(!games_flow_idle_work() && reads==1u); /* no I/O retry storm */
    ++shell.games_page;assert(games_flow_idle_work() && reads==2u && games_flow_request.failed);
    assert(!games_flow_idle_work() && reads==2u);
    ++shell.games_flow_epoch;read_ok=true;assert(games_flow_idle_work() && reads==4u && games_flow_ready);
    reset();read_ok=false;assert(games_flow_idle_work());strcpy(shell.games_query.text,"another");
    assert(games_flow_idle_work() && reads==2u); /* changed query is a new attempt */
    reset();needed_page=UINT_MAX/KUI_GAMES_ROWS+1u;
    assert(games_flow_idle_work() && !reads && games_flow_request.failed && !games_flow_ready);
    assert(!games_flow_idle_work() && !reads);
    reset();prefetch_ok=false;assert(games_flow_idle_work() && reads==2u && games_flow_ready);
    /* Missing optional titles remains a valid bounded catalogue result. */
    reset();needed=false;idle_scheduler(true);assert(!reads && art_reads==1u && other_idle==4u);
    reset();idle_scheduler(false);assert(!reads && !art_reads && !other_idle);
    puts("PASS Flow dispatch: actual worker context/cancel fences, row priority, immutable ready buffer, main-only publication, bounded failure retry and page overflow");
    return 0;
}
'''

helpers = '\n\n'.join((
    function(shell_source, 'kui_shell_games_view'),
    function(source, 'games_query_same'),
    function(source, 'games_idle_cancelled'),
    function(source, 'games_flow_current'),
    function(source, 'games_flow_cancelled'),
    function(source, 'games_flow_idle_work'),
    'static void apply_flow_result(void){' + publication + '}',
    'static void idle_scheduler(bool idle){' + scheduler + '}',
))
# The real globals must precede stubs whose assertions name the worker buffer.
program = prefix[:prefix.index('bool kui_games_library_rows_query')] + globals_source + \
    prefix[prefix.index('bool kui_games_library_rows_query'):] + helpers + suffix
with tempfile.TemporaryDirectory(prefix='kui-flow-dispatch-') as tmp:
    c = Path(tmp) / 'flow.c'
    binary = Path(tmp) / 'flow'
    c.write_text(program)
    subprocess.run([*shlex.split(os.environ.get('CC', 'cc')), '-std=c11', '-O1', '-g',
                    '-Wall', '-Wextra', '-Werror', '-Wpedantic', '-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer', '-fno-pie', '-no-pie', '-Iinclude', '-I.deps/fatfs/source',
                    '-Ifirmware/kui-wifi/components/kwlink/include', str(c), '-o', str(binary)], cwd=ROOT, check=True)
    subprocess.run([str(binary)], cwd=ROOT, check=True, env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='halt_on_error=1'))
