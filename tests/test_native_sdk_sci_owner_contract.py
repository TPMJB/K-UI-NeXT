#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check original externally supplied Evolution SDK callback contracts.

No retail code bytes are embedded. Exact identities and compact fingerprints
bind the reviewed original windows. The actual table initializer and five-word
VBR/GBR/PR/SPC/SSR save/restore pairs execute in the bounded integer model.
The full SDK FPU frame and interrupt scheduling remain outside this test;
the linked SDK callback auditor separately replays each owner's dispatcher.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tests'),str(ROOT/'tools')]
from test_vmu_native_heap_execution import SH,MASK,signed
from test_vmu_native_sci_sdk_restore import InterruptCPU

LOAD=0x8c010000
VBR=0x8c00f400
OWNERS=(
 dict(game='evolution1',source_bytes=2143652,program_bytes=2143652,
      source_sha256='ffb5a8e5ff5547026d93649e1f9058b7ee5e29d8d0ff8a89335701f7e9647f35',
      program_sha256='ffb5a8e5ff5547026d93649e1f9058b7ee5e29d8d0ff8a89335701f7e9647f35',
      vector=0x8c1da080,dispatch=0x8c1da31a,event=0x8c1da418,continuation=0x8c1da348,
      default=0x8c1d9f50,table_init=0x8c1b0e56,table_stop=0x8c1b0e74,
      common=(0x8c1da274,0x8c1da10c),
      frame_pairs=((0x8c1da29a,0x8c1da3d0),(0x8c1da10e,0x8c1da23a)),
      windows=((0x8c1da080,96,'d9b22252ca3d8af7b551404cd462563b81c7fed00aa4a42a09a1d1fbdacb4c62'),
               (0x8c1da274,0x19c,'de67585ea70930606216eebbd192d5622560b58f13e9aae431c02decfc183a5d'),
               (0x8c1da10c,0x168,'cb4f52117bedbff2c75034020ff3b7e8b2ee650cb3e8fd82a3e45c142f5759c3'),
               (0x8c1d9f50,0xf8,'040fbacbd7384f73aeeb7b43cd7bf5415551e30a107e4e731eddc8eb06926b93'),
               (0x8c1b0e56,0x2e,'9a604563d26d59bdab4938f54b690b6b9656a9a53f82457c51c6a15790bfcff3'))),
 dict(game='evolution2',source_bytes=4057056,program_bytes=4024288,
      source_sha256='16b45fcc84992a60d5a64bf64bf22b699b27306b95f177ba3d9730ca380069ae',
      program_sha256='52e00d2b3e9db00016b1f6d782cf8a2141617f3d00bd628200de757fd2e11605',
      vector=0x8c1ade3c,dispatch=0x8c1ae0d8,event=0x8c1ae1d4,continuation=0x8c1ae104,
      default=0x8c1add0c,table_init=0x8c17e7c2,table_stop=0x8c17e7e0,
      common=(0x8c1ae030,0x8c1adec8),
      frame_pairs=((0x8c1ae056,0x8c1ae18c),(0x8c1adeca,0x8c1adff6)),
      windows=((0x8c1ade3c,96,'dcda3daf9a295c624f6f18e71c4960962cbf7d79ac8cfab49f1fcad53a046c58'),
               (0x8c1ae030,0x19c,'a65e4ed8f8fdae94a9afe30b38759b51f3292b0a3bef12a9512a2a3247dfe98a'),
               (0x8c1adec8,0x168,'cc73775f1bf292bd1d710db96250842a6b5a6de68eb068b5aa523c216e67e435'),
               (0x8c1add0c,0xf8,'219ac536df2bd50903322875180c7b0495d0c8bb39b75f053ebf7d85f54a01ec'),
               (0x8c17e7c2,0x2e,'968d010927af0d49041c5ae7317dc77e6042fa620f28efbc99184f6125ec3f92'))),
)


def require(ok,message):
    if not ok:raise ValueError(message)


def owner_contract(path):
    """Return exact image plus reviewed per-owner dispatcher/ABI metadata."""
    source=Path(path).read_bytes();digest=hashlib.sha256(source).hexdigest()
    matches=[o for o in OWNERS if len(source)==o['source_bytes'] and digest==o['source_sha256']]
    require(len(matches)==1,'startup differs from both reviewed original owners')
    result=dict(matches[0]);image=source[:result['program_bytes']]
    require(hashlib.sha256(image).hexdigest()==result['program_sha256'],'normalized owner identity differs')
    for address,count,expected in result['windows']:
        body=image[address-LOAD:address-LOAD+count]
        require(len(body)==count and hashlib.sha256(body).hexdigest()==expected,
                'original owner contract window changed at '+hex(address))
    result.update(image=image,vbr=VBR,eri_slot=VBR+0x25c,rxi_slot=VBR+0x260,
                  digest=result['program_sha256'],load=LOAD)
    # Each reviewed dispatcher passes INTEVT in R4 and invokes JSR @R14;
    # the low callback therefore returns via RTS to the original continuation.
    call=result['continuation']-4
    require(struct.unpack_from('<HH',image,call-LOAD)==(0x4e0b,0x0009),
            'original owner callback is no longer the reviewed delayed JSR')
    for save,restore in result['frame_pairs']:
        require(struct.unpack_from('<5H',image,save-LOAD)==(0x4f33,0x4f43,0x4f22,0x4f13,0x4f23) and
                struct.unpack_from('<5H',image,restore-LOAD)==(0x4f27,0x4f17,0x4f26,0x4f47,0x4f37),
                'original SDK saved control-frame ABI changed')
    return result


