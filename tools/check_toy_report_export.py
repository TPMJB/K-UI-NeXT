#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check fixed Toy text-worker fit, marker, terminal stack and cold-only LTO."""
import argparse
import json
from pathlib import Path
import struct
from check_loader_layout import inspect_elf
from check_native_cdda_compat import config
from toy_pilot_symbols import _linked_symbols


def records(paths):
    result=[]
    for path in paths:
        for row in path.read_text().splitlines():
            name,size,kind=row.split('\t')
            if kind!='static':
                raise ValueError('Unbounded terminal frame: '+row)
            result.append((name,int(size)))
    return result


def check(directory):
    directory=Path(directory);settings=config(directory)
    profile=int(settings.get('TOY_DECISION_PROFILE','0'))
    if settings.get('TOY_TEXT_REPORT')!='1' or profile not in (1,2):
        raise ValueError('Expected an isolated Toy text decision build')
    raw=(directory/'worker.elf').read_bytes()
    image=inspect_elf(raw,0x8cfd0000,0x8cfe0000,private_p2=True,
        entry_at_base=False,entry_symbol='_kui_toy_pilot_initialize')
    if image['payload']!=(directory/'worker.bin').read_bytes():
        raise ValueError('Worker binary differs from linked ELF')
    symbols=image['symbols'];payload=image['payload']
    stage=inspect_elf((directory/'stage.elf').read_bytes(),0x8ce00000,0x8cfcffe0)
    cache_address=stage['symbols'].get('_toy_report_cache',0)
    if not cache_address or cache_address&31:
        raise ValueError('Stage metadata batch buffers lack32-byte alignment')
    address=symbols['_kui_toy_report_binding'];offset=address-0x20000000-0x8cfd0000
    prefix=struct.pack('<4I',0x54585954,0x31444e42,1,profile)
    matches=[i for i in range(len(payload)) if payload.startswith(prefix,i)]
    if matches!=[offset] or offset&31 or offset<80 or offset+272>len(payload) or any(payload[offset+16:offset+272]):
        raise ValueError('Missing unique initialized aligned zero report binding')
    typed=_linked_symbols(raw,{'_kui_toy_report_binding','_kui_toy_loader_trace_terminal_bridge'})
    at,size,kind,section=typed['_kui_toy_report_binding']
    if at!=address or size!=272 or kind!=1 or section is None or section[1]!=1 or section[2]&3!=3:
        raise ValueError('Report binding is not initialized writable worker data')
    bottom=symbols['__toy_pilot_gd_stack_bottom'];top=symbols['__toy_pilot_gd_stack_top']
    if top-bottom!=4096 or symbols['__toy_pilot_stack_top']-symbols['__toy_pilot_stack_bottom']!=8192:
        raise ValueError('Existing private stack reservations changed')
    at,size,kind,_=typed['_kui_toy_loader_trace_terminal_bridge']
    bridge=payload[at-0x8cfd0000:at-0x8cfd0000+size]
    if kind!=2 or not all(struct.pack('<I',word) in bridge for word in (bottom,top,0xa55a4aa5)):
        raise ValueError('Terminal bridge lacks its linked GD-stack guard/top')
    cold_paths=list(directory.glob('worker.elf.ltrans*.su'))
    if not cold_paths:raise ValueError('Missing final cold LTO stack records')
    cold=records(cold_paths)
    allowed={'toy_report_text.c','report_file.c','report_sci_pio.c','sd_reader.c',
             'toy_report_export.c','report_sd_storage.c'}
    if any(Path(name.split(':')[0]).name not in allowed for name,_ in cold):
        raise ValueError('An accepted AP source was recompiled through cold LTO')
    base=directory/'worker'
    def frames(names):
        return records(p for p in base.rglob('*.su') if p.stem in names)
    report=frames({'toy_loader_trace_report','minic'})
    # The writer is a nonrecursive callback closure over only these new
    # sources and minic. Count ALL frames, including mutually exclusive
    # branches. Fixed256 covers the authored bridge, libgcc and low helpers.
    writer=256+sum(n for _,n in cold+report)
    # Snapshot/quiesce/RESET execute before export's1024B automatic work
    # exists. Count all frames from the admitted private reader/model source
    # groups, even though RESET never enters their decoder or SDK paths.
    prefix_frames=frames({'toy_loader_trace_report','minic','toy_decision_observer',
        'toy_pilot_sci','sci_stream','toy_pilot_worker','toy_pilot_bus','toy_pilot',
        'retail_cursor','async_gd'})
    prefix_bound=256+sum(n for _,n in prefix_frames)
    bound=max(writer,prefix_bound)
    if bound>4096-32:raise ValueError('Guarded GD terminal stack bound exceeded')
    return {'worker_end':hex(symbols['__toy_pilot_worker_end']),
        'fixed_end':hex(0x8cfe0000),'spare_bytes':0x8cfe0000-symbols['__toy_pilot_worker_end'],
        'binding_address':hex(address),'binding_offset':offset,'binding_bytes':272,
        'stage_cache_address':hex(cache_address),'stage_cache_alignment':32,
        'existing_exports_bytes':struct.unpack_from('<I',payload,8)[0],
        'cold_sources_only':True,'gd_stack_bytes':4096,'guard_bytes':32,
        'writer_frame_bound':writer,'snapshot_quiesce_reset_frame_bound':prefix_bound,
        'terminal_frame_bound':bound,'terminal_margin':4096-32-bound,
        'console_tested':False}


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('build',type=Path)
    print(json.dumps(check(parser.parse_args().build),indent=2))
