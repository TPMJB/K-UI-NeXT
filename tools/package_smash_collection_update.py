#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Deliver a reviewed GUI and its embedded four-stage Smash cohort.

All five .kui files have the same source build. The GUI embeds the four stages;
each CE stage embeds its own typed helper and full padded-helper CRC owner.
Unchanged CE helpers retain their exact accepted component identity and bytes,
independently of the new stage/GUI commit.
Current Main's v2 producer stages the GUI, four real modules and both matched
raw helpers. This replaces pinned older bank stages as well as root fallbacks;
unrelated effective software and user data remain shared and unchanged.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import struct
import tempfile
import zlib

from package_smash_current_main_test import regular, sha
from runtime_package import ADDRESS, envelope, flatten_elf, verify as verify_runtime
from smash_ce_profile import profile as ce_profile
from update_package import SELF_KUI, RAW_SOFTWARE, verify
from make_update import make_update
from check_smash_helper_body import (validate as validate_helper,
    REFERENCE_BUILD as HELPER_COMPONENT_BUILD, PINS as HELPER_PINS)
from build_runtime_module_bundle import locate as locate_bundle, decode as decode_bundle
from check_vmu_native_worker_gateway import read_elf

ROOT = Path(__file__).resolve().parents[1]
MAIN_UPDATER = 'a27da110f02cf356c5b287ecbf67f4ae5df715ea'
STAGES = ('apps/games/retail-smash-menu.kui',
          'apps/games/retail-smash-native-menu.kui',
          'apps/games/retail-smash-vc2.kui',
          'apps/games/retail-smash-swirl.kui')
HELPERS = {1: 'apps/games/smash-vc2-vmu-helper.bin',
           2: 'apps/games/smash-swirl-vmu-helper.bin'}
HELPER_HEADER = struct.Struct('<8I')
HELPER_TAG = struct.pack('<4I', 0x5349554b, 0x31484543, 1, 32)


def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT).decode().strip()


def linked_member(path, elf, commit):
    path, elf = regular(path), regular(elf)
    data, linked = path.read_bytes(), elf.read_bytes()
    info = verify_runtime(data)
    payload, memory = flatten_elf(linked)
    if info['build'] != commit[:12] or envelope(payload, memory, commit[:12]) != data:
        raise ValueError('Module does not match this source build and linked ELF: ' + path.name)
    receipt_path = regular(elf.with_suffix('.compile.json'))
    receipt_data = receipt_path.read_bytes()
    receipt = json.loads(receipt_data)
    if (receipt.get('commit') != commit or receipt.get('source_dirty') is not False or
            receipt.get('elf_sha256') != sha(linked)):
        raise ValueError('Missing same clean-source link receipt: ' + path.name)
    return data, dict(build=info['build'], bytes=len(data), sha256=sha(data),
                      elf_sha256=sha(linked), compile_receipt_sha256=sha(receipt_data))


def qualified_source(report, commit):
    if (report.get('schema') != 'kui.smash.collection-focused.v1' or report.get('status') != 'PASS' or
            report.get('source_commit') != commit or report.get('source_dirty') is not False or
            report.get('development_validation_only') is not False):
        raise ValueError('Smash qualification must identify this clean complete source cohort')
    hashes = report.get('source_sha256', {})
    if not hashes or any(not isinstance(path, str) or path.startswith('/') or
            '..' in path.split('/') or not (ROOT / path).is_file() or
            sha((ROOT / path).read_bytes()) != digest for path, digest in hashes.items()):
        raise ValueError('Smash qualification no longer matches current source')
    if report.get('reports', {}).get('native_worker_bytes') != 72 * 1024:
        raise ValueError('Native child must retain its qualified72KiB VMU worker')


def embedded_helper(payload, raw, kind):
    """Match the production typed-header admission and independent CRC owner."""
    found = None
    at = payload.find(HELPER_TAG)
    while at != -1:
        if found is not None or at & 3 or len(payload) - at < HELPER_HEADER.size:
            raise ValueError('Malformed or duplicate embedded CE helper descriptor')
        words = HELPER_HEADER.unpack_from(payload, at)
        size, padded = words[5:7]
        if (words[4] != kind or size < 16 or size & 3 or padded != 49152 or
                size > padded or words[7] or size > len(payload) - at - HELPER_HEADER.size):
            raise ValueError('Invalid embedded CE helper geometry or kind')
        found = payload[at + HELPER_HEADER.size:at + HELPER_HEADER.size + size]
        at = payload.find(HELPER_TAG, at + 1)
    if found != raw:
        raise ValueError('CE module does not embed its exact linked helper bytes')
    descriptor = ce_profile(raw, kind, embedded=True)
    if payload.count(descriptor) != 1:
        raise ValueError('CE module does not uniquely own its embedded padded helper CRC')
    padded = raw + bytes(49152 - len(raw))
    late = struct.pack('<11I', 0x32554d56, 0x44414f4c, 2, 44, 49152,
                       zlib.crc32(padded), 0, 0, 0, 0, 0)
    if payload.count(late) != 1:
        raise ValueError('CE module late-load seed differs from its embedded helper')
    return dict(raw_bytes=len(raw), padded_bytes=len(padded), raw_sha256=sha(raw),
                padded_sha256=sha(padded), padded_crc32=f'{zlib.crc32(padded):08x}',
                profile_sha256=sha(descriptor), profile_version=2,
                typed_embedded_helper=True, external_root_helper_required=False)


