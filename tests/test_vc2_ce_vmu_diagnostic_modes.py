#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise actual production mode 3 no-op and mode 1 sampler/fence control.

Only fixed CPU/KData loads and source-bound sampler/provider boundaries are
modeled. The complete production dispatch and A2 gate definitions are
preprocessed and compiled. No guest pointer is mapped by the fixture.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

from test_armada_ce_maple_continuation import definition

ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'src/loader/armada_ce_maple_traffic.c'
FLAGS=['-DKUI_VC2_CE_VMU=1','-DKUI_VC2_VMU_PERSIST=1',
    '-DKUI_ARMADA_CE_MAPLE_A2=1','-DKUI_ARMADA_CE_MAPLE_FENCE=1',
    '-DKUI_ARMADA_CE_MAPLE_WRITE_LAYOUT=1','-DKUI_ARMADA_CE_MAPLE_WRITE_STAGING=1',
    '-DKUI_ARMADA_CE_VMU_WRITE_RUNTIME=1','-DKUI_ARMADA_CE_MAPLE_AUTO_STOP=8']

PREFIX=r'''
#include "kui/armada_ce_maple_rx_slice.h"
#include "kui/armada_ce_maple_fence.h"
#include "kui/armada_ce_maple_continuation.h"
#include "kui/armada_ce_maple_auto.h"
#include "kui/armada_ce_vmu_a2_publish.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct {
    unsigned platform,sampler,resolve,mmio_read,mmio_write,query,payload;
    unsigned backend,publish,store,admit,terminal,held,pending,automatic;
} calls;
struct kui_armada_ce_maple_traffic_snapshot kui_armada_ce_maple_hook_log;
struct kui_armada_ce_maple_fence_snapshot kui_armada_ce_maple_fence_log;
struct kui_armada_ce_vmu_a2_publish_snapshot kui_armada_ce_vmu_a2_log;
volatile uint32_t kui_armada_ce_maple_traffic_freeze;
volatile uint32_t kui_armada_ce_maple_proof_initial,kui_armada_ce_maple_proof_final;
const volatile uint32_t kui_armada_ce_maple_diagnostic_mode=TEST_MODE;
static struct kui_armada_ce_maple_traffic_scratch traffic_scratch;
static struct kui_armada_ce_maple_fence_state fence_state;
static struct kui_armada_ce_vmu_a2_publish_scratch a2_staging;
static struct kui_armada_ce_vmu_a2_input a2_input;
static struct kui_armada_ce_maple_rx_slice_scratch a2_query;
static struct kui_armada_ce_maple_rx_slice a2_slice[2];
static struct {
    uint32_t ttb,process,ccr,records,index[2],needed[2],parent,target,target_bytes;
} a2_context;
typedef uint32_t a2_copy_word __attribute__((__may_alias__));
static uint32_t mmio[4];
static uint32_t platform_read(uint32_t address) {
    ++calls.platform;
    if(address==0xff000008u)return 0x8c1d58a0u;
    if(address==KUI_CE_MAPLE_CURRENT_PROCESS)return KUI_CE_MAPLE_PROCESS_BASE;
    if(address==0xff00001cu)return 0x105u;
    assert(address==KUI_CE_MAPLE_CURRENT_THREAD);return 0x8c080000u;
}
static bool traffic_read_p1(void *context,uint32_t address,uint32_t *out) {
    (void)context;(void)address;(void)out;++calls.admit;return false;
}
static bool traffic_read_cache(void *context,uint32_t address,uint32_t *out) {
    return traffic_read_p1(context,address,out);
}
static bool traffic_read_p2(void *context,uint32_t address,uint32_t *out) {
    return traffic_read_p1(context,address,out);
}
static unsigned mmio_index(uint32_t address) {
    switch(address) {
    case KUI_ARMADA_CE_MAPLE_FENCE_MDSTAR:return 0;
    case KUI_ARMADA_CE_MAPLE_FENCE_MDTSEL:return 1;
    case KUI_ARMADA_CE_MAPLE_FENCE_MDEN:return 2;
    case KUI_ARMADA_CE_MAPLE_FENCE_MDST:return 3;
    default:assert(0);return 0;
    }
}
static bool fence_read_mmio(void *context,uint32_t address,uint32_t *out) {
    assert(!context);++calls.mmio_read;*out=mmio[mmio_index(address)];return true;
}
static bool fence_write_mmio(void *context,uint32_t address,uint32_t value) {
    assert(!context && address==KUI_ARMADA_CE_MAPLE_FENCE_MDEN && value<=1u);
    ++calls.mmio_write;mmio[2]=value;return true;
}
static enum kui_armada_ce_maple_traffic_status fake_visit(uint32_t raw,uint32_t ttb,
    uint32_t process,uint32_t ccr,kui_armada_ce_ram_read32 p1,kui_armada_ce_ram_read32 cache,
    kui_armada_ce_ram_read32 p2,void *context,struct kui_armada_ce_maple_traffic_scratch *scratch,
    struct kui_armada_ce_maple_traffic_snapshot *snapshot) {
    assert(raw==0x30a60u && ttb==0x8c1d58a0u && process==KUI_CE_MAPLE_PROCESS_BASE && ccr==0x105u);
    assert(p1==traffic_read_p1 && cache==traffic_read_cache && p2==traffic_read_p2 && !context);
    ++calls.sampler;memset(scratch,0,sizeof(*scratch));
    scratch->word[1]=0x0c020000u;scratch->word[5]=0x60c00u;
    /* A parent GETCOND and A2 READ are intentionally present. Removing the
     * mode1 gate must enter the closed query boundary and fail this control. */
    scratch->word[2]=0x02010000u;scratch->word[3]=0x02010010u;
    scratch->row[0][3]=0x01002009u;scratch->row[1][0]=16u;scratch->row[1][3]=0x0200020bu;
    snapshot->counts[0]=KUI_ARMADA_CE_MAPLE_TRAFFIC_MAGIC;return KUI_ARMADA_CE_MAPLE_TRAFFIC_OK;
}
static enum kui_armada_ce_ram_bind_status fake_resolve(uint32_t ttb,uint32_t address,
    kui_armada_ce_ram_read32 read,void *context,struct kui_armada_ce_maple_mapping *out) {
    assert(ttb==0x8c1d58a0u && address==0x02060c00u && read==traffic_read_p1 && !context);
    ++calls.resolve;memset(out,0,sizeof(*out));out->page_word=0x005f6001u;
    return KUI_ARMADA_CE_RAM_BIND_DESTINATION;
}
static enum kui_armada_ce_maple_rx_slice_status fake_query(
    const struct kui_armada_ce_maple_traffic_scratch *capture,unsigned records,unsigned index,
    uint32_t ttb,uint32_t process,uint32_t ccr,uint32_t min_bytes,
    kui_armada_ce_ram_read32 p1,kui_armada_ce_ram_read32 cache,kui_armada_ce_ram_read32 p2,void *context,
    struct kui_armada_ce_maple_rx_slice_scratch *scratch,struct kui_armada_ce_maple_rx_slice *out) {
    (void)capture;(void)records;(void)index;(void)ttb;(void)process;(void)ccr;(void)min_bytes;
    (void)p1;(void)cache;(void)p2;(void)context;(void)scratch;(void)out;
    ++calls.query;return KUI_ARMADA_CE_MAPLE_RX_SLICE_ARGUMENT;
}
static bool a2_payload(unsigned index,unsigned words,uint8_t *out) {
    (void)index;(void)words;(void)out;++calls.payload;return false;
}
static int a2_backend_call(void *context,const uint8_t *request,size_t bytes,
    uint8_t *reply,size_t capacity,size_t *produced) {
    (void)context;(void)request;(void)bytes;(void)reply;(void)capacity;(void)produced;
    ++calls.backend;return -1;
}
static bool a2_recheck(void *context) {(void)context;++calls.admit;return false;}
static bool a2_store32(void *context,uint32_t address,uint32_t value) {
    (void)context;(void)address;(void)value;++calls.store;return false;
}
enum kui_armada_ce_vmu_a2_publish_result kui_armada_ce_vmu_a2_publish(
    const struct kui_armada_ce_vmu_a2_input *input,kui_armada_ce_vmu_a2_backend backend,void *backend_context,
    kui_armada_ce_vmu_a2_admit admit,void *admit_context,kui_armada_ce_vmu_a2_store32 store,void *store_context,
    struct kui_armada_ce_vmu_a2_publish_scratch *scratch,struct kui_armada_ce_vmu_a2_publish_snapshot *snapshot) {
    (void)input;(void)backend;(void)backend_context;(void)admit;(void)admit_context;
    (void)store;(void)store_context;(void)scratch;(void)snapshot;++calls.publish;
    return KUI_ARMADA_CE_VMU_A2_PUBLISH_ARGUMENT;
}
static unsigned a2_terminal(unsigned why,uint32_t detail) {
    (void)why;(void)detail;++calls.terminal;return KUI_ARMADA_CE_MAPLE_TERMINAL;
}
static bool a2_held(void) {++calls.held;return false;}
static unsigned a2_pending(void) {++calls.pending;return KUI_ARMADA_CE_MAPLE_SERVICE;}
static void a2_auto_capture(unsigned phase) {(void)phase;++calls.automatic;}
#define kui_armada_ce_maple_traffic_visit fake_visit
#define kui_armada_ce_maple_resolve fake_resolve
#define kui_armada_ce_maple_rx_slice_query fake_query
'''

