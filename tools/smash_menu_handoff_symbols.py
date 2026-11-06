#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Publish the separately linked P2 redirect without a resident link cycle."""
import sys
from pathlib import Path
from check_vmu_native_worker_gateway import read_elf
image=read_elf(Path(sys.argv[1]));s=image[4]
entry,n,kind,owner=s['_kui_smash_menu_loader_redirect']
assert kind==2 and n and image[3][owner][2]&7==6
assert 0xac008300<=entry<entry+n<=s['__smash_menu_loader_binary_end'][0]<=0xac00b800
Path(sys.argv[2]).write_text('/* Generated from the separately linked P2 handoff ELF. */\n'
    f'#define KUI_SMASH_MENU_LOADER_REDIRECT_P2 UINT32_C(0x{entry:08x})\n')
