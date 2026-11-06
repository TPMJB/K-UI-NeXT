#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute linked CE return failure masks and its shared IEEE CRC.

The controller, all typed validators and both CRC entrypoints are actual ELF
instructions. The live records are authored fixtures; only MMUCR is an authored
read-only register. A forbidden-read trap bounds the immutable helper span.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tests'),str(ROOT/'tools')]
from check_vmu_native_worker_gateway import read_elf,symbol_bytes
from smash_ce_return_view import audit as view_audit
from test_smash_ce_return_loader import fixture,ENTRY,RAW
from test_smash_native_gin_linked import Machine,State,RETURN

HELPER,HELPER_BYTES,DATA=0xacfc4000,41152,0x8cc00000
RUNTIME_BYTES,CODE_CRC,RAW_CRC,PROFILE,HELPER_SIZE,HELPER_CRC=632,404,408,412,416,420
RECEIPT,VIDEO=456,552
NAMES=('profile','helper_size','helper_crc_nonzero','controller_crc_nonzero','raw_crc_nonzero',
       'return_seed','return_context','phase_video','receipt_owner','sequence_room',
       'controller_runtime_crc','raw_512_crc','helper_41152_crc')

def require(ok,message):
    if not ok:raise AssertionError(message)
def sha(data):return hashlib.sha256(data).hexdigest()
def crc(data,previous=0):return zlib.crc32(data,previous)&0xffffffff
def word(data,offset,value):struct.pack_into('<I',data,offset,value)


def decoder(directory):
    """Reuse the test integer decoder; add only MMUCR and a read-bound trap."""
    source=(ROOT/'tests/sh4_integer_fast.c').read_text()
    marker='static uint32_t rd(struct sh4_fast *c,uint32_t a,unsigned n) {'
    require(source.count(marker)==1,'Integer decoder read boundary is ambiguous')
    source=source.replace(marker,'static uint32_t ce_test_mmu=1u;\n'
        'void sh4_ce_test_mmu(uint32_t value){ce_test_mmu=value;}\n'+marker)
    marker='    if(a==0xff00001cu && n==4u)return c->ccr;'
    require(source.count(marker)==1,'Integer decoder MMIO seam is ambiguous')
    source=source.replace(marker,marker+'\n'
        '    if(a==0xff000010u && n==4u)return ce_test_mmu;\n'
        '    {uint32_t p=a&0x1fffffffu;\n'
        '     if(p>=0x0cfce0c0u && p<0x0cfd0000u){error(c,5u,a);return 0;}}')
    adapted=directory/'integer.c';adapted.write_text(source)
    shared=directory/'integer.so'
    subprocess.run([os.environ.get('HOST_CC','cc'),'-std=c11','-O2','-fPIC','-shared',
        str(adapted),'-o',str(shared)],check=True,capture_output=True)
    library=ctypes.CDLL(str(shared));run=library.sh4_fast_run
    run.argtypes=[ctypes.POINTER(State),ctypes.c_uint32,ctypes.c_uint64];run.restype=ctypes.c_int
    mode=library.sh4_ce_test_mmu;mode.argtypes=[ctypes.c_uint32];mode.restype=None
    return run,mode,dict(base_sha256=sha((ROOT/'tests/sh4_integer_fast.c').read_bytes()),
        adapted_source_sha256=sha(source.encode()),authored_register='MMUCR',
        forbidden_helper_reads=['0x0cfce0c0','0x0cfd0000'])


