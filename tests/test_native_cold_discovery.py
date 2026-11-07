#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compare production factored discovery against the original inline owner.

Core image recognizers are unchanged and retain their own authored/executable
tests. Adverse predicate fixtures here bind ordering, partial private outputs,
every original refusal, and separation of live coherence/capture from the leaf.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]

def function(source,name):
    at=source.index(name+'(')
    at=source.rfind('\n',0,at)+1
    first=source.index('{',at);depth=1;end=first+1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[at:end]

PREFIX=r'''
#include "kui/vmu_native_discovery.h"
#include "kui/vmu_native_layout.h"
#include "kui/vmu_native_heap.h"
#include "kui/vmu_native_exit.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#define KUI_RETAIL_EXEC_ADDRESS UINT32_C(0x8c010000)
#define KUI_VMU_NATIVE_PASSIVE_PROBE 1
#define KUI_VMU_NATIVE_WORKER_PROBE 0
#define UNUSED __attribute__((unused))
static struct kui_vmu_native_discovery native_driver;
static struct kui_vmu_native_layout passive_layout;
static struct kui_vmu_native_heap native_heap;
static struct kui_vmu_native_heap_call native_call;
static struct kui_vmu_native_exit native_exits;
static uint32_t native_arena,native_fixed,exec_bytes=4096u,mode;
static uint32_t events[40],event_count,captured[20],failure_phase,failure_detail;
static jmp_buf stopped;
static void event(unsigned value){assert(event_count<40u);events[event_count++]=value;}
static void retail_display_line(const char *text){(void)text;}
static void native_report(uint32_t phase,uint32_t detail){failure_phase=phase;failure_detail=detail;event(99u);longjmp(stopped,1);}
static bool bit(unsigned n){return (mode&(1u<<n))!=0u;}
bool kui_vmu_native_discover_layout(const void *p,size_t n,uint32_t load,
    struct kui_vmu_native_discovery *d,struct kui_vmu_native_layout *l){
    assert(p==(const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS && n==exec_bytes && load==KUI_RETAIL_EXEC_ADDRESS);
    event(1u);if(!bit(0u))return false;
    memset(d,0x12,sizeof(*d));memset(l,0x23,sizeof(*l));
    d->family=bit(10u)?KUI_VMU_NATIVE_FAMILY_EARLY_P2_V1:KUI_VMU_NATIVE_FAMILY_EARLY_V1;
    d->submit=load+0x100u;l->initializer_call=load+0x200u;return true;
}
enum kui_vmu_native_heap_result kui_vmu_native_heap_discover(const void *p,size_t n,uint32_t load,
    struct kui_vmu_native_heap *h){
    assert(p==(const void *)(uintptr_t)load && n==exec_bytes);event(2u);memset(h,0x34,sizeof(*h));
    h->initializer=load+0x300u;h->release=load+0x400u+(bit(11u)?2u:0u);
    h->initializer_slot=bit(4u)?load+0x500u:0u;
    return (enum kui_vmu_native_heap_result)((int)((mode>>1u)&3u)-2);
}
static bool UNUSED native_arena_plan_bind(bool replayed){
    assert(!replayed);event(3u);if(!bit(5u))return false;
    native_arena=native_fixed=1u;native_heap.family=KUI_VMU_NATIVE_HEAP_ARENA;
    native_call=(struct kui_vmu_native_heap_call){1u,2u,3u,4u,5u};return true;
}
static bool UNUSED native_fixed_plan_bind(bool replayed){
    assert(!replayed);event(4u);if(!bit(5u))return false;
    native_fixed=1u;native_heap.initializer_slot=0x8c010700u;
    native_call=(struct kui_vmu_native_heap_call){6u,7u,8u,9u,10u};return true;
}
bool kui_vmu_native_heap_call_discover(const void *p,size_t n,const struct kui_vmu_native_heap *h,
    struct kui_vmu_native_heap_call *c){
    assert(p==(const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS && n==exec_bytes && h==&native_heap);
    event(5u);if(!bit(6u))return false;*c=(struct kui_vmu_native_heap_call){11u,12u,13u,14u,15u};return true;
}
static bool allocation_safe(uint32_t begin,uint32_t bytes){
    assert(begin==native_call.entry_begin && bytes==native_call.entry_bytes);event(6u);return bit(7u);
}
bool kui_vmu_native_early_p2_calls_valid(const void *p,size_t n,
    const struct kui_vmu_native_discovery *d,uint32_t heap,uint32_t maple){
    assert(p==(const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS && n==exec_bytes && d==&native_driver);
    assert(heap==native_call.call && maple==passive_layout.initializer_call);event(7u);return bit(8u);
}
static bool UNUSED readonly_release_replay_valid(void){event(8u);return bit(9u);}
bool kui_vmu_native_exit_discover_guarded(const void *p,size_t n,uint32_t load,
    const struct kui_vmu_native_patch_window *windows,size_t count,struct kui_vmu_native_exit *out){
    assert(p==(const void *)(uintptr_t)load && n==exec_bytes && windows[0].begin==native_driver.submit);
    assert(count==(native_arena?1u:3u));event(9u);if(!bit(12u))return false;
    memset(out,0x45,sizeof(*out));return true;
}
bool kui_vmu_native_patch_no_interiors(const void *p,size_t n,uint32_t load,
    const struct kui_vmu_native_patch_window *windows,size_t count){
    assert(p==(const void *)(uintptr_t)load && n==exec_bytes && windows && count);event(9u);return bit(12u);
}
bool kui_vmu_native_patch_no_interior(const void *p,size_t n,uint32_t load,uint32_t at,uint32_t bytes){
    assert(p==(const void *)(uintptr_t)load && n==exec_bytes && at==native_driver.submit && bytes==12u);
    event(9u);return bit(12u);
}
static bool UNUSED native_sdk_coherence_cached_admit(void){event(10u);return bit(13u);}
/* Factoring equivalence keeps the coherence result/event identical. The
 * separate production SDK test accounts for its removed duplicate role scan. */
static bool UNUSED native_sdk_coherence_after_image_proof(void){return native_sdk_coherence_cached_admit();}
static void UNUSED native_passive_capture_windows(void){event(11u);for(unsigned i=0;i<20u;i++)captured[i]=native_heap.initializer+i;}
struct observation {
    struct kui_vmu_native_discovery driver;
    struct kui_vmu_native_layout layout;
    struct kui_vmu_native_heap heap;
    struct kui_vmu_native_heap_call call;
    struct kui_vmu_native_exit exits;
    uint32_t arena,fixed,phase,detail,count,events[40],captured[20];
};
static void reset(void){
    memset(&native_driver,0,sizeof(native_driver));memset(&passive_layout,0,sizeof(passive_layout));
    memset(&native_heap,0,sizeof(native_heap));memset(&native_call,0,sizeof(native_call));
    memset(&native_exits,0,sizeof(native_exits));memset(events,0,sizeof(events));memset(captured,0,sizeof(captured));
    native_arena=native_fixed=failure_phase=failure_detail=event_count=0u;
}
static struct observation observe(void){
    struct observation r={0};r.driver=native_driver;r.layout=passive_layout;r.heap=native_heap;
    r.call=native_call;r.exits=native_exits;r.arena=native_arena;r.fixed=native_fixed;
    r.phase=failure_phase;r.detail=failure_detail;r.count=event_count;
    memcpy(r.events,events,sizeof(events));memcpy(r.captured,captured,sizeof(captured));return r;
}
'''

