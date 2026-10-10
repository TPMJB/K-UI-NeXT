#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check source contracts and exported native connectivity; refresh integration audit.

The independent safety equations are evaluated using the actual XML netlist nets,
not an HDL simulation. This checks static connectivity and Boolean behavior only.
Human primary-source reviews and open bench/mechanical release gates are retained.
Use --check to validate everything without rewriting the reviewed report.
"""
import argparse
import collections
import copy
import csv
import hashlib
import itertools
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
REPORT = ROOT / 'design/integration-audit.json'
NATIVE = ROOT / 'design/integrated-components.json'
NETLIST = ROOT / 'controller/checks/netlist.xml'
PINOUT = ROOT / 'design/sources/machxo2-2000-pinout.csv'
LPF = ROOT / 'fpga/bridge.lpf'
FLEX = ROOT / 'design/flex-interface.json'
CHANNELS = {
    'SN74LVC2G08DCUR': [(('1A', '1B'), '1Y', 'AND'), (('2A', '2B'), '2Y', 'AND')],
    'SN74LVC1G11DBVR': [(('A', 'B', 'C'), 'Y', 'AND')],
    'SN74LVC2G04DBVR': [(('1A',), '1Y', 'NOT'), (('2A',), '2Y', 'NOT')],
    'SN74LVC2G04DBVT': [(('1A',), '1Y', 'NOT'), (('2A',), '2Y', 'NOT')],
    'SN74LVC1G32DBVR': [(('A', 'B'), 'Y', 'OR')],
    'SN74LVC1G08DBVR': [(('A', 'B'), 'Y', 'AND')],
    'SN74LVC1G00DBVR': [(('A', 'B'), 'Y', 'NAND')],
    'SN74LVC1G04DBVR': [(('A',), 'Y', 'NOT')],
    'SN74LVC1G332DBVR': [(('A', 'B', 'C'), 'Y', 'OR')],
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def load_sources():
    blocks = {}
    for path in sorted((ROOT / 'design/blocks').glob('*.json')):
        data = json.loads(path.read_text())
        blocks[path.stem] = data if isinstance(data, list) else data['components']
        for part in blocks[path.stem]:
            part['block'] = path.stem
            part.setdefault('populate', True)
            part.setdefault('variant', 'BASE')
    parts = [p for group in blocks.values() for p in group]
    require(len(parts) == 364, 'Source component count changed; review audit scope.')
    require(len({p['ref'] for p in parts}) == len(parts), 'Duplicate source reference.')
    return blocks, parts


def check_native(source, flex):
    native = json.loads(NATIVE.read_text())
    byref = {c['ref']: c for c in native}
    require(len(byref) == len(native) == 375, 'Native component count or uniqueness failed.')
    for part in source:
        require(part['ref'] in byref, 'Missing native component: ' + part['ref'])
        target = byref[part['ref']]
        for key in ['mpn', 'pins', 'populate', 'variant', 'block']:
            require(target.get(key) == part.get(key), f"Native/source mismatch: {part['ref']} {key}")
        require(target.get('footprint', '').split(':')[-1] == part.get('footprint', '').split(':')[-1],
                'Native footprint differs from source: ' + part['ref'])
        if 'probe_access' in part:
            for key in ['probe_access', 'in_bom', 'on_board', 'height_max_mm']:
                require(target.get(key) == part.get(key), f"Native/source probe metadata mismatch: {part['ref']} {key}")
    connector_refs = set()
    for connector in flex['connectors']:
        ref = connector['carrier_reference']
        connector_refs.add(ref)
        part = byref[ref]
        require(part['mpn'] == connector['mpn'], 'CN503 socket MPN mismatch: ' + ref)
        expected = {str(p['pin']): ('FLEX_5V' if p['signal'] == 'CONSOLE_5V' else p['signal'])
                    for p in connector['pins']}
        expected['MP'] = 'GND'
        require({pin: info['net'] for pin, info in part['pins'].items()} == expected,
                'CN503 native pin/net mismatch: ' + ref)
    # Manufactured copper pads are deliberately excluded from the purchased BOM
    # but remain physical components in XML. Only ERC power flags are omitted.
    flag_refs = {c['ref'] for c in native if c['ref'].startswith('#FLG')}
    require(len(connector_refs) == 2 and len(flag_refs) == 9, 'Unexpected native extra references.')
    require(set(byref) == {c['ref'] for c in source} | connector_refs | flag_refs,
            'Unexpected or missing native component.')
    require(all(ref.startswith('#FLG') for ref in flag_refs), 'Unexpected excluded native component.')

    xml = ET.parse(NETLIST).getroot()
    xml_components = xml.findall('components/comp')
    xml_byref = {c.attrib['ref']: c for c in xml_components}
    require(len(xml_byref) == len(xml_components) == 366, 'XML component count or uniqueness failed.')
    require(set(xml_byref) == set(byref) - flag_refs,
            'XML references differ; only the nine ERC flags may be omitted.')
    endpoint = {}
    net_nodes = {}
    for net in xml.findall('nets/net'):
        name = net.attrib['name']
        require(name not in net_nodes, 'Duplicate XML net: ' + name)
        nodes = net.findall('node')
        net_nodes[name] = nodes
        for node in nodes:
            key = (node.attrib['ref'], node.attrib['pin'])
            require(key not in endpoint, 'XML pin appears on multiple nets: ' + str(key))
            endpoint[key] = (name, node.attrib['pintype'])
    real = [copy.deepcopy(c) for c in native if c['ref'] not in flag_refs]
    assigned = unconnected = 0
    for part in real:
        ref = part['ref']
        component = xml_byref[ref]
        fields = {v.attrib['name']: v.text or '' for v in component.findall('fields/field')}
        require(fields.get('MPN', '') == part.get('mpn', ''), 'XML exact MPN mismatch: ' + ref)
        require(fields.get('LCSC', '') == part.get('lcsc', ''), 'XML supplier field mismatch: ' + ref)
        require(fields.get('Variant') == part['variant'], 'XML variant mismatch: ' + ref)
        require((component.findtext('footprint') or '') == part.get('footprint', ''),
                'XML footprint mismatch: ' + ref)
        dnp = any(p.attrib['name'] == 'dnp' for p in component.findall('property'))
        require(dnp == (not part['populate']), 'XML DNP mismatch: ' + ref)
        terminals = {p.attrib['num'] for p in component.findall('units/unit/pins/pin')}
        require(terminals == set(part['pins']), 'XML physical terminal mismatch: ' + ref)
        for pin, info in part['pins'].items():
            key = (ref, pin)
            require(key in endpoint, 'XML lacks physical endpoint: ' + str(key))
            actual, pin_type = endpoint[key]
            require(pin_type.removesuffix('+no_connect') == info['type'],
                    'XML electrical pin type mismatch: ' + str(key))
            if info.get('net') is None:
                require(actual.startswith('unconnected-') and len(net_nodes[actual]) == 1,
                        'Intentionally unused pin is connected: ' + str(key))
                info['net'] = None
                unconnected += 1
            else:
                require(actual == info['net'], 'XML functional net mismatch: ' + str(key))
                # Downstream gate proofs use the independently exported XML net.
                info['net'] = actual
                assigned += 1
    require(set(endpoint) == {(c['ref'], pin) for c in real for pin in c['pins']},
            'Unexpected XML endpoint.')
    return native, real, {
        'native_components': len(native), 'xml_components': len(xml_components),
        'onboard_footprinted_components': sum(bool(c.get('footprint')) for c in native),
        'omitted_erc_flags': sorted(flag_refs), 'xml_named_nets': len(net_nodes),
        'assigned_pin_endpoints_checked': assigned,
        'isolated_no_connect_endpoints_checked': unconnected,
    }


def check_pin_functions(parts, flex):
    b = {c['ref']: c for c in parts}
    def terminal(ref, pin, net, kind=None, name=None):
        info = b[ref]['pins'][str(pin)]
        require(info['net'] == net, f'{ref}.{pin} net must be {net}')
        if kind:
            require(info['type'] == kind, f'{ref}.{pin} type must be {kind}')
        if name:
            require(info['name'] == name, f'{ref}.{pin} function must be {name}')
    terminal('U201', 35, 'LOGIC_RESETn')
    terminal('U23', 1, 'LOGIC_RESETn')
    terminal('U35', 39, 'GND', 'power_in', 'GND')
    require(len(b['U35']['pins']) == 48, 'U35 must represent all48 physical pins.')
    terminal('U20', 2, '+5V_HOLD', 'power_out')
    terminal('U20', 7, '+5V_HOLD', 'passive')

    physical = list(csv.reader(PINOUT.open()))
    functions = {row[12]: row[1] for row in physical[4:] if len(row) > 12 and row[12].isdigit()}
    require(len(functions) == 100 and len(b['U10']['pins']) == 100, 'FPGA must have100 package pins.')
    for pin, info in b['U10']['pins'].items():
        require(functions[pin] == info['name'].split(' / ')[0], 'FPGA manufacturer function mismatch: ' + pin)
    locations = re.findall(r'LOCATE COMP "([^"]+)" SITE "(\d+)";', LPF.read_text())
    require(len(locations) == len(set(n for n, _ in locations)) == len(set(p for _, p in locations)) == 56,
            'FPGA constraints must have56 unique signals and locations.')
    for net, pin in locations:
        terminal('U10', pin, re.sub(r'\[(\d+)\]', r'\1', net))

    raw = {p['signal'] for c in flex['connectors'] for p in c['pins'] if p['signal'].startswith('ATA_')}
    required = {f'ATA_DD{i}' for i in range(16)} | {f'ATA_DA{i}' for i in range(3)} | {
        'ATA_CS0n', 'ATA_CS1n', 'ATA_DIORn', 'ATA_DIOWn', 'ATA_RESETn', 'ATA_DMACKn',
        'ATA_IORDY', 'ATA_INTRQ', 'ATA_DMARQ'}
    require(raw == required and len(raw) == 28, 'CN503 must expose exactly28 ATA signals.')
    all_raw = {p['signal'] for c in flex['connectors'] for p in c['pins']}
    require('ROM_CEn' not in all_raw and not any(s.startswith('ROM_A') for s in all_raw),
            'CN503 cannot replace the extra BIOS tap.')

    for i, pin in enumerate([2, 3, 5, 6, 8, 9, 11, 12, 13, 14, 16, 17, 19, 20, 22, 23]):
        terminal('U35', pin, f'ATA_DD{i}', 'bidirectional', f'{i // 8 + 1}B{i % 8 + 1}')
    for i, pin in enumerate([47, 46, 44, 43, 41, 40, 38, 37, 36, 35, 33, 32, 30, 29, 27, 26]):
        terminal('U35', pin, f'BUS_DD{i}', 'bidirectional', f'{i // 8 + 1}A{i % 8 + 1}')
    for pin in [1, 24]: terminal('U35', pin, 'DATA_DIR', 'input')
    for pin in [48, 25]: terminal('U35', pin, 'DATA_OEn', 'input')
    for pin in [31, 42]: terminal('U35', pin, '+3V3_LOGIC', 'power_in', 'VCCA')
    for pin in [7, 18]: terminal('U35', pin, 'CONSOLE_3V3', 'power_in', 'VCCB')
    for pin in [4, 10, 15, 21, 28, 34, 39, 45]: terminal('U35', pin, 'GND', 'power_in')
    controls = ['DA0', 'DA1', 'DA2', 'CS0n', 'CS1n', 'DIORn', 'DIOWn', 'DMACKn']
    for i, (a, y, signal) in enumerate(zip([2, 4, 6, 8, 11, 13, 15, 17], [18, 16, 14, 12, 9, 7, 5, 3], controls)):
        terminal('U36', a, 'ATA_' + signal, 'input', f'{i // 4 + 1}A{i % 4 + 1}')
        terminal('U36', y, 'BUS_' + signal, 'output', f'{i // 4 + 1}Y{i % 4 + 1}')
    for pin in [1, 19]: terminal('U36', pin, 'GND', 'input')
    terminal('U36', 20, '+3V3_LOGIC', 'power_in')
    terminal('U36', 10, 'GND', 'power_in')
    terminal('U37', 2, 'ATA_RESETn', 'input', 'A')
    terminal('U37', 4, 'BUS_RESETn', 'output', 'Y')
    terminal('U37', 5, '+3V3_LOGIC', 'power_in')
    # TI AXC package pin functions and fixed direction for each independent pair.
    for ref in ['U204', 'U205']:
        terminal(ref, 1, '+3V3_LOGIC', 'power_in', 'VCCA')
        terminal(ref, 16, '+3V3_C5_IO', 'power_in', 'VCCB')
        terminal(ref, 2, '+3V3_LOGIC', 'input', '1DIR')
        terminal(ref, 3, 'GND', 'input', '2DIR')
        for pin in [8, 9]: terminal(ref, pin, 'GND', 'power_in')
        for pin in [14, 15]: terminal(ref, pin, 'C5_IO_OEn', 'input')
        for pin, name in [(4, '1A1'), (5, '1A2'), (6, '2A1'), (7, '2A2'),
                          (10, '2B2'), (11, '2B1'), (12, '1B2'), (13, '1B1')]:
            require(b[ref]['pins'][str(pin)]['name'] == name, f'{ref}.{pin} AXC channel function mismatch.')

    # Physical x8 NOR addresses include DQ15/A-1; A20/A21 remain the two bank bits.
    require(set(b['U40']['pins']) == {str(i) for i in range(1, 49)}, 'NOR must have48 physical pins.')
    terminal('U40', 45, 'ATA_DD8', 'input', 'DQ15/A-1')
    for i, pin in enumerate([25, 24, 23, 22, 21, 20, 19]): terminal('U40', pin, f'ATA_DD{i + 9}', 'input', f'A{i}')
    for i, pin in enumerate([18, 8, 7]): terminal('U40', pin, f'ATA_DA{i}', 'input', f'A{i + 7}')
    for address, pin in {10:6, 11:5, 12:4, 13:3, 14:2, 15:1, 16:48, 17:17, 18:16, 19:9}.items():
        terminal('U40', pin, f'ROM_A{address}', 'input', f'A{address}')
    for i, pin in enumerate([29, 31, 33, 35, 38, 40, 42, 44]): terminal('U40', pin, f'ATA_DD{i}', 'bidirectional', f'DQ{i}')
    for i, pin in enumerate([30, 32, 34, 36, 39, 41, 43]): terminal('U40', pin, None, 'no_connect', f'DQ{i + 8}')
    terminal('U40', 47, 'GND', 'input', 'BYTE#')
    terminal('U40', 10, 'BIOS_NOR_BANK0', 'input', 'A20')
    terminal('U40', 13, 'BIOS_NOR_BANK1', 'input', 'A21')
    terminal('U40', 26, 'BIOS_NOR_CEn', 'input', 'CE#')
    terminal('U40', 28, 'ATA_DIORn', 'input', 'OE#')
    terminal('U40', 37, 'CONSOLE_3V3', 'power_in', 'VCC')
    selector = ['STOCK_CEn', 'ROM_CEn', 'BIOS_NOR_CEn', 'GND', 'BIOS_RECOVERYn', 'CONSOLE_3V3']
    for pin, net in enumerate(selector, 1):
        terminal('SW40', pin, net, 'passive')
        terminal('J41', pin, net, 'passive')
    for i in range(10): terminal('J40', i + 1, f'ROM_A{i + 10}', 'passive')
    terminal('J40', 11, 'ROM_CEn', 'passive')
    terminal('J40', 12, 'STOCK_CEn', 'passive')
    require(not b['SW40'].get('footprint'), 'Recovery selector must remain the independent offboard switch.')
    require(all(not c['populate'] for c in parts if c['variant'] == 'BIOS_OPTION'),
            'Factory BIOS option must remain DNP.')
    require(b['R409']['variant'] == 'ALL' and b['R409']['populate'], 'Recovery pulldown must always be fitted.')
    require({p['net'] for p in b['R409']['pins'].values()} == {'BIOS_RECOVERYn', 'GND'}, 'Recovery pulldown must default to stock.')
    require(b['R410']['variant'] == 'BASE_ONLY' and b['R410']['populate'], 'Base recovery-high strap must be BASE_ONLY.')
    require({p['net'] for p in b['R410']['pins'].values()} == {'BIOS_RECOVERYn', 'CONSOLE_3V3'}, 'Base recovery-high strap connection mismatch.')

    for i, sidepin in enumerate([str(i) for i in range(1, 15)] + ['TP2', 'TP8'], 201):
        c = b['K' + str(i)]
        require(c['mpn'] == 'S7221-45R' and list(c['pins']) == ['1'], 'C5 contact identity or terminal mismatch.')
        terminal(c['ref'], 1, b['U206']['pins'][sidepin]['net'])
    for ref, net in [('R111', 'FPGA_SDA'), ('R112', 'FPGA_SCL')]:
        require({p['net'] for p in b[ref]['pins'].values()} == {'+3V3_LOGIC', net}, 'Missing FPGA configuration pullup: ' + ref)
    for ref in ['J40', 'J41']: terminal(ref, 'MP', 'GND', 'passive')
    return len(locations)


def check_probe_access(actual, flex):
    """Verify pads against independent exported physical endpoints and CN503."""
    b = {c['ref']: c for c in actual}
    probes = {ref: part for ref, part in b.items() if 'probe_access' in part}
    require(set(probes) == {f'TP{i}' for i in range(100, 139)}, 'Expected39 debug pads are missing or duplicated.')
    raw = {p['signal'] for c in flex['connectors'] for p in c['pins'] if p['signal'].startswith('ATA_')}
    raw_pads = {ref: part['pins']['1']['net'] for ref, part in probes.items() if 100 <= int(ref[2:]) <= 127}
    require(set(raw_pads.values()) == raw and len(raw_pads) == len(raw) == 28,
            'Raw debug pads must cover each of the28 distinct CN503 ATA nets exactly once.')
    expected_controls = ['DATA_DIR', 'INTRQ_OEn', 'DMARQ_OEn', 'IORDY_RELEASE',
                         'REQ_DATA_OEn', 'REQ_INTRQ_OEn', 'REQ_DMARQ_OEn', 'REQ_IORDY_LOW']
    require([probes[f'TP{i}']['pins']['1']['net'] for i in range(128, 136)] == expected_controls,
            'Debug enable/direction/request pads do not match actual gating nets.')
    for ref, net in [('TP22', 'BUS_SAFE'), ('TP23', 'DATA_OEn')]:
        require(b[ref]['pins']['1']['net'] == net, 'Existing hardware safety probe is missing: ' + ref)
    for i in range(136, 139):
        require(probes[f'TP{i}']['pins']['1']['net'] == 'GND', 'Local debug return is not GND.')
    mappings = []
    for ref, part in sorted(probes.items(), key=lambda item: int(item[0][2:])):
        require(part['mpn'] == 'PCB_TEST_PAD' and not part['in_bom'] and part['on_board']
                and part['populate'] and part['height_max_mm'] == 0,
                'Debug pad must be manufactured bare copper, not a purchased connector: ' + ref)
        require(part['footprint'].split(':')[-1] == 'RawProbe_Pad_D1.0mm_NoPaste' and set(part['pins']) == {'1'}
                and part['pins']['1']['type'] == 'passive', 'Debug pad physical terminal mismatch: ' + ref)
        access = part['probe_access']
        endpoint_ref, endpoint_pin = access['endpoint_reference'], access['endpoint_pin']
        require(endpoint_ref in b and endpoint_pin in b[endpoint_ref]['pins'], 'Missing debug target: ' + ref)
        net = part['pins']['1']['net']
        require(b[endpoint_ref]['pins'][endpoint_pin]['net'] == net,
                'Probe is connected to a different physical endpoint net: ' + ref)
        local_ground = access['local_ground_reference']
        require((net == 'GND' and local_ground is None)
                or (net != 'GND' and local_ground in probes and probes[local_ground]['pins']['1']['net'] == 'GND'),
                'Probe ground reference is absent or not GND: ' + ref)
        require(access['placement_side'] == 'top or bare bottom copper; removed-carrier bench access' and access['maximum_added_stub_mm'] == 3.0
                and access['minimum_goal_added_stub_mm'] == 0.0,
                'Probe placement contract changed; review loading and accessibility: ' + ref)
        if net in raw:
            sockets = [(c['carrier_reference'], str(p['pin'])) for c in flex['connectors']
                       for p in c['pins'] if p['signal'] == net]
            require(len(sockets) == 1 and b[sockets[0][0]]['pins'][sockets[0][1]]['net'] == net,
                    'Raw pad does not join the actual CN503 socket endpoint: ' + ref)
        mappings.append({'ref': ref, 'net': net, 'endpoint_ref': endpoint_ref,
                         'endpoint_pin': endpoint_pin, 'local_ground_ref': local_ground,
                         'access_policy': access['placement_side']})
    return {'raw_g1_pads': 28, 'added_enable_direction_request_pads': 8,
            'added_local_ground_pads': 3, 'existing_safety_enable_pads': ['TP22', 'TP23'],
            'native_xml_probe_endpoint_checks': len(mappings), 'mappings': mappings,
            'qualification': 'Static exported-net connectivity and probe metadata only; physical placement, stub length, access and probe loading remain layout/bench checks.'}


def check_safety_equations(actual):
    gates = []
    for c in actual:
        if c['mpn'] not in CHANNELS:
            continue
        nets = {info['name']: info['net'] for info in c['pins'].values()}
        for inputs, out, operation in CHANNELS[c['mpn']]:
            gates.append(([nets[n] for n in inputs], nets[out], operation))
    def evaluate(initial):
        values = dict(initial)
        for _ in range(len(gates) + 1):
            changed = False
            for inputs, output, operation in gates:
                if output in values or not all(net in values for net in inputs):
                    continue
                bits = [values[net] for net in inputs]
                if operation == 'AND': result = all(bits)
                elif operation == 'OR': result = any(bits)
                elif operation == 'NOT': result = not bits[0]
                else: result = not all(bits)
                values[output] = result
                changed = True
            if not changed:
                break
        return values
    permits = ['LOGIC_RESETn', 'STORAGE_PGOOD', 'HOST_OKn', 'PWR_FAILn', 'BRIDGE_ARM',
               'FPGA_READY', 'BUS_RESETn', 'FPGA_RESETn', 'BIOS_RECOVERYn']
    requests = ['REQ_DATA_OEn', 'REQ_INTRQ_OEn', 'REQ_DMARQ_OEn', 'REQ_IORDY_LOW']
    bus = bios = power = 0
    for bits in itertools.product([False, True], repeat=13):
        inputs = dict(zip(permits + requests, bits))
        values = evaluate(inputs)
        safe = all(inputs[n] for n in permits)
        require(values['BUS_SAFE'] == safe, 'BUS_SAFE does not equal all nine independent permit terms.')
        for output, request in [('DATA_OEn', 'REQ_DATA_OEn'), ('INTRQ_OEn', 'REQ_INTRQ_OEn'), ('DMARQ_OEn', 'REQ_DMARQ_OEn')]:
            require(values[output] == (not safe or inputs[request]), 'Host response fails open on disable: ' + output)
        require(values['IORDY_RELEASE'] == (not (safe and inputs['REQ_IORDY_LOW'])), 'IORDY release condition failed.')
        bus += 1
    for bits in itertools.product([False, True], repeat=5):
        inputs = dict(zip(['BIOS_RECOVERYn', 'BIOS_FLASH_ARMED', 'BIOS_WR_EN', 'BIOS_NOR_CEn', 'ATA_DIOWn'], bits))
        values = evaluate(inputs)
        allowed = inputs['BIOS_RECOVERYn'] and inputs['BIOS_FLASH_ARMED'] and inputs['BIOS_WR_EN']
        require(values['BIOS_NOR_WEn'] == (not allowed or inputs['BIOS_NOR_CEn'] or inputs['ATA_DIOWn']),
                'NOR WE# escaped physical selector/arm/write-cycle interlock.')
        bios += 1
    for bits in itertools.product([False, True], repeat=4):
        inputs = dict(zip(['PWR_FAILn', 'C5_PWR_EN', 'STORAGE_PGOOD', 'SD_IO_EN'], bits))
        values = evaluate(inputs)
        require(values['C5_SWITCH_EN'] == (inputs['PWR_FAILn'] and inputs['C5_PWR_EN']), 'C5 cutoff equation failed.')
        require(values['SD_IO_OEn'] == (not (inputs['STORAGE_PGOOD'] and inputs['SD_IO_EN'])), 'SD isolation equation failed.')
        power += 1
    require((bus, bios, power) == (8192, 32, 16), 'Functional proof case count changed.')
    return {'bus_ownership_cases': bus, 'bios_write_interlock_cases': bios,
            'c5_sd_enable_cases': power, 'total_cases': bus + bios + power,
            'connectivity_basis': 'controller/checks/netlist.xml; all native component pin nets and types checked before gate evaluation'}


def check_probe_negative_controls(actual, flex):
    """Reject plausible raw/buffered swaps even when all28 raw names remain."""
    failures = {}
    cases = ['swapped_data_labels', 'buffered_endpoint_substitution', 'wrong_ground_return', 'purchased_probe_header']
    for case in cases:
        changed = copy.deepcopy(actual)
        b = {c['ref']: c for c in changed}
        if case == 'swapped_data_labels':
            b['TP100']['pins']['1']['net'], b['TP101']['pins']['1']['net'] = 'ATA_DD1', 'ATA_DD0'
        elif case == 'buffered_endpoint_substitution':
            b['TP100']['probe_access']['endpoint_pin'] = '47'  # BUS_DD0, not raw ATA_DD0
        elif case == 'wrong_ground_return':
            b['TP100']['probe_access']['local_ground_reference'] = 'TP101'
        else:
            b['TP100']['in_bom'] = True
        try:
            check_probe_access(changed, flex)
        except ValueError as error:
            failures[case] = str(error)
        else:
            raise ValueError('Probe audit accepted invalid negative control: ' + case)
    return {'rejected_cases': len(failures), 'expected_rejections': failures,
            'basis': 'In-memory mutations of verified native XML endpoints; no design files changed.'}


def audit_report(blocks, source, native, netlist_summary, pins, proof, probes):
    report = json.loads(REPORT.read_text())
    snapshots = sorted((ROOT / 'design/blocks').glob('*.json')) + [FLEX, PINOUT, LPF, NATIVE, NETLIST, Path(__file__)]
    report['snapshot'] = [{'path': str(p.relative_to(ROOT)), 'sha256': hashlib.sha256(p.read_bytes()).hexdigest()} for p in snapshots]
    report['scope'] = 'Static review of logical component/pin/net source blocks, selected manufacturer pinouts, passive flex interface, FPGA pin constraints and exported native XML connectivity. Boolean proofs use actual XML net names. This is not an assembled-board or fabrication-release validation.'
    report['components'] = {
        'total': len(source), 'by_block': {name: len(parts) for name, parts in blocks.items()},
        'duplicate_refs': [], 'native_total_including_cn503_and_erc_flags': len(native),
        'native_extra_cn503_connectors': 2, 'native_extra_erc_flags': 9,
    }
    report['native_netlist'] = netlist_summary
    report['debug_probe_access'] = probes
    report['automated_proof'] = {**proof, 'fpga_package_pin_functions_checked': 100,
                                 'fpga_lpf_pin_net_constraints_checked': pins,
                                 'command': 'python hardware/g1-sd-wifi/tools/check_integration.py --check'}
    report['checks'] = [c for c in report['checks'] if c['id'] not in {'INT-C13', 'INT-C14'}]
    for check in report['checks']:
        if check['id'] == 'INT-C01':
            check['description'] = f'{len(source)} unique reference designators across controller, frontend, power-bus and BIOS source blocks; native generator additionally adds two CN503 FPC connectors and nine ERC declarations. Manufactured probe pads are retained in the native circuit but excluded from purchased parts. Optional BIOS has complete 48-pin NOR mapping.'
    report['checks'].append({'id': 'INT-C13', 'result': 'pass_exported_native_connectivity',
                            'description': f"All {len(source)} source components and two native CN503 sockets agree with integrated native pin contracts and exported XML component fields/terminals/nets/types/DNP. Nine ERC flags are omitted from XML; all {netlist_summary['isolated_no_connect_endpoints_checked']} intended unused endpoints are isolated. Physical bare probe pads remain in XML despite being excluded from purchased BOM. Safety equations are evaluated from exported XML connectivity."})
    report['checks'].append({'id': 'INT-C14', 'result': 'pass_exported_probe_connectivity',
                            'description': 'All 28 raw G1 signals have distinct bare probe pads joining their actual CN503 socket and named circuit endpoint. Eight added direction/enable/request probes and three ground returns agree with XML; existing BUS_SAFE/DATA_OEn pads are retained. Probe placement/stub/loading targets are not physical qualification.'})
    endpoints = collections.defaultdict(list)
    for part in source:
        for pin, info in part['pins'].items():
            if info.get('net') is not None:
                endpoints[info['net']].append({'block': part['block'], 'ref': part['ref'], 'pin': pin,
                                             'name': info['name'], 'type': info['type'], 'populate': part['populate']})
    report['cross_block_signal_nets'] = {n: entries for n, entries in sorted(endpoints.items())
                                        if n != 'GND' and len({v['block'] for v in entries}) > 1}
    report['revision_note'] = f"Refreshed after the corrected J51 mapping and 39 manufactured debug pads. Reusable checker verifies {len(native)} integrated references, {netlist_summary['xml_components']} XML references, {netlist_summary['onboard_footprinted_components']} onboard footprints, all native endpoints, 100 FPGA package functions and 56 LPF pin/net constraints; all 8240 functional Boolean combinations use exported native connectivity. All 39 debug probe mappings are verified from XML. No mechanical, probe loading, timing or assembler fixture release is claimed."
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true', help='check all invariants and reviewed report freshness without writing')
    args = parser.parse_args()
    blocks, source = load_sources()
    flex = json.loads(FLEX.read_text())
    native, actual, netlist_summary = check_native(source, flex)
    pins = check_pin_functions(actual, flex)
    probes = check_probe_access(actual, flex)
    probes['negative_controls'] = check_probe_negative_controls(actual, flex)
    proof = check_safety_equations(actual)
    report = audit_report(blocks, source, native, netlist_summary, pins, proof, probes)
    output = json.dumps(report, indent=2) + '\n'
    if args.check:
        require(REPORT.read_text() == output, 'Integration audit is stale; run tools/check_integration.py to refresh after review.')
    else:
        REPORT.write_text(output)
    print(json.dumps({'status': report['status'], 'native_components': len(native),
                      'xml_components': netlist_summary['xml_components'],
                      'functional_cases': proof['total_cases'], 'fpga_constraints': pins,
                      'assigned_native_endpoints': netlist_summary['assigned_pin_endpoints_checked'],
                      'unused_isolated_endpoints': netlist_summary['isolated_no_connect_endpoints_checked']}))


if __name__ == '__main__':
    main()
