#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Code-only hosted fallback for the isolated U hardware test candidate."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/data-probe-ci'
BASELINE = '6e7be09fe3684291b09e6420adae04eeb2469e03'
T_BUILD = '83ddfdfcb4ca'
R_BUILD = '73e8363c10f7'
T_SHA = 'fef4abb8b7f217eb004357e377630e8e8e64f45ac7b4e5106d5c72abc2e54227'
R_SHA = '99e1c0ad84a916fa9e08cf68b494c2d0efe0748fe956e579841c3d7614e49037'
PREFIX = ROOT / '.deps/sh-elf'

def run(command, *, cwd=ROOT, log=None):
    print('+ ' + ' '.join(str(part) for part in command), flush=True)
    if log is None:
        subprocess.run([str(part) for part in command], cwd=cwd, check=True)
        return
    Path(log).parent.mkdir(parents=True, exist_ok=True)
    with Path(log).open('a', encoding='utf-8') as stream:
        process = subprocess.Popen([str(part) for part in command], cwd=cwd,
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                   text=True, bufsize=1)
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

def check_recovery():
    metadata = json.loads((ROOT / 'build/compiler-recovery/artifact.json').read_text())
    if (metadata.get('id') != 11493202582 or metadata.get('expired') is not False or
            metadata.get('name') != 'cdda-toolchain-recovery-37644910026' or
            metadata.get('workflow_run', {}).get('id') != 37644910026):
        raise ValueError('Compiler recovery identity or expiration differs')

def recover():
    check_recovery()
    archive = ROOT / 'build/compiler-recovery/sh-elf-cdda-toolchain.tar.gz'
    dest = ROOT / 'build/compiler-recovery/unpacked'
    dest.mkdir(parents=True, exist_ok=True)
    with tarfile.open(archive, 'r:gz') as stream:
        stream.extractall(dest, filter='data')
    bundle = dest / 'cdda-toolchain'
    count = 0
    for line in (bundle / 'SHA256SUMS').read_text().splitlines():
        digest, name = line.split('  ', 1)
        source = bundle / name
        if not source.resolve().is_relative_to(bundle.resolve()):
            raise ValueError('Compiler checksum path escapes its bundle')
        if hashlib.sha256(source.read_bytes()).hexdigest() != digest:
            raise ValueError('Compiler recovery checksum differs: ' + name)
        count += 1
    if count != 793:
        raise ValueError('Compiler recovery file count differs')
    if PREFIX.exists():
        shutil.rmtree(PREFIX)
    shutil.copytree(bundle / 'sh-elf', PREFIX)
    (PREFIX / '.kui-toolchain-complete').touch()
    print('Pinned compiler recovery: 793 file hashes passed', flush=True)

def compiler():
    gcc = subprocess.check_output([str(PREFIX / 'bin/sh-elf-gcc'),
                                   '-dumpfullversion'], text=True).strip()
    objdump = subprocess.check_output([str(PREFIX / 'bin/sh-elf-objdump'),
                                       '--version'], text=True).splitlines()[0]
    if gcc != '15.2.0' or '2.45.1' not in objdump:
        raise ValueError('Compiler/binutils version differs from the pinned build')
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / 'compiler.json').write_text(json.dumps({'gcc': gcc, 'objdump': objdump},
                                                 indent=2) + '\n')
    print(gcc + '; ' + objdump, flush=True)

def make(cwd, directory, build_id, trace, probe):
    run(['make', '-j2', '-f', 'Makefile.toy_pilot', 'BUILD=' + directory,
         'BUILD_ID=' + build_id, 'PRIVATE_P2=1', 'NATIVE_CACHE=0',
         'SHARED_SCI=0', 'ASYNC_CDDA=0', 'SYNTHETIC_SOURCE=0',
         'GD_FIXED_STEP=2', 'SCI_REUSE_TDRE=0', 'LOADER_TRACE=' + str(trace),
         'DATA_PROBE=' + str(probe), 'CC=' + str(PREFIX / 'bin/sh-elf-gcc'),
         'OBJCOPY=' + str(PREFIX / 'bin/sh-elf-objcopy')],
        cwd=cwd, log=OUT / 'cross-build.log')

