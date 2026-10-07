#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bundle only the controlled SCI CDDA service-handoff experiment."""
import argparse
import json
from pathlib import Path
import re
import struct
import subprocess
import zipfile
import zlib

from package_cdda_calibration import (
    FATFS_FILES, MARKER, ROOT, archive_name, build_config, git, object_id,
    read_file, sha, valid_boot_markers, write_archive,
)
from package_cdda_mixed import FIXTURES, TMU_HZ, document_copy
from runtime_package import ADDRESS, ELF_HEADER, PROGRAM_HEADER, flatten_elf, verify

PROFILE = 7
MEMORY_BYTES = 0x310000
CLIENT_BEGIN = 0x8c300000
CLIENT_LIMIT = 0x8c310000
SERVICE_STACK_BEGIN = 0x8c220000
SERVICE_STACK_LIMIT = 0x8c230000
STACKS = {'.stack': (0x8c200000, 0x8c210000),
          '.service_stack': (SERVICE_STACK_BEGIN, SERVICE_STACK_LIMIT),
          '.client_stack': (0x8c310000, 0x8c320000)}
SECTION_HEADER = struct.Struct('<10I')


def client_layout(elf, payload):
    """Require initialized executable client bytes in the independent region.

    flatten_elf already validates load segments and the SH executable header.
    This checks the section table as additional evidence that the combined
    payload contains the linked controlled client, rather than a low-engine
    binary with an inflated memory declaration.
    """
    header = ELF_HEADER.unpack_from(elf)
    section_offset, section_size, section_count, names_index = (
        header[6], header[11], header[12], header[13])
    if (section_size != SECTION_HEADER.size or not section_count or
            names_index >= section_count or
            section_offset < ELF_HEADER.size or
            section_offset + section_count * section_size > len(elf)):
        raise ValueError('Missing or invalid client section evidence')
    sections = [SECTION_HEADER.unpack_from(elf, section_offset + i * section_size)
                for i in range(section_count)]
    segments = [PROGRAM_HEADER.unpack_from(elf, header[5] + i * header[9])
                for i in range(header[10])]
    names = sections[names_index]
    if names[1] != 3 or names[4] + names[5] > len(elf):
        raise ValueError('Invalid ELF section-name table')
    strings = elf[names[4]:names[4] + names[5]]
    client = []
    executable = []
    stacks = {}
    for name_at, kind, flags, address, offset, size, *_ in sections:
        if not flags & 2 or not size:
            continue
        if name_at >= len(strings) or b'\0' not in strings[name_at:]:
            raise ValueError('Invalid client section name')
        name = strings[name_at:strings.index(b'\0', name_at)].decode('ascii')
        if name in STACKS:
            first, end = STACKS[name]
            if (kind != 8 or flags & 7 != 3 or address != first or size != end - first or
                    not any(segment[0] == 1 and segment[6] & 2 and segment[2] <= first and
                            end <= segment[2] + segment[5] for segment in segments)):
                raise ValueError('Invalid dedicated stack layout: ' + name)
            if name in stacks:
                raise ValueError('Duplicate dedicated stack section: ' + name)
            stacks[name] = {'address': address, 'bytes': size}
        if address < CLIENT_BEGIN or address >= CLIENT_LIMIT:
            continue
        if address + size > CLIENT_LIMIT:
            raise ValueError('Client section exceeds the controlled code/data region')
        record = {'name': name, 'address': address, 'bytes': size,
                  'initialized': kind != 8, 'executable': bool(flags & 4)}
        covering = [segment for segment in segments
                    if segment[0] == 1 and segment[2] <= address and
                    address + size <= segment[2] + segment[5]]
        if not covering:
            raise ValueError('Client section is absent from ELF load memory')
        if kind != 8:
            first = address - ADDRESS
            if (offset + size > len(elf) or first + size > len(payload) or
                    payload[first:first + size] != elf[offset:offset + size] or
                    not any(address + size <= segment[2] + segment[4] and
                            offset == segment[1] + address - segment[2]
                            for segment in covering)):
                raise ValueError('Linked client section is missing from the runtime payload')
            record['sha256'] = sha(elf[offset:offset + size])
        if flags & 4:
            if kind != 1 or not any(segment[6] & 1 for segment in covering):
                raise ValueError('Client executable section lacks an executable PROGBITS load')
            executable.append(record)
        client.append(record)
    entry = [item for item in executable if item['name'] == '.client']
    if len(entry) != 1 or entry[0]['address'] != CLIENT_BEGIN:
        raise ValueError('Missing linked client entry/text at 0x8c300000')
    if set(stacks) != set(STACKS):
        raise ValueError('Missing dedicated engine/service/client stack section')
    return {'sections': client, 'dedicated_stacks': stacks}


