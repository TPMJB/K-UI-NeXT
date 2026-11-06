#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bind the normal-menu outer loader and native child to their linked artifacts.

The report contains hashes, addresses and finite model results, never original
game bytes. Hardware, peripheral timing and durable saving remain console
checks. Final packaging must use clean, freshly compiled source provenance.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tests')]
from check_loader_layout import PH, SH, inspect_elf, region
from check_vmu_native_worker_gateway import read_elf
from check_vc2_ce_vmu_package import payload_at, sections
from check_vmu_native_persistent import check_source
from check_native_vmu_only import audit as child_audit
from runtime_package import flatten_elf, verify
from retail_package import inspect_retail
from check_retail_stack import load as graph_load, base as function_base, LIBRARY

ONE = (1, 0, 0, 0xfff1)
RAW_P1, RAW_P2, HANDOFF, LIMIT, BOTTOM, TOP = (
    0x8c008100, 0xac008100, 0xac008300, 0xac00b800, 0xac00b800, 0xac00c000)
LOADER_PREFIX = struct.pack('<4I', 0x5349554b, 0x31444c4d, 1, 64)
MENU_PREFIX = struct.pack('<4I', 0x5349554b, 0x31554e4d, 1, 304)
RETURN_PREFIX = struct.pack('<4I', 0x5349554b, 0x3154524d, 1, 368)
PHASE_PREFIX = struct.pack('<4I', 0x5349554b, 0x314d4954, 1, 992)
VIDEO_PREFIX = struct.pack('<4I', 0x5349554b, 0x31444956, 1, 80)
PHASE_OUTER_PROFILE = struct.pack('<4I', 0x5349554b, 0x3154504f, 1, 16)
CAPABILITY = struct.pack('<8I', 0x5349554b, 0x314c4e4d, 1, 32, 1, 64, 304, 0)
FOREGROUND = struct.pack('<8I', 0x3146474b, 0x574f4c46, 1, 32, 0, 48, 0, 4)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def unique_blank(payload, prefix, size, label):
    require(payload.count(prefix) == 1, label + ' is absent or ambiguous')
    at = payload.index(prefix)
    require(not at & 3 and payload[at:at + size] == prefix + bytes(size - len(prefix)),
            label + ' is unaligned, populated or truncated')
    return at


def embedded(image, payload, label):
    # The exact complete child blob must occur once, including padding.
    base = image[1][4]
    data = b''
    for segment in image[2]:
        if segment[0] == 1 and segment[2] == base:
            data = image[0][segment[1]:segment[1] + segment[4]]
    require(data.count(payload) == 1, label + ' is not embedded exactly once')
    offset = data.index(payload)
    require(not offset & 3, label + ' embedded bytes are unaligned')
    return base + offset


def module(build, filename, allow_dirty):
    data = (build / filename).read_bytes()
    metadata = verify(data)
    entry = (build / 'entry.elf').read_bytes()
    payload, memory = flatten_elf(entry)
    require(data[64:] == payload and metadata['memory_bytes'] == memory,
            filename + ' differs from its exact linked entry')
    provenance = check_source(build, metadata)
    if not allow_dirty:
        require(not provenance['source_dirty'], 'Final module was compiled from dirty source')
    inspect_retail(data)
    inspected = inspect_elf(entry, 0x8c010000, 0x8c112000)
    require(inspected['payload'] == payload and inspected['memory_end'] == 0x8c010000 + len(payload),
            'Module entry escapes its exact packed reservation')
    symbols = inspected['symbols']
    require(symbols['__retail_map'] == 0x8c011000 and symbols['__retail_stage_blob_start'] == 0x8c012000 and
            symbols['__retail_stage_blob_end'] == 0x8c010000 + len(payload),
            'Module map/stage fixed linked bounds differ')
    instructions = (0xd007, 0x400e, 0xd007, 0xd108, 0xd208, 0x4209, 0x6306, 0x2132,
                    0x7104, 0x4210, 0x8bfa, 0xdf06, 0xd406, 0xd003, 0x402b, 9)
    entry_body = struct.pack('<16H6I', *instructions, 0x500000f0, 0x8c012000, 0x8ce00000,
        len(payload) - 0x2000, 0x8cff0000, 0x8c011000)
    require(payload[:0x100] == entry_body + bytes(0x100 - len(entry_body)),
            'Module copy/jump instructions or their relocation operands differ')
    return data, metadata, provenance


def resident(build, image):
    data, header, segments, rows, symbols = image
    require(header[4] == 0x8c004000, 'Outer low resident entry moved')
    require(symbols.get('__kui_smash_menu_loader_resident') == ONE,
            'Outer low resident lacks its immutable menu-loader marker')
    table = sections(image)
    raw = table.get('.vc2_late_raw')
    require(raw is not None, 'Outer resident has no separate raw-reader section')
    raw_index = rows.index(raw)
    _, kind, flags, address, offset, size, *_ = raw
    require(kind == 1 and flags == 6 and address == RAW_P2 and 0 < size <= 512 and not size & 3,
            'Raw reader section range or permissions differ')
    entry, count, function, owner = symbols['_kui_vc2_ce_vmu_read_card']
    require(entry == RAW_P2 and 0 < count <= size and function == 2 and owner == raw_index,
            'Raw reader linked entry differs')
    loads = [(i, row) for i, row in enumerate(segments) if row[0] == 1]
    require(len(loads) == 2, 'Outer resident needs exactly low and raw load segments')
    raw_load = [(i, row) for i, row in loads if row[2] == RAW_P2]
    require(len(raw_load) == 1, 'Raw reader segment is absent or ambiguous')
    raw_ph, segment = raw_load[0]
    require(segment[1:7] == (offset, RAW_P2, RAW_P1, size, size, 5),
            'Raw reader physical segment differs')
    body = region(data, offset, size, 'raw reader')
    raw_bytes = body + bytes(512 - size)
    require(raw_bytes == (build / 'smash-menu-raw.bin').read_bytes(),
            'Raw reader file differs from its complete linked section')
    # Only the independently checked raw island is removed in memory before
    # applying the established single-reservation low ELF checker.
    filtered = bytearray(data)
    PH.pack_into(filtered, header[5] + raw_ph * PH.size, 0, *segment[1:])
    ignored = list(raw)
    ignored[2] = 0
    SH.pack_into(filtered, header[6] + raw_index * SH.size, *ignored)
    low = inspect_elf(bytes(filtered), 0x8c004000, 0x8c007ea0)
    require(low['payload'] == (build / 'resident-sci.bin').read_bytes(),
            'Ordinary resident payload includes a different linked image')
    require(symbols['__retail_hook_stack_bottom'][0] == 0x8c007ea0 and
            symbols['__retail_hook_stack'][0] == 0x8c008000,
            'Outer guarded reader stack differs')
    require(symbols['__retail_resident_bss_end'][0] <= 0x8c007ea0,
            'Outer BSS reaches its stack')
    firmware, count, object_kind, owner = symbols['_kui_smash_menu_loader_firmware']
    require(count == 36 and object_kind == 1 and rows[owner][1] == 8 and rows[owner][2] & 7 == 3 and
            symbols['__retail_resident_bss_begin'][0] <= firmware < firmware + 36 <=
            symbols['__retail_resident_bss_end'][0], 'Saved firmware escapes zeroed resident BSS')
    stop, stop_bytes, stop_kind, stop_owner = symbols['_kui_smash_menu_loader_stop']
    require(stop_bytes and stop_kind == 2 and rows[stop_owner][2] & 7 == 6 and
            0x8c004000 <= stop < stop + stop_bytes <= 0x8c007ea0,
            'Low diagnostic stop callback escapes the authenticated reader')
    require(symbols['_manifest'][1] == 252 + 48 * 12,
            'Outer retained manifest differs from exact 48-slot object')
    return low['payload'], raw_bytes, dict(raw_p2=hex(RAW_P2), raw_physical=hex(RAW_P1),
        raw_code_bytes=size, raw_padded_bytes=512, firmware_words=9,
        firmware_capture_p2=hex(firmware + 0x20000000),
        diagnostic_stop_p2=hex(stop + 0x20000000),
        bss_end=hex(symbols['__retail_resident_bss_end'][0]),
        private_stack_begin='0x8c007ea0', private_stack_end='0x8c008000')


