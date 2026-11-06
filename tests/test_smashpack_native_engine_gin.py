#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Replay the supplied Gin copy using the existing scalar SH interpreter.

This establishes copy/argument ownership, not native SDK or game execution.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
from check_vmu_native_worker_gateway import read_elf
from test_native_standalone_foreground_gin import Gin,GIN_STAGE,GIN_ENTRY,COPY_STORE,LOAD,TARGET

OWNERS={'IP.BIN':'33c9cf893ead0b7bd001f85d4ee1197c35ef7eeef4093fed3a5877eebcb2246e',
        'GINXFER.BIN':'fa12196993a7c757a78565963e257cd33443f80b3f226729fdc1cf7da51ed5b3',
        'MDE_US.BIN':'04f49d583b39bdc710cec6d0a434b5d710b9bccedd539bedda29755135f0fe61',
        '1ST_READ.BIN':'3a311b189a4870ccbefc4fcd306e2d38e5ae810ae5edd225af6b9aa6d5df5f10'}
def sha(data):return hashlib.sha256(data).hexdigest()
def overlaps(a,b,c,d):return a<d and c<b

def boot2_contract(ip):
    """Check the actual owner bootstrap's final straight-line entry contract.

    This is source decoding, not a model of its preceding BIOS/cache calls.
    Bootstrap executes via P2, so its JSR return address uses that alias.
    """
    word=lambda pc:struct.unpack_from('<H',ip,pc-0x8c008000)[0]
    def literal(pc,register):
        op=word(pc);assert op>>12==13 and op>>8&15==register
        address=((pc+4)&~3)+(op&255)*4
        return struct.unpack_from('<I',ip,address-0x8c008000)[0]
    assert literal(0x8c00e07c,0)==0x700000f0 and word(0x8c00e07e)==0x400e
    assert word(0x8c00e080)==0x3008
    assert all(word(0x8c00e080+2*n)==(0x6003|(n<<8)) for n in range(1,15))
    assert literal(0x8c00e09e,0)==0x8c00f400 and word(0x8c00e0a0)==0x6f03
    assert literal(0x8c00e0a2,0)==0x8c00f400 and word(0x8c00e0a4)==0x402e
    assert literal(0x8c00e0a6,0)==0x00040000 and word(0x8c00e0a8)==0x406a
    assert literal(0x8c00e0aa,0)==0xac00e004
    assert [word(pc) for pc in (0x8c00e0ac,0x8c00e0ae,0x8c00e0b0)]==[0x6002,0x400b,0x6013]
    return dict(source_final_span=['0x8c00e07c','0x8c00e0b2'],
                sp='0x8c00f400',vbr='0x8c00f400',sr='0x700000f0',pr='0xac00e0b2',
                stage_values_preserved_by_boot2={'MACL':'0x00000000','MACH':'0x00000000','GBR':'0x8c000000'},
                r0_through_r14_zero=True,frame_words=21,frame='0x8c00f3ac',
                frame_order=['MACL','MACH','GBR','VBR','SR','PR']+
                            ['R'+str(n) for n in range(14,-1,-1)],
                explicit_cold_frame_initialization={'0':'0x00000028','5':'0x00000000',
                                   '18':'0x00000800','19':'0x00000800','20':'0x8c0100a0'},
                gin_banked_register_limit='GIN clears RB before writing R0..R7, then RTE restores RB1. The existing scalar model does not prove hardware post-RTE banked R0..R7 values. Cold R0..R2 initialization is safe because the original MDE entry/CRT overwrites them before use.',
                untouched=['FPSCR','CCR'])