def collect(commit, published_tree):
    if git('status', '--porcelain'):
        raise ValueError('Commit reviewed source before packaging')
    source_tree = git('rev-parse', 'HEAD^{tree}')
    if published_tree != source_tree:
        raise ValueError('Published source tree differs from the archived checkpoint')
    names = subprocess.check_output(['git', 'ls-files', '-z'], cwd=ROOT).decode('utf-8')
    tracked = {name for name in names.split('\0') if name}
    for name in tracked:
        archive_name(name)

    files = {}

    def add(name, data):
        archive_name(name)
        if name in files:
            raise ValueError('Duplicate archive path: ' + name)
        files[name] = data

    clock_header = read_file(ROOT / 'include/kui/cdda_clock.h')
    if not re.search(rb'^#define\s+KUI_CDDA_TMU_HZ\s+12468720u\s*$',
                     clock_header, flags=re.MULTILINE):
        raise ValueError('Service profile must use the documented 12468720 Hz clock contract')

    build = ROOT / 'build/cdda-service'
    config_bytes = read_file(build / 'build-config')
    config = build_config(build / 'build-config')
    if config.get('BUILD') != commit[:12] or config.get('PROFILE') != str(PROFILE):
        raise ValueError('Wrong build configuration for service handoff')
    runtime = read_file(build / 'cdda-harness.kui')
    info = verify(runtime)
    elf = read_file(build / 'cdda-harness.elf')
    payload, memory = flatten_elf(elf)
    if (info['build'] != commit[:12] or runtime[64:] != payload or
            memory != MEMORY_BYTES or info['memory_bytes'] != memory or
            payload.count(MARKER) != 1 or valid_boot_markers(payload) != 1):
        raise ValueError('Envelope/source/layout/boot-marker mismatch for service handoff')
    linked_layout = client_layout(elf, payload)
    add('runtimes/07-service-handoff.kui', runtime)
    add('KUI/runtime.kui', runtime)
    add('evidence/service/cdda-harness.elf', elf)
    map_bytes = read_file(build / 'cdda-harness.map')
    add('evidence/service/cdda-harness.map', map_bytes)
    add('evidence/service/build-config', config_bytes)
    stack_hashes = {}
    stack_files = sorted(build.rglob('*.su'))
    if not stack_files:
        raise ValueError('Missing compiler stack reports for service handoff')
    for path in stack_files:
        relative = path.relative_to(build).as_posix()
        data = read_file(path)
        add('evidence/service/stack/' + relative, data)
        stack_hashes[relative] = sha(data)

    private_fatfs = {name: read_file(build / 'fatfs' / name) for name in FATFS_FILES}
    configuration = private_fatfs['ffconf.h'].decode('utf-8')
    for key, value in (('FF_FS_READONLY', 1), ('FF_FS_EXFAT', 1)):
        if not re.search(r'^#define\s+' + key + r'\s+' + str(value) + r'\b',
                         configuration, flags=re.MULTILINE):
            raise ValueError('Unexpected private FatFs configuration: ' + key)
    for name, data in private_fatfs.items():
        add('source-fatfs/' + name, data)

    fixture_metadata = []
    fixture_input_hashes = {}
    fixture = ROOT / 'build/cdda/fixture'
    for name, expected in FIXTURES.items():
        data = read_file(fixture / expected['file'])
        metadata_bytes = read_file(fixture / (name + '.json'))
        metadata = json.loads(metadata_bytes)
        if (not isinstance(metadata, dict) or len(data) != expected['bytes'] or
                sha(data) != expected['sha256'] or
                f'{zlib.crc32(data):08x}' != expected['crc32'] or
                any(metadata.get(key) != value for key, value in expected.items())):
            raise ValueError('Generated fixture differs from the verified input: ' + name)
        add('KUI/tests/cdda/' + expected['file'], data)
        add('fixtures/' + name + '.json', metadata_bytes)
        fixture_metadata.append(metadata)
        fixture_input_hashes[expected['file']] = sha(data)
        fixture_input_hashes[name + '.json'] = sha(metadata_bytes)

    for path in sorted((ROOT / 'LICENSES').rglob('*')):
        if path.is_file():
            add('LICENSES/' + path.relative_to(ROOT / 'LICENSES').as_posix(), read_file(path))
    add('LICENSE', read_file(ROOT / 'LICENSE'))
    notice = read_file(ROOT / 'data/known-dumps/README.txt')
    if 'LICENSES/known-dumps-README.txt' in files:
        if files['LICENSES/known-dumps-README.txt'] != notice:
            raise ValueError('Known-dumps notices disagree')
    else:
        add('LICENSES/known-dumps-README.txt', notice)

    source_url = 'https://github.com/TPMJB/K-UI-NeXT/tree/' + commit
    add('source-url.txt', (source_url + '\n').encode())
    snapshot = subprocess.check_output(['git', 'archive', '--format=tar', 'HEAD'], cwd=ROOT)
    add('source-snapshot.tar', snapshot)

    documents = sorted((ROOT / 'docs').glob('cdda-*.md'))
    evidence = sorted((ROOT / 'docs/evidence').glob('cdda-*.md'))
    document_paths = []
    for path in documents:
        source = path.relative_to(ROOT).as_posix()
        document_paths.extend(((source, source), (source, path.name)))
    for path in evidence:
        source = path.relative_to(ROOT).as_posix()
        document_paths.extend(((source, source), (source, 'evidence/' + path.name)))
    document_paths.extend((('docs/cdda-service-test.md', 'README.md'),
                           ('THIRD_PARTY.md', 'THIRD_PARTY.md')))
    available = set(files) | {destination for _, destination in document_paths}
    available.update(('build.json', 'SHA256SUMS'))
    document_input_hashes = {}
    for source, destination in document_paths:
        data = read_file(ROOT / source)
        document_input_hashes[source] = sha(data)
        add(destination, document_copy(data, source, destination, available, tracked, commit))

    metadata = {
        'source_commit': commit, 'source_tree': source_tree,
        'local_checkpoint': git('rev-parse', 'HEAD'), 'source_dirty': False,
        'source_publication': 'published; supplied tree matches local checkpoint',
        'source_url': source_url, 'source_snapshot': 'source-snapshot.tar',
        'source_snapshot_sha256': sha(snapshot),
        'compiler': subprocess.check_output(['sh-elf-gcc', '--version'], text=True).splitlines()[0],
        'scope': 'Controlled SCI homebrew client calling an owned CDDA service; no retail integration',
        'default_profile': 'service-handoff', 'default_profile_number': PROFILE,
        'hardware_tested': False,
        'runtimes': [{
            'profile': PROFILE, 'name': 'service-handoff', 'file': 'runtimes/07-service-handoff.kui',
            'build': info['build'], 'hardware_tested': False,
            'runtime_sha256': sha(runtime), 'elf_sha256': sha(elf), 'map_sha256': sha(map_bytes),
            'payload_bytes': info['payload_bytes'], 'payload_crc32': info['crc32'],
            'memory_footprint_bytes': memory,
            'boot_marker': 'one unpatched AUTO marker; bootstrap must select SCI',
            'build_configuration': config, 'build_configuration_sha256': sha(config_bytes),
            'stack_reports_sha256': stack_hashes,
            'linked_client_layout': linked_layout,
        }],
        'clock_contract': {
            'tmu_hz': TMU_HZ, 'configured_aica_pitch': 0,
            'automatic_tuning': False, 'independent_console_frequency_measurement': False,
            'header': 'include/kui/cdda_clock.h', 'header_sha256': sha(clock_header),
        },
        'owned_game_files_included': False, 'sample_included': False,
        'required_game_files': [], 'fixtures': fixture_metadata,
        'fixture_inputs_sha256': fixture_input_hashes,
        'fatfs_inputs_sha256': {name: sha(data) for name, data in private_fatfs.items()},
        'document_source_inputs_sha256': document_input_hashes,
        'package_document_links': 'CDDA source-layout copies and aliases; other targets pin published source',
        'service_scope': {
            'entry': 'controlled client explicit cooperative calls',
            'interrupt_hook': False, 'retail_game': False,
            'sr_vbr_fpu_game_sharing_established': False,
            'gbr_interrupt_state_sharing_established': False,
            'integer_call_probe': 'r8-r14 and PR; no broken-SP/nonreturn recovery',
            'engine_code_base': ADDRESS,
            'engine_stack': [0x8c200000, 0x8c210000],
            'service_stack': [SERVICE_STACK_BEGIN, SERVICE_STACK_LIMIT],
            'client_code_region': [CLIENT_BEGIN, CLIENT_LIMIT],
            'client_stack': [0x8c310000, 0x8c320000],
        },
        'test_contract': {
            'completed_stages': 7, 'minimum_tmu_seconds': 90,
            'initialization_excluded': True,
            'minimum_checked_data_bytes': 65536,
            'minimum_integer_abi_checks': 100, 'controlled_context_checks': 1,
            'abi_checks_equal_service_calls_plus': 2,
            'completed_audio_actions': 6, 'explicit_status_checks': 5,
            'expected_descriptor_refusals': 8, 'expected_busy_reentry_refusals': 1,
            'expected_deadline_refusals': 1, 'expected_retired_epoch_refusals': 2,
            'intentional_service_gap_approximate_ms': 190,
            'maximum_physical_data_read_bytes': 2048,
            'data_progress_gap_strictly_below_us': 5000000,
            'maximum_observed_client_stack_bytes': 65472,
            'maximum_observed_service_stack_bytes': 65472,
            'final_audio_state': 'STOPPED',
            'service_test_errors': 0, 'unexpected_failures': 0,
        },
        'sound_ring_bytes': 65536,
        'private_engine_stack_bytes': 65536, 'private_service_stack_bytes': 65536,
        'private_client_stack_bytes': 65536,
    }
    add('build.json', (json.dumps(metadata, indent=2) + '\n').encode())
    if git('status', '--porcelain') or git('rev-parse', 'HEAD^{tree}') != source_tree:
        raise ValueError('Source changed while collecting the package')
    add('SHA256SUMS', ''.join(sha(data) + '  ' + name + '\n'
                            for name, data in sorted(files.items())).encode())
    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('--source-commit', required=True, type=object_id)
    parser.add_argument('--published-tree', required=True, type=object_id,
                        help='Tree SHA independently verified on the published commit')
    args = parser.parse_args()
    try:
        if args.output.is_symlink() or args.output.suffix.lower() != '.zip':
            raise ValueError('Output must be a ZIP filename, not a symlink')
        output = args.output.resolve()
        archive_name(output.name)
        if output.exists() and not output.is_file():
            raise ValueError('Output path is not a regular file')
        if output.is_relative_to(ROOT):
            relative = output.relative_to(ROOT).as_posix()
            tracked = subprocess.run(['git', 'ls-files', '--error-unmatch', '--', relative],
                                     cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            if tracked.returncode == 0:
                raise ValueError('Output must not overwrite a tracked repository file')
        files = collect(args.source_commit, args.published_tree)
        write_archive(output, files)
        print(json.dumps({'file': str(output), 'bytes': output.stat().st_size,
                          'build': args.source_commit[:12], 'profiles': [PROFILE],
                          'sha256': sha(output.read_bytes())}, indent=2))
    except (OSError, ValueError, struct.error, subprocess.SubprocessError, zipfile.BadZipFile) as error:
        parser.exit(1, 'FAIL: ' + str(error) + '\n')


if __name__ == '__main__':
    main()
