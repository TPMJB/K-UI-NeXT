#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the actual new idle predicate and unchanged production PIO release.

Fixture offsets come from target-compiled C types, not host pointer layouts.
Only PIO MMIO/SR/delay boundaries use authored RAM-backed hardware callbacks;
the release function and every idle/refusal decision execute linked SH code.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shlex
import struct
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
from smash_ce_return_view import audit as layout_audit
from check_vmu_native_worker_gateway import read_elf
from test_smash_native_gin_linked import Machine,State

VIEW=0x8cd00000
ORDER=('runtime','loan','continuation','fence','freeze','saved_region','region','manifest')
HOOKS=r'''
#include <stdint.h>
uint32_t kui_ce_return_test_mmio[9];
static const uint32_t registers[9]={0xffe00000,0xffe00004,0xffe00008,0xffe00010,
 0xffe00018,0xffc00004,0xff80002c,0xff800030,0xffe00014};
uint32_t kui_armada_ce_vmu_sd_pio_test_read(uint32_t a,unsigned w) {
 (void)w;for(unsigned i=0;i<9u;i++)if(registers[i]==a)return kui_ce_return_test_mmio[i];return 0;
}
void kui_armada_ce_vmu_sd_pio_test_write(uint32_t a,uint32_t v,unsigned w) {
 (void)w;for(unsigned i=0;i<9u;i++)if(registers[i]==a)kui_ce_return_test_mmio[i]=v;
}
uint32_t kui_armada_ce_vmu_sd_pio_test_sr(void) {return 0x400000f0;}
void kui_armada_ce_vmu_sd_pio_test_set_sr(uint32_t v) {(void)v;}
void kui_armada_ce_vmu_sd_pio_test_delay(uint32_t v) {(void)v;}
'''


def require(ok,message):
    if not ok:raise AssertionError(message)


def put(cpu,address,value,width=4):
    cpu.put(address,int(value).to_bytes(width,'little'))


def build(directory,helper,cc):
    flags=shlex.split((helper.parent/'helper-config').read_text().splitlines()[0])
    flags=[x for x in flags if not x.startswith('-DKUI_ARMADA_CE_VMU_WRITE_RUNTIME=')]
    flags+=['-DKUI_ARMADA_CE_VMU_WRITE_RUNTIME=1']+shlex.split(
        (helper.parent/'helper-config').read_text().splitlines()[1])
    hooks=directory/'hooks.c';hooks.write_text(HOOKS)
    objects=[]
    for source,name,extra in ((ROOT/'src/loader/smash_ce_return_view.c','view',()),
        (ROOT/'src/loader/armada_ce_vmu_sd_pio.c','pio',('-DKUI_ARMADA_CE_VMU_SD_PIO_TEST=1',)),
        (ROOT/'src/loader/minic.c','minic',()),(hooks,'hooks',())):
        obj=directory/(name+'.o');objects.append(obj)
        subprocess.run([cc,*flags,*extra,'-c',str(source),'-o',str(obj)],cwd=ROOT,check=True,capture_output=True)
    script=directory/'view.ld';script.write_text('OUTPUT_FORMAT("elf32-shl")\nOUTPUT_ARCH(sh)\n'
        'ENTRY(_kui_smash_ce_return_idle)\nSECTIONS { . = 0x8ce00000; '
        '.text : { *(.text .text.*) } .rodata : { *(.rodata .rodata.*) } '
        '.data : { *(.data .data.*) } .bss : { *(.bss .bss.* COMMON) } '
        '/DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) } }\n')
    elf=directory/'view.elf'
    subprocess.run([cc,'-m4-single','-ml','-nostdlib','-Wl,--gc-sections',
        '-Wl,--build-id=none','-Wl,-u,_kui_armada_ce_vmu_sd_pio_release','-T'+str(script),
        *map(str,objects),'-lgcc','-o',str(elf)],cwd=ROOT,check=True,capture_output=True)
    decoder=directory/'integer.so'
    subprocess.run([os.environ.get('HOST_CC','cc'),'-std=c11','-O2','-fPIC','-shared',
        str(ROOT/'tests/sh4_integer_fast.c'),'-o',str(decoder)],check=True,capture_output=True)
    lib=ctypes.CDLL(str(decoder));run=lib.sh4_fast_run
    run.argtypes=[ctypes.POINTER(State),ctypes.c_uint32,ctypes.c_uint64];run.restype=ctypes.c_int
    return read_elf(elf),run


