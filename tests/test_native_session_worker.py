#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Replay the actual post-publication gesture helper against guarded devices."""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(source, name):
    for match in re.finditer(r'\b' + re.escape(name) + r'\s*\(', source):
        begin = source.rfind('static ', 0, match.start())
        opening = source.find('{', begin)
        semicolon = source.find(';', begin)
        if begin >= 0 and opening > match.start() and (semicolon < 0 or opening < semicolon):
            break
    else:
        raise ValueError('Function body not found: ' + name)
    level = 1
    end = opening + 1
    while level:
        if source[end] == '{':
            level += 1
        elif source[end] == '}':
            level -= 1
        end += 1
    return source[begin:end]


def main():
    source = (ROOT / 'src/loader/native_session_worker.inc').read_text()
    program = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <setjmp.h>
#include <stdio.h>
#include "kui/native_return.h"
#include "kui/native_disc_set.h"
#include "sd_reader.h"
static struct {uint32_t configured,return_available,disc_hold,disc_latched,exit_pending;
    uint32_t active,result,attempted,return_prepared;} session;
static struct {uint32_t inflight,handling;struct {uint32_t active;} protocol;} durable_card;
static struct {uint32_t pending;} overlay;
static struct kui_native_disc_set session_discs;
static unsigned requests,visits,last_error;
static unsigned return_calls;
static int visit_result,return_result;
static uint64_t return_blocks;
static jmp_buf stopped;
static struct {uint32_t armed,pending,submitted,completed;} state;
static struct {struct {uint32_t consumer;} driver;
    struct {uint32_t payload,lease_bytes;} lease;} config;
