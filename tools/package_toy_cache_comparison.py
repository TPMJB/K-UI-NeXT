#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package the controlled private-P2 Toy cache comparison and exact fallback."""
import argparse
import io
import json
from pathlib import Path
import re
import struct
import subprocess
import zipfile

from check_loader_layout import EH, inspect_elf, padded, region
from check_retail_loader_layout import (FORBIDDEN_PREFIXES, FORBIDDEN_SYMBOLS,
                                       check_bss, code_symbol)
from check_retail_instructions import audit
from package_cdda_calibration import (ROOT, archive_name, build_config, git,
                                      read_file, sha, write_archive)
from package_cdda_preflight import allocated_sections
from package_cdda_toy_pilot import (
    BOOT_SHA256, DRIVER_SHA256, GD_TIMING_WORDS, LEASE_BYTES, PILOT_LABEL, SNAPSHOT_VERSION,
    WORKER_BASE, WORKER_LIMIT, WORKER_EXPORTS, conservative_stack, embedded_blob,
    fixed_step_store_audit, linked_symbol_sizes, pilot_contract, pilot_low_stack,
    pure_worker_leaf_audit, ring_contract, scalar_worker_adapter_audit,
    snapshot_legend, stack_rows,
)
import retail_package as retail_layout
from retail_package import inspect_retail
from runtime_package import flatten_elf, verify

OUTPUT_NAME = 'K-UI-Toy-Cache-Comparison.zip'
README_SOURCE = 'docs/cdda-toy-cache-test.md'
FALLBACK_DEFAULT = ROOT.parent / 'audio-rollback-delivery/K-UI-Toy-Audio-Rollback.zip'
FALLBACK_ZIP_SHA = '0c56ac11e7763403f7015604c7d6073266e96cf9b5f6a2db1ce7ee676033261d'
FALLBACK_COMMIT = '7b55156aafa26a74c0c3ef3f595a9756cba5c73d'
FALLBACK_GAME_SHA = '161ea24d655b6531867c6704c57a57c1ee25668e534d2f56dc793a5cd73a80de'
FALLBACK_SOURCE_SHA = 'aa34fca921b1f0abfe5bb1854dfc61791eeb040f9d36466276ac07634e881da0'
RUNTIME_MEMBER = 'KUI/apps/games/retail-boot.kui'
PROFILES = ((False, 'R', 'profiles/R-private-write-through.kui'),
            (True, 'C', 'profiles/C-native-copy-back.kui'))
FALLBACK_MEMBER = 'fallback/7b55156aafa2-retail-boot.kui'
PROTOCOL_SOURCES = ('src/loader/sci_sd_bus.c', 'src/loader/sd_reader.c',
                    'src/loader/retail_sd.c', 'src/loader/retail_sd.h',
                    'src/loader/sd_reader.h', 'src/loader/sci_sd_bus.h')
UNCHANGED_AUDIO_SOURCES = ('include/kui/toy_pilot_ring.h', 'src/core/toy_pilot.c')


def profile_build_id(commit, native_cache):
    """Bind each visible twelve-hex build ID to source and all comparison flags."""
    if not re.fullmatch(r'[0-9a-f]{40}', commit) or native_cache not in (False, True, 0, 1):
        raise ValueError('Invalid cache profile source identity')
    identity = (commit + '\nPRIVATE_P2=1\nNATIVE_CACHE=' + str(int(native_cache)) +
                '\nGD_FIXED_STEP=2\nSHARED_SCI=0\nASYNC_CDDA=0\nSCI_REUSE_TDRE=0\n')
    return sha(identity.encode())[:12]


def physical(address):
    if 0xac000000 <= address < 0xad000000:
        return address - 0x20000000
    return address


