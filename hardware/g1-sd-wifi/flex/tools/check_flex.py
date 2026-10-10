#!/usr/bin/env python3
"""Check both native passive flex designs against the original logical CN503 map."""
from __future__ import annotations
import csv
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import xml.etree.ElementTree as ET
import pcbnew
ROOT = Path(__file__).resolve().parents[2]
FLEX = ROOT / 'flex'
CHECKS = FLEX / 'checks'

def run(*args):
    proc = subprocess.run(args, text=True, capture_output=True)
    if proc.returncode:
        raise RuntimeError(f"{' '.join((str(a) for a in args))} failed ({proc.returncode}):\n{proc.stdout}{proc.stderr}")
    return proc.stdout.strip()

def require(condition, message='Native flex invariant failed'):
    if not condition:
        raise AssertionError(message)

def check():
    interface = json.loads((ROOT / 'design/flex-interface.json').read_text())
    original = list(csv.DictReader((ROOT / 'docs/CN503-reference.csv').open()))
    require(len(original) == 50, 'Original map must enumerate all50contacts')
    by_contact = {r['cn503_contact']: r for r in original}
    require(len(by_contact) == 50, 'Duplicate CN503contact')
    useful = {c for c, r in by_contact.items() if r['proposed_signal'].startswith('ATA_') or r['proposed_signal'] in {'CONSOLE_3V3', 'CONSOLE_5V', 'GND'}}
    require(len(useful) == 40, 'Expected28ATA+4supply+8returncontacts')
    excluded = set(by_contact) - useful
    require(excluded == set(interface['excluded_contacts']), 'Excluded contact policy changed')
    require({'A25', 'B25'}.issubset(excluded), 'Both+12Vcontacts must be excluded')
    require(len(interface['connectors']) == 2, 'Native flex invariant failed')
    assigned = []
    CHECKS.mkdir(exist_ok=True)
    results = []
    for c in interface['connectors']:
        row = c['flex_row']
        name = f'KUI-CN503-{row}-Flex-RevA'
        stem = FLEX / name
        pins = c['pins']
        require(c['positions'] == 20 and len(pins) == 20, 'Native flex invariant failed')
        require(c['mpn'] == 'FH12-20S-0.5SH(55)' and c['contact_orientation'] == 'bottom', 'Native flex invariant failed')
        require(sorted(p['pin'] for p in pins) == list(range(1, 21)), 'Each FPC position must appear exactly once')
        require([p['pin'] for p in pins] == (list(range(1, 21)) if row == 'A' else list(range(20, 0, -1))), 'Mirrored B copper must use reversed tail numbers')
        require([int(p['cn503_contact'][1:]) for p in pins] == sorted((int(p['cn503_contact'][1:]) for p in pins)), 'Native flex invariant failed')
        for p in pins:
            contact = p['cn503_contact']
            assigned.append(contact)
            require(contact.startswith(row), 'Native flex invariant failed')
            require(contact in useful and p['signal'] == by_contact[contact]['proposed_signal'], 'Native flex invariant failed')
        erc = CHECKS / f'{row}-erc.json'
        drc = CHECKS / f'{row}-drc.json'
        netlist = CHECKS / f'{row}-netlist.xml'
        run('kicad-cli', 'sch', 'erc', '--format', 'json', '--exit-code-violations', '--output', str(erc), str(stem.with_suffix('.kicad_sch')))
        run('kicad-cli', 'pcb', 'drc', '--format', 'json', '--exit-code-violations', '--schematic-parity', '--output', str(drc), str(stem.with_suffix('.kicad_pcb')))
        run('kicad-cli', 'sch', 'export', 'netlist', '--format', 'kicadxml', '--output', str(netlist), str(stem.with_suffix('.kicad_sch')))
        e = json.loads(erc.read_text())
        d = json.loads(drc.read_text())
        require(not [v for s in e['sheets'] for v in s['violations']], 'Native flex invariant failed')
        require(not d['violations'] and (not d['unconnected_items']) and (not d['schematic_parity']), 'Native flex invariant failed')
        xml = ET.parse(netlist).getroot()
        actual = {n.attrib['name']: {(z.attrib['ref'], z.attrib['pin']) for z in n.findall('node')} for n in xml.find('nets')}
        expected = {f"/CN503_{p['cn503_contact']}_{p['signal']}": {('J1', p['cn503_contact'][1:]), ('J2', str(p['pin']))} for p in pins}
        require(actual == expected, f'{row}:native schematic mapping differs from originalCN503/carrier contract')
        # This is a physical handedness check, independent of the logical
        # schematic/netlist checks above. A mirrored tail cannot be inserted
        # into the same bottom-contact socket by silently reversing numbers.
        board = pcbnew.LoadBoard(str(stem.with_suffix('.kicad_pcb')))
        tail = next(fp for fp in board.GetFootprints() if fp.GetReference() == 'J2')
        physical_pads = sorted(tail.Pads(), key=lambda p: p.GetPosition().x)
        require([p.GetNumber() for p in physical_pads] == [str(i) for i in range(1, 21)], f'{row}: tail mating positions must run pin1 left to pin20 right with tip north')
        require(c['pin1_side_top_view_with_tip_north'] == 'left', f'{row}: interface handedness contradicts native tail')
        require(len({p.GetPosition().y for p in physical_pads}) == 1, f'{row}: tail fingers must be collinear')
        require(all(p.GetLayerSet().Contains(pcbnew.B_Cu) and not p.GetLayerSet().Contains(pcbnew.F_Cu) for p in physical_pads), f'{row}: exposed mating fingers must face the bottom-contact socket')
        require(all(abs(pcbnew.ToMM(b.GetPosition().x-a.GetPosition().x)-.5) < .000001 for a,b in zip(physical_pads, physical_pads[1:])), f'{row}: mating finger pitch must be0.50mm')
        physical_mapping = [{'pin': int(p.GetNumber()), 'x_mm': pcbnew.ToMM(p.GetPosition().x), 'y_mm': pcbnew.ToMM(p.GetPosition().y), 'net': p.GetNetname()} for p in physical_pads]
        sources = [stem.with_suffix(ext) for ext in ['.kicad_pcb', '.kicad_sch', '.kicad_pro', '.kicad_dru']] + [ROOT / 'design/flex-interface.json', ROOT / 'docs/CN503-reference.csv', FLEX / 'KUI_Flex.kicad_sym', FLEX / 'sym-lib-table', FLEX / 'fp-lib-table'] + sorted((FLEX / 'KUI_Flex.pretty').glob('*.kicad_mod'))
        results.append({'row': row, 'conductors': len(actual), 'erc_violations': 0, 'drc_violations': 0, 'unconnected_items': 0, 'schematic_parity_issues': 0, 'native_mapping_matches_original_logical_reference': True, 'physical_tail_pin1_left_pin20_right': True, 'physical_mating_datum': 'F.Cu/top viewed from above, tip north, B.Cu gold fingers down; same handedness as unrotated Hirose bottom-contact socket', 'physical_tail_mapping_left_to_right': physical_mapping, 'source_sha256': {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}})
    require(len(assigned) == len(set(assigned)) == 40 and set(assigned) == useful, 'Native flex invariant failed')
    summary = {'kicad_version': run('kicad-cli', 'version'), 'status': 'CAD consistency checks pass; installed hardware qualification is pending', 'routing_complete': True, 'motherboard_continuity_qualified': False, 'installed_route_qualified': False, 'signal_integrity_qualified': False, 'jlc_production_files_approved': False, 'arms': results}
    (CHECKS / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print('Both flex arms: ERC0, DRC0, unconnected0, schematic parity0; all40contacts match original logical reference.')
    print('No hardware, fit, rail-current or signal-integrity qualification is implied.')
if __name__ == '__main__':
    try:
        check()
    except (AssertionError, RuntimeError) as error:
        print(f'FlexCAD check failed: {error}', file=sys.stderr)
        sys.exit(1)
