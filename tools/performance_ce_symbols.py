#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Export only the protected CE demand collector's typed high entry.

The low reader imports no other high-controller symbols or mutable addresses.
"""
import argparse
from pathlib import Path
from check_vmu_native_worker_gateway import read_elf, symbol_bytes


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('elf', type=Path)
    p.add_argument('output', type=Path)
    args = p.parse_args()
    image = read_elf(args.elf)
    address, body, section = symbol_bytes(image, '_kui_armada_ce_demand_event', 2)
    if not body or section[2] & 7 != 6 or address & 1 or \
       not 0x8cfa7000 <= address < address + len(body) <= 0x8cfbf000:
        raise ValueError('CE demand collector is outside its admitted executable high owner')
    args.output.write_text('_kui_armada_ce_demand_event = 0x%08x;\n' % address)


if __name__ == '__main__':
    main()
