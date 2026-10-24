#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the production five-argument cold init and whole-region publish.

The extracted initializer runs unchanged. Only its base initializer, guest
mapping callback and cache publisher are host stubs; geometry admission and
workspace clearing/pointer handoff are the production code.
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
source=(ROOT/'src/loader/vmu_native_background_resident.c').read_text()
begin=source.index('int kui_vmu_native_background_init(')
opening=source.index('{',begin)
depth=1
end=opening+1
while depth:
    depth+=(source[end]=='{')-(source[end]=='}')
    end+=1
body=source[begin:end]

prefix=r'''
#define _GNU_SOURCE
#define KUI_RETAIL_MANIFEST_SLOTS 64
#include <stdint.h>
/* Match SH-4's four-byte scalar alignment without reducing the production
 * image cache's explicit 32-byte alignment. */
typedef uint64_t fixture_sh4_u64 __attribute__((aligned(4)));
#define uint64_t fixture_sh4_u64
#include "kui/vmu_native_background.h"
#include "kui/retail_resident.h"
#include "kui/retail_image.h"
#include "retail_storage.h"
#include "retail_display.h"
#undef uint64_t
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

typedef uint8_t *(*fixture_map)(void *,uint32_t,uint32_t,int);
struct fixture_service {struct {fixture_map map;} ops;};
struct fixture_region {
    uint8_t before_service[0x320];
    struct fixture_service active_service;
    uint8_t after_service[0x760-0x320-sizeof(struct fixture_service)];
};
_Static_assert(sizeof(struct fixture_region)==0x760,"Host region extent");
static struct fixture_region *kui_retail_async_region_storage;
#define service (kui_retail_async_region_storage->active_service)
static const struct kui_retail_manifest *expected_prepared;
static const struct kui_retail_storage *expected_card;
static const struct retail_display_state *expected_display;
static void *expected_workspace;
static uint32_t expected_vector;
static int base_result;
static unsigned base_calls,purge_calls;
static uint8_t published[0x760];
static uint8_t *base_map(void *c,uint32_t a,uint32_t b,int w) {
    (void)c;(void)a;(void)b;(void)w;return NULL;
}
static uint8_t *native_background_map(void *c,uint32_t a,uint32_t b,int w) {
    (void)c;(void)a;(void)b;(void)w;return NULL;
}
static int kui_vmu_observe_background_base_init(const struct kui_retail_manifest *p,
    const struct kui_retail_storage *c,uint32_t v,const struct retail_display_state *d) {
    assert(!base_calls && !purge_calls);
    assert(p==expected_prepared && c==expected_card && v==expected_vector && d==expected_display);
    assert((void *)kui_retail_async_region_storage==expected_workspace);
    for(unsigned i=0;i<0x760;i++)assert(((const uint8_t *)expected_workspace)[i]==0u);
    ++base_calls;
    ((uint8_t *)expected_workspace)[0]=0x17u;
    ((uint8_t *)expected_workspace)[0x23f]=0x28u;
    ((uint8_t *)expected_workspace)[0x53f]=0x39u;
    ((uint8_t *)expected_workspace)[0x75f]=0x4au;
    service.ops.map=base_map;
    return base_result;
}
static void purge(uint32_t area,uint32_t bytes) {
    assert(base_calls==1u && !purge_calls && base_result==0);
    assert(area==(uint32_t)(uintptr_t)expected_workspace && bytes==0x760u);
    assert(service.ops.map==native_background_map);
    assert(((uint8_t *)expected_workspace)[0]==0x17u);
    assert(((uint8_t *)expected_workspace)[0x75f]==0x4au);
    memcpy(published,expected_workspace,bytes);++purge_calls;
}
'''
suffix=r'''
static uint8_t *arena;
static struct kui_retail_manifest *prepared;
static struct kui_retail_storage card;
static struct retail_display_state display;
static void reset(uint32_t map_address,uint32_t region_address) {
    memset(arena,0xa6,0x202000);
    prepared=(void *)(uintptr_t)map_address;
    if(prepared) {
        uint32_t transport=KUI_STORAGE_SCI,reader=KUI_RETAIL_READER_ASYNC;
        memcpy((uint8_t *)prepared+offsetof(struct kui_retail_manifest,storage_transport),&transport,sizeof(transport));
        memcpy((uint8_t *)prepared+offsetof(struct kui_retail_manifest,reader),&reader,sizeof(reader));
    }
    expected_prepared=prepared;expected_card=&card;expected_display=&display;
    expected_workspace=(void *)(uintptr_t)region_address;expected_vector=0x8c001234u;
    base_calls=0;purge_calls=0;base_result=0;
    kui_retail_async_region_storage=(void *)(uintptr_t)0x13572468u;
    memset(published,0xd5,sizeof(published));
}
static int invoke(void) {
    return kui_vmu_native_background_init(prepared,&card,expected_vector,&display,expected_workspace);
}
static void refused(void) {
    uint8_t before[0x202000];memcpy(before,arena,sizeof(before));
    assert(invoke()==KUI_RETAIL_RESIDENT_ARGUMENT);
    assert(!base_calls && !purge_calls);
    assert(kui_retail_async_region_storage==(void *)(uintptr_t)0x13572468u);
    assert(!memcmp(before,arena,sizeof(before)));
}
static void success(void) {
    assert(invoke()==0 && base_calls==1u && purge_calls==1u);
    assert((void *)kui_retail_async_region_storage==expected_workspace);
    assert(service.ops.map==native_background_map);
    assert(published[0]==0x17u && published[0x23f]==0x28u);
    assert(published[0x53f]==0x39u && published[0x75f]==0x4au);
    /* Model the owner's invalidating startup: every workspace byte and the
     * replacement callback must already be present in published RAM. */
    memset(expected_workspace,0xcc,0x760);memcpy(expected_workspace,published,0x760);
    assert(service.ops.map==native_background_map);
    assert(((uint8_t *)expected_workspace)[0x75f]==0x4au);
}
int main(void) {
    arena=mmap((void *)(uintptr_t)0x8cdff000u,0x202000,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    assert(arena==(void *)(uintptr_t)0x8cdff000u);
    reset(0x8ce00000u,0x8ce02000u);success();
    reset(0x8ce00000u,0x8ce02000u);prepared->reader=KUI_RETAIL_READER_ASYNC_EAGER;success();
    reset(0x8ce00000u,0x8cfdf8a0u);success(); /* exact upper workspace bound */
    reset(0x8cfdfc04u,0x8ce00000u);success(); /* exact upper map-prefix bound */
    reset(0x8ce00000u,0x8ce02000u);base_result=KUI_RETAIL_RESIDENT_SD;
    assert(invoke()==KUI_RETAIL_RESIDENT_SD && base_calls==1u && !purge_calls);
    assert((void *)kui_retail_async_region_storage==expected_workspace && service.ops.map==base_map);
    reset(0u,0x8ce02000u);refused();
    reset(0x8cdffffcu,0x8ce02000u);refused();
    reset(0x8ce00001u,0x8ce02000u);refused();
    reset(0x8cfdfc08u,0x8ce02000u);refused();
    reset(0x8ce00000u,0x8ce02000u);prepared=(void *)(uintptr_t)0xace00000u;
    expected_prepared=prepared;refused(); /* unmapped alias must not be read */
    reset(0x8ce00000u,0u);refused();
    reset(0x8ce00000u,0x8cdfffe0u);refused();
    reset(0x8ce00000u,0x8ce02004u);refused();
    reset(0x8ce00000u,0x8cfdf8c0u);refused();
    reset(0x8ce00000u,0xace02000u);refused();
    reset(0x8ce00000u,0x8ce00000u);refused();
    reset(0x8ce00000u,0x8ce003e0u);refused(); /* region begins in map tail */
    reset(0x8ce00400u,0x8ce00000u);refused(); /* map begins in region tail */
    reset(0x8ce00000u,0x8ce00400u);success(); /* aligned regions can be adjacent */
    reset(0x8ce00000u,0x8ce02000u);prepared->storage_transport=KUI_STORAGE_SCIF;refused();
    reset(0x8ce00000u,0x8ce02000u);prepared->storage_transport=KUI_STORAGE_IDE;refused();
    const uint32_t invalid_readers[]={KUI_RETAIL_READER_STANDARD,3u,UINT32_MAX};
    for(unsigned i=0;i<sizeof(invalid_readers)/sizeof(*invalid_readers);i++) {
        reset(0x8ce00000u,0x8ce02000u);prepared->reader=invalid_readers[i];refused();
    }
    assert(munmap(arena,0x202000)==0);
    puts("Native Background cold init: workspace zeroing, pointer-before-base, complete cache publication, refusal and reader admission passed");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='kui-native-background-init-') as directory:
    directory=Path(directory)
    fixture=directory/'initializer.c';binary=directory/'initializer'
    fixture.write_text(prefix+body+suffix)
    subprocess.run([os.environ.get('CC','cc'),'-std=c11','-O2','-g','-Wall','-Wextra','-Werror',
        '-fsanitize=undefined','-fno-sanitize-recover=undefined','-fno-pie','-no-pie','-I'+str(ROOT/'include'),'-I'+str(ROOT/'src/loader'),
        str(fixture),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
