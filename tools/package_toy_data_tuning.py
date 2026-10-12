#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package AI/AJ/AK/AL with independent admission and exact tested references."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import zipfile

from package_toy_loader_budget import budget_data_legend, schedule_legend, schedule_legend_markdown
from package_toy_loader_data_probe import pilot_legend, legend_markdown
from package_toy_loader_trace import trace_legend
from retail_package import inspect_retail

ROOT = Path(__file__).resolve().parents[1]
AE_SHA = '037697fca88aacec5927784c24091a553918f44f68814d1bafb614e7b4d769f8'
AF_SHA = '1fdc977d61d5ec50b0c66777a60db4dc9bc3cd73dbeecc5bda787af9e195d5f4'
AD_SHA = '8f478c473f2574409a5a0d346237a1b09995d350d960f3150a8f3ebaf716a57a'
R_SHA = '99e1c0ad84a916fa9e08cf68b494c2d0efe0748fe956e579841c3d7614e49037'
# Tuning number, label, SCHED profile, verified512-byte blocks/ticks/finite steps.
PROFILES = ((1, 'AI', 9, 24, 12000, 2048, 'data-poll-step-allowance'),
            (2, 'AJ', 10, 24, 9000, 1024, 'data-intermediate-timer'),
            (3, 'AK', 11, 32, 12000, 2048, 'data-block-allowance'),
            (4, 'AL', 12, 32, 18000, 2048, 'data-longer-timer'))
HOST_REQUIREMENTS = {
    'host-all.log': '38 Toy pilot regression suites passed',
    'host-ad-audio.log': '5 AD audio worker profile suites and 2 limit mutations passed',
    'host-ad-followup-data.log': 'AD follow-up DATA profiles 1..4 passed; AG/AH DATA transcripts equal AD; 7 behavioral mutations and 19 unsafe configurations rejected',
    'host-ad-config.log': 'AD follow-up configuration suites passed (399 production admission/refusal checks; profile0 CPP/source paths unchanged)',
    'host-tuning-data.log': 'DATA tuning profiles 0..4 and 5 real-wire suites passed; 14 behavioral mutations and 23 unsafe configurations rejected',
    'host-tuning-config.log': 'DATA tuning configuration suites passed (AI/AJ/AK/AL; legacy R/B/Y/Z/AA..AH preserved)',
}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT, text=True).strip()


def tuning_schedule_legend():
    schema = schedule_legend(version=2)
    schema['data_tuning_profiles'] = {
        label: {'data_tuning_profile': profile, 'followup_profile': 2,
                'budget_profile': budget, 'data_block_limit': blocks,
                'data_tick_limit': ticks, 'finite_step_limit': steps,
                'audio_refill_quanta': 4, 'audio_service_ticks': 12500,
                'tick_hz': 781250}
        for profile, label, budget, blocks, ticks, steps, _ in PROFILES}
    schema['data_tuning_identification'] = (
        'SCHED budget.profile9..12 plus exact build ID; unchanged192-word version2. '
        'Finite step limits are profile metadata, not additional wire words.')
    return schema


def tuning_schedule_markdown(schema):
    text = schedule_legend_markdown(schema).decode()
    text += '\nDATA tuning over exact AF:\n\n'
    text += '| Profile | SCHED profile | DATA blocks | DATA ticks | Finite steps | Audio quanta | Audio ticks |\n'
    text += '|---|---:|---:|---:|---:|---:|---:|\n'
    for _, label, budget, blocks, ticks, steps, _ in PROFILES:
        text += f'| {label} | {budget} | {blocks} | {ticks} | {steps} | 4 | 12500 |\n'
    text += ('\nThe step limit is profile metadata. Poll steps can make no wire progress. '
             'All limits remain finite and a final operation can cross time admission; '
             'they do not promise frame deadlines. Audio refill policy remains exact AF.\n')
    return text.encode()