def patch_crc(data,offset,target,previous=0):
    """Independent GF(2) four-byte fixup, used to isolate nonzero guards."""
    changed=bytearray(data);base=crc(changed,previous);basis={}
    for bit in range(32):
        trial=changed.copy();trial[offset+bit//8]^=1<<(bit%8)
        value=crc(trial,previous)^base;mask=1<<bit
        while value:
            top=value.bit_length()-1
            if top in basis:value^=basis[top][0];mask^=basis[top][1]
            else:basis[top]=(value,mask);break
    wanted=base^target;mask=0
    while wanted:
        top=wanted.bit_length()-1
        require(top in basis,'Independent CRC fixup is singular')
        value,bits=basis[top];wanted^=value;mask^=bits
    for bit in range(32):
        if mask>>bit&1:changed[offset+bit//8]^=1<<(bit%8)
    require(crc(changed,previous)==target,'Independent CRC fixup failed')
    return changed


def invoke(cpu,address,args=(),ccr=1):
    cpu.c.ccr=ccr;cpu.c.sr=0x400000f0
    before=cpu.c.ccr_writes
    value,instructions,status=cpu.call(address,args)
    require(status==0,'Linked integrity/CRC routine did not return')
    return value,instructions,cpu.c.ccr_writes-before


def reseal_nested(wire,first,size,checksum):
    word(wire,first+checksum,0);word(wire,first+checksum,crc(wire[first:first+size]))


def predicate_cases(image,helper,view,profile,run):
    authored=fixture(image,helper,view,profile,'idle')
    cpu=Machine(run);cpu.put(0x8c000000,authored.block(0x8c000000,0x1000000))
    sy=image[4];runtime=sy['_kui_smash_ce_return_runtime'][0]
    end=sy['__smash_ce_return_binary_end'][0];entry=sy['_runtime_failures'][0]
    original_code=cpu.block(ENTRY,end-ENTRY);original_wire=cpu.block(runtime,RUNTIME_BYTES)
    canonical='_kui_smash_ce_return_raw_prefix' in sy
    raw_domain=(sy['_kui_smash_ce_return_raw_prefix'][0] if canonical else
                sy.get('__smash_ce_return_raw_shadow_begin',(RAW,))[0])
    raw_stored_bytes=260 if canonical else 512
    original_live_raw=cpu.block(RAW,512)
    original_raw=cpu.block(raw_domain,raw_stored_bytes)+(bytes(252) if canonical else b'')
    original_helper=cpu.block(HELPER,HELPER_BYTES)
    normalized=bytearray(original_code);normalized[runtime-ENTRY:runtime-ENTRY+RUNTIME_BYTES]=bytes(RUNTIME_BYTES)
    code_hash=crc(normalized)
    def restore():
        cpu.put(ENTRY,original_code);cpu.put(runtime,original_wire)
        cpu.put(RAW,original_live_raw);cpu.put(raw_domain,original_raw[:raw_stored_bytes]);cpu.put(HELPER,original_helper)
    def current_code_hash():
        current=bytearray(cpu.block(ENTRY,len(original_code)))
        current[runtime-ENTRY:runtime-ENTRY+RUNTIME_BYTES]=bytes(RUNTIME_BYTES)
        return crc(current)
    def seal(wire,code=code_hash):
        word(wire,CODE_CRC,0);word(wire,CODE_CRC,crc(wire,code))
    def check(expected):
        for index,(first,size)in enumerate(((ENTRY,len(original_code)),(raw_domain,raw_stored_bytes),(HELPER,HELPER_BYTES))):
            cpu.c.watch_first[index]=first&0x1fffffff;cpu.c.watch_end[index]=(first&0x1fffffff)+size
            cpu.c.watch_writes[index]=0
        before=(cpu.block(ENTRY,len(original_code)),cpu.block(raw_domain,raw_stored_bytes),cpu.block(HELPER,HELPER_BYTES),cpu.block(RAW,512))
        value,steps,ccr_writes=invoke(cpu,entry)
        require(value==expected,f'Expected failure mask{expected:04x}, got{value:04x}')
        require(not any(cpu.c.watch_writes) and not ccr_writes,'Read-only predicate wrote an authenticated span or CCR')
        require(before==(cpu.block(ENTRY,len(original_code)),cpu.block(raw_domain,raw_stored_bytes),cpu.block(HELPER,HELPER_BYTES),cpu.block(RAW,512)),
            'Integrity predicate changed its inputs')
        return steps
    baseline=check(0);cases=[]
    for bit,name in enumerate(NAMES):
        restore();wire=bytearray(original_wire)
        if bit==0:word(wire,PROFILE,3-profile)
        elif bit==1:word(wire,HELPER_SIZE,0xffffffff)
        elif bit==2:
            cpu.put(HELPER,patch_crc(original_helper,HELPER_BYTES-4,0));word(wire,HELPER_CRC,0)
        elif bit==3:
            # Firmware words are opaque to this predicate. A direct CRC-only
            # fixture may vary one without invoking firmware restoration.
            word(wire,CODE_CRC,0);wire=patch_crc(wire,368,0,code_hash)
        elif bit==4:
            cpu.put(raw_domain,patch_crc(original_raw,256 if canonical else 508,0)[:raw_stored_bytes]);word(wire,RAW_CRC,0)
        elif bit==5:wire[0]^=1
        elif bit==6:
            word(wire,16,1);wire[96:368]=bytes(272);reseal_nested(wire,0,368,28)
        elif bit==7:wire[VIDEO+36]^=1
        elif bit==8:
            word(wire,RECEIPT+16,0x101);reseal_nested(wire,RECEIPT,96,92)
        elif bit==9:
            word(wire,RECEIPT+88,0xffffffff);reseal_nested(wire,RECEIPT,96,92)
        elif bit==10:
            at=sy['_kui_smash_ce_return_main'][0];cpu.put(at,bytes([cpu.block(at,1)[0]^1]))
        elif bit==11:cpu.put(raw_domain,bytes([original_raw[0]^1]))
        elif bit==12:cpu.put(HELPER,bytes([original_helper[0]^1]))
        if bit!=3:seal(wire,current_code_hash() if canonical and bit in (4,11) else code_hash)
        cpu.put(runtime,wire);steps=check(1<<bit)
        restore();resolved=check(0)
        cases.append(dict(case=name,bit=bit,mask=hex(1<<bit),instructions=steps,
            resolved_mask=0,resolved_instructions=resolved))
    # Larger or malformed length fields cannot expand the fixed41152-byte
    # read; the decoder independently traps every byte beyond that span.
    bounds=[]
    for size in (0,41151,41153,0x80000000,0xffffffff):
        restore();wire=bytearray(original_wire);word(wire,HELPER_SIZE,size);seal(wire);cpu.put(runtime,wire)
        steps=check(1<<1);bounds.append(dict(helper_bytes_field=size,mask='0x2',instructions=steps,
            read_past_owned_helper=False))
    edge=[]
    for address,name,bit in ((raw_domain+raw_stored_bytes-1,'raw_last_stored_byte',11),(HELPER+HELPER_BYTES-1,'helper_last_byte',12)):
        restore();cpu.put(address,bytes([cpu.block(address,1)[0]^1]))
        expected=(1<<bit)|((1<<10) if canonical and bit==11 else 0)
        steps=check(expected)
        edge.append(dict(case=name,mask=hex(expected),instructions=steps))
    restore();wire=bytearray(original_wire);word(wire,PROFILE,3-profile);wire[VIDEO+36]^=1;seal(wire)
    cpu.put(runtime,wire);cpu.put(raw_domain,bytes([original_raw[0]^1]));cpu.put(HELPER,bytes([original_helper[0]^1]))
    combination=(1<<0)|(1<<7)|(1<<11)|(1<<12)|((1<<10) if canonical else 0);steps=check(combination)
    restore();check(0)
    retired_live_slot=None
    if raw_domain!=RAW:
        # The former bootstrap slot is not authority while CE runs. Its
        # bytes may differ; only the sealed backup may supply restoration.
        cpu.put(RAW,bytes(x^0xff for x in original_live_raw));steps=check(0)
        retired_live_slot=dict(address=hex(RAW),bytes=512,all_live_bytes_changed=True,
            mask=0,instructions=steps,live_slot_unchanged_by_validation=True)
        restore();check(0)
    return dict(entry=hex(entry),entry_sha256=sha(symbol_bytes(image,'_runtime_failures',2)[1]),
        baseline_mask=0,baseline_instructions=baseline,isolated_bits=cases,fixed_bounds=bounds,
        crc_domain_edges=edge,combined_mask=dict(mask=hex(combination),instructions=steps),
        independent_crc_zero_fixups=True,all_failures_read_only=True,all_mutations_resolved=True,
        raw_validation_domain=hex(raw_domain),raw_validation_bytes=512,raw_stored_bytes=raw_stored_bytes,
        canonical_zero_tail_bytes=252 if canonical else 0,retired_live_slot=retired_live_slot)


def crc_cases(image,run,mode):
    sy=image[4];leaf=sy['_kui_cold_load_crc32_leaf'][0]
    public=sy['_kui_retail_crc32'][0];scoped=sy['_kui_cold_load_crc32'][0]
    cpu=Machine(run);cpu.load(image)
    marker=sy.get('__kui_smash_ce_return_crc32_bitwise')
    require(marker in (None,(1,0,0,0xfff1)),'Unknown CE-only CRC capability')
    bitwise=marker is not None
    table=b''.join(struct.pack('<I',crc(bytes([i]),0xffffffff)^0xffffffff)for i in range(128))
    # IEEE's raw one-step table has zero seed and no final XOR.
    loaded=b''.join(image[0][p[1]:p[1]+p[4]]for p in image[2]if p[0]==1 and p[4])
    objects=[n for n,(a,size,kind,_)in sy.items()if kind==1 and size==512 and symbol_bytes(image,n,1)[1]==table]
    if bitwise:
        require(len(table)==512 and loaded.count(table)==0 and not objects,
            'Bitwise CE CRC retains an initialized folded/full byte table')
    else:
        require(len(table)==512 and loaded.count(table)==1 and len(objects)==1,
            'Shared folded table lacks its unique typed512-byte owner')
    vectors=0;instructions=0
    def vector(data,alignment=0,previous=0,entry=leaf,mmu=1,ccr=1):
        nonlocal vectors,instructions
        mode(mmu);address=DATA+alignment;cpu.put(address-16,b'\xa7'*16+data+b'\xa7'*16)
        before=cpu.block(address-16,len(data)+32)
        value,steps,writes=invoke(cpu,entry,[previous,address,len(data)],ccr)
        require(value==crc(data,previous),'Actual shared CRC differs from zlib')
        require(cpu.block(address-16,len(data)+32)==before,'CRC changed its input/canaries')
        require(writes==(2 if entry==scoped and not mmu and ccr in (0,0x100)else 0),
            'CRC wrapper changed its established cache-scope contract')
        require(cpu.c.ccr==ccr,'CRC wrapper did not restore the original CCR')
        vectors+=1;instructions+=steps;return value
    for byte in range(256):vector(bytes([byte]))
    sizes=(0,1,2,3,4,5,7,15,16,31,32,33,63,64,65,127,128,129,255,256,257,511,512,513,631,632,633,1023,1024,4096,41152)
    for size in sizes:
        data=bytes((i*73+0x53)&255 for i in range(size))
        for alignment in (0,1,2,3,7):
            for previous in (0,0xffffffff,0x12345678):vector(data,alignment,previous)
    split_cases=0
    data=bytes(range(256))*3+b'CE-return-high-bit\x80\xff'
    for split in (0,1,3,127,128,129,255,256,511,len(data)-1,len(data)):
        for previous in (0,0x89abcdef):
            prefix=vector(data[:split],3,previous);actual=vector(data[split:],1,prefix)
            require(actual==crc(data,previous),'Incremental CRC result changed across a split');split_cases+=1
    wrappers=[]
    for name,entry in (('retail',public),('cold',scoped)):
        for mmu,ccr in ((1,1),(1,0x101),(0,0),(0,0x100)):
            vector(data,1,0x12345678,entry,mmu,ccr)
            wrappers.append(dict(entry=name,mmu=mmu,ccr=hex(ccr)))
    mode(1)
    # Changing the actual reached polynomial must fail for either linked
    # implementation. Every byte, seed, length, split and public wrapper
    # above executes without replacing a CRC result in the test decoder.
    address,body,_=symbol_bytes(image,'_kui_cold_load_crc32_leaf',2)
    literal=body.find(struct.pack('<I',0xedb88320));require(literal>=0,'CRC polynomial literal is unowned')
    cpu.put(address+literal,struct.pack('<I',0xedb88321));cpu.put(DATA,b'\0')
    wrong,_,_=invoke(cpu,leaf,[0,DATA,1]);require(wrong!=crc(b'\0'),'Polynomial mutation escaped golden coverage')
    cpu.put(address,body)
    return dict(implementation='bitwise-ieee' if bitwise else 'folded-byte-ieee',
        leaf_address=hex(leaf),leaf_sha256=sha(body),table_symbol=None if bitwise else objects[0],
        table_bytes=0 if bitwise else 512,no_initialized_byte_table=bitwise,
        vectors=vectors,all256_input_bytes=True,all256_initial_indices=not bitwise,byte_alignments=[0,1,2,3,7],
        lengths=list(sizes),incremental_split_cases=split_cases,public_wrappers=wrappers,
        golden_zlib_equal=True,polynomial_mutation_refused=True,
        high_bit_fold_mutation_refused=not bitwise,instructions=instructions)


def audit(build,profile):
    build=Path(build);paths=[build/'ce-return.elf',build/'helper/helper.elf',build/'helper/helper.bin',build/'resident-sci.elf']
    sources=('tests/test_smash_ce_return_integrity.py','tests/test_smash_ce_return_loader.py',
        'tests/test_smash_native_gin_linked.py','tests/sh4_integer_fast.c',
        'src/loader/smash_ce_return_loader.c','src/loader/ce_return_crc32.h',
        'src/loader/cold_load_crc32.c','src/loader/cold_load_scope.S','src/core/smash_ce_return_loader.c',
        'Makefile.smash-ce-return-probe','src/loader/smash_ce_return_loader.ld','src/loader/smash_ce_return_raw_image.h')
    sources=set(sources)
    for module in tuple(sys.modules.values()):
        name=getattr(module,'__file__',None)
        if name:
            path=Path(name).resolve()
            if path.is_relative_to(ROOT)and path.suffix=='.py':sources.add(str(path.relative_to(ROOT)))
    sources=sorted(sources)
    source_before={p:sha((ROOT/p).read_bytes())for p in sources}
    before={str(p):sha(p.read_bytes())for p in paths}
    image,helper=read_elf(paths[0]),read_elf(paths[1]);view=view_audit(paths[1],paths[3],paths[2])
    view['old_e0']=read_elf(paths[3])[4]['_kui_retail_menu_hook'][0]
    with tempfile.TemporaryDirectory(prefix='kui-ce-return-integrity-')as folder:
        run,mode,adaptation=decoder(Path(folder))
        predicates=predicate_cases(image,helper,view,profile,run)
        crypto=crc_cases(image,run,mode)
    require(before=={str(p):sha(p.read_bytes())for p in paths},'Linked inputs changed during integrity audit')
    require(source_before=={p:sha((ROOT/p).read_bytes())for p in sources},'Integrity proof source changed during audit')
    return dict(schema='kui.smash-ce.return-integrity.v1',status='PASS',profile=profile,inputs=before,
        inputs_immutable_during_execution=True,
        test_sha256=sha(Path(__file__).read_bytes()),
        predicate=predicates,shared_crc=crypto,decoder=adaptation,
        source_sha256=source_before,
        limits=['Executes actual linked controller validators and shared CRC with authored ROOT/receipt/video/raw/helper state.',
            'MMUCR and coherent aliased RAM are fixtures; no physical caches, kernel lifetime, scheduler, SD or console-success claim.',
            'CRC-zero fixups isolate nonzero guards only; these deliberately changed code/firmware bytes never execute as a real return.'])


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--build',type=Path,required=True)
    parser.add_argument('--profile',type=int,choices=(1,2),required=True);parser.add_argument('--report',type=Path)
    args=parser.parse_args();result=audit(args.build,args.profile);rendered=json.dumps(result,indent=2)+'\n'
    if args.report:args.report.parent.mkdir(parents=True,exist_ok=True);args.report.write_text(rendered)
    print(rendered,end='')
if __name__=='__main__':main()
