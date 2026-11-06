#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package the original Smash Pack menu's shared native VMU integration test."""
import argparse
import json
import re
from pathlib import Path
import subprocess
import tempfile
import zipfile

from check_smash_menu_loader import audit
from package_vc2_ce_vmu_late_test import ROOT, git, installed_module, sha, wire
from package_vc2_ce_vmu_persist import paired_helper
from runtime_package import verify

CONFIRMED_VC2 = '10ef97cc87b7ee4ee393ebd40f83a3d94cce6461'
CONFIRMED_SWIRL = 'c1790560d26e43decc2a84471fab40d97a3efe45'
TESTED_MENU = '17b7c1bbf6d0c45d3c6ec457c8e277921ee13a66'
TESTED_MENU_TREE = '5eb672150339a5c715940b97d31851ca819edd9e'
TESTED_MENU_ZIP_SHA = 'f8fdcd85477424b0b9f2bd6a2dd09af2aef9b59f934da5f2e4f65e1cef06a0e4'
RESTORE = ('runtime.kui', 'apps/games/retail-physical.kui',
           'apps/games/vc2-vmu-helper.bin')
INSTALL = ('runtime.kui', 'apps/games/retail-smash-menu.kui',
           'apps/games/retail-smash-native-menu.kui')


def optimized_runtime_proof(elf):
    # GCC records the effective options in the linked DWARF producer strings.
    # Check the actual ELF, rather than trusting an environment or Makefile
    # which might not have rebuilt previously unoptimized objects.
    producers = sorted({part.decode('ascii') for part in elf.read_bytes().split(b'\0')
        if re.match(rb'^GNU C(?:[0-9]+|\+\+[0-9]+) [0-9]', part)})
    if not producers or any(not re.search(r'(?:^|\s)-O(?:2|3|s|fast)(?:\s|$)', p)
                            for p in producers):
        raise ValueError('Runtime contains missing or unoptimized compiler provenance')
    return dict(actual_linked_producers=producers, optimized=True)


