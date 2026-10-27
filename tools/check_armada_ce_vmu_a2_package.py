#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit the isolated Armada CE A2 package and explicit persistent runtime profile."""
import argparse
import hashlib
import json
import struct
from pathlib import Path
from check_armada_ce_bind import check as check_binding
from check_armada_ce_vmu_a2 import check as check_a2,RUNTIME_HANDOFF,RUNTIME_SERVICES,RUNTIME_SERVICES_BYTES
from check_armada_ce_excerpt import audit_cache_reader,function_sizes
from check_loader_layout import EH,SH,SYM,inspect_elf,region
from check_armada_ce_cached_publication import check as check_cached_publication
from vmu_persist_profile import decode_profile,WIRE_BYTES as PROFILE_BYTES


def check_layout_marker(data,name,write_layout,*,value=1):
    """Bind each relevant translation unit to the admitted helper profile."""
    header=EH.unpack(region(data,0,EH.size,'layout-marker ELF header'))
    if header[11]!=SH.size or not header[12]:
        raise ValueError('Invalid layout-marker ELF sections')
    sections=[SH.unpack(region(data,header[6]+i*SH.size,SH.size,'layout-marker section'))
        for i in range(header[12])]
    tables=[s for s in sections if s[1]==2]
    if len(tables)!=1:
        raise ValueError('Layout marker requires one static symbol table: '+name)
    table=tables[0]
    if table[9]!=SYM.size or table[5]%SYM.size or not 0<=table[6]<len(sections):
        raise ValueError('Invalid layout-marker symbol table: '+name)
    strings=sections[table[6]]
    if strings[1]!=3:
        raise ValueError('Invalid layout-marker symbol strings: '+name)
    strings=region(data,strings[4],strings[5],'layout-marker symbol strings')
    symbols=region(data,table[4],table[5],'layout-marker symbols');markers=[]
    for at in range(0,len(symbols),SYM.size):
        start,symbol_value,size,info,other,index=SYM.unpack_from(symbols,at)
        if not start:continue
        end=strings.find(b'\0',start)
        if not 0<=start<len(strings) or end<0:
            raise ValueError('Invalid layout-marker symbol name')
        if strings[start:end].decode('ascii')==name:
            markers.append((symbol_value,size,info,other,index))
    expected=[(value,0,0x10,0,0xfff1)] if write_layout else []
    if markers!=expected:
        raise ValueError('Stage/resident layout marker disagrees with helper profile: '+name)


def check_runtime_stage_addresses(data,runtime):
    """Reject stale or mixed stage/helper retained-record layouts."""
    for name,value in (('__kui_maple_write_runtime_handoff_stage',RUNTIME_HANDOFF),
            ('__kui_maple_write_runtime_services_stage',RUNTIME_SERVICES)):
        check_layout_marker(data,name,runtime,value=value)


def check_runtime_contract(stage,resident,resident_data,profile_wire):
    """Bind fixed low services and the embedded profile without exposing card identity."""
    rs=resident['symbols'];stream='_kui_retail_ce_stream_bus_entry' in rs
    offset=52+4*stream;at=rs.get('_kui_retail_ce_vmu_loan_descriptor',0)
    begin=rs.get('_kui_retail_async_vmu_loan_begin',0);end=rs.get('_kui_retail_async_vmu_loan_end',0)
    if at!=0x8c008300+offset or region(resident['payload'],offset,20,'resident loan descriptor')!=struct.pack('<5I',0x564c4f31,1,20,begin,end):
        raise ValueError('Runtime resident loan descriptor differs from its exact source entries')
    if any(p&1 or not 0x8c008300+offset+20<=p<rs.get('__retail_resident_binary_end',0)<=0x8c00d800 for p in (begin,end)):
        raise ValueError('Runtime loan entry leaves admitted low code')
    sizes=function_sizes(resident_data)
    if any(name not in sizes or sizes[name][1]<=0 or sizes[name][0]+sizes[name][1]>rs['__retail_resident_binary_end']
            for name in ('_kui_retail_async_vmu_loan_begin','_kui_retail_async_vmu_loan_end')):
        raise ValueError('Runtime loan descriptor target lacks a bounded function span')
    start=stage['symbols'].get('__vmu_sd_profile_blob_start',0)
    finish=stage['symbols'].get('__vmu_sd_profile_blob_end',0)
    if start%4 or finish-start!=PROFILE_BYTES:raise ValueError('Runtime requires an exact retained canonical profile')
    wire=region(stage['payload'],start-0x8ce10000,PROFILE_BYTES,'prepared persistent profile')
    decode_profile(wire)
    if profile_wire!=wire:
        raise ValueError('Runtime embedded persistent profile differs from admitted input')
    return {'prepared_storage_profile':{'bytes':PROFILE_BYTES,'sha256':hashlib.sha256(wire).hexdigest(),
            'canonical_preparation_codec':True,'fresh_physical_stage_admission_required':True,
            'private_identity_and_extents_exported':False},
        'resident_vmu_loan':{'descriptor_offset':offset,'descriptor_bytes':20,
            'begin':hex(begin),'end':hex(end),'bound_to_exact_function_symbols':True,
            'retained_services_p2':hex(RUNTIME_SERVICES),'retained_services_bytes':RUNTIME_SERVICES_BYTES,
            'low_guard_bottom':'0x8c00dc00','low_stack_top':'0x8c00e000'}}


