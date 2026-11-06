#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bind actual low firmware capture, raw reader and separate P2 handoff."""
import argparse
from pathlib import Path
import zlib
from check_vmu_native_worker_gateway import read_elf

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('resident',type=Path);p.add_argument('handoff',type=Path);p.add_argument('header',type=Path)
p.add_argument('--raw-output',type=Path,required=True);p.add_argument('--handoff',dest='handoff_bin',type=Path,required=True)
a=p.parse_args();low=read_elf(a.resident);handoff=read_elf(a.handoff);s=low[4];h=handoff[4]
firmware,n,kind,owner=s['_kui_smash_menu_loader_firmware']
assert n==36 and kind==1 and low[3][owner][1]==8 and low[3][owner][2]&7==3
assert s['__retail_resident_bss_begin'][0]<=firmware<firmware+n<=s['__retail_resident_bss_end'][0]<=0x8c007ea0
stop,n,kind,owner=s['_kui_smash_menu_loader_stop']
assert kind==2 and 0x8c004000<=stop<stop+n<=s['__retail_resident_binary_end'][0]
entry,n,kind,owner=s['_kui_vc2_ce_vmu_read_card'];section=low[3][owner]
assert entry==0xac008100 and kind==2 and 0<n<=512 and section[3]==entry
raw=low[0][section[4]:section[4]+section[5]];assert n<=len(raw)<=512
raw+=bytes(512-len(raw));a.raw_output.write_bytes(raw)
assert h['_kui_smash_menu_loader_entry'][0]==h['__smash_menu_loader_begin'][0]==0xac008300
begin=h['__smash_menu_loader_begin'][0];end=h['__smash_menu_loader_binary_end'][0]
bss=h['__smash_menu_loader_bss_end'][0]
assert 0xac008300<end<=bss<=0xac00b800
assert (h['__smash_menu_loader_stack_bottom'][0],h['__smash_menu_loader_stack_top'][0])==(0xac00b800,0xac00c000)
runtime,n,kind,owner=h['_kui_smash_menu_loader_runtime']
assert n==(632+96 if "__kui_smash_card_session_flow" in h else 632) and kind==1 and handoff[3][owner][1]==1 and handoff[3][owner][2]&7==3
assert begin<=runtime<runtime+n<=end
blob=a.handoff_bin.read_bytes();assert len(blob)==end-begin and blob[runtime-begin:runtime-begin+n]==bytes(n)
lines=['/* Generated from actual low resident and independent P2 handoff ELF. */']
video=h.get('_kui_smash_menu_loader_phase_video')
if video is not None:
    address,n,kind,owner=video
    assert n==80 and kind==1 and handoff[3][owner][1]==8 and handoff[3][owner][2]&7==3
    assert h['__smash_menu_loader_bss_begin'][0]<=address<address+n<=bss
    lines.append(f'#define KUI_SMASH_MENU_PHASE_VIDEO_P2 UINT32_C(0x{address:08x})')
for key,value in {'FIRMWARE_P2':firmware+0x20000000,'STOP_P2':stop+0x20000000,'RAW_CRC32':zlib.crc32(raw),'HANDOFF_CRC32':zlib.crc32(blob),
    'HANDOFF_BYTES':len(blob),'HANDOFF_END':end,'HANDOFF_BSS_END':bss,'RUNTIME_P2':runtime}.items():
    lines.append(f'#define KUI_SMASH_MENU_{key} UINT32_C(0x{value:08x})')
a.header.write_text('\n'.join(lines)+'\n')