def private_worker(raw):
    """Use the strict ELF parser; the export table is data before the code entry."""
    image = inspect_elf(raw, WORKER_BASE, WORKER_LIMIT, private_p2=True,
                        entry_symbol='_kui_toy_pilot_initialize', entry_at_base=False)
    image['symbol_sizes'] = linked_symbol_sizes(raw)
    exports = WORKER_EXPORTS.unpack_from(image['payload'])
    image['exports'] = list(exports)
    if exports[:3] != (0x54595031, SNAPSHOT_VERSION, WORKER_EXPORTS.size):
        raise ValueError('Private worker export ABI differs from snapshot version 8')
    if EH.unpack_from(raw)[4] != exports[3]:
        raise ValueError('Private worker entry does not match its initialize export')
    names = {3: '_kui_toy_pilot_initialize', 4: '_kui_toy_pilot_request',
             5: '_kui_toy_pilot_service_hook', 6: '_kui_toy_pilot_am_init_hook',
             7: '_kui_toy_pilot_shutdown_hook', 8: '_kui_toy_pilot_snapshot',
             15: '_kui_toy_pilot_allstop_hook', 16: '_kui_toy_pilot_driver_load_hook',
             17: '_kui_toy_pilot_gd_dispatch', 18: '_kui_toy_pilot_pause_hook'}
    sections = allocated_sections(raw)
    for index, name in names.items():
        address = exports[index]
        if (image['symbols'].get(name) != address or address % 2 or
                address < WORKER_BASE + WORKER_EXPORTS.size or not any(
                    item['flags'] & 4 and item['address'] <= address <
                    item['address'] + item['bytes'] for item in sections.values())):
            raise ValueError('Private worker export does not identify linked P1 code: ' + name)
    bss_begin, bss_end, bottom, top, worker_end, lease_bytes = exports[9:15]
    if (any(not 0xacfd0000 <= value <= 0xacfe0000 or value % 32
            for value in (bss_begin, bss_end, bottom, top)) or
            not bss_begin <= bss_end <= bottom < top or top - bottom != 8192 or
            worker_end != image['memory_end'] or worker_end != physical(top) or
            not WORKER_BASE < worker_end <= WORKER_LIMIT or lease_bytes != LEASE_BYTES or
            image['symbols'].get('__toy_pilot_binary_end') !=
            ((WORKER_BASE + len(image['payload']) + 31) & ~31) or
            physical(bss_begin) < image['symbols']['__toy_pilot_binary_end']):
        raise ValueError('Private worker BSS, physical lease or guarded stack differs')
    expected = {'__toy_pilot_bss_begin': bss_begin, '__toy_pilot_bss_end': bss_end,
                '__toy_pilot_stack_bottom': bottom, '__toy_pilot_stack_top': top,
                '__toy_pilot_worker_end': worker_end}
    if any(image['symbols'].get(name) != value for name, value in expected.items()):
        raise ValueError('Private worker export reservations disagree with linked symbols')
    return image


