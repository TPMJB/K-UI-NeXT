#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise actual worker dispatch/ownership predicates and blocked replies."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
source=(ROOT/'src/dreamcast/main.c').read_text()

def function(signature):
    begin=source.index(signature)
    opening=source.index('{',begin)
    depth=1;at=opening+1
    while depth:
        if source[at]=='{':depth+=1
        elif source[at]=='}':depth-=1
        at+=1
    return source[begin:at]

handoff=source[source.index('if(action && needs_cd_handoff(action) && !kui_cd_audio_stop_for_io'):]
handoff=handoff[:handoff.index('} else if(action && needs_cd_handoff(action))')]
assert 'if(action==119) disc_cards_failure(disc_cards_pending.generation,false,' in handoff
reset=source[source.index('if(drive_reset_required && (action==1'):]
reset=reset[:reset.index('if(action>=49 && action<=53)')]
assert 'action==119' in reset and 'disc_cards_failure(shell.disc_cards_request.generation,false,' in reset
assert 'if(action==119) disc_cards_failure(disc_cards_pending.generation,true,' in source
assert 'else if(active_app==119) disc_cards_snapshot.status=*status;' in source
assert 'disc_cards_pending=shell.disc_cards_request;disc_cards_snapshot=shell.disc_cards;' in source
assert 'kui_shell_set_disc_cards(&shell,&disc_cards_snapshot);seen_disc_cards=disc_cards_generation;' in source
body=source[source.index('if(action==119) {\n                struct kui_disc_cards_view result;'):]
body=body[:body.index('\n            }')]
assert 'active_app=action;kui_sd_set_params(KUI_STORAGE_AUTO,true);' in body
assert 'kui_disc_cards_run(&disc_cards_pending,&result,kui_log,kui_cancelled,app_progress);' in body
assert 'inspection_read=result.identified;' in body
assert 'disc_cards_snapshot=result;++disc_cards_generation;' in body

code='''#include "kui/shell.h"
#include "kui/disc_idle.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct kui_disc_cards_view disc_cards_snapshot;
static unsigned disc_cards_generation;
'''
for signature in ('static unsigned worker_action(', 'static bool needs_cd_handoff(',
                  'static bool games_action_invalidates(', 'static bool games_session_action(',
                  'static void disc_cards_failure('):
    code+=function(signature)+'\n'
code+='''int main(void) {
    assert(worker_action(KUI_SHELL_DISC_CARDS_RUN)==119u);
    assert(needs_cd_handoff(119u) && games_action_invalidates(119u));
    assert(!games_session_action(119u));
    assert(kui_disc_idle_cleanup(119u) && kui_disc_idle_inspection(119u));
    memset(&disc_cards_snapshot,0xa5,sizeof(disc_cards_snapshot));
    disc_cards_failure(42u,false,"Drive reset required");
    assert(disc_cards_generation==1u && disc_cards_snapshot.generation==42u);
    assert(!disc_cards_snapshot.identified && !disc_cards_snapshot.token && !disc_cards_snapshot.count);
    assert(disc_cards_snapshot.status.complete && disc_cards_snapshot.status.errors==1u);
    assert(!disc_cards_snapshot.status.passed && !disc_cards_snapshot.status.stopped);
    disc_cards_failure(43u,true,"Stopped before starting");
    assert(disc_cards_generation==2u && disc_cards_snapshot.generation==43u);
    assert(disc_cards_snapshot.status.stopped && !disc_cards_snapshot.status.errors);
    assert(!disc_cards_snapshot.identified && !disc_cards_snapshot.token);
    puts("PASS actual disc-card worker dispatch: CD handoff, released storage, drive reset, cancel completion and spindle ownership");
    return 0;
}'''
with tempfile.TemporaryDirectory(prefix='disc-cards-dispatch-') as temporary:
    folder=Path(temporary);path=folder/'dispatch.c';path.write_text(code)
    binary=folder/'dispatch'
    subprocess.run([*shlex.split(os.environ.get('CC','cc')),'-std=c11','-O1','-g',
        '-Wall','-Wextra','-Werror','-Wpedantic','-fsanitize=address,undefined',
        '-fno-pie','-no-pie','-Iinclude','-I.deps/fatfs/source',
        '-Ifirmware/kui-wifi/components/kwlink/include',str(path),'-o',str(binary)],cwd=ROOT,check=True)
    subprocess.run([str(binary)],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
