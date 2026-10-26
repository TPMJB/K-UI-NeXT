#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compile the production cold callback selector, arming and config helpers.

Expected addresses come from each actual low ELF, not from the generated
headers under test. The linked SH audit separately executes the manifest
binder call wrapper; this host test never executes an SH address as host code.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
from check_vmu_native_worker_gateway import read_elf
from check_native_dual_callbacks import expected_entries
from vmu_maple_observe_symbols import generate
from test_native_dual_readers import function_body

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
#include "kui/vmu_native_passive.h"
#include "kui/vmu_maple_observe_boot.h"
#include "new-symbols.h"
#include "legacy-symbols.h"
static struct kui_retail_manifest manifest;
static struct kui_vmu_native_heap native_heap;
static struct kui_vmu_native_heap_call native_call;
static uint8_t native_installer_stack[0x10000];
static unsigned checks,refusals,publishes;
static uint32_t refusal_phase,refusal_detail;
static bool bad_readback;
static jmp_buf refusal;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"CHECK failed line%d: %s\n",__LINE__,#x);exit(2);}}while(0)
static void __attribute__((noreturn)) native_report(uint32_t phase,uint32_t detail) {
    ++refusals;refusal_phase=phase;refusal_detail=detail;longjmp(refusal,1);
}
static void __attribute__((noreturn)) stopped(const char *text,uint32_t detail) {
    CHECK(text && !strcmp(text,"DUAL LOW CALLBACK READER REFUSED"));
    ++refusals;refusal_phase=0u;refusal_detail=detail;longjmp(refusal,1);
}
static uint32_t native_word(uint32_t address) {
    uint32_t value=*(volatile uint32_t *)(uintptr_t)address;
    return value^(bad_readback && address==(native_heap.initializer_slot|0x20000000u)?4u:0u);
}
static void native_publish(uint32_t address) {
    CHECK(address==native_heap.initializer_slot);++publishes;
}
void kui_vmu_observe_stage_install(void) {}
'''

SUFFIX=r'''
static void *map_at(uintptr_t address,int fd) {
    void *p=mmap((void *)address,4096u,PROT_READ|PROT_WRITE,
        (fd<0?MAP_PRIVATE|MAP_ANONYMOUS:MAP_SHARED)|MAP_FIXED_NOREPLACE,fd,0);
    CHECK(p==(void *)address);return p;
}
static void selections(void) {
    for(unsigned reader_i=0;reader_i<258u;reader_i++) {
        uint32_t reader=reader_i<257u?reader_i:UINT32_MAX;
        const uint32_t transports[]={0,1,2,3,255,UINT32_MAX};
        for(unsigned transport_i=0;transport_i<sizeof(transports)/sizeof(*transports);transport_i++) {
            uint32_t transport=transports[transport_i];
            manifest.reader=reader;manifest.storage_transport=transport;
            bool valid=transport==KUI_STORAGE_SCI && (reader==1u || reader==2u);
            if(!setjmp(refusal)) {
                const struct native_low_bindings *p=native_low_bindings();
                CHECK(valid && p==&native_low_bindings_table[reader-1u]);
                CHECK(!memcmp(p,expected[reader-1u],sizeof(*p)));
            } else CHECK(!valid && refusal_phase==0u && refusal_detail==reader);
        }
    }
}
static void configuration(unsigned row) {
    struct kui_vmu_native_passive_config cfg,wanted;
    memset(&cfg,0xa5,sizeof(cfg));wanted=cfg;
    wanted.sd_lease=expected[row][4];wanted.low_terminal=expected[row][1];wanted.low_heap_hook=expected[row][0];
    native_low_configure(&cfg);CHECK(!memcmp(&cfg,&wanted,sizeof(cfg)));
}
static void arming(unsigned row,unsigned variant) {
    const struct native_low_bindings *low=&native_low_bindings_table[row];
    volatile uint32_t *control=(void *)(uintptr_t)(low->control+0x20000000u);
    volatile uint32_t *original=(void *)(uintptr_t)(low->heap_original+0x20000000u);
    uint32_t *frame=(void *)(uintptr_t)0x8c00e400u;
    uint32_t old_frame[8];
    for(unsigned i=0;i<8u;i++)frame[i]=old_frame[i]=0x12345670u+i;
    memset((void *)control,0,64u);memset((void *)original,0,32u);
    native_heap.initializer_slot=0x8c020020u;native_heap.initializer_target=0x8c020040u;
    *(uint32_t *)(uintptr_t)native_heap.initializer_slot=native_heap.initializer_target;
    native_call.entry_begin=0x0c400000u;native_call.entry_bytes=0x00600000u;native_call.return_pr=0xac020052u;
    publishes=0;bad_readback=false;
    uint32_t expected_detail=0;
    if(variant<14u) {control[variant]=0x12345678;expected_detail=low->control+4u*variant;}
    else if(variant<22u) {original[variant-14u]=0x12345678;expected_detail=low->heap_original+4u*(variant-14u);}
    else if(variant==22u) {*(uint32_t *)(uintptr_t)native_heap.initializer_slot^=4;expected_detail=native_heap.initializer_slot;}
    else if(variant==23u) {bad_readback=true;expected_detail=native_heap.initializer_slot;}
    if(!setjmp(refusal)) {
        native_arm_heap_hook(frame);CHECK(variant==24u);
        CHECK(control[0]==(uint32_t)(uintptr_t)kui_vmu_observe_stage_install);
        CHECK(control[1]==(uint32_t)(uintptr_t)(native_installer_stack+sizeof(native_installer_stack)-32u));
        CHECK(control[2]==KUI_VMU_OBS_BOOT_ARMED && control[14]==0u && control[15]==0u);
        CHECK(original[0]==native_heap.initializer_target && original[1]==native_call.entry_begin &&
              original[2]==native_call.entry_bytes && original[3]==(native_call.return_pr&0x1fffffffu));
        for(unsigned i=4;i<8u;i++)CHECK(original[i]==0u);
        CHECK(*(uint32_t *)(uintptr_t)native_heap.initializer_slot==expected[row][0]);
        old_frame[5]=expected[row][1];CHECK(!memcmp(frame,old_frame,sizeof(old_frame)));
        CHECK(publishes==1u);
    } else {
        CHECK(variant<24u && refusal_phase==2u && refusal_detail==expected_detail);
        CHECK(!memcmp(frame,old_frame,sizeof(old_frame)));
        CHECK(publishes==(variant==23u?1u:0u));
    }
}
int main(void) {
    CHECK(sizeof(struct native_low_bindings)==24u);
    (void)map_at(0xac007000u,-1);(void)map_at(0x8c00e000u,-1);
    int fd=memfd_create("kui-cold-slot",0);CHECK(fd>=0 && ftruncate(fd,4096)==0);
    (void)map_at(0x8c020000u,fd);(void)map_at(0xac020000u,fd);
    selections();
    for(unsigned row=0;row<2u;row++) {
        manifest.reader=row+1u;manifest.storage_transport=KUI_STORAGE_SCI;
        configuration(row);
        for(unsigned variant=0;variant<25u;variant++)arming(row,variant);
    }
    CHECK(munmap((void *)0xac007000u,4096u)==0 && munmap((void *)0x8c00e000u,4096u)==0);
    CHECK(munmap((void *)0x8c020000u,4096u)==0 && munmap((void *)0xac020000u,4096u)==0 && close(fd)==0);
    printf("dual callback host helpers: %u checks; %u refusals; both linked ELF tuples, exact arming/config writes and refusal ordering passed\n",checks,refusals);
    return 0;
}
'''


def production_source(directory):
    stage=(ROOT/'src/loader/vmu_native_heap_probe_stage.c').read_text()
    passive=(ROOT/'src/loader/vmu_native_passive_stage.inc').read_text()
    begin=stage.index('struct native_low_bindings {')
    end=stage.index('__asm__(',begin)
    expected=[expected_entries(read_elf(directory/name)) for name in ('resident-sci.elf','resident-legacy.elf')]
    values='static const uint32_t expected[2][6]={\n'+',\n'.join(
        '{'+','.join(f'0x{value:08x}u' for value in row)+'}' for row in expected)+'\n};\n'
    return PREFIX+'\n'+values+stage[begin:end]+'\n'+function_body(stage,'native_low_bindings')+'\n'+\
        function_body(stage,'native_arm_heap_hook')+'\n'+function_body(passive,'native_low_configure')+'\n'+SUFFIX


def check(directory):
    directory=Path(directory)
    new=generate(directory/'resident-sci.elf')
    legacy=generate(directory/'resident-legacy.elf',prefix='LEGACY_')
    saved=directory/'vmu_maple_observe_symbols.h'
    if saved.exists():
        assert saved.read_text()==new,'Default generator output changed for an existing SCI header'
    assert '#ifndef KUI_VMU_MAPLE_OBSERVE_SYMBOLS_H\n' in new
    assert '#ifndef LEGACY_KUI_VMU_MAPLE_OBSERVE_SYMBOLS_H\n' in legacy
    assert len(re.findall(r'^#define LEGACY_KUI_VMU_\w+ 0x[0-9a-f]+u$',legacy,re.M))==6
    for prefix in ('1BAD','bad-prefix','x\n#define BAD','lowercase','A B',';'):
        try:generate(directory/'resident-sci.elf',prefix=prefix)
        except ValueError:pass
        else:raise AssertionError('Invalid macro prefix was accepted')
    source=production_source(directory)
    mutants={
        'old-sci-only-y':source.replace('LEGACY_KUI_VMU_', 'KUI_VMU_'),
        'wrong-heap-config':source.replace('cfg->low_heap_hook=low->heap_hook;',
                                         'cfg->low_heap_hook=low->return_hook;'),
        'wrong-owner-return':source.replace('frame)[5]=low->return_hook;',
                                           'frame)[5]=low->heap_hook;'),
    }
    with tempfile.TemporaryDirectory(prefix='kui-dual-callbacks-') as temporary:
        work=Path(temporary)
        (work/'new-symbols.h').write_text(new);(work/'legacy-symbols.h').write_text(legacy)
        for name,text in [('production',source),*mutants.items()]:
            assert name=='production' or text!=source,'Counterfactual did not change production source'
            input_=work/(name+'.c');output=work/name;input_.write_text(text)
            subprocess.run([os.environ.get('HOST_CC','cc'),'-std=c11','-O1','-g','-Wall','-Wextra',
                '-Werror','-Wpedantic','-Wno-clobbered','-fsanitize=undefined','-fno-sanitize-recover=all',
                '-fno-pie','-no-pie','-DKUI_NATIVE_DUAL_READERS=1','-DKUI_VMU_NATIVE_PERSISTENT_PROBE=1',
                '-I',str(ROOT/'include'),'-I',str(ROOT/'src/loader'),'-I',str(work),str(input_),'-o',str(output)],check=True)
            result=subprocess.run([str(output)],capture_output=True,text=True)
            if name=='production':
                assert result.returncode==0,result.stderr
                print(result.stdout,end='')
            else:assert result.returncode!=0,'Counterfactual wrongly passed: '+name
    print('Generator prefix guards and three old/wrong-target source counterfactuals refused.')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory',type=Path,help='Build with both actual low reader ELFs')
    args=parser.parse_args();check(args.directory)


if __name__=='__main__':main()
