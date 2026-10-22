#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Mutate the real linked opt-in runtime's critical native admission surfaces."""
import argparse
import json
from pathlib import Path
import struct
import sys
import zlib

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import check_armada_ce_vmu_a2 as audit
from check_armada_ce_vmu_a2_package import check_layout_marker,check_runtime_contract,check_runtime_stage_addresses
from check_loader_layout import inspect_elf
from check_retail_loader_layout import exact_profile_marker


def refuse(data):
    try:audit.read_section_elf(bytes(data))
    except ValueError:return
    raise AssertionError('Runtime native audit admitted a critical mutation')


def symbol_row(data,sections,name):
    tab=sections['.symtab'];strings=next(s for s in sections.values() if s['index']==tab['link'])
    names=data[strings['offset']:strings['offset']+strings['size']]
    for at in range(tab['offset'],tab['offset']+tab['size'],audit.SYM.size):
        row=list(audit.SYM.unpack_from(data,at))
        if names[row[0]:names.find(b'\0',row[0])].decode('ascii')==name:return at,row
    raise AssertionError('Missing linked symbol '+name)


def helper_mutations(path):
    data=path.read_bytes();sections,symbols,functions=audit.read_section_elf(data)
    assert audit.WRITE_RUNTIME in symbols and audit.WRITE_LAYOUT in symbols
    checks=0
    protected=[(sections['.uncached']['offset'],audit.RUNTIME_SHIM_BYTES)]
    for name in ('_a2_backend_call','_a2_store32','_a2_held','_kui_armada_ce_maple_runtime_owner',
            '_kui_armada_ce_maple_runtime_wait',
            audit.RUNTIME_NATIVE_WAIT,'_kui_armada_ce_vmu_sd_pio_bus','_armada_sd_pio_write'):
        start,size=functions[name]
        s=next(s for s in sections.values() if s['type']==1 and s['address']<=start and start+size<=s['address']+s['size'])
        protected.append((s['offset']+start-s['address'],size))
    root=functions['_'+audit.ROOT][0]
    for start,size in [(symbols[audit.RUNTIME_HELD_TABLE],len(audit.RUNTIME_HELD_TABLE_BYTES)),
            *((root+offset,len(body)) for offset,body in audit.RUNTIME_HELD_WINDOWS),
            *((root+offset,4) for offset,_ in audit.RUNTIME_HELD_RUN_LITERALS)]:
        s=next(s for s in sections.values() if s['type']==1 and s['address']<=start and start+size<=s['address']+s['size'])
        protected.append((s['offset']+start-s['address'],size))
    for offset,size in protected:
        for byte in range(size):
            changed=bytearray(data);changed[offset+byte]^=1;refuse(changed);checks+=1
    for name in (audit.WRITE_RUNTIME,audit.WRITE_LAYOUT):
        at,row=symbol_row(data,sections,name)
        for field,value in ((1,0),(1,2),(2,4),(3,1),(5,sections['.hook']['index']),(0,0)):
            changed=bytearray(data);r=row.copy();r[field]=value
            audit.SYM.pack_into(changed,at,*r);refuse(changed);checks+=1
    # Marker and section geometry must agree; no section may opt into runtime.
    h=audit.EH.unpack_from(data)
    for name in ('.vmu_handoff','.vmu_services','.worker_stack'):
        section=sections[name];at=h[6]+section['index']*audit.SH.size
        row=list(audit.SH.unpack_from(data,at))
        for field,value in ((1,1),(2,6),(3,row[3]+4),(5,row[5]+4)):
            changed=bytearray(data);r=row.copy();r[field]=value
            audit.SH.pack_into(changed,at,*r);refuse(changed);checks+=1
    for name in ('_kui_armada_ce_vmu_runtime_handoff','_kui_armada_ce_vmu_runtime_services',
            '__kui_vmu_worker_stack_bottom','__kui_vmu_worker_stack_top'):
        at,row=symbol_row(data,sections,name)
        changed=bytearray(data);row[1]+=4;audit.SYM.pack_into(changed,at,*row);refuse(changed);checks+=1
    return checks


