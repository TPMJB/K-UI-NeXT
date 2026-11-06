#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute linked native GIN verifier/prefix and fresh full original SHA/CRC.

The fast test-only integer SH decoder executes supplied ELF instructions. Its
short SHA executions are cross-checked with the existing Python SH model and
hashlib. No SHA, CRC, memcpy, normalization or verifier helper is substituted.
Coherent RAM/cache-operation observations are modeled, not hardware cache,
timing, interrupt delivery, FPU execution or register-bank switching.
Original executable bytes stay external and never appear in reports.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
from check_vmu_native_worker_gateway import read_elf,symbol_bytes
from test_smash_native_gin import original_contexts,OWNERS
from profile_smash_native_cold import HashCPU,CTX,DATA,DIGEST

SOURCE,LOAD,CONTEXT,GIN=0x8ce00000,0x8c010000,0x18ee60,0x8cfd0000
CAPSULE,TRANSFER,STACK,RETURN=0x8cc00000,0x8cd00000,0x8cff0000,0x8c003000
ENGINE_BYTES,TRANSFER_BYTES=1848288,8628

def require(test,message):
    if not test:raise ValueError(message)
def sha(data):return hashlib.sha256(data).hexdigest()

class State(ctypes.Structure):
    _fields_=[('r',ctypes.c_uint32*16),*[(n,ctypes.c_uint32)for n in
        ('pc','pr','sr','mach','macl','gbr','vbr','spc','ssr','ccr')],
        *[(n,ctypes.c_uint64)for n in ('steps','reads','writes')],
        *[(n,ctypes.c_uint32)for n in ('error_pc','error_op','error_address','error_kind','cache_count')],
        ('cache_lines',ctypes.c_uint32*1024),('cache_ops',ctypes.c_uint32*1024),
        *[(n,ctypes.c_uint32)for n in ('cache_last','cache_jumps','cache_opcode',
            'ccr_writes','ccr_writer_pc','ccr_last_write','break_pc')],
        ('watch_first',ctypes.c_uint32*4),('watch_end',ctypes.c_uint32*4),
        ('watch_writes',ctypes.c_uint32*4),('minimum_stack',ctypes.c_uint32),
        ('track_first',ctypes.c_uint32),('track_end',ctypes.c_uint32),('track_bits',ctypes.c_uint8*1024),
        ('memory',ctypes.c_uint8*0x1000000)]

class Machine:
    def __init__(self,run,snapshot=None):
        self.run_native=run;self.c=State()
        if snapshot:ctypes.memmove(ctypes.addressof(self.c),snapshot,len(snapshot))
        else:
            for i in range(16):self.c.r[i]=(0x11335577+0x01030507*i)&0xffffffff
            self.c.sr=0x700000f0;self.c.ccr=0x100;self.c.minimum_stack=STACK&0x1fffffff
        self.address=ctypes.addressof(self.c)+State.memory.offset
    def snapshot(self):return ctypes.string_at(ctypes.addressof(self.c),ctypes.sizeof(self.c))
    def put(self,address,data):
        offset=(address&0x1fffffff)-0x0c000000
        require(0<=offset<=0x1000000-len(data),'Fixture outside RAM')
        ctypes.memmove(self.address+offset,bytes(data),len(data))
    def block(self,address,size):
        offset=(address&0x1fffffff)-0x0c000000
        require(0<=offset<=0x1000000-size,'Observation outside RAM')
        return ctypes.string_at(self.address+offset,size)
    def load(self,elf,delta=0):
        for p in elf[2]:
            if p[0]==1:self.put(p[2]+delta,elf[0][p[1]:p[1]+p[4]]+bytes(p[5]-p[4]))
    def execute(self,stop=RETURN,budget=1500000000,break_pc=0):
        self.c.break_pc=break_pc
        result=self.run_native(ctypes.byref(self.c),stop,budget)
        require(result>=0,f'Actual SH refused opcode/state: kind{self.c.error_kind} '
            f'pc={self.c.error_pc:08x} op={self.c.error_op:04x} address={self.c.error_address:08x}')
        return result
    def call(self,address,args=(),break_pc=0):
        self.c.pc=address;self.c.pr=RETURN;self.c.r[15]=STACK
        for i,value in enumerate(args):self.c.r[4+i]=value
        saved=list(self.c.r[8:15]);start=self.c.steps
        result=self.execute(break_pc=break_pc)
        if result==0:
            require(self.c.r[15]==STACK and list(self.c.r[8:15])==saved,'Actual SH violated callee ABI')
        return self.c.r[0],self.c.steps-start,result