def private_layout(directory, remember, native_cache):
    """Retain the original package gates, with explicit P2 writable reservations."""
    bounds = {'entry': (retail_layout.EXEC_ADDRESS, retail_layout.EXEC_ADDRESS +
                       retail_layout.STAGE_BLOB_OFFSET + retail_layout.STAGE_MAX_BYTES),
              'stage': (retail_layout.STAGE_ADDRESS, retail_layout.STAGE_MEMORY_END),
              'resident-sci': (retail_layout.LOW_RESIDENT_ADDRESS,
                               retail_layout.LOW_RESIDENT_LIMIT),
              'worker': (WORKER_BASE, WORKER_LIMIT)}
    images, sections = {}, {}
    for name, (base, limit) in bounds.items():
        raw = remember(directory / (name + '.elf'))
        images[name] = (private_worker(raw) if name == 'worker' else
                        inspect_elf(raw, base, limit, private_p2=name == 'resident-sci'))
        sections[name] = allocated_sections(raw)
        forbidden = [symbol for symbol in images[name]['symbols'] if
                     symbol.startswith(FORBIDDEN_PREFIXES) or symbol in FORBIDDEN_SYMBOLS or
                     symbol.startswith('_kui_cdda_aica_')]
        if forbidden:
            raise ValueError('Unexpected runtime/detached sound owner: ' + forbidden[0])
    entry, stage, resident, worker = (images[name] for name in bounds)
    es, ss, rs, ws = (image['symbols'] for image in (entry, stage, resident, worker))
    sizes = linked_symbol_sizes(remember(directory / 'resident-sci.elf'))
    for name in ('read', 'write', 'copy', 'publish'):
        code_symbol(worker, '_kui_toy_pilot_bus_' + name, WORKER_BASE)
    code_symbol(worker, '_kui_toy_pilot_lease_allocate', WORKER_BASE)
    ring_symbols = {}
    for name in ('_ring_observe', '_ring_write_allowed', '_ring_recover'):
        matches = [symbol for symbol in ws if symbol == name or symbol.startswith(name + '.')]
        if not matches:
            raise ValueError('Missing emitted ring ownership helper: ' + name)
        for symbol in matches:
            code_symbol(worker, symbol, WORKER_BASE)
            ring_symbols[symbol] = {'address': f'0x{ws[symbol]:08x}',
                                    'bytes': worker['symbol_sizes'][symbol]}
    for address in (0x8c0840d6, 0x8c083fae, 0x8c069c00, 0x8c083d34,
                    0x8c068f20, 0x8c068b28, 0x8c069158, 0x8c0690d4, 0x8c068a04):
        if struct.pack('<I', address) in worker['payload']:
            raise ValueError('Worker retains an unbounded sound helper literal')
    blobs = {}
    for name in ('stage', 'resident-sci', 'worker'):
        raw = remember(directory / (name + '.bin'))
        actual = raw if name == 'worker' else padded(raw)
        expected = images[name]['payload'] if name == 'worker' else padded(images[name]['payload'])
        if actual != expected:
            raise ValueError(name + '.bin differs from its strict linked ELF load bytes')
        blobs[name] = actual
    check_bss(stage, retail_layout.STAGE_ADDRESS, '__retail_stage')
    bss_begin, bss_end = (rs.get('__retail_resident_' + name, 0)
                          for name in ('bss_begin', 'bss_end'))
    binary_end = rs.get('__retail_resident_binary_end', 0)
    file_end = retail_layout.LOW_RESIDENT_ADDRESS + len(resident['payload'])
    if (not 0xac004000 <= bss_begin <= bss_end <= 0xac007800 or
            bss_begin % 32 or bss_end % 32 or physical(bss_end) != resident['memory_end'] or
            not file_end <= binary_end <= file_end + 31 or binary_end > physical(bss_begin) or
            rs.get('__retail_hook_stack_bottom') != 0xac007800 or
            rs.get('__retail_hook_stack') != 0xac007d00 or
            rs.get('__retail_hook_stack_bottom_physical') != 0x8c007800 or
            rs.get('__retail_hook_stack_physical') != 0x8c007d00):
        raise ValueError('Private resident BSS/physical reservation/stack geometry differs')
    for name in ('_kui_retail_resident_init', '_kui_retail_resident_hook',
                 '_kui_retail_resident_dispatch', '_kui_retail_gd_dispatch',
                 '_kui_retail_image_read', '_kui_loader_sd_stream_next',
                 '_kui_sci_sd_acquire', '_kui_sci_sd_release',
                 '_kui_toy_pilot_read_raw', '_kui_toy_pilot_heap_hook',
                 '_kui_toy_pilot_return_hook'):
        code_symbol(resident, name, retail_layout.LOW_RESIDENT_ADDRESS)
    for name in ('_kui_retail_hook_active', '_kui_retail_hook_fault'):
        if not bss_begin <= rs.get(name, 0) < bss_end:
            raise ValueError('Missing protected P2 resident hook state')
    for name in ('_kui_retail_stage_main', '_kui_retail_stage_relay',
                 '_kui_retail_bootstrap_enter', '_kui_retail_game_resume',
                 '_kui_toy_pilot_stage_install', '_kui_toy_pilot_stage_publish'):
        code_symbol(stage, name, retail_layout.STAGE_ADDRESS)
    if any(label + b'\0' not in resident['payload'] for label in (PILOT_LABEL, b'WORDS')):
        raise ValueError('Low resident lacks its exact report legend')
    if bytes.fromhex(BOOT_SHA256) not in stage['payload'] or bytes.fromhex(DRIVER_SHA256) not in worker['payload']:
        raise ValueError('Original executable or ARM input identity changed')
    timing = rs.get('_toy_gd_timing', 0)
    if (sizes.get('_toy_gd_timing') != 24 or timing % 4 or not bss_begin <= timing <= bss_end - 24 or
            not 0 < sizes.get('_toy_gd_clock_valid', 0) <= 96 or
            not 0 < sizes.get('_step', 0) <= 512 or
            struct.pack('<I', 0x47444d31) not in resident['payload']):
        raise ValueError('Unchanged two-sector timing/report identity differs')
    code_symbol(resident, '_toy_gd_clock_valid', retail_layout.LOW_RESIDENT_ADDRESS)
    code_symbol(resident, '_step', retail_layout.LOW_RESIDENT_ADDRESS)
    timing_audit = fixed_step_store_audit(resident, sizes['_step'], 2)
    from toy_pilot_symbols import generate
    if remember(directory / 'toy_pilot_resident_symbols.h') != generate(
            directory / 'resident-sci.elf', private_p2=True).encode():
        raise ValueError('Stage bindings do not match this strict private resident')
    embedded = {}
    for transport in ('scif', 'sci', 'ide', 'scia'):
        prefix = '__retail_resident_' + transport + '_blob_'
        embedded[transport] = embedded_blob(stage, prefix + 'start', prefix + 'end',
            retail_layout.STAGE_ADDRESS, blobs['resident-sci'], 'SCI resident ' + transport)
    if len({(item['address'], item['bytes']) for item in embedded.values()}) != 1:
        raise ValueError('Comparison must embed one baseline SCI resident')
    embedded['worker'] = embedded_blob(stage, '__toy_pilot_worker_blob_start',
        '__toy_pilot_worker_blob_end', retail_layout.STAGE_ADDRESS, blobs['worker'], 'private worker')
    begin, end = ss.get('__retail_trampoline_start', 0), ss.get('__retail_trampoline_end', 0)
    if begin % 4 or end - begin != retail_layout.TRAMPOLINE_BYTES:
        raise ValueError('Invalid bounded title-entry trampoline')
    trampoline = region(stage['payload'], begin - retail_layout.STAGE_ADDRESS,
                         retail_layout.TRAMPOLINE_BYTES, 'trampoline')
    if struct.pack('<I', ss['_kui_retail_game_resume'] | 0x20000000) not in trampoline:
        raise ValueError('Trampoline does not target actual uncached stage relay')
    if (es.get('__retail_map') != retail_layout.EXEC_ADDRESS + retail_layout.MAP_OFFSET or
            es.get('__retail_stage_blob_start') != retail_layout.EXEC_ADDRESS + retail_layout.STAGE_BLOB_OFFSET or
            es.get('__retail_stage_blob_end') != retail_layout.EXEC_ADDRESS + len(entry['payload']) or
            entry['payload'][retail_layout.STAGE_BLOB_OFFSET:] != blobs['stage'] or
            entry['memory_end'] != retail_layout.EXEC_ADDRESS + len(entry['payload']) or
            region(entry['payload'], retail_layout.HEADER_OFFSET, retail_layout.HEADER_BYTES,
                   'relocation header') != retail_layout.relocation_header(len(blobs['stage']), low=True) or
            any(region(entry['payload'], retail_layout.MAP_OFFSET, retail_layout.MAP_BYTES,
                       'card-specific manifest'))):
        raise ValueError('Entry/stage/runtime header or blank manifest geometry differs')
    disassemblies, instruction_audits = {}, {}
    for name in bounds:
        disassembly = subprocess.check_output(['sh-elf-objdump', '-d',
            str(directory / (name + '.elf'))], text=True)
        disassemblies[name] = disassembly
        try:
            instruction_audits[name] = audit(name, disassembly)
        except SystemExit as error:
            raise ValueError(str(error)) from error
    leaf = pure_worker_leaf_audit(worker, disassemblies['worker'])
    scalar = scalar_worker_adapter_audit(worker, disassemblies['worker'])
    from toy_pilot_cache_audit import audit_cache_layout
    cache = audit_cache_layout(resident, worker, stage, disassemblies['resident-sci'],
        disassemblies['worker'], disassemblies['stage'], native_cache=bool(native_cache), source_root=ROOT)
    pause = cache['pause_stack']
    scratch = cache['native_scratch']
    low_stack = pilot_low_stack(directory, rs, ws)
    bottom, top = ws['__toy_pilot_stack_bottom'], ws['__toy_pilot_stack_top']
    stack = conservative_stack(directory / 'worker', stack_bytes=top - bottom, assembly_bytes=256)
    callback_rows = [row for row in stack_rows(list((directory / 'sci/lto').glob('*.ltrans*.su')))
                     if row['function'] != 'kui_retail_resident_init']
    stack['callback_low_c_frames'] = len(callback_rows)
    stack['callback_low_c_bytes'] = sum(row['bytes'] for row in callback_rows)
    stack['conservative_bytes'] += stack['callback_low_c_bytes']
    stack['margin'] = stack['available_bytes'] - stack['conservative_bytes']
    if stack['margin'] < 0:
        raise ValueError('Worker plus low raw callback exceeds guarded private stack')
    return {'images': {name: {'payload_bytes': len(image['payload']),
                'memory_end_physical': f"0x{image['memory_end']:08x}", 'unresolved_symbols': 0}
                for name, image in images.items()},
            'allocated_sections': sections, 'embedded_blobs': embedded,
            'resident_stack': low_stack, 'worker_stack': stack,
            'instruction_audit': instruction_audits, 'GD_pure_worker_leaf_audit': leaf,
            'GD_scalar_worker_adapter_audit': scalar, 'native_pause_stack_audit': pause,
            'native_scratch_audit': scratch, 'cache_isolation_audit': cache,
            'GD_fixed_step_store_audit': timing_audit,
            'continuous_ring_identity_audit': ring_symbols,
            'compiled_resident_transports': ['SCI'], 'embedded_resident_copies': 1,
            'worker_export_words': worker['exports'], 'private_P2': True,
            'resident_physical_reservation': ['0x8c004000', '0x8c007800'],
            'resident_runtime_stack': ['0xac007800', '0xac007d00'],
            'worker_physical_reservation': ['0x8cfd0000', '0x8cfe0000'],
            'worker_runtime_stack': [f'0x{bottom:08x}', f'0x{top:08x}']}


