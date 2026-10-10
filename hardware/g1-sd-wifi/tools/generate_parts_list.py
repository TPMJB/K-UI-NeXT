#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Generate purchasing quantities from the physical component contracts.

This is a sourcing worksheet, not an assembler BOM or a production release.
Supplier IDs come exclusively from reviewed procurement-evidence.json entries.
"""
import argparse
import collections
import csv
import hashlib
import importlib.util
import json
from pathlib import Path
import re
from urllib.parse import urlparse

ROOT = Path(__file__).resolve().parents[1]
RESERVE_REFS = {'R202', 'D20', 'C202', 'C203', 'C204', 'C205'}
PSRAM_DNP_REFS = {'U203'}
FABRICATED_MPNS = {
    'CUSTOM_C5_SERVICE_PADS', 'CUSTOM_SWD_SERVICE_PADS', 'CUSTOM_USB_SERVICE_PADS',
    'PCB solder pads; no fitted connector', 'PCB test point', 'PCB_SOLDER_BRIDGE', 'PCB_TEST_PAD',
}
# Values are BIOS population, reserve population and PSRAM population. BASE keeps
# its existing fitted 8 MiB cache. DNP variants change assembly, not copper.
VARIANTS = {
    'BASE': (False, False, True),
    'BIOS': (True, False, True),
    'RESERVE': (False, True, True),
    'BIOS_AND_RESERVE': (True, True, True),
    'BASE_PSRAM_DNP': (False, False, False),
    'BIOS_PSRAM_DNP': (True, False, False),
    'RESERVE_PSRAM_DNP': (False, True, False),
    'BIOS_AND_RESERVE_PSRAM_DNP': (True, True, False),
}
QTY_COLUMNS = ['qty_base', 'qty_bios_variant', 'qty_reserve_variant', 'qty_bios_and_reserve_variant',
               'qty_base_psram_dnp_variant', 'qty_bios_psram_dnp_variant', 'qty_reserve_psram_dnp_variant',
               'qty_bios_and_reserve_psram_dnp_variant']
FIELDS = ['mpn', 'manufacturer', 'description', 'electrical_spec', 'physical_package', 'footprint',
          *QTY_COLUMNS, 'refs_base', 'refs_bios_only', 'refs_reserve_only', 'refs_base_only', 'refs_psram_only', 'refs_all',
          'assembly_method', 'lcsc_code', 'sourcing_status', 'supplier_identity_url',
          'jlc_assembly_status', 'jlc_assembly_url', 'jlc_pcba_type', 'jlc_fixture_notice',
          'manufacturer_source_urls', 'spec_review_status', 'notes']

def natural(s):
    return tuple(int(v) if v.isdigit() else v for v in re.split(r'(\d+)', s))

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def components():
    path = ROOT / 'tools/generate_design.py'
    spec = importlib.util.spec_from_file_location('physical_contracts', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return [c for parts in module.load_components().values() for c in parts]

def exclusion(c):
    if c.get('mpn') in FABRICATED_MPNS:
        return 'Fabricated PCB copper; purchase through PCB fabrication, not parts order'
    if not c.get('in_bom', True):
        return 'ERC declaration, not a physical purchased component'
    if c['ref'] == 'J10' and not c.get('mpn') and not c['populate']:
        return 'Unpopulated JTAG pogo pads; no header fitted'
    if not c.get('mpn'):
        raise ValueError('Unspecified purchased MPN: ' + c['ref'])
    return None

def populated(c, bios=False, reserve=False, psram=True):
    if c['ref'] in PSRAM_DNP_REFS and not psram:
        return False
    variant = c['variant']
    if variant == 'BIOS_OPTION':
        return bios
    if variant == 'BASE_ONLY':
        return not bios and c['populate']
    if variant not in {'BASE', 'ALL'}:
        raise ValueError('Unknown population variant: ' + variant)
    if c['ref'] in RESERVE_REFS:
        return reserve
    if not c['populate']:
        raise ValueError('Unclassified DNP purchased component: ' + c['ref'])
    return True

def make_rows(cs, evidence):
    groups = collections.defaultdict(list)
    excluded = []
    for c in cs:
        why = exclusion(c)
        if why:
            excluded.append({'ref': c['ref'], 'mpn': c.get('mpn', ''), 'reason': why})
            continue
        groups[(c['mpn'], c.get('footprint', ''))].append(c)
    rows = []
    for (mpn, footprint), parts in sorted(groups.items()):
        if mpn not in evidence['parts']:
            raise ValueError('Missing procurement metadata for ' + mpn)
        e = evidence['parts'][mpn]
        if not e.get('manufacturer') or not e.get('physical_package'):
            raise ValueError('Missing manufacturer/package for ' + mpn)
        supplier = e.get('supplier_identity', {})
        code = supplier.get('lcsc_code', '')
        if code:
            host = urlparse(supplier.get('url', '')).hostname or ''
            if not re.fullmatch(r'C\d+', code) or host not in {'lcsc.com', 'www.lcsc.com', 'jlcpcb.com', 'www.jlcpcb.com', 'item.szlcsc.com'}:
                raise ValueError('Unverified or non-official supplier identity: ' + mpn)
            if supplier.get('verified_mpn') != mpn or not supplier.get('verified_on'):
                raise ValueError('Supplier identity must match exact MPN and review date: ' + mpn)
        for c in parts:
            if c.get('lcsc') and c['lcsc'] != code:
                raise ValueError('Circuit supplier ID lacks matching reviewed evidence: ' + c['ref'])
        jlc = e.get('jlc_assembly', {})
        if jlc:
            if (urlparse(jlc.get('url', '')).hostname not in {'jlcpcb.com', 'www.jlcpcb.com'}
                    or jlc.get('verified_mpn') != mpn or jlc.get('verified_lcsc_code') != code
                    or not jlc.get('verified_on') or jlc.get('assembly_type') != 'SMT Assembly'):
                raise ValueError('JLC assembly evidence must match exact reviewed supplier identity: ' + mpn)
        method = e.get('assembly_method', 'PCB SMT; assembler acceptance pending')
        fixture = jlc.get('fixture_required_by_catalog', False)
        assembly_status = ('manual_purchase_not_carrier_smt' if method.startswith('Manual') else
                           'jlc_smt_catalog_verified_fixture_agreement_pending' if jlc and fixture else
                           'jlc_smt_catalog_verified_order_acceptance_pending' if jlc else
                           'jlc_assembly_identity_pending')
        row = {
            'mpn': mpn, 'manufacturer': e['manufacturer'],
            'description': e.get('description', parts[0]['value']),
            'electrical_spec': e.get('electrical_spec', ''),
            'physical_package': e['physical_package'], 'footprint': footprint,
            'assembly_method': method,
            'lcsc_code': code,
            'sourcing_status': 'catalog_identity_verified_assembly_pending' if code else 'sourcing_pending',
            'supplier_identity_url': supplier.get('url', ''),
            'jlc_assembly_status': assembly_status,
            'jlc_assembly_url': jlc.get('url', ''),
            'jlc_pcba_type': jlc.get('pcba_type', ''),
            'jlc_fixture_notice': 'explicit_fixture_requirement' if fixture else '',
            'manufacturer_source_urls': ' | '.join(e.get('manufacturer_sources', [])),
            'spec_review_status': e.get('spec_review_status', 'selected_exact_mpn; datasheet qualification pending'),
            'notes': e.get('notes', ''),
            'refs_base': ','.join(sorted((c['ref'] for c in parts if populated(c)), key=natural)),
            'refs_bios_only': ','.join(sorted((c['ref'] for c in parts if c['variant'] == 'BIOS_OPTION'), key=natural)),
            'refs_reserve_only': ','.join(sorted((c['ref'] for c in parts if c['ref'] in RESERVE_REFS), key=natural)),
            'refs_base_only': ','.join(sorted((c['ref'] for c in parts if c['variant'] == 'BASE_ONLY'), key=natural)),
            'refs_psram_only': ','.join(sorted((c['ref'] for c in parts if c['ref'] in PSRAM_DNP_REFS), key=natural)),
            'refs_all': ','.join(sorted((c['ref'] for c in parts), key=natural)),
        }
        for column, (bios, reserve, psram) in zip(QTY_COLUMNS, VARIANTS.values()):
            row[column] = sum(populated(c, bios, reserve, psram) for c in parts)
        rows.append(row)
    return rows, excluded

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true', help='verify tracked outputs without rewriting')
    args = parser.parse_args()
    evidence_path = ROOT / 'design/procurement-evidence.json'
    evidence = json.loads(evidence_path.read_text())
    cs = components()
    rows, excluded = make_rows(cs, evidence)
    purchased = [c for c in cs if not exclusion(c)]
    source_paths = sorted((ROOT / 'design/blocks').glob('*.json')) + [ROOT / 'design/flex-interface.json', ROOT / 'tools/generate_design.py', Path(__file__), evidence_path]
    totals = {name: sum(row[col] for row in rows) for name, col in zip(VARIANTS, QTY_COLUMNS)}
    # Functional population invariants: stock bypass cannot coexist with populated selector.
    byref = {c['ref']: c for c in purchased}
    assert populated(byref['R410']) and not populated(byref['R410'], bios=True)
    assert all(populated(byref['R409'], bios=b, reserve=r, psram=p) for b, r, p in VARIANTS.values())
    assert all(not populated(byref[r]) and populated(byref[r], reserve=True) for r in RESERVE_REFS)
    assert all(populated(byref[r], bios=True) and not populated(byref[r]) for r in ['U40', 'J40', 'J41', 'SW40'])
    for variant, (bios, reserve, psram) in VARIANTS.items():
        assert totals[variant] == sum(populated(c, bios, reserve, psram) for c in purchased)
    # Omitting the PSRAM must not disconnect boot flash or remove the hardware
    # CS1 bias. Retain the link, pullup and bypass; only U203 is omitted.
    assert byref['U203']['mpn'] == 'APS6404L-3SQR-ZR'
    assert byref['U203']['pins']['1']['net'] == 'PSRAM_CSn'
    assert byref['R2008']['value'] == '4.7k'
    assert {v['net'] for v in byref['R2008']['pins'].values()} == {'+3V3_LOGIC', 'PSRAM_CSn'}
    assert {v['net'] for v in byref['R2007']['pins'].values()} == {'PSRAM_CSn_MCU', 'PSRAM_CSn'}
    for bios, reserve, _ in VARIANTS.values():
        fitted = {c['ref'] for c in purchased if populated(c, bios, reserve, True)}
        dnp = {c['ref'] for c in purchased if populated(c, bios, reserve, False)}
        assert fitted - dnp == PSRAM_DNP_REFS and not dnp - fitted
        assert {'U201', 'U202', 'R2007', 'R2008'} <= dnp
    audit = {
        'status': 'SOURCING_WORKSHEET; NOT_A_PRODUCTION_RELEASE',
        'evidence_review_date': evidence['review_date'],
        'quantity_basis': 'Full component quantities for one complete assembly per variant; no scrap allowance, reel/MOQ rounding or inventory promise.',
        'source_sha256': {str(p.relative_to(ROOT)): sha(p) for p in source_paths},
        'source_component_refs': len(cs), 'purchased_component_refs': len(purchased),
        'unique_purchase_rows': len(rows), 'variant_total_parts': totals,
        'variant_dnp_purchased_refs': {
            name: sorted((c['ref'] for c in purchased if not populated(c, *settings)), key=natural)
            for name, settings in VARIANTS.items()
        },
        'supplier_identity_verified_rows': sum(bool(r['lcsc_code']) for r in rows),
        'sourcing_pending_rows': sum(not r['lcsc_code'] for r in rows),
        'catalog_identity_verified_base_rows': sum(bool(r['lcsc_code']) and r['qty_base'] > 0 for r in rows),
        'sourcing_pending_base_rows': [r['mpn'] for r in rows if not r['lcsc_code'] and r['qty_base'] > 0],
        'manual_base_purchase_rows': [r['mpn'] for r in rows if r['qty_base'] > 0 and r['assembly_method'].startswith('Manual')],
        'jlc_smt_catalog_verified_base_rows': sum(bool(r['jlc_assembly_url']) and r['qty_base'] > 0 for r in rows),
        'jlc_assembly_pending_base_rows': [r['mpn'] for r in rows if r['qty_base'] > 0 and not r['jlc_assembly_url'] and not r['assembly_method'].startswith('Manual')],
        'jlc_fixture_required_base_rows': [r['mpn'] for r in rows if r['qty_base'] > 0 and r['jlc_fixture_notice']],
        'excluded': excluded,
        'population_rules': {
            'BIOS': 'Populate BIOS_OPTION; omit BASE_ONLY R410; retain ALL R409. Offboard mechanical selector is included as a manual purchased part.',
            'RESERVE': 'Populate R202, D20 and C202-C205. Effective reserve capacitance and usable hold-up remain unqualified.',
            'BASE': 'Optional BIOS and reserve components are DNP; no JTAG header is purchased.',
            'BASE_PSRAM_DNP': 'Omit only U203; retain R2007 CS1 link, R2008 4.7k pullup and existing bypass capacitors. Also available with every BIOS/reserve combination; BASE remains PSRAM populated.',
        },
        'psram_dnp_firmware_requirements': [
            'Build or configure firmware for no external PSRAM; use internal SRAM/FIFOs for sectors and cache.',
            'Keep GPIO47/CS1 inactive high; do not issue QMI CS1 memory accesses or enable CS autodetection.',
            'Do not expose absent PSRAM in the MCU memory map or treat floating reads as detected RAM. Firmware qualification remains pending.',
        ],
        'open_release_items': evidence['open_release_items'],
    }
    import io
    output = io.StringIO(newline='')
    writer = csv.DictWriter(output, fieldnames=FIELDS, lineterminator='\n')
    writer.writeheader(); writer.writerows(rows)
    outputs = {ROOT / 'manufacturing/parts-to-buy.csv': output.getvalue(), ROOT / 'design/parts-to-buy-audit.json': json.dumps(audit, indent=2) + '\n'}
    if args.check:
        bad = [str(p.relative_to(ROOT)) for p, text in outputs.items() if not p.exists() or p.read_text() != text]
        if bad:
            raise SystemExit('Stale procurement outputs: ' + ', '.join(bad))
    else:
        for path, text in outputs.items():
            path.write_text(text)
    print(json.dumps({k:audit[k] for k in ['unique_purchase_rows','variant_total_parts','supplier_identity_verified_rows','sourcing_pending_rows']}))

if __name__ == '__main__':
    main()
