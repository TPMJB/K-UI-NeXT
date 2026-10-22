#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Mutate compiled explicit layout admission and complete protected bodies."""
import argparse
import json
from pathlib import Path
import struct
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import check_armada_ce_vmu_a2 as audit
from check_armada_ce_vmu_a2_package import check_layout_marker


def refuses(data):
    try:audit.read_section_elf(bytes(data))
    except ValueError:return
    raise AssertionError('Native audit admitted a layout/body mutation')


def mutations(path,write_layout):
    data=path.read_bytes();sections,symbols,functions=audit.read_section_elf(data)
    assert (audit.WRITE_LAYOUT in symbols)==write_layout
    checks=0
    # Entry, bridge helper and private source/store/cache exclusion must agree.
    changed=bytearray(data);struct.pack_into('<I',changed,24,audit.HOOK if write_layout else audit.WRITE_HOOK)
    refuses(changed);checks+=1
    for name in ('_a2_store32','_kui_armada_ce_cache_snapshot'):
        at,size=functions[name]
        section=next(s for s in sections.values() if s['type']==1 and s['address']<=at and at+size<=s['address']+s['size'])
        offset=section['offset']+at-section['address']
        for i in range(size):
            changed=bytearray(data);changed[offset+i]^=1
            refuses(changed);checks+=1
    bridge=sections['.bridge']
    changed=bytearray(data);struct.pack_into('<I',changed,bridge['offset']+80,audit.HOOK if write_layout else audit.WRITE_HOOK)
    refuses(changed);checks+=1
    if write_layout:
        tab=sections['.symtab'];strings=next(s for s in sections.values() if s['index']==tab['link'])
        names=data[strings['offset']:strings['offset']+strings['size']]
        for at in range(tab['offset'],tab['offset']+tab['size'],audit.SYM.size):
            row=list(audit.SYM.unpack_from(data,at));name=names[row[0]:names.find(b'\0',row[0])]
            if name.decode('ascii')!=audit.WRITE_LAYOUT:continue
            for field,value in ((1,0),(1,2),(2,4),(3,1),(5,sections['.hook']['index'])):
                changed=bytearray(data);mutated=row.copy();mutated[field]=value
                audit.SYM.pack_into(changed,at,*mutated);refuses(changed);checks+=1
            break
        else:raise AssertionError('Expanded layout marker missing')
    return checks


def marker_mutations(path,name,write_layout):
    data=path.read_bytes();check_layout_marker(data,name,write_layout)
    def reject(changed,profile):
        try:check_layout_marker(bytes(changed),name,profile)
        except ValueError:return
        raise AssertionError('Accepted mixed stage/resident profile')
    reject(data,not write_layout);checks=1
    if not write_layout:return checks
    header=audit.EH.unpack_from(data)
    sections=[audit.SH.unpack_from(data,header[6]+i*audit.SH.size) for i in range(header[12])]
    table=next(s for s in sections if s[1]==2);strings=sections[table[6]]
    names=data[strings[4]:strings[4]+strings[5]]
    for at in range(table[4],table[4]+table[5],audit.SYM.size):
        row=list(audit.SYM.unpack_from(data,at));label=names[row[0]:names.find(b'\0',row[0])]
        if label.decode('ascii')!=name:continue
        for field,value in ((1,0),(1,2),(2,4),(3,1),(5,1),(0,0)):
            changed=bytearray(data);mutated=row.copy();mutated[field]=value
            audit.SYM.pack_into(changed,at,*mutated);reject(changed,True);checks+=1
        changed=bytearray(data)
        other=table[4]+audit.SYM.size
        if other==at:other+=audit.SYM.size
        audit.SYM.pack_into(changed,other,*row);reject(changed,True);checks+=1
        return checks
    raise AssertionError('Expanded stage/resident marker missing')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--readonly',type=Path,required=True)
    parser.add_argument('--expanded',type=Path,required=True)
    parser.add_argument('--json',type=Path)
    args=parser.parse_args()
    report={'readonly_mutations_rejected':mutations(args.readonly,False),
            'expanded_mutations_rejected':mutations(args.expanded,True),
            'stage_resident_mutations_rejected':sum(marker_mutations(directory/file,name,mode)
                for directory,mode in ((args.readonly.parent,False),(args.expanded.parent,True))
                for file,name in (('stage.elf','__kui_maple_write_layout_stage'),
                    ('resident-scia.elf','__kui_maple_write_layout_resident'))),
            'complete_store_and_snapshot_bodies_pinned':True,
            'mixed_layouts_rejected':True,'scope':'Native admission only; no hardware validation'}
    if args.json:args.json.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__=='__main__':main()
