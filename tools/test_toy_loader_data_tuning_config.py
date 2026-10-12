#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check production tuning admission and profile0 against the frozen old source."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FOOTER = 'DATA tuning configuration suites passed (AI/AJ/AK/AL; legacy R/B/Y/Z/AA..AH preserved)'
SAFE = {
    'KUI_TOY_PILOT_AD_FOLLOWUP_PROFILE': 2,
    'KUI_TOY_PILOT_DATA_SCHEDULE_MODE': 2,
    'KUI_TOY_PILOT_DATA_BUDGET_PROFILE': 4,
    'KUI_TOY_PILOT_PRIVATE_P2': 1,
    'KUI_TOY_PILOT_NATIVE_CACHE': 0,
    'KUI_TOY_PILOT_SHARED_SCI': 0,
    'KUI_TOY_PILOT_ASYNC_CDDA': 0,
    'KUI_TOY_PILOT_SYNTHETIC_SOURCE': 0,
    'KUI_TOY_PILOT_GD_FIXED_STEP': 2,
    'KUI_SCI_DMA_REUSE_TDRE': 0,
    'KUI_TOY_PILOT_DATA_PAYLOAD_MODE': 0,
    'KUI_TOY_PILOT_LOADER_TRACE': 1,
    'KUI_TOY_PILOT_DATA_PROBE': 1,
    'KUI_RETAIL_GD_ASYNC': 1,
    'KUI_TOY_LOADER_SCHEDULE_HOST_TEST': 1,
    'KUI_TOY_PILOT_SCI_TEST': 1,
    'KUI_TOY_PILOT_WORKER_TEST': 1,
}
# Independent reviewed matrix: label, SCHED report profile, blocks, ticks, steps.
PROFILES = {0: ('AF', 6, 24, 12000, 1024), 1: ('AI', 9, 24, 12000, 2048),
            2: ('AJ', 10, 24, 9000, 1024), 3: ('AK', 11, 32, 12000, 2048),
            4: ('AL', 12, 32, 18000, 2048)}
UNSAFE = [
    *[('KUI_TOY_PILOT_AD_FOLLOWUP_PROFILE', p) for p in (0, 1, 3, 4)],
    *[('KUI_TOY_PILOT_DATA_BUDGET_PROFILE', p) for p in range(4)],
    *[('KUI_TOY_PILOT_DATA_SCHEDULE_MODE', p) for p in (0, 1, 3)],
    ('KUI_TOY_PILOT_PRIVATE_P2', 0), ('KUI_TOY_PILOT_NATIVE_CACHE', 1),
    ('KUI_TOY_PILOT_SHARED_SCI', 1), ('KUI_TOY_PILOT_ASYNC_CDDA', 1),
    ('KUI_TOY_PILOT_SYNTHETIC_SOURCE', 1), ('KUI_TOY_PILOT_GD_FIXED_STEP', 1),
    ('KUI_TOY_PILOT_GD_FIXED_STEP', 3), ('KUI_SCI_DMA_REUSE_TDRE', 1),
    ('KUI_TOY_PILOT_DATA_PAYLOAD_MODE', 1), ('KUI_TOY_PILOT_DATA_PAYLOAD_MODE', 2),
    ('KUI_TOY_PILOT_LOADER_TRACE', 0), ('KUI_TOY_PILOT_DATA_PROBE', 0),
]
MAKE_NAMES = {
    'KUI_TOY_PILOT_AD_FOLLOWUP_PROFILE': 'AD_FOLLOWUP_PROFILE',
    'KUI_TOY_PILOT_DATA_SCHEDULE_MODE': 'DATA_SCHEDULE_MODE',
    'KUI_TOY_PILOT_DATA_BUDGET_PROFILE': 'DATA_BUDGET_PROFILE',
    'KUI_TOY_PILOT_PRIVATE_P2': 'PRIVATE_P2',
    'KUI_TOY_PILOT_NATIVE_CACHE': 'NATIVE_CACHE',
    'KUI_TOY_PILOT_SHARED_SCI': 'SHARED_SCI',
    'KUI_TOY_PILOT_ASYNC_CDDA': 'ASYNC_CDDA',
    'KUI_TOY_PILOT_SYNTHETIC_SOURCE': 'SYNTHETIC_SOURCE',
    'KUI_TOY_PILOT_GD_FIXED_STEP': 'GD_FIXED_STEP',
    'KUI_SCI_DMA_REUSE_TDRE': 'SCI_REUSE_TDRE',
    'KUI_TOY_PILOT_DATA_PAYLOAD_MODE': 'DATA_PAYLOAD_MODE',
    'KUI_TOY_PILOT_LOADER_TRACE': 'LOADER_TRACE',
    'KUI_TOY_PILOT_DATA_PROBE': 'DATA_PROBE',
}
HEADERS = ('kui/toy_loader_data_tuning.h', 'kui/toy_loader_ad_followup.h',
           'kui/toy_loader_schedule.h', 'toy_pilot_sci.h')
