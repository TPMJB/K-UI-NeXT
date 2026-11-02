#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Focused linked checks for the optional native SCI session-control module.

This is a separate profile audit. It does not extend, bypass or repin the
historical persistent-reader instruction contracts, and does not establish
Dreamcast execution. Unknown callback sites and missing stack reports fail.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from check_native_background_compact import complete_callgraphs
from check_retail_stack import load as load_graph, base
from check_vmu_native_worker_gateway import read_elf, symbol_bytes
from vmu_native_relocations import generate, HEADER

ONE=(1,0,0,0xfff1)
WORKER=0x1c000
LEASE=0x51000
STACK=0x1000
# Reviewed complete leaves: they never touch r15 or call C. Both first enter
# their own P2 alias. The cache leaf publishes512 operand-cache lines before
# disabling caches; enter resets SR/MMU/VBR and jumps to0xac010000.
HARDWARE={
    '_kui_native_return_cache_off': bytes.fromhex(
        '02c70dd11b202b40090009000bd00cd1fde302623922222020701041f98b09d009d1122'
        '0090009000900090009000900090009000b00090000000020080000f4000200001c0000ff08080000'),
    '_kui_native_return_enter': bytes.fromhex(
        '02c70fd11b202b40090009000dd00e400dd004e112200dd00dd1122009000900090009000'
        '90009000900090009d02e4009df00e02a401e4008d02b400900090000000020f0000050'
        '100000ff1c0000ff080800000000008c00ffff8c000001ac'),
    '_kui_native_session_invalidate': bytes.fromhex(
        '02c70ad11b202b400900090008d0026108d22b211220')+
        bytes.fromhex('0900')*8+bytes.fromhex('0b0009000900000000201c0000ff00080000'),
}


def require(value,message):
    if not value:raise ValueError(message)


def span(image,first,last):
    address=image[4][first][0];end=image[4][last][0]
    require(address<end,'Invalid embedded span: '+first)
    owners=[s for s in image[3] if s[1]==1 and s[2]&2 and
            s[3]<=address<end<=s[3]+s[5]]
    require(len(owners)==1,'Unowned embedded span: '+first)
    s=owners[0];return image[0][s[4]+address-s[3]:s[4]+end-s[3]]


def worker_geometry(worker):
    symbols=worker[4]
    require(symbols.get('__kui_native_session_controls_worker')==ONE,
            'Missing or malformed session-controls marker')
    slot=symbols.get('__kui_native_session_virtual_slot')
    require(slot in (ONE,(2,0,0,0xfff1)),
            'Missing or malformed session virtual slot marker')
    require(symbols.get('__kui_native_general_lean_worker_layout')==ONE and
            symbols.get('__kui_native_isolation_lease_bytes')==(LEASE,0,0,0xfff1) and
            symbols.get('__kui_native_worker_span')==(WORKER,0,0,0xfff1),
            'Session-controls geometry is not112KiB worker /324KiB lease')
    require(not any(name in symbols for name in ('__kui_native_background_compact_worker',
        '__kui_native_vmu_lite_worker','__kui_native_vmu_only_worker')),
        'Conflicting worker profile marker')
    _,header,_=symbol_bytes(worker,'_kui_vmu_native_worker_exports',1)
    words=struct.unpack('<20I',header)
    require(words[:3]==(0x4b4e5031,4,80),'Invalid session worker exports')
    require(words[9:12]==(0xac100000+WORKER-STACK,0xac100000+WORKER,0xac100000+WORKER),
            'Worker does not retain its entire4KiB stack')
    begin=symbols['__vmu_native_worker_bss_begin'][0]
    end=symbols['__vmu_native_worker_bss_end'][0]
    require(0xac100050<=begin<=end<=words[9],'Worker BSS overlaps its stack')
    require(symbols['_overlay'][1]==1452,'Worker does not use one bounded reply staging slot')
    require(symbols['_captured'][1]==symbols['_issued_chain'][1]==0x3000,
            'Session controls must preserve both full12KiB chain copies')
    for name,expected in HARDWARE.items():
        _,actual,section=symbol_bytes(worker,name,2)
        require(section[2]&7==6 and actual==expected,'Hardware handoff leaf changed: '+name)
    return dict(virtual_slot=slot[0],worker_bytes=WORKER,lease_bytes=LEASE,cache_bytes=0x30000,
        worker_stack_bytes=STACK,bss_bytes=end-begin,bss_end=hex(end),
        space_before_stack=words[9]-end,full_chain_copies=True,single_reply_staging=True,
        exact_hardware_cache_and_P2_entry_leaves=True)


