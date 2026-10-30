#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise actual UI/audio snapshot C under real thread contention.

This measures synchronization behavior on the host, not Dreamcast latency.
The refill thread holds the audio mutex until every simulated UI poll has
returned; a blocking snapshot would deadlock and fail the process timeout.
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
main = (ROOT / "src/dreamcast/main.c").read_text()
audio = (ROOT / "src/apps/music.c").read_text()


def function(source, name):
    name_at = source.index(name + "(")
    at = source.rfind("\n", 0, name_at) + 1
    brace = source.index("{", at)
    depth, end = 1, brace + 1
    while depth:
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
        end += 1
    return source[at:end]


# The actual UI poll path must use the nonblocking helper. Worker/control
# publication deliberately retains the authoritative blocking API.
loop = main[main.index("for(;;) {", main.index("int main(void)")):]
assert "publish_music_if_ready();\n        mutex_lock(&lock);" in loop
assert "kui_music_status_copy(&status);" in function(main, "publish_music")
assert "last_draw=is_busy?t:timer_ms_gettime64();" in loop
assert "thd_sleep(kui_ui_menu_wait_ms(is_busy,menu_drew));" in loop

program = r'''
#define KUI_ON_CONSOLE 1
#include "kui/music.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
typedef pthread_mutex_t mutex_t;
static mutex_t audio_lock=PTHREAD_MUTEX_INITIALIZER,lock=PTHREAD_MUTEX_INITIALIZER;
static mutex_t gate=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ready=PTHREAD_COND_INITIALIZER,release=PTHREAD_COND_INITIALIZER;
static struct {struct kui_music_status status;} music;
static struct kui_music_status music_snapshot;
static bool refill_ready,refill_done;
static unsigned ui_lock_calls;
static int mutex_trylock(mutex_t *value) {
    return pthread_mutex_trylock(value)?-1:0;
}
static void mutex_lock(mutex_t *value) {
    assert(value==&lock);++ui_lock_calls;assert(!pthread_mutex_lock(value));
}
static void mutex_unlock(mutex_t *value) {assert(!pthread_mutex_unlock(value));}
static void unlock_audio(void) {assert(!pthread_mutex_unlock(&audio_lock));}
''' + function(audio, "kui_music_status_try_copy") + "\n" + function(main, "publish_music_if_ready") + r'''
static void *refill(void *unused) {
    (void)unused;
    assert(!pthread_mutex_lock(&audio_lock));
    music.status.volume=70u;strcpy(music.status.title,"New audio state");
    assert(!pthread_mutex_lock(&gate));
    refill_ready=true;assert(!pthread_cond_signal(&ready));
    while(!refill_done)assert(!pthread_cond_wait(&release,&gate));
    assert(!pthread_mutex_unlock(&gate));
    unlock_audio();return NULL;
}
int main(void) {
    assert(!kui_music_status_try_copy(NULL));
    music.status.volume=30u;strcpy(music.status.title,"Previous audio state");
    publish_music_if_ready();
    assert(ui_lock_calls==1u && music_snapshot.volume==30u);
    struct kui_music_status prior=music_snapshot,retained=prior;
    pthread_t thread;assert(!pthread_create(&thread,NULL,refill,NULL));
    assert(!pthread_mutex_lock(&gate));
    while(!refill_ready)assert(!pthread_cond_wait(&ready,&gate));
    assert(!pthread_mutex_unlock(&gate));
    for(unsigned i=0;i<10000u;i++) {
        assert(!kui_music_status_try_copy(&retained));
        assert(!memcmp(&retained,&prior,sizeof(prior)));
        publish_music_if_ready();
        assert(!memcmp(&music_snapshot,&prior,sizeof(prior)));
    }
    /* Failed display samples never acquire the UI mutex or expose an audio
     * state that is still being updated under the decoder's mutex. */
    assert(ui_lock_calls==1u);
    assert(!pthread_mutex_lock(&gate));
    refill_done=true;assert(!pthread_cond_signal(&release));
    assert(!pthread_mutex_unlock(&gate));assert(!pthread_join(thread,NULL));
    publish_music_if_ready();
    assert(ui_lock_calls==2u && music_snapshot.volume==70u);
    assert(!strcmp(music_snapshot.title,"New audio state"));
    assert(kui_music_status_try_copy(&retained));
    assert(!memcmp(&retained,&music_snapshot,sizeof(retained)));
    puts("PASS menu audio dispatch: actual C, real competing refill thread, 20000 nonblocking reads, retained coherent status, fresh publication after refill; hardware latency unmeasured");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="kui-menu-audio-") as work:
    test = Path(work) / "audio.c"
    test.write_text(program)
    binary = Path(work) / "audio"
    subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-O1", "-g",
                    "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-pthread",
                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie",
                    "-Iinclude", "-I.deps/fatfs/source",
                    "-Ifirmware/kui-wifi/components/kwlink/include",
                    str(test), "-o", str(binary)], cwd=ROOT, check=True)
    subprocess.run([str(binary)], check=True, timeout=10,
                   env=dict(os.environ, ASAN_OPTIONS="detect_leaks=0"))
