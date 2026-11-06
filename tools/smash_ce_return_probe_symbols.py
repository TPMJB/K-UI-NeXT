#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bind the terminal CE return to the actual source-sealed low reader."""
import argparse
from pathlib import Path
import zlib
from check_vmu_native_worker_gateway import read_elf
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('resident',type=Path);p.add_argument('probe',type=Path);p.add_argument('binary',type=Path);p.add_argument('header',type=Path)
a=p.parse_args();low=read_elf(a.resident);probe=read_elf(a.probe);s=low[4];h=probe[4]
begin=h['__smash_ce_return_begin'][0];end=h['__smash_ce_return_binary_end'][0]
assert h['_kui_smash_ce_return_probe_entry'][0]==begin==0xac008300
assert begin<end<=h['__smash_ce_return_bss_end'][0]<=0xac00b800
assert (h['__smash_ce_return_stack_bottom'][0],h['__smash_ce_return_stack_top'][0])==(0xac00b800,0xac00c000)
runtime,n,kind,owner=h['_kui_smash_ce_return_probe_runtime']
assert n==396 and kind==1 and probe[3][owner][1]==1 and begin<=runtime<runtime+n<=end
b=a.binary.read_bytes();assert len(b)==end-begin and b[runtime-begin:runtime-begin+n]==bytes(n)
raw,n,kind,owner=s['_kui_vc2_ce_vmu_read_card'];section=low[3][owner]
assert raw==0xac008100 and 0<n<=512 and kind==2
rb=low[0][section[4]:section[4]+section[5]];assert 0<len(rb)<=512
rb+=bytes(512-len(rb))
report,n,kind,_=s['_kui_retail_menu_return'];assert kind==2 and n and 0x8c004000<=report<report+n<=0x8c007ea0
trace,n,kind,_=s['_kui_native_ocram_trace'];assert n==148 and kind==1 and 0x8c004000<=trace<trace+n<=0x8c007ea0
v={'CODE_BYTES':len(b),'CODE_CRC32':zlib.crc32(b),'RUNTIME_P2':runtime,'RAW_CRC32':zlib.crc32(rb),
   'REPORT_P2':report+0x20000000,'TRACE_P2':trace+0x20000000}
a.header.write_text('/* Generated from the actual CE child reader and terminal return ELF. */\n'+
    ''.join(f'#define KUI_SMASH_CE_RETURN_{k} UINT32_C(0x{x:08x})\n' for k,x in v.items()))
