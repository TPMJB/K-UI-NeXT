#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package reviewed U cooked-DATA measurements with exact R restoration."""
import argparse
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import zipfile

from check_loader_layout import inspect_elf, padded, region
from check_retail_instructions import audit as instruction_audit
from package_cdda_calibration import ROOT, archive_name, build_config, git, sha, write_archive
from package_cdda_toy_pilot import GD_TIMING_WORDS, embedded_blob, pilot_contract, snapshot_legend
from package_toy_loader_trace import R_BUILD, R_SHA, _wire_fields, expected_config, trace_legend
import retail_package as layout
from retail_package import inspect_retail
from runtime_package import flatten_elf

README_SOURCE = 'docs/toy-loader-data-probe-test.md'
T_BUILD = '83ddfdfcb4ca'
T_SHA = 'fef4abb8b7f217eb004357e377630e8e8e64f45ac7b4e5106d5c72abc2e54227'
T_SOURCE_COMMIT = '83ddfdfcb4ca24e82cb86c698fdcb3c78104c881'
T_SOURCE_TREE = 'ba4c76e04a5e5606e13178f4b3c3bd277a444977'
T_PUBLISHED_COMMIT = '6e7be09fe3684291b09e6420adae04eeb2469e03'
T_PACKAGE_SHA = '8bfe1c631c56f8c6cbe736ab6dec14f6df594412a26a3b818d15fe263124ba82'
DATA_WORDS = 192
DATA_COUNTS = ('data_visits', 'read_calls', 'read_ok', 'read_failed',
               'payload_calls', 'payload_ok', 'payload_failed', 'payload_bytes',
               'no_payload_reads', 'invalid_intervals', 'reentrant_reads',
               'reentrant_payload', 'callback_conflicts', 'nonstandard_payload')
DATA_METRICS = ('read_body', 'first_payload_gap', 'payload_body',
                'inter_payload_gap', 'tail_gap')
DATA_WORST = ('token', 'request_lba', 'chunk_lba', 'chunk_sectors',
              'caller_sr', 'caller_pr', 'read_ticks', 'payload_calls')
HOST_DATA_FOOTER = b'Toy loader DATA probe production telemetry and report suites passed'


