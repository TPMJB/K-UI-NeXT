#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the production cold map binder against independent ownership faults.

Only STC and the console's P2 control-cell address are replaced. The admission,
pointer updates, copied-byte comparison and cache-publication calls are the
actual body from retail_resident.c, not a second binder implementation.
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
source=(ROOT/'src/loader/retail_resident.c').read_text()
body=source.split('/* NATIVE_BACKGROUND_BIND_BEGIN',1)[1]
body=body[body.index('int kui_vmu_native_background_manifest_bind'):]
body=body.split('/* NATIVE_BACKGROUND_BIND_END */',1)[0]
hardware='__asm__ volatile("stc sr,%0":"=r"(sr)::"memory");'
assert body.count(hardware)==1
body=body.replace(hardware,'sr=fake_sr;')
hardware_vbr='__asm__ volatile("stc vbr,%0":"=r"(vbr)::"memory");'
assert body.count(hardware_vbr)==1
body=body.replace(hardware_vbr,'vbr=fake_vbr;')
control='''const volatile struct kui_vmu_observe_boot_control *c=
        (const volatile struct kui_vmu_observe_boot_control *)
        ((uintptr_t)&kui_vmu_observe_boot_control+UINT32_C(0x20000000));'''
assert body.count(control)==1
body=body.replace(control,'const volatile struct kui_vmu_observe_boot_control *c=&fake_control;')