def handoff(build,returning=False):
    stem='return-loader' if returning else 'handoff'
    prefix='__smash_menu_return_' if returning else '__smash_menu_loader_'
    entry_name='_kui_smash_menu_return_entry' if returning else '_kui_smash_menu_loader_entry'
    runtime_name='_kui_smash_menu_return_runtime' if returning else '_kui_smash_menu_loader_runtime'
    image = read_elf(build / (stem+'.elf'))
    data, header, segments, rows, symbols = image
    marker=symbols.get('__kui_smash_card_session_flow')
    require(marker is None or marker==ONE,'Invalid collection-session runtime marker')
    runtime_size=(420 if returning else 632)+(96 if marker else 0)
    video=symbols.get('__kui_smash_return_phase_video_transport')
    require(video is None or (returning and video==ONE),'Invalid return video transport marker')
    if video:runtime_size+=80
    require(header[4] == HANDOFF and symbols[entry_name][0] == HANDOFF,
            'Handoff must enter fixed P2 code after raw reader')
    require(not any(row[0] in (2, 3, 7) for row in segments), 'Handoff is dynamic, interpreted or TLS')
    loads = [row for row in segments if row[0] == 1]
    require(len(loads) == 1, 'Handoff has unexpected additional load segments')
    row = loads[0]
    _, offset, address, physical, size, memory, flags, _ = row
    require(address == HANDOFF and physical == HANDOFF - 0x20000000 and flags == 7 and
            0 < size <= memory and address + memory == TOP,
            'Handoff code/data/private stack leaves its physical reservation')
    blob = region(data, offset, size, 'handoff load bytes')
    require(blob == (build / (stem+'.bin')).read_bytes(), 'Handoff binary differs from linked load bytes')
    for section in rows:
        if section[2] & 2 and section[5]:
            require(HANDOFF <= section[3] < section[3] + section[5] <= TOP,
                    'Handoff allocated section escapes its P2 reservation')
            if section[1] != 8:
                require(section[3] + section[5] <= HANDOFF + size and
                        payload_at(image, section[3], section[5]) ==
                        region(data, section[4], section[5], 'handoff section'),
                        'Handoff initialized section differs from its load segment')
    require(symbols[prefix+'binary_end'][0] == HANDOFF + size <=
            symbols[prefix+'bss_begin'][0] <=
            symbols[prefix+'bss_end'][0] <= BOTTOM,
            'Handoff initialized bytes/BSS reach its private stack')
    require((symbols[prefix+'stack_bottom'][0], symbols[prefix+'stack_top'][0]) ==
            (BOTTOM, TOP), 'Handoff private stack differs')
    runtime, count, kind, owner = symbols[runtime_name]
    require(count == runtime_size and kind == 1 and rows[owner][1] == 1 and rows[owner][2] & 7 == 3 and
            HANDOFF <= runtime < runtime + runtime_size <= HANDOFF + size and
            payload_at(image, runtime, runtime_size) == bytes(runtime_size),
            'Handoff runtime must be one initialized blank typed object')
    return image, blob, dict(entry=hex(HANDOFF), initialized_bytes=size,
        bss_end=hex(symbols[prefix+'bss_end'][0]), runtime=hex(runtime), runtime_bytes=runtime_size,
        return_plan_offset=0 if returning else 104,return_plan_bytes=368,
        private_stack_begin=hex(BOTTOM), private_stack_end=hex(TOP), normalized_crc32=f'{zlib.crc32(blob):08x}')


