#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package matching menu/World Party candidates over the installed CDDA set."""
import argparse
import gzip
import json
from pathlib import Path
import subprocess
import zipfile

from check_armada_ce_excerpt import check as check_linked
from package_armada_excerpt import check, digest, verify_members
from package_armada_ce_stream import inspect_stream_seed
from package_wince_cdda_diagnostic import matched_package
from package_wince_ce_stream import write_zip


ROOT = Path(__file__).resolve().parents[1]
BASE = '1aa16821209bcd9a186c344dcba6223afee7f420'
BASELINE_SHA = '9e867bb8bf93eebbd30f1f77494fe5d4e95000beb2f001f35193513659bbd7e7'
WORKING_WORMS_SHA = '3c39d5b4e149b4ed911f7d4cf1fd19ea750f94a0a783b149cf4c5357cd36651a'
RUNTIME = 'KUI/runtime.kui'
MODULE = 'KUI/apps/games/ce-cdda-wwp.kui'


def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT)


def encoded(value):
    return (json.dumps(value, indent=2, sort_keys=True) + '\n').encode()


def dependencies(path):
    check(path.stat().st_size < 50 * 1024 * 1024 and
          digest(path.read_bytes()) == BASELINE_SHA,
          'Expected the delivered experimental CDDA baseline ZIP')
    with zipfile.ZipFile(path) as archive:
        verify_members(archive)
        check(archive.testzip() is None, 'Baseline CRC failure')
        return {name: archive.read(name) for name in archive.namelist()
                if name in ('LICENSE', 'THIRD_PARTY.md') or
                name.startswith(('LICENSES/', 'source/dependencies/'))}


def build(args):
    commit = git('rev-parse', 'HEAD').decode().strip()
    check(not git('status', '--porcelain'), 'Commit intended candidate source first')
    subprocess.run(['git', 'merge-base', '--is-ancestor', BASE, commit], cwd=ROOT, check=True)
    runtime, runtime_info, runtime_compile, runtime_evidence = matched_package(
        ROOT / 'build/kui-runtime.elf', ROOT / 'build/runtime.kui', commit)
    directory = ROOT / 'build/wwp-ce-stream'
    module, module_info, module_compile, module_evidence = matched_package(
        directory / 'entry.elf', directory / 'ce-cdda-wwp.kui', commit, retail=True)
    seed = inspect_stream_seed(module)
    audit = check_linked(directory, stream=True, wwp=True)
    check(audit['title_profile'] == 'wwp-us' and audit['stream_profile'] is True and
          audit['distributed_pcm_bytes'] == 0 and audit['distributed_audio_map_bytes'] == 4096 and
          seed['wire_all_zero'] is True and
          int(audit['seed_address'], 16) - 0x8ce10000 == seed['stage_offset'],
          'World Party profile, blank source map and linked seed must agree')
    check(audit['native_owner_reader']['kernel_profile'] == 'wwp-us' and
          audit['stream_boot_arm']['attach_only_after_relay'] is True and
          audit['stream_boot_arm']['preserves_controller_zero_check'] is True,
          'World Party ownership/bootstrap audit missing')
    worms = ROOT / 'build/worms-ce-stream/ce-cdda-worms.kui'
    check(digest(worms.read_bytes()) == WORKING_WORMS_SHA,
          'Previously tested Armageddon module changed during preparation')
    files = dependencies(args.baseline)
    files.update({RUNTIME: runtime, MODULE: module,
        'source/K-UI-World-Party-Candidate-' + commit[:12] + '.tar.gz': gzip.compress(
            git('archive', '--format=tar', '--prefix=K-UI-World-Party-Candidate/', commit), mtime=0),
        'source/cdda-release-preparation.patch': git('diff', '--binary', BASE, commit),
        'evidence/runtime-compile.json': runtime_evidence,
        'evidence/wwp-compile.json': module_evidence,
        'evidence/wwp-layout-and-stack.json': encoded(audit)})
    for name in ('experimental-cdda.md', 'cdda-2.0-handoff.md', 'wince-runtime-inventory.md'):
        files['docs/' + name] = (ROOT / 'docs' / name).read_bytes()
    for name in ('world-party-startup-audit-2026-10-15.md',
                 'world-party-kernel-contracts-2026-10-15.md',
                 'world-party-sound-contracts-2026-10-15.md',
                 'world-party-status-restart-2026-10-15.md'):
        files['docs/evidence/' + name] = (ROOT / 'docs/evidence' / name).read_bytes()
    for item in args.evidence:
        name, path = item.split('=', 1)
        check(name and '/' not in name and '\\' not in name and name not in ('.', '..'),
              'Evidence must use a basename')
        destination = 'evidence/' + name
        check(destination not in files, 'Duplicate evidence member')
        check(Path(path).stat().st_size < 16 * 1024 * 1024, 'Oversized evidence file')
        files[destination] = Path(path).read_bytes()
    files['build.json'] = encoded(dict(source_commit=commit, source_dirty=False,
        patch_base=BASE, baseline_zip_sha256=BASELINE_SHA,
        installed_files=[RUNTIME, MODULE], hardware_tested=False, public_release=False,
        world_party_experimental=True, quake_iii_cdda_supported=False,
        preserved_armageddon_sha256=WORKING_WORMS_SHA,
        preserved_armageddon_included=False,
        runtime=dict(envelope=runtime_info, compile=runtime_compile, sha256=digest(runtime)),
        world_party=dict(envelope=module_info, compile=module_compile, sha256=digest(module), seed=seed)))
    files['README.md'] = (f'# World Party CDDA candidate\n\nBuild **{commit[:12]}**.\n\n'
        'With the console off, merge the KUI folder onto the card with the existing '
        'experimental CDDA installation. This candidate supplies the matching menu '
        'and new World Party module; it is an incremental test update for 2.0 preparation.\n\n'
        'Launch Worms World Party from its GDI on SCI microSD. Turn CD music On '
        'and background loading On. Check that the game boots and music continues beyond the '
        'previous five-second restart in the menu or a match. Photograph the '
        'entire error screen on failure.\n\n'
        'This update corrects malformed linked reader calls found in the failing '
        'builds. Every embedded reader passes a direct-call target audit; memory '
        'and stack reservations retain their existing bounds. Music status is '
        'reported only after native playback starts, retaining the correction '
        'for the five-second music restart.\n\n'
        'World Party remains Experimental until its delivered build is tested on '
        'the console. Quake III CD music is still unsupported. Keep the working '
        'Armageddon module installed; it is not an installation member here. '
        'Broader compatibility testing can follow after 2.0.\n\n'
        'Source, merge patch, linked audits, dependency sources and licenses are '
        'included. No game executable, kernel, firmware or audio is distributed.\n').encode()
    check(not git('status', '--porcelain'), 'Source changed during candidate packaging')
    write_zip(args.output, files, installed_files=[RUNTIME, MODULE])
    return dict(path=str(args.output.resolve()), build=commit[:12], bytes=args.output.stat().st_size,
                sha256=digest(args.output.read_bytes()), installed_files=[RUNTIME, MODULE])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--evidence', action='append', default=[], metavar='NAME=PATH')
    args = parser.parse_args()
    print(json.dumps(build(args), indent=2))


if __name__ == '__main__':
    main()
