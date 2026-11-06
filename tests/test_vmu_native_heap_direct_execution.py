#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Replay original syMalloc 1.35 with the production heap ownership checks.

Supply owner-provided startup files; no retail instructions are embedded here.
The unchanged SH integer model executes the original allocator. The actual C
heap API discovers metadata and checks every bottom-reservation transition and
later release against coherent reads from that model's RAM. This is an
allocator proof, not whole-game admission or a console timing measurement.
"""
import argparse
import ctypes as ct
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
sys.path.insert(0,str(ROOT/'tests'))
from test_vmu_native_heap_execution import SH,intersects,read_elf
from prove_vmu_native_evolution1_readahead_lease import geometry_defines
from native_runtime_layout import BACKGROUND_COMPACT
from check_native_bottom_heap_worker import Worker,LEASE,READER,OUTPUT
from check_native_bottom_heap import records as typed_records

U=ct.c_uint32
class Heap(ct.Structure):
    _fields_=[(name,U) for name in ('version','bytes','family','layout','image_bytes','load',
        'initializer','initializer_slot','initializer_target','allocate','release',
        'dispatch_cell','dispatch_table','sentinel','cursor_cell','begin_cell','bytes_cell',
        'begin_copy_cell')]

class Lease(ct.Structure):
    _fields_=[(name,U) for name in ('version','bytes','phase','initializations')]+[
        ('owner',Heap)]+[(name,U) for name in
        ('begin','heap_bytes','lease_bytes','header','payload','units','remaining')]

class Call(ct.Structure):
    _fields_=[(name,U) for name in ('load','call','return_pr','entry_begin','entry_bytes')]

READ=ct.CFUNCTYPE(ct.c_bool,ct.c_void_p,U,ct.POINTER(U))
BEGIN=0x0c400000
HEAP_BYTES=0x00c00000
STACK=0x8c3fffe0

def compile_api(directory,profile=None):
    output=Path(directory)/('heap-'+str(profile)+'.so')
    defines=geometry_defines(True,BACKGROUND_COMPACT) if profile=='compact' else (
        ['-DKUI_NATIVE_PHYSICAL_DISC=1','-DKUI_NATIVE_VMU_LITE=1','-DKUI_NATIVE_VMU_ONLY=1',
         '-DKUI_VMU_NATIVE_CACHED_CODE=1','-DKUI_VMU_NATIVE_PERSISTENT_PROBE=1',
         '-DKUI_NATIVE_SPLIT_CONTROLLER_ISOLATION=1','-DKUI_NATIVE_SPLIT_CONTROLLER_GENERAL=1',
         '-DKUI_NATIVE_GENERAL_LEAN_LAYOUT=1']
        if profile=='disc' else [])
    subprocess.run([*shlex.split(os.environ.get('HOST_CC','cc')),'-std=c11','-O2',
        '-shared','-fPIC','-Wall','-Wextra','-Werror',*defines,'-I',str(ROOT/'include'),
        str(ROOT/'src/core/vmu_native_heap.c'),str(ROOT/'src/core/vmu_native_stage_placement.c'),'-o',str(output)],
        check=True,capture_output=True,text=True)
    lib=ct.CDLL(str(output))
    signatures={
        'discover':([ct.c_void_p,ct.c_size_t,U,ct.POINTER(Heap)],ct.c_int),
        'call_discover':([ct.c_void_p,ct.c_size_t,ct.POINTER(Heap),ct.POINTER(Call)],ct.c_bool),
        'metadata_valid':([ct.POINTER(Heap)],ct.c_bool),
        'bottom_code_valid':([ct.POINTER(Heap),READ,ct.c_void_p],ct.c_bool),
        'bottom_fresh':([ct.POINTER(Heap),READ,ct.c_void_p,U,U,U,U,ct.POINTER(Lease)],ct.c_bool),
        'preallocate':([ct.POINTER(Lease),READ,ct.c_void_p,U],ct.c_bool),
        'bottom_placeholder':([ct.POINTER(Lease),READ,ct.c_void_p,U,U],ct.c_bool),
        'bottom_allocated':([ct.POINTER(Lease),READ,ct.c_void_p,U,U],ct.c_bool),
        'bottom_ready':([ct.POINTER(Lease),READ,ct.c_void_p,U],ct.c_bool),
        'lease_valid':([ct.POINTER(Lease),READ,ct.c_void_p,U],ct.c_bool),
        'lease_valid_with_release':([ct.POINTER(Lease),READ,ct.c_void_p,U,U,ct.POINTER(U)],ct.c_bool)}
    for suffix,(args,result) in signatures.items():
        function=getattr(lib,'kui_vmu_native_heap_'+suffix)
        function.argtypes=args;function.restype=result
        setattr(lib,suffix,function)
    lib.placement=lib.kui_vmu_native_stage_bottom_allocation_safe
    lib.placement.argtypes=[ct.POINTER(Heap),U,U,U,U,U];lib.placement.restype=ct.c_bool
    assert ct.sizeof(Heap)==72 and ct.sizeof(Lease)==116
    return lib

def linked_layout(build,profile):
    """Read complete live stage and its actual private installer stack."""
    stage=Path(build)/('stage.elf' if profile=='compact' else 'disc-stage.elf')
    data=stage.read_bytes();elf=read_elf(stage);symbols=elf[4];sections=elf[3]
    begin=0x8ce00000
    end=symbols['__retail_stage_bss_end' if profile=='compact' else '__disc_vmu_stage_bss_end'][0]
    stack,size,kind,index=symbols['_native_installer_stack'];section=sections[index]
    assert size==0x10000 and kind==1 and not stack&31 and section[1]==8 and section[2]&3==3
    assert begin<=stack<stack+size<=end<=0x8cfe0000
    header=struct.unpack_from('<16sHHIIIIIHHHHHH',data)
    assert data[:7]==b'\x7fELF\x01\x01\x01' and header[2]==42 and header[9]==32
    loads=[]
    for index in range(header[10]):
        kind,offset,address,_,stored,memory,flags,_=struct.unpack_from('<8I',data,header[5]+index*32)
        if kind==1:
            assert begin<=address<=end-memory and stored<=memory
            loads.append((address,data[offset:offset+stored],memory,flags))
    assert loads and any(address<=stack and stack+size<=address+memory and flags&2
        for address,_,memory,flags in loads)
    worker=(Path(build)/'native-worker.bin').read_bytes();worker_elf=read_elf(Path(build)/'native-worker.elf')
    marker='__kui_native_background_compact_worker' if profile=='compact' else '__kui_native_physical_disc_worker'
    assert worker_elf[4].get(marker)==(1,0,0,0xfff1),'linked worker profile differs'
    export=struct.unpack_from('<20I',worker)
    origin=(export[7]&0x1fffffff)-len(worker)
    span=(worker_elf[4]['__vmu_native_worker_end'][0]&0x1fffffff)-origin
    assert span==0x12000 and 80<=len(worker)<span
    assert export[:3]==(0x4b4e5031,4,80) and export[8]<=export[9]<export[10]==export[11]
    assert export[7:12]==tuple(worker_elf[4][name][0] for name in (
        '__vmu_native_worker_bss_begin','__vmu_native_worker_bss_end','__vmu_native_worker_stack_bottom',
        '__vmu_native_worker_stack_top','__vmu_native_worker_end'))
    assert (export[9]&0x1fffffff)>=origin+len(worker) and export[10]-export[9]==(0xc00 if profile=='compact' else 0x1000)
    _,shape,kind,section=typed_records(worker_elf)['_heap_shapes@vmu_native_heap.c']
    assert kind==1 and section[1]==1 and section[2]&7==2
    assert shape==struct.pack('<8h',0x1d0,0xe2,0x1b0,1,0x268,0xae,0x160,3)
    return dict(profile=profile,stage=stage,begin=begin,end=end,stack=stack,stack_bytes=size,
        sp=stack+size-32,loads=loads,worker=worker,worker_span=span,
        worker_path=Path(build)/'native-worker.elf',
        artifact_sha256={name:hashlib.sha256((Path(build)/name).read_bytes()).hexdigest()
            for name in (stage.name,'native-worker.elf','native-worker.bin')})

def exercise(lib,path,layout=None):
    source=Path(path).read_bytes()
    image=ct.create_string_buffer(source)
    owner=Heap()
    assert lib.discover(image,len(source),0x8c010000,ct.byref(owner))==1,'original direct allocator not resolved'
    assert (owner.version,owner.bytes,owner.family,owner.layout)==(1,72,3,2)
    assert owner.allocate==owner.initializer+0xae and owner.release==owner.initializer+0x160
    assert owner.dispatch_cell==owner.dispatch_table==0,'direct owner must not invent dispatch metadata'
    assert lib.metadata_valid(ct.byref(owner))
    call=Call();startup_call=bool(lib.call_discover(image,len(source),ct.byref(owner),ct.byref(call)))
    cases=[]
    configurations=[(BEGIN|alias,HEAP_BYTES,lease_bytes) for alias in (0,0x80000000,0xa0000000)
        for lease_bytes in (0x47000,0x14000)] if layout is None else [
        (call.entry_begin,call.entry_bytes,0x47000 if layout['profile']=='compact' else 0x14000)]
    if layout is not None:assert startup_call,'original startup arguments not uniquely resolved'
    for begin,heap_bytes,lease_bytes in configurations:
            alias=begin&0xe0000000;stack=STACK if layout is None else layout['sp']
            cpu=SH(source);reads=[];bad_reads=[]
            compiled=None;compiled_calls=compiled_reads=compiled_refusals=0
            if layout is not None:
                compiled=Worker(layout['worker_path'],direct=True);compiled.reader_source=cpu.read
                assert lib.placement(ct.byref(owner),len(source),layout['begin'],layout['end'],begin,heap_bytes)
                cpu.store_block(layout['begin'],bytes((i*17+53)&255 for i in range(layout['end']-layout['begin'])))
                for address,data,_,_ in layout['loads']:cpu.store_block(address,data)
                stage_before=cpu.block(layout['begin'],layout['end']-layout['begin'])
            def stage_intact(writes):
                if layout is None:return
                before=stack-16-layout['begin'];after=stack-layout['begin']
                current=cpu.block(layout['begin'],layout['end']-layout['begin'])
                assert current[:before]==stage_before[:before] and current[after:]==stage_before[after:]
                assert all(not intersects(address,width,layout['begin'],layout['end']-layout['begin']) or
                    stack-16<=address and address+width<=stack for _,address,_,width in writes)
            @READ
            def read(_context,address,out):
                try:
                    assert not address&3 and address&0xe0000000 in (0,0x80000000,0xa0000000)
                    value=cpu.read(address)
                except (AssertionError,ValueError):
                    bad_reads.append(address);return False
                reads.append(address);out[0]=value;return True
            assert lib.bottom_code_valid(ct.byref(owner),read,None),'complete original code contract refused'
            _,writes,init_instructions=cpu.run(owner.initializer,[begin,heap_bytes],stack);stage_intact(writes)
            lease=Lease()
            def linked_guard(suffix,candidate=None):
                nonlocal compiled_calls,compiled_reads
                if compiled is None:return
                compiled.store_block(LEASE,bytes(lease));compiled.write(OUTPUT,0x5a3c7e19)
                arguments=[LEASE,READER,0,1]
                if candidate is not None:arguments.extend([candidate,OUTPUT])
                result=compiled.call('_kui_vmu_native_heap_'+suffix,arguments)
                compiled_calls+=1;compiled_reads+=result['reader_calls']
                assert result['result']==1 and all(compiled.reader_addresses),'actual linked direct guard refused'
                if candidate is not None:
                    count=U()
                    assert lib.lease_valid_with_release(ct.byref(lease),read,None,1,candidate,ct.byref(count))
                    assert compiled.read(OUTPUT)==count.value
            def linked_refusals():
                nonlocal compiled_calls,compiled_reads,compiled_refusals
                if compiled is None:return
                def reject(descriptor=lease,initializations=1):
                    nonlocal compiled_calls,compiled_reads,compiled_refusals
                    for suffix in ('lease_identity_valid','lease_valid','lease_valid_with_release'):
                        compiled.store_block(LEASE,bytes(descriptor));compiled.write(OUTPUT,0x5a3c7e19)
                        arguments=[LEASE,READER,0,initializations]
                        if suffix=='lease_valid_with_release':arguments.extend([0,OUTPUT])
                        result=compiled.call('_kui_vmu_native_heap_'+suffix,arguments)
                        compiled_calls+=1;compiled_reads+=result['reader_calls'];compiled_refusals+=1
                        assert not result['result'] and compiled.read(OUTPUT)==0x5a3c7e19
                        assert all(compiled.reader_addresses),'invalid direct descriptor read absent dispatch'
                witnesses=(owner.begin_cell,owner.begin_copy_cell,owner.bytes_cell,
                    owner.initializer+0x14c,owner.initializer+0x150,owner.initializer+0x154,
                    owner.sentinel+4,lease.header+4)
                for address in witnesses:
                    original=cpu.read(address);cpu.write(address,original^1)
                    reject();cpu.write(address,original)
                compiled.reader_failure=True;reject();compiled.reader_failure=False
                reject(initializations=2)
                for field in ('dispatch_cell','dispatch_table'):
                    altered=Lease.from_buffer_copy(bytes(lease));setattr(altered.owner,field,owner.sentinel-4)
                    reject(altered)
            assert lib.bottom_fresh(ct.byref(owner),read,None,begin,heap_bytes,1,lease_bytes,ct.byref(lease))
            assert lib.preallocate(ct.byref(lease),read,None,1)
            assert not lib.lease_valid(ct.byref(lease),read,None,1)
            placeholder,writes,placeholder_instructions=cpu.run(owner.allocate,[heap_bytes-lease_bytes-64],stack);stage_intact(writes)
            assert placeholder==begin+lease_bytes+64
            assert lib.bottom_placeholder(ct.byref(lease),read,None,1,placeholder)
            assert lease.phase==3 and not lib.lease_valid(ct.byref(lease),read,None,1)
            payload,writes,retained_instructions=cpu.run(owner.allocate,[lease_bytes],stack);stage_intact(writes)
            assert payload==begin+32
            assert lib.bottom_allocated(ct.byref(lease),read,None,1,payload)
            assert lib.lease_valid(ct.byref(lease),read,None,1)
            linked_guard('lease_identity_valid');linked_guard('lease_valid')
            released=U()
            assert lib.lease_valid_with_release(ct.byref(lease),read,None,1,placeholder,ct.byref(released))
            assert released.value==heap_bytes-lease_bytes-32
            linked_guard('lease_valid_with_release',placeholder)
            assert not lib.lease_valid_with_release(ct.byref(lease),read,None,1,payload,ct.byref(released))
            _,writes,release_instructions=cpu.run(owner.release,[placeholder],stack);stage_intact(writes)
            assert lib.bottom_ready(ct.byref(lease),read,None,1)
            assert lib.lease_valid(ct.byref(lease),read,None,1)
            linked_guard('lease_valid')
            assert not lib.lease_valid(ct.byref(lease),read,None,2),'a second initialization cannot retain the lease'
            linked_refusals()
            cpu.store_block(payload,bytes((i*37+(i>>5)+19)&255 for i in range(lease_bytes)))
            if layout is not None:
                # Model the complete bounded worker copy/zero span. The rest
                # of the lease stays patterned, including its private stack.
                cpu.store_block(payload,layout['worker']+bytes(layout['worker_span']-len(layout['worker'])))
            protected=cpu.block(begin,lease_bytes+32)
            calls=0
            def intact(writes):
                stage_intact(writes)
                assert cpu.block(begin,lease_bytes+32)==protected
                assert not any(intersects(address,width,begin,lease_bytes+32) for _,address,_,width in writes)
                assert lib.lease_valid(ct.byref(lease),read,None,1)
                linked_guard('lease_valid')
            def allocate(size):
                nonlocal calls
                pointer,writes,_=cpu.run(owner.allocate,[size],stack);calls+=1
                assert pointer>=payload+lease_bytes+32 and pointer<=begin+heap_bytes
                intact(writes)
                return pointer
            def release(pointer):
                nonlocal calls
                count=U()
                assert lib.lease_valid_with_release(ct.byref(lease),read,None,1,pointer,ct.byref(count))
                linked_guard('lease_valid_with_release',pointer)
                assert count.value>=32 and not count.value&31
                _,writes,_=cpu.run(owner.release,[pointer],stack);calls+=1
                intact(writes)
                # A released pointer cannot be admitted a second time.
                assert not lib.lease_valid_with_release(ct.byref(lease),read,None,1,pointer,ct.byref(count))
            for size in (1,31,32,33,512,4096,0x10000):release(allocate(size))
            for order in ((0,1,2),(2,1,0),(1,0,2)):
                pointers=[allocate(size) for size in (64,512,4096)]
                for index in order:release(pointers[index])
            # Runtime frees may move the next-fit cursor to the upper free
            # node. The narrow bootstrap bottom_ready transition required
            # the initializer's sentinel cursor only before arming.
            assert lib.lease_valid(ct.byref(lease),read,None,1)
            upper=payload+lease_bytes
            assert cpu.read(owner.sentinel)==upper and cpu.read(upper)==owner.sentinel
            assert cpu.read(upper+4)==lease.remaining
            assert cpu.read(owner.cursor_cell) in (owner.sentinel,upper)
            assert not bad_reads and all(address for address in reads),'no direct owner may read a zero dispatch role'
            assert image.raw[:len(source)]==source
            cases.append(dict(alias=hex(alias),arena_begin=hex(begin),arena_bytes=heap_bytes,lease_bytes=lease_bytes,payload=hex(payload),
                initializer_instructions=init_instructions,placeholder_instructions=placeholder_instructions,
                retained_instructions=retained_instructions,release_placeholder_instructions=release_instructions,
                subsequent_original_calls=calls,production_coherent_reads=len(reads),
                actual_linked_worker_guard_calls=compiled_calls,actual_linked_worker_coherent_reads=compiled_reads,
                actual_linked_worker_invalid_state_refusals=compiled_refusals,
                full_header_and_payload_preserved=True,release_guard_before_each_original_free=True,
                actual_stage_and_installer_stack_preserved=layout is not None,
                copied_worker_and_private_stack_preserved=layout is not None))
    return dict(file_name=Path(path).name,source_bytes=len(source),source_sha256=hashlib.sha256(source).hexdigest(),
        initializer=hex(owner.initializer),allocate=hex(owner.allocate),release=hex(owner.release),
        no_dispatch_cell_or_table=True,startup_call=dict(load=hex(call.load),call=hex(call.call),
            return_pr=hex(call.return_pr),entry_begin=hex(call.entry_begin),entry_bytes=call.entry_bytes)
            if startup_call else None,cases=cases)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--silver',type=Path)
    parser.add_argument('--time-stalkers',type=Path)
    parser.add_argument('--egg',type=Path)
    parser.add_argument('--output',type=Path)
    parser.add_argument('--build',type=Path,help='actual linked compact stage/worker for startup-derived cases')
    parser.add_argument('--disc-build',type=Path,help='actual linked physical-disc stage/worker for startup-derived cases')
    args=parser.parse_args()
    paths=[path for path in (args.silver,args.time_stalkers,args.egg) if path is not None]
    if not paths:parser.error('supply at least one original startup')
    if (args.build or args.disc_build) and not (args.time_stalkers or args.egg):
        parser.error('linked placement proof requires --time-stalkers or --egg')
    try:
        with tempfile.TemporaryDirectory(prefix='kui-direct-heap-original-') as directory:
            lib=compile_api(directory)
            result=dict(schema='kui.native.direct-heap.original-execution.v1',
                cases=[exercise(lib,path) for path in paths],console_timing=False,
                source_sha256={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in (
                    'tests/test_vmu_native_heap_direct_execution.py','tests/test_vmu_native_heap_execution.py',
                    'tools/check_native_bottom_heap_worker.py','tools/profile_vmu_native_release_sh.py',
                    'tools/check_native_bottom_heap.py','tools/check_vmu_native_worker_gateway.py',
                    'tools/sh4_delay_slots.py',
                    'src/core/vmu_native_heap.c','src/core/vmu_native_heap_bottom_code.inc',
                    'src/core/vmu_native_stage_placement.c','include/kui/vmu_native_heap.h',
                    'include/kui/vmu_native_background.h','include/kui/disc_vmu_layout.h')},
                scope='Original allocator and production heap guards; no complete game or loader admission')
            result['actual_startup_placements']=[]
            for profile,build in (('compact',args.build),('disc',args.disc_build)):
                if build is None:continue
                layout=linked_layout(build,profile);actual_lib=compile_api(directory,profile)
                result['actual_startup_placements'].append(dict(profile=profile,
                    stage_begin=hex(layout['begin']),stage_end=hex(layout['end']),
                    installer_stack=hex(layout['stack']),installer_stack_bytes=layout['stack_bytes'],
                    allocator_sp=hex(layout['sp']),artifact_sha256=layout['artifact_sha256'],
                    cases=[exercise(actual_lib,path,layout) for path in (args.time_stalkers,args.egg) if path is not None]))
    except (AssertionError,OSError,ValueError,subprocess.CalledProcessError) as error:
        parser.exit(2,'Original direct allocator proof refused: '+str(error)+'\n')
    if args.output:args.output.write_text(json.dumps(result,indent=2)+'\n')
    print('Original syMalloc 1.35:',sum(len(item['cases']) for item in result['cases']),
        'bottom transactions; production ownership/release guards, all three heap aliases, both lease sizes')

if __name__=='__main__':main()
