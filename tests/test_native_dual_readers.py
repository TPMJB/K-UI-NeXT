#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compile the production dual selector/getter and trap premature aux reads.

The disposable host harness uses the real public types, with their ordinary
host pointer width. Console field offsets and the linked SH instruction path
are checked separately. No production branch is translated into Python.
"""
from pathlib import Path
import argparse
import os
import json
import shlex
import shutil
import struct
import re
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]


def function_body(source,name):
    match=re.search(r'(?m)^(?![\s#])[^\n]*?\b'+re.escape(name)+r'\s*\(',source)
    if match is None:raise ValueError('production function is missing: '+name)
    starts=list(re.finditer(r'(?m)^static\b',source[:match.start()+1]))
    start=match.start() if source[match.start():].startswith('static') else starts[-1].start()
    at=source.index('{',match.start());depth=0;quote=None;comment=None;escape=False;i=at
    while i<len(source):
        c=source[i];next_=source[i:i+2]
        if comment=='line':
            if c=='\n':comment=None
        elif comment=='block':
            if next_=='*/':comment=None;i+=1
        elif quote:
            if escape:escape=False
            elif c=='\\':escape=True
            elif c==quote:quote=None
        elif next_=='//':comment='line';i+=1
        elif next_=='/*':comment='block';i+=1
        elif c in ('"',"'"):quote=c
        elif c=='{':depth+=1
        elif c=='}':
            depth-=1
            if depth==0:return source[start:i+1]
        i+=1
    raise ValueError('production function is incomplete: '+name)


PREFIX=r'''
#define _GNU_SOURCE 1
#include <assert.h>
#include <inttypes.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "kui/native_dual_reader.h"
#include "kui/retail_loader_layout.h"
#include "kui/native_sdk_sci_runtime.h"
#include "kui/native_readahead.h"
#include "kui/vmu_native_passive.h"
#include "kui/vmu_native_background.h"
static struct {struct {uint32_t payload;} lease;} config;
static struct kui_retail_manifest manifest;
static const uint8_t x_blob[37]={1},y_blob[53]={2};
#define __retail_resident_sci_blob_start x_blob
#define __retail_resident_sci_blob_end (x_blob+sizeof(x_blob))
#define __retail_resident_legacy_blob_start y_blob
#define __retail_resident_legacy_blob_end (y_blob+sizeof(y_blob))
static const uint8_t *resident_blob;
static size_t resident_bytes;
static uint32_t resident_limit;
static const struct kui_native_dual_capability valid_capability={
    KUI_NATIVE_DUAL_MAGIC0,KUI_NATIVE_DUAL_MAGIC1,1u,KUI_NATIVE_DUAL_CAPABILITY_BYTES,
    1u,2u,KUI_NATIVE_DUAL_LEASE_BYTES,0u};
static struct kui_native_dual_capability kui_native_dual_reader_capability={
    KUI_NATIVE_DUAL_MAGIC0,KUI_NATIVE_DUAL_MAGIC1,1u,KUI_NATIVE_DUAL_CAPABILITY_BYTES,
    1u,2u,KUI_NATIVE_DUAL_LEASE_BYTES,0u};
static uint32_t expected_stop_detail;
static jmp_buf stopped_jump;
static unsigned stopped_calls,checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"CHECK failed line%d: %s\n",__LINE__,#x);exit(2);}}while(0)
static void __attribute__((noreturn)) stopped(const char *text,uint32_t detail) {
    CHECK(text && *text && detail==expected_stop_detail);++stopped_calls;longjmp(stopped_jump,1);
}
static unsigned binding_calls,binding_order;
static void native_loader_timeline_bind(const void *e,uint32_t dest,uint32_t bytes,uint32_t payload,uint32_t mode) {
    CHECK(e==(void *)(uintptr_t)0x1357u && dest==0x2468u && bytes==0x369cu && payload==0x48adu && mode==0x5bdeu);
    ++binding_calls;binding_order=binding_order*10u+1u;
}
static void native_sdk_sci_route_bind(uint32_t payload) {
    CHECK(payload==0x48adu);++binding_calls;binding_order=binding_order*10u+2u;
}
static void native_readahead_bind(uint32_t payload) {
    CHECK(payload==0x48adu);++binding_calls;binding_order=binding_order*10u+3u;
}
'''

SUFFIX=r'''
static void *map_exact(uintptr_t address,size_t bytes) {
    void *result=mmap((void *)address,bytes,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    CHECK(result==(void *)address);return result;
}
static void protect(uintptr_t address,int mode) {
    CHECK(mprotect((void *)address,4096u,mode)==0);
}
static void descriptor_scan(const uint8_t *buffer,size_t bytes) {
    unsigned found=0u;size_t expected=0u,offset=SIZE_MAX;
    for(size_t at=0u;at+sizeof(valid_capability)<=bytes;at++)
        if(!memcmp(buffer+at,&valid_capability,sizeof(valid_capability))) {++found;expected=at;}
    int result=kui_native_dual_descriptor_find(buffer,bytes,&offset);
    CHECK(result==(found>1u?-1:found?1:0));
    CHECK(offset==(found==1u?expected:SIZE_MAX));
}
static void capability(void) {
    CHECK(sizeof(valid_capability)==32u);
    CHECK(kui_native_dual_descriptor_valid(&valid_capability,sizeof(valid_capability)));
    CHECK(!kui_native_dual_descriptor_valid(NULL,SIZE_MAX));
    CHECK(!kui_native_dual_descriptor_find(NULL,SIZE_MAX,NULL));
    uint8_t buffer[192];
    for(unsigned bit=0u;bit<256u;bit++) {
        memcpy(buffer,&valid_capability,sizeof(valid_capability));buffer[bit/8u]^=(uint8_t)(1u<<(bit%8u));
        CHECK(!kui_native_dual_descriptor_valid(buffer,sizeof(valid_capability)));
        descriptor_scan(buffer,sizeof(valid_capability));
        memcpy(&kui_native_dual_reader_capability,buffer,sizeof(valid_capability));
        manifest.reader=1u;manifest.storage_transport=KUI_STORAGE_SCI;expected_stop_detail=0u;
        resident_blob=NULL;resident_bytes=0u;stopped_calls=0u;
        if(!setjmp(stopped_jump)) {select_resident();CHECK(false);}
        else CHECK(stopped_calls==1u && !resident_blob && !resident_bytes);
    }
    kui_native_dual_reader_capability=valid_capability;
    for(unsigned offset=0u;offset<64u;offset++) {
        memset(buffer,0xa5,sizeof(buffer));memcpy(buffer+offset,&valid_capability,32u);
        for(unsigned bytes=0u;bytes<=sizeof(buffer);bytes++)descriptor_scan(buffer,bytes);
        /* Every pair, including overlapping serialized records, is scanned
         * byte-by-byte against an independent exact32-byte pattern search. */
        for(unsigned second=offset+1u;second<offset+65u;second++) {
            memset(buffer,0xa5,sizeof(buffer));memcpy(buffer+offset,&valid_capability,32u);
            memcpy(buffer+second,&valid_capability,32u);descriptor_scan(buffer,sizeof(buffer));
        }
    }
    const uintptr_t at=UINT32_C(0x8c700000);
    (void)map_exact(at,8192u);protect(at+4096u,PROT_NONE);
    for(unsigned bytes=0u;bytes<32u;bytes++) {
        uint8_t *truncated=(void *)(at+4096u-bytes);
        if(bytes)memcpy(truncated,&valid_capability,bytes);
        CHECK(!kui_native_dual_descriptor_valid(truncated,bytes));
        CHECK(!kui_native_dual_descriptor_find(truncated,bytes,NULL));
    }
    CHECK(munmap((void *)at,8192u)==0);
}
static void bindings(void) {
    static const uint32_t readers[]={0u,1u,2u,3u,255u,UINT32_MAX};
    for(unsigned i=0u;i<sizeof(readers)/sizeof(readers[0]);i++) {
        manifest.reader=readers[i];binding_calls=binding_order=0u;bind_selected();
        CHECK(binding_calls==(readers[i]==1u?3u:0u));
        CHECK(binding_order==(readers[i]==1u?123u:0u));
    }
}
static void selection(void) {
    static const uint32_t edge[]={0u,1u,2u,3u,255u,256u,UINT32_MAX};
    for(unsigned a=0;a<256u+sizeof(edge)/sizeof(edge[0]);a++) {
        uint32_t reader=a<256u?a:edge[a-256u];
        for(unsigned b=0;b<256u+sizeof(edge)/sizeof(edge[0]);b++) {
            uint32_t transport=b<256u?b:edge[b-256u];
            manifest.reader=reader;manifest.storage_transport=transport;expected_stop_detail=reader;
            bool dual_valid=transport==KUI_STORAGE_SCI && (reader==1u || reader==2u);
            bool valid=dual_valid && (!KUI_NATIVE_READER_ONLY_ISOLATION || reader==1u);
            CHECK(kui_native_dual_reader_valid(reader,transport)==dual_valid);
            CHECK(kui_native_reader_uses_sdk(reader)==(reader==1u));
            resident_blob=NULL;resident_bytes=0u;resident_limit=0u;stopped_calls=0u;
            if(!setjmp(stopped_jump)) {
                select_resident();CHECK(valid && stopped_calls==0u);
                CHECK(resident_blob==(reader==1u?x_blob:y_blob));
                CHECK(resident_bytes==(reader==1u?sizeof(x_blob):sizeof(y_blob)));
                CHECK(resident_limit==KUI_RETAIL_LOW_ASYNC_LIMIT);
            } else {
                CHECK(!valid && stopped_calls==1u);
                CHECK(!resident_blob && !resident_bytes);
            }
        }
    }
}
static void seed(kui_retail_async_region_type *r,uintptr_t lease) {
    struct kui_retail_manifest *owned=(void *)(lease+KUI_VMU_NATIVE_BACKGROUND_WORKER_BYTES);
    struct kui_native_ra_exports *a=(void *)((lease+KUI_NATIVE_RA_AUX_OFFSET)|UINT32_C(0x20000000));
    memset(r,0,sizeof(*r));memset(owned,0,sizeof(*owned));memset(a,0,4096u);
    owned->reader=1u;owned->storage_transport=KUI_STORAGE_SCI;r->engine.manifest=owned;
    r->vector600[KUI_NATIVE_SDK_SCI_COOKIE_INDEX]=KUI_NATIVE_SDK_SCI_COOKIE;
    a->magic=KUI_NATIVE_RA_AUX_MAGIC;a->version=1u;a->bytes=sizeof(*a);
    a->visit=(kui_native_ra_visit)(lease+KUI_NATIVE_RA_AUX_OFFSET+0x1000u);
    a->image_end=(uint32_t)(lease+KUI_NATIVE_RA_AUX_OFFSET+0x2000u);
    a->context=(void *)((lease+KUI_NATIVE_RA_AUX_OFFSET+64u)|UINT32_C(0x20000000));
    r->vector600[KUI_NATIVE_RA_ENTRY_INDEX]=(uint32_t)(uintptr_t)a->visit;
    r->vector600[KUI_NATIVE_RA_CONTEXT_INDEX]=(uint32_t)(uintptr_t)a->context;
}
static void worker_getter(void) {
    const uintptr_t lease=UINT32_C(0x8c400000),map=lease+KUI_VMU_NATIVE_BACKGROUND_WORKER_BYTES;
    const uintptr_t aux=(lease+KUI_NATIVE_RA_AUX_OFFSET)|UINT32_C(0x20000000);
    kui_retail_async_region_type r;
    struct kui_retail_manifest *owned=(void *)map;
    struct kui_native_ra_exports *a=(void *)aux;
    (void)map_exact(lease,KUI_VMU_NATIVE_BACKGROUND_LEASE_BYTES);
    (void)map_exact(aux,4096u);
    static const uint32_t aliases[]={UINT32_C(0x0c400000),UINT32_C(0x8c400000),UINT32_C(0xac400000)};
    for(unsigned alias=0u;alias<3u;alias++) {
        config.lease.payload=aliases[alias];seed(&r,lease);
        protect(aux,PROT_NONE);
        CHECK(!reader_readahead_exports(NULL));
        static const uintptr_t wrong_manifest[]={0u,1u,UINT32_C(0x0c420000),UINT32_C(0xac420000),
            UINT32_C(0x8c41fffc),UINT32_C(0x8c420004),UINT32_C(0xffffffff)};
        protect(map,PROT_NONE);
        for(unsigned i=0u;i<sizeof(wrong_manifest)/sizeof(wrong_manifest[0]);i++) {
            r.engine.manifest=(void *)wrong_manifest[i];CHECK(!reader_readahead_exports(&r));
        }
        protect(map,PROT_READ|PROT_WRITE);r.engine.manifest=owned;
        static const uint32_t bad_readers[]={0u,2u,3u,255u,256u,UINT32_MAX};
        for(unsigned i=0u;i<sizeof(bad_readers)/sizeof(bad_readers[0]);i++) {
            owned->reader=bad_readers[i];
            /* Even forged ready cookie and pointer-looking vector opcodes
             * cannot authorize Y/Standard to inspect the auxiliary page. */
            r.vector600[0]=(uint32_t)(uintptr_t)(lease+KUI_NATIVE_RA_AUX_OFFSET+0x1000u);
            r.vector600[1]=(uint32_t)(aux+64u);
            CHECK(!reader_readahead_exports(&r));
        }
        owned->reader=1u;
        static const uint32_t bad_cookies[]={0u,1u,KUI_NATIVE_SDK_SCI_COOKIE^1u,UINT32_MAX};
        for(unsigned i=0u;i<sizeof(bad_cookies)/sizeof(bad_cookies[0]);i++) {
            r.vector600[KUI_NATIVE_SDK_SCI_COOKIE_INDEX]=bad_cookies[i];
            CHECK(!reader_readahead_exports(&r));
        }
        protect(aux,PROT_READ|PROT_WRITE);seed(&r,lease);
        CHECK(reader_readahead_exports(&r)==a);
        a->magic^=1u;CHECK(!reader_readahead_exports(&r));seed(&r,lease);
        a->version=2u;CHECK(!reader_readahead_exports(&r));seed(&r,lease);
        a->bytes-=4u;CHECK(!reader_readahead_exports(&r));seed(&r,lease);
        a->image_end=(uint32_t)(lease+KUI_NATIVE_RA_AUX_OFFSET+0x1000u);
        CHECK(!reader_readahead_exports(&r));seed(&r,lease);
        a->image_end=(uint32_t)(lease+KUI_NATIVE_RA_AUX_OFFSET+KUI_NATIVE_RA_AUX_IMAGE_BYTES);
        CHECK(reader_readahead_exports(&r)==a);
        a->image_end+=4u;
        CHECK(!reader_readahead_exports(&r));seed(&r,lease);
        a->image_end=(uint32_t)(lease+KUI_NATIVE_RA_AUX_OFFSET+0x5000u);
        CHECK(!reader_readahead_exports(&r));seed(&r,lease);
        a->visit=(kui_native_ra_visit)(lease+KUI_NATIVE_RA_AUX_OFFSET+0xffeu);
        CHECK(!reader_readahead_exports(&r));seed(&r,lease);
        a->visit=(kui_native_ra_visit)(uintptr_t)a->image_end;
        CHECK(!reader_readahead_exports(&r));seed(&r,lease);
        r.vector600[0]=0u;CHECK(!reader_readahead_exports(&r));seed(&r,lease);
        r.vector600[1]=0u;CHECK(!reader_readahead_exports(&r));seed(&r,lease);
        a->context=(void *)(aux+32u);r.vector600[1]=(uint32_t)(uintptr_t)a->context;
        CHECK(!reader_readahead_exports(&r));seed(&r,lease);
        a->context=(void *)(aux+4096u-sizeof(struct kui_native_ra_public));
        r.vector600[1]=(uint32_t)(uintptr_t)a->context;CHECK(reader_readahead_exports(&r)==a);
        a->context=(void *)((uintptr_t)a->context+4u);r.vector600[1]=(uint32_t)(uintptr_t)a->context;
        CHECK(!reader_readahead_exports(&r));
    }
    CHECK(munmap((void *)aux,4096u)==0);CHECK(munmap((void *)lease,KUI_VMU_NATIVE_BACKGROUND_LEASE_BYTES)==0);
}
int main(void) {
    CHECK(KUI_VMU_NATIVE_BACKGROUND_LEASE_BYTES==(KUI_NATIVE_GENERAL_LEAN_LAYOUT?0x51000u:0x55000u));
    capability();selection();bindings();worker_getter();
    printf("dual readers: %u checks; actual selector, Standard/non-SCI refusal, distinct blobs, all3leasealiases, Y/Standard/foreign-map/cookie refusal before protected aux reads, exact public bounds passed\n",checks);
    return 0;
}
'''


LAYOUT_FIELDS={
    'pointer_bytes':'sizeof(uintptr_t)',
    'region_bytes':'sizeof(kui_retail_async_region_type)',
    'engine_bytes':'sizeof(struct kui_retail_async)',
    'manifest_bytes':'sizeof(struct kui_retail_manifest)',
    'manifest_reader':'offsetof(struct kui_retail_manifest,reader)',
    'manifest_transport':'offsetof(struct kui_retail_manifest,storage_transport)',
    'config_payload':'offsetof(struct kui_vmu_native_passive_config,lease.payload)',
    'durable.handling':'offsetof(struct kui_vmu_durable,handling)',
    'durable.inflight':'offsetof(struct kui_vmu_durable,inflight)',
    'lease_bytes':'KUI_VMU_NATIVE_BACKGROUND_LEASE_BYTES',
    **{name:'offsetof(kui_retail_async_region_type,'+name+')' for name in
       ('vector100','area0','engine','vector400','shared','vector600','area1',
        'engine.manifest','engine.token','engine.active','engine.hooked','engine.target',
        'engine.native_loan_state','engine.cursor.block','engine.cursor.done',
        'engine.release.vbr','engine.release.sci','engine.release.armed','engine.release.depth',
        'engine.release.pending','engine.release.rehooks','engine.release.released',
        'engine.release.vbr_changes','engine.stats.irq_blocks','engine.stats.call_blocks',
        'engine.stats.waits','engine.stats.execs','engine.stats.exec_int','engine.stats.hooks',
        'engine.stats.releases','shared.service','shared.service.pending','shared.service.token',
        'shared.service.command','shared.service.diag.last_function','shared.card','shared.display',
        'shared.stream','shared.stream.stats.blocks','shared.stream.stats.polled',
        'shared.stream.stats.starts','shared.stream.stats.kept','shared.stream.stats.overruns',
        'shared.stream.stats.crc_errors','shared.stream.stats.token_errors',
        'shared.stream.stats.foreign','shared.stream.stats.repaired','shared.stream.stats.ahead',
        'shared.stream.stats.deferred','shared.port')},
}


def object_section(path,wanted):
    data=Path(path).read_bytes()
    header=struct.unpack_from('<16sHHIIIIIHHHHHH',data)
    if header[0][:7]!=b'\x7fELF\x01\x01\x01' or header[2]!=42:
        raise ValueError('console layout requires an actual little-endian SH ELF object')
    sections=[struct.unpack_from('<10I',data,header[6]+header[11]*i) for i in range(header[12])]
    names=sections[header[13]];strings=data[names[4]:names[4]+names[5]]
    found=[s for s in sections if strings[s[0]:].split(b'\0',1)[0].decode()==wanted]
    if len(found)!=1:raise ValueError('console layout output section missing or duplicated')
    section=found[0]
    return data[section[4]:section[4]+section[5]]


def console_layouts(general=False,compact=False):
    compiler=shutil.which('sh-elf-gcc')
    if compiler is None:raise ValueError('sh-elf-gcc must be on PATH for the actual console layout proof')
    with tempfile.TemporaryDirectory(prefix='kui-dual-layout-') as temporary:
        directory=Path(temporary)
        make=directory/'flags.mk'
        makefile='Makefile.vmu-native-background-compact' if compact else \
            'Makefile.vmu-native-split-controller-stream-guard' if general else 'Makefile.vmu-native-dual-readers'
        make.write_text('include '+str(ROOT/makefile)+'\n'+
            'dump-dual-flags:\n\t$(file >'+str(directory/'x.flags')+',$(RESIDENT_FLAGS))\n'+
            '\t$(file >'+str(directory/'y.flags')+',$(LEGACY_FLAGS))\n\t@true\n')
        subprocess.run(['make','--no-print-directory','-s','-f',str(make),'dump-dual-flags'],
                       cwd=ROOT,check=True)
        reports={}
        for role in ('x','y'):
            original=shlex.split((directory/(role+'.flags')).read_text())
            flags=[flag for flag in original if flag.startswith(('-D','-I'))]
            extra={'engine.native_max_lba':'offsetof(kui_retail_async_region_type,engine.native_max_lba)',
                   'engine.native_max_count':'offsetof(kui_retail_async_region_type,engine.native_max_count)'} if role=='x' else {}
            fields={**LAYOUT_FIELDS,**extra}
            source=directory/(role+'.c');binary=directory/(role+'.o')
            source.write_text('#include "kui/native_dual_reader.h"\n#include "kui/vmu_native_background.h"\n'+
                '#include "retail_async.h"\n#include "kui/vmu_native_passive.h"\n'+
                'const uint32_t kui_dual_layout[] __attribute__((section(".rodata.dual_layout"),used))={\n'+
                ',\n'.join(fields.values())+'\n};\n')
            subprocess.run([compiler,'-std=c11','-Os','-m4-single','-ml','-ffreestanding',*flags,
                            '-c',str(source),'-o',str(binary)],cwd=ROOT,check=True)
            raw=object_section(binary,'.rodata.dual_layout')
            if len(raw)!=4*len(fields):raise ValueError('console layout scalar count differs')
            reports[role]=dict(zip(fields,struct.unpack('<'+str(len(fields))+'I',raw)))
        differences=[name for name in LAYOUT_FIELDS if name!='engine_bytes' and reports['x'][name]!=reports['y'][name]]
        if differences:raise ValueError('X/Y worker-visible console offsets differ: '+', '.join(differences))
        x,y=reports['x'],reports['y']
        if x['pointer_bytes']!=4 or x['region_bytes']!=0x760 or x['lease_bytes']!=(0x47000 if compact else 0x51000 if general else 0x55000) or x['manifest_bytes']!=1020:
            raise ValueError('dual console geometry differs from the existing lease/region/map ABI')
        if not (x['engine']+y['engine_bytes']<=x['engine.native_max_lba'] and
                x['engine.native_max_count']+4<=x['vector400']):
            raise ValueError('X timing-only words do not fit the unused Y engine gap')
        return dict(actual_sh_compiler_console_layout=True,worker_visible_equal_fields=len(LAYOUT_FIELDS)-1,
                    x_timing_words_only_in_unused_y_gap=True,layouts=reports)


def dual_lease_guards():
    production=(ROOT/'src/loader/vmu_native_background_resident.c').read_text()
    found=re.search(r'/\* NATIVE_RESIDENT_GUARDS_BEGIN.*?\*/\n(.*?)/\* NATIVE_RESIDENT_GUARDS_END \*/',production,re.S)
    if not found:raise ValueError('actual low resident guard block is missing')
    with tempfile.TemporaryDirectory(prefix='kui-dual-lease-') as temporary:
        directory=Path(temporary)
        (directory/'vmu_native_background_guards.h').write_text(found.group(1))
        for enabled,lean in ((0,0),(1,0),(0,1),(1,1)):
            flags=['-DKUI_NATIVE_DUAL_READERS=1',f'-DKUI_NATIVE_READAHEAD={enabled}',
                   f'-DKUI_NATIVE_GENERAL_LEAN_LAYOUT={lean}',
                   '-DKUI_VMU_NATIVE_PERSISTENT_PROBE=1','-DKUI_VMU_NATIVE_CACHED_CODE=1']
            common=[os.environ.get('HOST_CC','cc'),'-std=c11','-O1','-g','-Wall','-Wextra',
                    '-Werror','-Wpedantic','-fsanitize=address,undefined','-fno-omit-frame-pointer',
                    '-fno-pie','-no-pie','-I',str(ROOT/'include'),'-I',str(directory),*flags]
            for name,inputs in (
                ('lease',[ROOT/'tests/test_vmu_native_readahead_lease.c',ROOT/'src/core/vmu_native_stage_placement.c']),
                ('map',[ROOT/'tests/test_vmu_native_background_map.c']),
            ):
                binary=directory/f'{name}-{enabled}-{lean}'
                subprocess.run([*common,*map(str,inputs),'-o',str(binary)],check=True)
                subprocess.run([str(binary)],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})


def main(reader_only=False,host_only=False):
    stage=(ROOT/'src/loader/retail_stage.c').read_text()
    worker=(ROOT/'src/loader/vmu_native_reader_capture.inc').read_text()
    passive=(ROOT/'src/loader/vmu_native_passive_stage.inc').read_text()
    with tempfile.TemporaryDirectory(prefix='kui-native-dual-') as temporary:
        directory=Path(temporary);source=directory/'dual.c';binary=directory/'dual'
        source.write_text(PREFIX+'\n'+function_body(stage,'select_resident')+'\n'+
                          function_body(worker,'reader_readahead_exports')+'\n'+
                          function_body(passive,'native_dual_reader_bind')+'\n'+
                          'static void bind_selected(void) {'+
                          'native_dual_reader_bind((void *)(uintptr_t)0x1357u,0x2468u,0x369cu,0x48adu,0x5bdeu);'+
                          '}\n'+SUFFIX)
        flags=['-DKUI_NATIVE_DUAL_READERS=1','-DKUI_RETAIL_LOW_RESIDENT=1',
               f'-DKUI_NATIVE_READER_ONLY_ISOLATION={int(reader_only)}',
               '-DKUI_NATIVE_READAHEAD=1','-DKUI_RETAIL_MANIFEST_SLOTS=64',
               '-DKUI_RETAIL_ASYNC_TEST=1','-DKUI_RETAIL_ASYNC=1',
               '-DKUI_RETAIL_GD_ASYNC=1','-DKUI_RETAIL_TRANSPORT=1','-DKUI_SCI_SD_NO_BLOCK=1',
               '-DKUI_RETAIL_NATIVE_HYBRID=1','-DKUI_VMU_NATIVE_BACKGROUND=1',
               '-DKUI_GENERAL_SCI_RESUME_FIX=1','-DKUI_VMU_NATIVE_PERSISTENT_PROBE=1',
               '-DKUI_RETAIL_NATIVE_DRIVE_STATE=1','-DKUI_RETAIL_NATIVE_DISC_CHECK=1',
               '-DKUI_NATIVE_LOADER_TIMELINE=1','-DKUI_NATIVE_IRQ_REARM=1',
               '-DKUI_NATIVE_SDK_SCI_ROUTE=1']
        for general in ((0,) if reader_only else (0,1)):
            selected=flags+(['-DKUI_NATIVE_SPLIT_CONTROLLER_GENERAL=1','-DKUI_NATIVE_GENERAL_LEAN_LAYOUT=1',
                '-DKUI_NATIVE_SPLIT_CONTROLLER_ISOLATION=1','-DKUI_NATIVE_SPLIT_CONTROLLER_PREFETCH=1',
                '-DKUI_NATIVE_COOP_PUMP=1','-DKUI_VMU_NATIVE_CACHED_CODE=1'] if general else [])
            subprocess.run([os.environ.get('HOST_CC','cc'),'-std=c11','-O1','-g','-Wall','-Wextra',
                            '-Werror','-Wpedantic','-Wno-clobbered','-fsanitize=undefined',
                            '-fno-sanitize-recover=all','-I',str(ROOT/'include'),'-I',str(ROOT/'src/loader'),
                            *selected,str(source),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)
    if not host_only:
        print(json.dumps(console_layouts(),sort_keys=True))
        print(json.dumps(console_layouts(general=True),sort_keys=True))
    dual_lease_guards()


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reader-only',action='store_true',
                        help='also enforce the isolation module\'s New-only admission')
    parser.add_argument('--host-only',action='store_true',
                        help='run host selector/lease guards without compiling the separate SH layout proof')
    args=parser.parse_args()
    main(reader_only=args.reader_only,host_only=args.host_only)
