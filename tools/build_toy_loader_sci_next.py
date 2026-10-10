#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build references and V/W/X readers; emit hardware ZIP only after admission."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

from package_toy_loader_sci_next import (PROFILES, R_BUILD, R_SHA, U_BUILD,
    U_COMMIT, U_SHA, T_BUILD, T_SHA, profile_id)

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/sci-next-tests'
BASELINE = ROOT / 'build/toy-u-reference'
PREFIX = ROOT / '.deps/sh-elf'
WORKER_STACK_SOURCES = (
    'src/loader/toy_pilot_worker.c', 'src/loader/toy_pilot_bus.c',
    'src/loader/toy_pilot_lease.c', 'src/loader/toy_pilot_gd.c',
    'src/loader/toy_pilot_pause.c', 'src/core/toy_pilot.c', 'src/core/hash.c',
    'src/loader/minic.c', 'src/loader/toy_loader_trace.c',
    'src/loader/toy_loader_trace_report.c', 'src/loader/retail_display.c',
    'src/loader/toy_loader_data_probe.c', 'src/loader/toy_loader_payload_control.c')
STAGE_STACK_SOURCES = (
    'src/loader/toy_pilot_stage.c', 'src/loader/sd_reader.c',
    'src/loader/retail_sd.c', 'src/loader/retail_storage.c',
    'src/loader/sci_sd_bus.c', 'src/core/ata.c', 'src/dreamcast/ata_bus.c',
    'src/loader/minic.c', 'src/core/retail_image.c', 'src/loader/retail_display.c',
    'src/core/retail_observe.c', 'src/core/hash.c')


def run(command, *, cwd=ROOT, log=None):
    command = [str(part) for part in command]
    print('+ ' + ' '.join(command), flush=True)
    if log is None:
        subprocess.run(command, cwd=cwd, check=True)
        return
    Path(log).parent.mkdir(parents=True, exist_ok=True)
    with Path(log).open('a', encoding='utf-8') as stream:
        process = subprocess.Popen(command, cwd=cwd, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, text=True, bufsize=1)
        for line in process.stdout:
            print(line, end='', flush=True)
            stream.write(line)
        status = process.wait()
        if status:
            raise subprocess.CalledProcessError(status, command)


def exact_runtime(path, expected):
    actual = hashlib.sha256(Path(path).read_bytes()).hexdigest()
    if actual != expected:
        raise ValueError('Retained runtime SHA256 differs: ' + str(path))
    print(actual + '  ' + str(path.relative_to(ROOT)), flush=True)


def json_command(command, output):
    data = subprocess.check_output([str(part) for part in command], cwd=ROOT, text=True)
    json.loads(data)
    Path(output).parent.mkdir(parents=True, exist_ok=True)
    Path(output).write_text(data, encoding='utf-8')
    print(data, end='', flush=True)


def compiler():
    gcc = subprocess.check_output(['sh-elf-gcc', '-dumpfullversion'], text=True).strip()
    objdump = subprocess.check_output(['sh-elf-objdump', '--version'], text=True).splitlines()[0]
    if gcc != '15.2.0' or '2.45.1' not in objdump:
        raise ValueError('Requires pinned GCC 15.2.0 / Binutils 2.45.1')
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / 'compiler.json').write_text(json.dumps({'gcc': gcc, 'objdump': objdump}, indent=2) + '\n')
    print(gcc + '; ' + objdump, flush=True)


def make(cwd, directory, build_id, trace, probe, mode=0):
    run(['make', '-j1', '-f', 'Makefile.toy_pilot', 'BUILD=' + directory,
         'BUILD_ID=' + build_id, 'PRIVATE_P2=1', 'NATIVE_CACHE=0',
         'SHARED_SCI=0', 'ASYNC_CDDA=0', 'SYNTHETIC_SOURCE=0',
         'GD_FIXED_STEP=2', 'SCI_REUSE_TDRE=0', 'LOADER_TRACE=' + str(trace),
         'DATA_PROBE=' + str(probe), 'DATA_PAYLOAD_MODE=' + str(mode),
         'CC=' + str(shutil.which('sh-elf-gcc')),
         'OBJCOPY=' + str(shutil.which('sh-elf-objcopy'))],
        cwd=cwd, log=OUT / 'cross-build.log')


