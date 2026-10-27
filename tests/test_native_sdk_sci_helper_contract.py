#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the original SDK default helper's complete SCI return path.

These externally supplied originals have different debug-print pointers.
Only the proved SCI path's exact instruction/constant windows are needed:
both SCI events skip every handled-debug-event body and all external calls.
No retail executable bytes are embedded or emitted by this test.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tests'))
from test_native_sdk_sci_owner_contract import OwnerCPU,owner_contract,require

WINDOWS=((0,0x34,'56fb7aca2af823e85502ad444fd1df4130a93b58536037fd856605b7b35c1423'),
         (0x122,8,'3d1afae2d76bdfe828d53371594cc0fa539920fde560ebccd3f00a6472dfbffb'),
         (0x224,20,'fe0a08ca04e79c38dc469e8221aa20f3991dbdb49d8f3df7b78b115392c05940'))


def helper_contract(image,load,default):
    def at(address,count):
        require(address&0xe0000000 in (0,0x80000000),'default helper uses a forbidden owner alias')
        offset=(address&0x1fffffff)-(load&0x1fffffff)
        require(offset>=0 and offset<=len(image)-count,'default helper role outside original image')
        return offset
    at(default,0xf8)
    call=default+0x40
    require(struct.unpack_from('<HHH',image,at(call,6))==(0xd02c,0x400b,0x0009),
            'SDK default helper call role changed')
    literal=((call+4)&~3)+4*0x2c
    require(literal==default+0xf4,'SDK default helper literal is not its named field')
    helper=struct.unpack_from('<I',image,at(literal,4))[0]
    require(not helper&3 and helper&0xe0000000==default&0xe0000000,
            'SDK default helper target has an unaligned or mixed owner alias')
    offset=at(helper,0x238)
    for offset,count,digest in WINDOWS:
        start=at(helper+offset,count)
        require(hashlib.sha256(image[start:start+count]).hexdigest()==digest,
                'SDK default SCI helper path changed at '+hex(offset))
    return helper


class HelperCPU(OwnerCPU):
    def __init__(self,image,helper,seed,mask):
        super().__init__(image,seed,mask)
        self.helper=helper;self.stack=self.r[15];self.read_count=0

    def read(self,address,count=4):
        require(self.stack-44<=address and address+count<=self.stack or
                any(self.helper+offset<=address and address+count<=self.helper+offset+length
                    for offset,length,_ in WINDOWS),
                'SCI helper read external code, data or MMIO at '+hex(address))
        self.read_count+=1
        return super().read(address,count)


def helper_cases(owner):
    image=owner['image'];helper=helper_contract(image,owner['load'],owner['default'])
    cases=0
    for mask in range(16):
        for event in (0x4e0,0x500):
            c=HelperCPU(image,helper,cases+1,mask)
            c.r[4]=event;c.pr=0x8c00e000
            saved=(c.r[8:16],c.pr,c.sr&~1)
            c.run_to(helper,c.pr)
            require((c.r[8:16],c.pr,c.sr&~1)==saved,'SCI helper changed callee state or caller mask')
            require(c.r[4]==event and len(c.writes)==9 and
                    all(c.stack-44<=address and address+count<=c.stack
                        for _,address,_,count in c.writes),
                    'SCI helper changed event or wrote outside its44-byte own frame')
            require(c.read_count>0,'SCI helper did not execute')
            cases+=1
    return dict(game=owner['game'],helper=hex(helper),actual_sci_return_cases=cases,
        events=['0x4e0','0x500'],masks=list(range(16)),maximum_own_frame_bytes=44,
        external_code_data_mmio_reads=0,writes_outside_own_frame=0,
        unrelated_debug_print_paths_executed=False)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('startups',nargs='+',type=Path)
    a=p.parse_args()
    try:owners=[helper_cases(owner_contract(path)) for path in a.startups]
    except (OSError,ValueError,AssertionError,KeyError,struct.error) as e:
        p.exit(2,'Original SDK SCI helper contract refused: '+str(e)+'\n')
    print(json.dumps(dict(schema='kui.native.sdk-sci.helper-contract.v1',owners=owners),indent=2))


if __name__=='__main__':main()
