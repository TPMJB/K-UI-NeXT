#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Validate the native-menu reader's one separate retired-BOOT1 raw island."""
from check_loader_layout import EH,PH,SH,inspect_elf,region
from toy_pilot_symbols import _linked_symbols

MARKER='__kui_smash_native_menu_resident'

def inspect(data):
    h=EH.unpack(region(data,0,EH.size,'native menu resident ELF header'))
    typed=_linked_symbols(data,{MARKER,'_kui_smash_menu_return_read_card','_kui_smash_menu_return_stop'})
    if typed.get(MARKER)!=(1,0,0,None):
        raise ValueError('Native menu reader requires its exact profile marker')
    entry,count,kind,raw=typed.get('_kui_smash_menu_return_read_card',(0,0,0,None))
    if (raw is None or entry!=0xac008100 or kind!=2 or not 0<count<=raw[5]<=512 or
            raw[1]!=1 or raw[2]!=6 or raw[3]!=entry or raw[5]&3):
        raise ValueError('Native menu raw reader must own its bounded fixed P2 code section')
    segments=[PH.unpack(region(data,h[5]+i*PH.size,PH.size,'native menu resident segment'))
              for i in range(h[10])]
    sections=[SH.unpack(region(data,h[6]+i*SH.size,SH.size,'native menu resident section'))
              for i in range(h[12])]
    loads=[(i,p) for i,p in enumerate(segments) if p[0]==1]
    raw_load=[(i,p) for i,p in loads if p[2]==entry]
    if len(loads)!=2 or len(raw_load)!=1:
        raise ValueError('Native menu resident requires exactly its low and raw load segments')
    index,segment=raw_load[0]
    if segment[1:7]!=(raw[4],entry,0x8c008100,raw[5],raw[5],5):
        raise ValueError('Native menu raw physical load or permissions differ')
    blob=region(data,raw[4],raw[5],'native menu raw reader')
    filtered=bytearray(data)
    PH.pack_into(filtered,h[5]+index*PH.size,0,*segment[1:])
    section=list(raw);section[2]=0
    SH.pack_into(filtered,h[6]+sections.index(raw)*SH.size,*section)
    low=inspect_elf(bytes(filtered),0x8c004000,0x8c007800)
    stop,n,kind,section=typed.get('_kui_smash_menu_return_stop',(0,0,0,None))
    if (kind!=2 or not n or section is None or section[1]!=1 or section[2]&7!=6 or
            not 0x8c004000<=stop<stop+n<=0x8c004000+len(low['payload'])):
        raise ValueError('Native menu stop callback must remain bounded low code')
    return {'low':low,'raw':blob+bytes(512-len(blob)),'raw_code_bytes':len(blob),
            'stop_p2':stop+0x20000000}
