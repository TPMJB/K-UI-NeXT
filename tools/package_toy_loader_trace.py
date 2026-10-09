#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package the opt-in Toy loader trace and byte-exact audio restoration files."""
import argparse
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import zipfile

from check_loader_layout import EH, inspect_elf, padded, region
from check_retail_instructions import audit as instruction_audit
from package_cdda_calibration import ROOT, archive_name, build_config, git, sha, write_archive
from package_cdda_toy_pilot import embedded_blob, pilot_contract, snapshot_legend
from package_toy_cache_comparison import fallback_inputs
import retail_package as layout
from retail_package import inspect_retail
from runtime_package import flatten_elf

R_BUILD = '73e8363c10f7'
R_SHA = '99e1c0ad84a916fa9e08cf68b494c2d0efe0748fe956e579841c3d7614e49037'
TRACE_WORDS = 416
TRACE_EXPORTS = struct.Struct('<20I')
README_SOURCE = 'docs/toy-loader-trace-test.md'
TRACE_HEADER_FIELDS = (
    'magic', 'version', 'words', 'phase_words', 'record_words', 'worst_per_phase',
    'tick_hz', 'active_phase', 'frozen', 'accepted_play_requests',
    'boundary_command', 'boundary_token', 'boundary_generation',
    'boundary_tick', 'boundary_clock_valid', 'first_tick', 'first_clock_valid',
    'counters_saturated', 'clock_epoch', 'unmatched_ends', 'nested_begins',
    'total_call_samples',
)
TRACE_PHASE_COUNTS = (
    'calls', 'request_calls', 'exec_calls', 'check_calls', 'drive_calls',
    'accepted_data', 'rejected_data', 'requested_sectors', 'requested_bytes',
    'delivered_sectors', 'delivered_bytes', 'progress_exec', 'progress_check',
    'zero_progress_exec', 'data_completed', 'data_failed', 'data_acknowledged',
    'data_aborted', 'data_reset', 'ownership_lost',
    'sequential', 'overlapping', 'forward_seek', 'backward_seek',
    'invalid_clock_calls', 'invalid_intervals', 'sampled_line_wraps',
    'sampled_fb_changes', 'invalid_pvr_samples', 'pvr_geometry_changes',
    'worst_seen', 'worst_valid', 'worst_retained', 'worst_excluded',
    'peak_request_sectors', 'peak_request_bytes',
)
TRACE_METRICS = ('call_body', 'pending_service_gap', 'request_first_credit',
                 'request_complete', 'complete_acknowledge')
TRACE_OUTSTANDING = ('outstanding_token', 'outstanding_lba', 'outstanding_sectors',
                     'outstanding_destination', 'outstanding_credited_bytes',
                     'outstanding_started_tick', 'outstanding_clock_valid',
                     'outstanding_terminal_observed')
TRACE_CONTEXT = ('token', 'lba', 'sectors', 'destination', 'first_credit_ticks',
                 'complete_ticks', 'acknowledge_ticks', 'flags')


def _wire_fields(source, name, types, defines):
    match = re.search(r'struct\s+' + re.escape(name) + r'\s*\{(.*?)\};', source, re.S)
    if not match:
        raise ValueError('Missing trace wire structure: ' + name)
    body = re.sub(r'/\*.*?\*/', '', match[1], flags=re.S)
    words = []
    for statement in body.split(';'):
        if not statement.strip():
            continue
        declaration = re.fullmatch(r'\s*(uint32_t|struct\s+\w+)\s+(.+?)\s*', statement, re.S)
        if not declaration:
            raise ValueError('Unreviewed trace declaration: ' + statement)
        kind, fields = declaration.groups()
        for field in fields.split(','):
            item = re.fullmatch(r'\s*(\w+)(?:\[(\w+)\])?\s*', field)
            if not item:
                raise ValueError('Unreviewed trace field: ' + field)
            count = int(item[2]) if item[2] and item[2].isdigit() else defines.get(item[2], 1)
            if item[2] and not item[2].isdigit() and item[2] not in defines:
                raise ValueError('Unreviewed trace array bound: ' + item[2])
            children = [''] if kind == 'uint32_t' else types.get(kind)
            if children is None:
                raise ValueError('Unreviewed nested trace type: ' + kind)
            for index in range(count):
                prefix = item[1] + (f'[{index}]' if item[2] else '')
                words.extend(prefix + ('.' + child if child else '') for child in children)
    return words


