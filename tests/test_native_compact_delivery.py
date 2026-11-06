#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Mutate real compact workers and their embedding chain, not synthetic pins."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT/'tools')]
from check_native_compact_delivery import audit, worker_equivalence, component_equivalence
from check_vmu_native_worker_gateway import read_elf


def run(build, reference, slot):
    result = audit(build, reference=reference, slot=slot, allow_dirty=True)
    worker, original = read_elf(build/'native-worker.elf'), read_elf(reference)
    commit = result['source_provenance']['commit'][:12]
    rejected = 0

    def refuse(image=worker, ref=original, virtual=slot, label=commit):
        nonlocal rejected
        try:
            worker_equivalence(image, ref, virtual, label)
        except (ValueError, AssertionError, KeyError, struct.error):
            rejected += 1
            return
        raise AssertionError('Changed worker/reference/slot was accepted')

    for segment in worker[2]:
        if segment[0] != 1 or not segment[4]:
            continue
        for at in (segment[1], segment[1]+segment[4]-1):
            data = bytearray(worker[0]);data[at] ^= 1
            refuse((bytes(data), *worker[1:]))
    for field in (4, 7):
        header=list(worker[1]);header[field] += 2
        refuse((worker[0],tuple(header),*worker[2:]))
    # Physical geometry matters even when every initialized instruction agrees.
    for index, segment in enumerate(worker[2]):
        if segment[0] != 1:
            continue
        for field in (2, 3, 4, 5, 6):
            segments = list(worker[2]);row=list(segment);row[field] += 4
            segments[index] = tuple(row)
            refuse((worker[0], worker[1], segments, *worker[3:]))
    # Mutate a real BSS owner and a real typed symbol in the ELF symbol table.
    symbol = struct.Struct('<IIIBBH')
    changed_function = changed_object = changed_marker = False
    for table in worker[3]:
        if table[1] != 2:
            continue
        strings = worker[3][table[6]]
        names = worker[0][strings[4]:strings[4]+strings[5]]
        for pos in range(table[4], table[4]+table[5], symbol.size):
            name, address, count, info, other, owner = symbol.unpack_from(worker[0], pos)
            name = names[name:].split(b'\0', 1)[0].decode('ascii')
            if info & 15 == 2 and count and not changed_function:
                data=bytearray(worker[0]);struct.pack_into('<I',data,pos+4,address+2)
                refuse((bytes(data),*worker[1:]));changed_function=True
            if info & 15 == 1 and owner < len(worker[3]) and worker[3][owner][1] == 8 and not changed_object:
                data=bytearray(worker[0]);struct.pack_into('<I',data,pos+8,count+4)
                refuse((bytes(data),*worker[1:]));changed_object=True
            if name == '__kui_native_virtual_slot_worker':
                data=bytearray(worker[0]);data[pos+12] = 0
                refuse((bytes(data),*worker[1:]));changed_marker=True
    assert changed_function and changed_object and changed_marker
    symbols=dict(worker[4]);symbols['__kui_native_virtual_slot_worker']=(3-slot,0,0,0xfff1)
    refuse((*worker[:4],symbols))
    refuse(virtual=3-slot)
    refuse(label='0'*12 if commit != '0'*12 else '1'*12)
    data=bytearray(original[0]);data[-1] ^= 1
    refuse(ref=(bytes(data),*original[1:]))
    # The low image combines code/BSS in a writable load segment, but the
    # display string itself must retain its exact read-only section owner.
    low=read_elf(build/'resident-sci.elf');needle=b'BUILD '+commit.encode()+b'\0'
    assert low[0].count(needle)==1
    replacement='0'*12 if commit!='0'*12 else '1'*12
    data=low[0].replace(needle,b'BUILD '+replacement.encode()+b'\0')
    altered=(data,*low[1:]);label_at=low[0].index(needle)
    parameters=dict(reference_sha256=hashlib.sha256(low[0]).hexdigest(),old_build=commit,new_build=replacement)
    component_equivalence(altered,low,**parameters)
    generic_rejections=0
    for owner in (False,True):
        if owner:
            sections=list(low[3]);index=next(i for i,s in enumerate(sections) if s[1]==1 and s[2]&3==2 and s[4]<=label_at<label_at+len(needle)<=s[4]+s[5])
            section=list(sections[index]);section[2]|=1;sections[index]=tuple(section)
            mutant=(data,low[1],low[2],sections,low[4])
        else:
            changed=bytearray(data);segment=next(p for p in low[2] if p[0]==1 and p[4]);changed[segment[1]+5]^=1
            mutant=(bytes(changed),*low[1:])
        try:component_equivalence(mutant,low,**parameters)
        except ValueError:generic_rejections+=1
        else:raise AssertionError('Low read-only owner/code mutation accepted')
    # A coherently copied directory cannot substitute a bin which its actual
    # entry/stage ELF does not contain. All other files remain the real build.
    embedding_rejections=0
    for name in ('stage.bin','native-worker.bin','native-worker.reloc','resident-sci.bin',
                 'resident-legacy.bin','native-ra-aux.bin','native-ra-aux.reloc'):
        with tempfile.TemporaryDirectory(prefix='kui-compact-delivery-mutant-') as tmp:
            directory=Path(tmp)
            for source in build.iterdir():
                if source.is_dir():
                    (directory/source.name).symlink_to(source.resolve(),target_is_directory=True)
                elif source.name != name:
                    (directory/source.name).symlink_to(source.resolve())
            data=bytearray((build/name).read_bytes());data[len(data)//2] ^= 1
            (directory/name).write_bytes(data)
            try:
                audit(directory,reference=reference,slot=slot,allow_dirty=True)
            except (ValueError,AssertionError,KeyError,struct.error):
                embedding_rejections += 1
            else:
                raise AssertionError('Changed embedded artifact accepted: '+name)
    return dict(slot=slot,status='PASS',worker_mutation_rejections=rejected,
        embedding_mutation_rejections=embedding_rejections,generic_low_mutation_rejections=generic_rejections,
        geometry=result['geometry'],full_historical_persistent_audit_executed=False)


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('build-a1','build-a2','reference-a1','reference-a2'):
        parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args()
    print(json.dumps(dict(status='PASS',profiles=[
        run(args.build_a1,args.reference_a1,1),run(args.build_a2,args.reference_a2,2)]),indent=2))
