#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Admit and package the V/W/X cooked-SCI experiments with exact R restoration."""
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
from package_cdda_toy_pilot import embedded_blob, pilot_contract
from package_toy_loader_data_probe import (DATA_WORDS, DATA_COUNTS, DATA_METRICS, DATA_WORST,
    HOST_DATA_FOOTER, legend_markdown, pilot_legend)
from package_toy_loader_trace import R_BUILD, R_SHA, _wire_fields, expected_config, trace_legend
import retail_package as layout
from retail_package import inspect_retail
from runtime_package import flatten_elf

U_BUILD = '52562af7aba3'
U_COMMIT = '52562af7aba3bc77679f8a567d3d3dddd57b410c'
U_SHA = 'a6505d0330a75c6d199a7e3420c8561d51da66a0f92edceefc27530f91adfff5'
T_BUILD = '83ddfdfcb4ca'
T_SHA = 'fef4abb8b7f217eb004357e377630e8e8e64f45ac7b4e5106d5c72abc2e54227'
PROFILES = ((0, 'V', 'sci-attribution'), (1, 'W', 'cooked-p1'),
            (2, 'X', 'cooked-pio-control'))
HOST_PAYLOAD_FOOTER = b'Toy loader payload control suites passed (modes 0/1/2)'


def profile_id(commit, mode):
    if not re.fullmatch(r'[0-9a-f]{40}', commit) or mode not in (0, 1, 2):
        raise ValueError('Invalid committed source or DATA mode')
    return commit[:10] + f'{mode + 1:02x}'


def profile_config(commit, mode):
    return {**expected_config(profile_id(commit, mode), True),
            'DATA_PROBE': '1', 'DATA_PAYLOAD_MODE': str(mode)}