def build():
    compiler()
    OUT.mkdir(parents=True, exist_ok=True)
    baseline_root = ROOT / 'build/reference-source'
    run(['git', 'worktree', 'add', '--detach', str(baseline_root), BASELINE])
    make(baseline_root, 'build/recovered-t-check', T_BUILD, 1, 0)
    shutil.copytree(baseline_root / 'build/recovered-t-check',
                    ROOT / 'build/recovered-t-check')
    exact_runtime(ROOT / 'build/recovered-t-check/retail-toy-pilot.kui', T_SHA)
    json_command([sys.executable, 'tools/toy_loader_trace_audit.py',
                  'build/recovered-t-check'], OUT / 'T-linked-audit.json')
    make(ROOT, 'build/probe-default-off-check', R_BUILD, 0, 0)
    exact_runtime(ROOT / 'build/probe-default-off-check/retail-toy-pilot.kui', R_SHA)
    json_command([sys.executable, 'tools/toy_pilot_cache_audit.py',
                  'build/probe-default-off-check'], OUT / 'R-linked-audit.json')
    run([sys.executable, 'tests/test_toy_pilot_cache_audit.py', '--build-dir',
         'build/probe-default-off-check'], log=OUT / 'linked-reference-tests.log')
    make(ROOT, 'build/probe-trace-only-check', T_BUILD, 1, 0)
    exact_runtime(ROOT / 'build/probe-trace-only-check/retail-toy-pilot.kui', T_SHA)
    json_command([sys.executable, 'tools/toy_loader_trace_audit.py',
                  'build/probe-trace-only-check'], OUT / 'disabled-T-linked-audit.json')
    head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    make(ROOT, 'build/toy-data-probe', head[:12], 1, 1)
    (OUT / 'source.json').write_text(json.dumps({'head': head, 'baseline': BASELINE},
                                               indent=2) + '\n')
    for directory in ('recovered-t-check', 'probe-default-off-check',
                       'probe-trace-only-check', 'toy-data-probe'):
        for name in ('resident-sci', 'worker', 'stage'):
            target = ROOT / 'build' / directory
            raw = subprocess.check_output([str(PREFIX / 'bin/sh-elf-objdump'), '-d',
                                           str(target / (name + '.elf'))], text=True)
            (target / (name + '.dis')).write_text(raw)
    run([sys.executable, 'tests/test_toy_pilot_p2_layout.py',
         '--resident-elf', 'build/toy-data-probe/resident-sci.elf',
         '--worker-elf', 'build/toy-data-probe/worker.elf'],
        log=OUT / 'linked-reference-tests.log')

def host():
    log = OUT / 'host.log'
    if log.exists():
        log.unlink()
    for command in (
        [sys.executable, 'tools/test_toy_pilot.py', '--build-dir',
         'build/data-probe-ci/host-tests'],
        [sys.executable, 'tests/test_toy_loader_trace.py'],
        [sys.executable, 'tests/test_toy_loader_data_probe.py'],
    ):
        run(command, log=log)

def review():
    json_command([sys.executable, 'tools/toy_loader_data_probe_audit.py',
                  'build/toy-data-probe', '--baseline', 'build/recovered-t-check',
                  '--review-candidate'], OUT / 'U-review-candidate.json')

def admit():
    json_command([sys.executable, 'tools/toy_loader_data_probe_audit.py',
                  'build/toy-data-probe', '--baseline', 'build/recovered-t-check'],
                 OUT / 'U-linked-audit.json')
    run([sys.executable, 'tests/test_toy_loader_data_probe_audit.py'],
        log=OUT / 'U-linked-fixtures.log')
    destination = ROOT / 'dist/K-UI-Toy-Loader-DATA-Probe.zip'
    destination.parent.mkdir(parents=True, exist_ok=True)
    run([sys.executable, 'tools/package_toy_loader_data_probe.py',
         '--build', 'build/toy-data-probe', '--baseline', 'build/recovered-t-check',
         '--default-check', 'build/probe-default-off-check',
         '--restore-r', 'build/probe-default-off-check/retail-toy-pilot.kui',
         '--host-log', 'build/data-probe-ci/host.log', '--out', str(destination)])
    print('Hardware test package built only after linked admission.', flush=True)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('phase', choices=('check-recovery', 'recover', 'compiler',
                                         'build', 'host', 'review', 'admit'))
    args = parser.parse_args()
    os.chdir(ROOT)
    globals()[args.phase.replace('-', '_')]()

if __name__ == '__main__':
    main()
