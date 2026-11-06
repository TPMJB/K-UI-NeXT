#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package native compatibility loaders and an optional matching GUI runtime.

This binds the supplied modules and linked artifacts. It does not rerun the
historical complete compatibility auditors or establish console execution.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import zipfile

from check_loader_layout import inspect_elf
from check_native_dual_readers import compact_component_markers
from check_vmu_native_worker_gateway import read_elf
from native_runtime_layout import BACKGROUND_COMPACT, layout_for
from package_native_compat_test import background_module
from package_native_vmu_only import bind_profile, checkout_script
from package_physical_disc_test import collect_vmu_build, ordinary, provenance, require, sha, wire
from package_vmu_native_dual_delivery import offline_source_recovery_check
from runtime_package import envelope, flatten_elf, verify
from vmu_native_relocations import generate

ROOT = Path(__file__).resolve().parents[1]
TARGETS = {
    'sd-a2': 'KUI/apps/games/retail-native-dual.kui',
    'sd-a1': 'KUI/apps/games/retail-native-dual-a1.kui',
    'disc-a2': 'KUI/apps/disc/native-disc-vmu.kui',
    'disc-a1': 'KUI/apps/disc/native-disc-vmu-a1.kui',
}
SD_COMPONENTS = ('entry', 'stage', 'native-worker', 'resident-sci', 'resident-legacy', 'native-ra-aux')


def git(*arguments):
    return subprocess.check_output(['git', *arguments], cwd=ROOT, text=True).strip()


def bind_gui_module(elf, module, commit):
    payload, memory = flatten_elf(elf)
    info = verify(module)
    require(info['build'] == commit[:12] and module[64:] == payload and
            info['memory_bytes'] == memory, 'GUI module differs from its exact source/ELF binding')


def gui_build(path, commit):
    elf = ordinary(path)
    proof = provenance(path.with_suffix('.compile.json'), elf, commit, True)
    payload, memory = flatten_elf(elf)
    module = envelope(payload, memory, commit[:12])
    bind_gui_module(elf, module, commit)
    changed = bytearray(payload)
    changed[-1] ^= 1
    other_build = '0'*12 if commit[:12] != '0'*12 else '1'*12
    other_memory = memory-4 if memory > len(payload) else memory+4
    cases = (envelope(bytes(changed), memory, commit[:12]),
             envelope(payload, memory, other_build),
             envelope(payload, other_memory, commit[:12]))
    for mutated in cases:
        # Each adversary has a valid header and checksums; only the exact
        # payload, source identifier and memory binding may authorize it.
        verify(mutated)
        try:
            bind_gui_module(elf, mutated, commit)
        except ValueError:
            continue
        raise ValueError('GUI module binding mutation was accepted')
    artifacts = {'runtime.elf': elf, 'runtime.compile.json': proof}
    if path.with_suffix('.map').exists():
        artifacts['runtime.map'] = ordinary(path.with_suffix('.map'))
    return module, artifacts, dict(exact_entry_elf_verified=True,
                                  clean_compile_record_verified=True,
                                  valid_envelope_mutation_rejections=len(cases))


def immutable_blob(image, begin, end, expected):
    """Bind a symbol-delimited blob to one initialized read-only ELF owner."""
    a, b = image[4][begin][0], image[4][end][0]
    require(b-a == len(expected) and a < b, 'Embedded blob length differs: '+begin)
    owners = [s for s in image[3] if s[1] == 1 and s[2] & 7 == 2 and
              s[3] <= a < b <= s[3]+s[5]]
    require(len(owners) == 1, 'Embedded blob lacks one immutable owner: '+begin)
    owner = owners[0]
    offset = owner[4]+a-owner[3]
    require(image[0][offset:offset+len(expected)] == expected, 'Embedded blob differs: '+begin)


