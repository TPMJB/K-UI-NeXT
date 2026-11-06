#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute shared native/disc FAT-window adapters with finite media boundaries.

The actual adapter, health policy and epoch invalidation instructions execute.
Only the existing card/stream/bus calls are ABI fixtures; their SPI framing and
CRC16 implementation have independent protocol tests. No elapsed-time claim.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
from check_vmu_native_worker_gateway import read_elf,symbol_bytes
from test_smash_phase_clock_linked import ClockCPU

CONTEXT,OUT,VERIFIED,STACK,CALLER=0x8cc00000,0x8cc01000,0x8cc04000,0x8c00d000,0x8c003000

def require(value,message):
    if not value:raise ValueError(message)

class WindowCPU(ClockCPU):
    def __init__(self,image,profile,transport=1):
        super().__init__(image)
        self.symbols=image[4];self.profile=profile;self.transport=transport
        self.disc=profile=='disc';self.prefix='persist_' if self.disc else 'native_persist_'
        self.cache=self.symbols['_disc_fat_read_cache' if self.disc else '_smash_fat_read_cache'][0]
        self.stream=self.symbols['_disc_fat_read_stream'][0] if self.disc else None
        self.events=[];self.health=True;self.fault=False;self.fail_at=None
        self.stop_error=False;self.post_unhealthy=False;self.post_fault=False;self.post_active=False
        self.start_error=False;self.blocks=0;self.requested=0;self.require_epoch=False
        self.boundaries={value[0]:name for name,value in self.symbols.items() if name in (
            '_kui_sci_sd_healthy','_kui_sci_sd_faulted','_kui_retail_sd_healthy','_kui_retail_sd_faulted',
            '_kui_retail_storage_read_run','_kui_retail_storage_stop',
            '_kui_loader_sd_stream_start','_kui_loader_sd_stream_next','_kui_loader_sd_stream_stop',
            '_kui_vmu_sd_identity_read','_kui_sci_sd_write','_kui_sci_sd_sync')}
        # Only ready, transport and inactive-stream fields are observed by the
        # adapter. Media API fixtures below separately validate their arguments.
        if self.disc:self.store_block(CONTEXT,bytes([1])*128)
        else:
            size=self.symbols['_card'][1]
            require(48<=size<=512,'Unexpected native storage ABI extent')
            self.store_block(CONTEXT,bytes([1])*size)
            self.store_block(CONTEXT,struct.pack('<I',transport))
            self.store_block(CONTEXT+size-24,bytes(24))
        self.store_block(OUT,bytes([0xa7])*(16*512+32))
        self.store_block(VERIFIED,struct.pack('<I',0xdeadbeef))
        self.store_block(self.cache,bytes(16512))

    def epoch_before_io(self):
        if self.require_epoch:
            require(self.read(self.cache+4)==2 and not self.read(self.cache+8) and not self.read(self.cache+12),
                    'Identity/write/sync reached media before dropping the old FAT epoch')

    def abi_return(self,result=0):
        target=self.pr
        for i in range(8):self.r[i]=(0xa7350000+0x12345*i+len(self.events))&0xffffffff
        self.r[0]=result&0xffffffff;self.pc=target

    def finish(self):
        if self.disc:self.store_block(self.stream+20,bytes([int(self.post_active)]))
        else:
            size=self.symbols['_card'][1]
            self.store_block(CONTEXT+size-4,bytes([int(self.post_active)]))
        if self.post_unhealthy:self.health=False
        if self.post_fault:self.fault=True

    def boundary(self):
        name=self.boundaries.get(self.pc)
        if name is None:return False
        if name.endswith('_healthy'):
            self.abi_return(int(self.health));return True
        if name.endswith('_faulted'):
            self.abi_return(int(self.fault));return True
        if name=='_kui_retail_storage_read_run':
            context,lba,available,dest=self.r[4:8]
            require(context==CONTEXT and lba==100+self.blocks and available==self.requested-self.blocks and
                    dest==OUT+512*self.blocks,'Native FAT window changed its contiguous bounded run')
            self.events.append(('read',lba,available));result=6 if self.fail_at==self.blocks else 0
            if not result:self.store_block(dest,bytes([self.blocks+1])*512)
            self.blocks+=1;self.abi_return(result);return True
        if name=='_kui_loader_sd_stream_start':
            require(self.r[4:8]==[CONTEXT,self.stream,100,self.requested],
                    'Disc FAT window changed stream/card/range arguments')
            self.events.append(('start',100,self.requested))
            self.store_block(self.stream+20,bytes([not self.start_error]))
            self.abi_return(4 if self.start_error else 0);return True
        if name=='_kui_loader_sd_stream_next':
            require(self.r[4:7]==[CONTEXT,self.stream,OUT+512*self.blocks] and self.read(self.stream+20,1),
                    'Disc FAT window changed per-block stream/output ownership')
            self.events.append(('read',100+self.blocks,self.requested-self.blocks))
            result=6 if self.fail_at==self.blocks else 0
            if not result:self.store_block(self.r[6],bytes([self.blocks+1])*512)
            self.blocks+=1
            if result or self.blocks==self.requested:self.store_block(self.stream+20,b'\0')
            self.abi_return(result);return True
        if name in ('_kui_retail_storage_stop','_kui_loader_sd_stream_stop'):
            require(self.r[4]==CONTEXT and (not self.disc or self.r[5]==self.stream),
                    'FAT stop changed its exact owned card/stream')
            self.events.append(('stop',));self.finish();self.abi_return(3 if self.stop_error else 0);return True
        self.epoch_before_io();self.events.append((name,))
        self.abi_return(1) # Identity, write and sync failures still retire cache authority first.
        return True

    def invoke(self,name,args,extra=()):
        self.r=[0x53530000+i*0x12345 for i in range(16)]
        self.r[4:4+len(args)]=args;self.r[15]=STACK
        self.store_block(STACK,struct.pack('<'+'I'*len(extra),*extra))
        self.pc=self.symbols[name][0];self.pr=CALLER;self.sr=0x700000f0
        before=self.r[8:15].copy();self.instructions=0;self.writes=[]
        for _ in range(100000):
            if self.pc==CALLER:
                require(self.r[8:15]==before and self.r[15]==STACK,'FAT adapter changed caller C ABI state')
                return self.r[0]
            if not self.boundary():self.step()
        raise ValueError('FAT adapter exceeded its finite instruction bound')

