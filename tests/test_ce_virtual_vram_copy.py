#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise generic CE payload delivery and reject its old byte-copy paths.

The production GD/reader/cursor/image/SCI code executes with the existing card
and scheduler fixture. An instrumented memcpy identifies unsafe sink selection;
it is not a physical VRAM model. The actual halfword function separately runs
on guarded host RAM and has an emitted-SH instruction audit when --toolchain is
supplied. No game, CE page-fault handler or console executes.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tests'))
from test_native_virtual_vram_copy import COPY_HARNESS
sys.path.insert(0,str(ROOT/'tools'))
from check_ce_virtual_vram_width import audit_code
from check_vmu_native_worker_gateway import read_elf,symbol_bytes

BASELINE='d393b859a9ee23d372daca54ef13c7839072097a'
BASELINE_HASHES={
    'src/loader/retail_async.c':'e17b7517e5d442d01a3488ce0de322a10116c4c2c3332464d60ee237715fa4cf',
    'src/core/retail_image.c':'08b6e2db82bf77f91c2ad944235d3233e96eff59f711d782984a605e1018455c'}
SOURCES=('src/loader/retail_async.c','src/loader/sci_stream.c',
         'src/core/retail_cursor.c','src/core/retail_image.c','src/core/retail_gd.c')
FLAGS=('-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wpedantic',
       '-Wno-unused-function','-fno-pie','-no-pie','-fno-builtin-memcpy',
       '-U_FORTIFY_SOURCE','-D_FORTIFY_SOURCE=0',
       '-fsanitize=address,undefined','-fno-sanitize-recover=all',
       '-DKUI_RETAIL_ASYNC_TEST=1','-DKUI_SCI_STREAM_TEST=1',
       '-DKUI_RETAIL_ASYNC=1','-DKUI_RETAIL_GD_ASYNC=1','-DKUI_RETAIL_CE=1',
       '-DKUI_RETAIL_TRANSPORT=1','-Dmemcpy=ce_copy_test_memcpy',
       '-Iinclude','-Isrc/loader','-Itests')

HARNESS=r'''
#define main inherited_ce_suite_main
#include "test_retail_async_ce.c"
#undef main
#undef memcpy
static unsigned virtual_byte_copies;
void *ce_copy_test_memcpy(void *destination,const void *source,size_t bytes) {
    uintptr_t d=(uintptr_t)destination,begin=(uintptr_t)vram;
    if(bytes && d>=begin && d<begin+sizeof(vram))++virtual_byte_copies;
    uint8_t *out=destination;const uint8_t *in=source;
    for(size_t i=0;i<bytes;i++)out[i]=in[i];
    return destination;
}
static void no_byte_sink(const char *name) {
    if(virtual_byte_copies) {
        fprintf(stderr,"%s: generic CE virtual payload reached memcpy (%u times)\n",
            name,virtual_byte_copies);exit(1);
    }
}
static void ordinary(void) {
    test_virtual_pio_read();no_byte_sink("ordinary-virtual");
    test_prefetched_virtual_tail();no_byte_sink("virtual-spill-tail");
    test_prefetch_crc_failure();no_byte_sink("crc-refusal");
}
static void pio(void) {
    static const uint32_t sizes[]={1u,3u,17u,510u,1023u,2049u,4095u};
    setup(48,true);mode(2048);reference_read(45020u,8u);
    int32_t token=request(KUI_RETAIL_GD_PIOREAD_STREAM,45020u,8u,0u);
    uint32_t done=0u;
    for(unsigned i=0u;done<8u*2048u;i++) {
        uint32_t n=sizes[i%7u];if(n>8u*2048u-done)n=8u*2048u-done;
        /* Every destination is aligned as the API requires. Odd stream
         * offsets and page-edge pieces must retain all neighboring bytes. */
        uint32_t offset=4094u+(i%2u)*8192u;
        memset(vram,0xa5,sizeof(vram));
        (void)interrupts(40u);
        put(PIECE,VBASE+offset);put(PIECE+4u,n);
        CHECK(gd(KUI_GD_PIO_TRANSFER,(uint32_t)token,PIECE)==0);
        CHECK(!memcmp(vram+offset,expected+done,n));
        CHECK(vram[offset-1u]==0xa5u && vram[offset+n]==0xa5u);
        no_byte_sink("pio-odd-page-piece");
        done+=n;(void)gd(KUI_GD_EXEC,0u,0u);R.shared.service.callback_due=0u;
    }
    CHECK(status(token)==KUI_GD_COMPLETED);
}
static void synchronous(void) {
    setup(48,true);reference_read(45020u,8u);
    static const uint32_t starts[]={0u,1u,4093u,4094u,4095u};
    static const uint32_t counts[]={1u,3u,511u,512u,2049u,4097u};
    for(unsigned i=0;i<5u;i++)for(unsigned j=0;j<6u;j++) {
        memset(vram,0xa5,sizeof(vram));
        uint32_t start=starts[i],n=counts[j],skip=17u+i;
        CHECK(kui_retail_image_read_part(&reference,45020u,skip,n,
            KUI_GAME_SECTOR_MODE1,vram+start)==KUI_GAME_OK);
        CHECK(!memcmp(vram+start,expected+skip,n));
        if(start)CHECK(vram[start-1u]==0xa5u);
        CHECK(vram[start+n]==0xa5u);no_byte_sink("sync-image-part");
    }
    CHECK(kui_retail_image_read(&reference,45020u,8u,KUI_GAME_SECTOR_MODE1,
        vram,sizeof(vram))==KUI_GAME_OK);
    CHECK(!memcmp(vram,expected,8u*2048u));no_byte_sink("sync-image-sectors");
}
int main(int argc,char **argv) {
    if(argc!=2)return 2;
    if(!strcmp(argv[1],"ordinary"))ordinary();
    else if(!strcmp(argv[1],"pio"))pio();
    else if(!strcmp(argv[1],"synchronous"))synchronous();
    else return 2;
    printf("{\"case\":\"%s\",\"checks\":%u,\"virtual_byte_copies\":%u}\n",
        argv[1],checks,virtual_byte_copies);return 0;
}
'''