prefix=r'''

#define _GNU_SOURCE
#define KUI_RETAIL_MANIFEST_SLOTS 64
#include "kui/vmu_native_background.h"
#include "kui/vmu_maple_observe_boot.h"
#include "kui/retail_image.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

/* Host pointers are wider. Keep the native region's vector/area offsets and
 * extent exact while independently modeling only the fields the binder uses. */
struct fixture_service { const union kui_retail_slot *tracks;
    struct {void *context;} ops;
    uint32_t initialized,pending,executing,token,completed_bytes,status; };
struct fixture_sd {struct {void *ctx;} bus;uint32_t identity;};
struct fixture_card {struct {struct fixture_sd sd;} device;uint32_t stamp;};
struct fixture_stream {const struct fixture_sd *card;uint8_t *area[2];uint32_t state,saved[4],position;};
struct fixture_engine {const struct kui_retail_manifest *manifest;
    struct {const struct kui_retail_manifest *manifest;void *context;uint32_t position;} cursor;
    struct {uint32_t armed,depth;} release;
    uint32_t active,in_irq,hooked,opened,stamp;};
struct fixture_shared {struct fixture_service service;struct fixture_card card;
    struct fixture_stream stream;struct {uint32_t acquired,fault,stamp;} port;uint32_t stamp;};
typedef struct {
    uint32_t vector100[8];uint8_t area0[544] __attribute__((aligned(32)));
    union {struct fixture_engine engine;uint8_t gap_a[0xc0];};
    uint32_t vector400[8];
    union {struct fixture_shared shared;uint8_t gap_b[0x1e0];};
    uint32_t vector600[16];uint8_t area1[544] __attribute__((aligned(32)));
} kui_retail_async_region_type;
_Static_assert(sizeof(kui_retail_async_region_type)==0x760,"Host region extent");
_Static_assert(offsetof(kui_retail_async_region_type,engine)==0x240,"Host engine offset");
_Static_assert(offsetof(kui_retail_async_region_type,shared)==0x320,"Host shared offset");
_Static_assert(offsetof(kui_retail_async_region_type,area1)==0x540,"Host area offset");
static const struct kui_retail_manifest *native_background_manifest;
static kui_retail_async_region_type *kui_retail_async_region_storage;
static struct kui_vmu_observe_boot_control fake_control;
static uint32_t fake_sr,fake_vbr,kui_retail_hook_active,stream_busy,purges;
static uint8_t fake_bss[64];
#define __retail_resident_bss_begin fake_bss
#define __retail_resident_bss_end (fake_bss+sizeof(fake_bss))
static bool kui_sci_stream_busy(void) {return stream_busy!=0;}
static void purge(uint32_t address,uint32_t bytes) {
    if(!purges) {assert(address==0x8cfb0400u);assert(bytes==0x760u);
        assert(kui_retail_hook_active==1u);}
    else if(purges==1u) {assert(address==(uint32_t)(uintptr_t)fake_bss);
        assert(bytes==sizeof(fake_bss));assert(kui_retail_hook_active==1u);}
    else {assert(purges==2u);assert(address==(uint32_t)(uintptr_t)&kui_retail_hook_active);
        assert(bytes==sizeof(kui_retail_hook_active));assert(kui_retail_hook_active==0u);}
    ++purges;
}
'''
suffix=r'''

static struct kui_retail_manifest *temporary,*owned;
static kui_retail_async_region_type *workspace,*retained;
#define S (workspace->shared.service)
#define E (workspace->engine)
#define T (workspace->shared.stream)
static void snapshot(void) {memcpy(retained,workspace,sizeof(*retained));}
static void reset(void) {
    memset(temporary,0x5a,KUI_VMU_NATIVE_BACKGROUND_MAP_BYTES);
    memcpy(owned,temporary,KUI_VMU_NATIVE_BACKGROUND_MAP_BYTES);
    memset(workspace,0,sizeof(*workspace));memset(&fake_control,0,sizeof(fake_control));
    fake_sr=0x400000f0u;fake_vbr=0x8c00f400u;fake_control.status=KUI_VMU_OBS_BOOT_LEASED;
    fake_control.lease=0x8cf90000u;
    S.initialized=1;S.tracks=temporary->slots;S.token=0x123456u;
    S.completed_bytes=0xabcdefu;S.status=2u;
    E.manifest=temporary;E.cursor.position=97u;E.stamp=0xe12345u;
    workspace->shared.card.device.sd.identity=0xc1d123u;
    workspace->shared.card.stamp=0xca456u;workspace->shared.port.stamp=0xb0789u;
    workspace->vector100[2]=0x8c004100u;workspace->vector400[2]=0x8c004200u;
    workspace->vector600[2]=0x8c004300u;
    memset(workspace->area0,0x12,sizeof(workspace->area0));memset(workspace->area1,0x34,sizeof(workspace->area1));
    T.saved[0]=0x0c123400u;T.saved[1]=0x0c567800u;
    T.saved[2]=123u;T.saved[3]=0x4212u;T.position=468u;
    native_background_manifest=temporary;kui_retail_async_region_storage=workspace;
    stream_busy=0;kui_retail_hook_active=0;purges=0;snapshot();
}
static void refused(const struct kui_retail_manifest *from,const struct kui_retail_manifest *to) {
    unsigned char workspace_before[sizeof(*workspace)],retained_before[sizeof(*retained)],control_before[sizeof(fake_control)];
    memcpy(workspace_before,workspace,sizeof(*workspace));memcpy(retained_before,retained,sizeof(*retained));
    memcpy(control_before,&fake_control,sizeof(fake_control));
    const struct kui_retail_manifest *before=native_background_manifest;
    kui_retail_async_region_type *region_before=kui_retail_async_region_storage;
    uint32_t lock=kui_retail_hook_active,sr=fake_sr,vbr=fake_vbr;
    assert(kui_vmu_native_background_manifest_bind(from,to)==0);
    assert(!memcmp(workspace_before,workspace,sizeof(*workspace)));
    assert(!memcmp(retained_before,retained,sizeof(*retained)));
    assert(!memcmp(control_before,&fake_control,sizeof(fake_control)));
    assert(native_background_manifest==before && kui_retail_async_region_storage==region_before);
    assert(kui_retail_hook_active==lock && !purges && fake_sr==sr && fake_vbr==vbr);
}
static void success(void) {
    uint32_t sr=fake_sr,vbr=fake_vbr;bool cursor=E.cursor.manifest!=NULL;
    kui_retail_async_region_type expected=*workspace;
    expected.shared.service.tracks=owned->slots;expected.engine.manifest=owned;
    if(cursor)expected.engine.cursor.manifest=owned;
    if(expected.shared.stream.card)expected.shared.stream.card=&retained->shared.card.device.sd;
    if(expected.shared.stream.area[0])expected.shared.stream.area[0]=retained->area0;
    if(expected.shared.stream.area[1])expected.shared.stream.area[1]=retained->area1;
    snapshot();
    assert(kui_vmu_native_background_manifest_bind(temporary,owned)==1);
    assert(native_background_manifest==owned && kui_retail_async_region_storage==retained);
    assert(!memcmp(retained,&expected,sizeof(expected)));
    assert(fake_sr==sr && fake_vbr==vbr && purges==3u && !kui_retail_hook_active);
    assert(!memcmp(temporary,owned,KUI_VMU_NATIVE_BACKGROUND_MAP_BYTES));
}
int main(void) {
    temporary=mmap((void *)(uintptr_t)0x8ce00000u,4096,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    owned=mmap((void *)(uintptr_t)0x8cfb0000u,4096,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    workspace=mmap((void *)(uintptr_t)0x8ce01000u,4096,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    retained=(void *)(uintptr_t)0x8cfb0400u;
    assert(temporary==(void *)(uintptr_t)0x8ce00000u && owned==(void *)(uintptr_t)0x8cfb0000u);
    assert(workspace==(void *)(uintptr_t)0x8ce01000u);
    reset();success();
    reset();E.cursor.manifest=temporary;fake_sr|=0x10000000u;success();
    reset();fake_control.lease=0x0cf90000u;success();
    reset();fake_control.lease=0xacf90000u;success();
    /* An idle paused CMD18 retains its port and saved guest DMA tuple. */
    reset();E.opened=1;workspace->shared.port.acquired=1;T.state=3;
    T.card=&workspace->shared.card.device.sd;T.area[0]=workspace->area0;T.area[1]=workspace->area1;
    E.cursor.manifest=temporary;success();
    assert(retained->engine.opened==1 && retained->shared.port.acquired==1);
    assert(retained->shared.stream.saved[1]==0x0c567800u && retained->shared.stream.position==468u);
    reset();success();purges=0;refused(temporary,owned); /* one cold bind only */
    for(unsigned i=0;i<6u;i++) {if(i==KUI_VMU_OBS_BOOT_LEASED)continue;
        reset();fake_control.status=i;refused(temporary,owned);}
    const uint32_t bad_sr[]={0u,0xf0u,0x40000000u,0x400000e0u,0x50000070u};
    for(unsigned i=0;i<sizeof(bad_sr)/sizeof(*bad_sr);i++) {
        reset();fake_sr=bad_sr[i];refused(temporary,owned);}
#define REJECT_FIELD(field,value) do {reset();field=value;snapshot();refused(temporary,owned);} while(0)
    REJECT_FIELD(S.initialized,0u);REJECT_FIELD(S.pending,1u);
    REJECT_FIELD(S.executing,1u);REJECT_FIELD(E.active,1u);
    REJECT_FIELD(E.in_irq,1u);REJECT_FIELD(E.hooked,1u);
    REJECT_FIELD(E.release.armed,1u);REJECT_FIELD(E.release.depth,1u);
    REJECT_FIELD(stream_busy,1u);REJECT_FIELD(kui_retail_hook_active,1u);
    REJECT_FIELD(fake_vbr,0x8ce00f00u);
    REJECT_FIELD(fake_control.lease,0x8cf90004u);REJECT_FIELD(fake_control.lease,0xecf90000u);
    REJECT_FIELD(fake_control.lease,0x8cfd0020u);
    REJECT_FIELD(native_background_manifest,owned);REJECT_FIELD(E.manifest,owned);
    REJECT_FIELD(E.cursor.manifest,owned);REJECT_FIELD(S.tracks,owned->slots);
    REJECT_FIELD(S.ops.context,(void *)temporary);REJECT_FIELD(E.cursor.context,(void *)workspace);
    REJECT_FIELD(workspace->shared.card.device.sd.bus.ctx,(void *)workspace);
    REJECT_FIELD(T.card,&retained->shared.card.device.sd);
    REJECT_FIELD(T.area[0],retained->area0);REJECT_FIELD(T.area[1],retained->area1);
    reset();kui_retail_async_region_storage=(void *)(uintptr_t)0x8ce01004u;refused(temporary,owned);
    reset();kui_retail_async_region_storage=(void *)(uintptr_t)0xace01000u;refused(temporary,owned);
    reset();((unsigned char *)owned)[0]^=1u;refused(temporary,owned);
    reset();((unsigned char *)owned)[KUI_VMU_NATIVE_BACKGROUND_MAP_BYTES-1]^=1u;refused(temporary,owned);
    reset();retained->area1[543]^=1u;refused(temporary,owned);
    reset();retained->vector600[1]^=1u;refused(temporary,owned);
    reset();refused((const void *)(uintptr_t)0x8cdffffcu,owned);
    reset();refused(temporary,(const void *)(uintptr_t)0x8cfb0020u);
    reset();refused(temporary,(const void *)(uintptr_t)0xacfb0000u);
    reset();E.opened=1;workspace->shared.port.acquired=1;T.state=3;
    T.card=&workspace->shared.card.device.sd;T.area[0]=workspace->area0;T.area[1]=workspace->area1;
    E.cursor.manifest=temporary;success();
    memset(temporary,0xa5,KUI_VMU_NATIVE_BACKGROUND_MAP_BYTES);memset(workspace,0xd3,sizeof(*workspace));
    assert(((const unsigned char *)native_background_manifest)[0]==0x5a);
    assert(kui_retail_async_region_storage->engine.manifest==owned);
    assert(kui_retail_async_region_storage->engine.cursor.manifest==owned);
    assert(kui_retail_async_region_storage->shared.service.tracks==owned->slots);
    assert(kui_retail_async_region_storage->shared.stream.card==&retained->shared.card.device.sd);
    assert(kui_retail_async_region_storage->shared.stream.area[0][7]==0x12);
    assert(kui_retail_async_region_storage->shared.stream.area[1][543]==0x34);
    assert(kui_retail_async_region_storage->vector600[2]==0x8c004300u);
    assert(munmap(temporary,4096)==0 && munmap(owned,4096)==0 && munmap(workspace,4096)==0);
    puts("Native Background cold binder: region migration, exact snapshots, pointer retirement, DMA tuple and refusal checks passed");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='kui-native-background-bind-') as directory:
    directory=Path(directory)
    fixture=directory/'binder.c';binary=directory/'binder'
    fixture.write_text(prefix+body+suffix)
    subprocess.run([os.environ.get('CC','cc'),'-std=c11','-O2','-g','-Wall','-Wextra','-Werror',
        '-fsanitize=undefined','-fno-pie','-no-pie','-I'+str(ROOT/'include'),str(fixture),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