def audit(path,profile='native',transport=1):
    require(profile in ('native','disc'),'Unknown window-adapter profile')
    path=Path(path);image=read_elf(path);symbols=image[4]
    prefix='persist_' if profile=='disc' else 'native_persist_'
    bodies={}
    for suffix in ('fat_read_window','identity','write','sync'):
        name='_'+prefix+suffix;address,body,_=symbol_bytes(image,name,2)
        bodies[name]=dict(address=hex(address),bytes=len(body),sha256=hashlib.sha256(body).hexdigest())
    cases=[]
    for label,count,changes,expected in (
        ('complete',16,{},16),('short_window',1,{},1),
        ('zero_count',0,{},0),('oversize',17,{},0),
        ('crc_first',16,{'fail_at':0},0),('crc_suffix',16,{'fail_at':3},3),
        ('failed_stop',16,{'stop_error':True},0),
        ('unhealthy_after_stop',16,{'post_unhealthy':True},0),
        ('fault_after_stop',16,{'post_fault':True},0),
        ('active_after_stop',16,{'post_active':True},0),
        ('unhealthy_before',16,{'health':False},0),
        ('fault_before',16,{'fault':True},0),
        *((('start_failure',16,{'start_error':True},0),) if profile=='disc' else ()),
    ):
        cpu=WindowCPU(image,profile,transport);cpu.requested=count
        for key,value in changes.items():setattr(cpu,key,value)
        result=cpu.invoke('_'+prefix+'fat_read_window',[CONTEXT,100,count,OUT],[VERIFIED])
        verified=cpu.read(VERIFIED)
        require(verified==expected and (result==0)==bool(expected),'Window changed verified-prefix result: '+label)
        skipped=label in ('zero_count','oversize','unhealthy_before','fault_before')
        require(not cpu.events if skipped else cpu.events[-1]==('stop',),
                'Window issued I/O on refusal or skipped its mandatory stop: '+label)
        for i in range(expected):require(cpu.block(OUT+512*i,512)==bytes([i+1])*512,'Verified block bytes changed')
        require(cpu.block(OUT+16*512,32)==bytes([0xa7])*32,'Window escaped bounded output')
        cases.append(dict(case=label,verified=verified,result=result,events=cpu.events))
    epochs=[]
    for suffix in ('identity','write','sync'):
        cpu=WindowCPU(image,profile,transport);cpu.require_epoch=True
        cpu.store_block(cpu.cache,struct.pack('<4I3Q2I',0x43465453,1,1,16,10000,100,10000,132,0))
        args=[CONTEXT,OUT] if suffix=='identity' else [CONTEXT] if suffix=='sync' else [CONTEXT,100,0,OUT]
        cpu.invoke('_'+prefix+suffix,args)
        require(cpu.events and cpu.read(cpu.cache+4)==2 and not cpu.read(cpu.cache+8),
                'Cache epoch did not retire before media callback: '+suffix)
        epochs.append(suffix)
    return dict(schema='kui.native-fat-window.linked.v1',status='PASS',profile=profile,transport=transport,
                elf_sha256=hashlib.sha256(image[0]).hexdigest(),helpers=bodies,cases=cases,
                identity_write_sync_retire_before_io=epochs,
                limits='Actual linked adapter, health and cache-epoch instructions execute. Stream/card/bus calls are finite ABI media fixtures; SPI/CRC protocol has separate tests.')

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--elf',type=Path,required=True)
    parser.add_argument('--profile',choices=('native','disc'),default='native')
    parser.add_argument('--transport',type=int,choices=(0,1),default=1);parser.add_argument('--report',type=Path)
    args=parser.parse_args();result=json.dumps(audit(args.elf,args.profile,args.transport),indent=2)+'\n'
    if args.report:args.report.write_text(result)
    print(result,end='')