def save_port_contract(app):
    """Decode the bound engine's save-unit and transmit receiver tables."""
    word=lambda pc:struct.unpack_from('<H',app,pc-LOAD)[0]
    longword=lambda pc:struct.unpack_from('<I',app,pc-LOAD)[0]
    assert word(0x8c0187e6)==0x430b and word(0x8c0187e8)==0xe400
    assert longword(0x8c0188b0)==0x8c01bd20
    assert word(0x8c01883c)==word(0x8c018846)==0xe400
    assert word(0x8c1cd902)==0x6d43 and word(0x8c1cd906)==0x4d08
    assert word(0x8c1cd90a)==0x0dde and longword(0x8c1cd9a4)==0x8c1d0f4c
    units=struct.unpack_from('<8I',app,0x8c1d0f4c-LOAD)
    assert units==(1,2,7,8,13,14,19,20)
    assert longword(0x8c1cc9ac)==0x8c1d0f1c and word(0x8c1cc8b8)==0x039c
    assert word(0x8c1cc8be)==0x2230
    receivers=app[0x8c1d0f1c-LOAD:0x8c1d0f1c-LOAD+24]
    assert list(receivers)==[0x20,1,2,4,8,0x10,0x60,0x41,0x42,0x44,0x48,0x50,
                              0xa0,0x81,0x82,0x84,0x88,0x90,0xe0,0xc1,0xc2,0xc4,0xc8,0xd0]
    assert receivers[units[0]]==1 and receivers[units[1]]==2
    return dict(method='SHA-bound static instruction and table decoding',
                save_call='0x8c0187e6',unit_zero_delay_slot='0x8c0187e8',
                save_target='0x8c01bd20',unit_table='0x8c1d0f4c',
                unit_table_consumer=['0x8c1cd902','0x8c1cd90a'],
                receiver_table='0x8c1d0f1c',transmit_consumer='0x8c1cc8b8',
                native_save={'bup_unit':0,'sdk_device':1,'maple_receiver':'0x01','slot':'A1'},
                next_unit={'bup_unit':1,'sdk_device':2,'maple_receiver':'0x02','slot':'A2'})

def low(path):
    data,_,segments,_,symbols=read_elf(path)
    assert symbols['__kui_native_standalone_foreground_resident']==(1,0,0,0xfff1)
    address=lambda name:symbols[name][0]
    assert address('_start')==0x8c004000
    begin,end=address('__retail_resident_bss_begin'),address('__retail_resident_bss_end')
    bottom,top=address('__retail_hook_stack_bottom'),address('__retail_hook_stack')
    assert 0x8c004000<address('__retail_resident_binary_end')==begin<=end<=bottom<top<=0x8c008000
    assert top-bottom==352 and symbols['_manifest'][1]==828
    assert begin<=address('_manifest')<address('_manifest')+828<=end
    region,size=address('_standalone_reader_region'),address('__native_dma_region_bytes')
    assert size==1600 and 0x8c004000<=region<region+size<=address('_manifest')
    loads=[p for p in segments if p[0]==1]
    assert len(loads)==2
    assert loads[0][2:4]==(0x8c004000,0x8c004000)
    assert loads[0][2]+loads[0][4]==begin and loads[0][2]+loads[0][5]==end
    assert loads[1][2:4]==(0xac008100,0x8c008100) and 0<loads[1][4]==loads[1][5]<=512
    return [(p[3],data[p[1]:p[1]+p[4]]) for p in loads],top,sha(data)