def capture_seed():
    frame=[0x28,0,0x8c000000,0x8c00f400,0x700000f0,0]+[0]*7
    frame += [(0x87654321^i)&0xffffffff for i in range(13,21)]
    seed=bytearray(struct.pack('<6I',0x5349554b,0x3149474e,1,160,3,0x100)+
        struct.pack('<21I',*frame)+bytes(52))
    struct.pack_into('<I',seed,108,zlib.crc32(seed));return bytes(seed)

def menu_seed(row,selection):
    live,ip=row
    seed=bytearray(struct.pack('<8I',0x5349554b,0x31554e4d,1,304,selection,0,0,0)+live[256:512]+ip)
    struct.pack_into('<I',seed,28,zlib.crc32(seed));return bytes(seed)

def prepare(machine,verifier,mde,row,selection,offset,ip,entry=None):
    if entry:machine.load(entry,CAPSULE-0x8c010000)
    else:machine.load(verifier)
    machine.put(SOURCE,mde);machine.put(LOAD,mde);machine.put(LOAD+CONTEXT,row[0]);machine.put(GIN,row[0])
    machine.put(CAPSULE+0x200,capture_seed());machine.put(CAPSULE+offset,menu_seed(row,selection))
    actual_ip=bytearray(ip[:256]);actual_ip[240:256]=row[1];machine.put(0x8c008000,actual_ip)
    session=bytes((i*19+0x31)&255 for i in range(96));machine.put(CAPSULE+0x2a0,session)
    for index,(first,end) in enumerate(((SOURCE,SOURCE+len(mde)),(LOAD,LOAD+CONTEXT),
        (LOAD+CONTEXT+8192,LOAD+len(mde)),(CAPSULE+0x2a0,CAPSULE+0x300))):
        machine.c.watch_first[index]=first&0x1fffffff;machine.c.watch_end[index]=end&0x1fffffff
    machine.c.track_first=(LOAD+CONTEXT)&0x1fffffff;machine.c.track_end=machine.c.track_first+8192
    return bytes(actual_ip),session

def crypto_crosscheck(run,elf):
    results=[]
    for count in (0,32,64,96,160):
        data=bytes((i*73+0x53)&255 for i in range(count));c=Machine(run);c.load(elf);c.put(DATA,data)
        c.call(elf[4]['_kui_sha256_init'][0],[CTX]);c.call(elf[4]['_kui_sha256_update'][0],[CTX,DATA,count])
        before=c.block(CTX,104);c.call(elf[4]['_kui_sha256_digest'][0],[CTX,DIGEST])
        require(c.block(CTX,104)==before,'Actual digest changed live104-byte SHA context')
        require(c.block(DIGEST,32)==hashlib.sha256(data).digest(),'Actual SH SHA differs from hashlib')
        crc,_,_=c.call(elf[4]['_kui_retail_crc32'][0],[0,DATA,count])
        require(crc==zlib.crc32(data),'Actual SH CRC differs from zlib')
        python=HashCPU(elf);python.store_block(DATA,data)
        python.call('_kui_sha256_init',[CTX]);python.call('_kui_sha256_update',[CTX,DATA,count])
        require(python.block(CTX,104)==before,'Native integer decoder differs from independent Python SH model')
        python.call('_kui_sha256_digest',[CTX,DIGEST])
        require(python.block(DIGEST,32)==c.block(DIGEST,32),'Independent SH decoders disagree on digest')
        results.append(dict(bytes=count,sha256=sha(data),native_and_python_SH_equal=True,
            hashlib_and_zlib_equal=True,digest_preserves_live_104_byte_context=True))
    return results

