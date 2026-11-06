#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Create and extract-verify the paired late-loaded VC2 readonly VMU test."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import zipfile
import zlib

from check_vc2_ce_vmu_package import audit
from runtime_package import flatten_elf, verify

ROOT = Path(__file__).resolve().parents[1]
MAIN = 'a2b23b3e3d8c1769f1a78ae897aa84e8cc9f4cd7'
HELPER_BYTES = 49152


def sha(data):
    return hashlib.sha256(data).hexdigest()


def wire(value):
    return (json.dumps(value, indent=2) + '\n').encode()


def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT)


def linked_hashes(proof, build):
    assert proof['status'] == 'PASS', 'Linked transition proof did not pass'
    hashes = proof['artifacts_sha256']
    required = {'resident-sci.elf', 'helper/helper.elf', 'helper/prepared.bin', 'vc2-raw.bin'}
    assert isinstance(hashes, dict) and required.issubset(hashes), 'Incomplete late-loader artifact binding'
    for name, digest in hashes.items():
        path = Path(name)
        assert not path.is_absolute() and '..' not in path.parts, 'Invalid proof artifact path'
        assert sha((build / path).read_bytes()) == digest, 'Stale linked proof: ' + name


def installed_module(module, elf, commit):
    data = module.read_bytes()
    metadata = verify(data)
    assert metadata['build'] == commit[:12], 'Mismatched install build: ' + str(module)
    payload, memory = flatten_elf(elf.read_bytes())
    assert data[64:] == payload and metadata['memory_bytes'] == memory, 'Module/ELF mismatch'
    provenance = json.loads(elf.with_suffix('.compile.json').read_text())
    assert provenance['commit'] == commit and not provenance['source_dirty'], 'Build source is not the clean final commit'
    assert provenance['elf_sha256'] == sha(elf.read_bytes()), 'Stale compile provenance'
    return data, provenance