def run(gin,app,name,loads,top,tokens=None):
    cpu=Gin(gin,app,b'')
    ranges=[(0x8c003fe0,top+32),(0x8c0080e0,0x8c008100+len(loads[1][1])+32),
            (0x8c0000bc,0x8c0000c4),(0x8c000100,0x8c000160)]
    for first,last in ranges:cpu.store_block(first,bytes((i*17+0x53)&255 for i in range(last-first)))
    for address,data in loads:cpu.store_block(address,data)
    cpu.before=[(first,cpu.block(first,last-first)) for first,last in ranges]
    context=0x18ee60 if name=='MDE_US.BIN' else 0x26a44
    header=bytearray(app[context:context+8192]);assert len(header)==8192
    assert struct.unpack_from('<4I',header)==(0x47494e53,1,0x3ac59e76,8192)
    struct.pack_into('<4I',header,16,cpu.source,cpu.sectors,4,1)
    if tokens:
        vector=b'\0'.join(x.encode('ascii') for x in tokens)+b'\0\0'
        assert len(vector)<=256;header[256:512]=vector.ljust(256,b'\0')
    cpu.store_block(GIN_STAGE,header)
    for _ in range(6000000):
        if cpu.pc==TARGET:break
        cpu.step()
    assert cpu.pc==cpu.spc==TARGET and cpu.sr==cpu.ssr==0x700000f0
    # Gin deliberately uses an existing single-bank scalar interpreter. These
    # assertions describe its source-written values; RTE's RB transition does
    # not establish the console's post-RTE banked R0..R7 values in this model.
    assert cpu.r==[0x8c0100a0,0x800,0x800]+[0]*12+[0x8c00f400]
    assert cpu.vbr==0x8c00f400 and cpu.gbr==0x8c000000
    assert cpu.pr==cpu.mach==0 and cpu.macl==40
    restart=(list(cpu.r),cpu.pr,cpu.sr,cpu.gbr,cpu.vbr,cpu.mach,cpu.macl)
    assert cpu.copy_checkpoint[:len(app)]==app and not cpu.mmio_reads and not cpu.mmio_writes
    copies=[(a,n) for pc,a,_,n in cpu.writes if pc==COPY_STORE]
    assert len(copies)==cpu.sectors*512 and all(n==4 for _,n in copies)
    assert min(a for a,_ in copies)==LOAD and max(a+n for a,n in copies)==LOAD+cpu.sectors*2048
    assert cpu.block(LOAD+context,8192)==header
    current=cpu.block(LOAD,len(app))
    assert current[:context]==app[:context] and current[context+8192:]==app[context+8192:]
    for first,before in cpu.before:
        lo,hi=first&0x1fffffff,(first+len(before))&0x1fffffff
        assert not any(overlaps(a&0x1fffffff,(a&0x1fffffff)+n,lo,hi) for _,a,_,n in cpu.writes)
        assert cpu.block(first,len(before))==before
    if tokens:
        def call(entry,index=0):
            cpu.pc=entry;cpu.pr=0x8c003000;cpu.r[4]=index
            for _ in range(5000):
                if cpu.pc==cpu.pr:return cpu.r[0]
                cpu.step()
            raise AssertionError('original Ginsu accessor/setter exceeded bound')
        # Actual Ginsu delivers the folder through the low IP bootstrap
        # contract; the MDE GINS+0xc0 getter is not its filesystem consumer.
        for folder in ['SHFORCE ','PHSTAR2 ']:
            tail=cpu.block(0x8c0080fe,2)
            cpu.store_block(0x8cfa0000,folder.encode()+b'\0')
            assert call(0x8c1ad2c8,0x8cfa0000)==0
            assert call(0x8c1ad31a,4)==0
            assert cpu.block(0x8c0080f0,16)==folder.encode().ljust(12,b' ')+bytes([1,4])+tail
            # Execute only the original read/branch/copy span, ending before
            # the directory resolver or any other SDK helper is invoked.
            cpu.pc=0x8c1a77d2;cpu.r[0]=0x12345678;cpu.r[3]=0x8c600000;cpu.r[13]=0
            for _ in range(5000):
                if cpu.pc==0x8c1a7812:break
                cpu.step()
            assert cpu.pc==0x8c1a7812
            assert cpu.read(0x8c5f95e0,4)==0x8c5f95c8
            assert cpu.read(0x8c5f95c8,2)==1 and cpu.read(0x8c5f95ca,2)==4
            trimmed=folder.rstrip(' ').encode()
            assert cpu.block(0x8c5f95d0,len(trimmed)+1)==trimmed+b'\0'
        # Return to the selected Shining Force setup for the CRT/argv check.
        cpu.store_block(0x8cfa0000,b'SHFORCE\0');assert call(0x8c1ad2c8,0x8cfa0000)==0
        assert call(0x8c1ad31a,4)==0
        prepared_low=[(first,cpu.block(first,len(before))) for first,before in cpu.before]
        # The accessors above are independent source probes. Restore actual
        # Gin's modeled scalar entry state before the original CRT replay.
        (cpu.r,cpu.pr,cpu.sr,cpu.gbr,cpu.vbr,cpu.mach,cpu.macl)=restart
        # Execute the supplied scalar CRT only through its initialization
        # return, before the first unrelated SDK constructor/helper call.
        cpu.pc=0x8c1a0e60;cpu.writes=[]
        for _ in range(12000000):
            if cpu.pc==0x8c1a0e7a:break
            cpu.step()
        assert cpu.pc==0x8c1a0e7a and cpu.block(LOAD+context,8192)==header
        for first,before in prepared_low:
            assert cpu.block(first,len(before))==before
        assert call(0x8c159dde)==5
        for i,text in enumerate(tokens):
            pointer=call(0x8c159e28,i)
            assert cpu.block(pointer,len(text)+1)==text.encode()+b'\0'
        assert call(0x8c159e28,5)==0
    return dict(file=name,bytes=len(app),sectors=cpu.sectors,source=hex(cpu.source),
                copy_end=hex(LOAD+cpu.sectors*2048),gin_context=hex(LOAD+context),
                protected_bytes=sum(len(before) for _,before in cpu.before),arguments=tokens,
                gin_single_bank_scalar_model_entry_contract=True,
                original_scalar_crt_preserves_context=bool(tokens),
                original_ip_folder_setter_and_gdfs_consumer=['SHFORCE','PHSTAR2'] if tokens else [])

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--resident',type=Path,required=True);p.add_argument('--originals',type=Path,required=True)
    p.add_argument('--report',type=Path);args=p.parse_args()
    originals={name:(args.originals/name).read_bytes() for name in OWNERS}
    assert all(sha(originals[name])==digest for name,digest in OWNERS.items())
    loads,top,resident_sha=low(args.resident)
    cases=[run(originals['GINXFER.BIN'],originals['MDE_US.BIN'],'MDE_US.BIN',loads,top,
               ['\\MDE_US.BIN','SHFORCE.SGA','MODE0','SKIP0','SOUND1']),
           run(originals['GINXFER.BIN'],originals['1ST_READ.BIN'],'1ST_READ.BIN',loads,top)]
    begin=int(cases[0]['source'],16);end=begin+cases[0]['sectors']*2048
    assert (begin,end)==(0x8ce00000,0x8cfc3800)
    assert begin<=0x8cfaf000<end and not overlaps(begin,end,0x8cbaefe0,0x8cbaefe0+290816)
    assert all(LOAD<=site<int(cases[0]['copy_end'],16) for site in [0x8c041330,0x8c040df4,0x8c03eec2])
    report=dict(schema='kui.smashpack.actual-gin-copy.v1',passed=True,resident_sha256=resident_sha,cases=cases,
                owner_boot2_source_contract=boot2_contract(originals['IP.BIN']),
                native_save_port_source_contract=save_port_contract(originals['MDE_US.BIN']),
                limits=['CPU copy, original scalar CRT, argument getters and IP-folder consumer only; no subsequent SDK helpers, directory resolution, media access, gameplay or VMU compatibility claim.',
                        'The existing scalar interpreter does not model register-bank switching; post-RTE hardware R0..R7 values are not established.'])
    text=json.dumps(report,indent=2)+'\n'
    if args.report:args.report.parent.mkdir(parents=True,exist_ok=True);args.report.write_text(text)
    print(text,end='')
if __name__=='__main__':main()
