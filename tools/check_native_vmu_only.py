#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit the exact compact A2 worker, synchronous reader and cold handoff.

Linked instruction models and emitted static callgraphs establish the reported
ABI/layout/stack properties. They do not establish console compatibility,
hardware timing, filesystem correctness or universal game memory ownership.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess

import check_vmu_native_persistent as persistent
from check_loader_layout import inspect_elf, region
from check_retail_instructions import audit as instruction_audit
from check_retail_stack import NODE, load as load_graph
from check_retail_loader_layout import (ASSEMBLY_STACK_BYTES, STACK_GUARD_BYTES,
                                       STACK_ALIGNMENT_GAP)
from check_vmu_native_worker_gateway import read_elf, symbol_bytes
from check_vmu_native_stage_stack import Machine
from native_runtime_layout import layout_for
from package_vmu_maple_observe import _function_base, linked_stack_rows
from runtime_package import verify
from vmu_native_relocations import generate

ROOT = Path(__file__).resolve().parents[1]
ONE = (1, 0, 0, 0xfff1)
WORKER_BASE = 0x8c100000
WORKER_P2 = 0xac100000
SPAN = 0x1c000
STACK_BYTES = 4096


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def publisher_body(stage):
    """Bind the actual P2 cache publisher without inventing install ordering."""
    address = stage[4]['_kui_native_passive_publish'][0]
    ops = [0xd001,0x402b,9,9] + list(struct.unpack('<2H',struct.pack('<I',address+12+0x20000000))) + \
          [0xe0e0,0x2409,0x3452,0x8903,0x04a3,0x7420,0xaffa,9,
           0xd007,0x6102,0xd207,0x212b,0x2012] + [9]*8 + [0x000b,9,9] + \
          list(struct.unpack('<4H',struct.pack('<2I',0xff00001c,0x800)))
    expected = struct.pack('<34H',*ops)
    sections=[section for section in stage[3] if section[2]&7 == 6 and
              section[3]<=address<address+len(expected)<=section[3]+section[5]]
    image=inspect_elf(stage[0],0x8ce00000,0x8cfe0000)
    body=region(image['payload'],address-0x8ce00000,len(expected),'P2 publisher')
    require(len(sections) == 1 and address%4 == 0 and body == expected,
            'Linked P2 publisher changed its exact purge/ICI/owner-CCR-preserving body')
    return {'exact_body_bytes':len(expected),'p2_code':hex(address+12+0x20000000),
            'owner_ccr_preserved':True,'post_ccr_write_nops':8,
            'whole_passive_run_publication_order_modeled':False}


def scif_descriptor(low):
    """Bind the initialized loan's finite callback set and NULL block field."""
    address,body,section = symbol_bytes(low,'_kui_native_vmu_scif_bus',1)
    require(len(body) == 28 and not address&3 and section[2]&7 == 2,
            'SCIF bus descriptor must be one immutable aligned seven-word object')
    targets = [symbol_bytes(low,'_'+name,2)[0] for name in
               ('bus_begin','bus_end','bus_select','bus_transfer','bus_ticks')]
    require(struct.unpack('<7I',body) == (0,*targets,0),
            'SCIF immutable bus callbacks or NULL block transfer differ')
    literal = struct.pack('<I',address)
    users=[]
    for name,record in low[4].items():
        if record[2] != 2:
            continue
        _,code,_=symbol_bytes(low,name,2)
        if literal in code:
            users.append(name)
    require(users,'SCIF actual constructor does not reference its immutable descriptor')
    return {'descriptor_address':hex(address),'bytes':28,'context_null':True,
            'callback_symbols':['bus_begin','bus_end','bus_select','bus_transfer','bus_ticks'],
            'block_callback_null':True,'linked_descriptor_users':users}