def return_stack(build,runtime,child_proof):
    """Close return C plus source helpers and selected low callbacks on2KiB."""
    graphs=sorted((build/'return').rglob('*.ci'))
    require(graphs,'Missing return C compiler stack graphs')
    frames,calls=graph_load(graphs,qualified=True)
    def named(name):
        found=[owner for owner in frames if function_base(owner)==name]
        require(len(found)==1,'Missing/ambiguous return stack frame: '+name)
        return found[0]
    from test_cold_load_scope import audit as cold_scope_audit
    cold_scope=cold_scope_audit(build/'return-loader.elf',self_test=False)
    scope_node='kui_cold_load_call3'
    frames[scope_node]=cold_scope['private_frame_bytes']
    calls[scope_node]=[(named('kui_cold_load_crc32_leaf'),'cold_load_crc32.c:verified-leaf')]
    cold_sites=[dict(function=function,source=label) for function,edges in calls.items()
        for target,label in edges if function_base(target)=='kui_cold_load_call3']
    require(len(cold_sites)==3,'Return cold callback closure lacks both raw extents and full CRC')
    memo={};indirect=[]
    def depth(function,active=()):
        require(function not in active,'Recursion in return C stack graph')
        if function in memo:return memo[function]
        if function not in frames:
            if function_base(function) in ('kui_native_return_cache_off','kui_native_return_enter'):
                # Both authored assembly leaves are executed in the linked
                # runtime model; cache-off has no frame, enter replaces SP.
                return 0,[function+'(modeled assembly leaf)']
            function=named(function_base(function))
        best=(0,[])
        for target,label in calls.get(function,[]):
            if target=='__indirect_call':
                require(function_base(function) in ('kui_smash_menu_return_main','stopped'),
                        'Unreviewed return C indirect stack site: '+function+' '+label)
                match=re.fullmatch(r'smash_menu_return_loader.c:(\d+):\d+',label)
                require(match is not None,'Return C indirect site lacks its exact source owner')
                lines=(ROOT/'src/loader/smash_menu_return_loader.c').read_text().splitlines()
                statement='\n'.join(lines[int(match[1])-1:int(match[1])+2])
                require(any(token in statement for token in ('retire_entry','UINT32_C(0x8c1598c0)',
                        'UINT32_C(0x8c1ad2c8)','UINT32_C(0x8c1ad31a)','read_card(','uintptr_t)entry)')),
                        'Unreviewed fixed return callback: '+statement)
                # Worker proof/gateway, low callbacks and original helper
                # execution are separately bound below; no arbitrary target
                # can become a zero-frame edge here.
                indirect.append(dict(function=function,source=label,statement=statement))
                answer=(8 if 'retire_entry' in statement else 0,[label+'(separate bound)'])
            else:answer=depth(target,active+(function,))
            if answer[0]>best[0]:best=answer
        memo[function]=(frames[function]+best[0],[function+'('+str(frames[function])+')']+best[1])
        return memo[function]
    static,path=depth(named('kui_smash_menu_return_main'))
    require(len(indirect)+len(cold_sites)>=7,'Return C stack closure omitted fixed callbacks')
    require(all(case.get('private_stack_guard_unchanged') is True for case in runtime['cases']),
            'Return C/source-helper model changed private stack guard')
    observed=max(case['observed_private_stack_bytes'] for case in runtime['cases'])
    # The low reader's actual callback graph includes selected SCI providers,
    # sticky refusal/stop paths and no reader-stack switch for these entries.
    low=child_proof['stack_audit']['return_gate_low_paths']
    require(set(low)=={'kui_smash_menu_return_read_card','kui_smash_menu_return_stop'},
            'Incomplete low callback closure for return stack')
    worker=child_proof['stack_audit']['worker_paths'].get('kui_smash_native_return_retire')
    require(worker is not None and worker['conservative_bytes']<=4096,
            'Complete worker retirement proof is missing from its separate4KiB stack closure')
    preflight_graph=build/'native/src/loader/vmu_native_passive.ci'
    preflight_frames,_=graph_load([preflight_graph],qualified=True)
    preflight=[value for name,value in preflight_frames.items()
               if function_base(name)=='kui_smash_native_return_preflight']
    require(preflight==[0],'Phase-zero preflight no longer has the proven zero-byte caller frame')
    low_bytes=max(row['c_frame_bytes'] for row in low.values())
    high=max(static,observed);total=high+low_bytes+64
    require(total<=2048-32,'Return C/helper/low callbacks exceed guarded2KiB stack')
    return dict(return_C_static_bytes=static,return_C_path=path,
        actual_return_C_and_original_helpers_observed_bytes=observed,
        low_callback_paths=low,low_maximum_bytes=low_bytes,conservative_combined_bytes=total,
        assembly_library_margin=64,private_bytes=2048,bottom_guard_bytes=32,
        worker_retirement_separate_4KiB_guarded_stack=True,phase_zero_caller_frame_bytes=8,
        worker_retirement_stack=worker,
        finite_indirect_sites=indirect,cold_call_sites=cold_sites,cold_scope=cold_scope,
        compiler_graph_sha256={str(p.relative_to(build)):sha(p.read_bytes()) for p in graphs},
        limits='Compiler static closure plus actual source-helper/private-stack canaries; IRQ callback timing and physical media are not simulated.')


