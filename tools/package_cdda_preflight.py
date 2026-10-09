#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bundle read-only complete-image preflight and separately installed retail observation."""
import argparse
import json
from pathlib import Path
import posixpath
import re
import struct
import subprocess
from urllib.parse import quote, unquote, urlsplit, urlunsplit
import zipfile

from check_loader_layout import inspect_elf, padded, region
from check_retail_loader_layout import (
    FORBIDDEN_PREFIXES, FORBIDDEN_SYMBOLS, check_bss, check_stack_usage, code_symbol,
)
from check_retail_instructions import audit
from package_cdda_calibration import (
    FATFS_FILES, MARKER, ROOT, archive_name, build_config, git, object_id,
    read_file, sha, valid_boot_markers, write_archive,
)
from package_cdda_mixed import LINK, document_copy
import retail_package as retail_layout
from retail_package import inspect_retail
from runtime_package import ADDRESS, ELF_HEADER, PROGRAM_HEADER, flatten_elf, verify

PREFLIGHT_BUILD = 'build/cdda-preflight'
OBSERVATION_BUILD = 'build/retail-observe'
PREFLIGHT_FILE = 'runtimes/13-image-preflight.kui'
OBSERVATION_FILE = 'observation/14-retail-observe.kui'
PREFLIGHT_LABEL = b'PROFILE13 PREFLIGHT /'
OBSERVATION_LABEL = b'PROFILE14 OBSERVE /'
TOY_DESCRIPTOR_BYTES = 451
TOY_DESCRIPTOR_SHA256 = '96e3a54b9aa528c7172121ba3c6cfabf391aacb5a84f292e89843ff2d8853803'
SECTION_HEADER = struct.Struct('<10I')


def loaded_objects(map_bytes):
    return [name.rsplit(b'/', 1)[-1] for name in
            re.findall(rb'^LOAD[ \t]+(\S+)[ \t]*$', map_bytes, flags=re.MULTILINE)]


def allocated_sections(elf):
    """Require every allocated section to be represented by its ELF load segments."""
    header = ELF_HEADER.unpack_from(elf)
    offset, width, count, names_index = header[6], header[11], header[12], header[13]
    if (width != SECTION_HEADER.size or not count or names_index >= count or
            offset < ELF_HEADER.size or offset + width * count > len(elf)):
        raise ValueError('Invalid linked section evidence')
    sections = [SECTION_HEADER.unpack_from(elf, offset + i * width) for i in range(count)]
    names = sections[names_index]
    if names[1] != 3 or names[4] + names[5] > len(elf):
        raise ValueError('Invalid section name table')
    strings = elf[names[4]:names[4] + names[5]]
    segments = [PROGRAM_HEADER.unpack_from(elf, header[5] + i * header[9])
                for i in range(header[10])]
    result = {}
    for name_at, kind, flags, address, file_offset, size, *_ in sections:
        if not flags & 2 or not size:
            continue
        if name_at >= len(strings) or b'\0' not in strings[name_at:]:
            raise ValueError('Invalid allocated section name')
        name = strings[name_at:strings.index(b'\0', name_at)].decode('ascii')
        if name in result:
            raise ValueError('Duplicate allocated section: ' + name)
        covering = [segment for segment in segments if segment[0] == 1 and
                    segment[2] <= address and address + size <= segment[2] + segment[5]]
        if not covering or (flags & 1 and not any(s[6] & 2 for s in covering)) or (
                flags & 4 and not any(s[6] & 1 for s in covering)):
            raise ValueError('Allocated section lacks matching load permissions: ' + name)
        if kind != 8:
            if file_offset + size > len(elf) or not any(
                    address + size <= s[2] + s[4] and
                    file_offset == s[1] + address - s[2] for s in covering):
                raise ValueError('Initialized section is absent from linked file bytes: ' + name)
        result[name] = {'address': address, 'bytes': size, 'kind': kind, 'flags': flags}
    return result