def relocation(directory,name,link,owned):
    elf=(directory/(name+'.elf')).read_bytes()
    binary=(directory/(name+'.bin')).read_bytes()
    expected=generate(elf,binary,link,owned)
    require(expected==(directory/(name+'.reloc')).read_bytes(),
            'Relocation table differs from complete linked extraction: '+name)
    return dict(bytes=len(binary),relocations=(len(expected)-HEADER)//8,
                owned_span=owned,sha256=hashlib.sha256(binary).hexdigest())


def descriptor(stage):
    address,wire,section=symbol_bytes(stage,'__native_session_descriptor',1)
    require(len(wire)==18432 and not address&31 and section[2]&7==2,
            'Session descriptor is not the aligned immutable18432-byte object')
    require(wire==b'KUISES01'+struct.pack('<II',1,18432)+bytes(18416),
            'Built session descriptor is not the canonical unpatched template')
    count=0
    for s in stage[3]:
        if s[1]==1 and s[2]&2:
            data=stage[0][s[4]:s[4]+s[5]];count+=data.count(wire)
    require(count==1,'Session descriptor is missing or duplicated in allocated stage bytes')
    return dict(bytes=len(wire),address=hex(address),unique_blank_template=True)


def stack_bounds(directory,worker,resident,auxiliary):
    """Close actual emitted C paths using authored, source-qualified callbacks.

    The existing read-ahead gateway execution proof establishes its20-byte
    parked owner frame. Its auxiliary stack is a different owned allocation;
    it is never added as if it were part of the worker or352-byte GD stack.
    """
    from check_vmu_native_readahead import gateway_cases
    gateway=gateway_cases(resident,auxiliary)
    require(gateway.get('parked_owner_stack_bytes')==20,
            'Missing actual read-ahead stack-switch proof')
    _,bus,section=symbol_bytes(resident,'_bus',1)
    require(len(bus)==28 and section[2]&7==2,'Low SD bus is not immutable')
    targets=[symbol_bytes(resident,'_'+name,2)[0] for name in ('select_card','transfer','loan_ticks')]
    require(bus==struct.pack('<7I',0,0,0,*targets,0),
            'Low SD callback descriptor or NULL optional block hook changed')
    frames={};calls={};origins={}
    from check_retail_stack import NODE
    sets={'worker':sorted((directory/'native').rglob('*.ci')),
          'low':sorted(directory.glob('resident-sci.elf*.ltrans*.ci'))}
    if not sets['low']:sets['low']=sorted((directory/'resident/lto').glob('*.ltrans*.ci'))
    for domain,reports in sets.items():
        require(reports,'Missing compiler callgraph: '+domain)
        f,c=load_graph(reports,qualified=True,scope=domain)
        image=worker if domain=='worker' else resident
        linked={base(n[1:] if n.startswith('_') else n) for n in image[4]}
        frames.update({n:v for n,v in f.items() if base(n) in linked});calls.update(c)
        for report in reports:
            for title,label in NODE.findall(report.read_text()):
                parts=label.split(r'\n')
                if len(parts)>1:origins[domain+':'+title]=Path(parts[1].rsplit(':',2)[0]).name
    for name in HARDWARE:
        key='worker:'+name[1:];frames[key]=0;calls[key]=[]
    _,body,_=symbol_bytes(worker,'_kui_vmu_native_trace_irq',2)
    require(body==bytes.fromhex('420002243200011422000b000214'),
            'Saved exception reader is not its exact zero-stack leaf')
    frames['worker:kui_vmu_native_trace_irq']=0;calls['worker:kui_vmu_native_trace_irq']=[]
    frames['switch:auxiliary']=20;calls['switch:auxiliary']=[]
    by_base={}
    for name in frames:by_base.setdefault((name.split(':',1)[0],base(name)),[]).append(name)
    def resolve(domain,function,owner=None):
        exact=domain+':'+function
        found=[exact] if exact in frames else by_base.get((domain,base(function)),[])
        if owner:found=[n for n in found if origins.get(n)==owner]
        require(found,'Missing emitted callback frame: '+domain+':'+function)
        return found
    def high(*names):return [n for name in names for n in resolve('worker',name)]
    def low(*names):return [n for name in names for n in resolve('low',name)]
    indirect_sites=set();escapes=set();null_sites=set()
    def indirect(node,label):
        domain=node.split(':',1)[0];function=base(node);source=label.split(':',1)[0]
        if source=='vmu_native_reader_capture.inc' and function in (
                'reader_cooperative_service','reader_readahead_visit'):
            return ['switch:auxiliary']
        if domain=='low':
            if source=='retail_async.c' and function in ('native_readahead_visit','kui_vmu_observe_sd_lease'):
                return ['switch:auxiliary']
            if source=='vmu_native_background_resident.c' and function=='kui_vmu_observe_sd_lease':
                return high('kui_vmu_native_persistent_sd_transaction')
            fixed={('guest','retail_gd.c'):('native_background_map',),
                ('kui_retail_gd_dispatch','retail_gd.c'):('check_sectors',),
                ('byte','sci_stream.c'):('transfer',),('select','sci_stream.c'):('select_card',),
                ('deliver','retail_cursor.c'):('write_out',),
                ('kui_retail_cursor_feed','retail_cursor.c'):('write_out',),
                ('write_out','retail_async.c'):('native_background_map',),
                ('kui_retail_async_trace','retail_async.c'):('kui_native_loader_timeline_visit',)}
            if (function,source) in fixed:return low(*fixed[function,source])
            if (function,source) in (('kui_retail_menu_return','retail_resident.c'),
                    ('kui_vmu_observe_low_failure','vmu_native_background_resident.c')):
                escapes.add((node,label));return []
        else:
            fixed={'vmu_durable.c':('persistent_lease',),
                'vmu_emulation.c':('durable_card_read','durable_card_commit'),
                'vmu_native_heap.c':('read_heap',),'vmu_native_layout.c':('read_value',),
                'vmu_native_lifetime.c':('read_heap',),'vmu_native_live.c':('boundary_read',),
                'vmu_native_runtime.c':('boundary_read',),
                'vmu_store.c':('durable_card_sector_read','durable_card_sector_write','durable_card_sector_sync'),
                'vmu_readonly_overlay.c':('persistent_admit','persistent_handler'),
                'native_return.c':('native_session_return_read',),
                'native_disc_set.c':('native_session_control_visit',)}
            owners={'vmu_durable.c':{'durable_card_execute'},
                'vmu_emulation.c':{'kui_vmu_emulation_handle'},
                'vmu_native_heap.c':{'read_is','kui_vmu_native_heap_lease_valid','kui_vmu_native_heap_lease_valid_with_release'},
                'vmu_native_layout.c':{'kui_vmu_native_layout_bind_checked'},
                'vmu_native_lifetime.c':{'kui_vmu_native_lifetime_release_safe_ranges','kui_vmu_native_lifetime_prepare'},
                'vmu_native_live.c':{'kui_vmu_native_live_check'},
                'vmu_native_runtime.c':{'kui_vmu_native_runtime_check'},
                'vmu_store.c':{'write_sync','kui_vmu_store_open','kui_vmu_store_read','kui_vmu_store_sync'},
                'vmu_readonly_overlay.c':{'kui_vmu_readonly_overlay_prepare','kui_vmu_readonly_overlay_complete'},
                'native_return.c':{'kui_native_return_load'},
                'native_disc_set.c':{'kui_native_disc_set_visit'}}
            if source in fixed:
                require(function in owners[source],'Unexpected session callback owner: '+node)
                return high(*fixed[source])
            if source=='vmu_native_passive.c' and function=='persistent_lease':
                return low('kui_vmu_observe_sd_lease')
            if source=='native_session_worker.inc':
                if function in ('kui_vmu_native_passive_arm','session_bind','native_session_worker_prepare'):
                    return low('kui_retail_native_session_control')
                if function=='native_session_control_visit':
                    # RESET and the scoped loan are the only two typed
                    # callbacks in this adapter. Their union conservatively
                    # overbounds each call site without assuming an ordering.
                    return ['switch:auxiliary',*low('kui_vmu_observe_sd_lease')]
                if function in ('report','native_session_return_menu'):
                    return low('kui_vmu_observe_sd_lease')
                if function=='native_session_controller_complete':
                    return high('native_session_control_visit')
            if source=='sd_reader.c':
                if function=='read_payload':null_sites.add((node,label));return []
                if function in ('ticks',):return low('loan_ticks')
                if function in ('transfer',):return low('transfer')
                if function in ('release','kui_loader_sd_read','kui_loader_sd_stream_start','kui_loader_sd_stream_stop'):
                    return low('select_card')
            if source=='sci_sd_storage.c':
                if function=='kui_sci_sd_write' and label.endswith(':83:17'):
                    null_sites.add((node,label));return []
                callbacks={'sci_storage_byte':('transfer',),'sci_storage_ready':('loan_ticks',),
                    'sci_storage_release':('select_card',),'kui_sci_sd_sync':('select_card',),
                    'kui_sci_sd_write':('select_card',)}
                require(function in callbacks,'Unexpected writer callback owner: '+node)
                return low(*callbacks[function])
            if source=='vmu_backing.c':
                require(function in ('byte','wait_byte','kui_vmu_sd_identity_read'),
                        'Unexpected backing callback owner: '+node)
                return low('transfer','loan_ticks','select_card')
        raise ValueError('Unadmitted session callback: '+node+' at '+label)
    memo={};visiting=set()
    library={'__udivsi3','__sdivsi3','__udivdi3','__umoddi3','__moddi3'}
    def depth(node):
        if node in memo:return memo[node]
        require(node not in visiting,'Recursive session stack path: '+node)
        if node not in frames:
            if base(node) in library:return (0,[node+'(library margin)'])
            domain,function=node.split(':',1)
            return max((depth(n) for n in resolve(domain,function)),key=lambda x:x[0])
        visiting.add(node);best=(0,[])
        for target,label in calls.get(node,[]):
            targets=indirect(node,label) if target=='__indirect_call' else [target]
            if target=='__indirect_call':indirect_sites.add((node,label,tuple(targets)))
            for callee in targets:
                answer=depth(callee)
                if answer[0]>best[0]:best=answer
        visiting.remove(node);memo[node]=(frames[node]+best[0],[node+'('+str(frames[node])+')']+best[1])
        return memo[node]
    roots=('kui_vmu_native_passive_arm','kui_vmu_native_passive_submit',
        'kui_vmu_native_passive_complete','kui_vmu_native_readonly_release_check',
        'kui_vmu_native_passive_report','kui_vmu_native_runtime_fault',
        'kui_vmu_native_passive_split_submit','kui_vmu_native_persistent_sd_transaction',
        'native_session_control_visit','native_session_controller_complete')
    paths={}
    for root in roots:
        answer=max((depth(n) for n in resolve('worker',root)),key=lambda x:x[0])
        total=answer[0]+336
        require(total<=STACK,f'Worker stack {root} needs{total} of4096 bytes')
        paths[root]=dict(c_bytes=answer[0],guard_and_assembly_margin=336,total_bytes=total,path=answer[1])
    from check_vmu_native_background import stack_paths,library_cfg,rows,disassemble
    low_frames,low_calls=load_graph(sets['low'],qualified=True)
    # This General-profile cross-image callback is retained only for ABI.
    # Bind its exact linked RTS/NOP before treating it as a zero-frame leaf.
    _,timeline,owner=symbol_bytes(worker,'_kui_native_loader_timeline_visit',2)
    require(timeline==bytes.fromhex('0b000900') and owner[2]&7==6,
            'Low timeline callback is not the proved inert General leaf')
    low_frames['kui_native_loader_timeline_visit']=0;low_calls['kui_native_loader_timeline_visit']=[]
    low_roots={'kui_retail_resident_dispatch':0,'kui_retail_async_irq':44,'kui_retail_menu_return':0,
               'kui_retail_sdk_sci_dispatch':0}
    low_paths,low_callbacks,low_escapes=stack_paths(low_frames,low_calls,low_roots,
        {('deliver','retail_async.c'):20,('native_readahead_visit','retail_async.c'):20})
    helper_address,helper,owner=symbol_bytes(resident,'___udivsi3',2)
    library_proof=library_cfg(rows(disassemble(directory/'resident-sci.elf')),helper_address,len(helper))
    low_total=max(p['bytes'] for p in low_paths.values())+library_proof['stack_bytes']+32+16+32
    require(low_total<=352,f'Ordinary low stack needs{low_total} of352 bytes')
    return dict(worker_paths=paths,worker_available_bytes=STACK,
        indirect_sites=len(indirect_sites),null_bus_callback_sites=len(null_sites),
        source_qualified=True,read_ahead_gateway=gateway,
        ordinary_low=dict(paths=low_paths,conservative_bytes=low_total,available_bytes=352,
            linked_library=library_proof,guard_and_additional_margin=80,
            callback_sites=len(low_callbacks),stack_switch_escapes=low_escapes),
        terminal_stack_escapes=[list(x) for x in sorted(escapes)])


