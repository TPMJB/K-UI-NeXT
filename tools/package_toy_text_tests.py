#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build two isolated Toy TXT banks; request one Observer console capture."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import zipfile

from build_native_cdda_compat import TOY
from check_decision_baselines import compare_bytes, entry_shape
from check_native_cdda_compat import check_directory, config
from check_toy_report_export import check as check_export
from check_loader_layout import inspect_elf
from package_fmv_tests import checked_module
from runtime_package import flatten_elf, verify

ROOT = Path(__file__).resolve().parents[1]
BASE = 'aa8ef45f6145f82b33fd06b1334f2154ac116cdc'
GUI = '41d46c85954d53973f524dc54521a09f950db8c5'
IDS = {1: 'd20261104031', 2: 'd20261104032'}


def sha(value):
    return hashlib.sha256(value).hexdigest()


def wire(value):
    return (json.dumps(value, indent=2, sort_keys=True) + '\n').encode()


def git(*args, cwd=ROOT):
    return subprocess.check_output(['git', *args], cwd=cwd)


def pair(clean, observer):
    normalized = []
    reports = []
    for profile, directory in ((1, clean), (2, observer)):
        image = inspect_elf((directory/'worker.elf').read_bytes(), 0x8cfd0000,
            0x8cfe0000, private_p2=True, entry_at_base=False,
            entry_symbol='_kui_toy_pilot_initialize')
        payload = bytearray(image['payload'])
        symbols = image['symbols']
        allowed = []
        for symbol, extra in (('_decision_enabled', 0), ('_kui_toy_report_binding', 12)):
            offset = symbols[symbol]-0x20000000-0x8cfd0000+extra
            if struct.unpack_from('<I', payload, offset)[0] != profile:
                raise ValueError('Missing initialized profile word: '+symbol)
            payload[offset:offset+4] = bytes(4)
            allowed.append(offset)
        stamp = IDS[profile].encode()+b'\0'
        if not payload.count(stamp):
            raise ValueError('Missing linked test build identity')
        payload = payload.replace(stamp, bytes(13))
        for symbol in ('_kui_toy_loader_trace_begin', '_kui_toy_loader_trace_end',
                '_kui_toy_loader_data_probe_begin', '_kui_toy_loader_schedule_report_begin'):
            if symbol in symbols:
                raise ValueError('Legacy hot observer linked: '+symbol)
        normalized.append((symbols, payload))
        reports.append(dict(profile=profile, allowed_profile_offsets=allowed,
            export=check_export(directory)))
    if normalized[0] != normalized[1]:
        raise ValueError('B/C worker differs beyond two profile words and build ID')
    low = compare_bytes('B/C low reader', (clean/'resident-sci.bin').read_bytes(),
        (observer/'resident-sci.bin').read_bytes(), IDS[1], IDS[2])
    return dict(profiles=reports, identical_worker_except_two_profile_words_and_build_id=True,
        unchanged_low_reader=low, console_tested=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gui-root', required=True, type=Path)
    parser.add_argument('--baseline-root', required=True, type=Path)
    parser.add_argument('--recording-evidence', required=True, type=Path)
    parser.add_argument('--publication-evidence', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args()
    if not 1 <= args.jobs <= 16:
        parser.error('--jobs must be 1..16')
    for root, expected in ((ROOT, None), (args.gui_root, GUI), (args.baseline_root, BASE)):
        if git('status', '--porcelain', cwd=root).strip():
            parser.error('Source must be committed and clean: '+str(root))
        if expected and git('rev-parse', 'HEAD', cwd=root).decode().strip() != expected:
            parser.error('Wrong pinned source: '+str(root))
    commit = git('rev-parse', 'HEAD').decode().strip()
    evidence = ROOT/'build/toy-text-evidence'
    evidence.mkdir(parents=True, exist_ok=True)
    files, profiles = {}, {}

    def execute(command, name, cwd=ROOT):
        log = evidence/(name+'.log')
        with log.open('w') as output:
            result = subprocess.run(command, cwd=cwd, stdout=output, stderr=subprocess.STDOUT)
        if result.returncode:
            raise ValueError('Failed: '+str(log))
        files['evidence/'+log.name] = log.read_bytes()

    for test in ('test_toy_report_launcher', 'test_toy_report_bank'):
        execute(['python3', 'tests/'+test+'.py'], 'gui-'+test, cwd=args.gui_root)
    for test in ('test_toy_decision_observer', 'test_toy_report_text',
            'test_report_file_admission', 'test_report_sci_pio',
            'test_report_sd_storage', 'test_toy_report_export', 'test_report_read_window'):
        execute(['python3', 'tests/'+test+'.py'], test)

    for selected in (1, 2):
        build_id = IDS[selected]
        label = 'clean' if selected == 1 else 'observer'
        modules = {}
        for cooked in (False, True):
            geometry = 'cooked' if cooked else 'raw'
            title = 'toy-text-'+str(selected)+'-'+geometry
            directory = ROOT/('build/'+title)
            reference = ROOT/('build/'+title+'-exact')
            flags = list(TOY)+['BUILD_ID='+build_id, 'TOY_DECISION_PROFILE='+str(selected),
                'FMV_TEST=1', 'FMV_IDLE_REARM=0', 'TOY_DEMAND_TEST_PROFILE=0',
                'WORD_REVERSE_PROFILE=0', 'TOY_TEXT_REPORT=1']
            if cooked:
                flags += ['COOKED_DATA_PROFILE=1']
            for compat, destination in ((0, reference), (1, directory)):
                execute(['make', '-f', 'Makefile.toy_pilot',
                    'BUILD='+str(destination.relative_to(ROOT)), 'NATIVE_COMPAT='+str(compat),
                    *flags, '-j'+str(args.jobs), 'all'], title+'-compat'+str(compat))
            data = checked_module(directory, 'retail-toy-pilot.kui', build_id, commit)
            modules[geometry] = directory/'retail-toy-pilot.kui'
            proof = dict(admission=check_directory(directory, 'toy-'+geometry, reference),
                terminal=check_export(directory))
            files['evidence/'+title+'.json'] = wire(proof)
            files['evidence/'+title+'-config.txt'] = (directory/'build-config').read_bytes()
            # Compile default-off from the final source and compare every loaded
            # byte with the previously reviewed decision build. Do not install it.
            control = ROOT/('build/'+title+'-export-off')
            old = args.baseline_root/('build/decision-toy-'+str(selected)+'-'+geometry)
            off = [flag for flag in flags if not flag.startswith('TOY_TEXT_REPORT=')]
            execute(['make', '-f', 'Makefile.toy_pilot',
                'BUILD='+str(control.relative_to(ROOT)), 'NATIVE_COMPAT=1',
                *off, 'TOY_TEXT_REPORT=0', '-j'+str(args.jobs), 'all'], title+'-export-off')
            old_id = config(old)['BUILD']
            if entry_shape(old) != entry_shape(control):
                raise ValueError('Default-off entry load shape changed')
            parity = {}
            for name in ('worker', 'resident-sci', 'stage', 'entry'):
                a, _ = flatten_elf((old/(name+'.elf')).read_bytes())
                b, _ = flatten_elf((control/(name+'.elf')).read_bytes())
                parity[name] = compare_bytes('Default-off '+name, a, b, old_id, build_id)
            files['evidence/'+title+'-default-off-parity.json'] = wire(parity)
            print('Built/audited '+title, flush=True)
        runtime = args.gui_root/('build/toy-report-gui-'+label+'/runtime.kui')
        runtime_elf = runtime.with_name('runtime.elf')
        # GUI filenames are generated by Makefile.dc; identify the adjacent ELF
        # through its clean compilation receipt rather than rebuilding stale objects.
        if not runtime_elf.exists():
            candidates = list(runtime.parent.glob('*.elf'))
            if len(candidates) != 1:
                raise ValueError('Ambiguous GUI ELF')
            runtime_elf = candidates[0]
        receipt = json.loads(runtime_elf.with_suffix('.compile.json').read_text())
        if receipt != dict(commit=GUI, source_dirty=False, elf_sha256=sha(runtime_elf.read_bytes())):
            raise ValueError('GUI compilation provenance mismatch')
        payload, memory = flatten_elf(runtime_elf.read_bytes())
        if runtime.read_bytes()[64:] != payload or verify(runtime.read_bytes())['build'] != build_id:
            raise ValueError('GUI envelope/ELF/ID mismatch')
        kup = evidence/('Toy-'+label.title()+'-TXT-'+build_id+'.kup')
        if kup.exists():
            kup.unlink()  # Generated output only, never an installed bank.
        execute(['python3', 'tools/package_toy_report_bank.py', '--runtime', str(runtime),
            '--toy-raw', str(modules['raw']), '--toy-cooked', str(modules['cooked']),
            '--build-id', build_id, '--output', str(kup)], title+'-bank', cwd=args.gui_root)
        member = 'KUI/updates/'+kup.name
        files[member] = kup.read_bytes()
        profiles[label] = dict(member=member, build_id=build_id, sha256=sha(files[member]),
            replacements=3, inherit_root=23, console_tested=False)
    for geometry in ('raw', 'cooked'):
        files['evidence/toy-'+geometry+'-matched-pair.json'] = wire(pair(
            ROOT/('build/toy-text-1-'+geometry), ROOT/('build/toy-text-2-'+geometry)))
    for name, profile in (('TOYCLEAN.TXT', 'CLEAN'), ('TOYOBS.TXT', 'OBSERVER')):
        head = ('EMPTY TOY '+profile+' REPORT SLOT\nNOT A CURRENT CAPTURE\n').encode()
        footer = b'KUI REPORT INCOMPLETE V1\n'
        files['KUI/reports/'+name] = head+b' '*(32256-len(head))+footer+b' '*(512-len(footer))
    files['README.md'] = (ROOT/'docs/toy-text-tests.md').read_bytes()
    files['tools/analyze_toy_report.py'] = (ROOT/'tools/analyze_toy_report.py').read_bytes()
    files['evidence/recording-identification.json'] = args.recording_evidence.read_bytes()
    for name in ('gui-publication.json', 'core-publication.json'):
        files['evidence/'+name] = (args.publication_evidence/name).read_bytes()
    files['source/K-UI-Toy-TXT-'+commit[:12]+'.tar.gz'] = gzip.compress(
        git('archive', '--format=tar', '--prefix=K-UI-Toy-TXT/', commit), mtime=0)
    files['source/K-UI-Toy-GUI-'+GUI[:12]+'.tar.gz'] = gzip.compress(
        git('archive', '--format=tar', '--prefix=K-UI-Toy-GUI/', GUI, cwd=args.gui_root), mtime=0)
    files['source/core-changes-from-aa8ef45.patch'] = git('diff', BASE, commit)
    files['source/gui-changes-from-332c902.patch'] = git('diff', GUI+'^', GUI, cwd=args.gui_root)
    files['build.json'] = wire(dict(core_source_commit=commit,
        core_source_tree=git('rev-parse', 'HEAD^{tree}').decode().strip(),
        gui_source_commit=GUI,
        gui_source_tree=git('rev-parse', 'HEAD^{tree}', cwd=args.gui_root).decode().strip(),
        fixed_bank_tags_are_not_commit_prefixes=True, source_dirty=False,
        console_tested=False, initial_console_runs=1, profiles=profiles,
        root_runtime_replaced=False, native_vmu_background_prefetch_ce_replaced=False,
        report_slots_bytes=32768, terminal_writes_only=True,
        compiler=subprocess.check_output(['sh-elf-gcc', '--version'], text=True).splitlines()[0]))
    expected = {'KUI/reports/TOYCLEAN.TXT', 'KUI/reports/TOYOBS.TXT',
        *(profile['member'] for profile in profiles.values())}
    if {name for name in files if name.startswith('KUI/')} != expected:
        raise ValueError('Unexpected installed path')
    files['SHA256SUMS'] = ''.join(sha(value)+'  '+name+'\n'
        for name, value in sorted(files.items())).encode()
    if git('status', '--porcelain').strip() or git('status', '--porcelain', cwd=args.gui_root).strip():
        raise ValueError('Source changed during packaging')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, value in sorted(files.items()):
            archive.writestr(name, value)
    with zipfile.ZipFile(args.output) as archive:
        if archive.testzip() is not None or any(archive.read(k) != v for k, v in files.items()):
            raise ValueError('ZIP roundtrip validation failed')
    print(json.dumps(dict(path=str(args.output.resolve()), bytes=args.output.stat().st_size,
        sha256=sha(args.output.read_bytes()), source_commit=commit), indent=2))


if __name__ == '__main__':
    main()