def preflight_layout(elf, payload, memory):
    """Check the detached read-only image, without borrowing a client-stack contract."""
    if memory != 0x200000:
        raise ValueError('Preflight must retain its single 64 KiB engine-stack layout')
    image = inspect_elf(elf, ADDRESS, 0x8c210000)
    sections = allocated_sections(elf)
    if sections.get('.stack') != {'address': 0x8c200000, 'bytes': 65536, 'kind': 8, 'flags': 3}:
        raise ValueError('Preflight must have one loaded writable NOLOAD engine stack')
    symbols = image['symbols']
    if (symbols.get('__cdda_stack_bottom') != 0x8c200000 or
            symbols.get('__cdda_stack_top') != 0x8c210000 or
            image['memory_end'] != 0x8c210000):
        raise ValueError('Preflight engine stack differs from the linked reservation')
    for name in ('_cdda_main', '_kui_cdda_preflight_read',
                 '_cdda_preflight_storage_discover', '_cdda_preflight_storage_extents'):
        value = symbols.get(name, 0)
        if value % 2 or not any(section['flags'] & 4 and
                                section['address'] <= value < section['address'] + section['bytes']
                                for section in sections.values()):
            raise ValueError('Missing retained read-only preflight entry: ' + name)
    forbidden = ('_kui_cdda_aica_', '_kui_cdda_client_call', '_cdda_disc_client_entry',
                 '_cdda_bios_native_dispatch', '__cdda_client_', '__cdda_service_stack_')
    if any(name.startswith(forbidden) for name in symbols):
        raise ValueError('Read-only preflight must not retain playback/vector/client-stack work')
    if payload.count(MARKER) != 1 or valid_boot_markers(payload) != 1:
        raise ValueError('Preflight must contain one unpatched AUTO storage marker')
    return {
        'entry': '0x8c010000', 'engine_stack': ['0x8c200000', '0x8c210000'],
        'memory_bytes': memory, 'separate_client': False, 'service_worker_stack': False,
        'linked_playback_or_owned_bios_vector': False,
        'allocated_sections': sections,
    }