SOURCES = ('src/loader/toy_pilot_sci.c', 'src/loader/toy_pilot_worker.c',
           'src/loader/toy_loader_schedule_report.c')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build/tuning-tests')
    parser.add_argument('--legacy-root', type=Path, default=ROOT.parent / 'ad-followup-source')
    args = parser.parse_args()
    cc = shlex.split(args.cc)
    legacy = args.legacy_root.resolve()
    if not (legacy / 'Makefile.toy_pilot').is_file():
        raise RuntimeError('Supply the frozen pre-tuning checkout with --legacy-root')
    directory = args.build_dir.resolve(); directory.mkdir(parents=True, exist_ok=True)
    checks = []

    def checked(label, kind, detail=None):
        checks.append({'label': label, 'kind': kind, **(detail or {})})

    with tempfile.TemporaryDirectory(prefix='toy-tuning-config-') as temporary:
        temp = Path(temporary)
        # CPP requires only the generated include's existence. Empty contents
        # avoid importing symbols from a stale target binary into observations.
        (temp / 'toy_pilot_resident_symbols.h').write_text('')
        query = temp / 'query.mk'
        query.write_text('.PHONY: tuning-config-flags\ntuning-config-flags:\n\t@printf "%s\\n" "$(FLAGS)"\n')

        def command(root, definitions, extra):
            return [*cc, '-std=c11', '-I' + str(root / 'include'),
                    '-I' + str(root / 'src/loader'), '-I' + str(temp),
                    *['-D' + k + '=' + str(v) for k, v in definitions.items()], *extra]

        def header(profile, changes=None, expected=True, include=HEADERS[0], constant=False):
            definitions = dict(SAFE, KUI_TOY_PILOT_DATA_TUNING_PROFILE=profile)
            definitions.update(changes or {})
            source = '#include "' + include + '"\n'
            if constant:
                source += 'enum { observed_tuning_profile=KUI_TOY_PILOT_DATA_TUNING_PROFILE };\n'
            result = subprocess.run(command(ROOT, definitions,
                ['-Wall', '-Wextra', '-Werror', '-fsyntax-only', '-x', 'c', '-']),
                input=source, text=True, capture_output=True)
            if (result.returncode == 0) != expected or (not expected and not result.stderr):
                raise RuntimeError('Header admission differs: ' + repr(definitions) + '\n' + result.stderr)
            if not expected and not constant and '#error' not in result.stderr:
                raise RuntimeError('Expected production header guard did not cause refusal:\n' + result.stderr)
            checked(include + ' profile' + str(profile) + ' ' + repr(changes or {}),
                    'header-C-constant-admission' if constant else 'bare-header-admission',
                    {'accepted': expected})

        def make(profile=None, changes=None, expected=True, root=ROOT, suffix='config'):
            definitions = {MAKE_NAMES[k]: v for k, v in SAFE.items() if k in MAKE_NAMES}
            if profile is not None: definitions['DATA_TUNING_PROFILE'] = profile
            definitions.update(changes or {})
            build = temp / suffix
            argv = ['make', '-s', '-f', 'Makefile.toy_pilot', '-f', str(query),
                    'BUILD=' + str(build), 'BUILD_ID=123456789abc',
                    *[k + '=' + str(v) for k, v in definitions.items()],
                    str(build / 'build-config'), 'tuning-config-flags']
            result = subprocess.run(argv, cwd=root, text=True, capture_output=True)
            if (result.returncode == 0) != expected or (not expected and '***' not in result.stderr):
                raise RuntimeError('Make admission differs: ' + repr(definitions) + '\n' + result.stdout + result.stderr)
            checked('Make profile' + str(profile) + ' ' + repr(changes or {}),
                    'make-admission', {'accepted': expected})
            if expected:return (build / 'build-config').read_bytes(), shlex.split(result.stdout)

        def preprocess(source, definitions, root=ROOT, audio_observation=False):
            argv = command(root, definitions, ['-E', '-P'])
            if audio_observation:
                argv += ['-x', 'c', '-']
                source_text = '#include "' + str(root / source) + '"\n' + \
                    'int tuning_quanta_observation=REFILL_QUANTA;\n' + \
                    'int tuning_ticks_observation=REFILL_VISIT_TICKS;\n'
            else:
                argv += [str(root / source)];source_text = None
            result = subprocess.run(argv, input=source_text, text=True, capture_output=True)
            if result.returncode:raise RuntimeError('Production CPP failed: ' + source + '\n' + result.stderr)
            return result.stdout

        for profile, (label, report, blocks, ticks, steps) in PROFILES.items():
            definitions = dict(SAFE, KUI_TOY_PILOT_DATA_TUNING_PROFILE=profile,
                EXPECT_TUNING=profile, EXPECT_REPORT=report, EXPECT_BLOCKS=blocks,
                EXPECT_TICKS=ticks, EXPECT_STEPS=steps, EXPECT_AUDIO_QUANTA=4,
                EXPECT_AUDIO_TICKS=12500)
            result = subprocess.run(command(ROOT, definitions, ['-Wall', '-Wextra', '-Werror',
                '-fsyntax-only', str(ROOT / 'tests/test_toy_loader_data_tuning_config.c')]),
                text=True, capture_output=True)
            if result.returncode:raise RuntimeError(label + ' cap/label/ABI mismatch:\n' + result.stderr)
            checked(label, 'public-caps-and-ABI', {'report_profile': report,
                    'blocks': blocks, 'ticks': ticks, 'steps': steps})
            worker = preprocess('src/loader/toy_pilot_worker.c', definitions, audio_observation=True)
            observed = dict(re.findall(r'int\s+tuning_(quanta|ticks)_observation\s*=\s*(\d+)u;', worker))
            if observed != {'quanta': '4', 'ticks': '12500'}:
                raise RuntimeError(label + ' changed retained AF audio allowances: ' + repr(observed))
            checked(label, 'production-worker-audio-caps')
            for include in HEADERS: header(profile, include=include)
            config, flags = make(profile)
            fields = config.decode().splitlines()
            lines = [x for x in fields if x.startswith('DATA_TUNING_PROFILE=')]
            optflags = [x for x in flags if x.startswith('-DKUI_TOY_PILOT_DATA_TUNING_PROFILE=')]
            if lines != (['DATA_TUNING_PROFILE=' + str(profile)] if profile else []) or \
                    optflags != (['-DKUI_TOY_PILOT_DATA_TUNING_PROFILE=' + str(profile)] if profile else []):
                raise RuntimeError(label + ' opt-in profile not bound exactly once')
            if fields.count('AD_FOLLOWUP_PROFILE=2') != 1 or fields.count('DATA_BUDGET_PROFILE=4') != 1:
                raise RuntimeError(label + ' did not retain AF/budget4 parent binding')
            checked(label, 'exact-Make-profile-binding')

        defaults = {'EXPECT_TUNING': 0, 'EXPECT_REPORT': 0, 'EXPECT_BLOCKS': 4,
                    'EXPECT_TICKS': 1500, 'EXPECT_STEPS': 1024,
                    'EXPECT_AUDIO_QUANTA': 4, 'EXPECT_AUDIO_TICKS': 12500}
        result = subprocess.run(command(ROOT, defaults, ['-Wall', '-Wextra', '-Werror',
            '-fsyntax-only', str(ROOT / 'tests/test_toy_loader_data_tuning_config.c')]),
            text=True, capture_output=True)
        if result.returncode:raise RuntimeError('Omitted header defaults changed:\n' + result.stderr)
        checked('all controls omitted', 'public-header-defaults')

        for profile in range(1, 5):
            for name, value in UNSAFE:
                for include in HEADERS:header(profile, {name: value}, False, include)
                make(profile, {MAKE_NAMES[name]: value}, False)
        for invalid in (-1, 5, 255):
            for include in HEADERS:header(invalid, expected=False, include=include)
        for alias in ('AI', 'AJ', 'AK', 'AL', 'unknown'):
            # Bare CPP treats an undefined identifier as0 inside#if. This
            # test uses a real C integer constant and is labelled accordingly;
            # Make's exact whitelist independently refuses every such alias.
            for include in HEADERS:header(alias, expected=False, include=include, constant=True)
        for invalid in (-1, 5, 255, '', '1 2', '01', '1.0', '2.5', 'AI', 'AJ',
                        'AK', 'AL', 'AF', '1u', '1+0', 'unknown'):
            make(invalid, expected=False)
        for fraction in ('1.0', '2.5'):
            for include in HEADERS:header(fraction, expected=False, include=include, constant=True)

        # Check every old admitted family rather than only the AF parent.
        legacy_modes = [('R', 0, 0, 0), ('B', 3, 0, 0), ('Y', 1, 0, 0), ('Z', 2, 0, 0),
            *[(label, 2, p, 0) for p, label in enumerate(('AA', 'AB', 'AC', 'AD'), 1)],
            *[(label, 2, 4, p) for p, label in enumerate(('AE', 'AF', 'AG', 'AH'), 1)]]
        cpp_hashes = {}
        for label, schedule, budget, followup in legacy_modes:
            definitions = dict(SAFE, KUI_TOY_PILOT_DATA_SCHEDULE_MODE=schedule,
                KUI_TOY_PILOT_DATA_BUDGET_PROFILE=budget,
                KUI_TOY_PILOT_AD_FOLLOWUP_PROFILE=followup)
            if label == 'R':definitions.update(KUI_TOY_PILOT_LOADER_TRACE=0, KUI_TOY_PILOT_DATA_PROBE=0)
            changes = {MAKE_NAMES[k]: v for k, v in definitions.items() if k in MAKE_NAMES}
            omitted_config, omitted_flags = make(None, changes, suffix='legacy-' + label)
            explicit_config, explicit_flags = make(0, changes, suffix='legacy-' + label)
            old_config, old_flags = make(None, changes, root=legacy, suffix='legacy-' + label)
            if omitted_config != explicit_config or omitted_config != old_config or \
                    omitted_flags != explicit_flags or omitted_flags != old_flags:
                raise RuntimeError('Tuning0 changed frozen legacy Make output: ' + label)
            if any(x.startswith('DATA_TUNING_PROFILE=') for x in omitted_config.decode().splitlines()) or \
                    any(x.startswith('-DKUI_TOY_PILOT_DATA_TUNING_PROFILE=') for x in omitted_flags):
                raise RuntimeError('Disabled tuning flag/config leaked into ' + label)
            checked(label, 'frozen-legacy-Make-config-and-flags')
            sources = SOURCES if schedule == 2 else (SOURCES[1:] if schedule else SOURCES[1:2])
            for source in sources:
                omitted = preprocess(source, definitions)
                explicit = preprocess(source, dict(definitions, KUI_TOY_PILOT_DATA_TUNING_PROFILE=0))
                old = preprocess(source, definitions, root=legacy)
                normalized = re.sub(r'\s+', ' ', omitted).strip()
                if omitted != explicit or re.sub(r'\s+', ' ', old).strip() != normalized:
                    raise RuntimeError('Tuning0 changed frozen production CPP: ' + label + ' ' + source)
                cpp_hashes[label + ':' + source] = hashlib.sha256(normalized.encode()).hexdigest()
                checked(label + ' ' + source, 'frozen-legacy-production-CPP')

    def commit(path):
        return subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=path, text=True).strip()
    evidence = {'schema': 1, 'profiles': {str(k): {'label': v[0], 'report_profile': v[1],
        'blocks': v[2], 'ticks': v[3], 'steps': v[4], 'audio_quanta': 4, 'audio_ticks': 12500}
        for k, v in PROFILES.items()}, 'checks': checks, 'cpp_sha256': cpp_hashes,
        'runtime_source_commit': commit(ROOT), 'frozen_legacy_source_commit': commit(legacy),
        'scope': 'Actual Make/header/cap/config and full eligible production CPP paths; linked-runtime equivalence and hardware behavior have separate admission gates.',
        'alias_admission_note': 'Make exact integer whitelist rejects aliases. Unknown identifier rejection in headers is tested as a C integer constant; bare#if treats undefined identifiers as0.'}
    (directory / 'data-tuning-config-tests.json').write_text(json.dumps(evidence, indent=2) + '\n')
    print(f'Checked {len(checks)} production configuration admission/refusal and legacy preservation cases')
    print(FOOTER)


if __name__ == '__main__':main()
