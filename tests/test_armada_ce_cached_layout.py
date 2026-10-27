#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Mutate an actual split-alias helper ELF and require strict audit refusal."""
import argparse
import json
from pathlib import Path
import struct
import sys

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from check_armada_ce_vmu_a2 import (EH,SH,PH,SYM,HOOK,ROOT,ARRAY_READ,
    read_section_elf,helper_region)
import armada_ce_cache_snapshot_contract as snapshot


def check(path):
    original=Path(path).read_bytes()
    sections,symbols,functions=read_section_elf(original)
    assert '.uncached' in sections,'A split cached/P2 ELF is required'
    header=EH.unpack_from(original);symtab=sections['.symtab']
    strings=next(s for s in sections.values() if s['index']==symtab['link'])
    names=original[strings['offset']:strings['offset']+strings['size']]
    symbol_rows={}
    for at in range(symtab['offset'],symtab['offset']+symtab['size'],SYM.size):
        name,*_=SYM.unpack_from(original,at)
        symbol_rows[names[name:names.find(b'\0',name)].decode()]=at
    results=[]
    def rejected(label,offset,value,fmt='<I'):
        data=bytearray(original);struct.pack_into(fmt,data,offset,value)
        try:read_section_elf(bytes(data))
        except ValueError as error:results.append({'mutation':label,'refusal':str(error)})
        else:raise AssertionError('Accepted malformed cached helper: '+label)
    def section_field(name,field):
        return header[6]+sections[name]['index']*SH.size+field*4
    def file_offset(address):
        for section in sections.values():
            if section['type']==1 and section['address']<=address<section['address']+section['size']:
                return section['offset']+address-section['address']
        raise AssertionError('Not an immutable address')
    def redirected(caller,target,value,label):
        address,size=functions[caller]
        body=helper_region(sections,address,size)
        at=body.index(struct.pack('<I',symbols[target]))
        rejected(label,file_offset(address)+at,value)
    rejected('P2 island moved to P1',section_field('.uncached',3),HOOK-0x20000000)
    rejected('C section moved to P2',section_field('.hook',3),sections['.hook']['address']+0x20000000)
    rejected('P2 island loaded at wrong physical address',header[5]+PH.size+12,HOOK)
    rejected('C segment made writable',header[5]+2*PH.size+24,7)
    rejected('Private segment made executable',header[5]+3*PH.size+24,7)
    rejected('Private workspace moved to P1',section_field('.workspace',3),sections['.workspace']['address']-0x20000000)
    rejected('Private workspace shares immutable cache line',section_field('.workspace',3),sections['.workspace']['address']+4)
    rejected('C function uses P2 alias',symbol_rows['_'+ROOT]+4,symbols['_'+ROOT]+0x20000000)
    rejected('Snapshot function uses P1 alias',symbol_rows[snapshot.LEAF_SYMBOL]+4,symbols[snapshot.LEAF_SYMBOL]-0x20000000)
    rejected('Snapshot job uses P1 alias',symbol_rows[snapshot.JOB_SYMBOL]+4,symbols[snapshot.JOB_SYMBOL]-0x20000000)
    rejected('Snapshot job has wrong size',symbol_rows[snapshot.JOB_SYMBOL]+8,64)
    rejected('Snapshot job is untyped',symbol_rows[snapshot.JOB_SYMBOL]+12,0x10,'<B')
    rejected('Immutable C section made writable',section_field('.hook',2),7)
    rejected('Unexpected allocated debug section',section_field('.debug_info',2),2)
    rejected('P2 island padding contains unpinned bytes',sections['.uncached']['offset']+sections['.uncached']['size']-1,1,'<B')
    rejected('Shim calls P2 alias of C root',file_offset(HOOK)+48,symbols['_'+ROOT]+0x20000000)
    redirected('_traffic_read_cache',ARRAY_READ,symbols[ARRAY_READ]-0x20000000,'Scalar array wrapper calls P1 leaf')
    redirected('_kui_armada_ce_cache_line_native',snapshot.LEAF_SYMBOL,
        symbols[snapshot.LEAF_SYMBOL]-0x20000000,'Bulk wrapper calls P1 snapshot')
    query='_kui_armada_ce_maple_rx_slice_query.constprop.0'
    redirected('_'+ROOT,query,symbols[query]+0x20000000,'Cached C retains uncached query target')
    for name,label in ((snapshot.LEAF_SYMBOL,'Snapshot instruction pin'),
            ('_fence_write_mmio','MMIO write instruction pin'),('_a2_store32','RX store instruction pin')):
        at=file_offset(functions[name][0]);rejected(label,at,original[at]^1,'<B')
    return {'status':'PASS','elf':str(Path(path).resolve()),'malformed_layouts_rejected':len(results),
        'mutations':results,'scope':'Strict ELF alias/layout and complete native pin refusal; no hardware performance proof'}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf',type=Path);parser.add_argument('--json',type=Path)
    args=parser.parse_args();report=check(args.elf)
    if args.json:args.json.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__=='__main__':main()
