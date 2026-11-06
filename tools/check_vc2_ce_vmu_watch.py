#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bind the scoped native write-watch to actual linked geometry and stacks."""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import check_retail_stack as stack
from check_vmu_native_worker_gateway import read_elf
from check_vc2_ce_vmu_package import audit as base_audit,payload_at,require

def audit(build):
    out=base_audit(build)
    resident=read_elf(build/'resident-sci.elf');s=resident[4]
    require(s['__kui_vc2_vmu_watch_resident'][0]==1,'Not a write-watch resident')
    entry,size,kind,_=s['_kui_vc2_ce_vmu_watch_entry']
    require(kind==2 and entry>=0x8c004000 and
            entry+size<=s['__native_dma_cold_begin'][0],'Watch entry is not permanent low code')
    require(payload_at(resident,entry,2)==struct.pack('<H',0x6f03),
            'Watch entry must preserve R0 without touching interrupted SP')
    symbol_header=(build/'vc2_watch_symbols.h').read_text()
    require(f'0x{entry+0x20000000:08x}u' in symbol_header and
            f'0x{s["_manifest"][0]+64+0x20000000:08x}u' in symbol_header,
            'Stale watch installer addresses')
    reports=sorted(build.glob('resident-sci.elf.ltrans*.ci'))
    frames,calls=stack.load(reports)
    bounded=stack.worst(frames,calls,roots_override={
        'kui_vc2_ce_vmu_watch_stop':0,
        'kui_retail_resident_dispatch':0,'kui_retail_menu_return':0})
    require(max(row['bytes'] for row in bounded.values())+64<=336,
            'Watch C path exceeds the unchanged guarded low stack')
    hashes={p:hashlib.sha256((build/p).read_bytes()).hexdigest()
            for p in ('resident-sci.elf','stage.elf','entry.elf')}
    out.update(schema='kui.vc2.native-write-watch.linked.v1',mode='native write watch',
        virtual_card=None,persistent_writes=False,hardware_verified=False,
        watched_addresses=['8cfc4000','acfc4000'],coverage='CPU operand writes while BL=0',
        uncovered=['BL=1 writes','P0 alias','DMA'],entry=hex(entry+0x20000000),
        entry_bytes=size,watch_stack=bounded,artifacts_sha256=hashes)
    return out

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build',type=Path);parser.add_argument('--output',type=Path)
    args=parser.parse_args();report=audit(args.build)
    text=json.dumps(report,indent=2)+'\n'
    if args.output:args.output.write_text(text)
    print(text,end='')
