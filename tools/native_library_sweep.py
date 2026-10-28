#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Sweep an owner's Library-startup parts through K-UI's native launch gates.

Checks and unpacks the parts written by kui-library-startup-bundle.py into a
private work folder, runs each bundled startup through the production
structural corpus audit and the SDK SCI family finder, and writes a JSON
report and a Markdown table: VMU and New, New only, or the first blocking
gate. The parts are untrusted: no name from them becomes a path, sizes are
checked before reading, and no game bytes enter the report. Structural results
are necessary evidence, not console compatibility.
"""
import argparse
from collections import Counter
import hashlib
import io
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
KNOWN = ROOT / 'docs/evidence/native-console-known.json'
SCHEMA = 'kui.native-library-startup.v1'
SLUG = re.compile(r'[0-9]{3,4}-[A-Za-z0-9-]{0,56}')
ARCHIVE = re.compile(r'games/([0-9]{3,4}-[A-Za-z0-9-]{0,56})\.zip')
RUN_ID = re.compile(r'[0-9a-f]{32}')
LIBRARY_LIMIT = 8 * 1024 * 1024
INNER_LIMIT = 16 * 1024 * 1024
BOOT_LIMIT = 12 * 1024 * 1024
GDI_LIMIT = 64 * 1024
INFO_LIMIT = 4 * 1024 * 1024
STATUSES = ('bundled', 'duplicate', 'windows_ce', 'cdda', 'error', 'other_format')
SCI_SOURCES = ('tools/native_sci_family_probe.c', 'src/core/native_sdk_sci_family.c',
               'src/core/hash.c', 'src/core/data.c')
GATE_NOTES = {
    'extent': 'executable extent not proved',
    'driver': 'Maple driver family not resolved',
    'layout': 'Maple layout not resolved',
    'heap': 'SDK heap family not resolved',
    'initializer_slot': 'no heap initializer slot (fixed RAM; needs a RAM map)',
    'initializer_call': 'heap initializer call not proved',
    'aligned_hook_entries': 'hook entries not aligned',
    'release_hook_shape': 'heap release shape differs',
    'exits': 'terminal exits not resolved',
    'guarded_exits': 'terminal exits overlap hook windows',
    'combined_resolution': 'launch record refused',
    'placement_384k': '384 KiB lease does not fit',
    'transport_execution': 'Maple transport model did not run',
    'live_bind_384k': 'live layout bind refused',
}


class SweepError(ValueError):
    """An uploaded part breaks the bundle contract."""


def bounded_read(archive, info, limit, label):
    if info.flag_bits & 1 or info.file_size > limit:
        raise SweepError(f'{label} is encrypted or larger than {limit:,} bytes')
    data = archive.read(info)
    if len(data) != info.file_size:
        raise SweepError(f'{label} size differs from its ZIP header')
    return data


def read_parts(paths):
    """Merged library rows and the inner game ZIPs present in the parts."""
    libraries, blobs = {}, {}
    for path in paths:
        with zipfile.ZipFile(path) as archive:
            infos = archive.infolist()
            names = [info.filename for info in infos]
            if not names or names[0] != 'library.json' or len(set(names)) != len(names):
                raise SweepError(f'{path.name}: expected library.json first and unique names')
            if any(not ARCHIVE.fullmatch(name) for name in names[1:]):
                raise SweepError(f'{path.name}: unexpected entry name')
            library = json.loads(bounded_read(archive, infos[0], LIBRARY_LIMIT, 'library.json'))
            if (library.get('schema') != SCHEMA or not RUN_ID.fullmatch(str(library.get('run_id'))) or
                    type(library.get('part')) is not int or type(library.get('parts')) is not int or
                    not 1 <= library['part'] <= library['parts'] <= 999 or
                    type(library.get('games')) is not list):
                raise SweepError(f'{path.name}: not a library startup part')
            if library['part'] in libraries:
                raise SweepError(f'{path.name}: part {library["part"]} was given twice')
            libraries[library['part']] = library
            for info, name in zip(infos[1:], names[1:]):
                blobs[name] = (library['part'], bounded_read(archive, info, INNER_LIMIT, name))
    first = next(iter(libraries.values()))
    for library in libraries.values():
        if (library['run_id'], library['parts'], library['games']) != \
                (first['run_id'], first['parts'], first['games']):
            raise SweepError('Parts come from different bundler runs')
    rows = first['games']
    slugs = set()
    for row in rows:
        if type(row) is not dict or row.get('status') not in STATUSES or type(row.get('source')) is not str:
            raise SweepError('Malformed library row')
        if row['status'] != 'bundled':
            continue
        if (not SLUG.fullmatch(str(row.get('slug'))) or row['slug'] in slugs or
                row.get('archive') != f"games/{row['slug']}.zip" or type(row.get('part')) is not int or
                not re.fullmatch(r'[0-9a-f]{64}', str(row.get('boot_sha256'))) or
                not re.fullmatch(r'[0-9a-f]{64}', str(row.get('ip_sha256'))) or
                type(row.get('boot_path')) is not str):
            raise SweepError('Malformed bundled row')
        slugs.add(row['slug'])
        if row['part'] in libraries:
            if blobs.get(row['archive'], (None,))[0] != row['part']:
                raise SweepError(f"{row['archive']} is missing from part {row['part']}")
        elif row['archive'] in blobs:
            raise SweepError(f"{row['archive']} is in the wrong part")
    referenced = {row['archive'] for row in rows if row['status'] == 'bundled'}
    if set(blobs) - referenced:
        raise SweepError('A part holds a game ZIP that no row names')
    missing = sorted(set(range(1, first['parts'] + 1)) - set(libraries))
    return first, rows, {name: blob for name, (_, blob) in blobs.items()}, missing


def open_game(blob, row):
    """The checked boot file of one single-game bundle."""
    with zipfile.ZipFile(io.BytesIO(blob)) as archive:
        infos = archive.infolist()
        expected = ['IP.BIN', 'disc.gdi', row['boot_path'], 'startup-info.json']
        if [info.filename for info in infos] != expected:
            raise SweepError(f"{row['slug']}: game ZIP entries differ from the bundle layout")
        ip = bounded_read(archive, infos[0], 32768, 'IP.BIN')
        gdi = bounded_read(archive, infos[1], GDI_LIMIT, 'disc.gdi')
        boot = bounded_read(archive, infos[2], BOOT_LIMIT, 'boot file')
        report = json.loads(bounded_read(archive, infos[3], INFO_LIMIT, 'startup-info.json'))
    files = {entry.get('path'): entry for entry in report.get('files', []) if type(entry) is dict}
    if (len(ip) != 32768 or report.get('windows_ce') is not False or
            report.get('boot_path') != row['boot_path'] or
            hashlib.sha256(boot).hexdigest() != row['boot_sha256'] or
            hashlib.sha256(ip).hexdigest() != row['ip_sha256'] or
            any(files.get(name, {}).get('sha256') != hashlib.sha256(data).hexdigest()
                for name, data in (('IP.BIN', ip), ('disc.gdi', gdi), (row['boot_path'], boot)))):
        raise SweepError(f"{row['slug']}: game ZIP contents differ from its hashes")
    return boot


def stage_end_from_elf(path):
    sys.path[:0] = [str(ROOT / 'tools')]
    from check_vmu_native_worker_gateway import read_elf
    symbols = read_elf(path)[4]
    if '__retail_stage_bss_end' not in symbols:
        raise SweepError('The stage ELF has no __retail_stage_bss_end')
    return symbols['__retail_stage_bss_end'][0]


def sci_families(startups, work):
    executable = work / 'sci-family-probe'
    command = [*shlex.split(os.environ.get('HOST_CC', 'cc')), '-std=c11', '-O2', '-Wall', '-Wextra',
               '-Werror', '-Wpedantic', '-I', str(ROOT / 'include'),
               *[str(ROOT / name) for name in SCI_SOURCES], '-o', str(executable)]
    subprocess.run(command, check=True)
    result = {}
    for slug, path in startups.items():
        run = subprocess.run([str(executable), str(path)], capture_output=True, text=True)
        result[slug] = json.loads(run.stdout) if run.returncode == 0 else {'sdk_sci_family': None}
    return result


def verdict(row):
    """What New (X) does with this game now, by the launcher's own order."""
    status = row['status']
    if status != 'bundled':
        return {'duplicate': 'same startup as ' + str(row.get('duplicate_of')),
                'windows_ce': 'Windows CE (separate reader)',
                'cdda': 'CD audio (out of scope)',
                'error': 'not read: ' + str(row.get('error')),
                'other_format': 'not a GDI (' + str(row.get('format')) + ')',
                'not_uploaded': 'part not uploaded'}[status], 'not_native'
    family = row.get('sdk_sci_family') == 1
    if row.get('structural_384k_models_passed') and family:
        return 'VMU + New' + ('' if row.get('console') else ' (structural; console test next)'), 'vmu_and_new'
    if not row.get('combined_resolution'):
        if family:
            return 'New only; VMU blocked: ' + GATE_NOTES.get(row.get('first_refusal'), str(row.get('first_refusal'))), 'new_only'
        return 'neither; ' + GATE_NOTES.get(row.get('first_refusal'), str(row.get('first_refusal'))) + '; no SDK SCI family', 'blocked'
    if not family:
        return 'neither; launch record resolves but no SDK SCI family', 'blocked'
    return ('neither; the VMU module is chosen, then refuses: ' +
            GATE_NOTES.get(row.get('first_refusal'), str(row.get('first_refusal')))), 'blocked'