def restore_files(path, expected):
    with zipfile.ZipFile(path) as archive:
        if archive.testzip() is not None:
            raise ValueError('Restore ZIP failed CRC verification')
        files = {name: archive.read('KUI/' + name) for name in RESTORE}
    if any(verify(files[name])['build'] != expected[:12] for name in RESTORE[:2]):
        raise ValueError('Restore build differs from its confirmed source')
    pair = paired_helper(files[RESTORE[1]], files[RESTORE[2]])
    return files, dict(commit=expected, helper_pair=pair,
                       sha256={name: sha(data) for name, data in files.items()})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--outer-build', type=Path, default=ROOT / 'build/smash-menu-loader')
    parser.add_argument('--child-build', type=Path, default=ROOT / 'build/smash-native-menu')
    parser.add_argument('--confirmed-vc2', type=Path, required=True)
    parser.add_argument('--confirmed-swirl', type=Path, required=True)
    parser.add_argument('--tested-menu', type=Path, required=True)
    parser.add_argument('--baseline', required=True)
    parser.add_argument('--gin', type=Path, required=True,
                        help='Exact private GINXFER.BIN beside the reviewed MDE/menu fixtures')
    parser.add_argument('--main', required=True)
    parser.add_argument('--evidence', type=Path, action='append', default=[])
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.suffix != '.zip':
        raise ValueError('Use a ZIP output filename')
    if git('status', '--porcelain'):
        raise ValueError('Commit final source before packaging')
    commit = git('rev-parse', 'HEAD').decode().strip()
    main_commit = git('rev-parse', args.main).decode().strip()
    baseline = git('rev-parse', args.baseline).decode().strip()
    if git('rev-parse', baseline + '^{tree}').decode().strip() != TESTED_MENU_TREE:
        raise ValueError('Recovered baseline differs from the delivered tested-menu tree')
    if sha(args.tested_menu.read_bytes()) != TESTED_MENU_ZIP_SHA:
        raise ValueError('Tested-menu restore differs from the delivered package')
    for parent in (main_commit, baseline):
        subprocess.run(['git', 'merge-base', '--is-ancestor', parent, commit],
                       cwd=ROOT, check=True)
    proof = audit(args.outer_build, args.child_build, gin=args.gin)
    if proof['status'] != 'PASS' or proof.get('hardware_verified') is not False:
        raise ValueError('Need a passing linked menu-loader audit')
    runtime_optimization = optimized_runtime_proof(ROOT / 'build/kui-runtime.elf')
    files = {
        'LICENSE.txt': (ROOT / 'LICENSE').read_bytes(),
        'source/change-from-main.patch': git('diff', '--binary', main_commit, commit),
        'source/change-from-tested-menu.patch': git('diff', '--binary', baseline, commit),
        'evidence/menu-linked.json': wire(proof),
        'Genesis-save-check/kui-smash-genesis-save-headers.py':
            (ROOT / 'tools/kui-smash-genesis-save-headers.py').read_bytes(),
        'Genesis-save-check/save-scope.md':
            (ROOT / 'docs/smash-genesis-save-scope.md').read_bytes(),
    }
    for path in args.evidence:
        name = 'evidence/' + path.name
        if name in files:
            raise ValueError('Duplicate evidence filename: ' + name)
        files[name] = path.read_bytes()
    for name, module, elf in (
        (INSTALL[0], args.outer_build / 'runtime.kui', ROOT / 'build/kui-runtime.elf'),
        (INSTALL[1], args.outer_build / 'retail-smash-menu.kui', args.outer_build / 'entry.elf'),
        (INSTALL[2], args.child_build / 'retail-smash-native-menu.kui', args.child_build / 'entry.elf'),
    ):
        data, provenance = installed_module(module, elf, commit)
        files['KUI/' + name] = data
        files['evidence/' + Path(name).name + '.compile.json'] = wire(provenance)
    restores = {}
    with zipfile.ZipFile(args.tested_menu) as archive:
        if archive.testzip() is not None:
            raise ValueError('Tested-menu restore ZIP failed CRC verification')
        prior = {name: archive.read('KUI/' + name) for name in INSTALL}
    if any(verify(data)['build'] != TESTED_MENU[:12] for data in prior.values()):
        raise ValueError('Tested-menu restore has mixed module builds')
    for name, data in prior.items():
        files['Restore-first-launch-17b7/KUI/' + name] = data
    restores['first_launch_17b7'] = dict(commit=TESTED_MENU,
        sha256={name: sha(data) for name, data in prior.items()},
        console_report='First Shining Force card detection worked; later menu selection lost the card')
    for label, path, expected in (
        ('VC2', args.confirmed_vc2, CONFIRMED_VC2),
        ('Swirl', args.confirmed_swirl, CONFIRMED_SWIRL),
    ):
        original, info = restore_files(path, expected)
        for name, data in original.items():
            files['Restore-confirmed-' + label + '/KUI/' + name] = data
        restores[label] = info
    files['BUILD.json'] = wire(dict(
        commit=commit, main_included=main_commit,
        purpose='Successive native Genesis selections with fresh shared-card admission',
        tested_source=TESTED_MENU, recovered_baseline=baseline, recovered_baseline_tree=TESTED_MENU_TREE,
        hardware_verified=False, save_persistence_verified=False,
        virtual_card='existing shared Sega Smash Pack VMU', virtual_slot='A1',
        native_menu_selections=10, install_files=list(INSTALL),
        source_game_bytes_included=False, confirmed_restores=restores,
        runtime_optimization=runtime_optimization,
    ))
    with tempfile.TemporaryDirectory(prefix='smash-source-bundle-') as folder:
        bundle = Path(folder) / 'menu-return.bundle'
        subprocess.run(['git', 'bundle', 'create', str(bundle), 'HEAD', '^' + main_commit],
                       cwd=ROOT, check=True, capture_output=True)
        subprocess.run(['git', 'bundle', 'verify', str(bundle)], cwd=ROOT, check=True, capture_output=True)
        files['source/menu-return.bundle'] = bundle.read_bytes()
    files['README.txt'] = f'''K-UI Smash Pack menu return test {commit[:12]}

This revision targets the card disappearing after ABXY+Start returns to the
original menu, using the existing shared virtual VMU. No original game files
or private card identity are included.

INSTALL
Copy this ZIP's TOP-LEVEL KUI folder over the current SD installation.
Install all THREE files together. No installer script is required.
Remove physical VMUs. Enable Virtual VMU, select Native/Default boot, and
leave background reading, CD music and RAM patches OFF.

TEST THE COMPLETE SESSION
1. Launch Sega Smash Pack normally and choose SHINING FORCE. Confirm the
   card is present, then load/save distinguishable progress. Wait for Save
   Successful and SD activity to finish.
2. Use the game's ABXY+Start return to the original menu. Choose PHANTASY
   STAR II without powering off. Confirm the card is present and save there.
3. Use ABXY+Start again, choose SHINING FORCE, and load its saved progress.
4. Save one controller setting. Power off, restart and check both RPGs'
   progress and that setting.

Each selected game receives a fresh worker admitted against the current
shared card; pending writes cannot be retired. Return is intercepted before
the old SDK's cleanup. The launcher prepares two checked capsules for this.
Loader reads are batched by physical run with every block CRC retained.
The native stage uses the established fast full-file CRC and cached private
SHA verification. Console elapsed time remains unmeasured; report if the
previous minute-or-two wait remains and the last visible launch message.

If a blue diagnostic appears, photograph every cycling page and report the
last action and selected game. Power off to leave a diagnostic screen.
This new return route still needs the complete console sequence above.

OTHER GENESIS GAMES
All ten native menu selections get the same A1 save backend. The engine
saves cartridge SRAM when that cartridge declares it and changes it; its
Save Game command does not create arbitrary gameplay savestates. Controller
settings use the separate shared SMASH_01.CFG file. Individual high-score
persistence still needs checking; this build does not establish that every
game stores high scores. The Genesis-save-check helper collects only the
ten small ROM headers needed to classify SRAM support. Run it beside the
original GDI if we need the remaining metadata; it does not change the image.

CONFIRMED CE BUILDS
This test targets Genesis selections. To resume the already confirmed
Virtua Cop 2 or Sega Swirl test, copy the matching Restore-confirmed-VC2/KUI
or Restore-confirmed-Swirl/KUI folder over the SD installation. Those files
are byte-identical to the confirmed packages. The shared card is retained.

Restore-first-launch-17b7/KUI restores the previous native-menu build
byte-for-byte. It retains the already reported slow first launch and missing
card on a later selection; it is a comparison restore.

Source and linked/host checks pass; the new return path and console timing
still require testing. Source contains the full delta from main {main_commit},
the delta from recovered tested-menu tree {baseline}, and a Git bundle
requiring that main base. The recovered baseline is exactly the delivered
{TESTED_MENU[:12]} source tree; its historical commit object was unavailable
and has not been relabeled or recreated.
'''.encode()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in files.items():
            archive.writestr(name, data)
    with zipfile.ZipFile(args.output) as archive, tempfile.TemporaryDirectory(prefix='smash-menu-delivery-') as folder:
        if archive.testzip() is not None:
            raise ValueError('Output ZIP failed CRC verification')
        archive.extractall(folder)
        for name, data in files.items():
            if (Path(folder) / name).read_bytes() != data:
                raise ValueError('Extracted output differs: ' + name)
    print(json.dumps(dict(status='PASS', file=str(args.output),
        bytes=args.output.stat().st_size, sha256=sha(args.output.read_bytes()),
        commit=commit, extract_verified=True, hardware_verified=False)))


if __name__ == '__main__':
    main()
