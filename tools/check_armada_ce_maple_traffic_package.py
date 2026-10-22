#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit the isolated Armada completion-time traffic observer package."""
import argparse
import json
from pathlib import Path
from check_armada_ce_bind import check as check_binding
from check_armada_ce_maple_traffic import check as check_traffic
from check_armada_ce_excerpt import audit_cache_reader,function_sizes
from check_loader_layout import inspect_elf,region


def check(directory):
    directory=Path(directory);report=check_binding(directory)
    controller=inspect_elf((directory/'bind-probe.elf').read_bytes(),0x8c004000,0x8c007c00,
        entry_symbol='_kui_armada_ce_bind_probe')
    at=controller['symbols'].get('_kui_armada_ce_maple_probe_contract',0)-0x8c004000
    if region(controller['payload'],at,4,'traffic report contract')!=b'CMP6':
        raise ValueError('Traffic controller requires CMP6')
    report['cache_reader']=audit_cache_reader(controller,function_sizes((directory/'bind-probe.elf').read_bytes()))
    stage=inspect_elf((directory/'stage.elf').read_bytes(),0x8ce10000,0x8cfce000)
    marker=stage['symbols'].get('_kui_armada_ce_maple_hook_stage_contract',0)
    if region(stage['payload'],marker-0x8ce10000,4,'pre-NK installer contract')!=b'CMH1':
        raise ValueError('Pre-NK traffic installer contract missing')
    for name,file in [('hook','hook.bin'),('bridge','bridge.bin')]:
        start=stage['symbols'].get('__maple_'+name+'_blob_start',0)
        end=stage['symbols'].get('__maple_'+name+'_blob_end',0)
        data=(directory/file).read_bytes()
        if start%4 or end-start!=len(data) or region(stage['payload'],start-0x8ce10000,len(data),name+' blob')!=data:
            raise ValueError('Embedded '+name+' differs from audited binary')
    if stage['symbols']['__retail_stage_bss_end']>=0x8cfce000:
        raise ValueError('Stage overlaps reserved traffic range')
    report['completion_traffic_observer']=check_traffic(directory)
    report.update(purpose='source-specific completion-time ordinary request/response header observation',
        completion_entry_patched_before_nk=True,live_driver_code_patched=False,
        reserved_range_precedes_unchanged_oem_globals=True,virtual_vmu_enabled=False,
        dma_chain_followed=True,dma_payload_read=False,save_data_written=False,
        dma_idle_or_ownership_established=False)
    return report


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('directory',type=Path)
    args=parser.parse_args();print(json.dumps(check(args.directory),indent=2))

if __name__=='__main__':main()
