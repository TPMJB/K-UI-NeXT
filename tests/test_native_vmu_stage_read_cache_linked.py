#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the actual linked native snapshot stream adapter under SD faults."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import struct
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
from check_vmu_native_worker_gateway import read_elf,symbol_bytes
from check_retail_stack import load,base
from test_native_standalone_foreground_gin import Gin
CONTEXT,OUT,STACK,CALLER=0x8cc08000,0x8cc10000,0x8c00c000,0x8c003000
def require(value,message):
    if not value:raise ValueError(message)
def audit(path):
    path=Path(path);image=read_elf(path);symbols=image[4]
    names=('_native_persist_read_run','_native_persist_read','_native_persist_fat_read_one',
        '_native_persist_identity','_native_persist_write','_native_persist_sync','_native_persist_fat_read_window',
        '_kui_vc2_ce_vmu_stage_cache_read','_kui_vc2_ce_vmu_stage_cache_metadata_read',
        '_kui_vc2_ce_vmu_stage_cache_admission_peek','_kui_vc2_ce_vmu_stage_cache_admission_store',
        '_kui_vc2_ce_vmu_stage_cache_invalidate')
    bodies={name:symbol_bytes(image,name,2) for name in names}
    for name,(address,body,_) in bodies.items():
        require(body and 0x8ce00000<=address<address+len(body)<=0x8cfe0000,
            'Missing bounded high-stage implementation: '+name)
    address,size,kind,section=symbols['_native_persist_read_cache']
    require(kind==1 and address%32==0 and image[3][section][1]==8 and size==165472,
        'Read-through cache must reproduce accepted bounded aligned high-stage BSS')
    require(address+size<=symbols['__retail_stage_bss_end'][0]<=0x8cfe0000,
        'Read-through cache exceeds actual stage allocation exclusions')
    graphs=[path.parent/'stage/src/loader'/name for name in
        ('vmu_native_heap_probe_stage.ci','vc2_ce_vmu_stage_cache.ci')]
    _,calls=load(graphs,qualified=True)
    def targets(name):
        owners=[owner for owner in calls if base(owner)==name]
        require(len(owners)==1,'Missing unique adapter graph: '+name)
        return {base(target) for target,_ in calls[owners[0]]}
    for name in ('native_persist_identity','native_persist_write','native_persist_sync'):
        require('kui_vc2_ce_vmu_stage_cache_invalidate' in targets(name),
            'Fresh snapshot/memo survives identity/write/sync: '+name)
    require({'kui_vc2_ce_vmu_stage_cache_read','kui_smash_fat_cache_read',
             'kui_vc2_ce_vmu_stage_cache_admission_peek'}<=targets('native_persist_read'),
        'Actual stage bypasses snapshot or fresh FAT admission')
    require('kui_vc2_ce_vmu_stage_cache_metadata_read' in targets('native_persist_fat_read_one'),
        'Actual admission bypasses metadata-only memo/aligned receiver')
    require({'kui_retail_storage_read_run','kui_retail_storage_stop','kui_loader_sd_read'}<=targets('native_persist_read_run'),
        'Snapshot adapter bypasses existing CRC reader or confirmed teardown')
    require({'kui_retail_storage_read_run','kui_retail_storage_stop',
             'kui_vc2_ce_vmu_stage_cache_admission_store'}<=targets('native_persist_fat_read_window'),
        'FAT window bypasses CRC reader, teardown, or verified-prefix memo')
    class CPU(Gin):
        def plain(self,op,pc):
            if op==0x0008:self.sr&=~1;return
            return super().plain(op,pc)
    cpu=CPU(b'',b'',b'')
    for segment in image[2]:
        if segment[0]==1:cpu.store_block(segment[2],image[0][segment[1]:segment[1]+segment[4]])
    def run(count,fail_sector=None,stop_failure=False,health_failure=False):
        cpu.store_block(OUT,bytes([0xa5])*(32*512))
        preserved=list(cpu.r[8:15]);cpu.r[4:8]=[CONTEXT,1000,count,OUT]
        cpu.r[15]=STACK;cpu.pc=symbols['_native_persist_read_run'][0]
        cpu.pr=CALLER;cpu.sr=0x700000f0;reads=[];stops=0;single=0
        for _ in range(100000):
            if cpu.pc==CALLER:
                require(cpu.r[8:15]==preserved and cpu.r[15]==STACK,'Snapshot adapter changed C ABI/stack')
                success=cpu.r[0]==0;valid=1<=count<=32
                expected=valid and fail_sector is None and not stop_failure and not health_failure
                require(success==expected,'Snapshot adapter acknowledged incomplete/uncertain read')
                require(stops==int(valid and count>1),'Snapshot teardown count differs')
                require(single==int(count==1),'Single-sector path changed')
                if success:
                    for i in range(count):
                        require(cpu.block(OUT+i*512,512)==bytes([i&255])*512,'Snapshot reordered sectors')
                return dict(count=count,fail_sector=fail_sector,failed_stop=stop_failure,
                    failed_final_health=health_failure,reads=len(reads),stops=stops,success=success)
            if cpu.pc==symbols['_native_persist_healthy'][0]:
                require(cpu.r[4]==CONTEXT,'Health checked another device')
                cpu.r[0]=int(not(health_failure and (stops or single)));cpu.pc=cpu.pr;continue
            if cpu.pc==symbols['_kui_loader_sd_read'][0]:
                _,lba,n,out=cpu.r[4:8]
                require(lba==1000 and n==1 and out==OUT,'Single read geometry differs')
                single+=1;reads.append(lba);cpu.store_block(out,bytes(512))
                cpu.r[0]=7 if fail_sector==0 else 0;cpu.pc=cpu.pr;continue
            if cpu.pc==symbols['_kui_retail_storage_read_run'][0]:
                context,lba,available,out=cpu.r[4:8];i=len(reads)
                require((context,lba,available,out)==(CONTEXT,1000+i,count-i,OUT+i*512),
                    'Snapshot crossed/repeated/reordered a physical sector')
                reads.append(lba);cpu.store_block(out,bytes([i&255])*512)
                cpu.r[0]=7 if fail_sector==i else 0;cpu.pc=cpu.pr;continue
            if cpu.pc==symbols['_kui_retail_storage_stop'][0]:
                require(cpu.r[4]==CONTEXT,'Snapshot stopped another device')
                stops+=1;cpu.r[0]=7 if stop_failure else 0;cpu.pc=cpu.pr;continue
            cpu.step()
        raise ValueError('Snapshot adapter exceeded bounded linked instruction budget')
    cases=[run(0),run(33),run(1),run(32),run(1,fail_sector=0),run(1,health_failure=True)]
    cases.extend(run(32,fail_sector=i) for i in range(32))
    cases.extend((run(32,stop_failure=True),run(32,health_failure=True)))
    # Derive one nested target-layout offset with the same SH ABI, rather
    # than mirroring the host's pointer-sized session struct or mapping BSS.
    with tempfile.TemporaryDirectory(prefix='kui-fat-memo-abi-') as folder:
        unit=Path(folder)/'layout.c';obj=Path(folder)/'layout.elf'
        unit.write_text('#include "kui/vmu_persist_stage.h"\n'
            'const uint32_t kui_memo_test_layout[]={offsetof(struct kui_vmu_persist_stage_workspace,session)+'
            'offsetof(struct kui_vmu_persist_session,phase),KUI_VMU_PERSIST_SESSION_ADMISSION,'
            'KUI_VMU_PERSIST_SESSION_VALIDATION};\n')
        subprocess.run([str(ROOT/'.deps/sh-elf/bin/sh-elf-gcc'),'-std=c11','-Os',
            '-m4-single-only','-ml','-ffreestanding','-fno-pic','-Iinclude','-nostdlib',
            '-Wl,--build-id=none,-e,_kui_memo_test_layout,-Ttext,0x8cc00000',str(unit),'-o',str(obj)],
            cwd=ROOT,check=True,stdout=subprocess.DEVNULL)
        _,layout,_=symbol_bytes(read_elf(obj),'_kui_memo_test_layout',1)
        phase_offset,admission,validation=struct.unpack('<3I',layout)
    phase_address=symbols['_native_persist_work'][0]+phase_offset
    verified_address=OUT+32*512+32
    def set_phase(value):cpu.store_block(phase_address,struct.pack('<I',value))
    def phase_counters():
        if '_smash_phase_io' in symbols:cpu.store_block(symbols['_smash_phase_io'][0],bytes(24))
    def window(count,fail_sector=None,stop_failure=False,health_failure=False,store_failure=False,phase=None):
        selected=admission if phase is None else phase;set_phase(selected);phase_counters()
        cpu.store_block(OUT,bytes([0xa5])*(32*512));cpu.store_block(verified_address,bytes([0xa5])*4)
        preserved=list(cpu.r[8:15]);cpu.r[4:8]=[CONTEXT,1000,count,OUT]
        cpu.r[15]=STACK;cpu.store_block(STACK,struct.pack('<I',verified_address))
        cpu.pc=symbols['_native_persist_fat_read_window'][0];cpu.pr=CALLER;cpu.sr=0x700000f0
        reads=[];stops=0;stores=[]
        for _ in range(100000):
            if cpu.pc==CALLER:
                require(cpu.r[8:15]==preserved and cpu.r[15]==STACK,'FAT window changed C ABI/stack')
                n=struct.unpack('<I',cpu.block(verified_address,4))[0]
                valid=1<=count<=16;prefix=count if fail_sector is None else fail_sector
                healthy=not(stop_failure or health_failure)
                can_store=valid and prefix>0 and healthy and selected==admission
                expected_n=prefix if valid and healthy and not(store_failure and can_store) else 0
                require(n==expected_n and (cpu.r[0]==0)==bool(expected_n),
                    'FAT window acknowledged unverified data/failed teardown/memo refusal')
                require(len(stores)==int(can_store),'FAT memo published outside the verified admission prefix')
                require(stops==int(valid),'FAT window did not confirm stream teardown')
                return dict(count=count,failed_sector=fail_sector,failed_stop=stop_failure,
                    failed_health=health_failure,failed_memo=store_failure,phase=selected,
                    verified=n,memo_calls=len(stores),reads=len(reads))
            if cpu.pc==symbols['_native_persist_healthy'][0]:
                require(cpu.r[4]==CONTEXT,'FAT health checked another device')
                cpu.r[0]=int(not(health_failure and stops));cpu.pc=cpu.pr;continue
            if cpu.pc==symbols['_kui_retail_storage_read_run'][0]:
                context,lba,available,out=cpu.r[4:8];i=len(reads)
                require((context,lba,available,out)==(CONTEXT,1000+i,count-i,OUT+i*512),
                    'FAT read changed live geometry or sector order')
                reads.append(lba);cpu.store_block(out,bytes([i])*512)
                cpu.r[0]=7 if fail_sector==i else 0;cpu.pc=cpu.pr;continue
            if cpu.pc==symbols['_kui_retail_storage_stop'][0]:
                require(cpu.r[4]==CONTEXT,'FAT window stopped another device')
                stops+=1;cpu.r[0]=7 if stop_failure else 0;cpu.pc=cpu.pr;continue
            if cpu.pc==symbols['_kui_vc2_ce_vmu_stage_cache_admission_store'][0]:
                n=count if fail_sector is None else fail_sector
                require(stops==1 and not(stop_failure or health_failure) and n>0 and selected==admission,
                    'FAT memo store precedes a healthy CRC-verified prefix and stop')
                require(cpu.r[4:8]==[symbols['_native_persist_read_cache'][0],
                    symbols['_native_persist_profile'][0],1000,n] and
                    struct.unpack('<I',cpu.block(cpu.r[15],4))[0]==OUT,
                    'FAT memo store changed exact verified bytes/map/count')
                stores.append(n);cpu.r[0]=0xffffffff if store_failure else 0;cpu.pc=cpu.pr;continue
            cpu.step()
        raise ValueError('FAT window exceeded linked instruction bound')
    window_cases=[window(0),window(17),window(1),window(16),window(16,phase=validation)]
    window_cases.extend(window(16,fail_sector=i) for i in range(16))
    window_cases.extend((window(16,stop_failure=True),window(16,health_failure=True),
        window(16,fail_sector=8,stop_failure=True),window(16,store_failure=True)))
    require(not cpu.mmio_reads and not cpu.mmio_writes,'Model unexpectedly reached hardware')
    return dict(schema='kui.native.stage-read-through-linked.v1',status='PASS',
        elf_sha256=hashlib.sha256(image[0]).hexdigest(),cache_bytes=size,
        stage_bss_end=hex(symbols['__retail_stage_bss_end'][0]),exact_adapter_execution_cases=cases,
        exact_FAT_window_execution_cases=window_cases,target_session_phase_offset=phase_offset,
        actual_helpers={name:dict(address=hex(item[0]),bytes=len(item[1]),sha256=hashlib.sha256(item[1]).hexdigest())
            for name,item in bodies.items()},
        limits='Actual compiled stream adapter executes; read/stop/health callees are adverse fixtures. Wire CRC and filesystem ownership have separate tests.')
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--elf',required=True,type=Path)
    parser.add_argument('--report',type=Path);args=parser.parse_args();report=audit(args.elf)
    output=json.dumps(report,indent=2)+'\n'
    if args.report:args.report.write_text(output)
    print(output,end='')
if __name__=='__main__':main()