static uint32_t kui_vmu_native_passive_consumer;
static bool lease_owned,stack_owned,hooks_owned;
static bool lease_valid(void) {return lease_owned;}
static bool stack_valid(void) {return stack_owned;}
static bool installed_hooks_valid(void) {return hooks_owned;}
static struct kui_native_return_plan session_return_plan;
struct native_session_return_reader {struct kui_loader_sd *card;struct kui_loader_sd_stream stream;};
static unsigned stream_starts,stream_reads,stream_stops,load_calls;
static bool stream_fail_start,stream_fail_read;
static enum kui_native_return_result load_result;
static enum kui_loader_sd_result stop_result;
enum kui_loader_sd_result kui_loader_sd_stream_start(struct kui_loader_sd *card,
    struct kui_loader_sd_stream *stream,uint32_t lba,uint32_t count) {
    assert(card && !stream->active && count && count<=128u);++stream_starts;
    if(stream_fail_start)return KUI_LOADER_SD_COMMAND;
    *stream=(struct kui_loader_sd_stream){.active=true,.next_lba=lba,.remaining=count};return KUI_LOADER_SD_OK;
}
enum kui_loader_sd_result kui_loader_sd_stream_next(struct kui_loader_sd *card,
    struct kui_loader_sd_stream *stream,void *out) {
    assert(card && stream->active && stream->remaining && out);++stream_reads;
    if(stream_fail_read)return KUI_LOADER_SD_CRC;
    ++stream->next_lba;if(!--stream->remaining)stream->active=false;return KUI_LOADER_SD_OK;
}
enum kui_loader_sd_result kui_loader_sd_stream_stop(struct kui_loader_sd *card,
    struct kui_loader_sd_stream *stream) {
    assert(card && stream);++stream_stops;stream->active=false;return stop_result;
}
enum kui_native_return_result kui_native_return_load(const struct kui_native_return_plan *plan,
    uint32_t lease,uint32_t bytes,void *destination,size_t capacity,
    kui_native_return_read read,void *context) {
    assert(plan==&session_return_plan && lease==config.lease.payload && bytes==config.lease.lease_bytes);
    assert(destination==(void *)(uintptr_t)UINT32_C(0xac010000) && capacity==UINT32_C(0x400000));
    assert(read && context);++load_calls;return load_result;
}
static int low_control(uint32_t operation,const struct kui_retail_manifest *next,
    struct kui_loader_sd *card) {
    assert(operation==KUI_NATIVE_SESSION_RETURN && !next && card);++return_calls;
    if(return_result==1)*card=(struct kui_loader_sd){.blocks=return_blocks};
    return return_result;
}
static kui_native_session_control session_control(void) {return low_control;}
bool kui_native_disc_set_request_next(struct kui_native_disc_set *set) {
    assert(set==&session_discs);++requests;session_discs.phase=1u;return true;
}
int kui_native_disc_set_visit(struct kui_native_disc_set *set,kui_native_session_control control) {
    assert(set==&session_discs && control==low_control);++visits;
    if(visit_result==1)session_discs.phase=0u;
    return visit_result;
}
static void fail(uint32_t value) {last_error=value;longjmp(stopped,1);}
''' + function(source, 'native_session_controller_complete') + '\n' + function(source, 'native_session_return_read') + '\n' + function(source, 'native_session_return_guard') + '\n' + function(source, 'native_session_return_transaction') + r'''
int main(void) {
    const uint32_t next=UINT32_C(0xfffffff7),released=UINT32_C(0x0000ffff);
    for(unsigned i=0;i<100;i++)native_session_controller_complete(next);
    assert(!requests && !visits);session.configured=1u;visit_result=1;
    for(unsigned i=0;i<89;i++)native_session_controller_complete(next);
    assert(!requests);native_session_controller_complete(next);assert(requests==1 && visits==1);
    for(unsigned i=0;i<400;i++)native_session_controller_complete(next);
    assert(requests==1);native_session_controller_complete(released);
    for(unsigned i=0;i<90;i++)native_session_controller_complete(next);
    assert(requests==2 && visits==2);
    session_discs.phase=KUI_NATIVE_DISC_WAITING;
    native_session_controller_complete(UINT32_MAX);
    assert(visits==2 && session_discs.phase==KUI_NATIVE_DISC_WAITING);
    session_discs.phase=KUI_NATIVE_DISC_IDLE;
    /* A face button excludes this chord; wrong/absent triggers cannot cycle. */
    native_session_controller_complete(released);
    for(unsigned i=0;i<120;i++)native_session_controller_complete(next & ~2u);
    assert(requests==2);
    for(unsigned i=0;i<120;i++)native_session_controller_complete(next & UINT32_C(0x00ffffff));
    assert(requests==2);
    /* Save protocol staging and pending overlay publication delay both kinds
     * of service work. They never synthesize an acknowledgment or discard it. */
    session.exit_pending=1u;durable_card.protocol.active=1u;
    for(unsigned i=0;i<20;i++)native_session_controller_complete(released);
    assert(session.exit_pending && !last_error && durable_card.protocol.active);
    durable_card.protocol.active=0u;overlay.pending=1u;
    native_session_controller_complete(released);assert(session.exit_pending && !last_error);
    overlay.pending=0u;session_discs.phase=1u;visit_result=0;
    unsigned old_visits=visits;
    if(!setjmp(stopped)) {native_session_controller_complete(released);assert(!"expected intentional terminal exit");}
    assert(last_error==0x4f && !session.exit_pending && visits==old_visits && session_discs.phase==KUI_NATIVE_DISC_IDLE);
    /* Reader preparation happens before terminal reporting. A busy reader
     * leaves the request latched and lets the SDK continue to complete I/O. */
    last_error=0u;session.exit_pending=1u;session.return_available=1u;
    session_discs.phase=KUI_NATIVE_DISC_WAITING;
    session_return_plan.card_sectors=4096u;return_blocks=8192u;return_result=0;
    native_session_controller_complete(released);
    assert(!last_error && session.exit_pending && !session.return_prepared && return_calls==1u &&
        session_discs.phase==KUI_NATIVE_DISC_WAITING && visits==old_visits);
    return_result=1;
    if(!setjmp(stopped)) {native_session_controller_complete(released);assert(!"expected prepared intentional exit");}
    assert(last_error==0x4f && !session.exit_pending && session.return_prepared && return_calls==2u &&
        session_discs.phase==KUI_NATIVE_DISC_IDLE && visits==old_visits);
    /* A failed or mismatched reader snapshot becomes an evidence-preserving
     * fault, rather than showing an unusable return prompt. */
    session.return_prepared=0u;session.exit_pending=1u;return_blocks=4095u;
    if(!setjmp(stopped)) {native_session_controller_complete(released);assert(!"expected card identity refusal");}
    assert(last_error==0x52 && !session.return_prepared && session.exit_pending);
    session.exit_pending=0u;
    session_discs.phase=1u;visit_result=-1;
    if(!setjmp(stopped)) {native_session_controller_complete(released);assert(!"expected disc transition refusal");}
    assert(last_error==0x51);
    /* A completed terminal session can return; a pending transfer, an
     * unpublished completion or any lost worker ownership cannot. */
    state.armed=1u;state.submitted=state.completed=200u;
    lease_owned=stack_owned=hooks_owned=true;
    config.driver.consumer=kui_vmu_native_passive_consumer=0x8c123456u;
    assert(native_session_return_guard());
    state.pending=1u;assert(!native_session_return_guard());state.pending=0u;
    ++state.submitted;assert(!native_session_return_guard());--state.submitted;
    lease_owned=false;assert(!native_session_return_guard());lease_owned=true;
    stack_owned=false;assert(!native_session_return_guard());stack_owned=true;
    hooks_owned=false;assert(!native_session_return_guard());hooks_owned=true;
    ++kui_vmu_native_passive_consumer;assert(!native_session_return_guard());
    struct kui_loader_sd loan={.blocks=8192u,.ready=true};
    config.lease.payload=UINT32_C(0x8c500000);config.lease.lease_bytes=UINT32_C(0x51000);
    assert(!native_session_return_transaction(&loan) && load_calls==1u && stream_stops==1u);
    loan.blocks=4095u;
    assert(native_session_return_transaction(&loan)==-1 && load_calls==1u && stream_stops==1u);
    loan.blocks=8192u;loan.slow=true;
    assert(native_session_return_transaction(&loan)==-1 && load_calls==1u);loan.slow=false;
    loan.ready=false;assert(native_session_return_transaction(&loan)==-1 && load_calls==1u);loan.ready=true;
    load_result=KUI_NATIVE_RETURN_CRC;
    assert(native_session_return_transaction(&loan)==-1 && session.result==KUI_NATIVE_RETURN_CRC && stream_stops==2u);
    load_result=KUI_NATIVE_RETURN_OK;stop_result=KUI_LOADER_SD_CRC;
    assert(native_session_return_transaction(&loan)==-1 && session.result==KUI_NATIVE_RETURN_IO && stream_stops==3u);
    struct kui_loader_sd card={0};struct native_session_return_reader reader={.card=&card};uint8_t block[512];
    session_return_plan.extent_count=2;
    session_return_plan.extents[0]=(struct kui_native_return_extent){0,100,130};
    session_return_plan.extents[1]=(struct kui_native_return_extent){130,700,2};
    for(uint32_t lba=100;lba<230;lba++)assert(native_session_return_read(&reader,lba,block));
    assert(stream_starts==2 && stream_reads==130 && !reader.stream.active);
    assert(native_session_return_read(&reader,700,block));assert(native_session_return_read(&reader,701,block));
    assert(stream_starts==3 && stream_reads==132 && !reader.stream.active);
    assert(!native_session_return_read(&reader,702,block) && stream_starts==3);
    assert(native_session_return_read(&reader,100,block));
    assert(!native_session_return_read(&reader,105,block) && reader.stream.next_lba==101);
    reader.stream.active=false;stream_fail_start=true;
    assert(!native_session_return_read(&reader,100,block));stream_fail_start=false;stream_fail_read=true;
    assert(!native_session_return_read(&reader,100,block));
    puts("PASS actual native session worker: separate held gesture, one cycle per hold, save/publication fencing, deliberate exit after completion and fault propagation");
    puts("PASS actual native return streaming: bounded128-block runs stop at every extent, discontinuities and failures refuse without skipping bytes");
}
'''
    with tempfile.TemporaryDirectory(prefix='kui-session-worker-') as directory:
        folder = Path(directory)
        c = folder / 'worker.c'
        c.write_text(program)
        binary = folder / 'worker'
        subprocess.run([*shlex.split(os.environ.get('CC', 'cc')), '-std=c11', '-O1', '-g',
                        '-Wall', '-Wextra', '-Werror', '-Wpedantic', '-fsanitize=address,undefined',
                        '-fno-omit-frame-pointer', '-fno-pie', '-no-pie', '-I' + str(ROOT / 'include'),
                        '-I' + str(ROOT / 'src/loader'), '-I' + str(ROOT / '.deps/fatfs/source'),
                        str(c), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True, env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0'))
        # The optional module shares the already reviewed compact one-target
        # staging implementation while retaining its full12KiB chain copies.
        # Replay the real READ/WRITE publication and pre-backend second-target
        # refusal fixture with only this profile selector enabled.
        overlay = folder / 'overlay'
        subprocess.run([*shlex.split(os.environ.get('CC', 'cc')), '-std=c11', '-O1', '-g',
                        '-Wall', '-Wextra', '-Werror', '-Wpedantic', '-fsanitize=address,undefined',
                        '-fno-omit-frame-pointer', '-fno-pie', '-no-pie', '-I' + str(ROOT / 'include'),
                        '-I' + str(ROOT / 'src/loader'), '-DKUI_NATIVE_SESSION_CONTROLS=1',
                        *(str(ROOT / name) for name in ('src/core/vmu_readonly.c',
                          'src/core/vmu_emulation.c', 'src/core/vmu_image.c',
                          'src/loader/vmu_readonly_overlay.c', 'tests/test_vmu_lite_overlay.c')),
                        '-o', str(overlay)], check=True)
        subprocess.run([str(overlay)], check=True, env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0'))
        print('PASS session controls overlay: original one-target READ/WRITE and refusal fixture under the new profile')


if __name__ == '__main__':
    main()