def observation_layout(directory, entry_elf):
    """Audit the actual three observation ELFs; no fictitious transport images."""
    entry = inspect_elf(entry_elf, ADDRESS, ADDRESS + retail_layout.STAGE_BLOB_OFFSET +
                        retail_layout.STAGE_MAX_BYTES)
    stage_elf = read_file(directory / 'stage.elf')
    resident_elf = read_file(directory / 'resident-sci.elf')
    stage = inspect_elf(stage_elf, retail_layout.STAGE_ADDRESS, retail_layout.STAGE_MEMORY_END)
    resident = inspect_elf(resident_elf, retail_layout.LOW_RESIDENT_ADDRESS,
                           retail_layout.LOW_RESIDENT_LIMIT)
    images = {'entry': entry, 'stage': stage, 'resident-sci': resident}
    sections = {'entry': allocated_sections(entry_elf), 'stage': allocated_sections(stage_elf),
                'resident-sci': allocated_sections(resident_elf)}
    for name, image in images.items():
        bad = [symbol for symbol in image['symbols'] if symbol.startswith(FORBIDDEN_PREFIXES) or
               symbol in FORBIDDEN_SYMBOLS or symbol.startswith('_kui_cdda_aica_')]
        if bad:
            raise ValueError('Unexpected runtime/device symbol in ' + name + ': ' + bad[0])
    high = padded(read_file(directory / 'stage.bin'))
    low = padded(read_file(directory / 'resident-sci.bin'))
    if high != padded(stage['payload']) or low != padded(resident['payload']):
        raise ValueError('Observation stage/resident binary differs from the actual linked ELF')
    check_bss(stage, retail_layout.STAGE_ADDRESS, '__retail_stage')
    check_bss(resident, retail_layout.LOW_RESIDENT_ADDRESS, '__retail_resident')
    es, ss, rs = entry['symbols'], stage['symbols'], resident['symbols']
    if (rs.get('__retail_hook_stack_bottom') != 0x8c007800 or
            rs.get('__retail_hook_stack') != 0x8c007d00):
        raise ValueError('Observation must retain the normal guarded low SCI stack')
    for symbol in ('_kui_retail_resident_init', '_kui_retail_resident_hook',
                   '_kui_retail_resident_dispatch', '_kui_retail_gd_dispatch',
                   '_kui_retail_image_read', '_kui_loader_sd_stream_next',
                   '_kui_sci_sd_acquire', '_kui_sci_sd_release'):
        code_symbol(resident, symbol, retail_layout.LOW_RESIDENT_ADDRESS)
    for symbol in ('_kui_retail_hook_active', '_kui_retail_hook_fault'):
        if not retail_layout.LOW_RESIDENT_ADDRESS <= rs.get(symbol, 0) < resident['memory_end']:
            raise ValueError('Missing resident-owned observation guard: ' + symbol)
    for symbol in ('_kui_retail_stage_main', '_kui_retail_stage_relay',
                   '_kui_retail_bootstrap_enter', '_kui_retail_game_resume',
                   '_kui_retail_observe_admit'):
        code_symbol(stage, symbol, retail_layout.STAGE_ADDRESS)
    # The existing high-stage slots all hold the ONE native SCI diagnostic
    # resident. Admission refuses non-SCI and background readers before use.
    for transport in ('scif', 'sci', 'ide', 'scia'):
        prefix = '__retail_resident_' + transport + '_blob_'
        begin, end = ss.get(prefix + 'start', 0), ss.get(prefix + 'end', 0)
        if (begin % 4 or begin < retail_layout.STAGE_ADDRESS or end - begin != len(low) or
                region(stage['payload'], begin - retail_layout.STAGE_ADDRESS,
                       len(low), 'embedded observation SCI resident') != low):
            raise ValueError('Observation stage must embed its exact SCI resident in each legacy slot')
    begin, end = ss.get('__retail_trampoline_start', 0), ss.get('__retail_trampoline_end', 0)
    if begin % 4 or end - begin != retail_layout.TRAMPOLINE_BYTES:
        raise ValueError('Invalid observation executable-entry trampoline')
    trampoline = region(stage['payload'], begin - retail_layout.STAGE_ADDRESS,
                        retail_layout.TRAMPOLINE_BYTES, 'observation trampoline')
    if struct.pack('<I', ss['_kui_retail_game_resume'] | 0x20000000) not in trampoline:
        raise ValueError('Observation trampoline does not target the actual uncached stage relay')
    if (es.get('__retail_map') != ADDRESS + retail_layout.MAP_OFFSET or
            es.get('__retail_stage_blob_start') != ADDRESS + retail_layout.STAGE_BLOB_OFFSET or
            es.get('__retail_stage_blob_end') != ADDRESS + len(entry['payload']) or
            entry['payload'][retail_layout.STAGE_BLOB_OFFSET:] != high or
            entry['memory_end'] != ADDRESS + len(entry['payload']) or
            region(entry['payload'], retail_layout.HEADER_OFFSET, retail_layout.HEADER_BYTES,
                   'observation relocation header') != retail_layout.relocation_header(len(high), low=True)):
        raise ValueError('Invalid observation entry/stage/relocation geometry')
    if any(region(entry['payload'], retail_layout.MAP_OFFSET, retail_layout.MAP_BYTES,
                  'observation card-specific manifest')):
        raise ValueError('Observation must ship with a blank card-specific manifest')
    stack = check_stack_usage(directory / 'sci', rs, 'sci', low=True)
    instructions = {}
    for name in images:
        disassembly = subprocess.check_output(['sh-elf-objdump', '-d', str(directory / (name + '.elf'))],
                                              text=True)
        try:
            instructions[name] = audit(name, disassembly)
        except SystemExit as error:
            raise ValueError(str(error)) from error
    return {
        'images': {name: {'payload_bytes': len(image['payload']),
                         'memory_end': f"0x{image['memory_end']:08x}"}
                   for name, image in images.items()},
        'allocated_sections': sections, 'resident_stack': stack,
        'instruction_audit': instructions, 'compiled_resident_transports': ['SCI'],
        'legacy_blob_slots': 'four exact copies of the same admitted standard SCI resident',
        'resident_address': '0x8c004000', 'resident_limit': '0x8c007800',
        'hook_stack': ['0x8c007800', '0x8c007d00'], 'manifest_slots': 64,
    }