def linked_bundle(elf, payload, offset, bundle, elf_sha256):
    """Bind the decoder's actual SH literal loads to its initialized asset."""
    image = read_elf(regular(elf), allow_tls=True)
    if sha(image[0]) != elf_sha256 or flatten_elf(image[0])[0] != payload:
        raise ValueError('GUI ELF changed during bundle binding')
    symbols = image[4]
    names = ('__kui_runtime_module_bundle_start', '__kui_runtime_module_bundle_end')
    if (any(name not in symbols for name in names) or
            any(name.startswith('___kui_runtime_module_bundle_') for name in symbols)):
        raise ValueError('GUI bundle symbols do not match the SH C/assembly ABI')
    begin, end = (symbols[name][0] for name in names)
    if (not ADDRESS <= begin < end <= ADDRESS + len(payload) or
            begin != ADDRESS + offset or end - begin != len(bundle) or
            payload[begin - ADDRESS:end - ADDRESS] != bundle):
        raise ValueError('GUI bundle symbols do not own the complete linked bundle bytes')
    references = {}
    for name in ('_kui_runtime_module_bundle_read', '_kui_runtime_module_bundle_contains'):
        symbol = symbols.get(name)
        if (not symbol or symbol[2] != 2 or not symbol[1] or
                not ADDRESS <= symbol[0] < symbol[0] + symbol[1] <= ADDRESS + len(payload)):
            raise ValueError('GUI bundle codec function is missing: ' + name)
        loads = []
        for at in range(symbol[0] - ADDRESS, symbol[0] - ADDRESS + symbol[1] - 1, 2):
            instruction = struct.unpack_from('<H', payload, at)[0]
            if instruction & 0xf000 != 0xd000:
                continue
            pool = ((ADDRESS + at + 4) & ~3) + (instruction & 255) * 4
            if ADDRESS <= pool <= ADDRESS + len(payload) - 4:
                value = struct.unpack_from('<I', payload, pool - ADDRESS)[0]
                if value in (begin, end):
                    loads.append(dict(instruction=ADDRESS + at, literal=pool, value=value))
        if {load['value'] for load in loads} != {begin, end}:
            raise ValueError('GUI bundle codec does not load both exact asset bounds: ' + name)
        references[name] = loads
    return dict(start=begin, end=end, bytes=end - begin, SH_PC_relative_loads=references)


