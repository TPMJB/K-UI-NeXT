#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise actual worker routing and token ownership without Dreamcast I/O."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "src/dreamcast/main.c").read_text()


def function(name):
    at = source.rfind("\n", 0, source.index(name + "(")) + 1
    brace = source.index("{", at)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[at:end]


program = r'''
#include "kui/shell.h"
#include "kui/vmu_library.h"
#include "kui/vmu_backing.h"
#include "kui/games_storage.h"
#include "kui/media.h"
#include "kui/storage.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct kui_shell_vmu_delete_request vmu_virtual_request_pending;
static struct kui_vmu_delete_preview vmu_virtual_preview;
static struct kui_vmu_game_candidates_storage vmu_virtual_preview_storage;
static bool vmu_virtual_preview_storage_valid;
static struct kui_app_status vmu_virtual_status;
static unsigned vmu_virtual_request_generation,vmu_virtual_result_generation,vmu_virtual_generation;
static bool vmu_virtual_committed,vmu_virtual_ready;
static unsigned vmu_virtual_users;
static unsigned previews,commits,mounts,releases,depth;
static int lock;
static bool mount_fail,close_fail,pin_fail,cancelled;
static bool snapshot_source,swap_during_preview,swap_during_commit,pin_fail_during_commit;
static int backend_result;
static struct kui_volume volume={.start=2048,.count=900000};
static struct kui_vmu_sd_identity current_sd;
static void mutex_lock(int *p) {(void)p;assert(!depth);depth=1;}
static void mutex_unlock(int *p) {(void)p;assert(depth==1);depth=0;}
static void kui_sd_set_params(unsigned transport,bool crc) {assert(transport==KUI_STORAGE_AUTO && crc);}
static void kui_log(const char *format,...) {(void)format;}
static bool kui_cancelled(void) {return cancelled;}
enum kui_games_storage_result kui_games_storage_open(kui_log_fn log) {
    (void)log;assert(!depth);++mounts;return mount_fail?KUI_GAMES_STORAGE_NO_CARD:KUI_GAMES_STORAGE_OK;
}
bool kui_games_storage_done(bool keep) {assert(!depth && !keep);++releases;return !close_fail;}
const struct kui_volume *kui_media_volume(void) {return &volume;}
static bool kui_sd_vmu_profile_identity(struct kui_vmu_sd_identity *out) {
    *out=current_sd;return !pin_fail;
}
int kui_vmu_library_delete_preview(const char *path,const uint8_t id[16],const char *name,
    struct kui_vmu_delete_preview *out,kui_log_fn log,kui_cancel_fn cancel) {
    (void)log;assert(!depth && cancel==kui_cancelled);++previews;
    assert(!strcmp(path,snapshot_source?"0:/KUI/vmu/cards/v0012/card.vmd":"0:/KUI/vmu-test/p0012/card.vmd") && id[0]==12);
    memset(out,0,sizeof(*out));strcpy(out->path,path);memcpy(out->identity,id,16);
    out->whole_card=name==NULL;if(name) {assert(!strcmp(name,"EXACTSAVE"));strcpy(out->save_name,name);}
    out->snapshot=snapshot_source;out->users=3;out->ready=backend_result==0;
    /* No catalogue/storage binding is supplied by this backend stub: cloned
     * path/identity/image/reference bytes would otherwise be admissible. */
    if(swap_during_preview) ++current_sd.cid[0];
    return backend_result;
}
int kui_vmu_library_delete_commit(const struct kui_vmu_delete_preview *token,
    const struct kui_vmu_game_candidates_storage *storage,kui_log_fn log,kui_cancel_fn cancel) {
    (void)log;assert(!depth && cancel==kui_cancelled && token==&vmu_virtual_preview && token->ready);++commits;
    assert(storage->cid[0]==42 && storage->card_sectors==1000000 && storage->high_capacity==1);
    assert(storage->partition_first==2048 && storage->partition_end==902048);
    if(swap_during_commit) ++current_sd.cid[0];
    if(pin_fail_during_commit) pin_fail=true;
    return backend_result;
}
''' + function("vmu_virtual_storage_pin") + "\n" + function("vmu_virtual_storage_same") + "\n" + function("vmu_virtual_delete_work") + "\n" + function("worker_action") + r'''
static void reset(void) {
    memset(&vmu_virtual_request_pending,0,sizeof(vmu_virtual_request_pending));
    memset(&vmu_virtual_preview,0,sizeof(vmu_virtual_preview));
    memset(&vmu_virtual_preview_storage,0,sizeof(vmu_virtual_preview_storage));vmu_virtual_preview_storage_valid=false;
    strcpy(vmu_virtual_request_pending.path,"0:/KUI/vmu-test/p0012/card.vmd");
    vmu_virtual_request_pending.identity[0]=12;strcpy(vmu_virtual_request_pending.save_name,"EXACTSAVE");
    vmu_virtual_request_generation=7;vmu_virtual_generation=0;
    previews=commits=mounts=releases=0;mount_fail=close_fail=pin_fail=cancelled=false;backend_result=0;
    snapshot_source=swap_during_preview=swap_during_commit=pin_fail_during_commit=false;
    memset(&current_sd,0,sizeof(current_sd));current_sd.cid[0]=42;current_sd.sectors=1000000;current_sd.high_capacity=true;
    volume.start=2048;volume.count=900000;
}
int main(void) {
    assert(worker_action(KUI_SHELL_VMU_BACKUP_SAVES)==118);
    assert(worker_action(KUI_SHELL_DISC_CARDS_RUN)==119);
    assert(worker_action(KUI_SHELL_GAMES_SMASH_PHANTASY_STAR_II)==121);
    assert(worker_action(KUI_SHELL_GAMES_SMASH_SHINING_FORCE)==122);
    assert(worker_action(KUI_SHELL_VMU_VIRTUAL_DELETE_PREVIEW)==123);
    assert(worker_action(KUI_SHELL_VMU_VIRTUAL_DELETE_COMMIT)==124);
    assert(worker_action(KUI_SHELL_VMU_CARD_LOAD)==125);
    assert(worker_action(KUI_SHELL_VMU_DELETE_COMMIT)==38); /* Physical route preserved. */
    reset();vmu_virtual_delete_work(false);
    assert(previews==1 && !commits && mounts==1 && releases==1);
    assert(vmu_virtual_ready && !vmu_virtual_committed && vmu_virtual_users==3);
    assert(vmu_virtual_preview_storage_valid && vmu_virtual_preview_storage.cid[0]==42);
    assert(vmu_virtual_result_generation==7 && vmu_virtual_status.passed);
    ++vmu_virtual_request_generation;vmu_virtual_delete_work(true);
    assert(commits==1 && !vmu_virtual_preview.ready && !vmu_virtual_ready && vmu_virtual_committed);
    assert(!vmu_virtual_preview_storage_valid);
    assert(vmu_virtual_result_generation==8 && vmu_virtual_status.passed);
    vmu_virtual_delete_work(true);assert(commits==1 && vmu_virtual_status.errors); /* One use. */
    reset();vmu_virtual_request_pending.whole_card=true;vmu_virtual_delete_work(false);
    assert(vmu_virtual_preview.whole_card && !vmu_virtual_preview.save_name[0]);
    vmu_virtual_delete_work(true);assert(commits==1);
    reset();snapshot_source=true;vmu_virtual_request_pending.whole_card=true;
    strcpy(vmu_virtual_request_pending.path,"0:/KUI/vmu/cards/v0012/card.vmd");
    vmu_virtual_delete_work(false);vmu_virtual_delete_work(true);assert(commits==1 && vmu_virtual_status.passed);
    reset();vmu_virtual_delete_work(false);vmu_virtual_request_pending.identity[0]=11;
    vmu_virtual_delete_work(true);assert(!commits && vmu_virtual_status.errors);
    reset();vmu_virtual_delete_work(false);strcpy(vmu_virtual_request_pending.save_name,"OTHER");
    vmu_virtual_delete_work(true);assert(!commits && vmu_virtual_status.errors);
    reset();vmu_virtual_delete_work(false);pin_fail=true;vmu_virtual_delete_work(true);
    assert(!commits && vmu_virtual_status.errors && !vmu_virtual_preview.ready);
    /* A catalogue-absent clone with identical VMU bytes must be rejected by
     * the worker before the first backend mutation, for every delete kind. */
    for(unsigned kind=0;kind<3;kind++) for(unsigned field=0;field<6;field++) {
        reset();vmu_virtual_request_pending.whole_card=kind!=0;
        if(kind==2) {snapshot_source=true;strcpy(vmu_virtual_request_pending.path,"0:/KUI/vmu/cards/v0012/card.vmd");}
        vmu_virtual_delete_work(false);assert(vmu_virtual_ready && vmu_virtual_preview_storage_valid);
        if(field==0) ++current_sd.cid[0];
        else if(field==1) ++current_sd.sectors;
        else if(field==2) current_sd.high_capacity=false;
        else if(field==3) ++volume.start;
        else if(field==4) --volume.count;
        else pin_fail=true;
        vmu_virtual_delete_work(true);
        assert(!commits && vmu_virtual_status.errors && !vmu_virtual_preview.ready && !vmu_virtual_preview_storage_valid);
    }
    reset();pin_fail=true;vmu_virtual_delete_work(false);
    assert(!previews && !commits && !vmu_virtual_ready && !vmu_virtual_preview_storage_valid);
    reset();swap_during_preview=true;vmu_virtual_delete_work(false);
    assert(previews==1 && !commits && !vmu_virtual_ready && !vmu_virtual_preview_storage_valid);
    for(unsigned failure=0;failure<2;failure++) {
        reset();vmu_virtual_delete_work(false);
        swap_during_commit=failure==0;pin_fail_during_commit=failure==1;
        vmu_virtual_delete_work(true);
        assert(commits==1 && vmu_virtual_status.errors && !vmu_virtual_status.passed);
        assert(strstr(vmu_virtual_status.message,"uncertain") && !vmu_virtual_preview.ready && !vmu_virtual_preview_storage_valid);
    }
    reset();mount_fail=true;vmu_virtual_delete_work(false);
    assert(!previews && !commits && !releases && !vmu_virtual_ready && vmu_virtual_status.errors);
    reset();vmu_virtual_delete_work(false);mount_fail=true;vmu_virtual_delete_work(true);
    assert(!commits && !vmu_virtual_preview.ready && vmu_virtual_status.errors);
    reset();close_fail=true;vmu_virtual_delete_work(false);
    assert(!vmu_virtual_ready && vmu_virtual_status.errors && strstr(vmu_virtual_status.message,"uncertain"));
    reset();backend_result=KUI_VMU_LIBRARY_CANCELLED;vmu_virtual_delete_work(false);
    assert(vmu_virtual_status.stopped && !vmu_virtual_status.passed && !vmu_virtual_ready);
    puts("PASS delete dispatcher: actual worker routes, exact card/save token, catalogue-independent preview SD pin, cloned/swapped SD rejection for saves/cards/snapshots, one-use commit, storage errors and cancellation");
}
'''
with tempfile.TemporaryDirectory(prefix="kui-vmu-delete-dispatch-") as work:
    test = Path(work) / "dispatch.c"
    test.write_text(program)
    output = Path(work) / "dispatch"
    subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-O1", "-g",
                    "-Wall", "-Wextra", "-Werror", "-Wpedantic",
                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie",
                    "-Iinclude", "-I.deps/fatfs/source", "-Ifirmware/kui-wifi/components/kwlink/include",
                    str(test), "-o", str(output)], cwd=ROOT, check=True)
    subprocess.run([str(output)], check=True, env=dict(os.environ, ASAN_OPTIONS="detect_leaks=0"))