def mutation_checks(worker,stage):
    """Prove profile/bounds/entry/descriptor corruption fails closed."""
    rejected=0
    def refused(check,changed):
        nonlocal rejected
        try:check(changed)
        except (ValueError,KeyError,struct.error):rejected+=1;return
        raise ValueError('Session audit accepted a corrupted linked contract')
    for name,replacement in (
            ('__kui_native_session_controls_worker',None),
            ('__kui_native_session_controls_worker',(2,0,0,0xfff1)),
            ('__kui_native_isolation_lease_bytes',(0x55000,0,0,0xfff1)),
            ('__kui_native_worker_span',(0x20000,0,0,0xfff1)),
            ('__vmu_native_worker_bss_end',(0xac11b004,0,0,0xfff1)),
            ('_overlay',(0xac110000,17172,1,0)),
            ('_captured',(0xac110000,4096,1,0))):
        changed=list(worker);changed[4]=dict(worker[4])
        if replacement is None:del changed[4][name]
        else:changed[4][name]=replacement
        refused(worker_geometry,changed)
    for name in HARDWARE:
        address,body,section=symbol_bytes(worker,name,2)
        changed=list(worker);changed[0]=bytearray(worker[0])
        changed[0][section[4]+address-section[3]]^=1
        refused(worker_geometry,changed)
    address,wire,section=symbol_bytes(stage,'__native_session_descriptor',1)
    for offset in (0,8,12,16,len(wire)-1):
        changed=list(stage);changed[0]=bytearray(stage[0])
        changed[0][section[4]+address-section[3]+offset]^=1
        refused(descriptor,changed)
    return rejected