def build(runtime, runtime_elf, smash_report, output):
    if git('status', '--porcelain'):
        raise ValueError('Commit the reviewed source before producing a collection update')
    commit = git('rev-parse', 'HEAD')
    if not re.fullmatch(r'[0-9a-f]{40}', commit):
        raise ValueError('The source must identify an exact commit')
    output = Path(output).absolute()
    if (output.suffix != '.kup' or output.exists() or output.is_symlink() or
            any(p.is_symlink() for p in output.parents)):
        raise ValueError('Choose a new regular .kup output path')
    report_path = regular(smash_report)
    report_data = report_path.read_bytes()
    report = json.loads(report_data)
    qualified_source(report, commit)
    installs = report.get('install_files', {})
    expected = {'KUI/' + path for path in (*STAGES, *HELPERS.values())}
    if set(installs) != expected:
        raise ValueError('Qualification must bind exactly four stages and two embedded helper components')
    files, links = {}, {}
    files['runtime.kui'], links['runtime.kui'] = linked_member(runtime, runtime_elf, commit)
    folders = {}
    for name in STAGES:
        record = installs['KUI/' + name]
        path = regular(record.get('path', ''))
        data, binding = linked_member(path, path.with_name('entry.elf'), commit)
        if record.get('sha256') != sha(data):
            raise ValueError('Qualified stage bytes changed: ' + name)
        files[name], links[name] = data, binding
        folders[name] = path.parent
    helpers = {}
    raw_files = {}
    for kind, name in HELPERS.items():
        record = installs['KUI/' + name]
        qualified = regular(record.get('path', '')).read_bytes()
        stage_name = STAGES[2 if kind == 1 else 3]
        raw = regular(folders[stage_name] / 'helper/helper.bin').read_bytes()
        prepared = regular(folders[stage_name] / 'helper/prepared.bin').read_bytes()
        component = report.get('reports', {}).get('CE_' + str(kind), {})
        component_build = component.get('helper_component_build')
        if (component_build != HELPER_COMPONENT_BUILD or
                component.get('helper_qualified_source_commit') != commit):
            raise ValueError('CE helper must declare its actual accepted component build and current qualification source')
        validation = validate_helper(raw, kind, component_build)
        if (sha(raw) != HELPER_PINS[kind]['reference_sha256'] or
                component.get('helper_body') != validation):
            raise ValueError('Normal CE helper differs from the exact qualified accepted component')
        if (len(prepared) != 49152 or prepared != raw + bytes(49152 - len(raw)) or
                qualified != prepared or record.get('sha256') != sha(prepared)):
            raise ValueError('Qualified embedded CE helper component changed: ' + name)
        helper_elf = regular(folders[stage_name] / 'helper/helper.elf').read_bytes()
        if report.get('artifacts_sha256', {}).get('CE' + str(kind) + '/helper.elf') != sha(helper_elf):
            raise ValueError('Linked CE helper differs from the qualification: ' + name)
        raw_files[name] = prepared
        helpers[stage_name] = dict(**embedded_helper(files[stage_name][64:], raw, kind),
            helper_elf_sha256=sha(helper_elf), component_build=component_build,
            qualified_source_commit=commit, body_validation=validation)
    bundle_offset, bundle = locate_bundle(files['runtime.kui'][64:])
    decoded = decode_bundle(bundle)
    if (set(decoded) != set(STAGES) or any(decoded[name] != files[name] for name in STAGES) or
            bundle[32:44] != commit[:12].encode('ascii')):
        raise ValueError('GUI bundle does not contain exactly the four qualified current-build stages')
    bundle_binding = dict(payload_offset=bundle_offset, bytes=len(bundle), sha256=sha(bundle),
        build=commit[:12], members={name: dict(bytes=len(decoded[name]), sha256=sha(decoded[name]))
                                  for name in STAGES})
    bundle_binding['linked_ELF'] = linked_bundle(runtime_elf, files['runtime.kui'][64:],
        bundle_offset, bundle, links['runtime.kui']['elf_sha256'])
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='kui-smash-cohort-') as temp:
        stage = Path(temp) / 'KUI'
        stage.mkdir()
        for name, content in {**files, **raw_files}.items():
            target = stage / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(content)
        candidate = Path(temp) / 'collection.kup'
        # Use Main's current producer and actual C consumer, with no inherited
        # catalog rows. Explicit module replacements supersede a pinned older
        # bank; a GUI bundle alone cannot override such a managed replacement.
        package = make_update(stage, candidate, channel='testing')
        checked = verify(candidate)
        if (checked['format'] != 2 or checked['full'] or
                checked['members'] != package['members'] or
                set(row['path'] for row in checked['members']) != set(files) | set(raw_files) or
                package.get('consumer', {}).get('decoder') != 'current-source' or
                package['consumer'].get('format') != 2):
            raise ValueError('Produced collection update failed current Main v2/C-consumer verification')
        for row in checked['members']:
            expected_policy = SELF_KUI if row['path'] in files else RAW_SOFTWARE
            if row['policy'] != expected_policy:
                raise ValueError('Collection components must be explicit checked replacements')
        data = candidate.read_bytes()
        # Recheck original inputs after collecting the package, before publication.
        if regular(runtime).read_bytes() != files['runtime.kui'] or report_path.read_bytes() != report_data:
            raise ValueError('GUI or qualification changed during packaging')
        for name in STAGES:
            if regular(installs['KUI/' + name]['path']).read_bytes() != files[name]:
                raise ValueError('A qualified stage changed during packaging: ' + name)
        for kind, name in HELPERS.items():
            stage_name = STAGES[2 if kind == 1 else 3]
            if (sha(regular(folders[stage_name] / 'helper/helper.bin').read_bytes()) != helpers[stage_name]['raw_sha256'] or
                    sha(regular(folders[stage_name] / 'helper/helper.elf').read_bytes()) != helpers[stage_name]['helper_elf_sha256'] or
                    sha(regular(installs['KUI/' + name]['path']).read_bytes()) != helpers[stage_name]['padded_sha256']):
                raise ValueError('An embedded helper input changed during packaging: ' + name)
        with output.open('xb') as stream:
            stream.write(data)
    return dict(schema='kui.smash.collection-update.v2', file=str(output), bytes=len(data), sha256=sha(data),
        source_commit=commit, runtime_build=commit[:12], runtime_source_commit=commit,
        manifest_entries=len(package['members']), replacements=[row['path'] for row in package['members']],
        inherited_roles=0, update_format=2, updater_consumer=package['consumer'],
        embedded_stage_modules=4, stage_source_commit=commit, main_updater_source=MAIN_UPDATER,
        links=links, qualification_sha256=sha(report_data), embedded_helpers=helpers,
        runtime_stage_bundle=bundle_binding,
        engine_minimum=package['consumer']['engine_minimum'], effective_software_preserved=True,
        root_dispatcher_upgrade_required=True,
        external_root_helpers_required=False, root_files_written_by_updater=False,
        raw_helper_replacements=2, explicit_stage_replacements=4,
        worker_geometry_changed=False, hardware_acceptance=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('runtime', 'runtime-elf', 'smash-report', 'output'):
        parser.add_argument('--' + name, required=True, type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps(build(args.runtime, args.runtime_elf, args.smash_report, args.output), indent=2))
    except (OSError, ValueError, zlib.error, subprocess.CalledProcessError) as error:
        parser.exit(1, str(error) + '\n')


if __name__ == '__main__':
    main()