POLICY=r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
static unsigned wide,fast;
#define kui_retail_ce_vram_write actual_halfword_write
#define kui_retail_ce_payload_copy actual_payload_copy
#include "kui/retail_ce_payload_copy.h"
#undef kui_retail_ce_vram_write
#undef kui_retail_ce_payload_copy
static void kui_retail_ce_vram_write(uint8_t *d,const uint8_t *s,uint32_t n) {
    (void)d;(void)s;(void)n;++wide;
}
static void *fast_copy(void *d,const void *s,size_t n) {
    (void)s;(void)n;++fast;return d;
}
#define memcpy fast_copy
FUNCTION
#undef memcpy
int main(void) {
    static const uint32_t addresses[]={0x00100ffeu,0x0c800001u,0x4243e600u,
        0x7fffffffu,0xc0000000u,0xdfffffffu,0x8c300000u,0xac300001u};
    for(unsigned i=0;i<8u;i++) {
        wide=fast=0u;kui_retail_ce_payload_copy((uint8_t *)(uintptr_t)addresses[i],0,1u);
        if(wide!=(i<6u) || fast!=(i>=6u))return 1;
    }
    printf("{\"cases\":8,\"virtual_halfword_policy\":true,\"physical_fast_path\":true}\n");
}
'''

def sha(data):return hashlib.sha256(data).hexdigest()
def old_source(source):
    """Reverse only the exact two new hunks, then bind the complete old file.

    This runs in shallow CI checkouts without requesting historical Git bytes.
    Future unrelated changes need a reviewed new baseline hash/control rather
    than silently executing a guessed old source.
    """
    body=(ROOT/source).read_text()
    include='#ifdef KUI_RETAIL_CE\n#include "kui/retail_ce_payload_copy.h"\n#endif\n'
    if body.count(include)!=1:raise AssertionError('CE include changed: '+source)
    body=body.replace(include,'',1)
    if source.endswith('retail_async.c'):
        old='    memcpy(out, bytes, count);'
        new='    kui_retail_ce_payload_copy(out,bytes,count);'
    else:
        old='        memcpy(out, image->block + inside, take);'
        new='#ifdef KUI_RETAIL_CE\n        kui_retail_ce_payload_copy(out,image->block+inside,take);\n#else\n'+old+'\n#endif'
    if body.count(new)!=1:raise AssertionError('CE sink hunk changed: '+source)
    body=body.replace(new,old,1)
    if sha(body.encode())!=BASELINE_HASHES[source]:raise AssertionError('Historical source hash differs: '+source)
    return body
def run(command,**kwargs):
    return subprocess.run(command,cwd=ROOT,text=True,capture_output=True,
        env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1'),**kwargs)
def require(result,label):
    if result.returncode:raise AssertionError(label+'\n'+result.stdout[-2000:]+result.stderr)
    return result.stdout
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path);p.add_argument('--toolchain',type=Path)
    a=p.parse_args();cc=os.environ.get('HOST_CC','cc')
    report=dict(schema='kui.generic-ce.virtual-vram-copy.v1',status='FAIL',
        baseline_ref=BASELINE,hardware_executed=False,source_sha256={
            s:sha((ROOT/s).read_bytes()) for s in (*SOURCES,'include/kui/retail_ce_payload_copy.h')},runs={},
        limits=['Existing fixture authors hardware, scheduler and guest mappings; no CE kernel or game executes.',
            'Sink instrumentation identifies memcpy selection; physical VRAM/MMU permissions remain hardware checks.'])
    with tempfile.TemporaryDirectory(prefix='kui-ce-payload-') as temporary:
        d=Path(temporary);h=d/'harness.c';h.write_text(HARNESS)
        old={}
        for source in ('src/loader/retail_async.c','src/core/retail_image.c'):
            body=old_source(source)
            path=d/Path(source).name;path.write_text(body);old[source]=path
        for variant in ('current','baseline'):
            binary=d/variant
            sources=[str(old[s]) if variant=='baseline' and s in old else s for s in SOURCES]
            require(run([cc,*FLAGS,*sources,str(h),'-o',str(binary)]),'build '+variant)
            report['runs'][variant]={}
            for case in ('ordinary','pio','synchronous'):
                result=run([str(binary),case])
                if variant=='baseline':
                    if result.returncode!=1 or 'generic CE virtual payload reached memcpy' not in result.stderr:
                        raise AssertionError('Old control failed to reproduce '+case+'\n'+result.stdout+result.stderr)
                    report['runs'][variant][case]=dict(rejected=True,reason=result.stderr.strip())
                else:report['runs'][variant][case]=json.loads(require(result,case))
        copy=d/'copy.c';copy.write_text(COPY_HARNESS.replace('"retail_vram_copy.h"',
            '"kui/retail_ce_payload_copy.h"').replace('native_vram_write','kui_retail_ce_vram_write'))
        hostflags=[cc,*FLAGS[:15],'-Iinclude']
        copybin=d/'copy';require(run([*hostflags,str(copy),'-o',str(copybin)]),'real copy build')
        report['real_halfword_copy']=json.loads(require(run([str(copybin)]),'real copy'))
        header=(ROOT/'include/kui/retail_ce_payload_copy.h').read_text()
        function=header[header.index('static inline void kui_retail_ce_payload_copy'):header.rindex('#endif')]
        policy=d/'policy.c';policy.write_text(POLICY.replace('FUNCTION',function))
        policybin=d/'policy';require(run([*hostflags,str(policy),'-o',str(policybin)]),'policy build')
        report['policy']=json.loads(require(run([str(policybin)]),'policy'))
        report['native_preservation']={}
        # Ask the real recipe for its fully inherited flags instead of
        # approximating the foreground/MMU profile's structural prerequisites.
        make_flags=require(run(['make','--no-print-directory','-s','-f','Makefile.native-ocram-bootstrap',
            '--eval=ce-copy-preservation-flags: ; @printf \'%s\\n\' \'$(RESIDENT_FLAGS)\'',
            'ce-copy-preservation-flags']),'effective native MMU flags')
        mmu_defines=tuple(x[2:] for x in shlex.split(make_flags) if x.startswith('-D'))
        report['native_mmu_effective_defines']=mmu_defines
        for name,defines in {'standard':(), 'background':('KUI_RETAIL_ASYNC=1','KUI_RETAIL_GD_ASYNC=1'),
            'native-mmu':mmu_defines}.items():
            for source in old:
                flags=[cc,'-E','-P','-Iinclude','-Isrc/loader',*['-D'+x for x in defines]]
                current=require(run([*flags,source]),'current preprocess')
                previous=require(run([*flags,str(old[source])]),'baseline preprocess')
                if current!=previous:raise AssertionError('Native preprocessing changed: '+name+'/'+source)
            report['native_preservation'][name]=True
        if a.toolchain:
            shcc=a.toolchain/'bin/sh-elf-gcc'
            shsource=d/'sink.c';shsource.write_text('#include "kui/retail_ce_payload_copy.h"\n'
                'void entry(unsigned char *d,const unsigned char *s,unsigned n) {kui_retail_ce_vram_write(d,s,n);}\n')
            elf=d/'sink.elf'
            require(run([str(shcc),'-Os','-m4-single','-ml','-ffreestanding','-fno-builtin',
                '-mfixed-range=fr0-fr15,fpul-fpul','-nostdlib','-Iinclude','-Wl,-e,_entry',
                '-Wl,-Ttext=0x8c004000',str(shsource),'-o',str(elf)]),'SH sink link')
            image=read_elf(elf);address,code,_=symbol_bytes(image,'_kui_retail_ce_vram_write',2)
            report['linked_halfword_sink']=audit_code(address,code)
        report['status']='PASS'
    if a.output:
        a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))
if __name__=='__main__':main()