def controller_submit_mode(stage, worker, self_test=True):
    """Bind both control markers and execute the actual full-entry forwarder.

    The authored callee represents the retained full submit's C ABI only;
    production-C protocol/lifecycle tests exercise that callee separately.
    No capture function or other external target is allowed in this replay.
    """
    names=('__kui_native_vmu_only_full_submit_stage',
           '__kui_native_vmu_only_full_submit_worker')

    def mode(stage_symbols, worker_symbols):
        markers=(stage_symbols.get(names[0]),worker_symbols.get(names[1]))
        require(markers==(None,None) or markers==(ONE,ONE),
                'Full-submit control requires exact matching stage/worker markers')
        return 'full' if markers[0] is not None else 'split'

    selected=mode(stage[4],worker[4])
    cases=0
    rejected=0
    result={'controller_submit':selected,'matching_immutable_markers':True,
            'forwarder_instruction_cases':0,'mutation_rejections':0,
            'full_callee_is_authored_abi_stub':selected=='full',
            'full_protocol_execution_included_in_this_model':False}
    if selected=='full':
        target=symbol_bytes(worker,'_kui_vmu_native_passive_submit',2)[0]
        forwarder='_kui_vmu_native_passive_split_submit'
        _,forward_body,forward_section=symbol_bytes(worker,forwarder,2)
        require(len(forward_body)==12 and forward_section[2]&7==6,
                'Full-submit forwarder must be one immutable twelve-byte tail transfer')
        first,jump,slot,padding,literal=struct.unpack('<4HI',forward_body)
        register=first>>8&15
        require(first&0xf0ff==0xd001 and jump==(0x402b|(register<<8)) and
                slot==padding==9 and literal==target,
                'Full-submit forwarder changed its exact unconditional retained target')

        class Forwarder(Machine):
            def __init__(self,seed,body=None):
                super().__init__(worker,forwarder,seed,6 if seed&1 else 15,
                                 0x8c61bfc0,body=body)
                self.frame=(0x8c61bf00,0xac61bf00,0x0c61bf00,0x8c61be80)[seed%4]
                self.r[4]=self.frame
                self.saved=(self.r.copy(),self.mach,self.macl,self.gbr,self.sr,
                            self.fpscr,self.fr.copy())

            def call(self,address):
                if address==self.stop_pc:
                    return False
                require(address==target,'Control forwarder reaches a target other than retained full submit')
                require(self.r[4]==self.frame,'Control forwarder changes the captured-frame argument')
                self.calls.append(address)
                # Clobber volatile integer registers, PR and T as an ABI
                # adversary. A tail transfer may leave PR clobbered on return;
                # arrival at the original stop PC proves its return path.
                self.abi_return(0x73510000|len(self.calls))
                return True

            def check(self):
                self.run()
                before,mach,macl,gbr,sr,fpscr,fr=self.saved
                require(self.calls==[target] and self.r[8:]==before[8:] and
                        (self.mach,self.macl,self.gbr)==(mach,macl,gbr) and
                        self.sr&~1==sr&~1 and self.fpscr==fpscr and self.fr==fr and
                        not self.published,
                        'Compiled full-submit forwarder violates preserved caller ABI')

        for seed in range(8):
            Forwarder(seed).check()
            cases+=1
        result.update(full_submit_target=hex(target),captured_frame_argument_unchanged=True,
                      callee_preserved_registers_unchanged=True,
                      no_capture_or_other_external_target=True,
                      exact_unconditional_forwarder_bytes=len(forward_body))
        if self_test:
            _,body,_=symbol_bytes(worker,forwarder,2)
            # Change the first transfer's legal delay slot to destroy R4.
            # The unmodified target/caller code and remaining literal pool
            # still execute, so the model must reject the changed argument.
            changed=bytearray(body)
            transfer=None
            for offset in range(0,len(body)-3,2):
                word=struct.unpack_from('<H',body,offset)[0]
                if word>>12 in (0xa,0xb) or word&0xf0ff in (0x400b,0x402b):
                    transfer=offset
                    break
            require(transfer is not None,'Control forwarder lacks a compiled transfer')
            struct.pack_into('<H',changed,transfer+2,0xe400)
            try:
                Forwarder(0,bytes(changed)).check()
            except (ValueError,AssertionError):
                rejected+=1
            else:
                raise ValueError('Full-submit forwarder accepted a changed frame argument')
    if self_test:
        for selected_image in range(2):
            symbols=[dict(stage[4]),dict(worker[4])]
            if selected=='full':
                symbols[selected_image].pop(names[selected_image])
            else:
                symbols[selected_image][names[selected_image]]=ONE
            try:
                mode(*symbols)
            except ValueError:
                rejected+=1
            else:
                raise ValueError('Submit mode accepted mismatched stage/worker control markers')
    result.update(forwarder_instruction_cases=cases,mutation_rejections=rejected)
    return result