def return_pair(build,gin=None,child_proof=None):
    from smash_native_menu_resident import inspect as inspect_menu_reader
    low=inspect_menu_reader((build/'resident-sci.elf').read_bytes())
    require(low['low']['payload']==(build/'resident-sci.bin').read_bytes(),
            'Child ordinary reader differs from its independently filtered ELF')
    raw=(build/'smash-return-raw.bin').read_bytes()
    require(raw==low['raw'] and len(raw)==512,'Child return raw shim differs from its exact linked island')
    image,blob,geometry=handoff(build,True)
    stage=read_elf(build/'stage.elf')
    for payload,begin,end in ((raw,'__smash_return_raw_start','__smash_return_raw_end'),
                             (blob,'__smash_return_code_start','__smash_return_code_end')):
        address=embedded(stage,payload,begin)
        require(stage[4][begin][0]==address and stage[4][end][0]==address+len(payload),
                'Child return artifact linked bounds differ: '+begin)
    constants={name:int(value,16) for name,value in re.findall(
        r'#define (KUI_SMASH_RETURN_\w+) UINT32_C\((0x[0-9a-f]+)\)',
        (build/'smash_menu_return_symbols.h').read_text())}
    require(constants==dict(KUI_SMASH_RETURN_CODE_BYTES=len(blob),KUI_SMASH_RETURN_CODE_CRC32=zlib.crc32(blob),
        KUI_SMASH_RETURN_CODE_BSS_END=int(geometry['bss_end'],0),KUI_SMASH_RETURN_RUNTIME_P2=int(geometry['runtime'],0),
        KUI_SMASH_RETURN_RAW_CRC32=zlib.crc32(raw),KUI_SMASH_RETURN_STOP_P2=low['stop_p2']),
        'Return generated constants differ from actual reader and gate bytes')
    for name in ('_kui_smash_native_return_stage_prepare','_kui_smash_native_return_stage_publish',
                 '_kui_smash_native_return_stage_bind'):
        address,size,kind,_=stage[4][name]
        require(kind==2 and size and 0x8ce00000<=address<address+size<=0x8cfe0000,
                'Child lacks its bounded return preparation/publication/binding entry: '+name)
    from test_smash_menu_return_entry import exercise as busy_entry
    cases=[busy_entry(image[0],image[2],image[4],pattern,sr,ccr) for pattern in range(8)
        for sr in (0x40000000,0x40000001,0x60000000,0x60000001)
        for ccr in (0,0x100,0x909,0x109)]
    elf_sha=sha(image[0]);runtime=None;source_proof=None;publication=None
    original_folder=Path(gin).parent if gin else None
    if original_folder and (original_folder/'MDE_US.BIN').is_file():
        mde=(original_folder/'MDE_US.BIN').read_bytes()
        require(sha(mde)=='04f49d583b39bdc710cec6d0a434b5d710b9bccedd539bedda29755135f0fe61',
                'Return proof requires the exact original MDE owner')
        from test_smash_menu_return_runtime import exercise as return_runtime
        runtime=dict(elf_sha256=elf_sha,passed=True,cases=[return_runtime(image[0],image[2],image[4],mde,kind)
            for kind in ('sf_return','psii_return','sf_return_again','busy_irq_progress','busy_timeout',
                         'busy_bl_masked','busy_imask_masked','bad_worker','bad_capsule_crc',
                         'bad_return_seed','changed_retire_pointer','changed_stop_pointer')],model_reexecuted=True)
        from test_smash_menu_return_source import source
        from test_smashpack_native_engine_gin import OWNERS
        files={name:(original_folder/name).read_bytes() for name in OWNERS}
        require(all(sha(files[name])==digest for name,digest in OWNERS.items()),'Original return source owners differ')
        source_proof=source(files)
        from test_smash_menu_return_publication import audit as return_publication
        publication={**return_publication(build,original_folder/'MDE_US.BIN'),'model_reexecuted':True}
    else:
        path=ROOT/'docs/evidence/smash-menu-return-runtime.json'
        runtime=json.loads(path.read_text())
        require(runtime.get('passed') is True and runtime.get('elf_sha256')==elf_sha,
                'Return runtime evidence is stale; rerun with --gin beside the exact original MDE')
        runtime={**runtime,'model_reexecuted':False,'checked_report_sha256':sha(path.read_bytes())}
        source_proof=json.loads((ROOT/'docs/evidence/smash-menu-return-source.json').read_text())
        path=ROOT/'docs/evidence/smash-menu-return-publication.json'
        publication={**json.loads(path.read_text()),'model_reexecuted':False,
                     'checked_report_sha256':sha(path.read_bytes())}
    require(len(runtime['cases'])==12,'Incomplete actual return runtime cases')
    successful=[case for case in runtime['cases'] if case.get('fresh_outer_cold_entry')]
    require([case['case'] for case in successful]==['sf_return','psii_return','sf_return_again','busy_irq_progress'] and
        all([(call['lba'],call['blocks']) for call in case['raw_calls']]==[(100,1),(101,1),(200,2)]
            for case in successful),'Successive return models lack fresh outer entries and grouped reads')
    waits={case['case']:case for case in runtime['cases'] if case['case'].startswith('busy_')}
    require(set(waits)=={'busy_irq_progress','busy_timeout','busy_bl_masked','busy_imask_masked'} and
            waits['busy_irq_progress']['retirement_calls']==2 and
            waits['busy_irq_progress']['irq_wait_visits']==1 and
            all(waits[name].get('busy_state_preserved') and waits[name].get('no_old_cleanup_resumed')
                for name in ('busy_timeout','busy_bl_masked','busy_imask_masked')) and
            [waits[name]['reason'] for name in ('busy_timeout','busy_bl_masked','busy_imask_masked')]==[13,12,12],
            'Busy return must drain via owner IRQ context or stop without old cleanup or owner mutation')
    require(source_proof.get('passed') and source_proof.get('reset_entry')=='0x8c0117f0' and
            source_proof.get('reset_source_crc32')=='d71c4066',
            'Return is not bound to the original reset entry before cleanup/GIN')
    require(publication.get('passed') is True and
            publication.get('stage_elf_sha256')==sha(stage[0]) and
            publication.get('gate_elf_sha256')==elf_sha and
            publication.get('worker_elf_sha256')==sha((build/'native-worker.elf').read_bytes()) and
            all(publication.get(key) is True for key in ('original_firmware_captured_before_patch',
                'raw_and_code_exact','initialized_runtime_exact','private_stack_zeroed',
                'metadata_resealed_after_worker_bind')) and
            publication.get('reset_patch_before_cleanup')=='0x8c0117f0' and
            publication.get('native_return_seed_delta')==304,
            'Actual return prepare/publish/worker-bind proof is incomplete or stale')
    return dict(geometry=geometry,elf_sha256=elf_sha,raw_sha256=sha(raw),raw_bytes=512,
        busy_assembly_cases=len(cases),busy_instruction_counts=sorted(set(cases)),
        original_owner_frame_preserved=True,no_old_cleanup_caller_resumed=True,
        owner_irq_context_restored_between_retries=True,
        runtime=runtime,original_reset_source=source_proof,stage_publication_and_worker_bind=publication,
        private_stack_closure=return_stack(build,runtime,child_proof),
        boundary='Actual return gate/entry and original pure helpers; worker/raw I/O are separate bounded contracts, not SDK/game/hardware execution')


