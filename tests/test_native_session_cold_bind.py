#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the production temporary-stage rebase and low publication together.

Only privileged SR/VBR reads, the fixed control-cell alias and the two
address-selected calls are substituted. Admission and mutation are actual C.
P1/P2 addresses overlap ASan shadow space, so this suite uses UBSan.
"""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
cold=(ROOT/"src/loader/native_session_cold_bind.inc").read_text()
low=(ROOT/"src/loader/retail_resident.c").read_text().split("/* NATIVE_BACKGROUND_BIND_BEGIN",1)[1]
low=low[low.index("int kui_vmu_native_background_manifest_bind"):].split("/* NATIVE_BACKGROUND_BIND_END */",1)[0]
for hardware,replacement in (
    ('__asm__ volatile("stc sr,%0":"=r"(sr)::"memory");','sr=fake_sr;'),
    ('__asm__ volatile("stc vbr,%0":"=r"(vbr)::"memory");','vbr=fake_vbr;')):
    assert cold.count(hardware)==1 and low.count(hardware)==1
    cold=cold.replace(hardware,replacement)
    low=low.replace(hardware,replacement)
fixed='''const volatile struct kui_vmu_observe_boot_control *c=
        (const volatile struct kui_vmu_observe_boot_control *)
        ((uintptr_t)&kui_vmu_observe_boot_control+UINT32_C(0x20000000));'''
assert low.count(fixed)==1
low=low.replace(fixed,'const volatile struct kui_vmu_observe_boot_control *c=&fake_control;')
getter='kui_native_session_control getter=(void *)(uintptr_t)getter_entry;'
publisher='''int (*publish)(const struct kui_retail_manifest *,const struct kui_retail_manifest *)=
        (void *)(uintptr_t)publish_entry;'''
assert cold.count(getter)==1 and cold.count(publisher)==1
cold=cold.replace(getter,'kui_native_session_control getter=test_getter;')
cold=cold.replace(publisher,'int (*publish)(const struct kui_retail_manifest *,const struct kui_retail_manifest *)=kui_vmu_native_background_manifest_bind;')
prefix=r'''
#define _GNU_SOURCE
#include "kui/native_session_binding.h"
#include "kui/native_disc_set.h"
#include "kui/vmu_maple_observe_boot.h"
#include "kui/vmu_native_background.h"
#include "retail_async.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#define KUI_RETAIL_RESIDENT_ADDRESS UINT32_C(0x8c004000)
static uint8_t resident_blob[0x3000];
static const uint32_t resident_bytes=sizeof(resident_blob),resident_limit=0x8c007ba0u;
kui_retail_async_region_type *kui_retail_async_region_storage;
static const struct kui_retail_manifest *native_background_manifest;
static struct kui_vmu_observe_boot_control fake_control;
static uint32_t fake_sr,fake_vbr,kui_retail_hook_active,getter_result;
static unsigned checks,publishes,purges;
#define CHECK(x) do {++checks;assert(x);} while(0)
static uint8_t bss[64];
#define __retail_resident_bss_begin bss
#define __retail_resident_bss_end (bss+sizeof(bss))
static struct kui_vmu_observe_boot_control *control(void) {return &fake_control;}
static uint32_t native_word(uint32_t at) {return *(const uint32_t *)(uintptr_t)at;}
static void purge(uint32_t at,uint32_t bytes) {
    if(!purges)CHECK(at==(uint32_t)(uintptr_t)bss && bytes==sizeof(bss) && kui_retail_hook_active==1u);
    else CHECK(purges==1u && at==(uint32_t)(uintptr_t)&kui_retail_hook_active && bytes==4u && !kui_retail_hook_active);
    ++purges;
}
static void kui_native_passive_publish(uint32_t begin,uint32_t end) {
    CHECK(begin==0x8c71c400u && end==begin+KUI_VMU_NATIVE_BACKGROUND_REGION_BYTES);
    CHECK(kui_retail_async_region_storage==(void *)(uintptr_t)0x8ce01000u);
    ++publishes;
}
static int test_getter(uint32_t op,const struct kui_retail_manifest *next,struct kui_loader_sd *sd) {
    CHECK(op==KUI_NATIVE_SESSION_BIND && !next && !sd);return (int)getter_result;
}
'''
suffix=r'''
static struct kui_retail_manifest *temporary,*owned;
static kui_retail_async_region_type *workspace,*retained;
static struct kui_native_session_binding *binding;
#define S (workspace->shared.service)
#define E (workspace->engine)
#define T (workspace->shared.stream)
static void snapshot(void) {memcpy(retained,workspace,KUI_VMU_NATIVE_BACKGROUND_REGION_BYTES);}
static void seal(void) {
    memcpy((void *)(uintptr_t)0xac006800u,binding,sizeof(*binding));
}
static void reset(void) {
    CHECK(sizeof(*workspace)<4096u);
    memset(workspace,0,sizeof(*workspace));memset(retained,0,sizeof(*retained));
    memset(temporary,0x31,sizeof(*temporary));memcpy(owned,temporary,sizeof(*owned));
    memset(&fake_control,0,sizeof(fake_control));memset(resident_blob,0,sizeof(resident_blob));
    fake_control.status=KUI_VMU_OBS_BOOT_LEASED;fake_control.lease=0x0c700000u;
    fake_sr=0x400000f0u;fake_vbr=0x8c00f400u;kui_retail_hook_active=0u;purges=publishes=0u;
    S.initialized=1u;S.tracks=temporary->slots;S.token=0x1234u;S.completed_bytes=4096u;
    E.manifest=temporary;E.disc_generation=1u;E.cursor.done=17u;
    T.saved[0]=0x11111111u;T.saved[1]=0x22222222u;T.saved[2]=0x33333333u;
    native_background_manifest=temporary;kui_retail_async_region_storage=workspace;
    *(uint32_t *)(uintptr_t)0xac007100u=(uint32_t)(uintptr_t)workspace;
    *(uint32_t *)(uintptr_t)0xac007104u=(uint32_t)(uintptr_t)temporary;
    binding=(void *)(resident_blob+0x2800u);
    *binding=(struct kui_native_session_binding){KUI_NATIVE_SESSION_BINDING_MAGIC,1u,24u,
        0x8c007100u,0x8c007104u,0x8c007108u};
    getter_result=0x8c006800u;seal();snapshot();
}
static void refused(uint32_t getter,uint32_t publisher) {
    kui_retail_async_region_type a=*workspace,b=*retained;
    const struct kui_retail_manifest *before=native_background_manifest;
    CHECK(native_session_cold_bind(getter,publisher,temporary,owned)==0);
    CHECK(!memcmp(workspace,&a,sizeof(a)) && !memcmp(retained,&b,sizeof(b)));
    CHECK(native_background_manifest==before && kui_retail_async_region_storage==workspace);
    CHECK(!publishes && !purges);
}
static void bad(void) {refused(0x8c004100u,0x8c004200u);}
static void success(void) {
    kui_retail_async_region_type expected=*workspace;
    expected.shared.service.tracks=owned->slots;expected.engine.manifest=owned;
    if(expected.engine.cursor.manifest)expected.engine.cursor.manifest=owned;
    if(expected.shared.stream.card)expected.shared.stream.card=&retained->shared.card.device.sd;
    if(expected.shared.stream.area[0])expected.shared.stream.area[0]=retained->area0;
    if(expected.shared.stream.area[1])expected.shared.stream.area[1]=retained->area1;
    snapshot();
    CHECK(native_session_cold_bind(0x8c004100u,0x8c004200u,temporary,owned)==1);
    CHECK(native_background_manifest==owned && kui_retail_async_region_storage==retained);
    CHECK(!memcmp(retained,&expected,KUI_VMU_NATIVE_BACKGROUND_REGION_BYTES));
    CHECK(publishes==1u && purges==2u && !kui_retail_hook_active);
}
static void *mapped(uintptr_t address,size_t bytes) {
    void *p=mmap((void *)address,bytes,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    CHECK(p==(void *)address);return p;
}
int main(void) {
    (void)mapped(0xac004000u,0x4000u);
    (void)mapped(0x8c700000u,0x51000u);
    temporary=mapped(0x8ce00000u,0x1000u);workspace=mapped(0x8ce01000u,0x1000u);
    owned=(void *)(uintptr_t)0x8c71c000u;retained=(void *)(uintptr_t)0x8c71c400u;
    reset();success();
    reset();E.cursor.manifest=temporary;T.card=&workspace->shared.card.device.sd;
    T.area[0]=workspace->area0;T.area[1]=workspace->area1;T.state=1u;
    E.opened=1u;workspace->shared.port.acquired=true;success();
    reset();refused(0u,0x8c004200u);reset();refused(0x8c004101u,0x8c004200u);
    reset();refused(0x8c004100u,0x8c007100u);
    const uint32_t pointers[]={0u,0x8c004029u,0x8c006801u,0x8c006ff0u,0xac006800u};
    for(unsigned i=0;i<sizeof(pointers)/sizeof(pointers[0]);i++) {reset();getter_result=pointers[i];bad();}
    reset();binding->magic^=1u;seal();bad();reset();binding->version++;seal();bad();
    reset();binding->bytes--;seal();bad();reset();binding->region_cell=0x8c006ffcu;seal();bad();
    reset();binding->region_cell=0x8c007101u;seal();bad();
    reset();binding->manifest_cell=binding->region_cell;seal();bad();
    reset();binding->firmware_snapshot=0x8c007b80u;seal();bad();
    reset();((uint8_t *)(uintptr_t)0xac006800u)[0]^=1u;bad();
    reset();fake_sr=0xf0u;bad();reset();fake_vbr=0x8ce00f00u;bad();
    reset();fake_control.status=KUI_VMU_OBS_BOOT_INSTALLED;bad();
    reset();fake_control.lease=0x0c700020u;bad();
    reset();*(uint32_t *)(uintptr_t)0xac007100u=0x8c010000u;bad();
    reset();*(uint32_t *)(uintptr_t)0xac007104u=0x8ce00004u;bad();
    uint32_t *busy[]={&S.pending,&S.executing,&E.active,&E.in_irq,&E.hooked,&E.release.armed,
        &E.release.depth,&E.native_loan_state,&workspace->shared.stream.state};
    for(unsigned i=0;i<sizeof(busy)/sizeof(busy[0]);i++) {reset();*busy[i]=2u;snapshot();bad();}
    reset();S.initialized=0u;snapshot();bad();
    reset();workspace->shared.port.fault=true;snapshot();bad();
    reset();E.opened=1u;snapshot();bad();reset();E.manifest=owned;snapshot();bad();
    reset();S.tracks=owned->slots;snapshot();bad();reset();S.ops.context=workspace;snapshot();bad();
    reset();E.cursor.context=workspace;snapshot();bad();
    reset();workspace->shared.card.device.sd.bus.ctx=workspace;snapshot();bad();
    reset();E.cursor.manifest=owned;snapshot();bad();
    reset();T.card=&retained->shared.card.device.sd;snapshot();bad();
    reset();T.area[0]=retained->area0;snapshot();bad();reset();T.area[1]=retained->area1;snapshot();bad();
    reset();((uint8_t *)owned)[0]^=1u;bad();reset();retained->area0[0]^=1u;bad();
    printf("PASS native session cold bind: %u checks; actual stage admission/rebase plus actual low publication, independent ownership refusals\n",checks);
    return 0;
}
'''
flags=("KUI_RETAIL_ASYNC_TEST=1","KUI_RETAIL_ASYNC=1","KUI_RETAIL_GD_ASYNC=1",
       "KUI_RETAIL_MANIFEST_SLOTS=64","KUI_RETAIL_TRANSPORT=1","KUI_SCI_SD_NO_BLOCK=1",
       "KUI_RETAIL_NATIVE_HYBRID=1","KUI_GENERAL_SCI_RESUME_FIX=1",
       "KUI_VMU_NATIVE_BACKGROUND=1","KUI_VMU_NATIVE_PERSISTENT_PROBE=1",
       "KUI_NATIVE_SESSION_CONTROLS=1","KUI_NATIVE_GENERAL_LEAN_LAYOUT=1","KUI_NATIVE_READAHEAD=1")
with tempfile.TemporaryDirectory(prefix="kui-session-cold-") as folder:
    test=Path(folder)/"cold.c";binary=Path(folder)/"cold"
    test.write_text(prefix+low+"\n"+cold+suffix)
    subprocess.run([os.environ.get("HOST_CC","cc"),"-std=c11","-O2","-g","-Wall","-Wextra","-Werror",
        "-fsanitize=undefined","-fno-pie","-no-pie","-Iinclude","-Isrc/loader"]+
        ["-D"+flag for flag in flags]+[str(test),"-o",str(binary)],cwd=ROOT,check=True)
    subprocess.run([str(binary)],cwd=ROOT,check=True)
