#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Create and extract-verify the VC2 native-writer diagnostic package."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import zipfile

from check_vc2_ce_vmu_watch import audit
from runtime_package import flatten_elf, verify

ROOT = Path(__file__).resolve().parents[1]
MAIN = 'a2b23b3e3d8c1769f1a78ae897aa84e8cc9f4cd7'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def wire(value):
    return (json.dumps(value, indent=2) + '\n').encode()


def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT)


def check_hashes(proof, build):
    hashes = proof['artifacts_sha256']
    assert isinstance(hashes, dict) and hashes, 'Proof has no artifact binding'
    assert 'resident-sci.elf' in hashes, 'Proof does not bind the actual low entry'
    for name, digest in hashes.items():
        relative = Path(name)
        assert not relative.is_absolute() and '..' not in relative.parts, 'Invalid proof path'
        assert sha((build / relative).read_bytes()) == digest, 'Stale proof: ' + name


def install_file(module, elf, commit):
    data = module.read_bytes()
    metadata = verify(data)
    assert metadata['build'] == commit[:12], 'Mismatched install build: ' + str(module)
    payload, memory = flatten_elf(elf.read_bytes())
    assert data[64:] == payload and metadata['memory_bytes'] == memory, 'Module/ELF mismatch'
    provenance = json.loads(elf.with_suffix('.compile.json').read_text())
    assert provenance['commit'] == commit and not provenance['source_dirty'], 'Uncommitted build'
    assert provenance['elf_sha256'] == sha(elf.read_bytes()), 'Stale compile provenance'
    return data, provenance


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=ROOT / 'build/vc2-ce-vmu-watch')
    parser.add_argument('--entry-proof', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    assert args.output.suffix == '.zip', 'Use a ZIP output filename'
    assert not git('status', '--porcelain'), 'Commit source before packaging'
    commit = git('rev-parse', 'HEAD').decode().strip()
    stamp = commit[:12]
    subprocess.run(['git', 'merge-base', '--is-ancestor', MAIN, commit], cwd=ROOT, check=True)
    package_proof = audit(args.build)
    assert package_proof['status'] == 'PASS', 'Linked watch-package audit failed'
    check_hashes(package_proof, args.build)
    entry = json.loads(args.entry_proof.read_text())
    assert entry['status'] == 'PASS', 'Native debug-entry test did not pass'
    check_hashes(entry, args.build)
    files = {
        'LICENSE.txt': (ROOT / 'LICENSE').read_bytes(),
        'source/change-from-main-a2b23b3.patch': git('diff', '--binary', MAIN, commit),
        'source/package_vc2_ce_vmu_watch.py': Path(__file__).read_bytes(),
        'source/vc2-ce-vmu-write-watch-test.md': (ROOT / 'docs/vc2-ce-vmu-write-watch-test.md').read_bytes(),
        'evidence/watch-package.json': wire(package_proof),
        'evidence/watch-entry.json': args.entry_proof.read_bytes(),
    }
    for target, module, elf, provenance_name in (
        ('KUI/runtime.kui', args.build / 'runtime.kui', ROOT / 'build/kui-runtime.elf', 'runtime'),
        ('KUI/apps/games/retail-physical.kui', args.build / 'retail-native-foreground.kui',
         args.build / 'entry.elf', 'watch'),
        ('KUI/apps/disc/native-disc.kui', ROOT / 'build/disc-native/native-disc.kui',
         ROOT / 'build/disc-native/entry.elf', 'disc'),
    ):
        data, provenance = install_file(module, elf, commit)
        files[target] = data
        files['evidence/' + provenance_name + '.compile.json'] = wire(provenance)
    files['README.txt'] = f'''K-UI VC2 native write-watch diagnostic {stamp}

Copy this package's KUI folder onto the existing SD installation, replacing
the matching files. There is no installer script to run.

This diagnostic watches a native CPU write at the first staged-helper address.
It includes main {MAIN}, including the current GD-Play, GD-ripper and recovery
comparison changes. It does not present a virtual VMU or save game data.

TEST
1. Remove physical VMUs, leaving the controller connected.
2. Use Native/Default boot. Leave virtual VMU, background reading,
   CD music and RAM patches OFF in the launcher.
3. Launch the same Sega Smash Pack SD image and select Virtua Cop 2 if its
   menu appears. A write-watch stop BEFORE that menu is useful too.
4. At a captured write, photograph BOTH alternating diagnostic pages:
   "VC2 WRITE WATCH 1 OF 2 REGS" and "VC2 WRITE WATCH 2 OF 2 CONTEXT".
   Each page stays visible for eight seconds. Then power off.

The pages contain all 36 captured CPU words. Each row starts with a hexadecimal
index followed by four words. Page 1 covers indices 0-15; page 2 covers 16-35.
No PC, PR or stack memory windows are read. The original binaries already
supplied can be used to resolve the captured PC and PR after the test.
The complete index mapping is in source/vc2-ce-vmu-write-watch-test.md.

"VC2 WATCH NO TRAP - ONE PAGE" is a different, single-page result.
Photograph that whole page once; it will not cycle. Photograph any earlier
launch/GD STOP screen once instead. If no diagnostic appears, report the
last visible stage and whether the SD LED is active.

SCOPE
The watch covers the helper's first address through P1/P2 aliases while SR.BL
is clear. It cannot capture writes elsewhere in the helper, BL-masked writes,
P0-alias writes or hardware DMA.
A trap is not guaranteed. The test preserves the owner's SR and VBR; a
captured write is terminal and the game does not resume afterwards.
No game save, virtual-card or other SD writes are performed by this module.

This is a writer diagnostic, not a VMU compatibility or save-capable build.
The 48-slot mapping correction, full track list and working reader remain.
Host/machine checks passed; console watch acceptance remains unverified.
No retail game files are included in this package.
'''.encode()
    files['BUILD.json'] = wire(dict(
        commit=commit, main_included=MAIN, hardware_verified=False,
        purpose='VC2 staged-helper native CPU writer diagnostic',
        persistent_writes=False, virtual_card=False, read_ahead_bytes=0,
        expected_checkpoint='admitted native CPU write at helper entry with SR.BL clear',
        watch_addresses=['0x8cfc4000', '0xacfc4000'],
        capture_words=36, memory_windows=False,
        diagnostic_pages=['VC2 WRITE WATCH 1 OF 2 REGS', 'VC2 WRITE WATCH 2 OF 2 CONTEXT'],
        no_hit_result='VC2 WATCH NO TRAP - ONE PAGE',
        watch_limits=['other helper addresses', 'BL-masked writes', 'P0-alias writes', 'hardware DMA'],
        installed={name: dict(bytes=len(data), sha256=sha(data))
                   for name, data in files.items() if name.startswith('KUI/')},
    ))
    files['SHA256SUMS.txt'] = ''.join(sha(data) + '  ' + name + '\n'
                                      for name, data in sorted(files.items())).encode()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output.with_suffix('.zip.tmp')
    with zipfile.ZipFile(temporary, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in sorted(files.items()):
            archive.writestr(name, data)
    with zipfile.ZipFile(temporary) as archive:
        assert archive.testzip() is None
        with tempfile.TemporaryDirectory() as directory:
            archive.extractall(directory)
            for name, data in files.items():
                assert (Path(directory) / name).read_bytes() == data
    subprocess.run(['unzip', '-t', str(temporary)], check=True, stdout=subprocess.DEVNULL)
    temporary.replace(args.output)
    print(json.dumps(dict(status='PASS', file=str(args.output), bytes=args.output.stat().st_size,
                          sha256=sha(args.output.read_bytes()), commit=commit,
                          main_included=MAIN, extract_verified=True)))


if __name__ == '__main__':
    main()
