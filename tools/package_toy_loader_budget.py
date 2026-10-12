#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package AA/AB/AC/AD only after independent startup DATA budget admission."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import zipfile

from package_toy_loader_data_probe import pilot_legend, legend_markdown
from package_toy_loader_sci_next import data_legend
from package_toy_loader_trace import trace_legend, _wire_fields
from retail_package import inspect_retail

ROOT = Path(__file__).resolve().parents[1]
PROFILES = ((1, 'AA', 4, 1500, 'observed-control'),
            (2, 'AB', 12, 1500, 'block-allowance'),
            (3, 'AC', 4, 6000, 'timer-allowance'),
            (4, 'AD', 12, 6000, 'combined-allowance'))
B_SHA = 'bb2d25eb99b3ee6b75db87b643239bce7ef755b3f6bd017ca5525a4ebf6e6a97'
R_SHA = '99e1c0ad84a916fa9e08cf68b494c2d0efe0748fe956e579841c3d7614e49037'
Z_SHA = 'f58f8dad7f51233f01088f68eb09fa78a7228aea3489ce8b62c12d691a77a1ef'
BUDGET_FIELDS = ('profile', 'block_limit', 'tick_limit', 'service_calls',
                 'block_exits', 'time_exits', 'token_exits', 'step_exits',
                 'finished_or_inactive_exits', 'service_verified_blocks',
                 'zero_credit_calls', 'credited_bytes')
HOST_REQUIREMENTS = {
    'host-all.log': ('38 Toy pilot regression suites passed',),
    'host-trace.log': ('Toy loader trace production differential, telemetry and report suites passed',),
    'host-data.log': ('Toy loader DATA probe production telemetry and report suites passed',),
    'host-one.log': ('DATA schedule production differential:',),
    'host-report.log': ('SCHED production report modes1/2/3:',),
    'host-startup.log': ('Startup DATA scheduling suites passed (independent audio profile)',),
    # Final production runners supply these three explicit successful footers.
    'host-budget.log': ('Startup DATA budget profiles 1..4 passed; 7 behavioral mutations and 15 unsafe configurations rejected',),
    'host-budget-report.log': ('SCHED budget report profiles1/2/3/4 passed (192 words and per-request IRQ/retry attribution)',),
    'host-budget-trace.log': ('Budget TRACE IRQ credit suites passed (profiles 1/2/3/4; old delta mutation rejected; default zero unchanged)',),
    'host-budget-config.log': ('DATA budget configuration suites passed',),
    'linked-tests.log': ('OK',),
}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT, text=True).strip()