def blob_binding_self_tests(image, begin, end, expected):
    """Mutate actual linked blob bytes, ownership and endpoint roles."""
    immutable_blob(image, begin, end, expected)
    a, b = image[4][begin][0], image[4][end][0]
    owner_index = next(i for i, s in enumerate(image[3]) if s[1] == 1 and
                       s[2] & 7 == 2 and s[3] <= a < b <= s[3]+s[5])
    owner = image[3][owner_index]
    changed_bytes = bytearray(image[0])
    changed_bytes[owner[4]+a-owner[3]+len(expected)//2] ^= 1
    changed_owner = list(image[3])
    changed_owner[owner_index] = (*owner[:2], owner[2] | 1, *owner[3:])
    changed_symbols = dict(image[4])
    changed_symbols[end] = (b+2, *changed_symbols[end][1:])
    wrong_expected = bytearray(expected)
    wrong_expected[-1] ^= 1
    cases = ((tuple([bytes(changed_bytes), *image[1:]]), expected),
             ((image[0], image[1], image[2], changed_owner, image[4]), expected),
             ((image[0], image[1], image[2], image[3], changed_symbols), expected),
             (image, bytes(wrong_expected)))
    for mutated, blob in cases:
        try:
            immutable_blob(mutated, begin, end, blob)
        except ValueError:
            continue
        raise ValueError('Embedded blob mutation was accepted: '+begin)
    return len(cases)


def sd_build(directory, commit, slot):
    directory = directory.resolve(strict=True)
    module = background_module(directory, commit[:12], slot)
    bound, personalization = bind_profile(directory, commit, 'retail-native-background-compact.kui')
    require(bound == module, 'Typed profile binding selected a different module')
    images = {name: read_elf(directory/(name+'.elf')) for name in SD_COMPONENTS}
    artifacts = {}
    for name, image in images.items():
        artifacts[name+'.elf'] = image[0]
        artifacts[name+'.map'] = ordinary(directory/(name+'.map'))
        artifacts[name+'.compile.json'] = provenance(directory/(name+'.compile.json'), image[0], commit, True)
    payload, memory = flatten_elf(images['entry'][0])
    require(module[64:] == payload and verify(module)['memory_bytes'] == memory,
            'SD runtime differs from its exact entry ELF')
    stage = images['stage']
    require(inspect_elf(stage[0], 0x8ce00000, 0x8cfe0000)['payload'] == ordinary(directory/'stage.bin'),
            'SD stage binary differs from ELF')
    immutable_blob(images['entry'], '__retail_stage_blob_start', '__retail_stage_blob_end', ordinary(directory/'stage.bin'))
    for begin, end, name in (
            ('__native_worker_blob_begin', '__native_worker_blob_end', 'native-worker.bin'),
            ('__native_worker_reloc_begin', '__native_worker_reloc_end', 'native-worker.reloc'),
            ('__retail_resident_sci_blob_start', '__retail_resident_sci_blob_end', 'resident-sci.bin'),
            ('__retail_resident_legacy_blob_start', '__retail_resident_legacy_blob_end', 'resident-legacy.bin'),
            ('_native_ra_aux_blob_start', '_native_ra_aux_blob_end', 'native-ra-aux.bin'),
            ('_native_ra_aux_reloc_start', '_native_ra_aux_reloc_end', 'native-ra-aux.reloc')):
        immutable_blob(stage, begin, end, ordinary(directory/name))
    worker = images['native-worker']
    geometry = layout_for(worker[4])
    require(geometry is BACKGROUND_COMPACT, 'SD worker does not select the compact profile')
    markers = compact_component_markers(stage, worker, images['resident-sci'],
                                       images['resident-legacy'], images['native-ra-aux'])
    require(markers.get('matching_immutable_component_markers') is True, 'SD component markers disagree')
    for name, base, span in (('native-worker', 0x8c100000, 0x12000),
                             ('native-ra-aux', geometry.AUX_LINK_BASE, geometry.AUX_SPAN)):
        binary, reloc = ordinary(directory/(name+'.bin')), ordinary(directory/(name+'.reloc'))
        require(generate(images[name][0], binary, base, span) == reloc,
                'SD ELF/binary/relocation binding differs: '+name)
    for name in ('stage.bin', 'native-worker.bin', 'native-worker.reloc', 'resident-sci.bin',
                 'resident-legacy.bin', 'native-ra-aux.bin', 'native-ra-aux.reloc', 'vmu-sd-profile.bin', 'readonly-config'):
        artifacts[name] = ordinary(directory/name)
    # Only known emitted compiler families; private game/test fixtures are never swept in.
    for family in ('native', 'stage', 'resident', 'legacy', 'ra', 'worker'):
        for path in sorted((directory/family).rglob('*')):
            if path.is_file() and path.suffix in ('.su', '.ci'):
                artifacts[path.relative_to(directory).as_posix()] = ordinary(path)
    symbols = worker[4]
    bss_end, stack_bottom, stack_top = (symbols[name][0] for name in
        ('__vmu_native_worker_bss_end', '__vmu_native_worker_stack_bottom', '__vmu_native_worker_stack_top'))
    require(0xac100000 <= bss_end <= stack_bottom < stack_top == 0xac112000 and
            stack_top-stack_bottom == 3072, 'SD initialized/state/private-stack geometry differs')
    report = dict(worker_bytes=0x12000, lease_bytes=0x47000, cache_bytes=0x30000,
                  worker_stack_bytes=3072, bss_end=bss_end, stack_bottom=stack_bottom,
                  stack_top=stack_top, bss_stack_gap_bytes=stack_bottom-bss_end,
                  matching_immutable_component_markers=True, typed_profile=personalization,
                  embedded_blob_mutation_rejections=blob_binding_self_tests(stage,
                      '__native_worker_blob_begin', '__native_worker_blob_end', artifacts['native-worker.bin']))
    return module, artifacts, report


def disc_build(directory, commit, slot):
    directory = directory.resolve(strict=True)
    module, gathered = collect_vmu_build(directory, commit[:12], commit)
    artifacts = {name.removeprefix('artifacts/disc-vmu/'): data for name, data in gathered.items()}
    entry, stage = read_elf(directory/'disc-entry.elf'), read_elf(directory/'disc-stage.elf')
    require(inspect_elf(stage[0], 0x8ce00000, 0x8cfe0000)['payload'] == artifacts['disc-stage.bin'],
            'Physical-disc stage binary differs from its exact ELF')
    immutable_blob(entry, '__disc_stage_blob_start', '__disc_stage_blob_end', artifacts['disc-stage.bin'])
    for begin, end, name in (
            ('__native_worker_blob_begin', '__native_worker_blob_end', 'native-worker.bin'),
            ('__native_worker_reloc_begin', '__native_worker_reloc_end', 'native-worker.reloc'),
            ('__disc_support_blob_begin', '__disc_support_blob_end', 'support/support.bin'),
            ('__disc_support_reloc_begin', '__disc_support_reloc_end', 'support/support.reloc')):
        immutable_blob(stage, begin, end, artifacts[name])
    other = 'A1' if slot == 'A2' else 'A2'
    require(('PHYSICAL DISC / VIRTUAL '+slot+' / SCI').encode() in module and
            ('PHYSICAL DISC / VIRTUAL '+other+' / SCI').encode() not in module,
            'Physical-disc module virtual slot differs')
    expected = (1 if slot == 'A1' else 2, 0, 0, 0xfff1)
    require(stage[4].get('__kui_disc_vmu_virtual_slot') == expected and
            read_elf(directory/'native-worker.elf')[4].get('__kui_native_physical_disc_virtual_slot') == expected,
            'Physical-disc stage/worker slot markers disagree')
    stack = ordinary(directory/'physical-disc-stack-audit.json')
    artifacts['physical-disc-stack-audit.json'] = stack
    report = json.loads(stack)
    require(report.get('schema') == 'kui.physical-disc-vmu.stack.v1' and
            report.get('worker_stack_bytes') == 4096 and
            0 < report.get('worst_bytes', 4097) <= 4096 and
            report.get('artifact_sha256') == dict(worker=sha(artifacts['native-worker.elf']),
                support=sha(artifacts['support/support.elf'])),
            'Physical-disc private-stack report does not bind these linked artifacts')
    return module, artifacts, dict(worker_bytes=0x12000, support_bytes=0x2000,
                                  lease_bytes=0x14000, worker_stack_bytes=4096,
                                  embedded_blob_mutation_rejections=blob_binding_self_tests(stage,
                                      '__disc_support_blob_begin', '__disc_support_blob_end', artifacts['support/support.bin']),
                                  stack_report=report)


def package(args):
    require(not git('status', '--porcelain', '--untracked-files=all'), 'Commit source changes before packaging')
    commit, tree = git('rev-parse', 'HEAD'), git('rev-parse', 'HEAD^{tree}')
    files, profiles = {}, {}
    for name, directory, slot, collect in (
            ('sd-a2', args.sd_a2_build, 'A2', sd_build), ('sd-a1', args.sd_a1_build, 'A1', sd_build),
            ('disc-a2', args.disc_a2_build, 'A2', disc_build), ('disc-a1', args.disc_a1_build, 'A1', disc_build)):
        module, artifacts, geometry = collect(directory, commit, slot)
        files[TARGETS[name]] = module
        for path, data in artifacts.items():
            files['details/artifacts/'+name+'/'+path] = data
        profiles[name] = dict(destination=TARGETS[name], bytes=len(module), sha256=sha(module),
                              runtime=verify(module), geometry=geometry)
    if args.runtime_elf:
        module, artifacts, binding = gui_build(args.runtime_elf.resolve(strict=True), commit)
        files['KUI/runtime.kui'] = module
        for path, data in artifacts.items():
            files['details/artifacts/gui/'+path] = data
        profiles['gui'] = dict(destination='KUI/runtime.kui', bytes=len(module), sha256=sha(module),
                               runtime=verify(module), binding=binding)
    for path in args.evidence:
        name = 'details/evidence/'+path.name
        require(name not in files, 'Duplicate evidence filename: '+path.name)
        files[name] = ordinary(path)
    verification = json.loads(ordinary(args.verification))
    require(isinstance(verification, dict), 'Verification summary must be a JSON object')
    candidate = dict(schema='kui.native.shutdown-test.candidate.v2', source_commit=commit,
        source_tree=tree, build=commit[:12], console_execution_verified=False,
        complete_historical_aggregate_audit_rerun=False, profiles=profiles,
        validation=verification, gui_updated=bool(args.runtime_elf), windows_ce_updated=False,
        compatibility_scope='Smash Pack SDK-disabled continuation candidate; Quake III executable handoff remains unresolved')
    with tempfile.TemporaryDirectory(prefix='kui-shutdown-source-') as temporary:
        bundle = Path(temporary)/'native-source-full.bundle'
        subprocess.run(['git', 'bundle', 'create', str(bundle), 'HEAD'], cwd=ROOT, check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        subprocess.run(['git', 'bundle', 'verify', str(bundle)], cwd=ROOT, check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        data = bundle.read_bytes()
    candidate['source_full_bundle'] = dict(path='details/source/native-source-full.bundle', bytes=len(data),
                                           sha256=sha(data), full_history=True, prerequisites=[])
    script = checkout_script()
    candidate['offline_source_recovery'] = offline_source_recovery_check(candidate, script, data)
    files['details/source/native-source-full.bundle'] = data
    files['details/source/checkout-source.py'] = script
    files['details/CANDIDATE.json'] = wire(candidate)
    files['README-FIRST.md'] = ordinary(ROOT/'docs/native-sdk-shutdown-test.md').replace(
        b'@BUILD@', commit[:12].encode()).replace(b'@INSTALL_CONTENTS@',
        b'the GUI runtime and four loader files' if args.runtime_elf else b'four loader files').replace(
        b'@GUI_SCOPE@', b'This package includes the updated VMU management screens.' if args.runtime_elf else
        b'This loader-only package does not update VMU management screens; those require the matching GUI runtime.')
    files['LICENSE'] = ordinary(ROOT/'LICENSE')
    files['THIRD_PARTY.md'] = ordinary(ROOT/'THIRD_PARTY.md')
    require(git('rev-parse', 'HEAD') == commit and not git('status', '--porcelain', '--untracked-files=all'),
            'Source changed while packaging')
    files['SHA256SUMS.txt'] = ''.join(sha(data)+'  '+name+'\n' for name, data in sorted(files.items())).encode()
    output = args.output.resolve()
    require(not output.is_relative_to(ROOT), 'Package output must be outside the source checkout')
    require(not output.exists(), 'Refusing to overwrite an existing delivery')
    output.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix='.shutdown-test-', suffix='.zip', dir=output.parent)
    os.close(fd)
    temporary = Path(temporary)
    try:
        with zipfile.ZipFile(temporary, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
            for name, data in sorted(files.items()):
                archive.writestr(name, data)
        with zipfile.ZipFile(temporary) as archive:
            require(len(archive.namelist()) == len(files) and set(archive.namelist()) == set(files) and
                    all(archive.read(name) == data for name, data in files.items()), 'ZIP round-trip differs')
        os.replace(temporary, output)
    finally:
        temporary.unlink(missing_ok=True)
    result = dict(zip=str(output), bytes=output.stat().st_size, sha256=sha(output.read_bytes()),
                  source_commit=commit, source_tree=tree, profiles=profiles, console_execution_verified=False)
    print(json.dumps(result, indent=2, sort_keys=True))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('sd-a2-build', 'sd-a1-build', 'disc-a2-build', 'disc-a1-build', 'verification', 'output'):
        parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--evidence', type=Path, action='append', default=[])
    parser.add_argument('--runtime-elf', type=Path,
                        help='Include the matching clean GUI ELF as KUI/runtime.kui; requires its compile record')
    args = parser.parse_args()
    try:
        package(args)
    except (OSError, ValueError, KeyError, TypeError, subprocess.CalledProcessError) as error:
        parser.exit(2, 'SDK shutdown candidate package refused: '+str(error)+'\n')


if __name__ == '__main__':
    main()