HARNESS=r'''
static void clear(void) {
    memset(&calls,0,sizeof(calls));memset(&traffic_scratch,0,sizeof(traffic_scratch));
    memset(&fence_state,0,sizeof(fence_state));memset(&a2_continuation,0,sizeof(a2_continuation));
    memset(&kui_armada_ce_maple_hook_log,0,sizeof(kui_armada_ce_maple_hook_log));
    memset(&kui_armada_ce_maple_fence_log,0,sizeof(kui_armada_ce_maple_fence_log));
    memset(&kui_armada_ce_vmu_a2_log,0,sizeof(kui_armada_ce_vmu_a2_log));
    kui_armada_ce_maple_traffic_freeze=kui_armada_ce_maple_proof_initial=kui_armada_ce_maple_proof_final=0u;
    mmio[0]=0x0c020000u;mmio[1]=mmio[2]=1u;mmio[3]=0u;
}
static void zero_snapshot(const void *p,size_t bytes) {
    const unsigned char *data=p;for(size_t i=0;i<bytes;i++)assert(!data[i]);
}
int main(void) {
    if(TEST_MODE==3) {
        for(unsigned phase=0u;phase<3u;phase++)for(unsigned frozen=0u;frozen<2u;frozen++) {
            clear();kui_armada_ce_maple_traffic_freeze=frozen;
            /* Deliberately invalid/unmapped guest pointers and poisoned live
             * state would fault or terminate if the early gate ran late. */
            memset(&a2_continuation,0xa5,sizeof(a2_continuation));
            memset(&traffic_scratch,0x5a,sizeof(traffic_scratch));
            memset(&fence_state,0xa5,sizeof(fence_state));
            unsigned char saved_continuation[sizeof(a2_continuation)],saved_capture[sizeof(traffic_scratch)],saved_fence[sizeof(fence_state)];
            memcpy(saved_continuation,&a2_continuation,sizeof(saved_continuation));
            memcpy(saved_capture,&traffic_scratch,sizeof(saved_capture));
            memcpy(saved_fence,&fence_state,sizeof(saved_fence));
            assert(kui_armada_ce_maple_traffic_run(0xfffffffcu,phase,0xdeadbeefu,UINT32_MAX)==KUI_ARMADA_CE_MAPLE_CONTINUE);
            zero_snapshot(&calls,sizeof(calls));
            zero_snapshot(&kui_armada_ce_maple_hook_log,sizeof(kui_armada_ce_maple_hook_log));
            zero_snapshot(&kui_armada_ce_maple_fence_log,sizeof(kui_armada_ce_maple_fence_log));
            zero_snapshot(&kui_armada_ce_vmu_a2_log,sizeof(kui_armada_ce_vmu_a2_log));
            assert(!memcmp(saved_continuation,&a2_continuation,sizeof(saved_continuation)));
            assert(!memcmp(saved_capture,&traffic_scratch,sizeof(saved_capture)));
            assert(!memcmp(saved_fence,&fence_state,sizeof(saved_fence)));
            assert(!kui_armada_ce_maple_proof_initial && !kui_armada_ce_maple_proof_final);
        }
        puts("PASS mode3 actual production PRE/POST/RESUME: zero owner/MMIO/sampler/provider calls, snapshots/state preserved");
    } else {
        assert(TEST_MODE==1);clear();
        assert(kui_armada_ce_maple_traffic_run(0x30a60u,0u,0u,0x40008000u)==KUI_ARMADA_CE_MAPLE_CONTINUE);
        assert(calls.platform==3u && calls.sampler==1u && calls.resolve==1u);
        assert(calls.mmio_read && calls.mmio_write==1u && !mmio[2]);
        assert(fence_state.active && fence_state.pending);
        assert(kui_armada_ce_maple_hook_log.counts[0]==KUI_ARMADA_CE_MAPLE_TRAFFIC_MAGIC);
        assert(kui_armada_ce_maple_traffic_run(0x30a60u,1u,0u,0x40008000u)==KUI_ARMADA_CE_MAPLE_CONTINUE);
        assert(calls.platform==3u && calls.sampler==1u && calls.resolve==1u);
        assert(calls.mmio_write==2u && mmio[2]==1u && !fence_state.active && !fence_state.pending);
        assert(!calls.query && !calls.payload && !calls.backend && !calls.publish && !calls.store && !calls.admit);
        assert(!calls.terminal && !calls.held && !calls.pending);
        zero_snapshot(&kui_armada_ce_vmu_a2_log,sizeof(kui_armada_ce_vmu_a2_log));
        puts("PASS mode1 actual production sampler+DMA fence PRE/POST: no A2/provider work");
    }
    return 0;
}
'''


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc',default=os.environ.get('CC','cc'))
    parser.add_argument('--json',type=Path)
    args=parser.parse_args();compiler=shlex.split(args.cc)
    preprocessed=subprocess.check_output(compiler+['-E','-P','-std=c11','-I'+str(ROOT/'include'),
        *FLAGS,str(SOURCE)],text=True)
    state=re.search(r'(?ms)^static struct \{\n\s*uint32_t pending,rounds,raw,mode,sr,result,checked;.*?\} a2_continuation;',preprocessed)
    if not state:raise ValueError('Production continuation state missing')
    names=('a2_same_words','a2_fault','a2_minimum','a2_run','kui_armada_ce_maple_traffic_run')
    bodies='\n'.join(definition(preprocessed,name) for name in names)
    bodies,loads=re.subn(r'\*\(volatile const uint32_t \*\)\(uintptr_t\)(0x[0-9a-f]+[uU]?)',
        r'platform_read(\1)',bodies)
    if loads!=4:raise ValueError(f'Expected four fixed platform reads, found {loads}')
    report=dict(status='PASS',production_functions=list(names),modes={},sanitizers=['address','undefined'],
        source_sha256=hashlib.sha256(SOURCE.read_bytes()).hexdigest())
    with tempfile.TemporaryDirectory(prefix='vc2-mode-controls-') as temporary:
        folder=Path(temporary)
        fixture=folder/'fixture.c';fixture.write_text(PREFIX+state.group()+'\n'+bodies+HARNESS)
        def run_fixture(path,binary,mode):
            subprocess.run(compiler+['-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
                '-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-pie','-no-pie',
                '-I'+str(ROOT/'include'),*FLAGS,f'-DTEST_MODE={mode}',str(path),'-o',str(binary)],check=True)
            return subprocess.run([str(binary)],capture_output=True,text=True,
                env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0:halt_on_error=1'))
        for mode in (1,3):
            binary=folder/f'mode{mode}'
            run=run_fixture(fixture,binary,mode)
            if run.returncode:raise RuntimeError(run.stdout+run.stderr)
            report['modes'][str(mode)]=dict(status='PASS',result=run.stdout.strip())
            if mode==3:
                pattern=r'if\s*\(kui_armada_ce_maple_diagnostic_mode==3u\)\s*return 0;'
            else:pattern=r'if\s*\(diagnostic==1u\)\s*return;'
            changed,count=re.subn(pattern,'',bodies)
            if count!=1:raise ValueError(f'Expected one production mode{mode} gate, found {count}')
            mutant=folder/f'mutant{mode}.c'
            mutant.write_text(PREFIX+state.group()+'\n'+changed+HARNESS)
            refused=run_fixture(mutant,folder/f'mutant{mode}',mode)
            if not refused.returncode:raise RuntimeError(f'Mode{mode} admitted its removed-gate mutant')
            report['modes'][str(mode)]['removed_gate_mutant_rejected']=True
    output=json.dumps(report,indent=2)+'\n'
    if args.json:args.json.write_text(output)
    print(output,end='')


if __name__=='__main__':main()
