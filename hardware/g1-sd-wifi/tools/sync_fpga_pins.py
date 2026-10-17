#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Regenerate the FPGA pin table and LPF from the circuit block.

design/blocks/frontend.json is the source of truth for U10's pin-to-net
assignment. This writes the Net column of design/front-end-pinplan.csv and
the LOCATE/IOBUF lines of fpga/bridge.lpf from it, in RTL port order, so a
layout-driven pin swap cannot leave the schematic, table and constraints
disagreeing. With --apply MAP.json it first moves the named nets to new pins
(MAP is {"NET": "pin"}); it refuses configuration, power or occupied pins.
"""
import argparse
import csv
import io
import json
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[1]
BLOCK = ROOT / 'design' / 'blocks' / 'frontend.json'
PINPLAN = ROOT / 'design' / 'front-end-pinplan.csv'
LPF = ROOT / 'fpga' / 'bridge.lpf'
RTL = ROOT / 'fpga' / 'bridge.v'
# Ports whose LPF enables the I/O's internal pull-down. BUS_DD idles
# tri-stated between the FPGA and U35's disabled A port; the FPGA's own
# pull-down replaces sixteen discrete 47 k biases that could not be placed
# near the bus.
PULL_DOWN = re.compile(r'BUS_DD\[')
HEADER = ('# LCMXO2-2000HC-4TG100C, physical allocation only.\n'
          '# External ATA/link setup, hold, latency and recovery constraints remain required.\n'
          '# No global asynchronous false-path exemption is supplied.\n'
          '# sysCONFIG (MachXO2 defaults: JTAG_PORT=ENABLE, SDM_PORT=DISABLE). Every image keeps\n'
          '# JTAG and PROGRAMN/DONE/INITN as configuration pins, so U201 can reload U10 through\n'
          '# PROGRAMn (GPIO28) and the controller always meets the same ports. Confirm in\n'
          '# Diamond Global Preferences on the first build.\n'
          'SYSCONFIG JTAG_PORT=ENABLE SDM_PORT=PROGRAMN_DONE_INITN ;\n'
          'FREQUENCY PORT "FPGA_CLK50" 50.000 MHZ;\n')


def rtl_ports():
    """Scalar port names in declaration order, buses expanded."""
    text = RTL.read_text()
    header = text[text.index('module bridge'):text.index(');', text.index('module bridge'))]
    ports = []
    for kind, width, names in re.findall(r'\b(input|output|inout)\s+(?:wire|reg)?\s*(\[\d+:\d+\])?\s*([^;\n]+?)(?:,\s*$|\s*$)',
                                         header, re.M):
        for name in [n.strip() for n in names.split(',') if n.strip()]:
            if width:
                hi, lo = (int(v) for v in width[1:-1].split(':'))
                ports += ['%s[%d]' % (name, i) for i in range(lo, hi + 1)]
            else:
                ports.append(name)
    return ports


def net_of(port):
    return re.sub(r'\[(\d+)\]', r'\1', port)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--apply', type=pathlib.Path, help='JSON {net: pin} moves to apply first')
    args = ap.parse_args()
    block = json.loads(BLOCK.read_text())
    u10 = next(c for c in block if c['ref'] == 'U10')
    pins = u10['pins']
    if args.apply:
        moves = json.loads(args.apply.read_text())
        if len(set(moves.values())) != len(moves):
            raise ValueError('Two nets mapped to one pin')
        moving = set(moves)
        for net, pin in moves.items():
            info = pins[pin]
            function = info['name'].split(' / ')
            if info['type'] in ('power_in', 'no_connect') or not function[0].startswith('P'):
                raise ValueError('Pin %s is not a user I/O' % pin)
            if len(function) > 1 and not re.match(r'(PCLK|L_GPLL)', function[1]):
                raise ValueError('Pin %s carries configuration function %s' % (pin, function[1]))
            if info['net'] is not None and info['net'] not in moving:
                raise ValueError('Pin %s already carries %s' % (pin, info['net']))
        # A net keeps its direction when it changes pin; freed pins return to
        # the unused-I/O default.
        direction = {info['net']: info['type'] for info in pins.values() if info['net'] in moving}
        for info in pins.values():
            if info['net'] in moving:
                info['net'] = None
                info['type'] = 'bidirectional'
        for net, pin in moves.items():
            pins[pin]['net'] = net
            pins[pin]['type'] = direction[net]
        BLOCK.write_text(json.dumps(block, indent=2) + '\n')
    rows = list(csv.reader(PINPLAN.open()))
    head, body = rows[0], rows[1:]
    col_pin, col_net, col_dir = head.index('Physical pin'), head.index('Net'), head.index('Direction')
    for row in body:
        info = pins[row[col_pin]]
        row[col_net] = info['net'] or 'NO_CONNECT'
        row[col_dir] = info['type']
    out = io.StringIO()
    csv.writer(out, lineterminator='\n').writerows([head] + body)
    PINPLAN.write_text(out.getvalue())
    by_net = {info['net']: pin for pin, info in pins.items() if info['net']}
    lines = [HEADER]
    for port in rtl_ports():
        pin = by_net.get(net_of(port))
        if pin is None:
            raise ValueError('RTL port %s has no U10 pin' % port)
        pull = 'DOWN' if PULL_DOWN.match(port) else 'NONE'
        lines.append('LOCATE COMP "%s" SITE "%s";\nIOBUF PORT "%s" IO_TYPE=LVCMOS33 PULLMODE=%s;\n'
                     % (port, pin, port, pull))
    LPF.write_text(''.join(lines))
    print(json.dumps({'lpf_constraints': len(lines) - 1, 'pinplan_rows': len(body)}))


if __name__ == '__main__':
    main()
