#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise production AM/AN admission, C/assembly binding and disabled exactness."""
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
FOOTER = 'Startup IRQ configuration suites passed (AM/AN; legacy R/B/Y/Z/AA..AL preserved)'
SAFE = {
    'KUI_TOY_PILOT_DATA_TUNING_PROFILE': 1,
    'KUI_TOY_PILOT_AD_FOLLOWUP_PROFILE': 2,
    'KUI_TOY_PILOT_DATA_BUDGET_PROFILE': 4,
    'KUI_TOY_PILOT_DATA_SCHEDULE_MODE': 2,
    'KUI_TOY_PILOT_PRIVATE_P2': 1,
    'KUI_TOY_PILOT_NATIVE_CACHE': 0,
    'KUI_TOY_PILOT_SHARED_SCI': 0,
    'KUI_TOY_PILOT_ASYNC_CDDA': 0,
    'KUI_TOY_PILOT_SYNTHETIC_SOURCE': 0,
    'KUI_TOY_PILOT_LOADER_TRACE': 1,
    'KUI_TOY_PILOT_DATA_PROBE': 1,
    'KUI_TOY_PILOT_DATA_PAYLOAD_MODE': 0,
    'KUI_TOY_PILOT_GD_FIXED_STEP': 2,
    'KUI_SCI_DMA_REUSE_TDRE': 0,
    'KUI_RETAIL_GD_ASYNC': 1,
    'KUI_TOY_LOADER_SCHEDULE_HOST_TEST': 1,
    'KUI_TOY_PILOT_SCI_TEST': 1,
    'KUI_TOY_PILOT_WORKER_TEST': 1,
}
MAKE_NAMES = {
    'KUI_TOY_PILOT_DATA_TUNING_PROFILE': 'DATA_TUNING_PROFILE',
    'KUI_TOY_PILOT_AD_FOLLOWUP_PROFILE': 'AD_FOLLOWUP_PROFILE',
    'KUI_TOY_PILOT_DATA_BUDGET_PROFILE': 'DATA_BUDGET_PROFILE',
    'KUI_TOY_PILOT_DATA_SCHEDULE_MODE': 'DATA_SCHEDULE_MODE',
    'KUI_TOY_PILOT_PRIVATE_P2': 'PRIVATE_P2',
    'KUI_TOY_PILOT_NATIVE_CACHE': 'NATIVE_CACHE',
    'KUI_TOY_PILOT_SHARED_SCI': 'SHARED_SCI',
    'KUI_TOY_PILOT_ASYNC_CDDA': 'ASYNC_CDDA',
    'KUI_TOY_PILOT_SYNTHETIC_SOURCE': 'SYNTHETIC_SOURCE',
    'KUI_TOY_PILOT_LOADER_TRACE': 'LOADER_TRACE',
    'KUI_TOY_PILOT_DATA_PROBE': 'DATA_PROBE',
    'KUI_TOY_PILOT_DATA_PAYLOAD_MODE': 'DATA_PAYLOAD_MODE',
    'KUI_TOY_PILOT_GD_FIXED_STEP': 'GD_FIXED_STEP',
    'KUI_SCI_DMA_REUSE_TDRE': 'SCI_REUSE_TDRE',
}
HEADERS = ('kui/toy_loader_irq_progress.h', 'kui/toy_loader_ad_followup.h',
           'kui/toy_loader_schedule.h', 'toy_pilot_sci.h')
ASM_SOURCE = 'src/loader/toy_pilot_sci_irq.S'
SOURCES = ('src/loader/toy_pilot_sci.c', 'src/loader/toy_pilot_worker.c',
           'src/loader/toy_loader_schedule_report.c')
PROFILES = {0: ('AI', 9, 1, 0x10, 24, 12000, 2048),
            1: ('AM', 13, 7, 0x70, 24, 12000, 2048),
            2: ('AN', 14, 7, 0x70, 24, 12000, 1)}