def terminal_guard_model(low, self_test=True):
    """Execute the emitted pure low guard at compact-lease boundaries."""
    class Guard(Machine):
        def __init__(self, control, body=None):
            super().__init__(low, '_native_resident_terminal', 7, 15, 0x8c007cf0, body=body)
            self.control = 0xac900000
            self.values = control
            self.r[4] = self.control
            self.writes = []

        def read(self, address):
            if self.control <= address < self.control+56:
                return self.values[(address-self.control)//4]
            return super().read(address)

        def write(self, address, value):
            self.writes.append((address, value))
            return super().write(address, value)

    cases = 0
    def expected(c):
        installer, stack, status, lease = c[:4]
        terminal, worker_end = c[10], c[8]
        start = lease|0x80000000
        return (status == 4 and installer == stack == 0 and not lease&31 and
                0x0c010020 <= lease <= 0x0d000000-SPAN and
                worker_end == start+0x20000000+SPAN and not terminal&1 and
                80 <= ((terminal-start)&0xffffffff) < SPAN)
    def check(c, body=None):
        nonlocal cases
        machine = Guard(c, body).run()
        require(machine.r[0] == int(expected(c)) and not machine.writes,
                'Compiled low terminal guard accepted wrong compact geometry')
        cases += 1
    for lease in (0, 0x0c010000, 0x0c010020, 0x0c600020, 0x0d000000-SPAN,
                  0x0d000000-SPAN+32, 0x0c600021, 0x8c600020, 0xac600020):
        for offset in (-2,0,78,79,80,82,SPAN-2,SPAN-1,SPAN,SPAN+2):
            c = [0]*14
            c[2],c[3],c[8],c[10] = 4,lease,(lease|0x80000000)+0x20000000+SPAN,((lease|0x80000000)+offset)&0xffffffff
            check(c)
            for field in (0,1,2,8):
                changed = c.copy()
                changed[field] ^= 1
                check(changed)
    mutations = 0
    if self_test:
        _,body,_ = symbol_bytes(low,'_native_resident_terminal',2)
        literal = struct.pack('<I',0x20000000+SPAN)
        require(body.count(literal) == 1,'Low guard lacks unique worker-end bound')
        changed = body.replace(literal,struct.pack('<I',0x20020000))
        c=[0]*14;c[2]=4;c[3]=0x0c600020;c[8]=0xac61c020;c[10]=0x8c600070
        try:
            check(c,changed)
        except ValueError:
            mutations += 1
        else:
            raise ValueError('Low guard accepted old128KiB worker endpoint')
    return {'instruction_cases':cases,'mutation_rejections':mutations,'owner_writes':0,
            'lease_bytes':SPAN,'worker_bytes':SPAN}


def callgraph_stacks(directory, worker, resident, transport, coherence):
    """Walk actual source-qualified graphs; every indirect site is finite.

    Unlike the old combined reader auditor this has no timer/read-ahead edges.
    The SD bus callbacks are chosen from the selected transport's actual source
    unit. No arbitrary callback or missing/dynamic frame is treated as zero.
    """
    reports = {'worker': sorted((directory/'native').rglob('*.ci')),
               'low': sorted(directory.glob('resident-sci.elf*.ltrans*.ci'))}
    require(all(reports.values()), 'Missing emitted worker or resident callgraphs')
    images = {'worker': worker[4], 'low': resident['symbols']}
    frames, calls, origins = {}, {}, {}
    for domain, paths in reports.items():
        f, c = load_graph(paths, qualified=True, scope=domain)
        live = {_function_base(name) for name in images[domain]}
        frames.update({name: size for name, size in f.items() if _function_base(name) in live})
        calls.update(c)
        for path in paths:
            for title, label in NODE.findall(path.read_text()):
                pieces = label.split(r'\n')
                if len(pieces) >= 2:
                    origins[domain+':'+title] = Path(pieces[1].rsplit(':', 2)[0]).name
    _, leaf, section = symbol_bytes(worker, '_kui_vmu_native_trace_irq', 2)
    require(section[2]&7 == 6 and leaf == bytes.fromhex('420002243200011422000b000214'),
            'Unexpected exception reader assembly leaf')
    frames['worker:kui_vmu_native_trace_irq'] = 0
    calls['worker:kui_vmu_native_trace_irq'] = []
    # These gateways were independently executed with unusable owner stacks.
    for name in coherence.get('verified_cold_owner_stack_gateways', []):
        frames['worker:'+name] = 0
        calls['worker:'+name] = []
    by_base = {}
    for name in frames:
        by_base.setdefault((name.split(':', 1)[0], _function_base(name)), []).append(name)

    def resolve(domain, name, owner=None):
        exact = domain+':'+name
        found = [exact] if exact in frames else by_base.get((domain, _function_base(name)), [])
        if owner is not None:
            found = [item for item in found if origins.get(item) == owner]
        require(found, 'Missing emitted callback frame: '+domain+':'+name)
        return found

    def high(*names):
        return [node for name in names for node in resolve('worker', name)]

    provider = 'sci_sd_bus.c' if transport == 1 else 'retail_sd.c'
    bus_names = {'transfer': 'transfer' if transport else 'bus_transfer',
                 'ticks': 'ticks' if transport else 'bus_ticks',
                 'select_card': 'select_card' if transport else 'bus_select',
                 'transfer_block': 'transfer_block' if transport else None}

    def bus(*names):
        result = []
        for name in names:
            actual = bus_names[name]
            require(actual is not None, 'SCIF cannot supply a block-transfer callback')
            result += resolve('low', actual, provider)
        return result

    fixed = {
        'vmu_durable.c': ('persistent_lease',),
        'vmu_emulation.c': ('durable_card_read', 'durable_card_commit'),
        'vmu_native_heap.c': ('read_heap',),
        'vmu_native_layout.c': ('read_value',),
        'vmu_native_lifetime.c': ('read_heap',),
        'vmu_native_live.c': ('boundary_read',),
        'vmu_native_runtime.c': ('boundary_read',),
        'vmu_store.c': ('durable_card_sector_read', 'durable_card_sector_write', 'durable_card_sector_sync'),
        'vmu_readonly_overlay.c': ('persistent_admit', 'persistent_handler'),
    }
    owners = {
        'vmu_durable.c': {'durable_card_execute'},
        'vmu_emulation.c': {'kui_vmu_emulation_handle'},
        'vmu_native_heap.c': {'read_is', 'kui_vmu_native_heap_lease_valid', 'kui_vmu_native_heap_lease_valid_with_release'},
        'vmu_native_layout.c': {'kui_vmu_native_layout_bind_checked'},
        'vmu_native_lifetime.c': {'kui_vmu_native_lifetime_release_safe_ranges'},
        'vmu_native_live.c': {'kui_vmu_native_live_check'},
        'vmu_native_runtime.c': {'kui_vmu_native_runtime_check'},
        'vmu_store.c': {'write_sync', 'kui_vmu_store_open', 'kui_vmu_store_read', 'kui_vmu_store_sync'},
        'vmu_readonly_overlay.c': {'kui_vmu_readonly_overlay_prepare', 'kui_vmu_readonly_overlay_complete'},
    }
    indirect_sites, null_sites = set(), set()

    def indirect(name, label):
        domain, function = name.split(':', 1)[0], _function_base(name)
        source = Path(label.split(':', 1)[0]).name
        if domain == 'low':
            if (function, source) == ('kui_vmu_observe_sd_lease', 'vmu_sd_lease.inc'):
                return high('kui_vmu_native_persistent_sd_transaction')
            if (function,source) == ('guest','retail_gd.c'):
                return resolve('low','observe_map','vmu_maple_observe_resident.c')
            if (function,source) == ('kui_retail_gd_dispatch','retail_gd.c'):
                if label.endswith(':233:16'):
                    return resolve('low','check_sectors','retail_resident.c')
                require(label.endswith(':387:16'),'Unexpected GD service callback site')
                return resolve('low','read_sectors','retail_resident.c')
            if (function,source) == ('file_read','retail_image.c'):
                return [node for target in ('read_block','read_run')
                        for node in resolve('low',target,'retail_resident.c')]
            if source == 'sd_reader.c':
                if function == 'kui_loader_sd_stream_next':
                    if not transport:
                        null_sites.add((name,label));return []
                    return bus('transfer_block')
                mapping = {'transfer':'transfer','ticks':'ticks','release':'select_card',
                           'read_run':'select_card'}
                require(function in mapping,'Unexpected low SD callback site: '+name)
                return bus(mapping[function])
            if (function,source) == ('kui_vmu_observe_low_failure','vmu_maple_observe_resident.c'):
                # The exact linked caller signature and executed pure guard
                # are admitted separately; model_cached_hooks proves that its
                # sole worker target replaces SP before entering worker C.
                return []
            if (function,source) == ('kui_retail_menu_return','retail_resident.c'):
                return []  # Separately bound sole boot-ROM target never returns.
            raise ValueError('Unexpected indirect low callback on VMU stack: '+name+' at '+label)
        if source in fixed:
            require(function in owners[source], 'Unexpected callback source owner: '+name)
            return high(*fixed[source])
        if (function, source) == ('persistent_lease', 'vmu_native_passive.c'):
            return resolve('low', 'kui_vmu_observe_sd_lease')
        if source == 'sd_reader.c':
            callbacks = {'transfer': ('transfer',), 'ticks': ('ticks',), 'release': ('select_card',),
                         'read_payload': ('transfer_block',), 'kui_loader_sd_read': ('select_card',)}
            require(function in callbacks, 'Unexpected SD reader callback: '+name)
            if function == 'read_payload' and transport == 0:
                # The SCIF bus constructor is independently bound below and
                # omits the optional seventh callback field entirely.
                null_sites.add((name, label))
                return []
            return bus(*callbacks[function])
        if source == 'vmu_backing.c':
            require(function in ('byte', 'wait_byte', 'kui_vmu_sd_identity_read'),
                    'Unexpected backing callback: '+name)
            return bus('transfer', 'ticks', 'select_card')
        if source == 'sci_sd_storage.c':
            if function == 'kui_sci_sd_write' and label.endswith(':83:17'):
                # Both compiled PIO adapters independently prove NULL at the
                # sole writer entry; check_pio_writers executes that SH code.
                null_sites.add((name, label))
                return []
            return bus('transfer', 'ticks', 'select_card')
        raise ValueError('Unadmitted VMU-only indirect call: '+name+' at '+label)

    memo, visiting = {}, set()

    def depth(name):
        if name in memo:
            return memo[name]
        require(name not in visiting, 'Recursive VMU stack path: '+name)
        if name not in frames:
            if _function_base(name) in ('__udivsi3', '__sdivsi3', '__udivdi3', '__umoddi3', '__moddi3'):
                return 0, [name+'(library margin)']
            domain, target = name.split(':', 1)
            return max((depth(node) for node in resolve(domain, target)), key=lambda item: item[0])
        visiting.add(name)
        best = 0, []
        for target, label in calls.get(name, []):
            targets = indirect(name, label) if target == '__indirect_call' else [target]
            if target == '__indirect_call':
                indirect_sites.add((name, label, tuple(targets)))
            for callee in targets:
                answer = depth(callee)
                if answer[0] > best[0]:
                    best = answer
        visiting.remove(name)
        memo[name] = frames[name]+best[0], [name+'('+str(frames[name])+')']+best[1]
        return memo[name]

    roots = ('kui_vmu_native_passive_arm', 'kui_vmu_native_passive_submit',
             'kui_vmu_native_passive_split_submit', 'kui_vmu_native_passive_complete',
             'kui_vmu_native_readonly_release_check', 'kui_vmu_native_passive_report',
             'kui_vmu_native_runtime_fault', 'kui_vmu_native_trace_game')
    paths = {}
    for root in roots:
        answer = max((depth(node) for node in resolve('worker', root)), key=lambda item: item[0])
        total = answer[0]+336  # 256B canary border plus 80B assembly/library allowance.
        require(total <= STACK_BYTES, root+' exceeds private 4KiB stack: '+str(total))
        paths[root] = {'c_frame_bytes': answer[0], 'assembly_library_guard_bytes': 336,
                       'conservative_bytes': total, 'worst_path': answer[1]}
    require(null_sites, 'Missing independently guarded writer NULL callback site')
    low_rows = linked_stack_rows(sorted((directory/'resident/lto').glob('*.ltrans*.su')), resident['symbols'])
    lease = [size for name, size in low_rows if _function_base(name) == 'kui_vmu_observe_sd_lease']
    require(len(lease) == 1, 'Missing unique low lease stack frame')
    low_paths = {}
    for root in ('kui_retail_resident_dispatch','kui_retail_menu_return'):
        answer=max((depth(node) for node in resolve('low',root)),key=lambda item:item[0])
        low_paths[root]={'c_frame_bytes':answer[0],'assembly_library_allowance':ASSEMBLY_STACK_BYTES,
                         'conservative_bytes':answer[0]+ASSEMBLY_STACK_BYTES,'worst_path':answer[1]}
    low_bytes = max(path['conservative_bytes'] for path in low_paths.values())
    low_available = 0x500-STACK_GUARD_BYTES-STACK_ALIGNMENT_GAP
    require(low_bytes <= low_available, 'Low synchronous stack sum exceeds guarded stack: '+str(low_bytes))
    return {'worker_paths': paths, 'worker_available_bytes': STACK_BYTES,
            'worst_worker_bytes': max(path['conservative_bytes'] for path in paths.values()),
            'ordinary_low_conservative_bytes': low_bytes, 'ordinary_low_available_bytes': low_available,
            'ordinary_low_paths':low_paths,
            'low_lease_frame_on_worker_stack_only': lease[0], 'indirect_sites': len(indirect_sites),
            'null_callback_sites': [list(site) for site in sorted(null_sites)],
            'source_qualified_callbacks': True, 'selected_bus_source': provider}


def audit(directory, *, transport, allow_dirty=False, self_test=True):
    directory = Path(directory)
    worker, stage, low = (read_elf(directory/name) for name in ('native-worker.elf','stage.elf','resident-sci.elf'))
    submit_mode=controller_submit_mode(stage,worker,self_test)
    for image, marker in ((worker, '__kui_native_vmu_only_worker'), (stage, '__kui_native_vmu_only_stage')):
        require(image[4].get(marker) == ONE, 'Missing exact VMU-only marker: '+marker)
        require(layout_for(image[4]).LEASE_BYTES == SPAN, 'VMU-only lease must be112KiB')
    for image, marker in ((stage, '__kui_native_vmu_only_transport_stage'),
                          (low, '__kui_native_vmu_only_transport_resident')):
        require(image[4].get(marker) == (transport,0,0,0xfff1), 'Selected transport differs: '+marker)
    require(low[4].get('__kui_native_vmu_only_resident') == ONE, 'Missing VMU-only low resident marker')
    forbidden = ('_kui_vmu_native_background_manifest_bind', '_native_background_boot_region',
                 '_native_readahead_bind', '_kui_retail_async_irq', '_kui_sci_stream_poll',
                 '__native_readahead_aux_blob_begin', '__native_legacy_resident_blob_begin')
    for image in (worker, stage, low):
        require(not set(forbidden)&image[4].keys(), 'VMU-only image contains async/cache/dual machinery')
        require(not any('readahead' in name or 'read_ahead' in name for name in image[4]),
                'VMU-only image retains read-ahead symbols')
    binary = (directory/'native-worker.bin').read_bytes()
    wire = (directory/'native-worker.reloc').read_bytes()
    require(generate(worker[0],binary,WORKER_BASE,SPAN) == wire, 'Relocations differ from actual ELF')
    policy = persistent.worker_load_policy(worker,binary)
    require(policy['cached_code'], 'VMU-only requires cached code and P2 persistent data')
    symbols = {name: row[0] for name, row in worker[4].items()}
    names = ('_kui_vmu_native_passive_arm','_kui_vmu_native_passive_submit_hook',
             '_kui_vmu_native_passive_complete_hook','_kui_vmu_native_passive_terminal',
             '__vmu_native_worker_bss_begin','__vmu_native_worker_bss_end',
             '__vmu_native_worker_stack_bottom','__vmu_native_worker_stack_top',
             '__vmu_native_worker_end','_kui_vmu_native_passive_resume',
             '_kui_vmu_native_passive_consumer','_kui_vmu_native_readonly_release_hook',
             '_kui_vmu_native_readonly_release_resume','_kui_vmu_native_persistent_sd_transaction',
             '_kui_vmu_native_runtime_fault_hook','_kui_vmu_native_allocation_failure_hook',
             '_kui_vmu_native_trace_exports')
    fields = struct.unpack_from('<20I',binary)
    require(fields[:3] == (0x4b4e5031,4,80) and fields[3:] == tuple(symbols[name] for name in names),
            'Typed worker exports differ from actual linked symbols')
    require(fields[7] == WORKER_P2+len(binary) <= fields[8] <= fields[9] and
            fields[9:12] == (WORKER_P2+SPAN-STACK_BYTES,WORKER_P2+SPAN,WORKER_P2+SPAN),
            'Worker BSS/stack escapes its exact reservation')
    for name,(address,size,kind,owner) in worker[4].items():
        if kind not in (1,2) or not size:
            continue
        physical = address&0x1fffffff
        require(0x0c100000 <= physical < physical+size <= 0x0c100000+SPAN,
                'Worker typed object/function escapes allocation: '+name)
        require(owner < len(worker[3]), 'Worker object lacks a section owner: '+name)
        if kind == 2:
            require(address&0xff000000 == 0x8c000000, 'Worker function uses wrong code alias: '+name)
        elif worker[3][owner][2]&1:
            require(address&0xff000000 == 0xac000000, 'Writable worker object uses cached alias: '+name)
        require(not name.startswith(('_kui_retail_storage_','_kui_sci_sd_acquire','_kui_sci_sd_release',
                                     '_kui_loader_sd_init','_kui_loader_sd_adopt')),
                'Worker independently reinitializes or owns storage: '+name)
    images = {}
    for name,base,limit in (('resident-sci',0x8c004000,0x8c007800),
                            ('stage',0x8ce00000,0x8cfe0000),('entry',0x8c010000,0x8c112000)):
        images[name] = inspect_elf((directory/(name+'.elf')).read_bytes(),base,limit)
        if name != 'entry':
            require(images[name]['payload'] == (directory/(name+'.bin')).read_bytes(), name+' binary differs from ELF')
    stage_binary = images['stage']['payload']
    for begin,end,payload in (('__native_worker_blob_begin','__native_worker_blob_end',binary),
                              ('__native_worker_reloc_begin','__native_worker_reloc_end',wire),
                              ('__retail_resident_sci_blob_start','__retail_resident_sci_blob_end',images['resident-sci']['payload'])):
        a,b = stage[4][begin][0],stage[4][end][0]
        require(b-a == len(payload) and region(stage_binary,a-0x8ce00000,b-a,begin) == payload,
                'Stage embeds different linked artifact: '+begin)
    module = (directory/'retail-maple-observe.kui').read_bytes()
    runtime = verify(module)
    require(module[64:] == images['entry']['payload'], 'Runtime envelope differs from linked entry')
    source = persistent.check_source(directory,runtime)
    if not allow_dirty:
        require(source['source_dirty'] is False, 'Final build was linked from dirty source')
        require(subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip() == source['commit'],
                'Current source differs from compiled commit')
        require(not subprocess.check_output(['git','status','--porcelain'],cwd=ROOT), 'Current source is dirty')
    objdump = shutil.which('sh-elf-objdump')
    require(objdump, 'sh-elf-objdump is required')
    instructions = {}
    disassemblies = {}
    for name in ('resident-sci','stage','entry','native-worker'):
        dis = subprocess.check_output([objdump,'-d',str(directory/(name+'.elf'))],text=True)
        disassemblies[name] = dis
        try:
            instructions[name] = instruction_audit(name,dis)
        except SystemExit as error:
            raise ValueError(str(error)) from error
    low_guard = terminal_guard_model(low,self_test)
    from check_vmu_native_map_refusal import signature, PINS
    require(signature(low,disassemblies['resident-sci'],'_kui_vmu_observe_low_failure')[:2] ==
            PINS['_kui_vmu_observe_low_failure'],
            'Low failure transfer changed from its complete reviewed call/guard body')
    _,menu,_ = symbol_bytes(low,'_kui_retail_menu_return',2)
    require(menu.count(struct.pack('<I',0xa0000000)) == 1,
            'Menu return lacks its unique boot-ROM transfer target')
    coherence = persistent.sdk_coherence_admission(worker,self_test)
    require(coherence['available'], 'Missing actual SDK cold-coherence gates')
    fault = persistent.check_runtime_fault_gateways(worker,binary,wire,self_test)
    guard = persistent.check_cached_stack_guard(worker,self_test)
    publication = persistent.check_resume_publication(stage,True,self_test)
    publisher = publisher_body(stage)
    relocation_count = persistent.check_worker_pointer_aliases(wire,policy,
        persistent.fault_capture_admission(worker,self_test),coherence)
    bus_descriptor = scif_descriptor(low) if transport == 0 else {'transport':'SCI','enabled':False}
    pio = persistent.check_pio_writers(directory,worker)
    gateway_address,gateway,_ = symbol_bytes(stage,'_kui_vmu_native_worker_call',2)
    stage_cases = persistent.model_cached_stage(gateway_address,gateway)
    hook_cases = persistent.model_cached_hooks(binary,wire,symbols)
    installer = persistent.check_stage_stack(stage,low,worker=worker,self_test=self_test)
    from check_native_vmu_only_install import audit as audit_install
    installation = audit_install(stage,low,worker,binary,wire)
    stacks = callgraph_stacks(directory,worker,images['resident-sci'],transport,coherence)
    return {'schema':'kui.native.vmu-only.linked.v1','transport':transport,'lease_bytes':SPAN,
            'controller_submit':submit_mode['controller_submit'],'controller_submit_audit':submit_mode,
            'worker_span':SPAN,'worker_initialized_bytes':len(binary),'worker_stack_bytes':STACK_BYTES,
            'worker_bss_end':hex(fields[8]),'worker_stack_bottom':hex(fields[9]),
            'worker_bss_stack_gap':fields[9]-fields[8], 'module_sha256':sha(module),'runtime':runtime,
            'source_provenance':source,'load_policy':policy,'instruction_counts':instructions,
            'relocations_exact':True,'typed_exports_exact':True,'embedded_artifacts_exact':True,
            'typed_relocation_records':relocation_count,'scif_loan_bus_descriptor':bus_descriptor,
            'prefetch_enabled':False,'background_reader_enabled':False,
            'sdk_cold_coherence':coherence,'runtime_failure_gateways':fault,'cached_stack_guard':guard,
            'low_terminal_guard':low_guard,'low_failure_transfer_signature_verified':True,
            'resume_cache_publication':publication,'worker_cache_publisher':publisher,
            'pio_writer_adapters':pio,'stage_gateway_model_cases':stage_cases,'maple_hook_model_cases':hook_cases,
            'cold_installer_stack':installer,'actual_worker_installation':installation,
            'stack_audit':stacks,'console_execution_verified':False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build',type=Path)
    parser.add_argument('--transport',required=True,choices=('sci','scif'))
    parser.add_argument('--allow-dirty',action='store_true',help='Development audit only; final packages reject this')
    parser.add_argument('--output',type=Path)
    args = parser.parse_args()
    try:
        report = audit(args.build,transport=1 if args.transport == 'sci' else 0,allow_dirty=args.allow_dirty)
        data = json.dumps(report,indent=2)+'\n'
        if args.output:
            args.output.parent.mkdir(parents=True,exist_ok=True)
            args.output.write_text(data)
        print(data,end='')
    except (ValueError,AssertionError,KeyError,FileNotFoundError,subprocess.CalledProcessError) as error:
        parser.exit(2,'VMU-only linked audit refused: '+str(error)+'\n')


if __name__ == '__main__':
    main()