def paired_helper(module, prepared):
    assert len(prepared) == HELPER_BYTES, 'Late helper must contain all 48 KiB'
    payload = module[64:]
    capability = struct.pack('<8I', 0x3146474b, 0x574f4c46, 1, 32, 0, 48, 0, 2)
    assert payload.count(capability) == 1, 'Late capability must be unique flag2/48'
    prefix = struct.pack('<4I', 0x32554d56, 0x44414f4c, 1, 44)
    assert payload.count(prefix) == 1, 'Late helper seed is missing or ambiguous'
    at = payload.index(prefix)
    expected = struct.pack('<11I', 0x32554d56, 0x44414f4c, 1, 44,
                           HELPER_BYTES, zlib.crc32(prepared), 0, 0, 0, 0, 0)
    assert at % 4 == 0 and payload[at:at + 44] == expected, 'Late helper and blank module seed differ'
    return dict(bytes=len(prepared), sha256=sha(prepared), crc32=f'{zlib.crc32(prepared):08x}',
                seed_payload_offset=at, capability_flags=2, map_slots=48)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=ROOT / 'build/vc2-ce-vmu-late')
    parser.add_argument('--linked-proof', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    assert args.output.suffix == '.zip', 'Use a ZIP output filename'
    assert not git('status', '--porcelain'), 'Commit source before packaging'
    commit = git('rev-parse', 'HEAD').decode().strip()
    stamp = commit[:12]
    subprocess.run(['git', 'merge-base', '--is-ancestor', MAIN, commit], cwd=ROOT, check=True)
    package_proof = audit(args.build)
    assert package_proof['status'] == 'PASS' and package_proof.get('late_loader'), 'Not a checked late-loader build'
    assert package_proof['capability']['flags'] == 2 and package_proof['capability']['slots'] == 48
    assert package_proof.get('hardware_verified') is False and package_proof.get('persistent_writes') is False
    linked = json.loads(args.linked_proof.read_text())
    linked_hashes(linked, args.build)
    files = {
        'LICENSE.txt': (ROOT / 'LICENSE').read_bytes(),
        'source/change-from-main-a2b23b3.patch': git('diff', '--binary', MAIN, commit),
        'source/package_vc2_ce_vmu_late_test.py': Path(__file__).read_bytes(),
        'evidence/late-package.json': wire(package_proof),
        'evidence/linked-transition.json': args.linked_proof.read_bytes(),
        'evidence/owner-source.json': (ROOT / 'docs/evidence/vc2-ce-vmu-owner.json').read_bytes(),
        'evidence/runtime-geometry.json': (ROOT / 'docs/evidence/vc2-ce-vmu-runtime-geometry.json').read_bytes(),
    }
    for target, module, elf, label in (
        ('KUI/runtime.kui', args.build / 'runtime.kui', ROOT / 'build/kui-runtime.elf', 'runtime'),
        ('KUI/apps/games/retail-physical.kui', args.build / 'retail-native-foreground.kui',
         args.build / 'entry.elf', 'late-reader'),
        ('KUI/apps/disc/native-disc.kui', ROOT / 'build/disc-native/native-disc.kui',
         ROOT / 'build/disc-native/entry.elf', 'disc'),
    ):
        data, provenance = installed_module(module, elf, commit)
        files[target] = data
        files['evidence/' + label + '.compile.json'] = wire(provenance)
    helper = (args.build / 'helper/prepared.bin').read_bytes()
    pair = paired_helper(files['KUI/apps/games/retail-physical.kui'], helper)
    files['KUI/apps/games/vc2-vmu-helper.bin'] = helper
    files['README.txt'] = f'''K-UI VC2 late-loaded readonly VMU test {stamp}

Copy this package's KUI folder onto the existing SD installation, replacing
the matching files. There is no installer script to run.

Install ALL FOUR files together: runtime.kui, apps/games/retail-physical.kui,
apps/disc/native-disc.kui and apps/games/vc2-vmu-helper.bin. The helper is paired
with this reader; mixing files from earlier test packages is refused.

This package includes main {MAIN}, including the current GD-Play, GD-ripper
and recovery-comparison changes. Use the same Sega Smash Pack SD image.

TEST
1. Remove physical VMUs while leaving the controller connected.
2. Use Native/Default boot. Leave virtual VMU, background reading,
   CD music and RAM patches OFF in the launcher.
3. Launch Sega Smash Pack, continue past any outer no-card prompt, then
   select Virtua Cop 2. This reader injects its own blank readonly A2 in VC2.
4. The intended checkpoint is the first completed virtual-card read and
   finalization. Photograph BOTH cycling pages headed
   "VC2 VMU 1 OF 2 READ ONLY" and "VC2 VMU 2 OF 2 CONTEXT".
   Each page stays visible for eight seconds. Then power off.

If a "VC2 STOP - THREE PAGES" screen appears, photograph ALL THREE
numbered pages. PAGE shows 1, 2 and 3; they cycle every eight seconds. That
screen reports an earlier loader refusal, before the VMU checkpoint.
For another early launch screen, photograph it once. If no diagnostic appears,
report the last visible stage and whether the SD LED is active.

WHAT CHANGED
The helper is read from its separately checked SD file at the VC2 handoff,
after native startup has finished using the previous staging area. K-UI maps
that small file read-only before launch; game files and their TOC are unchanged.
Its allocation must fit at most two physical runs. A missing, changed or more
fragmented helper is refused before handoff. No files are repaired or rewritten.

This test presents an empty 200-block A2 card. It does not load your existing
virtual cards or persist game saves. Detection and reads are the current goal;
durable saving follows after this checkpoint succeeds on the console.
The working reader, full track list and zero speculative read-ahead remain.

Host/machine checks passed; console compatibility is not yet established.
The linked proof uses an authored raw-SD API/data-arrival boundary and does not
certify peripheral timing or complete hardware behavior. No retail game files
are included. The source patch applies to the main commit stated above.
'''.encode()
    files['BUILD.json'] = wire(dict(
        commit=commit, main_included=MAIN, hardware_verified=False,
        purpose='VC2 late-loaded blank readonly A2 integration test',
        persistent_writes=False, virtual_card='generated blank readonly 200-block A2',
        read_ahead_bytes=0, capability_flags=2, map_slots=48,
        helper=pair, helper_loading='read-only SD file at the admitted pre-NK transition',
        expected_checkpoint='first read finalization',
        failure_screen='VC2 LOAD STOP - THREE PAGES',
        validation_limits='authored raw-SD API/data arrival boundary; hardware timing unverified',
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
                          main_included=MAIN, helper=pair, extract_verified=True)))


if __name__ == '__main__':
    main()
