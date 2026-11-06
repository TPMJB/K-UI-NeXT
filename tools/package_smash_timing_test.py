#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Deliver the opt-in Smash timing probe with its exact console-confirmed restore."""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import zipfile

from check_smash_menu_loader import audit
from package_smash_native_menu_test import INSTALL, optimized_runtime_proof
from package_vc2_ce_vmu_late_test import ROOT, git, installed_module, sha, wire
from runtime_package import verify
from test_smash_launch_timing import audit as ui_timing_audit

CONFIRMED = '2c53ceafadf51d10b2aedeeb4e33ed8b5a6ca0c0'
CONFIRMED_ZIP_SHA = 'acfa1c691ba55bb927051176af8f8bdb046be6981566637fe070721ca4f12ed1'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--outer-build', type=Path, default=ROOT / 'build/smash-menu-timing')
    parser.add_argument('--child-build', type=Path, default=ROOT / 'build/smash-native-timing')
    parser.add_argument('--runtime-elf', type=Path, default=ROOT / 'build/kui-runtime.elf')
    parser.add_argument('--confirmed', type=Path, required=True)
    parser.add_argument('--gin', type=Path, required=True)
    parser.add_argument('--main', required=True)
    parser.add_argument('--evidence', type=Path, action='append', default=[])
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if git('status', '--porcelain'):
        raise ValueError('Commit final source before packaging')
    if args.output.suffix != '.zip':
        raise ValueError('Use a ZIP output filename')
    commit = git('rev-parse', 'HEAD').decode().strip()
    base = git('rev-parse', args.main).decode().strip()
    for parent in (base, CONFIRMED):
        subprocess.run(['git', 'merge-base', '--is-ancestor', parent, commit], cwd=ROOT, check=True)
    if sha(args.confirmed.read_bytes()) != CONFIRMED_ZIP_SHA:
        raise ValueError('Confirmed restore differs from the delivered 2c53 package')
    proof = audit(args.outer_build, args.child_build, runtime_elf=args.runtime_elf, gin=args.gin)
    if (proof['status'] != 'PASS' or not proof['runtime']['bound'] or
            proof.get('timing_diagnostic', {}).get('status') != 'PASS'):
        raise ValueError('Need complete passing linked outer/child/runtime audit')
    ui_proof = ui_timing_audit()
    if not ui_proof.get('ok'):
        raise ValueError('Need passing production UI timing accounting and sidecar checks')
    runtime_optimization = optimized_runtime_proof(args.runtime_elf)
    files = {
        'LICENSE.txt': (ROOT / 'LICENSE').read_bytes(),
        'source/change-from-confirmed-2c53.patch': git('diff', '--binary', CONFIRMED, commit),
        'source/change-from-main.patch': git('diff', '--binary', base, commit),
        'evidence/menu-linked.json': wire(proof),
        'evidence/ui-timing.json': wire(ui_proof),
        'evidence/timing-design.md': (ROOT / 'docs/smash-launch-timing.md').read_bytes(),
    }
    for path in args.evidence:
        name = 'evidence/' + path.name
        if name in files:
            raise ValueError('Duplicate evidence filename: ' + name)
        files[name] = path.read_bytes()
    for name, module, elf in (
        (INSTALL[0], args.outer_build / 'runtime.kui', args.runtime_elf),
        (INSTALL[1], args.outer_build / 'retail-smash-menu.kui', args.outer_build / 'entry.elf'),
        (INSTALL[2], args.child_build / 'retail-smash-native-menu.kui', args.child_build / 'entry.elf'),
    ):
        data, provenance = installed_module(module, elf, commit)
        files['KUI/' + name] = data
        files['evidence/' + Path(name).name + '.compile.json'] = wire(provenance)
    # The marker and typed trace are generated only by the timing profiles.
    runtime_marker = struct.pack('<4I', 0x5349554b, 0x31544955, 1, 16)
    if files['KUI/runtime.kui'][64:].count(runtime_marker) != 1:
        raise ValueError('Installed runtime lacks its unique opt-in UI timing profile')
    trace_prefix = struct.pack('<4I', 0x5349554b, 0x314d4954, 1, 992)
    child = files['KUI/apps/games/retail-smash-native-menu.kui'][64:]
    if child.count(trace_prefix) != 1:
        raise ValueError('Installed child lacks its unique timing trace seed')
    at = child.index(trace_prefix)
    if at & 3 or child[at:at + 992] != trace_prefix + bytes(992 - 16):
        raise ValueError('Timing trace seed is not blank, aligned and complete')
    with zipfile.ZipFile(args.confirmed) as archive:
        if archive.testzip() is not None:
            raise ValueError('Confirmed restore failed ZIP CRC verification')
        restore = {name: archive.read('KUI/' + name) for name in INSTALL}
        for name in archive.namelist():
            if name.startswith(('Restore-confirmed-VC2/', 'Restore-confirmed-Swirl/')) and not name.endswith('/'):
                files[name] = archive.read(name)
    if any(verify(data)['build'] != CONFIRMED[:12] for data in restore.values()):
        raise ValueError('Confirmed restore has mixed module builds')
    for name, data in restore.items():
        files['Restore-confirmed-2c53/KUI/' + name] = data
    files['BUILD.json'] = wire(dict(
        schema='kui.smash-timing.delivery.v1', commit=commit,
        purpose='Observe UI preparation and native RPG launch elapsed phases',
        diagnostic_only=True, performance_improvement_claimed=False,
        hardware_verified=False, main_included=base,
        install_files=list(INSTALL), source_game_bytes_included=False,
        confirmed_restore=dict(commit=CONFIRMED,
            console_report='VMU survives Shining Force / Phantasy Star II menu switching; launches remain slow',
            package_sha256=CONFIRMED_ZIP_SHA,
            sha256={name: sha(data) for name, data in restore.items()}),
        runtime_optimization=runtime_optimization,
    ))
    with tempfile.TemporaryDirectory(prefix='smash-timing-source-') as folder:
        bundle = Path(folder) / 'smash-timing.bundle'
        subprocess.run(['git', 'bundle', 'create', str(bundle), 'HEAD', '^' + base], cwd=ROOT, check=True, capture_output=True)
        subprocess.run(['git', 'bundle', 'verify', str(bundle)], cwd=ROOT, check=True, capture_output=True)
        files['source/smash-timing.bundle'] = bundle.read_bytes()
    files['README.txt'] = f'''K-UI Smash Pack launch timing diagnostic {commit[:12]}

The 2c53 build is console confirmed for VMU use across Shining Force and
Phantasy Star II menu returns. This separate test measures the remaining
delays. It does not claim a speed improvement.

INSTALL
Copy the TOP-LEVEL KUI folder over the SD installation. Install all THREE
files together. Keep your existing card and game image. Remove physical
VMUs; use Virtual VMU, Native/Default boot, background reading OFF, CD music
OFF and RAM patches OFF, as with the confirmed build.

COLLECT THE TIMINGS
Launch Sega Smash Pack and select Shining Force or Phantasy Star II.
The diagnostic records the K-UI preparation phases automatically in:
KUI/apps/games/smash-launch-timing.txt
Photograph every page of the labelled native timing screen. The intentional
diagnostic stop requires power off; it is not a new launch hang. Attach the
text report and photographs, naming the selected game. If you repeat the
test, keep the text report from each run before it is replaced.

The native trace starts at the first arrived MDE payload chunk and stops
before entering the game engine. It measures the intervening loader work;
the earliest selection-to-first-chunk gap and the later game/SGA-loading
tail are outside it. This stop also means a second selection after returning
from gameplay is not measured in this build. If reaching the timing screen
is quick, that itself points to the later game/SDK phase for the next trace.
The UI report ends when the storage worker is ready for handoff. The final
main-thread/KOS shutdown after that point is outside that report, so compare
its action-to-ready total with your observed pre-bootstrap wait as well.

RESTORE NORMAL PLAY
Copy Restore-confirmed-2c53/KUI over the SD installation, replacing all
three files. Those bytes are identical to the confirmed 2c53 delivery.
The existing shared virtual card is retained. The same package also carries
the previously confirmed Virtua Cop 2 and Sega Swirl restore folders.

The diagnostic observes the existing performance counter without changing
its hardware configuration. Long masked-interrupt phases cannot use the
ordinary KOS uptime clock. Invalid clock samples are reported as unavailable,
not zero. UI report output and intentional photograph holds are identified
separately. Existing journal recovery/checkpoint work may write storage as
on a normal launch; this is not a read-only-card test. Source verification,
every block CRC, current card admission and
the confirmed return behavior remain part of the linked functional checks.

Source includes the delta from confirmed {CONFIRMED}, the full delta from
main {base}, and an incremental Git bundle requiring that main commit.
Linked and host checks describe authored models, not a console timing result.
No original game executables, ROMs or private VMU image bytes are included.
'''.encode()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in files.items():
            archive.writestr(name, data)
    with zipfile.ZipFile(args.output) as archive, tempfile.TemporaryDirectory(prefix='smash-timing-readback-') as folder:
        if archive.testzip() is not None:
            raise ValueError('Delivery ZIP failed CRC verification')
        archive.extractall(folder)
        for name, data in files.items():
            if (Path(folder) / name).read_bytes() != data:
                raise ValueError('Extracted file differs: ' + name)
    print(json.dumps(dict(status='PASS', file=str(args.output), bytes=args.output.stat().st_size,
        sha256=sha(args.output.read_bytes()), commit=commit, extract_verified=True,
        diagnostic_only=True, hardware_verified=False)))


if __name__ == '__main__':
    main()
