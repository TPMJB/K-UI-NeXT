#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the actual AD-family audio worker and reject limit reversions."""
import argparse
import hashlib
import os
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]
COMMON = (
    'KUI_TOY_PILOT_DATA_BUDGET_PROFILE=4', 'KUI_TOY_PILOT_DATA_SCHEDULE_MODE=2',
    'KUI_TOY_PILOT_PRIVATE_P2=1', 'KUI_TOY_PILOT_NATIVE_CACHE=0',
    'KUI_TOY_PILOT_SHARED_SCI=0', 'KUI_TOY_PILOT_ASYNC_CDDA=0',
    'KUI_TOY_PILOT_LOADER_TRACE=1', 'KUI_TOY_PILOT_DATA_PROBE=1',
    'KUI_TOY_PILOT_DATA_PAYLOAD_MODE=0', 'KUI_TOY_PILOT_SYNTHETIC_SOURCE=0',
    'KUI_TOY_PILOT_GD_FIXED_STEP=2', 'KUI_SCI_DMA_REUSE_TDRE=0',
)


def identities():
    names = ('src/loader/toy_pilot_worker.c', 'include/kui/toy_loader_ad_followup.h',
             'src/core/toy_pilot.c', 'src/core/hash.c',
             'tests/toy_pilot_async_fixture.h', 'tests/test_toy_ad_audio_worker.c')
    return {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in names}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build/ad-audio-host')
    parser.add_argument('--no-sanitizers', action='store_true')
    args = parser.parse_args()
    out = args.build_dir.resolve(); out.mkdir(parents=True, exist_ok=True)
    # Initialization is not executed by this independently prepared worker
    # fixture. These typed compile-only exports satisfy its unchanged ABI;
    # gc-sections removes that uncalled initialization entry from the host.
    (out / 'toy_pilot_resident_symbols.h').write_text(''.join(
        f'#define KUI_TOY_PILOT_LOW_SCI_{name} 0x8c004100u\n'
        for name in ('CARD', 'ACQUIRE', 'RELEASE', 'HEALTHY')))
    flags = ['-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
             '-Wno-unused-function', '-fno-pie', '-no-pie', '-ffunction-sections',
             '-fdata-sections', '-Iinclude', '-Isrc/loader', '-I' + str(out)]
    if not args.no_sanitizers:
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0')

    def build(profile, target, worker=None):
        expected_quanta = 2 if profile == 3 else 4
        expected_slow = 2 if profile in (3, 4) else 3
        definitions = [*COMMON, f'KUI_TOY_PILOT_AD_FOLLOWUP_PROFILE={profile}',
                       f'EXPECT_QUANTA={expected_quanta}', f'EXPECT_SLOW_READS={expected_slow}']
        if worker is not None:
            definitions.append('TOY_AD_WORKER_SOURCE="' + str(worker) + '"')
        subprocess.run(shlex.split(args.cc) + flags + ['-D' + d for d in definitions] +
                       ['tests/test_toy_ad_audio_worker.c', 'src/core/toy_pilot.c',
                        'src/core/hash.c', '-Wl,--gc-sections', '-o', str(target)],
                       cwd=ROOT, check=True)

    before = identities()
    for profile in range(5):
        target = out / f'test-audio-{profile}'
        build(profile, target)
        subprocess.run([str(target)], cwd=ROOT, env=env, check=True)
    original = (ROOT / 'src/loader/toy_pilot_worker.c').read_text()
    mutations = (
        (3, 'AG-quantum-reversion', '#define REFILL_QUANTA KUI_TOY_AD_REFILL_QUANTA',
         '#define REFILL_QUANTA 4u'),
        (4, 'AH-time-reversion', '#define REFILL_VISIT_TICKS KUI_TOY_AD_REFILL_TICKS',
         '#define REFILL_VISIT_TICKS 12500u'),
    )
    for profile, name, old, new in mutations:
        if original.count(old) != 1:
            raise RuntimeError('Missing exact audio mutation anchor: ' + name)
        source = out / (name + '.c'); source.write_text(original.replace(old, new, 1))
        target = out / ('test-' + name); build(profile, target, source)
        run = subprocess.run([str(target)], cwd=ROOT, env=env, capture_output=True, text=True)
        if run.returncode == 0 or 'Assertion' not in run.stderr:
            raise RuntimeError('Audio limit regression escaped mutation: ' + name + '\n' + run.stderr)
        print('Rejected intended audio limit reversion: ' + name)
    if before != identities():
        raise RuntimeError('Production audio or fixture sources changed during host validation')
    print('5 AD audio worker profile suites and 2 limit mutations passed')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