def trace_legend():
    """Parse and compare every delivered word against the reviewed trace schema."""
    source = (ROOT / 'include/kui/toy_loader_trace.h').read_text()
    defines = {'KUI_TOY_LOADER_TRACE_BINS': 8, 'KUI_TOY_LOADER_TRACE_WORST': 8,
               'KUI_TOY_LOADER_TRACE_VERSION': 1, 'KUI_TOY_LOADER_TRACE_TICK_HZ': 781250,
               'KUI_TOY_LOADER_TRACE_HEADER_WORDS': 32,
               'KUI_TOY_LOADER_TRACE_PHASE_WORDS': 192, 'KUI_TOY_LOADER_TRACE_WORDS': TRACE_WORDS}
    for name, value in defines.items():
        found = re.findall(r'^#define\s+' + name + r'\s+(\d+)u\b', source, re.M)
        if found != [str(value)]:
            raise ValueError('Trace constant differs from packaged legend: ' + name)
    if not re.search(r'^#define\s+KUI_TOY_LOADER_TRACE_MAGIC\s+UINT32_C\(0x4c545231\)', source, re.M):
        raise ValueError('Trace report marker differs')
    types = {}
    for name in ('kui_toy_loader_trace_metric', 'kui_toy_loader_trace_record',
                 'kui_toy_loader_trace_phase'):
        types['struct ' + name] = _wire_fields(source, name, types, defines)
    metric = ['samples', 'ticks_total', 'ticks_max', 'ticks_min'] + [f'histogram[{i}]' for i in range(8)]
    phase = list(TRACE_PHASE_COUNTS) + [f'request_size_histogram[{i}]' for i in range(8)]
    for name in TRACE_METRICS:
        phase.extend(name + '.' + field for field in metric)
    phase.extend(TRACE_OUTSTANDING)
    phase.extend('data_service_body.' + field for field in metric)
    phase.extend(f'reserved[{i}]' for i in range(4))
    for index in range(8):
        phase.extend(f'worst[{index}].' + field for field in TRACE_CONTEXT)
    expected = list(TRACE_HEADER_FIELDS) + [f'reserved[{i}]' for i in range(10)]
    for index in range(2):
        expected.extend(f'phase[{index}].' + field for field in phase)
    actual = _wire_fields(source, 'kui_toy_loader_trace_report', types, defines)
    if (types['struct kui_toy_loader_trace_metric'] != metric or
            types['struct kui_toy_loader_trace_record'] != list(TRACE_CONTEXT) or
            types['struct kui_toy_loader_trace_phase'] != phase or
            actual != expected or len(actual) != TRACE_WORDS):
        raise ValueError('Trace wire schema differs from the reviewed package legend')
    return {'magic': '0x4c545231', 'version': 1, 'words': TRACE_WORDS, 'bytes': TRACE_WORDS * 4,
            'header_words': 32, 'phase_words': 192, 'phases': [
                'startup until first accepted PLAY mailbox', 'after first accepted PLAY mailbox'],
            'tick_hz': 781250, 'time_histogram_inclusive_ceilings_ticks': [782, 1563, 3125, 6250, 12500, 25000, 50000],
            'request_size_inclusive_ceilings_sectors': [1, 2, 4, 8, 16, 32, 64],
            'metric_order': list(TRACE_METRICS) + ['data_service_body'], 'context_flags': {
                'first_valid': 1, 'complete_valid': 2, 'ack_valid': 4,
                'success': 8, 'failed': 16, 'aborted': 32},
            'missing_context_time': '0xffffffff; corresponding VALID flag clear',
            'pages': [{'page': page, 'first_word': page * 16,
                       'rows': [actual[page * 16 + row * 4:page * 16 + row * 4 + 4] for row in range(4)]}
                      for page in range(TRACE_WORDS // 16)]}


def legend_markdown(legend):
    lines = ['# Exact TRACE PAGE word legend', '',
             'Read each page from top left across four words, then the next row. '
             'Page numbers and values are hexadecimal on the console; the page column below is decimal.', '',
             '| Page | Hex page | Row | Word 1 | Word 2 | Word 3 | Word 4 |',
             '|---:|---:|---:|---|---|---|---|']
    for page in legend['pages']:
        for row, fields in enumerate(page['rows']):
            lines.append('| ' + ' | '.join([str(page['page']), f"{page['page']:08X}",
                          str(row + 1), *('`' + field + '`' for field in fields)]) + ' |')
    return ('\n'.join(lines) + '\n').encode()


def expected_config(build_id, enabled):
    config = {'BUILD': build_id, 'PROFILE': '15', 'PILOT': '1', 'LOW': '1',
            'SLOTS': '64', 'SCI': '1', 'SCI_PACED': '1', 'SCI_FAULT': '1',
            'SCI_REUSE_TDRE': '0', 'GD_FIXED_STEP': '2', 'SHARED_SCI': '0',
            'ASYNC_CDDA': '0', 'PRIVATE_P2': '1', 'NATIVE_CACHE': '0',
            'SYNTHETIC_SOURCE': '0',
            'OPT': '-Os -fno-tree-scev-cprop'}
    if enabled:
        config['LOADER_TRACE'] = '1'
    return config


def collect(build, default_check, retained_r, fallback, host_log, published_commit=None):
    if git('status', '--porcelain'):
        raise ValueError('Commit exact reviewed source before packaging')
    commit, tree = git('rev-parse', 'HEAD'), git('rev-parse', 'HEAD^{tree}')
    if published_commit is not None and not re.fullmatch(r'[0-9a-f]{40}', published_commit):
        raise ValueError('Published commit must be a full verified object ID')
    inputs, files = {}, {}

    def remember(path):
        path = Path(path)
        if path.is_symlink() or not path.is_file():
            raise ValueError('Package input must be a regular file: ' + str(path))
        data = path.read_bytes()
        digest = sha(data)
        if path in inputs and inputs[path] != digest:
            raise ValueError('Input changed during collection: ' + str(path))
        inputs[path] = digest
        return data

    def add(name, data):
        archive_name(name)
        if name in files:
            raise ValueError('Duplicate package member: ' + name)
        files[name] = data

    for directory in (build, default_check, retained_r):
        directory = Path(directory)
        if not directory.is_relative_to(ROOT) or directory == ROOT:
            raise ValueError('Build evidence must be inside the source checkout')
        if any(parent.is_symlink() for parent in (directory, *directory.parents)):
            raise ValueError('Build evidence path cannot contain symlinks')
    if build_config(build / 'build-config') != expected_config(commit[:12], True):
        raise ValueError('Wrong complete loader-trace configuration')
    if build_config(default_check / 'build-config') != expected_config(R_BUILD, False):
        raise ValueError('Wrong complete disabled-trace configuration')
    host = remember(host_log)
    from test_toy_pilot import SUITES
    footer = f'{len(SUITES) + 7} Toy pilot regression suites passed'.encode()
    if footer not in host.splitlines() or b'FAIL' in host:
        raise ValueError('Host log lacks complete regression success')
    trace_footer = b'Toy loader trace production differential, telemetry and report suites passed'
    if trace_footer not in host.splitlines():
        raise ValueError('Host log lacks the actual loader-trace regression')
    # Execute the independent linked proof now, never accept a supplied JSON
    # assertion. It owns the new export80/boot56 and high-stack contracts.
    from toy_loader_trace_audit import audit_loader_trace
    trace_proof = audit_loader_trace(build)
    exports_proof, report_proof = trace_proof['exports'], trace_proof['trace']
    if (any(exports_proof.get(key) != value for key, value in
            {'version': 8, 'bytes': 80, 'words': 20, 'boot_bytes': 56}.items()) or
            any(report_proof.get(key) != value for key, value in
            {'version': 1, 'report_words': TRACE_WORDS, 'report_bytes': TRACE_WORDS * 4,
             'report_magic': '0x4c545231'}.items())):
        raise ValueError('Independent trace proof does not admit the exact export/boot/report ABI')
    from toy_pilot_cache_audit import audit_cache_profiles
    default_proof = audit_cache_profiles(default_check, native_cache=False)
    pilot_contract(remember(ROOT / 'include/kui/toy_pilot.h'))
    legend = trace_legend()
    runtime = remember(build / 'retail-toy-pilot.kui')
    envelope = inspect_retail(runtime)
    bounds = {'entry': (layout.EXEC_ADDRESS, layout.EXEC_ADDRESS + layout.EXEC_MAX_BYTES),
              'stage': (layout.STAGE_ADDRESS, layout.STAGE_MEMORY_END),
              'resident-sci': (layout.LOW_RESIDENT_ADDRESS, layout.LOW_RESIDENT_LIMIT),
              'worker': (0x8cfd0000, 0x8cfe0000)}
    images = {}
    instructions = {}
    objdump = shutil.which('sh-elf-objdump')
    if objdump is None:
        candidate = ROOT.parent / 'launch-logo-fix/.deps/sh-elf/bin/sh-elf-objdump'
        if not candidate.is_file():
            raise ValueError('SH objdump is required')
        objdump = str(candidate)
    for name, (base, limit) in bounds.items():
        options = {'private_p2': True} if name in ('worker', 'resident-sci') else {}
        if name == 'worker':
            options.update(entry_symbol='_kui_toy_pilot_initialize', entry_at_base=False)
        raw = remember(build / (name + '.elf'))
        images[name] = inspect_elf(raw, base, limit, **options)
        disassembly = subprocess.check_output([objdump, '-d', str(build / (name + '.elf'))], text=True)
        instructions[name] = instruction_audit(name, disassembly)
        if name != 'entry' and remember(build / (name + '.bin')) != images[name]['payload']:
            raise ValueError('Linked binary differs: ' + name)
    exports = TRACE_EXPORTS.unpack_from(images['worker']['payload'])
    if exports[:3] != (0x54595031, 8, TRACE_EXPORTS.size) or EH.unpack_from(
            remember(build / 'worker.elf'))[4] != exports[3]:
        raise ValueError('Trace worker must use explicitly reviewed 80-byte exports')
    payload, memory = flatten_elf(remember(build / 'entry.elf'))
    if runtime[64:] != payload or envelope['memory_bytes'] != memory or envelope['build'] != commit[:12]:
        raise ValueError('Runtime/source/entry identity differs')
    stage = images['stage']
    embedded = {'worker': embedded_blob(stage, '__toy_pilot_worker_blob_start',
        '__toy_pilot_worker_blob_end', layout.STAGE_ADDRESS, images['worker']['payload'], 'trace worker')}
    for transport in ('scif', 'sci', 'ide', 'scia'):
        prefix = '__retail_resident_' + transport + '_blob_'
        embedded[transport] = embedded_blob(stage, prefix + 'start', prefix + 'end',
            layout.STAGE_ADDRESS, padded(images['resident-sci']['payload']), 'SCI resident ' + transport)
    if len({(embedded[name]['address'], embedded[name]['bytes']) for name in ('scif', 'sci', 'ide', 'scia')}) != 1:
        raise ValueError('Trace package must embed exactly one SCI resident')
    entry = images['entry']
    if (payload[layout.STAGE_BLOB_OFFSET:] != padded(stage['payload']) or
            entry['symbols'].get('__retail_map') != layout.EXEC_ADDRESS + layout.MAP_OFFSET or
            region(payload, layout.HEADER_OFFSET, layout.HEADER_BYTES, 'relocation header') !=
            layout.relocation_header(len(padded(stage['payload'])), low=True) or
            any(region(payload, layout.MAP_OFFSET, layout.MAP_BYTES, 'blank card manifest'))):
        raise ValueError('Runtime stage/header/blank manifest geometry differs')
    r = remember(retained_r / 'retail-toy-pilot.kui')
    reproduced_r = remember(default_check / 'retail-toy-pilot.kui')
    r_payload, r_memory = flatten_elf(remember(default_check / 'entry.elf'))
    if (sha(r) != R_SHA or reproduced_r != r or inspect_retail(r)['build'] != R_BUILD or
            reproduced_r[64:] != r_payload or inspect_retail(r)['memory_bytes'] != r_memory):
        raise ValueError('Disabled trace must reproduce exact hardware-tested R bytes')
    fallback_data, fallback_game, fallback_source, fallback_metadata = fallback_inputs(fallback)
    inputs[Path(fallback)] = sha(fallback_data)
    add('profiles/T-loader-trace.kui', runtime)
    add('restore/R-private-write-through.kui', r)
    add('restore/7b55156aafa2-retail-boot.kui', fallback_game)
    add('restore/K-UI-Toy-Audio-Rollback.zip', fallback_data)
    add('restore/7b55156aafa2-source.tar.gz', fallback_source)
    add('restore/7b55156aafa2-build.json', (json.dumps(fallback_metadata, indent=2) + '\n').encode())
    add('README.md', remember(ROOT / README_SOURCE))
    add(README_SOURCE, files['README.md'])
    add('evidence/trace-word-legend.json', (json.dumps(legend, indent=2) + '\n').encode())
    add('evidence/trace-word-legend.md', legend_markdown(legend))
    add('evidence/loader-trace-audit.json', (json.dumps(trace_proof, indent=2) + '\n').encode())
    add('evidence/default-cache-audit.json', (json.dumps(default_proof, indent=2) + '\n').encode())
    add('evidence/host-tests.txt', host)
    evidence = {}
    for label, directory in (('T', build), ('R-reproduced', default_check)):
        evidence[label] = {}
        for path in sorted(directory.rglob('*')):
            if not path.is_file() or (path.suffix not in ('.elf', '.map', '.su', '.bin') and
                    path.name not in ('build-config', 'toy_pilot_resident_symbols.h')):
                continue
            data = remember(path)
            if not data:
                raise ValueError('Empty emitted evidence: ' + str(path))
            relative = path.relative_to(directory).as_posix()
            add('evidence/' + label + '/' + relative, data)
            evidence[label][relative] = sha(data)
        required = {name + suffix for name in ('worker', 'resident-sci', 'stage')
                    for suffix in ('.elf', '.map', '.bin')} | {'entry.elf', 'entry.map', 'build-config'}
        if not required <= evidence[label].keys():
            raise ValueError('Missing emitted ELF/map/binary evidence for ' + label)
    for name in git('ls-files').splitlines():
        archive_name(name)
        if name in ('LICENSE', 'THIRD_PARTY.md') or name.startswith('LICENSES/'):
            add(name, remember(ROOT / name))
    snapshot = subprocess.check_output(['git', 'archive', '--format=tar', commit], cwd=ROOT)
    add('source-snapshot.tar', snapshot)
    metadata = {'purpose': 'High-only serialized loader trace; diagnostic, no performance fix claimed',
        'source_commit': commit, 'source_tree': tree, 'source_dirty': False,
        'published_commit': published_commit, 'source_snapshot_sha256': sha(snapshot),
        'configuration': expected_config(commit[:12], True), 'runtime': envelope,
        'runtime_sha256': sha(runtime), 'runtime_file': 'profiles/T-loader-trace.kui',
        'worker_export_bytes': 80, 'boot_control_bytes': 56,
        'audio_snapshot': {'version': 8, 'bytes': 448, 'worker_pages': snapshot_legend()},
        'loader_trace': legend, 'linked_loader_trace_proof': trace_proof,
        'embedded_blobs': embedded, 'instruction_audit': instructions,
        'evidence_sha256': evidence, 'host_log_sha256': sha(host),
        'restoration': {'R': {'file': 'restore/R-private-write-through.kui', 'build': R_BUILD,
                            'sha256': R_SHA, 'disabled_trace_reproduces_exact_bytes': True,
                            'disabled_configuration': expected_config(R_BUILD, False)},
                        'clean_audio': {'file': 'restore/7b55156aafa2-retail-boot.kui',
                            'build': '7b55156aafa2', 'sha256': sha(fallback_game)}},
        'baseline_is_ordinary_loader': False, 'hardware_timing_verified': False,
        'audio_or_video_improvement_proven': False, 'AICA_DMA_added': False,
        'ARM_firmware_changed': False, 'game_or_driver_files_included': False,
        'launcher_included': False, 'live_SD_logging_enabled': False}
    add('build.json', (json.dumps(metadata, indent=2, sort_keys=True) + '\n').encode())
    if git('status', '--porcelain') or git('rev-parse', 'HEAD') != commit or git('rev-parse', 'HEAD^{tree}') != tree:
        raise ValueError('Committed source changed during collection')
    for path, digest in inputs.items():
        if sha(path.read_bytes()) != digest:
            raise ValueError('Input changed during collection: ' + str(path))
    add('SHA256SUMS', ''.join(sha(data) + '  ' + name + '\n' for name, data in sorted(files.items())).encode())
    return files, metadata


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--default-check', type=Path, required=True)
    parser.add_argument('--retained-r', type=Path, default=ROOT / 'build/toy-source-default-check')
    parser.add_argument('--fallback', type=Path, default=ROOT.parent / 'audio-rollback-delivery/K-UI-Toy-Audio-Rollback.zip')
    parser.add_argument('--host-log', type=Path, required=True)
    parser.add_argument('--published-commit')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.output.exists() or args.output.is_symlink():
            raise ValueError('Use a new output ZIP path to preserve previous deliveries')
        files, report = collect(args.build_dir.resolve(), args.default_check.resolve(),
            args.retained_r.resolve(), args.fallback, args.host_log, args.published_commit)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        write_archive(args.output, files)
        print(json.dumps({'output': str(args.output.resolve()), 'build': report['runtime']['build'],
            'bytes': args.output.stat().st_size, 'sha256': sha(args.output.read_bytes()),
            'trace_pages': len(report['loader_trace']['pages']), 'restoration': report['restoration']}, indent=2))
    except (OSError, ValueError, KeyError, UnicodeError, struct.error, ImportError,
            subprocess.SubprocessError, zipfile.BadZipFile) as error:
        parser.exit(1, 'FAIL: ' + str(error) + '\n')


if __name__ == '__main__':
    main()