def fixture(image,run,report,initialized):
    cpu=Machine(run);cpu.load(image);v=report['view'];o=report['target_layout']['offsets']
    cpu.put(VIEW,struct.pack('<8I',*(v[k] for k in ORDER)))
    def field(name,value,width=4):
        owner=name.split('.',1)[0]
        put(cpu,v[owner]+o[name],value,width)
    put(cpu,v['saved_region'],v['region'])
    for key,value in {'loan.region':v['region'],'loan.manifest':v['manifest'],
        'loan.active':v['manifest']+244,'loan.fault':v['manifest']+240,
        'loan.begin':0x8c004000,'loan.end':0x8c007ea0,'loan.magic':0x314c4356,
        'loan.started':1,'loan.state':1,'region.service.initialized':1,
        'region.service.ops.context':1,'region.service.tracks':(v['manifest']&~0x20000000)+o['manifest.slots'],
        'region.engine.manifest':v['manifest']&~0x20000000,'region.card.transport':1,
        'manifest.storage_transport':1}.items():field(key,value)
    field('region.card.sd.ready',1,1);field('region.card.sd.blocks',4096,8)
    field('manifest.card_sectors',4096,8)
    if initialized:
        for key,value in {'runtime.started':1,'runtime.ready':1,'runtime.owner':0x06123400,
            'runtime.endpoint.self':v['runtime'],'runtime.endpoint.magic':0x31574341,
            'runtime.endpoint.started':1,'runtime.queue.self':v['runtime']+o['runtime.queue.self'],
            'runtime.queue.magic':0x314a4d56,'runtime.sd.self':v['runtime']+o['runtime.sd.self'],
            'runtime.sd.magic':0x31534441,'runtime.sd.started':1,
            'runtime.sd.pio.self':v['runtime']+o['runtime.sd.pio.self'],
            'runtime.sd.pio.magic':0x4b565031}.items():field(key,value)
        field('runtime.endpoint.protocol.initialized',1,1)
    return cpu,field


def exercise(cpu,image,report,wanted):
    spans=((0xacfc0000,0x4000),(0xac004000,0x4000),(VIEW,32),(0xacfcfe78,4))
    before=[cpu.block(a,n)for a,n in spans]
    for i,(a,n)in enumerate(spans):
        cpu.c.watch_first[i]=a&0x1fffffff;cpu.c.watch_end[i]=(a&0x1fffffff)+n;cpu.c.watch_writes[i]=0
    value,steps,status=cpu.call(image[4]['_kui_smash_ce_return_idle'][0],[VIEW])
    require(status==0 and value==(wanted&0xffffffff),f'Idle result {value:08x}, expected{wanted}')
    require(before==[cpu.block(a,n)for a,n in spans]and not any(cpu.c.watch_writes),
        'Idle predicate changed caller/helper/reader state')
    return steps


