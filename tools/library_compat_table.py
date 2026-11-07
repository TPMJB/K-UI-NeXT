#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""One compatibility row per game in the owner's library-startup uploads.

Reads every Library-startup part (v1 and v2, any number of bundler runs),
runs each native startup through the production checks the Background (X/Y)
launch uses, in the launcher's own order, and joins the console results
already recorded. Background with virtual VMU needs the compact 284 KiB
structural gates (or a reviewed fixed arena) and the SDK SCI family; when it
refuses, New falls back to the standalone reader if the SCI family exists.
Ordinary Standard launches with VMU Off have no static gate and are not rows.

The parts are untrusted: they are read through native_library_sweep's checked
reader, and no game bytes enter the report. Static results are necessary
evidence, not console compatibility.
"""
import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools')]
import native_library_sweep as sweep  # noqa: E402
from audit_vmu_native_corpus import audit  # noqa: E402
from native_runtime_layout import BACKGROUND_COMPACT  # noqa: E402

CONSOLE = ROOT / 'docs/evidence/library-console-results.json'
ARENA_SOURCES = ('tools/native_arena_plan_probe.c', 'src/core/vmu_native_heap.c',
                 'src/core/vmu_native_program.c', 'src/core/vmu_native_exit.c',
                 'src/core/vmu_native_patch.c', 'src/core/hash.c', 'src/core/data.c')
STAGE_BEGIN = 0x8ce00000
NOTES = dict(sweep.GATE_NOTES, placement_384k='284 KiB lease does not fit',
             live_bind_384k='live layout bind refused',
             sdk_sci_family='no SDK SCI family (reader interrupt route)')


def product_key(product):
    """IP product number without the version suffix some headers carry."""
    return str(product or '').split(' ')[0].upper()


V1_COOKED = 'not read: Requires original raw2352 GDI tracks with zero file offsets'


def group_runs(paths):
    """Parts grouped by bundler run, so each run is read as one checked set."""
    runs = {}
    for path in paths:
        with zipfile.ZipFile(path) as archive:
            info = archive.infolist()[0]
            if info.filename != 'library.json':
                raise sweep.SweepError(f'{path.name}: expected library.json first')
            library = json.loads(sweep.bounded_read(archive, info, sweep.LIBRARY_LIMIT, 'library.json'))
        runs.setdefault(str(library.get('run_id')), []).append(path)
    return list(runs.values())


def arena_plans(startups, work):
    executable = work / 'arena-plan-probe'
    subprocess.run([*shlex.split(os.environ.get('HOST_CC', 'cc')), '-std=c11', '-O2', '-Wall', '-Wextra',
                    '-Werror', '-Wpedantic', '-I', str(ROOT / 'include'),
                    *[str(ROOT / name) for name in ARENA_SOURCES], '-o', str(executable)], check=True)
    result = {}
    for key, path in startups.items():
        run = subprocess.run([str(executable), str(path)], capture_output=True, text=True)
        result[key] = json.loads(run.stdout).get('arena_plan') is True if run.returncode == 0 else False
    return result


def routes(gates, family, arena):
    """(Background X/Y with virtual VMU, without VMU, first blocker) as the launcher decides."""
    structural = bool(gates.get('structural_384k_models_passed'))
    arena_route = arena and gates.get('driver') == 1 and gates.get('layout') and gates.get('exits')
    if (structural or arena_route) and family:
        return 'virtual VMU' + (' (fixed arena)' if arena_route and not structural else ''), '—', None
    blocker = None if structural or arena_route else gates.get('first_refusal')
    if not family:
        blocker = blocker or 'sdk_sci_family'
        return 'refused', 'refused', blocker
    return 'refused', 'New, no virtual VMU', blocker


def build(parts, work, stage_end, console_path=CONSOLE):
    work.mkdir(parents=True, exist_ok=True)
    rows, startups, runs = [], {}, []
    (work / 'startups').mkdir(exist_ok=True)
    for group in group_runs(parts):
        library, merged, blobs, missing = sweep.read_parts(group)
        runs.append(dict(run_id=library['run_id'], schema=library['schema'],
                         parts=library['parts'], missing_parts=missing))
        for row in merged:
            row = dict(row)
            if row['status'] == 'bundled' and row['part'] in missing:
                row['status'] = 'not_uploaded'
            if row['status'] == 'bundled' and not row.get('windows_ce'):
                key = row['boot_sha256']
                if key not in startups:
                    path = work / 'startups' / (key[:16] + '.bin')
                    path.write_bytes(sweep.open_game(blobs[row['archive']], row))
                    startups[key] = path
            rows.append(row)
    gates, families, arenas = {}, {}, {}
    if startups:
        report = audit([(key[:16], path) for key, path in startups.items()], STAGE_BEGIN, stage_end,
                       selected_layout=BACKGROUND_COMPACT)
        by_slug = {row['slug']: row for row in report['results']}
        gates = {key: by_slug[key[:16]] for key in startups}
        families = {key: value.get('sdk_sci_family') == 1
                    for key, value in sweep.sci_families(startups, work).items()}
        arenas = arena_plans(startups, work)
    console = {product_key(game['product']): game
               for game in json.loads(Path(console_path).read_text())['games']}
    games, seen = [], {}
    for row in rows:
        out = {key: row[key] for key in ('source', 'status', 'title', 'product', 'version', 'region',
                                         'disc', 'boot_filename', 'boot_bytes', 'boot_sha256',
                                         'windows_ce', 'cdda_tracks', 'duplicate_of', 'error', 'format')
               if key in row}
        key = row.get('boot_sha256')
        if key in startups and row['status'] in ('bundled', 'already_sent') and not row.get('windows_ce'):
            if key in seen:
                out.update(status='duplicate', duplicate_of=seen[key])
            else:
                seen[key] = row.get('slug') or row.get('title') or row['source']
                g = gates[key]
                out.update({name: g.get(name) for name in ('first_refusal', 'structural_384k_models_passed',
                                                           'driver', 'layout', 'heap', 'fixed_arena', 'exits',
                                                           'placement_384k', 'live_bind_384k', 'exit_family',
                                                           'layout_version')})
                out['sdk_sci_family'] = families[key]
                out['arena_plan'] = arenas[key]
                out['background_vmu'], out['without_vmu'], blocker = routes(g, families[key], arenas[key])
                out['blocker'] = blocker
                out['class'] = ('background_vmu' if out['background_vmu'] != 'refused' else
                                'new_only' if out['without_vmu'] != 'refused' else 'blocked')
        if 'class' not in out:
            out['class'] = 'not_checked'
            out['reason'] = (
                'Windows CE: separate reader; kernel profile check pending' if row.get('windows_ce') else
                'same startup as ' + str(row.get('duplicate_of')) if out['status'] == 'duplicate' else
                'startup not received yet' if out['status'] in ('windows_ce', 'cdda', 'already_sent', 'not_uploaded') else
                'not read: ' + str(row.get('error')) if out['status'] == 'error' else
                'image format not read (' + str(row.get('format')) + ')' if out['status'] == 'other_format' else
                out['status'])
            if out['status'] == 'cdda':
                out['reason'] = 'native with CD audio: startup not received yet'
            if out['status'] == 'windows_ce':
                out['reason'] = 'Windows CE: startup not received yet'
        if product_key(row.get('product')) in console:
            out['console'] = console[product_key(row.get('product'))]
        games.append(out)
    return dict(schema='kui.library-compat-table.v1', runs=runs, stage_end=hex(stage_end),
                layout='compact Background 72 KiB worker / 284 KiB lease',
                counts=dict(Counter(game['class'] for game in games)),
                blockers=dict(Counter(game['blocker'] for game in games if game.get('blocker'))),
                limits=['Static gates and the SCI family finder are necessary, not sufficient; '
                        'the console decides.',
                        'Standard (A) with virtual VMU uses a 72 KiB placement that has no reviewed host model; '
                        'its discovery gates are the same as Background.',
                        'No game bytes are stored in this report.'],
                games=games)


def cell(text):
    return str(text if text is not None else '').replace('|', '/')


def markdown(report):
    order = {'background_vmu': 0, 'new_only': 1, 'blocked': 2}
    checked = sorted((g for g in report['games'] if g['class'] in order),
                     key=lambda g: (order[g['class']], str(g.get('title')).upper()))
    lines = ['| Game | Product | Boot KiB | Background X/Y + virtual VMU | Without VMU | Blocker | Console (recorded) |',
             '| --- | --- | ---: | --- | --- | --- | --- |']
    def recorded(console):
        if not console:
            return ''
        text = f"{console['result']} ({console['route']})" if console.get('result') else ''
        if console.get('known_blocker'):
            text += ('; ' if text else '') + 'known limit: ' + console['known_blocker']
        return text
    for g in checked:
        console = g.get('console')
        lines.append(f"| {cell(g.get('title'))} | {cell(g.get('product'))} | {g['boot_bytes'] // 1024:,} | "
                     f"{g['background_vmu']} | {g['without_vmu']} | {cell(NOTES.get(g.get('blocker'), g.get('blocker') or ''))} | "
                     f"{cell(recorded(console))} |")
    rest, seen, cooked, other = [], set(), 0, 0
    for g in sorted((g for g in report['games'] if g['class'] == 'not_checked'),
                    key=lambda g: str(g.get('title') or g['source']).upper()):
        if g['status'] == 'other_format':
            other += 1
            continue
        if g['reason'] == V1_COOKED:
            cooked += 1  # the v1 bundler's 2048-byte copies; v2 reads them
            continue
        key = (g.get('title'), product_key(g.get('product')), g['reason'])
        if key not in seen:
            seen.add(key)
            rest.append(g)
    if rest:
        lines += ['', '| Not checked yet | Product | Why | Console (recorded) |', '| --- | --- | --- | --- |']
        for g in rest:
            lines.append(f"| {cell(g.get('title') or Path(g['source']).name)} | {cell(product_key(g.get('product')))} | "
                         f"{cell(g['reason'])} | {cell(recorded(g.get('console')))} |")
    if cooked or other:
        lines += ['', f'Also listed: {cooked} 2048-byte GDI copies the v1 bundler could not read '
                  f'(the v2 bundler reads them) and {other} files in other image formats.']
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('parts', type=Path, nargs='+', help='Library-startup-NN.zip files from any runs')
    parser.add_argument('--work', type=Path, required=True, help='Private folder for unpacked startups')
    end = parser.add_mutually_exclusive_group(required=True)
    end.add_argument('--stage-end', type=lambda value: int(value, 0))
    end.add_argument('--stage-elf', type=Path, help='Compact Background stage ELF')
    parser.add_argument('--console', type=Path, default=CONSOLE)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--table', type=Path, required=True)
    args = parser.parse_args()
    stage_end = args.stage_end if args.stage_end is not None else sweep.stage_end_from_elf(args.stage_elf)
    try:
        report = build(args.parts, args.work, stage_end, args.console)
    except (sweep.SweepError, zipfile.BadZipFile, json.JSONDecodeError) as error:
        parser.exit(1, f'Table stopped: {error}\n')
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    args.table.write_text(markdown(report))
    print(json.dumps(report['counts']))


if __name__ == '__main__':
    main()
