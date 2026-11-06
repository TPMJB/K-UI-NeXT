#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bind the late loader's retired boot scalars and small P2 bootstrap reader."""
import argparse
from pathlib import Path
import zlib
from check_vmu_native_worker_gateway import read_elf
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('elf',type=Path);p.add_argument('header',type=Path)
p.add_argument('--raw-output',type=Path,required=True)
a=p.parse_args();image=read_elf(a.elf);s=image[4]
address,size,*_=s['_manifest']
assert size==828 and 0x8c004000<=address<address+size<=0x8c007ea0
entry,length,kind,index=s['_kui_vc2_ce_vmu_read_card'];row=image[3][index]
assert entry==0xac008100 and kind==2 and 0<length<=512
blob=image[0][row[4]:row[4]+row[5]]
assert row[3]==entry and len(blob)<=512 and len(blob)>=length
blob+=bytes(512-len(blob));a.raw_output.write_bytes(blob)
a.header.write_text('/* Generated from actual resident ELF. */\n'
 f'#define KUI_VC2_LATE_MAP_P2 0x{address+0x20000000:08x}u\n'
 f'#define KUI_VC2_LATE_RAW_CRC32 0x{zlib.crc32(blob):08x}u\n')
