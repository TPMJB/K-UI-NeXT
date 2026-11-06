#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the actual linked GUI CRC entry points and bind their byte tables."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
import test_smash_native_fast_crc as crc
from check_loader_layout import SYM,region

def sha(data):return hashlib.sha256(data).hexdigest()

def audit(runtime):
    original_read,original_table=crc.read_elf,crc.linked_byte_table
    crc.read_elf=lambda path:original_read(path,allow_tls=True)
    tail_bindings=[]
    def table(image,body):
        # KOS GCC can retain a local CRC function and emit this exact public
        # tail-call veneer. Still execute from the public entry, including
        # its real branch, and authenticate the reached leaf's entire table.
        if len(body)==12 and struct.unpack('<4H',body[:8])==(0xd001,0x402b,0x0009,0x0009):
            target=struct.unpack('<I',body[8:])[0];leaves=[]
            for symbols in image[3]:
                if symbols[1]!=2:continue
                for at in range(symbols[4],symbols[4]+symbols[5],SYM.size):
                    _,address,size,info,_,owner=SYM.unpack_from(image[0],at)
                    if address!=target or info&15!=2 or not size or owner>=len(image[3]):continue
                    section=image[3][owner]
                    if section[2]&6!=6:raise ValueError('GUI CRC tail target is not executable')
                    leaves.append(region(image[0],section[4]+address-section[3],size,'GUI CRC leaf'))
            if len(leaves)!=1:raise ValueError('GUI CRC veneer has no unique bounded linked leaf')
            tail_bindings.append(dict(public_body_sha256=sha(body),target=hex(target),leaf_sha256=sha(leaves[0])))
            return original_table(image,leaves[0])
        return original_table(image,body)
    crc.linked_byte_table=table
    try:
        reports={name:crc.audit(runtime,helper=name,relay=None) for name in ('_kui_crc32','_kui_retail_crc32')}
    finally:
        crc.read_elf,crc.linked_byte_table=original_read,original_table
    inputs=('tools/check_gui_launch_crc.py','tests/test_smash_native_fast_crc.py',
        'src/core/data.c','src/core/retail_image.c','src/loader/stage_fast_crc32.h','Makefile.dc')
    commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    dirty=bool(subprocess.check_output(['git','status','--porcelain'],cwd=ROOT).strip())
    return dict(schema='kui.gui-launch-crc.v1',status='PASS',source_commit=commit,source_dirty=dirty,
        runtime_elf_sha256=sha(runtime.read_bytes()),reports=reports,tail_bindings=tail_bindings,
        actual_SH_cases=sum(x['actual_SH_cases'] for x in reports.values()),
        source_sha256={p:sha((ROOT/p).read_bytes()) for p in inputs},
        applies_to='GUI mounted verification only; installed worker and reader bytes are preserved')

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime',type=Path,required=True);parser.add_argument('--report',type=Path,required=True)
    args=parser.parse_args();result=audit(args.runtime);args.report.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(dict(status='PASS',report=str(args.report),actual_SH_cases=result['actual_SH_cases'])))