def data_legend():
    """Derive the full exact wire map while checking the retained ABI geometry."""
    source = (ROOT / 'include/kui/toy_loader_data_probe.h').read_text()
    defines = {'KUI_TOY_LOADER_DATA_PROBE_WORDS': DATA_WORDS,
               'KUI_TOY_LOADER_DATA_PROBE_PHASE_WORDS': 88}
    for name, value in defines.items():
        if re.findall(r'^#define\s+' + name + r'\s+(\d+)u\b', source, re.M) != [str(value)]:
            raise ValueError('SCI next DATA constant differs: ' + name)
    if re.findall(r'^#define\s+KUI_TOY_LOADER_DATA_PROBE_VERSION\s+(\d+)u\b', source, re.M) != ['2']:
        raise ValueError('SCI next DATA report version differs')
    if not re.search(r'^#define\s+KUI_TOY_LOADER_DATA_PROBE_MAGIC\s+UINT32_C\(0x4c445031\)', source, re.M):
        raise ValueError('SCI next DATA marker differs')
    types = {}
    for name in ('kui_toy_loader_data_probe_metric', 'kui_toy_loader_data_probe_phase'):
        types['struct ' + name] = _wire_fields(source, name, types, defines)
    phase = types['struct kui_toy_loader_data_probe_phase']
    actual = _wire_fields(source, 'kui_toy_loader_data_probe_report', types, defines)
    metric = ['samples', 'ticks_total', 'ticks_max', 'ticks_min']
    header = ['magic', 'version', 'words', 'phase_words', 'tick_hz', 'frozen',
              'saturated', 'unmatched_end', 'nested_begin', 'feature_flags',
              'payload_mode', 'payload_attempts', 'payload_declines',
              'payload_publications', 'payload_failed', 'reserved']
    suffix = ['dma_started', 'dma_payload_ok', 'pio_fallback', 'prestart_failed']
    suffix.extend(name + '.' + field for name in ('dma_payload_body', 'pio_payload_body')
                  for field in metric)
    if (types['struct kui_toy_loader_data_probe_metric'] != metric or len(phase) != 88 or
            len(actual) != DATA_WORDS or actual[:16] != header or
            phase[:16] != [*DATA_COUNTS, 'dma_delta_invalid', 'dma_counter_wraps'] or
            actual[16:104] != ['phase[0].' + word for word in phase] or
            actual[104:] != ['phase[1].' + word for word in phase] or
            phase[16:36] != [name + '.' + field for name in DATA_METRICS for field in metric] or
            phase[36:68] != [f'sr_buckets[{i}]' for i in range(32)] or
            phase[68:76] != [f'worst_read[{i}]' for i in range(8)] or phase[76:] != suffix):
        raise ValueError('SCI next DATA schema does not retain exact 192-word geometry')
    return {'magic': '0x4c445031', 'version': 2, 'words': DATA_WORDS,
            'bytes': DATA_WORDS * 4, 'header_words': 16, 'phase_words': 88,
            'tick_hz': 781250, 'mode': {'0': 'V: attribution with original callback',
             '1': 'W: exact cooked payload cached P1 alias',
             '2': 'X: exact cooked payload unaligned PIO scratch'},
            'feature_flags': {'1': 'DMA attribution', '2': 'cached payload mode',
                              '4': 'PIO payload mode'},
            'attribution_status': ['dma_delta_invalid', 'dma_counter_wraps'],
            'helper_counter_snapshot': 'Header words 11..14 are authoritative after freeze and zero before freeze',
            'helper_saturation_rule': 'Any frozen helper counter UINT32_MAX conservatively sets saturated',
            'control_window': {'V': 'Observes both phases until terminal freeze',
                'W_X': 'Freezes/restores before next dispatch after accepted PLAY; accepting PLAY has no DATA payload',
                'rejected_PLAY_or_refused_mailbox': 'Startup phase and controls remain active'},
            'phases': ['startup until first accepted PLAY mailbox',
                       'after first accepted PLAY mailbox'],
            'metric_words': metric, 'metric_order': list(DATA_METRICS),
            'sr_bucket_rule': '(SR.BL ? 16 : 0) + SR.IMASK',
            'sr_bucket_scope': 'DATA-owned EXEC/CHECK visits including terminal CHECK',
            'worst_read_words': {str(i): word for i, word in enumerate(DATA_WORST)},
            'metric_scope': 'Cooked DATA read and wrapped original payload, including observation/helper cost',
            'attribution_limit': 'Existing low DMA diagnostics around cooked payloads; not independent wire/CPU timings',
            'pages': [{'page': page, 'first_word': page * 16,
                       'rows': [actual[page * 16 + row * 4:page * 16 + row * 4 + 4]
                                for row in range(4)]} for page in range(DATA_WORDS // 16)]}


def collect(builds, baseline, default_check, restore_r, host_log, published_commit,
            restore_t=None, linked_log=None):
    if git('status', '--porcelain'):
        raise ValueError('Commit the exact reviewed source before packaging')
    commit, tree = git('rev-parse', 'HEAD'), git('rev-parse', 'HEAD^{tree}')
    if published_commit != commit:
        raise ValueError('Published commit must match the exact clean source HEAD')
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

    for directory in (*builds, baseline, default_check):
        if not directory.is_relative_to(ROOT) or directory == ROOT:
            raise ValueError('Build evidence must be inside the source checkout')
        if any(parent.is_symlink() for parent in (directory, *directory.parents)):
            raise ValueError('Build evidence must not contain symlinks')
    reference = remember(baseline / 'retail-toy-pilot.kui')
    if sha(reference) != U_SHA or inspect_retail(reference)['build'] != U_BUILD:
        raise ValueError('Linked proof baseline must be the exact delivered U runtime')
    if build_config(default_check / 'build-config') != expected_config(R_BUILD, False):
        raise ValueError('Diagnostic-off check must retain complete R configuration')
    r = remember(restore_r)
    reproduced = remember(default_check / 'retail-toy-pilot.kui')
    r_payload, r_memory = flatten_elf(remember(default_check / 'entry.elf'))
    if (sha(r) != R_SHA or reproduced != r or inspect_retail(r)['build'] != R_BUILD or
            reproduced[64:] != r_payload or inspect_retail(r)['memory_bytes'] != r_memory):
        raise ValueError('Disabled candidates must reproduce exact hardware-tested R bytes')
    host = remember(host_log)
    from test_toy_pilot import SUITES
    required = (f'{len(SUITES) + 7} Toy pilot regression suites passed'.encode(),
       b'Toy loader trace production differential, telemetry and report suites passed',
       HOST_DATA_FOOTER, HOST_PAYLOAD_FOOTER)
    if any(line not in host.splitlines() for line in required) or b'FAIL' in host:
        raise ValueError('Host log lacks successful actual retained/trace/DATA/payload suites')
    from toy_loader_sci_next_audit import audit_sci_next
    from toy_pilot_cache_audit import audit_cache_profiles
    disabled = audit_cache_profiles(default_check, native_cache=False)
    pilot_contract(remember(ROOT / 'include/kui/toy_pilot.h'))
    legends = {'data': data_legend(), 'trace': trace_legend(), 'pilot': pilot_legend()}
    objdump = shutil.which('sh-elf-objdump')
    if objdump is None:
        raise ValueError('SH objdump is required')
    profiles, evidence = [], {}
    bounds = {'entry': (layout.EXEC_ADDRESS, layout.EXEC_ADDRESS + layout.EXEC_MAX_BYTES),
              'stage': (layout.STAGE_ADDRESS, layout.STAGE_MEMORY_END),
              'resident-sci': (layout.LOW_RESIDENT_ADDRESS, layout.LOW_RESIDENT_LIMIT),
              'worker': (0x8cfd0000, 0x8cfe0000)}
    for (mode, label, name), directory in zip(PROFILES, builds, strict=True):
        build_id = profile_id(commit, mode)
        if build_config(directory / 'build-config') != profile_config(commit, mode):
            raise ValueError('Wrong complete candidate configuration: ' + label)
        proof = audit_sci_next(directory, baseline, mode)
        retained = proof.get('retained', {})
        if (proof.get('profile') != 'toy-loader-sci-next-R' or proof.get('build') != build_id or
                proof.get('mode') != mode or any(proof.get('DATA_probe', {}).get(key) != value
                for key, value in {'version': 2, 'report_words': 192, 'phase_words': 88}.items()) or
                retained.get('baseline_build') != U_BUILD or
                retained.get('retained_audio_instructions_identical') is not True or
                retained.get('low_binary_identical_except_build_string') is not True or
                retained.get('low_symbols_and_reservations_identical') is not True):
            raise ValueError('Actual linked admission does not cover the retained contracts: ' + label)
        images, instructions = {}, {}
        for image, (base, limit) in bounds.items():
            options = {'private_p2': True} if image in ('resident-sci', 'worker') else {}
            if image == 'worker':
                options.update(entry_symbol='_kui_toy_pilot_initialize', entry_at_base=False)
            images[image] = inspect_elf(remember(directory / (image + '.elf')), base, limit, **options)
            instructions[image] = instruction_audit(image, subprocess.check_output(
                [objdump, '-d', str(directory / (image + '.elf'))], text=True))
            if image != 'entry' and remember(directory / (image + '.bin')) != images[image]['payload']:
                raise ValueError('Emitted binary differs from linked payload: ' + label + '/' + image)
        runtime = remember(directory / 'retail-toy-pilot.kui')
        envelope = inspect_retail(runtime)
        payload, memory = flatten_elf(remember(directory / 'entry.elf'))
        if runtime[64:] != payload or envelope['memory_bytes'] != memory or envelope['build'] != build_id:
            raise ValueError('Runtime/source/linked entry identity differs: ' + label)
        stage = images['stage']
        embedded = {'worker': embedded_blob(stage, '__toy_pilot_worker_blob_start',
            '__toy_pilot_worker_blob_end', layout.STAGE_ADDRESS, images['worker']['payload'], label + ' worker')}
        for transport in ('scif', 'sci', 'ide', 'scia'):
            prefix = '__retail_resident_' + transport + '_blob_'
            embedded[transport] = embedded_blob(stage, prefix + 'start', prefix + 'end',
                layout.STAGE_ADDRESS, padded(images['resident-sci']['payload']), 'retained SCI resident ' + transport)
        if len({(embedded[key]['address'], embedded[key]['bytes']) for key in ('scif', 'sci', 'ide', 'scia')}) != 1:
            raise ValueError('Candidate must embed exactly one retained SCI resident')
        if (payload[layout.STAGE_BLOB_OFFSET:] != padded(stage['payload']) or
                images['entry']['symbols'].get('__retail_map') != layout.EXEC_ADDRESS + layout.MAP_OFFSET or
                region(payload, layout.HEADER_OFFSET, layout.HEADER_BYTES, 'relocation header') !=
                layout.relocation_header(len(padded(stage['payload'])), low=True) or
                any(region(payload, layout.MAP_OFFSET, layout.MAP_BYTES, 'blank card manifest'))):
            raise ValueError('Runtime stage/header/manifest geometry differs: ' + label)
        filename = f'profiles/{label}-{name}.kui'
        add(filename, runtime)
        add('evidence/' + label + '/linked-admission.json',
            (json.dumps(proof, indent=2, sort_keys=True) + '\n').encode())
        profiles.append({'label': label, 'mode': mode, 'build': build_id, 'file': filename,
            'configuration': profile_config(commit, mode), 'runtime': envelope,
            'runtime_sha256': sha(runtime), 'hardware_tested': False,
            'linked_admission': proof, 'embedded_blobs': embedded, 'instruction_audit': instructions})
    for label, directory in [*( (label, directory) for (_, label, _), directory in zip(PROFILES, builds, strict=True)),
                              ('U-proof-baseline', baseline), ('R-disabled-check', default_check)]:
        evidence[label] = {}
        for path in sorted(directory.rglob('*')):
            if not path.is_file() or (path.suffix not in ('.elf', '.map', '.su', '.bin', '.dis') and
                    path.name not in ('build-config', 'toy_pilot_resident_symbols.h')):
                continue
            data = remember(path)
            if not data:
                raise ValueError('Empty emitted evidence: ' + str(path))
            relative = path.relative_to(directory).as_posix()
            add('evidence/' + label + '/' + relative, data)
            evidence[label][relative] = sha(data)
        required_evidence = {name + suffix for name in ('worker', 'resident-sci', 'stage')
            for suffix in ('.elf', '.map', '.bin')} | {'entry.elf', 'entry.map', 'build-config'}
        if not required_evidence <= evidence[label].keys():
            raise ValueError('Missing emitted ELF/map/binary evidence: ' + label)
    add('restore/R-private-write-through.kui', r)
    restoration = {'R': {'file': 'restore/R-private-write-through.kui', 'build': R_BUILD,
       'sha256': R_SHA, 'disabled_diagnostics_reproduce_exact_bytes': True}}
    if restore_t is not None:
        t = remember(restore_t)
        if sha(t) != T_SHA or inspect_retail(t)['build'] != T_BUILD:
            raise ValueError('Optional T restoration must be exact delivered T bytes')
        add('restore/T-loader-trace.kui', t)
        restoration['T'] = {'file': 'restore/T-loader-trace.kui', 'build': T_BUILD, 'sha256': T_SHA}
    add('TESTING.md', remember(ROOT / 'docs/toy-loader-sci-next-tests.md'))
    add('evidence/host-tests.txt', host)
    if linked_log is not None:
        add('evidence/linked-fixtures.txt', remember(linked_log))
    add('evidence/diagnostic-off-audit.json', (json.dumps(disabled, indent=2, sort_keys=True) + '\n').encode())
    for name, legend in legends.items():
        add('evidence/' + name + '-word-legend.json', (json.dumps(legend, indent=2) + '\n').encode())
        add('evidence/' + name + '-word-legend.md', legend_markdown(name.upper(), legend))
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
        raise ValueError('Requires pinned GCC 15.2.0 / Binutils 2.45.1')
    tracked = git('ls-files').splitlines()
    for name in tracked:
        archive_name(name)
        if name in ('LICENSE', 'THIRD_PARTY.md') or name.startswith('LICENSES/'):
            add(name, remember(ROOT / name))
    snapshot = subprocess.check_output(['git', 'archive', '--format=tar', commit], cwd=ROOT)
    add('source-snapshot.tar', snapshot)
    metadata = {'purpose': 'Controlled cooked SCI payload attribution, P1-alias comparison and retained PIO control',
        'source_commit': commit, 'source_tree': tree, 'source_dirty': False,
        'source_url': 'https://github.com/TPMJB/K-UI-NeXT/tree/' + published_commit,
        'published_commit': published_commit, 'source_snapshot_sha256': sha(snapshot),
        'compiler': compiler, 'profiles': profiles, 'baseline': {'build': U_BUILD,
            'source_commit': U_COMMIT, 'runtime_sha256': U_SHA}, 'restoration': restoration,
        'report_order': ['DATA', 'PILOT', 'TRACE'], 'report_pages': 46,
        'legends': legends, 'evidence_sha256': evidence, 'host_log_sha256': sha(host),
        'hardware_timing_verified': False, 'audio_or_video_improvement_proven': False,
        'low_SCI_reader_changed': False, 'CDDA_algorithm_changed': False,
        'ARM_firmware_changed': False, 'launcher_included': False,
        'game_or_driver_files_included': False, 'live_SD_logging_enabled': False}
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
    parser.add_argument('--v', type=Path, required=True)
    parser.add_argument('--w', type=Path, required=True)
    parser.add_argument('--x', type=Path, required=True)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--default-check', type=Path, required=True)
    parser.add_argument('--restore-r', type=Path, required=True)
    parser.add_argument('--restore-t', type=Path)
    parser.add_argument('--host-log', type=Path, required=True)
    parser.add_argument('--linked-log', type=Path)
    parser.add_argument('--published-commit', required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.out.exists() or args.out.is_symlink():
            raise ValueError('Use a new ZIP path to preserve previous deliveries')
        files, metadata = collect(tuple(path.absolute() for path in (args.v, args.w, args.x)),
            args.baseline.absolute(), args.default_check.absolute(), args.restore_r,
            args.host_log, args.published_commit, args.restore_t, args.linked_log)
        write_archive(args.out, files)
        if args.out.stat().st_size >= 50 * 1024 * 1024:
            args.out.unlink()
            raise ValueError('Diagnostic ZIP exceeds 50 MiB')
        print(json.dumps({'output': str(args.out.resolve()), 'bytes': args.out.stat().st_size,
            'sha256': sha(args.out.read_bytes()), 'profiles': [{key: p[key] for key in
                ('label', 'mode', 'build', 'file', 'runtime_sha256')} for p in metadata['profiles']],
            'restoration': metadata['restoration']}, indent=2))
    except (OSError, ValueError, KeyError, UnicodeError, struct.error, ImportError,
            subprocess.SubprocessError, zipfile.BadZipFile) as error:
        parser.exit(1, 'FAIL: ' + str(error) + '\n')


if __name__ == '__main__':
    main()
