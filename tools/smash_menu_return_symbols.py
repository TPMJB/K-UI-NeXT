#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bind the checked synchronous raw reader to its independent return gate."""
import argparse
from pathlib import Path
import zlib
from check_vmu_native_worker_gateway import read_elf
from smash_native_menu_resident import inspect

def require(ok,message):
    if not ok:raise ValueError(message)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('resident',type=Path);p.add_argument('gate',type=Path)
    p.add_argument('header',type=Path);p.add_argument('--raw-output',type=Path,required=True)
    p.add_argument('--code',type=Path,required=True);a=p.parse_args()
    low=inspect(a.resident.read_bytes());gate=read_elf(a.gate);s=gate[4]
    begin=s['__smash_menu_return_begin'][0];end=s['__smash_menu_return_binary_end'][0]
    bss_begin=s['__smash_menu_return_bss_begin'][0];bss_end=s['__smash_menu_return_bss_end'][0]
    require(s['_kui_smash_menu_return_entry'][0]==begin==0xac008300,'Return entry moved')
    require(begin<end<=bss_begin<=bss_end<=0xac00b800,'Return code/BSS exceed retired BOOT1')
    require((s['__smash_menu_return_stack_bottom'][0],s['__smash_menu_return_stack_top'][0])==
        (0xac00b800,0xac00c000),'Return stack bounds differ')
    runtime,n,kind,owner=s['_kui_smash_menu_return_runtime']
    video=s.get('__kui_smash_return_phase_video_transport')
    require(video is None or video==(1,0,0,0xfff1),'Invalid return video transport marker')
    runtime_bytes=420+(96 if '__kui_smash_card_session_flow' in s else 0)+(80 if video else 0)
    require(n==runtime_bytes and kind==1 and gate[3][owner][1]==1 and gate[3][owner][2]&7==3 and
        begin<=runtime<runtime+n<=end,'Return runtime ABI or bounds differ')
    code=a.code.read_bytes()
    require(len(code)==end-begin and code[runtime-begin:runtime-begin+n]==bytes(n),
        'Return immutable binary/runtime bytes differ')
    a.raw_output.write_bytes(low['raw'])
    values={'CODE_BYTES':len(code),'CODE_CRC32':zlib.crc32(code),'CODE_BSS_END':bss_end,
        'RUNTIME_P2':runtime,'RAW_CRC32':zlib.crc32(low['raw']),'STOP_P2':low['stop_p2']}
    a.header.write_text('/* Generated from checked native reader and return gate ELF. */\n'+
        ''.join(f'#define KUI_SMASH_RETURN_{key} UINT32_C(0x{value:08x})\n' for key,value in values.items()))

if __name__=='__main__':main()
