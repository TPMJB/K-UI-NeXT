#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compose the first universal-loader test while preserving accepted services.

Fourteen newly qualified cold-reader aliases and a current GUI accompany the
six exact accepted Smash files. Ordinary ce-probe.kui is deliberately omitted:
that installed path may own the personalized Armada virtual-VMU adapter. The
partial update preserves its effective software and the other omitted workers.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import subprocess
import tempfile
import zipfile

from build_runtime_module_bundle import locate, decode
from global_loader_rollout import DEFAULT_BUILDS, INSTALL_TARGETS, collect, wire
from make_update import make_update
from package_smash_collection_update import STAGES, HELPERS, linked_member, linked_bundle
from runtime_package import verify as verify_runtime
from smash_ce_profile import profile as helper_profile
from update_package import SELF_KUI, RAW_SOFTWARE, verify

ROOT = Path(__file__).resolve().parents[1]
ACCEPTED_BUILD = 'cc81e63ca4d2'
ACCEPTED_ZIP_SHA256 = 'eb469340a2a6732a356a77536ca8a04a261740c3bca0620e84266f06c204ca6b'
CE_PROBE = 'KUI/apps/games/ce-probe.kui'
SMASH_TARGETS = frozenset('KUI/' + name for name in (*STAGES, *HELPERS.values()))
REPLACEMENTS = frozenset((INSTALL_TARGETS - {CE_PROBE}) | SMASH_TARGETS | {'KUI/runtime.kui'})


def sha(data):
    return hashlib.sha256(data).hexdigest()


def require(ok, message):
    if not ok:
        raise ValueError(message)


def source_state():
    return dict(commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT,
                                               text=True).strip(),
                dirty=bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT)))


def new_output(path, suffix=None):
    path = Path(path).absolute()
    require(not path.exists() and not path.is_symlink() and
            not any(p.is_symlink() for p in path.parents) and
            (suffix is None or path.suffix == suffix),
            'Choose a new regular ' + (suffix or 'evidence directory') + ' output: ' + str(path))
    return path


def accepted_smash(path):
    path = Path(path)
    require(path.is_file() and not path.is_symlink(), 'Accepted Smash archive must be a regular file')
    require(sha(path.read_bytes()) == ACCEPTED_ZIP_SHA256, 'Accepted Smash archive is not the pinned cc81 delivery')
    files = {}
    with zipfile.ZipFile(path) as archive:
        for name in SMASH_TARGETS:
            require(archive.namelist().count(name) == 1, 'Missing or duplicate accepted Smash member: ' + name)
            files[name] = archive.read(name)
    for name in STAGES:
        require(verify_runtime(files['KUI/' + name])['build'] == ACCEPTED_BUILD,
                'Accepted Smash module identity changed: ' + name)
    for kind, name in HELPERS.items():
        raw = files['KUI/' + name]
        child = 'KUI/' + STAGES[2 if kind == 1 else 3]
        require(len(raw) == 49152 and files[child][64:].count(helper_profile(raw, kind, embedded=True)) == 1,
                'Accepted Smash child/helper ownership differs: ' + name)
    return files


