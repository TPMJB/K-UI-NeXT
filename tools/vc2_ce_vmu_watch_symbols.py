#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Publish actual low DBR entry and terminal-map scalar to the high installer."""
import sys
from pathlib import Path
from check_vmu_native_worker_gateway import read_elf

image=read_elf(Path(sys.argv[1]));symbols=image[4]
entry,size,kind,_=symbols['_kui_vc2_ce_vmu_watch_entry']
manifest=symbols['_manifest'][0]
assert 0x8c004000<=entry<entry+size<=symbols['__native_dma_cold_begin'][0]
assert kind==2 and symbols['_manifest'][1]==828
Path(sys.argv[2]).write_text('/* Generated from actual native ELF. */\n'
    f'#define KUI_VC2_WATCH_ENTRY 0x{entry+0x20000000:08x}u\n'
    f'#define KUI_VC2_WATCH_DBR_SAVE 0x{manifest+64+0x20000000:08x}u\n')
