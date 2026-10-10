#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Produce BASE or BASE_PSRAM_DNP candidate BOM/CPL from actual PCB placements.

Empty unverified catalog IDs remain empty. This does not approve assembly or
invent rotations: the manufacturer placement preview still needs inspection.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import pcbnew

ROOT = Path(__file__).resolve().parents[1]
MFG = ROOT / 'manufacturing'
PCB = ROOT / 'controller/KUI-G1-Bridge-RevA.kicad_pcb'
PARTS = MFG / 'parts-to-buy.csv'

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--variant', choices=['BASE', 'BASE_PSRAM_DNP'], default='BASE')
    args = parser.parse_args()
    omitted = {'U203'} if args.variant == 'BASE_PSRAM_DNP' else set()
    prefix = 'rigid-psram-dnp' if omitted else 'rigid'
    report_name = 'assembly-psram-dnp-draft-report.json' if omitted else 'assembly-draft-report.json'
    rows = list(csv.DictReader(PARTS.open()))
    board = pcbnew.LoadBoard(str(PCB))
    if board is None:
        raise ValueError('Native carrier PCB cannot be loaded')
    footprints = {fp.GetReference(): fp for fp in board.GetFootprints()}
    origin = board.GetDesignSettings().GetAuxOrigin()
    bom, cpl, manual, pending, fixtures = [], [], [], [], []
    jlc_verified = 0
    requested = set()
    base_smt_refs = set()
    for row in rows:
        if int(row['qty_base']) == 0:
            continue
        refs = row['refs_base'].split(',') if row['refs_base'] else []
        # refs_base_only is informational; BASE references already include them.
        if len(refs) != int(row['qty_base']):
            raise ValueError('Procurement BASE quantity differs from actual refs: ' + row['mpn'])
        if row['assembly_method'].startswith('Manual offboard'):
            manual.extend(refs)
            continue
        base_smt_refs.update(refs)
        refs = [ref for ref in refs if ref not in omitted]
        if not refs:
            continue
        for ref in refs:
            if ref in requested:
                raise ValueError('Duplicate assembly reference: ' + ref)
            requested.add(ref)
            if ref not in footprints:
                raise ValueError('Purchased SMT reference absent from actual board: ' + ref)
            fp = footprints[ref]
            if fp.GetLayer() != pcbnew.F_Cu:
                raise ValueError('Assembly candidate expects top-side part: ' + ref)
            pos = fp.GetPosition()
            cpl.append({'Designator': ref,
                        'Mid X': f'{pcbnew.ToMM(pos.x-origin.x):.4f}',
                        'Mid Y': f'{pcbnew.ToMM(origin.y-pos.y):.4f}',
                        'Layer': 'Top', 'Rotation': f'{fp.GetOrientationDegrees()%360:.3f}'})
        actual_fps = {
            str(footprints[r].GetFPID().GetLibNickname()) + ':'
            + str(footprints[r].GetFPID().GetLibItemName()) for r in refs
        }
        if len(actual_fps) != 1:
            raise ValueError('One part row spans unequal physical footprints: ' + row['mpn'])
        bom.append({'Comment': row['mpn'], 'Designator': ','.join(refs),
                    'Footprint': next(iter(actual_fps)), 'LCSC Part #': row['lcsc_code']})
        if row.get('jlc_assembly_status', '').startswith('jlc_smt_catalog_verified'):
            jlc_verified += 1
        if row.get('jlc_fixture_notice') == 'explicit_fixture_requirement':
            fixtures.append({'mpn': row['mpn'], 'refs': refs,
                             'part_code': row['lcsc_code'],
                             'source': row['jlc_assembly_url'],
                             'status': 'assembler_fixture_agreement_pending'})
        if not row['lcsc_code']:
            pending.append({'mpn': row['mpn'], 'refs': refs})
    if not omitted.issubset(base_smt_refs) or requested != base_smt_refs-omitted:
        raise ValueError('Variant must omit only the selected PSRAM reference')
    for name, items, fields in [
        (prefix+'-bom.csv', bom, ['Comment', 'Designator', 'Footprint', 'LCSC Part #']),
        (prefix+'-cpl.csv', sorted(cpl, key=lambda r:r['Designator']), ['Designator', 'Mid X', 'Mid Y', 'Layer', 'Rotation'])
    ]:
        with (MFG / name).open('w', newline='') as stream:
            writer = csv.DictWriter(stream, fieldnames=fields, lineterminator='\n')
            writer.writeheader(); writer.writerows(items)
    report = {
        'status': 'draft_not_released', 'variant': args.variant, 'SMT_parts': len(cpl),
        'omitted_base_refs': sorted(omitted),
        'psram_policy': ('Firmware must not probe or assert CS1/GPIO47; disable PSRAM cache and use SRAM. CS1 pulls and decoupling remain fitted.'
                         if omitted else 'U203 populated; exact-part driver and bandwidth remain unqualified.'),
        'BOM_rows': len(bom), 'manual_offboard_refs': manual,
        'exact_jlc_smt_catalog_rows': jlc_verified,
        'assembly_fixture_requirements': fixtures,
        'unverified_catalog_rows': pending,
        'pcb_sha256': hashlib.sha256(PCB.read_bytes()).hexdigest(),
        'parts_list_sha256': hashlib.sha256(PARTS.read_bytes()).hexdigest(),
        'origin_mm': [pcbnew.ToMM(origin.x), pcbnew.ToMM(origin.y)],
        'coordinates': 'Actual footprint body origin; X right, Y up, mm, relative to native aux origin',
        'rotation_status': 'Native orientation only; manufacturer model zero-angle/placement preview review pending',
        'note': 'Catalog identities are checked; placement rotations, factory fixtures and carrier routing remain unqualified. Do not submit as approved PCBA data.'}
    (MFG / report_name).write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({'variant':args.variant,'SMT_parts':len(cpl),'BOM_rows':len(bom),'pending_catalog_rows':len(pending),'status':report['status']}))

if __name__ == '__main__':
    main()