def table(rows):
    order = {'vmu_and_new': 0, 'new_only': 1, 'blocked': 2, 'not_native': 3}
    lines = ['| # | Title | Product | Boot | Now | Console |', '| ---: | --- | --- | ---: | --- | --- |']
    for row in sorted(rows, key=lambda r: (order[r['class']], str(r.get('title') or r['source']).upper())):
        title = str(row.get('title') or row['source']).replace('|', '/')
        boot = f"{row['boot_bytes']:,}" if row.get('boot_bytes') else ''
        lines.append(f"| {row.get('slug', '').split('-')[0]} | {title} | {row.get('product', '')} | {boot} | "
                     f"{row['now']} | {row.get('console', '')} |")
    return '\n'.join(lines) + '\n'


def sweep(parts, work, stage_end, output, markdown, known=KNOWN):
    library, rows, blobs, missing = read_parts(parts)
    console = {game['source_sha256']: game['console']
               for game in json.loads(Path(known).read_text())['games']}
    startups = {}
    (work / 'startups').mkdir(parents=True, exist_ok=True)
    for row in rows:
        if row['status'] == 'bundled' and row['part'] in missing:
            row['status'] = 'not_uploaded'
        if row['status'] != 'bundled':
            continue
        path = work / 'startups' / (row['slug'] + '.bin')
        path.write_bytes(open_game(blobs[row['archive']], row))
        startups[row['slug']] = path
    structural = {}
    if startups:
        manifest = {'schema': 'kui.private.native-startup-inputs.v1', 'files': [
            {'slug': row['slug'], 'startup': str(startups[row['slug']]), 'source_bytes': row['boot_bytes'],
             'source_sha256': row['boot_sha256'], 'title': row['title'],
             'boot_filename': row['boot_filename'], 'windows_ce': False}
            for row in rows if row['slug'] in startups]}
        (work / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
        subprocess.run([sys.executable, str(ROOT / 'tools/audit_vmu_native_corpus.py'),
                        '--manifest', str(work / 'manifest.json'), '--stage-end', hex(stage_end),
                        '--output', str(work / 'structural.json')], check=True)
        audit = json.loads((work / 'structural.json').read_text())
        structural = {row['slug']: row for row in audit['results']}
        families = sci_families(startups, work)
    keep = ('first_refusal', 'structural_384k_models_passed', 'combined_resolution', 'fixed_arena',
            'driver', 'layout', 'heap', 'initializer_slot', 'initializer_call', 'exits',
            'guarded_exits', 'placement_384k', 'live_bind_384k', 'image_bytes', 'probe_returncode')
    results = []
    for row in rows:
        out = {key: row[key] for key in ('source', 'status', 'slug', 'title', 'product', 'version',
                                         'region', 'boot_filename', 'boot_bytes', 'boot_sha256',
                                         'cdda_tracks', 'duplicate_of', 'error', 'format')
               if key in row}
        if row.get('slug') in startups:
            out.update({key: structural[row['slug']].get(key) for key in keep})
            out.update(families[row['slug']])
        if row.get('boot_sha256') in console:
            out['console'] = console[row['boot_sha256']]
        out['now'], out['class'] = verdict(out)
        results.append(out)
    report = {'schema': 'kui.native.library-sweep.v1', 'run_id': library['run_id'],
              'parts': library['parts'], 'missing_parts': missing, 'stage_end': stage_end,
              'counts': dict(Counter(row['class'] for row in results)),
              'first_refusals': dict(Counter(row.get('first_refusal') or 'models_passed'
                                             for row in results if row['class'] != 'not_native')),
              'limits': ['Structural gates and the SCI family finder are necessary, not sufficient: '
                         'VMU + New still needs a console save, reboot and load.',
                         'Rows read only from the uploaded startup files; no game bytes are stored here.'],
              'results': results}
    output.write_text(json.dumps(report, indent=2) + '\n')
    markdown.write_text(table(results))
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('parts', type=Path, nargs='+', help='Library-startup-NN.zip files')
    parser.add_argument('--work', type=Path, required=True, help='Private folder for unpacked startups')
    end = parser.add_mutually_exclusive_group(required=True)
    end.add_argument('--stage-end', type=lambda value: int(value, 0))
    end.add_argument('--stage-elf', type=Path, help='Dual stage ELF; reads __retail_stage_bss_end')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--table', type=Path, required=True)
    args = parser.parse_args()
    stage_end = args.stage_end if args.stage_end is not None else stage_end_from_elf(args.stage_elf)
    args.work.mkdir(parents=True, exist_ok=True)
    try:
        report = sweep(args.parts, args.work, stage_end, args.output, args.table)
    except (SweepError, zipfile.BadZipFile, json.JSONDecodeError) as error:
        parser.exit(1, f'Sweep stopped: {error}\n')
    print(json.dumps(report['counts']))


if __name__ == '__main__':
    main()