def check_success(c,mde,row,ip,session):
    require(c.c.r[0]==0,'Actual verifier refused original owned input')
    require(c.block(SOURCE,len(mde))==mde and c.c.watch_writes[0]==0,'Verifier changed original MDE source')
    require(not any(c.c.watch_writes[1:]),'Verifier changed body or fixed session reservation')
    require(c.block(LOAD,len(mde))==mde,'Verifier did not produce exact canonical MDE')
    written={i for i in range(8192)if c.c.track_bits[i//8]>>(i%8)&1}
    require(written==set(range(16,32))|set(range(0xc0,0x200)),
        'Actual normalization wrote outside its two authorized context spans')
    saved=c.block(TRANSFER,TRANSFER_BYTES)
    require(struct.unpack_from('<4I',saved)==(0x5349554b,0x3152544e,1,8628),'Transfer shape changed')
    require(saved[16:176]==capture_seed() and saved[176:8368]==row[0] and saved[8368:8624]==ip,
        'Verifier failed to preserve exact GIN/context/IP continuation')
    require(struct.unpack_from('<I',saved,8624)[0]==zlib.crc32(saved[:8624]),'Transfer CRC differs')
    wanted=list(range(TRANSFER,TRANSFER+8628,32))+list(range(LOAD+CONTEXT,LOAD+CONTEXT+8192,32))
    require(list(c.c.cache_lines[:c.c.cache_count])==wanted and set(c.c.cache_ops[:c.c.cache_count])=={0xb3},
        'Actual verifier did not publish every transfer/context cache line')
    require(c.block(CAPSULE+0x2a0,96)==session,'Fixed2a0 session receipt was overwritten')

def owner_checkpoint(run,elf,mde,row,offset,ip):
    c=Machine(run);actual_ip,session=prepare(c,elf,mde,row,1,offset,ip)
    entry=elf[4]['_kui_smash_native_gin_verify'][0];memcmp=elf[4]['_memcmp'][0]
    _,_,result=c.call(entry,break_pc=memcmp)
    require(result==1 and c.c.r[6]==32,'Missing source-owner digest comparison')
    require(c.block(c.c.r[5],32)==hashlib.sha256(mde).digest(),'Fresh full-MDE SH SHA differs from original owner')
    # Execute the actual32-byte owner comparison, then stop at the next actual
    # memcmp call. This checkpoint follows BOTH actual whole-source hash loops.
    c.execute(stop=c.c.pr);require(c.c.r[0]==0,'Actual owner digest comparison failed')
    c.execute(break_pc=memcmp)
    require(c.c.r[6]==CONTEXT,'Checkpoint is not the first complete loaded-body comparison')
    require(c.c.watch_writes[0]==0,'Source owner proof wrote its source')
    return c.snapshot(),dict(actual_full_source_bytes=len(mde),actual_fresh_sha256=sha(mde),
        actual_fresh_crc32=f'{zlib.crc32(mde):08x}',actual_instructions=c.c.steps,
        source_writes=0),actual_ip,session

def crc_preserving_change(data):
    b=bytearray(data);b[0]^=1;base=zlib.crc32(b);wanted=zlib.crc32(data)^base;basis={}
    for i in range(32):
        test=b.copy();test[-4+i//8]^=1<<(i%8);v=zlib.crc32(test)^base;mask=1<<i
        while v:
            bit=v.bit_length()-1
            if bit in basis:v^=basis[bit][0];mask^=basis[bit][1]
            else:basis[bit]=(v,mask);break
    mask=0
    while wanted:
        bit=wanted.bit_length()-1;v,m=basis[bit];wanted^=v;mask^=m
    for i in range(32):
        if mask>>i&1:b[-4+i//8]^=1<<(i%8)
    require(zlib.crc32(b)==zlib.crc32(data) and sha(b)!=sha(data),'Failed independent CRC-collision fixture')
    return bytes(b)

def highstage_and_bridge(run,stage,snapshot,row,selection,ip):
    def imported():
        c=Machine(run,snapshot);c.load(stage)
        c.put(stage[4]['__smash_native_menu_seed'][0],menu_seed(row,selection))
        c.c.ccr=0x101;c.c.cache_count=c.c.cache_jumps=c.c.ccr_writes=0
        c.c.watch_first[0]=c.c.watch_end[0]=0
        return c
    begin=stage[4]['_smash_native_gin_begin'][0];stopped=stage[4]['_stopped'][0]
    saved=stage[4]['_smash_native_gin_saved'][0];c=imported()
    _,steps,result=c.call(begin,break_pc=stopped)
    require(result==0 and c.block(saved,8628)==c.block(TRANSFER,8628),'Actual highstage failed exact continuation import')
    ready=c.snapshot();refusals=[]
    for address in (TRANSFER,TRANSFER+24,TRANSFER+176,TRANSFER+8368,TRANSFER+8624):
        bad=imported();bad.put(address,bytes([bad.block(address,1)[0]^1]))
        _,_,stop=bad.call(begin,break_pc=stopped)
        require(stop==1,'Actual highstage accepted a corrupted transfer')
        refusals.append(dict(kind='transfer',offset=address-TRANSFER))
    bad=imported();bad.c.sr^=0x10000000
    _,_,stop=bad.call(begin,break_pc=stopped);require(stop==1,'Actual highstage accepted wrong active bank')
    refusals.append(dict(kind='SR_RB'))
    # A synchronized folder-row alteration lies in the original writable
    # footprint; only the full high-stage stock-row proof may authorize it.
    bad=imported();transfer=bytearray(bad.block(TRANSFER,8628));transfer[176+0xc0]^=1
    struct.pack_into('<I',transfer,8624,zlib.crc32(transfer[:8624]));bad.put(TRANSFER,transfer)
    _,_,stop=bad.call(begin,break_pc=stopped);require(stop==1,'Actual highstage accepted a foreign folder-table row')
    refusals.append(dict(kind='folder_row_with_fresh_transfer_CRC'))
    restore=stage[4]['_smash_native_gin_restore_arguments'][0]
    c=Machine(run,ready);c.c.cache_count=c.c.cache_jumps=c.c.ccr_writes=0
    c.put(LOAD+CONTEXT,b'\xa5'*8192)
    _,restore_steps,result=c.call(restore,break_pc=stage[4]['_relay_stopped'][0])
    require(result==0 and c.block(LOAD+CONTEXT,8192)==row[0] and c.block(0x8c008000,256)==ip,
        'Actual argument restoration failed exact context/IP ownership')
    require(c.c.cache_count==256 and c.c.cache_opcode==0xa3 and not c.c.cache_jumps and
        c.c.cache_lines[0]==LOAD+CONTEXT and c.c.cache_last==LOAD+CONTEXT+8192-32,
        'Actual restoration did not publish the complete old context before P2 overwrite')
    bad=Machine(run,ready);bad.put(0x8c008000,b'\xff')
    _,_,stop=bad.call(restore,break_pc=stage[4]['_relay_stopped'][0])
    require(stop==1,'Actual restore accepted an altered retained IP byte')
    refusals.append(dict(kind='restore_IP'))
    bridge=stage[4]['_kui_smash_native_gin_arm_scope'][0];relay=stage[4]['_kui_retail_stage_relay'][0]
    c=Machine(run,ready);c.c.ccr=0x101;c.c.cache_count=c.c.cache_jumps=c.c.ccr_writes=0
    preserved=list(c.c.r[8:15]);sr=c.c.sr;frame=0x8c00f3ac;c.put(frame,capture_seed()[24:108])
    bridge_start=c.c.steps
    _,_,stop=c.call(bridge,[frame,0x100],break_pc=relay)
    require(stop==1 and list(c.c.r[4:6])==[frame,0x100] and c.c.r[15]==STACK-12,
        'Actual bridge did not call the exact relay/frame on its preserved stack')
    require(c.c.cache_count==524288 and not c.c.cache_jumps and c.c.cache_opcode==0xa3 and
        c.c.cache_lines[0]==0x8c000000 and c.c.cache_last==0x8cffffe0,
        'Actual bridge did not execute each of the524288 full-RAM publication operations')
    require(c.c.ccr==0x100 and c.c.ccr_last_write==0x908 and c.c.ccr_writes==1 and
        c.c.ccr_writer_pc&0xe0000000==0xa0000000 and (c.c.sr&~1)==(sr&~1),
        'Actual bridge entered proof with wrong cache/SR authority')
    require(c.block(c.c.ccr_writer_pc+2,16)==b'\x09\0'*8,'Bridge cache transition lacks eight following NOPs')
    bridge_steps=c.c.steps-bridge_start
    # Only the already separately qualified relay body is an ABI adversary.
    # The surrounding actual96-byte bridge executes before/after that boundary.
    return_pc=c.c.pr
    for i in range(8):c.c.r[i]=(0xf00d0000+17*i)&0xffffffff
    c.c.pr=0x87654320;c.c.pc=return_pc;c.c.break_pc=0;c.execute()
    require(list(c.c.r[8:15])==preserved and c.c.r[15]==STACK and
        (c.c.sr&~1)==(sr&~1) and c.c.ccr==0x100,
        'Actual bridge failed callee/stack/SR preservation after adverse proof callback')
    return dict(actual_import_instructions=steps,exact_transfer_bytes=8628,refusals=refusals,
        restore_actual_instructions=restore_steps,restore_exact_context_bytes=8192,
        bridge=dict(address=hex(bridge),actual_instructions=c.c.steps-bridge_start,
            instructions_before_proof=bridge_steps,whole_RAM_ocbp_operations=524288,
            CCR_write='0x908',proof_entry_CCR='0x100',proof_entry=hex(relay),
            callback='ABI-adverse fixture; actual relay/allocator proof is separate',
            callee_registers_stack_SR_preserved=True))

def qualification(run,build,originals):
    files={name:(build/name).read_bytes()for name in ('native-gin-verify.elf','entry.elf','stage.elf',
        'native-gin-verify.bin','smash_native_gin_offsets.h')}
    verifier=read_elf(build/'native-gin-verify.elf');entry=read_elf(build/'entry.elf');stage=read_elf(build/'stage.elf')
    source=(originals/'MDE_US.BIN').read_bytes();ip=(originals/'IP.BIN').read_bytes()
    require(len(source)==ENGINE_BYTES and sha(source)==OWNERS['MDE_US.BIN'],'Wrong external MDE owner')
    rows=original_contexts(originals)
    offset=int(re.search(r'OFFSET\s+(0x[0-9a-f]+)',(build/'smash_native_gin_offsets.h').read_text())[1],16)
    raw=next(entry[0][p[1]:p[1]+p[4]]for p in entry[2]if p[0]==1)
    verifier_bin=(build/'native-gin-verify.bin').read_bytes();stage_blob=raw[0x2000:]
    require(raw[0x300:0x300+len(verifier_bin)]==verifier_bin and len(verifier_bin)<=0xd00,
        'Prefix does not embed this exact bounded verifier')
    require(raw[0x1e0:0x1f0]==b'KUINAD01'+struct.pack('<2I',1,16),'Wrong native adoption capability')
    require(not any(s[1]==8 and s[5] for s in verifier[3]),'Verifier has mutable BSS')
    crypto=crypto_crosscheck(run,verifier)
    checkpoint,owner,_,_=owner_checkpoint(run,verifier,source,rows[0],offset,ip)
    cases=[];normalization=[];highstage_snapshot=None;highstage_ip=None
    for selection,row in enumerate(rows,1):
        c=Machine(run,checkpoint);actual_ip=bytearray(ip[:256]);actual_ip[240:256]=row[1]
        c.put(LOAD+CONTEXT,row[0]);c.put(GIN,row[0]);c.put(0x8c008000,actual_ip)
        c.put(CAPSULE+offset,menu_seed(row,selection));session=c.block(CAPSULE+0x2a0,96)
        c.execute();check_success(c,source,row,bytes(actual_ip),session)
        if selection==4:highstage_snapshot=c.snapshot();highstage_ip=bytes(actual_ip)
        cases.append(dict(kind='selection',selection=selection,result=0))
        normalization.append(dict(selection=selection,live_different_bytes=sum(a!=b for a,b in
            zip(source[CONTEXT:CONTEXT+8192],row[0])),canonical_source_preserved=True,
            changed_only_ranges=['context+0x10..0x1f','context+0xc0..0x1ff']))
    # Refusal cases execute actual comparison/normalization tails from the
    # exact owner-verified snapshot; source bytes and completed hash state stay
    # unchanged. This avoids replacing any SHA/CRC helper with an oracle.
    mutations=[('loaded_prefix',LOAD+64,False,3),('loaded_suffix',LOAD+CONTEXT+8192,False,3),
        ('immutable_context_header',LOAD+CONTEXT,True,4),('immutable_context_gap',LOAD+CONTEXT+32,True,4),
        ('immutable_context_suffix',LOAD+CONTEXT+512,True,4),('argv',LOAD+CONTEXT+256,True,4),
        ('high_context_disagrees',GIN+48,False,4),('transfer_source',LOAD+CONTEXT+16,True,4),
        ('transfer_count',LOAD+CONTEXT+20,True,4),('transfer_mode',LOAD+CONTEXT+24,True,4),
        ('transfer_ready',LOAD+CONTEXT+28,True,4)]
    for name,address,synchronized,expected in mutations:
        c=Machine(run,checkpoint);c.put(address,bytes([c.block(address,1)[0]^1]))
        if synchronized:c.put(GIN+address-(LOAD+CONTEXT),bytes([c.block(address,1)[0]]))
        c.execute();require(c.c.r[0]==expected,'Wrong actual verifier refusal: '+name)
        require(c.c.cache_count==0 and c.c.watch_writes[0]==0,'Refused input published or changed source')
        require(c.block(TRANSFER,8628)==bytes(8628),'Refused input published a partial continuation')
        cases.append(dict(kind=name,result=expected))
    for name,changed in [('source_byte_changed',source[:1]+bytes([source[1]^1])+source[2:]),
                         ('source_crc_collision',crc_preserving_change(source))]:
        c=Machine(run);prepare(c,verifier,changed,rows[0],1,offset,ip)
        result,steps,_=c.call(verifier[4]['_kui_smash_native_gin_verify'][0])
        require(result==2 and c.c.cache_count==0 and c.c.watch_writes[0]==0 and
            c.block(SOURCE,len(changed))==changed,'Actual whole source ownership accepted/modified changed bytes')
        cases.append(dict(kind=name,result=result,instructions=steps,
            crc32_equal_to_original=zlib.crc32(changed)==zlib.crc32(source)))
    for byte in (0,16,24,108,112,159):
        c=Machine(run);prepare(c,verifier,source,rows[0],1,offset,ip)
        address=CAPSULE+0x200+byte;c.put(address,bytes([c.block(address,1)[0]^1]))
        result,_,_=c.call(verifier[4]['_kui_smash_native_gin_verify'][0])
        require(result==1 and c.c.watch_writes[0]==0,'Malformed GIN seed was accepted')
        cases.append(dict(kind='GIN_seed_byte',byte=byte,result=result))
    # Execute the actual relocated entry, including its actual verifier and
    # copy loop, stopping immediately before the first high-stage instruction.
    c=Machine(run);actual_ip,session=prepare(c,verifier,source,rows[3],4,offset,ip,entry)
    expected_stage=c.block(CAPSULE+0x2000,len(stage_blob))
    c.c.pc=CAPSULE;c.c.r[15]=0x8c00f3ac;before_sr=c.c.sr
    c.execute(stop=0xace00000)
    require(c.c.r[4]==CAPSULE+0x1000 and c.c.r[15]==STACK,'Prefix source/map/stack binding changed')
    require(c.block(0x8ce00000,len(stage_blob))==expected_stage,
        'Actual prefix did not copy the exact authenticated stage from scratch')
    require(c.c.ccr==0x101 and c.c.ccr_last_write==0x909 and c.c.ccr_writes==1 and
        c.c.ccr_writer_pc&0xe0000000==0xa0000000,
        'Prefix did not enable the cache profile through its P2 continuation')
    require(c.block(c.c.ccr_writer_pc+2,16)==b'\x09\0'*8,'Prefix cache transition lacks eight following NOPs')
    require((c.c.sr&0x700080f0)==(before_sr&0x700080f0),'Prefix changed captured SR/RB/FPU-control bits')
    require(c.block(LOAD,len(source))==source and c.block(CAPSULE+0x2a0,96)==session,
        'Prefix damaged already loaded original MDE or fixed session slot')
    prefix=dict(actual_instructions=c.c.steps,stage_bytes=len(stage_blob),
        stage_source='0xacc02000',map_pointer=hex(c.c.r[4]),entry_target=hex(c.c.pc),
        cache_profile=hex(c.c.ccr),cache_write=hex(c.c.ccr_last_write),cache_writer_pc=hex(c.c.ccr_writer_pc),
        session_2a0_bytes_preserved=96,loaded_canonical_MDE_exact=True)
    highstage=highstage_and_bridge(run,stage,highstage_snapshot,rows[3],4,highstage_ip)
    require(all((build/name).read_bytes()==data for name,data in files.items()),
        'Linked production inputs changed while qualification was executing')
    return dict(schema='kui.smash.native-gin-linked.v1',status='PASS',
        verifier_elf_sha256=sha(verifier[0]),entry_elf_sha256=sha(entry[0]),stage_elf_sha256=sha(stage[0]),
        verifier_bytes=len(verifier_bin),source_owner=owner,crypto_crosscheck=crypto,
        original_menu_context_copy_executed=True,normalization=normalization,cases=cases,prefix=prefix,
        highstage_and_bridge=highstage,
        inputs_immutable_during_execution=True,
        limits=['Coherent RAM and cache-operation sequencing model; no hardware cache effects, timing or gameplay claim.',
            'Ten context tails resume the exact actual whole-source-owner-verified instruction snapshot; the full owner proof also executes on changed-source and actual-prefix paths.',
            'Full native SDK/allocator/worker installation, original RTE bank switching and CE return are separate qualification boundaries.'])

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build',required=True,type=Path);parser.add_argument('--originals',required=True,type=Path)
    parser.add_argument('--report',type=Path);parser.add_argument('--cc',default=os.environ.get('CC','cc'))
    args=parser.parse_args()
    decoder=(ROOT/'tests/sh4_integer_fast.c').read_bytes();test_source=Path(__file__).read_bytes()
    with tempfile.TemporaryDirectory(prefix='kui-linked-gin-')as folder:
        library=Path(folder)/'integer.so'
        subprocess.run([args.cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-Wpedantic','-fPIC','-shared',
            str(ROOT/'tests/sh4_integer_fast.c'),'-o',str(library)],check=True)
        loaded=ctypes.CDLL(str(library));run=loaded.sh4_fast_run
        run.argtypes=[ctypes.POINTER(State),ctypes.c_uint32,ctypes.c_uint64];run.restype=ctypes.c_int
        report=qualification(run,args.build,args.originals)
    require((ROOT/'tests/sh4_integer_fast.c').read_bytes()==decoder and Path(__file__).read_bytes()==test_source,
        'Qualification or decoder source changed during execution')
    report['decoder_sha256']=sha(decoder);report['test_sha256']=sha(test_source)
    text=json.dumps(report,indent=2)+'\n'
    if args.report:args.report.write_text(text)
    print(text,end='')
if __name__=='__main__':main()