def held_call_graph_mutations(directory):
    reports=sorted({p for pattern in ('traffic*.ci','a2*.ci','vmu_emulation.ci')
        for p in directory.glob(pattern)}|set((directory/'runtime').rglob('*.ci')))
    frames,calls=audit.load_graph(reports,qualified=True);audit.check_runtime_held_calls(frames,calls)
    checks=0;held=next(n for n in frames if audit.base_name(n)=='a2_held')
    owner=next(n for n in frames if audit.base_name(n)=='kui_armada_ce_maple_runtime_owner')
    def reject(changed_frames,changed_calls):
        nonlocal checks
        try:audit.check_runtime_held_calls(changed_frames,changed_calls)
        except ValueError:checks+=1
        else:raise AssertionError('Parked-DMA graph admitted a missing guard or device write')
    changed=calls.copy();changed[held]=[('fence_write_mmio','mutated:1')];reject(frames,changed)
    changed=calls.copy();changed[held]=calls[held]+[('fence_write_mmio','mutated:1')];reject(frames,changed)
    for at in (owner,audit.ROOT):
        changed=calls.copy();changed[at]=[(t,l) for t,l in calls[at] if audit.base_name(t)!='a2_held'];reject(frames,changed)
    changed=calls.copy();edges=list(calls[audit.ROOT]);index=next(i for i,(t,_) in enumerate(edges) if audit.base_name(t)=='a2_held')
    del edges[index];changed[audit.ROOT]=edges;reject(frames,changed)
    changed=frames.copy();changed['src/other.c:a2_held']=frames[held];reject(changed,calls)
    changed=frames.copy();changed['src/loader/armada_ce_maple_traffic.c:a2_service']=4;reject(changed,calls)
    return checks


def marker_mutations(path,name):
    data=path.read_bytes();check_layout_marker(data,name,True)
    assert exact_profile_marker(data,name)
    h=audit.EH.unpack_from(data);raw=[audit.SH.unpack_from(data,h[6]+i*audit.SH.size) for i in range(h[12])]
    table=next(s for s in raw if s[1]==2);strings=raw[table[6]]
    names=data[strings[4]:strings[4]+strings[5]];checks=0
    try:check_layout_marker(data,name,False)
    except ValueError:checks+=1
    else:raise AssertionError('Runtime marker admitted as default')
    for at in range(table[4],table[4]+table[5],audit.SYM.size):
        row=list(audit.SYM.unpack_from(data,at));label=names[row[0]:names.find(b'\0',row[0])]
        if label.decode('ascii')!=name:continue
        for field,value in ((1,0),(1,2),(2,4),(3,1),(5,1)):
            changed=bytearray(data);r=row.copy();r[field]=value;audit.SYM.pack_into(changed,at,*r)
            for checker in (lambda:check_layout_marker(changed,name,True),lambda:exact_profile_marker(changed,name)):
                try:checker()
                except ValueError:checks+=1
                else:raise AssertionError('Malformed runtime source marker admitted')
        return checks
    raise AssertionError('Missing runtime source marker')


def runtime_address_marker_mutations(path):
    """Reject old, partially moved, malformed, missing and duplicate source markers."""
    data=path.read_bytes();check_runtime_stage_addresses(data,True);checks=0
    h=audit.EH.unpack_from(data);raw=[audit.SH.unpack_from(data,h[6]+i*audit.SH.size) for i in range(h[12])]
    table=next(s for s in raw if s[1]==2);strings=raw[table[6]]
    names=data[strings[4]:strings[4]+strings[5]];rows={}
    for at in range(table[4],table[4]+table[5],audit.SYM.size):
        row=list(audit.SYM.unpack_from(data,at));name=names[row[0]:names.find(b'\0',row[0])].decode('ascii')
        if name in ('__kui_maple_write_runtime_handoff_stage','__kui_maple_write_runtime_services_stage'):
            rows[name]=(at,row)
    assert len(rows)==2
    def reject(changed,runtime=True):
        nonlocal checks
        try:check_runtime_stage_addresses(changed,runtime)
        except ValueError:checks+=1
        else:raise AssertionError('Mixed or malformed retained-address source markers admitted')
    reject(data,False)
    old=bytearray(data);missing=bytearray(data)
    for at,row in rows.values():
        for field,value in ((1,0),(1,row[1]-0x200),(1,row[1]+4),(2,4),
                (3,1),(3,0x20),(4,1),(5,1),(0,0)):
            changed=bytearray(data);r=row.copy();r[field]=value
            audit.SYM.pack_into(changed,at,*r);reject(changed)
        duplicate=bytearray(data);audit.SYM.pack_into(duplicate,table[4],*row);reject(duplicate)
        r=row.copy();r[1]-=0x200;audit.SYM.pack_into(old,at,*r)
        r=row.copy();r[0]=0;audit.SYM.pack_into(missing,at,*r)
    reject(old);reject(missing)
    check_runtime_stage_addresses(missing,False)
    return checks


