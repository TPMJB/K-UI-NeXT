#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute actual root startup order with zero updater I/O before the old splash.

The frozen 2.0 prefix is the positive lifecycle control. The later early-artwork
workaround is the failing control: painting cannot authorize cold preflight I/O.
RAM handoff classification uses the real production helper, including corrupt
nonempty contexts; explicit bank validation remains the existing preflight.
"""
import hashlib
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / 'src/dreamcast/main.c').read_text()
END = '    kthread_t *io_worker=thd_create_ex(&attrs,worker,NULL);'
start = SOURCE.index('int main(void) {') + len('int main(void) {')
prefix = SOURCE[start:SOURCE.index(END, start) + len(END)]

def body(text, marker):
    at = text.index(marker) + len(marker)
    level, end = 1, at
    while level:
        level += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[at:end - 1]

worker = body(SOURCE, '            if(action==27) {')
fixtures = (
    ('v2', 'startup-v2-root-original.inc', '50c9c19064f3451b9e71307c149bea566b9c5d475eed37077eceec6ceb2e1348'),
    ('old_mask', 'startup-f722-preflight-original.inc', 'cf4b86f90dc155573ca6412909781dd9866602fe9974c12f3b17c63f5f8ab335'),
)
controls = []
for label, name, digest in fixtures:
    text = (ROOT / 'tests/fixtures' / name).read_text()
    assert hashlib.sha256(text.encode()).hexdigest() == digest
    controls.append((label, text))

before = r'''
#include "kui/update_handoff.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define KUI_SD_RUNTIME 1
#define KUI_UPDATE_ENGINE_CURRENT 3
#define KUI_IO_WORKER_STACK_BYTES (96u*1024u)
#define KUI_RELEASE_SHORT "test"
#define KUI_ROLE "runtime"
#define KUI_BUILD_ID "012345abcdef"
#define DM_640x480 1
#define DM_MULTIBUFFER 2
#define PM_RGB565 16
#define CT_VGA 1
#define VID_PAL 4
#define VID_INTERLACE 8
#define CONT_Y 8u
static uint8_t kui_update_bank_capability[16] = {[8] = 1};
static uint8_t kui_update_engine_capability[16] = {[8] = KUI_UPDATE_ENGINE_CURRENT};
static volatile uint8_t kui_update_ram_handoff[KUI_UPDATE_HANDOFF_BYTES];
static const uint8_t empty_slot[KUI_UPDATE_HANDOFF_BYTES]=KUI_UPDATE_HANDOFF_INITIALIZER;
static struct {char build[13];} update_boot_view;
static struct video_mode {unsigned width,height,cable_type,flags;} video = {640,480,CT_VGA,0};
static const struct video_mode *vid_mode = &video;
static void *ui_thread;
static uint16_t *vram_s;
static struct preferences {bool startup_chime,menu_sounds;unsigned music_volume;} system_current,system_pending;
static int disc_identity,disc_snapshot,settings_current,settings_pending,shell;
static bool safe_video_boot,busy,splash_active,startup_finished;
static unsigned pending;
static uint64_t splash_deadline;
static char destination_current[32],settings_note[128];
static int lock;
static unsigned events[32],event_count,preflights,resets,early_frames;
static bool warm,skip_chime;
static void event(unsigned n) {assert(event_count<32);events[event_count++]=n;}
void kui_storage_boot_begin(void) {}
void *thd_get_current(void) {return (void *)(uintptr_t)1;}
void vid_set_mode(int mode,int format) {
    assert(ui_thread && mode==(DM_640x480|DM_MULTIBUFFER) && format==PM_RGB565);event(1);
}
void kui_log(const char *format,...) {assert(format);}
void kui_clock_start(void (*log)(const char *,...)) {assert(log==kui_log);event(2);}
void kui_update_boot_require_engine(unsigned n) {assert(n==3);}
void kui_update_boot_reset(void) {++resets;}
void update_boot_start(void) {
    ++preflights;
    if(!warm) {fputs("forbidden ordinary-root updater preflight I/O\n",stderr);exit(23);}
}
void kui_splash_draw(uint16_t *frame) {(void)frame;++early_frames;}
void vid_waitvbl(void) {}
void vid_flip(int target) {assert(target==-1);}
void kui_system_settings_default(struct preferences *p) {*p=(struct preferences){true,true,3};}
void kui_music_init(void (*log)(const char *,...)) {assert(log==kui_log);event(3);}
void kui_disc_identity_init(int *p) {*p=1;}
void kui_disc_identity_park(int *p) {assert(*p==1);}
unsigned controller_buttons(void) {return 0;}
void kui_settings_default(int *p) {*p=2;}
void kui_shell_init(int *p,const int *settings) {assert(*settings==2);*p=3;event(4);}
void kui_shell_set_system_preferences(int *p,const struct preferences *s) {assert(*p==3 && s->startup_chime);}
void kui_destination_default(char *p) {strcpy(p,"root");}
uint64_t timer_ms_gettime64(void) {return 10;}
void draw_shell(void) {assert(pending==27 && busy && splash_active);event(5);}
bool kui_disc_stop(void) {assert(event_count && events[event_count-1]==5);event(6);return true;}
void kui_vmu_lcd_startup(void) {event(7);}
void mutex_lock(int *p) {assert(p==&lock);}
void mutex_unlock(int *p) {assert(p==&lock);}
void settings_operation(bool save) {assert(!save && event_count>=8);event(9);}
void system_operation(bool save) {assert(!save);event(10);}
void games_selection_startup(void) {event(11);}
bool startup_sound_cancelled(void) {return skip_chime;}
void kui_music_play_boot_chime(bool (*cancel)(void)) {assert(cancel==startup_sound_cancelled);event(12);}
bool kui_menu_sound_init(void) {event(13);return true;}
void kui_menu_sound_config(bool on,unsigned volume) {assert(on && volume==3);}
static void *worker(void *unused) {(void)unused;
'''
middle = r'''
    return NULL;
}
typedef struct {unsigned stack_size;const char *label;} kthread_attr_t;
typedef int kthread_t;
kthread_t *thd_create_ex(const kthread_attr_t *attrs,void *(*entry)(void *),void *argument) {
    assert(attrs->stack_size>=64u*1024u && !strcmp(attrs->label,"kui-io") && entry==worker && !argument);
    event(8);return (void *)(uintptr_t)1;
}
static int startup_prefix(void) {
'''
after = r'''
    assert(io_worker);return 0;
}
static void reset(void) {
    event_count=preflights=resets=early_frames=0;startup_finished=false;warm=false;
    for(unsigned i=0;i<KUI_UPDATE_HANDOFF_BYTES;i++)kui_update_ram_handoff[i]=empty_slot[i];
    memset(&update_boot_view,0,sizeof(update_boot_view));
    /* The frozen 2.0 lifecycle did not yet carry these RAM capability bytes. */
    (void)kui_update_bank_capability;(void)kui_update_engine_capability;(void)vram_s;
}
static void lifecycle(bool skip) {
    reset();skip_chime=skip;assert(startup_prefix()==0);
    assert(!preflights && !early_frames && (!CURRENT_COHORT || resets==1));
    if(CURRENT_COHORT)assert(!strcmp(update_boot_view.build,KUI_BUILD_ID));
    assert(worker(NULL)==NULL && startup_finished && !splash_active);
    const unsigned expected[]={1,2,3,4,5,6,7,8,9,10,11,12,13};
    const unsigned skipped[]={1,2,3,4,5,6,7,8,9,10,11,13};
    assert(event_count==(skip?12u:13u));
    assert(!memcmp(events,skip?skipped:expected,event_count*sizeof(events[0])));
}
int main(void) {
    lifecycle(false);lifecycle(true);
    if(CURRENT_COHORT) {
        struct kui_update_handoff h={.build=KUI_BUILD_ID,.transport=1,.sequence=1,
            .file_bytes=4096,.memory_bytes=8192,.payload_offset=128,.nonce={1}};
        uint8_t wire[KUI_UPDATE_HANDOFF_BYTES];assert(kui_update_handoff_encode(wire,&h));
        /* A valid explicit Restart context, damaged context, and malformed
         * header all retain checked selection rather than silently becoming root. */
        for(unsigned kind=0;kind<3;kind++) {
            reset();warm=true;
            for(unsigned i=0;i<KUI_UPDATE_HANDOFF_BYTES;i++)kui_update_ram_handoff[i]=wire[i];
            if(kind==1)kui_update_ram_handoff[132]^=1;
            if(kind==2)kui_update_ram_handoff[0]^=1;
            assert(startup_prefix()==0 && preflights==1 && !resets && !early_frames);
        }
        reset();kui_update_engine_capability[8]=2;
        assert(startup_prefix()==1 && !event_count && !preflights && !resets);
    }
    puts("PASS root startup: actual prefix matches 2.0 splash/STOP/worker/chime lifecycle; no ordinary updater preflight; nonempty/corrupt RAM contexts retain checked routing");
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='kui-startup-root-') as temporary:
    directory = Path(temporary)
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='halt_on_error=1')
    for label, code in [('current', prefix), *controls]:
        source, binary = directory / (label + '.c'), directory / label
        source.write_text('#define CURRENT_COHORT '+str(int(label == 'current'))+'\n'+before+worker+middle+code+after)
        subprocess.run([*shlex.split(os.environ.get('HOST_CC', 'cc')), '-std=c11', '-O1', '-g',
            '-Wall', '-Wextra', '-Werror', '-Wpedantic', '-fsanitize=address,undefined',
            '-fno-omit-frame-pointer', '-fno-pie', '-no-pie', '-I', str(ROOT / 'include'),
            '-I', str(ROOT / 'config'), '-I', str(ROOT / '.deps/fatfs/source'),
            str(source), str(ROOT / 'src/core/update_handoff.c'), str(ROOT / 'src/core/data.c'),
            '-o', str(binary)], check=True)
        result = subprocess.run([str(binary)], env=env, capture_output=True, text=True)
        if label == 'old_mask':
            assert result.returncode == 23 and 'forbidden ordinary-root updater preflight I/O' in result.stderr, result.stdout+result.stderr
            print('PASS frozen f7229 control: early artwork still reaches forbidden ordinary-root preflight and is rejected')
        else:
            assert result.returncode == 0, result.stdout+result.stderr
            print(label+': '+result.stdout.strip())