SUFFIX=r'''
static struct observation original(void){reset();if(!setjmp(stopped))reference_miss();return observe();}
static struct observation factored(void){
    reset();if(!setjmp(stopped)){
        uint32_t detail=0u;
        if(!kui_native_stage_discover_image((const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,
            exec_bytes,KUI_RETAIL_EXEC_ADDRESS,&detail))native_report(2u,detail);
        native_passive_prepare_finish();
    }return observe();
}
int main(void){
    unsigned successes=0u,refusals=0u;
    for(mode=0u;mode<16384u;mode++){
        struct observation old=original(),new=factored();
        if(memcmp(&old,&new,sizeof(old))){fprintf(stderr,"Discovery differs at%u\n",mode);return 1;}
        if(new.phase)++refusals;else ++successes;
    }
    assert(successes && refusals);
    printf("PASS exact original miss chain: %u successes,%u refusals, outputs and predicate order identical\n",successes,refusals);
    return 0;
}
'''

def audit():
    passive=(ROOT/'src/loader/vmu_native_passive_stage.inc').read_text()
    image=function(passive,'native_passive_prepare_image')
    finish=function(passive,'native_passive_prepare_finish')
    leaf=(ROOT/'src/loader/native_cold_discovery.inc').read_text()
    original=(ROOT/'tests/fixtures/native-cold-miss-original.inc').read_text()
    assert 'native_report' not in image and 'native_sdk_coherence' not in image
    assert not any(x in leaf for x in ('native_report(', 'retail_display_', 'native_passive_prepare_finish('))
    assert not any(x in leaf+image for x in ('native_passive_capture_windows(',
        'native_sdk_coherence_cached_admit(', 'kui_retail_storage_', 'native_word('))
    env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1')
    runs=[]
    with tempfile.TemporaryDirectory(prefix='native-cold-discovery-') as tmp:
        path=Path(tmp);source=path/'test.c';binary=path/'test'
        for dual in (0,1):
            for readonly,persistent in ((0,0),(1,0),(1,1)):
                source.write_text(PREFIX+original+'\n'+image+'\n'+finish+'\n'+leaf+SUFFIX)
                subprocess.run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wpedantic',
                    '-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-pie','-no-pie','-Iinclude',
                    f'-DKUI_NATIVE_DUAL_READERS={dual}',f'-DKUI_VMU_NATIVE_READONLY_PROBE={readonly}',
                    f'-DKUI_VMU_NATIVE_PERSISTENT_PROBE={persistent}',str(source),'-o',str(binary)],cwd=ROOT,check=True)
                result=subprocess.run([str(binary)],cwd=ROOT,env=env,check=True,text=True,capture_output=True)
                runs.append(result.stdout.strip())
        mutants=(
            ('exit proof omitted',image,finish,leaf.replace('*detail=native_passive_prepare_image();','*detail=0u;')),
            ('allocation predicate omitted',image,finish,leaf.replace('!allocation_safe(native_call.entry_begin,native_call.entry_bytes)','0')),
            ('early role refusal omitted',image.replace('return 0x76u;','return 0u;'),finish,leaf),
            ('live coherence omitted',image,finish.replace('if(!native_sdk_coherence_after_image_proof())','if(0 && !native_sdk_coherence_after_image_proof())'),leaf),
        )
        for label,mut_image,mut_finish,mut_leaf in mutants:
            assert (mut_image,mut_finish,mut_leaf)!=(image,finish,leaf),label
            source.write_text(PREFIX+original+'\n'+mut_image+'\n'+mut_finish+'\n'+mut_leaf+SUFFIX)
            subprocess.run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wpedantic',
                '-Wno-unused-function','-fsanitize=address,undefined','-fno-sanitize-recover=all',
                '-fno-pie','-no-pie','-Iinclude','-DKUI_NATIVE_DUAL_READERS=1',
                '-DKUI_VMU_NATIVE_READONLY_PROBE=1','-DKUI_VMU_NATIVE_PERSISTENT_PROBE=1',
                str(source),'-o',str(binary)],cwd=ROOT,check=True)
            result=subprocess.run([str(binary)],cwd=ROOT,env=env,text=True,capture_output=True)
            assert result.returncode==1 and 'Discovery differs at' in result.stderr,label
        runs.append('PASS4 unsafe shortcut controls rejected: exit, allocation, early role and live coherence')
    return runs

if __name__=='__main__':
    argparse.ArgumentParser(description=__doc__).parse_args()
    for line in audit():print(line)