def build(runtime, runtime_elf, accepted_zip, output, evidence_dir, *, builds=None):
    initial = source_state()
    require(not initial['dirty'], 'Commit the intended source before producing the universal test')
    commit = initial['commit']
    output, evidence_dir = new_output(output, '.kup'), new_output(evidence_dir)
    require(not output.is_relative_to(evidence_dir) and not evidence_dir.is_relative_to(output),
            'Keep the KUP output separate from its evidence directory')
    gui, gui_link = linked_member(runtime, runtime_elf, commit)
    preserved = accepted_smash(accepted_zip)
    offset, bundle = locate(gui[64:])
    decoded = decode(bundle)
    require(bundle[32:44] == ACCEPTED_BUILD.encode() and set(decoded) == set(STAGES) and
            all(decoded[name] == preserved['KUI/' + name] for name in STAGES),
            'GUI bundle must contain the exact four accepted cc81 Smash modules')
    bundle_link = linked_bundle(runtime_elf, gui[64:], offset, bundle, gui_link['elf_sha256'])
    cold, cold_report = collect(DEFAULT_BUILDS if builds is None else builds, commit,
                                runtime_elf=runtime_elf)
    require(cold_report.get('status') == 'PASS' and cold_report.get('source_commit') == commit and
            {name for name in cold if name.startswith('KUI/')} == INSTALL_TARGETS,
            'Cold-reader qualification has incomplete or foreign-source inventory')
    files = {name: body for name, body in cold.items() if name.startswith('KUI/') and name != CE_PROBE}
    files.update(preserved)
    files['KUI/runtime.kui'] = gui
    require(set(files) == REPLACEMENTS and len(files) == 21,
            'Universal test must replace exactly fourteen cold aliases, the GUI and six accepted Smash files')
    output.parent.mkdir(parents=True, exist_ok=True)
    evidence_dir.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.kui-universal-evidence-', dir=evidence_dir.parent) as temporary:
        evidence = Path(temporary)
        stage = evidence / 'staged/KUI'
        stage.mkdir(parents=True)
        for name, body in files.items():
            destination = evidence / 'staged' / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(body)
        for name, body in cold.items():
            if not name.startswith('evidence/'):
                continue
            relative = PurePosixPath(name)
            require(str(relative) == name and '..' not in relative.parts and '\\' not in name,
                    'Unsafe qualification evidence path')
            destination = evidence / 'qualification' / relative.relative_to('evidence')
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(body)
        candidate = evidence / 'checked-test.kup'
        package = make_update(stage, candidate, channel='testing')
        checked = verify(candidate)
        checked_bytes = candidate.read_bytes()
        expected = {name[4:]: body for name, body in files.items()}
        require(checked == {key: value for key, value in package.items() if key in checked} and
                checked['format'] == 2 and not checked['full'] and checked['engine_minimum'] == 3 and
                len(checked['members']) == 21 and
                {row['path'] for row in checked['members']} == set(expected) and
                all(row['sha256'] == sha(expected[row['path']]) and row['bytes'] == len(expected[row['path']]) and
                    row['policy'] == (SELF_KUI if row['path'].endswith('.kui') else RAW_SOFTWARE)
                    for row in checked['members']),
                'Produced KUP differs from the exact partial replacement inventory')
        require(source_state() == initial, 'Source changed during qualification; refusing a mixed-source test')
        report = dict(schema='kui.universal-loader-test.v1', status='PASS', source_commit=commit,
            source_dirty=False, output=str(output), package_sha256=sha(candidate.read_bytes()),
            package_bytes=candidate.stat().st_size, package=package,
            replacements={name: dict(bytes=len(body), sha256=sha(body)) for name, body in sorted(files.items())},
            accepted_Smash=dict(build=ACCEPTED_BUILD, archive_sha256=ACCEPTED_ZIP_SHA256,
                exact_six_files_preserved=True, GUI_embedded_four_exact=True, linked_bundle=bundle_link),
            GUI_link=gui_link, cold_reader_qualification=cold_report,
            qualified_but_not_installed={CE_PROBE: dict(sha256=sha(cold[CE_PROBE]),
                reason='Preserve the effective personalized Armada virtual-VMU adapter occupying ce-probe.kui')},
            partial_update_preserves_other_effective_software=True,
            Standard_Background_physical_disc_workers_rebuilt=False,
            personalized_card_profile_packaged=False, full_global_rollout=False,
            new_binary_hardware_tested=False, console_test_required=True,
            source_sha256={name: sha((ROOT / name).read_bytes()) for name in (
                'tools/package_universal_loader_test.py', 'tools/global_loader_rollout.py',
                'tools/make_update.py', 'tools/build_runtime_module_bundle.py',
                'tools/package_smash_collection_update.py')})
        (evidence / 'universal-loader-test.json').write_bytes(wire(report))
        (evidence / 'checked-test.kup').unlink()
        # The current producer already checked the exact bytes with the real
        # C consumer. Retain those bytes while publishing the evidence tree.
        with tempfile.NamedTemporaryFile(prefix='.kui-universal-', dir=output.parent, delete=False) as target:
            ready = Path(target.name)
            try:
                # Recreate no package: the exact checked candidate was retained
                # before retiring its temporary evidence-only pathname.
                target.write(checked_bytes)
            except BaseException:
                ready.unlink(missing_ok=True)
                raise
        evidence.replace(evidence_dir)
        ready.replace(output)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', required=True, type=Path)
    parser.add_argument('--runtime-elf', required=True, type=Path)
    parser.add_argument('--accepted-smash', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--evidence-dir', required=True, type=Path)
    args = parser.parse_args()
    try:
        report = build(args.runtime, args.runtime_elf, args.accepted_smash, args.output, args.evidence_dir)
        print(json.dumps(dict(status='PASS', output=report['output'], sha256=report['package_sha256'],
            replacements=len(report['replacements']), evidence=str(args.evidence_dir))))
    except (ValueError, OSError, zipfile.BadZipFile) as error:
        parser.exit(1, str(error) + '\n')


if __name__ == '__main__':
    main()
