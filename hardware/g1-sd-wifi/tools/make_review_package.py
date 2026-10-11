#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build an unapproved review package of the carrier and both flex arms.

The release exporter (manufacturing/prepare_jlcpcb_release.py --export)
refuses to run until the owner has accepted the design reviews, which is
correct. This makes the same artwork for that review: per-layer Gerbers and
Excellon drill exactly as the exporter plots them (one isolated export per
layer, drill origin at the aux origin), the BASE BOM/CPL candidates, 3D
renders and the native DRC/ERC reports, under a directory whose README says
it is not an order package. Run with KiCad 10's kicad-cli on PATH or in
KICAD10_CLI; 3D renders use KICAD10_3DMODEL_DIR when set.
"""
import argparse
import datetime
import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
MFG = ROOT / 'manufacturing'
BOARDS = {
    'rigid': (ROOT / 'controller' / 'KUI-G1-Bridge-RevA.kicad_pcb', MFG / 'rigid-order-settings.json'),
    'flex_a': (ROOT / 'flex' / 'KUI-CN503-A-Flex-RevA.kicad_pcb', MFG / 'flex-order-settings.json'),
    'flex_b': (ROOT / 'flex' / 'KUI-CN503-B-Flex-RevA.kicad_pcb', MFG / 'flex-order-settings.json'),
}
RENDERS = [('top', 'top', ''), ('bottom', 'bottom', ''), ('iso', 'top', '-30,0,25')]


def cli(*args, log=None):
    exe = os.environ.get('KICAD10_CLI', 'kicad-cli')
    proc = subprocess.run([exe] + [str(a) for a in args], text=True, capture_output=True)
    if log:
        log.write_text(proc.stdout + proc.stderr)
    if proc.returncode:
        raise RuntimeError('kicad-cli %s failed:\n%s' % (' '.join(map(str, args[:3])), (proc.stdout + proc.stderr)[-2000:]))
    return proc


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--output', type=pathlib.Path, required=True)
    ap.add_argument('--skip-3d-renders', action='store_true', help='Plot artwork/checks without optional package-model renders')
    args = ap.parse_args()
    if args.output.exists():
        raise SystemExit('Output exists; choose a new directory.')
    out = args.output
    out.mkdir(parents=True)
    summary = {'generated_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(timespec='seconds'),
               'approved_for_order': False, 'three_d_renders_included': not args.skip_3d_renders, 'boards': {}}
    for name, (pcb, settings_path) in BOARDS.items():
        settings = json.loads(settings_path.read_text())
        folder = out / name
        checks = folder / 'checks'
        gerbers = folder / 'gerbers'
        checks.mkdir(parents=True)
        gerbers.mkdir()
        sch = pcb.with_suffix('.kicad_sch')
        cli('sch', 'erc', '--severity-all', '--format', 'json', '--output', checks / 'erc.json', sch)
        cli('pcb', 'drc', '--severity-all', '--schematic-parity', '--all-track-errors', '--refill-zones',
            '--format', 'json', '--output', checks / 'drc.json', pcb)
        drc = json.loads((checks / 'drc.json').read_text())
        erc = json.loads((checks / 'erc.json').read_text())
        for index, layer in enumerate(settings['gerber_layers']):
            tmp = folder / ('layer-%02d' % index)
            tmp.mkdir()
            cli('pcb', 'export', 'gerbers', '--layers', layer, '--no-protel-ext', '--check-zones',
                '--use-drill-file-origin', '--output', str(tmp) + '/', pcb)
            art = list(tmp.glob('*.gbr'))
            if len(art) != 1 or not art[0].stat().st_size:
                raise RuntimeError('%s: layer %s did not export one nonempty Gerber' % (name, layer))
            shutil.copyfile(art[0], gerbers / settings.get('layer_filenames', {}).get(layer, art[0].name))
            shutil.rmtree(tmp)
        cli('pcb', 'export', 'drill', '--format', 'excellon', '--drill-origin', 'plot', '--excellon-units', 'mm',
            '--excellon-separate-th', '--output', str(gerbers) + '/', pcb)
        zpath = folder / ('%s-gerbers-REVIEW-ONLY.zip' % name)
        with zipfile.ZipFile(zpath, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
            for path in sorted(gerbers.iterdir()):
                archive.write(path, path.name)
        if name == 'rigid':
            for f in ('rigid-bom.csv', 'rigid-cpl.csv', 'rigid-psram-dnp-bom.csv', 'rigid-psram-dnp-cpl.csv'):
                shutil.copyfile(MFG / f, folder / f)
            renders = folder / 'renders'
            renders.mkdir()
            for label, side, rotate in ([] if args.skip_3d_renders else RENDERS):
                extra = ['--rotate', rotate, '--perspective'] if rotate else []
                cli('pcb', 'render', '-o', renders / ('%s.png' % label), '-w', 1600, '-h', 2400 if not rotate else 1600,
                    '--side', side, '--quality', 'high', '--background', 'opaque', *extra, pcb)
        summary['boards'][name] = {
            'pcb_sha256': sha256(pcb),
            'gerbers': sorted(p.name for p in gerbers.iterdir()),
            'drc_violations': len(drc['violations']), 'unconnected': len(drc['unconnected_items']),
            'schematic_parity': len(drc.get('schematic_parity', [])),
            'erc_violations': sum(len(s['violations']) for s in erc['sheets']),
        }
    (out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    (out / 'README.md').write_text(
        '# REVIEW PACKAGE: NOT APPROVED FOR ORDER\n\n'
        'Artwork for inspection only (for example in a Gerber viewer or the fabricator\'s online preview). '
        'It is plotted the same way as the release exporter, but the owner reviews in '
        '`manufacturing/review-evidence.json` are not accepted, so `prepare_jlcpcb_release.py --export` '
        'still refuses to build an order package. Order only from that exporter\'s output.\n\n'
        'Native check results for each board are in `summary.json` and each `checks/` folder.\n')
    print(json.dumps(summary['boards'], indent=1))


if __name__ == '__main__':
    main()