def combined_stacks(build):
    """Walk separate actual low/handoff graphs with finite cross-image calls.

    The new redirect is reachable while a GD request uses the 352-byte low
    stack. It must not inherit the old file-wide map_guest-only callback set.
    Local names and duplicate public CRC helpers keep their image ownership.
    """
    low_graphs = sorted(build.glob('resident-sci.elf.ltrans*.ci'))
    high_graphs = sorted((build / 'handoff').rglob('*.ci'))
    require(low_graphs and high_graphs, 'Missing compiler graphs for cross-image stack closure')
    frames, calls = {}, {}
    for scope, graphs in (('low', low_graphs), ('handoff', high_graphs)):
        for path in graphs:
            require(path.read_text().rstrip().endswith('}'), 'Truncated compiler stack graph')
        added_frames, added_calls = graph_load(graphs, qualified=True, scope=scope)
        frames.update(added_frames)
        calls.update(added_calls)

    def named(scope, name):
        found = [function for function in frames if function.startswith(scope + ':') and
                 function_base(function) == name]
        require(len(found) == 1, 'Missing or ambiguous scoped compiler function: ' + scope + ':' + name)
        return found[0]

    # The shared assembly scope has a proved16-byte ABI frame. Its only
    # source-owned callbacks here are the raw reader, CRC leaf and pure copy;
    # charge the deeper branch conservatively at each scoped call site.
    from test_cold_load_scope import audit as cold_scope_audit
    cold_scope=cold_scope_audit(build/'handoff.elf',self_test=False)
    scope_node='handoff:kui_cold_load_call3'
    frames[scope_node]=cold_scope['private_frame_bytes']
    calls[scope_node]=[(named('low','kui_vc2_ce_vmu_read_card'),'cold-load-raw'),
                       (named('handoff','kui_cold_load_crc32_leaf'),'cold-load-crc'),
                       (named('handoff','memcpy'),'cold-image-preserve-copy')]

    def source(label):
        match = re.fullmatch(r'([^:]+):(\d+):\d+', label)
        require(match is not None, 'Indirect call lacks exact source-qualified site')
        relative, line = match[1], int(match[2])
        # The shared graph parser intentionally keeps only the basename in
        # labels. These reviewed source owners are unique for this closure.
        owners = {'retail_gd.c': 'src/core/retail_gd.c',
            'sci_stream.c': 'src/loader/sci_stream.c', 'retail_cursor.c': 'src/core/retail_cursor.c',
            'retail_async.c': 'src/loader/retail_async.c', 'smash_menu_loader.c': 'src/loader/smash_menu_loader.c'}
        require(relative in owners, 'Unreviewed indirect source owner: ' + relative)
        lines = (ROOT / owners[relative]).read_text().splitlines()
        require(0 < line <= len(lines), 'Indirect call source line is out of range')
        return Path(relative).name, '\n'.join(lines[line - 1:line + 2])

    indirect_sites = {}

    def indirect(function, label):
        scope = function.split(':', 1)[0]
        base = function_base(function)
        filename, statement = source(label)
        choices = None
        if scope == 'low':
            if filename == 'retail_gd.c' and base in ('guest', 'dma_guest') and 's->ops.map' in statement:
                choices = [('low', 'map_guest')]
            elif filename == 'retail_gd.c' and base == 'kui_retail_gd_dispatch' and 's->ops.check' in statement:
                choices = [('low', 'check_sectors')]
            elif filename == 'sci_stream.c' and base == 'byte' and 'bus.transfer' in statement:
                choices = [('low', 'transfer')]
            elif filename == 'sci_stream.c' and base == 'select' and 'bus.select' in statement:
                choices = [('low', 'select_card')]
            elif filename == 'retail_cursor.c' and base == 'deliver' and 'write' in statement:
                choices = [('low', 'write_out')]
            elif filename == 'retail_async.c' and base == 'native_payload_write':
                if 's->ops.map' in statement:
                    choices = [('low', 'map_guest')]
                elif 'KUI_SMASH_MENU_LOADER_REDIRECT_P2' in statement:
                    choices = [('handoff', 'kui_smash_menu_loader_redirect')]
        elif scope == 'handoff' and filename == 'smash_menu_loader.c':
            if base == 'kui_smash_menu_loader_main' and 'read_card(' in statement:
                choices = [('low', 'kui_vc2_ce_vmu_read_card')]
            elif base == 'stopped' and 'entry)' in statement:
                choices = [('low', 'kui_smash_menu_loader_stop')]
        require(choices is not None, 'Unreviewed cross-image indirect stack site: ' + function + ' ' + label)
        selected = [named(*choice) for choice in choices]
        indirect_sites[function + ' ' + label] = selected
        return selected

    memo = {}

    def depth(function, path=()):
        require(function not in path, 'Recursion in combined stack closure: ' + function)
        if function in memo:
            return memo[function]
        if function not in frames:
            # LTO can emit a folded local alias without another frame node.
            # Keep the image domain and take its deepest emitted same-base
            # owner, as the existing low-stack auditor does.
            scope = function.split(':', 1)[0]
            twins = [candidate for candidate in frames if candidate.startswith(scope + ':') and
                     function_base(candidate) == function_base(function)]
            if twins:
                return max((depth(candidate, path) for candidate in twins), key=lambda row: row[0])
            require(function_base(function) in LIBRARY, 'Missing actual stack frame: ' + function)
            return 0, [function + '(library reserve)']
        deepest, chain = 0, []
        for target, label in calls.get(function, []):
            targets = indirect(function, label) if target == '__indirect_call' else [target]
            for callee in targets:
                value, candidate = depth(callee, path + (function,))
                if value > deepest:
                    deepest, chain = value, candidate
        result = frames[function] + deepest, [function + '(' + str(frames[function]) + ')'] + chain
        memo[function] = result
        return result

    roots = {}
    for scope, name in (('low', 'kui_retail_resident_dispatch'), ('low', 'kui_retail_menu_return'),
                        ('handoff', 'kui_smash_menu_loader_redirect'), ('handoff', 'kui_smash_menu_loader_main')):
        count, chain = depth(named(scope, name))
        roots[scope + ':' + name] = dict(bytes=count, path=chain)
    low_depth = max(roots['low:' + name]['bytes'] for name in
                    ('kui_retail_resident_dispatch', 'kui_retail_menu_return'))
    high_depth = roots['handoff:kui_smash_menu_loader_main']['bytes']
    redirect = named('handoff', 'kui_smash_menu_loader_redirect')

    def through(function, active=()):
        if function == redirect:
            return depth(function)
        require(function not in active, 'Recursion in redirect stack path')
        best = None
        for target, label in calls.get(function, []):
            targets = indirect(function, label) if target == '__indirect_call' else [target]
            for callee in targets:
                candidate = through(callee, active + (function,))
                if candidate is not None and (best is None or candidate[0] > best[0]):
                    best = candidate
        if best is None:
            return None
        return frames[function] + best[0], [function + '(' + str(frames[function]) + ')'] + best[1]

    redirect_path = through(named('low', 'kui_retail_resident_dispatch'))
    require(redirect_path is not None and redirect_path[0] <= low_depth,
            'Private GD root never reaches the authenticated cross-image redirect')
    require(low_depth + 64 <= 352 - 16, 'Combined low reader/redirect exceeds guarded 352-byte stack')
    require(high_depth + 64 <= 2048 - 16, 'Handoff C/media/stop closure exceeds guarded 2 KiB stack')
    require(any('kui_smash_menu_loader_redirect' in callee for targets in indirect_sites.values() for callee in targets),
            'Combined stack proof never reaches the actual cross-image redirect')
    return dict(roots=roots, low_private_bytes=352, low_deepest_bytes=low_depth,
        private_GD_redirect_path=dict(bytes=redirect_path[0], path=redirect_path[1]),
        handoff_private_bytes=2048, handoff_deepest_bytes=high_depth,
        library_and_guard_reserve=64, top_reserved_bytes=16,
        image_and_source_qualified_callbacks=indirect_sites,
        cold_scope=cold_scope,
        compiler_graph_sha256={str(path.relative_to(build)): sha(path.read_bytes())
            for path in low_graphs + high_graphs},
        limits='Finite compiler graph closure; library helpers use the existing conservative reserve, not timing simulation')


