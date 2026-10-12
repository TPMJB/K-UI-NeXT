#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise real Make/header admission and every AD follow-up allowance."""
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
SAFE = {
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
# Stated from the reviewed test matrix, rather than copied from production
# macros. Tuple: label, SCHED budget label, DATA blocks/ticks, audio quanta/ticks.
PROFILES = {
    0: ('AD', 4, 12, 6000, 4, 12500),
    1: ('AE', 5, 24, 6000, 4, 12500),
    2: ('AF', 6, 24, 12000, 4, 12500),
    3: ('AG', 7, 12, 6000, 2, 12500),
    4: ('AH', 8, 12, 6000, 4, 6250),
}
UNSAFE = [
    *[('KUI_TOY_PILOT_DATA_BUDGET_PROFILE', p) for p in range(4)],
    *[('KUI_TOY_PILOT_DATA_SCHEDULE_MODE', p) for p in (0, 1, 3)],
    ('KUI_TOY_PILOT_PRIVATE_P2', 0),
    ('KUI_TOY_PILOT_NATIVE_CACHE', 1),
    ('KUI_TOY_PILOT_SHARED_SCI', 1),
    ('KUI_TOY_PILOT_ASYNC_CDDA', 1),
    ('KUI_TOY_PILOT_SYNTHETIC_SOURCE', 1),
    ('KUI_TOY_PILOT_GD_FIXED_STEP', 1),
    ('KUI_TOY_PILOT_GD_FIXED_STEP', 3),
    ('KUI_SCI_DMA_REUSE_TDRE', 1),
    ('KUI_TOY_PILOT_DATA_PAYLOAD_MODE', 1),
    ('KUI_TOY_PILOT_DATA_PAYLOAD_MODE', 2),
    ('KUI_TOY_PILOT_LOADER_TRACE', 0),
    ('KUI_TOY_PILOT_DATA_PROBE', 0),
]
MAKE_NAMES = {
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
SOURCES = ('src/loader/toy_pilot_sci.c', 'src/loader/toy_pilot_worker.c',
           'src/loader/toy_loader_schedule_report.c')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    parser.add_argument('--build-dir', type=Path,
                        default=ROOT / 'build/ad-followup-config-host')
    parser.add_argument('--legacy-root', type=Path,
                        help='Optional admitted pre-follow-up checkout for profile0 CPP comparison')
    args = parser.parse_args()
    cc = shlex.split(args.cc)
    directory = args.build_dir.resolve()
    directory.mkdir(parents=True, exist_ok=True)
    checks = []

    def checked(label, kind, detail=None):
        checks.append({'label': label, 'kind': kind, **(detail or {})})

    with tempfile.TemporaryDirectory(prefix='toy-ad-config-') as temporary:
        temp = Path(temporary)
        # Preprocessing needs the build-generated include's existence, but no
        # symbol values from it. It is deliberately empty so a worker macro
        # observation cannot accidentally depend on a stale linked build.
        (temp / 'toy_pilot_resident_symbols.h').write_text('')
        query = temp / 'query.mk'
        query.write_text('.PHONY: ad-config-flags\nad-config-flags:\n\t@printf "%s\\n" "$(FLAGS)"\n')

        def command(root, definitions, extra):
            return [*cc, '-std=c11', '-I' + str(root / 'include'),
                    '-I' + str(root / 'src/loader'), '-I' + str(temp),
                    *['-D' + k + '=' + str(v) for k, v in definitions.items()], *extra]

        def header(profile, changes=None, expected=True, include='kui/toy_loader_schedule.h'):
            definitions = dict(SAFE, KUI_TOY_PILOT_AD_FOLLOWUP_PROFILE=profile)
            definitions.update(changes or {})
            result = subprocess.run(command(ROOT, definitions, ['-Wall', '-Wextra', '-Werror',
                '-fsyntax-only', '-x', 'c', '-']), input=f'#include "{include}"\n',
                text=True, capture_output=True)
            if (result.returncode == 0) != expected or (not expected and '#error' not in result.stderr):
                raise RuntimeError('Header admission differs: ' + repr(definitions) +
                                   '\n' + result.stdout + result.stderr)
            checked(f'{include} profile{profile} {changes or {}}', 'header-admission',
                    {'accepted': expected})

        def make(profile=None, changes=None, expected=True, root=ROOT, suffix='config'):
            definitions = {MAKE_NAMES[k]: v for k, v in SAFE.items() if k in MAKE_NAMES}
            if profile is not None:
                definitions['AD_FOLLOWUP_PROFILE'] = profile
            definitions.update(changes or {})
            build = temp / suffix
            argv = ['make', '-s', '-f', 'Makefile.toy_pilot', '-f', str(query),
                    'BUILD=' + str(build), 'BUILD_ID=123456789abc',
                    *[k + '=' + str(v) for k, v in definitions.items()],
                    str(build / 'build-config'), 'ad-config-flags']
            result = subprocess.run(argv, cwd=root, text=True, capture_output=True)
            if (result.returncode == 0) != expected or (not expected and '***' not in result.stderr):
                raise RuntimeError('Make admission differs: ' + repr(definitions) +
                                   '\n' + result.stdout + result.stderr)
            checked(f'Make profile{profile} {changes or {}}', 'make-admission',
                    {'accepted': expected})
            if expected:
                return (build / 'build-config').read_bytes(), shlex.split(result.stdout)

        def preprocess(source, definitions, root=ROOT, observation=False):
            if observation:
                result = subprocess.run(command(root, definitions, ['-E', '-P', '-x', 'c', '-']),
                    input='#include "' + str(root / source) + '"\n' +
                          'int ad_quanta_observation = REFILL_QUANTA;\n' +
                          'int ad_ticks_observation = REFILL_VISIT_TICKS;\n',
                    text=True, capture_output=True)
                if result.returncode:
                    raise RuntimeError('Production worker observation CPP failed:\n' + result.stderr)
                return result.stdout
            extra = ['-E', '-P', str(root / source)]
            result = subprocess.run(command(root, definitions, extra), text=True, capture_output=True)
            if result.returncode:
                raise RuntimeError('Production CPP failed: ' + source + '\n' + result.stderr)
            return result.stdout

        for profile, (label, report, blocks, ticks, quanta, audio_ticks) in PROFILES.items():
            definitions = dict(SAFE, KUI_TOY_PILOT_AD_FOLLOWUP_PROFILE=profile,
                EXPECT_FOLLOWUP=profile, EXPECT_REPORT_PROFILE=report,
                EXPECT_BLOCKS=blocks, EXPECT_TICKS=ticks,
                EXPECT_AUDIO_QUANTA=quanta, EXPECT_AUDIO_TICKS=audio_ticks)
            result = subprocess.run(command(ROOT, definitions, ['-Wall', '-Wextra', '-Werror',
                '-fsyntax-only', str(ROOT / 'tests/test_toy_loader_ad_followup_config.c')]),
                text=True, capture_output=True)
            if result.returncode:
                raise RuntimeError(label + ' cap/label mismatch:\n' + result.stderr)
            checked(label, 'public-caps', {'report_profile': report, 'DATA_blocks': blocks,
                    'DATA_ticks': ticks})
            worker = preprocess('src/loader/toy_pilot_worker.c', definitions, observation=True)
            found = dict(re.findall(r'int\s+ad_(quanta|ticks)_observation\s*=\s*(\d+)u;', worker))
            if found != {'quanta': str(quanta), 'ticks': str(audio_ticks)}:
                raise RuntimeError(label + ' worker audio cap mismatch: ' + repr(found))
            checked(label, 'production-worker-caps', {'audio_quanta': quanta, 'audio_ticks': audio_ticks})
            config, flags = make(profile)
            config_fields = config.decode().splitlines()
            expected_line = 'AD_FOLLOWUP_PROFILE=' + str(profile)
            expected_flag = '-DKUI_TOY_PILOT_AD_FOLLOWUP_PROFILE=' + str(profile)
            bound_lines = [x for x in config_fields if x.startswith('AD_FOLLOWUP_PROFILE=')]
            bound_flags = [x for x in flags if x.startswith('-DKUI_TOY_PILOT_AD_FOLLOWUP_PROFILE=')]
            if bound_lines != ([expected_line] if profile else []) or bound_flags != ([expected_flag] if profile else []):
                raise RuntimeError(label + ' opt-in profile was not bound exactly')
            if config_fields.count('DATA_BUDGET_PROFILE=4') != 1:
                raise RuntimeError(label + ' did not retain admitted AD budget profile4')
            checked(label, 'config-and-flag-binding')

        # Include defaults independently of Make's always-present schedule
        # definitions, so central-header defaults are also real C constants.
        defaults = {'EXPECT_FOLLOWUP': 0, 'EXPECT_REPORT_PROFILE': 0,
                    'EXPECT_BLOCKS': 4, 'EXPECT_TICKS': 1500,
                    'EXPECT_AUDIO_QUANTA': 4, 'EXPECT_AUDIO_TICKS': 12500}
        result = subprocess.run(command(ROOT, defaults, ['-Wall', '-Wextra', '-Werror',
            '-fsyntax-only', str(ROOT / 'tests/test_toy_loader_ad_followup_config.c')]),
            text=True, capture_output=True)
        if result.returncode:
            raise RuntimeError('Omitted header controls changed retained defaults:\n' + result.stderr)
        checked('omitted controls', 'public-header-defaults')

        # Independent include entry points must all reject each unsafe factor.
        for profile in range(1, 5):
            for name, value in UNSAFE:
                for include in ('kui/toy_loader_ad_followup.h',
                                'kui/toy_loader_schedule.h', 'toy_pilot_sci.h'):
                    header(profile, {name: value}, False, include)
                make(profile, {MAKE_NAMES[name]: value}, False)
        for invalid in (-1, 5, 255):
            for include in ('kui/toy_loader_ad_followup.h',
                            'kui/toy_loader_schedule.h', 'toy_pilot_sci.h'):
                header(invalid, expected=False, include=include)
        for invalid in (-1, 5, 255, '', '1 2', '01', '2.0', '1x'):
            make(invalid, expected=False)

        omitted_config, omitted_flags = make(None, suffix='legacy-zero')
        explicit_config, explicit_flags = make(0, suffix='legacy-zero')
        if omitted_config != explicit_config or omitted_flags != explicit_flags:
            raise RuntimeError('Explicit follow-up0 changed default build configuration/flags')
        checked('omitted equals explicit0', 'legacy-Make-config-and-flags')

        # Profile0 leaves all earlier reader/audio code paths identical, both
        # with the macro omitted and explicitly0. This compares emitted CPP
        # source, not the mere presence of an #if in a header.
        legacy_modes = [('R', 0, 0), ('B', 3, 0), ('Y', 1, 0), ('Z', 2, 0),
                        *[('budget' + str(p), 2, p) for p in range(1, 5)]]
        cpp_hashes = {}
        for label, schedule, budget in legacy_modes:
            definitions = dict(SAFE, KUI_TOY_PILOT_DATA_SCHEDULE_MODE=schedule,
                               KUI_TOY_PILOT_DATA_BUDGET_PROFILE=budget)
            if label == 'R':
                definitions.update(KUI_TOY_PILOT_LOADER_TRACE=0, KUI_TOY_PILOT_DATA_PROBE=0)
            make_changes = {MAKE_NAMES[k]: v for k, v in definitions.items() if k in MAKE_NAMES}
            zero_config, zero_flags = make(None, make_changes, suffix='legacy-' + label)
            explicit_config, explicit_flags = make(0, make_changes, suffix='legacy-' + label)
            if zero_config != explicit_config or zero_flags != explicit_flags:
                raise RuntimeError('Follow-up0 changed legacy configuration/flags: ' + label)
            if any(x.startswith('AD_FOLLOWUP_PROFILE=') for x in zero_config.decode().splitlines()) or any(
                    x.startswith('-DKUI_TOY_PILOT_AD_FOLLOWUP_PROFILE=') for x in zero_flags):
                raise RuntimeError('Disabled follow-up emitted a config/compile flag: ' + label)
            if args.legacy_root:
                old_config, old_flags = make(None, make_changes, root=args.legacy_root.resolve(),
                                            suffix='legacy-' + label)
                if old_config != zero_config or old_flags != zero_flags:
                    raise RuntimeError('Admitted legacy Make configuration/flags changed: ' + label)
            checked(label, 'legacy-Make-config-and-flags')
            sources = SOURCES if schedule == 2 else (SOURCES[1:] if schedule else SOURCES[1:2])
            for source in sources:
                omitted = preprocess(source, definitions)
                explicit = preprocess(source, dict(definitions, KUI_TOY_PILOT_AD_FOLLOWUP_PROFILE=0))
                if omitted != explicit:
                    raise RuntimeError('Follow-up0 emitted different production CPP: ' + label + ' ' + source)
                # Whitespace is irrelevant to C output, while token changes
                # matter. Normalize it only for cross-checkout comparison.
                normalized = re.sub(r'\s+', ' ', omitted).strip()
                if args.legacy_root:
                    old = preprocess(source, definitions, args.legacy_root.resolve())
                    if re.sub(r'\s+', ' ', old).strip() != normalized:
                        raise RuntimeError('Admitted legacy source changed with follow-up0: ' + label + ' ' + source)
                cpp_hashes[label + ':' + source] = hashlib.sha256(normalized.encode()).hexdigest()
                checked(label + ' ' + source, 'legacy-production-CPP')

        if args.legacy_root:
            old_config, old_flags = make(None, root=args.legacy_root.resolve(), suffix='legacy-zero')
            if old_config != omitted_config or old_flags != omitted_flags:
                raise RuntimeError('Profile0 changed admitted legacy Make configuration/flags')
            checked('admitted checkout equals follow-up0', 'legacy-Make-comparison')

    result = {'schema': 1, 'profiles': {str(k): {'label': v[0], 'report_profile': v[1],
              'DATA_blocks': v[2], 'DATA_ticks': v[3], 'audio_quanta': v[4], 'audio_ticks': v[5]}
              for k, v in PROFILES.items()}, 'checks': checks, 'cpp_sha256': cpp_hashes,
              'admitted_legacy_checkout_compared': bool(args.legacy_root),
              'scope': 'Production Make/header/cap and preprocessing admission; runtime hardware behavior is tested separately.'}
    (directory / 'ad-followup-config-tests.json').write_text(json.dumps(result, indent=2) + '\n')
    print(f'AD follow-up configuration suites passed ({len(checks)} production admission/refusal checks; profile0 CPP/source paths unchanged)')


if __name__ == '__main__':
    main()
