#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the linked stage FAT cache; replace only its physical read callbacks."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
from check_vmu_native_worker_gateway import read_elf,symbol_bytes
from check_vmu_native_stage_stack import require
from check_retail_stack import load,base
from test_native_standalone_foreground_gin import Gin

CACHE,OPS,OUT,STACK,CALLER,ONE,WINDOW=(0x8cc00000,0x8cc05000,0x8cc06000,
    0x8c00d000,0x8c003000,0x8c003010,0x8c003020)


def audit(path,profile='smash'):
    require(__debug__,'Run the linked cache model without Python optimization')
    path=Path(path);image=read_elf(path);symbols=image[4]
    require(profile in ('smash','native','disc'),'Unknown FAT cache profile')
    marker='__kui_smash_native_fat_read_cache' if profile=='smash' else '__kui_native_fat_read_cache'
    require(symbols.get(marker)==(1,0,0,0xfff1),
            'Stage lacks its exact fresh FAT cache marker')
    if profile=='disc':
        require(symbols.get('__kui_disc_fat_read_cache')==(1,0,0,0xfff1),'Physical stage lacks its cache marker')
    prefix='persist_' if profile=='disc' else 'native_persist_'
    cache_name='_disc_fat_read_cache' if profile=='disc' else '_smash_fat_read_cache'
    address,size,kind,section=symbols[cache_name]
    require(kind==1 and size==16512 and address%32==0 and image[3][section][1]==8,
            'Stage FAT cache must be the exact aligned two-window BSS object')
    bodies={}
    for name in ('_kui_smash_fat_cache_read','_kui_smash_fat_cache_begin_epoch',
                 '_kui_smash_fat_cache_configure',
                 *('_'+prefix+suffix for suffix in ('fat_read_one','fat_read_window','read','identity','write','sync'))):
        entry,body,_=symbol_bytes(image,name,2)
        require(body and 0x8ce00000<=entry<entry+len(body)<=0x8cfe0000,
                'Missing bounded actual cache/stage helper: '+name)
        bodies[name]=dict(entry=hex(entry),bytes=len(body),sha256=hashlib.sha256(body).hexdigest())
    directory=path.parent/('disc/src/loader' if profile=='disc' else 'stage/src/loader')
    graphs=[directory/name for name in
            (('disc_vmu_stage.ci' if profile=='disc' else 'vmu_native_heap_probe_stage.ci'),'smash_fat_read_cache.ci')]
    frames,calls=load(graphs,qualified=True)
    def targets(name):
        found=[owner for owner in calls if base(owner)==name]
        require(len(found)==1,'Missing unique stage graph owner: '+name)
        return {base(target) for target,_ in calls[found[0]]}
    require('kui_smash_fat_cache_read' in targets(prefix+'read'),
            'Live stage admission bypasses the linked cache')
    for name in (prefix+suffix for suffix in ('identity','write','sync')):
        require('kui_smash_fat_cache_begin_epoch' in targets(name),
                'Stage does not invalidate cache before identity/write/sync: '+name)
    stream_calls=({'kui_loader_sd_stream_start','kui_loader_sd_stream_next','kui_loader_sd_stream_stop'}
                  if profile=='disc' else {'kui_retail_storage_read_run','kui_retail_storage_stop'})
    require(stream_calls<=targets(prefix+'fat_read_window'),
            'FAT window does not use the bounded stream and verified teardown')

    class CacheCPU(Gin):
        def plain(self,op,pc):
            if op==0x0008:  # CLRT, used by the actual uint64 bounds checks.
                self.sr&=~1;return
            if op&0xf00f==0x300e:  # ADDC Rm,Rn, including the carry bit.
                n,m=(op>>8)&15,(op>>4)&15
                total=self.r[n]+self.r[m]+(self.sr&1)
                self.r[n]=total&0xffffffff;self.sr=(self.sr&~1)|int(total>0xffffffff);return
            if op&0xf00f==0x300a:  # SUBC Rm,Rn, T is the unsigned borrow.
                n,m=(op>>8)&15,(op>>4)&15
                total=self.r[n]-self.r[m]-(self.sr&1)
                self.r[n]=total&0xffffffff;self.sr=(self.sr&~1)|int(total<0);return
            if op&0xf00f==0x3005:  # DMULU.L Rm,Rn.
                n,m=(op>>8)&15,(op>>4)&15;total=self.r[n]*self.r[m]
                self.macl=total&0xffffffff;self.mach=(total>>32)&0xffffffff;return
            if op&0xf0ff in (0x000a,0x001a):  # STS MACH/MACL,Rn.
                self.r[(op>>8)&15]=self.macl if op&0x10 else self.mach;return
            if op&0xf00f==0x200e:  # MULU.W Rm,Rn.
                self.macl=(self.r[(op>>8)&15]&65535)*(self.r[(op>>4)&15]&65535);return
            return super().plain(op,pc)
    cpu=CacheCPU(b'',b'',b'')
    for segment in image[2]:
        if segment[0]==1:cpu.store_block(segment[2],image[0][segment[1]:segment[1]+segment[4]])
    cpu.store_block(CACHE,bytes(16512));cpu.store_block(OPS,struct.pack('<3I',0,ONE,WINDOW))
    # Synthetic live geometry is learned from the successful VBR read.
    # The original full admission/parser independently proves its semantics.
    header=struct.pack('<4I3Q2I',0x43465453,1,0,0,10000,100,10000,0,0)
    cpu.store_block(CACHE,header)
    vbr=bytearray(512);vbr[510:]=b'\x55\xaa'
    struct.pack_into('<H',vbr,11,512);struct.pack_into('<H',vbr,14,32)
    vbr[16]=2;struct.pack_into('<I',vbr,28,100);struct.pack_into('<I',vbr,32,9900)
    struct.pack_into('<I',vbr,36,17)
    generation=0;requests=[];fail=False;verified_limit=16
    def sector(lba):
        return bytes(vbr) if lba==100 else bytes((lba+i*31+generation)&255 for i in range(512))
    def call(name,args):
        before=list(cpu.r[8:15]);cpu.r[4:4+len(args)]=args;cpu.r[15]=STACK
        cpu.pc=symbols[name][0];cpu.pr=CALLER;cpu.sr=0x700000f0
        for _ in range(200000):
            if cpu.pc==CALLER:
                require(cpu.r[8:15]==before and cpu.r[15]==STACK,'Cache changed C ABI owner state')
                return cpu.r[0]
            if cpu.pc==ONE:
                _,lba,out=cpu.r[4:7];requests.append(('one',lba,1))
                cpu.store_block(out,sector(lba));cpu.r[0]=0;cpu.pc=cpu.pr;continue
            if cpu.pc==WINDOW:
                _,lba,count,out=cpu.r[4:8];verified=cpu.read(cpu.r[15])
                require(1<=count<=16,'Linked cache issued an unbounded read window')
                requests.append(('window',lba,count));prefix=min(count,verified_limit)
                for i in range(prefix):cpu.store_block(out+512*i,sector(lba+i))
                cpu.write(verified,0 if fail else prefix);cpu.r[0]=0xffffffff if fail else 0
                cpu.pc=cpu.pr;continue
            cpu.step()
        raise ValueError('Linked FAT cache exceeded its instruction bound')
    def read(lba,success=True):
        cpu.store_block(OUT,bytes([0xa7])*512)
        result=call('_kui_smash_fat_cache_read',[CACHE,OPS,lba,OUT])
        require((result==0)==success,'Linked cache changed physical callback result')
        if success:require(cpu.block(OUT,512)==sector(lba),'Linked cache returned stale/unverified bytes')
        else:require(cpu.block(OUT,512)==bytes([0xa7])*512,'Failed cache read published bytes')
    read(100)
    for lba in range(132,149):read(lba)
    for lba in range(149,166):read(lba)
    require(requests==[('one',100,1),('window',132,16),('window',148,1),
                       ('window',149,16),('window',165,1)],'FAT copy/tail batching changed')
    initial=list(requests)
    # A new write/identity epoch has no learned geometry or retained sectors.
    generation=19;call('_kui_smash_fat_cache_begin_epoch',[CACHE]);read(132)
    require(requests[-1]==('one',132,1),'New epoch served stale FAT geometry/data')
    read(100);verified_limit=1;read(132);read(133)
    require(requests[-2:]==[('window',132,16),('window',133,16)],
            'Unverified look-ahead suffix became cached data')
    call('_kui_smash_fat_cache_begin_epoch',[CACHE]);read(100);fail=True;read(132,False)
    fail=False;read(132)
    require(requests[-2:]==[('window',132,16),('window',132,16)],'Failed stream remained cached')
    require(not cpu.mmio_reads and not cpu.mmio_writes,'Read-only cache unexpectedly touched hardware')
    return dict(schema='kui.smash-menu.fat-cache-linked.v1',status='PASS',profile=profile,
        elf_sha256=hashlib.sha256(image[0]).hexdigest(),cache_bss=hex(address),cache_bytes=size,
        actual_helpers=bodies,actual_initial_read_sessions=initial,epochs_drop_geometry_and_data=True,
        independent_copies_and_bounded_tail=True,verified_prefix_only=True,failed_stream_not_cached=True,
        stage_identity_write_sync_invalidation_bound=True,stage_bounded_stream_and_stop_bound=True,
        compiler_graph_sha256={str(p.relative_to(path.parent)):hashlib.sha256(p.read_bytes()).hexdigest() for p in graphs},
        limits='Actual compiled cache/epoch C and libc execute; physical callbacks are fixtures. Full filesystem correctness is separately tested by actual old/new admission under sanitizers.')


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--elf',required=True,type=Path)
    parser.add_argument('--profile',choices=('smash','native','disc'),default='smash')
    parser.add_argument('--report',type=Path);args=parser.parse_args();report=audit(args.elf,args.profile)
    text=json.dumps(report,indent=2)+'\n'
    if args.report:args.report.parent.mkdir(parents=True,exist_ok=True);args.report.write_text(text)
    print(text,end='')
if __name__=='__main__':main()
