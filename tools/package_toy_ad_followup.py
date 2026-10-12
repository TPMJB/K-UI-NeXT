#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package AE/AF/AG/AH after independent admission, preserving exact AD and R."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import zipfile

from package_toy_loader_budget import (HOST_REQUIREMENTS, budget_data_legend,
                                      schedule_legend, schedule_legend_markdown)
from package_toy_loader_data_probe import pilot_legend, legend_markdown
from package_toy_loader_trace import trace_legend
from retail_package import inspect_retail

ROOT = Path(__file__).resolve().parents[1]
AD_SHA = '8f478c473f2574409a5a0d346237a1b09995d350d960f3150a8f3ebaf716a57a'
R_SHA = '99e1c0ad84a916fa9e08cf68b494c2d0efe0748fe956e579841c3d7614e49037'
# Follow-up number, label, SCHED profile, DATA blocks/ticks, audio quanta/ticks.
PROFILES = ((1, 'AE', 5, 24, 6000, 4, 12500, 'data-block-allowance'),
            (2, 'AF', 6, 24, 12000, 4, 12500, 'data-combined-allowance'),
            (3, 'AG', 7, 12, 6000, 2, 12500, 'audio-quantum-allowance'),
            (4, 'AH', 8, 12, 6000, 4, 6250, 'audio-timer-allowance'))
FOLLOWUP_HOST_REQUIREMENTS = {
    'host-ad-audio.log': ('5 AD audio worker profile suites and 2 limit mutations passed',),
    'host-ad-followup-data.log': ('AD follow-up DATA profiles 1..4 passed; AG/AH DATA transcripts equal AD; 7 behavioral mutations and 19 unsafe configurations rejected',),
    'host-ad-config.log': ('AD follow-up configuration suites passed (399 production admission/refusal checks; profile0 CPP/source paths unchanged)',),
}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT, text=True).strip()


def followup_schedule_legend():
    schema = schedule_legend(version=2)
    schema['followup_profiles'] = {
        label: {'followup_profile': profile, 'budget_profile': budget,
                'data_block_limit': blocks, 'data_tick_limit': ticks,
                'audio_refill_quanta': quanta, 'audio_service_ticks': audio_ticks,
                'tick_hz': 781250}
        for profile, label, budget, blocks, ticks, quanta, audio_ticks, _ in PROFILES}
    schema['followup_identification'] = 'SCHED budget.profile 5..8 plus exact build ID; unchanged 192-word version2 geometry'
    return schema