def audit(helper,resident,cc=None):
    report=layout_audit(helper,resident,cc=cc)
    cc=str(cc or ROOT/'.deps/sh-elf/bin/sh-elf-gcc')
    with tempfile.TemporaryDirectory(prefix='kui-ce-return-view-')as name:
        image,run=build(Path(name),helper,cc)
        cases=[]
        for initialized in (False,True):
            cpu,_=fixture(image,run,report,initialized)
            cases.append(dict(case='initialized'if initialized else'unstarted-zero',
                instructions=exercise(cpu,image,report,1)))
        busy=('runtime.handling','runtime.worker','runtime.endpoint.handling',
            'runtime.endpoint.protocol.active','runtime.endpoint.protocol.commit_attempted',
            'runtime.sd.busy','runtime.sd.store.io.context','runtime.sd.pio.acquired',
            'runtime.sd.pio.selected','region.service.pending','region.service.executing',
            'region.service.callback_due','region.engine.active','region.engine.in_irq',
            'region.engine.opened','region.engine.hooked','region.engine.release.armed',
            'region.engine.release.depth','region.port.acquired','region.stream.state',
            'region.card.stream.active','manifest.active')
        byte_fields={'runtime.endpoint.protocol.active','runtime.endpoint.protocol.commit_attempted',
            'runtime.sd.pio.acquired','runtime.sd.pio.selected','region.port.acquired',
            'region.card.stream.active','region.port.fault','region.card.sd.ready','region.card.sd.slow'}
        for field_name in busy:
            cpu,field=fixture(image,run,report,True);field(field_name,1,1 if field_name in byte_fields else 4)
            exercise(cpu,image,report,0);cases.append(dict(case=field_name,result=0))
        for state in (1,2,3):
            cpu,field=fixture(image,run,report,True);field('runtime.queue.state',state)
            exercise(cpu,image,report,0);cases.append(dict(case='queue'+str(state),result=0))
        for name,value in (('continuation',1),('fence',1),('fence',0x100000000)):
            cpu,_=fixture(image,run,report,True)
            put(cpu,report['view'][name]+(4 if value>0xffffffff else 0),1)
            exercise(cpu,image,report,0);cases.append(dict(case=name+str(value),result=0))
        faults=('runtime.faulted','runtime.sd.faulted','runtime.sd.pio.fault','runtime.queue.failures',
            'loan.first_fault','region.port.fault','region.engine.failed','manifest.fault')
        for field_name in faults:
            cpu,field=fixture(image,run,report,True);field(field_name,1,1 if field_name in byte_fields else 4)
            exercise(cpu,image,report,-1);cases.append(dict(case=field_name,result=-1))
        for field_name,value,width in (('runtime.queue.state',4,4),('loan.state',3,4),
            ('runtime.sd.pio.captured',2,1),('region.card.sd.ready',0,1),
            ('region.card.sd.slow',1,1),('runtime.endpoint.magic',0,4)):
            cpu,field=fixture(image,run,report,True);field(field_name,value,width)
            exercise(cpu,image,report,-1);cases.append(dict(case=field_name+' invalid',result=-1))
        cpu,field=fixture(image,run,report,True);field('region.service.error',0x1234)
        exercise(cpu,image,report,1);cases.append(dict(case='completed GD error is historical',result=1))
        cpu,_=fixture(image,run,report,False);put(cpu,report['view']['runtime']+8767,1,1)
        exercise(cpu,image,report,-1);cases.append(dict(case='unstarted cache-tail mutation',result=-1))
        for field_index in range(8):
            cpu,_=fixture(image,run,report,True);put(cpu,VIEW+field_index*4,0)
            exercise(cpu,image,report,-1);cases.append(dict(case='invalid address'+str(field_index),result=-1))
        # Real production release retains captured=1 after restoring hardware.
        cpu,field=fixture(image,run,report,True)
        for key,value,width in (('runtime.sd.pio.acquired',1,1),('runtime.sd.pio.captured',1,1),
            ('runtime.sd.pio.selected',1,1),('runtime.sd.pio.mode',0x4000,4),
            ('runtime.sd.pio.data',0x80,2),('runtime.sd.pio.smr',0x80,1),('runtime.sd.pio.brr',31,1)):
            field(key,value,width)
        mmio=image[4]['_kui_ce_return_test_mmio'][0]
        cpu.put(mmio,struct.pack('<9I',0x80,31,0,4,0,0,0x4000,0x80,0))
        pio=report['view']['runtime']+report['target_layout']['offsets']['runtime.sd.pio.self']
        value,steps,status=cpu.call(image[4]['_kui_armada_ce_vmu_sd_pio_release'][0],[pio])
        require(status==0 and value==0,'Actual production PIO release failed authored restoration fixture')
        captured=cpu.block(report['view']['runtime']+report['target_layout']['offsets']['runtime.sd.pio.captured'],1)[0]
        require(captured==1,'Production release no longer retains captured diagnostic history')
        exercise(cpu,image,report,1)
        cases.append(dict(case='actual production PIO release then idle',release_instructions=steps,
            captured_after_release=captured,result=1))
    return dict(schema='kui.smash-ce.return-view.linked.v1',status='PASS',binding=report,
        source_sha256=hashlib.sha256((ROOT/'src/loader/smash_ce_return_view.c').read_bytes()).hexdigest(),
        cases=cases,idle_state_mutations=False,hardware_wallclock_measured=False,
        limits='Actual SH idle/release code; PIO MMIO/SR/delay use authored callbacks. No CE scheduler, retirement or console cache behavior is certified.')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--helper',type=Path,required=True)
    p.add_argument('--resident',type=Path,required=True);p.add_argument('--cc');p.add_argument('--report',type=Path)
    a=p.parse_args();r=audit(a.helper,a.resident,a.cc);text=json.dumps(r,indent=2)+'\n'
    if a.report:a.report.write_text(text)
    print(text,end='')