def incomplete_stack_evidence(directory):
    """Check every compiled high C unit and every real emitted resident LTO SU."""
    missing, expected = [], set()
    for prefix, sources in (('worker', WORKER_STACK_SOURCES), ('stage', STAGE_STACK_SOURCES)):
        for source in sources:
            report = directory / prefix / Path(source).with_suffix('.su')
            expected.add(report)
            if not report.is_file() or not report.read_bytes().strip():
                missing.append(report)
        emitted = set((directory / prefix).rglob('*.su'))
        if emitted - expected:
            raise ValueError('Unexpected high stack report(s): ' + ', '.join(str(p) for p in sorted(emitted - expected)))
    originals = set(directory.glob('resident-sci.elf.ltrans*.su'))
    copies = set((directory / 'sci/lto').glob('*.su'))
    if not originals or {p.name for p in originals} != {p.name for p in copies}:
        missing.append(directory / 'resident-sci.elf')
    else:
        for report in sorted(originals):
            copy = directory / 'sci/lto' / report.name
            if not report.read_bytes().strip() or copy.read_bytes() != report.read_bytes():
                missing.append(directory / 'resident-sci.elf')
                break
    return missing


def complete_stack_evidence(directory, build_id, mode):
    """Retry actual serial compilation once; never create substitute SU content."""
    missing = incomplete_stack_evidence(directory)
    if not missing:
        return
    print('Incomplete stack evidence; recompiling original objects once: ' +
          ', '.join(str(path.relative_to(ROOT)) for path in missing), flush=True)
    for report in missing:
        if report.suffix == '.elf':
            # Re-link the actual LTO resident and regenerate its copied reports
            # and header/embedding dependencies through the normal Make rules.
            for old in directory.glob('resident-sci.elf.ltrans*.su'):
                old.unlink()
            for old in (directory / 'sci/lto').glob('*.su'):
                old.unlink()
            report.unlink(missing_ok=True)
        else:
            report.unlink(missing_ok=True)
            report.with_suffix('.o').unlink(missing_ok=True)
    make(ROOT, directory.relative_to(ROOT).as_posix(), build_id, 1, 1, mode)
    remaining = incomplete_stack_evidence(directory)
    if remaining:
        raise ValueError('Real compiler stack evidence remains incomplete after serial retry: ' +
                         ', '.join(str(path) for path in remaining))


def reference():
    if (BASELINE / 'retail-toy-pilot.kui').exists():
        exact_runtime(BASELINE / 'retail-toy-pilot.kui', U_SHA)
        return
    checkout = ROOT / 'build/sci-next-u-source'
    if checkout.exists():
        raise ValueError('Use an empty reference checkout path or a complete checked U build')
    run(['git', 'worktree', 'add', '--detach', str(checkout), U_COMMIT])
    try:
        make(checkout, 'build/toy-u-reference', U_BUILD, 1, 1)
        exact = checkout / 'build/toy-u-reference'
        if hashlib.sha256((exact / 'retail-toy-pilot.kui').read_bytes()).hexdigest() != U_SHA:
            raise ValueError('Exact U source did not reproduce delivered U bytes')
        shutil.copytree(exact, BASELINE)
    finally:
        run(['git', 'worktree', 'remove', '--force', str(checkout)])
    exact_runtime(BASELINE / 'retail-toy-pilot.kui', U_SHA)


def build():
    compiler()
    reference()
    if not (ROOT / 'build/sci-next-r-disabled/retail-toy-pilot.kui').exists():
        make(ROOT, 'build/sci-next-r-disabled', R_BUILD, 0, 0)
    exact_runtime(ROOT / 'build/sci-next-r-disabled/retail-toy-pilot.kui', R_SHA)
    json_command([sys.executable, 'tools/toy_pilot_cache_audit.py',
                  'build/sci-next-r-disabled'], OUT / 'R-linked-audit.json')
    if not (ROOT / 'build/sci-next-t-disabled/retail-toy-pilot.kui').exists():
        make(ROOT, 'build/sci-next-t-disabled', T_BUILD, 1, 0)
    exact_runtime(ROOT / 'build/sci-next-t-disabled/retail-toy-pilot.kui', T_SHA)
    json_command([sys.executable, 'tools/toy_loader_trace_audit.py',
                  'build/sci-next-t-disabled'], OUT / 'T-linked-audit.json')
    head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    for mode, label, _ in PROFILES:
        name = 'build/sci-next-' + label.lower()
        build_id = profile_id(head, mode)
        make(ROOT, name, build_id, 1, 1, mode)
        complete_stack_evidence(ROOT / name, build_id, mode)
    (OUT / 'source.json').write_text(json.dumps({'head': head, 'U_reference': U_COMMIT,
        'profiles': [{'label': label, 'mode': mode, 'build': profile_id(head, mode)}
                     for mode, label, _ in PROFILES]}, indent=2) + '\n')
    for directory in ('toy-u-reference', 'sci-next-r-disabled', 'sci-next-t-disabled',
                      'sci-next-v', 'sci-next-w', 'sci-next-x'):
        for name in ('resident-sci', 'worker', 'stage'):
            target = ROOT / 'build' / directory
            raw = subprocess.check_output(['sh-elf-objdump', '-d',
                str(target / (name + '.elf'))], text=True)
            (target / (name + '.dis')).write_text(raw)
    for label in ('v', 'w', 'x'):
        run([sys.executable, 'tests/test_toy_pilot_p2_layout.py',
             '--resident-elf', 'build/sci-next-' + label + '/resident-sci.elf',
             '--worker-elf', 'build/sci-next-' + label + '/worker.elf'],
            log=OUT / 'linked-layout-tests.log')


