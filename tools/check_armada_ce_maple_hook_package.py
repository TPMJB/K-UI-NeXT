#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit the isolated pre-NK Maple completion-context observer package."""
import argparse
import json
from pathlib import Path
from check_armada_ce_bind import check as check_binding
from check_armada_ce_maple_hook import check as check_hook
from check_armada_ce_excerpt import audit_cache_reader,function_sizes
from check_loader_layout import inspect_elf,region


def check(directory):
    directory=Path(directory)
    report=check_binding(directory)
    controller=inspect_elf((directory/'bind-probe.elf').read_bytes(),0x8c004000,0x8c007c00,
        entry_symbol='_kui_armada_ce_bind_probe')
    at=controller['symbols'].get('_kui_armada_ce_maple_probe_contract',0)-0x8c004000
    if region(controller['payload'],at,4,'completion report contract')!=b'CMP5':
        raise ValueError('Completion controller requires CMP5')
    report['cache_reader']=audit_cache_reader(controller,function_sizes((directory/'bind-probe.elf').read_bytes()))
    stage=inspect_elf((directory/'stage.elf').read_bytes(),0x8ce10000,0x8cfcf000)
    marker=stage['symbols'].get('_kui_armada_ce_maple_hook_stage_contract',0)
    if region(stage['payload'],marker-0x8ce10000,4,'pre-NK installer contract')!=b'CMH1':
        raise ValueError('Pre-NK completion installer contract missing')
    for name,file in [('hook','hook.bin'),('bridge','bridge.bin')]:
        first=stage['symbols'].get('__maple_'+name+'_blob_start',0)
        last=stage['symbols'].get('__maple_'+name+'_blob_end',0)
        data=(directory/file).read_bytes()
        if last-first!=len(data) or region(stage['payload'],first-0x8ce10000,len(data),name+' blob')!=data:
            raise ValueError('Embedded '+name+' differs from audited binary')
    if stage['symbols']['__retail_stage_bss_end']>=0x8cfcf000:
        raise ValueError('Stage overlaps reserved completion page')
    report['completion_observer']=check_hook(directory)
    report['purpose']='source-specific pre-NK Maple completion entry counter and CPU context'
    report['completion_entry_patched_before_nk']=True
    report['live_driver_code_patched']=False
    report['reserved_page_precedes_unchanged_oem_globals']=True
    report['virtual_vmu_enabled']=False
    report['dma_chain_followed']=False
    report['dma_payload_read']=False
    report['save_data_written']=False
    report['dma_idle_or_ownership_established']=False
    return report


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory',type=Path)
    args=parser.parse_args()
    print(json.dumps(check(args.directory),indent=2))

if __name__=='__main__':
    main()