def data_legend():
    """Fail closed if any all-uint32 report word differs from this reviewed ABI."""
    source = (ROOT / 'include/kui/toy_loader_data_probe.h').read_text()
    defines = {'KUI_TOY_LOADER_DATA_PROBE_WORDS': DATA_WORDS,
               'KUI_TOY_LOADER_DATA_PROBE_PHASE_WORDS': 88}
    for name, value in defines.items():
        found = re.findall(r'^#define\s+' + name + r'\s+(\d+)u\b', source, re.M)
        if found != [str(value)]:
            raise ValueError('DATA constant differs from packaged legend: ' + name)
    if not re.search(r'^#define\s+KUI_TOY_LOADER_DATA_PROBE_MAGIC\s+UINT32_C\(0x4c445031\)', source, re.M):
        raise ValueError('DATA report marker differs')
    metric = ['samples', 'ticks_total', 'ticks_max', 'ticks_min']
    types = {}
    for name in ('kui_toy_loader_data_probe_metric', 'kui_toy_loader_data_probe_phase'):
        types['struct ' + name] = _wire_fields(source, name, types, defines)
    phase = list(DATA_COUNTS) + ['reserved_counts[0]', 'reserved_counts[1]']
    for name in DATA_METRICS:
        phase.extend(name + '.' + field for field in metric)
    phase.extend(f'sr_buckets[{i}]' for i in range(32))
    phase.extend(f'worst_read[{i}]' for i in range(8))
    phase.extend(f'reserved[{i}]' for i in range(12))
    expected = ['magic', 'version', 'words', 'phase_words', 'tick_hz', 'frozen',
                'saturated', 'unmatched_end', 'nested_begin']
    expected.extend(f'reserved[{i}]' for i in range(7))
    for index in range(2):
        expected.extend(f'phase[{index}].' + word for word in phase)
    actual = _wire_fields(source, 'kui_toy_loader_data_probe_report', types, defines)
    if (types['struct kui_toy_loader_data_probe_metric'] != metric or
            types['struct kui_toy_loader_data_probe_phase'] != phase or
            actual != expected or len(actual) != DATA_WORDS):
        raise ValueError('DATA wire schema differs from reviewed package legend')
    return {'magic': '0x4c445031', 'version': 1, 'words': DATA_WORDS,
            'bytes': DATA_WORDS * 4, 'header_words': 16, 'phase_words': 88,
            'tick_hz': 781250,
            'phases': ['startup until first accepted PLAY mailbox',
                       'after first accepted PLAY mailbox'],
            'metric_words': metric, 'metric_order': list(DATA_METRICS),
            'sr_bucket_rule': '(SR.BL ? 16 : 0) + SR.IMASK',
            'sr_bucket_scope': 'DATA-owned EXEC/CHECK visits, including terminal CHECK',
            'worst_read_words': {str(i): word for i, word in enumerate(DATA_WORST)},
            'metric_scope': 'Cooked 2048-byte read and original payload callback; includes observation cost',
            'gap_limit': 'Gaps combine software, token/command, wire CRC tail, copy and cleanup work; not pure physical latency',
            'pages': [{'page': page, 'first_word': page * 16,
                       'rows': [actual[page * 16 + row * 4:page * 16 + row * 4 + 4]
                                for row in range(4)]} for page in range(DATA_WORDS // 16)]}


def pilot_legend():
    pages = snapshot_legend()
    pages.insert(5, list(GD_TIMING_WORDS))
    if len(pages) != 8 or any(len(words) != 16 for words in pages):
        raise ValueError('PILOT wire legend must contain eight exact sixteen-word pages')
    return {'magic': '0x54595031', 'version': 8, 'words': 128, 'bytes': 512,
            'snapshot_bytes': 448,
            'wire_mapping': 'snapshot[0:80], GD timing[0:16], snapshot[80:112]',
            'pages': [{'page': index, 'first_word': index * 16,
                       'rows': [words[row * 4:row * 4 + 4] for row in range(4)]}
                      for index, words in enumerate(pages)]}


def legend_markdown(label, legend):
    lines = ['# Exact ' + label + ' PAGE word legend', '',
             'Read each page from top left across four words, then the next row. '
             'Page numbers and values on the console are hexadecimal.', '',
             '| Page | Hex page | Row | Word 1 | Word 2 | Word 3 | Word 4 |',
             '|---:|---:|---:|---|---|---|---|']
    for page in legend['pages']:
        for row, words in enumerate(page['rows']):
            lines.append('| ' + ' | '.join([str(page['page']), f"{page['page']:08X}",
                          str(row + 1), *('`' + word + '`' for word in words)]) + ' |')
    if label == 'DATA':
        lines.extend(['', '`sr_buckets[i]`: IMASK 0–15, plus 16 when original caller SR.BL is set.', '',
                      '| `worst_read` index | Meaning |', '|---:|---|'])
        lines.extend(f'| {i} | `{word}` |' for i, word in enumerate(DATA_WORST))
        lines.extend(['', 'Metrics record sample count, total, maximum and minimum ticks. '
                      'Divide valid unsaturated ticks by 781250 for seconds. '
                      'The first accepted PLAY mailbox separates phases; it is not the audible music or FMV boundary.'])
    return ('\n'.join(lines) + '\n').encode()


def probe_config(build_id):
    return {**expected_config(build_id, True), 'DATA_PROBE': '1'}


def collect(build, baseline, default_check, restore_r, host_log):
    if git('status', '--porcelain'):
        raise ValueError('Commit exact reviewed source before packaging')
    commit, tree = git('rev-parse', 'HEAD'), git('rev-parse', 'HEAD^{tree}')
    inputs, files = {}, {}

    def remember(path):
        path = Path(path).absolute()
        if any(item.is_symlink() for item in (path, *path.parents)) or not path.is_file():
            raise ValueError('Package input must be a regular file: ' + str(path))
        path = path.resolve()
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

    for directory in (build, baseline, default_check):
        if not directory.is_relative_to(ROOT) or directory == ROOT:
            raise ValueError('Build evidence must be inside the source checkout')
        if any(parent.is_symlink() for parent in (directory, *directory.parents)):
            raise ValueError('Build evidence path cannot contain symlinks')
    if build_config(build / 'build-config') != probe_config(commit[:12]):
        raise ValueError('Wrong complete U DATA-probe configuration')
    if build_config(baseline / 'build-config') != expected_config(T_BUILD, True):
        raise ValueError('Baseline must use exact T configuration')
    if build_config(default_check / 'build-config') != expected_config(R_BUILD, False):
        raise ValueError('Disabled diagnostic must use exact R configuration')
    old_runtime = remember(baseline / 'retail-toy-pilot.kui')
    if sha(old_runtime) != T_SHA or inspect_retail(old_runtime)['build'] != T_BUILD:
        raise ValueError('Linked proof baseline is not the exact delivered T runtime')
    host = remember(host_log)
    from test_toy_pilot import SUITES
    required_lines = (f'{len(SUITES) + 7} Toy pilot regression suites passed'.encode(),
                      b'Toy loader trace production differential, telemetry and report suites passed',
                      HOST_DATA_FOOTER)
    if any(line not in host.splitlines() for line in required_lines) or b'FAIL' in host:
        raise ValueError('Host log lacks actual retained, trace and DATA probe regression success')
    # Run the separate admission against the actual ELF inputs now. The U
    # admission independently runs the original T gate on the exact baseline.
    from toy_loader_data_probe_audit import audit_data_probe
    proof = audit_data_probe(build, baseline)
    if (proof.get('profile') != 'toy-loader-data-probe-R' or proof.get('build') != commit[:12] or
            any(proof.get('DATA_probe', {}).get(key) != value for key, value in
                {'version': 1, 'report_magic': '0x4c445031', 'report_words': DATA_WORDS,
                 'phase_words': 88, 'tick_hz': 781250}.items()) or
            proof.get('retained_trace', {}).get('report_words') != 416 or
            proof.get('retained', {}).get('baseline_build') != T_BUILD or
            proof.get('retained', {}).get('retained_audio_instructions_identical') is not True or
            proof.get('retained', {}).get('low_binary_identical_except_build_string') is not True):
        raise ValueError('Actual linked proof does not admit exact DATA/TRACE/retained-audio contracts')
    from toy_pilot_cache_audit import audit_cache_profiles
    disabled_proof = audit_cache_profiles(default_check, native_cache=False)
    pilot_contract(remember(ROOT / 'include/kui/toy_pilot.h'))
    legends = {'data': data_legend(), 'trace': trace_legend(), 'pilot': pilot_legend()}
    runtime = remember(build / 'retail-toy-pilot.kui')
    envelope = inspect_retail(runtime)
    images, instructions = {}, {}
    objdump = shutil.which('sh-elf-objdump')
    if objdump is None:
        raise ValueError('SH objdump is required')
    bounds = {'entry': (layout.EXEC_ADDRESS, layout.EXEC_ADDRESS + layout.EXEC_MAX_BYTES),
              'stage': (layout.STAGE_ADDRESS, layout.STAGE_MEMORY_END),
              'resident-sci': (layout.LOW_RESIDENT_ADDRESS, layout.LOW_RESIDENT_LIMIT),
              'worker': (0x8cfd0000, 0x8cfe0000)}
    for name, (base, limit) in bounds.items():
        options = {'private_p2': True} if name in ('resident-sci', 'worker') else {}
        if name == 'worker':
            options.update(entry_symbol='_kui_toy_pilot_initialize', entry_at_base=False)
        images[name] = inspect_elf(remember(build / (name + '.elf')), base, limit, **options)
        dis = subprocess.check_output([objdump, '-d', str(build / (name + '.elf'))], text=True)
        instructions[name] = instruction_audit(name, dis)
        if name != 'entry' and remember(build / (name + '.bin')) != images[name]['payload']:
            raise ValueError('Binary differs from actual linked payload: ' + name)
    payload, memory = flatten_elf(remember(build / 'entry.elf'))
    if runtime[64:] != payload or envelope['memory_bytes'] != memory or envelope['build'] != commit[:12]:
        raise ValueError('U runtime/source/entry identity differs')
    stage = images['stage']
    embedded = {'worker': embedded_blob(stage, '__toy_pilot_worker_blob_start',
        '__toy_pilot_worker_blob_end', layout.STAGE_ADDRESS, images['worker']['payload'], 'U worker')}
    for transport in ('scif', 'sci', 'ide', 'scia'):
        prefix = '__retail_resident_' + transport + '_blob_'
        embedded[transport] = embedded_blob(stage, prefix + 'start', prefix + 'end',
            layout.STAGE_ADDRESS, padded(images['resident-sci']['payload']), 'retained SCI resident ' + transport)
    if len({(embedded[name]['address'], embedded[name]['bytes']) for name in ('scif', 'sci', 'ide', 'scia')}) != 1:
        raise ValueError('U package must embed exactly one SCI resident')
    if (payload[layout.STAGE_BLOB_OFFSET:] != padded(stage['payload']) or
            images['entry']['symbols'].get('__retail_map') != layout.EXEC_ADDRESS + layout.MAP_OFFSET or
            region(payload, layout.HEADER_OFFSET, layout.HEADER_BYTES, 'relocation header') !=
            layout.relocation_header(len(padded(stage['payload'])), low=True) or
            any(region(payload, layout.MAP_OFFSET, layout.MAP_BYTES, 'blank card manifest'))):
        raise ValueError('Runtime stage/header/blank manifest geometry differs')
    r = remember(restore_r)
    reproduced = remember(default_check / 'retail-toy-pilot.kui')
    r_payload, r_memory = flatten_elf(remember(default_check / 'entry.elf'))
    if (sha(r) != R_SHA or reproduced != r or inspect_retail(r)['build'] != R_BUILD or
            reproduced[64:] != r_payload or inspect_retail(r)['memory_bytes'] != r_memory):
        raise ValueError('Disabled U must reproduce exact hardware-tested R bytes')
    compiler = {}
    for tool in ('sh-elf-gcc', 'sh-elf-objcopy', 'sh-elf-objdump'):
        path = shutil.which(tool)
        if path is None:
            raise ValueError('Required build tool missing: ' + tool)
        compiler[tool] = {'version': subprocess.check_output([path, '--version'], text=True),
                          'executable_sha256': sha(remember(path))}
    if ('15.2.0' not in compiler['sh-elf-gcc']['version'].splitlines()[0] or
            any('2.45.1' not in compiler[tool]['version'].splitlines()[0]
                for tool in ('sh-elf-objcopy', 'sh-elf-objdump'))):
        raise ValueError('Package requires the recovered pinned GCC 15.2.0 / Binutils 2.45.1')
    add('profiles/U-loader-data-probe.kui', runtime)
    add('restore/R-private-write-through.kui', r)
    add('TESTING.md', remember(ROOT / README_SOURCE))
    for name, legend in legends.items():
        add('evidence/' + name + '-word-legend.json', (json.dumps(legend, indent=2) + '\n').encode())
        add('evidence/' + name + '-word-legend.md', legend_markdown(name.upper(), legend))
    add('evidence/loader-data-probe-audit.json', (json.dumps(proof, indent=2, sort_keys=True) + '\n').encode())
    add('evidence/default-cache-audit.json', (json.dumps(disabled_proof, indent=2, sort_keys=True) + '\n').encode())
    add('evidence/host-tests.txt', host)
    evidence = {}
    for label, directory in (('U', build), ('T-proof-baseline', baseline), ('R-disabled-check', default_check)):
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
    metadata = {'purpose': 'Cooked DATA callback timing and original caller interrupt-state diagnostic',
        'source_commit': commit, 'source_tree': tree, 'source_dirty': False,
        'source_snapshot_sha256': sha(snapshot), 'configuration': probe_config(commit[:12]),
        'compiler': compiler, 'runtime': envelope, 'runtime_sha256': sha(runtime),
        'runtime_file': 'profiles/U-loader-data-probe.kui', 'loader_DATA_probe': legends['data'],
        'loader_trace': legends['trace'], 'audio_snapshot': legends['pilot'],
        'report_order': ['DATA', 'PILOT', 'TRACE'], 'report_pages': 46,
        'linked_loader_DATA_proof': proof, 'embedded_blobs': embedded,
        'instruction_audit': instructions, 'evidence_sha256': evidence, 'host_log_sha256': sha(host),
        'restoration': {'R': {'file': 'restore/R-private-write-through.kui', 'build': R_BUILD,
            'sha256': R_SHA, 'disabled_probe_reproduces_exact_bytes': True,
            'disabled_configuration': expected_config(R_BUILD, False)}},
        'retained_T_provenance': {'build': T_BUILD, 'runtime_sha256': T_SHA,
            'source_commit': T_SOURCE_COMMIT, 'source_tree': T_SOURCE_TREE,
            'published_commit': T_PUBLISHED_COMMIT, 'package_sha256': T_PACKAGE_SHA,
            'recovered_source_snapshot': True},
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
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--default-check', type=Path, default=ROOT / 'build/probe-default-off-check')
    parser.add_argument('--restore-r', type=Path, required=True)
    parser.add_argument('--host-log', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.out.exists() or args.out.is_symlink():
            raise ValueError('Use a new output ZIP path to preserve previous deliveries')
        files, metadata = collect(args.build.absolute(), args.baseline.absolute(),
            args.default_check.absolute(), args.restore_r, args.host_log)
        write_archive(args.out, files)
        if args.out.stat().st_size >= 50 * 1024 * 1024:
            args.out.unlink()
            raise ValueError('Diagnostic ZIP exceeds the 50 MiB delivery limit')
        print(json.dumps({'output': str(args.out.resolve()), 'build': metadata['runtime']['build'],
            'bytes': args.out.stat().st_size, 'sha256': sha(args.out.read_bytes()),
            'report_pages': metadata['report_pages'], 'restoration': metadata['restoration']}, indent=2))
    except (OSError, ValueError, KeyError, UnicodeError, struct.error, ImportError,
            subprocess.SubprocessError, zipfile.BadZipFile) as error:
        parser.exit(1, 'FAIL: ' + str(error) + '\n')


if __name__ == '__main__':
    main()