class OwnerCPU(InterruptCPU):
    """Original scalar initializer/control instructions, without C stubs."""
    def __init__(self,image,seed=0,mask=15):
        SH.__init__(self,image)
        self.r[15]=0x8c00ed00
        self.sr=0x60000100|(mask<<4)|(seed&1)
        self.control=dict(vbr=VBR,ssr=(0x61234567^seed)&MASK,spc=(0x8c123450+2*seed)&MASK,
                          sgr=0x8c00ed00,dbr=0x8c223344)
        self.gbr=0x8c445566;self.pr=0x8c778898
        self.event=0;self.iprb=0x5a0f;self.targets=set()

    def plain(self,op,pc):
        n=op>>8&15
        if op&0xf0ff in (0x4013,0x4023,0x4033,0x4043):
            self.r[n]=(self.r[n]-4)&MASK
            key={0x4013:'gbr',0x4023:'vbr',0x4033:'ssr',0x4043:'spc'}[op&0xf0ff]
            self.write(self.r[n],self.gbr if key=='gbr' else self.control[key]);return
        if op&0xf0ff in (0x4017,0x4027,0x4037,0x4047):
            value=self.read(self.r[n]);self.r[n]=(self.r[n]+4)&MASK
            key={0x4017:'gbr',0x4027:'vbr',0x4037:'ssr',0x4047:'spc'}[op&0xf0ff]
            if key=='gbr':self.gbr=value
            else:self.control[key]=value
            return
        if op&0xf00f==0x3003:
            self.flag(signed(self.r[op>>8&15],32)>=signed(self.r[op>>4&15],32));return
        super().plain(op,pc)


def contract_cases(owner):
    # The initializer counter is already INTEVT-0x200. The dispatcher
    # subtracts0x200 itself before using this identical table formula.
    tables=frames=0;expected=[VBR+0x200+delta//8 for delta in range(-0x1c0,0x601,0x20)]
    for mask in range(16):
        c=OwnerCPU(owner['image'],mask,mask);before_vbr=c.control['vbr']
        c.run_to(owner['table_init'],owner['table_stop'])
        require([(address,value,count) for _,address,value,count in c.writes]==
                [(address,owner['default'],4) for address in expected],
                'original callback initializer writes outside its exact slots')
        require(c.read(owner['eri_slot'])==c.read(owner['rxi_slot'])==owner['default'] and
                c.control['vbr']==before_vbr and c.sr&0xf0==mask<<4,
                'original callback initializer loses owner/mask/default binding')
        tables+=1
        for save,restore in owner['frame_pairs']:
            c=OwnerCPU(owner['image'],mask,mask)
            before=(c.r[15],c.pr,c.gbr,c.control.copy())
            for pc in range(save,save+10,2):c.plain(c.read(pc,2),pc)
            require(c.r[15]==before[0]-20,'SDK control frame width differs')
            c.pr^=MASK;c.gbr^=MASK
            for key in ('vbr','spc','ssr'):c.control[key]^=MASK
            for pc in range(restore,restore+10,2):c.plain(c.read(pc,2),pc)
            require((c.r[15],c.pr,c.gbr,c.control)==before,
                    'original SDK control frame fails its exact stack round-trip')
            frames+=1
    return dict(game=owner['game'],source_bytes=owner['source_bytes'],program_bytes=owner['program_bytes'],
        source_sha256=owner['source_sha256'],program_sha256=owner['program_sha256'],
        actual_table_initializer_cases=tables,actual_control_frame_roundtrip_cases=frames,
        callback_argument_register='r4',callback_return='RTS to original JSR continuation',
        owner_vbr=hex(VBR),eri_slot=hex(owner['eri_slot']),rxi_slot=hex(owner['rxi_slot']),
        default=hex(owner['default']),dispatch=hex(owner['dispatch']),event=hex(owner['event']),
        continuation=hex(owner['continuation']),full_fpu_prologue_modelled=False,
        console_execution_verified=False)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--evolution1',type=Path);p.add_argument('--evolution2',type=Path)
    a=p.parse_args();paths=[v for v in (a.evolution1,a.evolution2) if v]
    if not paths:p.error('supply at least one exact externally owned startup')
    try:result=[contract_cases(owner_contract(path)) for path in paths]
    except (OSError,ValueError,AssertionError,KeyError,struct.error) as e:
        p.exit(2,'Original SDK owner contract refused: '+str(e)+'\n')
    print(json.dumps(dict(schema='kui.native.sdk-sci.owner-contract.v1',owners=result),indent=2))


if __name__=='__main__':main()
