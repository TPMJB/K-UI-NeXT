#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Same-invocation immutable role reuse retains the actual coherence predicate."""
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile
from test_native_cold_discovery import function

ROOT=Path(__file__).resolve().parents[1]
PREFIX=r'''
#include "kui/vmu_native_discovery.h"
#include "kui/vmu_native_layout.h"
#include "kui/vmu_native_heap.h"
#include <assert.h>
#include <stdio.h>
#define KUI_RETAIL_EXEC_ADDRESS UINT32_C(0x8c010000)
static struct kui_vmu_native_discovery native_driver;
static struct kui_vmu_native_layout passive_layout;
static struct kui_vmu_native_heap_call native_call;
static size_t exec_bytes=4096u;
static bool role_ok,slot_ok;
static unsigned roles,slots;
bool kui_vmu_native_early_p2_calls_valid(const void *image,size_t bytes,
    const struct kui_vmu_native_discovery *driver,uint32_t heap,uint32_t maple){
    assert(image==(const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS && bytes==exec_bytes);
    assert(driver==&native_driver && heap==native_call.call && maple==passive_layout.initializer_call);
    ++roles;return role_ok;
}
static bool native_sdk_coherence_slot(uint32_t *out){++slots;*out=0x8c010800u;return slot_ok;}
'''
SUFFIX=r'''
int main(void){
    unsigned cases=0u;
    for(unsigned family=1u;family<=3u;family++)for(unsigned good=0u;good<4u;good++){
        native_driver.family=family;native_call.call=0x8c010100u;passive_layout.initializer_call=0x8c010200u;
        role_ok=(good&1u)!=0u;slot_ok=(good&2u)!=0u;
        roles=slots=0u;bool old=reference_sdk_coherence_admit();unsigned old_roles=roles,old_slots=slots;
        roles=slots=0u;bool complete=native_sdk_coherence_cached_admit();
        assert(old==complete && roles==old_roles && slots==old_slots);
        /* An unsuccessful first image proof never reaches checked admission.
         * Successful cache replay and fresh image discovery both establish
         * these exact immutable roles before either slot consumer runs. */
        roles=slots=0u;
        bool first=family!=KUI_VMU_NATIVE_FAMILY_EARLY_P2_V1 ||
            kui_vmu_native_early_p2_calls_valid((const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,
                exec_bytes,&native_driver,native_call.call,passive_layout.initializer_call);
        bool before=first && reference_sdk_coherence_admit();
        unsigned before_roles=roles,before_slots=slots;
        roles=slots=0u;
        first=family!=KUI_VMU_NATIVE_FAMILY_EARLY_P2_V1 ||
            kui_vmu_native_early_p2_calls_valid((const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,
                exec_bytes,&native_driver,native_call.call,passive_layout.initializer_call);
        bool after=first && native_sdk_coherence_after_image_proof();
        assert(before==after && slots==before_slots);
        assert(roles+(family==KUI_VMU_NATIVE_FAMILY_EARLY_P2_V1 && role_ok?1u:0u)==before_roles);
        if(family==KUI_VMU_NATIVE_FAMILY_EARLY_P2_V1 && role_ok)assert(before_roles==2u && roles==1u);
        ++cases;
    }
    printf("PASS%u production coherence cases: identical complete admission/refusals; EARLY_P2 immutable roles2->1 and every live slot check retained\n",cases);
    return 0;
}
'''

def audit():
    source=(ROOT/'src/loader/vmu_native_sdk_coherence_stage.inc').read_text()
    checked=function(source,'native_sdk_coherence_after_image_proof')
    complete=function(source,'native_sdk_coherence_cached_admit')
    assert hashlib.sha256(function(source,'native_sdk_coherence_install').encode()).hexdigest()==\
        '16b3473ce0937f12df8c936224d7a07f9f19363a5934ecd189d47f283e937379',\
        'The independently fresh live SDK installation predicate changed'
    original=(ROOT/'tests/fixtures/native-sdk-coherence-admit-original.inc').read_text()
    assert 'native_sdk_coherence_slot(&slot)' in checked
    assert 'kui_vmu_native_early_p2_calls_valid' not in checked
    core=(ROOT/'src/core/vmu_native_resolution_cache.c').read_text()
    replay=function(core,'kui_vmu_native_resolution_cache_replay')
    assert replay.index('hash(image,n,digest)')<replay.index('kui_vmu_native_early_p2_calls_valid')<replay.index('*out=decoded')
    assert 'memcmp(digest,decoded.driver.image_sha256,32u)' in replay
    assert 'decoded.call.call,decoded.layout.initializer_call' in replay
    assert 'memcmp(&call,&decoded.call,sizeof(call))' in replay
    passive=(ROOT/'src/loader/vmu_native_passive_stage.inc').read_text()
    image=function(passive,'native_passive_prepare_image')
    finish=function(passive,'native_passive_prepare_finish')
    assert 'kui_vmu_native_early_p2_calls_valid' in image and 'native_call.call,passive_layout.initializer_call' in image
    assert 'native_sdk_coherence_after_image_proof' in finish
    linked_replay=function(passive,'native_resolution_replay')
    assert linked_replay.index('return false;')<linked_replay.index('native_driver=native_cached_resolution.driver')<linked_replay.index('native_sdk_coherence_after_image_proof')
    env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1')
    with tempfile.TemporaryDirectory(prefix='native-role-reuse-') as tmp:
        path=Path(tmp);c=path/'coherence.c';binary=path/'coherence'
        c.write_text(PREFIX+original+'\n'+checked+'\n'+complete+SUFFIX)
        subprocess.run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wpedantic',
            '-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-pie','-no-pie','-Iinclude',
            str(c),'-o',str(binary)],cwd=ROOT,check=True)
        result=subprocess.run([str(binary)],cwd=ROOT,env=env,text=True,capture_output=True,check=True)
        output=result.stdout.strip()
        for label,mut_checked,mut_complete in (
            ('live slot omission',checked.replace('uint32_t slot;return native_sdk_coherence_slot(&slot);','return true;'),complete),
            ('standalone role omission',checked,complete.replace('if(native_driver.family==KUI_VMU_NATIVE_FAMILY_EARLY_P2_V1 &&','if(0 && native_driver.family==KUI_VMU_NATIVE_FAMILY_EARLY_P2_V1 &&')),
        ):
            assert (mut_checked,mut_complete)!=(checked,complete),label
            c.write_text(PREFIX+original+'\n'+mut_checked+'\n'+mut_complete+SUFFIX)
            subprocess.run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wpedantic',
                '-Wno-unused-function','-fsanitize=address,undefined','-fno-sanitize-recover=all',
                '-fno-pie','-no-pie','-Iinclude',str(c),'-o',str(binary)],cwd=ROOT,check=True)
            bad=subprocess.run([str(binary)],cwd=ROOT,env=env,text=True,capture_output=True)
            assert bad.returncode!=0 and 'Assertion' in bad.stderr,label
    return output+'\nPASS2 unsafe omission controls rejected; later live-install source exact'

if __name__=='__main__':print(audit())
