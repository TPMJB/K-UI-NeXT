#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Validate all budget TRACE credit profiles and reject the old IRQ omission."""
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
TRACE = ROOT / 'src/loader/toy_loader_trace.c'
BASELINE = '8d2820a6586c0cd4ccedfd50d8804a205ba85f09'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build/budget-trace-host')
    parser.add_argument('--no-sanitizers', action='store_true')
    args = parser.parse_args()
    directory = args.build_dir.resolve()
    directory.mkdir(parents=True, exist_ok=True)
    compiler = shlex.split(args.cc)
    flags = ['-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
             '-fno-pie', '-no-pie', '-Iinclude', '-Isrc/loader',
             '-DKUI_TOY_PILOT_PRIVATE_P2=1', '-DKUI_TOY_PILOT_GD_FIXED_STEP=2',
             '-DKUI_TOY_PILOT_LOADER_TRACE=1', '-DKUI_TOY_LOADER_TRACE_HOST_TEST=1',
             '-DKUI_RETAIL_GD_ASYNC=1']
    if not args.no_sanitizers:
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    environment = dict(os.environ, ASAN_OPTIONS='detect_leaks=0')
    results = []

    def compile_and_run(profile, trace, name):
        executable = directory / name
        command = [*compiler, *flags, '-DKUI_TOY_PILOT_DATA_BUDGET_PROFILE=' + str(profile),
                   'tests/test_toy_loader_trace_irq_credit.c', str(trace),
                   'src/core/retail_gd.c', '-o', str(executable)]
        subprocess.run(command, cwd=ROOT, env=environment, check=True)
        result = subprocess.run([str(executable)], cwd=ROOT, env=environment,
                                capture_output=True, text=True)
        return command, result

    for profile in range(1, 5):
        command, result = compile_and_run(profile, TRACE, 'irq-credit-' + str(profile))
        if result.returncode:
            raise RuntimeError('Budget credit profile failed:\n' + result.stdout + result.stderr)
        print(result.stdout, end='', flush=True)
        results.append({'profile': profile, 'command': command, 'exit_code': 0,
                        'stdout': result.stdout, 'stderr': result.stderr})

    raw_binary = directory / 'raw-sync-fallback'
    raw_command = [*compiler, *[flag for flag in flags if flag != '-DKUI_RETAIL_GD_ASYNC=1'],
                   '-DKUI_TOY_PILOT_DATA_BUDGET_PROFILE=1',
                   'tests/test_toy_loader_trace_raw_sync.c', 'src/core/retail_gd.c',
                   '-o', str(raw_binary)]
    subprocess.run(raw_command, cwd=ROOT, env=environment, check=True)
    raw_result = subprocess.run([str(raw_binary)], cwd=ROOT, env=environment,
                                capture_output=True, text=True)
    if raw_result.returncode:
        raise RuntimeError('Raw synchronous TRACE fallback failed:\n' + raw_result.stdout + raw_result.stderr)
    print(raw_result.stdout, end='', flush=True)

    # A genuine regression control: retain real GD progress and the same
    # payloads/timestamps, but restore the former per-visit diagnostic delta.
    corrected = ('            if(s->sector_bytes==2048u)\n'
                 '                add(&rp->delivered_sectors,(s->completed_bytes>>11)-\n'
                 '                    (request.credited>>11));\n'
                 '            else if(s->sector_bytes)\n'
                 '                add(&rp->delivered_sectors,s->diag.sectors_read-visit.sectors);')
    original_delta = '            add(&rp->delivered_sectors,s->diag.sectors_read-visit.sectors);'
    source = TRACE.read_text()
    if source.count(corrected) != 1:
        raise RuntimeError('TRACE credit mutation anchor differs')
    with tempfile.TemporaryDirectory(prefix='kui-budget-trace-credit-') as temporary:
        temporary = Path(temporary)
        mutant = temporary / 'visit-only.c'
        mutant.write_text(source.replace(corrected, original_delta, 1))
        _, result = compile_and_run(1, mutant, 'irq-credit-legacy-mutant')
        if result.returncode == 0 or 'p->delivered_sectors==1u' not in result.stderr:
            raise RuntimeError('Legacy IRQ-credit omission was not rejected by the production fixture')

        # Preprocessing preserves the entire old default profile's C tokens;
        # linked SH-4 runtime identity remains the package audit's gate.
        baseline = subprocess.run(['git', 'show', BASELINE + ':src/loader/toy_loader_trace.c'],
                                  cwd=ROOT, check=True, capture_output=True, text=True).stdout
        baseline_path = temporary / 'default-baseline.c'
        baseline_path.write_text(baseline)
        preprocess = [*compiler, '-std=c11', '-E', '-P', '-Iinclude', '-Isrc/loader',
                      '-DKUI_TOY_PILOT_PRIVATE_P2=1', '-DKUI_TOY_PILOT_GD_FIXED_STEP=2',
                      '-DKUI_TOY_PILOT_LOADER_TRACE=1', '-DKUI_TOY_LOADER_TRACE_HOST_TEST=1',
                      '-DKUI_TOY_PILOT_DATA_BUDGET_PROFILE=0']
        previous = subprocess.run([*preprocess, str(baseline_path)], cwd=ROOT, check=True,
                                  capture_output=True, text=True).stdout
        current = subprocess.run([*preprocess, str(TRACE)], cwd=ROOT, check=True,
                                 capture_output=True, text=True).stdout
        if previous != current:
            raise RuntimeError('Default-zero TRACE preprocessing differs from retained baseline')
    (directory / 'irq-credit-tests.json').write_text(json.dumps({
        'schema': 1, 'sanitizers': [] if args.no_sanitizers else ['address', 'undefined'],
        'profiles': results, 'old_visit_only_delta_mutation_rejected': True,
        'default_baseline_commit': BASELINE,
        'raw_sync_command': raw_command,
        'raw_sync_stdout': raw_result.stdout,
        'raw_sync_exit_code': raw_result.returncode,
        'default_zero_preprocessed_source_unchanged': True,
        'scope': 'Production TRACE/GD protocol with independent modeled arrivals; no hardware IRQ timing claim',
    }, indent=2) + '\n')
    print('Budget TRACE IRQ credit suites passed (profiles 1/2/3/4; old delta mutation rejected; default zero unchanged)')


if __name__ == '__main__':
    main()