def check(directory):
    directory=Path(directory);report=check_binding(directory)
    adapter=check_a2(directory)
    write_layout=adapter.get('write_layout',False)
    runtime=adapter.get('write_runtime',False)
    reserve_begin=int(adapter['reserved_range_p1'][0],16)
    controller=inspect_elf((directory/'bind-probe.elf').read_bytes(),0x8c004000,0x8c007c00,
        entry_symbol='_kui_armada_ce_bind_probe')
    at=controller['symbols'].get('_kui_armada_ce_maple_probe_contract',0)-0x8c004000
    if region(controller['payload'],at,4,'A2 report contract')!=b'CMP8':
        raise ValueError('A2 controller requires CMP8')
    report['cache_reader']=audit_cache_reader(controller,function_sizes((directory/'bind-probe.elf').read_bytes()))
    stage_data=(directory/'stage.elf').read_bytes()
    stage=inspect_elf(stage_data,0x8ce10000,reserve_begin)
    check_layout_marker(stage_data,'__kui_maple_write_layout_stage',write_layout)
    check_layout_marker(stage_data,'__kui_maple_write_runtime_stage',runtime)
    check_runtime_stage_addresses(stage_data,runtime)
    resident_data=(directory/'resident-scia.elf').read_bytes()
    resident=inspect_elf(resident_data,0x8c008300,0x8c00dc00 if runtime else 0x8c00d800)
    check_layout_marker(resident_data,'__kui_maple_write_layout_resident',write_layout)
    check_layout_marker(resident_data,'__kui_maple_write_runtime_resident',runtime)
    if runtime:
        report.update(check_runtime_contract(stage,resident,resident_data,
            (directory/'vmu-sd-profile.bin').read_bytes()))
    marker=stage['symbols'].get('_kui_armada_ce_maple_hook_stage_contract',0)
    if region(stage['payload'],marker-0x8ce10000,4,'pre-NK installer contract')!=b'CMH1':
        raise ValueError('Pre-NK A2 installer contract missing')
    for name,file in [('hook','hook.bin'),('bridge','bridge.bin')]:
        start=stage['symbols'].get('__maple_'+name+'_blob_start',0)
        end=stage['symbols'].get('__maple_'+name+'_blob_end',0)
        data=(directory/file).read_bytes()
        if start%4 or end-start!=len(data) or region(stage['payload'],start-0x8ce10000,len(data),name+' blob')!=data:
            raise ValueError('Embedded '+name+' differs from audited binary')
    if stage['symbols']['__retail_stage_bss_end']>=reserve_begin:
        raise ValueError('Stage overlaps reserved A2 range')
    report['read_only_a2_adapter']=adapter
    if report['read_only_a2_adapter']['cached_c_execution']:
        report['cached_helper_publication']=check_cached_publication(
            stage_data,write_layout=write_layout)
    mode=report['read_only_a2_adapter']['diagnostic_mode']
    full=mode==0
    if report['read_only_a2_adapter']['automatic_terminal_capture']:
        target=resident['symbols'].get('_kui_retail_menu_hook',0)
        if target&1 or not 0x8c008300<=target<0x8c00d800:
            raise ValueError('Automatic A2 capture has no bounded resident menu entry')
        if b'AUTO A2 STOP\0' not in resident['payload']:
            raise ValueError('Automatic A2 capture resident label is missing')
        report['automatic_terminal_capture']={'menu_vector':'0xac0000e0',
            'expected_menu_word':'0xacfcff80','installed_resident_entry':hex(target),
            'private_resident_stack_audited':True,
            'trigger':'deterministic VMU checkpoint or bounded completion count; not an interrupt watchdog'}
    purposes={0:'source-specific completion-fenced read-only synthetic blank virtual A2 experiment',
        1:'matched gate-only performance control with synthetic A2 publication disabled',
        2:'matched ownership-proof-only performance control with synthetic A2 publication disabled'}
    report.update(purpose=purposes[mode],diagnostic_mode=mode,
        diagnostic_mode_name=report['read_only_a2_adapter']['diagnostic_mode_name'],
        diagnostic_mode_word_helper_offset=report['read_only_a2_adapter']['diagnostic_mode_word_helper_offset'],
        worker_callsite_patched_before_nk=True,original_completion_prologue_unchanged=True,live_driver_code_patched=False,
        reserved_range_precedes_unchanged_oem_globals=True,virtual_vmu_enabled=full,virtual_slot='A2',synthetic_blank_card=full,sd_io=False,
        dma_chain_followed=True,dma_payload_read=True,save_data_written=False,
        a2_hardware_test_pending=True)
    if write_layout:
        report.update(write_layout=True,stage_range_p1=['0x8ce10000',hex(reserve_begin)],
            reserved_range_p1=adapter['reserved_range_p1'],write_commands_enabled=False)
    if runtime:
        report['runtime_a2_adapter']=report.pop('read_only_a2_adapter')
        report.update(purpose='source-specific completion-held persistent virtual A2 runtime experiment',
            write_runtime=True,write_commands_enabled=full,synthetic_blank_card=False,
            sd_io=full,physical_storage_write_capability=full,save_data_written=False,
            a2_hardware_test_pending=True,
            scope='Native linked build/profile/stack admission; physical save/load/recovery and external shutdown behavior remain pending')
    return report


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('directory',type=Path)
    args=parser.parse_args();print(json.dumps(check(args.directory),indent=2))

if __name__=='__main__':main()