def fallback_inputs(path):
    path = Path(path)
    if path.is_symlink() or not path.is_file():
        raise ValueError('Fallback must be the retained regular ZIP file')
    data = path.read_bytes()
    if sha(data) != FALLBACK_ZIP_SHA:
        raise ValueError('Fallback ZIP differs from the exact retained rollback delivery')
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        names = archive.namelist()
        if len(names) != len(set(names)) or archive.testzip():
            raise ValueError('Fallback ZIP has duplicate or damaged members')
        for name in names:
            archive_name(name)
        sums = {}
        for line in archive.read('SHA256SUMS').decode().splitlines():
            match = re.fullmatch(r'([0-9a-f]{64})  (.+)', line)
            if not match or match[2] in sums:
                raise ValueError('Fallback checksum manifest is malformed')
            sums[archive_name(match[2])] = match[1]
        if set(sums) != set(names) - {'SHA256SUMS'} or any(
                sha(archive.read(name)) != digest for name, digest in sums.items()):
            raise ValueError('Fallback checksum tree does not cover its exact members')
        game = archive.read(RUNTIME_MEMBER)
        metadata = json.loads(archive.read('build.json'))
        source_name = 'source/7b55156aafa2-source.tar.gz'
        source = archive.read(source_name)
        if (sha(game) != FALLBACK_GAME_SHA or verify(game)['build'] != FALLBACK_COMMIT[:12] or
                metadata.get('runtime_source_commit') != FALLBACK_COMMIT or
                metadata.get('runtime_sha256') != FALLBACK_GAME_SHA or
                metadata.get('source_archive') != source_name or
                metadata.get('source_archive_sha256') != FALLBACK_SOURCE_SHA or
                sha(source) != FALLBACK_SOURCE_SHA):
            raise ValueError('Fallback runtime or exact corresponding source identity changed')
    return data, game, source, metadata