def audit(directory):
    directory=Path(directory)
    callgraphs=complete_callgraphs(directory)
    worker=read_elf(directory/'native-worker.elf')
    stage=read_elf(directory/'stage.elf')
    # read_elf rejects undefined imports, dynamic sections and TLS in every
    # linked component, independently of the relocation extraction below.
    components={name:read_elf(directory/(name+'.elf')) for name in
                ('resident-sci','resident-legacy','native-ra-aux')}
    geometry=worker_geometry(worker)
    relocations={name:relocation(directory,name,link,owned) for name,link,owned in
                 (('native-worker',0x8c100000,WORKER),('native-ra-aux',0x8c421000,0x4000))}
    require(span(stage,'__native_worker_blob_begin','__native_worker_blob_end')==
            (directory/'native-worker.bin').read_bytes(),'Stage embeds a different worker')
    require(span(stage,'__native_worker_reloc_begin','__native_worker_reloc_end')==
            (directory/'native-worker.reloc').read_bytes(),'Stage embeds a different relocation table')
    stacks=stack_bounds(directory,worker,components['resident-sci'],components['native-ra-aux'])
    return dict(schema='kui.native.session-controls.audit.v1',geometry=geometry,
        descriptor=descriptor(stage),relocations=relocations,
        compiler_callgraphs=callgraphs,all_components_without_imports=True,
        stage_embedded_worker_verified=True,stacks=stacks,console_execution_verified=False,
        mutation_rejections=mutation_checks(worker,stage),
        baseline_instruction_contracts_reused=False)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build',type=Path)
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    try:
        result=audit(args.build)
        text=json.dumps(result,indent=2)+'\n'
        if args.output:args.output.write_text(text)
        print(text,end='')
    except (ValueError,KeyError,OSError,struct.error) as error:
        parser.exit(2,'Native session-controls audit refused: '+str(error)+'\n')


if __name__=='__main__':main()