def package_mutations(directory):
    """Never write a package fixture: mutate independent bytearrays in memory only."""
    stage=(directory/'stage.elf').read_bytes();resident=(directory/'resident-scia.elf').read_bytes()
    profile=(directory/'vmu-sd-profile.bin').read_bytes();checks=0
    def check(stage_data=stage,resident_data=resident,wire=profile):
        return check_runtime_contract(inspect_elf(stage_data,0x8ce10000,0x8cfc4000),
            inspect_elf(resident_data,0x8c008300,0x8c00dc00),resident_data,wire)
    check()
    def reject(**kw):
        nonlocal checks
        try:check(**kw)
        except ValueError:checks+=1
        else:raise AssertionError('Runtime package admitted a low-service/profile mutation')
    def sections(data):
        h=audit.EH.unpack_from(data)
        return [audit.SH.unpack_from(data,h[6]+i*audit.SH.size) for i in range(h[12])]
    def file_offset(data,address):
        s=next(s for s in sections(data) if s[1]==1 and s[3]<=address<s[3]+s[5])
        return s[4]+address-s[3]
    def symbol(data,name,field,value):
        table=next(s for s in sections(data) if s[1]==2);strings=sections(data)[table[6]]
        names=data[strings[4]:strings[4]+strings[5]]
        for at in range(table[4],table[4]+table[5],audit.SYM.size):
            row=list(audit.SYM.unpack_from(data,at))
            if names[row[0]:names.find(b'\0',row[0])].decode('ascii')==name:
                row[field]=value;changed=bytearray(data);audit.SYM.pack_into(changed,at,*row)
                return changed
        raise AssertionError('Missing package symbol '+name)
    rs=inspect_elf(resident,0x8c008300,0x8c00dc00)['symbols']
    descriptor=file_offset(resident,rs['_kui_retail_ce_vmu_loan_descriptor'])
    for byte in range(20):
        changed=bytearray(resident);changed[descriptor+byte]^=1;reject(resident_data=changed)
    for name in ('_kui_retail_async_vmu_loan_begin','_kui_retail_async_vmu_loan_end'):
        for field,value in ((1,rs[name]+2),(2,0),(2,0xffffffff),(3,1)):
            reject(resident_data=symbol(resident,name,field,value))
    reject(resident_data=symbol(resident,'_kui_retail_ce_vmu_loan_descriptor',1,rs['_kui_retail_ce_vmu_loan_descriptor']+4))
    reject(resident_data=symbol(resident,'__retail_resident_binary_end',1,0x8c00dc00))
    ss=inspect_elf(stage,0x8ce10000,0x8cfc4000)['symbols'];start=ss['__vmu_sd_profile_blob_start']
    profile_at=file_offset(stage,start)
    for byte in (0,8,12,16,56,188,288,512,1856,2880,4095):
        changed=bytearray(stage);changed[profile_at+byte]^=1;reject(stage_data=changed)
    # Re-seal malformed canonical fields to exercise semantic checks after CRC.
    for byte in (20,188,288,512,2888):
        wire=bytearray(profile);wire[byte]^=1;wire[16:20]=bytes(4)
        struct.pack_into('<I',wire,16,zlib.crc32(wire))
        changed=bytearray(stage);changed[profile_at:profile_at+len(wire)]=wire
        reject(stage_data=changed,wire=wire)
    mismatch=bytearray(profile);mismatch[-1]^=1;reject(wire=mismatch)
    for name,value in (('__vmu_sd_profile_blob_start',start+1),('__vmu_sd_profile_blob_start',0),
            ('__vmu_sd_profile_blob_end',ss['__vmu_sd_profile_blob_end']+4)):
        reject(stage_data=symbol(stage,name,1,value))
    return checks


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory',type=Path)
    parser.add_argument('--json',type=Path)
    args=parser.parse_args()
    report={'critical_helper_mutations_rejected':helper_mutations(args.directory/'hook.elf'),
        'stage_resident_profile_mutations_rejected':sum(marker_mutations(args.directory/file,name)
            for file,name in (('stage.elf','__kui_maple_write_runtime_stage'),
                ('resident-scia.elf','__kui_maple_write_runtime_resident'))),
        'runtime_package_contract_mutations_rejected':package_mutations(args.directory),
        'runtime_stage_address_mutations_rejected':runtime_address_marker_mutations(args.directory/'stage.elf'),
        'parked_dma_call_graph_mutations_rejected':held_call_graph_mutations(args.directory),
        'separate_profile_admission':True,'scope':'Native critical-body/profile/layout admission only; no hardware result'}
    if args.json:args.json.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__=='__main__':main()