UNSAFE = [
    *[('KUI_TOY_PILOT_DATA_TUNING_PROFILE', p) for p in (0, 2, 3, 4)],
    *[('KUI_TOY_PILOT_AD_FOLLOWUP_PROFILE', p) for p in (0, 1, 3, 4)],
    *[('KUI_TOY_PILOT_DATA_BUDGET_PROFILE', p) for p in range(4)],
    *[('KUI_TOY_PILOT_DATA_SCHEDULE_MODE', p) for p in (0, 1, 3)],
    ('KUI_TOY_PILOT_PRIVATE_P2', 0), ('KUI_TOY_PILOT_NATIVE_CACHE', 1),
    ('KUI_TOY_PILOT_SHARED_SCI', 1), ('KUI_TOY_PILOT_ASYNC_CDDA', 1),
    ('KUI_TOY_PILOT_SYNTHETIC_SOURCE', 1), ('KUI_TOY_PILOT_LOADER_TRACE', 0),
    ('KUI_TOY_PILOT_DATA_PROBE', 0), ('KUI_TOY_PILOT_DATA_PAYLOAD_MODE', 1),
    ('KUI_TOY_PILOT_DATA_PAYLOAD_MODE', 2), ('KUI_TOY_PILOT_GD_FIXED_STEP', 1),
    ('KUI_TOY_PILOT_GD_FIXED_STEP', 3), ('KUI_SCI_DMA_REUSE_TDRE', 1),
]
# Independently reviewed prior matrix, not read back from runtime selectors.
# label,schedule,budget,followup,tuning,report,blocks,ticks,steps,audio-quanta,ticks
OLD = [('R', 0, 0, 0, 0, 0, 4, 1500, 1024, 4, 12500),
       ('B', 3, 0, 0, 0, 0, 4, 1500, 1024, 4, 12500),
       ('Y', 1, 0, 0, 0, 0, 4, 1500, 1024, 4, 12500),
       ('Z', 2, 0, 0, 0, 0, 4, 1500, 1024, 4, 12500),
       ('AA', 2, 1, 0, 0, 1, 4, 1500, 1024, 4, 12500),
       ('AB', 2, 2, 0, 0, 2, 12, 1500, 1024, 4, 12500),
       ('AC', 2, 3, 0, 0, 3, 4, 6000, 1024, 4, 12500),
       ('AD', 2, 4, 0, 0, 4, 12, 6000, 1024, 4, 12500),
       ('AE', 2, 4, 1, 0, 5, 24, 6000, 1024, 4, 12500),
       ('AF', 2, 4, 2, 0, 6, 24, 12000, 1024, 4, 12500),
       ('AG', 2, 4, 3, 0, 7, 12, 6000, 1024, 2, 12500),
       ('AH', 2, 4, 4, 0, 8, 12, 6000, 1024, 4, 6250),
       ('AI', 2, 4, 2, 1, 9, 24, 12000, 2048, 4, 12500),
       ('AJ', 2, 4, 2, 2, 10, 24, 9000, 1024, 4, 12500),
       ('AK', 2, 4, 2, 3, 11, 32, 12000, 2048, 4, 12500),
       ('AL', 2, 4, 2, 4, 12, 32, 18000, 2048, 4, 12500)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build/irq-tests')
    parser.add_argument('--legacy-root', type=Path, default=ROOT.parent / 'aeaf-tuning-source')
    args = parser.parse_args()
    cc = shlex.split(args.cc); legacy = args.legacy_root.resolve()
    if not (legacy / 'Makefile.toy_pilot').is_file():
        raise RuntimeError('Supply frozen pre-IRQ tuning source with --legacy-root')
    directory = args.build_dir.resolve(); directory.mkdir(parents=True, exist_ok=True)
    checks = []; hashes = {}

    def checked(label, kind, **detail):
        checks.append(dict(label=label, kind=kind, **detail))

    with tempfile.TemporaryDirectory(prefix='toy-irq-config-') as temporary:
        temp = Path(temporary)
        (temp / 'toy_pilot_resident_symbols.h').write_text('')
        query = temp / 'query.mk'
        query.write_text('.PHONY: irq-config-flags\nirq-config-flags:\n\t@printf "%s\\n" "$(FLAGS)"\n')

        def command(root, defs, extra):
            return [*cc, '-I' + str(root / 'include'), '-I' + str(root / 'src/loader'),
                    '-I' + str(temp), *['-D' + k + '=' + str(v) for k, v in defs.items()], *extra]

        def run(argv, label, expected=True, text=None, guard=False):
            result = subprocess.run(argv, input=text, text=True, capture_output=True)
            if (result.returncode == 0) != expected or (not expected and not result.stderr):
                raise RuntimeError(label + '\n' + result.stdout + result.stderr)
            if guard and not expected and '#error' not in result.stderr:
                raise RuntimeError(label + ' refusal did not originate in production guard:\n' + result.stderr)
            return result

        def header(profile, changes=None, expected=True, include=HEADERS[0], constant=False, asm=False):
            defs = dict(SAFE, KUI_TOY_PILOT_STARTUP_IRQ_PROFILE=profile); defs.update(changes or {})
            text = '#include "' + include + '"\n'
            if constant: text += 'enum { selected_irq_profile=KUI_TOY_PILOT_STARTUP_IRQ_PROFILE };\n'
            extra = ['-E', '-P', '-x', 'assembler-with-cpp', '-'] if asm else \
                    ['-std=c11', '-Wall', '-Wextra', '-Werror', '-fsyntax-only', '-x', 'c', '-']
            label = include + ' profile' + str(profile) + ' ' + repr(changes or {})
            run(command(ROOT, defs, extra), label, expected, text,
                guard=not constant and profile not in ('1.0', '2.5'))
            checked(label, 'assembler-header-admission' if asm else
                    ('C-integer-selector-admission' if constant else 'bare-C-header-admission'), accepted=expected)

        def make(profile=None, changes=None, expected=True, root=ROOT, suffix='config'):
            defs = {MAKE_NAMES[k]: v for k, v in SAFE.items() if k in MAKE_NAMES}
            if profile is not None: defs['STARTUP_IRQ_PROFILE'] = profile
            defs.update(changes or {}); build = temp / suffix
            argv = ['make', '-s', '-f', 'Makefile.toy_pilot', '-f', str(query),
                    'BUILD=' + str(build), 'BUILD_ID=123456789abc',
                    *[k + '=' + str(v) for k, v in defs.items()],
                    str(build / 'build-config'), 'irq-config-flags']
            result = subprocess.run(argv, cwd=root, text=True, capture_output=True)
            if (result.returncode == 0) != expected or (not expected and '***' not in result.stderr):
                raise RuntimeError('Make admission differs: ' + repr(defs) + '\n' + result.stdout + result.stderr)
            checked('Make profile' + str(profile) + ' ' + repr(changes or {}), 'make-admission', accepted=expected)
            if expected:return (build / 'build-config').read_bytes(), shlex.split(result.stdout)

        def preprocess(source, defs, root=ROOT):
            result = run(command(root, defs, ['-E', '-P', '-x',
                         'assembler-with-cpp' if source.endswith('.S') else 'c', str(root / source)]), source)
            return result.stdout

        def cap_case(label, defs, report, priority, nibble, blocks, ticks, steps, quanta=4, audio_ticks=12500):
            defs = dict(defs, EXPECT_IRQ=defs.get('KUI_TOY_PILOT_STARTUP_IRQ_PROFILE', 0),
                        EXPECT_REPORT=report, EXPECT_PRIORITY=priority, EXPECT_NIBBLE=nibble,
                        EXPECT_BLOCKS=blocks, EXPECT_TICKS=ticks, EXPECT_STEPS=steps,
                        EXPECT_AUDIO_QUANTA=quanta, EXPECT_AUDIO_TICKS=audio_ticks)
            run(command(ROOT, defs, ['-std=c11', '-Wall', '-Wextra', '-Werror', '-fsyntax-only',
                         str(ROOT / 'tests/test_toy_loader_irq_progress_config.c')]), label)
            checked(label, 'public-C-caps-and-ABI', report_profile=report, priority=priority,
                    nibble=nibble, blocks=blocks, ticks=ticks, steps=steps)

        for profile, (label, report, priority, nibble, blocks, ticks, steps) in PROFILES.items():
            defs = dict(SAFE, KUI_TOY_PILOT_STARTUP_IRQ_PROFILE=profile)
            cap_case(label, defs, report, priority, nibble, blocks, ticks, steps)
            for include in HEADERS:header(profile, include=include)
            header(profile, asm=True)
            # Only the two explicitly assembly-consumed values are emitted;
            # C-only types/report aliases never reach the actual .S body.
            asm_probe = '#include "kui/toy_loader_irq_progress.h"\n' + \
                '.long KUI_TOY_STARTUP_SCI_NIBBLE\n.long KUI_TOY_STARTUP_SCI_PRIORITY\n'
            result = run(command(ROOT, defs, ['-E', '-P', '-x', 'assembler-with-cpp', '-']), label, text=asm_probe)
            if result.stdout.split() != ['.long', hex(nibble), '.long', str(priority)]:
                raise RuntimeError(label + ' assembler values/suffixes differ:\n' + result.stdout)
            assembly = preprocess(ASM_SOURCE, defs)
            if re.findall(r'xor\s+#(0x[0-9a-f]+),r0', assembly) != [hex(nibble)] or \
                    'KUI_TOY_STARTUP' in assembly:
                raise RuntimeError(label + ' actual rehook assembly does not consume admitted priority')
            checked(label, 'actual-assembly-rehook-binding')
            engine = preprocess('src/loader/toy_pilot_sci.c', defs)
            hook = re.search(r'static void hook\(void\)\s*\{(.*?)\n\}', engine, re.S)
            written_levels = re.findall(r'\|\s*(0x[0-9a-f]+)[uU]', hook.group(1)) if hook else []
            if len(written_levels) != 1 or int(written_levels[0], 16) != nibble:
                raise RuntimeError(label + ' actual C hook does not install the admitted SCI field')
            checked(label, 'actual-C-hook-priority-binding')
            # Observe actual worker constants, rather than just a header alias.
            worker_text = '#include "' + str(ROOT / 'src/loader/toy_pilot_worker.c') + '"\n' + \
                'int observed_quanta=REFILL_QUANTA;\nint observed_audio_ticks=REFILL_VISIT_TICKS;\n'
            worker = run(command(ROOT, defs, ['-E', '-P', '-x', 'c', '-']), label, text=worker_text).stdout
            if dict(re.findall(r'int\s+observed_(quanta|audio_ticks)\s*=\s*(\d+)u;', worker)) != \
                    {'quanta': '4', 'audio_ticks': '12500'}:
                raise RuntimeError(label + ' changed the retained AF audio allowances')
            checked(label, 'production-worker-audio-admission')
            config, flags = make(profile); fields = config.decode().splitlines()
            expect = ['STARTUP_IRQ_PROFILE=' + str(profile)] if profile else []
            if [x for x in fields if x.startswith('STARTUP_IRQ_PROFILE=')] != expect or \
                    [x for x in flags if x.startswith('-DKUI_TOY_PILOT_STARTUP_IRQ_PROFILE=')] != \
                    (['-DKUI_TOY_PILOT_STARTUP_IRQ_PROFILE=' + str(profile)] if profile else []):
                raise RuntimeError(label + ' IRQ opt-in is not bound exactly once')
            for parent in ('DATA_TUNING_PROFILE=1', 'AD_FOLLOWUP_PROFILE=2', 'DATA_BUDGET_PROFILE=4', 'DATA_SCHEDULE_MODE=2'):
                if fields.count(parent) != 1:raise RuntimeError(label + ' parent profile binding changed: ' + parent)
            checked(label, 'exact-Make-profile-binding')

        cap_case('all controls omitted', {}, 0, 1, 0x10, 4, 1500, 1024)
        for profile in (1, 2):
            for name, value in UNSAFE:
                for include in HEADERS:header(profile, {name: value}, False, include)
                header(profile, {name: value}, False, asm=True)
                make(profile, {MAKE_NAMES[name]: value}, False)
            # Missing required-one selectors must also fail from bare headers.
            for name in ('KUI_TOY_PILOT_DATA_TUNING_PROFILE', 'KUI_TOY_PILOT_PRIVATE_P2',
                         'KUI_TOY_PILOT_LOADER_TRACE', 'KUI_TOY_PILOT_DATA_PROBE'):
                defs = dict(SAFE, KUI_TOY_PILOT_STARTUP_IRQ_PROFILE=profile);defs.pop(name)
                for include in HEADERS:
                    run(command(ROOT, defs, ['-E', '-P', '-x', 'c', '-']),
                        'missing ' + name, False, '#include "' + include + '"\n', True)
                    checked('profile' + str(profile) + ' omitted ' + name + ' ' + include,
                            'required-header-control-omitted', accepted=False)
        for invalid in (-1, 3, 255):
            for include in HEADERS:header(invalid, expected=False, include=include)
            header(invalid, expected=False, asm=True)
        for alias in ('AM', 'AN', 'AI', 'unknown'):
            # Bare#if converts an undefined identifier to0. C integer constant
            # validation and the Make whitelist independently reject aliases.
            for include in HEADERS:header(alias, expected=False, include=include, constant=True)
        for fraction in ('1.0', '2.5'):
            for include in HEADERS:header(fraction, expected=False, include=include, constant=True)
            header(fraction, expected=False, asm=True)
        for invalid in (-1, 3, 255, '', '1 2', '01', '1.0', '2.5', 'AM', 'AN', 'AI',
                        '1u', '1+0', 'unknown'):
            make(invalid, expected=False)

        for row in OLD:
            label, schedule, budget, followup, tuning, report, blocks, ticks, steps, quanta, audio_ticks = row
            defs = dict(SAFE, KUI_TOY_PILOT_DATA_SCHEDULE_MODE=schedule,
                        KUI_TOY_PILOT_DATA_BUDGET_PROFILE=budget,
                        KUI_TOY_PILOT_AD_FOLLOWUP_PROFILE=followup,
                        KUI_TOY_PILOT_DATA_TUNING_PROFILE=tuning)
            if label == 'R':defs.update(KUI_TOY_PILOT_LOADER_TRACE=0, KUI_TOY_PILOT_DATA_PROBE=0)
            cap_case(label + ' IRQ0', defs, report, 1, 0x10, blocks, ticks, steps, quanta, audio_ticks)
            changes = {MAKE_NAMES[k]: v for k, v in defs.items() if k in MAKE_NAMES}
            omitted_config, omitted_flags = make(None, changes, suffix='legacy-' + label)
            explicit_config, explicit_flags = make(0, changes, suffix='legacy-' + label)
            frozen_config, frozen_flags = make(None, changes, root=legacy, suffix='legacy-' + label)
            if omitted_config != explicit_config or omitted_config != frozen_config or \
                    omitted_flags != explicit_flags or omitted_flags != frozen_flags:
                raise RuntimeError('IRQ0 changed frozen Make/config/flags: ' + label)
            if any(x.startswith('STARTUP_IRQ_PROFILE=') for x in omitted_config.decode().splitlines()) or \
                    any(x.startswith('-DKUI_TOY_PILOT_STARTUP_IRQ_PROFILE=') for x in omitted_flags):
                raise RuntimeError('Disabled IRQ flag/config leaked into ' + label)
            checked(label, 'frozen-legacy-Make-config-and-flags')
            sources = (*SOURCES, ASM_SOURCE) if schedule == 2 else \
                      (SOURCES[1:] if schedule else SOURCES[1:2])
            for source in sources:
                omitted = preprocess(source, defs)
                explicit = preprocess(source, dict(defs, KUI_TOY_PILOT_STARTUP_IRQ_PROFILE=0))
                frozen = preprocess(source, defs, root=legacy)
                # Formatting whitespace has no C/assembler token effect. Literal
                # suffixes and values are deliberately NOT normalized away.
                norm = lambda x:re.sub(r'\s+', ' ', x).strip()
                if omitted != explicit or norm(omitted) != norm(frozen):
                    raise RuntimeError('IRQ0 changed frozen production CPP: ' + label + ' ' + source)
                hashes[label + ':' + source] = hashlib.sha256(norm(omitted).encode()).hexdigest()
                checked(label + ' ' + source, 'frozen-legacy-production-CPP')

    commit = lambda path:subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=path, text=True).strip()
    evidence = dict(schema=1,profiles={str(k):dict(label=v[0],report_profile=v[1],priority=v[2],
                    nibble=v[3],blocks=v[4],ticks=v[5],steps=v[6],audio_quanta=4,audio_ticks=12500)
                    for k,v in PROFILES.items()},checks=checks,cpp_sha256=hashes,
                    runtime_source_commit=commit(ROOT),frozen_legacy_source_commit=commit(legacy),
                    scope='Production Make/header admission and ABI/cap binding, actual rehook assembly and worker audio consumers, exact disabled token streams across old profiles. Runtime protocol/hardware behavior has separate gates.',
                    alias_admission_note='Make exact integer whitelist rejects aliases. C integer constant tests reject undefined aliases; bare CPP in either C or assembler treats undefined selectors as0.')
    (directory / 'startup-irq-config-tests.json').write_text(json.dumps(evidence, indent=2) + '\n')
    print(f'Checked {len(checks)} production configuration admission/refusal and legacy preservation cases')
    print(FOOTER)


if __name__ == '__main__':main()