def host():
    log = OUT / 'host.log'
    if log.exists():
        log.unlink()
    for command in (
        [sys.executable, 'tools/test_toy_pilot.py', '--build-dir', 'build/sci-next-tests/host-tests'],
        [sys.executable, 'tests/test_toy_loader_trace.py'],
        [sys.executable, 'tests/test_toy_loader_data_probe.py'],
        [sys.executable, 'tests/test_toy_loader_payload_control.py'],
    ):
        run(command, log=log)


def review():
    for mode, label, _ in PROFILES:
        json_command([sys.executable, 'tools/toy_loader_sci_next_audit.py',
            'build/sci-next-' + label.lower(), '--baseline', str(BASELINE),
            '--mode', str(mode), '--candidate'], OUT / (label + '-review-candidate.json'))


def admit(published_commit, destination):
    for mode, label, _ in PROFILES:
        json_command([sys.executable, 'tools/toy_loader_sci_next_audit.py',
            'build/sci-next-' + label.lower(), '--baseline', str(BASELINE),
            '--mode', str(mode)], OUT / (label + '-linked-admission.json'))
    linked_log = OUT / 'linked-fixtures.log'
    if linked_log.exists():
        linked_log.unlink()
    run([sys.executable, 'tests/test_toy_loader_sci_next_audit.py'], log=linked_log)
    destination.parent.mkdir(parents=True, exist_ok=True)
    run([sys.executable, 'tools/package_toy_loader_sci_next.py',
         '--v', 'build/sci-next-v', '--w', 'build/sci-next-w', '--x', 'build/sci-next-x',
         '--baseline', str(BASELINE), '--default-check', 'build/sci-next-r-disabled',
         '--restore-r', 'build/sci-next-r-disabled/retail-toy-pilot.kui',
         '--restore-t', 'build/sci-next-t-disabled/retail-toy-pilot.kui',
         '--host-log', str(OUT / 'host.log'), '--linked-log', str(linked_log),
         '--published-commit', published_commit, '--out', str(destination)])
    # This existing extractor validates every member against SHA256SUMS and
    # preserves the admitted ZIP unchanged before exposing hardware-ready files.
    import build_toy_loader_data_probe_ci as delivery
    delivery.OUT = OUT
    delivery.hardware_delivery(destination)
    print('Hardware test ZIP emitted only after linked admission.', flush=True)


def main():
    global BASELINE, PREFIX
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('phase', choices=('compiler', 'build', 'host', 'review', 'admit', 'all'))
    parser.add_argument('--toolchain', type=Path, default=PREFIX,
                        help='Prefix containing bin/sh-elf-gcc and Binutils')
    parser.add_argument('--baseline', type=Path, default=BASELINE)
    parser.add_argument('--published-commit', default=os.environ.get('GITHUB_SHA'))
    parser.add_argument('--out', type=Path, default=ROOT / 'dist/K-UI-Toy-SCI-Next-Tests.zip')
    args = parser.parse_args()
    BASELINE, PREFIX = args.baseline.absolute(), args.toolchain.absolute()
    os.environ['PATH'] = str(PREFIX / 'bin') + os.pathsep + os.environ.get('PATH', '')
    os.chdir(ROOT)
    try:
        if args.phase in ('admit', 'all') and not args.published_commit:
            raise ValueError('Supply the exact published source commit before hardware packaging')
        if args.phase == 'all':
            build()
            host()
            review()
            admit(args.published_commit, args.out.absolute())
        elif args.phase == 'admit':
            admit(args.published_commit, args.out.absolute())
        else:
            globals()[args.phase]()
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        parser.exit(1, 'FAIL: ' + str(error) + '\n')


if __name__ == '__main__':
    main()
