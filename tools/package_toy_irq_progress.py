#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package two independently admitted startup IRQ experiments and exact controls."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys

from package_cdda_calibration import archive_name, sha, write_archive
from package_toy_loader_budget import budget_data_legend, schedule_legend, schedule_legend_markdown
from package_toy_loader_data_probe import pilot_legend, legend_markdown
from package_toy_loader_trace import trace_legend
from retail_package import inspect_retail

ROOT = Path(__file__).resolve().parents[1]
AI_SHA = '3121b5e2b6efb6a34a51cfdb5aecd6496362048859092e3fa4d3caeada85d26e'
AF_SHA = '1fdc977d61d5ec50b0c66777a60db4dc9bc3cd73dbeecc5bda787af9e195d5f4'
R_SHA = '99e1c0ad84a916fa9e08cf68b494c2d0efe0748fe956e579841c3d7614e49037'
PROFILES = ((1, 'AM', 13, 2048), (2, 'AN', 14, 1))
HOST_FOOTER = ('Startup IRQ profiles 0..2 and 3 real-wire suites passed; 3 emitted rehooks verified; '
               '12 behavioral/assembly mutations and 27 unsafe configurations rejected')
CONFIG_FOOTER = 'Startup IRQ configuration suites passed (AM/AN; legacy R/B/Y/Z/AA..AL preserved)'
RETAINED = {
    'full-host': ('irq-retained-full.log', '38 Toy pilot regression suites passed'),
    'tuning-host': ('irq-retained-tuning.log', 'DATA tuning profiles 0..4 and 5 real-wire suites passed; 14 behavioral mutations and 23 unsafe configurations rejected'),
    'legacy-config': ('irq-retained-tuning-config.log', 'DATA tuning configuration suites passed (AI/AJ/AK/AL; legacy R/B/Y/Z/AA..AH preserved)'),
    'audio-host': ('irq-retained-audio.log', '5 AD audio worker profile suites and 2 limit mutations passed'),
}


def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT, text=True).strip()


def irq_schedule_legend():
    schema = schedule_legend(version=2)
    schema['startup_IRQ_profiles'] = {
        label: {'startup_irq_profile': profile, 'budget_profile': report,
                'data_tuning_profile': 1, 'followup_profile': 2,
                'data_block_limit': 24, 'data_tick_limit': 12000,
                'finite_foreground_step_limit': steps, 'startup_SCI_priority': 7,
                'single_pump_per_IRQ': True, 'audio_refill_quanta': 4,
                'audio_service_ticks': 12500, 'tick_hz': 781250,
                'AN_second_step_timer_check_optimized_out': profile == 2}
        for profile, label, report, steps in PROFILES}
    schema['identification'] = 'SCHED budget.profile13/14 and build ID; unchanged192-word version2. Priority and finite-step limits are profile metadata.'
    schema['scope'] = 'Startup SCI only; GD mask retained; original SCI priority/vector restored for forwarding and retirement; startup does not reenable after first accepted PLAY.'
    return schema


def irq_schedule_markdown(schema):
    text = schedule_legend_markdown(schema).decode()
    text += '\nStartup IRQ profiles over exact AI:\n\n'
    text += '| Profile | SCHED profile | Priority | Foreground steps | DATA blocks | DATA ticks | Audio quanta/ticks |\n'
    text += '|---|---:|---:|---:|---:|---:|---|\n'
    for _, label, report, steps in PROFILES:
        text += f'| {label} | {report} | 7 | {steps} | 24 | 12000 | 4 / 12500 |\n'
    text += ('\nOne bounded engine pump runs per IRQ. AN exits after one foreground step, '
             'so a second-step timer check is unreachable and compiled out; a single step is not a timer deadline. '
             'Startup retires permanently at accepted PLAY. The capture combination includes Start and may advance the game.\n')
    return text.encode()