def schedule_legend(version=2):
    """Exact wire map; version 1 is the byte-exact historical B reference."""
    if version not in (1, 2):
        raise ValueError('Unknown SCHED report version')
    header = ['magic', 'version', 'words', 'mode', 'tick_hz', 'frozen',
              'accepted_requests', 'next_slot', 'active_slot', 'after_accepted_PLAY',
              'saturated', 'fingerprint_or_record_faults', 'fingerprint_ticks_total',
              'fingerprint_ticks_max', 'cleanup_ok', 'cleanup_failed']
    engine = ['calls', 'irq_calls', 'call_blocks', 'irq_blocks', 'audio_claims',
              'audio_pending', 'audio_releases', 'data_resumes', 'errors', 'retries',
              'token_yields', 'polled_blocks', 'work_ticks_max', 'irq_ticks_max',
              'active_token', 'card_owned']
    startup = ['enabled', 'retired', 'quiesces', 'quiesce_failed']
    record = ['sequence', 'token', 'LBA', 'sector_count', 'destination',
              'sector_bytes', 'command', 'flags', 'credited_bytes', 'status',
              'error', 'first_sector_CRC32', 'first_credit_ticks', 'completion_ticks',
              'calls', 'max_call_ticks_before_fingerprint']
    words = header + ['engine.' + field for field in engine]
    words += ['startup.' + field for field in startup]
    if version == 2:
        schema = (ROOT / 'include/kui/toy_loader_schedule_report.h').read_text()
        for name, value in (('KUI_TOY_SCHEDULE_REPORT_VERSION', 2),
                            ('KUI_TOY_SCHEDULE_REPORT_WORDS', 192),
                            ('KUI_TOY_SCHEDULE_RECORD_WORDS', 18)):
            if not re.search(r'^#define\s+' + name +
                             r'\s+' + str(value) + r'u\b', schema, re.M):
                raise ValueError('SCHED v2 geometry differs: ' + name)
        actual = _wire_fields((ROOT / 'src/loader/toy_pilot_sci.h').read_text(),
                              'kui_toy_pilot_sci_budget_stats', {}, {})
        if actual != list(BUDGET_FIELDS):
            raise ValueError('Startup DATA budget fields differ from exact legend')
        words += ['budget.' + field for field in BUDGET_FIELDS]
        record += ['IRQ_verified_blocks', 'stream_retries']
    else:
        words += ['reserved_zero'] * 12
    for slot in range(8):
        words += [f'record[{slot}].' + field for field in record]
    expected = 192 if version == 2 else 176
    if len(words) != expected:
        raise ValueError('SCHED word map geometry differs')
    return {'magic': '0x4c534331', 'version': version, 'words': expected,
            'bytes': expected * 4, 'header_words': 16, 'engine_words': 32,
            'record_words': len(record), 'records': 8, 'tick_hz': 781250,
            'pages': [{'page': page, 'first_word': page * 16,
                       'rows': [words[page * 16 + row * 4:page * 16 + row * 4 + 4]
                                for row in range(4)]}
                      for page in range(expected // 16)]}


def schedule_legend_markdown(schema):
    result = legend_markdown('SCHED', schema).decode()
    result += '\nCRC32 covers only the first credited sector. Sort ring records by sequence.\n'
    result += ('Flags: 1=CRC valid, 2=first-credit timing valid, 4=terminal timing valid, '
               '8=CHECK consumed, 16=after accepted PLAY, 32=invalid call timing, '
               '64=CRC map refused, 128=accepted ABORT, 256=RESET/INIT.\n\n')
    result += ('Unavailable slot/timing fields use `FFFFFFFF`; record status `FFFFFFFF` means FAILED. '
               'Ticks / 781250 gives seconds. Maximum call time excludes the following fingerprint. '
               'Engine/startup/budget fields are frozen before cleanup; cleanup flags describe its result.\n')
    if schema['version'] == 2:
        result += ('\nBudget profile: 1=AA, 2=AB, 3=AC, 4=AD. Block/time limits are '
                   '4/1500, 12/1500, 4/6000 and 12/6000 respectively. Time units are TMU ticks. '
                   'Five exit counters partition counted service calls. Foreground budget blocks/bytes '
                   'exclude intervening IRQ work. Mutable budget counters saturate at `FFFFFFFF`.\n\n'
                   'Per-request physical IRQ blocks/retries are unsigned deltas from before accepted REQUEST '
                   'through the latest owned exit, including terminal CHECK. Active physical counters '
                   'are refreshed at terminal freeze before cleanup; logical bytes, status and timing '
                   'retain the last GD observation. Consumed records are immutable.\n')
    return result.encode()


def budget_data_legend():
    schema = data_legend()
    schema['mode'] = {'0': 'AA/AB/AC/AD and B: original synchronous payload callback mode'}
    schema['control_window'] = {
        'AA_AB_AC_AD': 'Startup asynchronous engine bypasses synchronous read/payload counters; use SCHED and TRACE',
        'B': 'Historical original synchronous DATA control, with SCHED v1'}
    return schema


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path,
                        default=ROOT.parent / 'scheduling-source/build/schedule-z')
    parser.add_argument('--reference-source', type=Path,
                        default=ROOT.parent / 'scheduling-source')
    parser.add_argument('--comparison', type=Path, default=ROOT.parent /
                        'scheduling-hardware-20261011/B-Y-Z-DATA-Scheduling-Hardware-Comparison.json')
    parser.add_argument('--legacy-r-check', type=Path,
                        help='Optional current-source diagnostic-off R byte-exact rebuild directory')
    parser.add_argument('--legacy-z-check', type=Path,
                        help='Optional current-source profile-zero Z byte-exact rebuild directory')
    parser.add_argument('--out', type=Path,
                        default=ROOT / 'dist/K-UI-Toy-DATA-Budget-Tests.zip')
    args = parser.parse_args()
    if git('status', '--porcelain'):
        raise ValueError('Commit exact reviewed source before packaging')
    # Lazy import keeps schema inspection usable while the independent gate is
    # being written. Missing or unfinished admission never grants a ZIP.
    from toy_loader_budget_audit import audit_budget
    inputs, files = {}, {}

    def remember(path):
        path = Path(path).absolute()
        if any(parent.is_symlink() for parent in (path, *path.parents)) or not path.is_file():
            raise ValueError('Input must be a regular nonsymlink file: ' + str(path))
        path = path.resolve()
        data = path.read_bytes()
        if not data:
            raise ValueError('Empty package evidence: ' + str(path))
        if path in inputs and inputs[path] != digest(data):
            raise ValueError('Package input changed: ' + str(path))
        inputs[path] = digest(data)
        return data

    def add(name, data):
        if name.startswith('/') or '\\' in name or '..' in Path(name).parts or name in files:
            raise ValueError('Invalid or duplicate archive member: ' + name)
        files[name] = data

    source = json.loads(remember(ROOT / 'build/budget-tests/source.json'))
    if not re.fullmatch(r'[0-9a-f]{40}', source.get('runtime_source_commit', '')):
        raise ValueError('Missing exact runtime source commit')
    if subprocess.run(['git', 'merge-base', '--is-ancestor', source['runtime_source_commit'], 'HEAD'],
                      cwd=ROOT, check=False, capture_output=True).returncode:
        raise ValueError('Runtime source commit is not included in the source snapshot')
    expected_profiles = source.get('profiles', [])
    if len(expected_profiles) != 4 or len({item.get('label') for item in expected_profiles}) != 4:
        raise ValueError('Runtime source manifest must cover four distinct profiles')
    expected_profiles = {item['label']: item for item in expected_profiles}
    metadata = {'format': 1, 'runtime_source_commit': source['runtime_source_commit'],
                'source_snapshot_commit': git('rev-parse', 'HEAD'), 'profiles': [],
                'CDDA_algorithm_changed': False, 'hardware_behavior_verified': False,
                'audio_or_video_improvement_proven': False}
    add('TESTING.md', remember(ROOT / 'docs/toy-data-budget-test.md'))
    add('tools/check_toy_sector.py', remember(ROOT / 'tools/check_toy_sector.py'))
    for label, schema in (('SCHED', schedule_legend()), ('DATA', budget_data_legend()),
                          ('PILOT', pilot_legend()), ('TRACE', trace_legend())):
        prefix = 'evidence/' + label.lower() + '-word-legend'
        add(prefix + '.json', (json.dumps(schema, indent=2) + '\n').encode())
        add(prefix + '.md', schedule_legend_markdown(schema) if label == 'SCHED'
            else legend_markdown(label, schema))
    legacy = schedule_legend(1)
    add('reference/B-sched-v1-word-legend.json', (json.dumps(legacy, indent=2) + '\n').encode())
    add('reference/B-sched-v1-word-legend.md', schedule_legend_markdown(legacy))
    for label, path, sha, build, name in (
            ('B', args.reference_source / 'build/schedule-b/retail-toy-pilot.kui',
             B_SHA, '143d51bf7603', 'reference/B-two-sector-control.kui'),
            ('R', args.reference_source / 'build/schedule-r/retail-toy-pilot.kui',
             R_SHA, '73e8363c10f7', 'restore/R-private-write-through.kui')):
        data = remember(path)
        if digest(data) != sha or inspect_retail(data)['build'] != build:
            raise ValueError('Historical reference must remain byte exact: ' + label)
        add(name, data)
        metadata.setdefault('historical_references', {})[label] = {
            'build': build, 'sha256': sha, 'file': name, 'recompiled': False}
    prior = args.reference_source / 'docs/evidence/toy-data-schedule-2026-10-11/B-linked-admission.json'
    proof = remember(prior)
    historical = json.loads(proof)
    if (historical.get('admitted') is not True or historical.get('build') != '143d51bf7603' or
            historical.get('runtime', {}).get('sha256') != B_SHA):
        raise ValueError('Historical B admission differs from its pinned reference')
    add('reference/B-historical-linked-admission.json', proof)
    for name, required in HOST_REQUIREMENTS.items():
        data = remember(ROOT / 'build/budget-tests' / name)
        if any(footer.encode() not in data for footer in required):
            raise ValueError('Missing passed production test evidence: ' + name)
        if name == 'linked-tests.log' and data.decode().rstrip().splitlines()[-1] != 'OK':
            raise ValueError('Linked audit test log does not end in OK')
        add('evidence/' + name, data)
    for label, directory, sha, build in (
            ('R', args.legacy_r_check, R_SHA, '73e8363c10f7'),
            ('Z', args.legacy_z_check, Z_SHA, '143d51bf7602')):
        if directory is None:
            continue
        rebuilt = remember(directory / 'retail-toy-pilot.kui')
        if digest(rebuilt) != sha or inspect_retail(rebuilt)['build'] != build:
            raise ValueError('Current-source legacy rebuild is not byte exact: ' + label)
        prefix = 'evidence/legacy-' + label + '-byte-exact-rebuild/'
        add(prefix + 'retail-toy-pilot.kui', rebuilt)
        for path in sorted(directory.rglob('*')):
            if path.is_file() and (path.suffix in ('.elf', '.bin', '.map', '.su', '.log') or
                                  path.name in ('build-config', 'toy_pilot_resident_symbols.h')):
                add(prefix + path.relative_to(directory).as_posix(), remember(path))
        metadata.setdefault('current_source_legacy_rebuilds', {})[label] = {
            'build': build, 'sha256': sha, 'byte_exact': True}
        log = ROOT / 'build/budget-tests' / ('legacy-' + label.lower() + '-build.log')
        if log.exists():
            add('evidence/' + log.name, remember(log))
    for profile, label, blocks, ticks, name in PROFILES:
        directory = ROOT / 'build' / ('budget-' + label.lower())
        admission = audit_budget(directory, args.baseline, profile)
        budget = admission.get('budget', {})
        if admission.get('admitted') is not True or any(budget.get(k) != value for k, value in
                (('credit_limit', blocks), ('tick_limit', ticks), ('tick_hz', 781250))):
            raise ValueError('Independent admission does not cover selected budget: ' + label)
        config = dict(line.split('=', 1) for line in remember(directory / 'build-config').decode().splitlines())
        expected = expected_profiles.get(label, {})
        if (any(expected.get(key) != value for key, value in
                (('profile', profile), ('blocks', blocks), ('ticks', ticks))) or
                expected.get('build') != admission.get('build') or config.get('BUILD') != admission['build'] or
                config.get('DATA_SCHEDULE_MODE') != '2' or config.get('DATA_BUDGET_PROFILE') != str(profile) or
                config.get('SHARED_SCI') != '0' or config.get('ASYNC_CDDA') != '0'):
            raise ValueError('Build and exact runtime source metadata disagree: ' + label)
        runtime = remember(directory / 'retail-toy-pilot.kui')
        if (digest(runtime) != admission.get('runtime', {}).get('sha256') or
                inspect_retail(runtime)['build'] != admission['build']):
            raise ValueError('Runtime differs from independently admitted artifact: ' + label)
        add('profiles/' + label + '-' + name + '.kui', runtime)
        add('evidence/' + label + '-linked-admission.json',
            (json.dumps(admission, indent=2, sort_keys=True) + '\n').encode())
        metadata['profiles'].append({'label': label, 'profile': profile, 'blocks': blocks,
                                     'ticks': ticks, 'build': admission['build'],
                                     'runtime_sha256': digest(runtime)})
        collected = set()
        for path in sorted(directory.rglob('*')):
            if path.is_file() and (path.suffix in ('.elf', '.bin', '.map', '.su') or
                                  path.name in ('build-config', 'toy_pilot_resident_symbols.h')):
                relative = path.relative_to(directory).as_posix()
                add('evidence/' + label + '/' + relative, remember(path)); collected.add(relative)
        required = {part + suffix for part in ('worker', 'resident-sci', 'stage')
                    for suffix in ('.elf', '.bin', '.map')} | {'entry.elf', 'entry.map', 'build-config'}
        if not required <= collected or not any(name.endswith('.su') for name in collected):
            raise ValueError('Missing linked binaries, maps or emitted stack evidence: ' + label)
    if args.comparison is not None and args.comparison.exists():
        comparison = remember(args.comparison)
        json.loads(comparison)
        if b'libfile_' in comparison:
            raise ValueError('Comparison evidence contains an internal file identifier')
        add('evidence/B-Y-Z-hardware-comparison.json', comparison)
    for path in [ROOT / 'LICENSE', ROOT / 'THIRD_PARTY.md', *sorted((ROOT / 'LICENSES').glob('*'))]:
        if path.is_file():
            add(path.relative_to(ROOT).as_posix(), remember(path))
    add('source-snapshot.tar', subprocess.check_output(['git', 'archive', 'HEAD'], cwd=ROOT))
    add('build.json', (json.dumps(metadata, indent=2, sort_keys=True) + '\n').encode())
    for path, sha in inputs.items():
        if digest(path.read_bytes()) != sha:
            raise ValueError('Input changed during collection: ' + str(path))
    if git('status', '--porcelain') or git('rev-parse', 'HEAD') != metadata['source_snapshot_commit']:
        raise ValueError('Committed source changed during collection')
    add('SHA256SUMS', ''.join(digest(data) + '  ' + name + '\n'
                            for name, data in sorted(files.items())).encode())
    out = args.out.resolve(); out.parent.mkdir(parents=True, exist_ok=True)
    temporary = out.with_suffix('.zip.tmp')
    with zipfile.ZipFile(temporary, 'w', zipfile.ZIP_DEFLATED, compresslevel=8) as archive:
        for name, data in sorted(files.items()):
            archive.writestr(name, data)
    with zipfile.ZipFile(temporary) as archive:
        if archive.testzip() is not None:
            raise ValueError('ZIP integrity failure')
        for line in archive.read('SHA256SUMS').decode().splitlines():
            sha, name = line.split('  ', 1)
            if digest(archive.read(name)) != sha:
                raise ValueError('ZIP member checksum differs: ' + name)
    temporary.replace(out)
    print(json.dumps({'file': str(out), 'sha256': digest(out.read_bytes()),
                      'bytes': out.stat().st_size, 'profiles': metadata['profiles'],
                      'all_member_checksums_verified': True}, indent=2))


if __name__ == '__main__':
    main()
