#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Focused next-test qualification of the unchanged accepted compact worker.

This is deliberately not the historical full persistent audit. The independently
rebuilt pre-optimization references pin the complete worker,
while exact embedding, geometry and source receipts bind its current cold stage.
The mandatory shared-performance gate separately executes the changed cold work.
"""
import hashlib
import json
from pathlib import Path
import struct
import subprocess

from check_loader_layout import inspect_elf
from check_native_background_compact import complete_callgraphs
from check_shared_loader_performance import compact_background_binding
from check_vmu_native_worker_gateway import read_elf
from package_native_shutdown_test import immutable_blob, blob_binding_self_tests
from package_native_vmu_only import bind_profile
from runtime_package import flatten_elf, verify
from vmu_native_relocations import generate

ROOT = Path(__file__).resolve().parents[1]
MAIN = '0045dd96937ee7485ecd4fe0432b28babea8646a'
PRE_SHARED = '3a6ae064bb3f456da8d71587a8db0ce974f139cf'
REFERENCE = {
    1: {
        '888a880a3e2897d49626bde713c8fa862664734a7cb8553dfb3b8b6515451899': PRE_SHARED,
    },
    2: {
        'a4b44a6e126ac5a7ea6b228696e735a54bed2a2810b9f61d915a157ebf0faecf': PRE_SHARED,
    },
}
# Authored only from separately checked-out pre-shared workers, not candidates.
BASELINE_LOAD = {
    1: 'eeb7282bd60edac67a35994784298c750c8b8f47eea8fcb06ca99f3f9f77d2d2',
    2: '93ac4c5352682bdf5398ef9f6bfa78ba6c0d65be9a98723b3a7452912bb7cf30',
}


def require(ok, message):
    if not ok:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def loads(image, build, *, label_required=True, readonly_section_label=False):
    require(len(build) == 12 and all(c in '0123456789abcdef' for c in build), 'Invalid worker build label')
    needle = b'BUILD ' + build.encode() + b'\0'
    chunks, labels = [], []
    for ph in image[2]:
        if ph[0] != 1:
            continue
        raw = image[0][ph[1]:ph[1]+ph[4]]
        if needle in raw:
            require(raw.count(needle) == 1, 'Build label is not unique in its load owner')
            if ph[6] != 5:
                address=ph[2]+raw.index(needle)
                owners=[s for s in image[3] if s[1]==1 and s[2]&3==2 and
                        s[3]<=address<address+len(needle)<=s[3]+s[5]]
                require(readonly_section_label and ph[6]&4 and len(owners)==1,
                        'Build label lacks its exact immutable PROGBITS section owner')
            labels.append(ph[2] + raw.index(needle) + 6)
            raw = raw.replace(needle, b'BUILD ' + bytes(12) + b'\0')
        chunks.append([ph, raw.hex()])
    require(len(labels) == int(label_required), 'Display build label is absent or not unique')
    return chunks, labels


def symbol_records(image, *, omit_slot=False):
    result, omitted = [], 0
    item = struct.Struct('<IIIBBH')
    for section in image[3]:
        if section[1] != 2:
            continue
        require(section[9] == item.size and section[5] % item.size == 0, 'Malformed worker symbol table')
        strings = image[3][section[6]]
        names = image[0][strings[4]:strings[4]+strings[5]]
        source = ''
        for pos in range(section[4], section[4]+section[5], item.size):
            name, address, count, info, other, owner = item.unpack_from(image[0], pos)
            name = names[name:].split(b'\0', 1)[0].decode('ascii')
            if info & 15 == 4:
                source = name
            if omit_slot and name == '__kui_native_virtual_slot_worker':
                require(info == 16 and count == other == 0 and owner == 0xfff1 and address in (1, 2),
                        'Malformed added absolute virtual-slot marker')
                omitted += 1
                continue
            result.append((source if info >> 4 == 0 else '', name, address, count, info, other, owner))
    require(not omit_slot or omitted == 1, 'Added absolute slot marker is not unique')
    return sorted(result)


def worker_equivalence(worker, reference, slot, build):
    require(slot in REFERENCE, 'Unknown compact slot')
    digest = sha(reference[0])
    require(digest in REFERENCE[slot], 'Reference differs from the independent pre-shared-source rebuild')
    commit = REFERENCE[slot][digest]
    require(tuple(x for i,x in enumerate(worker[1]) if i != 6) ==
            tuple(x for i,x in enumerate(reference[1]) if i != 6) and worker[2] == reference[2],
            'Compact worker entry, ABI or complete program headers changed')
    expected, labels = loads(reference, commit[:12])
    actual, candidate_labels = loads(worker, build)
    require(labels == candidate_labels == [0x8c10bece], 'Compact display label moved')
    require(sha(json.dumps(expected, separators=(',', ':')).encode()) == BASELINE_LOAD[slot],
            'Reference differs from the independent pre-shared worker')
    require(actual == expected, 'Compact worker initialized bytes or PT_LOAD geometry changed')
    require(worker[4].get('__kui_native_virtual_slot_worker') == (slot, 0, 0, 0xfff1),
            'Compact worker virtual slot changed')
    require(symbol_records(worker, omit_slot=True) == symbol_records(reference),
            'Compact worker symbol/type/source/ownership metadata changed')
    # Includes every BSS object and executable/immutable section boundary.
    allocated = lambda im: [s for s in im[3] if s[2] & 2]
    require(allocated(worker) == allocated(reference), 'Compact worker allocated section geometry changed')
    return dict(reference_commit=commit, reference_source_tree='c931fc2b2152d3d8428d65edaef6828384774faf',
        reference_build=dict(compiler='sh-elf-gcc 15.2.0',
            makefile='Makefile.vmu-native-background-compact'+('-a1' if slot==1 else ''),
            target='native-worker.elf',additional_compiler_flags=[]),
        historical_compact_profile_main_commit=MAIN,
        main_worker_byte_equivalence_claimed=False,
        reference_elf_sha256=digest, candidate_elf_sha256=sha(worker[0]),
        normalized_initialized_load_sha256=BASELINE_LOAD[slot],
        complete_initialized_bytes_and_headers_identical=True,
        complete_symbol_source_type_ownership_identical=True,
        complete_allocated_and_BSS_geometry_identical=True,
        permitted_changes=dict(display_build_label_address='0x8c10bece',display_build_label_bytes=12,
                               added_absolute_virtual_slot=slot))


def component_equivalence(candidate, reference, *, reference_sha256, old_build, new_build):
    """Compare an already independently authenticated component, without repinning it."""
    require(len(reference_sha256) == 64 and sha(reference[0]) == reference_sha256,
            'Component differs from its independently recorded reference hash')
    require(tuple(x for i,x in enumerate(candidate[1]) if i != 6) ==
            tuple(x for i,x in enumerate(reference[1]) if i != 6) and candidate[2] == reference[2],
            'Component entry, ABI or complete program headers changed')
    old_needle = b'BUILD ' + old_build.encode() + b'\0'
    present = sum(reference[0][p[1]:p[1]+p[4]].count(old_needle) for p in reference[2] if p[0] == 1)
    require(present in (0, 1), 'Reference has ambiguous build display ownership')
    previous, labels = loads(reference, old_build, label_required=bool(present),readonly_section_label=True)
    current, changed_labels = loads(candidate, new_build, label_required=bool(present),readonly_section_label=True)
    require(current == previous and labels == changed_labels,
            'Component initialized bytes/headers changed beyond its display build label')
    require(symbol_records(candidate) == symbol_records(reference),
            'Component source/type/symbol ownership changed')
    require([s for s in candidate[3] if s[2] & 2] == [s for s in reference[3] if s[2] & 2],
            'Component allocated/BSS section layout changed')
    return dict(reference_elf_sha256=reference_sha256, candidate_elf_sha256=sha(candidate[0]),
        normalized_initialized_load_sha256=sha(json.dumps(previous, separators=(',', ':')).encode()),
        complete_initialized_bytes_headers_and_symbol_ownership_identical=True,
        permitted_display_build_label_addresses=[hex(p) for p in labels],
        permitted_display_build_label_bytes=12*present)


def low_stacks(directory, images):
    """Walk both actual emitted low graphs with the established guarded ABI."""
    import check_retail_stack as graph
    from check_vmu_native_background import ROOTS,stack_paths,library_cfg,rows,disassemble
    result={}
    for label,stem,family in (('new_x','resident-sci','resident'),('legacy_y','resident-legacy','legacy')):
        image=images[stem]
        reports=sorted(directory.glob(stem+'.elf.ltrans*.ci')) or sorted((directory/family/'lto').glob('*.ci'))
        timing='__kui_native_loader_timeline_resident' in image[4]
        if timing:
            reports += [directory/'native/src/loader/vmu_native_passive.ci',
                directory/'native/src/core/native_loader_timeline.ci',
                directory/'native/src/core/vmu_native_profile.ci',directory/'native/src/loader/minic.ci']
        frames,calls=graph.load(reports,qualified=timing)
        roots=dict(ROOTS)
        if '__kui_native_sdk_sci_route_resident' in image[4]:
            roots['kui_retail_sdk_sci_dispatch']=0
        switched={('deliver','retail_async.c'):20} if '__kui_native_readahead_resident' in image[4] else None
        paths,callbacks,escapes=stack_paths(frames,calls,roots,switched)
        from check_vmu_native_worker_gateway import symbol_bytes
        address,body,_=symbol_bytes(image,'___udivsi3',2)
        library=library_cfg(rows(disassemble(directory/(stem+'.elf'))),address,len(body))
        total=max(p['bytes'] for p in paths.values())+library['stack_bytes']+32+16+32
        require(total<=352,'Compact low stack exceeds its unchanged352-byte limit')
        result[label]=dict(required_bytes=total,available_bytes=352,guard_bytes=16,
            top_gap_bytes=32,additional_margin_bytes=32,linked_library=library,paths=paths,
            callback_sites=len(callbacks),stack_escape_sites=len(escapes),
            retained_auxiliary_parked_owner_ABI_bytes=20 if switched else 0)
    return result


def audit(directory, *, reference, slot, allow_dirty=False):
    directory, reference = Path(directory), Path(reference)
    graphs = complete_callgraphs(directory)
    images = {name: read_elf(directory/(name+'.elf')) for name in
              ('entry', 'stage', 'native-worker', 'resident-sci', 'resident-legacy', 'native-ra-aux')}
    entry, stage, worker = (images[n] for n in ('entry', 'stage', 'native-worker'))
    record = json.loads((directory/'entry.compile.json').read_text())
    commit = record.get('commit')
    require(isinstance(commit, str) and len(commit) == 40 and
            all(c in '0123456789abcdef' for c in commit) and
            type(record.get('source_dirty')) is bool and record.get('elf_sha256') == sha(entry[0]),
            'Entry source receipt differs from the actual ELF')
    if not allow_dirty:
        require(not record['source_dirty'] and
                subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip() == commit and
                not subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT),
                'Final compact qualification requires its exact clean source checkpoint')
    geometry = compact_background_binding(stage, worker, images['resident-sci'],
        images['resident-legacy'], images['native-ra-aux'], slot)
    baseline = worker_equivalence(worker, read_elf(reference), slot, commit[:12])
    module, profile = bind_profile(directory, commit, 'retail-native-background-compact.kui')
    require((directory/'retail-native-dual.kui').read_bytes() == module, 'Compact dual alias differs')
    payload, memory = flatten_elf(entry[0])
    require(module[64:] == payload and verify(module)['memory_bytes'] == memory,
            'Module envelope differs from its exact entry ELF')
    stage_bytes = (directory/'stage.bin').read_bytes()
    require(inspect_elf(stage[0], 0x8ce00000, 0x8cfe0000)['payload'] == stage_bytes,
            'Cold stage binary differs from its linked ELF')
    immutable_blob(entry, '__retail_stage_blob_start', '__retail_stage_blob_end', stage_bytes)
    for first, last, name in (
        ('__native_worker_blob_begin', '__native_worker_blob_end', 'native-worker.bin'),
        ('__native_worker_reloc_begin', '__native_worker_reloc_end', 'native-worker.reloc'),
        ('__retail_resident_sci_blob_start', '__retail_resident_sci_blob_end', 'resident-sci.bin'),
        ('__retail_resident_legacy_blob_start', '__retail_resident_legacy_blob_end', 'resident-legacy.bin'),
        ('_native_ra_aux_blob_start', '_native_ra_aux_blob_end', 'native-ra-aux.bin'),
        ('_native_ra_aux_reloc_start', '_native_ra_aux_reloc_end', 'native-ra-aux.reloc')):
        immutable_blob(stage, first, last, (directory/name).read_bytes())
    for name, link, span in (('native-worker', 0x8c100000, 0x12000), ('native-ra-aux', 0x8c421000, 0x4000)):
        binary = (directory/(name+'.bin')).read_bytes()
        require(generate(images[name][0], binary, link, span) == (directory/(name+'.reloc')).read_bytes(),
                'ELF/binary/relocation extraction differs: '+name)
    return dict(schema='kui.compact.next-test.qualification.v1', status='PASS',
        source_provenance=record, geometry=geometry, unchanged_worker=baseline,
        canonical_profile=profile, compiler_callgraphs=graphs,
        actual_low_stack_closures=low_stacks(directory,images),
        exact_entry_stage_worker_dual_reader_auxiliary_embed_chain=True,
        worker_blob_mutation_rejections=blob_binding_self_tests(stage,
            '__native_worker_blob_begin','__native_worker_blob_end',(directory/'native-worker.bin').read_bytes()),
        artifact_sha256={name+'.elf':sha(image[0]) for name,image in images.items()},
        full_historical_persistent_audit_executed=False,
        changed_cold_stage_models='Required separately by shared_performance, not claimed by worker equivalence',
        console_execution_verified=False)