def validate_hardware(hardware):
    """Validate current raw words, independent readings and null unseen fields."""
    if not isinstance(hardware, dict) or not {'AI', 'AJ', 'AK', 'AL'} <= set(hardware.get('profiles', {})):
        raise ValueError('Hardware comparison lacks all four current DATA-tuning captures')
    fields = [name for page in trace_legend()['pages'] for row in page['rows'] for name in row]
    def pages(report, count, complete=True):
        result = {}
        for item in report.get('pages', []):
            number = int(item['page'], 16) if isinstance(item['page'], str) else item['page']
            if number in result or number not in range(count) or len(item['words']) != 16:
                raise ValueError('Invalid current hardware page geometry')
            result[number] = [int(word, 16) if isinstance(word, str) else word for word in item['words']]
            if any(not 0 <= word <= 0xffffffff for word in result[number]):
                raise ValueError('Hardware word is outside uint32')
        if complete and set(result) != set(range(count)):
            raise ValueError('Incomplete required current hardware report')
        return result
    for label in ('AI', 'AJ', 'AK', 'AL'):
        item = hardware['profiles'][label]; evidence = item['primary_evidence']
        if not {'sched-pages.json', 'data-pilot-pages.json', 'trace-pages.json', 'trace-decoded.json', 'critical-independent.json'} <= set(evidence):
            raise ValueError('Missing primary current reports: ' + label)
        sched = pages(evidence['sched-pages.json'], 12); trace = pages(evidence['trace-pages.json'], 26, False)
        if not set(range(14)) <= set(trace):
            raise ValueError('Missing startup TRACE words: ' + label)
        for name, count in (('data_pages_hex', 12), ('pilot_pages_hex', 8)):
            raw = evidence['data-pilot-pages.json'][name]
            if {int(k, 16) for k in raw} != set(range(count)) or any(len(v) != 16 for v in raw.values()):
                raise ValueError('Incomplete current DATA/PILOT words: ' + label)
        coverage = item['TRACE_coverage']; missing = [f'{i:02X}' for i in range(26) if i not in trace]
        if coverage['observed_words'] != len(trace) * 16 or coverage['missing_pages'] != missing or coverage['missing_words_not_assumed_zero'] is not True:
            raise ValueError('Unseen TRACE pages are not explicit: ' + label)
        raw_fields = {fields[page * 16 + i]: word for page, words in trace.items() for i, word in enumerate(words)}
        decoded = item['TRACE_decoded_by_assembler']
        if decoded.get('unobserved_fields_are_null') is not True or len(decoded['phases']) != 2:
            raise ValueError('TRACE decoder does not preserve missing words: ' + label)
        for phase in decoded['phases']:
            prefix = f"phase[{phase['phase']}]."
            for key, value in phase['counts'].items():
                if value != raw_fields.get(prefix + key):
                    raise ValueError('TRACE count differs from current pixels: ' + label)
            for name, metric in phase['metrics'].items():
                for key in ('samples', 'ticks_total', 'ticks_max', 'ticks_min'):
                    if metric[key] != raw_fields.get(prefix + name + '.' + key):
                        raise ValueError('TRACE timing changes or fills an unseen word: ' + label)
                for i, value in enumerate(metric['histogram']):
                    if value != raw_fields.get(prefix + name + f'.histogram[{i}]'):
                        raise ValueError('TRACE histogram changes or fills an unseen word: ' + label)
        independent = evidence['critical-independent.json']; reviewed = 0
        if independent.get('uncertain_words') or independent.get('mismatches') or independent.get('comparison_pending'):
            raise ValueError('Independent current transcription is unresolved: ' + label)
        for kind, primary in (('SCHED', sched), ('TRACE', trace)):
            for page, words in independent[kind].items():
                if primary.get(int(page, 16)) != [int(word, 16) for word in words]:
                    raise ValueError('Current primary/independent words differ: ' + label)
                reviewed += len(words)
        if reviewed != 144:
            raise ValueError('Missing complete144-word independent review: ' + label)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline-ai', type=Path, default=ROOT.parent / 'aeaf-tuning-source/build/tuning-ai')
    parser.add_argument('--baseline-af', type=Path, default=ROOT.parent / 'ad-followup-source/build/followup-af')
    parser.add_argument('--restore-r', type=Path, default=ROOT.parent / 'scheduling-source/build/schedule-r')
    parser.add_argument('--hardware-comparison', type=Path, default=ROOT.parent / 'tuning-matrix-20261012/AF-AI-AJ-AK-AL-Hardware-Comparison.json')
    parser.add_argument('--hardware-errata', type=Path, default=ROOT.parent / 'tuning-matrix-20261012/crc-glyph-errata.json')
    parser.add_argument('--out', type=Path, default=ROOT / 'dist/K-UI-Toy-IRQ-Progress-Tests.zip')
    args = parser.parse_args()
    if git('status', '--porcelain'):
        raise ValueError('Commit the exact reviewed source before packaging')
    from toy_irq_progress_audit import audit_irq_progress
    inputs, files = {}, {}
    def remember(path):
        path = Path(path).absolute()
        if any(p.is_symlink() for p in (path, *path.parents)) or not path.is_file():
            raise ValueError('Input must be a regular nonsymlink file: ' + str(path))
        path = path.resolve(); data = path.read_bytes()
        if not data or path in inputs and inputs[path] != sha(data):
            raise ValueError('Empty or changed package input: ' + str(path))
        inputs[path] = sha(data); return data
    def add(name, data):
        archive_name(name)
        if name in files:
            raise ValueError('Duplicate archive member: ' + name)
        files[name] = data
    def include(name, path):
        data = remember(path); add(name, data); return data
    def bind_sources(identities):
        if not identities:
            raise ValueError('Missing source bindings in test receipt')
        for name, expected in identities.items():
            archive_name(name)
            if sha(remember(ROOT / name)) != expected:
                raise ValueError('Test source changed after successful run: ' + name)
    def terminal_log(path, footer):
        data = remember(path)
        if data.decode().rstrip().splitlines()[-1] != footer:
            raise ValueError('Missing exact terminal success footer: ' + str(path))
        if any(marker in data for marker in (b'ERROR: AddressSanitizer', b'runtime error:', b'Traceback (most recent call last):')):
            raise ValueError('Unresolved sanitizer/test diagnostic: ' + str(path))
        return data
    test_dir = ROOT / 'build/irq-tests'
    source = json.loads(include('evidence/source.json', test_dir / 'source.json'))
    runtime_commit = source.get('runtime_source_commit', '')
    if not re.fullmatch(r'[0-9a-f]{40}', runtime_commit) or subprocess.run(['git', 'merge-base', '--is-ancestor', runtime_commit, 'HEAD'], cwd=ROOT, capture_output=True).returncode:
        raise ValueError('Missing committed runtime source ancestor')
    expected = source.get('profiles', [])
    if len(expected) != 2 or {p.get('label') for p in expected} != {'AM', 'AN'}:
        raise ValueError('Runtime source manifest must cover exactly AM and AN')
    expected = {p['label']: p for p in expected}
    metadata = {'format': 1, 'runtime_source_commit': runtime_commit, 'source_snapshot_commit': git('rev-parse', 'HEAD'),
        'profiles': [], 'CDDA_transport_changed': False, 'CDDA_refill_scheduling_changed': False,
        'startup_reenabled_after_accepted_PLAY': False, 'hardware_behavior_verified': False,
        'audio_or_video_improvement_proven': False}
    include('TESTING.md', ROOT / 'docs/toy-irq-progress-test.md')
    include('tools/check_toy_sector.py', ROOT / 'tools/check_toy_sector.py')
    for label, schema in (('SCHED', irq_schedule_legend()), ('DATA', budget_data_legend()), ('PILOT', pilot_legend()), ('TRACE', trace_legend())):
        prefix = 'evidence/' + label.lower() + '-word-legend'
        add(prefix + '.json', (json.dumps(schema, indent=2) + '\n').encode())
        add(prefix + '.md', irq_schedule_markdown(schema) if label == 'SCHED' else legend_markdown(label, schema))
    retained = json.loads(include('evidence/irq-retained-evidence.json', ROOT / 'build/irq-retained-evidence.json'))
    if retained.get('runtime_source_commit') != runtime_commit or {r['name'] for r in retained.get('results', [])} != set(RETAINED):
        raise ValueError('Retained regression receipt differs from this runtime freeze')
    for result in retained['results']:
        filename, footer = RETAINED[result['name']]
        log_path = Path(result.get('log_path', '')).absolute()
        if log_path.name != filename or log_path.parent != ROOT.parent:
            raise ValueError('Retained receipt points to an unexpected log: ' + filename)
        data = terminal_log(log_path, footer)
        if result.get('exit_code') != 0 or result.get('log_sha256') != sha(data) or result.get('stdout_stderr') != data.decode() or result.get('passed_footer', '').encode() not in data.splitlines():
            raise ValueError('Retained test receipt does not bind its actual passing log: ' + filename)
        add('evidence/' + filename, data)
    config = json.loads(include('evidence/startup-irq-config-tests.json', test_dir / 'startup-irq-config-tests.json'))
    host = json.loads(include('evidence/irq-progress-tests.json', test_dir / 'irq-progress-tests.json'))
    add('evidence/host-irq-config.log', terminal_log(test_dir / 'host-irq-config.log', CONFIG_FOOTER))
    add('evidence/host-irq-progress.log', terminal_log(test_dir / 'host-irq-progress.log', HOST_FOOTER))
    if config.get('runtime_source_commit') != runtime_commit or len(config.get('checks', [])) != 582:
        raise ValueError('Incomplete current582-case production configuration proof')
    if (host.get('sanitizers') != ['address', 'undefined'] or len(host.get('results', [])) != 6 or
            any(r.get('exit_code') != 0 for r in host['results']) or len(host.get('mutation_results', [])) != 12 or
            len(host.get('unsafe_configurations', [])) != 27 or any(r.get('exit_code') == 0 for r in host['mutation_results'] + host['unsafe_configurations'])):
        raise ValueError('Incomplete IRQ actual-engine/sanitizer/mutation evidence')
    bind_sources(host.get('source_sha256', {}))
    for result in host['results']:
        bind_sources(result.get('source_sha256', {}))
    emitted = host.get('actual_emitted_rehook_checks', [])
    if {r.get('profile') for r in emitted} != {0, 1, 2} or len(emitted) != 3 or any(r.get('IPRB_values_interpreted') != 65536 or r.get('outside_SCI_bits_preserved') is not True or r.get('priority') != (1 if r['profile'] == 0 else 7) for r in emitted):
        raise ValueError('Missing actual assembled rehook priority/restoration proof')
    linked = remember(test_dir / 'linked-audit-tests.log'); receipt = json.loads(include('evidence/linked-audit-tests.json', test_dir / 'linked-audit-tests.json'))
    count = receipt.get('tests', 0)
    if (receipt.get('runtime_source_commit') != runtime_commit or receipt.get('exit_code') != 0 or receipt.get('skips') != 0 or count < 30 or
            receipt.get('log_sha256') != sha(linked) or linked.decode().rstrip().splitlines()[-1] != 'OK' or
            not re.search(r'^Ran ' + str(count) + r' tests in ', linked.decode(), re.M) or re.search(r'\bskipped\b', linked.decode(), re.I)):
        raise ValueError('Revised linked-test receipt lacks complete current success without skips')
    bind_sources(receipt.get('source_sha256', {})); add('evidence/linked-audit-tests.log', linked)
    for profile, label, report, steps in PROFILES:
        for observed in (config['profiles'][str(profile)], host['matrix_observations'][str(profile)]):
            if observed.get('priority') != 7 or observed.get('steps') != steps:
                raise ValueError('Host IRQ profile differs: ' + label)
        directory = ROOT / 'build' / ('irq-' + label.lower()); admission = audit_irq_progress(directory, args.baseline_ai, profile)
        budget = admission.get('budget', {}); irq = admission.get('startup_IRQ', {}); audio = admission.get('audio_policy', {}); foreground = admission.get('foreground_policy', {})
        if (admission.get('admitted') is not True or admission.get('startup_irq_profile') != profile or admission.get('budget_profile') != report or
                admission.get('data_tuning_profile') != 1 or admission.get('followup_profile') != 2 or
                any(budget.get(k) != v for k, v in [('profile', report), ('credit_limit', 24), ('tick_limit', 12000), ('finite_step_limit', steps), ('tick_hz', 781250)]) or
                any(irq.get(k) != v for k, v in [('priority', 7), ('IPRB_SCI_nibble', '0x70'), ('single_pump_per_IRQ', True), ('GD_entry_mask_retained', True), ('original_game_VBR_and_SCI_nibble_restored_before_forwarding', True)]) or
                any(audio.get(k) != v for k, v in [('refill_quanta', 4), ('service_ticks', 12500), ('changed_from_AI', False)]) or
                foreground.get('finite_step_limit') != steps or foreground.get('AN_second_step_timer_check_optimized_out') is not (profile == 2) or
                admission['retained'].get('baseline_admitted_by_original_gate') is not True):
            raise ValueError('Independent admission differs from exact startup IRQ policy: ' + label)
        config_data = remember(directory / 'build-config').decode().splitlines()
        compiled_config = dict(line.split('=', 1) for line in config_data)
        if len(compiled_config) != len(config_data) or compiled_config != admission['config']:
            raise ValueError('Build configuration differs from the independently admitted values: ' + label)
        item = expected[label]; build = admission['build']; runtime = remember(directory / 'retail-toy-pilot.kui')
        if any(item.get(k) != v for k, v in [('profile', profile), ('budget_profile', report), ('blocks', 24), ('ticks', 12000), ('steps', steps), ('priority', 7), ('audio_quanta', 4), ('audio_ticks', 12500), ('build', build), ('runtime_sha256', sha(runtime)), ('runtime_bytes', len(runtime))]) or admission['runtime']['sha256'] != sha(runtime) or inspect_retail(runtime)['build'] != build or admission['config'].get('STARTUP_IRQ_PROFILE') != str(profile):
            raise ValueError('Runtime/config/source manifest binding differs: ' + label)
        add('profiles/' + label + '-startup-irq-progress.kui', runtime)
        add('evidence/' + label + '-linked-admission.json', (json.dumps(admission, indent=2, sort_keys=True) + '\n').encode())
        metadata['profiles'].append({**item, 'foreground_policy': foreground, 'startup_IRQ': irq})
        collect_build(directory, 'evidence/' + label + '/', remember, add)
    for label, directory, digest, build, name in (
            ('AI', args.baseline_ai, AI_SHA, '0251a9f05601', 'reference/AI-tested-data-steps.kui'),
            ('AF', args.baseline_af, AF_SHA, '025646791d02', 'reference/AF-tested-data-allowance.kui'),
            ('R', args.restore_r, R_SHA, '73e8363c10f7', 'restore/R-private-write-through.kui')):
        runtime = include(name, directory / 'retail-toy-pilot.kui')
        if sha(runtime) != digest or inspect_retail(runtime)['build'] != build:
            raise ValueError('Historical reference is not byte exact: ' + label)
        metadata.setdefault('historical_references', {})[label] = {'build': build, 'sha256': digest, 'file': name, 'recompiled': False}
        if label != 'R':
            parent = ROOT.parent / ('aeaf-tuning-source' if label == 'AI' else 'ad-followup-source')
            tool = 'toy_data_tuning_audit.py' if label == 'AI' else 'toy_ad_followup_audit.py'
            result = subprocess.run([sys.executable, '-B', str(parent / 'tools' / tool), str(directory.resolve()), '--profile', '1' if label == 'AI' else '2'], cwd=parent, text=True, capture_output=True)
            if result.returncode:
                raise ValueError('Frozen original reference gate rejected ' + label + ': ' + result.stderr)
            proof = json.loads(result.stdout)
            if proof.get('admitted') is not True or proof.get('runtime', {}).get('sha256') != digest:
                raise ValueError('Frozen original gate proof differs: ' + label)
            add('evidence/' + label + '-original-gate.json', (json.dumps(proof, indent=2, sort_keys=True) + '\n').encode())
    for label, directory, digest in [('AI', ROOT / 'build/irq-ai-exact', AI_SHA), ('R', ROOT / 'build/irq-r-exact', R_SHA)]:
        if sha(remember(directory / 'retail-toy-pilot.kui')) != digest:
            raise ValueError('Current-source exact control rebuild differs: ' + label)
        include('evidence/exact-' + label + '/retail-toy-pilot.kui', directory / 'retail-toy-pilot.kui')
        collect_build(directory, 'evidence/exact-' + label + '/', remember, add)
        if label == 'AI':
            proof = audit_irq_progress(directory, args.baseline_ai, 0)
            if proof.get('admitted') is not True or proof.get('original_immutable_AI_gate') is not True or proof.get('runtime', {}).get('sha256') != AI_SHA:
                raise ValueError('Exact AI rebuild did not pass its frozen original gate')
            add('evidence/exact-AI/original-gate.json', (json.dumps(proof, indent=2, sort_keys=True) + '\n').encode())
    comparison = include('evidence/AF-AI-AJ-AK-AL-hardware-comparison.json', args.hardware_comparison)
    if b'libfile_' in comparison:
        raise ValueError('Hardware evidence contains internal file identifiers')
    validate_hardware(json.loads(comparison)); metadata['hardware_comparison_sha256'] = sha(comparison)
    include('evidence/current-CRC-glyph-errata.json', args.hardware_errata)
    for path in sorted((ROOT / 'docs/evidence/toy-data-tuning-2026-10-12').glob('*')):
        if path.is_file():
            include('evidence/source-published-hardware/' + path.name, path)
    for path in [ROOT / 'LICENSE', ROOT / 'THIRD_PARTY.md', *sorted((ROOT / 'LICENSES').glob('*'))]:
        if path.is_file():
            include(path.relative_to(ROOT).as_posix(), path)
    snapshot = subprocess.check_output(['git', 'archive', 'HEAD'], cwd=ROOT)
    add('source-snapshot.tar', snapshot); metadata['source_snapshot_sha256'] = sha(snapshot)
    add('build.json', (json.dumps(metadata, indent=2, sort_keys=True) + '\n').encode())
    if any(sha(path.read_bytes()) != digest for path, digest in inputs.items()) or git('status', '--porcelain') or git('rev-parse', 'HEAD') != metadata['source_snapshot_commit']:
        raise ValueError('An input or committed source changed during collection')
    add('SHA256SUMS', ''.join(sha(data) + '  ' + name + '\n' for name, data in sorted(files.items())).encode())
    if args.out.is_symlink() or args.out.suffix.lower() != '.zip':
        raise ValueError('Output must be a ZIP path without a symlink')
    out = args.out.resolve()
    if out.exists() and not out.is_file():
        raise ValueError('Output is not a regular ZIP file')
    if out.is_relative_to(ROOT) and subprocess.run(['git', 'ls-files', '--error-unmatch', '--', out.relative_to(ROOT).as_posix()], cwd=ROOT, capture_output=True).returncode == 0:
        raise ValueError('Output cannot overwrite a tracked repository file')
    write_archive(out, files)
    print(json.dumps({'file': str(out), 'sha256': sha(out.read_bytes()), 'bytes': out.stat().st_size,
        'profiles': [p['label'] for p in metadata['profiles']], 'all_member_checksums_verified': True}, indent=2))


def collect_build(directory, prefix, remember, add):
    collected = set()
    for path in sorted(directory.rglob('*')):
        if path.is_file() and (path.suffix in ('.elf', '.bin', '.map', '.su') or path.name in ('build-config', 'toy_pilot_resident_symbols.h')):
            name = path.relative_to(directory).as_posix(); add(prefix + name, remember(path)); collected.add(name)
    required = {part + suffix for part in ('worker', 'resident-sci', 'stage') for suffix in ('.elf', '.bin', '.map')} | {'entry.elf', 'entry.map', 'build-config'}
    if not required <= collected or not any(name.endswith('.su') for name in collected):
        raise ValueError('Missing emitted ELF/binary/map/stack evidence: ' + str(directory))


if __name__ == '__main__':
    main()