def collect(write_through, native, fallback, *, host_test_log=None):
    commit, tree = git('rev-parse', 'HEAD'), git('rev-parse', 'HEAD^{tree}')
    if git('status', '--porcelain'):
        raise ValueError('Commit the complete reviewed source before packaging')
    files, inputs = {}, {}

    def add(name, data):
        archive_name(name)
        if name in files:
            raise ValueError('Duplicate archive path: ' + name)
        files[name] = data

    def remember(path):
        path = Path(path)
        data = read_file(path)
        digest = sha(data)
        if path in inputs and inputs[path] != digest:
            raise ValueError('Input changed during collection: ' + str(path))
        inputs[path] = digest
        return data

    schema = {'snapshot_version': SNAPSHOT_VERSION, 'snapshot_bytes': 448,
              'snapshot_pages': snapshot_legend(), 'GD_timing_page': list(GD_TIMING_WORDS),
              'PCM': {'format': 'signed 16-bit little-endian stereo', 'sample_rate': 44100,
                      'raw_sector_bytes': 2352, 'frames_per_raw_sector': 588,
                      'raw_sector_cache_count': 1, 'altered_samples': False},
              'pilot': pilot_contract(remember(ROOT / 'include/kui/toy_pilot.h')),
              'ring': ring_contract(remember(ROOT / 'include/kui/toy_pilot_ring.h'))}
    unchanged = {}
    for name in (*PROTOCOL_SOURCES, *UNCHANGED_AUDIO_SOURCES):
        data = remember(ROOT / name)
        baseline = subprocess.check_output(['git', 'show', FALLBACK_COMMIT + ':' + name], cwd=ROOT)
        if data != baseline:
            raise ValueError('Cache comparison changed retained protocol/audio proof source: ' + name)
        unchanged[name] = sha(data)
    profile_metadata = []
    for (native_cache, label, member), supplied in zip(PROFILES, (write_through, native)):
        build = (ROOT / supplied).resolve()
        if not build.is_relative_to(ROOT) or build == ROOT:
            raise ValueError('Profile build evidence must be inside this source checkout')
        config = build_config(build / 'build-config')
        required = {'BUILD': profile_build_id(commit, native_cache), 'PROFILE': '15',
                    'PILOT': '1', 'LOW': '1', 'SLOTS': '64', 'SCI': '1',
                    'SCI_PACED': '1', 'SCI_FAULT': '1', 'SCI_REUSE_TDRE': '0',
                    'GD_FIXED_STEP': '2', 'SHARED_SCI': '0', 'ASYNC_CDDA': '0',
                    'PRIVATE_P2': '1', 'NATIVE_CACHE': str(int(native_cache)),
                    'OPT': '-Os -fno-tree-scev-cprop'}
        if config != required:
            raise ValueError('Complete controlled build configuration differs for profile ' + label)
        runtime = remember(build / 'retail-toy-pilot.kui')
        envelope = inspect_retail(runtime)
        payload, memory = flatten_elf(remember(build / 'entry.elf'))
        if (runtime[64:] != payload or envelope['memory_bytes'] != memory or
                envelope['build'] != config['BUILD'] or
                config['BUILD'].encode() + b'\0' not in payload or PILOT_LABEL + b'\0' not in payload):
            raise ValueError('Profile runtime/entry ELF/build/report identity differs')
        linked = private_layout(build, remember, native_cache)
        evidence_prefix = 'evidence/' + label + '/'
        add(member, runtime)
        add(evidence_prefix + 'build-config', remember(build / 'build-config'))
        add(evidence_prefix + 'toy_pilot_resident_symbols.h', remember(build / 'toy_pilot_resident_symbols.h'))
        evidence = {}
        for path in sorted(build.rglob('*')):
            if not path.is_file() or path.suffix not in ('.elf', '.map', '.su', '.bin'):
                continue
            data = remember(path)
            name = path.relative_to(build).as_posix()
            if not data:
                raise ValueError('Empty linked build evidence: ' + name)
            add(evidence_prefix + name, data)
            evidence[name] = sha(data)
        if any(not {name + '.elf', name + '.map', name + '.bin'} <= set(evidence)
               for name in ('stage', 'resident-sci', 'worker')) or not {'entry.elf', 'entry.map'} <= set(evidence):
            raise ValueError('Missing actual ELF/map/binary evidence')
        profile_metadata.append({'label': label, 'file': member, 'configuration': config,
            'runtime': envelope, 'runtime_sha256': sha(runtime), 'runtime_bytes': len(runtime),
            'source_commit': commit, 'evidence_sha256': evidence, 'linked_layout': linked,
            'expected_CCR_policy': '0x00000105' if native_cache else '0x00000101',
            'hardware_tested': False, 'measured_performance_gain': None,
            'native_copy_back_experimental': bool(native_cache)})
    if len({item['runtime']['build'] for item in profile_metadata}) != 2:
        raise ValueError('R and C must have distinct visible build IDs')
    fallback_data, fallback_game, fallback_source, fallback_metadata = fallback_inputs(fallback)
    add('fallback/K-UI-Toy-Audio-Rollback.zip', fallback_data)
    add(FALLBACK_MEMBER, fallback_game)
    add('fallback/7b55156aafa2-source.tar.gz', fallback_source)
    add('fallback/build.json', (json.dumps(fallback_metadata, indent=2) + '\n').encode())
    for name in git('ls-files').splitlines():
        archive_name(name)
        if name in ('LICENSE', 'THIRD_PARTY.md') or name.startswith('LICENSES/'):
            add(name, remember(ROOT / name))
    add('README.md', remember(ROOT / README_SOURCE))
    add(README_SOURCE, files['README.md'])
    snapshot = subprocess.check_output(['git', 'archive', '--format=tar', commit], cwd=ROOT)
    add('source-snapshot.tar', snapshot)
    add('source-state.txt', ('Source commit: ' + commit + '\nSource tree: ' + tree + '\n'
        'Exact committed source included; no remote publication is claimed by this packager.\n').encode())
    host = None
    if host_test_log is not None:
        from test_toy_pilot import SUITES
        log = remember((ROOT / host_test_log).resolve())
        footer = f'{len(SUITES) + 7} Toy pilot regression suites passed'.encode()
        if not log.rstrip().endswith(footer) or b'FAIL' in log:
            raise ValueError('Host log lacks exact complete cache regression success footer')
        add('evidence/host-tests.txt', log)
        host = {'suites': len(SUITES) + 7, 'sha256': sha(log)}
    metadata = {'purpose': 'Controlled SCI private-state/cache-policy comparison',
        'source_commit': commit, 'source_tree': tree, 'source_dirty': False,
        'source_snapshot_sha256': sha(snapshot), 'source_publication_claimed': False,
        'test_order': [item['file'] for item in profile_metadata], 'profiles': profile_metadata,
        'comparison_variable': 'NATIVE_CACHE only; PRIVATE_P2=1 in R and C',
        'transport_changed': False, 'queued_SCI_enabled': False,
        'ARM_firmware_changed': False, 'AICA_DMA_added': False,
        'new_video_fix_claimed': False, 'measured_performance_gain': None,
        'unchanged_protocol_and_audio_proof_sources': unchanged, 'schema': schema,
        'host_regressions': host,
        'fallback': {'file': FALLBACK_MEMBER, 'zip': 'fallback/K-UI-Toy-Audio-Rollback.zip',
            'zip_sha256': FALLBACK_ZIP_SHA, 'runtime_sha256': FALLBACK_GAME_SHA,
            'source_commit': FALLBACK_COMMIT, 'source_archive_sha256': FALLBACK_SOURCE_SHA,
            'runtime_reused_byte_for_byte': True},
        'game_data_or_driver_included': False, 'launcher_included': False}
    add('build.json', (json.dumps(metadata, indent=2) + '\n').encode())
    if {name for name in files if name.endswith('.kui')} != {item[2] for item in PROFILES} | {FALLBACK_MEMBER}:
        raise ValueError('Comparison must contain exactly R, C and retained fallback runtimes')
    if git('status', '--porcelain') or git('rev-parse', 'HEAD') != commit or git('rev-parse', 'HEAD^{tree}') != tree:
        raise ValueError('Committed source changed during collection')
    for path, digest in inputs.items():
        if sha(read_file(path)) != digest:
            raise ValueError('Source/build evidence changed during collection: ' + str(path))
    if sha(Path(fallback).read_bytes()) != FALLBACK_ZIP_SHA:
        raise ValueError('Retained fallback changed during collection')
    add('SHA256SUMS', ''.join(sha(data) + '  ' + name + '\n'
                            for name, data in sorted(files.items())).encode())
    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--write-through', type=Path, required=True, metavar='BUILD_DIR')
    parser.add_argument('--native', type=Path, required=True, metavar='BUILD_DIR')
    parser.add_argument('--fallback', type=Path, default=FALLBACK_DEFAULT, metavar='ZIP')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--host-test-log', type=Path)
    args = parser.parse_args()
    try:
        if args.output.exists() or args.output.is_symlink():
            raise ValueError('Use a new output ZIP path; preserve previous deliveries')
        files = collect(args.write_through, args.native, args.fallback, host_test_log=args.host_test_log)
        write_archive(args.output.resolve(), files)
        print(json.dumps({'file': str(args.output.resolve()), 'bytes': args.output.stat().st_size,
                          'sha256': sha(args.output.read_bytes()),
                          'profiles': json.loads(files['build.json'])['test_order']}, indent=2))
    except (OSError, ValueError, UnicodeError, KeyError, struct.error, ImportError,
            subprocess.SubprocessError, zipfile.BadZipFile) as error:
        parser.exit(1, 'FAIL: ' + str(error) + '\n')


if __name__ == '__main__':
    main()