def followup_schedule_markdown(schema):
    text = schedule_legend_markdown(schema).decode()
    text += '\nFollow-ups over AD:\n\n'
    text += '| Profile | SCHED profile | DATA blocks | DATA ticks | Audio quanta | Audio ticks |\n'
    text += '|---|---:|---:|---:|---:|---:|\n'
    for _, label, budget, blocks, ticks, quanta, audio_ticks, _ in PROFILES:
        text += f'| {label} | {budget} | {blocks} | {ticks} | {quanta} | {audio_ticks} |\n'
    text += ('\nAudio quanta retain the original refill operation, including verified raw-sector fetch and cached fragments. '
             'An operation can cross time admission; these values are not hard frame deadlines. '
             'Startup DATA is retired after accepted PLAY.\n')
    return text.encode()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline-ad', type=Path,
                        default=ROOT.parent / 'budget-source/build/budget-ad')
    parser.add_argument('--reference-source', type=Path,
                        default=ROOT.parent / 'scheduling-source')
    parser.add_argument('--hardware-comparison', type=Path, default=ROOT.parent /
                        'morning-matrix-20261012/AA-AB-AC-AD-Hardware-Comparison.json')
    parser.add_argument('--legacy-ad-check', type=Path,
                        help='Optional current-source unchanged AD rebuild directory')
    parser.add_argument('--legacy-r-check', type=Path,
                        help='Optional current-source unchanged R rebuild directory')
    parser.add_argument('--out', type=Path,
                        default=ROOT / 'dist/K-UI-Toy-AD-Followup-Tests.zip')
    args = parser.parse_args()
    if git('status', '--porcelain'):
        raise ValueError('Commit exact reviewed source before packaging')
    from toy_ad_followup_audit import audit_followup
    inputs, files = {}, {}

    def remember(path):
        path = Path(path).absolute()
        if any(parent.is_symlink() for parent in (path, *path.parents)) or not path.is_file():
            raise ValueError('Input must be a regular nonsymlink file: ' + str(path))
        path = path.resolve(); data = path.read_bytes()
        if not data:
            raise ValueError('Empty package evidence: ' + str(path))
        if path in inputs and inputs[path] != digest(data):
            raise ValueError('Input changed during collection: ' + str(path))
        inputs[path] = digest(data)
        return data

    def add(name, data):
        if name.startswith('/') or '\\' in name or '..' in Path(name).parts or name in files:
            raise ValueError('Invalid or duplicate archive member: ' + name)
        files[name] = data

    test_dir = ROOT / 'build/followup-tests'
    source = json.loads(remember(test_dir / 'source.json'))
    runtime_commit = source.get('runtime_source_commit', '')
    if not re.fullmatch(r'[0-9a-f]{40}', runtime_commit):
        raise ValueError('Missing exact runtime source commit')
    if subprocess.run(['git', 'merge-base', '--is-ancestor', runtime_commit, 'HEAD'],
                      cwd=ROOT, check=False, capture_output=True).returncode:
        raise ValueError('Runtime source is not included in committed snapshot')
    expected = source.get('profiles', [])
    if len(expected) != 4 or {item.get('label') for item in expected} != {'AE', 'AF', 'AG', 'AH'}:
        raise ValueError('Runtime manifest must cover exactly AE, AF, AG and AH')
    expected = {item['label']: item for item in expected}
    metadata = {'format': 1, 'runtime_source_commit': runtime_commit,
                'source_snapshot_commit': git('rev-parse', 'HEAD'), 'profiles': [],
                'CDDA_transport_changed': False, 'CDDA_refill_scheduling_changed': ['AG', 'AH'],
                'new_candidates_hardware_behavior_verified': False,
                'new_candidates_audio_or_video_improvement_proven': False}
    add('TESTING.md', remember(ROOT / 'docs/toy-ad-followup-test.md'))
    add('tools/check_toy_sector.py', remember(ROOT / 'tools/check_toy_sector.py'))
    data_schema = budget_data_legend()
    data_schema['mode'] = {'0': 'AE/AF/AG/AH and AD: retained synchronous payload callback mode'}
    data_schema['control_window'] = {'followups': 'Startup DATA uses the independent engine; zero synchronous counters do not imply no DATA work'}
    for label, schema in (('SCHED', followup_schedule_legend()), ('DATA', data_schema),
                          ('PILOT', pilot_legend()), ('TRACE', trace_legend())):
        prefix = 'evidence/' + label.lower() + '-word-legend'
        add(prefix + '.json', (json.dumps(schema, indent=2) + '\n').encode())
        add(prefix + '.md', followup_schedule_markdown(schema) if label == 'SCHED'
            else legend_markdown(label, schema))

    for label, directory, sha, build, name in (
            ('AD', args.baseline_ad, AD_SHA, 'e680281d1604', 'reference/AD-tested-combined-allowance.kui'),
            ('R', args.reference_source / 'build/schedule-r', R_SHA, '73e8363c10f7', 'restore/R-private-write-through.kui')):
        data = remember(directory / 'retail-toy-pilot.kui')
        if digest(data) != sha or inspect_retail(data)['build'] != build:
            raise ValueError('Historical reference must remain byte exact: ' + label)
        add(name, data)
        metadata.setdefault('historical_references', {})[label] = {'build': build, 'sha256': sha,
                                                                 'file': name, 'recompiled': False}

    requirements = {**HOST_REQUIREMENTS, **FOLLOWUP_HOST_REQUIREMENTS}
    for name, footers in requirements.items():
        data = remember(test_dir / name)
        if any(footer.encode() not in data for footer in footers):
            raise ValueError('Missing passed production test evidence: ' + name)
        if name in FOLLOWUP_HOST_REQUIREMENTS and not data.rstrip().endswith(footers[-1].encode()):
            raise ValueError('Follow-up host test log does not end in its successful footer: ' + name)
        if name == 'linked-tests.log' and data.decode().rstrip().splitlines()[-1] != 'OK':
            raise ValueError('Linked audit test log does not end in OK')
        add('evidence/' + name, data)
    # Preserve the remaining build logs and structured host results as well.
    for path in sorted(test_dir.iterdir()):
        if path.is_file() and path.name != 'source.json' and path.name not in requirements and path.suffix in ('.log', '.json'):
            add('evidence/' + path.name, remember(path))

    for profile, label, budget_profile, blocks, ticks, quanta, audio_ticks, name in PROFILES:
        directory = ROOT / 'build' / ('followup-' + label.lower())
        admission = audit_followup(directory, args.baseline_ad, profile)
        budget = admission.get('budget', {}); audio = admission.get('audio_policy', {})
        if admission.get('admitted') is not True or any(budget.get(k) != value for k, value in
                (('profile', budget_profile), ('credit_limit', blocks), ('tick_limit', ticks), ('tick_hz', 781250))):
            raise ValueError('Independent DATA admission differs: ' + label)
        if any(audio.get(k) != value for k, value in
                (('refill_quanta', quanta), ('service_ticks', audio_ticks), ('tick_hz', 781250))):
            raise ValueError('Independent audio policy admission differs: ' + label)
        config = dict(line.split('=', 1) for line in remember(directory / 'build-config').decode().splitlines())
        item = expected[label]; build = admission.get('build')
        if (not build or item.get('build') != build or
                any(item.get(k) != value for k, value in
                    (('profile', profile), ('budget_profile', budget_profile), ('blocks', blocks), ('ticks', ticks),
                     ('audio_quanta', quanta), ('audio_ticks', audio_ticks))) or
                config.get('BUILD') != build or config.get('AD_FOLLOWUP_PROFILE') != str(profile) or
                config.get('DATA_SCHEDULE_MODE') != '2' or config.get('DATA_BUDGET_PROFILE') != '4' or
                config.get('SHARED_SCI') != '0' or config.get('ASYNC_CDDA') != '0'):
            raise ValueError('Compiled build and runtime manifest disagree: ' + label)
        runtime = remember(directory / 'retail-toy-pilot.kui')
        if digest(runtime) != admission.get('runtime', {}).get('sha256') or inspect_retail(runtime)['build'] != build:
            raise ValueError('Runtime differs from independently admitted artifact: ' + label)
        add('profiles/' + label + '-' + name + '.kui', runtime)
        add('evidence/' + label + '-linked-admission.json',
            (json.dumps(admission, indent=2, sort_keys=True) + '\n').encode())
        metadata['profiles'].append({'label': label, 'profile': profile, 'budget_profile': budget_profile,
                                     'blocks': blocks, 'ticks': ticks, 'audio_refill_quanta': quanta,
                                     'audio_service_ticks': audio_ticks, 'build': build,
                                     'runtime_sha256': digest(runtime)})
        collected = set()
        for path in sorted(directory.rglob('*')):
            if path.is_file() and (path.suffix in ('.elf', '.bin', '.map', '.su') or
                                  path.name in ('build-config', 'toy_pilot_resident_symbols.h')):
                relative = path.relative_to(directory).as_posix()
                add('evidence/' + label + '/' + relative, remember(path)); collected.add(relative)
        required = {part + suffix for part in ('worker', 'resident-sci', 'stage')
                    for suffix in ('.elf', '.bin', '.map')} | {'entry.elf', 'entry.map', 'build-config'}
        if not required <= collected or not any(path.endswith('.su') for path in collected):
            raise ValueError('Missing ELF/binary/map/emitted-stack evidence: ' + label)

    for label, directory, sha, build in (
            ('AD', args.legacy_ad_check, AD_SHA, 'e680281d1604'),
            ('R', args.legacy_r_check, R_SHA, '73e8363c10f7')):
        if directory is None:
            continue
        data = remember(directory / 'retail-toy-pilot.kui')
        if digest(data) != sha or inspect_retail(data)['build'] != build:
            raise ValueError('Current-source legacy rebuild is not byte exact: ' + label)
        prefix = 'evidence/legacy-' + label + '-byte-exact-rebuild/'
        add(prefix + 'retail-toy-pilot.kui', data)
        for path in sorted(directory.rglob('*')):
            if path.is_file() and (path.suffix in ('.elf', '.bin', '.map', '.su', '.log') or
                                  path.name in ('build-config', 'toy_pilot_resident_symbols.h')):
                add(prefix + path.relative_to(directory).as_posix(), remember(path))
        metadata.setdefault('current_source_legacy_rebuilds', {})[label] = {'build': build, 'sha256': sha, 'byte_exact': True}

    comparison = remember(args.hardware_comparison)
    hardware = json.loads(comparison)
    if not isinstance(hardware, dict) or b'libfile_' in comparison:
        raise ValueError('Hardware matrix must be a JSON object without internal file identifiers')
    if not all(('"' + label + '"').encode() in comparison for label in ('AA', 'AB', 'AC', 'AD')):
        raise ValueError('Hardware comparison must cover all four prior matrix profiles')
    add('evidence/AA-AB-AC-AD-hardware-comparison.json', comparison)
    metadata['hardware_comparison_sha256'] = digest(comparison)
    for path in [ROOT / 'LICENSE', ROOT / 'THIRD_PARTY.md', *sorted((ROOT / 'LICENSES').glob('*'))]:
        if path.is_file():
            add(path.relative_to(ROOT).as_posix(), remember(path))
    snapshot = subprocess.check_output(['git', 'archive', 'HEAD'], cwd=ROOT)
    add('source-snapshot.tar', snapshot); metadata['source_snapshot_sha256'] = digest(snapshot)
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
    print(json.dumps({'file': str(out), 'sha256': digest(out.read_bytes()), 'bytes': out.stat().st_size,
                      'profiles': metadata['profiles'], 'all_member_checksums_verified': True}, indent=2))


if __name__ == '__main__':
    main()
