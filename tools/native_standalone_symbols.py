#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Emit typed low-only standalone locations from the actual linked ELF."""
import sys
from pathlib import Path
from check_vmu_native_worker_gateway import read_elf
image=read_elf(Path(sys.argv[1]));symbols=image[4]
roles={'REGION':'_standalone_reader_region','REGION_POINTER':'_kui_retail_async_region_storage',
       'EXPORTS':'_kui_native_sdk_sci_route_exports','CALLBACK':'_kui_retail_sdk_sci_entry',
       'BSS_END':'__retail_resident_bss_end'}
assert symbols['_standalone_reader_region'][1]==0x760
assert symbols['__retail_resident_bss_end'][0]<=0x8c007ba0
lines=['/* Generated from the linked standalone resident. */']
for name,symbol in roles.items():
    lines.append('#define KUI_STANDALONE_'+name+' UINT32_C(0x%08x)'%symbols[symbol][0])
Path(sys.argv[2]).write_text('\n'.join(lines)+'\n')