def audit(outerbuild, childbuild, allow_dirty=False, runtime=None, runtime_elf=None, gin=None):
    require(__debug__, 'Run linked instruction models without Python optimization (-O)')
    require(not runtime_elf or runtime is not None or (Path(outerbuild) / 'runtime.kui').is_file(),
            'An explicit runtime ELF requires a runtime package to bind')
    require(runtime is None or Path(runtime).is_file(), 'Explicit runtime package is missing')
    outerbuild, childbuild = Path(outerbuild).resolve(), Path(childbuild).resolve()
    outer_module, outer_metadata, outer_source = module(outerbuild, 'retail-smash-menu.kui', allow_dirty)
    child_module, child_metadata, child_source = module(childbuild, 'retail-smash-native-menu.kui', allow_dirty)
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    require(outer_source['commit'] == child_source['commit'] == commit,
            'Outer and native child were compiled from different source commits')
    if not allow_dirty:
        require(not subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT),
                'Final audit requires clean source')
    outer_entry, outer_stage, outer_low = [read_elf(outerbuild / (name + '.elf'))
        for name in ('entry', 'stage', 'resident-sci')]
    require(outer_stage[4].get('__kui_smash_menu_loader_stage') == ONE,
            'Outer stage lacks its immutable menu-loader marker')
    stage = inspect_elf(outer_stage[0], 0x8ce00000, 0x8cfe0000)['payload']
    require(stage == (outerbuild / 'stage.bin').read_bytes(), 'Outer stage binary differs from ELF')
    low, raw, low_report = resident(outerbuild, outer_low)
    handoff_image, handoff_blob, handoff_report = handoff(outerbuild)
    embedded(outer_entry, stage, 'outer stage')
    low_at = embedded(outer_stage, low, 'outer low resident')
    raw_at = embedded(outer_stage, raw, 'outer raw reader')
    handoff_at = embedded(outer_stage, handoff_blob, 'outer surviving handoff')
    for begin, end, address, blob in (
        ('__retail_resident_sci_blob_start', '__retail_resident_sci_blob_end', low_at, low),
        ('__smash_menu_raw_start', '__smash_menu_raw_end', raw_at, raw),
        ('__smash_menu_handoff_start', '__smash_menu_handoff_end', handoff_at, handoff_blob)):
        require(outer_stage[4][begin][0] == address and outer_stage[4][end][0] == address + len(blob),
                'Embedded blob linked bounds differ: ' + begin)
    require(stage.count(CAPABILITY) == 1, 'Outer loader capability differs or is ambiguous')
    require(stage.count(FOREGROUND[:8]) == 1 and stage.count(FOREGROUND) == 1,
            'Outer reader needs its unique Standard/48-slot/flag4 collection route')
    foreground, foreground_bytes, foreground_kind, _ = outer_stage[4]['_kui_native_foreground_reader_capability']
    require(foreground_bytes == 32 and foreground_kind == 1 and not foreground & 3 and
            payload_at(outer_stage, foreground, 32) == FOREGROUND,
            'Typed foreground collection capability differs')
    cap_address, cap_bytes, cap_kind, cap_owner = outer_stage[4]['_kui_smash_menu_loader_capability']
    require(cap_bytes == 32 and cap_kind == 1 and not cap_address & 3 and
            payload_at(outer_stage, cap_address, 32) == CAPABILITY,
            'Typed outer capability differs from its one linked descriptor')
    loader_seed = unique_blank(outer_module[64:], LOADER_PREFIX, 64, 'outer loader seed')
    menu_seed = unique_blank(child_module[64:], MENU_PREFIX, 304, 'native menu selection seed')
    outer_return_seed=unique_blank(outer_module[64:],RETURN_PREFIX,368,'outer restart seed')
    child_return_seed=unique_blank(child_module[64:],RETURN_PREFIX,368,'child restart seed')
    require(child_return_seed==menu_seed+304,
            'Child restart plan is not exactly adjacent to its menu seed')
    require(MENU_PREFIX not in outer_module[64:] and LOADER_PREFIX not in child_module[64:],
            'Outer and child seed ownership is mixed')
    require(struct.pack('<4I', 0x5349554b, 0x31444843, 1, 32) not in child_module[64:],
            'Menu child contains the unrelated fixed developer-child seed')
    constants = {name: int(value, 16) for name, value in re.findall(
        r'#define (KUI_SMASH_MENU_\w+) UINT32_C\((0x[0-9a-f]+)\)',
        (outerbuild / 'smash_menu_loader_symbols.h').read_text())}
    expected = {'KUI_SMASH_MENU_FIRMWARE_P2': int(low_report['firmware_capture_p2'], 0),
        'KUI_SMASH_MENU_STOP_P2': int(low_report['diagnostic_stop_p2'], 0),
        'KUI_SMASH_MENU_RAW_CRC32': zlib.crc32(raw),
        'KUI_SMASH_MENU_HANDOFF_CRC32': zlib.crc32(handoff_blob),
        'KUI_SMASH_MENU_HANDOFF_BYTES': len(handoff_blob),
        'KUI_SMASH_MENU_HANDOFF_END': HANDOFF + len(handoff_blob),
        'KUI_SMASH_MENU_HANDOFF_BSS_END': int(handoff_report['bss_end'], 0),
        'KUI_SMASH_MENU_RUNTIME_P2': int(handoff_report['runtime'], 0)}
    if '_kui_smash_menu_loader_phase_video' in handoff_image[4]:
        expected['KUI_SMASH_MENU_PHASE_VIDEO_P2']=handoff_image[4]['_kui_smash_menu_loader_phase_video'][0]
    require(constants == expected, 'Generated outer constants differ from actual low/helper ELFs')
    redirect_address = handoff_image[4]['_kui_smash_menu_loader_redirect'][0]
    redirect_header = (outerbuild / 'smash_menu_handoff_symbols.h').read_text()
    redirect_constants = re.findall(r'#define KUI_SMASH_MENU_LOADER_REDIRECT_P2 UINT32_C\((0x[0-9a-f]+)\)',
                                   redirect_header)
    require(redirect_constants == [f'0x{redirect_address:08x}'] and
            HANDOFF <= redirect_address < HANDOFF + len(handoff_blob) and
            struct.pack('<I', redirect_address) in low,
            'Outer reader redirect target differs from its linked handoff function')
    child_proof = child_audit(childbuild, transport=1, allow_dirty=allow_dirty)
    require(child_proof['smash_native_child_profile']['menu_capture'] and
            child_proof['smash_native_child_profile']['virtual_slot'] == 1 and
            child_proof['variant'] == 'vmu-lite' and
            child_proof['prefetch_enabled'] is False and child_proof['background_reader_enabled'] is False,
            'Native child is not the exact synchronous shared-A1 menu profile')
    from test_smash_native_return_entry import audit as retirement_gateway
    retirement=retirement_gateway(childbuild/'native-worker.elf')
    from test_smash_native_cached_replay import audit as cached_replay
    replay=cached_replay(childbuild/'stage.elf')
    require(replay.get('actual_instruction_model_cases')==3840 and replay.get('mutation_rejections')==21,
            'Menu delivery requires the complete scoped cached-replay execution/mutation baseline')
    from test_smash_native_fast_crc import audit as fast_crc
    fast=fast_crc(childbuild/'stage.elf')
    require(fast['actual_SH_cases']==128 and fast['one_table_read_per_byte'] and fast['whole_image_relay_bound'],
            'Menu delivery requires actual linked byte-table full-file CRC execution')
    from test_smash_native_fat_cache_linked import audit as linked_fat_cache
    from test_smash_fat_read_cache import audit as fresh_fat_admission
    from test_smash_fat_read_cache_mutations import audit as fat_mutations
    fat=dict(linked_stage=linked_fat_cache(childbuild/'stage.elf'),
             full_admission=fresh_fat_admission(),unsafe_mutations=fat_mutations())
    require(fat['linked_stage']['status']=='PASS' and fat['full_admission']['status']=='PASS' and
            fat['full_admission']['fat32_and_exfat'] and
            fat['full_admission']['full_fresh_admission'] and
            fat['unsafe_mutations'].get('status')=='PASS' and
            fat['unsafe_mutations'].get('mutation_rejections')==5,
            'Menu delivery requires linked FAT batching with unchanged fresh FAT32/exFAT admission')
    # Execute the actual linked entry's integer capture/fallback separately
    # from the C selection/media path, with the model's stated boundary.
    from test_smash_menu_loader_entry import exercise
    require([struct.unpack('<H', payload_at(handoff_image, HANDOFF + 2 * i, 2))[0]
             for i in range(15)] == [0x2f06 | i << 4 for i in range(15)] and
            [struct.unpack('<H', payload_at(handoff_image, HANDOFF + 30 + 2 * i, 2))[0]
             for i in range(6)] == [0x4f22, 0x4f03, 0x4f23, 0x4f13, 0x4f02, 0x4f12],
            'Handoff changes SR/register bank before capturing the active integer frame')
    entry_cases = [exercise(handoff_image[0], handoff_image[2], handoff_image[4], pattern, ccr)
        for pattern in range(8) for ccr in (0, 0x100, 0x909, 0x109)]
    from test_smash_menu_loader_runtime import exercise as runtime_case, exercise_redirect
    runtime_cases = [runtime_case(handoff_image[0], handoff_image[2], handoff_image[4], kind)
        for kind in ('phantasy_star_ii', 'shining_force', 'phantasy_star_ii_c0_zero',
                     'phantasy_star_ii_c0_rom', 'phantasy_star_ii_after_menu_return',
                     'invalid_return_plan','malformed_argv', 'raw_probe_busy',
                     'bad_capsule_crc', 'nonnative_fallback')]
    grouped=[]
    for case in runtime_cases:
        if case.get('cold_entry_reached'):
            require([(call['lba'],call['blocks']) for call in case['raw_calls']]==[(100,1),(101,1),(200,2)],
                    'Capsule delivery regressed to per-sector sessions or lost the pre-overwrite probe')
            grouped.append(case['case'])
    require(len(grouped)==5,'Grouped-read performance baseline lacks successful selection/return cases')
    redirect_cases = []
    if gin is not None:
        gin_bytes = Path(gin).read_bytes()
        require(sha(gin_bytes) == 'fa12196993a7c757a78565963e257cd33443f80b3f226729fdc1cf7da51ed5b3',
                'External GIN does not match reviewed source')
        redirect_cases = [exercise_redirect(handoff_image[0], handoff_image[2], handoff_image[4], gin_bytes, kind)
            for kind in ('matched_gin', 'wrong_code_crc', 'partial_range', 'invalid_runtime')]
    stacks = combined_stacks(outerbuild)
    returning=return_pair(childbuild,gin,child_proof)
    timing = dict(enabled=False,status='DISABLED')
    if '_kui_smash_menu_loader_phase_trace' in handoff_image[4]:
        phase_seed=unique_blank(child_module[64:],PHASE_PREFIX,992,'diagnostic phase trace')
        require(phase_seed==menu_seed+304+368,
                'Diagnostic trace is not exactly after the two working child seeds')
        require(PHASE_PREFIX not in outer_module[64:],
                'Outer payload must not own the child diagnostic trace seed')
        video_seed=unique_blank(child_module[64:],VIDEO_PREFIX,80,'diagnostic original video seed')
        require(video_seed==phase_seed+992 and outer_module[64:].count(PHASE_OUTER_PROFILE)==1,
                'Diagnostic video seed/outer profile is missing, ambiguous or misplaced')
        from test_smash_phase_clock_linked import audit as clock_model
        from test_smash_menu_loader_phase import audit as low_phase_model
        from test_smash_phase_clock import audit as host_clock_model
        from test_smash_phase_trace import audit as host_trace_model
        from test_smash_phase_stage import audit as high_phase_model
        from test_smash_phase_video_seed import audit as video_seed_model
        from test_smash_phase_video_publish import audit as video_publish_model
        from test_smash_phase_video import audit as video_renderer_model
        timing=dict(enabled=True,status='PASS',trace_bytes=992,trace_offset=phase_seed,
            working_loader_runtime_bytes=632,working_return_runtime_bytes=420,
            clock_handoff=clock_model(outerbuild/'handoff.elf',require_delta=False),
            clock_child_stage=clock_model(childbuild/'stage.elf'),
            host_clock=host_clock_model(),host_trace=host_trace_model(),
            high_stage=high_phase_model(childbuild),
            original_video_seed=video_seed_model(outerbuild/'retail-smash-menu.kui',childbuild/'retail-smash-native-menu.kui'),
            original_video_publication=video_publish_model(outerbuild),
            original_video_renderer=video_renderer_model(outerbuild,childbuild),
            forward_low=low_phase_model(outerbuild/'handoff.elf'))
        require(timing['clock_handoff']['status']=='PASS' and
                timing['clock_child_stage']['status']=='PASS' and timing['forward_low'].get('passed'),
                'Diagnostic timing requires both linked clocks and the complete forward mark model')
        require(timing['host_clock']['status']=='PASS' and timing['host_trace']['status']=='PASS',
                'Diagnostic timing requires rollover/refusal and typed trace/order host checks')
        require(timing['high_stage']['status']=='PASS',
                'Diagnostic timing requires actual high-stage import, marker and terminal report execution')
        require(all(timing[key]['status']=='PASS' for key in
                    ('original_video_seed','original_video_publication','original_video_renderer')),
                'Diagnostic timing requires typed original-video carry, actual readback and scanout/renderer execution')
    else:
        require(PHASE_PREFIX not in child_module[64:] and VIDEO_PREFIX not in child_module[64:] and
                PHASE_OUTER_PROFILE not in outer_module[64:],
                'Diagnostic child trace requires the matching diagnostic outer loader')
    artifacts = {}
    for prefix, build in (('outer', outerbuild), ('child', childbuild)):
        for path in sorted(build.rglob('*')):
            if path.is_file() and (path.suffix in ('.elf', '.bin', '.kui', '.reloc') or
                    path.name in ('entry.compile.json', 'smash_menu_loader_symbols.h',
                                  'smash_menu_handoff_symbols.h','smash_menu_return_symbols.h')):
                artifacts[prefix + '/' + str(path.relative_to(build))] = sha(path.read_bytes())
    runtime_report = {'bound': False}
    runtime = Path(runtime) if runtime else outerbuild / 'runtime.kui'
    if runtime.is_file():
        runtime_elf = Path(runtime_elf) if runtime_elf else ROOT / 'build/kui-runtime.elf'
        data, provenance = runtime.read_bytes(), json.loads(runtime_elf.with_suffix('.compile.json').read_text())
        metadata = verify(data)
        payload, memory = flatten_elf(runtime_elf.read_bytes())
        require(data[64:] == payload and metadata['memory_bytes'] == memory and
                metadata['build'] == commit[:12] and provenance['commit'] == commit and
                provenance['elf_sha256'] == sha(runtime_elf.read_bytes()) and
                type(provenance.get('source_dirty')) is bool,
                'Runtime envelope, ELF or source provenance differs from matching outer/child build')
        require(allow_dirty or provenance['source_dirty'] is False, 'Runtime was built from dirty source')
        artifacts['outer/runtime.kui'] = sha(data)
        runtime_report = dict(bound=True, sha256=sha(data), elf_sha256=sha(runtime_elf.read_bytes()),
            metadata=metadata, source_provenance=provenance)
    source_paths = ['Makefile.smash-menu-loader', 'Makefile.smash-native-menu',
        'include/kui/smash_menu_loader.h', 'include/kui/smash_menu_loader_capability.h',
        'include/kui/smash_native_menu.h','include/kui/smash_menu_loader_runtime.h',
        'include/kui/smash_menu_return.h','include/kui/smash_native_return.h',
        'include/kui/smash_menu_return_loader.h','include/kui/smash_fat_read_cache.h',
        'src/loader/smash_menu_loader.c', 'src/loader/smash_menu_loader_entry.S',
        'src/loader/smash_menu_loader.ld', 'src/loader/smash_menu_loader_stage.inc',
        'src/loader/smash_menu_return_loader.c','src/loader/smash_menu_return_loader.ld',
        'src/loader/smash_menu_return_entry.S','src/loader/smash_menu_return_raw.inc',
        'src/loader/smash_native_return_stage.inc','src/loader/smash_native_menu_resident.ld',
        'src/loader/native_return_hardware.h','src/loader/native_return_hardware.c',
        'src/loader/native_return_hardware.S','src/loader/stage_fast_crc32.h',
        'src/loader/smash_fat_read_cache.c',
        'include/kui/native_standalone_foreground.h', 'src/loader/native_standalone_reader_stage.inc',
        'src/loader/retail_stage.c', 'src/loader/retail_stage.S', 'src/loader/retail_resident.c',
        'src/loader/retail_async.c',
        'src/loader/vmu_native_passive.c','src/loader/vmu_native_passive_hooks.S',
        'src/loader/smash_native_vmu_probe.inc',
        'src/loader/smash_native_child_stage.inc', 'src/loader/vmu_native_heap_probe_stage.c',
        'src/loader/vmu_native_persist_stage.inc',
        'src/core/vmu_fs_admission.c','src/loader/vmu_persist_stage.c','src/core/vmu_persist_session.c',
        'src/core/vmu_durable.c','src/core/vmu_store.c',
        'src/core/smash_menu_loader.c', 'src/core/smash_native_menu.c','src/core/smash_menu_return.c',
        'src/apps/smash_menu_capsule_prepare.inc', 'src/apps/smash_menu_capsule_file_prepare.inc',
        'src/apps/smash_menu_capsule_pair_prepare.inc','include/kui/smash_launch_timing.h',
        'src/apps/smash_launch_timing.c',
        'src/core/smash_menu_capsule.c', 'tools/smash_menu_loader_symbols.py',
        'tools/smash_menu_return_symbols.py','tools/smash_native_menu_resident.py',
        'tools/check_smash_menu_loader.py']
    if timing['enabled']:
        source_paths += ['include/kui/smash_phase_clock.h','include/kui/smash_phase_trace.h',
                         'include/kui/smash_phase_video.h','src/core/smash_phase_clock.c',
                         'src/core/smash_phase_trace.c','src/core/smash_phase_video.c']
    return dict(schema='kui.smash-menu.linked.v1', status='PASS', commit=commit,
        hardware_verified=False, gameplay_verified=False, save_persistence_verified=False,
        artifacts_sha256=artifacts, outer_source_provenance=outer_source, child_source_provenance=child_source,
        outer_runtime=outer_metadata, child_runtime=child_metadata, runtime=runtime_report,
        low_reader=low_report, handoff=handoff_report,
        foreground_capability=dict(reader='Standard', slots=48, flags=4, lease_bytes=0),
        embedded=dict(low=hex(low_at), raw=hex(raw_at), handoff=hex(handoff_at)),
        seeds=dict(outer_bytes=64, outer_offset=loader_seed, child_bytes=304, child_offset=menu_seed,
            outer_return_offset=outer_return_seed,child_return_offset=child_return_seed,return_bytes=368,
            blank_unique_and_owned=True, physical_capsule_crc_covers_padding=True),
        integer_capture_fallback=dict(cases=len(entry_cases), instruction_counts=sorted(set(entry_cases)),
            boundary='Actual linked assembly; C fallback is an authored ABI adversary'),
        actual_loader_C_and_entry=dict(cases=runtime_cases, gin_redirect_cases=redirect_cases,
            elf_sha256=sha(handoff_image[0]),
            boundary='Raw I/O uses an authored four-block fixture; MMIO is idle, OCBP counted; final cold/diagnostic entries stop'),
        combined_stack_audit=stacks,
        worker_retirement_gateway=retirement,scoped_cached_native_replay=replay,
        native_to_menu_return=returning,
        timing_diagnostic=timing,
        performance_delivery_requirements=dict(enforced=True,successful_grouped_read_cases=grouped,
            raw_sessions_for_four_block_two_run_fixture=3,probe_before_overwrite=True,
            native_discovery_cached_scope_cases=3840,native_discovery_mutants_rejected=21,
            linked_full_file_fast_crc=fast,fresh_fat_read_window=fat,hardware_wall_time_measured=False),
        child_linked_audit=child_proof,
        source_sha256={name: sha((ROOT / name).read_bytes()) for name in source_paths},
        proof_python_dependencies_sha256={str(Path(loaded.__file__).resolve().relative_to(ROOT)):
            sha(Path(loaded.__file__).read_bytes()) for loaded in tuple(sys.modules.values())
            if getattr(loaded, '__file__', None) and Path(loaded.__file__).suffix == '.py' and
            Path(loaded.__file__).resolve().is_relative_to(ROOT)},
        limits=['No console, SD peripheral timing, SDK/game execution or durable save result is established.',
            'Artifact and source hashes bind inputs; source/compiler provenance still depends on trusted fresh build receipts.',
            'Per-game high-score persistence is unclassified; SRAM eligibility and shared settings are separate.'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('outer', type=Path)
    parser.add_argument('child', type=Path)
    parser.add_argument('--allow-dirty', action='store_true')
    parser.add_argument('--runtime', type=Path)
    parser.add_argument('--runtime-elf', type=Path)
    parser.add_argument('--gin', type=Path, help='Optional exact original GINXFER.BIN for positive linked redirect checks')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    report = audit(args.outer, args.child, args.allow_dirty, args.runtime, args.runtime_elf, args.gin)
    rendered = json.dumps(report, indent=2) + '\n'
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered)
    print(rendered, end='')


if __name__ == '__main__':
    main()
