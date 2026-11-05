#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check the real mounted startup timing writer, including every I/O failure."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "src/dreamcast/main.c").read_text()
start = source.index("static void update_boot_timing_save(")
brace = source.index("{", start)
depth, end = 1, brace + 1
while depth:
    depth += (source[end] == "{") - (source[end] == "}")
    end += 1

program = r'''
#include "kui/update_boot.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define KUI_BUILD_ID "012345abcdef"
static struct kui_update_boot_view update_boot_view;
static unsigned scenario,opens,writes,syncs,closes,logs;
static bool opened;
static char captured[768];
static const char *kui_storage_name(unsigned mode) {assert(mode==1u);return "SCI SD";}
static unsigned kui_storage_selected(void) {return 1u;}
static void kui_log(const char *format,...) {assert(format);++logs;}
FRESULT f_open(FIL *file,const TCHAR *path,BYTE mode) {
    assert(file && !opened && !strcmp(path,"0:/KUI/updater-startup.txt"));
    assert(mode==(FA_WRITE|FA_CREATE_ALWAYS));++opens;
    opened=scenario!=1u;return opened?FR_OK:FR_DISK_ERR;
}
FRESULT f_write(FIL *file,const void *data,UINT bytes,UINT *done) {
    assert(file && opened && data && bytes<sizeof(captured));++writes;
    memcpy(captured,data,bytes);captured[bytes]=0;
    *done=scenario==3u?bytes-1u:bytes;
    return scenario==2u?FR_DISK_ERR:FR_OK;
}
FRESULT f_sync(FIL *file) {assert(file && opened);++syncs;return scenario==4u?FR_DISK_ERR:FR_OK;}
FRESULT f_close(FIL *file) {
    assert(file && opened);opened=false;++closes;
    return scenario==5u?FR_DISK_ERR:FR_OK;
}
''' + source[start:end] + r'''
int main(void) {
    strcpy(update_boot_view.bank,"111111111111");
    strcpy(update_boot_view.build,"222222222222");
    strcpy(update_boot_view.notice,"An older update was refused");update_boot_view.fallback=true;
    for(scenario=0u;scenario<6u;scenario++) {
        opens=writes=syncs=closes=logs=0u;opened=false;captured[0]=0;
        update_boot_timing_save(1100u,1120u,1650u,1000u,1050u,KUI_UPDATE_BOOT_CHAIN);
        assert(opens==1u && !opened);
        if(scenario==1u) {assert(!writes && !syncs && !closes && !logs);continue;}
        assert(writes==1u && closes==1u && logs==1u);
        assert(syncs==((scenario==2u || scenario==3u)?0u:1u));
        assert(strstr(captured,"Display setup: 50 ms\nConnection: 50 ms\nMount: 20 ms\nVersion selection: 530 ms"));
        assert(strstr(captured,"Before report: 650 ms"));
        assert(strstr(captured,"Build: 012345abcdef") && strstr(captured,"Storage: SCI SD"));
        assert(strstr(captured,"Bank: 111111111111") && strstr(captured,"GUI: 222222222222"));
        assert(strstr(captured,"Fallback: yes") && strstr(captured,update_boot_view.notice));
    }
    puts("PASS mounted updater timings: phase accounting, source/selection identity, bounded output and all I/O failures");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="kui-updater-timing-") as temporary:
    path = Path(temporary)
    (path / "timings.c").write_text(program)
    binary = path / "timings"
    subprocess.run([*shlex.split(os.environ.get("HOST_CC", "cc")), "-std=c11", "-O1", "-g",
                    "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-fsanitize=address,undefined",
                    "-fno-omit-frame-pointer", "-fno-pie", "-no-pie", "-Iinclude", "-I.deps/fatfs/source",
                    str(path / "timings.c"), "-o", str(binary)], cwd=ROOT, check=True)
    subprocess.run([str(binary)], check=True, env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0"})