def validate_hardware(hardware):
    """Check delivered raw geometry, critical agreement and explicit missing words."""
    if not isinstance(hardware, dict) or not isinstance(hardware.get('profiles'), dict):
        raise ValueError('Hardware matrix must contain decoded profiles')
    if set(hardware['profiles']) != {'AE', 'AF', 'AG', 'AH'} or not isinstance(hardware.get('reference_evidence', {}).get('AD_summary'), dict):
        raise ValueError('Hardware comparison must cover exact AE/AF/AG/AH and the AD reference')
    legend = trace_legend()
    trace_fields = [name for page in legend['pages'] for row in page['rows'] for name in row]

    def pages(report, count, complete=True):
        result = {}
        for item in report.get('pages', []):
            page = int(item['page'], 16) if isinstance(item['page'], str) else item['page']
            if page in result or page not in range(count) or len(item['words']) != 16:
                raise ValueError('Invalid hardware raw page geometry')
            result[page] = [int(word, 16) if isinstance(word, str) else word for word in item['words']]
            if any(not 0 <= word <= 0xffffffff for word in result[page]):
                raise ValueError('Hardware word is outside uint32')
        if complete and set(result) != set(range(count)):
            raise ValueError('Hardware report lacks complete required raw pages')
        return result

    for label, profile in hardware['profiles'].items():
        evidence = profile.get('primary_evidence', {})
        if not {'sched-pages.json', 'data-pilot-pages.json', 'trace-pages.json', 'trace-decoded.json', 'critical-independent.json'} <= set(evidence):
            raise ValueError('Missing primary current hardware evidence: ' + label)
        sched = pages(evidence['sched-pages.json'], 12)
        trace = pages(evidence['trace-pages.json'], 26, complete=False)
        if not set(range(14)) <= set(trace):
            raise ValueError('Missing startup TRACE words: ' + label)
        for key, count in (('data_pages_hex', 12), ('pilot_pages_hex', 8)):
            report = evidence['data-pilot-pages.json'].get(key, {})
            if {int(page, 16) for page in report} != set(range(count)) or any(len(words) != 16 for words in report.values()):
                raise ValueError('Missing complete DATA/PILOT raw words: ' + label)
        coverage = profile.get('TRACE_coverage', {})
        missing = [f'{i:02X}' for i in range(26) if i not in trace]
        if (coverage.get('observed_words') != len(trace) * 16 or coverage.get('missing_pages') != missing or
                coverage.get('missing_words_not_assumed_zero') is not True):
            raise ValueError('Missing hardware TRACE words are not explicitly declared: ' + label)
        fields = {trace_fields[page * 16 + i]: word for page, words in trace.items() for i, word in enumerate(words)}
        decoded = profile.get('TRACE_decoded_by_assembler', {})
        if decoded.get('unobserved_fields_are_null') is not True or len(decoded.get('phases', [])) != 2:
            raise ValueError('Hardware TRACE decode does not preserve missing fields: ' + label)
        for phase in decoded['phases']:
            prefix = f"phase[{phase['phase']}]."
            for key, value in phase['counts'].items():
                if value != fields.get(prefix + key):
                    raise ValueError('Hardware count differs from current observed words: ' + label)
            for name, metric in phase['metrics'].items():
                for key in ('samples', 'ticks_total', 'ticks_max', 'ticks_min'):
                    if metric[key] != fields.get(prefix + name + '.' + key):
                        raise ValueError('Hardware timing fills or changes an unobserved word: ' + label)
                for i, value in enumerate(metric['histogram']):
                    if value != fields.get(prefix + name + f'.histogram[{i}]'):
                        raise ValueError('Hardware histogram fills or changes an unobserved word: ' + label)
        independent = evidence['critical-independent.json']
        if independent.get('uncertain_words') or independent.get('mismatches'):
            raise ValueError('Hardware critical transcription is unresolved: ' + label)
        reviewed = 0
        for kind, primary in (('SCHED', sched), ('TRACE', trace)):
            for page, words in independent.get(kind, {}).items():
                if primary.get(int(page, 16)) != [int(word, 16) for word in words]:
                    raise ValueError('Hardware primary and independent critical words differ: ' + label)
                reviewed += len(words)
        if reviewed != 144:
            raise ValueError('Hardware profile lacks its complete144-word independent review: ' + label)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline-af', type=Path,
                        default=ROOT.parent / 'ad-followup-source/build/followup-af')
    parser.add_argument('--reference-followup-source', type=Path,
                        default=ROOT.parent / 'ad-followup-source')
    parser.add_argument('--baseline-ad', type=Path,
                        default=ROOT.parent / 'budget-source/build/budget-ad')
    parser.add_argument('--reference-source', type=Path,
                        default=ROOT.parent / 'scheduling-source')
    parser.add_argument('--hardware-comparison', type=Path, default=ROOT.parent /
                        'followup-matrix-20261012/AD-AE-AF-AG-AH-Hardware-Comparison.json')
    parser.add_argument('--hardware-ad-reference', type=Path, default=ROOT.parent /
                        'followup-matrix-20261012/reference/AA-AB-AC-AD-Hardware-Comparison.json')
    parser.add_argument('--legacy-af-check', type=Path,
                        help='Optional current-source unchanged AF rebuild directory')
    parser.add_argument('--legacy-r-check', type=Path,
                        help='Optional current-source unchanged R rebuild directory')
    parser.add_argument('--out', type=Path,
                        default=ROOT / 'dist/K-UI-Toy-DATA-Tuning-Tests.zip')
    args = parser.parse_args()
    if git('status', '--porcelain'):
        raise ValueError('Commit exact reviewed source before packaging')
    from toy_data_tuning_audit import audit_tuning
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

    test_dir = ROOT / 'build/tuning-tests'
    source_data = remember(test_dir / 'source.json'); source = json.loads(source_data)
    runtime_commit = source.get('runtime_source_commit', '')
    if not re.fullmatch(r'[0-9a-f]{40}', runtime_commit):
        raise ValueError('Missing exact runtime source commit')
    if subprocess.run(['git', 'merge-base', '--is-ancestor', runtime_commit, 'HEAD'],
                      cwd=ROOT, check=False, capture_output=True).returncode:
        raise ValueError('Runtime source is not included in committed snapshot')
    expected = source.get('profiles', [])
    if len(expected) != 4 or {item.get('label') for item in expected} != {'AI', 'AJ', 'AK', 'AL'}:
        raise ValueError('Runtime manifest must cover exactly AI, AJ, AK and AL')
    expected = {item['label']: item for item in expected}
    metadata = {'format': 1, 'runtime_source_commit': runtime_commit,
                'source_snapshot_commit': git('rev-parse', 'HEAD'), 'profiles': [],
                'CDDA_transport_changed': False, 'CDDA_refill_scheduling_changed': False,
                'new_candidates_hardware_behavior_verified': False,
                'new_candidates_audio_or_video_improvement_proven': False}
    add('evidence/source.json', source_data)
    add('TESTING.md', remember(ROOT / 'docs/toy-data-tuning-test.md'))
    add('tools/check_toy_sector.py', remember(ROOT / 'tools/check_toy_sector.py'))
    data_schema = budget_data_legend()
    data_schema['mode'] = {'0': 'AI/AJ/AK/AL and exact AE/AF/AD: retained synchronous payload callback mode'}
    data_schema['control_window'] = {'data_tunings': 'Startup DATA uses the independent engine; zero synchronous counters do not imply no DATA work'}
    for label, schema in (('SCHED', tuning_schedule_legend()), ('DATA', data_schema),
                          ('PILOT', pilot_legend()), ('TRACE', trace_legend())):
        prefix = 'evidence/' + label.lower() + '-word-legend'
        add(prefix + '.json', (json.dumps(schema, indent=2) + '\n').encode())
        add(prefix + '.md', tuning_schedule_markdown(schema) if label == 'SCHED'
            else legend_markdown(label, schema))

    reference_followup = args.reference_followup_source.resolve()
    for label, directory, sha, build, name in (
            ('AE', reference_followup / 'build/followup-ae', AE_SHA, '025646791d01', 'reference/AE-tested-data-allowance.kui'),
            ('AF', args.baseline_af, AF_SHA, '025646791d02', 'reference/AF-tested-combined-allowance.kui'),
            ('AD', args.baseline_ad, AD_SHA, 'e680281d1604', 'reference/AD-tested-combined-allowance.kui'),
            ('R', args.reference_source / 'build/schedule-r', R_SHA, '73e8363c10f7', 'restore/R-private-write-through.kui')):
        data = remember(directory / 'retail-toy-pilot.kui')
        if digest(data) != sha or inspect_retail(data)['build'] != build:
            raise ValueError('Historical reference must remain byte exact: ' + label)
        add(name, data)
        metadata.setdefault('historical_references', {})[label] = {'build': build, 'sha256': sha,
                                                                 'file': name, 'recompiled': False}
        if label in ('AE', 'AF'):
            completed = subprocess.run([sys.executable,
                str(reference_followup / 'tools/toy_ad_followup_audit.py'), str(directory.resolve()),
                '--profile', '1' if label == 'AE' else '2'], cwd=reference_followup,
                check=False, text=True, capture_output=True)
            if completed.returncode:
                raise ValueError(label + ' failed its original frozen linked gate: ' + completed.stderr.strip())
            proof = json.loads(completed.stdout)
            if proof.get('admitted') is not True or proof.get('build') != build or proof.get('runtime', {}).get('sha256') != sha:
                raise ValueError('Historical linked proof differs: ' + label)
            add('evidence/' + label + '-original-linked-admission.json',
                (json.dumps(proof, indent=2, sort_keys=True) + '\n').encode())

    for name, footer in HOST_REQUIREMENTS.items():
        if not footer:
            raise ValueError('New test successful footer has not been confirmed: ' + name)
        data = remember(test_dir / name)
        if data.decode().rstrip().splitlines()[-1] != footer:
            raise ValueError('Host log lacks its exact final successful footer: ' + name)
        if any(marker in data for marker in (b'ERROR: AddressSanitizer', b'runtime error:', b'Traceback (most recent call last):')):
            raise ValueError('Host log contains unresolved runtime/test diagnostics: ' + name)
        add('evidence/' + name, data)
    linked = remember(test_dir / 'host-tuning-linked.log').decode()
    if linked.rstrip().splitlines()[-1] != 'OK' or not re.search(r'^Ran 26 tests in ', linked, re.M):
        raise ValueError('Linked mutation test log does not end in a complete successful run')
    add('evidence/host-tuning-linked.log', linked.encode())
    structured_names = {'tuning-data-tests.json', 'data-tuning-config-tests.json'}
    data_tests = json.loads(remember(test_dir / 'tuning-data-tests.json'))
    config_tests = json.loads(remember(test_dir / 'data-tuning-config-tests.json'))
    if (data_tests.get('sanitizers') != ['address', 'undefined'] or
            data_tests.get('retained_AF_step_constraint_reproduced') is not True or
            len(data_tests.get('results', [])) != 10 or len(data_tests.get('mutation_results', [])) != 14 or
            len(data_tests.get('unsafe_configurations', [])) != 23 or
            any(result.get('exit_code') != 0 for result in data_tests['results']) or
            any(result.get('exit_code') == 0 for result in data_tests['mutation_results'] + data_tests['unsafe_configurations'])):
        raise ValueError('Structured DATA evidence lacks complete actual-engine/sanitizer/mutation success')
    if config_tests.get('runtime_source_commit') != runtime_commit or len(config_tests.get('checks', [])) != 637:
        raise ValueError('Structured configuration evidence differs from the exact runtime/source checks')
    source_hashes = [data_tests.get('source_sha256', {}),
                     *(result.get('source_sha256', {}) for result in data_tests['results'])]
    for identities in source_hashes:
        if not identities:
            raise ValueError('DATA test evidence lacks source bindings')
        for name, expected_sha in identities.items():
            if Path(name).is_absolute() or '..' in Path(name).parts or digest(remember(ROOT / name)) != expected_sha:
                raise ValueError('DATA host evidence source changed: ' + name)
    for profile, label, reported, blocks, ticks, steps, _ in PROFILES:
        observed = data_tests.get('matrix_observations', {}).get(str(profile), {})
        configured = config_tests.get('profiles', {}).get(str(profile), {})
        if (any(observed.get(key) != value for key, value in
                    (('report_profile', reported), ('block_limit', blocks), ('tick_limit', ticks), ('step_limit', steps))) or
                any(configured.get(key) != value for key, value in
                    (('label', label), ('report_profile', reported), ('blocks', blocks), ('ticks', ticks),
                     ('steps', steps), ('audio_quanta', 4), ('audio_ticks', 12500)))):
            raise ValueError('Structured host evidence profile/caps differ: ' + label)
    for name in sorted(structured_names):
        add('evidence/' + name, remember(test_dir / name))
    for path in sorted(test_dir.iterdir()):
        if path.is_file() and path.name not in {'source.json', 'host-tuning-linked.log', *HOST_REQUIREMENTS, *structured_names} and path.suffix in ('.log', '.json'):
            add('evidence/' + path.name, remember(path))

    for profile, label, budget_profile, blocks, ticks, steps, name in PROFILES:
        directory = ROOT / 'build' / ('tuning-' + label.lower())
        admission = audit_tuning(directory, args.baseline_af, profile)
        budget = admission.get('budget', {}); audio = admission.get('audio_policy', {})
        if (admission.get('admitted') is not True or admission.get('data_tuning_profile') != profile or
                admission.get('followup_profile') != 2 or admission.get('budget_profile') != budget_profile or
                any(budget.get(k) != value for k, value in
                    (('profile', budget_profile), ('credit_limit', blocks), ('tick_limit', ticks),
                     ('finite_step_limit', steps), ('tick_hz', 781250)))):
            raise ValueError('Independent DATA tuning admission differs: ' + label)
        if any(audio.get(k) != value for k, value in
                (('refill_quanta', 4), ('service_ticks', 12500), ('tick_hz', 781250), ('changed_from_AF', False))):
            raise ValueError('Independent audio policy differs from exact AF: ' + label)
        config_data = remember(directory / 'build-config')
        config = dict(line.split('=', 1) for line in config_data.decode().splitlines())
        item = expected[label]; build = admission.get('build')
        if (not build or item.get('build') != build or
                any(item.get(k) != value for k, value in
                    (('profile', profile), ('budget_profile', budget_profile), ('blocks', blocks), ('ticks', ticks),
                     ('steps', steps), ('audio_quanta', 4), ('audio_ticks', 12500))) or
                config.get('BUILD') != build or config.get('DATA_TUNING_PROFILE') != str(profile) or
                config.get('AD_FOLLOWUP_PROFILE') != '2' or config.get('DATA_SCHEDULE_MODE') != '2' or
                config.get('DATA_BUDGET_PROFILE') != '4' or config.get('SHARED_SCI') != '0' or config.get('ASYNC_CDDA') != '0'):
            raise ValueError('Compiled build and runtime manifest disagree: ' + label)
        runtime = remember(directory / 'retail-toy-pilot.kui')
        if digest(runtime) != admission.get('runtime', {}).get('sha256') or inspect_retail(runtime)['build'] != build:
            raise ValueError('Runtime differs from independently admitted artifact: ' + label)
        add('profiles/' + label + '-' + name + '.kui', runtime)
        add('evidence/' + label + '-linked-admission.json',
            (json.dumps(admission, indent=2, sort_keys=True) + '\n').encode())
        metadata['profiles'].append({'label': label, 'data_tuning_profile': profile,
            'followup_profile': 2, 'budget_profile': budget_profile, 'blocks': blocks, 'ticks': ticks,
            'finite_step_limit': steps, 'audio_refill_quanta': 4, 'audio_service_ticks': 12500,
            'build': build, 'runtime_sha256': digest(runtime)})
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
            ('AF', args.legacy_af_check, AF_SHA, '025646791d02'),
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

    comparison = remember(args.hardware_comparison); hardware = json.loads(comparison)
    if not isinstance(hardware, dict) or b'libfile_' in comparison:
        raise ValueError('Hardware matrix must be an object without internal file identifiers')
    validate_hardware(hardware)
    add('evidence/AD-AE-AF-AG-AH-hardware-comparison.json', comparison)
    metadata['hardware_comparison_sha256'] = digest(comparison)
    ad_comparison = remember(args.hardware_ad_reference)
    ad_hardware = json.loads(ad_comparison)
    reference = hardware['reference_evidence']
    if (digest(ad_comparison) != reference.get('sha256') or
            args.hardware_ad_reference.name != reference.get('file') or
            not isinstance(ad_hardware.get('profiles', {}).get('AD'), dict)):
        raise ValueError('Prior raw AD matrix differs from the exact hardware reference')
    add('evidence/AA-AB-AC-AD-hardware-comparison.json', ad_comparison)
    metadata['hardware_AD_reference_sha256'] = digest(ad_comparison)
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