def unpublished_document_copy(data, source, destination, available, tracked):
    """Link source-only references to the corresponding local archive member."""
    def replace(match):
        target = match[2]
        parsed = urlsplit(target)
        if parsed.scheme or parsed.netloc or not parsed.path:
            return match[0]
        original = posixpath.normpath(posixpath.join(
            posixpath.dirname(source), unquote(parsed.path)))
        archive_name(original)
        base = posixpath.dirname(destination) or '.'
        if original in available:
            path = posixpath.relpath(original, base)
            href = urlunsplit(('', '', quote(path, safe='/.-_'), parsed.query, parsed.fragment))
            return match[1] + href + match[3]
        if original not in tracked and not any(name.startswith(original + '/') for name in tracked):
            raise ValueError('Unavailable document target: ' + source + ' -> ' + target)
        href = quote(posixpath.relpath('source-snapshot.tar', base), safe='/.-_')
        member = original + ('#' + parsed.fragment if parsed.fragment else '')
        return (match[1] + href + match[3] +
                ' (source archive member: `' + member.replace('`', '&#96;') + '`)')

    return LINK.sub(replace, data.decode('utf-8')).encode('utf-8')


def collect(commit, published_tree=None, *, unpublished_source=False):
    if git('status', '--porcelain'):
        raise ValueError('Commit reviewed source before packaging')
    checkpoint = git('rev-parse', 'HEAD')
    source_tree = git('rev-parse', 'HEAD^{tree}')
    if unpublished_source:
        if published_tree is not None:
            raise ValueError('--published-tree is forbidden with --unpublished-source')
        if commit != checkpoint:
            raise ValueError('Unpublished source commit must be the exact clean local HEAD')
    elif published_tree is None:
        raise ValueError('--published-tree is required for published-source packaging')
    elif published_tree != source_tree:
        raise ValueError('Published source tree differs from the archived checkpoint')
    if b'PREFLIGHT_CHECKLIST_DRAFT' in read_file(ROOT / 'docs/cdda-preflight-test.md'):
        raise ValueError('Freeze the console checklist against both implementations before packaging')
    tracked = {name for name in subprocess.check_output(
        ['git', 'ls-files', '-z'], cwd=ROOT).decode('utf-8').split('\0') if name}
    for name in tracked:
        archive_name(name)
    files = {}

    def add(name, data):
        archive_name(name)
        if name in files:
            raise ValueError('Duplicate archive path: ' + name)
        files[name] = data

    def evidence(build, prefix):
        records = {}
        for path in sorted(build.rglob('*')):
            if not path.is_file() or path.suffix not in ('.elf', '.map', '.su'):
                continue
            relative = path.relative_to(build).as_posix()
            data = read_file(path)
            add(prefix + relative, data)
            records[relative] = sha(data)
        if not any(name.endswith('.su') for name in records):
            raise ValueError('Missing compiler stack reports: ' + str(build.relative_to(ROOT)))
        return records

    preflight = ROOT / PREFLIGHT_BUILD
    config_bytes = read_file(preflight / 'build-config')
    config = build_config(preflight / 'build-config')
    if config.get('BUILD') != commit[:12] or config.get('PROFILE') != '13':
        raise ValueError('Wrong source/profile configuration for image preflight')
    runtime = read_file(preflight / 'cdda-harness.kui')
    info = verify(runtime)
    elf = read_file(preflight / 'cdda-harness.elf')
    payload, memory = flatten_elf(elf)
    if (info['build'] != commit[:12] or runtime[64:] != payload or
            info['memory_bytes'] != memory or PREFLIGHT_LABEL not in payload or
            OBSERVATION_LABEL in payload or commit[:12].encode() + b'\0' not in payload):
        raise ValueError('Envelope/ELF/profile identity mismatch for image preflight')
    layout = preflight_layout(elf, payload, memory)
    map_bytes = read_file(preflight / 'cdda-harness.map')
    required = (b'cdda_preflight.o', b'cdda_preflight_storage.o')
    loaded = loaded_objects(map_bytes)
    if any(loaded.count(name) != 1 for name in required):
        raise ValueError('Preflight map must link the new image and storage checker exactly once')
    add('KUI/runtime.kui', runtime)
    add(PREFLIGHT_FILE, runtime)
    add('evidence/image-preflight/build-config', config_bytes)
    preflight_evidence = evidence(preflight, 'evidence/image-preflight/')
    fatfs = {name: read_file(preflight / 'fatfs' / name) for name in FATFS_FILES}
    configuration = fatfs['ffconf.h'].decode('utf-8')
    for key, value in (('FF_FS_READONLY', 1), ('FF_FS_EXFAT', 1),
                       ('FF_FS_MINIMIZE', 0), ('FF_USE_FASTSEEK', 1)):
        if not re.search(r'^#define\s+' + key + r'\s+' + str(value) + r'\b',
                         configuration, flags=re.MULTILINE):
            raise ValueError('Unexpected private preflight FatFs configuration: ' + key)
    for name, data in fatfs.items():
        add('source-fatfs/' + name, data)

    observe = ROOT / OBSERVATION_BUILD
    observe_config_bytes = read_file(observe / 'build-config')
    observe_config = build_config(observe / 'build-config')
    if (observe_config.get('BUILD') != commit[:12] or observe_config.get('PROFILE') != '14' or
            any(observe_config.get(key) != value for key, value in
                (('LOW', '1'), ('SLOTS', '64'), ('SCI', '1'), ('AUDIO', '1')))):
        raise ValueError('Wrong source/profile configuration for retail observation')
    retail = read_file(observe / 'retail-observe.kui')
    retail_info = inspect_retail(retail)
    entry_elf = read_file(observe / 'entry.elf')
    entry_payload, entry_memory = flatten_elf(entry_elf)
    if (retail_info['build'] != commit[:12] or retail[64:] != entry_payload or
            retail_info['memory_bytes'] != entry_memory or
            OBSERVATION_LABEL not in entry_payload or PREFLIGHT_LABEL in entry_payload or
            commit[:12].encode() + b'\0' not in entry_payload):
        raise ValueError('Envelope/ELF/profile identity mismatch for retail observation')
    observed_layout = observation_layout(observe, entry_elf)
    add(OBSERVATION_FILE, retail)
    add('evidence/retail-observe/build-config', observe_config_bytes)
    observe_evidence = evidence(observe, 'evidence/retail-observe/')

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
    source_url = None if unpublished_source else 'https://github.com/TPMJB/K-UI-NeXT/tree/' + commit
    if unpublished_source:
        add('source-checkpoint.txt', ('Unpublished source checkpoint\nCommit: ' + checkpoint +
                                    '\nTree: ' + source_tree +
                                    '\nCorresponding source: source-snapshot.tar\n').encode())
    else:
        add('source-url.txt', (source_url + '\n').encode())
    snapshot = subprocess.check_output(['git', 'archive', '--format=tar', 'HEAD'], cwd=ROOT)
    add('source-snapshot.tar', snapshot)
    documents = []
    for path in sorted((ROOT / 'docs').glob('cdda-*.md')):
        source = path.relative_to(ROOT).as_posix()
        documents.extend(((source, source), (source, path.name)))
    for path in sorted((ROOT / 'docs/evidence').glob('cdda-*.md')):
        source = path.relative_to(ROOT).as_posix()
        documents.extend(((source, source), (source, 'evidence/' + path.name)))
    documents.extend((('docs/cdda-preflight-test.md', 'README.md'), ('THIRD_PARTY.md', 'THIRD_PARTY.md')))
    available = set(files) | {destination for _, destination in documents} | {'build.json', 'SHA256SUMS'}
    document_hashes = {}
    for source, destination in documents:
        data = read_file(ROOT / source)
        document_hashes[source] = sha(data)
        if unpublished_source:
            copied = unpublished_document_copy(data, source, destination, available, tracked)
            if destination == 'README.md':
                copied = (('This package uses unpublished source checkpoint `' + checkpoint +
                           '`. Its corresponding source is included in [source-snapshot.tar](source-snapshot.tar).\n\n').encode() + copied)
        else:
            copied = document_copy(data, source, destination, available, tracked, commit)
        add(destination, copied)
    metadata = {
        'source_commit': commit, 'source_tree': source_tree,
        'local_checkpoint': checkpoint, 'source_dirty': False,
        'source_publication': ('unpublished; exact clean local checkpoint' if unpublished_source else
                               'published; supplied tree matches local checkpoint'),
        'source_url': source_url, 'source_snapshot': 'source-snapshot.tar',
        'source_snapshot_sha256': sha(snapshot),
        'compiler': subprocess.check_output(['sh-elf-gcc', '--version'], text=True).splitlines()[0],
        'scope': 'Read-only complete Toy image preflight followed by separately installed baseline SCI retail observation; no integrated CDDA playback',
        'default_profile_number': 13, 'hardware_tested': False,
        'game_files_or_descriptor_installed': False,
        'retail_observation_installed_on_extraction': False,
        'working_runtime_or_retail_package_replaced_in_bundle': False,
        'source_fatfs_sha256': {name: sha(data) for name, data in fatfs.items()},
        'document_source_sha256': document_hashes,
        'required_image': {
            'file': 'TOY_COMMANDER.gdi', 'descriptor_bytes': TOY_DESCRIPTOR_BYTES,
            'descriptor_sha256': TOY_DESCRIPTOR_SHA256, 'complete_backing_tracks': 15,
            'location': 'existing complete original game folder outside /KUI/tests/cdda',
            'autodiscovery': True, 'optional_path_file': '/KUI/tests/cdda/preflight.cfg',
            'copied_game_content_included': False,
        },
        'preflight_contract': {
            'read_only': True, 'game_launched': False, 'audio_started': False,
            'complete_backing_tracks': 15, 'audio_tracks': 12, 'data_tracks': 3,
            'descriptor_selection': 'exact original descriptor; one complete candidate or explicit configured path',
            'scan_limits': {'maximum_depth': 8, 'directories': 128, 'entries': 4096},
            'excluded_scan_directory': '/KUI/tests/cdda',
            'maximum_extents_per_backing': 160,
            'future_complete_map_slots': 64,
            'ip_hash_bytes': 32768, 'boot_hash': 'exact ISO boot extent length excluding final sector padding',
            'payload_identity_hashes': ['CRC32', 'SHA256'],
            'raw_sector_checks': ['sync', 'Mode1', 'FAD header'],
            'audio_gaps': 'unmapped; no inferred extension to the next track',
            'read_deadline_seconds': 180, 'manual_cancel': False,
            'report_pages': 6, 'automatic_page_seconds': 15,
            'required_completed_stages': 6, 'required_failures': 0,
            'retail_resource_ownership_established': False,
        },
        'observation_contract': {
            'installation': 'manual only after preflight PASS',
            'storage_transport': 'SCI', 'reader': 'native standard',
            'complete_manifest_slot_limit': 64,
            'audio_extents_required': True, 'silent_audio_extent_drop_allowed': False,
            'integrated_cdda_playback': False,
            'return_control': 'A+B+X+Y+Start', 'report_pages': 4,
            'each_page_frames': 1200,
            'report_returns_to_firmware': True,
            'resident_fault_report_preserved': True,
            'resident_fault_final_page_held_until_power_off': True,
            'observations': ['accepted PLAY20/PLAY21 parameters', 'caller CPU state',
                             'TMU state', 'sparse AICA/G2 state'],
            'resource_samples_establish_ownership': False,
            'call_bound_sampling_bounds_idle_service_gaps': False,
            'artificial_console_pass_counters': False,
        },
        'profiles': [
            {'profile': 13, 'file': PREFLIGHT_FILE, 'default_card_runtime': 'KUI/runtime.kui',
             'build': info['build'], 'runtime_sha256': sha(runtime), 'envelope': info,
             'linked_layout': layout, 'build_configuration': config,
             'build_configuration_sha256': sha(config_bytes), 'evidence_sha256': preflight_evidence,
             'scope': 'identity, complete track metadata, raw headers and backing extent admission; no launch or sound ownership'},
            {'profile': 14, 'file': OBSERVATION_FILE, 'install_only_after_profile13_pass': True,
             'manual_card_target': '/KUI/apps/games/retail-boot.kui',
             'required_runtime': 'retained working 1.8.5 runtime',
             'build': retail_info['build'], 'runtime_sha256': sha(retail), 'envelope': retail_info,
             'linked_layout': observed_layout, 'build_configuration': observe_config,
             'build_configuration_sha256': sha(observe_config_bytes), 'evidence_sha256': observe_evidence,
             'scope': 'normal SCI reader and bounded retail diagnostics; no music or new AICA ownership'},
        ],
    }
    add('build.json', (json.dumps(metadata, indent=2) + '\n').encode())
    if any(name.startswith('KUI/apps/') for name in files):
        raise ValueError('Observation must require a separate manual installation')
    if any(name.startswith('KUI/tests/') for name in files):
        raise ValueError('This bundle must not install a game descriptor or test image')
    if files['KUI/runtime.kui'] != files[PREFLIGHT_FILE] or retail == runtime:
        raise ValueError('Default preflight and separate retail observation must be distinct')
    if (git('status', '--porcelain') or git('rev-parse', 'HEAD^{tree}') != source_tree or
            git('rev-parse', 'HEAD') != checkpoint):
        raise ValueError('Source changed while collecting the package')
    add('SHA256SUMS', ''.join(sha(data) + '  ' + name + '\n'
                            for name, data in sorted(files.items())).encode())
    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('--source-commit', required=True, type=object_id)
    parser.add_argument('--published-tree', type=object_id,
                        help='Tree SHA independently verified on the published commit')
    parser.add_argument('--unpublished-source', action='store_true',
                        help='Package the exact clean local HEAD without claiming source publication')
    args = parser.parse_args()
    try:
        if args.unpublished_source and args.published_tree is not None:
            raise ValueError('--published-tree is forbidden with --unpublished-source')
        if not args.unpublished_source and args.published_tree is None:
            raise ValueError('--published-tree is required for published-source packaging')
        output = args.output.resolve()
        if output.name != 'K-UI-CDDA-Integration-Tests.zip':
            raise ValueError('Bundle filename must be K-UI-CDDA-Integration-Tests.zip')
        if output.exists() and not output.is_file():
            raise ValueError('Output must be a regular file')
        if output.is_relative_to(ROOT):
            relative = output.relative_to(ROOT).as_posix()
            tracked = subprocess.run(['git', 'ls-files', '--error-unmatch', '--', relative],
                                     cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            if tracked.returncode == 0:
                raise ValueError('Output must not overwrite a tracked repository file')
        files = collect(args.source_commit, args.published_tree,
                        unpublished_source=args.unpublished_source)
        write_archive(output, files)
        print(json.dumps({'file': str(output), 'bytes': output.stat().st_size,
                          'build': args.source_commit[:12], 'profiles': [13, 14],
                          'sha256': sha(output.read_bytes())}, indent=2))
    except (OSError, ValueError, UnicodeError, struct.error, subprocess.SubprocessError,
            zipfile.BadZipFile) as error:
        parser.exit(1, 'FAIL: ' + str(error) + '\n')


if __name__ == '__main__':
    main()
