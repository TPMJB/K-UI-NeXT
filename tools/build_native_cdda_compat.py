#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build and verify the three optional experimental native CDDA adapters.

Run with the SH compiler on PATH, after committing the intended source.
The exact-admission controls are evidence only and are never installed.
"""
import argparse
import json
from pathlib import Path
import subprocess

from check_native_cdda_compat import check_directory

ROOT = Path(__file__).resolve().parents[1]
TOY = ['PRIVATE_P2=1', 'LOADER_TRACE=1', 'DATA_PROBE=1', 'DATA_SCHEDULE_MODE=2',
       'DATA_BUDGET_PROFILE=4', 'AD_FOLLOWUP_PROFILE=2', 'DATA_TUNING_PROFILE=1',
       'STARTUP_IRQ_PROFILE=1', 'DATA_PIPELINE_PROFILE=2']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--jobs', type=int, default=4)
    parser.add_argument('--allow-dirty', action='store_true', help='Local development only')
    args = parser.parse_args()
    if not 1 <= args.jobs <= 32:
        parser.error('--jobs must be between 1 and 32')
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    dirty = bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT))
    if dirty and not args.allow_dirty:
        parser.error('Commit the intended source before creating native deliverables')
    commands = []
    for profile in ('toy-raw', 'toy-cooked', 'mdk2'):
        makefile = 'Makefile.mdk2_cdda' if profile == 'mdk2' else 'Makefile.toy_pilot'
        flags = [] if profile == 'mdk2' else TOY + (['COOKED_DATA_PROFILE=1'] if profile == 'toy-cooked' else [])
        for mode, selected in (('reference', 0), ('compat', 1)):
            directory = 'build/native-' + mode + '-' + profile
            command = ['make', '-f', makefile, 'BUILD=' + directory, 'BUILD_ID=' + commit[:12],
                       'NATIVE_COMPAT=' + str(selected), '-j' + str(args.jobs), *flags, 'all']
            log = ROOT / (directory + '.log')
            log.parent.mkdir(parents=True, exist_ok=True)
            with log.open('w') as output:
                result = subprocess.run(command, cwd=ROOT, stdout=output, stderr=subprocess.STDOUT)
            if result.returncode:
                raise SystemExit('Native build failed; inspect ' + str(log))
            commands.append(command)
    evidence = {'source_commit': commit, 'source_dirty': dirty, 'commands': commands, 'adapters': {}}
    for profile in ('toy-raw', 'toy-cooked', 'mdk2'):
        directory = ROOT / ('build/native-compat-' + profile)
        proof = check_directory(directory, profile)
        (directory / 'native-compat-audit.json').write_text(json.dumps(proof, indent=2) + '\n')
        evidence['adapters'][profile] = proof
        print('PASS ' + profile + ': unchanged playback worker/reader; experimental stage admission verified')
    destination = ROOT / 'build/native-compat-build-results.json'
    destination.write_text(json.dumps(evidence, indent=2) + '\n')
    print(destination)


if __name__ == '__main__':
    main()
