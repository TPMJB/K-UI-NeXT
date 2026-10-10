#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Generate reviewable native KiCad sheets from the audited physical pin contracts.

No fabricated symbol pins, automatic power flags, or layout/ERC exclusions.
Run with system Python after the component-block authors have completed their work.
The source block JSONs remain the physical/electrical source of truth.
"""
import argparse
import collections
import copy
import json
import math
import os
import pathlib
import re
import shutil
import uuid

ROOT = pathlib.Path(__file__).resolve().parents[1]
CAD = ROOT / 'controller'
NAME = 'KUI-G1-Bridge-RevA'
NAMESPACE = uuid.UUID('d0cfb36b-9f23-5d7c-ad82-05c63133d921')
# KiCad 10 standard footprints. An older system library (for example KiCad 7
# on stock Ubuntu 24.04) must never overwrite the localized 10.x footprints.
FOOTPRINT_DIR = pathlib.Path(os.environ.get('KICAD10_FOOTPRINT_DIR', '/usr/share/kicad/footprints'))

def uid(s):
    return str(uuid.uuid5(NAMESPACE, str(s)))

def q(s):
    return json.dumps(str(s), ensure_ascii=False)

def load_components():
    blocks = {}
    for name in ['frontend', 'controller', 'power-bus', 'bios']:
        data = json.loads((ROOT / 'design/blocks' / (name + '.json')).read_text())
        blocks[name] = copy.deepcopy(data if isinstance(data, list) else data['components'])
    fpc = json.loads((ROOT / 'design/flex-interface.json').read_text())
    blocks['cn503-interface'] = []
    for conn in fpc['connectors']:
        pins = {}
        for p in conn['pins']:
            signal = p['signal']
            if signal == 'CONSOLE_5V':
                signal = 'FLEX_5V'
            pins[str(p['pin'])] = {'name': p['cn503_contact'], 'net': signal, 'type': 'passive'}
        # These pads are mechanical hold-downs. They are intentionally grounded.
        pins['MP'] = {'name': 'shield_anchor', 'net': 'GND', 'type': 'passive'}
        blocks['cn503-interface'].append({
            'ref': conn['carrier_reference'], 'value': conn['mpn'], 'mpn': conn['mpn'],
            'footprint': conn['footprint'], 'pins': pins, 'populate': True,
            'height_max_mm': 2.0, 'sources': [fpc['connector_primary_source']],
            'description': 'Independent CN503 ' + conn['flex_row'] + ' arm. FLEX_5V has no connection to bridge supply.'})
    # Power sources behind passive inductors need explicit ERC source declarations.
    # These flags are justified by external supplies and the three regulators;
    # they do not imply that a rail has passed bench qualification.
    flag_nets = ['GND', 'CONSOLE_5V', 'CONSOLE_3V3', '+1V1_MCU',
                 '+3V3_LOGIC', '+3V3_STORAGE', 'RESERVE_5V',
                 'PRIMARY_5V', 'MCU_VREG_AVDD']
    blocks['power-source-declarations'] = [{
        'ref': '#FLG' + str(i), 'value': 'PWR_FLAG ' + net, 'mpn': '',
        'footprint': '', 'pins': {'1': {'name': 'power_source', 'net': net, 'type': 'power_out'}},
        'populate': True, 'in_bom': False,
        'description': 'ERC source: ' + net + '; reserve source exists only in reserve population.'
    } for i, net in enumerate(flag_nets, 1)]
    seen = set()
    for group, parts in blocks.items():
        for part in parts:
            if part['ref'] in seen:
                raise ValueError('Duplicate reference: ' + part['ref'])
            seen.add(part['ref'])
            part['block'] = group
            part.setdefault('populate', True)
            part.setdefault('variant', 'BASE')
    return blocks

def localize_footprints(blocks):
    lib = CAD / 'KUI_Footprints.pretty'
    lib.mkdir(exist_ok=True)
    copied = []
    missing = []
    for part in (p for b in blocks.values() for p in b):
        fp = part.get('footprint', '')
        if not fp:
            part['on_board'] = False
            continue
        nick, name = fp.split(':', 1)
        destination = lib / (name + '.kicad_mod')
        if nick not in ('KUI_Footprints', 'KUI_G1', 'KUI'):
            source = FOOTPRINT_DIR / (nick + '.pretty') / destination.name
            if not source.exists():
                missing.append(str(source))
                continue
            shutil.copyfile(source, destination)
            # Record the standard KiCad 10 install location, not a local mirror.
            canonical = pathlib.Path('/usr/share/kicad/footprints') / (nick + '.pretty') / destination.name
            copied.append({'source': str(canonical), 'local': str(destination.relative_to(ROOT))})
        elif not destination.exists():
            missing.append(fp)
        part['source_footprint'] = fp
        part['footprint'] = 'KUI_Footprints:' + name
        part['on_board'] = True
    if missing:
        raise ValueError('Missing physical footprints: ' + '; '.join(missing))
    (CAD / 'fp-lib-table').write_text('(fp_lib_table\n (lib (name "KUI_Footprints")(type "KiCad")(uri "${KIPRJMOD}/KUI_Footprints.pretty")(options "")(descr "Self-contained bridge footprints"))\n)\n')
    (CAD / 'FOOTPRINT-PROVENANCE.json').write_text(json.dumps(copied, indent=2) + '\n')

def make_symbol(c):
    pins = list(c['pins'].items())
    # Physical terminals, not aliases: all package pins appear once in each symbol.
    left = pins[:math.ceil(len(pins) / 2)]
    right = pins[len(left):]
    simple = len(pins) <= 2
    halfwidth = 3.81 if simple else 13.97
    height = max(2.54, (len(left) - 1) * 2.54 / 2 + 2.54)
    placements = []
    lines = [f'(symbol {q(c["ref"])} (pin_names (offset 0.508)) (in_bom yes) (on_board yes)',
             f' (property "Reference" {q(re.sub(r"\d", "", c["ref"]))} (at 0 {height + 2.54:g} 0) (effects (font (size 1.27 1.27))))',
             f' (property "Value" {q(c["value"])} (at 0 {height + 5.08:g} 0) (effects (font (size 1.27 1.27))))',
             f' (property "Footprint" {q(c.get("footprint", ""))} (at 0 0 0) (effects (font (size 1.27 1.27)) hide))',
             f' (symbol {q(c["ref"] + "_0_1")} (rectangle (start {-halfwidth:g} {height:g}) (end {halfwidth:g} {-height:g}) (stroke (width 0.254) (type default)) (fill (type background))))',
             f' (symbol {q(c["ref"] + "_1_1")}']
    for side, group in [(-1, left), (1, right)]:
        for i, (number, info) in enumerate(group):
            x = side * (halfwidth + 2.54)
            y = (len(group) - 1) * 1.27 - i * 2.54
            name = info['name'].split(' / ')[0]
            if len(name) > 20:
                name = name[:20]
            kind = info.get('type', 'passive')
            lines.append(f'  (pin {kind} line (at {x:g} {y:g} {0 if side == -1 else 180}) (length 2.54) (name {q(name)} (effects (font (size 0.85 0.85)))) (number {q(number)} (effects (font (size 0.85 0.85)))))')
            placements.append((number, info, x, -y, side))
    lines.extend([' )', ')'])
    return '\n'.join(lines), placements, height

def partition_sheets(blocks):
    sheets = []
    for group, parts in blocks.items():
        column = 0
        y = 31.75
        page = 1
        entries = []
        for c in parts:
            symbol, pins, h = make_symbol(c)
            needed = 2 * h + 12.7
            if y + needed > 276:
                column += 1
                y = 31.75
            if column == 4:
                sheets.append((f'{group}-{page}', entries))
                page += 1
                column = 0
                entries = []
            entries.append((c, symbol, pins, h, 55.88 + column * 101.6, y + h + 5.08))
            y += needed
        sheets.append((f'{group}-{page}', entries))
    return sheets

def schematic_header(identifier, title):
    return [f'(kicad_sch (version 20230121) (generator "eeschema") (uuid {uid(identifier)}) (paper "A3")',
            f' (title_block (title {q(title)}) (date "2026-10-10") (rev "A design draft") (company "K-UI NeXT") (comment 1 "Circuit implementation; prototype validation pending"))']

def label(text, x, y, side, key):
    # Label arrow points into its stub; text extends away from the component.
    return f'(global_label {q(text)} (shape bidirectional) (at {x:g} {y:g} {0 if side == -1 else 180}) (effects (font (size 0.85 0.85)) (justify {"right" if side == -1 else "left"})) (uuid {uid(key)}))'

def write_schematics(blocks):
    rootuid = uid('root')
    sheets = partition_sheets(blocks)
    allsymbols = []
    for sheetindex, (sheetname, entries) in enumerate(sheets, 2):
        content = schematic_header(sheetname, sheetname.replace('-', ' ').title())
        content.append(' (lib_symbols')
        for c, symbol, *_ in entries:
            embedded = symbol.replace('(symbol ' + q(c['ref']), '(symbol ' + q('KUI_G1:' + c['ref']), 1)
            content.append(embedded)
            allsymbols.append(symbol)
        content.append(' )')
        content.append(f' (text "Every terminal follows the physical pin contract. Global labels connect across sheets. DNP = optional population." (at 12.7 16.51 0) (effects (font (size 1.27 1.27)) (justify left)) (uuid {uid(sheetname + "/note")}))')
        for c, _, pins, h, x, y in entries:
            suid = uid('component/' + c['ref'])
            content.append(f'(symbol (lib_id {q("KUI_G1:" + c["ref"])}) (at {x:g} {y:g} 0) (unit 1) (in_bom {"yes" if c.get("in_bom", True) else "no"}) (on_board {"yes" if c.get("on_board", True) else "no"}) (dnp {"no" if c["populate"] else "yes"}) (uuid {suid})')
            displayvalue = c['value'] if len(c['value']) < 37 else c['mpn']
            for key, value, px, py, hide in [
                ('Reference', c['ref'] + ('' if c['populate'] else ' DNP'), x, y - h - 5.08, False),
                ('Value', displayvalue, x, y - h - 2.54, False),
                ('Footprint', c.get('footprint', ''), x, y, True),
                ('Datasheet', (c.get('sources') or [''])[0], x, y, True),
                ('MPN', c.get('mpn', ''), x, y, True),
                ('LCSC', c.get('lcsc', ''), x, y, True),
                ('Variant', c['variant'], x, y, True),
            ]:
                # A DNP annotation must not alter the actual reference designator.
                if key == 'Reference':
                    value = c['ref']
                content.append(f' (property {q(key)} {q(value)} (at {px:g} {py:g} 0) (effects (font (size 1.0 1.0)){" hide" if hide else ""}))')
            for number, *_ in pins:
                content.append(f' (pin {q(number)} (uuid {uid(c["ref"] + "/pin/" + number)}))')
            content.append(f' (instances (project {q(NAME)} (path {q("/" + rootuid + "/" + uid("sheet/" + sheetname))} (reference {q(c["ref"])}) (unit 1)))) )')
            if not c['populate']:
                content.append(f' (text "DNP / {c["variant"]}" (at {x:g} {y + h + 2.54:g} 0) (effects (font (size 0.85 0.85))) (uuid {uid(c["ref"] + "/dnp")}))')
            for number, info, dx, dy, side in pins:
                px, py = round(x + dx, 4), round(y + dy, 4)
                tip = round(px + side * 2.54, 4)
                net = info.get('net')
                if net:
                    content.append(f' (wire (pts (xy {px:g} {py:g}) (xy {tip:g} {py:g})) (stroke (width 0) (type default)) (uuid {uid(c["ref"] + "/wire/" + number)}))')
                    content.append(label(net, tip, py, side, c['ref'] + '/label/' + number))
                else:
                    content.append(f' (no_connect (at {px:g} {py:g}) (uuid {uid(c["ref"] + "/nc/" + number)}))')
        content.append(' (embedded_fonts no)\n)')
        (CAD / (sheetname + '.kicad_sch')).write_text('\n'.join(content) + '\n')
    root = schematic_header('root', 'G1 microSD / Wi-Fi bridge — Rev A circuit implementation')
    root.append(' (lib_symbols)')
    for i, (sheetname, _) in enumerate(sheets, 2):
        x = 25.4 + ((i - 2) % 3) * 127
        y = 38.1 + ((i - 2) // 3) * 43.18
        root.append(f' (sheet (at {x:g} {y:g}) (size 104.14 25.4) (stroke (width 0) (type default)) (fill (color 0 0 0 0)) (uuid {uid("sheet/" + sheetname)}) (property "Sheetname" {q(sheetname)} (at {x:g} {y-1.27:g} 0) (effects (font (size 1.27 1.27)) (justify left bottom))) (property "Sheetfile" {q(sheetname + ".kicad_sch")} (at {x:g} {y+26.67:g} 0) (effects (font (size 1.27 1.27)) (justify left top))) (instances (project {q(NAME)} (path {q("/" + rootuid)} (page {q(i)})))) )')
    note = 'Retained GD-ROM device 0 / bridge device 1. Original mask ROM recovery is a hardware path.\n8 MiB PSRAM is bridge cache. Native 4-bit SD exposes raw sectors to K-UI.\nReview source contracts and validation reports; prototype qualification remains open.'
    root.append(f' (text {q(note)} (at 25.4 20.32 0) (effects (font (size 1.27 1.27)) (justify left)) (uuid {uid("root/note")}))')
    root.append(' (sheet_instances (path "/" (page "1")))\n (embedded_fonts no)\n)')
    (CAD / (NAME + '.kicad_sch')).write_text('\n'.join(root) + '\n')
    (CAD / 'KUI_G1.kicad_sym').write_text('(kicad_symbol_lib (version 20241209) (generator "kicad_symbol_editor")\n' + '\n'.join(allsymbols) + '\n)\n')
    (CAD / 'sym-lib-table').write_text('(sym_lib_table\n (lib (name "KUI_G1")(type "KiCad")(uri "${KIPRJMOD}/KUI_G1.kicad_sym")(options "")(descr "Physical pin contracts for selected parts"))\n)\n')
    (ROOT / 'design/integrated-components.json').write_text(json.dumps([p for b in blocks.values() for p in b], indent=2) + '\n')
    return len(sheets)

def audit_pads(blocks):
    import pcbnew
    errors = []
    rows = []
    for c in (p for b in blocks.values() for p in b):
        if not c.get('on_board', True):
            continue
        nick, name = c['footprint'].split(':', 1)
        fp = pcbnew.FootprintLoad(str(CAD / 'KUI_Footprints.pretty'), name)
        if fp is None:
            raise ValueError('Cannot load ' + c['footprint'])
        physical = set(p.GetNumber() for p in fp.Pads() if p.GetNumber())
        schematic = set(c['pins'])
        missing = schematic - physical
        extra = physical - schematic
        if missing or extra:
            errors.append({'ref': c['ref'], 'missing_physical_pads': sorted(missing), 'unmodeled_physical_pads': sorted(extra)})
        rows.append({'ref': c['ref'], 'physical_pad_numbers': sorted(physical), 'schematic_pin_numbers': sorted(schematic), 'match': not (missing or extra)})
    report = {'status': 'pass' if not errors else 'fail', 'components': len(rows), 'errors': errors, 'rows': rows}
    (ROOT / 'design/physical-pad-audit.json').write_text(json.dumps(report, indent=2) + '\n')
    if errors:
        raise ValueError('Physical pad audit failed: ' + json.dumps(errors))

def configure_project():
    """Keep actual KiCad rule keys consistent with the carrier contracts."""
    path = CAD / (NAME + '.kicad_pro')
    project = json.loads(path.read_text())
    rules = project['board']['design_settings']['rules']
    rules.update({
        'min_clearance': 0.15, 'min_track_width': 0.15,
        'min_copper_edge_clearance': 0.30,
        'min_through_hole_diameter': 0.20,
        'min_via_annular_width': 0.15, 'min_via_diameter': 0.50,
    })
    classes = {c['name']: c for c in project['net_settings']['classes']}
    classes['Default'].update({'clearance': 0.15, 'track_width': 0.15,
                               'via_diameter': 0.50, 'via_drill': 0.20})
    classes['POWER'].update({'clearance': 0.15, 'track_width': 0.40,
                             'via_diameter': 0.60, 'via_drill': 0.30})
    path.write_text(json.dumps(project, indent=2) + '\n')

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--skip-pad-audit', action='store_true')
    args = parser.parse_args()
    blocks = load_components()
    localize_footprints(blocks)
    pages = write_schematics(blocks)
    configure_project()
    if not args.skip_pad_audit:
        audit_pads(blocks)
    print(json.dumps({'components': sum(len(b) for b in blocks.values()), 'sheets': pages, 'native_schematic': str(CAD / (NAME + '.kicad_sch'))}))

if __name__ == '__main__':
    main()
