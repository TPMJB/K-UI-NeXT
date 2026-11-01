#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Publish only the exact linked compact worker's durable-context offset."""
from pathlib import Path
import sys
import subprocess

def main():
    if len(sys.argv)!=3:
        raise SystemExit('usage: disc_vmu_worker_symbols.py worker.elf header.h')
    rows=[line.split() for line in subprocess.check_output(
        ['sh-elf-nm','-S','--defined-only',sys.argv[1]],text=True).splitlines()]
    matches=[row for row in rows if len(row)==4 and row[3]=='_durable_card']
    if len(matches)!=1:
        raise SystemExit('requires exactly one linked durable_card object')
    symbol=matches[0];address=int(symbol[0],16);size=int(symbol[1],16)
    offset=address-0xac100000
    if symbol[2] not in ('b','B') or offset<80 or offset>=0x11000 or size<=0 or size>0x11000-offset:
        raise SystemExit('durable context does not fit compact worker data before its private stack')
    # read_elf omits NOLOAD bytes from its section data; the symbol bound and
    # independently checked worker link map bind the retained BSS allocation.
    content=('#ifndef KUI_DISC_VMU_WORKER_SYMBOLS_H\n#define KUI_DISC_VMU_WORKER_SYMBOLS_H\n'
             f'#define KUI_DISC_VMU_DURABLE_CONTEXT_OFFSET 0x{offset:x}u\n'
             f'#define KUI_DISC_VMU_DURABLE_CONTEXT_BYTES {size}u\n#endif\n')
    Path(sys.argv[2]).write_text(content)
    print(f'Physical-disc worker durable context: offset0x{offset:x}, {size} bytes')

if __name__=='__main__':
    main()
